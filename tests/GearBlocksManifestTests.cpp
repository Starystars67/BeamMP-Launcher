/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Games/GearBlocksManifest.h"
#include "Net/Base64.h"
#include "Net/Ed25519.h"
#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

using namespace Games;

namespace {
const std::string Sha(64, 'a');

nlohmann::json File(const std::string& name) {
    return { { "url", "https://files.example.dev/" + name }, { "sha256", Sha } };
}

nlohmann::json Release(const std::string& version, std::vector<std::string> builds = { "0.8.9740" }, int protocol = 1) {
    return { { "version", version }, { "bridge_protocol", protocol }, { "game_builds", builds }, { "plugin", File("p-" + version) }, { "mod", File("m-" + version) } };
}

nlohmann::json Valid() {
    return { { "schema", 1 }, { "game", "gearblocks" }, { "sequence", 7 },
        { "loader", { { "version", "6.0.0-be.788" }, { "url", "https://files.example.dev/loader.zip" }, { "sha256", Sha } } },
        { "releases", { Release("0.1.0") } } };
}

std::optional<GearBlocksManifest> Parse(const nlohmann::json& json, bool loopback = false) {
    std::string error;
    return GearBlocksManifest::Parse(json.dump(), loopback, error);
}

std::string Error(const nlohmann::json& json) {
    std::string error;
    CHECK_FALSE(GearBlocksManifest::Parse(json.dump(), false, error));
    CHECK_FALSE(error.empty());
    return error;
}
}

TEST_CASE("a manifest is read") {
    auto manifest = Parse(Valid());
    REQUIRE(manifest);
    CHECK(manifest->sequence == 7);
    CHECK(manifest->loaderVersion == "6.0.0-be.788");
    CHECK(manifest->loader.url == "https://files.example.dev/loader.zip");
    CHECK(manifest->loader.sha256 == Sha);
    REQUIRE(manifest->releases.size() == 1);
    CHECK(manifest->releases[0].version == "0.1.0");
    CHECK(manifest->releases[0].bridgeProtocol == 1);
    CHECK(manifest->releases[0].gameBuilds == std::vector<std::string> { "0.8.9740" });
    CHECK(manifest->releases[0].plugin.url == "https://files.example.dev/p-0.1.0");
    CHECK(manifest->releases[0].mod.sha256 == Sha);
}

TEST_CASE("a manifest that is not one is refused") {
    std::string error;
    CHECK_FALSE(GearBlocksManifest::Parse("", false, error));
    CHECK_FALSE(GearBlocksManifest::Parse("not json", false, error));
    CHECK_FALSE(GearBlocksManifest::Parse("[1]", false, error));
    CHECK_FALSE(GearBlocksManifest::Parse("{}", false, error));

    for (const char* member : { "schema", "game", "sequence", "loader", "releases" }) {
        auto json = Valid();
        json.erase(member);
        Error(json);
    }
    auto json = Valid();
    json["schema"] = 2;
    CHECK(Error(json).find("update the launcher") != std::string::npos);
    json = Valid();
    json["game"] = "beamng";
    Error(json);
    json = Valid();
    json["sequence"] = -1;
    Error(json);
    json = Valid();
    json["sequence"] = "7";
    Error(json);
    json = Valid();
    json["releases"] = nlohmann::json::array();
    Error(json);
    json = Valid();
    json["releases"] = { 5 };
    Error(json);
}

