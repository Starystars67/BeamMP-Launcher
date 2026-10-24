/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Control/Lifecycle.h"

const char* ToString(EngineState state) {
    switch (state) {
    case EngineState::Starting:
        return "starting";
    case EngineState::Updating:
        return "updating";
    case EngineState::PreparingGame:
        return "preparing_game";
    case EngineState::Ready:
        return "ready";
    case EngineState::LaunchingGame:
        return "launching_game";
    case EngineState::GameRunning:
        return "game_running";
    case EngineState::Error:
        return "error";
    case EngineState::Fatal:
        return "fatal";
    }
    return "starting";
}

bool CanTransition(EngineState from, EngineState to) {
    if (from == EngineState::Fatal) {
        return false;
    }
    if (to == EngineState::Fatal || to == EngineState::Error) {
        return true;
    }
    switch (from) {
    case EngineState::Starting:
        return to == EngineState::Updating || to == EngineState::PreparingGame;
    case EngineState::Updating:
        return to == EngineState::PreparingGame;
    case EngineState::PreparingGame:
        return to == EngineState::Ready;
    case EngineState::Ready:
        return to == EngineState::LaunchingGame;
    case EngineState::LaunchingGame:
        return to == EngineState::GameRunning || to == EngineState::Ready;
    case EngineState::GameRunning:
        return to == EngineState::Ready;
    case EngineState::Error:
        return to == EngineState::Starting || to == EngineState::PreparingGame || to == EngineState::Ready;
    case EngineState::Fatal:
        return false;
    }
    return false;
}

EngineState Lifecycle::State() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_state;
}

std::string Lifecycle::Detail() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_detail;
}

bool Lifecycle::Set(EngineState state, std::string detail) {
    return Change(std::nullopt, state, std::move(detail));
}

bool Lifecycle::TryTransition(EngineState from, EngineState to, std::string detail) {
    return Change(from, to, std::move(detail));
}

// The notify lock is taken first so observers see changes in the order they happened.
bool Lifecycle::Change(std::optional<EngineState> from, EngineState to, std::string detail) {
    std::lock_guard<std::mutex> notify(m_notifyMutex);
    EngineState previous;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (from && m_state != *from) {
            return false;
        }
        if (m_state == to) {
            return true;
        }
        if (!CanTransition(m_state, to)) {
            return false;
        }
        previous = m_state;
        m_state = to;
        m_detail = detail;
    }
    m_observers.Notify(previous, to, detail);
    return true;
}

Lifecycle::StateObservers::Id Lifecycle::AddObserver(StateObservers::Callback callback) {
    return m_observers.Add(std::move(callback));
}

void Lifecycle::RemoveObserver(StateObservers::Id id) {
    m_observers.Remove(id);
}

void Lifecycle::RequestShutdown() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_shutdown = true;
    }
    m_shutdownWake.notify_all();
}

bool Lifecycle::ShutdownRequested() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_shutdown;
}

bool Lifecycle::WaitForShutdown(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(m_mutex);
    return m_shutdownWake.wait_for(lock, timeout, [this] { return m_shutdown; });
}

Lifecycle& GetLifecycle() {
    static Lifecycle instance;
    return instance;
}
