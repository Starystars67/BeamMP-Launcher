/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Games/Registry.h"
#include <doctest/doctest.h>

using namespace Games;

namespace {
struct Named : GameModule {
    std::string id;
    explicit Named(std::string id)
        : id(std::move(id)) { }
    std::string Id() const override { return id; }
    std::string Name() const override { return "Game " + id; }
    PrepareResult Prepare() override { return {}; }
    void Launch(const std::vector<std::string>&) override { }
    bool Stop() override { return false; }
};
}

TEST_CASE("the registry keeps games in the order they were added and finds them by id") {
    GameRegistry registry;
    registry.Add(std::make_unique<Named>("first"));
    registry.Add(std::make_unique<Named>("second"));
    REQUIRE(registry.All().size() == 2);
    CHECK(registry.All()[0]->Id() == "first");
    CHECK(registry.All()[1]->Id() == "second");
    REQUIRE(registry.Find("second"));
    CHECK(registry.Find("second")->Name() == "Game second");
    CHECK(registry.Find("third") == nullptr);
}

TEST_CASE("a second game with the same id is refused") {
    GameRegistry registry;
    registry.Add(std::make_unique<Named>("same"));
    CHECK_THROWS_AS(registry.Add(std::make_unique<Named>("same")), std::invalid_argument);
    CHECK(registry.All().size() == 1);
}

TEST_CASE("a module has no message channel unless it says so") {
    Named game("x");
    CHECK_FALSE(game.HasMessageChannel());
    CHECK_FALSE(game.Send({ { "a", 1 } }));
}

TEST_CASE("only an available game counts as ready") {
    CHECK(PrepareResult {}.ok());
    CHECK_FALSE(PrepareResult { GameStatus::NotFound, "x" }.ok());
    CHECK_FALSE(PrepareResult { GameStatus::Unavailable, "" }.ok());
    CHECK_FALSE(PrepareResult { GameStatus::Failed, "x" }.ok());
}
