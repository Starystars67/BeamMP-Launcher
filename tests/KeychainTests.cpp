/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Auth/SecretStore.h"
#include <doctest/doctest.h>
#include <unistd.h>

using namespace Auth;

#if defined(__APPLE__)
namespace {
// A service name of its own, so a real saved sign-in is never touched.
std::string TestService() {
    return "com.beammp.launcher.tests." + std::to_string(getpid());
}
}

TEST_CASE("the keychain keeps a secret and gives it back") {
    auto store = MakeSystemSecretStore(TestService(), "refresh-token");
    store->Erase();
    CHECK_FALSE(store->Load().has_value());

    REQUIRE(store->Save("bmp_rt_first"));
    CHECK(store->Load() == std::optional<std::string>("bmp_rt_first"));

    REQUIRE(store->Save("bmp_rt_second"));
    CHECK(store->Load() == std::optional<std::string>("bmp_rt_second"));

    store->Erase();
    CHECK_FALSE(store->Load().has_value());
    store->Erase();
}

TEST_CASE("the keychain keeps odd content intact") {
    auto store = MakeSystemSecretStore(TestService(), "odd");
    std::string secret = std::string("line1\nline2 ") + "\xC3\xA9" + std::string("\0end", 4);
    REQUIRE(store->Save(secret));
    CHECK(store->Load() == std::optional<std::string>(secret));
    store->Erase();
}

TEST_CASE("different accounts do not see each other's secrets") {
    auto a = MakeSystemSecretStore(TestService(), "a");
    auto b = MakeSystemSecretStore(TestService(), "b");
    REQUIRE(a->Save("only-a"));
    CHECK_FALSE(b->Load().has_value());
    a->Erase();
}
#else
TEST_CASE("without a keychain nothing is stored") {
    auto store = MakeSystemSecretStore();
    CHECK_FALSE(store->Save("x"));
    CHECK_FALSE(store->Load().has_value());
}
#endif
