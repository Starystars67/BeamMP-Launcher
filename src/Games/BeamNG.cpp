/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Games/BeamNG.h"

#include "Games/Process.h"
#include "Http.h"
#include "Logger.h"
#include "Options.h"
#include "Security/Init.h"
#include <filesystem>
#include <stdexcept>

namespace Games {

namespace fs = std::filesystem;

BeamNGModule::BeamNGModule(Lifecycle& lifecycle, ProcessRunner runner, ProcessStopper stopper)
    : m_cliUserPath(options.user_path)
    , m_process(lifecycle, std::move(runner), std::move(stopper)) { }

BeamNGModule::~BeamNGModule() {
    m_process.Stop();
}

void BeamNGModule::Configure(const GameConfig& config) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config = config;
}

GameConfig BeamNGModule::Config() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_config;
}

PrepareResult BeamNGModule::Prepare() {
    GameConfig config = Config();

    // the user folder of the settings wins over `--user-path`
    if (config.userPath.empty()) {
        options.user_path = m_cliUserPath;
    } else {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_userPath = config.userPath;
        options.user_path = m_userPath.c_str();
    }

    ClearGameDir();
    if (!config.directory.empty()) {
        std::error_code ec;
        if (!fs::is_regular_file(fs::path(config.directory) / "integrity.json", ec)) {
            return { GameStatus::NotFound, "BeamNG.drive was not found in " + config.directory + " (integrity.json is missing)" };
        }
        SetGameDir(config.directory);
    } else {
        try {
            LegitimacyCheck();
        } catch (const GameNotFound& e) {
            return { GameStatus::NotFound, e.what() };
        } catch (const std::exception& e) {
            error("Failure in LegitimacyCheck: " + std::string(e.what()));
            return { GameStatus::Failed, e.what() };
        }
        if (GetGameDir().empty()) {
            return { GameStatus::NotFound, "BeamNG.drive was not found" };
        }
    }

    try {
        // preparing again after a change of settings must not start a second proxy
        if (!m_proxyStarted.exchange(true)) {
            HTTP::StartProxy();
        }
    } catch (const std::exception& e) {
        error(std::string("Failed to start HTTP proxy: Some in-game functions may not work. Error: ") + e.what());
    }
    try {
        PreGame(GetGameDir());
    } catch (const std::exception& e) {
        return { GameStatus::Failed, e.what() };
    }
    return {};
}

void BeamNGModule::Launch(const std::vector<std::string>& args) {
    if (options.no_launch) {
        throw std::runtime_error("launching is disabled by --no-launch");
    }
    GameConfig config = Config();
    if (config.command.empty()) {
        // the game's own executable, as the launcher always did
        LaunchGame(args);
        return;
    }
    m_process.Start(BuildCommandLaunch(BeamNGId, fs::path(GetGameDir()), config.userPath, config.command, args));
}

bool BeamNGModule::Stop() {
    return m_process.Stop() || StopGame();
}

}
