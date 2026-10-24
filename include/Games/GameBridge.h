/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace Games {

/// The connection a game-side plugin makes back to the launcher (see gearblocks/plugin): loopback only,
/// newline delimited UTF-8 lines. The first line must be the token, anything else is dropped without a
/// reply (web pages can reach loopback ports). One connection at a time, never blocks the caller.
class GameBridge {
public:
    static constexpr size_t MaxLineBytes = 1024 * 1024;
    static constexpr size_t MaxQueuedLines = 4096;
    static constexpr size_t MinTokenLength = 16;

    /// At least 16 characters.
    explicit GameBridge(std::string token);
    ~GameBridge();
    GameBridge(const GameBridge&) = delete;
    GameBridge& operator=(const GameBridge&) = delete;

    /// 32 hex characters from the system's secure random source.
    static std::string GenerateToken();

    /// Callbacks run on the connection threads; set them before Start.
    void OnConnected(std::function<void()> callback);
    void OnDisconnected(std::function<void()> callback);
    void OnLine(std::function<void(const std::string& line)> callback);

    /// Binds 127.0.0.1 on a free port and returns it. Throws on failure.
    uint16_t Start();
    void Stop();

    const std::string& Token() const { return m_token; }
    bool Connected() const;

    /// Queues one line (no newline inside). False when nobody is connected, the line is empty, too long or
    /// contains a newline, or the queue is full.
    bool Send(const std::string& line);

private:
    class Session;

    void AcceptLoop();
    void ReleaseFinishedSession();

    const std::string m_token;
    std::function<void()> m_onConnected;
    std::function<void()> m_onDisconnected;
    std::function<void(const std::string&)> m_onLine;

    uint64_t m_listener = ~uint64_t(0);
    std::thread m_acceptor;
    std::atomic<bool> m_stopping = false;
    bool m_started = false;

    mutable std::mutex m_mutex;
    std::unique_ptr<Session> m_session;
};

}
