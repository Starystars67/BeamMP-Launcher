/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Games/RelaySession.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <doctest/doctest.h>
#include <future>
#include <mutex>
#include <thread>

using namespace Games;
using namespace Relay;
using nlohmann::json;

namespace {
template <typename Condition>
bool Eventually(Condition condition, int timeoutMs = 3000) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (!condition()) {
        if (std::chrono::steady_clock::now() > deadline) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return true;
}

// The relay as far as the session can tell: it records what it is asked and says what the test makes it say.
struct FakeLink : Link {
    std::mutex mutex;
    std::vector<std::string> calls;
    std::vector<std::string> sentBytes;
    bool open = true;
    bool sendWorks = true;

    void Note(std::string call) {
        std::lock_guard<std::mutex> lock(mutex);
        calls.push_back(std::move(call));
    }
    void CreateRoom(int maxPlayers, const std::string& password) override {
        Note("create " + std::to_string(maxPlayers) + " [" + password + "]");
    }
    void JoinRoom(const std::string& code, const std::string& password) override { Note("join " + code + " [" + password + "]"); }
    void LeaveRoom() override { Note("leave"); }
    bool SendData(DataKind kind, uint16_t target, const std::string& bytes) override {
        Note("data " + std::to_string(static_cast<int>(kind)) + " " + std::to_string(target));
        std::lock_guard<std::mutex> lock(mutex);
        sentBytes.push_back(bytes);
        return sendWorks;
    }
    void Close() override {
        Note("close");
        std::lock_guard<std::mutex> lock(mutex);
        open = false;
    }
    std::vector<std::string> Calls() {
        std::lock_guard<std::mutex> lock(mutex);
        return calls;
    }
    int Count(const std::string& prefix) {
        int count = 0;
        for (const auto& call : Calls()) {
            count += call.rfind(prefix, 0) == 0 ? 1 : 0;
        }
        return count;
    }
};

// A link that stays owned by the test (the session gets a view of it).
struct LinkView : Link {
    std::shared_ptr<FakeLink> link;
    explicit LinkView(std::shared_ptr<FakeLink> link)
        : link(std::move(link)) { }
    void CreateRoom(int maxPlayers, const std::string& password) override { link->CreateRoom(maxPlayers, password); }
    void JoinRoom(const std::string& code, const std::string& password) override { link->JoinRoom(code, password); }
    void LeaveRoom() override { link->LeaveRoom(); }
    bool SendData(DataKind kind, uint16_t target, const std::string& bytes) override { return link->SendData(kind, target, bytes); }
    void Close() override { link->Close(); }
};

struct Fixture {
    std::shared_ptr<FakeLink> link = std::make_shared<FakeLink>();
    std::mutex mutex;
    EventHandler handler;
    Hello lastHello;
    Endpoint lastEndpoint;
    int factoryCalls = 0;
    bool factoryFails = false;
    bool welcomeInsideFactory = false;
    std::shared_future<void> hold; // when set, the factory waits for it
    std::vector<SessionInfo> infos;
    std::vector<DataFrame> data;
    std::string name = "Ann";
    std::unique_ptr<RelaySession> session;

    explicit Fixture(Endpoint endpoint = { "127.0.0.1", 7777 }, bool allowInsecureRemote = false) {
        RelaySession::Options options;
        options.relay = endpoint;
        options.playerName = [this] { return name; };
        options.clientName = "tests";
        options.allowInsecureRemote = allowInsecureRemote;
        session = std::make_unique<RelaySession>(options, [this](const Endpoint& e, const Hello& h, EventHandler eventHandler, std::string& error) -> std::unique_ptr<Link> {
            {
                std::lock_guard<std::mutex> lock(mutex);
                ++factoryCalls;
                lastHello = h;
                lastEndpoint = e;
                handler = eventHandler;
            }
            if (hold.valid()) {
                hold.wait();
            }
            if (factoryFails) {
                error = "connection refused";
                return nullptr;
            }
            if (welcomeInsideFactory) {
                Emit(Event::Kind::Welcome, { { "v", 1 } });
            }
            return std::make_unique<LinkView>(link);
        });
        session->AddObserver([this](const SessionInfo& info) {
            std::lock_guard<std::mutex> lock(mutex);
            infos.push_back(info);
        });
        session->SetDataHandler([this](const DataFrame& frame) {
            std::lock_guard<std::mutex> lock(mutex);
            data.push_back(frame);
        });
    }

    void Emit(Event::Kind kind, json body = json::object()) {
        EventHandler current;
        {
            std::lock_guard<std::mutex> lock(mutex);
            current = handler;
        }
        Event event;
        event.kind = kind;
        event.json = std::move(body);
        current(event);
    }

