using System;
using System.Collections.Generic;
using System.Text.Json;
using BeamMP.GearBlocks;

static class Tests
{
    static int failures;
    static int checks;

    static void Check(bool condition, string what)
    {
        checks++;
        if (!condition)
        {
            failures++;
            Console.WriteLine("FAIL: " + what);
        }
    }

    static RoomState Room(string json) => RoomState.Parse(JsonDocument.Parse(json).RootElement);

    const string HostJson = "{\"game\":\"gearblocks\",\"state\":\"in_room\",\"role\":\"host\",\"code\":\"ABC234\",\"self_id\":1,\"host_id\":1,\"peers\":[{\"id\":1,\"name\":\"A\"},{\"id\":2,\"name\":\"B\"}]}";
    const string GuestJson = "{\"game\":\"gearblocks\",\"state\":\"in_room\",\"role\":\"guest\",\"code\":\"ABC234\",\"self_id\":2,\"host_id\":1,\"peers\":[{\"id\":1,\"name\":\"A\"},{\"id\":2,\"name\":\"B\"}]}";

    static int Main()
    {
        WireRoundTrips();
        WireRefusesBadFrames();
        WireStrings();
        RoomParsing();
        PolicyRules();
        BroadcastLogRules();
        PlayerStateRules();
        PhysicsRules();
        OwnershipRules();
        ReplayRules();
        GateRules();
        Console.WriteLine($"{checks - failures}/{checks} checks passed");
        return failures == 0 ? 0 : 1;
    }

    static void WireRoundTrips()
    {
        foreach (var length in new[] { 0, 1, 3, 255, 70000, Wire.MaxPayloadBytes })
        {
            var payload = new byte[length];
            for (int i = 0; i < length; i++) payload[i] = (byte)(i * 7);
            foreach (RpcKind kind in new[] { RpcKind.Request, RpcKind.Broadcast, RpcKind.ToPlayer })
            {
                var frame = Wire.Encode(kind, 0xdeadbeef, 9, payload);
                Check(frame.Length == Wire.HeaderBytes + length, "frame length " + length);
                Check(Wire.TryDecode(frame, out var k, out var h, out var p, out var data), "decodes " + kind + " " + length);
                Check(k == kind && h == 0xdeadbeef && p == 9, "header survives " + kind);
                Check(data.AsSpan().SequenceEqual(payload), "payload survives " + length);
            }
        }
        var le = Wire.Encode(RpcKind.Request, 0x01020304, 0, new byte[0]);
        Check(le[1] == 4 && le[2] == 3 && le[3] == 2 && le[4] == 1, "handle is little endian");
        Check(Wire.TryDecode(Wire.Encode(RpcKind.Request, 0, 0, new byte[0]), out _, out var zero, out _, out _) && zero == 0, "handle 0");
        Check(Wire.TryDecode(Wire.Encode(RpcKind.Request, uint.MaxValue, 255, new byte[0]), out _, out var max, out var player, out _) && max == uint.MaxValue && player == 255, "largest values");
    }

    static void WireRefusesBadFrames()
    {
        Check(!Wire.TryDecode(null, out _, out _, out _, out _), "null");
        Check(!Wire.TryDecode(new byte[0], out _, out _, out _, out _), "empty");
        Check(!Wire.TryDecode(new byte[5], out _, out _, out _, out _), "short header");
        Check(!Wire.TryDecode(new byte[] { 0, 0, 0, 0, 0, 0 }, out _, out _, out _, out _), "kind 0");
        Check(!Wire.TryDecode(new byte[] { 9, 0, 0, 0, 0, 0 }, out _, out _, out _, out _), "kind 9");
        Check(Wire.TryDecode(new byte[] { 4, 0, 0, 0, 0, 7 }, out var state, out _, out var pl, out _) && state == RpcKind.State && pl == 7, "kind 4 is a state");
        Check(!Wire.TryDecode(new byte[] { 255, 0, 0, 0, 0, 0 }, out _, out _, out _, out _), "kind 255");
        Check(!Wire.TryDecode(new byte[Wire.HeaderBytes + Wire.MaxPayloadBytes + 1].AsSpan().ToArray(), out _, out _, out _, out _), "oversize (kind 0 anyway)");
        var big = new byte[Wire.HeaderBytes + Wire.MaxPayloadBytes + 1];
        big[0] = 1;
        Check(!Wire.TryDecode(big, out _, out _, out _, out _), "oversize payload");
        bool threw = false;
        try { Wire.Encode(RpcKind.Request, 1, 0, new byte[Wire.MaxPayloadBytes + 1]); } catch (ArgumentException) { threw = true; }
        Check(threw, "encode refuses oversize");
    }

