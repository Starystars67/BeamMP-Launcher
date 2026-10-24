# BeamMP relay

A relay that BeamMP runs and players meet through, for games that have a host role but no dedicated server (GearBlocks first). A player creates a **room**, gets a short code, friends join with it. The relay forwards bytes between the players of a room; it never looks into what the game sends. Later the same service can grow rooms for events and game modes (the room is where such rules would live) without any game specific code in the relay.

It is not a BeamMP-Server: there is no world on it, no plugins, no server list entry. See `../docs/GEARBLOCKS_MULTIPLAYER.md` for why GearBlocks needs this shape.

```
node src/main.ts --host 0.0.0.0 --port 7777                       # plain TCP
node src/main.ts --port 7777 --tls-key key.pem --tls-cert cert.pem  # TLS
npm test
```

Node 22.18 or newer runs the TypeScript directly (no build step, no dependencies). `--games gearblocks,other` limits which games may connect. `RELAY_HOST`, `RELAY_PORT` and `RELAY_GAMES` do the same from the environment.

## Wire protocol, version 1

Length-prefixed frames over TCP (or TLS): `u32 big endian length | u8 type | payload`, the length counts the type byte and the payload, at most 4 MiB. Control messages are JSON objects, game data is bytes. Definitions: `src/protocol.ts`.

| Client to relay | Meaning |
|---|---|
| `0x01 Hello` `{v, name, game, client?, token?}` | first message; `name` is 1 to 32 characters |
| `0x02 CreateRoom` `{max_players?, password?}` | the sender is the room's host, peer 1 |
| `0x03 JoinRoom` `{code, password?}` | codes are 6 characters, no look-alikes |
| `0x04 LeaveRoom` | |
| `0x10 Data` `u8 kind, u16 target, bytes` | kinds below |
| `0x20 Ping` | answered with `Pong`; clients ping at least every 15 s |

| Relay to client | Meaning |
|---|---|
| `0x81 Welcome` `{v, server, limits}` | |
| `0x82 RoomCreated` `{code, peer_id}` | |
| `0x83 RoomJoined` `{code, peer_id, host_id, peers}` | `peers` is who is already there |
| `0x84 PeerJoined` `{id, name}`, `0x85 PeerLeft` `{id}` | ids are never reused in a room |
| `0x86 RoomClosed` `{reason}` | the host left; the connection stays and can create or join again |
| `0x90 Data` `u16 sender, u8 kind, bytes` | |
| `0xa0 Pong` | |
| `0xe0 Error` `{code, message}` | some errors end the connection |

Data kinds: `0 ToHost` (a player to the host), `1 ToAll` (host to every other player), `2 ToPeer` (host to one player), `3 ToOthers` (anyone to everyone else, host included, for state all players need). Only the host sends `ToAll` and `ToPeer`.

Errors that end the connection: `handshake_timeout`, `hello_first`, `bad_frame`, `bad_message` (in the hello), `unsupported_version`, `bad_name`, `unknown_game`, `refused`, `rate_limited`, `too_many_attempts`, `internal_error`. The others (`bad_code`, `room_not_found`, `bad_password`, `room_full`, `already_in_room`, `not_in_room`, `too_many_rooms`, `not_host`, `bad_target`, `peer_not_found`, `bad_message`) leave it open.

## What keeps it safe to run in public

- A hello within 10 s, or the connection is dropped; nothing but the hello is accepted first.
- Limits on connections (total and per address), rooms (total and per address), players per room (default 8, at most 32).
- A token bucket on what each connection sends (2 MiB/s, 8 MiB burst by default).
- A peer that cannot take what is queued for it (8 MiB) is dropped, so a slow player cannot make the relay hold everything.
- A silent connection is dropped after 45 s.
- Whoever guesses wrong codes or passwords 10 times is dropped. Codes are random (`crypto.randomInt`) and live as long as the room; passwords are compared in constant time.
- Names are trimmed and refused with control characters. The relay never interprets game payloads, so a hostile player can only send bytes to the room it is in.
- An `authenticate` hook (`RelayOptions.authenticate`) decides who may come in and under which name. Today everybody may; it is where BeamMP account tokens are checked once the launcher has them for this.

## Not done

- Authentication against BeamMP accounts, and a list of public rooms.
- Unreliable (UDP) delivery for state that does not need to arrive; everything is ordered TCP for now.
- Rate limits per room and metrics in the BeamMP monitoring stack; deployment (a Docker image, a stack file).
- TLS in the launcher's client: the relay serves TLS, the engine's `RelayLink` is plain TCP, so it only talks to this machine and private networks unless told otherwise. The client itself is `src/Games/RelayLink.cpp` and `RelaySession.cpp` in the launcher.
