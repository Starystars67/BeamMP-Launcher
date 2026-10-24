/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Auth/OAuth.h"
#include "AuthSupport.h"
#include <doctest/doctest.h>

using namespace Auth;

namespace {
HttpResponse Reply(long status, const std::string& body) {
    HttpResponse response;
    response.completed = true;
    response.status = status;
    response.body = body;
    return response;
}

Endpoints TestEndpoints() {
    Endpoints endpoints;
    endpoints.apiBase = "https://api.example/";
    endpoints.accountsBase = "https://accounts.example/";
    endpoints.clientId = "beammp-launcher";
    return endpoints;
}

struct Fixture {
    FakeTransport transport;
    OAuthApi api { transport, TestEndpoints() };
};

const char* GoodTokens = R"({"access_token":"bmp_at_1","token_type":"Bearer","expires_in":3600,"scope":"profile game","refresh_token":"bmp_rt_1"})";
}

TEST_CASE_FIXTURE(Fixture, "the authorize URL carries everything the Accounts site needs") {
    auto query = QueryOf(api.AuthorizeUrl("http://127.0.0.1:5000/callback", "state-1", "challenge-1"));
    CHECK(api.AuthorizeUrl("http://127.0.0.1:5000/callback", "s", "c").rfind("https://accounts.example/auth/external?", 0) == 0);
    CHECK(query["client_id"] == "beammp-launcher");
    CHECK(query["redirect_uri"] == "http://127.0.0.1:5000/callback");
    CHECK(query["response_type"] == "code");
    CHECK(query["scope"] == "profile game offline_access");
    CHECK(query["state"] == "state-1");
    CHECK(query["code_challenge"] == "challenge-1");
    CHECK(query["code_challenge_method"] == "S256");
}

TEST_CASE_FIXTURE(Fixture, "exchanging a code posts a form to the token endpoint") {
    transport.handler = [](const HttpRequest&) { return Reply(200, GoodTokens); };
    auto tokens = api.ExchangeCode("the code", "the-verifier", "http://127.0.0.1:5000/callback");
    REQUIRE(tokens);
    CHECK(tokens.value->accessToken == "bmp_at_1");
    CHECK(tokens.value->refreshToken == "bmp_rt_1");
    CHECK(tokens.value->expiresInSeconds == 3600);
    CHECK(tokens.value->scope == "profile game");

    REQUIRE(transport.requests.size() == 1);
    const auto& request = transport.requests[0];
    CHECK(request.method == "POST");
    CHECK(request.url == "https://api.example/v1/oauth/token");
    auto form = ParseForm(request.body);
    CHECK(form["grant_type"] == "authorization_code");
    CHECK(form["client_id"] == "beammp-launcher");
    CHECK(form["code"] == "the code");
    CHECK(form["code_verifier"] == "the-verifier");
    CHECK(form["redirect_uri"] == "http://127.0.0.1:5000/callback");
    CHECK(form.count("client_secret") == 0);
}

TEST_CASE_FIXTURE(Fixture, "refreshing posts the refresh token") {
    transport.handler = [](const HttpRequest&) { return Reply(200, GoodTokens); };
    REQUIRE(api.Refresh("bmp_rt_0"));
    auto form = ParseForm(transport.requests[0].body);
    CHECK(form["grant_type"] == "refresh_token");
    CHECK(form["refresh_token"] == "bmp_rt_0");
    CHECK(form["client_id"] == "beammp-launcher");
}

TEST_CASE_FIXTURE(Fixture, "a missing refresh token in the answer is allowed") {
    transport.handler = [](const HttpRequest&) { return Reply(200, R"({"access_token":"a","token_type":"Bearer","expires_in":60})"); };
    auto tokens = api.Refresh("x");
    REQUIRE(tokens);
    CHECK(tokens.value->refreshToken.empty());
}

