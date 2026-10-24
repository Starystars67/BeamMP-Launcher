/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Auth/Session.h"

#include "Auth/CallbackServer.h"
#include "Auth/Pkce.h"
#include "Logger.h"

namespace Auth {

const char* ToString(SignInState state) {
    switch (state) {
    case SignInState::Idle:
        return "idle";
    case SignInState::WaitingForBrowser:
        return "waiting_for_browser";
    case SignInState::Exchanging:
        return "exchanging";
    case SignInState::Failed:
        return "failed";
    }
    return "idle";
}

Session::Session(Config config, Transport& transport, SecretStore& secrets, BrowserOpener openBrowser)
    : m_config(std::move(config))
    , m_api(transport, m_config.endpoints)
    , m_secrets(secrets)
    , m_openBrowser(std::move(openBrowser)) { }

Session::~Session() {
    m_cancel = true;
    Wait();
}

void Session::OnSignedIn(SignedInHandler handler) {
    m_onSignedIn = std::move(handler);
}

void Session::OnSignedOut(SignedOutHandler handler) {
    m_onSignedOut = std::move(handler);
}

Observers<const SignInEvent&>::Id Session::AddObserver(Observers<const SignInEvent&>::Callback callback) {
    return m_observers.Add(std::move(callback));
}

void Session::RemoveObserver(Observers<const SignInEvent&>::Id id) {
    m_observers.Remove(id);
}

bool Session::SignedIn() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_signedIn;
}

bool Session::Busy() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_busy;
}

void Session::Wait() {
    if (m_worker.joinable()) {
        m_worker.join();
    }
}

void Session::Emit(SignInState state, std::string url, std::string reason) {
    m_observers.Notify(SignInEvent { state, std::move(url), std::move(reason) });
}

void Session::Fail(const std::string& reason) {
    Emit(SignInState::Failed, {}, reason);
}

std::string Session::ReasonFor(const ApiError& error, bool savedSignIn) {
    switch (error.kind) {
    case ApiError::Kind::Network:
        return "network";
    case ApiError::Kind::Server:
        return "server";
    case ApiError::Kind::Rejected:
        return savedSignIn && (error.code == "invalid_grant" || error.code == "invalid_token") ? "expired" : "rejected";
    case ApiError::Kind::Malformed:
        return "server";
    }
    return "internal";
}

SignInResult Session::SignIn() {
    bool restoring;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        restoring = m_restoring;
    }
    if (restoring) {
        Wait();
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_signedIn) {
            return SignInResult::AlreadySignedIn;
        }
        if (m_busy) {
            return SignInResult::Busy;
        }
        m_busy = true;
    }
    StartWorker(Mode::SignIn);
    return SignInResult::Started;
}

void Session::Restore() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_busy || m_signedIn) {
            return;
        }
        m_busy = true;
        m_restoring = true;
    }
    StartWorker(Mode::Restore);
}

void Session::StartWorker(Mode mode) {
    Wait();
    m_cancel = false;
    m_worker = std::thread([this, mode] { Run(mode); });
}

