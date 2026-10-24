/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include "Auth/OAuth.h"
#include "Auth/SecretStore.h"
#include "Auth/Transport.h"
#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

/// Query parameters of a URL, percent-decoded.
std::map<std::string, std::string> QueryOf(const std::string& url);

/// A plain HTTP GET over a raw socket, so the Host header can be chosen. Returns "status\nbody".
struct RawResponse {
    int status = 0;
    std::string body;
};
RawResponse RawGet(uint16_t port, const std::string& target, const std::string& host = "");

class MemorySecretStore : public Auth::SecretStore {
public:
    bool Save(const std::string& secret) override;
    std::optional<std::string> Load() override;
    void Erase() override;

    mutable std::mutex mutex;
    std::optional<std::string> value;
    bool failSaves = false;
    int saves = 0;
    int erases = 0;
};

/// Transport that answers with whatever the test says, and remembers the requests.
class FakeTransport : public Auth::Transport {
public:
    Auth::HttpResponse Send(const Auth::HttpRequest& request) override;

    std::function<Auth::HttpResponse(const Auth::HttpRequest&)> handler;

    std::mutex mutex;
    std::vector<Auth::HttpRequest> requests;
};

/// A stand-in for the BeamMP API's OAuth endpoints with the behaviour that
/// matters: PKCE, single-use codes, rotating refresh tokens with reuse
/// detection (a spent token revokes the whole family), game sessions.
class FakeAuthority {
public:
    FakeAuthority();

    Auth::HttpResponse Handle(const Auth::HttpRequest& request);

    /// What the user's approval in the browser does: returns the redirect URL
    /// (with code and state) for an authorize URL.
    std::string Approve(const std::string& authorizeUrl);

    std::mutex mutex;
    bool offline = false;
    bool rejectCodes = false;
    bool failGameSession = false;
    int gameSessions = 0;
    int tokenRequests = 0;
    int refreshes = 0;
    int revokes = 0;
    bool reuseDetected = false;
    std::string lastAuthorizeScope;

private:
    struct CodeRecord {
        std::string challenge;
        std::string redirectUri;
    };

    std::string IssueRefresh(const std::string& family);
    Auth::HttpResponse Json(long status, const std::string& body);
    Auth::HttpResponse TokenEndpoint(const std::map<std::string, std::string>& form);

    int m_counter = 0;
    std::map<std::string, CodeRecord> m_codes;
    std::map<std::string, std::string> m_refreshFamily;
    std::set<std::string> m_spent;
    std::set<std::string> m_revokedFamilies;
    std::set<std::string> m_accessTokens;
};

std::map<std::string, std::string> ParseForm(const std::string& body);