    static void RoomParsing()
    {
        var host = Room(HostJson);
        Check(host.InRoom && host.IsHost && host.SelfId == 1 && host.HostId == 1 && host.Code == "ABC234", "host room");
        Check(host.Peers.Count == 2, "host peers");
        var guest = Room(GuestJson);
        Check(guest.InRoom && !guest.IsHost && guest.SelfId == 2 && guest.HostId == 1, "guest room");
        Check(guest.PeerForPlayer(0) == 1 && guest.PeerForPlayer(1) == 2, "players map to peers");
        Check(guest.PeerForPlayer(5) == 0, "unknown player has no peer");
        var bad = new[]
        {
            "{\"state\":\"idle\"}",
            "{\"state\":\"connecting\"}",
            "{\"state\":\"in_room\"}",
            "{\"state\":\"in_room\",\"role\":\"host\",\"self_id\":1}",
            "{\"state\":\"in_room\",\"role\":\"host\",\"self_id\":\"1\",\"host_id\":1}",
            "{\"state\":\"in_room\",\"role\":\"host\",\"self_id\":70000,\"host_id\":1}",
            "{\"state\":\"in_room\",\"role\":\"host\",\"self_id\":1,\"host_id\":1,\"peers\":[{\"name\":\"x\"}]}",
            "[]", "5", "null", "{}",
        };
        foreach (var json in bad) Check(!Room(json).InRoom, "not a room: " + json);
        Check(!RoomState.Parse(default).InRoom, "default element");
        Check(RoomState.None.PeerForPlayer(0) == 0, "no room has no peers");
    }

    static void PolicyRules()
    {
        var none = RoomState.None;
        var host = Room(HostJson);
        var guest = Room(GuestJson);
        Check(!Policy.ForwardRequest(none) && !Policy.ForwardRequest(host) && Policy.ForwardRequest(guest), "only a guest forwards requests");
        Check(!Policy.ForwardBroadcast(none) && Policy.ForwardBroadcast(host) && !Policy.ForwardBroadcast(guest), "only a host forwards broadcasts");

        Check(Policy.Accept(host, RpcKind.Request, "ToServer_RequestSpawnPart", 2), "host serves a request");
        Check(!Policy.Accept(host, RpcKind.Request, "FromServer_DeserializePlayer", 2), "a request cannot run a server method");
        Check(!Policy.Accept(host, RpcKind.Request, "ToServer_Anything", 1), "not from itself");
        Check(!Policy.Accept(host, RpcKind.Broadcast, "FromServer_X", 2), "a host takes no broadcast");
        Check(!Policy.Accept(host, RpcKind.ToPlayer, "FromServer_X", 2), "a host takes nothing for a player");
        Check(!Policy.Accept(guest, RpcKind.Request, "ToServer_X", 1), "a guest serves no request");
        Check(Policy.Accept(guest, RpcKind.Request, "ToServer_SetControlValue", 1), "a guest takes the host's passing on of control input");
        Check(!Policy.Accept(guest, RpcKind.Request, "ToServer_SetControlValue", 3), "not from another guest");
        Check(!Policy.Accept(guest, RpcKind.Request, "ToServer_NotifyBeginUse", 1), "and nothing else");
        Check(Policy.Accept(guest, RpcKind.Broadcast, "FromServer_X", 1), "guest takes the host's broadcast");
        Check(Policy.Accept(guest, RpcKind.ToPlayer, "FromServer_X", 1), "guest takes what the host sends it");
        Check(!Policy.Accept(guest, RpcKind.Broadcast, "FromServer_X", 3), "not from another guest");
        Check(!Policy.Accept(guest, RpcKind.Broadcast, "ToServer_X", 1), "a broadcast cannot run a request method");
        Check(!Policy.Accept(guest, RpcKind.Broadcast, null, 1), "unknown method");
        Check(!Policy.Accept(guest, RpcKind.Broadcast, "fromserver_X", 1), "case matters");
        Check(!Policy.Accept(none, RpcKind.Broadcast, "FromServer_X", 1), "no room, nothing runs");
        Check(!Policy.Accept(none, RpcKind.Request, "ToServer_X", 2), "no room, no request");
        Check(!Policy.Accept(guest, (RpcKind)9, "FromServer_X", 1), "unknown kind");
    }

