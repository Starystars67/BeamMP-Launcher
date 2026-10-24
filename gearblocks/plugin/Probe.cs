using System;
using System.IO;
using System.Linq;
using BepInEx.Unity.IL2CPP.Utils;
using Il2CppInterop.Runtime.Injection;
using UnityEngine;
using UnityEngine.SceneManagement;

namespace BeamMP.GearBlocks;

// Development tool (BEAMMP_PROBE=1): reports what the game's own networking looks like in a running scene,
// every few seconds as `PROBE:` lines in the BepInEx log, and runs the actions listed in
// BepInEx/probe-actions.txt (one per line: `Dump <full type name>` writes BepInEx/probe-<type>.txt) each time the file changes.
// It only reads. It never touches Photon: no calls, no reads of its state, and nothing that could start a connection
// (so not NetworkConnect.StartServer either).
public class ProbeBehaviour : MonoBehaviour
{
    public ProbeBehaviour(IntPtr pointer) : base(pointer) { }

    float next;
    string lastActions = "";
    string lastReport = "";

    public static void Install()
    {
        ClassInjector.RegisterTypeInIl2Cpp<ProbeBehaviour>();
        var host = new GameObject("BeamMPProbe");
        UnityEngine.Object.DontDestroyOnLoad(host);
        host.hideFlags = HideFlags.HideAndDontSave;
        host.AddComponent<ProbeBehaviour>();
        Plugin.Logger.LogInfo("PROBE: installed");
    }

    void Update()
    {
        if (Time.unscaledTime < next) return;
        next = Time.unscaledTime + 4f;
        try { RunActions(); } catch (Exception e) { Plugin.Logger.LogWarning("PROBE: actions failed: " + e); }
        try { Report(); } catch (Exception e) { Plugin.Logger.LogWarning("PROBE: report failed: " + e); }
    }

    static string Try(Func<object> read)
    {
        try { return read()?.ToString() ?? "null"; }
        catch (Exception e) { return "<" + e.GetType().Name + ">"; }
    }

    static int Count<T>() where T : UnityEngine.Object
    {
        try { return UnityEngine.Object.FindObjectsOfType<T>().Length; }
        catch { return -1; }
    }

    void Report()
    {
        var nc = SmashHammer.Network.NetworkConnect.Instance;
        var parts = new System.Collections.Generic.List<string>
        {
            "scene=" + Try(() => SceneManager.GetActiveScene().name),
            "NetworkConnect=" + (nc == null ? "none" : "present"),
        };
        if (nc != null)
        {
            parts.Add("IsSingleplayer=" + Try(() => SmashHammer.Network.NetworkConnect.IsSingleplayer));
            parts.Add("IsMultiplayer=" + Try(() => SmashHammer.Network.NetworkConnect.IsMultiplayer));
            parts.Add("IsServer=" + Try(() => SmashHammer.Network.NetworkConnect.IsServer));
            parts.Add("IsClient=" + Try(() => SmashHammer.Network.NetworkConnect.IsClient));
            parts.Add("IsConnected=" + Try(() => nc.IsConnected));
            parts.Add("NetworkTime=" + Try(() => SmashHammer.Network.NetworkConnect.NetworkTime));
            parts.Add("ServerPlayerID=" + Try(() => SmashHammer.Network.NetworkConnect.ServerPlayerID));
        }
        parts.Add("StateSerializers=" + Count<SmashHammer.Network.NetworkStateSerializer>());
        parts.Add("NetworkRigidbodies=" + Count<SmashHammer.Network.NetworkRigidbody>());
        parts.Add("NetworkConstructions=" + Count<SmashHammer.GearBlocks.Network.NetworkConstructions>());
        parts.Add("PlayerPositions=" + Count<SmashHammer.GearBlocks.Network.NetworkPlayerPosition>());
        parts.Add("Players=" + Count<SmashHammer.GearBlocks.Player.Player>());
        parts.Add("Constructions=" + Count<SmashHammer.GearBlocks.Construction.Construction>());
        var report = string.Join(" ", parts);
        if (report != lastReport) // only changes are worth a line
        {
            lastReport = report;
            Plugin.Logger.LogInfo("PROBE: " + report);
        }
    }

