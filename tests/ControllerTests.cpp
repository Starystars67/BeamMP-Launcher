/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Control/Controller.h"
#include "Games/RelaySession.h"
#include "Games/SessionData.h"
#include "Logger.h"
#include "Options.h"
#include "Status.h"
#include "TestClient.h"
#include <atomic>
#include <chrono>
#include <doctest/doctest.h>
#include <memory>
#include <set>
#include <thread>

using namespace Control;
using nlohmann::json;

namespace {
const std::string Token = "0123456789abcdef0123456789abcdef";

template <typename Condition>
bool Eventually(Condition condition, int timeoutMs = 3000) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (!condition()) {
        if (std::chrono::steady_clock::now() > deadline) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return true;
}

// The relay as the session sees it: it keeps what it was asked and lets the test say what the relay says.
struct SessionLink : Relay::Link {
    std::mutex mutex;
    std::vector<std::string> calls;
    void Note(std::string call) {
        std::lock_guard<std::mutex> lock(mutex);
        calls.push_back(std::move(call));
    }
    void CreateRoom(int maxPlayers, const std::string& password) override { Note("create " + std::to_string(maxPlayers) + " [" + password + "]"); }
    void JoinRoom(const std::string& code, const std::string& password) override { Note("join " + code + " [" + password + "]"); }
    void LeaveRoom() override { Note("leave"); }
    bool SendData(Relay::DataKind kind, uint16_t target, const std::string& bytes) override {
        Note("data " + std::to_string(static_cast<int>(kind)) + " " + std::to_string(target) + " [" + bytes + "]");
        return true;
    }
    void Close() override { Note("close"); }
    bool Has(const std::string& prefix) {
        std::lock_guard<std::mutex> lock(mutex);
        for (const auto& call : calls) {
            if (call.rfind(prefix, 0) == 0) {
                return true;
            }
        }
        return false;
    }
};

struct SessionLinkView : Relay::Link {
    std::shared_ptr<SessionLink> link;
    explicit SessionLinkView(std::shared_ptr<SessionLink> link)
        : link(std::move(link)) { }
    void CreateRoom(int maxPlayers, const std::string& password) override { link->CreateRoom(maxPlayers, password); }
    void JoinRoom(const std::string& code, const std::string& password) override { link->JoinRoom(code, password); }
    void LeaveRoom() override { link->LeaveRoom(); }
    bool SendData(Relay::DataKind kind, uint16_t target, const std::string& bytes) override { return link->SendData(kind, target, bytes); }
    void Close() override { link->Close(); }
};

struct FakeGame : Games::GameModule {
    std::string id;
    std::string name;
    std::function<void(const std::vector<std::string>&)> onLaunch;
    std::function<bool()> onStop;
    std::function<bool(const json&)> onSend; // empty: the game has no message channel

    FakeGame(std::string id, std::string name)
        : id(std::move(id))
        , name(std::move(name)) { }

    std::string Id() const override { return id; }
    std::string Name() const override { return name; }
    Games::PrepareResult Prepare() override { return {}; }
    void Launch(const std::vector<std::string>& args) override { onLaunch(args); }
    bool Stop() override { return onStop(); }
    bool HasMessageChannel() const override { return static_cast<bool>(onSend); }
    bool sessions = false;
    bool SupportsSessions() const override { return sessions; }
    uint32_t steamAppId = 0;
    uint32_t SteamAppId() const override { return steamAppId; }
    Games::GameConfig config;
    bool installable = false;
    std::function<Games::InstallResult(const Games::InstallReporter&)> onInstall;
    std::function<Games::InstallResult(bool, const Games::InstallReporter&)> onUninstall;
    std::atomic<int> installCancels = 0;
    json installInfo = { { "state", "needs_install" } };
    bool SupportsInstall() const override { return installable; }
    Games::InstallResult Install(const Games::InstallReporter& progress) override { return onInstall(progress); }
    Games::InstallResult Uninstall(bool removeLoader, const Games::InstallReporter& progress) override { return onUninstall(removeLoader, progress); }
    void CancelInstall() override { ++installCancels; }
    json InstallInfo() const override { return installInfo; }
    void Configure(const Games::GameConfig& c) override { config = c; }
    Games::GameConfig Config() const override { return config; }
    bool Send(const json& message) override { return onSend(message); }
    std::function<void(const json&)> messageHandler;
    void SetMessageHandler(std::function<void(const json&)> handler) override { messageHandler = std::move(handler); }
};

struct Engine {
    Lifecycle lifecycle;
    Server server { Token };
    FakeGame beamng { "beamng", "BeamNG.drive" };
    FakeGame gearblocks { "gearblocks", "GearBlocks" };
    std::unique_ptr<Controller> controller;
    std::atomic<int> launches = 0;
    std::atomic<int> stops = 0;
    std::atomic<int> signOuts = 0;
    std::atomic<int> signIns = 0;
    std::atomic<int> cancels = 0;
    std::atomic<SignInStart> signInResult = SignInStart::Started;
    std::atomic<bool> launchThrows = false;
    std::atomic<bool> stopWorks = true;
    std::mutex argsMutex;
    std::vector<std::string> lastArgs;
    std::atomic<int> gbLaunches = 0;
    std::atomic<int> gbStops = 0;
    std::atomic<int> gbSends = 0;
    std::atomic<bool> gbSendWorks = true;
    std::atomic<bool> gbLaunchThrows = false;
    std::mutex configureMutex;
    std::vector<std::pair<std::string, Games::GameConfig>> configured;
    bool configureKnowsGame = true;
    std::mutex gbMutex;
    json gbLastMessage;
    uint16_t port = 0;

    std::shared_ptr<SessionLink> sessionLink = std::make_shared<SessionLink>();
    std::mutex relayMutex;
    Relay::EventHandler relayHandler;
    std::unique_ptr<Games::RelaySession> relaySession;
    std::string relayHost = "127.0.0.1";

    explicit Engine(bool withGearBlocks = false, bool withSession = false, bool withInstall = false) {
        options = Options();
        options.headless = true;
        InitLog();
        SetAccount({});
        ping = -1;
        GamePID = 0;
        UlStatus = "";
        MStatus = "";

        beamng.onLaunch = [this](const std::vector<std::string>& args) {
            if (launchThrows) {
                throw std::runtime_error("no such file");
            }
            ++launches;
            std::lock_guard<std::mutex> lock(argsMutex);
            lastArgs = args;
        };
        beamng.onStop = [this] {
            ++stops;
            return stopWorks.load();
        };
        gearblocks.onLaunch = [this](const std::vector<std::string>&) {
            if (gbLaunchThrows) {
                throw std::runtime_error("the plugin is not installed");
            }
            ++gbLaunches;
        };
        gearblocks.onStop = [this] {
            ++gbStops;
            return true;
        };
        gearblocks.onSend = [this](const json& message) {
            ++gbSends;
            std::lock_guard<std::mutex> lock(gbMutex);
            gbLastMessage = message;
            return gbSendWorks.load();
        };
        AccountOps account;
        account.signIn = [this] {
            ++signIns;
            return signInResult.load();
        };
        account.cancelSignIn = [this] { ++cancels; };
        account.signOut = [this] {
            ++signOuts;
            SetAccount({});
        };
        gearblocks.sessions = true;
        gearblocks.installable = withInstall;
        controller = std::make_unique<Controller>(server, lifecycle, account, "9.9.9");
        controller->RegisterGame(beamng);
        controller->RegisterGame(gearblocks);
        if (withSession) {
            Games::RelaySession::Options options;
            options.relay = { relayHost, 7777 };
            options.playerName = [] { return std::string("Ann"); };
            relaySession = std::make_unique<Games::RelaySession>(options, [this](const Relay::Endpoint&, const Relay::Hello&, Relay::EventHandler handler, std::string&) -> std::unique_ptr<Relay::Link> {
                {
                    std::lock_guard<std::mutex> lock(relayMutex);
                    relayHandler = handler;
                }
                return std::make_unique<SessionLinkView>(sessionLink);
            });
            controller->SetSession(*relaySession);
        }
        if (!withGearBlocks) {
            // listed, but nothing to launch
            controller->SetGameStatus("gearblocks", Games::GameStatus::Unavailable);
        }
        controller->SetConfigureHandler([this](const std::string& game, const Games::GameConfig& config) {
            std::lock_guard<std::mutex> lock(configureMutex);
            configured.emplace_back(game, config);
            return configureKnowsGame;
        });
        port = server.Start(0);
    }

