/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include "Games/RelayLink.h"
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace Games {

struct SessionPeer {
    uint16_t id = 0;
    std::string name;
};

struct SessionInfo {
    enum class State { Idle, Connecting, InRoom };

    /// The game the session is for, empty while idle.
    std::string game;
    State state = State::Idle;
    /// "host" or "guest" while in a room.
    std::string role;
    std::string code;
    uint16_t selfId = 0;
    uint16_t hostId = 0;
    /// Everybody in the room, the host first, yourself included.
    std::vector<SessionPeer> peers;
    /// Why the session ended or could not start (set together with Idle).
    std::string errorCode;
    std::string errorMessage;
};

const char* ToString(SessionInfo::State state);
nlohmann::json ToJson(const SessionInfo& info);

/// Why a request could not even be started (what happens after that is reported through the state).
enum class SessionStart {
    Started,
    /// No relay is set up, or it cannot be used safely.
    Unavailable,
    /// A session exists already (or is being set up).
    Busy,
    BadCode,
};

/// The player's own place in a relay room: connects to the relay, creates or joins a room, follows who comes
/// and goes, and carries game data between the players. One at a time. The game itself is not involved here,
/// its mod is told about the state (and, later, handed the data).
class RelaySession {
public:
    struct Options {
        Relay::Endpoint relay;
        /// Names the player at the relay.
        std::function<std::string()> playerName;
        std::string clientName = "beammp-launcher";
        /// Without TLS (not there yet) only this machine and private networks are accepted, unless this is set.
        bool allowInsecureRemote = false;
    };

    RelaySession(Options options, Relay::LinkFactory factory);
    ~RelaySession();
    RelaySession(const RelaySession&) = delete;
    RelaySession& operator=(const RelaySession&) = delete;

    /// Called with every change of the session, from whichever thread caused it. Add them before use.
    using Observer = std::function<void(const SessionInfo&)>;
    void AddObserver(Observer observer);

    /// Game data from other players: sender, kind, bytes. Set before use.
    using DataHandler = std::function<void(const Relay::DataFrame&)>;
    void SetDataHandler(DataHandler handler);

    /// Whether a relay is set up and can be used.
    bool Available() const;
    std::string UnavailableReason() const;

    /// Return at once; connecting and the answer of the relay follow as state changes.
    SessionStart Create(const std::string& game, int maxPlayers, const std::string& password);
    SessionStart Join(const std::string& game, const std::string& code, const std::string& password);
    /// Leaves the room and the relay. Nothing to do when idle.
    void Leave();

    SessionInfo Info() const;

    /// False when not in a room or the data could not be sent.
    bool SendData(Relay::DataKind kind, uint16_t target, const std::string& bytes);

private:
    struct Request {
        bool create = false;
        std::string code;
        std::string password;
        int maxPlayers = 0;
    };

    SessionStart Begin(const std::string& game, Request request);
    void Run(uint64_t generation, std::string game, Request request);
    void OnEvent(uint64_t generation, const Relay::Event& event);
    void SendRequestIfReady(uint64_t generation);
    /// Ends the session (with an error or a reason), tells the observer.
    void Finish(uint64_t generation, const std::string& errorCode, const std::string& errorMessage);
    void Notify(const SessionInfo& info);

    const Options m_options;
    const Relay::LinkFactory m_factory;
    std::vector<Observer> m_observers;
    DataHandler m_dataHandler;

    mutable std::mutex m_mutex;
    SessionInfo m_info;
    std::shared_ptr<Relay::Link> m_link;
    Request m_pending;
    bool m_requestSent = false;
    bool m_welcomed = false;
    uint64_t m_generation = 0;
    std::vector<std::thread> m_workers;
};

}
