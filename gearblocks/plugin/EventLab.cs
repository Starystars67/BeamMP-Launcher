using System;
using System.Collections.Generic;
using System.IO;
using SmashHammer.Events;

namespace BeamMP.GearBlocks;

// Development tool (BEAMMP_PROBE=1): the game's event assets (named ScriptableObjects the game raises and subscribes to),
// listed with who listens, and raised by name. Never touches Photon.
internal static class EventLab
{
    static string Describe(Il2CppSystem.Delegate d)
    {
        try { return d.Target?.GetIl2CppType().Name + "." + d.Method?.Name; }
        catch { return "?"; }
    }

    public static void List()
    {
        var lines = new List<string>();
        foreach (var entry in EventAssetBase.Instances)
        {
            var name = entry.Key;
            var asset = entry.Value;
            string subs = "";
            try
            {
                var list = asset.Subscribers;
                if (list != null)
                {
                    var names = new List<string>();
                    foreach (var d in list) names.Add(Describe(d));
                    subs = string.Join(", ", names);
                }
            }
            catch (Exception e) { subs = "<" + e.GetType().Name + ">"; }
            lines.Add($"{name} [{asset.ArgTypes}] <- {subs}");
        }
        var file = Path.Combine(BepInEx.Paths.BepInExRootPath, "probe-events.txt");
        File.WriteAllLines(file, lines);
        Plugin.Logger.LogInfo($"PROBE: {lines.Count} events (written to probe-events.txt)");
    }

    // `Raise <name> [byte]`: for events without arguments, or with one byte (a player id).
    public static void Raise(string name, string argument, string second = null)
    {
        if (!EventAssetBase.Instances.ContainsKey(name))
        {
            Plugin.Logger.LogWarning("PROBE: no event " + name);
            return;
        }
        var asset = EventAssetBase.Instances[name];
        Plugin.Logger.LogInfo($"PROBE: raising {name} [{asset.ArgTypes}] {argument}");
        var bytes = asset.TryCast<ByteEventAsset>();
        if (bytes != null && argument != null)
        {
            bytes.Raise(byte.Parse(argument), "BeamMP");
            return;
        }
        var plain = asset.TryCast<EventAsset>();
        if (plain != null)
        {
            plain.Raise("BeamMP");
            return;
        }
        var textByte = asset.TryCast<StringByteEventAsset>();
        if (textByte != null && argument != null && second != null)
        {
            textByte.Raise(argument.Replace('_', ' '), byte.Parse(second), "BeamMP");
            return;
        }
        var text = asset.TryCast<StringEventAsset>();
        if (text != null && argument != null)
        {
            text.Raise(argument.Replace('_', ' '), "BeamMP");
            return;
        }
        Plugin.Logger.LogWarning("PROBE: cannot raise an event of type " + asset.ArgTypes);
    }

    // `Inspect <full type name>`: the fields of every live object of that type, values as text (never follows references).
    public static void Inspect(string fullName)
    {
        Il2CppSystem.Type found = null;
        foreach (var assembly in Il2CppSystem.AppDomain.CurrentDomain.GetAssemblies())
        {
            try
            {
                foreach (var type in assembly.GetTypes()) if (type.FullName == fullName) found = type;
            }
            catch { }
        }
        if (found == null)
        {
            Plugin.Logger.LogWarning("PROBE: type not found: " + fullName);
            return;
        }
        var all = Il2CppSystem.Reflection.BindingFlags.Public | Il2CppSystem.Reflection.BindingFlags.NonPublic
            | Il2CppSystem.Reflection.BindingFlags.Instance | Il2CppSystem.Reflection.BindingFlags.Static;
        var lines = new List<string>();
        foreach (var obj in UnityEngine.Resources.FindObjectsOfTypeAll(found))
        {
            lines.Add("object " + obj.name + " (" + obj.GetInstanceID() + ")");
            foreach (var f in found.GetFields(all))
            {
                string value;
                try
                {
                    var v = f.GetValue(f.IsStatic ? null : obj);
                    value = v == null ? "null" : v.ToString();
                }
                catch (Exception e) { value = "<" + e.GetType().Name + ">"; }
                if (value.Length > 200) value = value.Substring(0, 200) + "...";
                lines.Add($"  {f.FieldType.Name} {f.Name} = {value}");
            }
        }
        var file = Path.Combine(BepInEx.Paths.BepInExRootPath, "probe-inspect-" + fullName + ".txt");
        File.WriteAllLines(file, lines);
        Plugin.Logger.LogInfo($"PROBE: inspected {fullName} ({lines.Count} lines)");
    }

