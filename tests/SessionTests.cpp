/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Auth/Session.h"
#include "AuthSupport.h"
#include "Options.h"
#include <chrono>
#include <doctest/doctest.h>
#include <memory>
#include <thread>

using namespace Auth;
using namespace std::chrono_literals;

namespace {
/// A launcher, a fake BeamMP API and a fake browser.
struct World {
    FakeAuthority authority;
    FakeTransport transport;
    MemorySecretStore secrets;
    Session::Config config;
    std::unique_ptr<Session> session;

    std::mutex mutex;
    std::vector<std::string> urls;
    std::vector<SignInEvent> events;
    std::vector<GameSession> signedIn;
    int signedOut = 0;

    /// What the user does with the page: by default signs in and lets the browser follow the redirect.
    std::function<bool(const std::string&)> browser = [this](const std::string& url) {
        std::string redirect = authority.Approve(url);
        size_t pathStart = redirect.find('/', std::string("http://127.0.0.1:").size());
        uint16_t port = static_cast<uint16_t>(std::stoi(redirect.substr(17, pathStart - 17)));
        return RawGet(port, redirect.substr(pathStart)).status == 200;
    };

    World() {
        transport.handler = [this](const HttpRequest& request) { return authority.Handle(request); };
        config.endpoints.apiBase = "https://api.test";
        config.endpoints.accountsBase = "https://accounts.test";
        Start();
    }

    void Start() {
        session = std::make_unique<Session>(config, transport, secrets, [this](const std::string& url) {
            {
                std::lock_guard<std::mutex> lock(mutex);
                urls.push_back(url);
            }
            return browser(url);
        });
        session->OnSignedIn([this](const GameSession& game) {
            std::lock_guard<std::mutex> lock(mutex);
            signedIn.push_back(game);
        });
        session->OnSignedOut([this] {
            std::lock_guard<std::mutex> lock(mutex);
            ++signedOut;
        });
        session->AddObserver([this](const SignInEvent& event) {
            std::lock_guard<std::mutex> lock(mutex);
            events.push_back(event);
        });
    }

    /// The launcher is closed and started again: same keychain, same server.
    void Restart() {
        session->Wait();
        session.reset();
        events.clear();
        signedIn.clear();
        signedOut = 0;
        Start();
    }

    void SignInAndWait() {
        REQUIRE(session->SignIn() == SignInResult::Started);
        session->Wait();
    }

    size_t EventCount() {
        std::lock_guard<std::mutex> lock(mutex);
        return events.size();
    }

    /// The page given to the browser, once the sign-in got that far.
    std::string WaitForUrl() {
        for (int i = 0; i < 300; ++i) {
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (!urls.empty()) {
                    return urls.back();
                }
            }
            std::this_thread::sleep_for(10ms);
        }
        return "";
    }

    std::vector<std::string> States() {
        std::vector<std::string> states;
        for (const auto& event : events) {
            states.push_back(ToString(event.state));
        }
        return states;
    }

    std::string FailureReason() {
        for (const auto& event : events) {
            if (event.state == SignInState::Failed) {
                return event.reason;
            }
        }
        return "";
    }
};
}

TEST_CASE_FIXTURE(World, "signing in goes through the browser and ends with a game session") {
    SignInAndWait();

    CHECK(States() == std::vector<std::string> { "waiting_for_browser", "exchanging", "idle" });
    REQUIRE(signedIn.size() == 1);
    CHECK(signedIn[0].username == "Racer");
    CHECK(signedIn[0].role == "EA");
    CHECK(signedIn[0].id == 7);
    CHECK(signedIn[0].publicKey == "public-1");
    CHECK(signedIn[0].privateKey == "private-1");
    CHECK(session->SignedIn());
    CHECK_FALSE(session->Busy());
}

TEST_CASE_FIXTURE(World, "the page the user is sent to is the authorize URL with PKCE and the launcher's scopes") {
    SignInAndWait();
    REQUIRE(urls.size() == 1);
    CHECK(urls[0].rfind("https://accounts.test/auth/external?", 0) == 0);
    auto query = QueryOf(urls[0]);
    CHECK(query["client_id"] == "beammp-launcher");
    CHECK(query["code_challenge_method"] == "S256");
    CHECK(query["code_challenge"].size() == 43);
    CHECK(query["state"].size() >= 20);
    CHECK(query["redirect_uri"].rfind("http://127.0.0.1:", 0) == 0);
    CHECK(authority.lastAuthorizeScope == "profile game offline_access");
    REQUIRE(events.size() >= 1);
    CHECK(events[0].url == urls[0]);
}

