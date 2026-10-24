/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

/// A minimal control-protocol client over a plain loopback socket (POSIX only).
class TestClient {
public:
    explicit TestClient(uint16_t port);
    ~TestClient();
    TestClient(const TestClient&) = delete;
    TestClient& operator=(const TestClient&) = delete;

    bool Connected() const { return m_socket >= 0; }
    void SendLine(const std::string& line);
    void SendRequest(int64_t id, const std::string& name, const nlohmann::json& data = nlohmann::json::object());

    /// Next message, or nothing on timeout or when the server closed the connection.
    std::optional<nlohmann::json> Read(int timeoutMs = 2000);

    /// The next line as it came (for peers that are not control servers), or nothing on timeout or close.
    std::optional<std::string> ReadLine(int timeoutMs = 2000);

    /// Messages that arrived while waiting for something else, oldest first.
    std::vector<nlohmann::json> TakePending();

    /// Sends a request and waits for its response; events seen meanwhile go to TakePending.
    nlohmann::json Call(const std::string& name, const nlohmann::json& data = nlohmann::json::object());
    nlohmann::json Hello(const std::string& token);

    /// Waits for the next event with this name; other events go to TakePending.
    std::optional<nlohmann::json> WaitForEvent(const std::string& name, int timeoutMs = 2000);

    /// True once the server has closed the connection (data still in flight is discarded).
    bool WaitForClose(int timeoutMs = 2000);

private:
    int m_socket = -1;
    int64_t m_nextId = 100;
    std::string m_buffer;
    std::vector<nlohmann::json> m_pending;
};
