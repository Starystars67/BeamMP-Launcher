/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Control/Server.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <stdexcept>

#include "Net/Sockets.h"

namespace Control {

namespace {
    constexpr auto HandshakeTimeout = std::chrono::seconds(5);
    constexpr int PollIntervalMs = 200;
    constexpr int SendTimeoutMs = 5000;
    constexpr size_t MaxQueuedMessages = 1024;

    using Net::CloseNative;
    using Net::Invalid;
    using Net::Native;
    using Net::SendAll;
    using Net::ShutdownNative;
    using Net::ToNative;
    using Net::WaitReadable;
}

class Server::Session {
public:
    Session(Server& server, Native socket)
        : m_server(server)
        , m_socket(socket) {
        Net::ConfigureClient(socket, SendTimeoutMs);
    }

    ~Session() {
        Close(false);
        m_reader.join();
        m_writer.join();
        CloseNative(m_socket);
    }

    void Start() {
        m_reader = std::thread(&Session::Read, this);
        m_writer = std::thread(&Session::Write, this);
    }

    bool Closed() const { return m_closing; }
    bool Authenticated() const { return m_authenticated; }

    void Send(std::string line, bool droppable) {
        line.push_back('\n');
        bool overflow = false;
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            if (m_closing) {
                return;
            }
            if (m_queue.size() >= MaxQueuedMessages) {
                auto oldest = std::find_if(m_queue.begin(), m_queue.end(), [](const Item& item) { return item.droppable; });
                if (oldest == m_queue.end()) {
                    overflow = true;
                } else {
                    m_queue.erase(oldest);
                }
            }
            if (!overflow) {
                m_queue.push_back({ std::move(line), droppable });
            }
        }
        if (overflow) {
            Close(false);
        } else {
            m_wake.notify_one();
        }
    }

    /// With `drain` the queued messages are still sent before the writer stops.
    void Close(bool drain) {
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            if (m_closing) {
                return;
            }
            m_closing = true;
            if (!drain) {
                m_queue.clear();
            }
        }
        m_wake.notify_all();
    }

private:
    struct Item {
        std::string line;
        bool droppable;
    };

    void Read() {
        std::string buffer;
        auto deadline = std::chrono::steady_clock::now() + HandshakeTimeout;
        char chunk[4096];
        bool open = true;
        while (open && !m_closing) {
            if (!WaitReadable(m_socket, PollIntervalMs)) {
                if (!m_authenticated && std::chrono::steady_clock::now() > deadline) {
                    break;
                }
                continue;
            }
            auto received = recv(m_socket, chunk, sizeof(chunk), 0);
            if (received <= 0) {
                break;
            }
            buffer.append(chunk, static_cast<size_t>(received));
            size_t start = 0;
            for (size_t end; open && (end = buffer.find('\n', start)) != std::string::npos; start = end + 1) {
                open = Dispatch(buffer.substr(start, end - start));
            }
            buffer.erase(0, start);
            if (buffer.size() > (m_authenticated ? MaxLineBytes : MaxHelloBytes)) {
                break;
            }
        }
        bool attached = m_authenticated;
        Close(false);
        if (attached && m_server.m_onDetached) {
            m_server.m_onDetached();
        }
    }

    void Write() {
        for (;;) {
            std::string line;
            {
                std::unique_lock<std::mutex> lock(m_queueMutex);
                m_wake.wait(lock, [this] { return !m_queue.empty() || m_closing; });
                if (m_queue.empty()) {
                    break;
                }
                line = std::move(m_queue.front().line);
                m_queue.pop_front();
            }
            if (!SendAll(m_socket, line.data(), line.size())) {
                Close(false);
                break;
            }
        }
        ShutdownNative(m_socket);
    }

    // Returns false when the connection has to be closed.
    bool Dispatch(const std::string& line) {
        auto request = ParseRequest(line);
        if (!m_authenticated) {
            return request && request->name == "hello" && Handshake(*request);
        }
        if (!request) {
            Send(MakeError(std::nullopt, "bad_request", "Malformed request"), false);
            return true;
        }
        Send(Respond(*request), false);
        return true;
    }

    // Anything but a correct token is dropped silently: browsers can reach loopback ports.
    bool Handshake(const Request& request) {
        auto token = request.data.find("token");
        if (token == request.data.end() || !token->is_string() || !TokenMatches(m_server.m_token, token->get<std::string>())) {
            return false;
        }
        auto offered = request.data.find("versions");
        auto version = NegotiateVersion(offered == request.data.end() ? nlohmann::json() : *offered);
        if (!version) {
            Send(MakeError(request.id, "incompatible_protocol", "No common protocol version, update the launcher"), false);
            Close(true);
            return false;
        }
        nlohmann::json info = m_server.m_helloInfo;
        info["version"] = *version;
        Send(MakeResponse(request.id, info), false);
        m_authenticated = true;
        if (m_server.m_onAttached) {
            m_server.m_onAttached();
        }
        return true;
    }

    std::string Respond(const Request& request) {
        if (request.name == "hello") {
            return MakeError(request.id, "already_authenticated", "Handshake already done");
        }
        auto handler = m_server.m_handlers.find(request.name);
        if (handler == m_server.m_handlers.end()) {
            return MakeError(request.id, "unknown_request", "Unknown request: " + request.name);
        }
        try {
            return MakeResponse(request.id, handler->second(request.data));
        } catch (const RequestError& e) {
            return MakeError(request.id, e.code(), e.what());
        } catch (const std::exception& e) {
            return MakeError(request.id, "internal_error", e.what());
        }
    }

    Server& m_server;
    const Native m_socket;
    std::atomic<bool> m_closing = false;
    std::atomic<bool> m_authenticated = false;
    std::mutex m_queueMutex;
    std::condition_variable m_wake;
    std::deque<Item> m_queue;
    std::thread m_reader;
    std::thread m_writer;
};

