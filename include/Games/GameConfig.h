/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include <string>

namespace Games {

/// What the user can say about where a game is and how it starts. Empty means "work it out yourself".
struct GameConfig {
    /// The game's folder (the one with its executable), instead of looking for it in Steam.
    std::string directory;
    /// A program that starts the game instead of the game's own executable, e.g. a script that runs it
    /// through Wine. It gets the user's launch arguments and the game folder in `<GAME>_DIR` and
    /// `BEAMMP_GAME_DIR` (`BEAMMP_USER_PATH` too when there is a user folder).
    std::string command;
    /// The game's folder for user data (BeamNG: the one that contains `current`).
    std::string userPath;

    bool operator==(const GameConfig&) const = default;
    bool empty() const { return directory.empty() && command.empty() && userPath.empty(); }
};

}