    void RunActions()
    {
        var path = Path.Combine(BepInEx.Paths.BepInExRootPath, "probe-actions.txt");
        if (!File.Exists(path)) return;
        var text = File.ReadAllText(path).Trim();
        if (text == lastActions) return;
        lastActions = text;
        foreach (var action in text.Split('\n').Select(line => line.Trim()).Where(line => line.Length > 0))
        {
            var nc = SmashHammer.Network.NetworkConnect.Instance;
            switch (action)
            {
                default:
                    if (action == "Scripts")
                    {
                        EventLab.Scripts();
                    }
                    else if (action.StartsWith("Parts "))
                    {
                        EventLab.Parts(action.Split(' ')[1]);
                    }
                    else if (action.StartsWith("EndUse "))
                    {
                        var parts = action.Split(' ');
                        EventLab.EndUse(parts[1], parts[2]);
                    }
                    else if (action.StartsWith("Use "))
                    {
                        var parts = action.Split(' ');
                        EventLab.Use(parts[1], parts[2]);
                    }
                    else if (action.StartsWith("Control "))
                    {
                        var parts = action.Split(' ');
                        EventLab.Control(parts[1], parts[2], parts[3]);
                    }
                    else if (action.StartsWith("Push "))
                    {
                        var parts = action.Split(' ');
                        EventLab.Push(parts[1], parts[2], parts[3], parts[4]);
                    }
                    else if (action.StartsWith("Freeze "))
                    {
                        var parts = action.Split(' ');
                        EventLab.Freeze(parts[1], parts[2]);
                    }
                    else if (action == "SaveData")
                    {
                        EventLab.SaveData();
                    }
                    else if (action == "Composites")
                    {
                        EventLab.Composites();
                    }
                    else if (action == "Players")
                    {
                        EventLab.Players();
                    }
                    else if (action == "Constructions")
                    {
                        EventLab.Constructions();
                    }
                    else if (action.StartsWith("LocalId"))
                    {
                        var parts = action.Split(' ');
                        EventLab.LocalId(parts.Length > 1 ? parts[1] : null);
                    }
                    else if (action.StartsWith("TraceType "))
                    {
                        RpcSpy.TraceType(action.Substring(10).Trim());
                    }
                    else if (action.StartsWith("Inspect "))
                    {
                        EventLab.Inspect(action.Substring(8).Trim());
                    }
                    else if (action == "Events")
                    {
                        EventLab.List();
                    }
                    else if (action.StartsWith("Raise "))
                    {
                        var parts = action.Split(' ');
                        EventLab.Raise(parts[1], parts.Length > 2 ? parts[2] : null, parts.Length > 3 ? parts[3] : null);
                    }
                    else if (action.StartsWith("Mark"))
                    {
                        RpcSpy.Mark(action);
                    }
                    else if (action == "Trace on")
                    {
                        RpcSpy.TraceExecutions();
                    }
                    else if (action.StartsWith("Block ToServer "))
                    {
                        RpcSpy.BlockToServer = action.EndsWith(" on");
                        Plugin.Logger.LogInfo("PROBE: BlockToServer=" + RpcSpy.BlockToServer);
                    }
                    else if (action.StartsWith("Call "))
                    {
                        // the player's own call to the server, as the game's code makes it (goes through the hooks)
                        var parts = action.Split(' ');
                        RpcSpy.CallToServer(parts[1], parts.Length > 2 ? parts[2] : "");
                    }
                    else if (action.StartsWith("Invoke "))
                    {
                        var parts = action.Split(' ');
                        RpcSpy.Invoke(parts[1], parts.Length > 2 ? parts[2] : "");
                    }
                    else if (action == "Methods")
                    {
                        ListRemoteMethods();
                    }
                    else if (action.StartsWith("Dump "))
                    {
                        DumpType(action.Substring(5).Trim());
                    }
                    else
                    {
                        Plugin.Logger.LogWarning("PROBE: unknown action " + action);
                    }
                    break;
            }
        }
    }

