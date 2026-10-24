/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace Net {

/// Lower case hex of the SHA-256 of the bytes.
std::string Sha256Hex(const std::string& bytes);

/// The same for a file (read in pieces); nothing when it cannot be read.
std::optional<std::string> Sha256FileHex(const std::filesystem::path& file);

}
