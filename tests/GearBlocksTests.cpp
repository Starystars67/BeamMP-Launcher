/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Games/GearBlocks.h"
#include "Net/Base64.h"
#include "Net/Ed25519.h"
#include "Net/Sha256.h"
#include "Net/Unzip.h"
#include "Logger.h"
#include "Options.h"
#include "Status.h"
#include "TestClient.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <doctest/doctest.h>
#include <fstream>
#include <map>
#include <thread>
#if !defined(_WIN32)
#include <csignal>
#include <unistd.h>
#endif

using namespace Games;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {
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

void Touch(const fs::path& path) {
    fs::create_directories(path.parent_path());
    std::ofstream(path) << "x";
}

struct GameFolder {
    fs::path dir;
    GameFolder() {
        dir = fs::temp_directory_path() / ("beammp-gb-" + GameBridge::GenerateToken());
        fs::create_directories(dir);
    }
    ~GameFolder() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    void Install() {
        Touch(dir / "GearBlocks.exe");
        Touch(dir / "winhttp.dll");
        Touch(dir / "BepInEx" / "plugins" / "BeamMP.GearBlocks.dll");
    }
};

std::map<std::string, std::string> Environment(const LaunchSpec& spec) {
    return { spec.environment.begin(), spec.environment.end() };
}
}

TEST_CASE_FIXTURE(GameFolder, "the install check names what is missing, in the order it matters") {
    CHECK(CheckGearBlocksInstall(dir).find("GearBlocks.exe was not found") == 0);
    Touch(dir / "GearBlocks.exe");
    CHECK(CheckGearBlocksInstall(dir).find("BepInEx is not installed") == 0);
    Touch(dir / "winhttp.dll");
    CHECK(CheckGearBlocksInstall(dir).find("The BeamMP plugin is not installed") == 0);
    Touch(dir / "BepInEx" / "plugins" / "BeamMP.GearBlocks.dll");
    CHECK(CheckGearBlocksInstall(dir) == "");
    CHECK(CheckGearBlocksInstall(dir / "nowhere").find("GearBlocks.exe was not found") == 0);
}

TEST_CASE("the launch gets the bridge address and the user's arguments") {
    LaunchSpec spec = BuildGearBlocksLaunch("/games/GearBlocks", "/opt/run-gearblocks.sh", 4711, "token-token-token-1", { "-a", "b c" });
    auto env = Environment(spec);
    CHECK(env.at(BridgePortVariable) == "4711");
    CHECK(env.at(BridgeTokenVariable) == "token-token-token-1");
    CHECK(spec.args == std::vector<std::string> { "-a", "b c" });
    CHECK(spec.workingDirectory == "/games/GearBlocks");
    // the user's start command runs on every system
    CHECK(spec.program == "/opt/run-gearblocks.sh");
    CHECK(env.at("GEARBLOCKS_DIR") == "/games/GearBlocks");
    CHECK(env.at("BEAMMP_GAME_DIR") == "/games/GearBlocks");
}

#if defined(_WIN32)
TEST_CASE("without a command Windows starts the game's own executable") {
    LaunchSpec spec = BuildGearBlocksLaunch("C:\\games\\GearBlocks", "", 1, "token-token-token-1", {});
    CHECK(fs::path(spec.program).filename() == "GearBlocks.exe");
}
#endif

TEST_CASE("a command launch passes the game folder, the user folder and the arguments") {
    LaunchSpec spec = BuildCommandLaunch("beamng", "/games/BeamNG", "/users/x/BeamNG.drive", "/opt/run-beamng.sh", { "vk", "-console" });
    auto env = Environment(spec);
    CHECK(spec.program == "/opt/run-beamng.sh");
    CHECK(spec.args == std::vector<std::string> { "vk", "-console" });
    CHECK(spec.workingDirectory == "/games/BeamNG");
    CHECK(env.at("BEAMNG_DIR") == "/games/BeamNG");
    CHECK(env.at("BEAMMP_GAME_DIR") == "/games/BeamNG");
    CHECK(env.at("BEAMMP_USER_PATH") == "/users/x/BeamNG.drive");
}

TEST_CASE("a command launch leaves out what is not known") {
    LaunchSpec spec = BuildCommandLaunch("beamng", "", "", "/opt/run-beamng.sh", {});
    CHECK(spec.environment.empty());
    CHECK(spec.workingDirectory.empty());
}

