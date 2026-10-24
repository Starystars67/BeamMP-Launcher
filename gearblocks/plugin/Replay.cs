using System;
using System.Collections.Generic;

namespace BeamMP.GearBlocks;

// What the host's server has told every player since the scene began, in order. A player that joins later gets the
// same calls in the same order and so ends up in the same world (scene, players, constructions), the way a game
// that replays its network messages does. Pure bookkeeping: nothing here touches the game.
internal sealed class BroadcastLog
{
    public sealed class Entry
    {
        public uint Handle;
        public byte[] Payload;
    }

    public const int MaxEntries = 50000;
    public const long MaxBytes = 32L * 1024 * 1024;

    readonly List<Entry> entries = new();
    long bytes;

    public int Count => entries.Count;
    public long Bytes => bytes;
    // true once old entries had to be dropped: a replay may then be incomplete
    public bool Truncated { get; private set; }

    // A new scene starts the log over: everything before it is gone with the old scene.
    public void Reset()
    {
        entries.Clear();
        bytes = 0;
        Truncated = false;
    }

    public void Add(uint handle, byte[] payload)
    {
        // state set again to what it already is (control states repeat a lot) adds nothing
        if (entries.Count > 0)
        {
            var last = entries[entries.Count - 1];
            if (last.Handle == handle && last.Payload.AsSpan().SequenceEqual(payload)) return;
        }
        entries.Add(new Entry { Handle = handle, Payload = payload });
        bytes += payload.Length;
        // never drop the first entry: it is the scene
        while ((entries.Count > MaxEntries || bytes > MaxBytes) && entries.Count > 2)
        {
            bytes -= entries[1].Payload.Length;
            entries.RemoveAt(1);
            Truncated = true;
        }
    }

    public Entry[] Snapshot() => entries.ToArray();
}

// Entries waiting to be sent to players that joined, handed out in portions so the relay's rate limit is not hit.
internal sealed class ReplayQueue
{
    sealed class Job
    {
        public ushort Peer;
        public BroadcastLog.Entry[] Entries;
        public int Next;
    }

    readonly List<Job> jobs = new();

    public bool Idle => jobs.Count == 0;

    public void Start(ushort peer, BroadcastLog.Entry[] entries)
    {
        Cancel(peer);
        if (entries.Length > 0) jobs.Add(new Job { Peer = peer, Entries = entries });
    }

    public void Cancel(ushort peer) => jobs.RemoveAll(job => job.Peer == peer);

    // Up to about `budgetBytes` (at least one entry per job when there is budget) in order, per peer.
    public List<KeyValuePair<ushort, BroadcastLog.Entry>> Take(int budgetBytes)
    {
        var taken = new List<KeyValuePair<ushort, BroadcastLog.Entry>>();
        foreach (var job in jobs)
        {
            int spent = 0;
            while (job.Next < job.Entries.Length && (spent == 0 || spent + job.Entries[job.Next].Payload.Length <= budgetBytes))
            {
                var entry = job.Entries[job.Next++];
                spent += entry.Payload.Length + Wire.HeaderBytes;
                taken.Add(new KeyValuePair<ushort, BroadcastLog.Entry>(job.Peer, entry));
                if (spent >= budgetBytes) break;
            }
        }
        jobs.RemoveAll(job => job.Next >= job.Entries.Length);
        return taken;
    }
}