TEST_CASE("addresses have to be https and checksums SHA-256") {
    auto json = Valid();
    json["loader"]["url"] = "http://files.example.dev/loader.zip";
    Error(json);
    json = Valid();
    json["releases"][0]["plugin"]["url"] = "ftp://files.example.dev/p";
    Error(json);
    json = Valid();
    json["releases"][0]["mod"]["url"] = "http://files.example.dev/m";
    Error(json);
    json = Valid();
    json["releases"][0]["mod"]["url"] = "http://127.0.0.1.evil.com/m";
    CHECK_FALSE(Parse(json, true));
    json = Valid();
    json["loader"]["sha256"] = "abc";
    Error(json);
    json = Valid();
    json["releases"][0]["plugin"]["sha256"] = std::string(63, 'a') + "g";
    Error(json);
    json = Valid();
    json["releases"][0]["plugin"]["sha256"] = std::string(64, 'A');
    auto upper = Parse(json);
    REQUIRE(upper);
    CHECK(upper->releases[0].plugin.sha256 == std::string(64, 'a'));

    // plain http to this machine only when it is allowed
    json = Valid();
    json["releases"][0]["plugin"]["url"] = "http://127.0.0.1:8000/p";
    CHECK_FALSE(Parse(json, false));
    CHECK(Parse(json, true));
}

TEST_CASE("releases are checked") {
    auto json = Valid();
    json["releases"][0]["version"] = "1.0; rm";
    Error(json);
    json = Valid();
    json["releases"][0]["version"] = "1..0";
    Error(json);
    json = Valid();
    json["releases"][0].erase("bridge_protocol");
    Error(json);
    json = Valid();
    json["releases"][0]["bridge_protocol"] = 0;
    Error(json);
    json = Valid();
    json["releases"][0]["bridge_protocol"] = "1";
    Error(json);
    json = Valid();
    json["releases"][0]["game_builds"] = nlohmann::json::array();
    Error(json);
    json = Valid();
    json["releases"][0]["game_builds"] = { "0.8.9740; rm" };
    Error(json);
    json = Valid();
    json["releases"][0]["game_builds"] = { 5 };
    Error(json);
    json = Valid();
    json["releases"][0].erase("plugin");
    Error(json);
    json = Valid();
    json["releases"][0]["mod"] = "x";
    Error(json);
    json = Valid();
    json["loader"]["version"] = "";
    Error(json);
}

TEST_CASE("versions compare by number") {
    CHECK(VersionNewer("0.10.0", "0.9.0"));
    CHECK_FALSE(VersionNewer("0.9.0", "0.10.0"));
    CHECK_FALSE(VersionNewer("1.0.0", "1.0.0"));
    CHECK(VersionNewer("1.0.1", "1.0"));
    CHECK_FALSE(VersionNewer("1.0", "1.0.0"));
    CHECK(VersionNewer("2", "1.9.9"));
    CHECK_FALSE(VersionNewer("x", "1"));
    CHECK_FALSE(VersionNewer("1", ""));
    CHECK(PlainVersion("0.8.9740"));
    CHECK(PlainVersion("12"));
    CHECK_FALSE(PlainVersion(""));
    CHECK_FALSE(PlainVersion(".1"));
    CHECK_FALSE(PlainVersion("1."));
    CHECK_FALSE(PlainVersion("1..2"));
    CHECK_FALSE(PlainVersion("1.2.3.4.5.6.7"));
    CHECK_FALSE(PlainVersion("1.2-beta"));
    CHECK_FALSE(PlainVersion("99999999999999999999"));
}

TEST_CASE("the release for a build is the highest one the launcher can talk to") {
    auto json = Valid();
    json["releases"] = { Release("0.1.0"), Release("0.10.0"), Release("0.9.0"), Release("2.0.0", { "0.9.1" }), Release("3.0.0", { "0.8.9740" }, 2) };
    auto manifest = Parse(json);
    REQUIRE(manifest);
    const ManifestRelease* release = nullptr;
    CHECK(manifest->Select("0.8.9740", release) == GearBlocksManifest::Pick::Found);
    REQUIRE(release);
    CHECK(release->version == "0.10.0");
    CHECK(manifest->Select("0.9.1", release) == GearBlocksManifest::Pick::Found);
    CHECK(release->version == "2.0.0");
    CHECK(manifest->Select("0.7.0", release) == GearBlocksManifest::Pick::UnsupportedBuild);
    CHECK(release == nullptr);
    // any build when it is not known
    CHECK(manifest->Select("", release) == GearBlocksManifest::Pick::Found);
    CHECK(release->version == "2.0.0");

    json["releases"] = { Release("3.0.0", { "0.8.9740" }, 2) };
    manifest = Parse(json);
    REQUIRE(manifest);
    CHECK(manifest->Select("0.8.9740", release) == GearBlocksManifest::Pick::LauncherTooOld);
    CHECK(manifest->Select("0.1.1", release) == GearBlocksManifest::Pick::UnsupportedBuild);
}

