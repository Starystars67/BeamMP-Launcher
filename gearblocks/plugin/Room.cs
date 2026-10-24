using System;
using System.Collections.Generic;
using System.Text.Json;

namespace BeamMP.GearBlocks;

// Where the player stands in a relay room, as the launcher last said (a `session` message). Immutable: readers
// take the current snapshot, the bridge's thread replaces it.
internal sealed class RoomState
{
    public static readonly RoomState None = new RoomState();

    public bool InRoom { get; private init; }
    public bool IsHost { get; private init; }
    public ushort SelfId { get; private init; }
    public ushort HostId { get; private init; }
    public string Code { get; private init; } = "";
    public IReadOnlyList<ushort> Peers { get; private init; } = Array.Empty<ushort>();

    // The `session` object of the launcher's message (see docs/CONTROL_PROTOCOL.md); anything else is no room.
    public static RoomState Parse(JsonElement session)
    {
        try
        {
            if (session.ValueKind != JsonValueKind.Object) return None;
            if (!session.TryGetProperty("state", out var state) || state.GetString() != "in_room") return None;
            var role = session.GetProperty("role").GetString();
            var peers = new List<ushort>();
            if (session.TryGetProperty("peers", out var list) && list.ValueKind == JsonValueKind.Array)
            {
                foreach (var peer in list.EnumerateArray()) peers.Add(peer.GetProperty("id").GetUInt16());
            }
            return new RoomState
            {
                InRoom = true,
                IsHost = role == "host",
                SelfId = session.GetProperty("self_id").GetUInt16(),
                HostId = session.GetProperty("host_id").GetUInt16(),
                Code = session.TryGetProperty("code", out var code) ? code.GetString() ?? "" : "",
                Peers = peers,
            };
        }
        catch (Exception e) when (e is JsonException || e is InvalidOperationException || e is KeyNotFoundException || e is FormatException)
        {
            return None;
        }
    }

    // Provisional until players are mapped (milestone 3): the game numbers players from 0 and the relay peers from 1.
    public ushort PeerForPlayer(byte player)
    {
        var peer = (ushort)(player + 1);
        foreach (var known in Peers)
        {
            if (known == peer) return peer;
        }
        return 0;
    }
}

internal static class Room
{
    static volatile RoomState current = RoomState.None;

    public static RoomState Current => current;
    public static void Set(RoomState state) => current = state;
}
