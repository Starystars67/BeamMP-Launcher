/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

// Small helpers for the loopback sockets of the control channel and the game bridges.

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <cerrno>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace Net {

#if defined(_WIN32)
using Native = SOCKET;
using SockLen = int;
inline const Native Invalid = INVALID_SOCKET;
inline void CloseNative(Native socket) { closesocket(socket); }
inline void ShutdownNative(Native socket) { shutdown(socket, SD_BOTH); }
#else
using Native = int;
using SockLen = socklen_t;
inline const Native Invalid = -1;
inline void CloseNative(Native socket) { close(socket); }
inline void ShutdownNative(Native socket) { shutdown(socket, SHUT_RDWR); }
#endif

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

inline Native ToNative(uint64_t handle) { return static_cast<Native>(handle); }

/// True when readable, closed or broken: the caller finds out from recv/accept.
inline bool WaitReadable(Native socket, int milliseconds) {
    fd_set set;
    FD_ZERO(&set);
    FD_SET(socket, &set);
    timeval timeout { milliseconds / 1000, (milliseconds % 1000) * 1000 };
    return select(static_cast<int>(socket) + 1, &set, nullptr, nullptr, &timeout) != 0;
}

/// A game is a child process and must not inherit our connections.
inline void NoInherit(Native socket) {
#if defined(_WIN32)
    SetHandleInformation(reinterpret_cast<HANDLE>(socket), HANDLE_FLAG_INHERIT, 0);
#else
    fcntl(socket, F_SETFD, FD_CLOEXEC);
#endif
}

/// Bounded sends (a stuck peer must not block us) and no SIGPIPE.
inline void ConfigureClient(Native socket, int sendTimeoutMs) {
    NoInherit(socket);
#if defined(_WIN32)
    DWORD timeout = static_cast<DWORD>(sendTimeoutMs);
    setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
    timeval timeout { sendTimeoutMs / 1000, (sendTimeoutMs % 1000) * 1000 };
    setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#endif
#if defined(SO_NOSIGPIPE)
    int on = 1;
    setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#endif
}

inline bool SendAll(Native socket, const char* data, size_t size) {
    while (size > 0) {
        auto sent = send(socket, data, static_cast<int>(size), MSG_NOSIGNAL);
        if (sent <= 0) {
            return false;
        }
        data += sent;
        size -= static_cast<size_t>(sent);
    }
    return true;
}

struct Listener {
    Native socket = Invalid;
    uint16_t port = 0;
};

/// Binds 127.0.0.1:port exclusively (0 = any free port). Throws `what` + the port on failure.
inline Listener ListenLoopback(uint16_t port, const std::string& what) {
#if defined(_WIN32)
    WSADATA data;
    WSAStartup(MAKEWORD(2, 2), &data);
#endif
    Native listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener == Invalid) {
        throw std::runtime_error("Failed to create the " + what + " socket");
    }
#if defined(_WIN32)
    int exclusive = 1;
    setsockopt(listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));
#endif
    NoInherit(listener);
    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 || listen(listener, 4) != 0) {
        CloseNative(listener);
        throw std::runtime_error("Failed to listen on the " + what + " port " + std::to_string(port));
    }
    SockLen length = sizeof(address);
    getsockname(listener, reinterpret_cast<sockaddr*>(&address), &length);
    return { listener, ntohs(address.sin_port) };
}

/// Connects to host:port within `timeoutMs`. Invalid on failure, with the reason in `error`.
inline Native ConnectTcp(const std::string& host, uint16_t port, int timeoutMs, std::string& error) {
#if defined(_WIN32)
    WSADATA data;
    WSAStartup(MAKEWORD(2, 2), &data);
#endif
    addrinfo hints {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* found = nullptr;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &found) != 0 || !found) {
        error = "could not find " + host;
        return Invalid;
    }
    Native result = Invalid;
    for (addrinfo* candidate = found; candidate && result == Invalid; candidate = candidate->ai_next) {
        Native socketHandle = socket(candidate->ai_family, candidate->ai_socktype, candidate->ai_protocol);
        if (socketHandle == Invalid) {
            continue;
        }
        NoInherit(socketHandle);
#if defined(_WIN32)
        u_long nonBlocking = 1;
        ioctlsocket(socketHandle, FIONBIO, &nonBlocking);
#else
        fcntl(socketHandle, F_SETFL, fcntl(socketHandle, F_GETFL, 0) | O_NONBLOCK);
#endif
        int connected = connect(socketHandle, candidate->ai_addr, static_cast<SockLen>(candidate->ai_addrlen));
        bool inProgress = connected != 0;
#if defined(_WIN32)
        inProgress = inProgress && WSAGetLastError() == WSAEWOULDBLOCK;
#else
        inProgress = inProgress && errno == EINPROGRESS;
#endif
        bool ok = connected == 0;
        if (!ok && inProgress) {
            fd_set writable;
            FD_ZERO(&writable);
            FD_SET(socketHandle, &writable);
            timeval timeout { timeoutMs / 1000, (timeoutMs % 1000) * 1000 };
            if (select(static_cast<int>(socketHandle) + 1, nullptr, &writable, nullptr, &timeout) > 0) {
                int failure = 0;
                SockLen length = sizeof(failure);
                getsockopt(socketHandle, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&failure), &length);
                ok = failure == 0;
            }
        }
        if (!ok) {
            CloseNative(socketHandle);
            continue;
        }
#if defined(_WIN32)
        u_long blocking = 0;
        ioctlsocket(socketHandle, FIONBIO, &blocking);
#else
        fcntl(socketHandle, F_SETFL, fcntl(socketHandle, F_GETFL, 0) & ~O_NONBLOCK);
#endif
        result = socketHandle;
    }
    freeaddrinfo(found);
    if (result == Invalid) {
        error = "could not connect to " + host + ":" + std::to_string(port);
    }
    return result;
}

}
