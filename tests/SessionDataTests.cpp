/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Games/SessionData.h"
#include <doctest/doctest.h>

using namespace Games;
using nlohmann::json;

TEST_CASE("a mod's data message is parsed for each kind") {
    auto host = ParseOutgoingData({ { "type", "data" }, { "kind", "host" }, { "data", "Zm9v" } });
    REQUIRE(host);
    CHECK(host->kind == Relay::DataKind::ToHost);
    CHECK(host->bytes == "foo");

    auto all = ParseOutgoingData({ { "type", "data" }, { "kind", "all" }, { "data", "" } });
    REQUIRE(all);
    CHECK(all->kind == Relay::DataKind::ToAll);
    CHECK(all->bytes.empty());

    auto others = ParseOutgoingData({ { "type", "data" }, { "kind", "others" }, { "data", "Zg==" } });
    REQUIRE(others);
    CHECK(others->kind == Relay::DataKind::ToOthers);

    auto peer = ParseOutgoingData({ { "type", "data" }, { "kind", "peer" }, { "target", 7 }, { "data", "Zg==" } });
    REQUIRE(peer);
    CHECK(peer->kind == Relay::DataKind::ToPeer);
    CHECK(peer->target == 7);
}

TEST_CASE("malformed data messages are refused") {
    CHECK_FALSE(ParseOutgoingData({ { "type", "session" } }));
    CHECK_FALSE(ParseOutgoingData(json::array()));
    CHECK_FALSE(ParseOutgoingData({ { "type", "data" }, { "data", "Zg==" } }));
    CHECK_FALSE(ParseOutgoingData({ { "type", "data" }, { "kind", "host" } }));
    CHECK_FALSE(ParseOutgoingData({ { "type", "data" }, { "kind", 3 }, { "data", "Zg==" } }));
    CHECK_FALSE(ParseOutgoingData({ { "type", "data" }, { "kind", "everyone" }, { "data", "Zg==" } }));
    CHECK_FALSE(ParseOutgoingData({ { "type", "data" }, { "kind", "host" }, { "data", "not base64!" } }));
    CHECK_FALSE(ParseOutgoingData({ { "type", "data" }, { "kind", "host" }, { "data", 5 } }));
}

TEST_CASE("a peer needs a valid target") {
    auto make = [](json target) {
        json message = { { "type", "data" }, { "kind", "peer" }, { "data", "Zg==" } };
        if (!target.is_null()) {
            message["target"] = target;
        }
        return ParseOutgoingData(message);
    };
    CHECK_FALSE(make(nullptr));
    CHECK_FALSE(make(0));
    CHECK_FALSE(make(-1));
    CHECK_FALSE(make(65536));
    CHECK_FALSE(make("2"));
    CHECK_FALSE(make(2.5));
    CHECK(make(1));
    CHECK(make(65535));
}

TEST_CASE("data from another player is handed to the mod") {
    Relay::DataFrame frame;
    frame.sender = 3;
    frame.kind = Relay::DataKind::ToAll;
    frame.bytes = std::string("\x00\xff\x01", 3);
    json message = MakeIncomingData(frame);
    CHECK(message["type"] == "data");
    CHECK(message["from"] == 3);
    CHECK(message["kind"] == "all");
    CHECK(message["data"] == "AP8B");
}

TEST_CASE("what the engine hands out can be sent back in") {
    for (auto kind : { Relay::DataKind::ToHost, Relay::DataKind::ToAll, Relay::DataKind::ToOthers }) {
        Relay::DataFrame frame;
        frame.sender = 2;
        frame.kind = kind;
        frame.bytes = std::string("\x00\x01\xfe\xff", 4);
        auto parsed = ParseOutgoingData(MakeIncomingData(frame));
        REQUIRE(parsed);
        CHECK(parsed->kind == kind);
        CHECK(parsed->bytes == frame.bytes);
    }
}

TEST_CASE("only messages of type data are data messages") {
    CHECK(IsDataMessage({ { "type", "data" } }));
    CHECK(IsDataMessage({ { "type", "data" }, { "kind", "bogus" } }));
    CHECK_FALSE(IsDataMessage({ { "type", "hello" } }));
    CHECK_FALSE(IsDataMessage({ { "type", 1 } }));
    CHECK_FALSE(IsDataMessage({ { "kind", "host" } }));
    CHECK_FALSE(IsDataMessage(json::array()));
    CHECK_FALSE(IsDataMessage(json("data")));
}