void Session::Run(Mode mode) {
    try {
        if (mode == Mode::SignIn) {
            RunSignIn();
        } else {
            RunRestore();
        }
    } catch (const std::exception& e) {
        error(std::string("Sign-in failed: ") + e.what());
        Fail("internal");
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    m_busy = false;
    m_restoring = false;
}

void Session::Cancel() {
    m_cancel = true;
}

void Session::RunSignIn() {
    Pkce pkce = GeneratePkce();
    std::string state = RandomToken(16);

    CallbackServer callbacks(state);
    uint16_t port = 0;
    try {
        port = callbacks.Start();
    } catch (const std::exception& e) {
        error(std::string("Sign-in: ") + e.what());
        Fail("internal");
        return;
    }

    std::string redirectUri = "http://127.0.0.1:" + std::to_string(port) + "/callback";
    std::string url = m_api.AuthorizeUrl(redirectUri, state, pkce.challenge);
    Emit(SignInState::WaitingForBrowser, url);
    if (m_openBrowser && !m_openBrowser(url)) {
        warn("The browser could not be opened for the sign-in");
    }

    auto callback = callbacks.Wait(m_config.signInTimeout, m_cancel);
    callbacks.Stop();
    if (m_cancel) {
        Fail("cancelled");
        return;
    }
    if (!callback) {
        Fail("timeout");
        return;
    }
    if (!callback->error.empty()) {
        Fail(callback->error == "access_denied" ? "denied" : "rejected");
        return;
    }

    Emit(SignInState::Exchanging);
    auto tokens = m_api.ExchangeCode(callback->code, pkce.verifier, redirectUri);
    if (!tokens) {
        error("Sign-in: the code could not be exchanged: " + tokens.error.code + " " + tokens.error.message);
        Fail(ReasonFor(tokens.error));
        return;
    }
    if (!Complete(*tokens.value)) {
        return;
    }
    Emit(SignInState::Idle);
}

void Session::RunRestore() {
    auto saved = m_secrets.Load();
    if (!saved) {
        return;
    }

    std::lock_guard<std::mutex> refreshing(m_refreshMutex);
    auto tokens = m_api.Refresh(*saved);
    if (!tokens) {
        if (tokens.error.kind == ApiError::Kind::Rejected) {
            info("The saved sign-in is no longer valid");
            m_secrets.Erase();
        } else {
            warn("The saved sign-in could not be restored: " + tokens.error.message);
        }
        Fail(ReasonFor(tokens.error, true));
        return;
    }
    if (tokens.value->refreshToken.empty()) {
        tokens.value->refreshToken = *saved;
    }
    Complete(*tokens.value);
}

bool Session::Complete(const Tokens& tokens) {
    auto game = m_api.StartGameSession(tokens.accessToken);
    if (!game) {
        error("Sign-in: no game session: " + game.error.code + " " + game.error.message);
        Fail(ReasonFor(game.error));
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_accessToken = tokens.accessToken;
        m_accessExpiry = std::chrono::steady_clock::now() + std::chrono::seconds(tokens.expiresInSeconds);
        if (!tokens.refreshToken.empty()) {
            m_refreshToken = tokens.refreshToken;
        }
        m_signedIn = true;
    }
    if (!tokens.refreshToken.empty() && !m_secrets.Save(tokens.refreshToken)) {
        warn("The sign-in could not be saved to the system keychain; you will have to sign in again next time");
    }
    if (m_onSignedIn) {
        m_onSignedIn(*game.value);
    }
    return true;
}

// A usable access token, refreshed (and the refresh token rotated) when it is about to expire.
std::optional<std::string> Session::FreshAccessToken(ApiError* failure) {
    std::lock_guard<std::mutex> refreshing(m_refreshMutex);
    std::string refreshToken;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_signedIn) {
            return std::nullopt;
        }
        if (!m_accessToken.empty() && std::chrono::steady_clock::now() + m_config.refreshMargin < m_accessExpiry) {
            return m_accessToken;
        }
        refreshToken = m_refreshToken;
    }

    auto tokens = m_api.Refresh(refreshToken);
    if (!tokens) {
        *failure = tokens.error;
        return std::nullopt;
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_accessToken = tokens.value->accessToken;
        m_accessExpiry = std::chrono::steady_clock::now() + std::chrono::seconds(tokens.value->expiresInSeconds);
        if (!tokens.value->refreshToken.empty()) {
            m_refreshToken = tokens.value->refreshToken;
        }
    }
    if (!tokens.value->refreshToken.empty() && !m_secrets.Save(tokens.value->refreshToken)) {
        warn("The refreshed sign-in could not be saved to the system keychain");
    }
    return tokens.value->accessToken;
}

bool Session::Renew() {
    if (!SignedIn()) {
        return false;
    }
    ApiError failure;
    auto accessToken = FreshAccessToken(&failure);
    if (!accessToken) {
        if (failure.kind == ApiError::Kind::Rejected) {
            warn("The sign-in has ended, please sign in again");
            ForgetLocally();
            Fail("expired");
        } else {
            error("The sign-in could not be renewed: " + failure.message);
        }
        return true;
    }

    auto game = m_api.StartGameSession(*accessToken);
    if (!game) {
        error("No game session could be started: " + game.error.code + " " + game.error.message);
        if (game.error.kind == ApiError::Kind::Rejected && game.error.status == 401) {
            // The access token was revoked early: next time it is refreshed.
            std::lock_guard<std::mutex> lock(m_mutex);
            m_accessToken.clear();
        }
        return true;
    }
    if (m_onSignedIn) {
        m_onSignedIn(*game.value);
    }
    return true;
}

void Session::ForgetLocally() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_signedIn = false;
        m_accessToken.clear();
        m_refreshToken.clear();
    }
    m_secrets.Erase();
    if (m_onSignedOut) {
        m_onSignedOut();
    }
}

void Session::SignOut() {
    m_cancel = true;
    Wait();

    std::string refreshToken;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        refreshToken = m_refreshToken;
    }
    if (refreshToken.empty()) {
        refreshToken = m_secrets.Load().value_or("");
    }
    bool wasSignedIn = SignedIn();
    ForgetLocally();
    if (!wasSignedIn && refreshToken.empty()) {
        return;
    }
    if (!refreshToken.empty() && !m_api.Revoke(refreshToken)) {
        warn("The sign-in could not be revoked on the server, it ends by itself");
    }
}

}
