/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "TestClient.h"

#include <arpa/inet.h>
#include <chrono>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

TestClient::TestClient(uint16_t port) {
    m_socket = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    if (connect(m_socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        close(m_socket);
        m_socket = -1;
    }
}

TestClient::~TestClient() {
    if (m_socket >= 0) {
        close(m_socket);
    }
}

void TestClient::SendLine(const std::string& line) {
    std::string data = line + "\n";
    size_t sent = 0;
    while (sent < data.size()) {
        auto n = send(m_socket, data.data() + sent, data.size() - sent, 0);
        if (n <= 0) {
            return;
        }
        sent += static_cast<size_t>(n);
    }
}

void TestClient::SendRequest(int64_t id, const std::string& name, const nlohmann::json& data) {
    SendLine(nlohmann::json { { "v", 1 }, { "id", id }, { "type", "request" }, { "name", name }, { "data", data } }.dump());
}

std::optional<std::string> TestClient::ReadLine(int timeoutMs) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    for (;;) {
        auto end = m_buffer.find('\n');
        if (end != std::string::npos) {
            std::string line = m_buffer.substr(0, end);
            m_buffer.erase(0, end + 1);
            return line;
        }
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        if (remaining <= 0) {
            return std::nullopt;
        }
        pollfd descriptor { m_socket, POLLIN, 0 };
        if (poll(&descriptor, 1, static_cast<int>(remaining)) <= 0) {
            return std::nullopt;
        }
        char chunk[65536];
        auto received = recv(m_socket, chunk, sizeof(chunk), 0);
        if (received <= 0) {
            return std::nullopt;
        }
        m_buffer.append(chunk, static_cast<size_t>(received));
    }
}

std::optional<nlohmann::json> TestClient::Read(int timeoutMs) {
    auto line = ReadLine(timeoutMs);
    if (!line) {
        return std::nullopt;
    }
    return nlohmann::json::parse(*line);
}

std::vector<nlohmann::json> TestClient::TakePending() {
    return std::exchange(m_pending, {});
}

nlohmann::json TestClient::Call(const std::string& name, const nlohmann::json& data) {
    int64_t id = m_nextId++;
    SendRequest(id, name, data);
    while (auto message = Read()) {
        if ((*message)["type"] == "response" && (*message)["id"] == id) {
            return *message;
        }
        m_pending.push_back(*message);
    }
    return nlohmann::json();
}

nlohmann::json TestClient::Hello(const std::string& token) {
    return Call("hello", { { "token", token }, { "client", "tests" }, { "versions", { 1 } } });
}

std::optional<nlohmann::json> TestClient::WaitForEvent(const std::string& name, int timeoutMs) {
    for (auto it = m_pending.begin(); it != m_pending.end(); ++it) {
        if ((*it)["type"] == "event" && (*it)["name"] == name) {
            nlohmann::json found = *it;
            m_pending.erase(it);
            return found;
        }
    }
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        auto message = Read(static_cast<int>(remaining));
        if (!message) {
            break;
        }
        if ((*message)["type"] == "event" && (*message)["name"] == name) {
            return message;
        }
        m_pending.push_back(*message);
    }
    return std::nullopt;
}

bool TestClient::WaitForClose(int timeoutMs) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    char chunk[4096];
    while (std::chrono::steady_clock::now() < deadline) {
        pollfd descriptor { m_socket, POLLIN, 0 };
        if (poll(&descriptor, 1, 50) > 0) {
            auto received = recv(m_socket, chunk, sizeof(chunk), 0);
            if (received <= 0) {
                return true;
            }
        }
    }
    return false;
}
