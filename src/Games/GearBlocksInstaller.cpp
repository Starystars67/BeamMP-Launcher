/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Games/GearBlocksInstaller.h"

#include "Net/Sha256.h"
#include "Net/Unzip.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>
#include <set>

namespace Games {

namespace fs = std::filesystem;

namespace {
    constexpr const char* ExeName = "GearBlocks.exe";
    constexpr const char* DataFolder = "GearBlocks_Data";
    constexpr const char* RecordName = "beammp-install.json";
    constexpr const char* ManifestCacheName = "beammp-manifest.json";
    constexpr const char* SignatureCacheName = "beammp-manifest.json.sig";

    constexpr uint64_t MaxManifestBytes = 256 * 1024;
    constexpr uint64_t MaxSignatureBytes = 8 * 1024;
    constexpr uint64_t MaxLoaderBytes = 256ull * 1024 * 1024;
    constexpr uint64_t MaxPluginBytes = 32ull * 1024 * 1024;
    constexpr uint64_t MaxModBytes = 32ull * 1024 * 1024;

    // "1.2.3": digits in three groups.
    bool LooksLikeVersion(const char* text, size_t length) {
        int dots = 0;
        bool digitSeen = false;
        for (size_t i = 0; i < length; i++) {
            char c = text[i];
            if (c == '.') {
                if (!digitSeen) {
                    return false;
                }
                dots++;
                digitSeen = false;
            } else if (std::isdigit(static_cast<unsigned char>(c))) {
                digitSeen = true;
            } else {
                return false;
            }
        }
        return dots == 2 && digitSeen;
    }

    std::optional<std::string> ReadFile(const fs::path& file, uint64_t limit) {
        std::error_code ec;
        auto size = fs::file_size(file, ec);
        if (ec || size > limit) {
            return std::nullopt;
        }
        std::ifstream stream(file, std::ios::binary);
        if (!stream) {
            return std::nullopt;
        }
        return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    }

    // Writes next to the target and moves it over, so a game that is reading it never sees half a file.
    void CopyReplace(const fs::path& from, const fs::path& to) {
        fs::create_directories(to.parent_path());
        fs::path temporary = to;
        temporary += ".beammp-new";
        fs::copy_file(from, temporary, fs::copy_options::overwrite_existing);
        std::error_code ec;
        fs::rename(temporary, to, ec);
        if (ec) {
            fs::remove(to, ec);
            fs::rename(temporary, to);
        }
    }

    void WriteReplace(const fs::path& to, const std::string& bytes) {
        fs::path temporary = to;
        temporary += ".beammp-new";
        {
            std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
            out << bytes;
            out.close();
            if (!out) {
                throw fs::filesystem_error("Cannot write the file", to, std::make_error_code(std::errc::io_error));
            }
        }
        std::error_code ec;
        fs::rename(temporary, to, ec);
        if (ec) {
            fs::remove(to, ec);
            fs::rename(temporary, to);
        }
    }

    std::vector<fs::path> FilesBelow(const fs::path& root) {
        std::vector<fs::path> files;
        std::error_code ec;
        for (auto it = fs::recursive_directory_iterator(root, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (it->is_regular_file(ec)) {
                files.push_back(fs::relative(it->path(), root, ec));
            }
        }
        std::sort(files.begin(), files.end());
        return files;
    }

    InstallResult Fail(const std::string& code, const std::string& message) { return { false, code, message }; }

    InstallResult FromFilesystemError(const fs::filesystem_error& e) {
        if (e.code() == std::errc::permission_denied || e.code() == std::errc::read_only_file_system) {
            return Fail("not_writable", "The game's folder cannot be changed (" + std::string(e.what()) + "). Run the launcher with the rights to write there, or move the game.");
        }
        return Fail("io_error", e.what());
    }

    SyncResult SyncFail(const InstallResult& failure) { return { false, failure.code, failure.message, false, false }; }

    void RemoveEmptyDirectories(fs::path directory, const fs::path& stop) {
        std::error_code ec;
        while (directory != stop && directory.has_parent_path() && fs::is_empty(directory, ec) && !ec) {
            fs::remove(directory, ec);
            directory = directory.parent_path();
        }
    }

    // Removes what it was given when it goes out of scope.
    struct Cleanup {
        std::vector<fs::path> paths;
        ~Cleanup() {
            std::error_code ignored;
            for (const auto& path : paths) {
                fs::remove_all(path, ignored);
            }
        }
    };

    std::string Stamp() { return std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()); }

