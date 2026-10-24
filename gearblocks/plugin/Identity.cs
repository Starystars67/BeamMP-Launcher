using SmashHammer.Network;

namespace BeamMP.GearBlocks;

// Every game is its own server with player 0, so in a room the players would all be player 0. The game keeps its local
// player's id in a variable and sends it in its spawn request, so each peer takes its own: the relay's peer id minus one
// (the host stays 0). The game's own spawn flow does the rest. Leaving the room puts the old value back.
internal static class Identity
{
    static bool changed;
    static byte original;

    public static void Ensure(RoomState room)
    {
        var variable = NetworkConnect.Instance?.localPlayerNetworkIDVar;
        if (variable == null) return;
        if (room.InRoom)
        {
            var wanted = (byte)(room.SelfId - 1);
            var current = variable.Value;
            if (!changed)
            {
                original = current;
                changed = true;
            }
            if (current != wanted)
            {
                variable.Value = wanted;
                Plugin.Logger.LogInfo($"NET: local player id {current} -> {wanted}");
            }
        }
        else if (changed)
        {
            changed = false;
            variable.Value = original;
            Plugin.Logger.LogInfo($"NET: local player id back to {original}");
        }
    }

    // The game's player id of a relay peer (the host is peer 1, player 0).
    public static byte PlayerOf(ushort peer) => (byte)(peer - 1);
}
