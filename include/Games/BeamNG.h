/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include "Control/Lifecycle.h"
#include "Games/GameModule.h"
#include "Games/ManagedProcess.h"
#include <atomic>
#include <mutex>

namespace Games {

constexpr const char* BeamNGId = "beamng";
constexpr const char* BeamNGName = "BeamNG.drive";

/// BeamNG.drive: finds the install, checks it, installs the BeamMP mod, starts the game. The network side
/// (CoreNetwork, the game's own proxy) is still the launcher's core and runs once this game is prepared.
///
/// Settings (GameConfig): `directory` is the install (it holds integrity.json) instead of looking in Steam,
/// `userPath` the folder that contains `current`, `command` a program that starts the game instead of its own
/// executable, which is how it runs under Wine (it gets the game folder in BEAMNG_DIR).
class BeamNGModule : public GameModule {
public:
    BeamNGModule(Lifecycle& lifecycle, ProcessRunner runner, ProcessStopper stopper);
    ~BeamNGModule() override;

    std::string Id() const override { return BeamNGId; }
    std::string Name() const override { return BeamNGName; }
    uint32_t SteamAppId() const override { return 284160; }
    void Configure(const GameConfig& config) override;
    GameConfig Config() const override;
    PrepareResult Prepare() override;
    void Launch(const std::vector<std::string>& args) override;
    bool Stop() override;

private:
    mutable std::mutex m_mutex;
    GameConfig m_config;
    // options.user_path points here while a user folder is configured
    std::string m_userPath;
    const char* m_cliUserPath;
    std::atomic<bool> m_proxyStarted = false;
    ManagedProcess m_process;
};

}
