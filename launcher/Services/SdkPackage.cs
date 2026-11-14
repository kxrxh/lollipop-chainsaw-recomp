using System;
using System.IO;
using System.IO.Compression;
using System.Linq;

namespace lollipop;

internal static class SdkPackage {
  internal const string Commit = "bd833a2ab69d305f99ddf57418fc38ffec8af93d";
  private static readonly string[] RequiredFiles = [
    "lollipop-sdk.id", "bin/rexglue.exe", "bin/rexruntime.dll", "bin/rexgpu-xenos.dll",
    "bin/msvcp140.dll", "bin/msvcp140_atomic_wait.dll", "bin/vcruntime140.dll",
    "bin/vcruntime140_1.dll", "include/rex/rex_app.h", "lib/rexruntime.lib",
    "lib/cmake/rexglue/rexglueConfig.cmake", "share/rexglue/rex_app.cpp",
    "lib/cmake/rexglue/rexglueTargets-release.cmake",
    "licenses/rexglue/LICENSE", "licenses/SOURCES.txt"
  ];

  internal static string Install(string kit, string destination) {
    using var payload =
        LauncherPackage.Assembly.GetManifestResourceStream("sdk.zip") ??
        throw new IOException("This launcher was built without the bundled game tools. Download " +
                              "the packaged launcher release to install or repair the game.");
    return Install(payload, Path.Combine(destination, "out/tools/sdk"),
                   GameUpdate.SdkId(kit, Commit));
  }

  private static void Validate(string directory, string identity) {
    if (!RequiredFiles.All(name => File.Exists(Path.Combine(directory, name))))
      throw new InvalidDataException(
          "The bundled SDK is incomplete. Use a complete launcher download.");
    var marker = Path.Combine(directory, "lollipop-sdk.id");
    if (new FileInfo(marker).Length != 16 || File.ReadAllText(marker) != identity)
      throw new InvalidDataException("The bundled SDK does not match this launcher's game " +
                                     "sources. Rebuild the SDK before packaging the launcher.");
  }

  internal static string Install(Stream payload, string cache, string identity) {
    payload.Position = 0;
    var folder = Path.Combine(cache, Tools.Hash(payload)[..16]);
    if (Directory.Exists(folder)) {
      try {
        Validate(folder, identity);
        return folder;
      } catch (InvalidDataException) {
        Directory.Move(folder, Path.Combine(cache, "incomplete-" + Guid.NewGuid().ToString("N")));
      }
    }
    Directory.CreateDirectory(cache);
    var stage = Path.Combine(cache, Guid.NewGuid().ToString("N"));
    Directory.CreateDirectory(stage);
    try {
      payload.Position = 0;
      using (var archive = new ZipArchive(payload, ZipArchiveMode.Read, true))
          Tools.Extract(archive, stage);
      Validate(stage, identity);
      try {
        Tools.MoveDirectory(stage, folder);
      } catch (IOException) when (Directory.Exists(folder)) {
        Validate(folder, identity);
      }
      return folder;
    } finally {
      Tools.DeleteStage(stage, cache);
    }
  }
}