    static byte[] B(params byte[] bytes) => bytes;

    static void BroadcastLogRules()
    {
        var log = new BroadcastLog();
        log.Add(1, B(1));
        log.Add(2, B(2));
        log.Add(2, B(2)); // the same again
        log.Add(2, B(3));
        log.Add(1, B(2));
        var all = log.Snapshot();
        Check(all.Length == 4 && log.Count == 4, "a repeat of the last entry is not kept");
        Check(all[0].Handle == 1 && all[1].Handle == 2 && all[2].Payload[0] == 3 && all[3].Handle == 1, "order is kept");
        log.Add(2, B(3)); // not the last one: kept
        Check(log.Count == 5, "a repeat of an older entry is kept");
        // a snapshot is not changed by what is added later
        Check(all.Length == 4, "snapshot is a copy");
        log.Reset();
        Check(log.Count == 0 && log.Bytes == 0 && !log.Truncated, "reset empties it");

        for (int i = 0; i < BroadcastLog.MaxEntries + 100; i++) log.Add(7, new[] { (byte)i, (byte)(i >> 8), (byte)(i >> 16) });
        Check(log.Count == BroadcastLog.MaxEntries && log.Truncated, "the entry count is capped");
        var capped = log.Snapshot();
        Check(capped[0].Payload[0] == 0 && capped[0].Payload[1] == 0, "the first entry (the scene) is never dropped");
        Check(capped[capped.Length - 1].Payload[0] == unchecked((byte)(BroadcastLog.MaxEntries + 99)), "the newest entry is kept");

        log.Reset();
        log.Add(1, new byte[10]);
        for (int i = 0; i < 40; i++) log.Add(2, MakeBlock(i));
        Check(log.Bytes <= BroadcastLog.MaxBytes && log.Truncated, "the size is capped");
        Check(log.Snapshot()[0].Payload.Length == 10, "scene kept under the size cap");
    }

    static byte[] MakeBlock(int seed)
    {
        var block = new byte[1024 * 1024];
        block[0] = (byte)seed;
        return block;
    }

