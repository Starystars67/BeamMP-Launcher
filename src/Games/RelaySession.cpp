/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Games/RelaySession.h"

#include <algorithm>

namespace Games {

namespace {
    constexpr size_t MaxNameBytes = 32;
    constexpr size_t CodeLength = 6;
    const std::string CodeAlphabet = "ABCDEFGHJKMNPQRSTUVWXYZ23456789";

    // Control characters out, at most 32 bytes without cutting a character in half.
    std::string CleanName(std::string name) {
        std::string clean;
        for (unsigned char c : name) {
            if (c >= 0x20 && c != 0x7f) {
                clean.push_back(static_cast<char>(c));
            }
        }
        while (!clean.empty() && clean.front() == ' ') {
            clean.erase(clean.begin());
        }
        while (!clean.empty() && clean.back() == ' ') {
            clean.pop_back();
        }
        if (clean.size() > MaxNameBytes) {
            clean.resize(MaxNameBytes);
            while (!clean.empty() && (static_cast<unsigned char>(clean.back()) & 0xC0) == 0x80) {
                clean.pop_back(); // inside a multi-byte character
            }
            if (!clean.empty() && static_cast<unsigned char>(clean.back()) >= 0xC0) {
                clean.pop_back(); // its first byte without the rest
            }
        }
        return clean.empty() ? "Player" : clean;
    }

    bool ValidCode(const std::string& code) {
        return code.size() == CodeLength && std::all_of(code.begin(), code.end(), [](char c) { return CodeAlphabet.find(c) != std::string::npos; });
    }

    std::vector<SessionPeer> PeersOf(const nlohmann::json& list) {
        std::vector<SessionPeer> peers;
        if (!list.is_array()) {
            return peers;
        }
        for (const auto& entry : list) {
            if (entry.is_object() && entry.value("id", -1) > 0 && entry.contains("name") && entry["name"].is_string()) {
                peers.push_back({ static_cast<uint16_t>(entry["id"].get<int>()), entry["name"].get<std::string>() });
            }
        }
        return peers;
    }
}

const char* ToString(SessionInfo::State state) {
    switch (state) {
    case SessionInfo::State::Idle:
        return "idle";
    case SessionInfo::State::Connecting:
        return "connecting";
    case SessionInfo::State::InRoom:
        return "in_room";
    }
    return "idle";
}

nlohmann::json ToJson(const SessionInfo& info) {
    nlohmann::json json = { { "game", info.game }, { "state", ToString(info.state) } };
    if (info.state == SessionInfo::State::InRoom) {
        json["role"] = info.role;
        json["code"] = info.code;
        json["self_id"] = info.selfId;
        json["host_id"] = info.hostId;
        json["peers"] = nlohmann::json::array();
        for (const SessionPeer& peer : info.peers) {
            json["peers"].push_back({ { "id", peer.id }, { "name", peer.name } });
        }
    }
    if (!info.errorCode.empty()) {
        json["error"] = { { "code", info.errorCode }, { "message", info.errorMessage } };
    }
    return json;
}

RelaySession::RelaySession(Options options, Relay::LinkFactory factory)
    : m_options(std::move(options))
    , m_factory(std::move(factory)) { }

RelaySession::~RelaySession() {
    std::shared_ptr<Relay::Link> link;
    std::vector<std::thread> workers;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        ++m_generation; // nothing that is still running changes anything any more
        link = std::move(m_link);
        workers = std::move(m_workers);
    }
    if (link) {
        link->Close();
    }
    for (auto& worker : workers) {
        worker.join();
    }
}

void RelaySession::AddObserver(Observer observer) {
    m_observers.push_back(std::move(observer));
}

void RelaySession::SetDataHandler(DataHandler handler) {
    m_dataHandler = std::move(handler);
}

bool RelaySession::Available() const {
    return UnavailableReason().empty();
}

std::string RelaySession::UnavailableReason() const {
    if (m_options.relay.host.empty() || m_options.relay.port == 0) {
        return "no relay is set up";
    }
    if (!m_options.allowInsecureRemote && !Relay::IsLocalHost(m_options.relay.host)) {
        return "talking to a relay over the internet needs TLS, which the launcher does not have yet";
    }
    return "";
}