#if !defined(_WIN32)
TEST_CASE("without a command GearBlocks cannot be started outside Windows, and the reason says what to do") {
    try {
        BuildGearBlocksLaunch("/games/GearBlocks", "", 1, "token-token-token-1", {});
        FAIL("should have thrown");
    } catch (const std::runtime_error& e) {
        CHECK(std::string(e.what()).find("start command") != std::string::npos);
    }
}
#endif

namespace {
// Stands in for the real process: reports a pid, then waits until it is told to end.
struct FakeProcess {
    std::mutex mutex;
    std::condition_variable wake;
    bool end = false;
    bool startFails = false;
    int exitCode = 0;
    unsigned long pid = 4321;
    std::atomic<int> runs = 0;
    LaunchSpec lastSpec;
    std::atomic<int> stops = 0;
    std::atomic<unsigned long> stoppedPid = 0;

    ProcessRunner Runner() {
        return [this](const LaunchSpec& spec, const std::function<void(unsigned long)>& started) {
            {
                std::lock_guard<std::mutex> lock(mutex);
                lastSpec = spec;
                end = false;
            }
            ++runs;
            RunResult result;
            if (startFails) {
                result.error = "no such file";
                return result;
            }
            started(pid);
            std::unique_lock<std::mutex> lock(mutex);
            wake.wait(lock, [this] { return end; });
            result.launched = true;
            result.exitCode = exitCode;
            return result;
        };
    }

    ProcessStopper Stopper() {
        return [this](unsigned long target) {
            ++stops;
            stoppedPid = target;
            Exit(143);
            return true;
        };
    }

    void Exit(int code) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            exitCode = code;
            end = true;
        }
        wake.notify_all();
    }

    LaunchSpec Spec() {
        std::lock_guard<std::mutex> lock(mutex);
        return lastSpec;
    }
};

struct Adapter : GameFolder, FakeProcess {
    Lifecycle lifecycle;
    std::unique_ptr<GearBlocksModule> adapter;
    std::mutex messageMutex;
    std::vector<json> messages;

    Adapter() {
        options = Options();
        options.headless = true;
        InitLog();
        GamePID = 0;
        Install();
        MakeAdapter("/opt/run-gearblocks.sh");
        lifecycle.Set(EngineState::PreparingGame);
        lifecycle.Set(EngineState::Ready);
    }

    void MakeAdapter(const std::string& command) {
        adapter.reset();
        GameConfig config;
        config.directory = dir.string();
        config.command = command;
        adapter = std::make_unique<GearBlocksModule>(config, lifecycle, Runner(), Stopper());
        adapter->SetMessageHandler([this](const json& message) {
            std::lock_guard<std::mutex> lock(messageMutex);
            messages.push_back(message);
        });
    }

    void Launch(const std::vector<std::string>& args = {}) {
        REQUIRE(lifecycle.TryTransition(EngineState::Ready, EngineState::LaunchingGame));
        adapter->Launch(args);
    }

    std::vector<json> Messages() {
        std::lock_guard<std::mutex> lock(messageMutex);
        return messages;
    }

    // The mod: connects with the token the launcher put in the game's environment.
    std::unique_ptr<TestClient> ConnectMod() {
        auto env = Environment(Spec());
        auto mod = std::make_unique<TestClient>(static_cast<uint16_t>(std::stoi(env.at(BridgePortVariable))));
        mod->SendLine(env.at(BridgeTokenVariable));
        return mod;
    }
};
}

TEST_CASE_FIXTURE(Adapter, "launching starts the game with the bridge in its environment and follows it to the end") {
    Launch({ "-x" });
    REQUIRE(Eventually([&] { return lifecycle.State() == EngineState::GameRunning; }));
    CHECK(GamePID == 4321);
    LaunchSpec spec = Spec();
    CHECK(spec.args == std::vector<std::string> { "-x" });
    CHECK(Environment(spec).at(BridgeTokenVariable).size() == 32);
    CHECK(std::stoi(Environment(spec).at(BridgePortVariable)) > 0);

    Exit(0);
    REQUIRE(Eventually([&] { return lifecycle.State() == EngineState::Ready; }));
    CHECK(lifecycle.Detail() == "The game exited with code 0");
    CHECK(GamePID == 0);
}

