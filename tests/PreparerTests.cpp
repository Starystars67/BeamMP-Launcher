/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Games/Preparer.h"
#include <atomic>
#include <chrono>
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

// shared by every fake: how many preparations run at the same moment
std::atomic<int> g_inside = 0;
std::atomic<int> g_maxInside = 0;

struct Fake : GameModule {
    std::string id;
    GameConfig config;
    std::mutex mutex;
    PrepareResult next;
    bool throws = false;
    std::atomic<int> prepared = 0;
    int delayMs = 0;

    explicit Fake(std::string id)
        : id(std::move(id)) { }
    std::string Id() const override { return id; }
    std::string Name() const override { return id; }
    void Configure(const GameConfig& c) override {
        std::lock_guard<std::mutex> lock(mutex);
        config = c;
    }
    GameConfig Config() const override { return config; }
    PrepareResult Prepare() override {
        int now = ++g_inside;
        int seen = g_maxInside;
        while (now > seen && !g_maxInside.compare_exchange_weak(seen, now)) { }
        if (delayMs > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
        }
        --g_inside;
        ++prepared;
        if (throws) {
            throw std::runtime_error("boom");
        }
        std::lock_guard<std::mutex> lock(mutex);
        return config.directory == "missing" ? PrepareResult { GameStatus::NotFound, "no such folder" } : next;
    }
    void Launch(const std::vector<std::string>&) override { }
    bool Stop() override { return false; }
};

struct Setup {
    GameRegistry registry;
    Fake* first;
    Fake* second;
    std::unique_ptr<GamePreparer> preparer;
    std::mutex mutex;
    std::vector<std::pair<std::string, GameStatus>> reports;

    Setup() {
        g_inside = 0;
        g_maxInside = 0;
        auto a = std::make_unique<Fake>("first");
        auto b = std::make_unique<Fake>("second");
        first = a.get();
        second = b.get();
        registry.Add(std::move(a));
        registry.Add(std::move(b));
        preparer = std::make_unique<GamePreparer>(registry);
        preparer->SetReporter([this](const std::string& id, const PrepareResult& result) {
            std::lock_guard<std::mutex> lock(mutex);
            reports.emplace_back(id, result.status);
        });
    }
    std::vector<std::pair<std::string, GameStatus>> Reports() {
        std::lock_guard<std::mutex> lock(mutex);
        return reports;
    }
};
}

TEST_CASE_FIXTURE(Setup, "preparing reports the result and remembers whether the game is ready") {
    CHECK_FALSE(preparer->IsReady("first"));
    PrepareResult ok = preparer->Prepare(*first);
    CHECK(ok.ok());
    CHECK(preparer->IsReady("first"));
    CHECK_FALSE(preparer->IsReady("second"));

    second->next = { GameStatus::NotFound, "nope" };
    PrepareResult missing = preparer->Prepare(*second);
    CHECK(missing.status == GameStatus::NotFound);
    CHECK_FALSE(preparer->IsReady("second"));
    CHECK(Reports() == std::vector<std::pair<std::string, GameStatus>> { { "first", GameStatus::Available }, { "second", GameStatus::NotFound } });
}

TEST_CASE_FIXTURE(Setup, "a module that throws is a failed game, not a failed engine") {
    first->throws = true;
    PrepareResult result = preparer->Prepare(*first);
    CHECK(result.status == GameStatus::Failed);
    CHECK(result.detail == "boom");
    CHECK_FALSE(preparer->IsReady("first"));
}

TEST_CASE_FIXTURE(Setup, "reconfiguring applies the settings, announces the preparation, and reports the result") {
    first->next = { GameStatus::NotFound, "nothing yet" };
    preparer->Prepare(*first);
    CHECK_FALSE(preparer->IsReady("first"));
    reports.clear();
    first->next = {};

    GameConfig config;
    config.directory = "/somewhere";
    config.command = "/start.sh";
    CHECK(preparer->Reconfigure("first", config));
    REQUIRE(Eventually([&] { return preparer->IsReady("first"); }));
    CHECK(first->Config() == config);
    CHECK(Reports() == std::vector<std::pair<std::string, GameStatus>> { { "first", GameStatus::Preparing }, { "first", GameStatus::Available } });
}

TEST_CASE_FIXTURE(Setup, "a game that is not ready while its new settings are prepared") {
    first->delayMs = 200;
    first->next = {};
    preparer->Prepare(*first);
    CHECK(preparer->IsReady("first"));
    GameConfig config;
    config.directory = "missing";
    CHECK(preparer->Reconfigure("first", config));
    CHECK_FALSE(preparer->IsReady("first")); // right away: not ready until the new preparation says so
    REQUIRE(Eventually([&] { return Reports().back().second == GameStatus::NotFound; }));
    CHECK_FALSE(preparer->IsReady("first"));
}

TEST_CASE_FIXTURE(Setup, "an unknown game cannot be reconfigured") {
    CHECK_FALSE(preparer->Reconfigure("third", {}));
    CHECK(Reports().empty());
}

TEST_CASE_FIXTURE(Setup, "two preparations never overlap, even for different games") {
    first->delayMs = 50;
    second->delayMs = 50;
    preparer->Reconfigure("first", {});
    preparer->Reconfigure("second", {});
    std::thread startup([&] { preparer->Prepare(*first); });
    startup.join();
    REQUIRE(Eventually([&] { return preparer->IsReady("first") && preparer->IsReady("second"); }, 5000));
    CHECK(g_maxInside == 1);
    CHECK(first->prepared + second->prepared == 3);
}
