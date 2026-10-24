/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include "Control/Protocol.h"
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace Control {

/// Loopback control channel: one authenticated client, newline-delimited JSON.
class Server {
public:
    using Handler = std::function<nlohmann::json(const nlohmann::json& data)>;

    explicit Server(std::string token);
    ~Server();
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    /// Register before Start. `hello` is reserved.
    void Handle(const std::string& name, Handler handler);

    /// Fields merged into the `hello` response next to `version`.
    void SetHelloInfo(nlohmann::json info);

    /// Run on the connection thread right after a client authenticated / went away.
    void OnAttached(std::function<void()> callback);
    void OnDetached(std::function<void()> callback);

    /// Binds 127.0.0.1:port (0 = any free port) and returns the port. Throws on failure.
    uint16_t Start(uint16_t port);

    /// Sends what is already queued to the client, then closes everything.
    void Stop();

    bool HasClient() const;

    /// Never blocks. Droppable events are the first to go when the client is slow.
    void Publish(const std::string& name, const nlohmann::json& data, bool droppable = false);

private:
    class Session;

    void AcceptLoop();
    std::unique_ptr<Session> TakeFinishedSession();

    const std::string m_token;
    std::map<std::string, Handler> m_handlers;
    nlohmann::json m_helloInfo = nlohmann::json::object();
    std::function<void()> m_onAttached;
    std::function<void()> m_onDetached;

    uint64_t m_listener = ~uint64_t(0);
    std::thread m_acceptor;
    std::atomic<bool> m_stopping = false;
    bool m_started = false;

    mutable std::mutex m_mutex;
    std::unique_ptr<Session> m_session;
};

}
