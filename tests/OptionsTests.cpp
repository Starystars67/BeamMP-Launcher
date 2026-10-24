/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Options.h"
#include <cstdlib>
#include <doctest/doctest.h>
#include <vector>

namespace {
bool Headless(std::vector<const char*> args) {
    return IsHeadlessRequested(int(args.size()), args.data());
}
}

TEST_CASE("headless is detected before options are parsed") {
    CHECK(Headless({ "launcher", "--headless" }));
    CHECK(Headless({ "launcher", "--no-update", "--headless", "-v" }));
}

TEST_CASE("headless is not assumed") {
    CHECK_FALSE(Headless({ "launcher" }));
    CHECK_FALSE(Headless({ "launcher", "--verbose" }));
    CHECK_FALSE(Headless({ "launcher", "--headless-ish" }));
}

TEST_CASE("a control port implies headless before the options are parsed") {
    CHECK(Headless({ "launcher", "--control-port", "0" }));
    CHECK_FALSE(Headless({ "launcher", "--", "--control-port", "0" }));
}

TEST_CASE("the program name is never treated as a flag") {
    CHECK_FALSE(Headless({ "--headless" }));
}

TEST_CASE("arguments meant for the game are ignored") {
    CHECK_FALSE(Headless({ "launcher", "--", "--headless" }));
    CHECK_FALSE(Headless({ "launcher", "--game", "--headless" }));
    CHECK(Headless({ "launcher", "--headless", "--", "-console" }));
}

TEST_CASE("InitOptions turns --headless into the option") {
    std::vector<const char*> args = { "launcher", "--headless", "--no-launch" };
    Options parsed;
    InitOptions(int(args.size()), args.data(), parsed);
    CHECK(parsed.headless);
    CHECK(parsed.no_launch);
    CHECK_FALSE(parsed.verbose);
}

TEST_CASE("defaults are unchanged") {
    Options defaults;
    CHECK_FALSE(defaults.headless);
    CHECK_FALSE(defaults.verbose);
    CHECK_FALSE(defaults.controlled());
    CHECK(defaults.control_port == -1);
    CHECK(defaults.port == 4444);
    CHECK(defaults.executable_name == "BeamMP-Launcher");
}

TEST_CASE("--control-port turns the control channel on and implies headless") {
    std::vector<const char*> args = { "launcher", "--control-port", "0" };
    Options parsed;
    InitOptions(int(args.size()), args.data(), parsed);
    CHECK(parsed.controlled());
    CHECK(parsed.control_port == 0);
    CHECK(parsed.headless);
}

TEST_CASE("a fixed control port is accepted") {
    std::vector<const char*> args = { "launcher", "--control-port", "5123", "--no-launch" };
    Options parsed;
    InitOptions(int(args.size()), args.data(), parsed);
    CHECK(parsed.control_port == 5123);
    CHECK(parsed.no_launch);
}

TEST_CASE("an invalid control port leaves the control channel off") {
    for (const char* bad : { "-1", "65536", "abc", "" }) {
        std::vector<const char*> args = { "launcher", "--control-port", bad };
        Options parsed;
        InitOptions(int(args.size()), args.data(), parsed);
        CHECK_FALSE(parsed.controlled());
        CHECK_FALSE(parsed.headless);
    }
    std::vector<const char*> missing = { "launcher", "--control-port" };
    Options parsed;
    InitOptions(int(missing.size()), missing.data(), parsed);
    CHECK_FALSE(parsed.controlled());
}

TEST_CASE("the control token comes from the environment and is removed from it") {
    setenv("BEAMMP_CONTROL_TOKEN", "0123456789abcdef0123", 1);
    std::vector<const char*> args = { "launcher", "--control-port", "0" };
    Options parsed;
    InitOptions(int(args.size()), args.data(), parsed);
    CHECK(parsed.control_token == "0123456789abcdef0123");
    CHECK(std::getenv("BEAMMP_CONTROL_TOKEN") == nullptr);
}

TEST_CASE("without a control port the token is left alone") {
    setenv("BEAMMP_CONTROL_TOKEN", "left-alone-left-alone", 1);
    std::vector<const char*> args = { "launcher" };
    Options parsed;
    InitOptions(int(args.size()), args.data(), parsed);
    CHECK(parsed.control_token.empty());
    CHECK(std::getenv("BEAMMP_CONTROL_TOKEN") != nullptr);
    unsetenv("BEAMMP_CONTROL_TOKEN");
}

TEST_CASE("--game-dir, --game-command and --game-user-path set a game's settings") {
    std::vector<const char*> args = { "launcher", "--game-dir", "beamng=/games/BeamNG.drive", "--game-command", "beamng=/opt/run-beamng.sh",
        "--game-user-path", "beamng=/users/x/BeamNG.drive", "--game-dir", "gearblocks=/games/GearBlocks" };
    Options parsed;
    InitOptions(int(args.size()), args.data(), parsed);
    REQUIRE(parsed.game_configs.size() == 2);
    CHECK(parsed.game_configs["beamng"].directory == "/games/BeamNG.drive");
    CHECK(parsed.game_configs["beamng"].command == "/opt/run-beamng.sh");
    CHECK(parsed.game_configs["beamng"].userPath == "/users/x/BeamNG.drive");
    CHECK(parsed.game_configs["gearblocks"].directory == "/games/GearBlocks");
    CHECK(parsed.game_configs["gearblocks"].command.empty());
}

TEST_CASE("a path may contain equals signs, only the first one separates the game") {
    std::vector<const char*> args = { "launcher", "--game-command", "beamng=/opt/run.sh=fast" };
    Options parsed;
    InitOptions(int(args.size()), args.data(), parsed);
    CHECK(parsed.game_configs["beamng"].command == "/opt/run.sh=fast");
}

TEST_CASE("a malformed game setting is ignored") {
    for (const char* bad : { "beamng", "=value", "beamng=", "" }) {
        std::vector<const char*> args = { "launcher", "--game-dir", bad };
        Options parsed;
        InitOptions(int(args.size()), args.data(), parsed);
        CHECK(parsed.game_configs.empty());
    }
    std::vector<const char*> missing = { "launcher", "--game-dir" };
    Options parsed;
    InitOptions(int(missing.size()), missing.data(), parsed);
    CHECK(parsed.game_configs.empty());
}
