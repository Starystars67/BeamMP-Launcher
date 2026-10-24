/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Games/GameBridge.h"

#include "Net/Sockets.h"
#include <chrono>
#include <condition_variable>
#include <deque>
#include <openssl/rand.h>
#include <stdexcept>

namespace Games {

namespace {
    constexpr auto HandshakeTimeout = std::chrono::seconds(5);
    constexpr int PollIntervalMs = 200;
    constexpr int SendTimeoutMs = 5000;

    using Net::CloseNative;
    using Net::Native;
    using Net::SendAll;
    using Net::ShutdownNative;
    using Net::ToNative;
    using Net::WaitReadable;

    // Compares the whole length whatever the content, the token is a secret.
    bool TokenMatches(const std::string& expected, const std::string& given) {
        if (expected.size() != given.size()) {
            return false;
        }
        unsigned char difference = 0;
        for (size_t i = 0; i < expected.size(); ++i) {
            difference |= static_cast<unsigned char>(expected[i] ^ given[i]);
        }
        return difference == 0;
    }
}

class GameBridge::Session {
public:
    Session(GameBridge& bridge, Native socket)
        : m_bridge(bridge)
        , m_socket(socket) {
        Net::ConfigureClient(socket, SendTimeoutMs);
    }

    ~Session() {
        Close();
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

    bool Send(std::string line) {
        line.push_back('\n');
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            if (m_closing || m_queue.size() >= MaxQueuedLines) {
                return false;
            }
            m_queue.push_back(std::move(line));
        }
        m_wake.notify_one();
        return true;
    }

    void Close() {
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            m_closing = true;
            m_queue.clear();
        }
        m_wake.notify_all();
    }

private:
    void Read() {
        std::string buffer;
        auto deadline = std::chrono::steady_clock::now() + HandshakeTimeout;
        char chunk[8192];
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
                // A chunk can carry a whole line past the limit: it is refused here, not only when it is still growing.
                open = end - start <= MaxLineBytes && Dispatch(buffer.substr(start, end - start));
            }
            buffer.erase(0, start);
            if (buffer.size() > (m_authenticated ? MaxLineBytes : m_bridge.m_token.size() + 2)) {
                break;
            }
        }
        bool attached = m_authenticated;
        Close();
        if (attached && m_bridge.m_onDisconnected) {
            m_bridge.m_onDisconnected();
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
                line = std::move(m_queue.front());
                m_queue.pop_front();
            }
            if (!SendAll(m_socket, line.data(), line.size())) {
                Close();
                break;
            }
        }
        ShutdownNative(m_socket);
    }

    // False closes the connection.
    bool Dispatch(std::string line) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!m_authenticated) {
            if (!TokenMatches(m_bridge.m_token, line)) {
                return false;
            }
            m_authenticated = true;
            if (m_bridge.m_onConnected) {
                m_bridge.m_onConnected();
            }
            return true;
        }
        if (m_bridge.m_onLine) {
            m_bridge.m_onLine(line);
        }
        return true;
    }

    GameBridge& m_bridge;
    const Native m_socket;
    std::atomic<bool> m_closing = false;
    std::atomic<bool> m_authenticated = false;
    std::mutex m_queueMutex;
    std::condition_variable m_wake;
    std::deque<std::string> m_queue;
    std::thread m_reader;
    std::thread m_writer;
};

GameBridge::GameBridge(std::string token)
    : m_token(std::move(token)) {
    if (m_token.size() < MinTokenLength) {
        throw std::invalid_argument("The bridge token must have at least 16 characters");
    }
}

GameBridge::~GameBridge() {
    Stop();
}

std::string GameBridge::GenerateToken() {
    unsigned char bytes[16];
    if (RAND_bytes(bytes, sizeof(bytes)) != 1) {
        throw std::runtime_error("No secure random source for the bridge token");
    }
    static const char digits[] = "0123456789abcdef";
    std::string token;
    for (unsigned char byte : bytes) {
        token.push_back(digits[byte >> 4]);
        token.push_back(digits[byte & 15]);
    }
    return token;
}

void GameBridge::OnConnected(std::function<void()> callback) {
    m_onConnected = std::move(callback);
}

void GameBridge::OnDisconnected(std::function<void()> callback) {
    m_onDisconnected = std::move(callback);
}

void GameBridge::OnLine(std::function<void(const std::string&)> callback) {
    m_onLine = std::move(callback);
}

uint16_t GameBridge::Start() {
    if (m_started) {
        throw std::logic_error("Bridge already started");
    }
    Net::Listener bound = Net::ListenLoopback(0, "game bridge");
    m_listener = static_cast<uint64_t>(bound.socket);
    m_stopping = false;
    m_started = true;
    m_acceptor = std::thread(&GameBridge::AcceptLoop, this);
    return bound.port;
}

void GameBridge::Stop() {
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
    session.reset();
}

bool GameBridge::Connected() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_session && m_session->Authenticated() && !m_session->Closed();
}

bool GameBridge::Send(const std::string& line) {
    if (line.empty() || line.size() > MaxLineBytes || line.find('\n') != std::string::npos) {
        return false;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_session && m_session->Authenticated() && m_session->Send(line);
}

// A finished session is destroyed outside the mutex: its destructor joins threads.
void GameBridge::ReleaseFinishedSession() {
    std::unique_ptr<Session> finished;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_session && m_session->Closed()) {
            finished = std::move(m_session);
        }
    }
}

void GameBridge::AcceptLoop() {
    Native listener = ToNative(m_listener);
    while (!m_stopping) {
        ReleaseFinishedSession();
        if (!WaitReadable(listener, PollIntervalMs)) {
            continue;
        }
        Native client = accept(listener, nullptr, nullptr);
        if (client == Net::Invalid) {
            continue;
        }
        std::unique_ptr<Session> replaced;
        Session* started = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            // A connection that never sent the token must not keep the real plugin out.
            if (m_session && (m_session->Closed() || !m_session->Authenticated())) {
                replaced = std::move(m_session);
            }
            if (!m_session) {
                m_session = std::make_unique<Session>(*this, client);
                started = m_session.get();
            }
        }
        replaced.reset();
        if (started) {
            started->Start();
        } else {
            CloseNative(client);
        }
    }
}

}