TEST_CASE_FIXTURE(World, "every sign-in uses a new state, challenge and port") {
    SignInAndWait();
    session->SignOut();
    SignInAndWait();
    REQUIRE(urls.size() == 2);
    auto first = QueryOf(urls[0]);
    auto second = QueryOf(urls[1]);
    CHECK(first["state"] != second["state"]);
    CHECK(first["code_challenge"] != second["code_challenge"]);
}

TEST_CASE_FIXTURE(World, "the refresh token is stored in the secret store, and nothing else") {
    SignInAndWait();
    REQUIRE(secrets.Load());
    CHECK(secrets.Load()->rfind("bmp_rt_", 0) == 0);
    CHECK(secrets.saves == 1);
}

TEST_CASE_FIXTURE(World, "a keychain that refuses to save does not stop the sign-in") {
    secrets.failSaves = true;
    SignInAndWait();
    CHECK(session->SignedIn());
    CHECK_FALSE(secrets.Load());
}

TEST_CASE_FIXTURE(World, "a callback with the wrong state does not complete the sign-in") {
    browser = [this](const std::string& url) {
        std::string redirect = authority.Approve(url);
        auto query = QueryOf(redirect);
        uint16_t port = static_cast<uint16_t>(std::stoi(redirect.substr(17, redirect.find('/', 17) - 17)));
        CHECK(RawGet(port, "/callback?code=" + query["code"] + "&state=forged").status == 400);
        return RawGet(port, "/callback?code=" + query["code"] + "&state=" + query["state"]).status == 200;
    };
    SignInAndWait();
    CHECK(session->SignedIn());
    CHECK(authority.tokenRequests == 1);
}

TEST_CASE_FIXTURE(World, "denying the request ends the sign-in without any token request") {
    browser = [this](const std::string& url) {
        auto query = QueryOf(url);
        uint16_t port = static_cast<uint16_t>(std::stoi(query["redirect_uri"].substr(17, query["redirect_uri"].find('/', 17) - 17)));
        return RawGet(port, "/callback?error=access_denied&state=" + query["state"]).status == 200;
    };
    SignInAndWait();
    CHECK(FailureReason() == "denied");
    CHECK_FALSE(session->SignedIn());
    CHECK(authority.tokenRequests == 0);
    CHECK(signedIn.empty());
}

TEST_CASE_FIXTURE(World, "cancelling while the browser is open ends the sign-in") {
    browser = [](const std::string&) { return true; };
    REQUIRE(session->SignIn() == SignInResult::Started);
    for (int i = 0; i < 300 && EventCount() == 0; ++i) {
        std::this_thread::sleep_for(10ms);
    }
    session->Cancel();
    session->Wait();
    CHECK(FailureReason() == "cancelled");
    CHECK_FALSE(session->SignedIn());
    CHECK(authority.tokenRequests == 0);
}

TEST_CASE_FIXTURE(World, "nobody coming back ends the sign-in after the timeout") {
    config.signInTimeout = 150ms;
    Start();
    browser = [](const std::string&) { return true; };
    SignInAndWait();
    CHECK(FailureReason() == "timeout");
    CHECK_FALSE(session->Busy());
}

TEST_CASE_FIXTURE(World, "a browser that could not be opened still lets the sign-in finish by hand") {
    browser = [](const std::string&) { return false; };
    REQUIRE(session->SignIn() == SignInResult::Started);
    std::string pending = WaitForUrl();
    REQUIRE_FALSE(pending.empty());
    std::string redirect = authority.Approve(pending);
    uint16_t port = static_cast<uint16_t>(std::stoi(redirect.substr(17, redirect.find('/', 17) - 17)));
    CHECK(RawGet(port, redirect.substr(redirect.find('/', 17))).status == 200);
    session->Wait();
    CHECK(session->SignedIn());
}

TEST_CASE_FIXTURE(World, "the server refusing the code fails the sign-in") {
    authority.rejectCodes = true;
    SignInAndWait();
    CHECK(States() == std::vector<std::string> { "waiting_for_browser", "exchanging", "failed" });
    CHECK(FailureReason() == "rejected");
    CHECK_FALSE(session->SignedIn());
    CHECK_FALSE(secrets.Load());
}

