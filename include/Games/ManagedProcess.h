/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include "Control/Lifecycle.h"
#include "Games/Process.h"
#include <functional>
#include <mutex>
#include <thread>

namespace Games {

/// Runs the game as a child process and keeps the engine's lifecycle in step with it: `GameRunning` once it
/// has started (and `GamePID` set), `Ready` with the exit code as detail when it ends. One at a time.
class ManagedProcess {
public:
    ManagedProcess(Lifecycle& lifecycle, ProcessRunner runner, ProcessStopper stopper);
    ~ManagedProcess();
    ManagedProcess(const ManagedProcess&) = delete;
    ManagedProcess& operator=(const ManagedProcess&) = delete;

    /// The lifecycle is already LaunchingGame. Returns at once; the game runs on its own thread.
    /// `ended` runs on that thread after the process ended, before the lifecycle goes back to Ready.
    void Start(LaunchSpec spec, std::function<void()> ended = {});

    /// False when nothing runs or the process could not be signalled.
    bool Stop();

private:
    void Run(LaunchSpec spec, std::function<void()> ended);

    Lifecycle& m_lifecycle;
    const ProcessRunner m_runner;
    const ProcessStopper m_stopper;
    std::mutex m_mutex;
    std::thread m_thread;
    unsigned long m_pid = 0;
};

}
