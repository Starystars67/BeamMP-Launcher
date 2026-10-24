/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

// The BeamMP relay's wire protocol, version 1 (see relay/README.md and relay/src/protocol.ts, the two have to agree).
// Frames: u32 big endian length (type byte + payload) | u8 type | payload.

#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace Relay {

constexpr int ProtocolVersion = 1;
constexpr size_t MaxFrameBytes = 4 * 1024 * 1024;
constexpr uint16_t HostPeerId = 1;

namespace ToRelay {
    constexpr uint8_t Hello = 0x01;
    constexpr uint8_t CreateRoom = 0x02;
    constexpr uint8_t JoinRoom = 0x03;
    constexpr uint8_t LeaveRoom = 0x04;
    constexpr uint8_t Data = 0x10;
    constexpr uint8_t Ping = 0x20;
}

namespace FromRelay {
    constexpr uint8_t Welcome = 0x81;
    constexpr uint8_t RoomCreated = 0x82;
    constexpr uint8_t RoomJoined = 0x83;
    constexpr uint8_t PeerJoined = 0x84;
    constexpr uint8_t PeerLeft = 0x85;
    constexpr uint8_t RoomClosed = 0x86;
    constexpr uint8_t Data = 0x90;
    constexpr uint8_t Pong = 0xa0;
    constexpr uint8_t Error = 0xe0;
}

enum class DataKind : uint8_t {
    ToHost = 0,
    ToAll = 1,
    ToPeer = 2,
    ToOthers = 3,
};

struct Frame {
    uint8_t type = 0;
    std::string payload;
};

/// Throws std::length_error for a frame that is over the limit.
std::string EncodeFrame(uint8_t type, const std::string& payload = {});
std::string EncodeJson(uint8_t type, const nlohmann::json& value);
std::string EncodeData(DataKind kind, uint16_t target, const std::string& bytes);

/// `u16 sender | u8 kind | bytes`.
struct DataFrame {
    uint16_t sender = 0;
    DataKind kind = DataKind::ToHost;
    std::string bytes;
};
std::optional<DataFrame> DecodeData(const std::string& payload);

/// A JSON object, or nothing for anything else.
std::optional<nlohmann::json> DecodeJsonObject(const std::string& payload);

/// Cuts a byte stream into frames. Throws std::runtime_error for a length that cannot be right.
class FrameDecoder {
public:
    std::vector<Frame> Push(const char* data, size_t size);
    size_t Pending() const { return m_buffer.size(); }

private:
    std::string m_buffer;
};

}