TEST_CASE_FIXTURE(World, "going offline after the browser step fails the sign-in as a network problem") {
    browser = [this](const std::string& url) {
        authority.offline = true;
        std::string redirect = authority.Approve(url);
        uint16_t port = static_cast<uint16_t>(std::stoi(redirect.substr(17, redirect.find('/', 17) - 17)));
        return RawGet(port, redirect.substr(redirect.find('/', 17))).status == 200;
    };
    SignInAndWait();
    CHECK(FailureReason() == "network");
}

TEST_CASE_FIXTURE(World, "a failing game session fails the sign-in") {
    authority.failGameSession = true;
    SignInAndWait();
    CHECK(FailureReason() == "server");
    CHECK_FALSE(session->SignedIn());
    CHECK(signedIn.empty());
}

TEST_CASE_FIXTURE(World, "only one sign-in at a time, and none while signed in") {
    browser = [](const std::string&) { return true; };
    REQUIRE(session->SignIn() == SignInResult::Started);
    CHECK(session->SignIn() == SignInResult::Busy);
    session->Cancel();
    session->Wait();

    browser = [this](const std::string& url) {
        std::string redirect = authority.Approve(url);
        uint16_t port = static_cast<uint16_t>(std::stoi(redirect.substr(17, redirect.find('/', 17) - 17)));
        return RawGet(port, redirect.substr(redirect.find('/', 17))).status == 200;
    };
    SignInAndWait();
    CHECK(session->SignIn() == SignInResult::AlreadySignedIn);
}

TEST_CASE_FIXTURE(World, "a saved sign-in is picked up after a restart without the browser") {
    SignInAndWait();
    Restart();
    session->Restore();
    session->Wait();

    CHECK(urls.size() == 1); // only the first run used the browser
    REQUIRE(signedIn.size() == 1);
    CHECK(signedIn[0].username == "Racer");
    CHECK(session->SignedIn());
    CHECK(authority.refreshes == 1);
}

TEST_CASE_FIXTURE(World, "restoring rotates the refresh token in the secret store") {
    SignInAndWait();
    std::string before = *secrets.Load();
    Restart();
    session->Restore();
    session->Wait();
    CHECK(*secrets.Load() != before);
}

TEST_CASE_FIXTURE(World, "signing in while a saved sign-in is being restored waits for it, and finds the user signed in") {
    SignInAndWait();
    Restart();
    transport.handler = [this](const HttpRequest& request) {
        std::this_thread::sleep_for(150ms);
        return authority.Handle(request);
    };
    session->Restore();
    CHECK(session->SignIn() == SignInResult::AlreadySignedIn);
    CHECK(session->SignedIn());
    CHECK(urls.size() == 1);
}

TEST_CASE_FIXTURE(World, "with nothing saved, restoring does nothing") {
    session->Restore();
    session->Wait();
    CHECK(events.empty());
    CHECK(authority.tokenRequests == 0);
    CHECK_FALSE(session->SignedIn());
}

TEST_CASE_FIXTURE(World, "a saved sign-in the server no longer accepts is forgotten") {
    secrets.value = "bmp_rt_unknown";
    session->Restore();
    session->Wait();
    CHECK(FailureReason() == "expired");
    CHECK_FALSE(secrets.Load());
    CHECK_FALSE(session->SignedIn());
}

TEST_CASE_FIXTURE(World, "being offline at startup keeps the saved sign-in for next time") {
    SignInAndWait();
    Restart();
    authority.offline = true;
    session->Restore();
    session->Wait();
    CHECK(FailureReason() == "network");
    CHECK(secrets.Load());
    CHECK_FALSE(session->SignedIn());

    authority.offline = false;
    session->Restore();
    session->Wait();
    CHECK(session->SignedIn());
}

TEST_CASE_FIXTURE(World, "renewing gives a new game session for a signed-in user") {
    SignInAndWait();
    CHECK(session->Renew());
    REQUIRE(signedIn.size() == 2);
    CHECK(signedIn[1].publicKey == "public-2");
    CHECK(signedIn[1].publicKey != signedIn[0].publicKey);
    CHECK(authority.refreshes == 0); // the access token was still fresh
}

