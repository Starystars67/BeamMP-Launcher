/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include <csignal>
#include <cstring>
#include "Utils.h"
#if defined(_WIN32)
#include <shlobj.h>
#elif (defined(__linux__) || defined(__APPLE__))
#include "vdf_parser.hpp"
#include <pwd.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__APPLE__)
extern char** environ; // not declared by unistd.h on macOS
#endif
#endif

#include "Control/Lifecycle.h"
#include "Logger.h"
#include "Options.h"
#include "Startup.h"
#include "Status.h"
#include <Security/Init.h>
#include <filesystem>
#include <thread>

#include <fstream>

#if defined(_WIN32)
std::wstring QueryKey(HKEY hKey, int ID);
std::filesystem::path GetGamePath() {
    static std::filesystem::path Path;
    if (!Path.empty())
        return Path.wstring();

    if (options.user_path) {
        if (std::filesystem::exists(options.user_path)) {
            Path = options.user_path;
            debug(L"Using custom user folder path: " + Path.wstring());
        } else
            warn(L"Invalid or non-existent path (" + Utils::ToWString(options.user_path) + L") specified using --user-path, skipping");
    }

    if (const auto startupIniPath = std::filesystem::path(GetGameDir()) / "startup.ini"; exists(startupIniPath)) {

        if (std::ifstream startupIni(startupIniPath); startupIni.is_open()) {
            std::string contents((std::istreambuf_iterator(startupIni)), std::istreambuf_iterator<char>());
            startupIni.close();

            auto ini = Utils::ParseINI(contents);
            if (ini.empty()) 
                warn("Failed to parse startup.ini");
            else
                debug("Successfully parsed startup.ini");

            std::wstring userPath;
            if (ini.contains("filesystem") && std::get<std::map<std::string, std::string>>(ini["filesystem"]).contains("UserPath"))
                userPath = Utils::ToWString(std::get<std::map<std::string, std::string>>(ini["filesystem"])["UserPath"]);

            if (userPath = Utils::ExpandEnvVars(userPath); std::filesystem::exists(userPath)) {
                Path = userPath;
                debug(L"Using custom user folder path from startup.ini: " + Path.wstring());
            } else
                warn(L"Found custom user folder path (" + userPath + L") in startup.ini but it doesn't exist, skipping");
        }

        if (Path.empty()) {
            wchar_t* appDataPath = new wchar_t[MAX_PATH];
            HRESULT result = SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, appDataPath);

            if (!SUCCEEDED(result)) {
                fatal("Cannot get Local Appdata directory");
            }

            auto BeamNGAppdataPath = std::filesystem::path(appDataPath) / "BeamNG";

            if (const auto beamngIniPath = BeamNGAppdataPath / "BeamNG.Drive.ini"; exists(beamngIniPath)) {
                if (std::ifstream beamngIni(beamngIniPath); beamngIni.is_open()) {
                    std::string contents((std::istreambuf_iterator(beamngIni)), std::istreambuf_iterator<char>());
                    beamngIni.close();

                    auto ini = Utils::ParseINI(contents);
                    if (ini.empty())
                        warn("Failed to parse BeamNG.Drive.ini");
                    else
                        debug("Successfully parsed BeamNG.Drive.ini");

                    std::wstring userPath;

                    if (ini.contains("userFolder")) {
                        userPath = Utils::ToWString(std::get<std::string>(ini["userFolder"]));
                        userPath.erase(0, userPath.find_first_not_of(L" \t"));

                    }

                    if (userPath = std::filesystem::path(Utils::ExpandEnvVars(userPath)); std::filesystem::exists(userPath)) {
                        Path = userPath;
                        debug(L"Using custom user folder path from BeamNG.Drive.ini: " + Path.wstring());
                    } else
                        warn(L"Found custom user folder path (" + userPath + L") in BeamNG.Drive.ini but it doesn't exist, skipping");
                }
            }

            if (Path.empty()) {
                Path = BeamNGAppdataPath / "BeamNG.drive";
            }

            delete[] appDataPath;
        }
    }

    std::string Ver = CheckVer(GetGameDir());
    Ver = Ver.substr(0, Ver.find('.', Ver.find('.') + 1));
    Path /= Utils::ToWString("current");
    return Path;
}
#elif (defined(__linux__) || defined(__APPLE__))
std::filesystem::path GetGamePath() {
    // Right now only steam is supported
    struct passwd* pw = getpwuid(getuid());
    std::string homeDir = pw->pw_dir;

    std::string Path = homeDir + "/.local/share/BeamNG/BeamNG.drive/";
    // same meaning as on Windows: the folder that contains "current"
    if (options.user_path) {
        if (std::filesystem::exists(options.user_path)) {
            Path = std::string(options.user_path) + "/";
            debug("Using custom user folder path: " + Path);
        } else
            warn("Invalid or non-existent path (" + std::string(options.user_path) + ") specified using --user-path, skipping");
    }
    std::string Ver = CheckVer(GetGameDir());
    Ver = Ver.substr(0, Ver.find('.', Ver.find('.') + 1));
    Path += "current/";
    return Path;
}
#endif

