using System;
using System.Collections.Generic;
using Il2CppInterop.Runtime.InteropTypes.Arrays;
using SmashHammer.Network;

namespace BeamMP.GearBlocks;

// The game's registered remote calls (handle -> delegate), by what they are called.
internal static class RemoteMethods
{
    static readonly Dictionary<uint, string> names = new();

    // "ToServer_RequestSpawnPart", or null for a handle the game did not register.
    public static string MethodName(uint handle)
    {
        if (names.TryGetValue(handle, out var known)) return known;
        try
        {
            if (!NetworkRemoteMethods.remoteMethods.ContainsKey(handle)) return null;
            var name = NetworkRemoteMethods.remoteMethods[handle].Method?.Name;
            if (name != null) names[handle] = name; // registrations do not change while a scene runs
            return name;
        }
        catch
        {
            return null;
        }
    }

    static readonly Dictionary<string, uint> handles = new();

    // The handle of the registered method of that name (0 when there is none).
    public static uint HandleOf(string method)
    {
        if (handles.TryGetValue(method, out var known)) return known;
        try
        {
            foreach (var entry in NetworkRemoteMethods.remoteMethods)
            {
                if (entry.Value.Method?.Name == method)
                {
                    handles[method] = entry.Key;
                    return entry.Key;
                }
            }
        }
        catch { }
        return 0;
    }

    // "ConstructionOperationsAsset.ToServer_RequestSetConstructionCollidable", for logs.
    public static string NameOf(uint handle)
    {
        try
        {
            if (NetworkRemoteMethods.remoteMethods.ContainsKey(handle))
            {
                var method = NetworkRemoteMethods.remoteMethods[handle];
                return method.Target?.GetIl2CppType().Name + "." + method.Method?.Name;
            }
        }
        catch { }
        return "?";
    }

    public static Il2CppStructArray<byte> ToGame(byte[] bytes)
    {
        var array = new Il2CppStructArray<byte>(bytes.Length);
        for (int i = 0; i < bytes.Length; i++) array[i] = bytes[i];
        return array;
    }

    public static byte[] FromGame(Il2CppStructArray<byte> array)
    {
        if (array == null) return new byte[0];
        var bytes = new byte[array.Length];
        for (int i = 0; i < bytes.Length; i++) bytes[i] = array[i];
        return bytes;
    }

    // Runs the registered method as the game's own netcode would on delivery. The game's exceptions stay here.
    public static bool Invoke(uint handle, byte[] payload)
    {
        try
        {
            NetworkRemoteMethods.remoteMethods[handle].Invoke(ToGame(payload));
            return true;
        }
        catch (Exception e)
        {
            Plugin.Logger.LogWarning($"Remote call {NameOf(handle)} failed: {e.Message}");
            return false;
        }
    }
}
