/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include "Games/GameConfig.h"
#include <map>
#include <string>

#ifndef BEAMMP_EXECUTABLE_NAME
#define BEAMMP_EXECUTABLE_NAME "BeamMP-Launcher"
#endif

struct Options {
#if defined(_WIN32)
    std::string executable_name = BEAMMP_EXECUTABLE_NAME ".exe";
#elif (defined(__linux__) || defined(__APPLE__))
    std::string executable_name = BEAMMP_EXECUTABLE_NAME;
#endif
    unsigned int port = 4444;
    bool verbose = false;
    bool headless = false;
    // -1 = no control channel, 0 = pick a free port. A controlled launcher is always headless.
    // Sign-in pages are only reported to the controller, not opened by the launcher.
    bool no_browser = false;
    int control_port = -1;
    std::string control_token;
    bool no_download = false;
    bool no_update = false;
    bool no_launch = false;
    const char* user_path = nullptr;
    // The BeamMP relay (host:port) players meet through, and whether it may be reached without TLS from afar.
    std::string relay;
    // Where the signed list of GearBlocks files is (its signature is at the same address plus `.sig`), and the file of public keys
    // (`ed25519:<base64>` lines) it has to be signed by. BEAMMP_GEARBLOCKS_MANIFEST / BEAMMP_GEARBLOCKS_TRUST do the same. Nothing is built in.
    std::string gearblocks_manifest;
    std::string gearblocks_trust;
    // Development: the manifest and its files may come over plain http from this machine.
    bool gearblocks_allow_loopback_http = false;
    bool relay_insecure = false;
    // Per game (by id): where it is, how it starts. See `--game-dir`, `--game-command`, `--game-user-path`.
    std::map<std::string, Games::GameConfig> game_configs;
    const char **game_arguments = nullptr;
    int game_arguments_length = 0;
    const char** argv = nullptr;
    int argc = 0;

    bool controlled() const { return control_port >= 0; }
};

void InitOptions(int argc, const char *argv[], Options &options);

// Usable before InitOptions: console output has to be suppressed from the very first line (--headless, --control-port).
bool IsHeadlessRequested(int argc, const char* argv[]);

extern Options options;