namespace {
struct GameResult {
    bool launched = false;
    int exit_code = 0;
    std::string error;
};

std::vector<std::string> ConsoleGameArguments() {
    return std::vector<std::string>(options.game_arguments, options.game_arguments + options.game_arguments_length);
}
}

// Starts the game and blocks until it exits.
#if defined(_WIN32)
static GameResult RunGame(const std::wstring& Dir, const std::vector<std::string>& args) {
    GameResult result;
    PROCESS_INFORMATION pi;
    STARTUPINFOW si = { 0 };
    si.cb = sizeof(si);
    std::wstring BaseDir = Dir; //+"\\Bin64";
    // Dir += R"(\Bin64\BeamNG.drive.x64.exe)";
    std::wstring Command = Dir + L"\\BeamNG.drive.exe";

    debug(L"BeamNG executable path: " + Command);

    for (const auto& arg : args) {
        Command += L" ";
        Command += Utils::ToWString(arg);
    }

    if (CreateProcessW(nullptr, Command.data(), nullptr, nullptr, TRUE, 0, nullptr, BaseDir.c_str(), &si, &pi)) {
        info("Game Launched!");
        GamePID = pi.dwProcessId;
        GetLifecycle().Set(EngineState::GameRunning);
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD code = 0;
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        GamePID = 0;
        result.launched = true;
        result.exit_code = static_cast<int>(code);
    } else {
        DWORD dw = GetLastError();
        LPVOID lpErrorMsgBuffer;

        if (FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, dw,
                MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR)&lpErrorMsgBuffer, 0, nullptr)
            == 0) {
            result.error = "Unknown error code: " + std::to_string(dw);
        } else {
            result.error = "Error " + std::to_string(dw) + ": " + (char*)lpErrorMsgBuffer;
        }
    }
    return result;
}

bool StopGame() {
    unsigned long pid = GamePID;
    if (pid == 0) {
        return false;
    }
    HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (!process) {
        return false;
    }
    BOOL stopped = TerminateProcess(process, 1);
    CloseHandle(process);
    return stopped;
}
#elif (defined(__linux__) || defined(__APPLE__))
static GameResult RunGame(const std::string& Dir, const std::vector<std::string>& args) {
    GameResult result;
    std::string filename = (Dir + "/BinLinux/BeamNG.drive.x64");
    std::vector<const char*> argv;
    argv.push_back(filename.data());
    for (const auto& arg : args) {
        argv.push_back(arg.c_str());
    }

    argv.push_back(nullptr);
    pid_t pid;

    posix_spawn_file_actions_t file_actions;
    auto status = posix_spawn_file_actions_init(&file_actions);
    // disable stdout
    if (status != 0) {
        error(std::string("posix_spawn_file_actions_init failed: ") + std::strerror(errno));
    }
    status = posix_spawn_file_actions_addclose(&file_actions, STDOUT_FILENO);
    if (status != 0) {
        error(std::string("posix_spawn_file_actions_addclose for STDOUT failed: ") + std::strerror(errno));
    }
    status = posix_spawn_file_actions_addclose(&file_actions, STDERR_FILENO);
    if (status != 0) {
        error(std::string("posix_spawn_file_actions_addclose for STDERR failed: ") + std::strerror(errno));
    }

    // launch the game
    int spawned = posix_spawn(&pid, filename.c_str(), &file_actions, NULL, const_cast<char**>(argv.data()), environ);
    posix_spawn_file_actions_destroy(&file_actions);

    if (spawned != 0) {
        result.error = std::strerror(spawned);
        return result;
    }
    GamePID = static_cast<unsigned long>(pid);
    GetLifecycle().Set(EngineState::GameRunning);
    int wait_status = 0;
    while (waitpid(pid, &wait_status, 0) == -1 && errno == EINTR) { }
    GamePID = 0;
    result.launched = true;
    result.exit_code = WIFEXITED(wait_status) ? WEXITSTATUS(wait_status) : 128 + WTERMSIG(wait_status);
    return result;
}

bool StopGame() {
    unsigned long pid = GamePID;
    return pid != 0 && kill(static_cast<pid_t>(pid), SIGTERM) == 0;
}
#endif

static void StartGame(beammp_fs_string Dir) {
    GameResult result = RunGame(Dir, ConsoleGameArguments());
    if (result.launched) {
        error("Game Closed! launcher closing soon");
    } else {
        error("Failed to Launch the game! launcher closing soon. " + result.error);
#if (defined(__linux__) || defined(__APPLE__))
        return; // the launcher has always kept running when the spawn fails here
#endif
    }
    std::this_thread::sleep_for(std::chrono::seconds(5));
    exit(2);
}

void LaunchGame(const std::vector<std::string>& args) {
    std::thread([Dir = GetGameDir(), args] {
        GameResult result = RunGame(Dir, args);
        std::string detail;
        if (result.launched) {
            detail = "The game exited with code " + std::to_string(result.exit_code);
            info(detail);
        } else {
            detail = "Failed to launch the game: " + result.error;
            error(detail);
        }
        GetLifecycle().Set(EngineState::Ready, detail);
    }).detach();
}

void InitGame(const beammp_fs_string& Dir) {
    if (!options.no_launch && !options.controlled()) {
        GetLifecycle().Set(EngineState::LaunchingGame);
        std::thread Game(StartGame, Dir);
        Game.detach();
    }
}
