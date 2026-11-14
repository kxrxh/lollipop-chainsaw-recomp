using System;
using System.IO;
using System.Linq;

namespace lollipop;

internal static class IncrementalBuildTests {
  internal static void Run(string sandbox, string cmake, string ninja, string compiler) {
    var first = Path.Combine(sandbox, "first source kit");
    var next = Path.Combine(sandbox, "new launcher source kit");
    var stable = Path.Combine(sandbox, "game/out/source");
    var build = Path.Combine(sandbox, "incremental build");
    Directory.CreateDirectory(first);
    File.WriteAllText(
        Path.Combine(first, "CMakeLists.txt"),
        "cmake_minimum_required(VERSION 3.25)\nproject(incremental LANGUAGES CXX)\n" +
            "set(CMAKE_CXX_STANDARD 23)\n" +
            "add_library(guest OBJECT guest.cpp)\ntarget_precompile_headers(guest PRIVATE " +
            "shared.h)\n" +
            "add_executable(host main.cpp)\ntarget_link_libraries(host PRIVATE guest)\n");
    File.WriteAllText(Path.Combine(first, "shared.h"), "#pragma once\nint guest();\n");
    File.WriteAllText(Path.Combine(first, "guest.cpp"), "int guest() { return 42; }\n");
    File.WriteAllText(Path.Combine(first, "main.cpp"),
                      "#include \"shared.h\"\nint main() { return guest(); }\n");
    File.WriteAllText(Path.Combine(first, "obsolete.h"), "// removed next release\n");
    var tools = new Tools(new RecordedProgress(), Console.WriteLine);
    void Build(string kit) {
      LauncherPackage.SyncKit(kit, stable);
      tools.Run(
          cmake, sandbox,
          Installer.GameConfigureOptions(stable, build)
              .Concat(new[] { "-S", stable, "-B", build, "-G", "Ninja",
                              "-DCMAKE_MAKE_PROGRAM=" + ninja, "-DCMAKE_CXX_COMPILER=" + compiler,
                              "-DCMAKE_BUILD_TYPE=Release",
                              "-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=" + Path.Combine(build, "stage") }),
          true);
      tools.Run(cmake, sandbox, new[] { "--build", build }, true);
    }
    tools.Run(cmake, sandbox,
              new[] { "-S", first, "-B", build, "-G", "Ninja", "-DCMAKE_MAKE_PROGRAM=" + ninja,
                      "-DCMAKE_CXX_COMPILER=" + compiler },
              true);
    Build(first);
    var objects = Directory.GetFiles(build, "*.obj", SearchOption.AllDirectories)
                      .Concat(Directory.GetFiles(build, "*.pch", SearchOption.AllDirectories))
                      .ToDictionary(file => file, File.GetLastWriteTimeUtc);
    if (objects.Count != 3)
      throw new Exception("Incremental build fixture did not compile both sources and its PCH");
    Directory.Move(Path.Combine(build, "stage"), Path.Combine(build, "active"));
    Directory.CreateDirectory(next);
    foreach (var file in Directory.GetFiles(first)) {
      var copy = Path.Combine(next, Path.GetFileName(file));
      File.Copy(file, copy);
      File.SetLastWriteTimeUtc(copy, DateTime.UtcNow.AddDays(2));
    }
    var original = File.GetLastWriteTimeUtc(Path.Combine(stable, "guest.cpp"));
    Build(next);
    if (objects.Any(pair => File.GetLastWriteTimeUtc(pair.Key) != pair.Value) ||
        File.GetLastWriteTimeUtc(Path.Combine(stable, "guest.cpp")) != original)
      throw new Exception("An unchanged new launcher kit rebuilt game objects or its PCH");
    File.WriteAllText(Path.Combine(next, "main.cpp"),
                      "#include \"shared.h\"\nint main() { return guest() + 1; }\n");
    File.SetLastWriteTimeUtc(Path.Combine(next, "main.cpp"), DateTime.UtcNow.AddDays(-2));
    File.Delete(Path.Combine(next, "obsolete.h"));
    Build(next);
    foreach (var pair in objects) {
      var changed = File.GetLastWriteTimeUtc(pair.Key) != pair.Value;
      if (changed != pair.Key.EndsWith("main.cpp.obj", StringComparison.OrdinalIgnoreCase))
        throw new Exception("A host edit did not rebuild only its own object: " + pair.Key);
    }
    if (File.Exists(Path.Combine(stable, "obsolete.h")) ||
        !File.Exists(Path.Combine(first, "obsolete.h")))
      throw new Exception("Source-kit synchronization kept a removed header or modified its input");
    objects = objects.Keys.ToDictionary(file => file, File.GetLastWriteTimeUtc);
    File.WriteAllText(Path.Combine(next, "shared.h"),
                      "#pragma once\nint guest();\ninline constexpr int version = 1;\n");
    Build(next);
    if (objects.Any(pair => File.GetLastWriteTimeUtc(pair.Key) == pair.Value))
      throw new Exception("A shared header edit reused stale objects or its precompiled header");
  }
}