    // `LocalId [n]`: the local player's network id (a variable the game keeps), read or set.
    public static void LocalId(string value)
    {
        var variable = SmashHammer.Network.NetworkConnect.Instance.localPlayerNetworkIDVar;
        Plugin.Logger.LogInfo("PROBE: localPlayerNetworkID = " + variable.Value);
        if (value != null)
        {
            variable.Value = byte.Parse(value);
            Plugin.Logger.LogInfo("PROBE: localPlayerNetworkID set to " + variable.Value);
        }
    }

    // `Constructions`: every construction in the world, and what the game's own serializer makes of it.
    public static void Constructions()
    {
        var populate = UnityEngine.Resources.FindObjectsOfTypeAll<SmashHammer.GearBlocks.Construction.PopulateConstructionsAsset>();
        Plugin.Logger.LogInfo($"PROBE: {populate.Length} PopulateConstructionsAsset");
        var lines = new List<string>();
        foreach (var entry in SmashHammer.GearBlocks.Construction.Construction.Instances)
        {
            var c = entry.Value;
            string serialized = "-";
            foreach (var compress in new[] { true, false })
            {
                try
                {
                    var bytes = populate[0].SerializeConstruction(compress, false, c, c.BuilderPlayerID, UnityEngine.Vector3.zero, null);
                    serialized += $" compress={compress}:{bytes.Length}:{BitConverter.ToString(RemoteMethods.FromGame(bytes), 0, Math.Min(12, bytes.Length)).Replace("-", "")}";
                }
                catch (Exception e) { serialized += $" compress={compress}:<{e.GetType().Name} {e.Message}>"; }
            }
            lines.Add($"id={entry.Key} parts={c.NumParts} character={c.IsPlayerCharacter} charPlayer={c.CharacterPlayerID} builder={c.BuilderPlayerID} {serialized}");
        }
        var file = Path.Combine(BepInEx.Paths.BepInExRootPath, "probe-constructions.txt");
        File.WriteAllLines(file, lines);
        Plugin.Logger.LogInfo($"PROBE: {lines.Count} constructions (probe-constructions.txt)");
    }

    // `Players`: every player object, whose it is, and where it stands.
    public static void Players()
    {
        var lines = new List<string>();
        foreach (var entry in SmashHammer.GearBlocks.Player.Player.Instances)
        {
            var player = entry.Value;
            var t = player.transform;
            var components = new List<string>();
            foreach (var c in player.GetComponents<UnityEngine.Component>()) components.Add(c.GetIl2CppType().Name);
            lines.Add($"key={entry.Key} id={player.ID} local={player.isLocallyControlled} pos={t.position} rot={t.rotation.eulerAngles} velocity={player.velocity} tool={player.currentActivatedToolID} components={string.Join(",", components)}");
        }
        var file = Path.Combine(BepInEx.Paths.BepInExRootPath, "probe-players.txt");
        File.WriteAllLines(file, lines);
        Plugin.Logger.LogInfo($"PROBE: {lines.Count} players (probe-players.txt)");
    }

