/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Net/Base64.h"
#include <doctest/doctest.h>

using namespace Net;

// The vectors of RFC 4648 section 10.
TEST_CASE("Base64 encodes like RFC 4648") {
    CHECK(Base64Encode("") == "");
    CHECK(Base64Encode("f") == "Zg==");
    CHECK(Base64Encode("fo") == "Zm8=");
    CHECK(Base64Encode("foo") == "Zm9v");
    CHECK(Base64Encode("foob") == "Zm9vYg==");
    CHECK(Base64Encode("fooba") == "Zm9vYmE=");
    CHECK(Base64Encode("foobar") == "Zm9vYmFy");
}

TEST_CASE("Base64 decodes like RFC 4648") {
    CHECK(Base64Decode("") == std::string());
    CHECK(Base64Decode("Zg==") == std::string("f"));
    CHECK(Base64Decode("Zm8=") == std::string("fo"));
    CHECK(Base64Decode("Zm9v") == std::string("foo"));
    CHECK(Base64Decode("Zm9vYg==") == std::string("foob"));
    CHECK(Base64Decode("Zm9vYmE=") == std::string("fooba"));
    CHECK(Base64Decode("Zm9vYmFy") == std::string("foobar"));
}

TEST_CASE("every byte value and every length survives a round trip") {
    std::string all;
    for (int i = 0; i < 256; i++) {
        all.push_back(static_cast<char>(i));
    }
    CHECK(Base64Decode(Base64Encode(all)) == all);
    for (size_t length = 0; length <= all.size(); length++) {
        std::string part = all.substr(0, length);
        CHECK(Base64Decode(Base64Encode(part)) == part);
    }
    std::string zeros(1000, '\0');
    CHECK(Base64Decode(Base64Encode(zeros)) == zeros);
}

TEST_CASE("text that is not Base64 is refused") {
    for (const char* bad : { "Zg=", "Zg", "Z", "Zm9", "Zm9v=", "Zg=a", "Z===", "====", "Zm9v Zm9v", "Zm9v\n", "Zm!v", "Zm9v====", "Zg==Zg==", "Zh==", "Zm9=" }) {
        CHECK_FALSE(Base64Decode(bad).has_value());
    }
}

TEST_CASE("the padding only comes at the end") {
    CHECK_FALSE(Base64Decode("Zg==Zm9v").has_value());
    CHECK_FALSE(Base64Decode("Zm8=Zm9v").has_value());
}

TEST_CASE("a large payload is handled") {
    std::string big(3 * 1024 * 1024 + 1, 'x');
    for (size_t i = 0; i < big.size(); i++) {
        big[i] = static_cast<char>(i * 31);
    }
    CHECK(Base64Decode(Base64Encode(big)) == big);
}
