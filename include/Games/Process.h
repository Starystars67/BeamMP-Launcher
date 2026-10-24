/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace Games {

struct LaunchSpec {
    std::string program;
    std::vector<std::string> args;
    /// Added to the launcher's own environment.
    std::vector<std::pair<std::string, std::string>> environment;
    std::string workingDirectory;
};

struct RunResult {
    bool launched = false;
    int exitCode = 0;
    std::string error;
};

/// Starts the process, calls `started` with its pid, and blocks until it exits.
using ProcessRunner = std::function<RunResult(const LaunchSpec&, const std::function<void(unsigned long pid)>& started)>;
using ProcessStopper = std::function<bool(unsigned long pid)>;

ProcessRunner SystemProcessRunner();
/// Ends the process and, outside Windows, everything it started (it runs in its own process group).
bool StopSystemProcess(unsigned long pid);

/// A launch of `command` (a script or program the user chose) for a game: `gameKey` names the
/// `<GAME>_DIR` variable (e.g. "beamng" gives BEAMNG_DIR). The game folder, user folder and arguments are passed on.
LaunchSpec BuildCommandLaunch(const std::string& gameKey, const std::filesystem::path& gameDir, const std::string& userPath,
    const std::string& command, const std::vector<std::string>& args);

}