    // `Composites`: the rigid bodies of every construction and the joints between them.
    public static void Composites()
    {
        var lines = new List<string>();
        foreach (var entry in SmashHammer.GearBlocks.Construction.Construction.Instances)
        {
            var c = entry.Value;
            lines.Add($"construction {entry.Key} parts={c.NumParts} composites={c.NumComposites} frozen={c.IsFrozen} character={c.IsPlayerCharacter} movementLocal={c.isMovementLocallyControlled}");
            int index = 0;
            foreach (var composite in c.Composites)
            {
                var rb = composite.Rigidbody;
                int joints = composite.GetComponents<UnityEngine.Joint>().Length;
                var kinds = new List<string>();
                foreach (var j in composite.GetComponents<UnityEngine.Joint>()) kinds.Add(j.GetIl2CppType().Name + (j.connectedBody != null ? "->" + j.connectedBody.name : ""));
                lines.Add($"  composite {index++} parts={composite.NumParts} pos={rb.position} rot={rb.rotation.eulerAngles} vel={rb.velocity} kinematic={rb.isKinematic} sleeping={rb.IsSleeping()} mass={rb.mass} interpolation={rb.interpolation} joints={joints} [{string.Join(";", kinds)}]");
            }
        }
        var file = Path.Combine(BepInEx.Paths.BepInExRootPath, "probe-composites.txt");
        File.WriteAllLines(file, lines);
        Plugin.Logger.LogInfo($"PROBE: {lines.Count} lines (probe-composites.txt)");
    }

    // `SaveData`: the places the game loads saved constructions and scenes from, with what each holds.
    public static void SaveData()
    {
        var lines = new List<string>();
        foreach (var asset in UnityEngine.Resources.FindObjectsOfTypeAll<SmashHammer.Core.SaveDataAssetBase>())
        {
            string saves = "";
            try
            {
                saves = "Go-kart exists=" + asset.SaveExists("Go-kart") + ", Off-road Go-kart exists=" + asset.SaveExists("Off-road Go-kart");
                var local = asset.TryCast<SmashHammer.Core.LocalSaveDataAsset>();
                if (local != null)
                {
                    var names = new List<string>();
                    foreach (var entry in local.saves) names.Add(entry.Key);
                    saves += "; " + string.Join(", ", names.GetRange(0, Math.Min(12, names.Count)));
                }
            }
            catch (Exception e) { saves = "<" + e.GetType().Name + ">"; }
            lines.Add($"{asset.name} typeID={asset.TypeID} type={asset.type} path={asset.savePath} local={asset.IsLocallySaved} populated={asset.populated} saves={saves}");
        }
        var file = Path.Combine(BepInEx.Paths.BepInExRootPath, "probe-savedata.txt");
        File.WriteAllLines(file, lines);
        Plugin.Logger.LogInfo($"PROBE: {lines.Count} save data assets (probe-savedata.txt)");
    }

    // `Freeze <construction id> <true|false>`: the game's own freeze operation, as the player would do it.
    public static void Freeze(string id, string freeze)
    {
        var populate = UnityEngine.Resources.FindObjectsOfTypeAll<SmashHammer.GearBlocks.Construction.PopulateConstructionsAsset>()[0];
        var ops = populate.constructionOperations;
        foreach (var method in ops.GetType().GetMethods(System.Reflection.BindingFlags.Public | System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance))
        {
            if (!method.Name.EndsWith("SetConstructionFrozen")) continue;
            var parameters = method.GetParameters();
            if (parameters.Length == 3 && parameters[0].ParameterType == typeof(ushort) && parameters[1].ParameterType == typeof(bool))
            {
                method.Invoke(ops, new object[] { ushort.Parse(id), bool.Parse(freeze), true });
                Plugin.Logger.LogInfo($"PROBE: construction {id} frozen={freeze}");
                return;
            }
        }
        Plugin.Logger.LogWarning("PROBE: no SetConstructionFrozen(ushort, bool, bool) found");
    }

