/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Games/GameBridge.h"
#include "TestClient.h"
#include <atomic>
#include <chrono>
#include <doctest/doctest.h>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

using Games::GameBridge;

namespace {
const std::string Token = "0123456789abcdef0123456789abcdef";

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

struct Bridge {
    GameBridge bridge { Token };
    std::atomic<int> connects = 0;
    std::atomic<int> disconnects = 0;
    std::mutex mutex;
    std::vector<std::string> lines;
    uint16_t port = 0;

    Bridge() {
        bridge.OnConnected([this] { ++connects; });
        bridge.OnDisconnected([this] { ++disconnects; });
        bridge.OnLine([this](const std::string& line) {
            std::lock_guard<std::mutex> lock(mutex);
            lines.push_back(line);
        });
        port = bridge.Start();
    }

    std::vector<std::string> Lines() {
        std::lock_guard<std::mutex> lock(mutex);
        return lines;
    }
};
}

TEST_CASE("a bridge needs a token of at least 16 characters") {
    CHECK_THROWS(GameBridge("short"));
    CHECK_THROWS(GameBridge(std::string(15, 'a')));
    CHECK_NOTHROW(GameBridge(std::string(16, 'a')));
}

TEST_CASE("generated tokens are long, hex, and different every time") {
    std::set<std::string> seen;
    for (int i = 0; i < 20; ++i) {
        std::string token = GameBridge::GenerateToken();
        CHECK(token.size() == 32);
        CHECK(token.find_first_not_of("0123456789abcdef") == std::string::npos);
        seen.insert(token);
    }
    CHECK(seen.size() == 20);
}

TEST_CASE_FIXTURE(Bridge, "the right token connects, and lines go both ways") {
    CHECK_FALSE(bridge.Connected());
    CHECK_FALSE(bridge.Send("{\"nobody\":true}"));

    TestClient client(port);
    client.SendLine(Token);
    REQUIRE(Eventually([&] { return bridge.Connected(); }));
    CHECK(connects == 1);

    client.SendLine("{\"type\":\"hello\"}");
    client.SendLine("second");
    REQUIRE(Eventually([&] { return Lines().size() == 2; }));
    CHECK(Lines() == std::vector<std::string> { "{\"type\":\"hello\"}", "second" });

    CHECK(bridge.Send("{\"type\":\"welcome\"}"));
    CHECK(bridge.Send("next"));
    CHECK(client.ReadLine() == "{\"type\":\"welcome\"}");
    CHECK(client.ReadLine() == "next");
}

TEST_CASE_FIXTURE(Bridge, "a carriage return before the newline is ignored") {
    TestClient client(port);
    client.SendLine(Token + "\r");
    REQUIRE(Eventually([&] { return bridge.Connected(); }));
    client.SendLine("line\r");
    REQUIRE(Eventually([&] { return Lines().size() == 1; }));
    CHECK(Lines()[0] == "line");
}

TEST_CASE_FIXTURE(Bridge, "a wrong token is dropped without any reply") {
    for (const std::string& wrong : { std::string("not the token at all"), Token.substr(0, Token.size() - 1), Token + "x", std::string("") }) {
        TestClient client(port);
        client.SendLine(wrong);
        client.SendLine("{\"type\":\"hello\"}");
        CHECK(client.WaitForClose());
        CHECK_FALSE(client.ReadLine(100));
    }
    CHECK_FALSE(bridge.Connected());
    CHECK(connects == 0);
    CHECK(Lines().empty());
}

TEST_CASE_FIXTURE(Bridge, "a connection that sends nothing but junk cannot fill memory before the token") {
    TestClient client(port);
    client.SendLine(std::string(5000, 'a')); // far longer than a token
    CHECK(client.WaitForClose());
    CHECK(connects == 0);
}

TEST_CASE_FIXTURE(Bridge, "a second connection is refused while one is connected") {
    TestClient first(port);
    first.SendLine(Token);
    REQUIRE(Eventually([&] { return bridge.Connected(); }));

    TestClient second(port);
    second.SendLine(Token);
    CHECK(second.WaitForClose());
    CHECK(bridge.Connected());
    CHECK(connects == 1);
    CHECK(bridge.Send("still the first"));
    CHECK(first.ReadLine() == "still the first");
}

TEST_CASE_FIXTURE(Bridge, "a silent connection does not keep the real one out") {
    TestClient squatter(port);
    std::this_thread::sleep_for(std::chrono::milliseconds(300)); // accepted, token never sent

    TestClient real(port);
    real.SendLine(Token);
    CHECK(Eventually([&] { return bridge.Connected(); }));
    CHECK(squatter.WaitForClose());
}

TEST_CASE_FIXTURE(Bridge, "after a disconnect the mod can connect again") {
    {
        TestClient first(port);
        first.SendLine(Token);
        REQUIRE(Eventually([&] { return bridge.Connected(); }));
    }
    REQUIRE(Eventually([&] { return disconnects == 1; }));
    CHECK_FALSE(bridge.Connected());
    CHECK_FALSE(bridge.Send("nobody home"));

    TestClient again(port);
    again.SendLine(Token);
    REQUIRE(Eventually([&] { return bridge.Connected(); }));
    CHECK(connects == 2);
    CHECK(disconnects == 1);
    CHECK(bridge.Send("hello again"));
    CHECK(again.ReadLine() == "hello again");
}

TEST_CASE_FIXTURE(Bridge, "a line over the limit closes the connection, lines under it pass") {
    TestClient client(port);
    client.SendLine(Token);
    REQUIRE(Eventually([&] { return bridge.Connected(); }));

    std::string big(GameBridge::MaxLineBytes - 10, 'x');
    client.SendLine(big);
    REQUIRE(Eventually([&] { return Lines().size() == 1; }, 5000));
    CHECK(Lines()[0].size() == big.size());

    client.SendLine(std::string(GameBridge::MaxLineBytes + 100, 'y'));
    CHECK(client.WaitForClose(5000));
    CHECK(Eventually([&] { return disconnects == 1; }));
    CHECK(Lines().size() == 1);
}

TEST_CASE_FIXTURE(Bridge, "Send refuses what the other side could not read") {
    TestClient client(port);
    client.SendLine(Token);
    REQUIRE(Eventually([&] { return bridge.Connected(); }));
    CHECK_FALSE(bridge.Send(""));
    CHECK_FALSE(bridge.Send("two\nlines"));
    CHECK_FALSE(bridge.Send(std::string(GameBridge::MaxLineBytes + 1, 'z')));
    CHECK(bridge.Send(std::string(GameBridge::MaxLineBytes, 'z')));
}

TEST_CASE_FIXTURE(Bridge, "stopping closes the connection and the bridge refuses new ones") {
    TestClient client(port);
    client.SendLine(Token);
    REQUIRE(Eventually([&] { return bridge.Connected(); }));
    bridge.Stop();
    CHECK(client.WaitForClose());
    CHECK_FALSE(bridge.Connected());
    CHECK_FALSE(bridge.Send("late"));
    TestClient late(port);
    CHECK_FALSE(late.Connected());
}
