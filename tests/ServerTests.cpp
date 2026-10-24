/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Control/Server.h"
#include "TestClient.h"
#include <atomic>
#include <chrono>
#include <doctest/doctest.h>
#include <thread>

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>

using namespace Control;
using nlohmann::json;

namespace {
const std::string Token = "0123456789abcdef0123456789abcdef";

struct Running {
    Server server { Token };
    uint16_t port = 0;

    Running() {
        server.SetHelloInfo({ { "engine", "test" }, { "games", { "beamng" } } });
        server.Handle("echo", [](const json& data) { return data; });
        server.Handle("fail", [](const json&) -> json { throw RequestError("custom_code", "custom message"); });
        server.Handle("crash", [](const json&) -> json { throw std::runtime_error("unexpected"); });
    }

    void Start() { port = server.Start(0); }
};

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
}

TEST_CASE_FIXTURE(Running, "the server binds to loopback and reports its port") {
    Start();
    CHECK(port != 0);
    TestClient client(port);
    CHECK(client.Connected());
}

TEST_CASE_FIXTURE(Running, "a second server cannot take the same port") {
    Start();
    Server other(Token);
    CHECK_THROWS_AS(other.Start(port), std::runtime_error);
}

TEST_CASE_FIXTURE(Running, "the handshake returns the version and the engine info") {
    Start();
    TestClient client(port);
    json response = client.Hello(Token);
    CHECK(response["ok"] == true);
    CHECK(response["data"]["version"] == 1);
    CHECK(response["data"]["engine"] == "test");
    CHECK(response["data"]["games"] == json::array({ "beamng" }));
}

TEST_CASE_FIXTURE(Running, "a wrong token is dropped without a reply") {
    Start();
    TestClient client(port);
    client.SendRequest(1, "hello", { { "token", "wrong-token-wrong-token-wrong-to" }, { "versions", { 1 } } });
    CHECK_FALSE(client.Read().has_value());
    CHECK(client.WaitForClose());
}

TEST_CASE_FIXTURE(Running, "a first message that is not a valid hello is dropped without a reply") {
    Start();
    SUBCASE("not json") {
        TestClient client(port);
        client.SendLine("GET / HTTP/1.1");
        CHECK_FALSE(client.Read().has_value());
        CHECK(client.WaitForClose());
    }
    SUBCASE("another request") {
        TestClient client(port);
        client.SendRequest(1, "echo", { { "token", Token } });
        CHECK_FALSE(client.Read().has_value());
        CHECK(client.WaitForClose());
    }
    SUBCASE("hello without a token") {
        TestClient client(port);
        client.SendRequest(1, "hello", { { "versions", { 1 } } });
        CHECK_FALSE(client.Read().has_value());
        CHECK(client.WaitForClose());
    }
    SUBCASE("token of the wrong type") {
        TestClient client(port);
        client.SendRequest(1, "hello", { { "token", 5 }, { "versions", { 1 } } });
        CHECK_FALSE(client.Read().has_value());
        CHECK(client.WaitForClose());
    }
}

TEST_CASE_FIXTURE(Running, "an oversized unauthenticated line closes the connection") {
    Start();
    TestClient client(port);
    client.SendLine(std::string(MaxHelloBytes + 100, 'x'));
    CHECK_FALSE(client.Read().has_value());
    CHECK(client.WaitForClose());
}

TEST_CASE_FIXTURE(Running, "a client that never says hello is dropped") {
    Start();
    TestClient client(port);
    CHECK(client.WaitForClose(8000));
}

TEST_CASE_FIXTURE(Running, "an incompatible protocol is reported, then the connection closes") {
    Start();
    TestClient client(port);
    json response = client.Call("hello", { { "token", Token }, { "versions", { 2, 3 } } });
    CHECK(response["ok"] == false);
    CHECK(response["error"]["code"] == "incompatible_protocol");
    CHECK(client.WaitForClose());
}