    // `Push <construction id> <x> <y> <z>`: sets the velocity of all the bodies of a construction (host-side lab).
    public static void Push(string id, string x, string y, string z)
    {
        var c = SmashHammer.GearBlocks.Construction.Construction.Instances[ushort.Parse(id)];
        var v = new UnityEngine.Vector3(float.Parse(x), float.Parse(y), float.Parse(z));
        foreach (var composite in c.Composites) composite.Rigidbody.velocity = v;
        Plugin.Logger.LogInfo($"PROBE: pushed construction {id} with {v}");
    }

    static SmashHammer.GearBlocks.Construction.PartDescriptor PartOf(string construction, string index)
    {
        var c = SmashHammer.GearBlocks.Construction.Construction.Instances[ushort.Parse(construction)];
        return c.GetPart(ushort.Parse(index));
    }

    // `Parts <construction id>`: the parts of a construction with their behaviours (which can be used, which control).
    public static void Parts(string construction)
    {
        var c = SmashHammer.GearBlocks.Construction.Construction.Instances[ushort.Parse(construction)];
        var lines = new List<string>();
        foreach (var part in c.Parts)
        {
            var behaviours = new List<string>();
            foreach (var b in part.Behaviours)
            {
                if (b == null) continue;
                behaviours.Add(b.GetIl2CppType().Name + (b.TryCast<SmashHammer.GearBlocks.Construction.PartBehaviourControllableBase<float>>() != null ? "(control)" : ""));
            }
            lines.Add($"idx={part.Idx} id={part.ID} asset={part.AssetName} pos={part.transform.position} rot={part.transform.rotation.eulerAngles} useable={part.HasUseableBehaviours} behaviours={string.Join(",", behaviours)}");
        }
        var file = Path.Combine(BepInEx.Paths.BepInExRootPath, "probe-parts.txt");
        File.WriteAllLines(file, lines);
        Plugin.Logger.LogInfo($"PROBE: {lines.Count} parts (probe-parts.txt)");
    }

    // `Use <construction id> <part idx>`: the local player uses the part, as when aiming at it and pressing the use key.
    public static void Use(string construction, string index)
    {
        var part = PartOf(construction, index);
        foreach (var entry in SmashHammer.GearBlocks.Player.Player.Instances)
        {
            if (!entry.Value.isLocallyControlled) continue;
            Plugin.Logger.LogInfo($"PROBE: player {entry.Key} uses part {index} ({part.AssetName})");
            entry.Value.targeter.BeginUse(part);
            return;
        }
        Plugin.Logger.LogWarning("PROBE: no local player");
    }

    // `Control <construction id> <part idx> <value>`: sets the control value of the part's control behaviours.
    public static void Control(string construction, string index, string value)
    {
        var part = PartOf(construction, index);
        foreach (var b in part.Behaviours)
        {
            var control = b?.TryCast<SmashHammer.GearBlocks.Construction.PartBehaviourControllableBase<float>>();
            if (control == null) continue;
            control.SetControlValue(float.Parse(value, System.Globalization.CultureInfo.InvariantCulture));
            Plugin.Logger.LogInfo($"PROBE: control value {value} on {b.GetIl2CppType().Name} of part {index}");
        }
    }

    // `EndUse <construction id> <part idx>`: the local player lets go of the part.
    public static void EndUse(string construction, string index)
    {
        var part = PartOf(construction, index);
        foreach (var entry in SmashHammer.GearBlocks.Player.Player.Instances)
        {
            if (!entry.Value.isLocallyControlled) continue;
            Plugin.Logger.LogInfo($"PROBE: player {entry.Key} lets go of part {index}");
            entry.Value.targeter.EndUse(part);
            return;
        }
    }

    // `Scripts`: the Lua scripts that are running.
    public static void Scripts()
    {
        var asset = UnityEngine.Resources.FindObjectsOfTypeAll<SmashHammer.Scripting.LuaScriptingAsset>()[0];
        var names = new List<string>();
        foreach (var entry in asset.activeScripts) names.Add(entry.Key);
        Plugin.Logger.LogInfo($"PROBE: scripts running: {string.Join(", ", names)}");
    }
}
