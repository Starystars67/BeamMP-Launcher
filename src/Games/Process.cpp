/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Games/Process.h"

#include <algorithm>
#include <cctype>

#if defined(_WIN32)
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace Games {

namespace fs = std::filesystem;

LaunchSpec BuildCommandLaunch(const std::string& gameKey, const fs::path& gameDir, const std::string& userPath,
    const std::string& command, const std::vector<std::string>& args) {
    std::string key = gameKey;
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    LaunchSpec spec;
    spec.program = command;
    spec.args = args;
    spec.workingDirectory = gameDir.string();
    if (!gameDir.empty()) {
        spec.environment.emplace_back(key + "_DIR", gameDir.string());
        spec.environment.emplace_back("BEAMMP_GAME_DIR", gameDir.string());
    }
    if (!userPath.empty()) {
        spec.environment.emplace_back("BEAMMP_USER_PATH", userPath);
    }
    return spec;
}

#if defined(_WIN32)
ProcessRunner SystemProcessRunner() {
    return [](const LaunchSpec& spec, const std::function<void(unsigned long)>& started) {
        RunResult result;
        // The child gets our environment plus the bridge variables.
        for (const auto& [name, value] : spec.environment) {
            SetEnvironmentVariableA(name.c_str(), value.c_str());
        }
        std::string command = "\"" + spec.program + "\"";
        for (const auto& arg : spec.args) {
            command += " " + arg;
        }
        STARTUPINFOA si = {};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi = {};
        BOOL created = CreateProcessA(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr,
            spec.workingDirectory.empty() ? nullptr : spec.workingDirectory.c_str(), &si, &pi);
        // Not left set in the launcher itself, the next launch has its own token.
        for (const auto& entry : spec.environment) {
            SetEnvironmentVariableA(entry.first.c_str(), nullptr);
        }
        if (!created) {
            result.error = "Error " + std::to_string(GetLastError());
            return result;
        }
        started(pi.dwProcessId);
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD code = 0;
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        result.launched = true;
        result.exitCode = static_cast<int>(code);
        return result;
    };
}

bool StopSystemProcess(unsigned long pid) {
    HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (!process) {
        return false;
    }
    BOOL stopped = TerminateProcess(process, 1);
    CloseHandle(process);
    return stopped;
}
#else
ProcessRunner SystemProcessRunner() {
    return [](const LaunchSpec& spec, const std::function<void(unsigned long)>& started) {
        RunResult result;
        std::vector<std::string> environment;
        for (char** entry = environ; entry && *entry; ++entry) {
            environment.emplace_back(*entry);
        }
        for (const auto& [name, value] : spec.environment) {
            std::string prefix = name + "=";
            std::erase_if(environment, [&](const std::string& existing) { return existing.rfind(prefix, 0) == 0; });
            environment.push_back(prefix + value);
        }
        std::vector<char*> envp;
        for (auto& entry : environment) {
            envp.push_back(entry.data());
        }
        envp.push_back(nullptr);

        std::vector<std::string> argvStorage { spec.program };
        argvStorage.insert(argvStorage.end(), spec.args.begin(), spec.args.end());
        std::vector<char*> argv;
        for (auto& arg : argvStorage) {
            argv.push_back(arg.data());
        }
        argv.push_back(nullptr);

        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        // stdout carries the control protocol of a controlled launcher, the game must not write there
        posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
        posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
        posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
        if (!spec.workingDirectory.empty()) {
#if defined(__APPLE__) && defined(__MAC_OS_X_VERSION_MAX_ALLOWED) && __MAC_OS_X_VERSION_MAX_ALLOWED >= 260000
            posix_spawn_file_actions_addchdir(&actions, spec.workingDirectory.c_str());
#else
            posix_spawn_file_actions_addchdir_np(&actions, spec.workingDirectory.c_str());
#endif
        }
        posix_spawnattr_t attributes;
        posix_spawnattr_init(&attributes);
        // its own process group, so that stopping it also stops what it started (Wine)
        posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
        posix_spawnattr_setpgroup(&attributes, 0);

        pid_t pid = 0;
        int spawned = posix_spawn(&pid, spec.program.c_str(), &actions, &attributes, argv.data(), envp.data());
        posix_spawn_file_actions_destroy(&actions);
        posix_spawnattr_destroy(&attributes);
        if (spawned != 0) {
            result.error = std::strerror(spawned);
            return result;
        }
        started(static_cast<unsigned long>(pid));
        int status = 0;
        while (waitpid(pid, &status, 0) == -1 && errno == EINTR) { }
        result.launched = true;
        result.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
        return result;
    };
}

bool StopSystemProcess(unsigned long pid) {
    pid_t target = static_cast<pid_t>(pid);
    return kill(-target, SIGTERM) == 0 || kill(target, SIGTERM) == 0;
}
#endif

}
