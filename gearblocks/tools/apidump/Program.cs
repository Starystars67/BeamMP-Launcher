using Mono.Cecil;
var dir = args[0];
var mode = args[1];
var needle = args.Length > 2 ? args[2] : "";
var resolver = new DefaultAssemblyResolver(); resolver.AddSearchDirectory(dir);
foreach (var name in new[] { "SmashHammer.dll", "SmashHammer.GearBlocks.dll", "Assembly-CSharp.dll" })
{
    var path = Path.Combine(dir, name);
    if (!File.Exists(path)) continue;
    var asm = AssemblyDefinition.ReadAssembly(path, new ReaderParameters { AssemblyResolver = resolver });
    if (mode == "namespaces")
    {
        foreach (var g in asm.MainModule.Types.GroupBy(t => t.Namespace).OrderBy(g => g.Key))
            Console.WriteLine($"{name}\t{g.Key}\t{g.Count()}");
    }
    else if (mode == "types")
    {
        foreach (var t in asm.MainModule.GetTypes().Where(t => (t.FullName ?? "").Contains(needle, StringComparison.OrdinalIgnoreCase)))
            Console.WriteLine($"{name}\t{t.FullName}\t{(t.BaseType?.Name ?? "")}");
    }
    else if (mode == "members")
    {
        foreach (var t in asm.MainModule.GetTypes().Where(t => t.FullName == needle))
        {
            Console.WriteLine($"== {t.FullName} : {t.BaseType?.FullName}");
            foreach (var f in t.Fields.Where(f => f.IsPublic)) Console.WriteLine($"  field {f.FieldType.Name} {f.Name}");
            foreach (var p in t.Properties) Console.WriteLine($"  prop {p.PropertyType.Name} {p.Name}");
            foreach (var m in t.Methods.Where(m => m.IsPublic && !m.IsSpecialName && !m.Name.StartsWith("NativeMethodInfoPtr")))
                Console.WriteLine($"  method {m.ReturnType.Name} {m.Name}({string.Join(", ", m.Parameters.Select(p => p.ParameterType.Name + " " + p.Name))})");
        }
    }
}
