using System;
using BepInEx.Unity.IL2CPP.Utils;
using Il2CppInterop.Runtime;
using MoonSharp.Interpreter;

namespace BeamMP.GearBlocks;

// The `BeamMP` table every Lua script gets:
//   BeamMP.connect()  -> true | false, reason   (starts the connection to the launcher; again once one has ended)
//   BeamMP.connected()-> bool
//   BeamMP.send(line) -> bool                   (one line, no newline inside)
//   BeamMP.poll()     -> string | nil           (next line from the launcher)
//   BeamMP.version    -> "0.1.0"
internal static class LuaApi
{
    public static void Configure() => Link.Configure();

    static DynValue Callback(Func<CallbackArguments, DynValue> body, string name)
    {
        var managed = new Func<ScriptExecutionContext, CallbackArguments, DynValue>((_, args) =>
        {
            try { return body(args); }
            catch (Exception e)
            {
                Plugin.Logger.LogWarning(name + " failed: " + e.Message);
                return DynValue.Nil;
            }
        });
        var func = DelegateSupport.ConvertDelegate<Il2CppSystem.Func<ScriptExecutionContext, CallbackArguments, DynValue>>(managed);
        return DynValue.NewCallback(new CallbackFunction(func, name));
    }

    public static void Install(Script script)
    {
        var api = new Table(script);
        api.Set("version", DynValue.NewString("0.1.0"));
        api.Set("connect", Callback(_ =>
        {
            if (!Link.Configured) return DynValue.NewTuple(DynValue.False, DynValue.NewString("no launcher bridge configured"));
            Link.Ensure();
            return DynValue.True;
        }, "BeamMP.connect"));
        api.Set("connected", Callback(_ => Link.Current != null && Link.Current.Connected ? DynValue.True : DynValue.False, "BeamMP.connected"));
        api.Set("error", Callback(_ => DynValue.NewString(Link.Current?.LastError ?? "not started"), "BeamMP.error"));
        api.Set("send", Callback(args =>
        {
            if (Link.Current == null || args.Count < 1) return DynValue.False;
            var text = args[0].String;
            return text != null && Link.Current.Send(text) ? DynValue.True : DynValue.False;
        }, "BeamMP.send"));
        api.Set("poll", Callback(_ =>
        {
            var line = Link.Current?.Poll();
            return line == null ? DynValue.Nil : DynValue.NewString(line);
        }, "BeamMP.poll"));
        script.Globals.Set("BeamMP", DynValue.NewTable(api));
    }
}