    static void ReplayRules()
    {
        var entries = new BroadcastLog.Entry[10];
        for (int i = 0; i < entries.Length; i++) entries[i] = new BroadcastLog.Entry { Handle = (uint)i, Payload = new byte[100] };
        var queue = new ReplayQueue();
        Check(queue.Idle, "idle at start");
        queue.Start(2, entries);
        Check(!queue.Idle, "busy after start");
        var first = queue.Take(350);
        Check(first.Count == 3 && first[0].Key == 2 && first[0].Value.Handle == 0 && first[2].Value.Handle == 2, "a portion in order within the budget");
        var second = queue.Take(1);
        Check(second.Count == 1 && second[0].Value.Handle == 3, "at least one entry per take");
        int rest = 0;
        uint expected = 4;
        bool ordered = true;
        while (!queue.Idle)
        {
            foreach (var item in queue.Take(250)) { ordered &= item.Value.Handle == expected++; rest++; }
        }
        Check(rest == 6 && ordered, "everything is delivered once, in order");
        Check(queue.Take(1000).Count == 0, "nothing after the end");

        queue.Start(2, entries);
        queue.Start(3, entries);
        var both = queue.Take(1000);
        int forTwo = 0, forThree = 0;
        foreach (var item in both) { if (item.Key == 2) forTwo++; else forThree++; }
        Check(forTwo > 0 && forThree > 0, "players are served side by side");
        queue.Cancel(2);
        queue.Cancel(3);
        Check(queue.Idle, "cancel removes the jobs");
        queue.Start(2, entries);
        queue.Take(150);
        queue.Start(2, entries); // joined again: starts over
        Check(queue.Take(100000).Count == entries.Length, "a second start replaces the first");
        queue.Start(4, new BroadcastLog.Entry[0]);
        Check(queue.Idle, "an empty log is nothing to send");
    }

    static void GateRules()
    {
        var gate = new InboundGate<int>();
        Check(!gate.Holding && !gate.Offer(1), "open gate lets items through");
        gate.Hold(100, 60);
        Check(gate.Holding && gate.Offer(2) && gate.Offer(3), "closed gate keeps items");
        Check(gate.Count == 2 && !gate.Expired(159) && gate.Expired(160), "expires after the limit");
        var released = gate.Release();
        Check(released.Length == 2 && released[0] == 2 && released[1] == 3 && !gate.Holding && gate.Count == 0, "release hands over in order and opens");
        Check(!gate.Expired(1000), "an open gate does not expire");

        gate.Hold(0, 10);
        gate.Offer(4);
        gate.Offer(5);
        gate.Offer(6);
        var items = gate.Release();
        // while applying 4 a scene closes the gate again: 5 and 6 go back, in front of anything that arrived since
        gate.Reclose(1, 10, new[] { items[1], items[2] });
        gate.Offer(7);
        Check(gate.Holding && gate.Count == 3, "reclose keeps the rest");
        var again = gate.Release();
        Check(again.Length == 3 && again[0] == 5 && again[1] == 6 && again[2] == 7, "the rest comes before newer items");
    }

    static void WireStrings()
    {
        Check(Wire.EncodeString("Concrete Pad").AsSpan().SequenceEqual(System.Text.Encoding.ASCII.GetBytes("\u000cConcrete Pad")), "string as the game writes it");
        Check(Wire.EncodeString("").Length == 1 && Wire.EncodeString("")[0] == 0, "empty string");
        var long200 = Wire.EncodeString(new string('a', 200));
        Check(long200.Length == 202 && long200[0] == 0xc8 && long200[1] == 0x01, "length over 127 takes two bytes");
        var umlaut = Wire.EncodeString("\u00e4");
        Check(umlaut.Length == 3 && umlaut[0] == 2, "length counts bytes, not characters");
        // what BinaryWriter itself writes
        using var stream = new System.IO.MemoryStream();
        using (var writer = new System.IO.BinaryWriter(stream)) writer.Write("Gary Island \u00e4" + new string('x', 300));
        Check(stream.ToArray().AsSpan().SequenceEqual(Wire.EncodeString("Gary Island \u00e4" + new string('x', 300))), "same bytes as BinaryWriter");
    }

