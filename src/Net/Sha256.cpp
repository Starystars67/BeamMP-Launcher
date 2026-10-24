/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Net/Sha256.h"

#include <fstream>
#include <memory>
#include <openssl/evp.h>

namespace Net {

namespace {
    struct ContextDeleter {
        void operator()(EVP_MD_CTX* context) const { EVP_MD_CTX_free(context); }
    };
    using Context = std::unique_ptr<EVP_MD_CTX, ContextDeleter>;

    std::string Hex(const unsigned char* digest, unsigned int length) {
        static const char Digits[] = "0123456789abcdef";
        std::string text;
        text.reserve(length * 2);
        for (unsigned int i = 0; i < length; i++) {
            text.push_back(Digits[digest[i] >> 4]);
            text.push_back(Digits[digest[i] & 15]);
        }
        return text;
    }
}

std::string Sha256Hex(const std::string& bytes) {
    Context context(EVP_MD_CTX_new());
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int length = 0;
    EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr);
    EVP_DigestUpdate(context.get(), bytes.data(), bytes.size());
    EVP_DigestFinal_ex(context.get(), digest, &length);
    return Hex(digest, length);
}

std::optional<std::string> Sha256FileHex(const std::filesystem::path& file) {
    std::ifstream stream(file, std::ios::binary);
    if (!stream) {
        return std::nullopt;
    }
    Context context(EVP_MD_CTX_new());
    EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr);
    char buffer[64 * 1024];
    while (stream.read(buffer, sizeof(buffer)) || stream.gcount() > 0) {
        EVP_DigestUpdate(context.get(), buffer, static_cast<size_t>(stream.gcount()));
    }
    if (stream.bad()) {
        return std::nullopt;
    }
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int length = 0;
    EVP_DigestFinal_ex(context.get(), digest, &length);
    return Hex(digest, length);
}

}