    void EmitData(uint16_t sender, DataKind kind, std::string bytes) {
        EventHandler current;
        {
            std::lock_guard<std::mutex> lock(mutex);
            current = handler;
        }
        Event event;
        event.kind = Event::Kind::Data;
        event.data = { sender, kind, std::move(bytes) };
        current(event);
    }

    bool WaitState(SessionInfo::State state) {
        return Eventually([&] { return session->Info().state == state; });
    }
    bool WaitFactory() {
        return Eventually([&] {
            std::lock_guard<std::mutex> lock(mutex);
            return factoryCalls > 0 && handler != nullptr;
        });
    }
    std::vector<SessionInfo> Infos() {
        std::lock_guard<std::mutex> lock(mutex);
        return infos;
    }

    // the session is a host in a room with code ABC234
    void BecomeHost() {
        REQUIRE(session->Create("gearblocks", 4, "pw") == SessionStart::Started);
        REQUIRE(WaitFactory());
        Emit(Event::Kind::Welcome, { { "v", 1 } });
        REQUIRE(Eventually([&] { return link->Count("create") == 1; }));
        Emit(Event::Kind::RoomCreated, { { "code", "ABC234" }, { "peer_id", 1 } });
        REQUIRE(WaitState(SessionInfo::State::InRoom));
    }
};
}

TEST_CASE("without a relay there is no session") {
    Fixture f({ "", 0 });
    CHECK_FALSE(f.session->Available());
    CHECK(f.session->UnavailableReason().find("no relay") != std::string::npos);
    CHECK(f.session->Create("gearblocks", 0, "") == SessionStart::Unavailable);
    CHECK(f.session->Join("gearblocks", "ABC234", "") == SessionStart::Unavailable);
    CHECK(f.factoryCalls == 0);
}

TEST_CASE("a relay over the internet needs TLS, which is not there yet, unless that is waived") {
    Fixture remote({ "relay.beammp.com", 7777 });
    CHECK_FALSE(remote.session->Available());
    CHECK(remote.session->UnavailableReason().find("TLS") != std::string::npos);
    CHECK(remote.session->Create("gearblocks", 0, "") == SessionStart::Unavailable);

    Fixture waived({ "relay.beammp.com", 7777 }, true);
    CHECK(waived.session->Available());
    Fixture local({ "192.168.1.5", 7777 });
    CHECK(local.session->Available());
}

TEST_CASE("creating a room: connect, say who you are, ask for the room, be its host") {
    Fixture f;
    CHECK(f.session->Create("gearblocks", 4, "pw") == SessionStart::Started);
    REQUIRE(f.WaitFactory());
    CHECK(f.session->Info().state == SessionInfo::State::Connecting);
    CHECK(f.lastHello.name == "Ann");
    CHECK(f.lastHello.game == "gearblocks");
    CHECK(f.lastHello.client == "tests");
    CHECK(f.lastEndpoint.host == "127.0.0.1");
    CHECK(f.lastEndpoint.port == 7777);
    // nothing is asked before the relay has welcomed us
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK(f.link->Count("create") == 0);

    f.Emit(Event::Kind::Welcome, { { "v", 1 } });
    REQUIRE(Eventually([&] { return f.link->Count("create") == 1; }));
    CHECK(f.link->Calls().back() == "create 4 [pw]");
    f.Emit(Event::Kind::RoomCreated, { { "code", "ABC234" }, { "peer_id", 1 } });
    REQUIRE(f.WaitState(SessionInfo::State::InRoom));

    SessionInfo info = f.session->Info();
    CHECK(info.game == "gearblocks");
    CHECK(info.role == "host");
    CHECK(info.code == "ABC234");
    CHECK(info.selfId == 1);
    CHECK(info.hostId == 1);
    REQUIRE(info.peers.size() == 1);
    CHECK(info.peers[0].name == "Ann");
    CHECK(info.errorCode.empty());

    auto infos = f.Infos();
    REQUIRE(infos.size() == 2);
    CHECK(infos[0].state == SessionInfo::State::Connecting);
    CHECK(infos[1].state == SessionInfo::State::InRoom);
}

TEST_CASE("the welcome may come before the connection is handed over, the request still goes out once") {
    Fixture f;
    f.welcomeInsideFactory = true;
    REQUIRE(f.session->Create("gearblocks", 0, "") == SessionStart::Started);
    REQUIRE(Eventually([&] { return f.link->Count("create") == 1; }));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK(f.link->Count("create") == 1);
    CHECK(f.link->Calls().back() == "create 0 []");
}

