/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include "Control/Lifecycle.h"
#include "Control/Server.h"
#include "Games/GameModule.h"
#include "Games/RelaySession.h"
#include "Logger.h"
#include "Status.h"
#include <atomic>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace Control {

enum class SignInStart {
    Started,
    /// A sign-in (or restoring a saved one) is already running.
    Busy,
    AlreadySignedIn,
    Unavailable,
};

/// What the controller can ask of the account side.
struct AccountOps {
    std::function<SignInStart()> signIn;
    std::function<void()> cancelSignIn;
    /// Forgets every sign-in, however it was made.
    std::function<void()> signOut;
};

/// Implements the control protocol requests and events on top of the engine's state.
/// Construct before Server::Start, and stop the server before destroying the controller.
class Controller {
public:
    Controller(Server& server, Lifecycle& lifecycle, AccountOps accountOps, std::string engineVersion);
    ~Controller();
    Controller(const Controller&) = delete;
    Controller& operator=(const Controller&) = delete;

    /// Reports progress of a sign-in to the controller (`account.signin`).
    /// `state` is waiting_for_browser, exchanging, failed or idle.
    void PublishSignIn(const std::string& state, const std::string& url, const std::string& reason);

    /// Lists a game and routes its requests to the module, which has to outlive the controller. The engine
    /// runs one game at a time. A new game starts as available; see SetGameStatus.
    void RegisterGame(Games::GameModule& module);

    /// Makes rooms at the relay available: `session.*` requests, `session.state` events, and `sessions: true` for
    /// the games whose modules support them. Only before the server starts; the session outlives the controller.
    void SetSession(Games::RelaySession& session);

    /// Called for `game.configure` (the engine's side applies the settings and prepares the game again).
    /// Return false for a game it does not know.
    using ConfigureHandler = std::function<bool(const std::string& game, const Games::GameConfig& config)>;
    void SetConfigureHandler(ConfigureHandler handler);

    /// What the engine found out about a game (after it prepared it): not installed, failed, or fine.
    /// Controllers are told with a `game.state` event.
    void SetGameStatus(const std::string& id, Games::GameStatus status, const std::string& detail = "");

    /// Passes a message from the game's mod on to the controller (`game.message`).
    void PublishGameMessage(const std::string& game, const nlohmann::json& message);

    /// True once a client has completed the handshake at least once.
    bool EverAttached() const { return m_everAttached; }

private:
    struct Subscription {
        int severity;
    };

    nlohmann::json State() const;
    nlohmann::json Subscribe(const nlohmann::json& data);
    nlohmann::json Launch(const nlohmann::json& data);
    nlohmann::json Stop(const nlohmann::json& data);
    nlohmann::json Shutdown(const nlohmann::json& data);
    nlohmann::json SendToGame(const nlohmann::json& data);
    nlohmann::json ConfigureGame(const nlohmann::json& data);
    nlohmann::json InstallRequest(const std::string& name, const nlohmann::json& data);
    nlohmann::json SessionRequest(const std::string& name, const nlohmann::json& data);
    void AdvertiseGames();
    /// Game data from the mod of `game` goes to the other players, when the game is the room's.
    void SendGameData(const std::string& game, const nlohmann::json& message);
    /// Data from another player goes to the mod of the room's game.
    void DeliverGameData(const Relay::DataFrame& frame);

    struct Game {
        Games::GameModule* module;
        std::string id;
        std::string name;
        Games::GameStatus status = Games::GameStatus::Available;
        std::string detail;
    };

    void RunInstall(Game game, bool uninstall, bool removeLoader);

    /// A copy, taken under the lock. Throws unknown_game.
    Game FindGame(const nlohmann::json& data) const;
    /// Like FindGame, but the game has to be usable: game_not_found, game_not_ready or game_unavailable otherwise.
    Game RequireGame(const nlohmann::json& data) const;
    std::vector<Game> Games() const;
    std::string ActiveGame() const;
    void SetActiveGame(std::string id);
    nlohmann::json GameJson(const Game& game, EngineState state) const;
    void PublishGameState(const Game& game, EngineState state, bool alwaysWithPid);
    /// `alwaysWithPid` is for a newly attached controller, which gets the pid even when it is 0.
    void PublishGameStates(EngineState state, bool alwaysWithPid);

    bool GameActive() const;
    void Attached();
    void Detached();
    void StateChanged(EngineState previous, EngineState current, const std::string& detail);
    void Logged(const LogRecord& record);

    Server& m_server;
    Lifecycle& m_lifecycle;
    ConfigureHandler m_configure;
    Games::RelaySession* m_session = nullptr;
    mutable std::mutex m_gamesMutex;
    std::vector<Game> m_games;
    const AccountOps m_account;
    const std::string m_engineVersion;
    std::atomic<bool> m_everAttached = false;

    // At most one install or uninstall at a time, on its own thread.
    std::atomic<bool> m_installing = false;
    mutable std::mutex m_installMutex;
    std::string m_installGame;
    std::thread m_installThread;

    std::mutex m_logMutex;
    std::deque<LogRecord> m_recent;
    std::optional<Subscription> m_subscription;

    mutable std::mutex m_activeMutex;
    std::string m_activeGame;

    std::mutex m_signInMutex;
    nlohmann::json m_lastSignIn;

    LogSinkId m_logSink;
    Lifecycle::StateObservers::Id m_stateObserver;
    Observers<const Account&>::Id m_accountObserver;
};

}
