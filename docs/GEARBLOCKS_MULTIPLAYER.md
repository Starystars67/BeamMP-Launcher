# GearBlocks multiplayer: what the game gives us and how to build on it

Status 2026-10-02: survey and plan, nothing of the multiplayer itself is built yet. What exists is the way into the game (BepInEx plugin with a `BeamMP` Lua table, loopback bridge to the launcher, the GearBlocks game module, see `GEARBLOCKS.md`).

## What the game has (from its interop assemblies, `gearblocks/tools/apidump`)

The game is Unity 6 IL2CPP, about 200 construction types in `SmashHammer.GearBlocks.Construction`, and **its own networking layer**:

- `SmashHammer.Network.NetworkConnect` (singleton): `StartServer()`, `Disconnect()`, `IsServer`, `IsClient`, `IsSingleplayer`, `IsMultiplayer`, `NetworkTime`, a byte player id per player (`ServerPlayerID`, `nullPlayerID`), events (server started, client connected/disconnected, loading level, level loaded), `AllocateStateSerializer(StateSyncMode ToAll | ToServer)`. The Lua globals `ServerStarted`, `ClientConnected` and so on are these events.
- `NetworkRemoteMethods`: remote calls by method handle (`CallFromPlayerToServer`, `CallFromServerToAllPlayers`, `CallFromServerToSpecificPlayer`, `CallFromPlayerToOthers`), payload a byte array.
- State replication components: `NetworkTransform`, `NetworkLocalTransform`, `NetworkRigidbody`, `NetworkPlayerPosition`, `NetworkPlayerOrientation`, `NetworkConstructions`, each with a `...State` class (`INetworkSerializeState`, `INetworkStateStream`).
- The transport underneath is **Photon PUN** (`PhotonUnityNetworking.dll`, `PhotonRealtime.dll` are shipped). This build has no host/join UI.
- Constructions can be turned into bytes and back: `ConstructionSerialize.SerializeConstruction(Stream, compress, Construction, translation, serializeConstructionIDs, serializePartIDs, characterPlayerID, builderPlayerID...)` and `DeserializeConstruction(...)` (JSON, optionally compressed). The Lua side has `SpawnConstruction`, `ConstructionSpawned`, `ConstructionDestroyed`, `Players`, `LocalPlayer`, `Constructions`.

So the game was built for multiplayer and most of the state model exists. What is unknown is how complete and how reachable it is in the shipped build, and where Photon can be replaced by our own transport.

## What the BeamMP side has

- `BeamMP-Server` (about 9k lines of C++) is built around vehicles (`Client`, `TServer`, `TNetwork`, `VehicleData`) and server side Lua plugins. It is not game agnostic.
- The launcher's network core (`CoreNetwork`, the proxy, `VehicleData`/`VehicleEvent`) is the BeamNG path: it talks to the game's mod on 4444 and to the server with BeamNG's packets.
- `protocol-prototype` is the intended shared protocol (typed state keys, permissions, update rates), not yet usable on the wire.
- The backend's server list and authentication are BeamNG only.

## Probe results (2026-10-02, build 0.8.9740)

Run with the plugin's probe (`BEAMMP_PROBE=1`, `gearblocks/plugin/Probe.cs`), sandboxed with no outbound network. **Photon is never touched: not called, not read, not started** (the probe was changed to say so, and `NetworkConnect.StartServer` is not used because it could start a connection). Photon belongs to a company we do not want to compete with or disturb; everything below sits above it, in the game's own code.

- Single player already runs the game's network layer as a **local server with one player**: `IsServer`, `IsConnected` (once a scene is up), `IsSingleplayer`, player id 0. `IsServer`, `IsClient`, `IsSingleplayer`, `IsMultiplayer`, `NetworkTime`, `ServerPlayerID` are **static** properties.
- The **state replication components are empty shells** in this build: `NetworkStateSerializer`, `NetworkRigidbody`, `NetworkPlayerPosition`, `NetworkConstructions` and their `...State` classes have no fields or methods beyond the base ones, and none are instantiated in a scene (`StateSerializers=0`, `NetworkConstructions=0`). Player positions and rigidbody state would be ours to build.
- The **remote method layer is whole**: 94 methods are registered during play (`docs/GEARBLOCKS_REMOTE_CALLS.md`), all of the form `ToServer_Request...` (a player asks) and `FromServer_Broadcast.../FromServer_Deserialize...` (the server tells everybody): spawn, duplicate and destroy parts and constructions, attach and detach, freeze, paint, selection and moving, player spawn, inventory, tool use, scene loading, simulation parameters. Payloads are byte arrays through `NetworkRemoteMethods.CallFromPlayerToServer`, `CallFromServerToAllPlayers`, `CallFromServerToSpecificPlayer`, `CallFromPlayerToOthers`, all public. The game's own code decides what runs where; only the carrying of the bytes is missing.

