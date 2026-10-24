/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Games/RelayProtocol.h"

#include <stdexcept>

namespace Relay {

std::string EncodeFrame(uint8_t type, const std::string& payload) {
    size_t length = 1 + payload.size();
    if (length > MaxFrameBytes) {
        throw std::length_error("relay frame too large");
    }
    std::string frame;
    frame.reserve(4 + length);
    frame.push_back(static_cast<char>((length >> 24) & 0xff));
    frame.push_back(static_cast<char>((length >> 16) & 0xff));
    frame.push_back(static_cast<char>((length >> 8) & 0xff));
    frame.push_back(static_cast<char>(length & 0xff));
    frame.push_back(static_cast<char>(type));
    frame += payload;
    return frame;
}

std::string EncodeJson(uint8_t type, const nlohmann::json& value) {
    return EncodeFrame(type, value.dump());
}

std::string EncodeData(DataKind kind, uint16_t target, const std::string& bytes) {
    std::string body;
    body.reserve(3 + bytes.size());
    body.push_back(static_cast<char>(kind));
    body.push_back(static_cast<char>((target >> 8) & 0xff));
    body.push_back(static_cast<char>(target & 0xff));
    body += bytes;
    return EncodeFrame(ToRelay::Data, body);
}

std::optional<DataFrame> DecodeData(const std::string& payload) {
    if (payload.size() < 3) {
        return std::nullopt;
    }
    DataFrame frame;
    frame.sender = static_cast<uint16_t>((static_cast<uint8_t>(payload[0]) << 8) | static_cast<uint8_t>(payload[1]));
    frame.kind = static_cast<DataKind>(static_cast<uint8_t>(payload[2]));
    frame.bytes = payload.substr(3);
    return frame;
}

std::optional<nlohmann::json> DecodeJsonObject(const std::string& payload) {
    nlohmann::json value = nlohmann::json::parse(payload, nullptr, false);
    if (!value.is_object()) {
        return std::nullopt;
    }
    return value;
}

std::vector<Frame> FrameDecoder::Push(const char* data, size_t size) {
    m_buffer.append(data, size);
    std::vector<Frame> frames;
    size_t offset = 0;
    for (;;) {
        if (m_buffer.size() - offset < 4) {
            break;
        }
        size_t length = (static_cast<size_t>(static_cast<uint8_t>(m_buffer[offset])) << 24)
            | (static_cast<size_t>(static_cast<uint8_t>(m_buffer[offset + 1])) << 16)
            | (static_cast<size_t>(static_cast<uint8_t>(m_buffer[offset + 2])) << 8)
            | static_cast<size_t>(static_cast<uint8_t>(m_buffer[offset + 3]));
        if (length < 1 || length > MaxFrameBytes) {
            throw std::runtime_error("invalid relay frame length");
        }
        if (m_buffer.size() - offset < 4 + length) {
            break;
        }
        Frame frame;
        frame.type = static_cast<uint8_t>(m_buffer[offset + 4]);
        frame.payload = m_buffer.substr(offset + 5, length - 1);
        frames.push_back(std::move(frame));
        offset += 4 + length;
    }
    m_buffer.erase(0, offset);
    return frames;
}

}
