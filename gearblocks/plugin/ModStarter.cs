using System;
using SmashHammer.Core;
using SmashHammer.Scripting;

namespace BeamMP.GearBlocks;

// Starts the BeamMP script mod (the in-game window that shows the room) by itself, the way the game's Script Mods menu
// does it: when a game is ready and the mod is not running. Only when the launcher is there to talk to. A mod the player
// closes (its window's close button) stays closed until the next game starts.
internal static class ModStarter
{
    const string ModName = "BeamMP";

    // The game lists its script mods some time after it starts (and after a new install it can be a while): until then
    // the mod is looked for once a second, for this long.
    const float PatienceSeconds = 180f;

    static bool startedThisGame;
    static bool warned;
    static bool waiting;
    static float nextTry;
    static float giveUpAt;

    // A new game or scene began: the mod may be started again.
    public static void NewGame()
    {
        startedThisGame = false;
        waiting = false;
    }

    // The game says it is ready.
    public static void GameReady()
    {
        if (startedThisGame) return;
        if (!Link.Configured && Environment.GetEnvironmentVariable("BEAMMP_PROBE") != "1") return;
        giveUpAt = UnityEngine.Time.unscaledTime + PatienceSeconds;
        nextTry = 0;
        waiting = true;
        Tick(UnityEngine.Time.unscaledTime);
    }

    // Every frame: looks again while the game has not listed its mods yet.
    public static void Tick(float now)
    {
        if (!waiting || now < nextTry) return;
        nextTry = now + 1f;
        if (TryStart())
        {
            waiting = false;
        }
        else if (now >= giveUpAt)
        {
            waiting = false;
            if (!warned)
            {
                warned = true;
                Plugin.Logger.LogWarning("The game did not list its script mods, the BeamMP script mod was not started (start it from the Script Mods menu)");
            }
        }
    }

    // True when there is nothing more to try: started, already running, or not installed.
    static bool TryStart()
    {
        try
        {
            var scripting = FindScripting();
            if (scripting == null) return false;
            if (scripting.activeScripts != null && scripting.activeScripts.ContainsKey(ModName))
            {
                startedThisGame = true; // already running
                return true;
            }
            var typeId = FindMod(out var listed);
            if (typeId < 0)
            {
                if (!listed) return false;
                if (!warned)
                {
                    warned = true;
                    Plugin.Logger.LogWarning("The BeamMP script mod is not installed (no folder named BeamMP in the game's script mods)");
                }
                return true;
            }
            startedThisGame = true;
            scripting.loadScriptModEvent.Raise(ModName, (byte)typeId, "BeamMP");
            Plugin.Logger.LogInfo($"BeamMP script mod started (save type {typeId})");
            return true;
        }
        catch (Exception e)
        {
            Plugin.Logger.LogWarning("Could not start the BeamMP script mod: " + e.Message);
            return true;
        }
    }

    static LuaScriptingAsset FindScripting()
    {
        var all = UnityEngine.Resources.FindObjectsOfTypeAll<LuaScriptingAsset>();
        return all.Length > 0 ? all[0] : null;
    }

    // The save type (where the game keeps it) of the mod: the one shipped inside the game's folder, or the player's own.
    static int FindMod(out bool listed)
    {
        int found = -1;
        listed = true;
        foreach (var asset in UnityEngine.Resources.FindObjectsOfTypeAll<SaveDataAssetBase>())
        {
            if (asset.type != "Script Mod") continue;
            if (!asset.populated)
            {
                listed = false;
                continue;
            }
            if (asset.SaveExists(ModName))
            {
                // the shipped copy (type 1) wins over the player's folder (type 0): it is the one the installer keeps current
                if (found < 0 || asset.TypeID == 1) found = asset.TypeID;
            }
        }
        return found;
    }
}
