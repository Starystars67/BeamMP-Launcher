/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Net/Base64.h"

namespace Net {

namespace {
    constexpr char Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    int ValueOf(char c) {
        if (c >= 'A' && c <= 'Z') {
            return c - 'A';
        }
        if (c >= 'a' && c <= 'z') {
            return c - 'a' + 26;
        }
        if (c >= '0' && c <= '9') {
            return c - '0' + 52;
        }
        if (c == '+') {
            return 62;
        }
        if (c == '/') {
            return 63;
        }
        return -1;
    }
}

std::string Base64Encode(const std::string& bytes) {
    std::string text;
    text.reserve((bytes.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < bytes.size(); i += 3) {
        unsigned value = (static_cast<unsigned char>(bytes[i]) << 16) | (static_cast<unsigned char>(bytes[i + 1]) << 8) | static_cast<unsigned char>(bytes[i + 2]);
        text.push_back(Alphabet[(value >> 18) & 63]);
        text.push_back(Alphabet[(value >> 12) & 63]);
        text.push_back(Alphabet[(value >> 6) & 63]);
        text.push_back(Alphabet[value & 63]);
    }
    if (i + 1 == bytes.size()) {
        unsigned value = static_cast<unsigned char>(bytes[i]) << 16;
        text.push_back(Alphabet[(value >> 18) & 63]);
        text.push_back(Alphabet[(value >> 12) & 63]);
        text += "==";
    } else if (i + 2 == bytes.size()) {
        unsigned value = (static_cast<unsigned char>(bytes[i]) << 16) | (static_cast<unsigned char>(bytes[i + 1]) << 8);
        text.push_back(Alphabet[(value >> 18) & 63]);
        text.push_back(Alphabet[(value >> 12) & 63]);
        text.push_back(Alphabet[(value >> 6) & 63]);
        text.push_back('=');
    }
    return text;
}

std::optional<std::string> Base64Decode(const std::string& text) {
    if (text.size() % 4 != 0) {
        return std::nullopt;
    }
    std::string bytes;
    bytes.reserve(text.size() / 4 * 3);
    for (size_t i = 0; i < text.size(); i += 4) {
        bool last = i + 4 == text.size();
        int a = ValueOf(text[i]);
        int b = ValueOf(text[i + 1]);
        if (a < 0 || b < 0) {
            return std::nullopt;
        }
        bool padTwo = text[i + 2] == '=' && text[i + 3] == '=';
        bool padOne = text[i + 3] == '=' && text[i + 2] != '=';
        if ((padTwo || padOne) && !last) {
            return std::nullopt;
        }
        int c = padTwo ? 0 : ValueOf(text[i + 2]);
        int d = (padTwo || padOne) ? 0 : ValueOf(text[i + 3]);
        if (c < 0 || d < 0) {
            return std::nullopt;
        }
        // the unused bits must be zero, or the text is not what an encoder would have written
        if (padTwo && (b & 0x0f) != 0) {
            return std::nullopt;
        }
        if (padOne && (c & 0x03) != 0) {
            return std::nullopt;
        }
        unsigned value = (a << 18) | (b << 12) | (c << 6) | d;
        bytes.push_back(static_cast<char>((value >> 16) & 0xff));
        if (!padTwo) {
            bytes.push_back(static_cast<char>((value >> 8) & 0xff));
        }
        if (!padTwo && !padOne) {
            bytes.push_back(static_cast<char>(value & 0xff));
        }
    }
    return bytes;
}

}