TEST_CASE("joining a room, the others are listed with the host first, yourself in place") {
    Fixture f;
    CHECK(f.session->Join("gearblocks", "abc234", "") == SessionStart::BadCode);
    CHECK(f.session->Join("gearblocks", "ABC", "") == SessionStart::BadCode);
    CHECK(f.session->Join("gearblocks", "ABCO34", "") == SessionStart::BadCode);
    CHECK(f.session->Info().state == SessionInfo::State::Idle);

    REQUIRE(f.session->Join("gearblocks", "ABC234", "pw") == SessionStart::Started);
    REQUIRE(f.WaitFactory());
    f.Emit(Event::Kind::Welcome, { { "v", 1 } });
    REQUIRE(Eventually([&] { return f.link->Count("join") == 1; }));
    CHECK(f.link->Calls().back() == "join ABC234 [pw]");
    f.Emit(Event::Kind::RoomJoined, { { "code", "ABC234" }, { "peer_id", 3 }, { "host_id", 1 }, { "peers", json::array({ { { "id", 2 }, { "name", "Bob" } }, { { "id", 1 }, { "name", "Host" } } }) } });
    REQUIRE(f.WaitState(SessionInfo::State::InRoom));

    SessionInfo info = f.session->Info();
    CHECK(info.role == "guest");
    CHECK(info.selfId == 3);
    CHECK(info.hostId == 1);
    REQUIRE(info.peers.size() == 3);
    CHECK(info.peers[0].id == 1);
    CHECK(info.peers[0].name == "Host");
    CHECK(info.peers[1].id == 2);
    CHECK(info.peers[2].id == 3);
    CHECK(info.peers[2].name == "Ann");
}

TEST_CASE("players coming and going update the list, repeats and strangers change nothing") {
    Fixture f;
    f.BecomeHost();
    size_t before = f.Infos().size();
    f.Emit(Event::Kind::PeerJoined, { { "id", 2 }, { "name", "Bob" } });
    f.Emit(Event::Kind::PeerJoined, { { "id", 2 }, { "name", "Bob again" } });
    f.Emit(Event::Kind::PeerJoined, { { "id", 3 }, { "name", "Cat" } });
    f.Emit(Event::Kind::PeerJoined, { { "name", "No id" } });
    f.Emit(Event::Kind::PeerLeft, { { "id", 9 } });
    f.Emit(Event::Kind::PeerLeft, { { "id", 2 } });
    SessionInfo info = f.session->Info();
    REQUIRE(info.peers.size() == 2);
    CHECK(info.peers[0].id == 1);
    CHECK(info.peers[1].id == 3);
    CHECK(info.peers[1].name == "Cat");
    CHECK(f.Infos().size() == before + 3); // joined, joined, left: the rest were no news
}

TEST_CASE("one session at a time") {
    Fixture f;
    REQUIRE(f.session->Create("gearblocks", 0, "") == SessionStart::Started);
    CHECK(f.session->Create("gearblocks", 0, "") == SessionStart::Busy);
    CHECK(f.session->Join("gearblocks", "ABC234", "") == SessionStart::Busy);
    REQUIRE(f.WaitFactory());
    f.Emit(Event::Kind::Welcome, { { "v", 1 } });
    f.Emit(Event::Kind::RoomCreated, { { "code", "ABC234" }, { "peer_id", 1 } });
    REQUIRE(f.WaitState(SessionInfo::State::InRoom));
    CHECK(f.session->Create("gearblocks", 0, "") == SessionStart::Busy);
    CHECK(f.factoryCalls == 1);
}

TEST_CASE("a relay that cannot be reached ends the attempt with the reason") {
    Fixture f;
    f.factoryFails = true;
    REQUIRE(f.session->Create("gearblocks", 0, "") == SessionStart::Started);
    REQUIRE(Eventually([&] { return f.session->Info().state == SessionInfo::State::Idle && !f.session->Info().errorCode.empty(); }));
    SessionInfo info = f.session->Info();
    CHECK(info.errorCode == "relay_unreachable");
    CHECK(info.errorMessage == "connection refused");
    f.factoryFails = false;
    CHECK(f.session->Create("gearblocks", 0, "") == SessionStart::Started); // and a new try is possible
}

TEST_CASE("the relay refusing the room ends the attempt, and the connection is closed") {
    Fixture f;
    REQUIRE(f.session->Join("gearblocks", "ABC234", "") == SessionStart::Started);
    REQUIRE(f.WaitFactory());
    f.Emit(Event::Kind::Welcome, { { "v", 1 } });
    f.Emit(Event::Kind::Error, { { "code", "room_not_found" }, { "message", "there is no such room" } });
    REQUIRE(f.WaitState(SessionInfo::State::Idle));
    SessionInfo info = f.session->Info();
    CHECK(info.errorCode == "room_not_found");
    CHECK(info.errorMessage == "there is no such room");
    CHECK(f.link->Count("close") == 1);
    CHECK(f.Infos().back().errorCode == "room_not_found");
}

