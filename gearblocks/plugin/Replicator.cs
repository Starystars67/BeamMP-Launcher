using System;
using System.Collections.Generic;
using System.Text;
using HarmonyLib;
using Il2CppInterop.Runtime.InteropTypes.Arrays;
using SmashHammer.Network;
using UnityEngine;

namespace BeamMP.GearBlocks;

// Carries the game's own remote calls between the players of a room (Photon is not involved: the shipped game has
// the calls but no transport behind them). In a room the host's game is the server:
//   guest: a call its player makes to the server is not run here but sent to the host,
//          the host's answers (what the server tells all players) run here when they arrive.
//   host:  runs the guests' requests as its own server would, and sends everything its server tells the players on.
// Outside a room nothing changes. Everything is wired through Harmony prefixes on NetworkRemoteMethods.
[HarmonyPatch]
internal static class Replicator
{
    // While an answer from the host runs on a guest, the calls it makes itself are not requests for the host.
    static bool applying;
    // A guest takes part once the host has put it in its world (the scene arrived): before that its own game is running
    // something else and what it asks of the "server" is meaningless, and a host would act on it twice.
    static bool inWorld;
    public static bool InWorld => inWorld;
    static readonly BroadcastLog log = new();
    static readonly ReplayQueue replays = new();
    static readonly InboundGate<Inbound> gate = new();
    const double LoadSeconds = 60;
    static int dropped;
    static int sentCount;
    static int appliedCount;

    sealed class Inbound
    {
        public ushort From;
        public RpcKind Kind;
        public uint Handle;
        public byte[] Payload;
    }

    internal static bool Trace => trace;
    static readonly bool trace = Environment.GetEnvironmentVariable("BEAMMP_PROBE") == "1";

    static bool IsSceneCall(string method) => method == "FromServer_NewScene" || method == "FromServer_LoadScene";

    public static void Install()
    {
        new Harmony("com.beammp.gearblocks.replicator").PatchAll(typeof(Replicator));
        new Harmony("com.beammp.gearblocks.guestuse").PatchAll(typeof(GuestUse));
    }

    [HarmonyPrefix]
    [HarmonyPatch(typeof(NetworkRemoteMethods), nameof(NetworkRemoteMethods.CallFromPlayerToServer))]
    static bool ToServer(uint methodHandle, Il2CppStructArray<byte> data)
    {
        if (applying) return true;
        var room = Room.Current;
        if (room.InRoom)
        {
            // input for a vehicle: whoever simulates the vehicle has to get it
            var vehicle = VehicleSync.ConstructionOfControl(methodHandle);
            if (vehicle.HasValue)
            {
                var owner = VehicleSync.Owners.OwnerOf(vehicle.Value);
                if (room.IsHost)
                {
                    if (owner != room.SelfId) Send(RpcKind.Request, methodHandle, 0, data, "peer", owner);
                    return true; // the host's copy takes it too
                }
                if (inWorld)
                {
                    Send(RpcKind.Request, methodHandle, 0, data, "host", 0); // the host's copy, and it passes it on to the owner
                    return owner == room.SelfId; // simulated here: the game takes it as well
                }
            }
        }
        if (!Policy.ForwardRequest(room)) return true;
        if (!inWorld)
        {
            dropped++;
            return false; // not in the host's world yet
        }
        Send(RpcKind.Request, methodHandle, 0, data, "host", 0);
        return false; // the host's game decides, not this one
    }

    [HarmonyPrefix]
    [HarmonyPatch(typeof(NetworkRemoteMethods), nameof(NetworkRemoteMethods.CallFromServerToAllPlayers))]
    static void ToAll(uint methodHandle, Il2CppStructArray<byte> data)
    {
        var room = Room.Current;
        if (Link.Configured && !(room.InRoom && !room.IsHost))
        {
            // this game is the server: what it tells the players is what a player who joins later has to be told too
            // (a scene is not kept here: the log starts when the level loads, and the scene is told first, see PlayerJoined)
            if (!IsSceneCall(RemoteMethods.MethodName(methodHandle))) log.Add(methodHandle, RemoteMethods.FromGame(data));
        }
        if (Policy.ForwardBroadcast(room)) Send(RpcKind.Broadcast, methodHandle, 0, data, "all", 0);
    }