    ~Engine() {
        server.Stop();
        controller.reset();
    }

    void BecomeReady() {
        lifecycle.Set(EngineState::PreparingGame);
        lifecycle.Set(EngineState::Ready);
    }

    void RunGame(unsigned long pid) {
        lifecycle.Set(EngineState::LaunchingGame);
        GamePID = pid;
        lifecycle.Set(EngineState::GameRunning);
    }
};

struct GearEngine : Engine {
    GearEngine()
        : Engine(true) { }
};

struct InstallEngine : Engine {
    InstallEngine()
        : Engine(true, false, true) { }
};

struct SessionEngine : Engine {
    SessionEngine()
        : Engine(true, true) { }

    // the relay says something
    void Relay(Relay::Event::Kind kind, json body = json::object()) {
        REQUIRE(Eventually([&] {
            std::lock_guard<std::mutex> lock(relayMutex);
            return relayHandler != nullptr;
        }));
        Relay::EventHandler handler;
        {
            std::lock_guard<std::mutex> lock(relayMutex);
            handler = relayHandler;
        }
        Relay::Event event;
        event.kind = kind;
        event.json = std::move(body);
        handler(event);
    }
};

std::string ErrorCode(const json& response) {
    return response["ok"] == true ? "" : response["error"]["code"].get<std::string>();
}
}

TEST_CASE_FIXTURE(Engine, "the handshake advertises what the engine can do") {
    TestClient client(port);
    json hello = client.Hello(Token);
    CHECK(hello["data"]["engine"] == "9.9.9");
    CHECK(hello["data"]["capabilities"] == json::array({ "logs", "games", "account", "signin", "game_messages" }));
    CHECK(hello["data"]["games"] == json::array({ "beamng", "gearblocks" }));
}

TEST_CASE_FIXTURE(Engine, "attaching sends the current state right away") {
    BecomeReady();
    TestClient client(port);
    client.Hello(Token);

    auto state = client.WaitForEvent("engine.state");
    REQUIRE(state);
    CHECK((*state)["data"]["state"] == "ready");
    auto account = client.WaitForEvent("account.state");
    REQUIRE(account);
    CHECK((*account)["data"]["signed_in"] == false);
    auto game = client.WaitForEvent("game.state");
    REQUIRE(game);
    CHECK((*game)["data"]["game"] == "beamng");
    CHECK((*game)["data"]["state"] == "ready");
    CHECK(controller->EverAttached());
}

TEST_CASE_FIXTURE(Engine, "engine.state reports the state and the status snapshot") {
    BecomeReady();
    ping = 42;
    UlStatus = "UlLoading Resource a.zip";
    MStatus = "mods";
    SetAccount({ true, "user", "EA", 9 });
    TestClient client(port);
    client.Hello(Token);

    json data = client.Call("engine.state")["data"];
    CHECK(data["state"] == "ready");
    CHECK(data["status"]["ping"] == 42);
    CHECK(data["status"]["upload_status"] == "UlLoading Resource a.zip");
    CHECK(data["status"]["mod_status"] == "mods");
    CHECK(data["status"]["game_pid"] == 0);
    CHECK(data["status"]["account"] == json { { "signed_in", true }, { "username", "user" }, { "role", "EA" }, { "id", 9 } });
}

TEST_CASE_FIXTURE(Engine, "games.list describes the built-in game") {
    BecomeReady();
    TestClient client(port);
    client.Hello(Token);
    json games = client.Call("games.list")["data"]["games"];
    REQUIRE(games.size() == 2);
    CHECK(games[0]["id"] == "beamng");
    CHECK(games[0]["name"] == "BeamNG.drive");
    CHECK(games[0]["state"] == "ready");
    CHECK_FALSE(games[0].contains("pid"));

    RunGame(4242);
    games = client.Call("games.list")["data"]["games"];
    CHECK(games[0]["state"] == "running");
    CHECK(games[0]["pid"] == 4242);
}

TEST_CASE_FIXTURE(Engine, "GearBlocks is listed as unavailable and cannot be launched or stopped") {
    BecomeReady();
    TestClient client(port);
    client.Hello(Token);
    json games = client.Call("games.list")["data"]["games"];
    CHECK(games[1] == json { { "id", "gearblocks" }, { "name", "GearBlocks" }, { "state", "unavailable" } });

    CHECK(ErrorCode(client.Call("game.launch", { { "game", "gearblocks" } })) == "game_unavailable");
    CHECK(ErrorCode(client.Call("game.stop", { { "game", "gearblocks" } })) == "game_unavailable");
    CHECK(launches == 0);
    CHECK(stops == 0);
    CHECK(lifecycle.State() == EngineState::Ready);
    CHECK(ErrorCode(client.Call("game.launch", { { "game", "other" } })) == "unknown_game");
}

TEST_CASE_FIXTURE(Engine, "the game cannot be launched until the engine is ready") {
    TestClient client(port);
    client.Hello(Token);
    CHECK(ErrorCode(client.Call("game.launch", { { "game", "beamng" } })) == "game_not_ready");
    CHECK(launches == 0);
    CHECK(lifecycle.State() == EngineState::Starting);
}

TEST_CASE_FIXTURE(Engine, "launching moves to LaunchingGame and hands the arguments on") {
    BecomeReady();
    TestClient client(port);
    client.Hello(Token);

    json response = client.Call("game.launch", { { "game", "beamng" }, { "args", { "-console", "-x" } } });
    CHECK(response["ok"] == true);
    CHECK(launches == 1);
    CHECK(lifecycle.State() == EngineState::LaunchingGame);
    std::lock_guard<std::mutex> lock(argsMutex);
    CHECK(lastArgs == std::vector<std::string> { "-console", "-x" });
}

TEST_CASE_FIXTURE(Engine, "a second launch while one is in progress is refused") {
    BecomeReady();
    TestClient client(port);
    client.Hello(Token);
    CHECK(client.Call("game.launch", { { "game", "beamng" } })["ok"] == true);
    CHECK(ErrorCode(client.Call("game.launch", { { "game", "beamng" } })) == "game_not_ready");
    RunGame(10);
    CHECK(ErrorCode(client.Call("game.launch", { { "game", "beamng" } })) == "game_not_ready");
    CHECK(launches == 1);
}

TEST_CASE_FIXTURE(Engine, "a launch that fails to start returns the engine to ready with the reason") {
    BecomeReady();
    launchThrows = true;
    TestClient client(port);
    client.Hello(Token);
    json response = client.Call("game.launch", { { "game", "beamng" } });
    CHECK(ErrorCode(response) == "launch_failed");
    CHECK(lifecycle.State() == EngineState::Ready);
    CHECK(lifecycle.Detail().find("no such file") != std::string::npos);

    launchThrows = false;
    CHECK(client.Call("game.launch", { { "game", "beamng" } })["ok"] == true);
}

TEST_CASE_FIXTURE(Engine, "launch arguments are validated") {
    BecomeReady();
    TestClient client(port);
    client.Hello(Token);
    CHECK(ErrorCode(client.Call("game.launch", json::object())) == "unknown_game");
    CHECK(ErrorCode(client.Call("game.launch", { { "game", "other" } })) == "unknown_game");
    CHECK(ErrorCode(client.Call("game.launch", { { "game", 5 } })) == "invalid_argument");
    CHECK(ErrorCode(client.Call("game.launch", { { "game", "beamng" }, { "args", "-console" } })) == "invalid_argument");
    CHECK(ErrorCode(client.Call("game.launch", { { "game", "beamng" }, { "args", { 1, 2 } } })) == "invalid_argument");
    CHECK(ErrorCode(client.Call("game.launch", { { "game", "beamng" }, { "args", json(std::vector<std::string>(65, "a")) } })) == "invalid_argument");
    CHECK(ErrorCode(client.Call("game.launch", { { "game", "beamng" }, { "args", { std::string(5000, 'a') } } })) == "invalid_argument");
    CHECK(ErrorCode(client.Call("game.launch", { { "game", "beamng" }, { "args", { std::string("a\0b", 3) } } })) == "invalid_argument");
    CHECK(launches == 0);
    CHECK(lifecycle.State() == EngineState::Ready);
}

