/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Games/RelayLink.h"
#include "Net/Sockets.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>

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

// Collects the events of a link.
struct Events {
    std::mutex mutex;
    std::vector<Event> events;

    EventHandler Handler() {
        return [this](const Event& event) {
            std::lock_guard<std::mutex> lock(mutex);
            events.push_back(event);
        };
    }
    size_t Count() {
        std::lock_guard<std::mutex> lock(mutex);
        return events.size();
    }
    bool WaitFor(size_t count, int timeoutMs = 3000) {
        return Eventually([&] { return Count() >= count; }, timeoutMs);
    }
    Event At(size_t index) {
        std::lock_guard<std::mutex> lock(mutex);
        return events.at(index);
    }
    int Of(Event::Kind kind) {
        std::lock_guard<std::mutex> lock(mutex);
        int count = 0;
        for (const Event& event : events) {
            count += event.kind == kind ? 1 : 0;
        }
        return count;
    }
};

// A relay that does what the test tells it: accepts one connection, records the frames it gets, sends what it is given.
class ScriptedRelay {
public:
    ScriptedRelay() {
        auto bound = Net::ListenLoopback(0, "scripted relay");
        m_listener = bound.socket;
        port = bound.port;
        m_acceptor = std::thread([this] {
            if (!Net::WaitReadable(m_listener, 3000)) {
                return;
            }
            m_client = accept(m_listener, nullptr, nullptr);
            if (m_client == Net::Invalid) {
                return;
            }
            m_connected = true;
            char chunk[8192];
            while (!m_stopping) {
                if (!Net::WaitReadable(m_client, 100)) {
                    continue;
                }
                auto received = recv(m_client, chunk, sizeof(chunk), 0);
                if (received <= 0) {
                    break;
                }
                std::lock_guard<std::mutex> lock(m_mutex);
                for (Frame& frame : m_decoder.Push(chunk, static_cast<size_t>(received))) {
                    m_frames.push_back(std::move(frame));
                }
            }
            m_closedByClient = true;
        });
    }

    ~ScriptedRelay() {
        m_stopping = true;
        if (m_acceptor.joinable()) {
            m_acceptor.join();
        }
        if (m_client != Net::Invalid) {
            Net::CloseNative(m_client);
        }
        Net::CloseNative(m_listener);
    }

    bool WaitConnected() { return Eventually([&] { return m_connected.load(); }); }
    bool WaitFrames(size_t count) { return Eventually([&] { return Frames().size() >= count; }); }
    std::vector<Frame> Frames() {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_frames;
    }
    void Send(const std::string& bytes) { Net::SendAll(m_client, bytes.data(), bytes.size()); }
    void HangUp() {
        if (m_client != Net::Invalid) {
            Net::ShutdownNative(m_client);
        }
    }
    bool ClientHungUp() { return Eventually([&] { return m_closedByClient.load(); }); }

    uint16_t port = 0;

private:
    Net::Native m_listener = Net::Invalid;
    Net::Native m_client = Net::Invalid;
    std::thread m_acceptor;
    std::atomic<bool> m_stopping = false;
    std::atomic<bool> m_connected = false;
    std::atomic<bool> m_closedByClient = false;
    std::mutex m_mutex;
    FrameDecoder m_decoder;
    std::vector<Frame> m_frames;
};

Hello Ann() {
    return { "Ann", "gearblocks", "tests", "" };
}
}

TEST_CASE("only this machine and private networks are talked to without TLS") {
    for (const char* local : { "localhost", "127.0.0.1", "127.1.2.3", "::1", "10.0.0.5", "172.16.0.1", "172.31.255.254", "192.168.1.20" }) {
        CHECK(IsLocalHost(local));
    }
    for (const char* remote : { "relay.beammp.com", "8.8.8.8", "172.15.0.1", "172.32.0.1", "192.169.0.1", "11.0.0.1", "", "10.0.0", "1.2.3.4.5" }) {
        CHECK_FALSE(IsLocalHost(remote));
    }
}

