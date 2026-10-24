using System;
using BepInEx;
using BepInEx.Logging;
using BepInEx.Unity.IL2CPP;
using HarmonyLib;

namespace BeamMP.GearBlocks;

[BepInPlugin("com.beammp.gearblocks", "BeamMP for GearBlocks", "0.1.0")]
public class Plugin : BasePlugin
{
    internal static ManualLogSource Logger = null!;

    public override void Load()
    {
        Logger = Log;
        Log.LogInfo("BeamMP plugin loaded");
        LuaApi.Configure();
        new Harmony("com.beammp.gearblocks").PatchAll(typeof(ScriptPatches));
        NetPump.Install();
        Replicator.Install();
        if (Environment.GetEnvironmentVariable("BEAMMP_PROBE") == "1")
        {
            ProbeBehaviour.Install();
            new Harmony("com.beammp.gearblocks.rpcspy").PatchAll(typeof(RpcSpy));
        }
        Log.LogInfo("Lua hooks installed");
    }
}