TEST_CASE("an error once in the room is only news about a refused request") {
    Fixture f;
    f.BecomeHost();
    f.Emit(Event::Kind::Error, { { "code", "not_host" }, { "message", "no" } });
    CHECK(f.session->Info().state == SessionInfo::State::InRoom);
    CHECK(f.link->Count("close") == 0);
}

TEST_CASE("when the host leaves the room the session ends with that reason") {
    Fixture f;
    REQUIRE(f.session->Join("gearblocks", "ABC234", "") == SessionStart::Started);
    REQUIRE(f.WaitFactory());
    f.Emit(Event::Kind::Welcome, { { "v", 1 } });
    f.Emit(Event::Kind::RoomJoined, { { "code", "ABC234" }, { "peer_id", 2 }, { "host_id", 1 }, { "peers", json::array({ { { "id", 1 }, { "name", "Host" } } }) } });
    REQUIRE(f.WaitState(SessionInfo::State::InRoom));
    f.Emit(Event::Kind::RoomClosed, { { "reason", "host_left" } });
    REQUIRE(f.WaitState(SessionInfo::State::Idle));
    CHECK(f.session->Info().errorCode == "room_closed");
    CHECK(f.session->Info().errorMessage == "The host left the room");
    CHECK(f.link->Count("close") == 1);

    // and the next room can be had at once
    CHECK(f.session->Create("gearblocks", 0, "") == SessionStart::Started);
}

TEST_CASE("losing the connection ends the session, a close while idle is nothing") {
    Fixture f;
    f.BecomeHost();
    f.Emit(Event::Kind::Closed, { { "reason", "the connection ended" } });
    REQUIRE(f.WaitState(SessionInfo::State::Idle));
    CHECK(f.session->Info().errorCode == "connection_lost");
    CHECK(f.session->Info().errorMessage.find("the connection ended") != std::string::npos);
    size_t count = f.Infos().size();
    f.Emit(Event::Kind::Closed, { { "reason", "again" } }); // the old connection, long gone
    CHECK(f.Infos().size() == count);
}

TEST_CASE("leaving says goodbye to the relay and the session is idle without an error") {
    Fixture f;
    f.BecomeHost();
    f.session->Leave();
    CHECK(f.session->Info().state == SessionInfo::State::Idle);
    CHECK(f.session->Info().errorCode.empty());
    auto calls = f.link->Calls();
    CHECK(calls.at(calls.size() - 2) == "leave");
    CHECK(calls.back() == "close");
    CHECK(f.Infos().back().state == SessionInfo::State::Idle);

    size_t count = f.Infos().size();
    f.session->Leave(); // nothing to leave
    CHECK(f.Infos().size() == count);
}

TEST_CASE("what an old connection says after leaving is ignored") {
    Fixture f;
    f.BecomeHost();
    f.session->Leave();
    size_t count = f.Infos().size();
    f.Emit(Event::Kind::PeerJoined, { { "id", 2 }, { "name", "Late" } });
    f.Emit(Event::Kind::RoomCreated, { { "code", "ZZZZZZ" }, { "peer_id", 1 } });
    f.Emit(Event::Kind::Closed, { { "reason", "x" } });
    CHECK(f.Infos().size() == count);
    CHECK(f.session->Info().state == SessionInfo::State::Idle);
}

TEST_CASE("leaving while still connecting cancels, and a connection that arrives late is closed") {
    Fixture f;
    std::promise<void> release;
    f.hold = release.get_future().share();
    REQUIRE(f.session->Create("gearblocks", 0, "") == SessionStart::Started);
    REQUIRE(Eventually([&] { return f.factoryCalls == 1; }));
    f.session->Leave();
    CHECK(f.session->Info().state == SessionInfo::State::Idle);
    release.set_value();
    REQUIRE(Eventually([&] { return f.link->Count("close") == 1; }));
    CHECK(f.link->Count("create") == 0);
    CHECK(f.session->Info().state == SessionInfo::State::Idle);
}

