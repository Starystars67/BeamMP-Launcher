/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include "Games/RelayProtocol.h"
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

namespace Games {

/// Game data a mod hands the engine to send to the other players of the room.
struct OutgoingData {
    Relay::DataKind kind = Relay::DataKind::ToHost;
    /// Only meaningful for ToPeer.
    uint16_t target = 0;
    std::string bytes;
};

/// The engine does not look inside: what the bytes mean is between the mods of one game. They travel as Base64
/// in the mod's JSON lines.
///
///   mod -> engine   {"type":"data","kind":"host"|"all"|"peer"|"others","target":<peer id, for "peer">,"data":"<base64>"}
///   engine -> mod   {"type":"data","from":<peer id>,"kind":<as sent>,"data":"<base64>"}
///
/// Nothing for a message that is not data, or is malformed (unknown kind, a peer without a valid target,
/// data that is not Base64).
std::optional<OutgoingData> ParseOutgoingData(const nlohmann::json& message);

/// The message for the mod about a frame that arrived from another player.
nlohmann::json MakeIncomingData(const Relay::DataFrame& frame);

/// True for a message of type "data" (valid or not), so it is not passed on to a user interface.
bool IsDataMessage(const nlohmann::json& message);

}
