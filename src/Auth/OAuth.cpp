/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Auth/OAuth.h"

#include <nlohmann/json.hpp>

namespace Auth {

namespace {
    using nlohmann::json;

    std::string TrimTrailingSlashes(std::string url) {
        while (!url.empty() && url.back() == '/') {
            url.pop_back();
        }
        return url;
    }

    std::string StringField(const json& object, const char* key) {
        auto it = object.find(key);
        return it != object.end() && it->is_string() ? it->get<std::string>() : std::string();
    }

    ApiError ErrorFor(const HttpResponse& response) {
        ApiError error;
        error.status = response.status;
        if (!response.completed) {
            error.kind = ApiError::Kind::Network;
            error.message = response.error;
            return error;
        }
        error.kind = response.status >= 500 ? ApiError::Kind::Server : ApiError::Kind::Rejected;
        json body = json::parse(response.body, nullptr, false);
        if (body.is_object()) {
            error.code = StringField(body, "error");
            error.message = StringField(body, "error_description");
            if (error.message.empty()) {
                error.message = StringField(body, "message");
            }
        }
        return error;
    }

    ApiError Malformed(const HttpResponse& response, const char* what) {
        ApiError error;
        error.kind = ApiError::Kind::Malformed;
        error.status = response.status;
        error.message = what;
        return error;
    }

    bool IsLoopbackHost(const std::string& host) {
        return host == "127.0.0.1" || host == "localhost" || host == "[::1]";
    }
}

std::string UrlEncode(const std::string& value) {
    static const char Hex[] = "0123456789ABCDEF";
    std::string result;
    for (unsigned char c : value) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' || c == '~') {
            result += static_cast<char>(c);
        } else {
            result += '%';
            result += Hex[c >> 4];
            result += Hex[c & 15];
        }
    }
    return result;
}

bool IsAcceptableBaseUrl(const std::string& url) {
    auto schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos) {
        return false;
    }
    std::string scheme = url.substr(0, schemeEnd);
    std::string rest = url.substr(schemeEnd + 3);
    auto pathStart = rest.find_first_of("/?#");
    std::string authority = rest.substr(0, pathStart);
    if (authority.empty() || authority.find('@') != std::string::npos || authority.find(' ') != std::string::npos) {
        return false;
    }
    if (pathStart != std::string::npos && rest.find_first_of("?#", pathStart) != std::string::npos) {
        return false;
    }
    if (scheme == "https") {
        return true;
    }
    if (scheme != "http") {
        return false;
    }
    // [::1]:port, or host:port
    std::string host = authority.front() == '[' ? authority.substr(0, authority.find(']') + 1) : authority.substr(0, authority.find(':'));
    return IsLoopbackHost(host);
}

std::string OAuthApi::AuthorizeUrl(const std::string& redirectUri, const std::string& state, const std::string& challenge) const {
    return TrimTrailingSlashes(m_endpoints.accountsBase) + "/auth/external"
        + "?client_id=" + UrlEncode(m_endpoints.clientId)
        + "&redirect_uri=" + UrlEncode(redirectUri)
        + "&response_type=code"
        + "&scope=" + UrlEncode(LauncherScope)
        + "&state=" + UrlEncode(state)
        + "&code_challenge=" + UrlEncode(challenge)
        + "&code_challenge_method=S256";
}

Outcome<Tokens> OAuthApi::TokenRequest(const std::string& form) {
    HttpRequest request;
    request.method = "POST";
    request.url = TrimTrailingSlashes(m_endpoints.apiBase) + "/v1/oauth/token";
    request.headers = { { "Content-Type", "application/x-www-form-urlencoded" }, { "Accept", "application/json" } };
    request.body = form;

    HttpResponse response = m_transport.Send(request);
    Outcome<Tokens> outcome;
    if (!response.completed || response.status != 200) {
        outcome.error = ErrorFor(response);
        return outcome;
    }

    json body = json::parse(response.body, nullptr, false);
    if (!body.is_object() || StringField(body, "access_token").empty() || StringField(body, "token_type") != "Bearer") {
        outcome.error = Malformed(response, "The token response is not valid");
        return outcome;
    }
    Tokens tokens;
    tokens.accessToken = StringField(body, "access_token");
    tokens.refreshToken = StringField(body, "refresh_token");
    tokens.scope = StringField(body, "scope");
    auto expires = body.find("expires_in");
    tokens.expiresInSeconds = expires != body.end() && expires->is_number_integer() ? expires->get<int>() : 0;
    outcome.value = std::move(tokens);
    return outcome;
}

Outcome<Tokens> OAuthApi::ExchangeCode(const std::string& code, const std::string& verifier, const std::string& redirectUri) {
    return TokenRequest("grant_type=authorization_code"
        "&client_id=" + UrlEncode(m_endpoints.clientId)
        + "&code=" + UrlEncode(code)
        + "&code_verifier=" + UrlEncode(verifier)
        + "&redirect_uri=" + UrlEncode(redirectUri));
}

Outcome<Tokens> OAuthApi::Refresh(const std::string& refreshToken) {
    return TokenRequest("grant_type=refresh_token"
        "&client_id=" + UrlEncode(m_endpoints.clientId)
        + "&refresh_token=" + UrlEncode(refreshToken));
}

Outcome<GameSession> OAuthApi::StartGameSession(const std::string& accessToken) {
    HttpRequest request;
    request.method = "POST";
    request.url = TrimTrailingSlashes(m_endpoints.apiBase) + "/v1/oauth/game-session";
    request.headers = { { "Authorization", "Bearer " + accessToken }, { "Accept", "application/json" }, { "Content-Type", "application/json" } };
    request.body = "{}";

    HttpResponse response = m_transport.Send(request);
    Outcome<GameSession> outcome;
    if (!response.completed || response.status != 200) {
        outcome.error = ErrorFor(response);
        return outcome;
    }

    json body = json::parse(response.body, nullptr, false);
    if (!body.is_object() || body.value("success", false) != true || StringField(body, "public_key").empty()
        || StringField(body, "private_key").empty() || StringField(body, "username").empty()) {
        outcome.error = Malformed(response, "The game session response is not valid");
        return outcome;
    }
    GameSession session;
    session.username = StringField(body, "username");
    session.role = StringField(body, "role");
    session.publicKey = StringField(body, "public_key");
    session.privateKey = StringField(body, "private_key");
    auto id = body.find("id");
    session.id = id != body.end() && id->is_number_integer() ? id->get<int>() : -1;
    auto groups = body.find("groups");
    if (groups != body.end() && groups->is_array()) {
        for (const auto& group : *groups) {
            if (group.is_string()) {
                session.groups.push_back(group.get<std::string>());
            }
        }
    }
    outcome.value = std::move(session);
    return outcome;
}

bool OAuthApi::Revoke(const std::string& token) {
    HttpRequest request;
    request.method = "POST";
    request.url = TrimTrailingSlashes(m_endpoints.apiBase) + "/v1/oauth/revoke";
    request.headers = { { "Content-Type", "application/x-www-form-urlencoded" }, { "Accept", "application/json" } };
    request.body = "client_id=" + UrlEncode(m_endpoints.clientId) + "&token=" + UrlEncode(token);
    request.timeoutSeconds = 8;
    HttpResponse response = m_transport.Send(request);
    return response.completed && response.status == 200;
}

}