TEST_CASE("a relay that is not there is reported, not waited for") {
    std::string error;
    auto bound = Net::ListenLoopback(0, "closed port"); // find a free port, then close it
    uint16_t port = bound.port;
    Net::CloseNative(bound.socket);
    auto link = ConnectTcpLink({ "127.0.0.1", port }, Ann(), nullptr, error);
    CHECK(link == nullptr);
    CHECK_FALSE(error.empty());

    std::string unknown;
    CHECK(ConnectTcpLink({ "no-such-host.invalid", 1 }, Ann(), nullptr, unknown) == nullptr);
    CHECK_FALSE(unknown.empty());
}

TEST_CASE("connecting says hello with the name, the game and the version") {
    ScriptedRelay relay;
    Events events;
    std::string error;
    Hello hello = { "Ann", "gearblocks", "beammp-launcher/9", "secret-token" };
    auto link = ConnectTcpLink({ "127.0.0.1", relay.port }, hello, events.Handler(), error);
    REQUIRE(link);
    REQUIRE(relay.WaitFrames(1));
    Frame first = relay.Frames()[0];
    CHECK(first.type == ToRelay::Hello);
    json message = json::parse(first.payload);
    CHECK(message["v"] == 1);
    CHECK(message["name"] == "Ann");
    CHECK(message["game"] == "gearblocks");
    CHECK(message["client"] == "beammp-launcher/9");
    CHECK(message["token"] == "secret-token");
}

TEST_CASE("without a token or client name none is sent") {
    ScriptedRelay relay;
    std::string error;
    auto link = ConnectTcpLink({ "127.0.0.1", relay.port }, { "Ann", "gearblocks", "", "" }, nullptr, error);
    REQUIRE(link);
    REQUIRE(relay.WaitFrames(1));
    json message = json::parse(relay.Frames()[0].payload);
    CHECK_FALSE(message.contains("token"));
    CHECK_FALSE(message.contains("client"));
}

TEST_CASE("room requests and data are sent as the relay expects them") {
    ScriptedRelay relay;
    Events events;
    std::string error;
    auto link = ConnectTcpLink({ "127.0.0.1", relay.port }, Ann(), events.Handler(), error);
    REQUIRE(link);

    link->CreateRoom(4, "pw");
    link->CreateRoom(0, "");
    link->JoinRoom("ABC234", "pw");
    link->JoinRoom("ABC234", "");
    link->SendData(DataKind::ToOthers, 0, "xyz");
    link->LeaveRoom();
    REQUIRE(relay.WaitFrames(7));
    auto frames = relay.Frames();
    CHECK(json::parse(frames[1].payload) == json { { "max_players", 4 }, { "password", "pw" } });
    CHECK(frames[1].type == ToRelay::CreateRoom);
    CHECK(json::parse(frames[2].payload) == json::object());
    CHECK(json::parse(frames[3].payload) == json { { "code", "ABC234" }, { "password", "pw" } });
    CHECK(json::parse(frames[4].payload) == json { { "code", "ABC234" } });
    CHECK(frames[5].type == ToRelay::Data);
    CHECK(frames[5].payload == std::string({ 3, 0, 0, 'x', 'y', 'z' }));
    CHECK(frames[6].type == ToRelay::LeaveRoom);
    CHECK(frames[6].payload.empty());
}

TEST_CASE("data that cannot fit in a frame is refused") {
    ScriptedRelay relay;
    std::string error;
    auto link = ConnectTcpLink({ "127.0.0.1", relay.port }, Ann(), nullptr, error);
    REQUIRE(link);
    CHECK_FALSE(link->SendData(DataKind::ToAll, 0, std::string(MaxFrameBytes, 'x')));
    CHECK(link->SendData(DataKind::ToAll, 0, std::string(100 * 1024, 'x')));
}