    // Everything IL2CPP knows about a type: fields, properties, methods (also private), nested types, interfaces.
    static void DumpType(string fullName)
    {
        Il2CppSystem.Type found = null;
        foreach (var assembly in Il2CppSystem.AppDomain.CurrentDomain.GetAssemblies())
        {
            try
            {
                foreach (var type in assembly.GetTypes())
                {
                    if (type.FullName == fullName)
                    {
                        found = type;
                    }
                }
            }
            catch { }
        }
        if (found == null)
        {
            Plugin.Logger.LogWarning("PROBE: type not found: " + fullName);
            return;
        }
        var all = Il2CppSystem.Reflection.BindingFlags.Public | Il2CppSystem.Reflection.BindingFlags.NonPublic
            | Il2CppSystem.Reflection.BindingFlags.Instance | Il2CppSystem.Reflection.BindingFlags.Static
            | Il2CppSystem.Reflection.BindingFlags.DeclaredOnly;
        var lines = new System.Collections.Generic.List<string> { "type " + found.FullName + " : " + found.BaseType?.FullName };
        foreach (var i in found.GetInterfaces()) lines.Add("  implements " + i.FullName);
        foreach (var n in found.GetNestedTypes(all)) lines.Add("  nested " + n.FullName);
        foreach (var f in found.GetFields(all)) lines.Add($"  field {(f.IsStatic ? "static " : "")}{f.FieldType.FullName} {f.Name}");
        foreach (var pr in found.GetProperties(all)) lines.Add($"  property {pr.PropertyType.FullName} {pr.Name}");
        foreach (var m in found.GetMethods(all))
        {
            var args = string.Join(", ", m.GetParameters().Select(a => a.ParameterType.FullName + " " + a.Name));
            lines.Add($"  method {(m.IsStatic ? "static " : "")}{(m.IsPublic ? "public " : "private ")}{m.ReturnType.FullName} {m.Name}({args})");
        }
        foreach (var c in found.GetConstructors(all)) lines.Add("  ctor " + c.Name + "(" + string.Join(", ", c.GetParameters().Select(a => a.ParameterType.FullName)) + ")");
        var file = Path.Combine(BepInEx.Paths.BepInExRootPath, "probe-" + fullName.Replace('`', '_').Replace('/', '+') + ".txt");
        File.WriteAllLines(file, lines);
        Plugin.Logger.LogInfo("PROBE: dumped " + fullName + " (" + lines.Count + " lines)");
    }

    // The remote calls the game registered (name hash -> delegate), the seam a transport of our own can use.
    static void ListRemoteMethods()
    {
        var methods = SmashHammer.Network.NetworkRemoteMethods.Instance;
        if (methods == null)
        {
            Plugin.Logger.LogInfo("PROBE: no NetworkRemoteMethods in this scene");
            return;
        }
        var lines = new System.Collections.Generic.List<string> { "remote methods: " + methods.NumRemoteMethods };
        foreach (var entry in SmashHammer.Network.NetworkRemoteMethods.remoteMethods)
        {
            var target = Try(() => entry.Value.Target?.GetIl2CppType().FullName);
            var method = Try(() => entry.Value.Method?.Name);
            lines.Add($"  {entry.Key:x8} {target}.{method}");
        }
        var file = Path.Combine(BepInEx.Paths.BepInExRootPath, "probe-remote-methods.txt");
        File.WriteAllLines(file, lines);
        Plugin.Logger.LogInfo("PROBE: " + lines[0] + " (written to probe-remote-methods.txt)");
    }
}
