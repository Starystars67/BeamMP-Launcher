using System;
using System.Collections.Generic;
using System.Text;
using SmashHammer.GearBlocks.Player;
using SmashHammer.Network;
using UnityEngine;

namespace BeamMP.GearBlocks;

// Players in a room: sends where this game's own player is and moves the others' players (bodies the game's own spawn
// calls have created) to where their games say. The game's state components for this are empty in this build.
internal static class Presence
{
    const float SendInterval = 1f / 15f;
    const float KeepAliveSeconds = 1f;
    // how fast a remote body follows what it was told (per second, exponential)
    const float Follow = 18f;

    sealed class Target
    {
        public PlayerState State;
        public Vector3 Position;
        public Quaternion Rotation;
        public bool Placed;
        public float NextLog;
    }

    static readonly bool Debug = Environment.GetEnvironmentVariable("BEAMMP_PROBE") == "1";
    static readonly Dictionary<byte, Target> targets = new();
    static float nextSend;
    static float nextAimLog;
    static float lastSent;
    static PlayerState lastState;
    static bool haveLast;
    static int sentCount;
    static int movedCount;

    public static string Summary => $"states sent={sentCount} applied={movedCount}";

    static SmashHammer.GearBlocks.Player.Player Find(byte id)
    {
        try
        {
            var all = SmashHammer.GearBlocks.Player.Player.Instances;
            return all.ContainsKey(id) ? all[id] : null;
        }
        catch
        {
            return null;
        }
    }

    // This game's own player, or null.
    public static SmashHammer.GearBlocks.Player.Player LocalPlayer(RoomState room)
    {
        var player = Find(Identity.PlayerOf(room.SelfId));
        return player != null && player.isLocallyControlled ? player : null;
    }

    // This game's own player exists (the host has answered the spawn request).
    public static bool LocalPlayerReady(RoomState room)
    {
        var player = Find(Identity.PlayerOf(room.SelfId));
        var ready = player != null && player.isLocallyControlled;
        return ready;
    }

    public static void Tick(float now, float delta)
    {
        var room = Room.Current;
        if (!room.InRoom)
        {
            if (targets.Count > 0) targets.Clear();
            haveLast = false;
            return;
        }
        if (now >= nextSend)
        {
            nextSend = now + SendInterval;
            SendLocal(now, room);
        }
    }

    // After everything else has had its say in the frame.
    public static void Late(float delta)
    {
        if (Room.Current.InRoom) Follow_(delta);
    }

    static void SendLocal(float now, RoomState room)
    {
        var id = Identity.PlayerOf(room.SelfId);
        var player = Find(id);
        if (player == null || !player.isLocallyControlled) return;
        var t = player.transform;
        var p = t.position;
        var q = t.rotation;
        float yaw = 0, pitch = 0;
        try { player.GetYawPitch(out yaw, out pitch); } catch { }
        var v = player.velocity;
        var state = new PlayerState
        {
            X = p.x, Y = p.y, Z = p.z, Qx = q.x, Qy = q.y, Qz = q.z, Qw = q.w,
            Yaw = yaw, Pitch = pitch, Vx = v.x, Vy = v.y, Vz = v.z, Tool = player.currentActivatedToolID,
        };
        // nothing moved: say so once a second, not fifteen times
        if (haveLast && now - lastSent < KeepAliveSeconds && Same(state, lastState)) return;
        haveLast = true;
        lastState = state;
        lastSent = now;
        var frame = Wire.Encode(RpcKind.State, 0, id, state.Encode());
        var line = new StringBuilder(160);
        line.Append("{\"type\":\"data\",\"kind\":\"others\",\"data\":\"").Append(Convert.ToBase64String(frame)).Append("\"}");
        var link = Link.Current;
        if (link != null && link.Send(line.ToString())) sentCount++;
    }

    static bool Same(PlayerState a, PlayerState b)
        => Math.Abs(a.X - b.X) < 0.001f && Math.Abs(a.Y - b.Y) < 0.001f && Math.Abs(a.Z - b.Z) < 0.001f
           && Math.Abs(a.Qy - b.Qy) < 0.0005f && Math.Abs(a.Qw - b.Qw) < 0.0005f && Math.Abs(a.Yaw - b.Yaw) < 0.05f && Math.Abs(a.Pitch - b.Pitch) < 0.05f
           && a.Tool == b.Tool;

    // What another game said about its player (already checked: the sender is that player's peer).
    public static void Remote(byte player, PlayerState state)
    {
        if (!targets.TryGetValue(player, out var target))
        {
            target = new Target { NextLog = Debug ? 0 : -1 };
            targets[player] = target;
        }
        target.State = state;
        target.Position = new Vector3(state.X, state.Y, state.Z);
        target.Rotation = new Quaternion(state.Qx, state.Qy, state.Qz, state.Qw);
    }

