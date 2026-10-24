/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Control/Lifecycle.h"
#include "Games/BeamNG.h"
#include "Games/GearBlocks.h"
#include "Games/Registry.h"
#include "Logger.h"
#include "Net/Ed25519.h"
#include "Options.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace Games {

GameRegistry BuildBuiltInGames() {
    GameRegistry registry;
    registry.Add(std::make_unique<BeamNGModule>(GetLifecycle(), SystemProcessRunner(), StopSystemProcess));
    if (options.controlled()) {
        auto gearBlocks = std::make_unique<GearBlocksModule>(GameConfig {}, GetLifecycle(), SystemProcessRunner(), StopSystemProcess);
        // where BeamMP's files for it come from: --gearblocks-manifest / BEAMMP_GEARBLOCKS_MANIFEST and the keys to trust it by,
        // or else the bundle that a release of the launcher ships in a `gearblocks` folder next to the engine (manifest.json,
        // its signature, trust.txt, the plugin and the mod). Without either the launcher cannot install anything for the game.
        std::string manifest = options.gearblocks_manifest;
        if (manifest.empty()) {
            if (const char* fromEnvironment = std::getenv("BEAMMP_GEARBLOCKS_MANIFEST")) {
                manifest = fromEnvironment;
            }
        }
        std::filesystem::path trust = options.gearblocks_trust;
        if (trust.empty()) {
            if (const char* fromEnvironment = std::getenv("BEAMMP_GEARBLOCKS_TRUST")) {
                trust = fromEnvironment;
            }
        }
        std::vector<std::string> keys;
        if (!trust.empty()) {
            std::ifstream file(trust, std::ios::binary);
            std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            std::string problem;
            if (!file) {
                warn("GearBlocks: the file of trusted keys cannot be read (" + trust.string() + ")");
            } else {
                keys = Net::ParseTrustedKeys(text, problem);
                if (!problem.empty()) {
                    warn("GearBlocks: the file of trusted keys is not usable: " + problem);
                } else if (keys.empty()) {
                    warn("GearBlocks: the file of trusted keys has no key in it");
                }
            }
        }
        std::error_code ec;
        std::filesystem::path bundle = std::filesystem::absolute(std::filesystem::path(options.executable_name), ec).parent_path() / "gearblocks";
        if (manifest.empty() && trust.empty() && std::filesystem::is_regular_file(bundle / "manifest.json", ec) && std::filesystem::is_regular_file(bundle / "trust.txt", ec)) {
            std::ifstream file(bundle / "trust.txt", std::ios::binary);
            std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            std::string problem;
            keys = Net::ParseTrustedKeys(text, problem);
            if (!problem.empty() || keys.empty()) {
                warn("GearBlocks: the bundled trust.txt is not usable " + problem);
                keys.clear();
            } else {
                manifest = "bundle:manifest.json";
                gearBlocks->SetBundleDir(bundle);
                info("GearBlocks: using the files bundled with the launcher (" + bundle.string() + ")");
            }
        }
        gearBlocks->SetInstallSource(manifest, std::move(keys), options.gearblocks_allow_loopback_http);
        registry.Add(std::move(gearBlocks));
    }
    // the user's settings from the command line (the UI passes its saved ones that way)
    for (const auto& module : registry.All()) {
        auto config = options.game_configs.find(module->Id());
        if (config != options.game_configs.end()) {
            module->Configure(config->second);
        }
    }
    return registry;
}

}
