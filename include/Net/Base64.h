/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include <optional>
#include <string>

namespace Net {

/// Standard Base64 with padding (RFC 4648), the form the game's mod uses for binary data in text lines.
std::string Base64Encode(const std::string& bytes);

/// Nothing for anything that is not valid Base64 (wrong characters, wrong padding, wrong length).
std::optional<std::string> Base64Decode(const std::string& text);

}
