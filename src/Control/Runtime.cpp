/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Control/Runtime.h"
#include "Auth/Runtime.h"
#include "Control/Controller.h"
#include "Games/RelayLink.h"
#include "Games/RelaySession.h"
#include "Logger.h"
#include "Network/network.hpp"
#include "Options.h"
#include "Security/Init.h"
#include "Startup.h"
#include "Status.h"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
constexpr size_t MinTokenLength = 16;
constexpr auto AttachTimeout = std::chrono::seconds(30);
}

namespace {
Control::Controller* g_controller = nullptr;

// The player's rooms at the BeamMP relay (--relay host:port or BEAMMP_RELAY), when there is one.
void StartSession(Control::Controller& controller, Games::GamePreparer& preparer) {
    std::string address = options.relay;
    if (address.empty()) {
        if (const char* fromEnvironment = std::getenv("BEAMMP_RELAY")) {
            address = fromEnvironment;
        }
    }
    Relay::Endpoint endpoint;
    size_t colon = address.rfind(':');
    if (colon != std::string::npos && colon + 1 < address.size()) {
        try {
            int port = std::stoi(address.substr(colon + 1));
            if (port > 0 && port < 65536) {
                endpoint = { address.substr(0, colon), static_cast<uint16_t>(port) };
            }
        } catch (const std::exception&) { }
    }
    if (endpoint.host.empty() && !address.empty()) {
        warn("The relay address needs the form host:port, sessions are off: " + address);
    }
    Games::RelaySession::Options sessionOptions;
    sessionOptions.relay = endpoint;
    sessionOptions.allowInsecureRemote = options.relay_insecure;
    sessionOptions.playerName = [] { return GetAccount().username; };
    sessionOptions.clientName = "beammp-launcher/" + GetVer() + GetPatch();
    // Lives until the process exits, like the controller.
    auto* session = new Games::RelaySession(sessionOptions, Relay::ConnectTcpLink);
    if (!session->Available()) {
        if (!endpoint.host.empty()) {
            warn("Sessions are off: " + session->UnavailableReason());
        }
    } else {
        info("Sessions go through the relay at " + endpoint.host + ":" + std::to_string(endpoint.port));
    }
    controller.SetSession(*session);

    // The game's mod is told how the session stands whenever it changes, and when it connects.
    auto tell = [&preparer](const Games::SessionInfo& state) {
        for (const auto& module : preparer.Games().All()) {
            if (module->SupportsSessions() && module->Id() == state.game) {
                module->Send({ { "type", "session" }, { "session", Games::ToJson(state) } });
            }
        }
    };
    session->AddObserver(tell);
    for (const auto& module : preparer.Games().All()) {
        if (module->SupportsSessions()) {
            Games::GameModule* raw = module.get();
            module->SetReadyHandler([session, raw] {
                Games::SessionInfo state = session->Info();
                if (state.game == raw->Id() && state.state != Games::SessionInfo::State::Idle) {
                    raw->Send({ { "type", "session" }, { "session", Games::ToJson(state) } });
                }
            });
        }
    }
}
}

Control::Controller* GetController() {
    return g_controller;
}

void StartControl(Games::GamePreparer& preparer) {
    if (options.control_token.size() < MinTokenLength) {
        fatal("The control channel needs the BEAMMP_CONTROL_TOKEN environment variable (at least 16 characters)");
    }

    Control::AccountOps account;
    account.signIn = [] {
        Auth::Session* auth = ActiveAuth();
        if (!auth) {
            return Control::SignInStart::Unavailable;
        }
        if (GetAccount().authenticated && !auth->SignedIn()) {
            return Control::SignInStart::AlreadySignedIn; // signed in from the game
        }
        switch (auth->SignIn()) {
        case Auth::SignInResult::Started:
            return Control::SignInStart::Started;
        case Auth::SignInResult::Busy:
            return Control::SignInStart::Busy;
        case Auth::SignInResult::AlreadySignedIn:
            break;
        }
        return Control::SignInStart::AlreadySignedIn;
    };
    account.cancelSignIn = [] {
        if (Auth::Session* auth = ActiveAuth()) {
            auth->Cancel();
        }
    };
    account.signOut = [] {
        if (Auth::Session* auth = ActiveAuth()) {
            auth->SignOut();
        }
        Login("LO");
    };

    // Both live until the process exits, other threads may still be using them then.
    auto* server = new Control::Server(options.control_token);
    auto* controller = new Control::Controller(*server, GetLifecycle(), account, GetVer() + GetPatch());
    for (const auto& module : preparer.Games().All()) {
        controller->RegisterGame(*module);
    }
    g_controller = controller;
    StartSession(*controller, preparer);
    preparer.SetReporter([controller](const std::string& id, const Games::PrepareResult& result) {
        if (!result.ok() && result.status != Games::GameStatus::Preparing) {
            warn(id + " is not available: " + result.detail);
        }
        controller->SetGameStatus(id, result.status, result.detail);
    });
    controller->SetConfigureHandler([&preparer](const std::string& id, const Games::GameConfig& config) {
        return preparer.Reconfigure(id, config);
    });

    uint16_t port = 0;
    try {
        port = server->Start(static_cast<uint16_t>(options.control_port));
    } catch (const std::exception& e) {
        fatal(e.what());
    }
    StartAuth();
    if (Auth::Session* auth = ActiveAuth()) {
        auth->AddObserver([controller](const Auth::SignInEvent& event) {
            controller->PublishSignIn(Auth::ToString(event.state), event.url, event.reason);
        });
        auth->Restore();
    }

    info("Control channel listening on 127.0.0.1:" + std::to_string(port));
    std::cout << nlohmann::json { { "control_port", port } }.dump() << std::endl;

    std::thread([server, controller] {
        auto started = std::chrono::steady_clock::now();
        Lifecycle& lifecycle = GetLifecycle();
        while (!lifecycle.WaitForShutdown(std::chrono::milliseconds(500))) {
            if (!controller->EverAttached() && std::chrono::steady_clock::now() - started > AttachTimeout) {
                warn("No controller attached, shutting down");
                lifecycle.RequestShutdown();
            }
        }
        info("Shutting down");
        server->Stop();
        std::exit(0);
    }).detach();
}
