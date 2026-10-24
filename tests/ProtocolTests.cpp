/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Control/Protocol.h"
#include <doctest/doctest.h>

using namespace Control;
using nlohmann::json;

namespace {
json Valid() {
    return { { "v", 1 }, { "id", 7 }, { "type", "request" }, { "name", "game.launch" }, { "data", { { "game", "beamng" } } } };
}

bool Rejected(const json& message) {
    return !ParseRequest(message.dump()).has_value();
}
}

TEST_CASE("a well-formed request is parsed") {
    auto request = ParseRequest(Valid().dump());
    REQUIRE(request);
    CHECK(request->id == 7);
    CHECK(request->name == "game.launch");
    CHECK(request->data["game"] == "beamng");
}

TEST_CASE("data is optional and unknown fields are ignored") {
    json message = Valid();
    message.erase("data");
    message["future"] = true;
    auto request = ParseRequest(message.dump());
    REQUIRE(request);
    CHECK(request->data.is_object());
    CHECK(request->data.empty());
}

TEST_CASE("malformed requests are rejected") {
    CHECK_FALSE(ParseRequest("").has_value());
    CHECK_FALSE(ParseRequest("not json").has_value());
    CHECK_FALSE(ParseRequest("[1,2]").has_value());
    CHECK_FALSE(ParseRequest("{\"v\":1").has_value());

    for (const char* field : { "v", "id", "type", "name" }) {
        json message = Valid();
        message.erase(field);
        CHECK_MESSAGE(Rejected(message), "missing ", field);
    }
    json wrongVersion = Valid();
    wrongVersion["v"] = 2;
    CHECK(Rejected(wrongVersion));
    json wrongType = Valid();
    wrongType["type"] = "event";
    CHECK(Rejected(wrongType));
    json textId = Valid();
    textId["id"] = "7";
    CHECK(Rejected(textId));
    json fractionalId = Valid();
    fractionalId["id"] = 7.5;
    CHECK(Rejected(fractionalId));
    json numberName = Valid();
    numberName["name"] = 5;
    CHECK(Rejected(numberName));
    json emptyName = Valid();
    emptyName["name"] = "";
    CHECK(Rejected(emptyName));
    json longName = Valid();
    longName["name"] = std::string(65, 'a');
    CHECK(Rejected(longName));
    json arrayData = Valid();
    arrayData["data"] = json::array();
    CHECK(Rejected(arrayData));
}

TEST_CASE("hostile input is rejected without recursing") {
    std::string deep(100000, '[');
    CHECK_FALSE(ParseRequest(deep).has_value());
    CHECK_FALSE(ParseRequest(std::string(MaxLineBytes + 1, ' ')).has_value());

    json nested = Valid();
    nested["data"] = { { "a", std::string(40, '[') + std::string(40, ']') } };
    CHECK(ParseRequest(nested.dump()).has_value()); // brackets inside a string are not nesting
}

TEST_CASE("responses and events have the documented shape") {
    json ok = json::parse(MakeResponse(3, { { "x", 1 } }));
    CHECK(ok == json { { "v", 1 }, { "type", "response" }, { "id", 3 }, { "ok", true }, { "data", { { "x", 1 } } } });

    json failure = json::parse(MakeError(3, "game_not_ready", "later"));
    CHECK(failure["ok"] == false);
    CHECK(failure["id"] == 3);
    CHECK(failure["error"] == json { { "code", "game_not_ready" }, { "message", "later" } });

    json anonymous = json::parse(MakeError(std::nullopt, "bad_request", "no"));
    CHECK(anonymous["id"].is_null());

    json event = json::parse(MakeEvent("game.state", { { "state", "ready" } }));
    CHECK(event == json { { "v", 1 }, { "type", "event" }, { "name", "game.state" }, { "data", { { "state", "ready" } } } });
}

TEST_CASE("messages never contain a raw newline") {
    json event = json::parse(MakeEvent("log", { { "message", "line1\nline2\r\n" } }));
    CHECK(event["data"]["message"] == "line1\nline2\r\n");
    CHECK(MakeEvent("log", { { "message", "a\nb" } }).find('\n') == std::string::npos);
}

TEST_CASE("tokens are compared exactly") {
    CHECK(TokenMatches("secret-token-123456", "secret-token-123456"));
    CHECK_FALSE(TokenMatches("secret-token-123456", "secret-token-123457"));
    CHECK_FALSE(TokenMatches("secret-token-123456", "secret-token-12345"));
    CHECK_FALSE(TokenMatches("secret-token-123456", ""));
    CHECK(TokenMatches("", ""));
}

TEST_CASE("version negotiation picks a shared version") {
    CHECK(NegotiateVersion({ 1 }) == 1);
    CHECK(NegotiateVersion({ 3, 2, 1 }) == 1);
    CHECK_FALSE(NegotiateVersion({ 2, 3 }).has_value());
    CHECK_FALSE(NegotiateVersion(json::array()).has_value());
    CHECK_FALSE(NegotiateVersion(json("1")).has_value());
    CHECK_FALSE(NegotiateVersion(json()).has_value());
    CHECK_FALSE(NegotiateVersion({ "1" }).has_value());
}

TEST_CASE("timestamps are ISO 8601 UTC with milliseconds") {
    using namespace std::chrono;
    CHECK(FormatTimestamp(system_clock::time_point()) == "1970-01-01T00:00:00.000Z");
    CHECK(FormatTimestamp(system_clock::time_point(milliseconds(1700000000123))) == "2023-11-14T22:13:20.123Z");
    CHECK(FormatTimestamp(system_clock::time_point(milliseconds(-1))) == "1969-12-31T23:59:59.999Z");
}
