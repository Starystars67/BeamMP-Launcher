/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Control/Lifecycle.h"
#include "Control/Controller.h"
#include "Control/Runtime.h"
#include "Games/BeamNG.h"
#include "Games/Preparer.h"
#include "Games/Registry.h"
#include "Http.h"
#include "Logger.h"
#include "Network/network.hpp"
#include "Security/Init.h"
#include "Startup.h"
#include "Utils.h"
#include <chrono>
#include <curl/curl.h>
#include <iostream>
#include <stdexcept>
#include <thread>
#include "Options.h"

Options options;

// Prepares every game. The console launcher has only ever been for BeamNG, so for it a game that cannot be
// prepared ends the launcher; a controlled one reports it to the controller and goes on.
static void PrepareGames(Games::GamePreparer& preparer) {
    for (const auto& module : preparer.Games().All()) {
        Games::PrepareResult result = preparer.Prepare(*module);
        if (!result.ok() && !options.controlled()) {
            throw std::runtime_error(result.detail);
        }
    }
}

[[noreturn]] void flush() {
    while (true) {
        std::cout.flush();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

int main(int argc, const char** argv) try {
    options.headless = IsHeadlessRequested(argc, argv);

    if (!options.headless) {
#if defined(_WIN32)
        system("cls");
#elif (defined(__linux__) || defined(__APPLE__))
        system("clear");
#endif
    }

#ifdef DEBUG
    std::thread th(flush);
    th.detach();
#endif

    curl_global_init(CURL_GLOBAL_ALL);

    GetEP(Utils::ToWString(std::string(argv[0])).c_str());

    InitLog();
    ConfigInit();
    InitOptions(argc, argv, options);
    static Games::GameRegistry games = Games::BuildBuiltInGames();
    static Games::GamePreparer preparer(games);
    if (options.controlled()) {
        StartControl(preparer);
    }

    Lifecycle& lifecycle = GetLifecycle();
    if (!options.no_update) {
        lifecycle.Set(EngineState::Updating);
    }
    InitLauncher();

    if (!options.headless) {
        info("IMPORTANT: You MUST keep this window open to play BeamMP!");
    }

    lifecycle.Set(EngineState::PreparingGame);
    PrepareGames(preparer);
    lifecycle.Set(EngineState::Ready);
    // The launcher's network core belongs to BeamNG. Without BeamNG the other games and the account still
    // work, so the launcher stays up for them, and the core starts once BeamNG has been found.
    for (;;) {
        if (preparer.IsReady(Games::BeamNGId)) {
            InitGame(GetGameDir());
            CoreNetwork();
        }
        if (lifecycle.WaitForShutdown(std::chrono::seconds(1))) {
            break;
        }
    }
} catch (const std::exception& e) {
    GetLifecycle().Set(EngineState::Fatal, e.what());
    error(std::string("Exception in main(): ") + e.what());
    info("Closing in 5 seconds");
    info("If this keeps happening, contact us on either: Forum: https://forum.beammp.com, Discord: https://discord.gg/beammp");
    std::this_thread::sleep_for(std::chrono::seconds(5));
}
