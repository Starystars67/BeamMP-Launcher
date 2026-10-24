/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Games/RelayLink.h"

#include "Net/Sockets.h"
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

namespace Relay {

namespace {
    constexpr int ConnectTimeoutMs = 5000;
    constexpr int SendTimeoutMs = 5000;
    constexpr int PollMs = 250;
    constexpr auto PingEvery = std::chrono::seconds(10);
    constexpr auto SilentFor = std::chrono::seconds(40);

    using Clock = std::chrono::steady_clock;

    // Everything the reader thread uses lives here, so the link can go away while the thread winds down.
    struct State {
        Net::Native socket = Net::Invalid;
        EventHandler handler;
        std::mutex sendMutex;
        std::atomic<bool> stopping = false;
        std::atomic<bool> closedReported = false;

        ~State() {
            if (socket != Net::Invalid) {
                Net::CloseNative(socket);
            }
        }

        bool Send(const std::string& frame) {
            std::lock_guard<std::mutex> lock(sendMutex);
            if (stopping) {
                return false;
            }
            if (!Net::SendAll(socket, frame.data(), frame.size())) {
                Stop();
                return false;
            }
            return true;
        }

        void Stop() {
            if (!stopping.exchange(true)) {
                Net::ShutdownNative(socket);
            }
        }

        void Report(Event event) {
            if (event.kind == Event::Kind::Closed && closedReported.exchange(true)) {
                return;
            }
            if (handler) {
                handler(event);
            }
        }
    };

    Event::Kind KindOf(uint8_t type) {
        switch (type) {
        case FromRelay::Welcome:
            return Event::Kind::Welcome;
        case FromRelay::RoomCreated:
            return Event::Kind::RoomCreated;
        case FromRelay::RoomJoined:
            return Event::Kind::RoomJoined;
        case FromRelay::PeerJoined:
            return Event::Kind::PeerJoined;
        case FromRelay::PeerLeft:
            return Event::Kind::PeerLeft;
        case FromRelay::RoomClosed:
            return Event::Kind::RoomClosed;
        case FromRelay::Error:
            return Event::Kind::Error;
        default:
            return Event::Kind::Closed;
        }
    }

    void ReadLoop(std::shared_ptr<State> state) {
        FrameDecoder decoder;
        auto lastPing = Clock::now();
        auto lastHeard = Clock::now();
        std::string reason = "the connection ended";
        char chunk[16384];
        while (!state->stopping) {
            auto now = Clock::now();
            if (now - lastPing > PingEvery) {
                state->Send(EncodeFrame(ToRelay::Ping));
                lastPing = now;
            }
            if (now - lastHeard > SilentFor) {
                reason = "the relay stopped answering";
                break;
            }
            if (!Net::WaitReadable(state->socket, PollMs)) {
                continue;
            }
            auto received = recv(state->socket, chunk, sizeof(chunk), 0);
            if (received <= 0) {
                break;
            }
            lastHeard = Clock::now();
            std::vector<Frame> frames;
            try {
                frames = decoder.Push(chunk, static_cast<size_t>(received));
            } catch (const std::exception&) {
                reason = "the relay sent something that is not the protocol";
                break;
            }
            for (const Frame& frame : frames) {
                if (frame.type == FromRelay::Pong) {
                    continue;
                }
                Event event;
                if (frame.type == FromRelay::Data) {
                    auto data = DecodeData(frame.payload);
                    if (!data) {
                        continue;
                    }
                    event.kind = Event::Kind::Data;
                    event.data = std::move(*data);
                } else {
                    auto json = DecodeJsonObject(frame.payload);
                    Event::Kind kind = KindOf(frame.type);
                    if (!json || kind == Event::Kind::Closed) {
                        continue; // not a message this client knows
                    }
                    event.kind = kind;
                    event.json = std::move(*json);
                }
                state->Report(std::move(event));
                if (state->stopping) {
                    break;
                }
            }
        }
        state->Stop();
        Event closed;
        closed.kind = Event::Kind::Closed;
        closed.json = { { "reason", reason } };
        state->Report(std::move(closed));
    }

    class TcpLink : public Link {
    public:
        explicit TcpLink(std::shared_ptr<State> state)
            : m_state(std::move(state)) { }

        ~TcpLink() override {
            m_state->Stop();
            if (m_reader.joinable()) {
                // a handler that lets go of its own link must not wait for itself
                if (m_reader.get_id() == std::this_thread::get_id()) {
                    m_reader.detach();
                } else {
                    m_reader.join();
                }
            }
        }

        void Start() { m_reader = std::thread(ReadLoop, m_state); }

        void CreateRoom(int maxPlayers, const std::string& password) override {
            nlohmann::json request = nlohmann::json::object();
            if (maxPlayers > 0) {
                request["max_players"] = maxPlayers;
            }
            if (!password.empty()) {
                request["password"] = password;
            }
            m_state->Send(EncodeJson(ToRelay::CreateRoom, request));
        }

        void JoinRoom(const std::string& code, const std::string& password) override {
            nlohmann::json request = { { "code", code } };
            if (!password.empty()) {
                request["password"] = password;
            }
            m_state->Send(EncodeJson(ToRelay::JoinRoom, request));
        }

        void LeaveRoom() override { m_state->Send(EncodeFrame(ToRelay::LeaveRoom)); }

        bool SendData(DataKind kind, uint16_t target, const std::string& bytes) override {
            if (bytes.size() + 4 > MaxFrameBytes) {
                return false;
            }
            return m_state->Send(EncodeData(kind, target, bytes));
        }

        void Close() override { m_state->Stop(); }

    private:
        std::shared_ptr<State> m_state;
        std::thread m_reader;
    };
}

bool IsLocalHost(const std::string& host) {
    if (host == "localhost" || host == "::1" || host.rfind("127.", 0) == 0) {
        return true;
    }
    unsigned a = 0, b = 0, c = 0, d = 0;
    if (sscanf(host.c_str(), "%u.%u.%u.%u", &a, &b, &c, &d) != 4 || a > 255 || b > 255 || c > 255 || d > 255) {
        return false;
    }
    return a == 10 || (a == 172 && b >= 16 && b <= 31) || (a == 192 && b == 168);
}

std::unique_ptr<Link> ConnectTcpLink(const Endpoint& endpoint, const Hello& hello, EventHandler handler, std::string& error) {
    Net::Native socket = Net::ConnectTcp(endpoint.host, endpoint.port, ConnectTimeoutMs, error);
    if (socket == Net::Invalid) {
        return nullptr;
    }
    Net::ConfigureClient(socket, SendTimeoutMs);
    auto state = std::make_shared<State>();
    state->socket = socket;
    state->handler = std::move(handler);
    auto link = std::make_unique<TcpLink>(state);
    nlohmann::json helloMessage = { { "v", ProtocolVersion }, { "name", hello.name }, { "game", hello.game } };
    if (!hello.client.empty()) {
        helloMessage["client"] = hello.client;
    }
    if (!hello.token.empty()) {
        helloMessage["token"] = hello.token;
    }
    if (!state->Send(EncodeJson(ToRelay::Hello, helloMessage))) {
        error = "could not talk to the relay";
        return nullptr;
    }
    link->Start();
    return link;
}

}
