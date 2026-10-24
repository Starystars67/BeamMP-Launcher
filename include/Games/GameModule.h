/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include "Games/GameConfig.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <vector>

namespace Games {

/// How a game stands after the engine looked at it.
enum class GameStatus {
    /// Being (re)prepared, for instance after its settings changed.
    Preparing,
    /// Installed and prepared: it can be launched.
    Available,
    /// Not installed (or its mod/plugin is not): the UI says so, nothing else is affected.
    NotFound,
    /// Listed, but there is nothing to launch (no support for it yet).
    Unavailable,
    /// Something failed while preparing it; `detail` has the reason.
    Failed,
};

struct PrepareResult {
    GameStatus status = GameStatus::Available;
    std::string detail;

    bool ok() const { return status == GameStatus::Available; }
};

/// What an install or uninstall of a game's multiplayer support is doing.
struct InstallProgress {
    /// A short stable name of the step: check, loader, plugin, mod, record, done, ...
    std::string step;
    /// For the user, in English.
    std::string message;
    /// 0 to 1 within the whole job, or -1 when not known.
    double fraction = -1;
};

struct InstallResult {
    bool ok = false;
    /// Stable code of what went wrong (game_not_found, unsupported_build, download_failed, bad_checksum, not_writable,
    /// cancelled, ...), empty when ok.
    std::string code;
    std::string message;
};

using InstallReporter = std::function<void(const InstallProgress&)>;

/// Thrown by game specific code that cannot go on because the game is missing (the engine then
/// reports that one game as not found instead of ending).
class GameNotFound : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// Everything that is specific to one game. The engine (lifecycle, account, control protocol) only talks
/// to this interface, so a game is added by implementing it and registering it, nothing else changes.
class GameModule {
public:
    virtual ~GameModule() = default;

    /// Stable id used in the control protocol, e.g. "beamng".
    virtual std::string Id() const = 0;
    /// Name for the UI.
    virtual std::string Name() const = 0;

    /// The game's Steam app id, 0 when it has none. A UI uses it to show the game's cover from the user's own
    /// Steam library (nothing is bundled or downloaded).
    virtual uint32_t SteamAppId() const { return 0; }

    /// The user's settings for this game; they take effect with the next Prepare. Modules without
    /// settings ignore them.
    virtual void Configure(const GameConfig& config) { (void)config; }
    virtual GameConfig Config() const { return {}; }

    /// Finds the game and makes it ready (updates, mods, plugins). May take a while and may use the
    /// network. Never throws: a game that cannot be prepared reports why.
    virtual PrepareResult Prepare() = 0;

    /// The lifecycle is already LaunchingGame; the module moves it on (GameRunning, then Ready when the
    /// game ends). Throws std::runtime_error when it cannot start at all (the caller resets the lifecycle).
    virtual void Launch(const std::vector<std::string>& args) = 0;

    /// Ends the running game; false when it is not running or could not be signalled.
    virtual bool Stop() = 0;

    /// Whether the module can set up (and remove) what multiplayer needs in the game's folder.
    virtual bool SupportsInstall() const { return false; }

    /// Sets it up; blocks, reports progress, never throws. At most one at a time (the caller sees to that), and not while
    /// the game runs.
    virtual InstallResult Install(const InstallReporter& progress) {
        (void)progress;
        return { false, "unsupported", "This game has nothing to install" };
    }

    /// Takes it out again (the loader too when asked).
    virtual InstallResult Uninstall(bool removeLoader, const InstallReporter& progress) {
        (void)removeLoader;
        (void)progress;
        return { false, "unsupported", "This game has nothing to uninstall" };
    }

    /// Asks a running Install to stop at the next chance.
    virtual void CancelInstall() { }

    /// Where the game's multiplayer support stands, for the UI: `{"state": ...}` (installed, needs_install,
    /// outdated, game_missing, unsupported_build) and more; null for a game without an install.
    virtual nlohmann::json InstallInfo() const { return nlohmann::json(); }

    /// Whether the mod of this game can exchange messages with the controller through the engine.
    virtual bool HasMessageChannel() const { return false; }

    /// Sends one JSON message to the game's mod; false when it is not connected.
    virtual bool Send(const nlohmann::json& message) {
        (void)message;
        return false;
    }

    /// Whether players of this game can meet in relay rooms (the game has a host role and a mod that can carry it).
    virtual bool SupportsSessions() const { return false; }

    /// Called when the mod's message channel has connected (a good time to tell it the session). Set once.
    virtual void SetReadyHandler(std::function<void()> handler) { (void)handler; }

    /// Messages the mod sends. Set once, before Launch.
    virtual void SetMessageHandler(std::function<void(const nlohmann::json&)> handler) { (void)handler; }
};

}
