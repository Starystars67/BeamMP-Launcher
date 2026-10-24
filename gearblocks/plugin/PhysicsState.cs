using System;

namespace BeamMP.GearBlocks;

// The pose of one rigid body of a construction (a composite): the part of a vehicle the game's remote calls never
// carry, since its physics runs locally. The host simulates and sends these, the guests' copies follow.
internal struct BodyPose
{
    public const int Floats = 13;
    public const int Bytes = Floats * 4;

    public float X, Y, Z;
    public float Qx, Qy, Qz, Qw;
    // how it is moving, so that whoever takes the vehicle over carries on from there
    public float Vx, Vy, Vz;
    public float Wx, Wy, Wz;
}

internal static class PhysicsFrame
{
    // A construction with more bodies than this is not streamed (its frame would be too big for the rate it needs).
    public const int MaxBodies = 128;

    public static byte[] Encode(BodyPose[] bodies)
    {
        if (bodies.Length > MaxBodies) throw new ArgumentException("too many bodies");
        var bytes = new byte[bodies.Length * BodyPose.Bytes];
        int at = 0;
        foreach (var b in bodies)
        {
            foreach (var value in new[] { b.X, b.Y, b.Z, b.Qx, b.Qy, b.Qz, b.Qw, b.Vx, b.Vy, b.Vz, b.Wx, b.Wy, b.Wz })
            {
                var raw = BitConverter.GetBytes(value);
                if (!BitConverter.IsLittleEndian) Array.Reverse(raw);
                Buffer.BlockCopy(raw, 0, bytes, at, 4);
                at += 4;
            }
        }
        return bytes;
    }

    // Nothing for a size that is not a whole number of bodies, too many bodies, or numbers that are not a position
    // and a rotation: the receiver puts real bodies wherever this says.
    public static bool TryDecode(byte[] bytes, out BodyPose[] bodies)
    {
        bodies = null;
        if (bytes == null || bytes.Length == 0 || bytes.Length % BodyPose.Bytes != 0) return false;
        int count = bytes.Length / BodyPose.Bytes;
        if (count > MaxBodies) return false;
        var result = new BodyPose[count];
        var values = new float[BodyPose.Floats];
        for (int i = 0; i < count; i++)
        {
            for (int j = 0; j < BodyPose.Floats; j++)
            {
                var raw = new byte[4];
                Buffer.BlockCopy(bytes, i * BodyPose.Bytes + j * 4, raw, 0, 4);
                if (!BitConverter.IsLittleEndian) Array.Reverse(raw);
                values[j] = BitConverter.ToSingle(raw, 0);
                if (float.IsNaN(values[j]) || float.IsInfinity(values[j])) return false;
            }
            const float Limit = 1e6f;
            if (Math.Abs(values[0]) > Limit || Math.Abs(values[1]) > Limit || Math.Abs(values[2]) > Limit) return false;
            for (int j = 7; j < BodyPose.Floats; j++)
            {
                if (Math.Abs(values[j]) > 1e4f) return false; // not a speed a body has
            }
            var length = values[3] * values[3] + values[4] * values[4] + values[5] * values[5] + values[6] * values[6];
            if (length < 0.5f || length > 1.5f) return false;
            result[i] = new BodyPose
            {
                X = values[0], Y = values[1], Z = values[2], Qx = values[3], Qy = values[4], Qz = values[5], Qw = values[6],
                Vx = values[7], Vy = values[8], Vz = values[9], Wx = values[10], Wy = values[11], Wz = values[12],
            };
        }
        bodies = result;
        return true;
    }

    // How often a construction of this many bodies is worth a frame at most (seconds): the bigger it is, the dearer
    // each frame, so it is sent less often (20 times a second up to 24 bodies, 10 up to 48, and so on).
    public static float MinInterval(int bodies) => 0.05f * Math.Max(1, (bodies + 23) / 24);

    // Whether anything moved enough since `before` to be worth a new frame (5 mm, a quarter of a degree: bodies
    // that rest jitter by less, and a stream of that would cost more than it shows).
    public static bool Moved(BodyPose[] before, BodyPose[] now)
    {
        if (before == null || before.Length != now.Length) return true;
        for (int i = 0; i < now.Length; i++)
        {
            var a = before[i];
            var b = now[i];
            if (Math.Abs(a.X - b.X) > 0.005f || Math.Abs(a.Y - b.Y) > 0.005f || Math.Abs(a.Z - b.Z) > 0.005f) return true;
            var dot = a.Qx * b.Qx + a.Qy * b.Qy + a.Qz * b.Qz + a.Qw * b.Qw;
            if (Math.Abs(dot) < 0.99999f) return true; // about 0.5 degrees of rotation between the two
        }
        return false;
    }
}
