/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Control/Controller.h"
#include "Games/SessionData.h"

#include <algorithm>
#include <stdexcept>

namespace Control {

namespace {
    // What a controller sees when no game was ever launched.
    constexpr const char* DefaultActiveGame = "beamng";
    constexpr size_t RecentLogs = 1000;
    constexpr size_t MaxLaunchArguments = 64;
    constexpr size_t MaxLaunchArgumentLength = 4096;
    constexpr size_t MaxSettingLength = 4096;
    constexpr size_t MaxPasswordLength = 64;
    constexpr int MaxRoomPlayers = 32;

    using nlohmann::json;

    const char* LevelName(LogLevel level) {
        switch (level) {
        case LogLevel::Debug:
            return "debug";
        case LogLevel::Info:
            return "info";
        case LogLevel::Warn:
            return "warn";
        case LogLevel::Error:
            return "error";
        case LogLevel::Fatal:
            return "fatal";
        case LogLevel::Except:
            return "except";
        }
        return "info";
    }

    int Severity(LogLevel level) {
        switch (level) {
        case LogLevel::Debug:
            return 0;
        case LogLevel::Info:
            return 1;
        case LogLevel::Warn:
            return 2;
        case LogLevel::Error:
        case LogLevel::Except:
            return 3;
        case LogLevel::Fatal:
            return 4;
        }
        return 1;
    }

    std::optional<int> ParseSeverity(const std::string& name) {
        for (LogLevel level : { LogLevel::Debug, LogLevel::Info, LogLevel::Warn, LogLevel::Error, LogLevel::Fatal }) {
            if (name == LevelName(level)) {
                return Severity(level);
            }
        }
        return std::nullopt;
    }

    template <typename T>
    T Argument(const json& data, const char* key, T fallback) {
        auto found = data.find(key);
        if (found == data.end()) {
            return fallback;
        }
        try {
            return found->get<T>();
        } catch (const json::exception&) {
            throw RequestError("invalid_argument", std::string("Invalid argument: ") + key);
        }
    }

    json AccountJson(const Account& account) {
        json result = { { "signed_in", account.authenticated } };
        if (!account.username.empty()) {
            result["username"] = account.username;
        }
        if (!account.role.empty()) {
            result["role"] = account.role;
        }
        if (account.id != -1) {
            result["id"] = account.id;
        }
        return result;
    }

    const char* GameStateName(EngineState state) {
        switch (state) {
        case EngineState::Starting:
        case EngineState::Updating:
        case EngineState::PreparingGame:
            return "preparing";
        case EngineState::Ready:
            return "ready";
        case EngineState::LaunchingGame:
            return "launching";
        case EngineState::GameRunning:
            return "running";
        case EngineState::Error:
        case EngineState::Fatal:
            return "error";
        }
        return "preparing";
    }

    json LogJson(const LogRecord& record) {
        return { { "ts", FormatTimestamp(record.time) }, { "level", LevelName(record.level) }, { "message", record.message } };
    }