TEST_CASE_FIXTURE(Adapter, "every launch gets its own token and port") {
    Launch();
    REQUIRE(Eventually([&] { return lifecycle.State() == EngineState::GameRunning; }));
    auto first = Environment(Spec());
    Exit(0);
    REQUIRE(Eventually([&] { return lifecycle.State() == EngineState::Ready; }));

    Launch();
    REQUIRE(Eventually([&] { return lifecycle.State() == EngineState::GameRunning; }));
    auto second = Environment(Spec());
    CHECK(first.at(BridgeTokenVariable) != second.at(BridgeTokenVariable));
    Exit(0);
    REQUIRE(Eventually([&] { return lifecycle.State() == EngineState::Ready; }));
    CHECK(runs == 2);
}

TEST_CASE_FIXTURE(Adapter, "messages from the mod are delivered as JSON objects, and the launcher can answer") {
    Launch();
    REQUIRE(Eventually([&] { return lifecycle.State() == EngineState::GameRunning; }));
    auto mod = ConnectMod();
    REQUIRE(Eventually([&] { return adapter->Connected(); }));

    mod->SendLine("{\"type\":\"hello\",\"mod\":\"beammp\"}");
    mod->SendLine("not json");
    mod->SendLine("[1,2]");
    mod->SendLine("{\"type\":\"second\"}");
    REQUIRE(Eventually([&] { return Messages().size() == 2; }));
    CHECK(Messages()[0] == json { { "type", "hello" }, { "mod", "beammp" } });
    CHECK(Messages()[1] == json { { "type", "second" } });

    CHECK(adapter->Send({ { "type", "welcome" } }));
    CHECK(mod->ReadLine() == "{\"type\":\"welcome\"}");

    Exit(0);
    REQUIRE(Eventually([&] { return lifecycle.State() == EngineState::Ready; }));
    CHECK_FALSE(adapter->Connected());
    CHECK_FALSE(adapter->Send({ { "type", "late" } }));
    CHECK(mod->WaitForClose());
}

TEST_CASE_FIXTURE(Adapter, "nothing can be sent before the mod has connected") {
    Launch();
    REQUIRE(Eventually([&] { return lifecycle.State() == EngineState::GameRunning; }));
    CHECK_FALSE(adapter->Send({ { "type", "early" } }));
    Exit(0);
    REQUIRE(Eventually([&] { return lifecycle.State() == EngineState::Ready; }));
}

TEST_CASE_FIXTURE(Adapter, "a connection with the previous game's token is refused") {
    Launch();
    REQUIRE(Eventually([&] { return lifecycle.State() == EngineState::GameRunning; }));
    auto old = Environment(Spec());
    Exit(0);
    REQUIRE(Eventually([&] { return lifecycle.State() == EngineState::Ready; }));

    Launch();
    REQUIRE(Eventually([&] { return lifecycle.State() == EngineState::GameRunning; }));
    auto current = Environment(Spec());
    TestClient stale(static_cast<uint16_t>(std::stoi(current.at(BridgePortVariable))));
    stale.SendLine(old.at(BridgeTokenVariable));
    CHECK(stale.WaitForClose());
    CHECK_FALSE(adapter->Connected());
    Exit(0);
    REQUIRE(Eventually([&] { return lifecycle.State() == EngineState::Ready; }));
}

TEST_CASE_FIXTURE(Adapter, "stopping hands the running pid to the stopper") {
    CHECK_FALSE(adapter->Stop()); // nothing runs yet
    Launch();
    REQUIRE(Eventually([&] { return lifecycle.State() == EngineState::GameRunning; }));
    CHECK(adapter->Stop());
    CHECK(stoppedPid == 4321);
    REQUIRE(Eventually([&] { return lifecycle.State() == EngineState::Ready; }));
    CHECK(lifecycle.Detail() == "The game exited with code 143");
    CHECK_FALSE(adapter->Stop()); // and nothing runs now
    CHECK(stops == 1);
}

TEST_CASE_FIXTURE(Adapter, "a game that cannot be started returns the engine to ready with the reason") {
    startFails = true;
    Launch();
    REQUIRE(Eventually([&] { return lifecycle.State() == EngineState::Ready; }));
    CHECK(lifecycle.Detail() == "Failed to launch the game: no such file");
    CHECK(GamePID == 0);
    CHECK_FALSE(adapter->Connected());
}

