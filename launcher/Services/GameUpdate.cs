using System;
using System.IO;
using System.Text.RegularExpressions;

namespace lollipop;
internal static class GameUpdate {
  internal const string Identity = "Lollipop Chainsaw Recomp v1";

  internal static bool CanUpdate(string? destination) {
    if (String.IsNullOrEmpty(destination))
      return false;
    try {
      return File.ReadAllText(Path.Combine(destination, "out/launcher/install.id")) == Identity &&
             File.Exists(Path.Combine(destination, "game/default.xex")) &&
             Directory.Exists(Path.Combine(destination, "game/AshGame")) &&
             Directory.Exists(Path.Combine(destination, "game/Engine"));
    } catch (IOException) {
      return false;
    } catch (UnauthorizedAccessException) {
      return false;
    }
  }

  internal static string SdkId(string kit, string commit) {
    return Tools
        .HashText(commit + "\n" + Tools.HashFile(Path.Combine(kit, "patches/rexglue-fixes.patch")) +
                  "\n" +
                  Tools.HashFile(Path.Combine(kit, "patches/rexglue-shader-precompile.patch")))
        .Substring(0, 16);
  }

  internal static string Stage(string destination) {
    return Path.Combine(destination, "out/build/game-staging");
  }

  internal static void CheckClosed(string destination, string launcher) {
    var build = Path.Combine(destination, "out/build/win-amd64-release");
    if (Directory.Exists(build)) {
      foreach (var path in Directory.GetFiles(build)) {
        var extension = Path.GetExtension(path);
        if (String.Equals(extension, ".exe", StringComparison.OrdinalIgnoreCase) ||
            String.Equals(extension, ".dll", StringComparison.OrdinalIgnoreCase))
          CheckWritable(path);
      }
    }
    var installedLauncher = Path.Combine(destination, "Play Lollipop Chainsaw.exe");
    if (File.Exists(installedLauncher) &&
        !String.Equals(Path.GetFullPath(installedLauncher), Path.GetFullPath(launcher),
                       StringComparison.OrdinalIgnoreCase))
      CheckWritable(installedLauncher);
  }

  private static void CheckWritable(string path) {
    try {
      using (File.Open(path, FileMode.Open, FileAccess.ReadWrite, FileShare.None)) {}
    } catch (IOException) {
      throw new IOException("Close the game and its other launcher window before updating.");
    }
  }

  internal static void Recover(string destination) {
    var state = Path.Combine(destination, "out/launcher");
    var journal = Path.Combine(state, "publish.pending");
    if (!File.Exists(journal))
      return;
    // Older records lack readiness state, so a restored build must be tested again.
    var record = Regex.Match(File.ReadAllText(journal),
                             "\\A([0-9a-f]{32})(?:\\n(ready|incomplete|new))?\\z");
    if (!record.Success)
      throw new IOException("The update recovery record is invalid. Keep your user folder; " +
                            "details are in the setup log.");
    var previous = Path.Combine(state, record.Groups[1].Value);
    var previousState = record.Groups[2].Value;
    var active = Path.Combine(destination, "out/build/win-amd64-release");
    var restored = Directory.Exists(previous) && !Directory.Exists(active);
    if (restored)
      Directory.Move(previous, active);
    if (Directory.Exists(previous) && !File.Exists(Path.Combine(active, "lollipop.exe")))
      throw new IOException(
          "The previous game build could not be recovered. Choose Repair game again.");
    var published = !restored && !Directory.Exists(Stage(destination)) &&
                    (Directory.Exists(previous) || previousState == "new");
    if (File.Exists(Path.Combine(active, "lollipop.exe")) &&
        (published || previousState == "ready"))
      File.Delete(Path.Combine(state, "setup.pending"));
    File.Delete(journal);
    // Cleanup failure must not make a successfully published game unplayable.
    try {
      Tools.DeleteStage(previous, state);
    } catch (IOException) {
    } catch (UnauthorizedAccessException) {
    }
  }

  internal static void Publish(string destination, bool hadPlayableBuild) {
    var stage = Stage(destination);
    if (!File.Exists(Path.Combine(stage, "lollipop.exe")) ||
        !File.Exists(Path.Combine(stage, "rexruntime.dll")) ||
        !File.Exists(Path.Combine(stage, "rexgpu-xenos.dll")))
      throw new IOException("The updated game is incomplete. The previous build was kept.");
    File.WriteAllText(Path.Combine(stage, "recomp-version.txt"), Releases.GameVersion);
    var state = Path.Combine(destination, "out/launcher");
    var active = Path.Combine(destination, "out/build/win-amd64-release");
    Directory.CreateDirectory(state);
    Directory.CreateDirectory(Path.GetDirectoryName(active)!);
    // Advanced key bindings are stored beside the executable; PC settings and saves stay in user/.
    if (Directory.Exists(active)) {
      foreach (var config in Directory.GetFiles(active, "*.toml")) {
        var copy = Path.Combine(stage, Path.GetFileName(config));
        if (File.Exists(copy))
          File.SetAttributes(copy, FileAttributes.Normal);
        File.Copy(config, copy, true);
      }
    }
    var id = Guid.NewGuid().ToString("N");
    var previous = Path.Combine(state, id);
    var journal = Path.Combine(state, "publish.pending");
    var previousState =
        Directory.Exists(active) ? (hadPlayableBuild ? "ready" : "incomplete") : "new";
    File.WriteAllText(journal + ".tmp", id + "\n" + previousState);
    File.Move(journal + ".tmp", journal);
    if (Directory.Exists(active))
      Directory.Move(active, previous);
    try {
      Directory.Move(stage, active);
    } catch {
      Recover(destination);
      throw;
    }
    Recover(destination);
  }
}
