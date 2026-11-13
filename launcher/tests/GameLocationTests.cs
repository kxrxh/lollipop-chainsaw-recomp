using System;
using System.IO;
using System.Linq;
using System.Windows.Controls;

namespace lollipop;

internal static class GameLocationTests {
  private static void Check(bool condition, string message) {
    if (!condition)
      throw new Exception(message);
  }

  private static void Game(string folder) {
    Installer.Initialize(folder);
    foreach (var child in new[] { "game/AshGame", "game/Engine", "out/build/win-amd64-release" })
      Directory.CreateDirectory(Path.Combine(folder, child));
    File.WriteAllText(Path.Combine(folder, "game/default.xex"), "disc");
    File.WriteAllText(Path.Combine(folder, "out/build/win-amd64-release/lollipop.exe"), "game");
  }

  internal static void Run(string sandbox) {
    var file = Path.Combine(sandbox, "preferences/game-location.txt");
    var selected = Path.Combine(sandbox, "remember player's [1] Ж game");
    var other = Path.Combine(sandbox, "another installation");
    Check(GameLocation.Load(file) == null, "A missing preference file prevented startup");
    Directory.CreateDirectory(selected);
    Check(GameLocation.Save(file, selected) && GameLocation.Load(file) == selected,
          "The selected installation folder was not remembered before setup");
    foreach (var invalid in new[] { "", "relative path", selected + "\n", "\0",
                                    Path.Combine(sandbox, "missing game") }) {
      File.WriteAllText(file, invalid);
      Check(GameLocation.Load(file) == null, "An invalid or unavailable saved path was restored");
      Check(File.ReadAllText(file) == invalid,
            "Loading a missing game discarded its saved location");
    }
    File.WriteAllBytes(file, [0xff]);
    Check(GameLocation.Load(file) == null, "Malformed UTF-8 prevented launcher startup");
    File.WriteAllText(file, new string('x', 128 * 1024 + 1));
    Check(GameLocation.Load(file) == null, "An oversized location file was accepted");
    Check(GameLocation.Save(file, selected), "A corrupt preference could not be replaced");
    Game(other);
    using (File.Open(file, FileMode.Open, FileAccess.Read, FileShare.Read))
        Check(!GameLocation.Save(file, other), "A locked preference file was overwritten");
    Check(GameLocation.Load(file) == selected &&
              !Directory.GetFiles(Path.GetDirectoryName(file)!, "*.tmp").Any(),
          "A failed preference write lost the saved location or left temporary files");

    Game(selected);
    File.Delete(file);
    var first = new Launcher(sandbox, Environment.ProcessPath!, file);
    try {
      first.Destination = selected;
      first.Home();
      Check(GameLocation.Load(file) == selected, "Selecting a game did not save its location");
    } finally {
      first.Window.Close();
    }
    var downloaded = Path.Combine(sandbox, "new download", LauncherInstallation.FileName);
    var reopened = new Launcher(sandbox, downloaded, file);
    try {
      Check(reopened.Destination == selected && reopened.Screen == "Ready" &&
                reopened.CurrentPage == "Home" &&
                reopened.Find<Button>("Primary").Content.Equals("Play"),
            "A restarted or updated launcher required locating the installed game again");
    } finally {
      reopened.Window.Close();
    }
    var installed = new Launcher(sandbox, LauncherInstallation.PathFor(other), file);
    try {
      Check(installed.Destination == other && GameLocation.Load(file) == other,
            "A per-installation launcher opened the previously remembered game");
    } finally {
      installed.Window.Close();
    }
    File.WriteAllText(Path.Combine(other, "out/launcher/setup.pending"), "Installing");
    var interrupted = new Launcher(sandbox, downloaded, file);
    try {
      Check(interrupted.Destination == other && interrupted.Screen == "Update",
            "An interrupted installation was forgotten on restart");
    } finally {
      interrupted.Window.Close();
    }

    var blocked = Path.Combine(sandbox, "blocked preference directory");
    File.WriteAllText(blocked, "keep");
    var readOnly = new Launcher(sandbox, LauncherInstallation.PathFor(selected),
                                Path.Combine(blocked, "game-location.txt"));
    try {
      Check(readOnly.Screen == "Ready" &&
                readOnly.Find<TextBlock>("Description").Text.Contains("Couldn't remember"),
            "Preference write failure prevented use of an installed game or was hidden");
    } finally {
      readOnly.Window.Close();
    }
  }
}