TEST_CASE("signatures are checked against the trusted keys") {
    std::string seedA(32, '\x01');
    std::string seedB(32, '\x02');
    std::string keyA = *Net::Ed25519PublicKey(seedA);
    std::string keyB = *Net::Ed25519PublicKey(seedB);
    std::string text = Valid().dump();
    std::string signatureA = Net::Base64Encode(*Net::Ed25519Sign(seedA, text));
    std::string signatureB = Net::Base64Encode(*Net::Ed25519Sign(seedB, text));

    CHECK(VerifyManifestSignature(text, signatureA, { keyA }));
    CHECK(VerifyManifestSignature(text, signatureA + "\n", { keyA }));
    CHECK(VerifyManifestSignature(text, signatureA, { keyB, keyA }));
    CHECK_FALSE(VerifyManifestSignature(text, signatureA, { keyB }));
    CHECK_FALSE(VerifyManifestSignature(text, signatureA, {}));
    CHECK_FALSE(VerifyManifestSignature(text + " ", signatureA, { keyA }));
    CHECK_FALSE(VerifyManifestSignature(text, "", { keyA }));
    CHECK_FALSE(VerifyManifestSignature(text, "# nothing\n\n", { keyA }));
    CHECK_FALSE(VerifyManifestSignature(text, "garbage", { keyA }));
    CHECK_FALSE(VerifyManifestSignature(text, Net::Base64Encode("short"), { keyA }));

    // during a key change the file carries both signatures; either trusted key is enough
    std::string both = "# release 7\n" + signatureB + "\n" + signatureA + "\n";
    CHECK(VerifyManifestSignature(text, both, { keyA }));
    CHECK(VerifyManifestSignature(text, both, { keyB }));
    // a bad line anywhere refuses the file
    CHECK_FALSE(VerifyManifestSignature(text, signatureA + "\nnot base64\n", { keyA }));
    std::string many;
    for (int i = 0; i < 9; i++) {
        many += signatureB + "\n";
    }
    CHECK_FALSE(VerifyManifestSignature(text, many + signatureA, { keyA }));
}

TEST_CASE("bundle addresses name a plain file and are only read when allowed") {
    CHECK(GearBlocksManifest::BundleName("bundle:plugin-0.1.0.dll") == "plugin-0.1.0.dll");
    for (const char* bad : { "bundle:", "bundle:..", "bundle:.", "bundle:a/b", "bundle:..\\x", "bundle:C:x", "https://x/y", "Bundle:x", "bundle", "file:///etc/passwd" }) {
        CHECK_FALSE(GearBlocksManifest::BundleName(bad));
    }
    auto json = Valid();
    json["releases"][0]["plugin"]["url"] = "bundle:plugin.dll";
    json["releases"][0]["mod"]["url"] = "bundle:mod.zip";
    std::string error;
    CHECK_FALSE(GearBlocksManifest::Parse(json.dump(), false, error));
    auto manifest = GearBlocksManifest::Parse(json.dump(), false, error, true);
    REQUIRE(manifest);
    CHECK(manifest->releases[0].plugin.url == "bundle:plugin.dll");
    json["releases"][0]["mod"]["url"] = "bundle:../mod.zip";
    CHECK_FALSE(GearBlocksManifest::Parse(json.dump(), false, error, true));
}