TEST_CASE_FIXTURE(Engine, "game.stop needs a running game") {
    BecomeReady();
    TestClient client(port);
    client.Hello(Token);
    CHECK(ErrorCode(client.Call("game.stop", { { "game", "beamng" } })) == "game_not_running");
    CHECK(stops == 0);

    RunGame(77);
    CHECK(client.Call("game.stop", { { "game", "beamng" } })["ok"] == true);
    CHECK(stops == 1);

    stopWorks = false;
    CHECK(ErrorCode(client.Call("game.stop", { { "game", "beamng" } })) == "stop_failed");
    CHECK(ErrorCode(client.Call("game.stop", { { "game", "nope" } })) == "unknown_game");
}

TEST_CASE_FIXTURE(Engine, "state changes are published as engine and game events") {
    TestClient client(port);
    client.Hello(Token);
    client.WaitForEvent("game.state"); // end of the attach burst
    client.TakePending();

    lifecycle.Set(EngineState::PreparingGame, "checking the game");
    auto engineState = client.WaitForEvent("engine.state");
    REQUIRE(engineState);
    CHECK((*engineState)["data"] == json { { "state", "preparing_game" }, { "detail", "checking the game" } });
    auto gameState = client.WaitForEvent("game.state");
    REQUIRE(gameState);
    CHECK((*gameState)["data"]["state"] == "preparing");

    lifecycle.Set(EngineState::Ready);
    lifecycle.Set(EngineState::LaunchingGame);
    GamePID = 555;
    lifecycle.Set(EngineState::GameRunning);
    lifecycle.Set(EngineState::Ready, "The game exited with code 0");

    std::vector<std::string> states;
    std::vector<json> games;
    for (int i = 0; i < 4; ++i) {
        auto event = client.WaitForEvent("game.state");
        REQUIRE(event);
        games.push_back((*event)["data"]);
        states.push_back((*event)["data"]["state"]);
    }
    // ready, launching, running, then exited followed by ready
    CHECK(states == std::vector<std::string> { "ready", "launching", "running", "exited" });
    CHECK(games[2]["pid"] == 555);
    CHECK(games[3]["detail"] == "The game exited with code 0");
    auto ready = client.WaitForEvent("game.state");
    REQUIRE(ready);
    CHECK((*ready)["data"]["state"] == "ready");
}

TEST_CASE_FIXTURE(Engine, "shutdown is refused while a game runs unless forced") {
    BecomeReady();
    RunGame(1);
    TestClient client(port);
    client.Hello(Token);

    CHECK(ErrorCode(client.Call("engine.shutdown")) == "game_running");
    CHECK_FALSE(lifecycle.ShutdownRequested());
    CHECK(ErrorCode(client.Call("engine.shutdown", { { "force", "yes" } })) == "invalid_argument");

    CHECK(client.Call("engine.shutdown", { { "force", true } })["ok"] == true);
    CHECK(lifecycle.ShutdownRequested());
}

TEST_CASE_FIXTURE(Engine, "shutdown is accepted when idle") {
    BecomeReady();
    TestClient client(port);
    client.Hello(Token);
    CHECK(client.Call("engine.shutdown")["ok"] == true);
    CHECK(lifecycle.ShutdownRequested());
}

TEST_CASE_FIXTURE(Engine, "losing the controller while idle ends the engine") {
    BecomeReady();
    {
        TestClient client(port);
        client.Hello(Token);
        CHECK_FALSE(lifecycle.ShutdownRequested());
    }
    CHECK(Eventually([&] { return lifecycle.ShutdownRequested(); }));
}

TEST_CASE_FIXTURE(Engine, "losing the controller during a game keeps serving it until the game ends") {
    BecomeReady();
    RunGame(1234);
    {
        TestClient client(port);
        client.Hello(Token);
    }
    CHECK(Eventually([&] { return !server.HasClient(); }));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK_FALSE(lifecycle.ShutdownRequested());

    lifecycle.Set(EngineState::Ready, "The game exited with code 0");
    CHECK(lifecycle.ShutdownRequested());
}

TEST_CASE_FIXTURE(Engine, "a game that ends while a controller is attached does not end the engine") {
    BecomeReady();
    RunGame(1234);
    TestClient client(port);
    client.Hello(Token);
    CHECK(Eventually([&] { return server.HasClient(); }));
    lifecycle.Set(EngineState::Ready);
    CHECK_FALSE(lifecycle.ShutdownRequested());
}

TEST_CASE_FIXTURE(Engine, "account changes are published and sign out goes through the engine") {
    TestClient client(port);
    client.Hello(Token);
    client.WaitForEvent("game.state");
    client.TakePending();

    SetAccount({ true, "user", "", -1 });
    auto event = client.WaitForEvent("account.state");
    REQUIRE(event);
    CHECK((*event)["data"] == json { { "signed_in", true }, { "username", "user" } });
    CHECK(client.Call("account.state")["data"]["signed_in"] == true);

    json response = client.Call("account.sign_out");
    CHECK(response["data"]["signed_in"] == false);
    CHECK(signOuts == 1);
    auto after = client.WaitForEvent("account.state");
    REQUIRE(after);
    CHECK((*after)["data"]["signed_in"] == false);
}

TEST_CASE_FIXTURE(Engine, "log.subscribe replays the backlog at the requested level, oldest first") {
    options.verbose = true;
    debug("d1");
    info("i1");
    warn("w1");
    error("e1");
    TestClient client(port);
    client.Hello(Token);

    json response = client.Call("log.subscribe", { { "level", "warn" }, { "backlog", 10 } });
    CHECK(response["data"]["sent"] == 2);
    std::vector<std::string> messages;
    for (const auto& message : client.TakePending()) {
        if (message["name"] == "log") {
            messages.push_back(message["data"]["message"]);
            CHECK(message["data"]["ts"].get<std::string>().size() == 24);
        }
    }
    CHECK(messages == std::vector<std::string> { "w1", "e1" });
}

TEST_CASE_FIXTURE(Engine, "the backlog is limited to the requested count") {
    for (int i = 0; i < 10; ++i) {
        info("line " + std::to_string(i));
    }
    TestClient client(port);
    client.Hello(Token);
    client.Call("log.subscribe", { { "level", "info" }, { "backlog", 3 } });
    std::vector<std::string> messages;
    for (const auto& message : client.TakePending()) {
        if (message["name"] == "log") {
            messages.push_back(message["data"]["message"]);
        }
    }
    CHECK(messages == std::vector<std::string> { "line 7", "line 8", "line 9" });
}

TEST_CASE_FIXTURE(Engine, "live records follow the subscription level, and unsubscribing stops them") {
    TestClient client(port);
    client.Hello(Token);
    info("before subscribing");
    client.Call("log.subscribe", { { "level", "warn" }, { "backlog", 0 } });
    client.TakePending();

    info("quiet");
    warn("loud");
    auto event = client.WaitForEvent("log");
    REQUIRE(event);
    CHECK((*event)["data"]["message"] == "loud");
    CHECK((*event)["data"]["level"] == "warn");

    client.Call("log.unsubscribe");
    client.TakePending();
    error("unheard");
    CHECK_FALSE(client.WaitForEvent("log", 300).has_value());
}

