using System;
using Il2CppInterop.Runtime.Injection;
using UnityEngine;

namespace BeamMP.GearBlocks;

// Lives in the game next to everything else: keeps the connection to the launcher up and hands the game's own
// thread the data other players sent (game objects may only be touched from there).
public class NetPump : MonoBehaviour
{
    public NetPump(IntPtr pointer) : base(pointer) { }

    const int MaxPerFrame = 256;
    float nextEnsure;
    float nextReport;
    string lastReport = "";

    internal static void Install()
    {
        ClassInjector.RegisterTypeInIl2Cpp<NetPump>();
        var host = new GameObject("BeamMPNet");
        UnityEngine.Object.DontDestroyOnLoad(host);
        host.hideFlags = HideFlags.HideAndDontSave;
        host.AddComponent<NetPump>();
        Plugin.Logger.LogInfo("Game data layer installed");
    }

    RoomState lastRoom = RoomState.None;

    void OnRoomChanged(RoomState room)
    {
        if (room.InRoom && (!lastRoom.InRoom || lastRoom.HostId != room.HostId)) VehicleSync.NewRoom(room.HostId);
        // a host sends the world to every player that has come
        if (room.InRoom && room.IsHost)
        {
            foreach (var peer in room.Peers)
            {
                if (peer == room.SelfId || Contains(lastRoom.Peers, peer)) continue;
                Replicator.PlayerJoined(peer);
            }
        }
        foreach (var peer in lastRoom.Peers)
        {
            if (!Contains(room.Peers, peer)) Replicator.PlayerLeft(peer);
        }
        if (!room.InRoom && lastRoom.InRoom) Replicator.RoomEnded();
    }

    static bool Contains(System.Collections.Generic.IReadOnlyList<ushort> peers, ushort peer)
    {
        foreach (var known in peers) if (known == peer) return true;
        return false;
    }

    void LateUpdate()
    {
        Presence.Late(Time.unscaledDeltaTime);
        VehicleSync.Late(Time.unscaledDeltaTime);
    }

    void Update()
    {
        if (!Link.Configured) return;
        var room = Room.Current;
        if (!ReferenceEquals(room, lastRoom))
        {
            OnRoomChanged(room); // compares with the one before
            lastRoom = room;
        }
        ModStarter.Tick(Time.unscaledTime);
        Identity.Ensure(room);
        Replicator.Tick();
        Presence.Tick(Time.unscaledTime, Time.unscaledDeltaTime);
        VehicleSync.Tick(Time.unscaledTime, room);
        Replicator.PumpReplays(Time.unscaledDeltaTime);
        if (Time.unscaledTime >= nextEnsure)
        {
            nextEnsure = Time.unscaledTime + 1f;
            Link.Ensure();
        }
        var bridge = Link.Current;
        if (bridge != null)
        {
            for (int i = 0; i < MaxPerFrame && bridge.PollData(out var line); i++) Replicator.Receive(line);
        }
        if (Time.unscaledTime >= nextReport && Room.Current.InRoom)
        {
            nextReport = Time.unscaledTime + 10f;
            var report = Replicator.Summary + " " + Presence.Summary + " " + VehicleSync.Summary;
            if (report != lastReport)
            {
                lastReport = report;
                Plugin.Logger.LogInfo("NET: " + report);
            }
        }
    }
}