SessionInfo RelaySession::Info() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_info;
}

SessionStart RelaySession::Create(const std::string& game, int maxPlayers, const std::string& password) {
    Request request;
    request.create = true;
    request.maxPlayers = maxPlayers;
    request.password = password;
    return Begin(game, std::move(request));
}

SessionStart RelaySession::Join(const std::string& game, const std::string& code, const std::string& password) {
    if (!ValidCode(code)) {
        return SessionStart::BadCode;
    }
    Request request;
    request.code = code;
    request.password = password;
    return Begin(game, std::move(request));
}

SessionStart RelaySession::Begin(const std::string& game, Request request) {
    if (!Available()) {
        return SessionStart::Unavailable;
    }
    SessionInfo started;
    uint64_t generation;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_info.state != SessionInfo::State::Idle) {
            return SessionStart::Busy;
        }
        generation = ++m_generation;
        m_info = {};
        m_info.game = game;
        m_info.state = SessionInfo::State::Connecting;
        started = m_info;
        m_pending = request;
        m_requestSent = false;
        m_workers.emplace_back(&RelaySession::Run, this, generation, game, request);
    }
    Notify(started);
    return SessionStart::Started;
}

// Connecting can take seconds, so it is not done on the caller's thread.
void RelaySession::Run(uint64_t generation, std::string game, Request) {
    std::string error;
    Relay::Hello hello { CleanName(m_options.playerName ? m_options.playerName() : ""), game, m_options.clientName, "" };
    auto link = m_factory(
        m_options.relay, hello, [this, generation](const Relay::Event& event) { OnEvent(generation, event); }, error);
    if (!link) {
        Finish(generation, "relay_unreachable", error.empty() ? "could not reach the relay" : error);
        return;
    }
    std::shared_ptr<Relay::Link> shared(std::move(link));
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (generation != m_generation) {
            shared->Close(); // left in the meantime
            return;
        }
        m_link = shared;
    }
    // The welcome may have come already (see LinkFactory): the request goes out once both are there.
    SendRequestIfReady(generation);
}

void RelaySession::SendRequestIfReady(uint64_t generation) {
    std::shared_ptr<Relay::Link> link;
    Request request;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (generation != m_generation || !m_link || !m_welcomed || m_requestSent || m_info.state != SessionInfo::State::Connecting) {
            return;
        }
        m_requestSent = true;
        link = m_link;
        request = m_pending;
    }
    if (request.create) {
        link->CreateRoom(request.maxPlayers, request.password);
    } else {
        link->JoinRoom(request.code, request.password);
    }
}

