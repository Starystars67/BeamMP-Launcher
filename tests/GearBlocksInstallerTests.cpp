/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Games/GearBlocksInstaller.h"
#include "Net/Base64.h"
#include "Net/Ed25519.h"
#include "Net/Sha256.h"
#include "Net/Unzip.h"
#include <chrono>
#include <doctest/doctest.h>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <unistd.h>

using namespace Games;
namespace fs = std::filesystem;

namespace {
struct TempDir {
    fs::path path;
    TempDir() {
        path = fs::temp_directory_path() / ("beammp-install-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::permissions(path, fs::perms::owner_all, fs::perm_options::add, ec);
        for (auto& entry : fs::recursive_directory_iterator(path, ec)) {
            fs::permissions(entry.path(), fs::perms::owner_all, fs::perm_options::add, ec);
        }
        fs::remove_all(path, ec);
    }
};

void Write(const fs::path& file, const std::string& bytes) {
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary | std::ios::trunc) << bytes;
}

std::string Slurp(const fs::path& file) {
    std::ifstream stream(file, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

// Unity keeps a string as a 32 bit length and the characters.
std::string UnityString(const std::string& text) {
    std::string bytes(4, '\0');
    bytes[0] = static_cast<char>(text.size());
    return bytes + text;
}

const std::vector<std::pair<std::string, std::string>> LoaderEntries = {
    { "winhttp.dll", "doorstop proxy" },
    { "doorstop_config.ini", "[General]" },
    { "BepInEx/", "" },
    { "BepInEx/core/BepInEx.Unity.IL2CPP.dll", "core" },
    { "BepInEx/core/Other.dll", "other" },
    { "BepInEx/plugins/", "" },
    { "dotnet/System.dll", "runtime" },
};

const std::string Seed(32, '\x11');
const std::string OtherSeed(32, '\x22');
const std::string ManifestUrl = "https://files.example.dev/gearblocks/manifest.json";
const std::string LoaderUrl = "https://files.example.dev/loader.zip";

struct ReleaseSpec {
    std::string version = "0.1.0";
    std::vector<std::string> builds = { "0.8.9740" };
    int protocol = 1;
    std::string plugin = "plugin v1";
    std::map<std::string, std::string> mod = { { "main.lua", "print('hi')" }, { "meta.json", "{}" } };
};

// A BeamMP file server in memory, and a game folder to install into.
struct Fixture {
    TempDir dir;
    fs::path game = dir.path / "game";
    std::map<std::string, std::string> server;
    std::map<std::string, int> hits;
    std::set<std::string> corrupt;
    bool down = false;
    bool cancelDuringDownload = false;
    std::string build = "0.8.9740";
    std::string signWith = Seed;
    std::string sourceUrl = ManifestUrl;
    uint64_t sequence = 1;
    std::vector<ReleaseSpec> releases = { ReleaseSpec {} };
    std::string loaderSha;
    std::vector<InstallProgress> progress;

    Fixture() {
        Write(game / "GearBlocks.exe", "exe");
        Write(game / "GearBlocks_Data" / "globalgamemanagers", std::string("junk\0", 5) + UnityString("2022.3.1") + UnityString(build) + "tail");
        fs::path zip = dir.path / "loader.zip";
        REQUIRE(Net::CreateZip(zip, LoaderEntries));
        server[LoaderUrl] = Slurp(zip);
        loaderSha = *Net::Sha256FileHex(zip);
        Publish();
    }

    static std::string PublicKey() { return *Net::Ed25519PublicKey(Seed); }

    std::string Url(const std::string& name) const { return "https://files.example.dev/gearblocks/" + name; }

    // Puts the releases, their files, the manifest and its signature on the server.
    void Publish() {
        nlohmann::json list = nlohmann::json::array();
        for (const auto& release : releases) {
            std::vector<std::pair<std::string, std::string>> entries(release.mod.begin(), release.mod.end());
            fs::path zip = dir.path / ("mod-" + release.version + ".zip");
            REQUIRE(Net::CreateZip(zip, entries));
            std::string modBytes = Slurp(zip);
            server[Url("plugin-" + release.version + ".dll")] = release.plugin;
            server[Url("mod-" + release.version + ".zip")] = modBytes;
            list.push_back({ { "version", release.version }, { "bridge_protocol", release.protocol }, { "game_builds", release.builds },
                { "plugin", { { "url", Url("plugin-" + release.version + ".dll") }, { "sha256", Net::Sha256Hex(release.plugin) } } },
                { "mod", { { "url", Url("mod-" + release.version + ".zip") }, { "sha256", Net::Sha256Hex(modBytes) } } } });
        }
        nlohmann::json manifest = { { "schema", 1 }, { "game", "gearblocks" }, { "sequence", sequence },
            { "loader", { { "version", "6.0.0-be.788" }, { "url", LoaderUrl }, { "sha256", loaderSha } } }, { "releases", list } };
        PublishText(manifest.dump());
    }

    void PublishText(const std::string& text) {
        server[ManifestUrl] = text;
        server[ManifestUrl + ".sig"] = Net::Base64Encode(*Net::Ed25519Sign(signWith, text)) + "\n";
    }

    nlohmann::json Manifest() const { return nlohmann::json::parse(server.at(ManifestUrl)); }

    int Hits(const std::string& url) const {
        auto it = hits.find(url);
        return it == hits.end() ? 0 : it->second;
    }

    int Downloads() const {
        int total = 0;
        for (const auto& [url, count] : hits) {
            total += count;
        }
        return total;
    }

    GearBlocksInstaller Installer(bool allowUnsupported = false, bool configured = true) {
        GearBlocksInstaller::Options options;
        options.gameDir = game;
        options.workDir = dir.path / "work";
        options.allowUnsupportedBuild = allowUnsupported;
        if (configured) {
            options.manifestUrl = sourceUrl;
            options.trustedKeys = { PublicKey() };
        }
        options.fetch = [this](const GearBlocksInstaller::FetchRequest& request) {
            hits[request.url]++;
            Net::DownloadResult result;
            auto found = server.find(request.url);
            if (down || found == server.end()) {
                result.error = down ? "no network" : "not found";
                return result;
            }
            if (request.progress && !request.progress(10, 100)) {
                result.cancelled = true;
                result.error = "Cancelled";
                return result;
            }
            if (cancelDuringDownload) {
                result.cancelled = true;
                result.error = "Cancelled";
                return result;
            }
            if (found->second.size() > request.maxBytes) {
                result.error = "too large";
                return result;
            }
            std::string bytes = found->second;
            if (corrupt.count(request.url)) {
                bytes[bytes.size() / 2] ^= 0x55;
            }
            Write(request.destination, bytes);
            if (request.progress) {
                request.progress(100, 100);
            }
            result.ok = true;
            return result;
        };
        return GearBlocksInstaller(options);
    }

    InstallResult Install(GearBlocksInstaller& installer) {
        progress.clear();
        return installer.Install([this](const InstallProgress& p) { progress.push_back(p); });
    }

    SyncResult Sync(GearBlocksInstaller& installer) {
        progress.clear();
        return installer.Sync([this](const InstallProgress& p) { progress.push_back(p); });
    }

    fs::path Plugin() const { return GearBlocksInstaller::PluginPath(game); }
    fs::path Mod() const { return GearBlocksInstaller::ModPath(game); }
};
}

TEST_CASE("the game's build is read from its data") {
    Fixture f;
    CHECK(GearBlocksInstaller::ReadGameBuild(f.game, {}) == "2022.3.1"); // the first version-looking string
    CHECK(GearBlocksInstaller::ReadGameBuild(f.game, { "0.8.9740" }) == "0.8.9740");
    CHECK(GearBlocksInstaller::ReadGameBuild(f.game, { "9.9.9" }) == "2022.3.1");
    CHECK(GearBlocksInstaller::ReadGameBuild(f.dir.path / "elsewhere", {}) == "");
    Write(f.game / "GearBlocks_Data" / "globalgamemanagers", "no versions in here 1.2 or 3..4");
    CHECK(GearBlocksInstaller::ReadGameBuild(f.game, {}) == "");
}

TEST_CASE("inspecting a game that has nothing, and one that has everything") {
    Fixture f;
    auto installer = f.Installer();
    auto state = installer.Inspect();
    CHECK(state.sourceConfigured);
    CHECK(state.gameFound);
    CHECK(state.gameBuild == "2022.3.1"); // nothing is known about the game's builds before the first manifest
    CHECK_FALSE(state.manifestKnown);
    CHECK_FALSE(state.loaderInstalled);
    CHECK_FALSE(state.pluginInstalled);
    CHECK_FALSE(state.modInstalled);
    CHECK_FALSE(state.Complete());
    CHECK(f.Downloads() == 0); // inspecting never uses the network

    REQUIRE(f.Install(installer).ok);
    state = installer.Inspect();
    CHECK(state.gameBuild == "0.8.9740");
    CHECK(state.manifestKnown);
    CHECK(state.buildSupported);
    CHECK(state.loaderInstalled);
    CHECK(state.pluginInstalled);
    CHECK(state.pluginCurrent);
    CHECK(state.modInstalled);
    CHECK(state.modCurrent);
    CHECK(state.installedVersion == "0.1.0");
    CHECK(state.latestVersion == "0.1.0");
    CHECK(state.Complete());

    GearBlocksInstaller::Options none;
    none.gameDir = f.dir.path / "nogame";
    auto missing = GearBlocksInstaller(none).Inspect();
    CHECK_FALSE(missing.gameFound);
    CHECK_FALSE(missing.sourceConfigured);
    CHECK_FALSE(missing.Complete());
}

TEST_CASE("without a manifest address and a trusted key nothing is installed or trusted") {
    Fixture f;
    auto unconfigured = f.Installer(false, false);
    CHECK_FALSE(unconfigured.Inspect().sourceConfigured);
    auto result = f.Install(unconfigured);
    CHECK_FALSE(result.ok);
    CHECK(result.code == "manifest_unconfigured");
    CHECK(f.Downloads() == 0);
    CHECK_FALSE(fs::exists(f.game / "winhttp.dll"));

    GearBlocksInstaller::Options noKey;
    noKey.gameDir = f.game;
    noKey.manifestUrl = ManifestUrl;
    CHECK_FALSE(GearBlocksInstaller(noKey).Inspect().sourceConfigured);
    GearBlocksInstaller::Options noAddress;
    noAddress.gameDir = f.game;
    noAddress.trustedKeys = { Fixture::PublicKey() };
    CHECK_FALSE(GearBlocksInstaller(noAddress).Inspect().sourceConfigured);
    CHECK(GearBlocksInstaller(noAddress).Sync(nullptr).code == "loader_missing");
}

TEST_CASE("a fresh install puts everything in place and records it") {
    Fixture f;
    auto installer = f.Installer();
    auto result = f.Install(installer);
    REQUIRE(result.ok);
    CHECK(f.Hits(LoaderUrl) == 1);
    CHECK(f.Hits(f.Url("plugin-0.1.0.dll")) == 1);
    CHECK(f.Hits(f.Url("mod-0.1.0.zip")) == 1);
    CHECK(fs::exists(f.game / "winhttp.dll"));
    CHECK(fs::exists(f.game / "BepInEx" / "core" / "BepInEx.Unity.IL2CPP.dll"));
    CHECK(fs::exists(f.game / "dotnet" / "System.dll"));
    CHECK(Slurp(f.Plugin()) == "plugin v1");
    CHECK(Slurp(f.Mod() / "main.lua") == "print('hi')");
    CHECK(Slurp(f.Mod() / "meta.json") == "{}");
    CHECK_FALSE(fs::exists(f.game / "BepInEx" / "plugins" / "BeamMP.GearBlocks.dll.beammp-new"));
    CHECK(fs::is_empty(f.dir.path / "work"));
    CHECK(fs::exists(GearBlocksInstaller::CachedManifestPath(f.game)));
    CHECK(fs::exists(GearBlocksInstaller::CachedSignaturePath(f.game)));

    auto record = nlohmann::json::parse(Slurp(f.game / "beammp-install.json"));
    CHECK(record["version"] == "0.1.0");
    CHECK(record["loader"]["version"] == "6.0.0-be.788");
    CHECK(record["loader"]["files"].size() == 5);
    CHECK(record["plugin"]["sha256"] == Net::Sha256Hex("plugin v1"));
    CHECK(record["mod"]["files"].size() == 2);
    CHECK(record["mod"]["files"]["main.lua"] == Net::Sha256Hex("print('hi')"));
}

TEST_CASE("progress goes forward and ends at one") {
    Fixture f;
    auto installer = f.Installer();
    REQUIRE(f.Install(installer).ok);
    REQUIRE_FALSE(f.progress.empty());
    double before = -1;
    for (const auto& p : f.progress) {
        CHECK_FALSE(p.step.empty());
        CHECK_FALSE(p.message.empty());
        CHECK(p.fraction >= before);
        CHECK(p.fraction <= 1.0);
        before = p.fraction;
    }
    CHECK(f.progress.back().step == "done");
    CHECK(f.progress.back().fraction == 1.0);
    std::set<std::string> steps;
    for (const auto& p : f.progress) {
        steps.insert(p.step);
    }
    CHECK(steps.count("manifest"));
    CHECK(steps.count("loader"));
    CHECK(steps.count("plugin"));
    CHECK(steps.count("mod"));
}

TEST_CASE("syncing again downloads only the manifest") {
    Fixture f;
    auto installer = f.Installer();
    REQUIRE(f.Install(installer).ok);
    f.hits.clear();
    auto result = f.Sync(installer);
    REQUIRE(result.ok);
    CHECK_FALSE(result.changed);
    CHECK_FALSE(result.offline);
    CHECK(f.Hits(ManifestUrl) == 1);
    CHECK(f.Hits(ManifestUrl + ".sig") == 1);
    CHECK(f.Downloads() == 2);
    REQUIRE(f.Install(installer).ok);
    CHECK(f.Hits(LoaderUrl) == 0);
    CHECK(f.Hits(f.Url("plugin-0.1.0.dll")) == 0);
}

TEST_CASE("a newer release replaces the plugin and the mod, and what it dropped") {
    Fixture f;
    auto installer = f.Installer();
    REQUIRE(f.Install(installer).ok);
    Write(f.Mod() / "sub" / "kept-by-the-user.lua", "x"); // not in any release: left alone

    f.releases.push_back({ "0.2.0", { "0.8.9740" }, 1, "plugin v2", { { "main.lua", "print('v2')" }, { "extra/new.lua", "n" } } });
    f.sequence = 2;
    f.Publish();
    f.hits.clear();
    auto before = installer.Inspect();
    CHECK(before.latestVersion == "0.1.0"); // the cached manifest is the old one until a sync
    auto result = f.Sync(installer);
    REQUIRE(result.ok);
    CHECK(result.changed);
    CHECK(Slurp(f.Plugin()) == "plugin v2");
    CHECK(Slurp(f.Mod() / "main.lua") == "print('v2')");
    CHECK(Slurp(f.Mod() / "extra" / "new.lua") == "n");
    CHECK_FALSE(fs::exists(f.Mod() / "meta.json")); // the release before had it
    CHECK(fs::exists(f.Mod() / "sub" / "kept-by-the-user.lua"));
    CHECK(f.Hits(LoaderUrl) == 0);
    auto after = installer.Inspect();
    CHECK(after.installedVersion == "0.2.0");
    CHECK(after.latestVersion == "0.2.0");
    CHECK(after.Complete());
    CHECK(nlohmann::json::parse(Slurp(f.game / "beammp-install.json")).contains("loader"));
}

TEST_CASE("the highest release for the game's build is the one that is installed") {
    Fixture f;
    f.releases = { { "0.1.0", { "0.8.9740" } }, { "0.10.0", { "0.8.9740" }, 1, "plugin ten" }, { "0.9.0", { "0.8.9740" }, 1, "plugin nine" }, { "5.0.0", { "0.9.9999" }, 1, "other build" } };
    f.Publish();
    auto installer = f.Installer();
    REQUIRE(f.Install(installer).ok);
    CHECK(Slurp(f.Plugin()) == "plugin ten");
}

TEST_CASE("a plugin or a mod that was changed on disk is put back") {
    Fixture f;
    auto installer = f.Installer();
    REQUIRE(f.Install(installer).ok);
    f.hits.clear();
    Write(f.Plugin(), "tampered plugin");
    CHECK_FALSE(installer.Inspect().pluginCurrent);
    CHECK(installer.Inspect().Complete() == false);
    auto result = f.Sync(installer);
    REQUIRE(result.ok);
    CHECK(result.changed);
    CHECK(Slurp(f.Plugin()) == "plugin v1");
    CHECK(f.Hits(f.Url("mod-0.1.0.zip")) == 0);

    Write(f.Mod() / "main.lua", "print('tampered')");
    CHECK_FALSE(installer.Inspect().modCurrent);
    REQUIRE(f.Sync(installer).ok);
    CHECK(Slurp(f.Mod() / "main.lua") == "print('hi')");
    CHECK(f.Hits(f.Url("plugin-0.1.0.dll")) == 1); // only the first repair needed it

    fs::remove(f.Mod() / "meta.json");
    CHECK_FALSE(installer.Inspect().modCurrent);
    REQUIRE(f.Sync(installer).ok);
    CHECK(fs::exists(f.Mod() / "meta.json"));
    CHECK(installer.Inspect().Complete());
}

TEST_CASE("when the network is down the last verified manifest is used") {
    Fixture f;
    auto installer = f.Installer();
    REQUIRE(f.Install(installer).ok);
    f.down = true;
    auto result = f.Sync(installer);
    CHECK(result.ok);
    CHECK(result.offline);
    CHECK_FALSE(result.changed);
    // and it still repairs from what the verified manifest names, when those can be fetched
    Write(f.Plugin(), "tampered plugin");
    auto repair = f.Sync(installer);
    CHECK_FALSE(repair.ok);
    CHECK(repair.code == "download_failed");
    CHECK(Slurp(f.Plugin()) == "tampered plugin");
}

TEST_CASE("without a network and without anything verified before, nothing happens") {
    Fixture f;
    f.down = true;
    auto installer = f.Installer();
    auto result = f.Install(installer);
    CHECK_FALSE(result.ok);
    CHECK(result.code == "download_failed");
    CHECK_FALSE(fs::exists(f.game / "winhttp.dll"));
    CHECK_FALSE(fs::exists(f.game / "beammp-install.json"));
    CHECK_FALSE(fs::exists(GearBlocksInstaller::CachedManifestPath(f.game)));
}

TEST_CASE("a manifest that is not signed by a trusted key is not used") {
    Fixture f;
    f.signWith = OtherSeed;
    f.Publish();
    auto installer = f.Installer();
    auto result = f.Install(installer);
    CHECK_FALSE(result.ok);
    CHECK(result.code == "bad_signature");
    CHECK(f.Hits(LoaderUrl) == 0);
    CHECK_FALSE(fs::exists(f.game / "winhttp.dll"));
    CHECK_FALSE(fs::exists(GearBlocksInstaller::CachedManifestPath(f.game)));

    // changed after it was signed
    f.signWith = Seed;
    f.Publish();
    f.server[ManifestUrl] = f.server[ManifestUrl] + " ";
    CHECK(f.Install(installer).code == "bad_signature");
    // no signature at all
    f.Publish();
    f.server.erase(ManifestUrl + ".sig");
    CHECK(f.Install(installer).code == "download_failed");
    // a signature that is not one
    f.Publish();
    f.server[ManifestUrl + ".sig"] = "not base64 at all";
    CHECK(f.Install(installer).code == "bad_signature");
    CHECK(f.Hits(LoaderUrl) == 0);
}

TEST_CASE("a bad signature does not fall back to the last manifest, the installed files stay") {
    Fixture f;
    auto installer = f.Installer();
    REQUIRE(f.Install(installer).ok);
    f.signWith = OtherSeed;
    f.releases[0].plugin = "evil plugin";
    f.Publish();
    auto result = f.Sync(installer);
    CHECK_FALSE(result.ok);
    CHECK(result.code == "bad_signature");
    CHECK(Slurp(f.Plugin()) == "plugin v1");
}

TEST_CASE("an older manifest than one already seen is a replay and is refused") {
    Fixture f;
    f.sequence = 5;
    f.Publish();
    auto old = std::make_pair(f.server[ManifestUrl], f.server[ManifestUrl + ".sig"]);
    auto installer = f.Installer();
    REQUIRE(f.Install(installer).ok);
    f.sequence = 6;
    f.releases.push_back({ "0.2.0", { "0.8.9740" }, 1, "plugin v2" });
    f.Publish();
    REQUIRE(f.Sync(installer).ok);
    CHECK(Slurp(f.Plugin()) == "plugin v2");
    // the attacker serves the old, genuinely signed one
    f.server[ManifestUrl] = old.first;
    f.server[ManifestUrl + ".sig"] = old.second;
    auto result = f.Sync(installer);
    CHECK_FALSE(result.ok);
    CHECK(result.code == "manifest_rollback");
    CHECK(Slurp(f.Plugin()) == "plugin v2");
}

TEST_CASE("a file whose checksum is not the signed one is not used") {
    Fixture f;
    auto installer = f.Installer();
    f.corrupt.insert(f.Url("plugin-0.1.0.dll"));
    auto result = f.Install(installer);
    CHECK_FALSE(result.ok);
    CHECK(result.code == "bad_checksum");
    CHECK_FALSE(fs::exists(f.Plugin()));
    CHECK_FALSE(fs::exists(f.Mod()));
    CHECK(fs::is_empty(f.dir.path / "work"));

    f.corrupt = { f.Url("mod-0.1.0.zip") };
    auto synced = f.Sync(installer);
    CHECK_FALSE(synced.ok);
    CHECK(synced.code == "bad_checksum");
    CHECK_FALSE(fs::exists(f.Plugin())); // nothing is put in until everything is checked
    CHECK_FALSE(fs::exists(f.Mod()));
    CHECK(fs::is_empty(f.dir.path / "work"));
}

TEST_CASE("a loader that is not the signed one is not used") {
    Fixture f;
    f.corrupt.insert(LoaderUrl);
    auto installer = f.Installer();
    auto result = f.Install(installer);
    CHECK_FALSE(result.ok);
    CHECK(result.code == "bad_checksum");
    CHECK_FALSE(fs::exists(f.game / "winhttp.dll"));
    CHECK_FALSE(fs::exists(f.game / "BepInEx"));
    CHECK_FALSE(fs::exists(f.game / "beammp-install.json"));
    CHECK_FALSE(fs::exists(f.Plugin()));
    CHECK(fs::is_empty(f.dir.path / "work"));
}

TEST_CASE("a mod zip without main.lua, or with a path out of its folder, is not installed") {
    Fixture f;
    f.releases[0].mod = { { "other.lua", "x" } };
    f.Publish();
    auto installer = f.Installer();
    auto result = f.Install(installer);
    CHECK_FALSE(result.ok);
    CHECK(result.code == "bad_manifest");
    CHECK_FALSE(fs::exists(f.Mod()));
    CHECK_FALSE(fs::exists(f.Plugin()));

    Fixture g;
    g.releases[0].mod = { { "../../escaped.lua", "x" }, { "main.lua", "y" } };
    g.Publish();
    auto second = g.Installer();
    auto bad = g.Install(second);
    CHECK_FALSE(bad.ok);
    CHECK(bad.code == "extract_failed");
    CHECK_FALSE(fs::exists(g.dir.path / "escaped.lua"));
    CHECK_FALSE(fs::exists(g.dir.path / "game" / "escaped.lua"));
}

TEST_CASE("another winhttp.dll is not overwritten") {
    Fixture f;
    Write(f.game / "winhttp.dll", "somebody else's");
    auto installer = f.Installer();
    auto result = f.Install(installer);
    CHECK_FALSE(result.ok);
    CHECK(result.code == "loader_conflict");
    CHECK(Slurp(f.game / "winhttp.dll") == "somebody else's");
    CHECK(f.Hits(LoaderUrl) == 0);
    CHECK_FALSE(fs::exists(f.Plugin()));
}

TEST_CASE("an install can be cancelled") {
    Fixture f;
    f.cancelDuringDownload = true;
    auto installer = f.Installer();
    auto result = f.Install(installer);
    CHECK_FALSE(result.ok);
    CHECK(result.code == "cancelled");
    CHECK_FALSE(fs::exists(f.game / "winhttp.dll"));
    CHECK_FALSE(fs::exists(f.game / "BepInEx"));
}

TEST_CASE("a game build no release was made for is refused unless allowed") {
    Fixture f;
    Write(f.game / "GearBlocks_Data" / "globalgamemanagers", UnityString("0.9.1000"));
    auto installer = f.Installer();
    auto result = f.Install(installer);
    CHECK_FALSE(result.ok);
    CHECK(result.code == "unsupported_build");
    CHECK(result.message.find("0.9.1000") != std::string::npos);
    CHECK(result.message.find("0.8.9740") != std::string::npos);
    CHECK_FALSE(fs::exists(f.game / "winhttp.dll"));
    CHECK(f.Hits(LoaderUrl) == 0); // found out before anything big is downloaded
    auto state = installer.Inspect();
    CHECK(state.manifestKnown);
    CHECK_FALSE(state.buildSupported);
    auto forced = f.Installer(true);
    CHECK(f.Install(forced).ok);

    // a build that cannot be read is not a reason to refuse
    Fixture g;
    Write(g.game / "GearBlocks_Data" / "globalgamemanagers", "nothing");
    auto unreadable = g.Installer();
    CHECK(g.Install(unreadable).ok);
}

TEST_CASE("a release for a protocol this launcher does not speak asks for an update of the launcher") {
    Fixture f;
    f.releases = { { "0.1.0", { "0.8.9740" }, 2 } };
    f.Publish();
    auto installer = f.Installer();
    auto result = f.Install(installer);
    CHECK_FALSE(result.ok);
    CHECK(result.code == "launcher_too_old");
    CHECK_FALSE(fs::exists(f.game / "winhttp.dll"));
    // an older release the launcher does speak is used
    f.releases.push_back({ "0.0.9", { "0.8.9740" }, 1, "old plugin" });
    f.Publish();
    REQUIRE(f.Install(installer).ok);
    CHECK(Slurp(f.Plugin()) == "old plugin");
}

TEST_CASE("only the loader can be installed, and then the sync adds the rest") {
    Fixture f;
    auto installer = f.Installer();
    auto result = installer.InstallLoader(nullptr);
    REQUIRE(result.ok);
    CHECK(GearBlocksInstaller::LoaderPresent(f.game));
    CHECK_FALSE(fs::exists(f.Plugin()));
    CHECK(f.Hits(f.Url("plugin-0.1.0.dll")) == 0);
    auto state = installer.Inspect();
    CHECK(state.loaderInstalled);
    CHECK_FALSE(state.Complete());
    auto synced = f.Sync(installer);
    REQUIRE(synced.ok);
    CHECK(synced.changed);
    CHECK(installer.Inspect().Complete());
    CHECK(f.Hits(LoaderUrl) == 1);
}

TEST_CASE("missing pieces are reported") {
    Fixture f;
    fs::remove(f.game / "GearBlocks.exe");
    auto installer = f.Installer();
    CHECK(f.Install(installer).code == "game_not_found");
    CHECK(f.Sync(installer).code == "game_not_found");
    CHECK(installer.InstallLoader(nullptr).code == "game_not_found");
    Write(f.game / "GearBlocks.exe", "exe");
    CHECK(f.Sync(installer).code == "loader_missing");
    f.server.erase(f.Url("mod-0.1.0.zip"));
    CHECK(f.Install(installer).code == "download_failed");
    CHECK_FALSE(fs::exists(f.Plugin()));
}

TEST_CASE("a game folder that cannot be written to says so") {
    if (geteuid() == 0) {
        return; // root writes anywhere
    }
    Fixture f;
    auto installer = f.Installer();
    REQUIRE(f.Install(installer).ok);
    f.releases[0].plugin = "plugin v2";
    f.releases[0].version = "0.1.1";
    f.sequence = 2;
    f.Publish();
    fs::permissions(f.game / "BepInEx" / "plugins", fs::perms::owner_write, fs::perm_options::remove);
    auto result = f.Sync(installer);
    CHECK_FALSE(result.ok);
    CHECK(result.code == "not_writable");
    CHECK(Slurp(f.Plugin()) == "plugin v1");
}

TEST_CASE("uninstalling takes out the plugin and the mod and leaves the loader") {
    Fixture f;
    auto installer = f.Installer();
    REQUIRE(f.Install(installer).ok);
    Write(f.game / "BepInEx" / "plugins" / "SomeoneElse.dll", "theirs");
    auto result = installer.Uninstall(false, nullptr);
    CHECK(result.ok);
    CHECK_FALSE(fs::exists(f.Plugin()));
    CHECK_FALSE(fs::exists(f.Mod()));
    CHECK(fs::exists(f.game / "BepInEx" / "plugins" / "SomeoneElse.dll"));
    CHECK(fs::exists(f.game / "winhttp.dll"));
    auto state = installer.Inspect();
    CHECK(state.loaderInstalled);
    CHECK_FALSE(state.pluginInstalled);
    // the loader is still on record, so it can be taken out later
    auto record = nlohmann::json::parse(Slurp(f.game / "beammp-install.json"));
    CHECK(record.contains("loader"));
    CHECK_FALSE(record.contains("mod"));
    CHECK(installer.Uninstall(true, nullptr).ok);
    CHECK_FALSE(fs::exists(f.game / "winhttp.dll"));
}

TEST_CASE("uninstalling the loader takes out what it wrote and what it made, nothing else") {
    Fixture f;
    auto installer = f.Installer();
    REQUIRE(f.Install(installer).ok);
    Write(f.game / "BepInEx" / "interop" / "Generated.dll", "generated");
    Write(f.game / "BepInEx" / "cache" / "x", "cache");
    Write(f.game / "BepInEx" / "LogOutput.log", "log");
    Write(f.game / "GearBlocks_Data" / "globalgamemanagers.keep", "game");
    Write(f.game / "BepInEx" / "plugins" / "SomeoneElse.dll", "theirs");
    std::vector<InstallProgress> seen;
    auto result = installer.Uninstall(true, [&](const InstallProgress& p) { seen.push_back(p); });
    CHECK(result.ok);
    CHECK_FALSE(seen.empty());
    CHECK(seen.back().step == "done");
    CHECK_FALSE(fs::exists(f.game / "winhttp.dll"));
    CHECK_FALSE(fs::exists(f.game / "doorstop_config.ini"));
    CHECK_FALSE(fs::exists(f.game / "dotnet"));
    CHECK_FALSE(fs::exists(f.game / "BepInEx" / "core"));
    CHECK_FALSE(fs::exists(f.game / "BepInEx" / "interop"));
    CHECK_FALSE(fs::exists(f.game / "BepInEx" / "LogOutput.log"));
    CHECK_FALSE(fs::exists(f.game / "beammp-install.json"));
    CHECK_FALSE(fs::exists(GearBlocksInstaller::CachedManifestPath(f.game)));
    // somebody else's plugin keeps its folder, and the game is untouched
    CHECK(fs::exists(f.game / "BepInEx" / "plugins" / "SomeoneElse.dll"));
    CHECK(fs::exists(f.game / "GearBlocks.exe"));
    CHECK(fs::exists(f.game / "GearBlocks_Data" / "globalgamemanagers.keep"));
}

TEST_CASE("a loader that BeamMP did not install is not removed") {
    Fixture f;
    for (const auto& [name, bytes] : LoaderEntries) {
        if (name.back() != '/') {
            Write(f.game / name, bytes);
        }
    }
    auto installer = f.Installer();
    REQUIRE(f.Install(installer).ok); // nothing to download for it: it is there
    CHECK(f.Hits(LoaderUrl) == 0);
    CHECK_FALSE(nlohmann::json::parse(Slurp(f.game / "beammp-install.json")).contains("loader"));
    auto result = installer.Uninstall(true, nullptr);
    CHECK_FALSE(result.ok);
    CHECK(result.code == "not_ours");
    CHECK(fs::exists(f.game / "winhttp.dll"));
    CHECK(fs::exists(f.game / "BepInEx" / "core" / "BepInEx.Unity.IL2CPP.dll"));
    // the plugin and the mod went anyway
    CHECK_FALSE(fs::exists(f.Plugin()));
}

TEST_CASE("uninstalling a game that is not there fails") {
    Fixture f;
    fs::remove(f.game / "GearBlocks.exe");
    auto installer = f.Installer();
    CHECK(installer.Uninstall(false, nullptr).code == "game_not_found");
}

// Opt in with BEAMMP_TEST_BEPINEX_ZIP=<the pinned BepInEx archive> (and BEAMMP_TEST_LOADER_JSON=<gearblocks/loader.json>):
// checks the real archive against the checksum the launcher's release tooling pins and installs it into an empty game folder.
TEST_CASE("the real BepInEx archive matches the pinned checksum and installs") {
    const char* archive = std::getenv("BEAMMP_TEST_BEPINEX_ZIP");
    const char* loaderJson = std::getenv("BEAMMP_TEST_LOADER_JSON");
    if (!archive || !loaderJson) {
        return;
    }
    auto loader = nlohmann::json::parse(Slurp(loaderJson));
    CHECK(Net::Sha256FileHex(archive) == loader["sha256"].get<std::string>());

    Fixture f;
    f.server[LoaderUrl] = Slurp(archive);
    f.loaderSha = loader["sha256"].get<std::string>();
    f.Publish();
    auto installer = f.Installer();
    auto result = f.Install(installer);
    CHECK(result.ok);
    CHECK(GearBlocksInstaller::LoaderPresent(f.game));
    CHECK(fs::exists(f.game / "doorstop_config.ini"));
    CHECK(fs::exists(f.game / "dotnet"));
    CHECK(installer.Inspect().Complete());
    auto record = nlohmann::json::parse(Slurp(f.game / "beammp-install.json"));
    CHECK(record["loader"]["files"].size() > 200);
    CHECK(installer.Uninstall(true, nullptr).ok);
    CHECK_FALSE(fs::exists(f.game / "winhttp.dll"));
    CHECK_FALSE(fs::exists(f.game / "dotnet"));
    CHECK_FALSE(fs::exists(f.game / "BepInEx"));
    CHECK(fs::exists(f.game / "GearBlocks.exe"));
}