TEST_CASE_FIXTURE(Engine, "log.subscribe validates its arguments") {
    TestClient client(port);
    client.Hello(Token);
    CHECK(ErrorCode(client.Call("log.subscribe", { { "level", "loud" } })) == "invalid_argument");
    CHECK(ErrorCode(client.Call("log.subscribe", { { "level", 3 } })) == "invalid_argument");
    CHECK(ErrorCode(client.Call("log.subscribe", { { "backlog", -1 } })) == "invalid_argument");
    CHECK(ErrorCode(client.Call("log.subscribe", { { "backlog", 100000 } })) == "invalid_argument");
    CHECK(ErrorCode(client.Call("log.subscribe", { { "backlog", "all" } })) == "invalid_argument");
    CHECK(client.Call("log.subscribe", json::object())["ok"] == true);
}

TEST_CASE_FIXTURE(Engine, "a new controller starts without a log subscription") {
    {
        TestClient first(port);
        first.Hello(Token);
        first.Call("log.subscribe", { { "level", "debug" }, { "backlog", 0 } });
    }
    CHECK(Eventually([&] { return !server.HasClient(); }));

    TestClient second(port);
    second.Hello(Token);
    second.TakePending();
    info("not subscribed");
    CHECK_FALSE(second.WaitForEvent("log", 300).has_value());
}

TEST_CASE_FIXTURE(Engine, "account.sign_in starts a sign-in") {
    TestClient client(port);
    client.Hello(Token);
    CHECK(client.Call("account.sign_in")["ok"] == true);
    CHECK(signIns == 1);
}

TEST_CASE_FIXTURE(Engine, "account.sign_in explains why it cannot start") {
    TestClient client(port);
    client.Hello(Token);
    signInResult = SignInStart::Busy;
    CHECK(ErrorCode(client.Call("account.sign_in")) == "sign_in_busy");
    signInResult = SignInStart::AlreadySignedIn;
    CHECK(ErrorCode(client.Call("account.sign_in")) == "already_signed_in");
    signInResult = SignInStart::Unavailable;
    CHECK(ErrorCode(client.Call("account.sign_in")) == "sign_in_unavailable");
}

TEST_CASE_FIXTURE(Engine, "account.cancel_sign_in reaches the account side") {
    TestClient client(port);
    client.Hello(Token);
    CHECK(client.Call("account.cancel_sign_in")["ok"] == true);
    CHECK(cancels == 1);
}

TEST_CASE_FIXTURE(Engine, "sign-in progress is published to the controller") {
    TestClient client(port);
    client.Hello(Token);
    client.WaitForEvent("game.state");
    client.TakePending();

    controller->PublishSignIn("waiting_for_browser", "https://accounts.test/auth/external?x=1", "");
    auto waiting = client.WaitForEvent("account.signin");
    REQUIRE(waiting);
    CHECK((*waiting)["data"] == json { { "state", "waiting_for_browser" }, { "url", "https://accounts.test/auth/external?x=1" } });

    controller->PublishSignIn("failed", "", "denied");
    auto failed = client.WaitForEvent("account.signin");
    REQUIRE(failed);
    CHECK((*failed)["data"] == json { { "state", "failed" }, { "reason", "denied" } });
}

TEST_CASE_FIXTURE(Engine, "a controller that attaches mid sign-in is told where it stands") {
    controller->PublishSignIn("waiting_for_browser", "https://accounts.test/x", "");
    TestClient client(port);
    client.Hello(Token);
    auto event = client.WaitForEvent("account.signin");
    REQUIRE(event);
    CHECK((*event)["data"]["state"] == "waiting_for_browser");
    CHECK((*event)["data"]["url"] == "https://accounts.test/x");
}

TEST_CASE_FIXTURE(Engine, "a finished or failed sign-in is not replayed to a new controller") {
    controller->PublishSignIn("failed", "", "timeout");
    TestClient client(port);
    client.Hello(Token);
    client.WaitForEvent("game.state");
    CHECK_FALSE(client.WaitForEvent("account.signin", 300).has_value());
}

TEST_CASE_FIXTURE(GearEngine, "a registered game is advertised and listed as launchable") {
    BecomeReady();
    TestClient client(port);
    json hello = client.Hello(Token);
    CHECK(hello["data"]["games"] == json::array({ "beamng", "gearblocks" }));
    json games = client.Call("games.list")["data"]["games"];
    REQUIRE(games.size() == 2);
    CHECK(games[1] == json { { "id", "gearblocks" }, { "name", "GearBlocks" }, { "state", "ready" } });
}

TEST_CASE_FIXTURE(GearEngine, "launching a registered game goes to that game and reports it as the busy one") {
    BecomeReady();
    TestClient client(port);
    client.Hello(Token);
    client.WaitForEvent("game.state"); // one per launchable game on attach
    client.WaitForEvent("game.state");
    client.TakePending();

    CHECK(client.Call("game.launch", { { "game", "gearblocks" }, { "args", { "-x" } } })["ok"] == true);
    CHECK(gbLaunches == 1);
    CHECK(launches == 0);

    GamePID = 777;
    lifecycle.Set(EngineState::GameRunning);

    json games = client.Call("games.list")["data"]["games"];
    CHECK(games[0] == json { { "id", "beamng" }, { "name", "BeamNG.drive" }, { "state", "busy" } });
    CHECK(games[1] == json { { "id", "gearblocks" }, { "name", "GearBlocks" }, { "state", "running" }, { "pid", 777 } });

    // events: the launching and running states of each game, then the exit of the one that ran
    std::map<std::string, std::vector<std::string>> seen;
    for (int i = 0; i < 4; ++i) {
        auto event = client.WaitForEvent("game.state");
        REQUIRE(event);
        seen[(*event)["data"]["game"]].push_back((*event)["data"]["state"]);
    }
    CHECK(seen["gearblocks"] == std::vector<std::string> { "launching", "running" });
    CHECK(seen["beamng"] == std::vector<std::string> { "busy", "busy" });

    lifecycle.Set(EngineState::Ready, "The game exited with code 0");
    auto exited = client.WaitForEvent("game.state");
    REQUIRE(exited);
    CHECK((*exited)["data"] == json { { "game", "gearblocks" }, { "state", "exited" }, { "detail", "The game exited with code 0" } });
}

TEST_CASE_FIXTURE(GearEngine, "only the game that runs can be stopped") {
    BecomeReady();
    TestClient client(port);
    client.Hello(Token);
    CHECK(client.Call("game.launch", { { "game", "gearblocks" } })["ok"] == true);
    lifecycle.Set(EngineState::GameRunning);

    CHECK(ErrorCode(client.Call("game.stop", { { "game", "beamng" } })) == "game_not_running");
    CHECK(stops == 0);
    CHECK(client.Call("game.stop", { { "game", "gearblocks" } })["ok"] == true);
    CHECK(gbStops == 1);
}

TEST_CASE_FIXTURE(GearEngine, "a refused launch does not change which game is active") {
    BecomeReady();
    TestClient client(port);
    client.Hello(Token);
    RunGame(1); // beamng, through the engine's own path
    CHECK(ErrorCode(client.Call("game.launch", { { "game", "gearblocks" } })) == "game_not_ready");
    CHECK(gbLaunches == 0);
    json games = client.Call("games.list")["data"]["games"];
    CHECK(games[0]["state"] == "running");
    CHECK(games[1]["state"] == "busy");
}

TEST_CASE_FIXTURE(GearEngine, "a launch that throws leaves the engine ready and the other game launchable") {
    BecomeReady();
    TestClient client(port);
    client.Hello(Token);
    gbLaunchThrows = true;

    json response = client.Call("game.launch", { { "game", "gearblocks" } });
    CHECK(ErrorCode(response) == "launch_failed");
    CHECK(response["error"]["message"] == "the plugin is not installed");
    CHECK(lifecycle.State() == EngineState::Ready);
    CHECK(lifecycle.Detail() == "Failed to launch the game: the plugin is not installed");
    json games = client.Call("games.list")["data"]["games"];
    CHECK(games[0]["state"] == "ready");
    CHECK(games[1]["state"] == "ready");

    CHECK(client.Call("game.launch", { { "game", "beamng" } })["ok"] == true);
    CHECK(launches == 1);
    lifecycle.Set(EngineState::GameRunning);
    games = client.Call("games.list")["data"]["games"];
    CHECK(games[0]["state"] == "running");
    CHECK(games[1]["state"] == "busy");
}

