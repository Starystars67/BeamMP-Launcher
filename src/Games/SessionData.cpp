/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Games/SessionData.h"

#include "Net/Base64.h"

namespace Games {

namespace {
    const char* KindName(Relay::DataKind kind) {
        switch (kind) {
        case Relay::DataKind::ToHost:
            return "host";
        case Relay::DataKind::ToAll:
            return "all";
        case Relay::DataKind::ToPeer:
            return "peer";
        case Relay::DataKind::ToOthers:
            return "others";
        }
        return "host";
    }
}

bool IsDataMessage(const nlohmann::json& message) {
    auto type = message.find("type");
    return message.is_object() && type != message.end() && type->is_string() && type->get<std::string>() == "data";
}

std::optional<OutgoingData> ParseOutgoingData(const nlohmann::json& message) {
    if (!IsDataMessage(message)) {
        return std::nullopt;
    }
    auto kind = message.find("kind");
    auto data = message.find("data");
    if (kind == message.end() || !kind->is_string() || data == message.end() || !data->is_string()) {
        return std::nullopt;
    }
    OutgoingData out;
    std::string name = kind->get<std::string>();
    if (name == "host") {
        out.kind = Relay::DataKind::ToHost;
    } else if (name == "all") {
        out.kind = Relay::DataKind::ToAll;
    } else if (name == "others") {
        out.kind = Relay::DataKind::ToOthers;
    } else if (name == "peer") {
        out.kind = Relay::DataKind::ToPeer;
        auto target = message.find("target");
        if (target == message.end() || !target->is_number_integer()) {
            return std::nullopt;
        }
        auto id = target->get<long long>();
        if (id < 1 || id > 0xffff) {
            return std::nullopt;
        }
        out.target = static_cast<uint16_t>(id);
    } else {
        return std::nullopt;
    }
    auto bytes = Net::Base64Decode(data->get<std::string>());
    if (!bytes) {
        return std::nullopt;
    }
    out.bytes = std::move(*bytes);
    return out;
}

nlohmann::json MakeIncomingData(const Relay::DataFrame& frame) {
    return {
        { "type", "data" },
        { "from", frame.sender },
        { "kind", KindName(frame.kind) },
        { "data", Net::Base64Encode(frame.bytes) },
    };
}

}
