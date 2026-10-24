/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Auth/OAuth.h"
#include "Auth/Pkce.h"
#include <doctest/doctest.h>
#include <regex>
#include <set>

using namespace Auth;

namespace {
std::string Encode(const std::string& text) {
    return Base64Url(reinterpret_cast<const unsigned char*>(text.data()), text.size());
}
}

TEST_CASE("base64url matches RFC 4648 without padding") {
    CHECK(Encode("") == "");
    CHECK(Encode("f") == "Zg");
    CHECK(Encode("fo") == "Zm8");
    CHECK(Encode("foo") == "Zm9v");
    CHECK(Encode("foob") == "Zm9vYg");
    CHECK(Encode("fooba") == "Zm9vYmE");
    CHECK(Encode("foobar") == "Zm9vYmFy");
}

TEST_CASE("base64url uses the URL-safe alphabet") {
    const unsigned char bytes[] = { 0xfb, 0xff, 0xfe };
    CHECK(Base64Url(bytes, 3) == "-__-");
}

TEST_CASE("the challenge matches the RFC 7636 example") {
    CHECK(ChallengeFor("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk") == "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM");
}

TEST_CASE("generated verifiers are valid, unique, and match their challenge") {
    std::set<std::string> seen;
    for (int i = 0; i < 50; ++i) {
        Pkce pkce = GeneratePkce();
        CHECK(std::regex_match(pkce.verifier, std::regex("^[A-Za-z0-9_-]{43}$")));
        CHECK(std::regex_match(pkce.challenge, std::regex("^[A-Za-z0-9_-]{43}$")));
        CHECK(ChallengeFor(pkce.verifier) == pkce.challenge);
        seen.insert(pkce.verifier);
    }
    CHECK(seen.size() == 50);
}

TEST_CASE("random tokens have the requested size") {
    CHECK(RandomToken(16).size() == 22);
    CHECK(RandomToken(32).size() == 43);
    CHECK(RandomToken(0).empty());
}

TEST_CASE("SafeEqual compares exactly") {
    CHECK(SafeEqual("abc", "abc"));
    CHECK_FALSE(SafeEqual("abc", "abd"));
    CHECK_FALSE(SafeEqual("abc", "abcd"));
    CHECK_FALSE(SafeEqual("", "a"));
    CHECK(SafeEqual("", ""));
}

TEST_CASE("percent-encoding keeps only unreserved characters") {
    CHECK(UrlEncode("abc-._~XYZ019") == "abc-._~XYZ019");
    CHECK(UrlEncode("a b&c=d/e?f") == "a%20b%26c%3Dd%2Fe%3Ff");
    CHECK(UrlEncode("http://127.0.0.1:5000/callback") == "http%3A%2F%2F127.0.0.1%3A5000%2Fcallback");
    CHECK(UrlEncode("é") == "%C3%A9");
}

TEST_CASE("only https or loopback http base URLs are acceptable") {
    CHECK(IsAcceptableBaseUrl("https://auth.beammp.com"));
    CHECK(IsAcceptableBaseUrl("https://auth.beammp.com/"));
    CHECK(IsAcceptableBaseUrl("https://example.com:8443/base"));
    CHECK(IsAcceptableBaseUrl("http://127.0.0.1:3000"));
    CHECK(IsAcceptableBaseUrl("http://localhost:3000"));
    CHECK(IsAcceptableBaseUrl("http://[::1]:3000"));
    CHECK_FALSE(IsAcceptableBaseUrl("http://example.com"));
    CHECK_FALSE(IsAcceptableBaseUrl("http://127.0.0.1.evil.com"));
    CHECK_FALSE(IsAcceptableBaseUrl("ftp://example.com"));
    CHECK_FALSE(IsAcceptableBaseUrl("file:///etc/passwd"));
    CHECK_FALSE(IsAcceptableBaseUrl("https://user:pass@example.com"));
    CHECK_FALSE(IsAcceptableBaseUrl("https://example.com?x=1"));
    CHECK_FALSE(IsAcceptableBaseUrl("https://"));
    CHECK_FALSE(IsAcceptableBaseUrl("example.com"));
    CHECK_FALSE(IsAcceptableBaseUrl(""));
}