    public static void Forget(byte player) => targets.Remove(player);

    // A player left the room: its body and its character go from this game too (every game does this for itself).
    public static void Remove(byte id)
    {
        targets.Remove(id);
        if (id == Identity.PlayerOf(Room.Current.SelfId)) return; // never our own
        try
        {
            var player = Find(id);
            if (player != null && player.isLocallyControlled) return;
            var populate = UnityEngine.Resources.FindObjectsOfTypeAll<SmashHammer.GearBlocks.Construction.PopulateConstructionsAsset>();
            var doomed = new List<SmashHammer.GearBlocks.Construction.Construction>();
            foreach (var entry in SmashHammer.GearBlocks.Construction.Construction.Instances)
            {
                if (entry.Value.IsPlayerCharacter && entry.Value.CharacterPlayerID == id) doomed.Add(entry.Value);
            }
            if (populate.Length > 0) foreach (var c in doomed) populate[0].DestroyConstruction(c);
            if (player != null) UnityEngine.Object.Destroy(player.gameObject);
            Plugin.Logger.LogInfo($"NET: removed player {id} ({doomed.Count} character construction(s))");
        }
        catch (Exception e)
        {
            Plugin.Logger.LogWarning($"Could not remove player {id}: {e.Message}");
        }
    }

    static void Follow_(float delta)
    {
        if (targets.Count == 0) return;
        var blend = 1f - Mathf.Exp(-Follow * Mathf.Min(delta, 0.1f));
        var logAim = Debug && Time.unscaledTime >= nextAimLog;
        List<byte> gone = null;
        foreach (var entry in targets)
        {
            var player = Find(entry.Key);
            if (player == null) continue; // not spawned (yet)
            if (player.isLocallyControlled)
            {
                (gone ??= new List<byte>()).Add(entry.Key); // that is ours, never moved from outside
                continue;
            }
            var target = entry.Value;
            // the other game decides where this body is: its own physics must not
            var networkPosition = player.GetComponent<SmashHammer.GearBlocks.Network.NetworkPlayerPosition>();
            if (networkPosition != null && networkPosition.enabled) networkPosition.enabled = false;
            var networkOrientation = player.GetComponent<SmashHammer.GearBlocks.Network.NetworkPlayerOrientation>();
            if (networkOrientation != null && networkOrientation.enabled) networkOrientation.enabled = false;
            var physics = player.characterPhysics;
            if (physics != null && physics.enabled) physics.enabled = false;
            var body = physics != null && physics.characterRigidbody != null ? physics.characterRigidbody : player.GetComponent<Rigidbody>();
            if (body != null && !body.isKinematic)
            {
                body.isKinematic = true;
                body.interpolation = RigidbodyInterpolation.None;
            }
            var t = player.transform;
            Vector3 position;
            Quaternion rotation;
            if (!target.Placed)
            {
                position = target.Position;
                rotation = target.Rotation;
                target.Placed = true;
            }
            else
            {
                position = Vector3.Lerp(t.position, target.Position, blend);
                rotation = Quaternion.Slerp(t.rotation, target.Rotation, blend);
            }
            t.SetPositionAndRotation(position, rotation);
            if (body != null)
            {
                body.position = position;
                body.rotation = rotation;
            }
            if (Time.unscaledTime >= target.NextLog && target.NextLog >= 0)
            {
                target.NextLog = Time.unscaledTime + 2f;
                Plugin.Logger.LogInfo($"NET: remote player {entry.Key} wants {target.Position} is at {t.position} (body {(body != null ? body.position.ToString() : "none")}) kinematic={(body != null && body.isKinematic)} physics={(physics != null && physics.enabled)} netpos={(networkPosition != null && networkPosition.enabled)}");
            }
            if (logAim)
            {
                try
                {
                    var aimed = player.targeter.targetedPart;
                    Plugin.Logger.LogInfo($"NET: aim of player {entry.Key}: {(aimed != null ? aimed.AssetName + " (part " + aimed.ID + ")" : "nothing")} at {t.position} yaw={target.State.Yaw} pitch={target.State.Pitch}");
                }
                catch { }
            }
            try { player.SetYawPitch(target.State.Yaw, target.State.Pitch); } catch { }
            movedCount++;
        }
        if (logAim) nextAimLog = Time.unscaledTime + 0.5f;
        if (gone != null) foreach (var id in gone) targets.Remove(id);
    }
}
