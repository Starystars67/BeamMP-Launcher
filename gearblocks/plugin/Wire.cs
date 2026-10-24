using System;

namespace BeamMP.GearBlocks;

// What kind of remote call a frame carries.
internal enum RpcKind : byte
{
    // a guest's game asks the host's game (the server) to do something: runs a ToServer_* method there
    Request = 1,
    // the host's game tells every player's game what happened: runs a FromServer_* method
    Broadcast = 2,
    // the same for one player
    ToPlayer = 3,
    // where a player is (a PlayerState); the player id field says whose
    State = 4,
    // the poses of a construction's bodies (a PhysicsFrame), from the host; the handle field says which construction
    Physics = 5,
    // a guest asks the host to be the one that simulates a vehicle (the handle field says which), and gives it back
    OwnerRequest = 6,
    OwnerRelease = 7,
    // the host tells everybody who simulates a vehicle now: the handle field is the construction, the player field
    // the relay peer
    Owner = 8,
}

// The bytes the plugins of one room exchange (the launcher and the relay carry them without looking):
//   u8 kind | u32 method handle (little endian) | u8 player id (for ToPlayer, else 0) | the game's own payload
internal static class Wire
{
    public const int HeaderBytes = 6;
    // the launcher's bridge takes lines of 1 MiB, and the bytes travel as Base64 inside JSON
    public const int MaxPayloadBytes = 700 * 1024;

    public static byte[] Encode(RpcKind kind, uint handle, byte player, byte[] payload)
    {
        if (payload.Length > MaxPayloadBytes) throw new ArgumentException("payload too large");
        var frame = new byte[HeaderBytes + payload.Length];
        frame[0] = (byte)kind;
        frame[1] = (byte)handle;
        frame[2] = (byte)(handle >> 8);
        frame[3] = (byte)(handle >> 16);
        frame[4] = (byte)(handle >> 24);
        frame[5] = player;
        Buffer.BlockCopy(payload, 0, frame, HeaderBytes, payload.Length);
        return frame;
    }

    public static bool TryDecode(byte[] frame, out RpcKind kind, out uint handle, out byte player, out byte[] payload)
    {
        kind = 0;
        handle = 0;
        player = 0;
        payload = null;
        if (frame == null || frame.Length < HeaderBytes || frame.Length - HeaderBytes > MaxPayloadBytes) return false;
        if (frame[0] < (byte)RpcKind.Request || frame[0] > (byte)RpcKind.Owner) return false;
        kind = (RpcKind)frame[0];
        handle = (uint)(frame[1] | frame[2] << 8 | frame[3] << 16 | frame[4] << 24);
        player = frame[5];
        payload = new byte[frame.Length - HeaderBytes];
        Buffer.BlockCopy(frame, HeaderBytes, payload, 0, payload.Length);
        return true;
    }

    // A string the way the game writes it into its payloads (.NET's BinaryWriter): 7-bit encoded length, then UTF-8.
    public static byte[] EncodeString(string text)
    {
        var utf8 = System.Text.Encoding.UTF8.GetBytes(text);
        var prefix = new System.Collections.Generic.List<byte>();
        uint length = (uint)utf8.Length;
        while (length >= 0x80)
        {
            prefix.Add((byte)(length | 0x80));
            length >>= 7;
        }
        prefix.Add((byte)length);
        var result = new byte[prefix.Count + utf8.Length];
        prefix.CopyTo(result);
        Buffer.BlockCopy(utf8, 0, result, prefix.Count, utf8.Length);
        return result;
    }
}
