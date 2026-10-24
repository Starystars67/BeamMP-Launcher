using System;
using System.Collections.Generic;
using System.Text;
using SmashHammer.GearBlocks.Construction;
using UnityEngine;

namespace BeamMP.GearBlocks;

// Vehicles (any construction that is not frozen) are simulated by one game each, the owner's: the host's by default, the
// guest's whose player sits in it once it has asked and the host agreed (so that its steering does not wait for a round
// trip). The owner sends the poses and speeds of the bodies; every other game holds its copy still with its physics and
// moves it to where the owner says. What the others do with a vehicle reaches the owner as the game's own calls: a
// guest's input for a vehicle somebody else owns goes to the host and on to that owner.
internal static class VehicleSync
{
    const float SendInterval = 1f / 20f;
    const float KeepAliveSeconds = 1f;
    const float Follow = 25f;
    const float WatchInterval = 0.25f;

    sealed class Sent
    {
        public BodyPose[] Last;
        public float At;
    }

    sealed class Target
    {
        public BodyPose[] Poses;
        public bool Placed;
    }

    public static readonly OwnerTable Owners = new();
    static readonly Dictionary<ushort, Sent> sent = new();
    static readonly Dictionary<ushort, Target> targets = new();
    static readonly HashSet<ushort> tooBig = new();
    static readonly Dictionary<uint, ushort> controlOwner = new();
    static float nextSend;
    static float nextWatch;
    static ushort requested; // the vehicle this guest asked for (0 = none; vehicle ids start at 0, see requestedSet)
    static bool requestedSet;
    static int sentCount;
    static int appliedCount;
    static int grants;
    static ushort? lastLogged;

    public static string Summary => $"vehicles sent={sentCount} followed={targets.Count} applied={appliedCount} owned elsewhere={Owners.NotTheHosts().Count}";

    static List<Composite> BodiesOf(Construction c)
    {
        var bodies = new List<Composite>();
        foreach (var composite in c.Composites)
        {
            if (composite != null && composite.Rigidbody != null) bodies.Add(composite);
        }
        return bodies;
    }

    static Construction Find(ushort id)
    {
        try { return Construction.Instances.ContainsKey(id) ? Construction.Instances[id] : null; } catch { return null; }
    }

    // ---- ownership ---------------------------------------------------------------------------------------------

    // A room began (or its host is another): nothing is owned by anybody but the host.
    public static void NewRoom(ushort host)
    {
        Owners.Reset(host);
        sent.Clear();
        requestedSet = false;
    }

    static void SendControl(RpcKind kind, ushort construction, byte player, string target, ushort peer)
    {
        var frame = Wire.Encode(kind, construction, player, new byte[0]);
        var line = new StringBuilder(96);
        line.Append("{\"type\":\"data\",\"kind\":\"").Append(target).Append('"');
        if (peer != 0) line.Append(",\"target\":").Append(peer);
        line.Append(",\"data\":\"").Append(Convert.ToBase64String(frame)).Append("\"}");
        Link.Current?.Send(line.ToString());
    }

    // Host: the owner of a vehicle is now `peer`: everybody is told, and this game takes the consequence.
    static void Announce(ushort construction, ushort peer)
    {
        SendControl(RpcKind.Owner, construction, (byte)peer, "all", 0);
        Changed(construction, peer);
        if (Replicator.Trace) Plugin.Logger.LogInfo($"NET: vehicle {construction} is now simulated by peer {peer}");
    }

    // Host: a guest asks to simulate a vehicle.
    public static void OwnerRequested(RoomState room, ushort from, ushort construction)
    {
        if (!Policy.AcceptOwnerRequest(room, from)) return;
        var c = Find(construction);
        if (c == null || c.IsPlayerCharacter || c.IsFrozen) return;
        var current = Owners.OwnerOf(construction);
        if (current == from) return;
        if (current != room.HostId) return; // somebody else is driving it
        // the host's own player sitting in it keeps it
        if (LocalLockedConstruction(room) == construction) return;
        Owners.Set(construction, from);
        grants++;
        Announce(construction, from);
    }