    [HarmonyPrefix]
    [HarmonyPatch(typeof(NetworkRemoteMethods), nameof(NetworkRemoteMethods.CallFromServerToSpecificPlayer))]
    static void ToOne(byte playerID, uint methodHandle, Il2CppStructArray<byte> data)
    {
        var room = Room.Current;
        if (!Policy.ForwardBroadcast(room)) return;
        var peer = room.PeerForPlayer(playerID);
        if (peer != 0 && peer != room.SelfId) Send(RpcKind.ToPlayer, methodHandle, playerID, data, "peer", peer);
    }

    static void Send(RpcKind kind, uint handle, byte player, Il2CppStructArray<byte> data, string target, ushort peer)
        => Send(kind, handle, player, RemoteMethods.FromGame(data), target, peer);

    static void Send(RpcKind kind, uint handle, byte player, byte[] payload, string target, ushort peer)
    {
        try
        {
            if (payload.Length > Wire.MaxPayloadBytes)
            {
                Plugin.Logger.LogWarning($"Remote call {RemoteMethods.NameOf(handle)} not sent: {payload.Length} bytes is too large");
                dropped++;
                return;
            }
            var frame = Wire.Encode(kind, handle, player, payload);
            var line = new StringBuilder(64 + frame.Length * 4 / 3);
            line.Append("{\"type\":\"data\",\"kind\":\"").Append(target).Append('"');
            if (peer != 0) line.Append(",\"target\":").Append(peer);
            line.Append(",\"data\":\"").Append(Convert.ToBase64String(frame)).Append("\"}");
            var link = Link.Current;
            if (sentCount < 20) Plugin.Logger.LogInfo($"NET: {kind} {RemoteMethods.NameOf(handle)} {payload.Length} bytes -> {target}");
            if (link != null && link.Send(line.ToString())) sentCount++;
            else dropped++;
        }
        catch (Exception e)
        {
            dropped++;
            Plugin.Logger.LogWarning("Could not send a remote call: " + e.Message);
        }
    }

    // A data message from the launcher (another player's call). Runs on the game's main thread.
    public static void Receive(string line)
    {
        try
        {
            using var document = System.Text.Json.JsonDocument.Parse(line);
            var root = document.RootElement;
            var from = root.GetProperty("from").GetUInt16();
            var bytes = Convert.FromBase64String(root.GetProperty("data").GetString());
            if (!Wire.TryDecode(bytes, out var kind, out var handle, out var player, out var payload))
            {
                dropped++;
                return;
            }
            if (kind == RpcKind.State)
            {
                // where another player is: not a game call, and never held back by a loading scene
                if (Policy.AcceptState(Room.Current, from, player) && PlayerState.TryDecode(payload, out var state)) Presence.Remote(player, state);
                else dropped++;
                return;
            }
            if (kind == RpcKind.Physics)
            {
                // where the host's vehicles are: not a game call; the handle field names the construction
                if (handle <= ushort.MaxValue && Policy.AcceptPhysics(Room.Current, from, VehicleSync.Owners.OwnerOf((ushort)handle)) && PhysicsFrame.TryDecode(payload, out var poses)) VehicleSync.Remote((ushort)handle, poses);
                else dropped++;
                return;
            }
            if (kind == RpcKind.OwnerRequest || kind == RpcKind.OwnerRelease || kind == RpcKind.Owner)
            {
                // who simulates a vehicle: not a game call, and never held back by a loading scene
                if (handle > ushort.MaxValue) { dropped++; return; }
                if (kind == RpcKind.OwnerRequest) VehicleSync.OwnerRequested(Room.Current, from, (ushort)handle);
                else if (kind == RpcKind.OwnerRelease) VehicleSync.OwnerReleased(Room.Current, from, (ushort)handle);
                else VehicleSync.OwnerAnnounced(Room.Current, from, (ushort)handle, player);
                return;
            }
            var item = new Inbound { From = from, Kind = kind, Handle = handle, Payload = payload };
            if (!Policy.Accept(Room.Current, kind, RemoteMethods.MethodName(handle), from))
            {
                dropped++;
                if (trace) Plugin.Logger.LogInfo($"NET: refused {kind} {handle:x8} from {from} ({RemoteMethods.MethodName(handle) ?? "unknown method"})");
                return;
            }
            if (gate.Offer(item)) return; // the scene is still loading
            Dispatch(item);
        }
        catch (Exception e)
        {
            dropped++;
            Plugin.Logger.LogWarning("Dropped a remote call from the room: " + e.Message);
        }
    }

