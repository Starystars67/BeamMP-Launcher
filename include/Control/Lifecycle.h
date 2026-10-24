/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include "Observers.h"
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>

enum class EngineState {
    Starting,
    Updating,
    PreparingGame,
    Ready,
    LaunchingGame,
    GameRunning,
    Error,
    Fatal,
};

const char* ToString(EngineState state);
bool CanTransition(EngineState from, EngineState to);

/// The engine's stage, reported to whoever observes it. Fatal is terminal.
class Lifecycle {
public:
    /// Observers must not call back into Set or TryTransition.
    using StateObservers = Observers<EngineState, EngineState, const std::string&>;

    EngineState State() const;
    std::string Detail() const;

    /// Returns false (and changes nothing) when the transition is not allowed.
    bool Set(EngineState state, std::string detail = "");
    bool TryTransition(EngineState from, EngineState to, std::string detail = "");

    /// Observers get (previous, current, detail) after every change.
    StateObservers::Id AddObserver(StateObservers::Callback callback);
    void RemoveObserver(StateObservers::Id id);

    void RequestShutdown();
    bool ShutdownRequested() const;
    /// True once shutdown was requested.
    bool WaitForShutdown(std::chrono::milliseconds timeout);

private:
    bool Change(std::optional<EngineState> from, EngineState to, std::string detail);

    mutable std::mutex m_mutex;
    std::mutex m_notifyMutex;
    std::condition_variable m_shutdownWake;
    EngineState m_state = EngineState::Starting;
    std::string m_detail;
    bool m_shutdown = false;
    StateObservers m_observers;
};

/// The engine's own lifecycle.
Lifecycle& GetLifecycle();