TEST_CASE_FIXTURE(Running, "requests are answered by their handlers") {
    Start();
    TestClient client(port);
    client.Hello(Token);

    json response = client.Call("echo", { { "a", 1 }, { "b", "two" } });
    CHECK(response["ok"] == true);
    CHECK(response["data"] == json { { "a", 1 }, { "b", "two" } });
}

TEST_CASE_FIXTURE(Running, "errors become error responses") {
    Start();
    TestClient client(port);
    client.Hello(Token);

    json custom = client.Call("fail");
    CHECK(custom["ok"] == false);
    CHECK(custom["error"] == json { { "code", "custom_code" }, { "message", "custom message" } });

    json crashed = client.Call("crash");
    CHECK(crashed["error"]["code"] == "internal_error");

    json unknown = client.Call("no.such.request");
    CHECK(unknown["error"]["code"] == "unknown_request");

    json again = client.Hello(Token);
    CHECK(again["error"]["code"] == "already_authenticated");

    CHECK(client.Call("echo", { { "still", "works" } })["ok"] == true);
}

TEST_CASE_FIXTURE(Running, "a malformed line after the handshake is answered and the connection stays open") {
    Start();
    TestClient client(port);
    client.Hello(Token);
    client.SendLine("{not json");
    auto response = client.Read();
    REQUIRE(response);
    CHECK((*response)["error"]["code"] == "bad_request");
    CHECK(client.Call("echo")["ok"] == true);
}

TEST_CASE_FIXTURE(Running, "responses arrive in request order when requests are pipelined") {
    Start();
    TestClient client(port);
    client.Hello(Token);
    for (int i = 0; i < 20; ++i) {
        client.SendRequest(1000 + i, "echo", { { "n", i } });
    }
    for (int i = 0; i < 20; ++i) {
        auto response = client.Read();
        REQUIRE(response);
        CHECK((*response)["id"] == 1000 + i);
        CHECK((*response)["data"]["n"] == i);
    }
}

TEST_CASE_FIXTURE(Running, "events only reach an authenticated client, after the handshake response") {
    Start();
    server.Publish("early", json::object());

    TestClient client(port);
    CHECK(Eventually([&] { return !server.HasClient(); }));
    server.Publish("before.hello", json::object());
    json hello = client.Hello(Token);
    CHECK(hello["ok"] == true);
    CHECK(Eventually([&] { return server.HasClient(); }));

    server.Publish("game.state", { { "state", "ready" } });
    auto event = client.WaitForEvent("game.state");
    REQUIRE(event);
    CHECK((*event)["data"]["state"] == "ready");
    CHECK(client.TakePending().empty());
}

TEST_CASE_FIXTURE(Running, "the handshake response is always the first message, even while events are being published") {
    Start();
    std::atomic<bool> stop = false;
    std::thread spammer([&] {
        while (!stop) {
            server.Publish("spam", json::object());
        }
    });
    for (int i = 0; i < 100; ++i) {
        TestClient client(port);
        REQUIRE(client.Connected());
        client.SendRequest(1, "hello", { { "token", Token }, { "versions", { 1 } } });
        auto first = client.Read();
        REQUIRE(first);
        REQUIRE((*first)["type"] == "response");
    }
    stop = true;
    spammer.join();
}

TEST_CASE_FIXTURE(Running, "attach and detach callbacks run once per client") {
    std::atomic<int> attached = 0, detached = 0;
    server.OnAttached([&] { ++attached; });
    server.OnDetached([&] { ++detached; });
    Start();
    {
        TestClient client(port);
        client.Hello(Token);
        CHECK(Eventually([&] { return attached == 1; }));
        CHECK(detached == 0);
    }
    CHECK(Eventually([&] { return detached == 1; }));
    CHECK(Eventually([&] { return !server.HasClient(); }));

    TestClient wrong(port);
    wrong.SendRequest(1, "hello", { { "token", "nope-nope-nope-nope-nope-nope-no" }, { "versions", { 1 } } });
    CHECK(wrong.WaitForClose());
    CHECK(attached == 1);
    CHECK(detached == 1); // a client that never authenticated is not a detach
}

