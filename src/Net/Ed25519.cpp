/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Net/Ed25519.h"

#include "Net/Base64.h"
#include <memory>
#include <openssl/evp.h>

namespace Net {

namespace {
    struct KeyDeleter {
        void operator()(EVP_PKEY* key) const { EVP_PKEY_free(key); }
    };
    using Key = std::unique_ptr<EVP_PKEY, KeyDeleter>;

    struct ContextDeleter {
        void operator()(EVP_MD_CTX* context) const { EVP_MD_CTX_free(context); }
    };
    using Context = std::unique_ptr<EVP_MD_CTX, ContextDeleter>;

    constexpr size_t KeyBytes = 32;
    constexpr size_t SignatureBytes = 64;
}

bool Ed25519Verify(const std::string& publicKey, const std::string& message, const std::string& signature) {
    if (publicKey.size() != KeyBytes || signature.size() != SignatureBytes) {
        return false;
    }
    Key key(EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr, reinterpret_cast<const unsigned char*>(publicKey.data()), publicKey.size()));
    Context context(EVP_MD_CTX_new());
    if (!key || !context) {
        return false;
    }
    if (EVP_DigestVerifyInit(context.get(), nullptr, nullptr, nullptr, key.get()) != 1) {
        return false;
    }
    return EVP_DigestVerify(context.get(), reinterpret_cast<const unsigned char*>(signature.data()), signature.size(),
               reinterpret_cast<const unsigned char*>(message.data()), message.size())
        == 1;
}

std::vector<std::string> ParseTrustedKeys(const std::string& text, std::string& error) {
    std::vector<std::string> keys;
    size_t start = 0;
    int lineNumber = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        std::string line = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        start = end == std::string::npos ? text.size() + 1 : end + 1;
        lineNumber++;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
            line.pop_back();
        }
        size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line[first] == '#') {
            continue;
        }
        line = line.substr(first);
        static const std::string Prefix = "ed25519:";
        if (line.rfind(Prefix, 0) != 0) {
            error = "Line " + std::to_string(lineNumber) + " is not an ed25519 key";
            return {};
        }
        auto raw = Base64Decode(line.substr(Prefix.size()));
        if (!raw || raw->size() != KeyBytes) {
            error = "Line " + std::to_string(lineNumber) + " is not a 32 byte key in Base64";
            return {};
        }
        keys.push_back(*raw);
    }
    return keys;
}

std::optional<std::string> Ed25519PublicKey(const std::string& seed) {
    if (seed.size() != KeyBytes) {
        return std::nullopt;
    }
    Key key(EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr, reinterpret_cast<const unsigned char*>(seed.data()), seed.size()));
    if (!key) {
        return std::nullopt;
    }
    size_t length = KeyBytes;
    std::string publicKey(KeyBytes, '\0');
    if (EVP_PKEY_get_raw_public_key(key.get(), reinterpret_cast<unsigned char*>(publicKey.data()), &length) != 1 || length != KeyBytes) {
        return std::nullopt;
    }
    return publicKey;
}

std::optional<std::string> Ed25519Sign(const std::string& seed, const std::string& message) {
    if (seed.size() != KeyBytes) {
        return std::nullopt;
    }
    Key key(EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr, reinterpret_cast<const unsigned char*>(seed.data()), seed.size()));
    Context context(EVP_MD_CTX_new());
    if (!key || !context || EVP_DigestSignInit(context.get(), nullptr, nullptr, nullptr, key.get()) != 1) {
        return std::nullopt;
    }
    size_t length = SignatureBytes;
    std::string signature(SignatureBytes, '\0');
    if (EVP_DigestSign(context.get(), reinterpret_cast<unsigned char*>(signature.data()), &length, reinterpret_cast<const unsigned char*>(message.data()), message.size()) != 1 || length != SignatureBytes) {
        return std::nullopt;
    }
    return signature;
}

}