    bool GameWasActive(EngineState state) {
        return state == EngineState::LaunchingGame || state == EngineState::GameRunning;
    }
}

Controller::Controller(Server& server, Lifecycle& lifecycle, AccountOps accountOps, std::string engineVersion)
    : m_server(server)
    , m_lifecycle(lifecycle)
    , m_account(std::move(accountOps))
    , m_engineVersion(std::move(engineVersion)) {
    AdvertiseGames();
    m_server.OnAttached([this] { Attached(); });
    m_server.OnDetached([this] { Detached(); });

    m_server.Handle("engine.state", [this](const json&) { return State(); });
    m_server.Handle("engine.shutdown", [this](const json& data) { return Shutdown(data); });
    m_server.Handle("log.subscribe", [this](const json& data) { return Subscribe(data); });
    m_server.Handle("log.unsubscribe", [this](const json&) {
        std::lock_guard<std::mutex> lock(m_logMutex);
        m_subscription.reset();
        return json::object();
    });
    m_server.Handle("games.list", [this](const json&) {
        json games = json::array();
        EngineState state = m_lifecycle.State();
        for (const Game& game : Games()) {
            games.push_back(GameJson(game, state));
        }
        return json { { "games", games } };
    });
    m_server.Handle("game.launch", [this](const json& data) { return Launch(data); });
    m_server.Handle("game.stop", [this](const json& data) { return Stop(data); });
    m_server.Handle("game.send", [this](const json& data) { return SendToGame(data); });
    m_server.Handle("game.configure", [this](const json& data) { return ConfigureGame(data); });
    for (const char* name : { "game.install", "game.uninstall", "game.install_cancel" }) {
        m_server.Handle(name, [this, name](const json& data) { return InstallRequest(name, data); });
    }
    m_server.Handle("account.state", [](const json&) { return AccountJson(GetAccount()); });
    m_server.Handle("account.sign_in", [this](const json&) {
        SignInStart started = m_account.signIn ? m_account.signIn() : SignInStart::Unavailable;
        switch (started) {
        case SignInStart::Started:
            return json::object();
        case SignInStart::Busy:
            throw RequestError("sign_in_busy", "A sign-in is already in progress");
        case SignInStart::AlreadySignedIn:
            throw RequestError("already_signed_in", "Already signed in");
        case SignInStart::Unavailable:
            break;
        }
        throw RequestError("sign_in_unavailable", "Signing in is not available");
    });
    m_server.Handle("account.cancel_sign_in", [this](const json&) {
        if (m_account.cancelSignIn) {
            m_account.cancelSignIn();
        }
        return json::object();
    });
    m_server.Handle("account.sign_out", [this](const json&) {
        if (m_account.signOut) {
            m_account.signOut();
        }
        return AccountJson(GetAccount());
    });

    m_stateObserver = m_lifecycle.AddObserver([this](EngineState previous, EngineState current, const std::string& detail) {
        StateChanged(previous, current, detail);
    });
    m_accountObserver = AddAccountObserver([this](const Account& account) {
        m_server.Publish("account.state", AccountJson(account));
    });
    m_logSink = AddLogSink([this](const LogRecord& record) { Logged(record); });
}

Controller::~Controller() {
    {
        std::lock_guard<std::mutex> lock(m_gamesMutex);
        for (const Game& game : m_games) {
            if (game.module->SupportsInstall()) {
                game.module->CancelInstall();
            }
        }
    }
    if (m_installThread.joinable()) {
        m_installThread.join();
    }
    RemoveLogSink(m_logSink);
    RemoveAccountObserver(m_accountObserver);
    m_lifecycle.RemoveObserver(m_stateObserver);
}

void Controller::RegisterGame(Games::GameModule& module) {
    std::string id = module.Id();
    {
        std::lock_guard<std::mutex> lock(m_gamesMutex);
        if (std::any_of(m_games.begin(), m_games.end(), [&](const Game& game) { return game.id == id; })) {
            throw std::invalid_argument("Game already registered: " + id);
        }
        m_games.push_back({ &module, id, module.Name(), Games::GameStatus::Available, "" });
    }
    if (module.HasMessageChannel()) {
        module.SetMessageHandler([this, id](const json& message) {
            // game data belongs to the room, not to the user interface
            if (Games::IsDataMessage(message)) {
                SendGameData(id, message);
            } else {
                PublishGameMessage(id, message);
            }
        });
    }
    AdvertiseGames();
}

void Controller::SetSession(Games::RelaySession& session) {
    m_session = &session;
    for (const char* name : { "session.create", "session.join", "session.leave", "session.state" }) {
        m_server.Handle(name, [this, name](const json& data) { return SessionRequest(name, data); });
    }
    session.AddObserver([this](const Games::SessionInfo& info) { m_server.Publish("session.state", Games::ToJson(info)); });
    session.SetDataHandler([this](const Relay::DataFrame& frame) { DeliverGameData(frame); });
    AdvertiseGames();
}

json Controller::SessionRequest(const std::string& name, const json& data) {
    if (name == "session.state") {
        return Games::ToJson(m_session->Info());
    }
    if (name == "session.leave") {
        m_session->Leave();
        return json::object();
    }
    Game game = RequireGame(data);
    if (!game.module->SupportsSessions()) {
        throw RequestError("session_unsupported", "This game cannot be played together through the relay");
    }
    std::string password = Argument<std::string>(data, "password", "");
    if (password.size() > MaxPasswordLength) {
        throw RequestError("invalid_argument", "Invalid argument: password");
    }
    Games::SessionStart started;
    if (name == "session.create") {
        int maxPlayers = Argument<int>(data, "max_players", 0);
        if (data.contains("max_players") && (maxPlayers < 2 || maxPlayers > MaxRoomPlayers)) {
            throw RequestError("invalid_argument", "Invalid argument: max_players");
        }
        started = m_session->Create(game.id, maxPlayers, password);
    } else {
        started = m_session->Join(game.id, Argument<std::string>(data, "code", ""), password);
    }
    switch (started) {
    case Games::SessionStart::Started:
        return json::object();
    case Games::SessionStart::Unavailable:
        throw RequestError("session_unavailable", m_session->UnavailableReason());
    case Games::SessionStart::Busy:
        throw RequestError("session_busy", "Already in a session; leave it first");
    case Games::SessionStart::BadCode:
        throw RequestError("invalid_argument", "Invalid argument: code");
    }
    throw RequestError("internal_error", "Unexpected session result");
}

void Controller::SetConfigureHandler(ConfigureHandler handler) {
    m_configure = std::move(handler);
}

json Controller::ConfigureGame(const json& data) {
    Game game = FindGame(data);
    Games::GameConfig config;
    for (auto [key, target] : { std::pair<const char*, std::string*> { "directory", &config.directory },
             { "command", &config.command }, { "user_path", &config.userPath } }) {
        *target = Argument<std::string>(data, key, "");
        if (target->size() > MaxSettingLength || target->find('\0') != std::string::npos) {
            throw RequestError("invalid_argument", std::string("Invalid argument: ") + key);
        }
    }
    if (m_lifecycle.State() != EngineState::Ready || game.status == Games::GameStatus::Preparing || m_installing) {
        throw RequestError("game_not_ready", "Settings can only be changed while the engine is ready and no game starts or runs");
    }
    if (!m_configure || !m_configure(game.id, config)) {
        throw RequestError("unknown_game", "Unknown game");
    }
    return json::object();
}

json Controller::InstallRequest(const std::string& name, const json& data) {
    Game game = FindGame(data);
    if (!game.module->SupportsInstall()) {
        throw RequestError("install_unsupported", "This game has nothing to install");
    }
    if (name == "game.install_cancel") {
        std::lock_guard<std::mutex> lock(m_installMutex);
        if (m_installing && m_installGame == game.id) {
            game.module->CancelInstall();
        }
        return json::object();
    }
    bool uninstall = name == "game.uninstall";
    bool removeLoader = uninstall && Argument<bool>(data, "remove_loader", false);
    if (m_lifecycle.State() != EngineState::Ready || game.status == Games::GameStatus::Preparing) {
        throw RequestError("game_not_ready", "Multiplayer support can only be changed while the engine is ready and no game starts or runs");
    }
    if (m_installing.exchange(true)) {
        throw RequestError("install_busy", "Something is being installed or removed already");
    }
    {
        std::lock_guard<std::mutex> lock(m_installMutex);
        m_installGame = game.id;
        if (m_installThread.joinable()) {
            m_installThread.join();
        }
        m_installThread = std::thread([this, game, uninstall, removeLoader] { RunInstall(game, uninstall, removeLoader); });
    }
    return json::object();
}

void Controller::RunInstall(Game game, bool uninstall, bool removeLoader) {
    auto publish = [this, &game](const std::string& state, const std::string& step, const std::string& message, double fraction, bool droppable) {
        json event = { { "game", game.id }, { "state", state }, { "step", step }, { "message", message } };
        if (fraction >= 0) {
            event["fraction"] = fraction;
        }
        m_server.Publish("game.install", event, droppable);
    };
    publish("running", "check", uninstall ? "Starting" : "Starting", 0.0, false);
    Games::InstallReporter report = [&](const Games::InstallProgress& progress) { publish("running", progress.step, progress.message, progress.fraction, true); };
    Games::InstallResult result;
    try {
        result = uninstall ? game.module->Uninstall(removeLoader, report) : game.module->Install(report);
    } catch (const std::exception& e) {
        result = { false, "internal_error", e.what() };
    }
    // the game's state follows what is on disk now
    if (m_configure) {
        m_configure(game.id, game.module->Config());
    }
    json event = { { "game", game.id }, { "state", result.ok ? "done" : (result.code == "cancelled" ? "cancelled" : "failed") }, { "step", "done" }, { "message", result.message } };
    if (result.ok) {
        event["fraction"] = 1.0;
    } else {
        event["error"] = { { "code", result.code }, { "message", result.message } };
    }
    event["uninstall"] = uninstall;
    m_installing = false;
    m_server.Publish("game.install", event, false);
}

void Controller::SetGameStatus(const std::string& id, Games::GameStatus status, const std::string& detail) {
    std::optional<Game> changed;
    {
        std::lock_guard<std::mutex> lock(m_gamesMutex);
        for (Game& game : m_games) {
            if (game.id == id) {
                game.status = status;
                game.detail = detail;
                changed = game;
            }
        }
    }
    if (changed) {
        PublishGameState(*changed, m_lifecycle.State(), false);
    }
}

std::vector<Controller::Game> Controller::Games() const {
    std::lock_guard<std::mutex> lock(m_gamesMutex);
    return m_games;
}

void Controller::AdvertiseGames() {
    json ids = json::array();
    for (const Game& game : Games()) {
        ids.push_back(game.id);
    }
    json capabilities = { "logs", "games", "account", "signin", "game_messages" };
    if (m_session && m_session->Available()) {
        capabilities.push_back("sessions");
    }
    for (const Game& game : Games()) {
        if (game.module->SupportsInstall()) {
            capabilities.push_back("game_install");
            break;
        }
    }
    m_server.SetHelloInfo({
        { "engine", m_engineVersion },
        { "capabilities", capabilities },
        { "games", ids },
    });
}

void Controller::SendGameData(const std::string& game, const json& message) {
    if (!m_session) {
        return;
    }
    Games::SessionInfo state = m_session->Info();
    if (state.state != Games::SessionInfo::State::InRoom || state.game != game) {
        debug("Game data from " + game + " dropped: no room for it");
        return; // a mod that is not the room's, or no room: nothing to send to
    }
    auto data = Games::ParseOutgoingData(message);
    if (!data) {
        debug("Dropped malformed game data from " + game);
        return;
    }
    if (!m_session->SendData(data->kind, data->target, data->bytes)) {
        debug("Could not send game data from " + game + " to the room");
    }
}

void Controller::DeliverGameData(const Relay::DataFrame& frame) {
    Games::SessionInfo state = m_session ? m_session->Info() : Games::SessionInfo();
    if (state.state != Games::SessionInfo::State::InRoom) {
        return;
    }
    for (const Game& game : Games()) {
        if (game.id == state.game) {
            game.module->Send(Games::MakeIncomingData(frame));
            return;
        }
    }
}

void Controller::PublishGameMessage(const std::string& game, const nlohmann::json& message) {
    m_server.Publish("game.message", { { "game", game }, { "message", message } }, true);
}

Controller::Game Controller::FindGame(const json& data) const {
    std::string id = Argument<std::string>(data, "game", "");
    for (const Game& game : Games()) {
        if (game.id == id) {
            return game;
        }
    }
    throw RequestError("unknown_game", "Unknown game");
}

Controller::Game Controller::RequireGame(const json& data) const {
    Game game = FindGame(data);
    switch (game.status) {
    case Games::GameStatus::Available:
        return game;
    case Games::GameStatus::Preparing:
        throw RequestError("game_not_ready", "The game is being prepared");
    case Games::GameStatus::NotFound:
        throw RequestError("game_not_found", game.detail.empty() ? "The game is not installed" : game.detail);
    case Games::GameStatus::Unavailable:
        throw RequestError("game_unavailable", "Multiplayer for this game is not available yet");
    case Games::GameStatus::Failed:
        throw RequestError("game_unavailable", "The game could not be prepared: " + game.detail);
    }
    throw RequestError("unknown_game", "Unknown game");
}

std::string Controller::ActiveGame() const {
    std::lock_guard<std::mutex> lock(m_activeMutex);
    return m_activeGame.empty() ? DefaultActiveGame : m_activeGame;
}

void Controller::SetActiveGame(std::string id) {
    std::lock_guard<std::mutex> lock(m_activeMutex);
    m_activeGame = std::move(id);
}

json Controller::GameJson(const Game& game, EngineState state) const {
    json result = { { "id", game.id }, { "name", game.name } };
    if (uint32_t steamAppId = game.module->SteamAppId()) {
        result["steam_app_id"] = steamAppId;
    }
    if (m_session && m_session->Available() && game.module->SupportsSessions()) {
        result["sessions"] = true;
    }
    if (game.module->SupportsInstall()) {
        json install = game.module->InstallInfo();
        if (!install.is_object()) {
            install = { { "state", "unknown" } };
        }
        {
            std::lock_guard<std::mutex> lock(m_installMutex);
            install["busy"] = m_installing.load() && m_installGame == game.id;
        }
        result["install"] = install;
    }
    switch (game.status) {
    case Games::GameStatus::Preparing:
        result["state"] = "preparing";
        break;
    case Games::GameStatus::Unavailable:
        result["state"] = "unavailable";
        break;
    case Games::GameStatus::NotFound:
        result["state"] = "not_found";
        break;
    case Games::GameStatus::Failed:
        result["state"] = "error";
        break;
    case Games::GameStatus::Available:
        if (GameWasActive(state) && game.id != ActiveGame()) {
            result["state"] = "busy";
        } else {
            result["state"] = GameStateName(state);
            if (state == EngineState::GameRunning) {
                result["pid"] = GamePID.load();
            }
        }
        break;
    }
    if (game.status != Games::GameStatus::Available && !game.detail.empty()) {
        result["detail"] = game.detail;
    }
    // what the user set (not what was detected), so a UI can show and edit it
    Games::GameConfig config = game.module->Config();
    if (!config.empty()) {
        json settings = json::object();
        if (!config.directory.empty()) {
            settings["directory"] = config.directory;
        }
        if (!config.command.empty()) {
            settings["command"] = config.command;
        }
        if (!config.userPath.empty()) {
            settings["user_path"] = config.userPath;
        }
        result["config"] = settings;
    }
    return result;
}

void Controller::PublishGameState(const Game& game, EngineState state, bool alwaysWithPid) {
    json data = GameJson(game, state);
    data["game"] = data["id"];
    data.erase("id");
    data.erase("name");
    data.erase("config");
    data.erase("steam_app_id");
    data.erase("sessions");
    if (alwaysWithPid && !data.contains("pid")) {
        data["pid"] = GamePID.load();
    }
    m_server.Publish("game.state", data);
}

// One `game.state` per launchable game, so a controller sees who is busy.
void Controller::PublishGameStates(EngineState state, bool alwaysWithPid) {
    for (const Game& game : Games()) {
        // A game that is not installed has nothing to say about the engine's stages.
        if (game.status == Games::GameStatus::Available) {
            PublishGameState(game, state, alwaysWithPid);
        }
    }
}

json Controller::State() const {
    StatusSnapshot snapshot = GetStatusSnapshot();
    return {
        { "state", ToString(m_lifecycle.State()) },
        { "detail", m_lifecycle.Detail() },
        { "status",
            {
                { "account", AccountJson(snapshot.account) },
                { "ping", snapshot.ping },
                { "upload_status", snapshot.uploadStatus },
                { "mod_status", snapshot.modStatus },
                { "game_pid", snapshot.gamePid },
            } },
    };
}

json Controller::Subscribe(const json& data) {
    std::string name = Argument<std::string>(data, "level", "info");
    auto severity = ParseSeverity(name);
    if (!severity) {
        throw RequestError("invalid_argument", "Invalid argument: level");
    }
    int backlog = Argument<int>(data, "backlog", 200);
    if (backlog < 0 || backlog > static_cast<int>(RecentLogs)) {
        throw RequestError("invalid_argument", "Invalid argument: backlog");
    }

    // Held while the backlog is queued so live records cannot overtake it.
    std::lock_guard<std::mutex> lock(m_logMutex);
    m_subscription = Subscription { *severity };
    std::vector<const LogRecord*> selected;
    for (auto record = m_recent.rbegin(); record != m_recent.rend() && selected.size() < static_cast<size_t>(backlog); ++record) {
        if (Severity(record->level) >= *severity) {
            selected.push_back(&*record);
        }
    }
    for (auto record = selected.rbegin(); record != selected.rend(); ++record) {
        m_server.Publish("log", LogJson(**record), true);
    }
    return { { "level", name }, { "sent", selected.size() } };
}

json Controller::Launch(const json& data) {
    if (m_installing) {
        throw RequestError("game_busy", "Multiplayer support is being installed or removed");
    }
    Game game = RequireGame(data);
    std::vector<std::string> args = Argument<std::vector<std::string>>(data, "args", {});
    if (args.size() > MaxLaunchArguments) {
        throw RequestError("invalid_argument", "Too many arguments");
    }
    for (const auto& argument : args) {
        if (argument.size() > MaxLaunchArgumentLength || argument.find('\0') != std::string::npos) {
            throw RequestError("invalid_argument", "Invalid launch argument");
        }
    }
    // Set first: the observers of the transition report which game is launching.
    std::string previous = ActiveGame();
    SetActiveGame(game.id);
    if (!m_lifecycle.TryTransition(EngineState::Ready, EngineState::LaunchingGame)) {
        SetActiveGame(previous);
        throw RequestError("game_not_ready", "The game cannot be launched right now");
    }
    try {
        game.module->Launch(args);
    } catch (const std::exception& e) {
        m_lifecycle.Set(EngineState::Ready, std::string("Failed to launch the game: ") + e.what());
        throw RequestError("launch_failed", e.what());
    }
    return json::object();
}

json Controller::Stop(const json& data) {
    Game game = RequireGame(data);
    if (!GameActive() || game.id != ActiveGame()) {
        throw RequestError("game_not_running", "The game is not running");
    }
    if (!game.module->Stop()) {
        throw RequestError("stop_failed", "Could not stop the game");
    }
    return json::object();
}

json Controller::SendToGame(const json& data) {
    Game game = RequireGame(data);
    auto message = data.find("message");
    if (message == data.end() || !message->is_object()) {
        throw RequestError("invalid_argument", "Invalid argument: message");
    }
    if (!GameActive() || game.id != ActiveGame()) {
        throw RequestError("game_not_running", "The game is not running");
    }
    if (!game.module->HasMessageChannel()) {
        throw RequestError("game_unavailable", "This game has no message channel");
    }
    if (!game.module->Send(*message)) {
        throw RequestError("game_not_connected", "The game's mod is not connected");
    }
    return json::object();
}

json Controller::Shutdown(const json& data) {
    bool force = Argument<bool>(data, "force", false);
    if (GameActive() && !force) {
        throw RequestError("game_running", "A game is running");
    }
    m_lifecycle.RequestShutdown();
    return json::object();
}

bool Controller::GameActive() const {
    return GameWasActive(m_lifecycle.State());
}

void Controller::PublishSignIn(const std::string& state, const std::string& url, const std::string& reason) {
    json data = { { "state", state } };
    if (!url.empty()) {
        data["url"] = url;
    }
    if (!reason.empty()) {
        data["reason"] = reason;
    }
    {
        std::lock_guard<std::mutex> lock(m_signInMutex);
        m_lastSignIn = data;
    }
    m_server.Publish("account.signin", data);
}

void Controller::Attached() {
    m_everAttached = true;
    {
        std::lock_guard<std::mutex> lock(m_logMutex);
        m_subscription.reset();
    }
    EngineState state = m_lifecycle.State();
    m_server.Publish("engine.state", { { "state", ToString(state) }, { "detail", m_lifecycle.Detail() } });
    m_server.Publish("account.state", AccountJson(GetAccount()));
    PublishGameStates(state, true);
    if (m_session) {
        m_server.Publish("session.state", Games::ToJson(m_session->Info()));
    }
    std::lock_guard<std::mutex> lock(m_signInMutex);
    if (m_lastSignIn.is_object()) {
        std::string signInState = m_lastSignIn.value("state", "");
        if (signInState == "waiting_for_browser" || signInState == "exchanging") {
            m_server.Publish("account.signin", m_lastSignIn);
        }
    }
}

// Without a controller nobody can re-attach: leave, unless a running game has to be served.
void Controller::Detached() {
    if (!GameActive()) {
        m_lifecycle.RequestShutdown();
    }
}

void Controller::StateChanged(EngineState previous, EngineState current, const std::string& detail) {
    m_server.Publish("engine.state", { { "state", ToString(current) }, { "detail", detail } });

    if (current == EngineState::Ready && GameWasActive(previous)) {
        m_server.Publish("game.state", { { "game", ActiveGame() }, { "state", "exited" }, { "detail", detail } });
    }
    if (current == EngineState::PreparingGame || current == EngineState::Ready || current == EngineState::LaunchingGame || current == EngineState::GameRunning) {
        PublishGameStates(current, false);
    }

    if (GameWasActive(previous) && !GameWasActive(current) && !m_server.HasClient()) {
        m_lifecycle.RequestShutdown();
    }
}

void Controller::Logged(const LogRecord& record) {
    std::lock_guard<std::mutex> lock(m_logMutex);
    m_recent.push_back(record);
    if (m_recent.size() > RecentLogs) {
        m_recent.pop_front();
    }
    if (record.level == LogLevel::Fatal) {
        m_server.Publish("fatal", { { "code", "fatal" }, { "message", record.message } });
        m_lifecycle.Set(EngineState::Fatal, record.message);
    }
    if (m_subscription && Severity(record.level) >= m_subscription->severity) {
        m_server.Publish("log", LogJson(record), true);
    }
}

}
