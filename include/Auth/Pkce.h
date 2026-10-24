/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include <cstddef>
#include <string>

namespace Auth {

/// RFC 4648 section 5, without padding.
std::string Base64Url(const unsigned char* data, size_t size);

/// `bytes` bytes from the system's secure random source, base64url encoded. Throws if there is none.
std::string RandomToken(size_t bytes = 32);

/// RFC 7636 S256: base64url(SHA-256(verifier)).
std::string ChallengeFor(const std::string& verifier);

struct Pkce {
    std::string verifier;
    std::string challenge;
};

/// A fresh verifier (43 characters) and its challenge.
Pkce GeneratePkce();

/// Compares in time independent of where the strings differ.
bool SafeEqual(const std::string& a, const std::string& b);

}