    static void PlayerStateRules()
    {
        var state = new PlayerState { X = 1.5f, Y = -2f, Z = 300.25f, Qx = 0, Qy = 0.7071068f, Qz = 0, Qw = 0.7071068f, Yaw = 90, Pitch = -10, Vx = 1, Vy = 2, Vz = 3, Tool = 4 };
        var bytes = state.Encode();
        Check(bytes.Length == PlayerState.Bytes && PlayerState.Bytes == 49, "state size");
        Check(PlayerState.TryDecode(bytes, out var back), "state decodes");
        Check(back.X == 1.5f && back.Y == -2f && back.Z == 300.25f && back.Qy == 0.7071068f && back.Yaw == 90 && back.Pitch == -10 && back.Vz == 3 && back.Tool == 4, "state survives");
        Check(bytes[0] == 0 && bytes[1] == 0 && bytes[2] == 0xc0 && bytes[3] == 0x3f, "floats are little endian (1.5)");
        Check(!PlayerState.TryDecode(null, out _) && !PlayerState.TryDecode(new byte[48], out _) && !PlayerState.TryDecode(new byte[50], out _), "wrong sizes");
        foreach (var bad in new[] { float.NaN, float.PositiveInfinity, 2e6f })
        {
            var broken = new PlayerState { X = bad, Qw = 1 };
            Check(!PlayerState.TryDecode(broken.Encode(), out _), "unusable position " + bad);
            var brokenV = new PlayerState { Vy = bad, Qw = 1 };
            Check(!PlayerState.TryDecode(brokenV.Encode(), out _), "unusable velocity " + bad);
        }
        Check(!PlayerState.TryDecode(new PlayerState().Encode(), out _), "all zero is not a rotation");
        Check(!PlayerState.TryDecode(new PlayerState { Qw = 3 }.Encode(), out _), "not a unit rotation");

        var host = Room(HostJson);
        var guest = Room(GuestJson);
        Check(Policy.AcceptState(host, 2, 1) && Policy.AcceptState(guest, 1, 0), "a peer moves its own player");
        Check(!Policy.AcceptState(host, 2, 0) && !Policy.AcceptState(host, 2, 2) && !Policy.AcceptState(guest, 1, 1), "not somebody else's");
        Check(!Policy.AcceptState(host, 1, 0) && !Policy.AcceptState(guest, 2, 1), "not from itself");
        Check(!Policy.AcceptState(RoomState.None, 2, 1), "no room");
        Check(!Policy.Accept(host, RpcKind.State, "FromServer_X", 2), "a state is never a method call");
    }

    static BodyPose Pose(float x, float y, float z, float qy = 0, float qw = 1) => new BodyPose { X = x, Y = y, Z = z, Qy = qy, Qw = qw };

