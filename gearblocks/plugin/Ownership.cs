using System.Collections.Generic;

namespace BeamMP.GearBlocks;

// Who simulates which vehicle. By default the host does; a guest whose player sits in a vehicle takes it over (the
// host decides and tells everybody), so that its steering does not wait for a round trip. The owner's game runs the
// physics and sends the poses of the bodies, everybody else's copy follows. Pure bookkeeping: nothing here touches the
// game.
internal sealed class OwnerTable
{
    readonly Dictionary<ushort, ushort> owners = new();
    ushort host;

    // The peer that is the host (what a construction without an entry belongs to). Changing it forgets everything.
    public void Reset(ushort hostPeer)
    {
        owners.Clear();
        host = hostPeer;
    }

    public ushort Host => host;

    public ushort OwnerOf(ushort construction) => owners.TryGetValue(construction, out var peer) ? peer : host;

    // True when the owner changed.
    public bool Set(ushort construction, ushort peer)
    {
        var before = OwnerOf(construction);
        if (peer == host) owners.Remove(construction);
        else owners[construction] = peer;
        return before != peer;
    }

    // A peer is gone: what it owned goes back to the host. The constructions that changed.
    public List<ushort> ReleaseAllOf(ushort peer)
    {
        var released = new List<ushort>();
        foreach (var entry in owners)
        {
            if (entry.Value == peer) released.Add(entry.Key);
        }
        foreach (var construction in released) owners.Remove(construction);
        return released;
    }

    // What is not the host's, for a player that joins.
    public List<KeyValuePair<ushort, ushort>> NotTheHosts() => new List<KeyValuePair<ushort, ushort>>(owners);

    public bool IsOwnedBy(ushort construction, ushort peer) => OwnerOf(construction) == peer;
}
