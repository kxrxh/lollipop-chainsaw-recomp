using System;
using System.IO;
using System.Linq;

namespace lollipop;

internal static class BuildToolchainTests {
  internal static void Run(string sandbox) {
    var source = Path.Combine(sandbox, "runtime source");
    var destination = Path.Combine(sandbox, "runtime destination");
    Directory.CreateDirectory(source);
    Directory.CreateDirectory(destination);
    File.WriteAllText(Path.Combine(destination, "keep.txt"), "keep");
    try {
      BuildToolchain.CopyRuntime(source, destination);
      throw new Exception("An incomplete runtime package was accepted");
    } catch (IOException) {
    }
    foreach (var name in new[] { "msvcp140.dll", "msvcp140_atomic_wait.dll", "vcruntime140.dll",
                                 "vcruntime140_1.dll" })
      File.WriteAllText(Path.Combine(source, name), name);
    File.WriteAllText(Path.Combine(source, "unrelated.dll"), "do not copy");
    BuildToolchain.CopyRuntime(source, destination);
    if (File.ReadAllText(Path.Combine(destination, "keep.txt")) != "keep" ||
        File.Exists(Path.Combine(destination, "unrelated.dll")) ||
        !Directory.GetFiles(source, "*140*.dll")
             .All(path => File.ReadAllText(Path.Combine(destination, Path.GetFileName(path))) ==
                          File.ReadAllText(path)))
      throw new Exception(
          "App-local runtime staging changed unrelated files or omitted required DLLs");
    if (BuildToolchain.RuntimeAvailable(source))
      throw new Exception("Invalid runtime DLLs were treated as compatible installed components");
  }
}
