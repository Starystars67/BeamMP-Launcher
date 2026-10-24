/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Games/ManagedProcess.h"

#include "Logger.h"
#include "Status.h"

namespace Games {

ManagedProcess::ManagedProcess(Lifecycle& lifecycle, ProcessRunner runner, ProcessStopper stopper)
    : m_lifecycle(lifecycle)
    , m_runner(std::move(runner))
    , m_stopper(std::move(stopper)) { }

ManagedProcess::~ManagedProcess() {
    Stop();
    std::thread thread;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        thread = std::move(m_thread);
    }
    if (thread.joinable()) {
        thread.join();
    }
}

void ManagedProcess::Start(LaunchSpec spec, std::function<void()> ended) {
    // The previous run has ended (the lifecycle was Ready); its thread only has to be collected.
    std::thread previous;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        previous = std::move(m_thread);
    }
    if (previous.joinable()) {
        previous.join();
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    m_thread = std::thread(&ManagedProcess::Run, this, std::move(spec), std::move(ended));
}

void ManagedProcess::Run(LaunchSpec spec, std::function<void()> ended) {
    debug("Starting " + spec.program);
    RunResult result = m_runner(spec, [this](unsigned long pid) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_pid = pid;
        }
        GamePID = pid;
        info("Game launched!");
        m_lifecycle.Set(EngineState::GameRunning);
    });
    std::string detail;
    if (result.launched) {
        detail = "The game exited with code " + std::to_string(result.exitCode);
        info(detail);
    } else {
        detail = "Failed to launch the game: " + result.error;
        error(detail);
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pid = 0;
    }
    GamePID = 0;
    if (ended) {
        ended();
    }
    m_lifecycle.Set(EngineState::Ready, detail);
}

bool ManagedProcess::Stop() {
    unsigned long pid;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        pid = m_pid;
    }
    return pid != 0 && m_stopper(pid);
}

}
