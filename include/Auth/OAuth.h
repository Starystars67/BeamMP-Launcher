/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include "Auth/Transport.h"
#include <optional>
#include <string>
#include <vector>

namespace Auth {

/// Where the launcher signs in. The defaults are the live BeamMP services.
struct Endpoints {
    std::string accountsBase = "https://portal.beammp.com";
    std::string apiBase = "https://auth.beammp.com";
    std::string clientId = "beammp-launcher";
};

/// https, or plain http to a loopback host (development servers); nothing else.
bool IsAcceptableBaseUrl(const std::string& url);

/// Scopes the launcher asks for: profile, a game session, and a refresh token.
inline constexpr const char* LauncherScope = "profile game offline_access";

struct ApiError {
    enum class Kind {
        /// Nothing arrived: offline, DNS, TLS, timeout.
        Network,
        /// The service said no (4xx): bad grant, revoked, not allowed.
        Rejected,
        /// 5xx.
        Server,
        /// An answer that is not what the protocol promises.
        Malformed,
    };

    Kind kind = Kind::Network;
    long status = 0;
    /// OAuth error code when there is one (`invalid_grant`, `invalid_token`, ...).
    std::string code;
    std::string message;
};

template <typename T>
struct Outcome {
    std::optional<T> value;
    ApiError error;

    explicit operator bool() const { return value.has_value(); }
};

struct Tokens {
    std::string accessToken;
    std::string refreshToken;
    int expiresInSeconds = 0;
    std::string scope;
};

/// What the game needs after a sign-in; same content as the legacy login result.
struct GameSession {
    std::string username;
    std::string role;
    int id = -1;
    std::string publicKey;
    std::string privateKey;
    std::vector<std::string> groups;
};

/// The OAuth calls the launcher makes (RFC 6749, 7009, 7636, 8252).
class OAuthApi {
public:
    OAuthApi(Transport& transport, Endpoints endpoints)
        : m_transport(transport)
        , m_endpoints(std::move(endpoints)) { }

    /// The page the user is sent to; the Accounts site shows it after signing in.
    std::string AuthorizeUrl(const std::string& redirectUri, const std::string& state, const std::string& challenge) const;

    Outcome<Tokens> ExchangeCode(const std::string& code, const std::string& verifier, const std::string& redirectUri);
    Outcome<Tokens> Refresh(const std::string& refreshToken);
    Outcome<GameSession> StartGameSession(const std::string& accessToken);

    /// Best effort: true when the service acknowledged it.
    bool Revoke(const std::string& token);

private:
    Outcome<Tokens> TokenRequest(const std::string& form);

    Transport& m_transport;
    Endpoints m_endpoints;
};

/// RFC 3986 percent-encoding (everything but unreserved characters).
std::string UrlEncode(const std::string& value);

}
