using System;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;

namespace lollipop;

internal static class LauncherUpdate {
  internal const string Command = "--finish-launcher-update";
  internal const string AssetName = "Lollipop-Chainsaw-Launcher.exe";

  internal static ReleaseAsset Asset(Release release) {
    var assets = (release.Assets ?? []).Where(asset => asset?.Name == AssetName).ToArray();
    if (assets.Length != 1)
      throw new InvalidDataException("This release does not contain a Windows launcher. " +
                                     "Open View release to check its downloads.");
    var asset = assets[0];
    var expected =
        Releases.Page + "/download/" + Uri.EscapeDataString(release.Tag) + "/" + AssetName;
    if (asset.Url != expected || asset.Size <= 0 || asset.Digest == null ||
        !Regex.IsMatch(asset.Digest, @"\Asha256:[0-9a-f]{64}\z"))
      throw new InvalidDataException("The launcher download has no valid GitHub URL or SHA-256. " +
                                     "The current launcher was kept.");
    return asset;
  }

  internal static string Download(ReleaseAsset asset, Tools tools, string cache) {
    var hash = asset.Digest![7..];
    var package = new Package("Launcher", asset.Url, hash, LauncherInstallation.FileName);
    var downloaded = tools.Get(package, cache, false);
    if (new FileInfo(downloaded).Length != asset.Size || Tools.HashFile(downloaded) != hash) {
      var folder = Path.GetDirectoryName(downloaded)!;
      Directory.Move(folder, folder + ".invalid-" + Guid.NewGuid().ToString("N"));
      throw new InvalidDataException("The launcher download failed its integrity check. Retry " +
                                     "the update; the current launcher was kept.");
    }
    return downloaded;
  }

  internal static void CheckVersion(string downloaded, string current) {
    var version = FileVersionInfo.GetVersionInfo(downloaded).ProductVersion;
    if (version == null || Releases.IsNewer(current.Split('-', '+')[0], version))
      throw new InvalidDataException("The latest published launcher is older than this build. " +
                                     "The current launcher was kept.");
  }

  internal static Process StartReplacement(string downloaded, string target, string hash) {
    if (Tools.HashFile(downloaded) != hash)
      throw new InvalidDataException("The downloaded launcher changed before the update.");
    // Use our helper protocol even when the latest stable EXE predates self-updating.
    var helper = Path.Combine(Path.GetDirectoryName(downloaded)!, "updater.exe");
    File.Copy(Environment.ProcessPath!, helper, true);
    var start = new ProcessStartInfo(helper) { UseShellExecute = false, CreateNoWindow = true };
    start.ArgumentList.Add(Command);
    start.ArgumentList.Add(Path.GetFullPath(target));
    start.ArgumentList.Add(Environment.ProcessId.ToString(CultureInfo.InvariantCulture));
    start.ArgumentList.Add(Path.GetFullPath(downloaded));
    start.ArgumentList.Add(hash);
    return Process.Start(start) ?? throw new IOException("Could not start the launcher update.");
  }

  internal static void Complete(string[] arguments) {
    if (arguments.Length != 5 || arguments[0] != Command ||
        !Int32.TryParse(arguments[2], NumberStyles.None, CultureInfo.InvariantCulture,
                        out var parent) ||
        parent <= 0 || parent == Environment.ProcessId ||
        !Regex.IsMatch(arguments[4], @"\A[0-9a-f]{64}\z") ||
        !Path.IsPathFullyQualified(arguments[1]) || !Path.IsPathFullyQualified(arguments[3]) ||
        !String.Equals(Path.GetExtension(arguments[1]), ".exe",
                       StringComparison.OrdinalIgnoreCase) ||
        String.Equals(Path.GetFullPath(arguments[3]), Path.GetFullPath(arguments[1]),
                      StringComparison.OrdinalIgnoreCase))
      throw new InvalidDataException("Invalid launcher update request.");
    if (Tools.HashFile(arguments[3]) != arguments[4])
      throw new InvalidDataException("The downloaded launcher failed its integrity check.");
    Process? previous = null;
    try {
      previous = Process.GetProcessById(parent);
    } catch (ArgumentException) {
      // The launcher may have exited before the downloaded helper started.
    }
    using (previous) {
      if (previous != null && !previous.WaitForExit(60000))
        throw new IOException("Close the previous launcher before retrying the update.");
    }
    Replace(arguments[3], arguments[1], arguments[4]);
    using var restarted = Process.Start(new ProcessStartInfo(arguments[1]) {
      UseShellExecute = false, WorkingDirectory = Path.GetDirectoryName(arguments[1])!
    }) ?? throw new IOException("The launcher was updated. Open it again to continue.");
  }

  internal static void Replace(string downloaded, string target, string hash) {
    var temporary = target + "." + Guid.NewGuid().ToString("N") + ".tmp";
    try {
      File.Copy(downloaded, temporary);
      if (Tools.HashFile(temporary) != hash)
        throw new InvalidDataException("The launcher copy failed its integrity check.");
      // Publish only a complete verified EXE; failed downloads never touch the running launcher.
      File.Replace(temporary, target, null);
    } finally {
      File.Delete(temporary);
    }
  }
}