So the game is an authoritative host model: one player is the server, the others send requests and receive broadcasts. That is exactly what a relay with rooms carries.

## Remote calls (milestone 2)

Findings (the probe's lab actions: `Trace on`, `Block ToServer on|off`, `Invoke`, `Call`; `gearblocks/plugin/RpcSpy.cs`):

- The original `CallFromPlayerToServer` **runs the registered `ToServer_*` method locally** (single player is its own server), and that method calls `CallFromServerToAllPlayers`, which runs the `FromServer_*` method locally. The chain is `PlayerToServer` -> `ToServer_Request...` -> `ServerToAll` -> `FromServer_...`.
- A Harmony prefix returning `false` on `CallFromPlayerToServer` stops the local run: that is how a guest hands the decision to the host.
- A registered delegate can be run by hand (`NetworkRemoteMethods.remoteMethods[handle].Invoke(bytes)`): that is how a call from another player is delivered. Payloads are self-contained.
- All 94 registered methods are `ToServer_*` or `FromServer_*`; there is no use of `CallFromPlayerToOthers`, so it is not carried.

How it works (`gearblocks/plugin`: `Replicator.cs`, `Policy.cs`, `Wire.cs`, `Room.cs`, `Link.cs`, `NetPump.cs`):

- **Guest:** a call its player makes to the server is sent to the host (`kind: host`) and **not** run locally. What the host's server tells everybody arrives and runs here through the registered delegate.
- **Host:** runs a guest's request as its own server would (the registered `ToServer_*` delegate), and sends everything its server broadcasts (`CallFromServerToAllPlayers`) to every guest (`kind: all`), `CallFromServerToSpecificPlayer` to the one peer.
- Outside a room nothing changes: the game behaves as before.
- **Frame** (what the plugins exchange, opaque to the launcher and the relay): `u8 kind (1 request, 2 broadcast, 3 to player) | u32 method handle LE | u8 player id | the game's payload`, at most 700 KiB of payload.
- **Between plugin and launcher** (over the bridge, game-agnostic, handled by `Games/SessionData.*`): `{"type":"data","kind":"host"|"all"|"peer"|"others","target":<peer, for "peer">,"data":"<base64>"}` out, `{"type":"data","from":<peer>,"kind":...,"data":"<base64>"}` in. The launcher only passes them on while a room exists for that game and never shows them to the UI.
- **Trust:** everything from another player is checked first (`Policy.Accept`): a request only runs on a host and only for a `ToServer_*` method, a broadcast only on a guest, only from the host and only for a `FromServer_*` method; anything else is dropped (counted in the `NET:` log line). The game trusts its own netcode, so this is what stands between a room and its methods.
- The plugin owns its connection to the launcher now (`Link`), connected as soon as the game starts, independent of the Lua mod, and kept up by a small behaviour (`NetPump`) that also hands received data to the game's main thread.

Tested: `gearblocks/tests/PluginTests` (wire, room parsing, policy; `dotnet run`), `tests/Base64Tests.cpp`, `tests/SessionDataTests.cpp`, the data cases in `tests/ControllerTests.cpp`, and for real with the game (a scripted fake player at the relay): host runs a guest request and broadcasts the result, guest forwards instead of running and runs a broadcast, a broadcast naming a request method, a request sent to a guest and garbage are all refused.

Known gaps of milestone 2, now closed by milestone 3: a guest gets the host's world, and each player has its own id. Still open: payloads over 700 KiB are not sent (chunking), nothing shapes how much the host sends through the relay.

## World and players (milestone 3)

Findings:

- **No late-join logic is left in this build.** `ClientConnected`, `ClientReady` and `ConnectedClientLoadingLevel` (event assets, `gearblocks/plugin/EventLab.cs` lists them with their subscribers) have no subscribers: the part of the game that told a joining client about the world was the transport's, and is gone. So the joiner's world is built here.
- **A player's identity is a variable the game keeps** (`NetworkConnect.localPlayerNetworkIDVar`, the game's own spawn request starts with it). Every game is its own server with player 0, so in a room each peer sets it to *relay peer id - 1* (host 0). The game's own flow then does the rest: `ToServer_RequestSpawnPlayer` (`id, 0xffff`) -> server `SpawnPlayer` -> `FromServer_DeserializePlayer` -> `ToServer_RequestSpawnCharacterConstruction` -> `FromServer_DeserializeConstructionCustomIDs`.
- **A scene is told by name.** `FromServer_NewScene` carries the level name (a .NET length-prefixed string). The game's own menu loads its first level directly (no call), so the scene a joiner is told comes from `GameLoader.LoadLevel`, not from a recorded call.
- **Calls to objects that do not exist yet are ignored** by the game itself (a method handle that is not registered): the server's own start-up state calls (controls, lights) arrive before the construction they belong to; the plugin counts them as dropped, harmlessly.
- **The load must not be interrupted.** Replayed calls that arrived while the scene's saved state was loading made the game's loader throw ("Collection was modified" in `SceneState.LoadPersisted`) and abort the load. A guest holds what arrives from the moment the scene starts until the game reports `PopulatePlayersAsset.OnGameReady` (or 60 s).
- **A remote player's body has its own physics and network components** (`CharacterPhysics`, a kinematic-able `Rigidbody`, `NetworkPlayerPosition`, `NetworkPlayerOrientation`). To show a pose they are switched off for remote players, the body is kinematic and moved in `LateUpdate`.