TEST_CASE_FIXTURE(Adapter, "a missing install is reported before anything starts") {
    fs::remove(dir / "BepInEx" / "plugins" / "BeamMP.GearBlocks.dll");
    REQUIRE(lifecycle.TryTransition(EngineState::Ready, EngineState::LaunchingGame));
    CHECK_THROWS_WITH_AS(adapter->Launch({}), doctest::Contains("plugin is not installed"), std::runtime_error);
    CHECK(runs == 0);
}

#if !defined(_WIN32)
TEST_CASE_FIXTURE(Adapter, "a missing command is reported and does not leave a bridge open") {
    MakeAdapter("");
    REQUIRE(lifecycle.TryTransition(EngineState::Ready, EngineState::LaunchingGame));
    CHECK_THROWS_WITH_AS(adapter->Launch({}), doctest::Contains("start command"), std::runtime_error);
    CHECK(runs == 0);
    CHECK_FALSE(adapter->Connected());
}
#endif

#if !defined(_WIN32)
TEST_CASE("the system runner passes arguments and environment, reports the pid, and returns the exit code") {
    std::atomic<unsigned long> pid = 0;
    LaunchSpec spec;
    spec.program = "/bin/sh";
    spec.args = { "-c", "test \"$BEAMMP_TEST_VALUE\" = \"a b\" && test \"$1\" = x && exit 7", "sh", "x" };
    spec.environment = { { "BEAMMP_TEST_VALUE", "a b" } };
    RunResult result = SystemProcessRunner()(spec, [&](unsigned long started) { pid = started; });
    CHECK(result.launched);
    CHECK(result.exitCode == 7);
    CHECK(pid > 0);
    CHECK(getenv("BEAMMP_TEST_VALUE") == nullptr); // only the child saw it
}

TEST_CASE("the system runner reports a program that does not exist") {
    LaunchSpec spec;
    spec.program = "/nonexistent/program";
    bool started = false;
    RunResult result = SystemProcessRunner()(spec, [&](unsigned long) { started = true; });
    CHECK_FALSE(result.launched);
    CHECK_FALSE(started);
    CHECK_FALSE(result.error.empty());
}

TEST_CASE("stopping the system process also stops what it started") {
    std::string pidFile = (fs::temp_directory_path() / ("beammp-pid-" + GameBridge::GenerateToken())).string();
    std::atomic<unsigned long> pid = 0;
    LaunchSpec spec;
    spec.program = "/bin/sh";
    spec.args = { "-c", "sleep 60 & echo $! > '" + pidFile + "'; wait" }; // a child of its own, like Wine's
    std::atomic<bool> finished = false;
    RunResult result;
    std::thread runner([&] {
        result = SystemProcessRunner()(spec, [&](unsigned long started) { pid = started; });
        finished = true;
    });
    REQUIRE(Eventually([&] { return pid != 0; }));
    long grandchild = 0;
    REQUIRE(Eventually([&] {
        std::ifstream in(pidFile);
        return static_cast<bool>(in >> grandchild) && grandchild > 0;
    }));
    REQUIRE(kill(static_cast<pid_t>(grandchild), 0) == 0); // alive before
    CHECK(StopSystemProcess(pid));
    CHECK(Eventually([&] { return finished.load(); }, 5000));
    runner.join();
    CHECK(result.launched);
    CHECK(Eventually([&] { return kill(static_cast<pid_t>(grandchild), 0) != 0; }, 5000));
    fs::remove(pidFile);
}

TEST_CASE("the system runner does not hand its output to the launcher's stdout") {
    // The control protocol's stdout must stay clean: a game that prints must not reach it.
    std::string path = (fs::temp_directory_path() / ("beammp-out-" + GameBridge::GenerateToken())).string();
    fflush(stdout);
    int saved = dup(STDOUT_FILENO);
    REQUIRE(freopen(path.c_str(), "w", stdout) != nullptr);
    LaunchSpec spec;
    spec.program = "/bin/sh";
    spec.args = { "-c", "echo from-the-game; echo from-the-game-err >&2" };
    SystemProcessRunner()(spec, [](unsigned long) {});
    fflush(stdout);
    dup2(saved, STDOUT_FILENO);
    close(saved);
    clearerr(stdout);
    std::ifstream captured(path);
    std::string content((std::istreambuf_iterator<char>(captured)), std::istreambuf_iterator<char>());
    fs::remove(path);
    CHECK(content.empty());
}
#endif