void RelaySession::OnEvent(uint64_t generation, const Relay::Event& event) {
    using Kind = Relay::Event::Kind;
    SessionInfo changed;
    bool notify = false;
    std::string closeCode;
    std::string closeMessage;
    bool closed = false;
    bool dataForHandler = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (generation != m_generation) {
            return;
        }
        switch (event.kind) {
        case Kind::Welcome:
            m_welcomed = true;
            break;
        case Kind::RoomCreated:
            m_info.state = SessionInfo::State::InRoom;
            m_info.role = "host";
            m_info.code = event.json.value("code", "");
            m_info.selfId = static_cast<uint16_t>(event.json.value("peer_id", 1));
            m_info.hostId = m_info.selfId;
            m_info.peers = { { m_info.selfId, CleanName(m_options.playerName ? m_options.playerName() : "") } };
            changed = m_info;
            notify = true;
            break;
        case Kind::RoomJoined:
            m_info.state = SessionInfo::State::InRoom;
            m_info.role = "guest";
            m_info.code = event.json.value("code", "");
            m_info.selfId = static_cast<uint16_t>(event.json.value("peer_id", 0));
            m_info.hostId = static_cast<uint16_t>(event.json.value("host_id", 1));
            m_info.peers = PeersOf(event.json.value("peers", nlohmann::json::array()));
            m_info.peers.push_back({ m_info.selfId, CleanName(m_options.playerName ? m_options.playerName() : "") });
            std::sort(m_info.peers.begin(), m_info.peers.end(), [&](const SessionPeer& a, const SessionPeer& b) {
                return a.id == m_info.hostId ? b.id != m_info.hostId : (b.id == m_info.hostId ? false : a.id < b.id);
            });
            changed = m_info;
            notify = true;
            break;
        case Kind::PeerJoined: {
            SessionPeer peer { static_cast<uint16_t>(event.json.value("id", 0)), event.json.value("name", std::string()) };
            if (m_info.state == SessionInfo::State::InRoom && peer.id != 0
                && std::none_of(m_info.peers.begin(), m_info.peers.end(), [&](const SessionPeer& p) { return p.id == peer.id; })) {
                m_info.peers.push_back(peer);
                changed = m_info;
                notify = true;
            }
            break;
        }
        case Kind::PeerLeft: {
            uint16_t id = static_cast<uint16_t>(event.json.value("id", 0));
            auto before = m_info.peers.size();
            m_info.peers.erase(std::remove_if(m_info.peers.begin(), m_info.peers.end(), [&](const SessionPeer& p) { return p.id == id; }), m_info.peers.end());
            if (m_info.peers.size() != before) {
                changed = m_info;
                notify = true;
            }
            break;
        }
        case Kind::RoomClosed:
            closed = true;
            closeCode = "room_closed";
            closeMessage = event.json.value("reason", "") == "host_left" ? "The host left the room" : "The host is gone";
            break;
        case Kind::Error:
            // while setting up it ends the attempt, afterwards the relay only tells about a refused request
            if (m_info.state != SessionInfo::State::InRoom) {
                closed = true;
                closeCode = event.json.value("code", "relay_error");
                closeMessage = event.json.value("message", "the relay refused");
            }
            break;
        case Kind::Closed:
            closed = m_info.state != SessionInfo::State::Idle;
            closeCode = "connection_lost";
            closeMessage = "Lost the connection to the relay: " + event.json.value("reason", std::string("it ended"));
            break;
        case Kind::Data:
            dataForHandler = m_info.state == SessionInfo::State::InRoom;
            break;
        }
    }
    if (dataForHandler && m_dataHandler) {
        m_dataHandler(event.data);
    }
    if (event.kind == Kind::Welcome) {
        SendRequestIfReady(generation);
    }
    if (notify) {
        Notify(changed);
    }
    if (closed) {
        Finish(generation, closeCode, closeMessage);
    }
}

void RelaySession::Finish(uint64_t generation, const std::string& errorCode, const std::string& errorMessage) {
    SessionInfo idle;
    std::shared_ptr<Relay::Link> link;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (generation != m_generation || m_info.state == SessionInfo::State::Idle) {
            return;
        }
        ++m_generation; // whatever else the old connection says is not ours any more
        link = std::move(m_link);
        m_welcomed = false;
        m_info = {};
        m_info.errorCode = errorCode;
        m_info.errorMessage = errorMessage;
        idle = m_info;
    }
    if (link) {
        link->Close();
    }
    Notify(idle);
}

void RelaySession::Leave() {
    std::shared_ptr<Relay::Link> link;
    SessionInfo idle;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_info.state == SessionInfo::State::Idle) {
            return;
        }
        ++m_generation;
        link = std::move(m_link);
        m_welcomed = false;
        m_info = {};
        idle = m_info;
    }
    if (link) {
        link->LeaveRoom();
        link->Close();
    }
    Notify(idle);
}

bool RelaySession::SendData(Relay::DataKind kind, uint16_t target, const std::string& bytes) {
    std::shared_ptr<Relay::Link> link;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_info.state != SessionInfo::State::InRoom) {
            return false;
        }
        link = m_link;
    }
    return link && link->SendData(kind, target, bytes);
}

void RelaySession::Notify(const SessionInfo& info) {
    for (const Observer& observer : m_observers) {
        observer(info);
    }
}

}
