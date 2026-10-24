/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include "Control/Lifecycle.h"
#include "Games/GameBridge.h"
#include "Games/GameConfig.h"
#include "Games/GameModule.h"
#include "Games/GearBlocksInstaller.h"
#include "Games/ManagedProcess.h"
#include "Games/Process.h"
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace Games {

/// Steam app id of GearBlocks.
constexpr const char* GearBlocksId = "gearblocks";
constexpr const char* GearBlocksName = "GearBlocks";

/// Variables the BepInEx plugin (gearblocks/plugin) reads to find the launcher.
constexpr const char* BridgePortVariable = "BEAMMP_BRIDGE_PORT";
constexpr const char* BridgeTokenVariable = "BEAMMP_BRIDGE_TOKEN";

/// Where Steam puts the game on this platform.
std::filesystem::path DefaultGearBlocksDir();

/// Empty when the game folder holds GearBlocks.exe and the BeamMP plugin with BepInEx, else why not.
std::string CheckGearBlocksInstall(const std::filesystem::path& gameDir);

/// With a `command` (the user's start script) that is what runs, on every system. Without one Windows starts
/// GearBlocks.exe itself; elsewhere the game is a Windows build run through Wine, which only the user's own
/// setup knows how to start, so a missing command is an error (std::runtime_error).
/// The bridge address is added to the environment either way.
LaunchSpec BuildGearBlocksLaunch(const std::filesystem::path& gameDir, const std::string& command,
    uint16_t bridgePort, const std::string& bridgeToken, const std::vector<std::string>& args);

/// GearBlocks: runs the game with a bridge for its BepInEx plugin. One launch at a time, the lifecycle
/// follows the process.
class GearBlocksModule : public GameModule {
public:
    GearBlocksModule(GameConfig config, Lifecycle& lifecycle, ProcessRunner runner, ProcessStopper stopper);
    ~GearBlocksModule() override;
    GearBlocksModule(const GearBlocksModule&) = delete;
    GearBlocksModule& operator=(const GearBlocksModule&) = delete;

    std::string Id() const override { return GearBlocksId; }
    std::string Name() const override { return GearBlocksName; }
    uint32_t SteamAppId() const override { return 1305080; }

    /// Where the game is and how it starts (see GameConfig). Takes effect with the next Prepare.
    void Configure(const GameConfig& config) override;
    GameConfig Config() const override;

    /// Game, BepInEx and the BeamMP plugin have to be installed (`game.install` does that).
    PrepareResult Prepare() override;

    /// The lifecycle is already LaunchingGame. Brings the plugin and the mod up to date with the signed manifest (when a
    /// source is configured; without a network what is installed is used), checks the install, opens the bridge, then starts the game
    /// on its own thread, which moves the lifecycle to GameRunning and, when the game ends, back to Ready.
    /// Throws std::runtime_error (the lifecycle is left for the caller to reset).
    void Launch(const std::vector<std::string>& args) override;

    bool Stop() override;

    /// Where BeamMP's files for the game come from: the signed manifest's address, the public keys it has to be signed by,
    /// whether plain http to this machine is accepted (development), and how a download is made (the system's by default).
    /// Without an address and a key the launcher cannot install or update anything (it still runs what is installed).
    void SetInstallSource(std::string manifestUrl, std::vector<std::string> trustedKeys, bool allowLoopbackHttp = false, GearBlocksInstaller::Fetcher fetcher = {});

    /// The files a launcher release ships with (`manifest.json`, its signature, the plugin, the mod zip): `bundle:<name>`
    /// addresses are answered from this folder instead of the network. Use with a `bundle:manifest.json` source.
    void SetBundleDir(std::filesystem::path bundleDir);
    bool SupportsInstall() const override { return true; }
    InstallResult Install(const InstallReporter& progress) override;
    InstallResult Uninstall(bool removeLoader, const InstallReporter& progress) override;
    void CancelInstall() override;
    nlohmann::json InstallInfo() const override;

    bool HasMessageChannel() const override { return true; }
    bool SupportsSessions() const override { return true; }
    void SetReadyHandler(std::function<void()> handler) override;

    /// Messages the game's mod sends (JSON objects; anything else is dropped). Set before Launch.
    void SetMessageHandler(std::function<void(const nlohmann::json&)> handler) override;

    /// False when the mod is not connected.
    bool Send(const nlohmann::json& message) override;

    bool Connected() const;

private:
    std::filesystem::path GameDir() const;
    GearBlocksInstaller MakeInstaller(bool quick = false) const;
    void RefreshInstallState();

    GameConfig m_config;
    ManagedProcess m_process;
    std::function<void(const nlohmann::json&)> m_onMessage;
    std::function<void()> m_onReady;

    mutable std::mutex m_mutex;
    std::unique_ptr<GameBridge> m_bridge;
    std::string m_manifestUrl;
    std::vector<std::string> m_trustedKeys;
    bool m_allowLoopbackHttp = false;
    std::filesystem::path m_bundleDir;
    GearBlocksInstaller::Fetcher m_fetcher;
    InstallState m_installState;
    bool m_installStateKnown = false;
    GearBlocksInstaller* m_installing = nullptr;
};

}
