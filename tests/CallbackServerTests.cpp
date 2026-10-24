/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Auth/CallbackServer.h"
#include "AuthSupport.h"
#include <chrono>
#include <doctest/doctest.h>
#include <thread>

using namespace Auth;
using namespace std::chrono_literals;

namespace {
const std::atomic<bool> NotCancelled = false;

struct Running {
    CallbackServer server { "the-state" };
    uint16_t port = server.Start();
};
}

TEST_CASE("the server listens on a free loopback port") {
    CallbackServer first("a");
    CallbackServer second("b");
    uint16_t p1 = first.Start();
    uint16_t p2 = second.Start();
    CHECK(p1 != 0);
    CHECK(p2 != 0);
    CHECK(p1 != p2);
    CHECK(RawGet(p1, "/callback?state=a&code=x").status == 200);
}

TEST_CASE_FIXTURE(Running, "a callback with the right state delivers the code") {
    auto response = RawGet(port, "/callback?code=abc123&state=the-state");
    CHECK(response.status == 200);
    CHECK(response.body.find("You are signed in") != std::string::npos);
    auto result = server.Wait(1s, NotCancelled);
    REQUIRE(result);
    CHECK(result->code == "abc123");
    CHECK(result->error.empty());
}

TEST_CASE_FIXTURE(Running, "an error callback is reported, not turned into a code") {
    auto response = RawGet(port, "/callback?error=access_denied&state=the-state");
    CHECK(response.status == 200);
    CHECK(response.body.find("not completed") != std::string::npos);
    auto result = server.Wait(1s, NotCancelled);
    REQUIRE(result);
    CHECK(result->error == "access_denied");
    CHECK(result->code.empty());
}

TEST_CASE_FIXTURE(Running, "a wrong or missing state is refused and does not end the wait") {
    CHECK(RawGet(port, "/callback?code=evil&state=other").status == 400);
    CHECK(RawGet(port, "/callback?code=evil").status == 400);
    CHECK(RawGet(port, "/callback?code=evil&state=").status == 400);
    CHECK(RawGet(port, "/callback?code=evil&state=the-statee").status == 400);
    CHECK_FALSE(server.Wait(150ms, NotCancelled).has_value());

    CHECK(RawGet(port, "/callback?code=good&state=the-state").status == 200);
    auto result = server.Wait(1s, NotCancelled);
    REQUIRE(result);
    CHECK(result->code == "good");
}

TEST_CASE_FIXTURE(Running, "a request that names another host is refused (DNS rebinding)") {
    CHECK(RawGet(port, "/callback?code=x&state=the-state", "evil.example").status == 400);
    CHECK(RawGet(port, "/callback?code=x&state=the-state", "localhost:" + std::to_string(port)).status == 400);
    CHECK(RawGet(port, "/callback?code=x&state=the-state", "127.0.0.1:1").status == 400);
    CHECK_FALSE(server.Wait(100ms, NotCancelled).has_value());
}

TEST_CASE_FIXTURE(Running, "only the first valid callback counts") {
    CHECK(RawGet(port, "/callback?code=first&state=the-state").status == 200);
    CHECK(RawGet(port, "/callback?code=second&state=the-state").status == 400);
    CHECK(server.Wait(1s, NotCancelled)->code == "first");
}

TEST_CASE_FIXTURE(Running, "a callback without a code or error is refused") {
    CHECK(RawGet(port, "/callback?state=the-state").status == 400);
    CHECK(RawGet(port, "/callback?state=the-state&code=").status == 400);
    CHECK_FALSE(server.Wait(100ms, NotCancelled).has_value());
}

TEST_CASE_FIXTURE(Running, "other paths are not served") {
    CHECK(RawGet(port, "/").status == 404);
    CHECK(RawGet(port, "/callback/extra?code=x&state=the-state").status == 404);
    CHECK(RawGet(port, "/admin").status == 404);
}

TEST_CASE_FIXTURE(Running, "nothing from the request is reflected into the page") {
    auto response = RawGet(port, "/callback?error=%3Cscript%3Ealert(1)%3C/script%3E&state=the-state");
    CHECK(response.body.find("script") == std::string::npos);
    CHECK(response.body.find("alert") == std::string::npos);
}

TEST_CASE_FIXTURE(Running, "waiting ends on timeout and on cancellation") {
    auto started = std::chrono::steady_clock::now();
    CHECK_FALSE(server.Wait(200ms, NotCancelled).has_value());
    CHECK(std::chrono::steady_clock::now() - started >= 190ms);

    std::atomic<bool> cancelled = false;
    std::thread canceller([&] {
        std::this_thread::sleep_for(100ms);
        cancelled = true;
    });
    started = std::chrono::steady_clock::now();
    CHECK_FALSE(server.Wait(30s, cancelled).has_value());
    CHECK(std::chrono::steady_clock::now() - started < 5s);
    canceller.join();
}

TEST_CASE("after Stop nothing listens any more, and stopping twice is fine") {
    CallbackServer server("s");
    uint16_t port = server.Start();
    CHECK(RawGet(port, "/callback?code=x&state=s").status == 200);
    server.Stop();
    server.Stop();
    CHECK(RawGet(port, "/callback?code=x&state=s").status == 0);
}

TEST_CASE("starting twice is a programming error") {
    CallbackServer server("s");
    server.Start();
    CHECK_THROWS_AS(server.Start(), std::logic_error);
}
