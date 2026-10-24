using System;

namespace BeamMP.GearBlocks;

// The plugin's one connection to the launcher. Everything that talks to the launcher (the Lua `BeamMP` table and the
// game-data layer) goes through it, and it is brought back by `Ensure` when the launcher has been away.
internal static class Link
{
    const string PortVariable = "BEAMMP_BRIDGE_PORT";
    const string TokenVariable = "BEAMMP_BRIDGE_TOKEN";

    static Bridge bridge;
    static bool configured;
    static int port;
    static string token = "";

    public static bool Configured => configured;
    public static Bridge Current => bridge;

    public static void Configure()
    {
        var portText = Environment.GetEnvironmentVariable(PortVariable);
        token = Environment.GetEnvironmentVariable(TokenVariable) ?? "";
        if (int.TryParse(portText, out var p) && p > 0 && p < 65536 && token.Length >= 16)
        {
            port = p;
            configured = true;
            Plugin.Logger.LogInfo("Bridge target: 127.0.0.1:" + p);
        }
        else
        {
            Plugin.Logger.LogInfo("No launcher bridge configured (" + PortVariable + "/" + TokenVariable + " unset)");
        }
    }

    // Connects, or connects again once a connection has ended (the launcher restarted). Cheap when all is well.
    public static void Ensure()
    {
        if (!configured) return;
        if (bridge == null || !bridge.Active)
        {
            bridge?.Dispose();
            bridge = new Bridge(port, token);
            bridge.Start();
        }
    }
}
