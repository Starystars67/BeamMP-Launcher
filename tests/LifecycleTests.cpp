/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Control/Lifecycle.h"
#include <algorithm>
#include <atomic>
#include <doctest/doctest.h>
#include <thread>
#include <vector>

namespace {
struct Change {
    EngineState previous;
    EngineState current;
    std::string detail;
};

struct Recorder {
    Lifecycle lifecycle;
    std::mutex mutex;
    std::vector<Change> changes;

    Recorder() {
        lifecycle.AddObserver([this](EngineState previous, EngineState current, const std::string& detail) {
            std::this_thread::yield(); // widens the window for out-of-order delivery
            std::lock_guard<std::mutex> lock(mutex);
            changes.push_back({ previous, current, detail });
        });
    }
};
}

TEST_CASE("the normal startup and game cycle is allowed") {
    Recorder recorder;
    Lifecycle& lifecycle = recorder.lifecycle;
    CHECK(lifecycle.State() == EngineState::Starting);
    CHECK(lifecycle.Set(EngineState::Updating));
    CHECK(lifecycle.Set(EngineState::PreparingGame));
    CHECK(lifecycle.Set(EngineState::Ready));
    CHECK(lifecycle.Set(EngineState::LaunchingGame));
    CHECK(lifecycle.Set(EngineState::GameRunning));
    CHECK(lifecycle.Set(EngineState::Ready));
    CHECK(lifecycle.Set(EngineState::LaunchingGame));
    CHECK(lifecycle.Set(EngineState::GameRunning));
    CHECK(lifecycle.State() == EngineState::GameRunning);
    CHECK(recorder.changes.size() == 8);
}

TEST_CASE("updating is optional") {
    Lifecycle lifecycle;
    CHECK(lifecycle.Set(EngineState::PreparingGame));
    CHECK(lifecycle.Set(EngineState::Ready));
}

TEST_CASE("impossible transitions are refused and change nothing") {
    Recorder recorder;
    Lifecycle& lifecycle = recorder.lifecycle;
    CHECK_FALSE(lifecycle.Set(EngineState::Ready));
    CHECK_FALSE(lifecycle.Set(EngineState::GameRunning));
    CHECK(lifecycle.State() == EngineState::Starting);

    lifecycle.Set(EngineState::PreparingGame);
    lifecycle.Set(EngineState::Ready);
    CHECK_FALSE(lifecycle.Set(EngineState::GameRunning));
    CHECK_FALSE(lifecycle.Set(EngineState::PreparingGame));
    CHECK(lifecycle.State() == EngineState::Ready);
    CHECK(recorder.changes.size() == 2);
}

TEST_CASE("a failed launch returns to ready") {
    Lifecycle lifecycle;
    lifecycle.Set(EngineState::PreparingGame);
    lifecycle.Set(EngineState::Ready);
    lifecycle.Set(EngineState::LaunchingGame);
    CHECK(lifecycle.Set(EngineState::Ready, "Failed to launch the game: no such file"));
    CHECK(lifecycle.Detail() == "Failed to launch the game: no such file");
}

TEST_CASE("errors are recoverable, fatal is final") {
    Lifecycle lifecycle;
    lifecycle.Set(EngineState::PreparingGame);
    CHECK(lifecycle.Set(EngineState::Error, "mod download failed"));
    CHECK(lifecycle.Set(EngineState::PreparingGame));
    CHECK(lifecycle.Set(EngineState::Ready));
    CHECK(lifecycle.Set(EngineState::Fatal, "boom"));
    CHECK_FALSE(lifecycle.Set(EngineState::Ready));
    CHECK_FALSE(lifecycle.Set(EngineState::Error));
    CHECK(lifecycle.Set(EngineState::Fatal, "again")); // already there
    CHECK(lifecycle.State() == EngineState::Fatal);
    CHECK(lifecycle.Detail() == "boom");
}

TEST_CASE("observers get the previous state, the new state and the detail") {
    Recorder recorder;
    recorder.lifecycle.Set(EngineState::PreparingGame, "checking the game");
    REQUIRE(recorder.changes.size() == 1);
    CHECK(recorder.changes[0].previous == EngineState::Starting);
    CHECK(recorder.changes[0].current == EngineState::PreparingGame);
    CHECK(recorder.changes[0].detail == "checking the game");
}

