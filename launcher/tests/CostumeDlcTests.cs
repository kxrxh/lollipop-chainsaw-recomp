using System;
using System.Buffers.Binary;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;

namespace lollipop;

internal static class CostumeDlcTests {
  private static void Check(bool condition, string message) {
    if (!condition)
      throw new Exception(message);
  }
  private static void Refuses(Action action) {
    try {
      action();
    } catch (Exception error) when (error is IOException or InvalidDataException) {
      return;
    }
    throw new Exception("Invalid DLC was accepted.");
  }

  private static byte[] Package(uint title = 0x565707d0, uint type = 2) {
    var bytes = new byte[0xa000];
    "LIVE"u8.CopyTo(bytes);
    BinaryPrimitives.WriteUInt32BigEndian(bytes.AsSpan(0x340), 0x971a);
    BinaryPrimitives.WriteUInt32BigEndian(bytes.AsSpan(0x344), type);
    BinaryPrimitives.WriteUInt32BigEndian(bytes.AsSpan(0x360), title);
    bytes[0x32c] = 1;
    Encoding.BigEndianUnicode.GetBytes("Fixture outfit").CopyTo(bytes, 0x411);
    return bytes;
  }

  internal static void Run(string sandbox) {
    var root = Path.Combine(sandbox, "costume fixtures with spaces");
    Directory.CreateDirectory(root);
    var bytes = Package();
    using (var stream = new MemoryStream(bytes)) {
      var pack = CostumeDlc.Inspect(stream, "fixture", "fixture");
      Check(pack.Name == "Fixture outfit" && pack.Id == "01" + new string('0', 38),
            "DLC metadata offsets changed.");
    }
    Refuses(() => CostumeDlc.Inspect(new MemoryStream(Package(1)), "wrong game", ""));
    Refuses(() => CostumeDlc.Inspect(new MemoryStream(Package(type: 1)), "save", ""));
    Refuses(() => CostumeDlc.Inspect(new MemoryStream(bytes[..100]), "truncated", ""));
    var zipPath = Path.Combine(root, "costumes.zip");
    using (var zip = ZipFile.Open(zipPath, ZipArchiveMode.Create)) {
      foreach (var name in new[] { "../../escape", "nested/565707D0/00000002/package" }) {
        using var stream = zip.CreateEntry(name).Open();
        stream.Write(bytes);
      }
      using var readme = zip.CreateEntry("README.txt").Open();
      readme.Write("No game assets in this fixture."u8);
    }
    var staging = Path.Combine(sandbox, "staging");
    using (var import = CostumeDlc.Scan([zipPath], staging)) {
      Check(import.Packs.Count == 1 && File.Exists(import.Packs[0].File),
            "ZIP nesting or duplicate detection failed.");
      Check(!File.Exists(Path.Combine(sandbox, "escape")), "ZIP path escaped staging.");
    }
    Check(!Directory.GetDirectories(staging).Any(), "DLC staging was not cleaned up.");
    var folder = Path.Combine(root, "Xbox DLC/565707D0/00000002");
    Directory.CreateDirectory(folder);
    File.WriteAllBytes(Path.Combine(folder, "package"), bytes);
    using (var import = CostumeDlc.Scan([Path.Combine(root, "Xbox DLC")], staging))
        Check(import.Packs.Count == 1, "Folder import lost nested Xbox packages.");
    var empty = Path.Combine(root, "empty");
    Directory.CreateDirectory(empty);
    Refuses(() => CostumeDlc.Scan([empty], staging));
    var invalid = Path.Combine(root, "invalid.zip");
    File.WriteAllText(invalid, "invalid");
    Refuses(() => CostumeDlc.Scan([invalid], staging));
    using (var import = CostumeDlc.Scan([zipPath, Path.Combine(root, "Xbox DLC")], staging))
        Check(import.Packs.Count == 1, "Mixed ZIP/folder selection duplicated content.");
    Console.WriteLine("Costume DLC parser checks passed.");
  }

  private static void Pump(Task task) {
    var limit = DateTime.UtcNow.AddSeconds(60);
    while (!task.IsCompleted && DateTime.UtcNow < limit) {
      var frame = new DispatcherFrame();
      Dispatcher.CurrentDispatcher.BeginInvoke(DispatcherPriority.Background,
                                               new Action(() => frame.Continue = false));
      Dispatcher.PushFrame(frame);
      Thread.Sleep(10);
    }
    Check(task.IsCompleted, "DLC action timed out.");
    task.GetAwaiter().GetResult();
  }

  private static void Capture(Launcher launcher, string path) {
    launcher.Window.UpdateLayout();
    launcher.Window.Dispatcher.Invoke(() => {}, DispatcherPriority.ApplicationIdle);
    var bitmap =
        new RenderTargetBitmap((int)launcher.Window.ActualWidth, (int)launcher.Window.ActualHeight,
                               96, 96, PixelFormats.Pbgra32);
    bitmap.Render(launcher.Window);
    var png = new PngBitmapEncoder();
    png.Frames.Add(BitmapFrame.Create(bitmap));
    using var stream = File.Create(path);
    png.Save(stream);
  }