    static void Apply(Inbound item)
    {
        // a guest applies the host's answer; the host runs a guest's request as its own server
        var wasApplying = applying;
        applying = item.Kind != RpcKind.Request;
        var before = ConstructionCount();
        if (item.Kind == RpcKind.Request && !Room.Current.IsHost)
        {
            // the host passes on somebody's input for a vehicle: only if this game simulates it
            var vehicle = VehicleSync.ConstructionOfControl(item.Handle);
            if (!vehicle.HasValue || !VehicleSync.Owners.IsOwnedBy(vehicle.Value, Room.Current.SelfId))
            {
                applying = wasApplying;
                dropped++;
                return;
            }
        }
        if (item.Kind == RpcKind.Request && Room.Current.IsHost)
        {
            // another player's use of a part is carried out here rather than by the game's handler
            var allowed = GuestUse.HandleUse(item.Handle, item.Payload, out var handled);
            if (handled)
            {
                applying = wasApplying;
                if (allowed) appliedCount++;
                else dropped++;
                return;
            }
        }
        try
        {
            var ok = RemoteMethods.Invoke(item.Handle, item.Payload);
            if (ok) appliedCount++;
            else dropped++;
            if (ok && item.Kind == RpcKind.Request && Room.Current.IsHost && RemoteMethods.MethodName(item.Handle) == Policy.ControlMethod)
            {
                // a guest's input for a vehicle another guest simulates goes on to that one
                var vehicle = VehicleSync.ConstructionOfControl(item.Handle);
                if (vehicle.HasValue)
                {
                    var owner = VehicleSync.Owners.OwnerOf(vehicle.Value);
                    if (owner != Room.Current.SelfId && owner != item.From) Send(RpcKind.Request, item.Handle, 0, item.Payload, "peer", owner);
                }
            }
            if (trace) Plugin.Logger.LogInfo($"NET: apply {item.Kind} {RemoteMethods.NameOf(item.Handle)} {item.Payload.Length} bytes -> {(ok ? "ok" : "failed")}");
        }
        finally
        {
            applying = wasApplying;
        }
        var name = RemoteMethods.MethodName(item.Handle);
        if (item.Kind == RpcKind.Broadcast && name != null && name.StartsWith("FromServer_DeserializeConstruction", StringComparison.Ordinal) && Room.Current.InRoom && !Room.Current.IsHost)
        {
            // the construction appears over the next frames and then materialises: wait for both
            waitingForMaterialise = true;
            constructionsBefore = before;
            materialiseNotBefore = Time.unscaledTimeAsDouble + 0.5;
            materialiseGiveUp = Time.unscaledTimeAsDouble + 6;
            gate.Hold(Time.unscaledTimeAsDouble, MaterialiseSeconds);
        }
        if (item.Kind != RpcKind.Request && IsSceneCall(name))
        {
            waitingForMaterialise = false;
            inWorld = true;
            // the host moved everybody to a scene: what comes next needs it to be up
            gate.Hold(Time.unscaledTimeAsDouble, LoadSeconds);
        }
    }