How it works (`Identity.cs`, `Replay.cs`, `InboundGate.cs`, `PlayerState.cs`, `Presence.cs`, `Replicator.cs`):

- **Late join.** The host keeps an ordered log of everything its server broadcast since the level loaded (`BroadcastLog`: repeats of the last entry are not kept, capped at 50,000 calls / 32 MiB, the log starts over with every `LoadLevel`). When a peer joins, the host sends it `NewScene(current level)` followed by the log, paced at about 768 KiB/s (`ReplayQueue`) because the relay limits a connection to 2 MiB/s.
- **Guest.** Holds what arrives while the scene loads (`InboundGate`), then applies it in order. Until the host's scene has arrived a guest sends nothing to the host: its own game is running something else and would have the host spawn its player twice.
- **Positions.** Each game sends its own player's pose (`PlayerState`: position, rotation, look yaw/pitch, velocity, tool; 49 bytes, frame kind 4) about 15 times a second (once a second while standing still) to everyone else (`kind: others`); receivers accept a pose only for the player the sender is (`Policy.AcceptState`) and only if the numbers are usable positions and a rotation.
- **Leaving.** When a peer leaves the room every game removes that player's body and character construction.

Tested (`gearblocks/tests/PluginTests` for the pure parts; the game for the rest, a scripted fake player at the relay): as host with a late joiner: scene first, the host's player and character replayed, the joiner's spawn request answered with a new player and announced, a joiner's pose moves its player on the host and the host's pose reaches the joiner, a pose naming somebody else's player is refused, the joiner's character is built and both go when it leaves. As guest: it loads the host's scene, asks for player 1 (not 0) exactly once, ends with the host's player and its own, shows the host's pose and refuses poses that name its own player.

Known gaps:

- **Saved scenes.** A host whose level came from a saved scene (not a new scene) gets a joiner a new scene of the same name, without the saved content. Constructions the host built are only told if its server broadcast them; things the scene file itself contains are not.
- **Animation and tools.** Only pose and the tool number travel; walking animation, camera mode and tool use are not shown.
- **Who owns what** is still the host's view: a guest's request runs on the host with the player id in the payload (that is how the game attributes it), nothing checks that a request names its sender's player.
- **The game's start-up.** The game's own menu starts its (local) server when a game begins (`NetworkConnect.StartServer`). The plugin never calls it, nor anything of Photon; in the tests the game's own menu is clicked, as a player would.
- **Long sessions.** The log keeps what the server said, not the world's state: after hours of building it holds every operation and replaying it takes that long (a snapshot of the constructions would be better).
- Payloads over 700 KiB are not sent.

