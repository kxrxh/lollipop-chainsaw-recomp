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

  internal static string SyncKit(string kit, string destination) {
    // Stable source paths and unchanged timestamps let Ninja reuse compiled objects.
    Directory.CreateDirectory(destination);
    foreach (var source in Directory.GetFiles(kit, "*", SearchOption.AllDirectories)) {
      var target = Path.Combine(destination, Path.GetRelativePath(kit, source));
      if (File.Exists(target) && Tools.HashFile(source) == Tools.HashFile(target))
        continue;
      Directory.CreateDirectory(Path.GetDirectoryName(target)!);
      File.Copy(source, target, true);
      File.SetLastWriteTimeUtc(target, DateTime.UtcNow);
    }
    foreach (var target in Directory.GetFiles(destination, "*", SearchOption.AllDirectories)) {
      if (!File.Exists(Path.Combine(kit, Path.GetRelativePath(destination, target))))
        File.Delete(target);
    }
    return destination;
  }

  internal static string ExtractKit(string? directory = null) {
    using var payload = Resource("kit.zip");
    var cache = directory ?? Path.Combine(GameLocation.DataDirectory, "source");
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