    // The level is up (the game populates it): what was held can run.
    public static void LevelReady() => DrainGate();

    static void DrainGate()
    {
        waitingForMaterialise = false;
        var items = gate.Release();
        for (int i = 0; i < items.Length; i++)
        {
            Dispatch(items[i]);
            if (gate.Holding)
            {
                // another scene: the rest waits for that one
                var rest = new Inbound[items.Length - i - 1];
                Array.Copy(items, i + 1, rest, 0, rest.Length);
                gate.Reclose(Time.unscaledTimeAsDouble, LoadSeconds, rest);
                return;
            }
        }
    }

    // Called every frame.
    public static void Tick()
    {
        if (deferred.Count > 0 && !gate.Holding)
        {
            var room = Room.Current;
            var ready = Presence.LocalPlayerReady(room);
            if (!room.InRoom || ready || Time.unscaledTimeAsDouble - deferredSince > DeferSeconds)
            {
                if (trace) Plugin.Logger.LogInfo($"NET: flushing {deferred.Count} deferred calls (inRoom={room.InRoom} localReady={ready} waited={Time.unscaledTimeAsDouble - deferredSince:F1}s)");
                FlushDeferred();
            }
        }
        var appeared = ConstructionCount() > constructionsBefore || Time.unscaledTimeAsDouble >= materialiseGiveUp;
        if (waitingForMaterialise && gate.Holding && appeared && Time.unscaledTimeAsDouble >= materialiseNotBefore && !AnyMaterialising())
        {
            waitingForMaterialise = false;
            DrainGate();
            return;
        }
        if (gate.Expired(Time.unscaledTimeAsDouble))
        {
            Plugin.Logger.LogWarning($"NET: the level did not report ready in {LoadSeconds}s, going on with {gate.Count} held calls");
            DrainGate();
        }
    }

    // A player joined: the host sends it everything its server has told the players since the scene began.
    public static void PlayerJoined(ushort peer)
    {
        var entries = log.Snapshot();
        var sceneHandle = RemoteMethods.HandleOf("FromServer_NewScene");
        var level = currentLevel ?? UnityEngine.SceneManagement.SceneManager.GetActiveScene().name;
        if (sceneHandle != 0 && !string.IsNullOrEmpty(level))
        {
            // however the host's level came about (the menu loads it directly), a new player is told to start it first
            var all = new BroadcastLog.Entry[entries.Length + 1];
            all[0] = new BroadcastLog.Entry { Handle = sceneHandle, Payload = Wire.EncodeString(level) };
            Array.Copy(entries, 0, all, 1, entries.Length);
            entries = all;
        }
        Plugin.Logger.LogInfo($"NET: replaying {entries.Length} calls ({log.Bytes} bytes{(log.Truncated ? ", truncated" : "")}) to peer {peer}");
        replays.Start(peer, entries);
        VehicleSync.PlayerJoined(Room.Current, peer);
    }

    public static void PlayerLeft(ushort peer)
    {
        replays.Cancel(peer);
        Presence.Remove(Identity.PlayerOf(peer));
        VehicleSync.PeerLeft(Room.Current, peer);
    }

    // The room is gone (or this game left it): a later room starts from nothing.
    public static void RoomEnded()
    {
        deferred.Clear();
        VehicleSync.Release();
        inWorld = false;
        gate.Release();
    }

    // About a second of the relay's rate budget per second.
    const int ReplayBytesPerSecond = 768 * 1024;

    public static void PumpReplays(float deltaSeconds)
    {
        if (replays.Idle) return;
        var link = Link.Current;
        if (link == null || !link.Connected) return;
        foreach (var item in replays.Take(Math.Max(1, (int)(ReplayBytesPerSecond * Math.Min(deltaSeconds, 0.25f)))))
        {
            Send(RpcKind.Broadcast, item.Value.Handle, 0, item.Value.Payload, "peer", item.Key);
        }
    }

