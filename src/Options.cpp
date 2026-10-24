/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Options.h"

#include "Logger.h"
#include <cstdlib>
#include <filesystem>
#include <string_view>

bool IsHeadlessRequested(int argc, const char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        std::string_view argument(argv[i]);
        if (argument == "--" || argument == "--game") {
            break;
        }
        if (argument == "--headless" || argument == "--control-port") {
            return true;
        }
    }
    return false;
}

// The token is removed from the environment so the game does not inherit it.
static void ReadControlToken(Options& options) {
    const char* token = std::getenv("BEAMMP_CONTROL_TOKEN");
    if (token) {
        options.control_token = token;
#if defined(_WIN32)
        _putenv_s("BEAMMP_CONTROL_TOKEN", "");
#else
        unsetenv("BEAMMP_CONTROL_TOKEN");
#endif
    }
}

void InitOptions(int argc, const char *argv[], Options &options) {
    int i = 1;

    options.argc = argc;
    options.argv = argv;

    std::string AllOptions;
    for (int i = 0; i < argc; ++i) {
        AllOptions += std::string(argv[i]);
        if (i + 1 < argc) {
            AllOptions += " ";
        }
    }
    debug("Launcher was invoked as: '" + AllOptions + "'");


    if (argc > 2) {
        if (std::string(argv[1]) == "0" && std::string(argv[2]) == "0") {
            options.verbose = true;
            options.no_download = true;
            options.no_launch = true;
            options.no_update = true;
            warn("You are using deprecated commandline arguments, please use --dev instead");
            return;
        }
    }

    options.executable_name = std::string(argv[0]);

    while (i < argc) {
        std::string argument(argv[i]);
        if (argument == "-p" || argument == "--port") {
            if (i + 1 >= argc) {
                std::string error_message =
                    "No port specified, resorting to default (";
                error_message += std::to_string(options.port);
                error_message += ")";
                error(error_message);
                i++;
                continue;
            }

            int port = options.port;

            try {
                port = std::stoi(argv[i + 1]);
            } catch (std::exception& e) {
                error("Invalid port specified: " + std::string(argv[i + 1]) + " " + std::string(e.what()));
            }

            if (port <= 0) {
                std::string error_message =
                    "Port invalid, must be a non-zero positive "
                    "integer, resorting to default (";
                error_message += options.port;
                error_message += ")";
                error(error_message);
                i++;
                continue;
            }

            options.port = port;
            i++;
        } else if (argument == "-v" || argument == "--verbose") {
            options.verbose = true;
        } else if (argument == "--no-download") {
            options.no_download = true;
        } else if (argument == "--no-update") {
            options.no_update = true;
        } else if (argument == "--no-launch") {
            options.no_launch = true;
        } else if (argument == "--headless") {
            options.headless = true;
        } else if (argument == "--no-browser") {
            options.no_browser = true;
        } else if (argument == "--control-port") {
            int port = -1;
            try {
                port = i + 1 < argc ? std::stoi(argv[i + 1]) : -1;
            } catch (const std::exception&) { }
            if (port < 0 || port > 65535) {
                error("The `--control-port` argument needs a port between 0 and 65535, the control channel stays off");
            } else {
                options.control_port = port;
                options.headless = true;
            }
            i++;
        } else if (argument == "--dev") {
            options.verbose = true;
            options.no_download = true;
            options.no_launch = true;
            options.no_update = true;
        } else if (argument == "--user-path") {
            if (i + 1 >= argc) {
                error("You must specify a path after the `--user-path` argument");
            }
            options.user_path = argv[i + 1];
            i++;
        } else if (argument == "--relay") {
            if (i + 1 >= argc) {
                error("You must specify host:port after the `--relay` argument");
            } else {
                options.relay = argv[i + 1];
            }
            i++;
        } else if (argument == "--gearblocks-manifest") {
            if (i + 1 >= argc) {
                error("You must specify an address after the `--gearblocks-manifest` argument");
            } else {
                options.gearblocks_manifest = argv[i + 1];
            }
            i++;
        } else if (argument == "--gearblocks-trust") {
            if (i + 1 >= argc) {
                error("You must specify a file after the `--gearblocks-trust` argument");
            } else {
                options.gearblocks_trust = argv[i + 1];
            }
            i++;
        } else if (argument == "--gearblocks-allow-loopback-http") {
            options.gearblocks_allow_loopback_http = true;
        } else if (argument == "--relay-insecure") {
            options.relay_insecure = true;
        } else if (argument == "--game-dir" || argument == "--game-command" || argument == "--game-user-path") {
            // <id>=<value>
            std::string value = i + 1 < argc ? argv[i + 1] : "";
            size_t equals = value.find('=');
            if (equals == std::string::npos || equals == 0 || equals + 1 >= value.size()) {
                error("The `" + argument + "` argument needs <game>=<value>, for example beamng=/path");
            } else {
                Games::GameConfig& config = options.game_configs[value.substr(0, equals)];
                std::string setting = value.substr(equals + 1);
                if (argument == "--game-dir") {
                    config.directory = setting;
                } else if (argument == "--game-command") {
                    config.command = setting;
                } else {
                    config.userPath = setting;
                }
            }
            i++;
        } else if (argument == "--" || argument == "--game") {
            options.game_arguments = &argv[i + 1];
            options.game_arguments_length = argc - i - 1;
            break;
        } else if (argument == "--help" || argument == "-h" || argument == "/?") {
            std::cout << "USAGE:\n"
                "\t" + std::filesystem::path(options.executable_name).filename().string() + " [OPTIONS] [-- <GAME ARGS>...]\n"
                "\n"
                "OPTIONS:\n"
                "\t--port <port>    -p  Change the default listen port to <port>. This must be configured ingame, too\n"
                "\t--verbose        -v  Verbose mode, prints debug messages\n"
                "\t--no-download        Skip downloading and installing the BeamMP Lua mod\n"
                "\t--no-update          Skip applying launcher updates (you must update manually)\n"
                "\t--no-launch          Skip launching the game (you must launch the game manually)\n"
                "\t--headless           No console output, log to Launcher.log only\n"
                "\t--no-browser         Do not open the sign-in page in the browser (the controller is told its address)\n"
                "\t--control-port <n>   Serve the launcher UI protocol on 127.0.0.1:<n> (0 = any free port), implies --headless.\n"
                "\t                     The access token is read from the BEAMMP_CONTROL_TOKEN environment variable\n"
                "\t--dev                Developer mode, same as --verbose --no-download --no-launch --no-update\n"
                "\t--user-path <path>   Path to BeamNG's User Path\n"
                "\t--gearblocks-manifest <url>  The signed list of BeamMP's GearBlocks files (https; the signature is at <url>.sig); BEAMMP_GEARBLOCKS_MANIFEST does the same\n"
                "\t--gearblocks-trust <file>    Public keys the list may be signed by (`ed25519:<base64>` per line); BEAMMP_GEARBLOCKS_TRUST does the same\n"
                "\t--gearblocks-allow-loopback-http  Development: take the list and the files over http from this machine\n"
                "\t--relay <host:port>        The BeamMP relay players meet through (rooms); BEAMMP_RELAY does the same\n"
                "\t--relay-insecure           Allow a relay over the internet without TLS (the launcher has no TLS for it yet)\n"
                "\t--game-dir <id>=<path>        Folder of a game (beamng, gearblocks) instead of looking for it in Steam\n"
                "\t--game-command <id>=<prog>   Program that starts a game instead of its executable (e.g. a Wine script); it gets <ID>_DIR\n"
                "\t--game-user-path <id>=<path> A game's user data folder (beamng: the one that contains `current`)\n"
                "\t--game <args...>     Passes ALL following arguments to the game, see also `--`\n"
                << std::flush;
            exit(0);
        } else {
            warn("Unknown option: " + argument);
        }

        i++;
    }

    if (options.controlled()) {
        ReadControlToken(options);
    }
}