TEST_CASE_FIXTURE(GearEngine, "game.send hands a message to the running game") {
    BecomeReady();
    TestClient client(port);
    client.Hello(Token);

    CHECK(ErrorCode(client.Call("game.send", { { "game", "gearblocks" }, { "message", { { "type", "x" } } } })) == "game_not_running");
    CHECK(gbSends == 0);

    CHECK(client.Call("game.launch", { { "game", "gearblocks" } })["ok"] == true);
    lifecycle.Set(EngineState::GameRunning);

    CHECK(client.Call("game.send", { { "game", "gearblocks" }, { "message", { { "type", "welcome" }, { "n", 1 } } } })["ok"] == true);
    CHECK(gbSends == 1);
    {
        std::lock_guard<std::mutex> lock(gbMutex);
        CHECK(gbLastMessage == json { { "type", "welcome" }, { "n", 1 } });
    }

    gbSendWorks = false;
    CHECK(ErrorCode(client.Call("game.send", { { "game", "gearblocks" }, { "message", { { "type", "x" } } } })) == "game_not_connected");

    CHECK(ErrorCode(client.Call("game.send", { { "game", "gearblocks" } })) == "invalid_argument");
    CHECK(ErrorCode(client.Call("game.send", { { "game", "gearblocks" }, { "message", "text" } })) == "invalid_argument");
    CHECK(ErrorCode(client.Call("game.send", { { "game", "gearblocks" }, { "message", json::array() } })) == "invalid_argument");
    CHECK(ErrorCode(client.Call("game.send", { { "game", "other" }, { "message", json::object() } })) == "unknown_game");
    CHECK(ErrorCode(client.Call("game.send", { { "game", "beamng" }, { "message", json::object() } })) == "game_not_running");
}

TEST_CASE_FIXTURE(Engine, "game.send needs a game with an adapter, and a game that has no message channel says so") {
    BecomeReady();
    TestClient client(port);
    client.Hello(Token);
    CHECK(ErrorCode(client.Call("game.send", { { "game", "gearblocks" }, { "message", json::object() } })) == "game_unavailable");

    // beamng runs through its own mod and has no message channel
    CHECK(client.Call("game.launch", { { "game", "beamng" } })["ok"] == true);
    lifecycle.Set(EngineState::GameRunning);
    CHECK(ErrorCode(client.Call("game.send", { { "game", "beamng" }, { "message", json::object() } })) == "game_unavailable");
}

TEST_CASE_FIXTURE(GearEngine, "messages from the game are published to the controller") {
    BecomeReady();
    TestClient client(port);
    client.Hello(Token);
    client.WaitForEvent("game.state");
    client.TakePending();

    controller->PublishGameMessage("gearblocks", { { "type", "hello" }, { "mod", "beammp" } });
    auto event = client.WaitForEvent("game.message");
    REQUIRE(event);
    CHECK((*event)["data"] == json { { "game", "gearblocks" }, { "message", { { "type", "hello" }, { "mod", "beammp" } } } });
}

TEST_CASE_FIXTURE(GearEngine, "a game that is not installed is listed as not found and cannot be used") {
    BecomeReady();
    TestClient client(port);
    client.Hello(Token);
    controller->SetGameStatus("gearblocks", Games::GameStatus::NotFound, "GearBlocks.exe was not found");

    json games = client.Call("games.list")["data"]["games"];
    CHECK(games[1] == json { { "id", "gearblocks" }, { "name", "GearBlocks" }, { "state", "not_found" }, { "detail", "GearBlocks.exe was not found" } });

    json launch = client.Call("game.launch", { { "game", "gearblocks" } });
    CHECK(ErrorCode(launch) == "game_not_found");
    CHECK(launch["error"]["message"] == "GearBlocks.exe was not found");
    CHECK(ErrorCode(client.Call("game.stop", { { "game", "gearblocks" } })) == "game_not_found");
    CHECK(ErrorCode(client.Call("game.send", { { "game", "gearblocks" }, { "message", json::object() } })) == "game_not_found");
    CHECK(gbLaunches == 0);
    CHECK(lifecycle.State() == EngineState::Ready);

    // the other game is not affected
    CHECK(client.Call("game.launch", { { "game", "beamng" } })["ok"] == true);
    CHECK(launches == 1);
}

TEST_CASE_FIXTURE(GearEngine, "a game that failed to prepare is reported as an error with the reason") {
    BecomeReady();
    TestClient client(port);
    client.Hello(Token);
    controller->SetGameStatus("gearblocks", Games::GameStatus::Failed, "disk full");

    json games = client.Call("games.list")["data"]["games"];
    CHECK(games[1] == json { { "id", "gearblocks" }, { "name", "GearBlocks" }, { "state", "error" }, { "detail", "disk full" } });
    json launch = client.Call("game.launch", { { "game", "gearblocks" } });
    CHECK(ErrorCode(launch) == "game_unavailable");
    CHECK(launch["error"]["message"] == "The game could not be prepared: disk full");
    CHECK(gbLaunches == 0);
}

TEST_CASE_FIXTURE(GearEngine, "a change of a game's status is published, and a game that is back is launchable") {
    BecomeReady();
    TestClient client(port);
    client.Hello(Token);
    client.WaitForEvent("game.state");
    client.WaitForEvent("game.state");
    client.TakePending();

    controller->SetGameStatus("gearblocks", Games::GameStatus::NotFound, "gone");
    auto event = client.WaitForEvent("game.state");
    REQUIRE(event);
    CHECK((*event)["data"] == json { { "game", "gearblocks" }, { "state", "not_found" }, { "detail", "gone" } });

    controller->SetGameStatus("gearblocks", Games::GameStatus::Available);
    event = client.WaitForEvent("game.state");
    REQUIRE(event);
    CHECK((*event)["data"] == json { { "game", "gearblocks" }, { "state", "ready" } });
    CHECK(client.Call("game.launch", { { "game", "gearblocks" } })["ok"] == true);
    CHECK(gbLaunches == 1);
}

TEST_CASE_FIXTURE(GearEngine, "engine stages are only reported for games that can be used") {
    TestClient client(port);
    client.Hello(Token);
    controller->SetGameStatus("gearblocks", Games::GameStatus::NotFound, "gone");
    client.WaitForEvent("game.state");
    client.WaitForEvent("game.state");
    client.WaitForEvent("game.state");
    client.TakePending();

    lifecycle.Set(EngineState::PreparingGame);
    lifecycle.Set(EngineState::Ready);
    std::set<std::string> games;
    while (auto event = client.WaitForEvent("game.state", 300)) {
        games.insert((*event)["data"]["game"]);
    }
    CHECK(games == std::set<std::string> { "beamng" });
}

TEST_CASE_FIXTURE(Engine, "the same game cannot be registered twice, and a status for an unknown game is ignored") {
    FakeGame again { "beamng", "Again" };
    CHECK_THROWS_AS(controller->RegisterGame(again), std::invalid_argument);
    CHECK_NOTHROW(controller->SetGameStatus("nothing", Games::GameStatus::NotFound));
    BecomeReady();
    TestClient client(port);
    client.Hello(Token);
    CHECK(client.Call("games.list")["data"]["games"].size() == 2);
}

TEST_CASE_FIXTURE(GearEngine, "game.configure hands the settings to the engine's side") {
    BecomeReady();
    TestClient client(port);
    client.Hello(Token);

    json response = client.Call("game.configure", { { "game", "gearblocks" }, { "directory", "/games/GearBlocks" }, { "command", "/opt/start.sh" }, { "user_path", "/users/x" } });
    CHECK(response["ok"] == true);
    std::lock_guard<std::mutex> lock(configureMutex);
    REQUIRE(configured.size() == 1);
    CHECK(configured[0].first == "gearblocks");
    CHECK(configured[0].second.directory == "/games/GearBlocks");
    CHECK(configured[0].second.command == "/opt/start.sh");
    CHECK(configured[0].second.userPath == "/users/x");
}