TEST_CASE("what the relay says becomes events, in order") {
    ScriptedRelay relay;
    Events events;
    std::string error;
    auto link = ConnectTcpLink({ "127.0.0.1", relay.port }, Ann(), events.Handler(), error);
    REQUIRE(link);
    REQUIRE(relay.WaitConnected());

    relay.Send(EncodeJson(FromRelay::Welcome, { { "v", 1 }, { "server", "x" } }));
    relay.Send(EncodeJson(FromRelay::RoomCreated, { { "code", "ABC234" }, { "peer_id", 1 } }));
    relay.Send(EncodeJson(FromRelay::PeerJoined, { { "id", 2 }, { "name", "Bob" } }));
    relay.Send(EncodeJson(FromRelay::PeerLeft, { { "id", 2 } }));
    relay.Send(EncodeJson(FromRelay::RoomJoined, { { "code", "ABC234" }, { "peer_id", 3 }, { "host_id", 1 }, { "peers", json::array() } }));
    relay.Send(EncodeJson(FromRelay::RoomClosed, { { "reason", "host_left" } }));
    relay.Send(EncodeJson(FromRelay::Error, { { "code", "room_full" }, { "message", "full" } }));
    relay.Send(EncodeData(DataKind::ToAll, 0, "") .substr(0, 0)); // nothing
    // a data frame as the relay sends it: sender, kind, bytes
    relay.Send(EncodeFrame(FromRelay::Data, std::string({ 0, 7, 1, 'h', 'i' })));
    REQUIRE(events.WaitFor(8));

    CHECK(events.At(0).kind == Event::Kind::Welcome);
    CHECK(events.At(1).kind == Event::Kind::RoomCreated);
    CHECK(events.At(1).json["code"] == "ABC234");
    CHECK(events.At(2).kind == Event::Kind::PeerJoined);
    CHECK(events.At(2).json["name"] == "Bob");
    CHECK(events.At(3).kind == Event::Kind::PeerLeft);
    CHECK(events.At(4).kind == Event::Kind::RoomJoined);
    CHECK(events.At(5).kind == Event::Kind::RoomClosed);
    CHECK(events.At(6).kind == Event::Kind::Error);
    CHECK(events.At(6).json["code"] == "room_full");
    CHECK(events.At(7).kind == Event::Kind::Data);
    CHECK(events.At(7).data.sender == 7);
    CHECK(events.At(7).data.kind == DataKind::ToAll);
    CHECK(events.At(7).data.bytes == "hi");
}

TEST_CASE("pongs, unknown messages and broken ones are not events") {
    ScriptedRelay relay;
    Events events;
    std::string error;
    auto link = ConnectTcpLink({ "127.0.0.1", relay.port }, Ann(), events.Handler(), error);
    REQUIRE(link);
    REQUIRE(relay.WaitConnected());
    relay.Send(EncodeFrame(FromRelay::Pong));
    relay.Send(EncodeFrame(0x55, "{}"));
    relay.Send(EncodeFrame(FromRelay::PeerJoined, "not json"));
    relay.Send(EncodeFrame(FromRelay::PeerJoined, "[1]"));
    relay.Send(EncodeFrame(FromRelay::Data, "ab")); // too short to be data
    relay.Send(EncodeJson(FromRelay::PeerLeft, { { "id", 9 } }));
    REQUIRE(events.WaitFor(1));
    CHECK(events.At(0).kind == Event::Kind::PeerLeft);
    CHECK(events.Count() == 1);
}

TEST_CASE("the relay hanging up is one Closed event, and sending stops working") {
    ScriptedRelay relay;
    Events events;
    std::string error;
    auto link = ConnectTcpLink({ "127.0.0.1", relay.port }, Ann(), events.Handler(), error);
    REQUIRE(link);
    REQUIRE(relay.WaitConnected());
    relay.HangUp();
    REQUIRE(events.WaitFor(1));
    CHECK(events.At(0).kind == Event::Kind::Closed);
    CHECK_FALSE(events.At(0).json["reason"].get<std::string>().empty());
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    CHECK(events.Of(Event::Kind::Closed) == 1);
    CHECK_FALSE(link->SendData(DataKind::ToAll, 0, "late"));
}

