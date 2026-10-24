/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Games/RelayProtocol.h"
#include <doctest/doctest.h>

using namespace Relay;

namespace {
std::string Bytes(std::initializer_list<int> values) {
    std::string result;
    for (int value : values) {
        result.push_back(static_cast<char>(value));
    }
    return result;
}

std::vector<Frame> PushAll(FrameDecoder& decoder, const std::string& bytes) {
    return decoder.Push(bytes.data(), bytes.size());
}
}

// The same bytes as relay/test/protocol.test.ts expects, so the two sides cannot drift apart unnoticed.
TEST_CASE("a frame is its length, its type and its payload, byte for byte") {
    CHECK(EncodeFrame(0x42, "abc") == Bytes({ 0, 0, 0, 4, 0x42, 0x61, 0x62, 0x63 }));
    CHECK(EncodeFrame(0x20) == Bytes({ 0, 0, 0, 1, 0x20 }));
}

TEST_CASE("data frames carry kind and target going in") {
    CHECK(EncodeData(DataKind::ToPeer, 0x0102, Bytes({ 9, 8, 7 })) == Bytes({ 0, 0, 0, 7, 0x10, 2, 1, 2, 9, 8, 7 }));
    CHECK(EncodeData(DataKind::ToHost, 0, "") == Bytes({ 0, 0, 0, 4, 0x10, 0, 0, 0 }));
}

TEST_CASE("data frames from the relay have a sender, a kind and the bytes") {
    auto frame = DecodeData(Bytes({ 3, 4, 1, 'h', 'i' }));
    REQUIRE(frame);
    CHECK(frame->sender == 0x0304);
    CHECK(frame->kind == DataKind::ToAll);
    CHECK(frame->bytes == "hi");
    CHECK(DecodeData(Bytes({ 3, 4 })) == std::nullopt);
    auto empty = DecodeData(Bytes({ 0, 1, 3 }));
    REQUIRE(empty);
    CHECK(empty->bytes.empty());
}

TEST_CASE("binary payloads survive, zero bytes included") {
    std::string payload = Bytes({ 0, 255, 0, 10, 13, 0 });
    auto decoded = DecodeData(Bytes({ 0, 2, 3 }) + payload);
    REQUIRE(decoded);
    CHECK(decoded->bytes == payload);
}

TEST_CASE("frames come out whole whatever way the bytes arrive") {
    std::string stream = EncodeFrame(1, "one") + EncodeFrame(2, "two") + EncodeFrame(3);
    FrameDecoder whole;
    auto frames = PushAll(whole, stream);
    REQUIRE(frames.size() == 3);
    CHECK(frames[0].type == 1);
    CHECK(frames[0].payload == "one");
    CHECK(frames[1].payload == "two");
    CHECK(frames[2].payload.empty());

    FrameDecoder byByte;
    std::vector<Frame> seen;
    for (char byte : stream) {
        for (Frame& frame : byByte.Push(&byte, 1)) {
            seen.push_back(std::move(frame));
        }
    }
    REQUIRE(seen.size() == 3);
    CHECK(seen[1].payload == "two");
    CHECK(byByte.Pending() == 0);
}

TEST_CASE("a half frame waits for the rest") {
    FrameDecoder decoder;
    std::string frame = EncodeFrame(7, "payload");
    CHECK(PushAll(decoder, frame.substr(0, 6)).empty());
    CHECK(decoder.Pending() == 6);
    auto rest = PushAll(decoder, frame.substr(6));
    REQUIRE(rest.size() == 1);
    CHECK(rest[0].payload == "payload");
}

TEST_CASE("lengths that cannot be right are refused") {
    FrameDecoder zero;
    CHECK_THROWS_AS(PushAll(zero, Bytes({ 0, 0, 0, 0 })), std::runtime_error);
    FrameDecoder huge;
    CHECK_THROWS_AS(PushAll(huge, Bytes({ 0xff, 0xff, 0xff, 0xff, 1 })), std::runtime_error);
    FrameDecoder justOver;
    CHECK_THROWS_AS(PushAll(justOver, Bytes({ 0, 0x40, 0, 1 })), std::runtime_error); // 4 MiB + 1
    FrameDecoder largest;
    CHECK_NOTHROW(PushAll(largest, Bytes({ 0, 0x40, 0, 0 }))); // 4 MiB: waits for the payload
}

TEST_CASE("a frame over the limit cannot be built") {
    CHECK_THROWS_AS(EncodeFrame(1, std::string(MaxFrameBytes, 'x')), std::length_error);
    CHECK_NOTHROW(EncodeFrame(1, std::string(MaxFrameBytes - 1, 'x')));
}

TEST_CASE("JSON payloads are objects, nothing else") {
    auto object = DecodeJsonObject("{\"a\":1}");
    REQUIRE(object);
    CHECK((*object)["a"] == 1);
    for (const char* bad : { "[1]", "\"text\"", "5", "null", "{oops", "" }) {
        CHECK_FALSE(DecodeJsonObject(bad));
    }
    CHECK(EncodeJson(FromRelay::Welcome, { { "v", 1 } }).substr(4, 1) == std::string(1, static_cast<char>(FromRelay::Welcome)));
}