TEST_CASE_FIXTURE(GearEngine, "settings that are left out are cleared, so the UI always sends the whole picture") {
    BecomeReady();
    TestClient client(port);
    client.Hello(Token);
    CHECK(client.Call("game.configure", { { "game", "beamng" }, { "directory", "/x" } })["ok"] == true);
    CHECK(client.Call("game.configure", { { "game", "beamng" } })["ok"] == true);
    std::lock_guard<std::mutex> lock(configureMutex);
    REQUIRE(configured.size() == 2);
    CHECK(configured[1].second.empty());
}

TEST_CASE_FIXTURE(GearEngine, "game.configure validates its arguments and the engine state") {
    TestClient client(port);
    client.Hello(Token);
    CHECK(ErrorCode(client.Call("game.configure", { { "game", "beamng" }, { "directory", "/x" } })) == "game_not_ready"); // still starting
    BecomeReady();
    CHECK(ErrorCode(client.Call("game.configure", { { "game", "nothing" } })) == "unknown_game");
    CHECK(ErrorCode(client.Call("game.configure", { { "game", "beamng" }, { "directory", 5 } })) == "invalid_argument");
    CHECK(ErrorCode(client.Call("game.configure", { { "game", "beamng" }, { "command", std::string(5000, 'a') } })) == "invalid_argument");
    CHECK(ErrorCode(client.Call("game.configure", { { "game", "beamng" }, { "user_path", std::string("a\0b", 3) } })) == "invalid_argument");
    configureKnowsGame = false;
    CHECK(ErrorCode(client.Call("game.configure", { { "game", "beamng" } })) == "unknown_game");
    configureKnowsGame = true;
    {
        std::lock_guard<std::mutex> lock(configureMutex);
        CHECK(configured.size() == 1); // only the one that got through to the handler (unknown by the handler)
    }

    // not while a game starts or runs
    CHECK(client.Call("game.launch", { { "game", "beamng" } })["ok"] == true);
    CHECK(ErrorCode(client.Call("game.configure", { { "game", "gearblocks" }, { "directory", "/x" } })) == "game_not_ready");
}

TEST_CASE_FIXTURE(GearEngine, "a game that is being prepared again is listed as preparing and cannot be launched") {
    BecomeReady();
    TestClient client(port);
    client.Hello(Token);
    controller->SetGameStatus("gearblocks", Games::GameStatus::Preparing);
    json games = client.Call("games.list")["data"]["games"];
    CHECK(games[1]["state"] == "preparing");
    CHECK(ErrorCode(client.Call("game.launch", { { "game", "gearblocks" } })) == "game_not_ready");
    CHECK(ErrorCode(client.Call("game.configure", { { "game", "gearblocks" } })) == "game_not_ready");
    CHECK(gbLaunches == 0);
}

TEST_CASE_FIXTURE(GearEngine, "games.list shows what the user set for a game") {
    BecomeReady();
    TestClient client(port);
    client.Hello(Token);
    gearblocks.config.directory = "/games/GearBlocks";
    gearblocks.config.command = "/opt/start.sh";
    json games = client.Call("games.list")["data"]["games"];
    CHECK(games[0].contains("config") == false);
    CHECK(games[1]["config"] == json { { "directory", "/games/GearBlocks" }, { "command", "/opt/start.sh" } });
}

TEST_CASE_FIXTURE(GearEngine, "games.list tells the Steam app id, so a UI can show the cover from the user's own library") {
    BecomeReady();
    TestClient client(port);
    client.Hello(Token);
    beamng.steamAppId = 284160;
    json games = client.Call("games.list")["data"]["games"];
    CHECK(games[0]["steam_app_id"] == 284160);
    CHECK_FALSE(games[1].contains("steam_app_id")); // none known

    // not repeated in the state events
    client.WaitForEvent("game.state");
    client.WaitForEvent("game.state");
    client.TakePending();
    controller->SetGameStatus("beamng", Games::GameStatus::NotFound, "gone");
    auto event = client.WaitForEvent("game.state");
    REQUIRE(event);
    CHECK_FALSE((*event)["data"].contains("steam_app_id"));
}

TEST_CASE_FIXTURE(GearEngine, "without a relay there are no sessions to ask for") {
    BecomeReady();
    TestClient client(port);
    json hello = client.Hello(Token);
    CHECK(hello["data"]["capabilities"] == json::array({ "logs", "games", "account", "signin", "game_messages" }));
    CHECK(ErrorCode(client.Call("session.create", { { "game", "gearblocks" } })) == "unknown_request");
    json games = client.Call("games.list")["data"]["games"];
    CHECK_FALSE(games[1].contains("sessions"));
}

TEST_CASE_FIXTURE(SessionEngine, "a relay adds the capability and marks the games that can use it") {
    BecomeReady();
    TestClient client(port);
    json hello = client.Hello(Token);
    CHECK(hello["data"]["capabilities"] == json::array({ "logs", "games", "account", "signin", "game_messages", "sessions" }));
    json games = client.Call("games.list")["data"]["games"];
    CHECK(games[1]["sessions"] == true);
    CHECK_FALSE(games[0].contains("sessions")); // BeamNG has no sessions
    beamng.sessions = true;
    games = client.Call("games.list")["data"]["games"];
    CHECK(games[0]["sessions"] == true);
}

TEST_CASE_FIXTURE(SessionEngine, "creating a room goes through connecting to being its host, with events all the way") {
    BecomeReady();
    TestClient client(port);
    client.Hello(Token);
    client.WaitForEvent("session.state"); // the one every new controller gets
    client.TakePending();

    CHECK(client.Call("session.create", { { "game", "gearblocks" }, { "max_players", 4 }, { "password", "pw" } })["ok"] == true);
    auto connecting = client.WaitForEvent("session.state");
    REQUIRE(connecting);
    CHECK((*connecting)["data"] == json { { "game", "gearblocks" }, { "state", "connecting" } });

    Relay(Relay::Event::Kind::Welcome, { { "v", 1 } });
    REQUIRE(Eventually([&] { return sessionLink->Has("create 4 [pw]"); }));
    Relay(Relay::Event::Kind::RoomCreated, { { "code", "ABC234" }, { "peer_id", 1 } });
    auto inRoom = client.WaitForEvent("session.state");
    REQUIRE(inRoom);
    CHECK((*inRoom)["data"]["state"] == "in_room");
    CHECK((*inRoom)["data"]["role"] == "host");
    CHECK((*inRoom)["data"]["code"] == "ABC234");
    CHECK((*inRoom)["data"]["peers"] == json::array({ { { "id", 1 }, { "name", "Ann" } } }));

    CHECK(client.Call("session.state")["data"] == (*inRoom)["data"]);
    Relay(Relay::Event::Kind::PeerJoined, { { "id", 2 }, { "name", "Bob" } });
    auto joined = client.WaitForEvent("session.state");
    REQUIRE(joined);
    CHECK((*joined)["data"]["peers"].size() == 2);
}

TEST_CASE_FIXTURE(SessionEngine, "joining a room by its code") {
    BecomeReady();
    TestClient client(port);
    client.Hello(Token);
    CHECK(client.Call("session.join", { { "game", "gearblocks" }, { "code", "ABC234" } })["ok"] == true);
    Relay(Relay::Event::Kind::Welcome, { { "v", 1 } });
    REQUIRE(Eventually([&] { return sessionLink->Has("join ABC234 []"); }));
    Relay(Relay::Event::Kind::RoomJoined, { { "code", "ABC234" }, { "peer_id", 2 }, { "host_id", 1 }, { "peers", json::array({ { { "id", 1 }, { "name", "Host" } } }) } });
    REQUIRE(Eventually([&] { return client.Call("session.state")["data"]["state"] == "in_room"; }));
    json state = client.Call("session.state")["data"];
    CHECK(state["role"] == "guest");
    CHECK(state["peers"].size() == 2);
}