  internal static void Preview(string[] args) {
    // Explicit paths keep real-package integration tests separate from synthetic unit fixtures.
    var root = Path.GetFullPath(args[1]);
    var destination = Path.Combine(root, "installation");
    Directory.CreateDirectory(root);
    Run(root);
    Installer.Initialize(destination);
    var build = Path.Combine(destination, "out/build/win-amd64-release");
    foreach (var folder in new[] { "game/AshGame", "game/Engine", "out/build/win-amd64-release",
                                   "user" })
      Directory.CreateDirectory(Path.Combine(destination, folder));
    File.WriteAllText(Path.Combine(destination, "game/default.xex"), "UI fixture");
    var save = Path.Combine(destination, "user/preserve-save");
    File.WriteAllText(save, "unchanged");
    foreach (var path in Directory.GetFiles(args[2]).Where(
                 p => Path.GetExtension(p) == ".dll" ||
                      Path.GetFileName(p) is "lollipop.exe" or "lollipop_dlc_tool.exe"))
      File.Copy(path, Path.Combine(build, Path.GetFileName(path)), true);
    SynchronizationContext.SetSynchronizationContext(new DispatcherSynchronizationContext());
    var launcher = new Launcher(root, Environment.ProcessPath!, Path.Combine(root, "location.txt"));
    try {
      launcher.Destination = destination;
      launcher.Home();
      launcher.SetPage("Costumes");
      if (args[0] == "--costume-preview") {
        launcher.Window.WindowStartupLocation = WindowStartupLocation.Manual;
        launcher.Window.Left = -10000;
        launcher.Window.Top = -10000;
        launcher.Window.ShowInTaskbar = false;
        launcher.Window.ShowActivated = false;
      }
      launcher.Window.Show();
      Pump(launcher.Costumes.Select(args.Skip(3).ToArray()));
      Check(launcher.Find<ItemsControl>("CostumeCandidates").Items.Count == 2 &&
                launcher.Find<Button>("InstallCostumes").IsEnabled,
            "Real archives did not reach the review UI.");
      Capture(launcher, Path.Combine(root, "costumes-review.png"));
      launcher.Window.Width = 900;
      launcher.Window.Height = 640;
      Capture(launcher, Path.Combine(root, "costumes-small.png"));
      foreach (var name in new[] { "CostumesNav", "ChooseDlcZips", "ChooseDlcFolder",
                                   "InstallCostumes" }) {
        var control = launcher.Find<Button>(name);
        Check(control.IsVisible && control.ActualWidth > 0,
              "Costume action disappeared at minimum size: " + name);
      }
      launcher.Window.Width = 1200;
      launcher.Window.Height = 780;
      if (args[0] == "--costume-review") {
        Capture(launcher, Path.Combine(root, "costumes-review.png"));
        launcher.Window.Closed += delegate {
          Dispatcher.CurrentDispatcher.BeginInvokeShutdown(DispatcherPriority.Background);
        };
        Dispatcher.Run();
        return;
      }
      using (File.Open(Path.Combine(destination, "out/launcher/launcher.lock"), FileMode.OpenOrCreate,
                       FileAccess.ReadWrite, FileShare.None)) {
        using var blocked = CostumeDlc.Scan(args.Skip(3), Path.Combine(root, "locked-import"));
        Refuses(() => CostumeDlc.Install(blocked, destination, Environment.ProcessPath!,
                                         new Tools(new RecordedProgress(), _ => {})));
      }
      using (File.Open(Path.Combine(build, "lollipop.exe"), FileMode.Open, FileAccess.Read,
                       FileShare.None)) {
        using var blocked = CostumeDlc.Scan(args.Skip(3), Path.Combine(root, "running-import"));
        Refuses(() => CostumeDlc.Install(blocked, destination, Environment.ProcessPath!,
                                         new Tools(new RecordedProgress(), _ => {})));
      }
      Pump(launcher.Costumes.Install());
      Check(CostumeDlc.List(destination).Count == 2,
            "Real DLC installation failed: " + launcher.Find<TextBlock>("CostumeStatus").Text);
      var originals = Directory
                          .GetFiles(Path.Combine(destination, CostumeDlc.TitlePath), "*",
                                    SearchOption.AllDirectories)
                          .ToDictionary(p => p, Tools.HashFile);
      Capture(launcher, Path.Combine(root, "costumes-installed.png"));
      Pump(launcher.Costumes.Select(args.Skip(3).ToArray()));
      Check(!launcher.Find<Button>("InstallCostumes").IsEnabled,
            "Already installed DLC can be overwritten from the UI.");
      foreach (var pair in originals)
        Check(Tools.HashFile(pair.Key) == pair.Value, "Reimport changed existing DLC.");
      Check(File.ReadAllText(save) == "unchanged", "Costume installation changed a save.");
      Console.WriteLine(
          "Both real DLC archives installed; duplicate and save-preservation checks passed.");
    } finally {
      launcher.Window.Close();
    }
  }
}
