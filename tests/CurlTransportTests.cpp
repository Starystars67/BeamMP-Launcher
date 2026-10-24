/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Auth/Transport.h"
#include <chrono>
#include <doctest/doctest.h>
#include <httplib.h>
#include <thread>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace Auth;

namespace {
struct Server {
    httplib::Server http;
    std::thread thread;
    int port = 0;
    std::string lastBody;
    std::string lastMethod;
    std::string lastAuthorization;

    Server() {
        http.Post("/echo", [this](const httplib::Request& request, httplib::Response& response) {
            lastBody = request.body;
            lastMethod = request.method;
            lastAuthorization = request.get_header_value("Authorization");
            response.set_content("{\"ok\":true}", "application/json");
        });
        http.Get("/status/:code", [](const httplib::Request& request, httplib::Response& response) {
            response.status = std::stoi(request.path_params.at("code"));
            response.set_content("{\"error\":\"nope\"}", "application/json");
        });
        http.Get("/redirect", [](const httplib::Request&, httplib::Response& response) { response.set_redirect("/echo"); });
        http.Get("/slow", [](const httplib::Request&, httplib::Response& response) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1800));
            response.set_content("late", "text/plain");
        });
        http.Get("/huge", [](const httplib::Request&, httplib::Response& response) { response.set_content(std::string(2 * 1024 * 1024, 'x'), "text/plain"); });
        port = http.bind_to_any_port("127.0.0.1");
        thread = std::thread([this] { http.listen_after_bind(); });
        http.wait_until_ready();
    }

    ~Server() {
        http.stop();
        thread.join();
    }

    std::string Url(const std::string& path) const { return "http://127.0.0.1:" + std::to_string(port) + path; }
};
}

TEST_CASE("a POST carries its body and headers and returns the answer") {
    Server server;
    auto transport = MakeCurlTransport();
    HttpRequest request;
    request.method = "POST";
    request.url = server.Url("/echo");
    request.headers = { { "Authorization", "Bearer abc" }, { "Content-Type", "application/x-www-form-urlencoded" } };
    request.body = "a=1&b=2";
    auto response = transport->Send(request);
    CHECK(response.completed);
    CHECK(response.status == 200);
    CHECK(response.body == "{\"ok\":true}");
    CHECK(server.lastBody == "a=1&b=2");
    CHECK(server.lastMethod == "POST");
    CHECK(server.lastAuthorization == "Bearer abc");
}

TEST_CASE("an HTTP error status is still a completed answer with its body") {
    Server server;
    HttpRequest request;
    request.url = server.Url("/status/400");
    auto response = MakeCurlTransport()->Send(request);
    CHECK(response.completed);
    CHECK(response.status == 400);
    CHECK(response.body == "{\"error\":\"nope\"}");
}

TEST_CASE("redirects are not followed") {
    Server server;
    HttpRequest request;
    request.url = server.Url("/redirect");
    auto response = MakeCurlTransport()->Send(request);
    CHECK(response.completed);
    CHECK(response.status == 302);
}

TEST_CASE("nobody listening is a transport failure") {
    // A port that was free a moment ago: bind it, read the number, close it again.
    int probe = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address {};
    address.sin_family = AF_INET;
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    bind(probe, reinterpret_cast<sockaddr*>(&address), sizeof(address));
    socklen_t length = sizeof(address);
    getsockname(probe, reinterpret_cast<sockaddr*>(&address), &length);
    int port = ntohs(address.sin_port);
    close(probe);
    HttpRequest request;
    request.url = "http://127.0.0.1:" + std::to_string(port) + "/";
    auto response = MakeCurlTransport()->Send(request);
    CHECK_FALSE(response.completed);
    CHECK_FALSE(response.error.empty());
}

TEST_CASE("a slow server hits the timeout") {
    Server server;
    HttpRequest request;
    request.url = server.Url("/slow");
    request.timeoutSeconds = 1;
    auto started = std::chrono::steady_clock::now();
    auto response = MakeCurlTransport()->Send(request);
    CHECK_FALSE(response.completed);
    CHECK(std::chrono::steady_clock::now() - started < std::chrono::milliseconds(1700));
}

TEST_CASE("answers over 1 MiB are cut off") {
    Server server;
    HttpRequest request;
    request.url = server.Url("/huge");
    auto response = MakeCurlTransport()->Send(request);
    CHECK_FALSE(response.completed);
    CHECK(response.body.empty());
}

TEST_CASE("only http and https are spoken") {
    HttpRequest request;
    request.url = "file:///etc/hosts";
    CHECK_FALSE(MakeCurlTransport()->Send(request).completed);
    request.url = "ftp://127.0.0.1/";
    CHECK_FALSE(MakeCurlTransport()->Send(request).completed);
}
