/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include "Games/RelayProtocol.h"
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>

namespace Relay {

struct Endpoint {
    std::string host;
    uint16_t port = 0;
};

struct Hello {
    std::string name;
    std::string game;
    std::string client;
    std::string token;
};

/// What the relay told us. `json` has the fields of the message, `Data` has `data`, `Closed` has `reason` in `json`.
struct Event {
    enum class Kind { Welcome, RoomCreated, RoomJoined, PeerJoined, PeerLeft, RoomClosed, Data, Error, Closed };
    Kind kind = Kind::Closed;
    nlohmann::json json = nlohmann::json::object();
    DataFrame data;
};

/// Events arrive on the link's own thread, in order, `Closed` last and once.
using EventHandler = std::function<void(const Event&)>;

/// One connection to a relay. Sending never blocks for long (a stuck relay ends the connection).
class Link {
public:
    virtual ~Link() = default;
    virtual void CreateRoom(int maxPlayers, const std::string& password) = 0;
    virtual void JoinRoom(const std::string& code, const std::string& password) = 0;
    virtual void LeaveRoom() = 0;
    /// False when the connection is gone or the data too large.
    virtual bool SendData(DataKind kind, uint16_t target, const std::string& bytes) = 0;
    /// Ends the connection; a `Closed` event follows. Safe to call from an event handler.
    virtual void Close() = 0;
};

/// Connects, says hello and starts reading. Returns nothing, with `error` set, when that is not possible.
/// The handler may already be called (with `Welcome`) before this returns.
using LinkFactory = std::function<std::unique_ptr<Link>(const Endpoint&, const Hello&, EventHandler, std::string& error)>;

/// The real thing: a plain TCP connection to a relay (the relay speaks TLS when given a certificate, the
/// client side of that is not here yet, see `IsLocalHost`).
std::unique_ptr<Link> ConnectTcpLink(const Endpoint& endpoint, const Hello& hello, EventHandler handler, std::string& error);

/// Whether a relay address is this machine or a private network. Until TLS is supported the engine only
/// talks to those, so a room password or name never crosses the internet in the clear.
bool IsLocalHost(const std::string& host);

}