TEST_CASE("setting the current state again does not notify") {
    Recorder recorder;
    recorder.lifecycle.Set(EngineState::PreparingGame);
    recorder.lifecycle.Set(EngineState::PreparingGame);
    CHECK(recorder.changes.size() == 1);
}

TEST_CASE("TryTransition only fires from the expected state") {
    Lifecycle lifecycle;
    lifecycle.Set(EngineState::PreparingGame);
    CHECK_FALSE(lifecycle.TryTransition(EngineState::Ready, EngineState::LaunchingGame));
    lifecycle.Set(EngineState::Ready);
    CHECK(lifecycle.TryTransition(EngineState::Ready, EngineState::LaunchingGame));
    CHECK_FALSE(lifecycle.TryTransition(EngineState::Ready, EngineState::LaunchingGame));
}

TEST_CASE("exactly one of many concurrent launches wins") {
    Lifecycle lifecycle;
    lifecycle.Set(EngineState::PreparingGame);
    lifecycle.Set(EngineState::Ready);
    std::atomic<int> winners = 0;
    std::vector<std::thread> threads;
    for (int i = 0; i < 16; ++i) {
        threads.emplace_back([&] {
            if (lifecycle.TryTransition(EngineState::Ready, EngineState::LaunchingGame)) {
                ++winners;
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    CHECK(winners == 1);
}

TEST_CASE("observers see changes in the order they happened, even from several threads") {
    Recorder recorder;
    recorder.lifecycle.Set(EngineState::PreparingGame);
    recorder.lifecycle.Set(EngineState::Ready);
    recorder.changes.clear();

    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&] {
            for (int i = 0; i < 500; ++i) {
                if (!recorder.lifecycle.TryTransition(EngineState::Ready, EngineState::LaunchingGame)) {
                    recorder.lifecycle.Set(EngineState::Ready);
                }
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }

    REQUIRE(!recorder.changes.empty());
    EngineState expected = EngineState::Ready;
    for (const auto& change : recorder.changes) {
        REQUIRE(change.previous == expected);
        expected = change.current;
    }
}

TEST_CASE("observers may read the lifecycle while being notified") {
    Lifecycle lifecycle;
    EngineState seen = EngineState::Starting;
    lifecycle.AddObserver([&](EngineState, EngineState current, const std::string&) { seen = lifecycle.State(); (void)current; });
    lifecycle.Set(EngineState::PreparingGame);
    CHECK(seen == EngineState::PreparingGame);
}

TEST_CASE("removed observers are no longer called") {
    Lifecycle lifecycle;
    int calls = 0;
    auto id = lifecycle.AddObserver([&](EngineState, EngineState, const std::string&) { ++calls; });
    lifecycle.Set(EngineState::PreparingGame);
    lifecycle.RemoveObserver(id);
    lifecycle.Set(EngineState::Ready);
    CHECK(calls == 1);
}

TEST_CASE("shutdown requests wake waiters") {
    Lifecycle lifecycle;
    CHECK_FALSE(lifecycle.ShutdownRequested());
    CHECK_FALSE(lifecycle.WaitForShutdown(std::chrono::milliseconds(20)));

    std::thread requester([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        lifecycle.RequestShutdown();
    });
    CHECK(lifecycle.WaitForShutdown(std::chrono::seconds(5)));
    requester.join();
    CHECK(lifecycle.ShutdownRequested());
}

TEST_CASE("every state has a distinct protocol name") {
    std::vector<std::string> names;
    for (auto state : { EngineState::Starting, EngineState::Updating, EngineState::PreparingGame, EngineState::Ready,
             EngineState::LaunchingGame, EngineState::GameRunning, EngineState::Error, EngineState::Fatal }) {
        names.push_back(ToString(state));
    }
    std::sort(names.begin(), names.end());
    CHECK(std::adjacent_find(names.begin(), names.end()) == names.end());
    CHECK(std::string(ToString(EngineState::GameRunning)) == "game_running");
}
