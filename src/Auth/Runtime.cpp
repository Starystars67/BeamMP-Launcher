/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Auth/Runtime.h"

#include "Logger.h"
#include "Network/network.hpp"
#include "Options.h"
#include "Status.h"
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>

#if defined(_WIN32)
#include <shellapi.h>
#include <windows.h>
#else
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace {
Auth::Session* Active = nullptr;

std::string EnvironmentUrl(const char* name, const std::string& fallback) {
    const char* value = std::getenv(name);
    if (!value || !*value) {
        return fallback;
    }
    if (!Auth::IsAcceptableBaseUrl(value)) {
        warn(std::string(name) + " is not an https URL (or a loopback http URL), ignoring it");
        return fallback;
    }
    return value;
}

bool OpenBrowser(const std::string& url) {
    if (options.no_browser) {
        return true;
    }
    // Only ever our own authorize page or the loopback server.
    if (url.rfind("https://", 0) != 0 && url.rfind("http://127.0.0.1", 0) != 0) {
        return false;
    }
#if defined(_WIN32)
    return reinterpret_cast<INT_PTR>(ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
#else
#if defined(__APPLE__)
    const char* opener = "/usr/bin/open";
#else
    const char* opener = "xdg-open";
#endif
    std::string target = url;
    char* argv[] = { const_cast<char*>(opener), target.data(), nullptr };
    pid_t pid = 0;
    if (posix_spawnp(&pid, opener, nullptr, nullptr, argv, environ) != 0) {
        return false;
    }
    // The opener returns at once; reaped in the background so it does not linger.
    std::thread([pid] {
        int status = 0;
        waitpid(pid, &status, 0);
    }).detach();
    return true;
#endif
}
}

void StartAuth() {
    if (Active) {
        return;
    }

    Auth::Session::Config config;
    config.endpoints.apiBase = EnvironmentUrl("BEAMMP_API_URL", config.endpoints.apiBase);
    config.endpoints.accountsBase = EnvironmentUrl("BEAMMP_ACCOUNTS_URL", config.endpoints.accountsBase);

    // Kept for the life of the process, other threads use them until it ends.
    static auto transport = Auth::MakeCurlTransport();
    const char* service = std::getenv("BEAMMP_KEYCHAIN_SERVICE");
    static auto secrets = service && *service ? Auth::MakeSystemSecretStore(service) : Auth::MakeSystemSecretStore();
    static Auth::Session session(config, *transport, *secrets, OpenBrowser);

    session.OnSignedIn([](const Auth::GameSession& game) {
        PublicKey = game.publicKey;
        PrivateKey = game.privateKey;
        SetAccount({ true, game.username, game.role, game.id });
        info("Signed in as " + game.username);
    });
    session.OnSignedOut([] {
        PublicKey.clear();
        PrivateKey.clear();
        SetAccount({});
        info("Signed out");
    });

    Active = &session;
}

Auth::Session* ActiveAuth() {
    return Active;
}