TEST_CASE("something that is not the protocol closes the link") {
    ScriptedRelay relay;
    Events events;
    std::string error;
    auto link = ConnectTcpLink({ "127.0.0.1", relay.port }, Ann(), events.Handler(), error);
    REQUIRE(link);
    REQUIRE(relay.WaitConnected());
    relay.Send(std::string({ char(0xff), char(0xff), char(0xff), char(0xff), 1, 2 }));
    REQUIRE(events.WaitFor(1));
    CHECK(events.At(0).kind == Event::Kind::Closed);
    CHECK(events.At(0).json["reason"].get<std::string>().find("protocol") != std::string::npos);
}

TEST_CASE("closing the link ends the connection and reports it once") {
    ScriptedRelay relay;
    Events events;
    std::string error;
    auto link = ConnectTcpLink({ "127.0.0.1", relay.port }, Ann(), events.Handler(), error);
    REQUIRE(link);
    REQUIRE(relay.WaitConnected());
    link->Close();
    link->Close();
    REQUIRE(events.WaitFor(1));
    CHECK(events.At(0).kind == Event::Kind::Closed);
    CHECK(relay.ClientHungUp());
    CHECK_FALSE(link->SendData(DataKind::ToAll, 0, "x"));
    link.reset();
    CHECK(events.Of(Event::Kind::Closed) == 1);
}

TEST_CASE("a handler may close its own link, and let go of it") {
    ScriptedRelay relay;
    std::atomic<bool> closed = false;
    std::unique_ptr<Link> link;
    std::mutex mutex;
    std::string error;
    auto handler = [&](const Event& event) {
        if (event.kind == Event::Kind::PeerLeft) {
            std::lock_guard<std::mutex> lock(mutex);
            if (link) {
                link->Close();
                link.reset(); // from the link's own thread
            }
        }
        if (event.kind == Event::Kind::Closed) {
            closed = true;
        }
    };
    {
        std::lock_guard<std::mutex> lock(mutex);
        link = ConnectTcpLink({ "127.0.0.1", relay.port }, Ann(), handler, error);
        REQUIRE(link);
    }
    REQUIRE(relay.WaitConnected());
    relay.Send(EncodeJson(FromRelay::PeerLeft, { { "id", 2 } }));
    CHECK(Eventually([&] { return closed.load(); }));
}

TEST_CASE("destroying a link while its relay is still talking is safe") {
    ScriptedRelay relay;
    Events events;
    std::string error;
    auto link = ConnectTcpLink({ "127.0.0.1", relay.port }, Ann(), events.Handler(), error);
    REQUIRE(link);
    REQUIRE(relay.WaitConnected());
    for (int i = 0; i < 50; i++) {
        relay.Send(EncodeJson(FromRelay::PeerJoined, { { "id", i + 2 }, { "name", "x" } }));
    }
    link.reset();
    CHECK(true); // no crash, no hang
}

namespace {
// The real relay (node), when this machine has one that can run it. Skipped otherwise.
class RealRelay {
public:
    RealRelay() {
        namespace fs = std::filesystem;
        fs::path main = fs::path(__FILE__).parent_path().parent_path() / "relay" / "src" / "main.ts";
        if (!fs::exists(main) || std::system("node -e \"process.exit(Number(process.versions.node.split('.')[0]) >= 22 ? 0 : 1)\" >/dev/null 2>&1") != 0) {
            return;
        }
        m_log = (fs::temp_directory_path() / ("beammp-relay-" + std::to_string(::getpid()) + ".log")).string();
        std::string command = "node \"" + main.string() + "\" --port 0 > \"" + m_log + "\" 2>&1 & echo $! > \"" + m_log + ".pid\"";
        if (std::system(command.c_str()) != 0) {
            return;
        }
        Eventually([&] {
            std::ifstream in(m_log);
            std::string line;
            while (std::getline(in, line)) {
                json message = json::parse(line, nullptr, false);
                if (message.is_object() && message.value("event", "") == "listening") {
                    port = message["port"].get<uint16_t>();
                    return true;
                }
            }
            return false;
        }, 8000);
    }