    nlohmann::json ReadRecord(const fs::path& gameDir) {
        auto text = ReadFile(GearBlocksInstaller::RecordPath(gameDir), 4 * 1024 * 1024);
        nlohmann::json record = text ? nlohmann::json::parse(*text, nullptr, false) : nlohmann::json();
        return record.is_object() ? record : nlohmann::json::object();
    }

    void WriteRecord(const fs::path& gameDir, const nlohmann::json& record) {
        std::ofstream out(GearBlocksInstaller::RecordPath(gameDir), std::ios::trunc);
        out << record.dump(2) << "\n";
        out.close();
        if (!out) {
            throw fs::filesystem_error("Cannot write the record of what was installed", GearBlocksInstaller::RecordPath(gameDir), std::make_error_code(std::errc::io_error));
        }
    }

    // The mod's files the record names, with their checksums ("files" is an object of path -> sha256).
    std::map<std::string, std::string> RecordedModFiles(const nlohmann::json& record) {
        std::map<std::string, std::string> files;
        if (!record.contains("mod") || !record["mod"].is_object()) {
            return files;
        }
        const auto& list = record["mod"].value("files", nlohmann::json::object());
        if (list.is_object()) {
            for (auto it = list.begin(); it != list.end(); ++it) {
                if (it.value().is_string()) {
                    files[it.key()] = it.value().get<std::string>();
                }
            }
        } else if (list.is_array()) {
            for (const auto& name : list) {
                if (name.is_string()) {
                    files[name.get<std::string>()] = "";
                }
            }
        }
        return files;
    }

    bool PluginMatches(const fs::path& gameDir, const ManifestRelease& release) {
        std::error_code ec;
        if (!fs::is_regular_file(GearBlocksInstaller::PluginPath(gameDir), ec)) {
            return false;
        }
        auto sum = Net::Sha256FileHex(GearBlocksInstaller::PluginPath(gameDir));
        return sum && *sum == release.plugin.sha256;
    }

    // What the record says was installed from this release's zip is still there, byte for byte.
    bool ModMatches(const fs::path& gameDir, const nlohmann::json& record, const ManifestRelease& release) {
        if (!record.contains("mod") || !record["mod"].is_object() || record["mod"].value("archive_sha256", "") != release.mod.sha256) {
            return false;
        }
        auto files = RecordedModFiles(record);
        if (files.find("main.lua") == files.end()) {
            return false;
        }
        for (const auto& [name, sum] : files) {
            auto actual = Net::Sha256FileHex(GearBlocksInstaller::ModPath(gameDir) / fs::path(name));
            if (!actual || *actual != sum) {
                return false;
            }
        }
        return true;
    }

    std::string BuildList(const GearBlocksManifest& manifest) {
        std::set<std::string> builds;
        for (const auto& release : manifest.releases) {
            builds.insert(release.gameBuilds.begin(), release.gameBuilds.end());
        }
        std::string list;
        for (const auto& build : builds) {
            list += (list.empty() ? "" : ", ") + build;
        }
        return list;
    }

