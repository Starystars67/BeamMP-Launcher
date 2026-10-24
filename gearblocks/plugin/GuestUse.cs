using System;
using HarmonyLib;
using SmashHammer.GearBlocks.Construction;
using SmashHammer.GearBlocks.Toolbox;
using SmashHammer.Network;

namespace BeamMP.GearBlocks;

// When the local player uses a part (sits in a seat, takes a control) a game that is the server just does it, without
// ever making the call to the server that a client's game makes, so on a guest the host would never hear of it. Here
// the guest also makes that call (the game's own, with the part's id) to the host, and the game still does its own part:
// the player sits in this game's copy of the vehicle (which is what lets its controls work: they need the player
// seated), and the host's copy of that player follows from the pose this game streams. The host carries out the use for
// that player too (`HandleUse`); making its game seat a remote player (the game's own seat logic refuses a player it
// has not seated itself) is not done. Making the game believe it is a client instead was tried: it stalled the game,
// and its handling of the host's players expects a real client's state.
[HarmonyPatch]
internal static class GuestUse
{
    [HarmonyPrefix]
    [HarmonyPatch(typeof(Targeter), "BeginUse")]
    static bool Begin(Targeter __instance, PartDescriptor useablePart) => Forward(__instance, useablePart, true);

    [HarmonyPrefix]
    [HarmonyPatch(typeof(Targeter), "EndUse")]
    static bool End(Targeter __instance, PartDescriptor useablePart) => Forward(__instance, useablePart, false);

    static bool Forward(Targeter targeter, PartDescriptor part, bool begin)
    {
        try
        {
            var room = Room.Current;
            if (part == null || !Policy.ForwardRequest(room) || !Replicator.InWorld || !targeter.isLocallyControlled) return true;
            var handle = begin ? targeter.notifyBeginUseMethod : targeter.notifyEndUseMethod;
            // an ordinary call to the server: the replicator sends it on to the host
            NetworkRemoteMethods.Instance.CallFromPlayerToServer(handle, RemoteMethods.ToGame(BitConverter.GetBytes(part.ID)));
            return true; // and the game does its own part: the player sits (and so may use the controls) in its own copy
        }
        catch (Exception e)
        {
            Plugin.Logger.LogWarning("Could not send the use of a part to the host: " + e.Message);
            return true;
        }
    }

    const float MaxUseDistance = 12f;

    // Host: another player's use of a part. The game decides what a player may use by what that player's targeter is
    // aiming at, and a remote player's targeter never aims (its game does that) and its server-side seat logic refuses,
    // so the part the guest names is checked here (it exists, can be used, the player's body is within reach) and then
    // used the way the targeter does it on a server: every usable behaviour of the part is told the player began (or
    // ended) using it. Returns whether the call is dealt with (the game's own handler is then not run); true for
    // every other call.
    internal static bool HandleUse(uint handle, byte[] payload, out bool handled)
    {
        handled = false;
        try
        {
            var name = RemoteMethods.MethodName(handle);
            if (name != "ToServer_NotifyBeginUse" && name != "ToServer_NotifyEndUse") return true;
            handled = true;
            if (payload.Length != 2) return false;
            var id = BitConverter.ToUInt16(payload, 0);
            var targeter = NetworkRemoteMethods.remoteMethods[handle].Target?.TryCast<Targeter>();
            if (targeter == null || targeter.isLocallyControlled) return false;
            if (!PartDescriptor.Instances.ContainsKey(id)) return false;
            var part = PartDescriptor.Instances[id];
            if (part == null || !part.HasUseableBehaviours) return false;
            if (UnityEngine.Vector3.Distance(targeter.transform.position, part.transform.position) > MaxUseDistance) return false;
            var player = targeter.playerID;
            targeter.targetedPart = part; // the seat logic asks the targeter what the player is aiming at
            foreach (var behaviour in part.UseableBehaviours)
            {
                if (behaviour == null) continue;
                if (name == "ToServer_NotifyBeginUse") behaviour.OnBeginUse(player);
                else behaviour.OnEndUse(player);
            }
            if (Replicator.Trace) Plugin.Logger.LogInfo($"NET: player {player} {(name == "ToServer_NotifyBeginUse" ? "began" : "ended")} using part {id} ({part.AssetName})");
            return true;
        }
        catch (Exception e)
        {
            Plugin.Logger.LogWarning("Could not carry out the use of a part: " + e.Message);
            return false;
        }
    }
}