    // A construction that has just been created plays a materialising effect, and the game ignores changes to it (a
    // freeze, for one) until that is over: what comes after waits for it.
    static bool waitingForMaterialise;
    static double materialiseNotBefore;
    static double materialiseGiveUp;
    static int constructionsBefore;
    static int ConstructionCount()
    {
        try { return SmashHammer.GearBlocks.Construction.Construction.Instances.Count; } catch { return 0; }
    }
    const double MaterialiseSeconds = 20;

    static bool AnyMaterialising()
    {
        try
        {
            foreach (var entry in SmashHammer.GearBlocks.Construction.Construction.Instances)
            {
                var m = entry.Value?.Materialisation;
                if (m != null && m.IsActive) return true;
            }
        }
        catch { }
        return false;
    }

    // The game's handlers for many calls (a freeze, say) do nothing without a local player. The calls that build the
    // world (the scene, players, constructions) run at once, the rest waits until this game's own player exists.
    static readonly List<Inbound> deferred = new();
    static double deferredSince;
    const double DeferSeconds = 90;

    static bool IsWorldBuilding(string method)
        => method != null && (IsSceneCall(method) || method == "FromServer_DeserializePlayer" || method.StartsWith("FromServer_DeserializeConstruction", StringComparison.Ordinal));

    static void Dispatch(Inbound item)
    {
        var room = Room.Current;
        if (room.InRoom && !room.IsHost && item.Kind != RpcKind.Request)
        {
            var building = IsWorldBuilding(RemoteMethods.MethodName(item.Handle));
            if (trace && !building) Plugin.Logger.LogInfo($"NET: dispatch {RemoteMethods.NameOf(item.Handle)} localReady={Presence.LocalPlayerReady(room)} deferred={deferred.Count}");
            if (!building && (deferred.Count > 0 || !Presence.LocalPlayerReady(room)))
            {
                if (deferred.Count == 0) deferredSince = Time.unscaledTimeAsDouble;
                deferred.Add(item);
                return;
            }
        }
        Apply(item);
    }

    static void FlushDeferred()
    {
        if (deferred.Count == 0) return;
        var items = deferred.ToArray();
        deferred.Clear();
        for (int i = 0; i < items.Length; i++)
        {
            Apply(items[i]);
            if (gate.Holding)
            {
                // a scene or a construction in between: the rest waits for that
                var rest = new Inbound[items.Length - i - 1];
                Array.Copy(items, i + 1, rest, 0, rest.Length);
                gate.Reclose(Time.unscaledTimeAsDouble, LoadSeconds, rest);
                return;
            }
        }
    }

    static string currentLevel;

    // Whatever starts a level (a menu, a broadcast, a replay): what the server tells the players is counted from here.
    [HarmonyPrefix]
    [HarmonyPatch(typeof(SmashHammer.GearBlocks.Game.GameLoader), "LoadLevel")]
    static void LoadingLevel(string levelName, bool isNewScene)
    {
        currentLevel = levelName;
        ModStarter.NewGame();
        log.Reset();
        if (!isNewScene) Plugin.Logger.LogInfo($"NET: level {levelName} is a saved scene, a player that joins gets a new one of that name");
    }

    // The game reports itself ready after it has loaded the scene's saved state and populated the level; the calls
    // held until then touch that state (objects, players) and must not run while it is being loaded.
    [HarmonyPostfix]
    [HarmonyPatch(typeof(SmashHammer.GearBlocks.Player.PopulatePlayersAsset), "OnGameReady")]
    static void GameReady()
    {
        LevelReady();
        ModStarter.GameReady();
    }

    public static string Summary => $"sent={sentCount} applied={appliedCount} dropped={dropped}";
}