    // Host: the guest is done with a vehicle.
    public static void OwnerReleased(RoomState room, ushort from, ushort construction)
    {
        if (!Policy.AcceptOwnerRequest(room, from)) return;
        if (!Owners.IsOwnedBy(construction, from)) return;
        Owners.Set(construction, room.HostId);
        Announce(construction, room.HostId);
    }

    // Guest: the host says who simulates a vehicle now.
    public static void OwnerAnnounced(RoomState room, ushort from, ushort construction, ushort peer)
    {
        if (!Policy.AcceptOwner(room, from, peer)) return;
        Owners.Set(construction, peer);
        Changed(construction, peer);
    }

    // Host: a peer left: what it simulated goes back to this game.
    public static void PeerLeft(RoomState room, ushort peer)
    {
        if (!room.InRoom || !room.IsHost) return;
        foreach (var construction in Owners.ReleaseAllOf(peer)) Announce(construction, room.SelfId);
    }

    // Host: a player joined, and is told who simulates what is not the host's.
    public static void PlayerJoined(RoomState room, ushort peer)
    {
        foreach (var entry in Owners.NotTheHosts()) SendControl(RpcKind.Owner, entry.Key, (byte)entry.Value, "peer", peer);
    }

    // The owner of a vehicle changed (in the table already): this game takes over, or gives up, the physics.
    static void Changed(ushort construction, ushort owner)
    {
        var room = Room.Current;
        var c = Find(construction);
        if (c == null || !room.InRoom) return;
        if (owner == room.SelfId)
        {
            targets.TryGetValue(construction, out var last);
            targets.Remove(construction);
            TakeOver(c, last?.Poses);
        }
        else
        {
            sent.Remove(construction);
            Yield(c, construction);
        }
    }

    // This game simulates the vehicle from now: its bodies are the game's again, going as the last pose says.
    static void TakeOver(Construction c, BodyPose[] last)
    {
        var bodies = BodiesOf(c);
        foreach (var composite in bodies) composite.UpdateIsKinematic(false);
        if (last == null || last.Length != bodies.Count) return;
        for (int i = 0; i < bodies.Count; i++)
        {
            var rb = bodies[i].Rigidbody;
            if (rb.isKinematic) continue;
            rb.velocity = new Vector3(last[i].Vx, last[i].Vy, last[i].Vz);
            rb.angularVelocity = new Vector3(last[i].Wx, last[i].Wy, last[i].Wz);
        }
    }

    // Somebody else simulates the vehicle from now: it is held where it is until their poses arrive.
    static void Yield(Construction c, ushort id)
    {
        if (c.IsFrozen) return;
        var bodies = BodiesOf(c);
        if (bodies.Count == 0 || bodies.Count > PhysicsFrame.MaxBodies) return;
        var poses = new BodyPose[bodies.Count];
        for (int i = 0; i < poses.Length; i++) poses[i] = PoseOf(bodies[i].Rigidbody);
        targets[id] = new Target { Poses = poses, Placed = true };
    }

    static BodyPose PoseOf(Rigidbody rb)
    {
        var p = rb.position;
        var q = rb.rotation;
        var v = rb.velocity;
        var w = rb.angularVelocity;
        return new BodyPose { X = p.x, Y = p.y, Z = p.z, Qx = q.x, Qy = q.y, Qz = q.z, Qw = q.w, Vx = v.x, Vy = v.y, Vz = v.z, Wx = w.x, Wy = w.y, Wz = w.z };
    }

    // The construction a part belongs to: through its composite (which knows its construction), or by the hierarchy.
    static Construction ConstructionOfPart(PartDescriptor part)
    {
        if (part == null) return null;
        try
        {
            var composite = part.GetComponentInParent<Composite>();
            var c = composite?.parentConstruction;
            if (c != null) return c;
        }
        catch { }
        try { return part.GetComponentInParent<Construction>(); } catch { return null; }
    }

