/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Games/GearBlocks.h"

#include "Logger.h"
#include "Net/Download.h"
#include "Options.h"
#include "Status.h"
#include <cstdlib>
#include <stdexcept>


namespace Games {

namespace fs = std::filesystem;

namespace {
    constexpr const char* PluginFile = "BepInEx/plugins/BeamMP.GearBlocks.dll";
    constexpr const char* LoaderFile = "winhttp.dll"; // BepInEx's Doorstop proxy
}

fs::path DefaultGearBlocksDir() {
#if defined(_WIN32)
    const char* programFiles = std::getenv("ProgramFiles(x86)");
    return fs::path(programFiles ? programFiles : "C:\\Program Files (x86)") / "Steam" / "steamapps" / "common" / "GearBlocks";
#else
    const char* home = std::getenv("HOME");
    fs::path base = home ? home : "";
#if defined(__APPLE__)
    return base / "Library" / "Application Support" / "Steam" / "steamapps" / "common" / "GearBlocks";
#else
    return base / ".local" / "share" / "Steam" / "steamapps" / "common" / "GearBlocks";
#endif
#endif
}

std::string CheckGearBlocksInstall(const fs::path& gameDir) {
    std::error_code ec;
    if (!fs::is_regular_file(gameDir / "GearBlocks.exe", ec)) {
        return "GearBlocks.exe was not found in " + gameDir.string() + " (set --gearblocks-dir)";
    }
    if (!fs::is_regular_file(gameDir / LoaderFile, ec)) {
        return "BepInEx is not installed in " + gameDir.string() + " (winhttp.dll is missing)";
    }
    if (!fs::is_regular_file(gameDir / PluginFile, ec)) {
        return "The BeamMP plugin is not installed in " + gameDir.string() + " (BepInEx/plugins/BeamMP.GearBlocks.dll is missing)";
    }
    return "";
}

LaunchSpec BuildGearBlocksLaunch(const fs::path& gameDir, const std::string& command,
    uint16_t bridgePort, const std::string& bridgeToken, const std::vector<std::string>& args) {
    LaunchSpec spec;
    if (!command.empty()) {
        spec = BuildCommandLaunch(GearBlocksId, gameDir, "", command, args);
    } else {
#if defined(_WIN32)
        spec.program = (gameDir / "GearBlocks.exe").string();
        spec.args = args;
        spec.workingDirectory = gameDir.string();
#else
        throw std::runtime_error("GearBlocks is a Windows game: on this system it has to be started through Wine, "
                                 "set a start command for it (a program that does that)");
#endif
    }
    spec.environment.emplace_back(BridgePortVariable, std::to_string(bridgePort));
    spec.environment.emplace_back(BridgeTokenVariable, bridgeToken);
    return spec;
}

GearBlocksModule::GearBlocksModule(GameConfig config, Lifecycle& lifecycle, ProcessRunner runner, ProcessStopper stopper)
    : m_config(std::move(config))
    , m_process(lifecycle, std::move(runner), std::move(stopper)) { }

GearBlocksModule::~GearBlocksModule() {
    m_process.Stop();
}

void GearBlocksModule::Configure(const GameConfig& config) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config = config;
}

GameConfig GearBlocksModule::Config() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_config;
}

fs::path GearBlocksModule::GameDir() const {
    GameConfig config = Config();
    return config.directory.empty() ? DefaultGearBlocksDir() : fs::path(config.directory);
}

void GearBlocksModule::SetInstallSource(std::string manifestUrl, std::vector<std::string> trustedKeys, bool allowLoopbackHttp, GearBlocksInstaller::Fetcher fetcher) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_manifestUrl = std::move(manifestUrl);
    m_trustedKeys = std::move(trustedKeys);
    m_allowLoopbackHttp = allowLoopbackHttp;
    m_fetcher = std::move(fetcher);
}

void GearBlocksModule::SetBundleDir(fs::path bundleDir) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_bundleDir = std::move(bundleDir);
}

