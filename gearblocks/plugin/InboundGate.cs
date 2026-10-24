using System.Collections.Generic;

namespace BeamMP.GearBlocks;

// While a scene is loading on a guest the world the host's calls refer to does not exist yet, so what arrives is kept
// and handed over, in order, once the level is up (or after a time limit, so a load that never finishes does not
// swallow the room).
internal sealed class InboundGate<T>
{
    readonly Queue<T> held = new();
    double holdUntil;

    public bool Holding { get; private set; }
    public int Count => held.Count;

    public void Hold(double now, double seconds)
    {
        Holding = true;
        holdUntil = now + seconds;
    }

    // Held while the gate is closed (true), or the item may be applied at once (false).
    public bool Offer(T item)
    {
        if (!Holding) return false;
        held.Enqueue(item);
        return true;
    }

    // Opens the gate; the held items in arrival order.
    public T[] Release()
    {
        Holding = false;
        var items = held.ToArray();
        held.Clear();
        return items;
    }

    // True when the time limit has passed (the caller then releases).
    public bool Expired(double now) => Holding && now >= holdUntil;

    // The gate closes again for a later item (a second scene): the rest stays held.
    public void Reclose(double now, double seconds, IEnumerable<T> rest)
    {
        Holding = true;
        holdUntil = now + seconds;
        var old = held.ToArray();
        held.Clear();
        foreach (var item in rest) held.Enqueue(item);
        foreach (var item in old) held.Enqueue(item);
    }
}