TEST_CASE_FIXTURE(SessionEngine, "a relay that refuses the room is reported as an event with the reason") {
    BecomeReady();
    TestClient client(port);
    client.Hello(Token);
    client.WaitForEvent("session.state"); // the idle one every new controller gets
    client.Call("session.join", { { "game", "gearblocks" }, { "code", "ABC234" } });
    Relay(Relay::Event::Kind::Welcome, { { "v", 1 } });
    Relay(Relay::Event::Kind::Error, { { "code", "room_not_found" }, { "message", "there is no such room" } });
    for (;;) {
        auto event = client.WaitForEvent("session.state");
        REQUIRE(event);
        if ((*event)["data"]["state"] == "idle") {
            CHECK((*event)["data"]["error"] == json { { "code", "room_not_found" }, { "message", "there is no such room" } });
            break;
        }
    }
}

TEST_CASE_FIXTURE(SessionEngine, "session requests are checked") {
    BecomeReady();
    TestClient client(port);
    client.Hello(Token);
    CHECK(ErrorCode(client.Call("session.create", { { "game", "nothing" } })) == "unknown_game");
    CHECK(ErrorCode(client.Call("session.create", json::object())) == "unknown_game");
    CHECK(ErrorCode(client.Call("session.create", { { "game", "gearblocks" }, { "max_players", 1 } })) == "invalid_argument");
    CHECK(ErrorCode(client.Call("session.create", { { "game", "gearblocks" }, { "max_players", 33 } })) == "invalid_argument");
    CHECK(ErrorCode(client.Call("session.create", { { "game", "gearblocks" }, { "max_players", "four" } })) == "invalid_argument");
    CHECK(ErrorCode(client.Call("session.create", { { "game", "gearblocks" }, { "password", std::string(65, 'x') } })) == "invalid_argument");
    CHECK(ErrorCode(client.Call("session.create", { { "game", "gearblocks" }, { "password", 5 } })) == "invalid_argument");
    CHECK(ErrorCode(client.Call("session.join", { { "game", "gearblocks" }, { "code", "abc" } })) == "invalid_argument");
    CHECK(ErrorCode(client.Call("session.join", { { "game", "gearblocks" } })) == "invalid_argument");
    CHECK(ErrorCode(client.Call("session.join", { { "game", "gearblocks" }, { "code", 5 } })) == "invalid_argument");
    CHECK(ErrorCode(client.Call("session.create", { { "game", "beamng" } })) == "session_unsupported");
    controller->SetGameStatus("gearblocks", Games::GameStatus::NotFound, "gone");
    CHECK(ErrorCode(client.Call("session.create", { { "game", "gearblocks" } })) == "game_not_found");
    CHECK(client.Call("session.state")["data"]["state"] == "idle");
}

TEST_CASE_FIXTURE(SessionEngine, "one session at a time, and leaving ends it") {
    BecomeReady();
    TestClient client(port);
    client.Hello(Token);
    CHECK(client.Call("session.create", { { "game", "gearblocks" } })["ok"] == true);
    CHECK(ErrorCode(client.Call("session.create", { { "game", "gearblocks" } })) == "session_busy");
    CHECK(ErrorCode(client.Call("session.join", { { "game", "gearblocks" }, { "code", "ABC234" } })) == "session_busy");

    CHECK(client.Call("session.leave")["ok"] == true);
    CHECK(client.Call("session.state")["data"]["state"] == "idle");
    CHECK(client.Call("session.leave")["ok"] == true); // nothing to leave is fine
    CHECK(client.Call("session.create", { { "game", "gearblocks" } })["ok"] == true);
}

TEST_CASE_FIXTURE(SessionEngine, "a relay that cannot be used says why") {
    relayHost = "relay.beammp.com";
    // the fixture was built with the default host: build a second one for this
    Games::RelaySession::Options options;
    options.relay = { "relay.beammp.com", 7777 };
    Games::RelaySession remote(options, [](const Relay::Endpoint&, const Relay::Hello&, Relay::EventHandler, std::string&) -> std::unique_ptr<Relay::Link> { return nullptr; });
    CHECK_FALSE(remote.Available());
    CHECK(remote.Create("gearblocks", 0, "") == Games::SessionStart::Unavailable);
}

TEST_CASE_FIXTURE(SessionEngine, "a new controller is told how the session stands") {
    BecomeReady();
    {
        TestClient first(port);
        first.Hello(Token);
        first.Call("session.create", { { "game", "gearblocks" } });
        Relay(Relay::Event::Kind::Welcome, { { "v", 1 } });
        Relay(Relay::Event::Kind::RoomCreated, { { "code", "ABC234" }, { "peer_id", 1 } });
        REQUIRE(Eventually([&] { return first.Call("session.state")["data"]["state"] == "in_room"; }));
    }
    TestClient second(port);
    second.Hello(Token);
    auto state = second.WaitForEvent("session.state");
    REQUIRE(state);
    CHECK((*state)["data"]["state"] == "in_room");
    CHECK((*state)["data"]["code"] == "ABC234");
}

TEST_CASE_FIXTURE(SessionEngine, "game data from the mod goes to the room, not to the controller") {
    BecomeReady();
    TestClient client(port);
    client.Hello(Token);
    client.Call("session.create", { { "game", "gearblocks" } });
    Relay(Relay::Event::Kind::Welcome, { { "v", 1 } });
    Relay(Relay::Event::Kind::RoomCreated, { { "code", "ABC234" }, { "peer_id", 1 } });
    REQUIRE(Eventually([&] { return client.Call("session.state")["data"]["state"] == "in_room"; }));
    client.TakePending();

    REQUIRE(gearblocks.messageHandler);
    gearblocks.messageHandler({ { "type", "data" }, { "kind", "all" }, { "data", "Zm9v" } });
    gearblocks.messageHandler({ { "type", "data" }, { "kind", "peer" }, { "target", 3 }, { "data", "YmFy" } });
    CHECK(sessionLink->Has("data 1 0 [foo]"));
    CHECK(sessionLink->Has("data 2 3 [bar]"));

    // malformed data is dropped
    gearblocks.messageHandler({ { "type", "data" }, { "kind", "peer" }, { "data", "YmFy" } });
    gearblocks.messageHandler({ { "type", "data" }, { "kind", "all" }, { "data", "!!" } });
    // other messages are still the controller's
    gearblocks.messageHandler({ { "type", "hello" } });
    auto event = client.WaitForEvent("game.message");
    REQUIRE(event);
    CHECK((*event)["data"]["message"]["type"] == "hello");
    for (const auto& pending : client.TakePending()) {
        if (pending["name"] == "game.message") {
            CHECK(pending["data"]["message"]["type"] != "data");
        }
    }
    std::lock_guard<std::mutex> lock(sessionLink->mutex);
    int sent = 0;
    for (const auto& call : sessionLink->calls) {
        sent += call.rfind("data ", 0) == 0 ? 1 : 0;
    }
    CHECK(sent == 2);
}

TEST_CASE_FIXTURE(SessionEngine, "game data is not sent while there is no room") {
    BecomeReady();
    REQUIRE(gearblocks.messageHandler);
    gearblocks.messageHandler({ { "type", "data" }, { "kind", "all" }, { "data", "Zm9v" } });
    CHECK_FALSE(sessionLink->Has("data "));
}

TEST_CASE_FIXTURE(SessionEngine, "data from other players reaches the mod of the room's game") {
    BecomeReady();
    TestClient client(port);
    client.Hello(Token);
    client.Call("session.create", { { "game", "gearblocks" } });
    Relay(Relay::Event::Kind::Welcome, { { "v", 1 } });
    Relay(Relay::Event::Kind::RoomCreated, { { "code", "ABC234" }, { "peer_id", 1 } });
    REQUIRE(Eventually([&] { return client.Call("session.state")["data"]["state"] == "in_room"; }));
    int before = gbSends;

    Relay::Event event;
    event.kind = Relay::Event::Kind::Data;
    event.data.sender = 2;
    event.data.kind = Relay::DataKind::ToHost;
    event.data.bytes = "foo";
    {
        std::lock_guard<std::mutex> lock(relayMutex);
        relayHandler(event);
    }
    REQUIRE(Eventually([&] { return gbSends > before; }));
    std::lock_guard<std::mutex> lock(gbMutex);
    CHECK(gbLastMessage == json { { "type", "data" }, { "from", 2 }, { "kind", "host" }, { "data", "Zm9v" } });
}