Server::Server(std::string token)
    : m_token(std::move(token)) { }

Server::~Server() {
    Stop();
}

void Server::Handle(const std::string& name, Handler handler) {
    if (m_started) {
        throw std::logic_error("Handlers must be registered before Start");
    }
    if (name == "hello") {
        throw std::logic_error("hello is reserved");
    }
    m_handlers[name] = std::move(handler);
}

void Server::SetHelloInfo(nlohmann::json info) {
    m_helloInfo = std::move(info);
}

void Server::OnAttached(std::function<void()> callback) {
    m_onAttached = std::move(callback);
}

void Server::OnDetached(std::function<void()> callback) {
    m_onDetached = std::move(callback);
}

uint16_t Server::Start(uint16_t port) {
    if (m_started) {
        throw std::logic_error("Server already started");
    }
    Net::Listener bound = Net::ListenLoopback(port, "control");
    Native listener = bound.socket;

    m_listener = static_cast<uint64_t>(listener);
    m_started = true;
    m_acceptor = std::thread(&Server::AcceptLoop, this);
    return bound.port;
}

void Server::Stop() {
    if (!m_started) {
        return;
    }
    m_started = false;
    m_stopping = true;
    m_acceptor.join();
    CloseNative(ToNative(m_listener));

    std::unique_ptr<Session> session;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        session = std::move(m_session);
    }
    if (session) {
        session->Close(true);
    }
}

bool Server::HasClient() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_session && m_session->Authenticated() && !m_session->Closed();
}

void Server::Publish(const std::string& name, const nlohmann::json& data, bool droppable) {
    std::string line = MakeEvent(name, data);
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_session && m_session->Authenticated()) {
        m_session->Send(std::move(line), droppable);
    }
}

std::unique_ptr<Server::Session> Server::TakeFinishedSession() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_session && m_session->Closed()) {
        return std::move(m_session);
    }
    return nullptr;
}

void Server::AcceptLoop() {
    Native listener = ToNative(m_listener);
    while (!m_stopping) {
        TakeFinishedSession();
        if (!WaitReadable(listener, PollIntervalMs)) {
            continue;
        }
        Native client = accept(listener, nullptr, nullptr);
        if (client == Invalid) {
            continue;
        }
        TakeFinishedSession();
        Session* started = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_session) {
                m_session = std::make_unique<Session>(*this, client);
                started = m_session.get();
            }
        }
        if (started) {
            started->Start();
        } else {
            CloseNative(client);
        }
    }
}

}