namespace {
    // Copies a file of the bundle (never anything outside it) to where a download would have been put.
    Net::DownloadResult FetchFromBundle(const fs::path& bundleDir, const GearBlocksInstaller::FetchRequest& request) {
        Net::DownloadResult result;
        auto name = GearBlocksManifest::BundleName(request.url);
        if (!name || bundleDir.empty()) {
            result.error = "Not a file of this launcher's bundle";
            return result;
        }
        std::error_code ec;
        fs::path source = bundleDir / *name;
        auto size = fs::file_size(source, ec);
        if (ec || !fs::is_regular_file(source, ec)) {
            result.error = "The launcher's bundle has no " + *name;
            return result;
        }
        if (size > request.maxBytes) {
            result.error = *name + " is larger than allowed";
            return result;
        }
        if (request.progress && !request.progress(0, size)) {
            result.cancelled = true;
            result.error = "Cancelled";
            return result;
        }
        fs::copy_file(source, request.destination, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            result.error = ec.message();
            return result;
        }
        if (request.progress) {
            request.progress(size, size);
        }
        result.ok = true;
        return result;
    }
}

GearBlocksInstaller GearBlocksModule::MakeInstaller(bool quick) const {
    GearBlocksInstaller::Options installer;
    installer.gameDir = GameDir();
    installer.quickNetwork = quick;
    fs::path bundleDir;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        installer.manifestUrl = m_manifestUrl;
        installer.trustedKeys = m_trustedKeys;
        installer.allowLoopbackHttp = m_allowLoopbackHttp;
        installer.fetch = m_fetcher;
        bundleDir = m_bundleDir;
    }
    installer.allowBundle = !bundleDir.empty() && GearBlocksManifest::BundleName(installer.manifestUrl).has_value();
    if (!installer.fetch) {
        bool loopback = installer.allowLoopbackHttp;
        installer.fetch = [loopback, bundleDir](const GearBlocksInstaller::FetchRequest& request) {
            if (GearBlocksManifest::BundleName(request.url)) {
                return FetchFromBundle(bundleDir, request);
            }
            Net::DownloadOptions download;
            download.progress = request.progress;
            download.maxBytes = request.maxBytes;
            download.allowLoopbackHttp = loopback;
            if (request.quick) {
                download.connectTimeoutSeconds = 5;
                download.stalledSeconds = 10;
            }
            return Net::DownloadFile(request.url, request.destination, download);
        };
    }
    return GearBlocksInstaller(std::move(installer));
}

void GearBlocksModule::RefreshInstallState() {
    InstallState state = MakeInstaller().Inspect();
    std::lock_guard<std::mutex> lock(m_mutex);
    m_installState = state;
    m_installStateKnown = true;
}

nlohmann::json GearBlocksModule::InstallInfo() const {
    InstallState state;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_installStateKnown) {
            return nlohmann::json();
        }
        state = m_installState;
    }
    const char* name = "installed";
    if (!state.gameFound) {
        name = "game_missing";
    } else if (!state.loaderInstalled || !state.pluginInstalled || !state.modInstalled) {
        name = "needs_install";
    } else if (state.manifestKnown && !state.buildSupported && !state.gameBuild.empty()) {
        name = "unsupported_build";
    } else if (state.manifestKnown && (!state.pluginCurrent || !state.modCurrent)) {
        name = "outdated";
    }
    nlohmann::json info = { { "state", name }, { "files_available", state.sourceConfigured } };
    if (!state.installedVersion.empty()) {
        info["version"] = state.installedVersion;
    }
    if (!state.latestVersion.empty()) {
        info["latest_version"] = state.latestVersion;
    }
    if (!state.gameBuild.empty()) {
        info["game_build"] = state.gameBuild;
    }
    return info;
}

