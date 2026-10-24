using System;

namespace BeamMP.GearBlocks;

// Where a player is and which way it looks: the part of a player the game's own calls do not carry (its state
// components are empty in this build), so each game sends it for its own player and the others show it.
internal struct PlayerState
{
    public const int Bytes = 3 * 4 + 4 * 4 + 2 * 4 + 3 * 4 + 1;

    public float X, Y, Z;
    // orientation of the body as a quaternion
    public float Qx, Qy, Qz, Qw;
    // where the player looks
    public float Yaw, Pitch;
    public float Vx, Vy, Vz;
    public byte Tool;

    public byte[] Encode()
    {
        var bytes = new byte[Bytes];
        int at = 0;
        foreach (var value in new[] { X, Y, Z, Qx, Qy, Qz, Qw, Yaw, Pitch, Vx, Vy, Vz })
        {
            var raw = BitConverter.GetBytes(value);
            if (!BitConverter.IsLittleEndian) Array.Reverse(raw);
            Buffer.BlockCopy(raw, 0, bytes, at, 4);
            at += 4;
        }
        bytes[at] = Tool;
        return bytes;
    }

    // Nothing for the wrong size or for numbers that are not usable positions (NaN, infinity, absurd distances):
    // the receiver puts a body wherever this says.
    public static bool TryDecode(byte[] bytes, out PlayerState state)
    {
        state = default;
        if (bytes == null || bytes.Length != Bytes) return false;
        var values = new float[12];
        for (int i = 0; i < values.Length; i++)
        {
            var raw = new byte[4];
            Buffer.BlockCopy(bytes, i * 4, raw, 0, 4);
            if (!BitConverter.IsLittleEndian) Array.Reverse(raw);
            values[i] = BitConverter.ToSingle(raw, 0);
            if (float.IsNaN(values[i]) || float.IsInfinity(values[i])) return false;
        }
        const float Limit = 1e6f;
        if (Math.Abs(values[0]) > Limit || Math.Abs(values[1]) > Limit || Math.Abs(values[2]) > Limit) return false;
        if (Math.Abs(values[9]) > Limit || Math.Abs(values[10]) > Limit || Math.Abs(values[11]) > Limit) return false;
        var length = values[3] * values[3] + values[4] * values[4] + values[5] * values[5] + values[6] * values[6];
        if (length < 0.5f || length > 1.5f) return false; // not a rotation
        state = new PlayerState
        {
            X = values[0], Y = values[1], Z = values[2],
            Qx = values[3], Qy = values[4], Qz = values[5], Qw = values[6],
            Yaw = values[7], Pitch = values[8],
            Vx = values[9], Vy = values[10], Vz = values[11],
            Tool = bytes[48],
        };
        return true;
    }
}
