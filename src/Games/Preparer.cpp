/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Games/Preparer.h"

namespace Games {

GamePreparer::GamePreparer(const GameRegistry& games)
    : m_games(games) { }

GamePreparer::~GamePreparer() {
    std::vector<std::thread> workers;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        workers = std::move(m_workers);
    }
    for (auto& worker : workers) {
        worker.join();
    }
}

void GamePreparer::SetReporter(Reporter reporter) {
    m_reporter = std::move(reporter);
}

void GamePreparer::Report(const std::string& id, const PrepareResult& result) {
    if (m_reporter) {
        m_reporter(id, result);
    }
}

PrepareResult GamePreparer::Prepare(GameModule& module) {
    std::lock_guard<std::mutex> prepare(m_prepareMutex);
    PrepareResult result;
    try {
        result = module.Prepare();
    } catch (const std::exception& e) {
        // The interface says it never throws; a module that does is a failed game, not a dead engine.
        result = { GameStatus::Failed, e.what() };
    } catch (...) {
        result = { GameStatus::Failed, "unknown error" };
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_ready[module.Id()] = result.ok();
    }
    Report(module.Id(), result);
    return result;
}

bool GamePreparer::Reconfigure(const std::string& id, const GameConfig& config) {
    GameModule* module = m_games.Find(id);
    if (!module) {
        return false;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    m_ready[id] = false;
    m_workers.emplace_back([this, module, config] {
        {
            std::lock_guard<std::mutex> prepare(m_prepareMutex);
            module->Configure(config);
        }
        Report(module->Id(), { GameStatus::Preparing, "" });
        Prepare(*module);
    });
    return true;
}

bool GamePreparer::IsReady(const std::string& id) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto found = m_ready.find(id);
    return found != m_ready.end() && found->second;
}

}
