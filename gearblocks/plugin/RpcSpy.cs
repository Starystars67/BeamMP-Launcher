using System;
using System.Linq;
using System.Reflection;
using System.Text;
using HarmonyLib;
using Il2CppInterop.Runtime.InteropTypes.Arrays;
using SmashHammer.Network;

namespace BeamMP.GearBlocks;

// Development tool (BEAMMP_PROBE=1): logs every remote call the game makes through NetworkRemoteMethods, with the
// name of the method it is for, the size of the payload and its first bytes. It changes nothing.
[HarmonyPatch]
internal static class RpcSpy
{
    static int logged;
    const int MaxLines = 600;
    internal static void Mark(string text) { logged = 0; Plugin.Logger.LogInfo("MARK " + text); }

    internal static string NameOf(uint handle) => RemoteMethods.NameOf(handle);

    // Lab switches (probe actions): `Block ToServer on|off` makes the game drop the calls its own player makes to
    // the server instead of running them, `Trace on` logs every registered method as it runs, `Invoke <handle> <hex>`
    // runs a registered method with the given bytes as if the server had asked for it.
    internal static volatile bool BlockToServer;
    static bool traced;
    static readonly Harmony tracer = new("com.beammp.gearblocks.rpctrace");

    internal static void TraceExecutions()
    {
        if (traced) return;
        traced = true;
        var all = BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance | BindingFlags.Static | BindingFlags.DeclaredOnly;
        var types = new System.Collections.Generic.Dictionary<string, Type>();
        foreach (var assembly in AppDomain.CurrentDomain.GetAssemblies())
        {
            Type[] list;
            try { list = assembly.GetTypes(); } catch { continue; }
            foreach (var t in list)
            {
                if (t.FullName != null && t.FullName.StartsWith("SmashHammer") && !types.ContainsKey(t.FullName)) types[t.FullName] = t;
            }
        }
        var prefix = new HarmonyMethod(typeof(RpcSpy).GetMethod(nameof(Executing), BindingFlags.Static | BindingFlags.NonPublic));
        int patched = 0, failed = 0;
        foreach (var entry in NetworkRemoteMethods.remoteMethods)
        {
            try
            {
                var typeName = entry.Value.Target?.GetIl2CppType().FullName?.Replace('/', '+');
                var methodName = entry.Value.Method?.Name;
                if (typeName == null || methodName == null || !types.TryGetValue(typeName, out var type)) { failed++; continue; }
                var method = type.GetMethod(methodName, all);
                if (method == null) { failed++; continue; }
                tracer.Patch(method, prefix: prefix);
                patched++;
            }
            catch (Exception e)
            {
                failed++;
                Plugin.Logger.LogWarning("RPC trace: " + e.Message);
            }
        }
        Plugin.Logger.LogInfo($"RPC trace: patched {patched}, failed {failed}");
    }

    // `TraceType <full name>`: logs every call of every method the type declares (except per-frame ones).
    internal static void TraceType(string fullName)
    {
        var all = BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance | BindingFlags.Static | BindingFlags.DeclaredOnly;
        Type found = null;
        foreach (var assembly in AppDomain.CurrentDomain.GetAssemblies())
        {
            Type[] list;
            try { list = assembly.GetTypes(); } catch { continue; }
            foreach (var t in list) if (t.FullName == fullName) found = t;
        }
        if (found == null) { Plugin.Logger.LogWarning("TraceType: not found " + fullName); return; }
        var prefix = new HarmonyMethod(typeof(RpcSpy).GetMethod(nameof(Executing), BindingFlags.Static | BindingFlags.NonPublic));
        int patched = 0;
        foreach (var method in found.GetMethods(all))
        {
            var n = method.Name;
            if (n == "Update" || n == "FixedUpdate" || n == "LateUpdate" || n.StartsWith("get_") || n.StartsWith("set_") || n.Contains("NativeMethodInfoPtr") || method.IsAbstract || method.ContainsGenericParameters) continue;
            try { tracer.Patch(method, prefix: prefix); patched++; }
            catch (Exception e) { Plugin.Logger.LogWarning("TraceType " + n + ": " + e.Message); }
        }
        Plugin.Logger.LogInfo($"TraceType {fullName}: patched {patched}");
    }

    static void Executing(MethodBase __originalMethod)
    {
        if (logged++ < MaxLines) Plugin.Logger.LogInfo("EXEC " + __originalMethod.DeclaringType?.Name + "." + __originalMethod.Name);
    }

    internal static void CallToServer(string handleHex, string dataHex)
    {
        var handle = Convert.ToUInt32(handleHex, 16);
        var bytes = new byte[dataHex.Length / 2];
        for (int i = 0; i < bytes.Length; i++) bytes[i] = Convert.ToByte(dataHex.Substring(i * 2, 2), 16);
        Plugin.Logger.LogInfo($"RPC call {handle:x8} {NameOf(handle)} bytes={bytes.Length}");
        NetworkRemoteMethods.Instance.CallFromPlayerToServer(handle, RemoteMethods.ToGame(bytes));
    }

    internal static void Invoke(string handleHex, string dataHex)
    {
        var handle = Convert.ToUInt32(handleHex, 16);
        var bytes = new Il2CppStructArray<byte>(dataHex.Length / 2);
        for (int i = 0; i < bytes.Length; i++) bytes[i] = Convert.ToByte(dataHex.Substring(i * 2, 2), 16);
        Plugin.Logger.LogInfo($"RPC invoke {handle:x8} {NameOf(handle)} bytes={bytes.Length}");
        NetworkRemoteMethods.remoteMethods[handle].Invoke(bytes);
    }

    static void Log(string kind, uint handle, Il2CppStructArray<byte> data, string extra = "")
    {
        if (logged++ >= MaxLines) return;
        var head = new StringBuilder();
        if (data != null)
        {
            for (int i = 0; i < Math.Min(24, data.Length); i++) head.Append(data[i].ToString("x2"));
        }
        Plugin.Logger.LogInfo($"RPC: {kind}{extra} {handle:x8} {NameOf(handle)} bytes={data?.Length ?? -1} {head}");
    }

    [HarmonyPrefix]
    [HarmonyPatch(typeof(NetworkRemoteMethods), nameof(NetworkRemoteMethods.CallFromPlayerToServer))]
    static bool ToServer(uint methodHandle, Il2CppStructArray<byte> data)
    {
        Log("PlayerToServer", methodHandle, data);
        return !BlockToServer;
    }

    [HarmonyPrefix]
    [HarmonyPatch(typeof(NetworkRemoteMethods), nameof(NetworkRemoteMethods.CallFromServerToAllPlayers))]
    static void ToAll(uint methodHandle, Il2CppStructArray<byte> data) => Log("ServerToAll", methodHandle, data);

    [HarmonyPrefix]
    [HarmonyPatch(typeof(NetworkRemoteMethods), nameof(NetworkRemoteMethods.CallFromServerToSpecificPlayer))]
    static void ToOne(byte playerID, uint methodHandle, Il2CppStructArray<byte> data) => Log("ServerToPlayer", methodHandle, data, " player=" + playerID);

    [HarmonyPrefix]
    [HarmonyPatch(typeof(NetworkRemoteMethods), nameof(NetworkRemoteMethods.CallFromPlayerToOthers))]
    static void ToOthers(uint methodHandle, Il2CppStructArray<byte> data) => Log("PlayerToOthers", methodHandle, data);
}
