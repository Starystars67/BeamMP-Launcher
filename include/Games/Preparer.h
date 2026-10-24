/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include "Games/Registry.h"
#include <functional>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

namespace Games {

/// Prepares the games of a registry, at startup and again when a game's settings change. Only one
/// preparation runs at a time (modules use shared state such as the BeamNG install folder).
class GamePreparer {
public:
    /// Told after every preparation (and when one starts, as `GameStatus::Preparing`).
    using Reporter = std::function<void(const std::string& id, const PrepareResult& result)>;

    explicit GamePreparer(const GameRegistry& games);
    ~GamePreparer();
    GamePreparer(const GamePreparer&) = delete;
    GamePreparer& operator=(const GamePreparer&) = delete;

    void SetReporter(Reporter reporter);

    /// Blocks. Never throws.
    PrepareResult Prepare(GameModule& module);

    /// Applies the settings and prepares that game again on its own thread; false for an unknown game.
    bool Reconfigure(const std::string& id, const GameConfig& config);

    /// Whether the game was prepared successfully (last time).
    bool IsReady(const std::string& id) const;

    const GameRegistry& Games() const { return m_games; }

private:
    void Report(const std::string& id, const PrepareResult& result);

    const GameRegistry& m_games;
    Reporter m_reporter;
    mutable std::mutex m_mutex;
    std::map<std::string, bool> m_ready;
    std::mutex m_prepareMutex;
    std::vector<std::thread> m_workers;
};

}
