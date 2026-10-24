/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include <optional>
#include <string>
#include <vector>

namespace Net {

/// Whether `signature` (64 bytes) is a valid Ed25519 signature of `message` under `publicKey` (32 bytes).
/// False for anything of the wrong size.
bool Ed25519Verify(const std::string& publicKey, const std::string& message, const std::string& signature);

/// The files that say whom to trust, one key per line: `ed25519:<base64 of the 32 byte public key>`, blank lines and
/// lines that start with `#` ignored. Returns the raw keys; `error` says what was wrong with the first bad line.
std::vector<std::string> ParseTrustedKeys(const std::string& text, std::string& error);

/// For tests and for tools that publish: the public key of a 32 byte private seed, and a signature with it.
std::optional<std::string> Ed25519PublicKey(const std::string& seed);
std::optional<std::string> Ed25519Sign(const std::string& seed, const std::string& message);

}