TEST_CASE_FIXTURE(InstallEngine, "a game with an install is advertised, with where its install stands") {
    BecomeReady();
    TestClient client(port);
    auto hello = client.Hello(Token);
    auto capabilities = hello["data"]["capabilities"];
    CHECK(std::find(capabilities.begin(), capabilities.end(), "game_install") != capabilities.end());
    auto games = client.Call("games.list")["data"]["games"];
    CHECK_FALSE(games[0].contains("install")); // BeamNG has none
    CHECK(games[1]["install"] == json { { "state", "needs_install" }, { "busy", false } });
    gearblocks.installInfo = nullptr;
    CHECK(client.Call("games.list")["data"]["games"][1]["install"] == json { { "state", "unknown" }, { "busy", false } });
}

TEST_CASE_FIXTURE(Engine, "without an install nothing is advertised") {
    TestClient client(port);
    auto capabilities = client.Hello(Token)["data"]["capabilities"];
    CHECK(std::find(capabilities.begin(), capabilities.end(), "game_install") == capabilities.end());
}

TEST_CASE_FIXTURE(InstallEngine, "an install reports its progress and its end, and the game is prepared again") {
    BecomeReady();
    gearblocks.onInstall = [](const Games::InstallReporter& progress) {
        progress({ "loader", "Downloading", 0.25 });
        progress({ "plugin", "Installing the plugin", 0.9 });
        return Games::InstallResult { true, "", "" };
    };
    TestClient client(port);
    client.Hello(Token);
    CHECK(client.Call("game.install", { { "game", "gearblocks" } })["ok"] == true);
    std::vector<json> events;
    for (int i = 0; i < 20; i++) {
        auto event = client.WaitForEvent("game.install", 3000);
        REQUIRE(event);
        events.push_back((*event)["data"]);
        if ((*event)["data"]["state"] != "running") {
            break;
        }
    }
    REQUIRE(events.size() >= 3);
    CHECK(events.front()["state"] == "running");
    CHECK(events.front()["game"] == "gearblocks");
    bool sawLoader = false;
    for (const auto& event : events) {
        sawLoader = sawLoader || (event["step"] == "loader" && event["message"] == "Downloading" && event["fraction"] == 0.25);
    }
    CHECK(sawLoader);
    CHECK(events.back()["state"] == "done");
    CHECK(events.back()["fraction"] == 1.0);
    CHECK(events.back()["uninstall"] == false);
    REQUIRE(Eventually([&] {
        std::lock_guard<std::mutex> lock(configureMutex);
        return !configured.empty();
    }));
    {
        std::lock_guard<std::mutex> lock(configureMutex);
        CHECK(configured.back().first == "gearblocks");
    }
    CHECK(Eventually([&] { return client.Call("games.list")["data"]["games"][1]["install"]["busy"] == false; }));
}

TEST_CASE_FIXTURE(InstallEngine, "a failed install says why") {
    BecomeReady();
    gearblocks.onInstall = [](const Games::InstallReporter&) { return Games::InstallResult { false, "download_failed", "no network" }; };
    TestClient client(port);
    client.Hello(Token);
    client.Call("game.install", { { "game", "gearblocks" } });
    json last;
    for (int i = 0; i < 20; i++) {
        auto event = client.WaitForEvent("game.install", 3000);
        REQUIRE(event);
        last = (*event)["data"];
        if (last["state"] != "running") {
            break;
        }
    }
    CHECK(last["state"] == "failed");
    CHECK(last["error"] == json { { "code", "download_failed" }, { "message", "no network" } });
}

TEST_CASE_FIXTURE(InstallEngine, "an install can be cancelled and ends as cancelled") {
    BecomeReady();
    std::atomic<bool> release = false;
    gearblocks.onInstall = [&](const Games::InstallReporter&) {
        while (!release) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return Games::InstallResult { false, "cancelled", "Cancelled" };
    };
    TestClient client(port);
    client.Hello(Token);
    client.Call("game.install", { { "game", "gearblocks" } });
    REQUIRE(Eventually([&] { return client.Call("games.list")["data"]["games"][1]["install"]["busy"] == true; }));
    CHECK(client.Call("game.install_cancel", { { "game", "gearblocks" } })["ok"] == true);
    CHECK(gearblocks.installCancels == 1);
    release = true;
    json last;
    for (int i = 0; i < 20; i++) {
        auto event = client.WaitForEvent("game.install", 3000);
        REQUIRE(event);
        last = (*event)["data"];
        if (last["state"] != "running") {
            break;
        }
    }
    CHECK(last["state"] == "cancelled");
}

TEST_CASE_FIXTURE(InstallEngine, "only one install at a time, and no launch or settings change while it runs") {
    BecomeReady();
    std::atomic<bool> release = false;
    gearblocks.onInstall = [&](const Games::InstallReporter&) {
        while (!release) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return Games::InstallResult { true, "", "" };
    };
    TestClient client(port);
    client.Hello(Token);
    client.Call("game.install", { { "game", "gearblocks" } });
    REQUIRE(Eventually([&] { return client.Call("games.list")["data"]["games"][1]["install"]["busy"] == true; }));
    CHECK(ErrorCode(client.Call("game.install", { { "game", "gearblocks" } })) == "install_busy");
    CHECK(ErrorCode(client.Call("game.uninstall", { { "game", "gearblocks" } })) == "install_busy");
    CHECK(ErrorCode(client.Call("game.launch", { { "game", "gearblocks" } })) == "game_busy");
    CHECK(ErrorCode(client.Call("game.configure", { { "game", "gearblocks" } })) == "game_not_ready");
    // another game's install state is not busy
    release = true;
    REQUIRE(client.WaitForEvent("game.install", 3000));
    CHECK(Eventually([&] { return client.Call("games.list")["data"]["games"][1]["install"]["busy"] == false; }));
}

TEST_CASE_FIXTURE(InstallEngine, "uninstalling passes on whether the loader goes too") {
    BecomeReady();
    std::atomic<int> removed = -1;
    gearblocks.onUninstall = [&](bool removeLoader, const Games::InstallReporter&) {
        removed = removeLoader ? 1 : 0;
        return Games::InstallResult { true, "", "" };
    };
    TestClient client(port);
    client.Hello(Token);
    client.Call("game.uninstall", { { "game", "gearblocks" } });
    json last;
    for (int i = 0; i < 20; i++) {
        auto event = client.WaitForEvent("game.install", 3000);
        REQUIRE(event);
        last = (*event)["data"];
        if (last["state"] != "running") {
            break;
        }
    }
    CHECK(last["state"] == "done");
    CHECK(last["uninstall"] == true);
    CHECK(removed == 0);
    REQUIRE(Eventually([&] { return client.Call("games.list")["data"]["games"][1]["install"]["busy"] == false; }));
    client.Call("game.uninstall", { { "game", "gearblocks" }, { "remove_loader", true } });
    CHECK(Eventually([&] { return removed == 1; }));
}

TEST_CASE_FIXTURE(InstallEngine, "install requests are checked") {
    TestClient client(port);
    client.Hello(Token);
    CHECK(ErrorCode(client.Call("game.install", { { "game", "nothing" } })) == "unknown_game");
    CHECK(ErrorCode(client.Call("game.install", json::object())) == "unknown_game");
    CHECK(ErrorCode(client.Call("game.install", { { "game", "beamng" } })) == "install_unsupported");
    // the engine is not ready yet (it is still starting)
    CHECK(ErrorCode(client.Call("game.install", { { "game", "gearblocks" } })) == "game_not_ready");
    BecomeReady();
    RunGame(4242);
    CHECK(ErrorCode(client.Call("game.install", { { "game", "gearblocks" } })) == "game_not_ready");
    // cancelling when nothing runs is fine
    CHECK(client.Call("game.install_cancel", { { "game", "gearblocks" } })["ok"] == true);
    CHECK(gearblocks.installCancels == 0);
}
