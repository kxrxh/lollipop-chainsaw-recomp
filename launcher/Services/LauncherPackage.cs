using System;
using System.IO;
using System.IO.Compression;
using System.Reflection;

namespace lollipop;

internal static class LauncherPackage {
  internal static readonly Assembly Assembly = typeof(LauncherPackage).Assembly;

  internal static Stream Resource(string name) {
    return Assembly.GetManifestResourceStream(name) ??
           throw new InvalidDataException("Missing launcher resource: " + name);
  }

  internal static string ExtractKit() {
    using var payload = Resource("kit.zip");
    var cache =
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                     "Lollipop Chainsaw Recomp", "installer");
    var kit = Path.Combine(cache, Tools.Hash(payload)[..16]);
    if (Directory.Exists(kit))
      return kit;
    Directory.CreateDirectory(cache);
    var stage = Path.Combine(cache, Guid.NewGuid().ToString("N"));
    Directory.CreateDirectory(stage);
    try {
      payload.Position = 0;
      using (var archive = new ZipArchive(payload, ZipArchiveMode.Read))
          Tools.Extract(archive, stage);
      try {
        Directory.Move(stage, kit);
      } catch (IOException) {
        if (!Directory.Exists(kit))
          throw;
      }
    } finally {
      Tools.DeleteStage(stage, cache);
    }
    return kit;
  }
}