Lab tools (`BEAMMP_PROBE=1`, actions written to `BepInEx/probe-actions.txt`, run when the file changes): `Composites`, `SaveData`, `Freeze <id> <true|false>`, `Push <id> x y z`, `Parts <id>`, `Use <id> <part idx>`, `EndUse <id> <part idx>`, `Control <id> <part idx> <value>`, `Events` (all event assets with their subscribers), `Raise <event> [arg]` (underscores in a string argument are spaces), `LocalId [n]`, `Players`, `Constructions` (also tries the game's own serializer), `TraceType <full name>` and `Trace on` (log every call), `Mark <text>`, `Block ToServer on|off`, `Call`/`Invoke <handle> <hex>`, `Dump`, `Methods`, `Inspect` (field values read through reflection are unreliable: use typed access). Note: an action file with unchanged text is not run again.

## Vehicles (milestone 4)

The shape of a vehicle in the game: a **construction** is made of **composites** (rigid bodies, one `Rigidbody` each, joined by `ConfigurableJoint`s) made of parts. The go-kart has 9 composites and 49 parts. A construction spawns frozen (kinematic) and becomes dynamic when unfrozen; each game runs its own physics for every construction, so without something else two games drift apart at once.

Findings:

- **Driving input is already a game call**: a controlling player's input goes through `ControlBehaviour.ToServer_SetControlValue` (one registered method per control, per instance), the seat is a `PlayerLockToBehaviour`, and `FromServer_SetIsControlling` tells the player (to one player). They travel like every other call (milestone 2), so the host simulates a guest's input.
- **The unfreeze that the replay carries needs the guest's own player**: the game's handler for `FromServer_BroadcastSetConstructionFrozen` does nothing when the local player does not exist (found by invoking it by hand with and without). Hence the replay is applied in two steps: the calls that build the world (scene, players, constructions) at once, the others when this game's own player exists (`Replicator.Dispatch`, up to 90 s).
- **Test lesson:** the host answers a guest's spawn request within a round trip, so the replay's deferral never waits long in practice; a test that answers late sees the 90 s limit instead.
- **A new construction materialises** (a short effect, `Construction.Materialisation`) and the game ignores changes to it until that is over: a guest waits for the construction to exist and to stop materialising before the next call.
- Spawning one from the shipped data for tests: event `SpawnConstruction` with the folder name and save type 1 (`ConstructionShippedData`, `GearBlocks_Data/StreamingAssets/Constructions`), in a creative scene; `SetConstructionFrozen` is `(invoking player, invoked by, construction id u16, freeze)`.

How it works (`VehicleSync.cs`, `PhysicsState.cs`):

- **Owner** (the host unless a guest has taken the vehicle over, see "Ownership"): for each construction it owns that is not frozen and not a player's character (at most 128 bodies), the poses of its composites (position, rotation, speed and spin of the rigid body, 52 bytes each) are sent to the others (frame kind 5, the construction id in the handle field; a host to everybody, a guest to everybody else) when anything moved by more than 5 mm or about half a degree, and once a second otherwise. Up to 24 bodies that is at most 20 times a second, a larger construction proportionally less often (a 96-body one 5 times): without these limits a scene's loose, jittering constructions streamed about three times as much as the kart did.
- **Everybody else:** takes poses only from the owner of that construction (`Policy.AcceptPhysics`), sets the bodies kinematic and moves them towards the pose each frame (exponential follow, after everything else in the frame). A construction that is frozen or gone is let go (the freeze call puts it where it belongs), and when the room ends the bodies are the game's again.
- Latency: a guest that does not own a vehicle sees its input's effect after a round trip plus about 50 ms; the driver of a vehicle, once it owns it, none.

Tested: `PhysicsFrame`/`Policy` in `gearblocks/tests/PluginTests`; with the game: as host with a joiner: the joiner's replay has the kart and its unfreeze, a push makes about 60 pose frames of 9 bodies tracing the flight, the last frame equals the host's bodies, and a pose frame from a guest does not move the host's kart. As guest: the replayed kart is unfrozen once the guest's own player exists, follows poses from the host and is held kinematic.

### Driving a vehicle

What was measured (single player, lab actions `Parts`, `Use`, `Control`, `EndUse`):

- The go-kart: a seat (`Seat Gokart`, `PlayerLockToBehaviour`), a drive motor, a steering servo and a brake, each with a `ControlAnalogBehaviour`. Sitting a player in the seat and giving the drive motor's control the value 1 accelerates the kart to about 20 m/s.
- **Control input is a call carrying only the value**: `ControlAnalogBehaviour.ToServer_SetControlValue`, 4 bytes (a float), one registered method per control (the handle is per instance and the same in every game that has the same part ids). The game's server handler takes it as it comes: no player id, and no seat is needed on the host (a guest's value drove the host's kart without one).
- **Using a part** (sitting, taking the controls) is not a call when the game is the server: `Targeter.BeginUse` handles it on the spot. A client's game sends `Targeter.ToServer_NotifyBeginUse`/`NotifyEndUse` (per player; payload: the part's id, 2 bytes) instead.
- Two things that did not work: making a guest's game report itself as a client by patching `NetworkConnect.IsServer` and friends (the patched getters stalled the game, and the game's handling of the host's players then failed to create the guest's own player), and the game's own server handler for a remote player's use (it checks what that player's targeter is aiming at, which never runs for a remote player, and the seat logic still refused after the part was set by hand).

How it works (`GuestUse.cs`):

- **Guest:** when its local player uses a part it sends the game's own use call to the host (through the ordinary request path) and the game still does its own part, so the player sits in this game's copy of the vehicle and its controls (which need the player seated) work. Their input values are forwarded to the host (milestone 2) and the host's physics answers through the pose stream.
- **Host:** carries out a guest's use of a part for that player (`HandleUse`): the part exists, can be used, and the player's body is within 12 m; then every usable behaviour of the part is told the player began (or ended) using it.

Tested with the real game: as host, a fake guest's throttle (the game's call, found by driving the throttle once) moves the host's kart from about x=0.6 to x=225 in 12 s, 200 pose frames come back in order, and a guest's use of the seat is carried out for its player; as guest, the real game forwards the use of the seat (part 28) and the throttle to the host with the game's own handles and bytes, runs the throttle only on the host, seats the player in its own copy, and forwards letting go. Not done: a test with physical key presses in the guest's window (the throttle was set through the same `SetControlValue` the game's input code calls), and the host seating the guest's avatar in the seat (other players see it follow the pose the guest streams as the kart moves).

### Ownership (driver-side simulation)

So that a driver's steering does not wait for a round trip, the game that simulates a vehicle is not always the host's (`VehicleSync.cs`, `Ownership.cs`):

- **Table:** every game keeps who owns which vehicle (`OwnerTable`); a vehicle that is not in it belongs to the host. The host decides, and tells everybody (frame kind 8, the construction in the handle field, the new owner's relay peer in the player field); a player that joins is told about everything that is not the host's.
- **Taking a vehicle:** every quarter second each game looks at whether its own player is seated in a vehicle (`PlayerLocker`, the part's construction through its composite). A guest in a seat of a vehicle it does not own asks the host (kind 6); the host agrees unless somebody else owns it (a guest) or its own player sits in it. The host's own player sitting in a vehicle just takes it. Getting out of the seat gives a guest's vehicle back (kind 7); a guest that leaves gives back everything it owned.
- **Handover:** the new owner makes the bodies the game's again (`Composite.UpdateIsKinematic(false)`) with the speed and spin of the last pose it was told, and from then on sends the poses; the old owner holds its copy where it is until the new owner's poses arrive. Poses from anybody but the owner are dropped.
- **Input:** the control calls of a vehicle (`ToServer_SetControlValue`, found through the behaviour the call is registered for) go to whoever simulates it. The owner's game runs its own player's input at once and also tells the host; the host passes on input for a vehicle a guest owns (its own player's, or another guest's) to that guest as a request (`Policy.Accept`: a guest only takes those, only from the host, and only for a vehicle it owns).

Tested with the real game, a scripted fake player at the relay. As host: the fake guest's request is granted and announced, the host's kart stops being simulated and sent and follows the guest's poses, giving it back makes the host simulate it again (it settles on the ground) and send it, poses from a guest that does not own it are ignored, a guest that leaves gives it back, the host's own throttle is passed on to the owner, the host's player sitting in the kart takes it back and a request while it sits there is refused, and a player that joins is told who owns it. As guest: the kart follows the host's poses, sitting in its seat makes the real game ask for it, once granted it simulates it (it falls) and sends its poses, the throttle drives it about 200 m in 10 s in its own game and is also told to the host, getting out gives it back, and then it follows the host again.

Known gaps: a vehicle with several drivers is not supported (the first to sit owns it); a guest that loses its connection without leaving is only noticed when the room tells the host that it left; the other players see the driver's avatar ride along through its streamed pose, not seated.

## Ways to build it

**A. Reuse the game's own netcode, replace the transport.** Start the game's server/client machinery (`NetworkConnect`) and carry its messages (state streams, remote method calls) over our connection instead of Photon. Least to invent (players, constructions, rigidbodies already replicate in the game's own format), most reverse engineering: the Photon calls have to be found and patched (Harmony), and their bodies may be stripped or tied to Photon objects.

**B. Our own sync on the game's public pieces.** The mod reads the local player and constructions (events + `ConstructionSerialize`), sends them as messages, and recreates remote players and constructions on the other side. Physics for built vehicles needs an ownership rule (the owner simulates, others follow). Fully ours, no dependency on Photon internals, but the most to build.

**Chosen: A, at the remote method layer, plus our own sync where the shells are empty.** Hook the `CallFrom...` methods (Harmony, no Photon), carry the bytes through the relay, and call the registered delegates on the receiving side. Every game stays its own local server (it is never made to believe it is a client: that stalled the game); the host's game is the one the others take their requests to, and what is not in the game's calls (positions, vehicle poses, ownership) is ours.

## Milestones

0. **Probe: done** (above).
1. **A session, no game state: done.** The relay (`relay/`: rooms, codes, routing, limits). The launcher engine connects to it (`RelaySession`, `RelayLink`), the UI has "Play together" under a game that supports it (host a room, join with a code, who is in the room), and the BeamMP script mod (`gearblocks/mod/BeamMP`) shows the room code and the players in a window in the game, live. Tested with the real relay, engine and game together. Not done: the mod starting by itself (it is started from the game's Script Mods menu), installing plugin and mod for the user (`gearblocks/install-dev.sh` does it for development), TLS to a relay over the internet (the engine only talks to this machine and private networks for now), BeamMP account login at the relay.
2. **Remote calls through the relay: done (first version).** The game's `NetworkRemoteMethods.CallFrom...` are hooked (Harmony, no Photon) and the bytes travel through the relay; see "Remote calls". Verified with the real game under Wine, the real engine and relay, both ways. What was missing then (a joining guest getting the host's world, a player id of its own) is milestone 3.
3. **World and players: done (first version).** A player that joins gets the host's scene, players and constructions, every player appears in every game with the game's own spawn flow, and positions are streamed; see "World and players". Verified with the real game as host and as guest. Not done: avatar animation, saved scenes.
4. **Physics and vehicles: done (the owner of a vehicle simulates it).** The game that simulates a vehicle (the host's by default, the driver's once it sits in it) sends the poses and speeds of its bodies and every other copy follows; what the others do with the vehicle reaches the owner as the game's own calls. See "Vehicles", "Driving a vehicle" and "Ownership". Verified with the real game as host and as guest. Not done: the host seating a guest's avatar in the seat (other players see it ride along instead), vehicles with more than 128 bodies, a vehicle with more than one driver.

## Decisions

1. **The server: a relay with rooms, run by BeamMP.** Not a dedicated server per world like BeamNG. A player creates a room, friends join by its code; the host's game is the game's server. Custom event and game-mode relays can come later as rooms with rules. Built first (`relay/README.md`).
2. **No Photon.** Nothing of it is called or read. (The game's own menu starts its local server when a game begins; the plugin never does.)
3. **Authentication and the server list.** The relay has an `authenticate` hook; today it admits everyone by name. BeamMP account checks and a list of public rooms need backend work.
4. **The game's makers.** SmashHammer has multiplayer in the game's code, and this build removed its transport. A mod that supplies one deserves a conversation with them before anything is public.
5. **Distribution: signed manifest, nothing bundled.** The launcher ships no plugin and no mod. BepInEx is an explicit install step; the plugin and the script mod are synced at launch from a manifest that is signed with an Ed25519 key the launcher is told to trust (`--gearblocks-manifest`, `--gearblocks-trust`, `docs/GEARBLOCKS.md`). Open: where BeamMP hosts it, and who holds the real signing key (it must not live on a development machine).