TEST_CASE_FIXTURE(Adapter, "preparing GearBlocks reports what is missing, and nothing when it is all there") {
    PrepareResult ready = adapter->Prepare();
    CHECK(ready.ok());
    CHECK(ready.detail.empty());
    CHECK(adapter->Id() == "gearblocks");
    CHECK(adapter->Name() == "GearBlocks");
    CHECK(adapter->HasMessageChannel());

    fs::remove(dir / "BepInEx" / "plugins" / "BeamMP.GearBlocks.dll");
    PrepareResult missing = adapter->Prepare();
    CHECK(missing.status == GameStatus::NotFound);
    CHECK(missing.detail.find("plugin is not installed") != std::string::npos);

    fs::remove(dir / "GearBlocks.exe");
    CHECK(adapter->Prepare().detail.find("GearBlocks.exe was not found") == 0);
}

TEST_CASE_FIXTURE(Adapter, "--no-launch stops a launch before anything starts") {
    options.no_launch = true;
    REQUIRE(lifecycle.TryTransition(EngineState::Ready, EngineState::LaunchingGame));
    CHECK_THROWS_WITH_AS(adapter->Launch({}), doctest::Contains("--no-launch"), std::runtime_error);
    CHECK(runs == 0);
}

TEST_CASE_FIXTURE(Adapter, "the settings tell where the game is, and a change is used by the next preparation") {
    GameConfig config = adapter->Config();
    CHECK(config.directory == dir.string());
    CHECK(config.command == "/opt/run-gearblocks.sh");
    CHECK(adapter->Prepare().ok());

    GameFolder other; // a folder with nothing in it
    config.directory = other.dir.string();
    adapter->Configure(config);
    CHECK(adapter->Config() == config);
    PrepareResult missing = adapter->Prepare();
    CHECK(missing.status == GameStatus::NotFound);
    CHECK(missing.detail.find(other.dir.string()) != std::string::npos);

    other.Install();
    CHECK(adapter->Prepare().ok());
}

TEST_CASE_FIXTURE(Adapter, "the configured command is what is started") {
    GameConfig config = adapter->Config();
    config.command = "/somewhere/else/start-gearblocks.sh";
    adapter->Configure(config);
    Launch();
    REQUIRE(Eventually([&] { return lifecycle.State() == EngineState::GameRunning; }));
    CHECK(Spec().program == "/somewhere/else/start-gearblocks.sh");
    Exit(0);
    REQUIRE(Eventually([&] { return lifecycle.State() == EngineState::Ready; }));
}

namespace {
const std::string SigningSeed(32, '\x31');
const std::string ManifestAddress = "https://files.example.dev/gb/manifest.json";

// A signed manifest with one release, and the files it names, served from memory.
struct Release {
    std::map<std::string, std::string> files;
    std::vector<std::string> requests;
    bool down = false;

    explicit Release(const std::string& plugin = "plugin v2") {
        fs::path zip = fs::temp_directory_path() / ("beammp-gb-mod-" + GameBridge::GenerateToken() + ".zip");
        REQUIRE(Net::CreateZip(zip, { { "main.lua", "print('mod')" } }));
        std::ifstream in(zip, std::ios::binary);
        std::string modBytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        in.close();
        std::error_code ec;
        fs::remove(zip, ec);
        files["https://files.example.dev/gb/plugin.dll"] = plugin;
        files["https://files.example.dev/gb/mod.zip"] = modBytes;
        json manifest = { { "schema", 1 }, { "game", "gearblocks" }, { "sequence", 1 },
            { "loader", { { "version", "6.0.0" }, { "url", "https://files.example.dev/loader.zip" }, { "sha256", std::string(64, 'b') } } },
            { "releases", { { { "version", "0.1.0" }, { "bridge_protocol", 1 }, { "game_builds", { "0.8.9740" } },
                                { "plugin", { { "url", "https://files.example.dev/gb/plugin.dll" }, { "sha256", Net::Sha256Hex(plugin) } } },
                                { "mod", { { "url", "https://files.example.dev/gb/mod.zip" }, { "sha256", Net::Sha256Hex(modBytes) } } } } } } };
        std::string text = manifest.dump();
        files[ManifestAddress] = text;
        files[ManifestAddress + ".sig"] = Net::Base64Encode(*Net::Ed25519Sign(SigningSeed, text));
    }

