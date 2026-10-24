/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include "Auth/OAuth.h"
#include "Auth/SecretStore.h"
#include "Auth/Transport.h"
#include "Observers.h"
#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace Auth {

enum class SignInState {
    Idle,
    WaitingForBrowser,
    Exchanging,
    Failed,
};

const char* ToString(SignInState state);

enum class SignInResult {
    Started,
    /// Another sign-in is running.
    Busy,
    AlreadySignedIn,
};

/// Progress of a sign-in, for whoever shows it.
struct SignInEvent {
    SignInState state = SignInState::Idle;
    /// While waiting: the page the user has to open (when the browser did not open by itself).
    std::string url;
    /// On failure: `cancelled`, `timeout`, `denied`, `network`, `server`, `rejected`, `expired`, `internal`.
    std::string reason;
};

/// Signing in through the system browser (authorization code with PKCE on a
/// loopback redirect), staying signed in with a rotating refresh token kept
/// in the system's secret store, and turning that into the game session the
/// game needs. The password never reaches the launcher.
class Session {
public:
    struct Config {
        Endpoints endpoints;
        std::chrono::milliseconds signInTimeout = std::chrono::minutes(5);
        /// Refresh a little before the access token runs out.
        std::chrono::seconds refreshMargin = std::chrono::seconds(60);
    };

    /// Opens the page in the user's browser; false if that failed.
    using BrowserOpener = std::function<bool(const std::string& url)>;

    /// Called after every sign-in, restore and renewal with the game session to use.
    using SignedInHandler = std::function<void(const GameSession& session)>;
    using SignedOutHandler = std::function<void()>;

    Session(Config config, Transport& transport, SecretStore& secrets, BrowserOpener openBrowser);
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    void OnSignedIn(SignedInHandler handler);
    void OnSignedOut(SignedOutHandler handler);

    Observers<const SignInEvent&>::Id AddObserver(Observers<const SignInEvent&>::Callback callback);
    void RemoveObserver(Observers<const SignInEvent&>::Id id);

    /// Starts a sign-in in the background. A restore of a saved sign-in that is
    /// still running is waited for first: it may well sign the user in.
    SignInResult SignIn();

    /// Ends a sign-in that is waiting for the browser.
    void Cancel();

    /// Forgets the sign-in everywhere: the stored secret, the tokens, and (best effort) on the server.
    void SignOut();

    /// Picks up a saved sign-in in the background, if there is one.
    void Restore();

    /// Gets a fresh game session for a signed-in user (the keys are single use
    /// per server join). Returns false when this is not an OAuth sign-in.
    bool Renew();

    bool SignedIn() const;
    bool Busy() const;

    /// Waits for the background work to finish.
    void Wait();

private:
    enum class Mode { SignIn, Restore };

    void Run(Mode mode);
    void RunSignIn();
    void RunRestore();
    void StartWorker(Mode mode);
    void Emit(SignInState state, std::string url = {}, std::string reason = {});
    void Fail(const std::string& reason);
    /// `savedSignIn`: the error is about a refresh token, so "invalid" means the sign-in has ended.
    static std::string ReasonFor(const ApiError& error, bool savedSignIn = false);

    /// Tokens of this sign-in, valid for a game session (refreshing them if needed).
    bool Complete(const Tokens& tokens);
    std::optional<std::string> FreshAccessToken(ApiError* error);
    void ForgetLocally();

    Config m_config;
    OAuthApi m_api;
    SecretStore& m_secrets;
    BrowserOpener m_openBrowser;

    mutable std::mutex m_mutex;
    // Refresh tokens are single use: only one refresh may be in flight.
    std::mutex m_refreshMutex;
    std::string m_accessToken;
    std::chrono::steady_clock::time_point m_accessExpiry;
    std::string m_refreshToken;
    bool m_signedIn = false;
    bool m_busy = false;
    bool m_restoring = false;

    std::atomic<bool> m_cancel = false;
    std::thread m_worker;

    SignedInHandler m_onSignedIn;
    SignedOutHandler m_onSignedOut;
    Observers<const SignInEvent&> m_observers;
};

}