TEST_CASE("the player's name is cleaned before it is given to the relay") {
    struct Case {
        std::string given;
        std::string expected;
    };
    std::string long40(40, 'x');
    for (const Case& c : std::vector<Case> {
             { "  Ann  ", "Ann" },
             { "A\x07nn\n", "Ann" },
             { "", "Player" },
             { " \t ", "Player" },
             { long40, std::string(32, 'x') },
             { std::string(31, 'x') + "\xC3\xA9", std::string(31, 'x') }, // the é would end at byte 33: cut whole
             { std::string(30, 'x') + "\xC3\xA9", std::string(30, 'x') + "\xC3\xA9" },
             { "Zo\xC3\xAB \xF0\x9F\x9A\x97", "Zo\xC3\xAB \xF0\x9F\x9A\x97" },
         }) {
        Fixture f;
        f.name = c.given;
        REQUIRE(f.session->Create("gearblocks", 0, "") == SessionStart::Started);
        REQUIRE(f.WaitFactory());
        CHECK(f.lastHello.name == c.expected);
        f.Emit(Event::Kind::Welcome, { { "v", 1 } });
        f.Emit(Event::Kind::RoomCreated, { { "code", "ABC234" }, { "peer_id", 1 } });
        REQUIRE(f.WaitState(SessionInfo::State::InRoom));
        CHECK(f.session->Info().peers[0].name == c.expected);
    }
}

TEST_CASE("game data goes out through the room and comes in from the other players") {
    Fixture f;
    CHECK_FALSE(f.session->SendData(DataKind::ToAll, 0, "not in a room"));
    f.BecomeHost();
    CHECK(f.session->SendData(DataKind::ToPeer, 7, "hello"));
    CHECK(f.link->Calls().back() == "data 2 7");
    CHECK(f.link->sentBytes.back() == "hello");
    f.link->sendWorks = false;
    CHECK_FALSE(f.session->SendData(DataKind::ToAll, 0, "x"));

    f.EmitData(2, DataKind::ToHost, "request");
    REQUIRE(Eventually([&] {
        std::lock_guard<std::mutex> lock(f.mutex);
        return f.data.size() == 1;
    }));
    CHECK(f.data[0].sender == 2);
    CHECK(f.data[0].bytes == "request");
}

TEST_CASE("data that arrives while not in a room is not passed on") {
    Fixture f;
    REQUIRE(f.session->Create("gearblocks", 0, "") == SessionStart::Started);
    REQUIRE(f.WaitFactory());
    f.EmitData(2, DataKind::ToAll, "too early");
    std::lock_guard<std::mutex> lock(f.mutex);
    CHECK(f.data.empty());
}

TEST_CASE("the session as JSON") {
    SessionInfo idle;
    CHECK(ToJson(idle) == json { { "game", "" }, { "state", "idle" } });

    SessionInfo failed;
    failed.game = "gearblocks";
    failed.errorCode = "room_full";
    failed.errorMessage = "The room is full";
    CHECK(ToJson(failed) == json { { "game", "gearblocks" }, { "state", "idle" }, { "error", { { "code", "room_full" }, { "message", "The room is full" } } } });

    SessionInfo connecting;
    connecting.game = "gearblocks";
    connecting.state = SessionInfo::State::Connecting;
    CHECK(ToJson(connecting) == json { { "game", "gearblocks" }, { "state", "connecting" } });

    SessionInfo room;
    room.game = "gearblocks";
    room.state = SessionInfo::State::InRoom;
    room.role = "guest";
    room.code = "ABC234";
    room.selfId = 2;
    room.hostId = 1;
    room.peers = { { 1, "Host" }, { 2, "Ann" } };
    CHECK(ToJson(room) == json { { "game", "gearblocks" }, { "state", "in_room" }, { "role", "guest" }, { "code", "ABC234" }, { "self_id", 2 }, { "host_id", 1 },
                             { "peers", json::array({ { { "id", 1 }, { "name", "Host" } }, { { "id", 2 }, { "name", "Ann" } } }) } });
}

TEST_CASE("destroying the session closes its connection, even with a setup still under way") {
    auto link = std::make_shared<FakeLink>();
    {
        Fixture f;
        f.link = link;
        f.BecomeHost();
    }
    CHECK(link->Count("close") == 1);

    std::promise<void> release;
    auto link2 = std::make_shared<FakeLink>();
    std::thread releaser;
    {
        Fixture f;
        f.link = link2;
        f.hold = release.get_future().share();
        REQUIRE(f.session->Create("gearblocks", 0, "") == SessionStart::Started);
        REQUIRE(Eventually([&] { return f.factoryCalls == 1; }));
        releaser = std::thread([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            release.set_value();
        });
    } // waits for the worker, which gets its connection at last and finds it is not wanted
    releaser.join();
    CHECK(link2->Count("close") == 1);
    CHECK(link2->Count("create") == 0);
}