TEST_CASE_FIXTURE(Fixture, "token failures are classified") {
    SUBCASE("offline") {
        transport.handler = [](const HttpRequest&) {
            HttpResponse down;
            down.error = "Could not resolve host";
            return down;
        };
        auto tokens = api.Refresh("x");
        CHECK_FALSE(tokens);
        CHECK(tokens.error.kind == ApiError::Kind::Network);
        CHECK(tokens.error.message == "Could not resolve host");
    }
    SUBCASE("rejected with an OAuth error") {
        transport.handler = [](const HttpRequest&) { return Reply(400, R"({"error":"invalid_grant","error_description":"expired"})"); };
        auto tokens = api.Refresh("x");
        CHECK(tokens.error.kind == ApiError::Kind::Rejected);
        CHECK(tokens.error.code == "invalid_grant");
        CHECK(tokens.error.message == "expired");
        CHECK(tokens.error.status == 400);
    }
    SUBCASE("rejected without a readable body") {
        transport.handler = [](const HttpRequest&) { return Reply(401, "<html>nope</html>"); };
        auto tokens = api.Refresh("x");
        CHECK(tokens.error.kind == ApiError::Kind::Rejected);
        CHECK(tokens.error.code.empty());
    }
    SUBCASE("server error") {
        transport.handler = [](const HttpRequest&) { return Reply(503, ""); };
        CHECK(api.Refresh("x").error.kind == ApiError::Kind::Server);
    }
    SUBCASE("a 200 that is not a token response") {
        for (const char* body : { "", "not json", "[]", R"({"access_token":"a"})", R"({"access_token":"a","token_type":"MAC"})", R"({"token_type":"Bearer"})" }) {
            transport.handler = [body](const HttpRequest&) { return Reply(200, body); };
            auto tokens = api.Refresh("x");
            CHECK_MESSAGE(!tokens, body);
            CHECK_MESSAGE(tokens.error.kind == ApiError::Kind::Malformed, body);
        }
    }
}

TEST_CASE_FIXTURE(Fixture, "a game session is requested with the bearer token and parsed") {
    transport.handler = [](const HttpRequest&) {
        return Reply(200, R"({"success":true,"message":"Welcome","private_key":"priv","public_key":"pub","username":"Racer","role":"EA","id":7,"groups":["USER","EA",5]})");
    };
    auto session = api.StartGameSession("bmp_at_1");
    REQUIRE(session);
    CHECK(session.value->username == "Racer");
    CHECK(session.value->role == "EA");
    CHECK(session.value->id == 7);
    CHECK(session.value->publicKey == "pub");
    CHECK(session.value->privateKey == "priv");
    CHECK(session.value->groups == std::vector<std::string> { "USER", "EA" });

    const auto& request = transport.requests[0];
    CHECK(request.method == "POST");
    CHECK(request.url == "https://api.example/v1/oauth/game-session");
    bool bearer = false;
    for (const auto& [name, value] : request.headers) {
        bearer = bearer || (name == "Authorization" && value == "Bearer bmp_at_1");
    }
    CHECK(bearer);
}

TEST_CASE_FIXTURE(Fixture, "game session failures are classified") {
    SUBCASE("forbidden carries the message") {
        transport.handler = [](const HttpRequest&) { return Reply(403, R"({"success":false,"error":"insufficient_scope","message":"No game scope"})"); };
        auto session = api.StartGameSession("t");
        CHECK(session.error.kind == ApiError::Kind::Rejected);
        CHECK(session.error.code == "insufficient_scope");
        CHECK(session.error.message == "No game scope");
    }
    SUBCASE("a success body without keys is malformed") {
        for (const char* body : { R"({"success":true})", R"({"success":false,"public_key":"a","private_key":"b","username":"c"})", R"({"success":true,"public_key":"a","username":"c"})" }) {
            transport.handler = [body](const HttpRequest&) { return Reply(200, body); };
            CHECK_MESSAGE(api.StartGameSession("t").error.kind == ApiError::Kind::Malformed, body);
        }
    }
}

TEST_CASE_FIXTURE(Fixture, "revoking posts the token and reports whether it was acknowledged") {
    transport.handler = [](const HttpRequest&) { return Reply(200, "{}"); };
    CHECK(api.Revoke("bmp_rt_9"));
    auto form = ParseForm(transport.requests[0].body);
    CHECK(transport.requests[0].url == "https://api.example/v1/oauth/revoke");
    CHECK(form["token"] == "bmp_rt_9");
    CHECK(form["client_id"] == "beammp-launcher");

    transport.handler = [](const HttpRequest&) { return Reply(500, ""); };
    CHECK_FALSE(api.Revoke("x"));
    transport.handler = [](const HttpRequest&) { return HttpResponse {}; };
    CHECK_FALSE(api.Revoke("x"));
}