    GearBlocksInstaller::Fetcher Fetcher() {
        return [this](const GearBlocksInstaller::FetchRequest& request) {
            requests.push_back(request.url);
            Net::DownloadResult result;
            auto found = files.find(request.url);
            if (down || found == files.end()) {
                result.error = "no network";
                return result;
            }
            std::ofstream(request.destination, std::ios::binary | std::ios::trunc) << found->second;
            result.ok = true;
            return result;
        };
    }
};

std::string Slurp(const fs::path& file) {
    std::ifstream stream(file, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}
}

TEST_CASE_FIXTURE(Adapter, "what the install says depends on what is there and on whether there is a source") {
    CHECK(adapter->InstallInfo().is_null()); // not looked at yet
    adapter->Prepare();
    json info = adapter->InstallInfo();
    CHECK(info["state"] == "needs_install"); // the loader is not complete: only winhttp.dll is there
    CHECK(info["files_available"] == false);

    Release release;
    adapter->SetInstallSource(ManifestAddress, { *Net::Ed25519PublicKey(SigningSeed) }, false, release.Fetcher());
    adapter->Prepare();
    CHECK(adapter->InstallInfo()["files_available"] == true);
    CHECK(release.requests.empty()); // preparing does not use the network

    Touch(dir / "BepInEx" / "core" / "BepInEx.Unity.IL2CPP.dll");
    Touch(dir / "GearBlocks_Data" / "StreamingAssets" / "ScriptMods" / "BeamMP" / "main.lua");
    adapter->Prepare();
    CHECK(adapter->InstallInfo()["state"] == "installed"); // nothing verified is known yet, so nothing to compare with
    fs::remove(dir / "GearBlocks.exe");
    adapter->Prepare();
    CHECK(adapter->InstallInfo()["state"] == "game_missing");
}

TEST_CASE_FIXTURE(Adapter, "a launch brings the plugin and the mod up to date from the signed manifest first") {
    Touch(dir / "BepInEx" / "core" / "BepInEx.Unity.IL2CPP.dll");
    Release release;
    adapter->SetInstallSource(ManifestAddress, { *Net::Ed25519PublicKey(SigningSeed) }, false, release.Fetcher());
    Launch();
    REQUIRE(Eventually([&] { return lifecycle.State() == EngineState::GameRunning; }));
    CHECK(Slurp(dir / "BepInEx" / "plugins" / "BeamMP.GearBlocks.dll") == "plugin v2");
    CHECK(Slurp(dir / "GearBlocks_Data" / "StreamingAssets" / "ScriptMods" / "BeamMP" / "main.lua") == "print('mod')");
    CHECK(release.requests.size() == 4); // manifest, signature, plugin, mod
    Exit(0);
    REQUIRE(Eventually([&] { return lifecycle.State() == EngineState::Ready; }));

    // the next launch finds everything as it should be and fetches only the manifest
    release.requests.clear();
    Launch();
    REQUIRE(Eventually([&] { return lifecycle.State() == EngineState::GameRunning; }));
    CHECK(release.requests.size() == 2);
    CHECK(adapter->InstallInfo()["state"] == "installed");
    CHECK(adapter->InstallInfo()["version"] == "0.1.0");
}

TEST_CASE_FIXTURE(Adapter, "a launch without a network runs what is installed") {
    Touch(dir / "BepInEx" / "core" / "BepInEx.Unity.IL2CPP.dll");
    Release release;
    release.down = true;
    adapter->SetInstallSource(ManifestAddress, { *Net::Ed25519PublicKey(SigningSeed) }, false, release.Fetcher());
    Launch();
    REQUIRE(Eventually([&] { return lifecycle.State() == EngineState::GameRunning; }));
    CHECK(Slurp(dir / "BepInEx" / "plugins" / "BeamMP.GearBlocks.dll") == "x"); // as it was
}

TEST_CASE_FIXTURE(Adapter, "a manifest nobody trusted signed does not change anything, and the launch goes on") {
    Touch(dir / "BepInEx" / "core" / "BepInEx.Unity.IL2CPP.dll");
    Release release;
    adapter->SetInstallSource(ManifestAddress, { *Net::Ed25519PublicKey(std::string(32, '\x32')) }, false, release.Fetcher());
    Launch();
    REQUIRE(Eventually([&] { return lifecycle.State() == EngineState::GameRunning; }));
    CHECK(Slurp(dir / "BepInEx" / "plugins" / "BeamMP.GearBlocks.dll") == "x");
    CHECK(release.requests.size() == 2); // it stopped at the signature
}

TEST_CASE_FIXTURE(Adapter, "without a source a launch leaves a plugin that is installed alone") {
    Touch(dir / "BepInEx" / "core" / "BepInEx.Unity.IL2CPP.dll");
    std::ofstream(dir / "BepInEx" / "plugins" / "BeamMP.GearBlocks.dll") << "my own build";
    Launch();
    REQUIRE(Eventually([&] { return lifecycle.State() == EngineState::GameRunning; }));
    CHECK(Slurp(dir / "BepInEx" / "plugins" / "BeamMP.GearBlocks.dll") == "my own build");
}

TEST_CASE_FIXTURE(Adapter, "a bundle that ships with the launcher installs without a network") {
    Touch(dir / "BepInEx" / "core" / "BepInEx.Unity.IL2CPP.dll");
    fs::path bundle = dir / "bundle";
    fs::create_directories(bundle);
    Release release;
    json manifest = json::parse(release.files[ManifestAddress]);
    manifest["releases"][0]["plugin"]["url"] = "bundle:plugin.dll";
    manifest["releases"][0]["mod"]["url"] = "bundle:mod.zip";
    std::string text = manifest.dump();
    std::ofstream(bundle / "manifest.json", std::ios::binary) << text;
    std::ofstream(bundle / "manifest.json.sig", std::ios::binary) << Net::Base64Encode(*Net::Ed25519Sign(SigningSeed, text));
    std::ofstream(bundle / "plugin.dll", std::ios::binary) << release.files["https://files.example.dev/gb/plugin.dll"];
    std::ofstream(bundle / "mod.zip", std::ios::binary) << release.files["https://files.example.dev/gb/mod.zip"];

    adapter->SetBundleDir(bundle);
    adapter->SetInstallSource("bundle:manifest.json", { *Net::Ed25519PublicKey(SigningSeed) });
    InstallResult result = adapter->Install(nullptr);
    CHECK(result.ok);
    CHECK(Slurp(dir / "BepInEx" / "plugins" / "BeamMP.GearBlocks.dll") == "plugin v2");
    CHECK(Slurp(dir / "GearBlocks_Data" / "StreamingAssets" / "ScriptMods" / "BeamMP" / "main.lua") == "print('mod')");
    CHECK(adapter->InstallInfo()["state"] == "installed");

    // a file that was changed in the bundle is not used
    std::ofstream(bundle / "plugin.dll", std::ios::binary | std::ios::trunc) << "something else";
    Touch(dir / "BepInEx" / "plugins" / "BeamMP.GearBlocks.dll");
    InstallResult changed = adapter->Install(nullptr);
    CHECK_FALSE(changed.ok);
    CHECK(changed.code == "bad_checksum");
}

TEST_CASE_FIXTURE(Adapter, "bundle addresses are refused when the source is not a bundle") {
    Touch(dir / "BepInEx" / "core" / "BepInEx.Unity.IL2CPP.dll");
    fs::create_directories(dir / "bundle");
    Release release;
    json manifest = json::parse(release.files[ManifestAddress]);
    manifest["releases"][0]["plugin"]["url"] = "bundle:plugin.dll";
    std::string text = manifest.dump();
    release.files[ManifestAddress] = text;
    release.files[ManifestAddress + ".sig"] = Net::Base64Encode(*Net::Ed25519Sign(SigningSeed, text));
    adapter->SetBundleDir(dir / "bundle");
    adapter->SetInstallSource(ManifestAddress, { *Net::Ed25519PublicKey(SigningSeed) }, false, release.Fetcher());
    InstallResult result = adapter->Install(nullptr);
    CHECK_FALSE(result.ok);
    CHECK(result.code == "bad_manifest");
}