    ~RealRelay() {
        std::ifstream pid(m_log + ".pid");
        std::string value;
        if (pid >> value) {
            std::system(("kill " + value + " >/dev/null 2>&1").c_str());
        }
        std::error_code ec;
        std::filesystem::remove(m_log, ec);
        std::filesystem::remove(m_log + ".pid", ec);
    }

    uint16_t port = 0;

private:
    std::string m_log;
};
}

TEST_CASE("the C++ client and the real relay agree on the protocol") {
    RealRelay relay;
    if (relay.port == 0) {
        MESSAGE("skipped: no node (22 or newer) to run the relay");
        return;
    }
    Events hostEvents;
    Events guestEvents;
    std::string error;
    auto host = ConnectTcpLink({ "127.0.0.1", relay.port }, { "Host", "gearblocks", "tests", "" }, hostEvents.Handler(), error);
    REQUIRE(host);
    REQUIRE(hostEvents.WaitFor(1));
    CHECK(hostEvents.At(0).kind == Event::Kind::Welcome);

    host->CreateRoom(3, "pw");
    REQUIRE(hostEvents.WaitFor(2));
    REQUIRE(hostEvents.At(1).kind == Event::Kind::RoomCreated);
    std::string code = hostEvents.At(1).json["code"];
    CHECK(code.size() == 6);
    CHECK(hostEvents.At(1).json["peer_id"] == 1);

    auto guest = ConnectTcpLink({ "127.0.0.1", relay.port }, { "Guest", "gearblocks", "tests", "" }, guestEvents.Handler(), error);
    REQUIRE(guest);
    REQUIRE(guestEvents.WaitFor(1));
    guest->JoinRoom(code, "wrong");
    REQUIRE(guestEvents.WaitFor(2));
    CHECK(guestEvents.At(1).kind == Event::Kind::Error);
    CHECK(guestEvents.At(1).json["code"] == "bad_password");
    guest->JoinRoom(code, "pw");
    REQUIRE(guestEvents.WaitFor(3));
    REQUIRE(guestEvents.At(2).kind == Event::Kind::RoomJoined);
    CHECK(guestEvents.At(2).json["peer_id"] == 2);
    CHECK(guestEvents.At(2).json["peers"][0]["name"] == "Host");
    REQUIRE(hostEvents.WaitFor(3));
    CHECK(hostEvents.At(2).kind == Event::Kind::PeerJoined);
    CHECK(hostEvents.At(2).json["name"] == "Guest");

    guest->SendData(DataKind::ToHost, 0, std::string("\0binary\xff", 8));
    REQUIRE(hostEvents.WaitFor(4));
    REQUIRE(hostEvents.At(3).kind == Event::Kind::Data);
    CHECK(hostEvents.At(3).data.sender == 2);
    CHECK(hostEvents.At(3).data.bytes == std::string("\0binary\xff", 8));

    host->SendData(DataKind::ToAll, 0, "everyone");
    REQUIRE(guestEvents.WaitFor(4));
    CHECK(guestEvents.At(3).data.sender == 1);
    CHECK(guestEvents.At(3).data.kind == DataKind::ToAll);
    CHECK(guestEvents.At(3).data.bytes == "everyone");

    host->LeaveRoom();
    REQUIRE(guestEvents.WaitFor(5));
    CHECK(guestEvents.At(4).kind == Event::Kind::RoomClosed);
}
