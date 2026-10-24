/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include "Games/GameModule.h"
#include <memory>
#include <vector>

namespace Games {

/// The games this launcher can run, in the order a UI lists them.
class GameRegistry {
public:
    /// Throws std::invalid_argument for a second module with the same id.
    void Add(std::unique_ptr<GameModule> module);

    const std::vector<std::unique_ptr<GameModule>>& All() const { return m_modules; }
    GameModule* Find(const std::string& id) const;

private:
    std::vector<std::unique_ptr<GameModule>> m_modules;
};

/// BeamNG.drive always. The other games only when the launcher is controlled by a UI (the console launcher
/// has only ever been for BeamNG).
GameRegistry BuildBuiltInGames();

}
