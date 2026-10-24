/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Games/ManagedProcess.h"
#include "Logger.h"
#include "Options.h"
#include "Status.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <doctest/doctest.h>
#include <thread>

using namespace Games;

namespace {
template <typename Condition>
bool Eventually(Condition condition, int timeoutMs = 3000) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (!condition()) {
        if (std::chrono::steady_clock::now() > deadline) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return true;
}

struct Runner {
    std::mutex mutex;
    std::condition_variable wake;
    bool end = false;
    int exitCode = 0;
    bool startFails = false;
    std::atomic<int> runs = 0;
    std::atomic<unsigned long> stopped = 0;

    ProcessRunner Run() {
        return [this](const LaunchSpec&, const std::function<void(unsigned long)>& started) {
            ++runs;
            RunResult result;
            if (startFails) {
                result.error = "nope";
                return result;
            }
            {
                std::lock_guard<std::mutex> lock(mutex);
                end = false;
            }
            started(777);
            std::unique_lock<std::mutex> lock(mutex);
            wake.wait(lock, [this] { return end; });
            result.launched = true;
            result.exitCode = exitCode;
            return result;
        };
    }
    ProcessStopper Stop() {
        return [this](unsigned long pid) {
            stopped = pid;
            Exit(143);
            return true;
        };
    }
    void Exit(int code) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            exitCode = code;
            end = true;
        }
        wake.notify_all();
    }
};

struct Fixture : Runner {
    Lifecycle lifecycle;
    ManagedProcess process { lifecycle, Run(), Stop() };

    Fixture() {
        options = Options();
        options.headless = true;
        InitLog();
        GamePID = 0;
        lifecycle.Set(EngineState::PreparingGame);
        lifecycle.Set(EngineState::Ready);
    }
    void Begin(std::function<void()> ended = {}) {
        REQUIRE(lifecycle.TryTransition(EngineState::Ready, EngineState::LaunchingGame));
        process.Start({}, std::move(ended));
    }
};
}

TEST_CASE_FIXTURE(Fixture, "a started process moves the lifecycle to running and back to ready with the exit code") {
    Begin();
    REQUIRE(Eventually([&] { return lifecycle.State() == EngineState::GameRunning; }));
    CHECK(GamePID == 777);
    Exit(3);
    REQUIRE(Eventually([&] { return lifecycle.State() == EngineState::Ready; }));
    CHECK(lifecycle.Detail() == "The game exited with code 3");
    CHECK(GamePID == 0);
}

TEST_CASE_FIXTURE(Fixture, "the end callback runs before the lifecycle is ready again") {
    std::atomic<int> stateInCallback = -1;
    Begin([&] { stateInCallback = static_cast<int>(lifecycle.State()); });
    REQUIRE(Eventually([&] { return lifecycle.State() == EngineState::GameRunning; }));
    Exit(0);
    REQUIRE(Eventually([&] { return lifecycle.State() == EngineState::Ready; }));
    CHECK(stateInCallback == static_cast<int>(EngineState::GameRunning));
}

TEST_CASE_FIXTURE(Fixture, "a process that cannot start leaves the engine ready with the reason, and the callback still runs") {
    startFails = true;
    std::atomic<bool> called = false;
    Begin([&] { called = true; });
    REQUIRE(Eventually([&] { return lifecycle.State() == EngineState::Ready; }));
    CHECK(lifecycle.Detail() == "Failed to launch the game: nope");
    CHECK(called);
}

TEST_CASE_FIXTURE(Fixture, "stop is only true while something runs") {
    CHECK_FALSE(process.Stop());
    Begin();
    REQUIRE(Eventually([&] { return lifecycle.State() == EngineState::GameRunning; }));
    CHECK(process.Stop());
    CHECK(stopped == 777);
    REQUIRE(Eventually([&] { return lifecycle.State() == EngineState::Ready; }));
    CHECK_FALSE(process.Stop());
}

TEST_CASE_FIXTURE(Fixture, "it can be started again after it ended") {
    Begin();
    REQUIRE(Eventually([&] { return lifecycle.State() == EngineState::GameRunning; }));
    Exit(0);
    REQUIRE(Eventually([&] { return lifecycle.State() == EngineState::Ready; }));
    Begin();
    REQUIRE(Eventually([&] { return lifecycle.State() == EngineState::GameRunning; }));
    Exit(0);
    REQUIRE(Eventually([&] { return lifecycle.State() == EngineState::Ready; }));
    CHECK(runs == 2);
}
