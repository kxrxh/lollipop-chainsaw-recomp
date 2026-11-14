using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Linq;

namespace lollipop;

internal static class SdkPackageTests {
  private const string Identity = "0123456789abcdef";

  private static MemoryStream Archive(string? omitted = null, string identity = Identity,
                                      bool unsafePath = false) {
    var stream = new MemoryStream();
    using (var zip = new ZipArchive(stream, ZipArchiveMode.Create, true)) {
      foreach (var name in new[] { "lollipop-sdk.id", "bin/rexglue.exe", "bin/rexruntime.dll",
                                   "bin/rexgpu-xenos.dll", "bin/msvcp140.dll",
                                   "bin/msvcp140_atomic_wait.dll", "bin/vcruntime140.dll",
                                   "bin/vcruntime140_1.dll", "include/rex/rex_app.h",
                                   "lib/rexruntime.lib", "lib/cmake/rexglue/rexglueConfig.cmake",
                                   "lib/cmake/rexglue/rexglueTargets-release.cmake",
                                   "share/rexglue/rex_app.cpp", "licenses/rexglue/LICENSE",
                                   "licenses/SOURCES.txt" }
                   .Where(name => name != omitted)) {
        using var writer = new StreamWriter(zip.CreateEntry(name).Open());
        writer.Write(name == "lollipop-sdk.id" ? identity : "SDK fixture");
      }
      if (unsafePath) {
        using var writer = new StreamWriter(zip.CreateEntry("../escaped.txt").Open());
        writer.Write("escape");
      }
    }
    stream.Position = 0;
    return stream;
  }

  private static void Reject(MemoryStream payload, string cache) {
    using (payload) {
      try {
        SdkPackage.Install(payload, cache, Identity);
      } catch (InvalidDataException) {
        if (Directory.Exists(cache) && Directory.EnumerateFileSystemEntries(cache).Any())
          throw new Exception("A rejected SDK left a published or staged cache");
        return;
      }
    }
    throw new Exception("An invalid SDK package was accepted");
  }

  internal static void Run(string root, string sandbox) {
    var cache = Path.Combine(sandbox, "SDK cache player's [1] Ж");
    using var payload = Archive();
    var first = SdkPackage.Install(payload, cache, Identity);
    File.WriteAllText(Path.Combine(first, "keep.txt"), "keep");
    payload.Position = 0;
    if (SdkPackage.Install(payload, cache, Identity) != first ||
        File.ReadAllText(Path.Combine(first, "keep.txt")) != "keep")
      throw new Exception("An intact SDK cache was not reused");
    File.Delete(Path.Combine(first, "bin/rexruntime.dll"));
    payload.Position = 0;
    if (SdkPackage.Install(payload, cache, Identity) != first ||
        !File.Exists(Path.Combine(first, "bin/rexruntime.dll")) ||
        !Directory.GetDirectories(cache, "incomplete-*")
             .Any(path => File.ReadAllText(Path.Combine(path, "keep.txt")) == "keep"))
      throw new Exception("SDK repair failed or discarded files from the old cache");
    Reject(Archive(omitted: "bin/rexgpu-xenos.dll"), Path.Combine(sandbox, "missing DLL"));
    Reject(Archive(identity: "fedcba9876543210"), Path.Combine(sandbox, "wrong patches"));
    Reject(Archive(identity: new string('a', 1024)), Path.Combine(sandbox, "oversized marker"));
    Reject(Archive(unsafePath: true), Path.Combine(sandbox, "unsafe SDK"));
    if (File.Exists(Path.Combine(sandbox, "escaped.txt")))
      throw new Exception("An SDK archive wrote outside its cache");
    PackageScript(root, sandbox);
  }

  private static void PackageScript(string root, string sandbox) {
    var identity = GameUpdate.SdkId(root, SdkPackage.Commit);
    using var payload = Archive(identity: identity);
    var sdk = SdkPackage.Install(payload, Path.Combine(sandbox, "pack SDK"), identity);
    var output = Path.Combine(sandbox, "SDK output", "sdk.zip");
    var cmake = Path.Combine(sdk, "lib/cmake/rexglue/rexglueConfig.cmake");
    var release = Path.Combine(sdk, "lib/cmake/rexglue/rexglueTargets-release.cmake");
    var configText =
        "# source: https://github.com/rexglue/rexglue-sdk\n" +
        "file(GLOB _cmake_config_files \"${CMAKE_CURRENT_LIST_DIR}/rexglueTargets-*.cmake\")\n";
    File.WriteAllText(cmake, configText);
    File.WriteAllText(release, "# Release import\n");
    File.WriteAllText(Path.Combine(sdk, "lib/cmake/rexglue/header-only.cmake"),
                      "file(GLOB _cmake_config_files \"${CMAKE_CURRENT_LIST_DIR}/header-only-*.cmake\")\n");
    var diagnostics = new List<string>();
    var tools = new Tools(new RecordedProgress(), diagnostics.Add);
    var program =
        Path.Combine(Environment.SystemDirectory, "WindowsPowerShell/v1.0/powershell.exe");
    var arguments = new[] { "-NoProfile",
                            "-ExecutionPolicy",
                            "Bypass",
                            "-File",
                            Path.Combine(root, "launcher/scripts/Pack-Sdk.ps1"),
                            "-SdkDirectory",
                            sdk,
                            "-OutputFile",
                            output };
    if (tools.Run(program, sandbox, arguments, false) != 0)
      throw new Exception("SDK packaging failed: " + String.Join('\n', diagnostics));
    using (var zip = ZipFile.OpenRead(output)) {
      using var reader =
          new StreamReader(zip.GetEntry("lib/cmake/rexglue/rexglueConfig.cmake")!.Open());
      var imports = reader.ReadToEnd();
      if (imports.Contains("file(GLOB") || !imports.Contains("rexglueTargets-release.cmake"))
        throw new Exception(
            "The SDK package still uses wildcard paths to load its import configuration");
    }
    var hash = Tools.HashFile(output);
    foreach (var invalid in new[] { "missing import configuration", "machine path",
                                    "game executable", "unknown DLL", "missing license",
                                    "wrong SDK" }) {
      var license = Path.Combine(sdk, "licenses/rexglue/LICENSE");
      var game = Path.Combine(sdk, "bin/lollipop.exe");
      var dll = Path.Combine(sdk, "bin/unknown.dll");
      var marker = Path.Combine(sdk, "lollipop-sdk.id");
      if (invalid == "missing import configuration")
        File.Delete(release);
      if (invalid == "machine path")
        File.WriteAllText(cmake, "set(prefix \"C:/Developer/SDK\")\n");
      if (invalid == "game executable")
        File.WriteAllText(game, "do not publish");
      if (invalid == "unknown DLL")
        File.WriteAllText(dll, "do not publish");
      if (invalid == "missing license")
        File.Delete(license);
      if (invalid == "wrong SDK")
        File.WriteAllText(marker, "fedcba9876543210");
      if (tools.Run(program, sandbox, arguments, false) == 0 || Tools.HashFile(output) != hash)
        throw new Exception(
            "SDK packaging accepted invalid input or damaged its previous output: " + invalid);
      File.WriteAllText(cmake, configText);
      File.WriteAllText(release, "# Release import\n");
      File.Delete(game);
      File.Delete(dll);
      File.WriteAllText(license, "SDK fixture");
      File.WriteAllText(marker, identity);
    }
  }
}
