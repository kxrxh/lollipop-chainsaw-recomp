using System;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;

namespace lollipop;

internal static class LauncherInstallationTests {
  private static void Check(bool condition, string message) {
    if (!condition)
      throw new Exception(message);
  }

  private static void Fails(Action action, string message) {
    try {
      action();
    } catch (IOException) {
      return;
    }
    throw new Exception(message);
  }

  internal static void Run(string sandbox) {
    var destination = Path.Combine(sandbox, "launcher player's [1] Ж game");
    Installer.Initialize(destination);
    var desktop = Path.Combine(sandbox, "redirected desktop");
    Directory.CreateDirectory(desktop);
    Fails(() => LauncherInstallation.Install(Environment.ProcessPath!, destination),
          "Launcher installation accepted an incomplete game");
    foreach (var folder in new[] { "game/AshGame", "game/Engine", "out/build/win-amd64-release" })
      Directory.CreateDirectory(Path.Combine(destination, folder));
    File.WriteAllText(Path.Combine(destination, "game/default.xex"), "disc");
    File.WriteAllText(Path.Combine(destination, "out/build/win-amd64-release/lollipop.exe"),
                      "game");
    Fails(() => LauncherInstallation.CreateShortcut(destination, desktop),
          "A desktop shortcut was created without the installed launcher");
    LauncherInstallation.Install(Environment.ProcessPath!, destination);
    var target = LauncherInstallation.PathFor(destination);
    Check(Tools.HashFile(target) == Tools.HashFile(Environment.ProcessPath!) &&
              !File.Exists(Path.Combine(destination, LauncherInstallation.LegacyFileName)),
          "Setup did not install the launcher under its own name");
    LauncherInstallation.Install(target, destination);
    using (File.Open(target, FileMode.Open, FileAccess.Read, FileShare.None)) {
      Fails(() => GameUpdate.CheckClosed(destination, Environment.ProcessPath!),
            "Updating ignored a running installed launcher");
      Fails(() => LauncherInstallation.Install(Environment.ProcessPath!, destination),
            "Setup replaced a locked launcher");
    }
    Check(!Directory.GetFiles(destination, "*.tmp").Any(),
          "A failed launcher replacement left staging files");

    var launcher = new Launcher(sandbox, target);
    try {
      Check(launcher.Destination == destination && launcher.CurrentPage == "Home" &&
                launcher.Screen == "Ready",
            "The installed launcher did not open its game's home page");
      launcher.CreateDesktopShortcut(desktop);
      Check(launcher.Find<System.Windows.Controls.TextBlock>("ShortcutStatus")
                .Text.StartsWith("Desktop shortcut created"),
            "Manage game could not create the desktop shortcut");
      launcher.CreateDesktopShortcut(Path.Combine(sandbox, "missing desktop"));
      Check(launcher.Screen == "Ready" && Installer.Ready(destination) &&
                launcher.Find<System.Windows.Controls.TextBlock>("ShortcutStatus")
                    .Text.StartsWith("Couldn't create"),
            "A desktop shortcut failure made the installed game unplayable");
    } finally {
      launcher.Window.Close();
    }
    var path = LauncherInstallation.CreateShortcut(destination, desktop);
    dynamic shell = Activator.CreateInstance(Type.GetTypeFromProgID("WScript.Shell", true)!)!;
    try {
      dynamic shortcut = shell.CreateShortcut(path);
      try {
        Check(String.Equals((string)shortcut.TargetPath, target,
                            StringComparison.OrdinalIgnoreCase) &&
                  (string)shortcut.Arguments == "" &&
                  String.Equals((string)shortcut.WorkingDirectory, destination,
                                StringComparison.OrdinalIgnoreCase) &&
                  ((string)shortcut.IconLocation)
                      .StartsWith(target, StringComparison.OrdinalIgnoreCase),
              "The desktop shortcut bypassed the launcher or lost its working directory or icon");
        shortcut.TargetPath = Path.Combine(destination, LauncherInstallation.LegacyFileName);
        shortcut.Arguments = "old arguments";
        shortcut.Save();
      } finally {
        Marshal.FinalReleaseComObject(shortcut);
      }
      LauncherInstallation.CreateShortcut(destination, desktop);
      shortcut = shell.CreateShortcut(path);
      try {
        Check((string)shortcut.TargetPath == target && (string)shortcut.Arguments == "",
              "An old launcher shortcut was not migrated");
        shortcut.TargetPath = Path.Combine(sandbox, "unrelated.exe");
        shortcut.Save();
      } finally {
        Marshal.FinalReleaseComObject(shortcut);
      }
      var original = File.ReadAllBytes(path);
      Fails(() => LauncherInstallation.CreateShortcut(destination, desktop),
            "An unrelated desktop shortcut was overwritten");
      Check(File.ReadAllBytes(path).SequenceEqual(original) &&
                Directory.GetFiles(desktop).Length == 1,
            "A failed shortcut creation changed existing files or left temporary links");
    } finally {
      Marshal.FinalReleaseComObject(shell);
    }
  }
}