InstallResult GearBlocksModule::Install(const InstallReporter& progress) {
    GearBlocksInstaller installer = MakeInstaller();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_installing = &installer;
    }
    InstallResult result = installer.Install(progress);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_installing = nullptr;
    }
    RefreshInstallState();
    if (result.ok) {
        info("GearBlocks: multiplayer support installed");
    } else {
        warn("GearBlocks: installing multiplayer support failed (" + result.code + "): " + result.message);
    }
    return result;
}

InstallResult GearBlocksModule::Uninstall(bool removeLoader, const InstallReporter& progress) {
    GearBlocksInstaller installer = MakeInstaller();
    InstallResult result = installer.Uninstall(removeLoader, progress);
    RefreshInstallState();
    return result;
}

void GearBlocksModule::CancelInstall() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_installing) {
        m_installing->Cancel();
    }
}

PrepareResult GearBlocksModule::Prepare() {
    RefreshInstallState();
    std::string problem = CheckGearBlocksInstall(GameDir());
    if (!problem.empty()) {
        debug("GearBlocks is not offered: " + problem);
        return { GameStatus::NotFound, problem };
    }
    info("GearBlocks is available (" + GameDir().string() + ")");
    return {};
}

void GearBlocksModule::SetReadyHandler(std::function<void()> handler) {
    m_onReady = std::move(handler);
}

void GearBlocksModule::SetMessageHandler(std::function<void(const nlohmann::json&)> handler) {
    m_onMessage = std::move(handler);
}

void GearBlocksModule::Launch(const std::vector<std::string>& args) {
    if (options.no_launch) {
        throw std::runtime_error("launching is disabled by --no-launch");
    }
    GameConfig config = Config();
    fs::path gameDir = GameDir();
    // BeamMP's plugin and mod follow the signed manifest. When that cannot be reached (or is refused) what is installed
    // runs as it is: it was checked when it was put there.
    if (GearBlocksInstaller::LoaderPresent(gameDir)) {
        GearBlocksInstaller installer = MakeInstaller(true);
        if (installer.Inspect().sourceConfigured) {
            SyncResult synced = installer.Sync(nullptr);
            if (!synced.ok) {
                warn("GearBlocks: BeamMP's files were not brought up to date (" + synced.code + "): " + synced.message);
            } else if (synced.changed) {
                info("GearBlocks: BeamMP's files were updated");
            } else if (synced.offline) {
                info("GearBlocks: no network, using BeamMP's files as they are");
            }
            RefreshInstallState();
        }
    }
    std::string problem = CheckGearBlocksInstall(gameDir);
    if (!problem.empty()) {
        throw std::runtime_error(problem);
    }

    std::unique_ptr<GameBridge> bridge = std::make_unique<GameBridge>(GameBridge::GenerateToken());
    bridge->OnConnected([this] {
        info("GearBlocks: the BeamMP mod connected");
        if (m_onReady) {
            m_onReady();
        }
    });
    bridge->OnDisconnected([] { info("GearBlocks: the BeamMP mod disconnected"); });
    bridge->OnLine([this](const std::string& line) {
        nlohmann::json message = nlohmann::json::parse(line, nullptr, false);
        if (!message.is_object()) {
            debug("GearBlocks: dropped a message that is not a JSON object");
            return;
        }
        if (m_onMessage) {
            m_onMessage(message);
        }
    });
    uint16_t port = bridge->Start();
    LaunchSpec spec;
    try {
        spec = BuildGearBlocksLaunch(gameDir, config.command, port, bridge->Token(), args);
    } catch (...) {
        bridge->Stop();
        throw;
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_bridge = std::move(bridge);
    }
    m_process.Start(std::move(spec), [this] {
        std::unique_ptr<GameBridge> bridge;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            bridge = std::move(m_bridge);
        }
        if (bridge) {
            bridge->Stop();
        }
    });
}

bool GearBlocksModule::Stop() {
    return m_process.Stop();
}

bool GearBlocksModule::Send(const nlohmann::json& message) {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_bridge && m_bridge->Send(message.dump());
}

bool GearBlocksModule::Connected() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_bridge && m_bridge->Connected();
}

}
