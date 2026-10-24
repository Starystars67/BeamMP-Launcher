using HarmonyLib;
using Il2CppInterop.Runtime.InteropTypes.Arrays;
using MoonSharp.Interpreter;

namespace BeamMP.GearBlocks;

[HarmonyPatch]
internal static class ScriptPatches
{
    // Every script has its own MoonSharp Script, and its main chunk is started with Script.Call, so the
    // first Call on a Script is the last moment before the script's own code can look for BeamMP.
    // (Constructors cannot be patched with the IL2CPP backend.)
    [HarmonyPrefix]
    [HarmonyPatch(typeof(Script), nameof(Script.Call), new[] { typeof(DynValue), typeof(Il2CppReferenceArray<DynValue>) })]
    static void BeforeCall(Script __instance)
    {
        try
        {
            if (__instance.Globals.Get("BeamMP").IsNil())
            {
                LuaApi.Install(__instance);
            }
        }
        catch (System.Exception e)
        {
            Plugin.Logger.LogError("Could not add BeamMP to a script: " + e);
        }
    }
}
