namespace BeamMP.GearBlocks;

// Who may make the game run what, and which of its own calls go over the relay. The game trusts its own netcode
// completely, so everything that comes from another player is checked here first.
internal static class Policy
{
    public const string RequestPrefix = "ToServer_";
    public const string BroadcastPrefix = "FromServer_";
    public const string ControlMethod = "ToServer_SetControlValue";

    // The player's own game asks the server for something. In a room as guest the host's game is the server, so
    // the call goes there and is not run here; otherwise the game's own server handles it as before.
    public static bool ForwardRequest(RoomState room) => room.InRoom && !room.IsHost;

    // The game (as the server) tells the players what happened: only a host has anyone to tell.
    public static bool ForwardBroadcast(RoomState room) => room.InRoom && room.IsHost;

    // Whether a frame from another player may run `method` here. `from` is the sender the relay vouches for.
    public static bool Accept(RoomState room, RpcKind kind, string method, ushort from)
    {
        if (!room.InRoom || method == null || from == room.SelfId) return false;
        switch (kind)
        {
            case RpcKind.State:
            case RpcKind.Physics:
            case RpcKind.OwnerRequest:
            case RpcKind.OwnerRelease:
            case RpcKind.Owner:
                // handled by their own rules below, never by a method name
                return false;
            case RpcKind.Request:
                // a host serves requests, for methods a player may ask for; a guest only takes the host's passing on
                // of somebody's control input for a vehicle this guest simulates (the vehicle is checked by the caller)
                if (room.IsHost) return method.StartsWith(RequestPrefix, System.StringComparison.Ordinal);
                return from == room.HostId && method == ControlMethod;
            case RpcKind.Broadcast:
            case RpcKind.ToPlayer:
                // only the host speaks for the server, and only through the server's methods
                return !room.IsHost && from == room.HostId && method.StartsWith(BroadcastPrefix, System.StringComparison.Ordinal);
            default:
                return false;
        }
    }

    // A player's position from another player: only for the player that peer is, so nobody moves somebody else.
    public static bool AcceptState(RoomState room, ushort from, byte player)
        => room.InRoom && from != room.SelfId && player == (byte)(from - 1);

    // The poses of a vehicle's bodies come from whoever simulates that vehicle, nobody else.
    public static bool AcceptPhysics(RoomState room, ushort from, ushort owner)
        => room.InRoom && from != room.SelfId && from == owner;

    // Only a host decides who simulates what, and only for a request that comes from somebody else.
    public static bool AcceptOwnerRequest(RoomState room, ushort from)
        => room.InRoom && room.IsHost && from != room.SelfId;

    // The host's word on who simulates a vehicle now (the peer it names has to be somebody in the room).
    public static bool AcceptOwner(RoomState room, ushort from, ushort newOwner)
    {
        if (!room.InRoom || room.IsHost || from != room.HostId) return false;
        foreach (var peer in room.Peers)
        {
            if (peer == newOwner) return true;
        }
        return false;
    }
}
