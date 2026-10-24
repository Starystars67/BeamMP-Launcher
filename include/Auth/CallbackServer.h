/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace Auth {

/// What the browser brought back to the redirect URI.
struct Callback {
    std::string code;
    /// OAuth error code (`access_denied`, ...) instead of a code.
    std::string error;
};

/// The one-shot loopback web server a sign-in redirects to (RFC 8252 section 7.3).
///
/// Bound to 127.0.0.1 on a free port. Only a request for the callback path
/// with the right `state` and the right Host header counts; everything else is
/// answered and ignored, so another page or process cannot end the sign-in.
class CallbackServer {
public:
    explicit CallbackServer(std::string state, std::string path = "/callback");
    ~CallbackServer();
    CallbackServer(const CallbackServer&) = delete;
    CallbackServer& operator=(const CallbackServer&) = delete;

    /// Starts listening and returns the port. Throws std::runtime_error.
    uint16_t Start();

    /// The first valid callback, or nothing on timeout or when `cancelled` is set.
    std::optional<Callback> Wait(std::chrono::milliseconds timeout, const std::atomic<bool>& cancelled);

    void Stop();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}