    static void PhysicsRules()
    {
        var bodies = new[] { Pose(1, 2, 3), Pose(-4.5f, 0.25f, 100), Pose(0, 0, 0, 0.7071068f, 0.7071068f) };
        var bytes = PhysicsFrame.Encode(bodies);
        Check(bytes.Length == 3 * BodyPose.Bytes && BodyPose.Bytes == 52, "frame size");
        Check(PhysicsFrame.TryDecode(bytes, out var back) && back.Length == 3, "frame decodes");
        Check(back[1].X == -4.5f && back[1].Y == 0.25f && back[1].Z == 100 && back[2].Qy == 0.7071068f && back[0].Qw == 1, "poses survive");
        Check(!PhysicsFrame.TryDecode(null, out _) && !PhysicsFrame.TryDecode(new byte[0], out _) && !PhysicsFrame.TryDecode(new byte[51], out _) && !PhysicsFrame.TryDecode(new byte[53], out _), "wrong sizes");
        var max = new BodyPose[PhysicsFrame.MaxBodies];
        for (int i = 0; i < max.Length; i++) max[i] = Pose(i, 0, 0);
        Check(PhysicsFrame.TryDecode(PhysicsFrame.Encode(max), out var many) && many.Length == PhysicsFrame.MaxBodies, "the largest frame");
        bool threw = false;
        try { PhysicsFrame.Encode(new BodyPose[PhysicsFrame.MaxBodies + 1]); } catch (ArgumentException) { threw = true; }
        Check(threw, "encode refuses too many bodies");
        Check(!PhysicsFrame.TryDecode(new byte[(PhysicsFrame.MaxBodies + 1) * BodyPose.Bytes], out _), "decode refuses too many bodies");
        foreach (var bad in new[] { float.NaN, float.PositiveInfinity, -3e6f })
        {
            var broken = new[] { Pose(0, 0, 0), Pose(bad, 0, 0) };
            Check(!PhysicsFrame.TryDecode(PhysicsFrame.Encode(broken), out _), "unusable position " + bad);
        }
        Check(!PhysicsFrame.TryDecode(PhysicsFrame.Encode(new[] { new BodyPose() }), out _), "all zero is not a rotation");
        Check(!PhysicsFrame.TryDecode(PhysicsFrame.Encode(new[] { Pose(0, 0, 0, 0, 3) }), out _), "not a unit rotation");

        var withSpeed = new[] { new BodyPose { X = 1, Qw = 1, Vx = 5, Vy = -6, Vz = 7, Wx = 1, Wy = 2, Wz = 3 } };
        Check(PhysicsFrame.TryDecode(PhysicsFrame.Encode(withSpeed), out var fast) && fast[0].Vx == 5 && fast[0].Vy == -6 && fast[0].Vz == 7 && fast[0].Wx == 1 && fast[0].Wz == 3, "speeds survive");
        Check(!PhysicsFrame.TryDecode(PhysicsFrame.Encode(new[] { new BodyPose { Qw = 1, Vx = 2e4f } }), out _), "an absurd speed is refused");
        Check(!PhysicsFrame.TryDecode(PhysicsFrame.Encode(new[] { new BodyPose { Qw = 1, Wy = float.NaN } }), out _), "a speed that is not a number is refused");
        var a = new[] { Pose(1, 2, 3), Pose(4, 5, 6) };
        var same = new[] { Pose(1, 2, 3), Pose(4, 5, 6) };
        Check(!PhysicsFrame.Moved(a, same), "nothing moved");
        Check(PhysicsFrame.Moved(null, a) && PhysicsFrame.Moved(new[] { Pose(1, 2, 3) }, a), "no earlier frame, or another number of bodies");
        Check(!PhysicsFrame.Moved(a, new[] { Pose(1.004f, 2, 3), Pose(4, 5, 6) }), "four millimetres is not movement");
        Check(PhysicsFrame.Moved(a, new[] { Pose(1.007f, 2, 3), Pose(4, 5, 6) }), "seven millimetres is");
        Check(PhysicsFrame.Moved(a, new[] { Pose(1, 2, 3), Pose(4, 5, 6.02f) }), "the second body moving counts");
        Check(PhysicsFrame.Moved(new[] { Pose(0, 0, 0) }, new[] { Pose(0, 0, 0, 0.0087f, 0.99996f) }), "a turn of one degree is movement");
        Check(!PhysicsFrame.Moved(new[] { Pose(0, 0, 0) }, new[] { Pose(0, 0, 0, 0, -1) }), "the same rotation written with the other sign is none");
        Check(!PhysicsFrame.Moved(new[] { Pose(0, 0, 0) }, new[] { Pose(0, 0, 0, 0.002f, 0.999998f) }), "a quarter of a degree of turn is not movement");
        Check(PhysicsFrame.MinInterval(1) == 0.05f && PhysicsFrame.MinInterval(24) == 0.05f && Math.Abs(PhysicsFrame.MinInterval(25) - 0.1f) < 1e-6 && Math.Abs(PhysicsFrame.MinInterval(96) - 0.2f) < 1e-6 && Math.Abs(PhysicsFrame.MinInterval(128) - 0.30f) < 1e-6, "big constructions are sent less often");

        var host = Room(HostJson);
        var guest = Room(GuestJson);
        Check(Policy.AcceptPhysics(guest, 1, 1), "a guest takes the owner's poses");
        Check(!Policy.AcceptPhysics(guest, 3, 1) && !Policy.AcceptPhysics(guest, 1, 3) && !Policy.AcceptPhysics(RoomState.None, 1, 1), "not from anybody but the owner, not without a room");
        Check(Policy.AcceptPhysics(host, 2, 2) && !Policy.AcceptPhysics(host, 2, 1) && !Policy.AcceptPhysics(host, 1, 1), "a host takes a guest's poses only for what that guest owns, never its own");
        Check(!Policy.Accept(guest, RpcKind.Physics, "FromServer_X", 1), "poses are never a method call");
        Check(Wire.TryDecode(Wire.Encode(RpcKind.Physics, 7, 0, bytes), out var kind, out var construction, out _, out var payload) && kind == RpcKind.Physics && construction == 7 && payload.Length == bytes.Length, "poses travel in a frame");
    }

