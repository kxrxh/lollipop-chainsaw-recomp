using System;
using System.IO;
using System.Runtime.InteropServices;

namespace lollipop;

internal static class LauncherInstallation {
  internal const string FileName = "Lollipop Chainsaw Launcher.exe";
  internal const string LegacyFileName = "Play Lollipop Chainsaw.exe";

  internal static string PathFor(string destination) => Path.Combine(destination, FileName);

  internal static void Install(string executable, string destination) {
    if (!GameUpdate.CanUpdate(destination))
      throw new IOException("Choose an installation created by this launcher.");
    var target = PathFor(destination);
    if (String.Equals(Path.GetFullPath(executable), Path.GetFullPath(target),
                      StringComparison.OrdinalIgnoreCase))
      return;
    var temporary = target + "." + Guid.NewGuid().ToString("N") + ".tmp";
    try {
      File.Copy(executable, temporary);
      if (File.Exists(target))
        File.Replace(temporary, target, null);
      else
        File.Move(temporary, target);
    } finally {
      File.Delete(temporary);
    }
  }

  internal static string CreateShortcut(string destination, string desktopDirectory) {
    if (!Installer.Ready(destination) || !File.Exists(PathFor(destination)))
      throw new IOException("Install the launcher before creating its desktop shortcut.");
    if (String.IsNullOrWhiteSpace(desktopDirectory) || !Directory.Exists(desktopDirectory))
      throw new IOException("Your desktop folder could not be found.");
    var target = Path.GetFullPath(PathFor(destination));
    var path = Path.Combine(Path.GetFullPath(desktopDirectory), "Lollipop Chainsaw Recomp.lnk");
    var temporary =
        Path.Combine(Path.GetDirectoryName(path)!, Guid.NewGuid().ToString("N") + ".lnk");
    dynamic shell = Activator.CreateInstance(Type.GetTypeFromProgID("WScript.Shell", true)!)!;
    try {
      if (File.Exists(path)) {
        dynamic existing = shell.CreateShortcut(path);
        try {
          string previous = existing.TargetPath;
          if (!String.Equals(previous, target, StringComparison.OrdinalIgnoreCase) &&
              !String.Equals(previous, Path.GetFullPath(Path.Combine(destination, LegacyFileName)),
                             StringComparison.OrdinalIgnoreCase))
            throw new IOException(
                "A different desktop shortcut already uses this name. Rename it, then try again.");
        } finally {
          Marshal.FinalReleaseComObject(existing);
        }
      }
      dynamic shortcut = shell.CreateShortcut(temporary);
      try {
        shortcut.TargetPath = target;
        shortcut.Arguments = "";
        shortcut.WorkingDirectory = Path.GetFullPath(destination);
        shortcut.IconLocation = target + ",0";
        shortcut.Description = "Open the Lollipop Chainsaw Recomp launcher";
        shortcut.Save();
      } finally {
        Marshal.FinalReleaseComObject(shortcut);
      }
      File.Move(temporary, path, true);
      return path;
    } finally {
      Marshal.FinalReleaseComObject(shell);
      File.Delete(temporary);
    }
  }
}
