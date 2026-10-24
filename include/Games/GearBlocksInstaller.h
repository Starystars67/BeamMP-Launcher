/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include "Games/GameModule.h"
#include "Games/GearBlocksManifest.h"
#include "Net/Download.h"
#include <atomic>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace Games {

/// Where the game stands, from what is on disk and the last manifest that was checked (no network).
struct InstallState {
    /// A manifest address and a key to trust it by are configured: the launcher can get the files.
    bool sourceConfigured = false;
    bool gameFound = false;
    /// The game's build, empty when it could not be read.
    std::string gameBuild;
    bool loaderInstalled = false;
    bool pluginInstalled = false;
    bool modInstalled = false;
    /// A manifest that was verified by an earlier sync is known; what follows compares with it.
    bool manifestKnown = false;
    /// Version of the release installed (as recorded), empty when BeamMP's files were not put there by the launcher.
    std::string installedVersion;
    /// The release the known manifest has for this game build.
    std::string latestVersion;
    /// False when the known manifest has no release for the game's build.
    bool buildSupported = true;
    /// The installed plugin / mod are exactly the ones of that release.
    bool pluginCurrent = false;
    bool modCurrent = false;

    bool Complete() const { return gameFound && loaderInstalled && pluginInstalled && modInstalled && (!manifestKnown || (pluginCurrent && modCurrent)); }
};

/// What a sync did.
struct SyncResult {
    bool ok = false;
    std::string code;
    std::string message;
    /// A file was written.
    bool changed = false;
    /// The manifest could not be fetched, the last verified one was used.
    bool offline = false;
};

/// Puts what multiplayer needs into a GearBlocks folder. Two parts, because they have different lives:
///  - the BepInEx loader: installed once on the player's request (and again after a game update that removes it);
///  - the BeamMP plugin and script mod: kept in step with a signed manifest, at every launch.
/// Everything that is downloaded is named by the manifest, which is checked against a trusted public key (an
/// Ed25519 signature that is downloaded beside it), and by the SHA-256 in it. Nothing is built in. The installer
/// records what it wrote so that uninstalling takes out exactly that. Nothing here talks to the game.
class GearBlocksInstaller {
public:
    struct FetchRequest {
        std::string url;
        std::filesystem::path destination;
        uint64_t maxBytes = 0;
        /// Give up quickly on a bad network (at a launch, the player is waiting).
        bool quick = false;
        /// What is there and the total (0 when unknown); false cancels.
        std::function<bool(uint64_t done, uint64_t total)> progress;
    };
    using Fetcher = std::function<Net::DownloadResult(const FetchRequest&)>;

    struct Options {
        std::filesystem::path gameDir;
        /// Where downloads are put while they are checked and unpacked (the system's temporary folder by default).
        std::filesystem::path workDir;
        Fetcher fetch;
        /// The signed manifest; its signature is at the same address with `.sig` added.
        std::string manifestUrl;
        /// Raw Ed25519 public keys (32 bytes each); a manifest signed by any of them is accepted.
        std::vector<std::string> trustedKeys;
        /// Plain http to this machine is accepted (tests and development servers).
        bool allowLoopbackHttp = false;
        /// The manifest and its files are `bundle:<name>` addresses that the fetcher answers from a folder that ships with the
        /// launcher (the manifest address itself is one).
        bool allowBundle = false;
        /// Go ahead although the game's build is not one a release was made for.
        bool allowUnsupportedBuild = false;
        bool quickNetwork = false;
    };

    explicit GearBlocksInstaller(Options options);

    /// Without network.
    InstallState Inspect() const;

    /// The loader (when it is not there) and then a sync. Blocks. Never throws.
    InstallResult Install(const InstallReporter& progress);
    /// Only the loader.
    InstallResult InstallLoader(const InstallReporter& progress);
    /// Only the plugin and the mod: fetch the manifest (the last verified one when the network is down), install the
    /// release it names for this game when what is there differs, check what is there against it. The loader has to be
    /// installed. Blocks. Never throws.
    SyncResult Sync(const InstallReporter& progress);

    InstallResult Uninstall(bool removeLoader, const InstallReporter& progress);
    void Cancel() { m_cancel = true; }

    /// The build in the game's data (the version string Unity keeps in `globalgamemanagers`), preferring one of
    /// `preferred`; empty when there is none.
    static std::string ReadGameBuild(const std::filesystem::path& gameDir, const std::vector<std::string>& preferred);

    static std::filesystem::path PluginPath(const std::filesystem::path& gameDir);
    static std::filesystem::path ModPath(const std::filesystem::path& gameDir);
    static std::filesystem::path RecordPath(const std::filesystem::path& gameDir);
    /// The last verified manifest and its signature, kept to check what is installed without a network and to
    /// refuse an older one later.
    static std::filesystem::path CachedManifestPath(const std::filesystem::path& gameDir);
    static std::filesystem::path CachedSignaturePath(const std::filesystem::path& gameDir);
    static bool LoaderPresent(const std::filesystem::path& gameDir);

private:
    using Say = std::function<void(const std::string& step, const std::string& message, double fraction)>;
    /// A manifest that passed the signature check and the replay check.
    struct Source {
        GearBlocksManifest manifest;
        /// It is the cached one, the network could not be reached.
        bool offline = false;
    };

    std::optional<Source> LoadCached() const;
    bool FetchManifest(Source& source, InstallResult& failure, const Say& say, double from, double to);
    /// The release for the game's build (see GearBlocksManifest::Select), or why there is none.
    bool ChooseRelease(const Source& source, const std::string& build, const ManifestRelease*& release, InstallResult& failure) const;
    InstallResult LoaderWith(const Source& source, const Say& say, double from, double to, bool& installed);
    SyncResult SyncWith(const Source& source, const Say& say, double from, double to);
    Say MakeSay(const InstallReporter& report) const;
    std::filesystem::path WorkDir() const;
    bool SourceConfigured() const { return !m_options.manifestUrl.empty() && !m_options.trustedKeys.empty(); }

    Options m_options;
    std::atomic<bool> m_cancel = false;
};

}
