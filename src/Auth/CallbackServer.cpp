/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Auth/CallbackServer.h"
#include "Auth/Pkce.h"

#include <httplib.h>
#include <stdexcept>

namespace Auth {

namespace {
    // Static pages: nothing from the request is ever echoed back.
    constexpr const char* SuccessPage = "<!doctype html><meta charset=utf-8><title>BeamMP</title>"
                                        "<body style=\"font-family:sans-serif;text-align:center;margin-top:20vh\">"
                                        "<h1>You are signed in</h1><p>You can close this tab and return to the BeamMP Launcher.</p>";
    constexpr const char* FailurePage = "<!doctype html><meta charset=utf-8><title>BeamMP</title>"
                                        "<body style=\"font-family:sans-serif;text-align:center;margin-top:20vh\">"
                                        "<h1>Sign-in was not completed</h1><p>Return to the BeamMP Launcher to try again.</p>";
    constexpr const char* RejectedPage = "<!doctype html><meta charset=utf-8><title>BeamMP</title><p>This sign-in link is not valid.";
}

struct CallbackServer::Impl {
    Impl(std::string expectedState, std::string callbackPath)
        : state(std::move(expectedState))
        , path(std::move(callbackPath)) { }

    void Reply(httplib::Response& response, int status, const char* page) {
        response.status = status;
        response.set_content(page, "text/html; charset=utf-8");
        response.set_header("Cache-Control", "no-store");
        response.set_header("Referrer-Policy", "no-referrer");
        response.set_header("Content-Security-Policy", "default-src 'none'; style-src 'unsafe-inline'");
    }

    void Handle(const httplib::Request& request, httplib::Response& response) {
        if (request.get_header_value("Host") != "127.0.0.1:" + std::to_string(port)) {
            Reply(response, 400, RejectedPage);
            return;
        }
        if (!request.has_param("state") || !SafeEqual(request.get_param_value("state"), state)) {
            Reply(response, 400, RejectedPage);
            return;
        }

        std::lock_guard<std::mutex> lock(mutex);
        if (result) {
            Reply(response, 400, RejectedPage);
            return;
        }
        Callback callback;
        if (request.has_param("error")) {
            callback.error = request.get_param_value("error");
        } else if (request.has_param("code") && !request.get_param_value("code").empty()) {
            callback.code = request.get_param_value("code");
        } else {
            Reply(response, 400, RejectedPage);
            return;
        }
        Reply(response, 200, callback.error.empty() ? SuccessPage : FailurePage);
        result = std::move(callback);
        ready.notify_all();
    }

    const std::string state;
    const std::string path;
    httplib::Server server;
    std::thread thread;
    int port = 0;

    std::mutex mutex;
    std::condition_variable ready;
    std::optional<Callback> result;
};

CallbackServer::CallbackServer(std::string state, std::string path)
    : m_impl(std::make_unique<Impl>(std::move(state), std::move(path))) { }

CallbackServer::~CallbackServer() {
    Stop();
}

uint16_t CallbackServer::Start() {
    if (m_impl->thread.joinable()) {
        throw std::logic_error("CallbackServer already started");
    }
    int port = m_impl->server.bind_to_any_port("127.0.0.1");
    if (port <= 0) {
        throw std::runtime_error("Could not open a local port for the sign-in");
    }
    m_impl->port = port;
    m_impl->server.Get(m_impl->path, [impl = m_impl.get()](const httplib::Request& request, httplib::Response& response) {
        impl->Handle(request, response);
    });
    m_impl->thread = std::thread([impl = m_impl.get()] { impl->server.listen_after_bind(); });
    m_impl->server.wait_until_ready();
    return static_cast<uint16_t>(port);
}

std::optional<Callback> CallbackServer::Wait(std::chrono::milliseconds timeout, const std::atomic<bool>& cancelled) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    std::unique_lock<std::mutex> lock(m_impl->mutex);
    while (!m_impl->result && !cancelled && std::chrono::steady_clock::now() < deadline) {
        m_impl->ready.wait_for(lock, std::chrono::milliseconds(100));
    }
    return m_impl->result;
}

void CallbackServer::Stop() {
    m_impl->server.stop();
    if (m_impl->thread.joinable()) {
        m_impl->thread.join();
    }
}

}