    // The vehicle the local player sits in (its id), or none.
    static ushort? LocalLockedConstruction(RoomState room)
    {
        try
        {
            var player = Presence.LocalPlayer(room);
            if (player == null) return null;
            var locker = player.GetComponent<PlayerLocker>();
            if (locker == null || !locker.IsLocked) return null;
            var part = locker.LockedTo ?? locker.lockedTo?.Part;
            var c = ConstructionOfPart(part);
            return c == null ? null : c.ID;
        }
        catch
        {
            return null;
        }
    }

    // Every quarter second: a player sitting in a vehicle takes it over (a guest asks the host, the host just does), and
    // gives it back when it gets out.
    static void Watch(float now, RoomState room)
    {
        if (now < nextWatch) return;
        nextWatch = now + WatchInterval;
        if (!Replicator.InWorld && !room.IsHost) return;
        var locked = LocalLockedConstruction(room);
        if (Replicator.Trace && locked != lastLogged)
        {
            lastLogged = locked;
            Plugin.Logger.LogInfo($"NET: the local player sits in vehicle {(locked.HasValue ? locked.Value.ToString() : "none")}");
        }
        if (room.IsHost)
        {
            if (locked.HasValue && !Owners.IsOwnedBy(locked.Value, room.SelfId))
            {
                Owners.Set(locked.Value, room.SelfId);
                Announce(locked.Value, room.SelfId);
            }
            return;
        }
        if (locked.HasValue && !Owners.IsOwnedBy(locked.Value, room.SelfId) && !(requestedSet && requested == locked.Value))
        {
            requested = locked.Value;
            requestedSet = true;
            SendControl(RpcKind.OwnerRequest, locked.Value, 0, "host", 0);
            if (Replicator.Trace) Plugin.Logger.LogInfo($"NET: asking the host to simulate vehicle {locked.Value}");
        }
        else if (requestedSet && (!locked.HasValue || locked.Value != requested))
        {
            if (Owners.IsOwnedBy(requested, room.SelfId)) SendControl(RpcKind.OwnerRelease, requested, 0, "host", 0);
            requestedSet = false;
        }
    }

    // ---- the vehicle of a control call ------------------------------------------------------------------------

    // The construction a control call (ToServer_SetControlValue) is about, from the behaviour it is registered for.
    public static ushort? ConstructionOfControl(uint handle)
    {
        if (controlOwner.TryGetValue(handle, out var known)) return Find(known) != null ? known : null;
        try
        {
            if (RemoteMethods.MethodName(handle) != "ToServer_SetControlValue") return null;
            var behaviour = SmashHammer.Network.NetworkRemoteMethods.remoteMethods[handle].Target?.TryCast<PartBehaviourBase>();
            var c = ConstructionOfPart(behaviour?.Part);
            if (c == null) return null;
            controlOwner[handle] = c.ID;
            return c.ID;
        }
        catch
        {
            return null;
        }
    }

    // ---- sending (the owner) -----------------------------------------------------------------------------------