    static void OwnershipRules()
    {
        var table = new OwnerTable();
        table.Reset(1);
        Check(table.OwnerOf(5) == 1 && table.Host == 1, "the host owns what nobody took");
        Check(table.Set(5, 2) && table.OwnerOf(5) == 2 && table.IsOwnedBy(5, 2) && !table.IsOwnedBy(5, 1), "a guest takes a vehicle");
        Check(!table.Set(5, 2), "taking it again changes nothing");
        Check(table.Set(7, 3) && table.OwnerOf(7) == 3 && table.OwnerOf(5) == 2, "owners are per vehicle");
        Check(table.NotTheHosts().Count == 2, "what is not the host's");
        Check(table.Set(5, 1) && table.OwnerOf(5) == 1 && table.NotTheHosts().Count == 1, "giving it back to the host removes the entry");
        Check(!table.Set(5, 1), "giving back what is the host's changes nothing");
        table.Set(8, 3);
        var released = table.ReleaseAllOf(3);
        released.Sort();
        Check(released.Count == 2 && released[0] == 7 && released[1] == 8 && table.OwnerOf(7) == 1 && table.OwnerOf(8) == 1, "a peer that is gone gives back all it had");
        Check(table.ReleaseAllOf(9).Count == 0, "a peer that owned nothing");
        table.Set(1, 2);
        table.Reset(4);
        Check(table.OwnerOf(1) == 4 && table.NotTheHosts().Count == 0, "a new room starts from nothing, with its own host");

        var host = Room(HostJson);
        var guest = Room(GuestJson);
        Check(Policy.AcceptOwnerRequest(host, 2) && !Policy.AcceptOwnerRequest(host, 1) && !Policy.AcceptOwnerRequest(guest, 1) && !Policy.AcceptOwnerRequest(RoomState.None, 2), "only a host takes requests, from others");
        Check(Policy.AcceptOwner(guest, 1, 2) && Policy.AcceptOwner(guest, 1, 1), "a guest takes the host's word, naming somebody in the room");
        Check(!Policy.AcceptOwner(guest, 1, 9) && !Policy.AcceptOwner(guest, 3, 2) && !Policy.AcceptOwner(host, 2, 2) && !Policy.AcceptOwner(RoomState.None, 1, 1), "not for a peer that is not there, not from a guest, not on a host, not without a room");
        Check(!Policy.Accept(host, RpcKind.OwnerRequest, "ToServer_X", 2) && !Policy.Accept(guest, RpcKind.Owner, "FromServer_X", 1) && !Policy.Accept(host, RpcKind.OwnerRelease, "ToServer_X", 2), "ownership is never a method call");
        Check(Wire.TryDecode(Wire.Encode(RpcKind.Owner, 12, 3, new byte[0]), out var kind, out var construction, out var peer, out _) && kind == RpcKind.Owner && construction == 12 && peer == 3, "an owner message travels in a frame");
        Check(Wire.TryDecode(Wire.Encode(RpcKind.OwnerRequest, 12, 0, new byte[0]), out kind, out _, out _, out _) && kind == RpcKind.OwnerRequest, "a request travels in a frame");
        Check(Wire.TryDecode(Wire.Encode(RpcKind.OwnerRelease, 12, 0, new byte[0]), out kind, out _, out _, out _) && kind == RpcKind.OwnerRelease, "a release travels in a frame");
    }
}
