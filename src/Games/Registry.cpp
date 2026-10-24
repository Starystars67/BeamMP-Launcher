/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Games/Registry.h"

#include <stdexcept>

namespace Games {

void GameRegistry::Add(std::unique_ptr<GameModule> module) {
    if (Find(module->Id())) {
        throw std::invalid_argument("A game with the id " + module->Id() + " is already registered");
    }
    m_modules.push_back(std::move(module));
}

GameModule* GameRegistry::Find(const std::string& id) const {
    for (const auto& module : m_modules) {
        if (module->Id() == id) {
            return module.get();
        }
    }
    return nullptr;
}

}
