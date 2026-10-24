/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Net/Base64.h"
#include "Net/Ed25519.h"
#include <doctest/doctest.h>

using namespace Net;

namespace {
std::string FromHex(const std::string& hex) {
    std::string bytes;
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        bytes.push_back(static_cast<char>(std::stoi(hex.substr(i, 2), nullptr, 16)));
    }
    return bytes;
}

// RFC 8032, section 7.1
const std::string Seed1 = FromHex("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60");
const std::string Public1 = FromHex("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
const std::string Signature1 = FromHex("e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b");
const std::string Seed2 = FromHex("4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb");
const std::string Public2 = FromHex("3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c");
const std::string Signature2 = FromHex("92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00");
}

TEST_CASE("the RFC 8032 signatures verify") {
    CHECK(Ed25519Verify(Public1, "", Signature1));
    CHECK(Ed25519Verify(Public2, "r", Signature2));
}

TEST_CASE("signing gives the RFC's keys and signatures") {
    CHECK(Ed25519PublicKey(Seed1) == Public1);
    CHECK(Ed25519PublicKey(Seed2) == Public2);
    CHECK(Ed25519Sign(Seed1, "") == Signature1);
    CHECK(Ed25519Sign(Seed2, "r") == Signature2);
    CHECK_FALSE(Ed25519PublicKey("short"));
    CHECK_FALSE(Ed25519Sign("short", "x"));
}

TEST_CASE("anything that is not exactly what was signed fails") {
    CHECK_FALSE(Ed25519Verify(Public1, "x", Signature1));
    CHECK_FALSE(Ed25519Verify(Public2, "r", Signature1));
    CHECK_FALSE(Ed25519Verify(Public1, "", Signature2));
    std::string flipped = Signature2;
    flipped[10] ^= 1;
    CHECK_FALSE(Ed25519Verify(Public2, "r", flipped));
    std::string otherKey = Public2;
    otherKey[0] ^= 1;
    CHECK_FALSE(Ed25519Verify(otherKey, "r", Signature2));
    CHECK_FALSE(Ed25519Verify(Public2, "r ", Signature2));
    CHECK_FALSE(Ed25519Verify(Public2, "", Signature2));
}

TEST_CASE("wrong sizes fail instead of crashing") {
    CHECK_FALSE(Ed25519Verify("", "r", Signature2));
    CHECK_FALSE(Ed25519Verify(Public2.substr(1), "r", Signature2));
    CHECK_FALSE(Ed25519Verify(Public2 + "x", "r", Signature2));
    CHECK_FALSE(Ed25519Verify(Public2, "r", ""));
    CHECK_FALSE(Ed25519Verify(Public2, "r", Signature2.substr(1)));
    CHECK_FALSE(Ed25519Verify(Public2, "r", Signature2 + "x"));
    CHECK_FALSE(Ed25519Verify(std::string(32, '\0'), "r", std::string(64, '\0')));
}

TEST_CASE("a large message signs and verifies") {
    std::string big(3 * 1024 * 1024, 'x');
    auto signature = Ed25519Sign(Seed1, big);
    REQUIRE(signature);
    CHECK(Ed25519Verify(Public1, big, *signature));
    big[big.size() / 2] = 'y';
    CHECK_FALSE(Ed25519Verify(Public1, big, *signature));
}

TEST_CASE("trusted keys are read from their file") {
    std::string error;
    std::string text = "# BeamMP release key\ned25519:" + Base64Encode(Public1) + "\n\n  ed25519:" + Base64Encode(Public2) + "  \r\n# end\n";
    auto keys = ParseTrustedKeys(text, error);
    REQUIRE(keys.size() == 2);
    CHECK(keys[0] == Public1);
    CHECK(keys[1] == Public2);
    CHECK(error.empty());
    CHECK(ParseTrustedKeys("", error).empty());
    CHECK(ParseTrustedKeys("# nothing\n\n", error).empty());
}

TEST_CASE("a bad line in the trusted keys refuses the whole file") {
    std::string error;
    CHECK(ParseTrustedKeys("ed25519:" + Base64Encode(Public1) + "\nrsa:abc\n", error).empty());
    CHECK(error.find("Line 2") != std::string::npos);
    CHECK(ParseTrustedKeys("ed25519:!!!\n", error).empty());
    CHECK(error.find("Line 1") != std::string::npos);
    CHECK(ParseTrustedKeys("ed25519:" + Base64Encode(Public1.substr(1)) + "\n", error).empty());
    CHECK(ParseTrustedKeys("ed25519:" + Base64Encode(Public1 + "x") + "\n", error).empty());
    CHECK(ParseTrustedKeys("ed25519:\n", error).empty());
    CHECK(ParseTrustedKeys("Ed25519:" + Base64Encode(Public1) + "\n", error).empty());
}
