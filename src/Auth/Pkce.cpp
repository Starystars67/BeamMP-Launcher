/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Auth/Pkce.h"

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <stdexcept>
#include <vector>

namespace Auth {

std::string Base64Url(const unsigned char* data, size_t size) {
    static const char Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string result;
    result.reserve((size * 4 + 2) / 3);
    size_t i = 0;
    for (; i + 2 < size; i += 3) {
        unsigned value = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
        result += Alphabet[(value >> 18) & 63];
        result += Alphabet[(value >> 12) & 63];
        result += Alphabet[(value >> 6) & 63];
        result += Alphabet[value & 63];
    }
    if (i + 1 == size) {
        unsigned value = data[i] << 16;
        result += Alphabet[(value >> 18) & 63];
        result += Alphabet[(value >> 12) & 63];
    } else if (i + 2 == size) {
        unsigned value = (data[i] << 16) | (data[i + 1] << 8);
        result += Alphabet[(value >> 18) & 63];
        result += Alphabet[(value >> 12) & 63];
        result += Alphabet[(value >> 6) & 63];
    }
    return result;
}

std::string RandomToken(size_t bytes) {
    std::vector<unsigned char> buffer(bytes);
    if (RAND_bytes(buffer.data(), static_cast<int>(buffer.size())) != 1) {
        throw std::runtime_error("No secure random source available");
    }
    return Base64Url(buffer.data(), buffer.size());
}

std::string ChallengeFor(const std::string& verifier) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int length = 0;
    if (EVP_Digest(verifier.data(), verifier.size(), digest, &length, EVP_sha256(), nullptr) != 1) {
        throw std::runtime_error("SHA-256 failed");
    }
    return Base64Url(digest, length);
}

Pkce GeneratePkce() {
    Pkce pkce;
    pkce.verifier = RandomToken(32);
    pkce.challenge = ChallengeFor(pkce.verifier);
    return pkce;
}

bool SafeEqual(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) {
        return false;
    }
    unsigned char difference = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        difference |= static_cast<unsigned char>(a[i] ^ b[i]);
    }
    return difference == 0;
}

}
