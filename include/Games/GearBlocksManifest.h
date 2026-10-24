/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Games {

/// The bridge protocol between the launcher and the BepInEx plugin that this launcher speaks. A release of the
/// plugin states the one it was made for; the launcher only installs releases that match.
constexpr int GearBlocksBridgeProtocol = 1;

struct ManifestFile {
    std::string url;
    /// Lower case hex SHA-256 of the downloaded file.
    std::string sha256;
};

struct ManifestRelease {
    /// Digits and dots ("0.1.0"); the highest compatible one is installed.
    std::string version;
    int bridgeProtocol = 0;
    /// The game builds (GearBlocks' own version, "0.8.9740") the plugin was made for.
    std::vector<std::string> gameBuilds;
    /// The plugin DLL itself.
    ManifestFile plugin;
    /// A zip with the script mod's files at its root (main.lua there).
    ManifestFile mod;
};

/// What the launcher is told to install, published by BeamMP and signed:
///   {"schema":1,"game":"gearblocks","sequence":7,
///    "loader":{"version":"6.0.0-be.788","url":"https://...zip","sha256":"..."},
///    "releases":[{"version":"0.1.0","bridge_protocol":1,"game_builds":["0.8.9740"],
///                 "plugin":{"url":"...","sha256":"..."},"mod":{"url":"...","sha256":"..."}}]}
/// `sequence` only ever goes up between publications; a manifest with a lower one than the last seen is a replay.
struct GearBlocksManifest {
    uint64_t sequence = 0;
    std::string loaderVersion;
    ManifestFile loader;
    std::vector<ManifestRelease> releases;

    enum class Pick { Found, UnsupportedBuild, LauncherTooOld };

    /// The release to install for `gameBuild` (any build when it is empty). Among those for the build that this
    /// launcher can talk to, the highest version; `Pick` says why there is none otherwise.
    Pick Select(const std::string& gameBuild, const ManifestRelease*& release) const;

    /// Nothing, with `error` set, for text that is not JSON, not schema 1 for GearBlocks, incomplete, or has an address
    /// that is not https (loopback http only with `allowLoopbackHttp`, for tests) or a checksum that is not SHA-256.
    /// With `allowBundle` an address may also be `bundle:<file name>`, a file that ships next to the launcher.
    static std::optional<GearBlocksManifest> Parse(const std::string& text, bool allowLoopbackHttp, std::string& error, bool allowBundle = false);

    /// `bundle:<plain file name>` gives the name, nothing for any other address.
    static std::optional<std::string> BundleName(const std::string& url);
};

/// Whether one of the signatures (Base64 of 64 bytes, one per line; blank lines and `#` lines are ignored) in
/// `signatureText` is a valid Ed25519 signature of `manifestText` by one of `trustedKeys` (raw 32 byte keys).
bool VerifyManifestSignature(const std::string& manifestText, const std::string& signatureText, const std::vector<std::string>& trustedKeys);

/// True when `a` is a higher version than `b` ("0.10.0" over "0.9.0"). Both digits and dots.
bool VersionNewer(const std::string& a, const std::string& b);

/// Digits and dots in 1 to 6 groups, none empty (a game build or a release version).
bool PlainVersion(const std::string& text);

}