TEST_CASE_FIXTURE(World, "renewing does nothing for someone who is not signed in through the browser") {
    CHECK_FALSE(session->Renew());
    CHECK(authority.gameSessions == 0);
    CHECK(transport.requests.empty());
}

TEST_CASE_FIXTURE(World, "renewing refreshes an access token that is about to run out") {
    config.refreshMargin = 2h;
    Start();
    SignInAndWait();
    std::string before = *secrets.Load();
    CHECK(session->Renew());
    CHECK(authority.refreshes == 1);
    CHECK(*secrets.Load() != before);
    CHECK(signedIn.size() == 2);
}

TEST_CASE_FIXTURE(World, "many renewals at once never spend a refresh token twice") {
    config.refreshMargin = 2h;
    Start();
    SignInAndWait();

    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i) {
        threads.emplace_back([this] { session->Renew(); });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    CHECK_FALSE(authority.reuseDetected);
    CHECK(authority.refreshes == 8);
    CHECK(session->SignedIn());
    CHECK(signedIn.size() == 9);
}

TEST_CASE_FIXTURE(World, "a refresh token the server rejects ends the sign-in locally") {
    config.refreshMargin = 2h;
    Start();
    SignInAndWait();
    authority.Handle([] {
        HttpRequest revoke;
        revoke.method = "POST";
        revoke.url = "https://api.test/v1/oauth/revoke";
        return revoke;
    }());
    // revoke the family on the server side the way a reuse detection would
    HttpRequest request;
    request.method = "POST";
    request.url = "https://api.test/v1/oauth/revoke";
    request.body = "token=" + *secrets.Load();
    authority.Handle(request);

    CHECK(session->Renew());
    CHECK_FALSE(session->SignedIn());
    CHECK(signedOut == 1);
    CHECK_FALSE(secrets.Load());
    CHECK(FailureReason() == "expired");
}

TEST_CASE_FIXTURE(World, "a server that is down during renewal keeps the sign-in") {
    config.refreshMargin = 2h;
    Start();
    SignInAndWait();
    authority.offline = true;
    CHECK(session->Renew());
    CHECK(session->SignedIn());
    CHECK(secrets.Load());
    CHECK(signedIn.size() == 1);
}

TEST_CASE_FIXTURE(World, "signing out revokes the token, forgets the secret and tells the launcher") {
    SignInAndWait();
    std::string token = *secrets.Load();
    session->SignOut();
    CHECK_FALSE(session->SignedIn());
    CHECK_FALSE(secrets.Load());
    CHECK(signedOut == 1);
    CHECK(authority.revokes == 1);

    // the old refresh token is dead on the server too
    Restart();
    secrets.value = token;
    session->Restore();
    session->Wait();
    CHECK_FALSE(session->SignedIn());
}

TEST_CASE_FIXTURE(World, "signing out when nobody is signed in is quiet") {
    session->SignOut();
    CHECK(authority.revokes == 0);
    CHECK(transport.requests.empty());
}

TEST_CASE_FIXTURE(World, "signing out still works when the server cannot be reached") {
    SignInAndWait();
    authority.offline = true;
    session->SignOut();
    CHECK_FALSE(session->SignedIn());
    CHECK_FALSE(secrets.Load());
    CHECK(signedOut == 1);
}

TEST_CASE_FIXTURE(World, "signing out ends a sign-in that is waiting for the browser") {
    browser = [](const std::string&) { return true; };
    REQUIRE(session->SignIn() == SignInResult::Started);
    for (int i = 0; i < 300 && EventCount() == 0; ++i) {
        std::this_thread::sleep_for(10ms);
    }
    session->SignOut();
    CHECK_FALSE(session->Busy());
    CHECK_FALSE(session->SignedIn());
}

TEST_CASE_FIXTURE(World, "after signing out one can sign in again") {
    SignInAndWait();
    session->SignOut();
    SignInAndWait();
    CHECK(session->SignedIn());
    CHECK(signedIn.size() == 2);
}

TEST_CASE_FIXTURE(World, "destroying the session while it waits does not hang") {
    browser = [](const std::string&) { return true; };
    REQUIRE(session->SignIn() == SignInResult::Started);
    auto started = std::chrono::steady_clock::now();
    session.reset();
    CHECK(std::chrono::steady_clock::now() - started < 5s);
}