    public static void Tick(float now, RoomState room)
    {
        if (!room.InRoom)
        {
            if (sent.Count > 0) sent.Clear();
            return;
        }
        Watch(now, room);
        if (room.Peers.Count < 2) return;
        if (now < nextSend) return;
        nextSend = now + SendInterval;
        List<ushort> gone = null;
        foreach (var entry in Construction.Instances)
        {
            var id = entry.Key;
            var c = entry.Value;
            if (c == null || c.IsPlayerCharacter || c.IsFrozen) continue;
            if (!Owners.IsOwnedBy(id, room.SelfId)) continue;
            var bodies = BodiesOf(c);
            if (bodies.Count == 0) continue;
            if (bodies.Count > PhysicsFrame.MaxBodies)
            {
                if (tooBig.Add(id)) Plugin.Logger.LogWarning($"NET: construction {id} has {bodies.Count} bodies, too many to stream");
                continue;
            }
            if (!sent.TryGetValue(id, out var last))
            {
                last = new Sent();
                sent[id] = last;
            }
            var since = now - last.At;
            if (since < PhysicsFrame.MinInterval(bodies.Count)) continue;
            var poses = new BodyPose[bodies.Count];
            for (int i = 0; i < poses.Length; i++) poses[i] = PoseOf(bodies[i].Rigidbody);
            if (!PhysicsFrame.Moved(last.Last, poses) && since < KeepAliveSeconds) continue;
            last.Last = poses;
            last.At = now;
            Send(id, poses, room.IsHost ? "all" : "others");
        }
        foreach (var id in sent.Keys)
        {
            if (!Construction.Instances.ContainsKey(id)) (gone ??= new List<ushort>()).Add(id);
        }
        if (gone != null) foreach (var id in gone) sent.Remove(id);
    }

    static void Send(ushort id, BodyPose[] poses, string target)
    {
        var frame = Wire.Encode(RpcKind.Physics, id, 0, PhysicsFrame.Encode(poses));
        var line = new StringBuilder(64 + frame.Length * 4 / 3);
        line.Append("{\"type\":\"data\",\"kind\":\"").Append(target).Append("\",\"data\":\"").Append(Convert.ToBase64String(frame)).Append("\"}");
        var link = Link.Current;
        if (link != null && link.Send(line.ToString())) sentCount++;
    }

    // ---- following (everybody else) ----------------------------------------------------------------------------

    // The owner's word on where a construction's bodies are (checked: it is from the owner).
    public static void Remote(ushort id, BodyPose[] poses)
    {
        if (!targets.TryGetValue(id, out var target))
        {
            target = new Target();
            targets[id] = target;
        }
        target.Poses = poses;
    }

    public static void Late(float delta)
    {
        var room = Room.Current;
        if (targets.Count == 0 || !room.InRoom) return;
        var blend = 1f - Mathf.Exp(-Follow * Mathf.Min(delta, 0.1f));
        List<ushort> done = null;
        foreach (var entry in targets)
        {
            var c = Find(entry.Key);
            if (c == null || c.IsFrozen || Owners.IsOwnedBy(entry.Key, room.SelfId))
            {
                // gone, frozen (the freeze call puts it where it belongs) or simulated here
                (done ??= new List<ushort>()).Add(entry.Key);
                continue;
            }
            var bodies = BodiesOf(c);
            var poses = entry.Value.Poses;
            if (bodies.Count != poses.Length) continue; // not built the same (yet)
            for (int i = 0; i < bodies.Count; i++)
            {
                var rb = bodies[i].Rigidbody;
                if (!rb.isKinematic)
                {
                    rb.isKinematic = true;
                    rb.interpolation = RigidbodyInterpolation.None;
                }
                var pose = poses[i];
                var position = new Vector3(pose.X, pose.Y, pose.Z);
                var rotation = new Quaternion(pose.Qx, pose.Qy, pose.Qz, pose.Qw);
                if (entry.Value.Placed)
                {
                    position = Vector3.Lerp(rb.position, position, blend);
                    rotation = Quaternion.Slerp(rb.rotation, rotation, blend);
                }
                rb.position = position;
                rb.rotation = rotation;
            }
            entry.Value.Placed = true;
            appliedCount++;
        }
        if (done != null) foreach (var id in done) targets.Remove(id);
    }

    // The room is over: the bodies are the game's again.
    public static void Release()
    {
        foreach (var entry in targets)
        {
            try
            {
                var c = Find(entry.Key);
                if (c == null) continue;
                foreach (var composite in BodiesOf(c)) composite.UpdateIsKinematic(false);
            }
            catch { }
        }
        targets.Clear();
        sent.Clear();
        requestedSet = false;
        Owners.Reset(0);
    }
}
