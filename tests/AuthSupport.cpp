/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "AuthSupport.h"
#include "Auth/Pkce.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <nlohmann/json.hpp>
#include <sys/socket.h>
#include <unistd.h>

namespace {
std::string Decode(const std::string& text) {
    std::string result;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size()) {
            result += static_cast<char>(std::stoi(text.substr(i + 1, 2), nullptr, 16));
            i += 2;
        } else if (text[i] == '+') {
            result += ' ';
        } else {
            result += text[i];
        }
    }
    return result;
}
}

std::map<std::string, std::string> ParseForm(const std::string& body) {
    std::map<std::string, std::string> result;
    size_t start = 0;
    while (start <= body.size()) {
        size_t end = body.find('&', start);
        std::string pair = body.substr(start, end == std::string::npos ? std::string::npos : end - start);
        size_t equals = pair.find('=');
        if (!pair.empty()) {
            result[Decode(pair.substr(0, equals))] = equals == std::string::npos ? "" : Decode(pair.substr(equals + 1));
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return result;
}

std::map<std::string, std::string> QueryOf(const std::string& url) {
    size_t question = url.find('?');
    return question == std::string::npos ? std::map<std::string, std::string>() : ParseForm(url.substr(question + 1));
}

RawResponse RawGet(uint16_t port, const std::string& target, const std::string& host) {
    RawResponse response;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    if (connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        close(fd);
        return response;
    }
    std::string hostHeader = host.empty() ? "127.0.0.1:" + std::to_string(port) : host;
    std::string request = "GET " + target + " HTTP/1.1\r\nHost: " + hostHeader + "\r\nConnection: close\r\n\r\n";
    send(fd, request.data(), request.size(), 0);
    std::string raw;
    char chunk[4096];
    ssize_t received;
    while ((received = recv(fd, chunk, sizeof(chunk), 0)) > 0) {
        raw.append(chunk, static_cast<size_t>(received));
    }
    close(fd);
    if (raw.rfind("HTTP/1.1 ", 0) == 0) {
        response.status = std::stoi(raw.substr(9, 3));
    }
    size_t bodyStart = raw.find("\r\n\r\n");
    response.body = bodyStart == std::string::npos ? "" : raw.substr(bodyStart + 4);
    return response;
}

bool MemorySecretStore::Save(const std::string& secret) {
    std::lock_guard<std::mutex> lock(mutex);
    if (failSaves) {
        return false;
    }
    value = secret;
    ++saves;
    return true;
}

std::optional<std::string> MemorySecretStore::Load() {
    std::lock_guard<std::mutex> lock(mutex);
    return value;
}

void MemorySecretStore::Erase() {
    std::lock_guard<std::mutex> lock(mutex);
    value.reset();
    ++erases;
}

Auth::HttpResponse FakeTransport::Send(const Auth::HttpRequest& request) {
    {
        std::lock_guard<std::mutex> lock(mutex);
        requests.push_back(request);
    }
    return handler ? handler(request) : Auth::HttpResponse {};
}

using nlohmann::json;

FakeAuthority::FakeAuthority() = default;

Auth::HttpResponse FakeAuthority::Json(long status, const std::string& body) {
    Auth::HttpResponse response;
    response.completed = true;
    response.status = status;
    response.body = body;
    return response;
}

std::string FakeAuthority::IssueRefresh(const std::string& family) {
    std::string token = "bmp_rt_" + std::to_string(++m_counter);
    m_refreshFamily[token] = family;
    return token;
}

std::string FakeAuthority::Approve(const std::string& authorizeUrl) {
    auto query = QueryOf(authorizeUrl);
    std::lock_guard<std::mutex> lock(mutex);
    lastAuthorizeScope = query["scope"];
    std::string code = "code-" + std::to_string(++m_counter);
    m_codes[code] = { query["code_challenge"], query["redirect_uri"] };
    return query["redirect_uri"] + "?code=" + code + "&state=" + query["state"];
}

Auth::HttpResponse FakeAuthority::TokenEndpoint(const std::map<std::string, std::string>& form) {
    ++tokenRequests;
    auto field = [&](const char* key) {
        auto it = form.find(key);
        return it == form.end() ? std::string() : it->second;
    };
    auto tokens = [&](const std::string& refresh) {
        std::string access = "bmp_at_" + std::to_string(++m_counter);
        m_accessTokens.insert(access);
        return Json(200, json { { "access_token", access }, { "token_type", "Bearer" }, { "expires_in", 3600 }, { "scope", "profile game offline_access" }, { "refresh_token", refresh } }.dump());
    };

    if (field("grant_type") == "authorization_code") {
        auto it = m_codes.find(field("code"));
        if (rejectCodes || it == m_codes.end()) {
            return Json(400, R"({"error":"invalid_grant","error_description":"bad code"})");
        }
        CodeRecord record = it->second;
        m_codes.erase(it);
        if (record.redirectUri != field("redirect_uri") || Auth::ChallengeFor(field("code_verifier")) != record.challenge) {
            return Json(400, R"({"error":"invalid_grant","error_description":"PKCE or redirect mismatch"})");
        }
        return tokens(IssueRefresh("family-" + std::to_string(m_counter)));
    }

    if (field("grant_type") == "refresh_token") {
        ++refreshes;
        std::string presented = field("refresh_token");
        auto it = m_refreshFamily.find(presented);
        if (it == m_refreshFamily.end() || m_revokedFamilies.count(it->second)) {
            return Json(400, R"({"error":"invalid_grant","error_description":"invalid"})");
        }
        if (m_spent.count(presented)) {
            reuseDetected = true;
            m_revokedFamilies.insert(it->second);
            return Json(400, R"({"error":"invalid_grant","error_description":"reused"})");
        }
        m_spent.insert(presented);
        return tokens(IssueRefresh(it->second));
    }
    return Json(400, R"({"error":"unsupported_grant_type"})");
}

Auth::HttpResponse FakeAuthority::Handle(const Auth::HttpRequest& request) {
    std::lock_guard<std::mutex> lock(mutex);
    if (offline) {
        Auth::HttpResponse down;
        down.error = "Could not resolve host";
        return down;
    }
    auto path = [&](const char* suffix) { return request.url.size() >= std::string(suffix).size() && request.url.compare(request.url.size() - std::string(suffix).size(), std::string::npos, suffix) == 0; };

    if (path("/v1/oauth/token")) {
        return TokenEndpoint(ParseForm(request.body));
    }
    if (path("/v1/oauth/revoke")) {
        ++revokes;
        auto form = ParseForm(request.body);
        auto it = m_refreshFamily.find(form["token"]);
        if (it != m_refreshFamily.end()) {
            m_revokedFamilies.insert(it->second);
        }
        return Json(200, "{}");
    }
    if (path("/v1/oauth/game-session")) {
        ++gameSessions;
        std::string bearer;
        for (const auto& [name, value] : request.headers) {
            if (name == "Authorization" && value.rfind("Bearer ", 0) == 0) {
                bearer = value.substr(7);
            }
        }
        if (!m_accessTokens.count(bearer)) {
            return Json(401, R"({"success":false,"error":"invalid_token","message":"The access token is invalid"})");
        }
        if (failGameSession) {
            return Json(500, R"({"success":false})");
        }
        return Json(200, json { { "success", true }, { "private_key", "private-" + std::to_string(gameSessions) }, { "public_key", "public-" + std::to_string(gameSessions) }, { "username", "Racer" }, { "role", "EA" }, { "id", 7 }, { "groups", json::array({ "USER", "EA" }) } }.dump());
    }
    return Json(404, "{}");
}