    std::vector<std::string> PreferredBuilds(const GearBlocksManifest& manifest) {
        std::vector<std::string> builds;
        for (const auto& release : manifest.releases) {
            builds.insert(builds.end(), release.gameBuilds.begin(), release.gameBuilds.end());
        }
        return builds;
    }
}

GearBlocksInstaller::GearBlocksInstaller(Options options)
    : m_options(std::move(options)) { }

fs::path GearBlocksInstaller::PluginPath(const fs::path& gameDir) {
    return gameDir / "BepInEx" / "plugins" / "BeamMP.GearBlocks.dll";
}

fs::path GearBlocksInstaller::ModPath(const fs::path& gameDir) {
    return gameDir / DataFolder / "StreamingAssets" / "ScriptMods" / "BeamMP";
}

fs::path GearBlocksInstaller::RecordPath(const fs::path& gameDir) {
    return gameDir / RecordName;
}

fs::path GearBlocksInstaller::CachedManifestPath(const fs::path& gameDir) {
    return gameDir / ManifestCacheName;
}

fs::path GearBlocksInstaller::CachedSignaturePath(const fs::path& gameDir) {
    return gameDir / SignatureCacheName;
}

bool GearBlocksInstaller::LoaderPresent(const fs::path& gameDir) {
    std::error_code ec;
    return fs::is_regular_file(gameDir / "winhttp.dll", ec) && fs::is_regular_file(gameDir / "BepInEx" / "core" / "BepInEx.Unity.IL2CPP.dll", ec);
}

std::string GearBlocksInstaller::ReadGameBuild(const fs::path& gameDir, const std::vector<std::string>& preferred) {
    auto data = ReadFile(gameDir / DataFolder / "globalgamemanagers", 256ull * 1024 * 1024);
    if (!data) {
        return "";
    }
    // Unity keeps its strings as a 32 bit length (little endian) and the characters
    std::vector<std::string> found;
    const std::string& bytes = *data;
    for (size_t i = 0; i + 4 < bytes.size(); i++) {
        uint32_t length = static_cast<uint8_t>(bytes[i]) | (static_cast<uint8_t>(bytes[i + 1]) << 8) | (static_cast<uint8_t>(bytes[i + 2]) << 16) | (static_cast<uint32_t>(static_cast<uint8_t>(bytes[i + 3])) << 24);
        if (length < 5 || length > 16 || i + 4 + length > bytes.size()) {
            continue;
        }
        if (LooksLikeVersion(bytes.data() + i + 4, length)) {
            std::string text = bytes.substr(i + 4, length);
            if (std::find(found.begin(), found.end(), text) == found.end()) {
                found.push_back(text);
            }
        }
    }
    for (const auto& want : preferred) {
        if (std::find(found.begin(), found.end(), want) != found.end()) {
            return want;
        }
    }
    return found.empty() ? "" : found.front();
}

fs::path GearBlocksInstaller::WorkDir() const {
    return m_options.workDir.empty() ? fs::temp_directory_path() : m_options.workDir;
}

GearBlocksInstaller::Say GearBlocksInstaller::MakeSay(const InstallReporter& report) const {
    return [report](const std::string& step, const std::string& message, double fraction) {
        if (report) {
            report({ step, message, fraction });
        }
    };
}

std::optional<GearBlocksInstaller::Source> GearBlocksInstaller::LoadCached() const {
    if (!SourceConfigured()) {
        return std::nullopt;
    }
    auto text = ReadFile(CachedManifestPath(m_options.gameDir), MaxManifestBytes);
    auto signature = ReadFile(CachedSignaturePath(m_options.gameDir), MaxSignatureBytes);
    if (!text || !signature || !VerifyManifestSignature(*text, *signature, m_options.trustedKeys)) {
        return std::nullopt;
    }
    std::string error;
    auto manifest = GearBlocksManifest::Parse(*text, m_options.allowLoopbackHttp, error, m_options.allowBundle);
    if (!manifest) {
        return std::nullopt;
    }
    return Source { std::move(*manifest), true };
}

InstallState GearBlocksInstaller::Inspect() const {
    InstallState state;
    std::error_code ec;
    const fs::path& game = m_options.gameDir;
    state.sourceConfigured = SourceConfigured();
    state.gameFound = fs::is_regular_file(game / ExeName, ec);
    if (!state.gameFound) {
        return state;
    }
    auto cached = LoadCached();
    state.manifestKnown = cached.has_value();
    state.gameBuild = ReadGameBuild(game, cached ? PreferredBuilds(cached->manifest) : std::vector<std::string> {});
    state.loaderInstalled = LoaderPresent(game);
    state.pluginInstalled = fs::is_regular_file(PluginPath(game), ec);
    state.modInstalled = fs::is_regular_file(ModPath(game) / "main.lua", ec);
    nlohmann::json record = ReadRecord(game);
    state.installedVersion = record.value("version", "");
    if (cached) {
        const ManifestRelease* release = nullptr;
        auto pick = cached->manifest.Select(state.gameBuild, release);
        if (release) {
            state.latestVersion = release->version;
            state.pluginCurrent = state.pluginInstalled && PluginMatches(game, *release);
            state.modCurrent = state.modInstalled && ModMatches(game, record, *release);
        } else if (pick == GearBlocksManifest::Pick::UnsupportedBuild) {
            state.buildSupported = false;
        }
    }
    return state;
}

bool GearBlocksInstaller::FetchManifest(Source& source, InstallResult& failure, const Say& say, double from, double to) {
    if (!SourceConfigured()) {
        failure = Fail("manifest_unconfigured", "This launcher is not set up to get BeamMP's GearBlocks files: it needs the address of the manifest (--gearblocks-manifest) and the public key to trust it by (--gearblocks-trust).");
        return false;
    }
    if (!m_options.fetch) {
        failure = Fail("download_failed", "Downloading is not available");
        return false;
    }
    say("manifest", "Checking for BeamMP's GearBlocks files", from);
    std::error_code ec;
    fs::path work = WorkDir();
    fs::create_directories(work, ec);
    std::string stamp = Stamp();
    fs::path manifestFile = work / ("beammp-manifest-" + stamp + ".json");
    fs::path signatureFile = work / ("beammp-manifest-" + stamp + ".json.sig");
    Cleanup cleanup { { manifestFile, signatureFile } };

    auto fetch = [&](const std::string& url, const fs::path& destination, uint64_t limit) {
        FetchRequest request;
        request.url = url;
        request.destination = destination;
        request.maxBytes = limit;
        request.quick = true;
        request.progress = [&](uint64_t, uint64_t) { return !m_cancel.load(); };
        return m_options.fetch(request);
    };
    auto manifestDownload = fetch(m_options.manifestUrl, manifestFile, MaxManifestBytes);
    Net::DownloadResult signatureDownload;
    if (manifestDownload.ok) {
        signatureDownload = fetch(m_options.manifestUrl + ".sig", signatureFile, MaxSignatureBytes);
    }
    if (!manifestDownload.ok || !signatureDownload.ok) {
        const auto& broken = manifestDownload.ok ? signatureDownload : manifestDownload;
        if (broken.cancelled || m_cancel) {
            failure = Fail("cancelled", "Cancelled");
            return false;
        }
        if (auto cached = LoadCached()) {
            cached->offline = true;
            source = std::move(*cached);
            say("manifest", "BeamMP's file list could not be fetched, using the last one that was checked", to);
            return true;
        }
        failure = Fail("download_failed", "BeamMP's file list could not be fetched: " + broken.error);
        return false;
    }

    auto text = ReadFile(manifestFile, MaxManifestBytes);
    auto signature = ReadFile(signatureFile, MaxSignatureBytes);
    if (!text || !signature || !VerifyManifestSignature(*text, *signature, m_options.trustedKeys)) {
        failure = Fail("bad_signature", "The list of BeamMP's GearBlocks files is not signed by a key this launcher trusts; it was not used.");
        return false;
    }
    std::string error;
    auto manifest = GearBlocksManifest::Parse(*text, m_options.allowLoopbackHttp, error, m_options.allowBundle);
    if (!manifest) {
        failure = Fail("bad_manifest", error);
        return false;
    }
    auto known = LoadCached();
    if (known && manifest->sequence < known->manifest.sequence) {
        failure = Fail("manifest_rollback", "The list of BeamMP's GearBlocks files is older than one this launcher has already seen; it was not used.");
        return false;
    }
    // remembered for the next time without a network, and for the replay check
    try {
        WriteReplace(CachedManifestPath(m_options.gameDir), *text);
        WriteReplace(CachedSignaturePath(m_options.gameDir), *signature);
    } catch (const fs::filesystem_error&) {
        // a folder that cannot be written fails the install itself, with a better message
    }
    source = Source { std::move(*manifest), false };
    say("manifest", "BeamMP's file list is verified", to);
    return true;
}

bool GearBlocksInstaller::ChooseRelease(const Source& source, const std::string& build, const ManifestRelease*& release, InstallResult& failure) const {
    auto pick = source.manifest.Select(build, release);
    if (pick == GearBlocksManifest::Pick::Found) {
        return true;
    }
    if (pick == GearBlocksManifest::Pick::UnsupportedBuild && m_options.allowUnsupportedBuild) {
        pick = source.manifest.Select("", release);
        if (pick == GearBlocksManifest::Pick::Found) {
            return true;
        }
    }
    if (pick == GearBlocksManifest::Pick::LauncherTooOld) {
        failure = Fail("launcher_too_old", "BeamMP's plugin for GearBlocks build " + build + " needs a newer launcher. Update the launcher.");
    } else {
        failure = Fail("unsupported_build", "GearBlocks build " + build + " is not one BeamMP's plugin was made for (" + BuildList(source.manifest) + "). Updating the launcher or waiting for a new plugin may help.");
    }
    return false;
}

InstallResult GearBlocksInstaller::LoaderWith(const Source& source, const Say& sayRaw, double from, double to, bool& installed) {
    auto say = [&](const std::string& step, const std::string& message, double fraction) { sayRaw(step, message, from + (to - from) * fraction); };
    installed = false;
    const fs::path& game = m_options.gameDir;
    std::error_code ec;
    if (LoaderPresent(game)) {
        say("loader", "BepInEx is installed", 1.0);
        return { true, "", "" };
    }
    if (fs::exists(game / "winhttp.dll", ec) || fs::exists(game / "doorstop_config.ini", ec)) {
        return Fail("loader_conflict", "The game's folder has a winhttp.dll or doorstop_config.ini that is not BepInEx's (another mod loader?). It is left alone: remove it first to install BeamMP's loader.");
    }
    if (!m_options.fetch) {
        return Fail("download_failed", "Downloading is not available");
    }
    fs::path work = WorkDir();
    fs::create_directories(work, ec);
    fs::path archive = work / ("beammp-loader-" + Stamp() + ".zip");
    Cleanup cleanup { { archive } };
    const std::string label = "Downloading BepInEx " + source.manifest.loaderVersion;
    say("loader", label, 0.0);
    FetchRequest request;
    request.url = source.manifest.loader.url;
    request.destination = archive;
    request.maxBytes = MaxLoaderBytes;
    request.quick = m_options.quickNetwork;
    request.progress = [&](uint64_t done, uint64_t total) {
        if (total > 0) {
            say("loader", label, 0.6 * static_cast<double>(done) / static_cast<double>(total));
        }
        return !m_cancel.load();
    };
    auto download = m_options.fetch(request);
    if (!download.ok) {
        return download.cancelled || m_cancel ? Fail("cancelled", "Cancelled") : Fail("download_failed", "The download of BepInEx failed: " + download.error);
    }
    say("loader", "Checking the download", 0.62);
    auto sum = Net::Sha256FileHex(archive);
    if (!sum || *sum != source.manifest.loader.sha256) {
        return Fail("bad_checksum", "The downloaded BepInEx is not the one BeamMP's list names (checksum mismatch); it was not used.");
    }
    if (m_cancel) {
        return Fail("cancelled", "Cancelled");
    }
    say("loader", "Unpacking BepInEx", 0.64);
    auto unpacked = Net::Unzip(archive, game, {}, [&](size_t done, size_t total) {
        say("loader", "Unpacking BepInEx", 0.64 + 0.3 * static_cast<double>(done) / static_cast<double>(std::max<size_t>(total, 1)));
    });
    if (!unpacked.ok) {
        for (const auto& f : unpacked.files) {
            fs::remove(game / fs::path(f), ec);
        }
        return Fail("extract_failed", unpacked.error);
    }
    nlohmann::json record = ReadRecord(game);
    record["loader"] = { { "version", source.manifest.loaderVersion }, { "files", unpacked.files } };
    WriteRecord(game, record);
    installed = true;
    say("loader", "BepInEx is installed", 1.0);
    return { true, "", "" };
}

SyncResult GearBlocksInstaller::SyncWith(const Source& source, const Say& sayRaw, double from, double to) {
    auto say = [&](const std::string& step, const std::string& message, double fraction) { sayRaw(step, message, from + (to - from) * fraction); };
    const fs::path& game = m_options.gameDir;
    std::error_code ec;
    std::string build = ReadGameBuild(game, PreferredBuilds(source.manifest));
    const ManifestRelease* release = nullptr;
    InstallResult failure;
    if (!ChooseRelease(source, build, release, failure)) {
        return SyncFail(failure);
    }
    nlohmann::json record = ReadRecord(game);
    bool pluginNeeded = !PluginMatches(game, *release);
    bool modNeeded = !ModMatches(game, record, *release);
    SyncResult result;
    result.ok = true;
    result.offline = source.offline;
    if (!pluginNeeded && !modNeeded) {
        if (record.value("version", "") != release->version) {
            record["version"] = release->version;
            WriteRecord(game, record);
        }
        say("done", "BeamMP's files for GearBlocks are up to date", 1.0);
        return result;
    }
    if (!m_options.fetch) {
        return SyncFail(Fail("download_failed", "Downloading is not available"));
    }
    fs::path work = WorkDir();
    fs::create_directories(work, ec);
    std::string stamp = Stamp();
    fs::path pluginFile = work / ("beammp-plugin-" + stamp + ".dll");
    fs::path modArchive = work / ("beammp-mod-" + stamp + ".zip");
    fs::path modStage = work / ("beammp-mod-" + stamp);
    Cleanup cleanup { { pluginFile, modArchive, modStage } };

    auto download = [&](const ManifestFile& file, const fs::path& destination, uint64_t limit, const std::string& label, double lo, double hi) -> InstallResult {
        say("download", label, lo);
        FetchRequest request;
        request.url = file.url;
        request.destination = destination;
        request.maxBytes = limit;
        request.quick = m_options.quickNetwork;
        request.progress = [&](uint64_t done, uint64_t total) {
            if (total > 0) {
                say("download", label, lo + (hi - lo) * static_cast<double>(done) / static_cast<double>(total));
            }
            return !m_cancel.load();
        };
        auto got = m_options.fetch(request);
        if (!got.ok) {
            return got.cancelled || m_cancel ? Fail("cancelled", "Cancelled") : Fail("download_failed", "The download of " + label + " failed: " + got.error);
        }
        auto sum = Net::Sha256FileHex(destination);
        if (!sum || *sum != file.sha256) {
            return Fail("bad_checksum", "The downloaded " + label + " is not the one BeamMP's list names (checksum mismatch); it was not used.");
        }
        return { true, "", "" };
    };

    // everything is fetched and checked before the game's folder is touched
    if (pluginNeeded) {
        auto got = download(release->plugin, pluginFile, MaxPluginBytes, "the BeamMP plugin " + release->version, 0.0, 0.3);
        if (!got.ok) {
            return SyncFail(got);
        }
    }
    std::map<std::string, std::string> newModFiles;
    if (modNeeded) {
        auto got = download(release->mod, modArchive, MaxModBytes, "the BeamMP script mod " + release->version, 0.3, 0.6);
        if (!got.ok) {
            return SyncFail(got);
        }
        say("unpack", "Unpacking the BeamMP script mod", 0.6);
        Net::UnzipLimits limits;
        limits.maxTotalBytes = 64ull * 1024 * 1024;
        limits.maxFileBytes = 32ull * 1024 * 1024;
        limits.maxFiles = 500;
        auto unpacked = Net::Unzip(modArchive, modStage, limits);
        if (!unpacked.ok) {
            return SyncFail(Fail("extract_failed", unpacked.error));
        }
        for (const auto& name : unpacked.files) {
            auto sum = Net::Sha256FileHex(modStage / fs::path(name));
            if (!sum) {
                return SyncFail(Fail("extract_failed", "A file of the script mod could not be read back"));
            }
            newModFiles[name] = *sum;
        }
        if (newModFiles.find("main.lua") == newModFiles.end()) {
            return SyncFail(Fail("bad_manifest", "The script mod BeamMP's list names has no main.lua"));
        }
    }
    if (m_cancel) {
        return SyncFail(Fail("cancelled", "Cancelled"));
    }

    try {
        if (pluginNeeded) {
            say("plugin", "Installing the BeamMP plugin", 0.7);
            CopyReplace(pluginFile, PluginPath(game));
            result.changed = true;
        }
        if (modNeeded) {
            say("mod", "Installing the BeamMP script mod", 0.85);
            for (const auto& [name, sum] : newModFiles) {
                CopyReplace(modStage / fs::path(name), ModPath(game) / fs::path(name));
            }
            // what an earlier release had and this one does not
            for (const auto& [name, sum] : RecordedModFiles(record)) {
                std::string cleaned;
                if (newModFiles.find(name) == newModFiles.end() && Net::SafeArchivePath(name, cleaned)) {
                    fs::remove(ModPath(game) / fs::path(cleaned), ec);
                }
            }
            result.changed = true;
        }
        say("record", "Saving what was installed", 0.95);
        record["version"] = release->version;
        record["plugin"] = { { "path", "BepInEx/plugins/BeamMP.GearBlocks.dll" }, { "sha256", release->plugin.sha256 } };
        if (modNeeded) {
            record["mod"] = { { "folder", "GearBlocks_Data/StreamingAssets/ScriptMods/BeamMP" }, { "archive_sha256", release->mod.sha256 }, { "files", newModFiles } };
        }
        WriteRecord(game, record);
    } catch (const fs::filesystem_error& e) {
        return SyncFail(FromFilesystemError(e));
    }
    say("done", "BeamMP's files for GearBlocks are updated to " + release->version, 1.0);
    return result;
}

SyncResult GearBlocksInstaller::Sync(const InstallReporter& report) {
    m_cancel = false;
    Say say = MakeSay(report);
    try {
        const fs::path& game = m_options.gameDir;
        std::error_code ec;
        if (!fs::is_regular_file(game / ExeName, ec)) {
            return SyncFail(Fail("game_not_found", std::string(ExeName) + " was not found in " + game.string()));
        }
        if (!LoaderPresent(game)) {
            return SyncFail(Fail("loader_missing", "BepInEx is not installed in the game's folder, install it first."));
        }
        Source source;
        InstallResult failure;
        if (!FetchManifest(source, failure, say, 0.0, 0.1)) {
            return SyncFail(failure);
        }
        return SyncWith(source, say, 0.1, 1.0);
    } catch (const fs::filesystem_error& e) {
        return SyncFail(FromFilesystemError(e));
    } catch (const std::exception& e) {
        return SyncFail(Fail("io_error", e.what()));
    }
}

InstallResult GearBlocksInstaller::InstallLoader(const InstallReporter& report) {
    m_cancel = false;
    Say say = MakeSay(report);
    try {
        const fs::path& game = m_options.gameDir;
        std::error_code ec;
        if (!fs::is_regular_file(game / ExeName, ec)) {
            return Fail("game_not_found", std::string(ExeName) + " was not found in " + game.string());
        }
        Source source;
        InstallResult failure;
        if (!FetchManifest(source, failure, say, 0.0, 0.1)) {
            return failure;
        }
        bool installed = false;
        auto result = LoaderWith(source, say, 0.1, 1.0, installed);
        if (result.ok) {
            say("done", installed ? "BepInEx is installed. The first start of the game takes a few minutes longer (BepInEx prepares itself)." : "BepInEx is installed", 1.0);
        }
        return result;
    } catch (const fs::filesystem_error& e) {
        return FromFilesystemError(e);
    } catch (const std::exception& e) {
        return Fail("io_error", e.what());
    }
}

InstallResult GearBlocksInstaller::Install(const InstallReporter& report) {
    m_cancel = false;
    Say say = MakeSay(report);
    try {
        const fs::path& game = m_options.gameDir;
        std::error_code ec;
        say("check", "Checking the game", 0.0);
        if (!fs::is_regular_file(game / ExeName, ec)) {
            return Fail("game_not_found", std::string(ExeName) + " was not found in " + game.string());
        }
        Source source;
        InstallResult failure;
        if (!FetchManifest(source, failure, say, 0.02, 0.1)) {
            return failure;
        }
        // a game build without a release is found out before anything is downloaded
        std::string build = ReadGameBuild(game, PreferredBuilds(source.manifest));
        const ManifestRelease* release = nullptr;
        if (!ChooseRelease(source, build, release, failure)) {
            return failure;
        }
        say("check", build.empty() ? "Checking the game (its build could not be read)" : "GearBlocks build " + build, 0.1);

        bool loaderInstalled = false;
        auto loader = LoaderWith(source, say, 0.1, 0.6, loaderInstalled);
        if (!loader.ok) {
            return loader;
        }
        if (m_cancel) {
            return Fail("cancelled", "Cancelled");
        }
        auto synced = SyncWith(source, say, 0.6, 1.0);
        if (!synced.ok) {
            return { false, synced.code, synced.message };
        }
        say("done", loaderInstalled ? "Multiplayer support for GearBlocks is installed. The first start takes a few minutes longer (BepInEx prepares itself)." : "Multiplayer support for GearBlocks is installed", 1.0);
        return { true, "", "" };
    } catch (const fs::filesystem_error& e) {
        return FromFilesystemError(e);
    } catch (const std::exception& e) {
        return Fail("io_error", e.what());
    }
}

InstallResult GearBlocksInstaller::Uninstall(bool removeLoader, const InstallReporter& report) {
    Say say = MakeSay(report);
    try {
        const fs::path& game = m_options.gameDir;
        std::error_code ec;
        if (!fs::is_regular_file(game / ExeName, ec)) {
            return Fail("game_not_found", std::string(ExeName) + " was not found in " + game.string());
        }
        say("plugin", "Removing the BeamMP plugin", 0.1);
        fs::remove(PluginPath(game), ec);
        say("mod", "Removing the BeamMP script mod", 0.3);
        fs::remove_all(ModPath(game), ec);

        nlohmann::json record = ReadRecord(game);
        if (removeLoader) {
            say("loader", "Removing BepInEx", 0.5);
            if (!record.contains("loader")) {
                return Fail("not_ours", "BepInEx was not installed by BeamMP, so it is left in place");
            }
            std::set<fs::path> directories;
            for (const auto& f : record["loader"].value("files", nlohmann::json::array())) {
                std::string cleaned;
                if (!f.is_string() || !Net::SafeArchivePath(f.get<std::string>(), cleaned)) {
                    continue;
                }
                fs::path file = game / fs::path(cleaned);
                fs::remove(file, ec);
                directories.insert(file.parent_path());
            }
            // what BepInEx made while it ran
            for (const char* generated : { "interop", "cache", "unity-libs" }) {
                fs::remove_all(game / "BepInEx" / generated, ec);
            }
            for (const char* log : { "LogOutput.log", "ErrorLog.log" }) {
                fs::remove(game / "BepInEx" / log, ec);
            }
            fs::remove(game / "BepInEx" / "config" / "BepInEx.cfg", ec);
            directories.insert(game / "BepInEx" / "config");
            directories.insert(game / "BepInEx" / "plugins");
            directories.insert(game / "BepInEx" / "core");
            directories.insert(game / "BepInEx" / "patchers");
            directories.insert(game / "BepInEx");
            directories.insert(game / "dotnet");
            for (auto it = directories.rbegin(); it != directories.rend(); ++it) {
                RemoveEmptyDirectories(*it, game);
            }
            fs::remove(RecordPath(game), ec);
            fs::remove(CachedManifestPath(game), ec);
            fs::remove(CachedSignaturePath(game), ec);
        } else if (record.contains("loader")) {
            // what the loader was recorded to be stays on record, so that it can be taken out later
            nlohmann::json kept;
            kept["loader"] = record["loader"];
            WriteRecord(game, kept);
        } else {
            fs::remove(RecordPath(game), ec);
        }
        say("done", "Multiplayer support for GearBlocks is removed", 1.0);
        return { true, "", "" };
    } catch (const fs::filesystem_error& e) {
        return FromFilesystemError(e);
    } catch (const std::exception& e) {
        return Fail("io_error", e.what());
    }
}

}