TEST_CASE_FIXTURE(Running, "only one client is served at a time, and the slot frees up") {
    Start();
    {
        TestClient first(port);
        REQUIRE(first.Hello(Token)["ok"] == true);

        TestClient second(port);
        CHECK(second.WaitForClose());
        CHECK(first.Call("echo")["ok"] == true);
    }
    CHECK(Eventually([&] { return !server.HasClient(); }));

    TestClient third(port);
    CHECK(third.Hello(Token)["ok"] == true);
}

TEST_CASE_FIXTURE(Running, "Stop delivers everything that was queued") {
    Start();
    TestClient client(port);
    client.Hello(Token);
    CHECK(Eventually([&] { return server.HasClient(); }));

    for (int i = 0; i < 200; ++i) {
        server.Publish("n", { { "i", i } });
    }
    server.Stop();

    int received = 0;
    while (auto event = client.Read()) {
        CHECK((*event)["data"]["i"] == received);
        ++received;
    }
    CHECK(received == 200);
}

TEST_CASE_FIXTURE(Running, "a client that does not read never blocks the engine, and the newest events survive") {
    Start();
    TestClient client(port);
    client.Hello(Token);
    CHECK(Eventually([&] { return server.HasClient(); }));

    constexpr int Count = 20000;
    const std::string padding(1024, 'p');
    auto started = std::chrono::steady_clock::now();
    for (int i = 0; i < Count; ++i) {
        server.Publish("log", { { "i", i }, { "padding", padding } }, true);
    }
    CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds(5));

    int last = -1;
    int received = 0;
    while (last != Count - 1) {
        auto event = client.Read();
        REQUIRE(event);
        int index = (*event)["data"]["i"];
        CHECK(index > last); // order is kept even when old events were dropped
        last = index;
        ++received;
    }
    CHECK(received < Count);
}

TEST_CASE_FIXTURE(Running, "events that must not be lost are never dropped for droppable ones") {
    Start();
    TestClient client(port);
    client.Hello(Token);
    CHECK(Eventually([&] { return server.HasClient(); }));

    const std::string padding(1024, 'p');
    server.Publish("important", { { "keep", true } });
    for (int i = 0; i < 20000; ++i) {
        server.Publish("log", { { "i", i }, { "padding", padding } }, true);
    }
    auto event = client.WaitForEvent("important", 10000);
    CHECK(event.has_value());
}

TEST_CASE_FIXTURE(Running, "handlers cannot be registered once the server runs") {
    Start();
    CHECK_THROWS_AS(server.Handle("late", [](const json&) { return json::object(); }), std::logic_error);
    CHECK_THROWS_AS(server.Start(0), std::logic_error);
}

TEST_CASE("hello is reserved") {
    Server server(Token);
    CHECK_THROWS_AS(server.Handle("hello", [](const json&) { return json::object(); }), std::logic_error);
}

TEST_CASE_FIXTURE(Running, "stopping twice, or never starting, is harmless") {
    Server idle(Token);
    idle.Stop();
    Start();
    server.Stop();
    server.Stop();
}

TEST_CASE_FIXTURE(Running, "the control connection is not inherited by child processes") {
    Start();
    TestClient client(port);
    client.Hello(Token);
    // The accepted socket is the server side; its descriptor is found by scanning for close-on-exec sockets.
    int checked = 0;
    for (int fd = 3; fd < 256; ++fd) {
        int flags = fcntl(fd, F_GETFD);
        if (flags < 0) {
            continue;
        }
        int type = 0;
        socklen_t length = sizeof(type);
        if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &length) != 0) {
            continue;
        }
        sockaddr_in address {};
        socklen_t addressLength = sizeof(address);
        if (getsockname(fd, reinterpret_cast<sockaddr*>(&address), &addressLength) != 0 || ntohs(address.sin_port) != port) {
            continue;
        }
        CHECK((flags & FD_CLOEXEC) != 0);
        ++checked;
    }
    CHECK(checked >= 2); // the listener and the accepted connection
}
