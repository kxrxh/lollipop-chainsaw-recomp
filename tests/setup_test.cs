using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Net;
using System.Net.Sockets;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Threading;

namespace lollipop {
internal sealed class RecordedProgress : IProgress<InstallProgress> {
  internal readonly List<InstallProgress> Updates = new List<InstallProgress>();
  public void Report(InstallProgress value) { lock (Updates) Updates.Add(value); }
}

internal static class SetupTests {
  [DllImport("shell32.dll", CharSet = CharSet.Unicode)]
  private static extern int GetCurrentProcessExplicitAppUserModelID(out IntPtr appId);

  private static void Check(bool condition, string message) {
    if (!condition)
      throw new Exception(message);
  }

  private static void Fails(Action action, string message) {
    try {
      action();
    } catch (IOException) {
      return;
    } catch (InvalidDataException) {
      return;
    }
    throw new Exception(message);
  }

  [STAThread]
  private static int Main(string[] args) {
    if (args.Length > 0 && args[0] == "--echo") {
      foreach (var value in args.Skip(1))
        Console.WriteLine(Convert.ToBase64String(Encoding.UTF8.GetBytes(value)));
      return 0;
    }
    if (args.Length > 0 && args[0] == "--fail") {
      Console.WriteLine("[1/4] Building fixture");
      for (int i = 1; i <= 150; i++)
        Console.Error.WriteLine("Fixture diagnostic " + i);
      return 7;
    }
    if (args.Any(arg => arg.StartsWith("--game_data_root=")))
      return FixtureGame(args);
    var sandbox =
        Path.Combine(Path.GetTempPath(), "lollipop-setup-" + Guid.NewGuid().ToString("N"));
    Directory.CreateDirectory(sandbox);
    try {
      Setup.SetTaskbarIdentity();
      IntPtr appId;
      Marshal.ThrowExceptionForHR(GetCurrentProcessExplicitAppUserModelID(out appId));
      try {
        Check(Marshal.PtrToStringUni(appId) == "LollipopChainsaw.Recomp.Setup",
              "Installer lost its taskbar identity");
      } finally { Marshal.FreeCoTaskMem(appId); }
      var progress = new RecordedProgress();
      var log = new List<string>();
      var tools = new Tools(progress, delegate(string line) { lock (log) log.Add(line); });
      var executable = Assembly.GetExecutingAssembly().Location;
      var special = new[] { "",
                            "plain",
                            "player's [disc] & $data %PATH%",
                            "unicode-Ж",
                            "quote\"inside",
                            @"C:\space path\",
                            @"slash\\\""quote" };
      var encoded = tools.Capture(executable, sandbox, new[] { "--echo" }.Concat(special).ToArray())
                        .Split('\n');
      // Capture trims its result; the empty first argument is checked separately below.
      var nonempty =
          encoded.Select(line => Encoding.UTF8.GetString(Convert.FromBase64String(line.Trim())))
              .ToArray();
      Check(nonempty.SequenceEqual(special.Skip(1)),
            "Windows argument quoting changed an argument");
      var empty = new List<string>();
      tools.Run(executable, sandbox, new[] { "--echo", "" }, true, empty.Add);
      Check(empty.Count == 1 && empty[0] == "", "Empty argument was lost");
      Fails(delegate { tools.Run(executable, sandbox, new[] { "--fail" }, true); },
            "Process failure was ignored");
      Check(log.Contains("Fixture diagnostic 1") && log.Contains("Fixture diagnostic 150"),
            "Full native diagnostics were lost");
      Check(progress.Updates.Any(update => update.Activity == "Compiling" && update.Percent == 25),
            "Compiler progress was lost");
      var writes = 0;
      var failingLog = new Tools(progress, delegate {
        if (++writes > 1)
          throw new IOException("Fixture disk full");
      });
      Fails(delegate { failingLog.Run(executable, sandbox, new[] { "--echo", "output" }, true); },
            "Log failure was ignored");
      Check(Tools.Compatible("Git", "git version 2.56.1.windows.1") &&
                !Tools.Compatible("Git", "git version 2.20.0"),
            "Git version check failed");
      Check(Tools.Compatible("LLVM", "clang version 20.1.8") &&
                !Tools.Compatible("LLVM", "clang version 21.1.0"),
            "LLVM pin was lost");
      ManifestTest(args[0], sandbox);
      FolderTest(sandbox);
      UpdateTest(sandbox);
      DownloadTest(tools, progress, sandbox);
      LauncherTest(sandbox, executable);
      Console.WriteLine("Standalone installer checks passed.");
      return 0;
    } catch (Exception error) {
      Console.Error.WriteLine(error);
      return 1;
    } finally {
      var resolved = Path.GetFullPath(sandbox);
      if (!String.Equals(Path.GetDirectoryName(resolved), Path.GetTempPath().TrimEnd('\\', '/'),
                         StringComparison.OrdinalIgnoreCase) ||
          !Regex.IsMatch(Path.GetFileName(resolved), "^lollipop-setup-[0-9a-f]{32}$"))
        throw new IOException("Unexpected test directory.");
      Directory.Delete(resolved, true);
    }
  }

  private static int FixtureGame(string[] arguments) {
    Func<string, string> value = key =>
        arguments.First(arg => arg.StartsWith("--" + key + "=")).Substring(key.Length + 3);
    if (value("d3d12_pipeline_creation_threads") != "4" ||
        value("render_target_path_d3d12") != "rov")
      return 8;
    if (arguments.Contains("--lollipop_precompile_only=true")) {
      if (!arguments.Contains("--window_hidden=true"))
        return 9;
      File.WriteAllText(value("lollipop_shader_progress"), "10 20 0 Compiling shaders\n");
      Thread.Sleep(250);
      File.WriteAllText(value("lollipop_shader_progress"),
                        File.Exists(Path.Combine(value("game_data_root"), "fail-shaders"))
                            ? "20 20 1 Failed\n"
                            : "20 20 0 Complete\n");
    } else {
      Directory.CreateDirectory(value("user_data_root"));
      File.WriteAllText(Path.Combine(value("user_data_root"), "played"), value("cache_root"));
    }
    return 0;
  }

  private static void ManifestTest(string root, string sandbox) {
    var template = File.ReadAllText(Path.Combine(root, "lollipop_manifest.toml"));
    var game = Path.Combine(sandbox, "owner's $game [files]");
    var generated = Path.Combine(sandbox, "out/generated code");
    var actual = Installer.Manifest(template, game, generated);
    Check(actual.Contains("game_root = \"" + game.Replace('\\', '/') + "\""),
          "Game path was not preserved");
    Check(actual.Contains("file_path = \"" + game.Replace('\\', '/') + "/default.xex\""),
          "XEX path was not preserved");
    Check(actual.Contains("out_directory_path = \"" + generated.Replace('\\', '/') + "\""),
          "Codegen output was not preserved");
    var pattern = @"(?m)^(game_root|file_path|out_directory_path)\s*=.*\r?\n?";
    Check(Regex.Replace(actual, pattern, "") == Regex.Replace(template, pattern, ""),
          "Manifest hooks changed");
  }

  private static void FolderTest(string sandbox) {
    var existing = Path.Combine(sandbox, "existing game");
    Directory.CreateDirectory(Path.Combine(existing, "user"));
    var save = Path.Combine(existing, "user/save");
    File.WriteAllText(save, "preserve");
    Fails(delegate { Installer.Initialize(existing); },
          "Existing saves were accepted for overwrite");
    Check(File.ReadAllText(save) == "preserve", "Existing save changed");
    var destination = Path.Combine(sandbox, "new folder [1]");
    Installer.Initialize(destination);
    Installer.Initialize(destination);
    Check(!Installer.Ready(destination), "Empty installation exposed Play");
    Directory.CreateDirectory(Path.Combine(destination, "game"));
    File.WriteAllText(Path.Combine(destination, "game/default.xex"), "wrong disc");
    Fails(delegate { Installer.ValidateGame(Path.Combine(destination, "game")); },
          "Wrong disc was accepted");
    var archive = Path.Combine(sandbox, "escape.zip");
    using (var zip = ZipFile.Open(archive, ZipArchiveMode.Create))
        zip.CreateEntry("../escaped.txt");
    using (var zip = ZipFile.OpenRead(archive)) Fails(delegate {
      Tools.Extract(zip, Path.Combine(sandbox, "unpack"));
    }, "Archive escaped its destination");
    Check(!File.Exists(Path.Combine(sandbox, "escaped.txt")),
          "Archive wrote outside its destination");
  }

  private static void UpdateTest(string sandbox) {
    var kit = Path.Combine(sandbox, "update kit [1]");
    Directory.CreateDirectory(Path.Combine(kit, "patches"));
    File.WriteAllText(Path.Combine(kit, "patches/rexglue-fixes.patch"), "first patch");
    File.WriteAllText(Path.Combine(kit, "patches/rexglue-shader-precompile.patch"), "second patch");
    var id = GameUpdate.SdkId(kit, "commit-a");
    Check(id == GameUpdate.SdkId(kit, "commit-a") && id != GameUpdate.SdkId(kit, "commit-b"),
          "SDK cache did not include the pinned commit");
    File.AppendAllText(Path.Combine(kit, "patches/rexglue-fixes.patch"), " changed");
    Check(id != GameUpdate.SdkId(kit, "commit-a"), "New SDK patches reused an old patched checkout");
    var destination = Path.Combine(sandbox, "update player's game");
    Installer.Initialize(destination);
    foreach (var directory in new[] { "game/AshGame", "game/Engine", "user", "cache",
                                       "out/build/win-amd64-release" })
      Directory.CreateDirectory(Path.Combine(destination, directory));
    File.WriteAllText(Path.Combine(destination, "game/default.xex"), "disc");
    File.WriteAllText(Path.Combine(destination, "user/save"), "save");
    File.WriteAllText(Path.Combine(destination, "user/pc-settings.toml"), "preferences");
    File.WriteAllText(Path.Combine(destination, "cache/player-cache"), "cache");
    var active = Path.Combine(destination, "out/build/win-amd64-release");
    File.WriteAllText(Path.Combine(active, "lollipop.exe"), "old game");
    File.WriteAllText(Path.Combine(active, "rexruntime.dll"), "old runtime");
    File.WriteAllText(Path.Combine(active, "UPPER.DLL"), "another runtime");
    File.WriteAllText(Path.Combine(active, "lollipop.toml"), "custom key bindings");
    Check(GameUpdate.CanUpdate(destination) && !GameUpdate.CanUpdate(null) &&
              !GameUpdate.CanUpdate(Path.Combine(sandbox, "existing game")),
          "Update ownership check accepted an unrelated folder");
    using (File.Open(Path.Combine(active, "rexruntime.dll"), FileMode.Open, FileAccess.Read,
                     FileShare.None))
      Fails(delegate { GameUpdate.CheckClosed(destination, "setup.exe"); },
            "A running or locked game was accepted for update");
    using (File.Open(Path.Combine(active, "UPPER.DLL"), FileMode.Open, FileAccess.Read,
                     FileShare.None))
      Fails(delegate { GameUpdate.CheckClosed(destination, "setup.exe"); },
            "A locked runtime with an uppercase extension was ignored");
    var stage = GameUpdate.Stage(destination);
    Directory.CreateDirectory(stage);
    File.WriteAllText(Path.Combine(stage, "lollipop.exe"), "new game");
    Fails(delegate { GameUpdate.Publish(destination, true); }, "Incomplete update replaced the old game");
    Check(File.ReadAllText(Path.Combine(active, "lollipop.exe")) == "old game",
          "Failed update changed the playable build");
    File.WriteAllText(Path.Combine(stage, "rexruntime.dll"), "new runtime");
    File.WriteAllText(Path.Combine(stage, "rexgpu-xenos.dll"), "new renderer");
    File.WriteAllText(Path.Combine(stage, "lollipop.toml"), "stale staged key bindings");
    File.SetAttributes(Path.Combine(stage, "lollipop.toml"), FileAttributes.ReadOnly);
    var state = Path.Combine(destination, "out/launcher");
    File.WriteAllText(Path.Combine(state, "setup.pending"), "Updating");
    GameUpdate.Publish(destination, true);
    Check(Installer.Ready(destination) && !Directory.Exists(stage) &&
              File.ReadAllText(Path.Combine(active, "lollipop.exe")) == "new game" &&
              File.ReadAllText(Path.Combine(active, "lollipop.toml")) == "custom key bindings",
          "Published update lost the game or advanced key bindings");
    var transaction = Guid.NewGuid().ToString("N");
    var previous = Path.Combine(state, transaction);
    File.WriteAllText(Path.Combine(state, "publish.pending"), transaction + "\nready");
    File.WriteAllText(Path.Combine(state, "setup.pending"), "Updating");
    Directory.Move(active, previous);
    GameUpdate.Recover(destination);
    Check(Installer.Ready(destination) &&
              File.ReadAllText(Path.Combine(active, "lollipop.exe")) == "new game",
          "An interruption between directory moves stranded the previous game");
    Directory.CreateDirectory(previous);
    File.WriteAllText(Path.Combine(previous, "lollipop.exe"), "previous game");
    File.WriteAllText(Path.Combine(state, "publish.pending"), transaction + "\nready");
    File.WriteAllText(Path.Combine(state, "setup.pending"), "Updating");
    GameUpdate.Recover(destination);
    Check(Installer.Ready(destination) && !Directory.Exists(previous) &&
              File.ReadAllText(Path.Combine(active, "lollipop.exe")) == "new game",
          "Recovery after publication replaced the tested game");
    foreach (var invalid in new[] { "../escape", "", transaction + "\n", transaction + "\nother" }) {
      File.WriteAllText(Path.Combine(state, "publish.pending"), invalid);
      Fails(delegate { GameUpdate.Recover(destination); }, "Unsafe recovery path was accepted");
    }
    File.Delete(Path.Combine(state, "publish.pending"));
    Check(File.ReadAllText(Path.Combine(destination, "game/default.xex")) == "disc" &&
              File.ReadAllText(Path.Combine(destination, "user/save")) == "save" &&
              File.ReadAllText(Path.Combine(destination, "user/pc-settings.toml")) == "preferences" &&
              File.ReadAllText(Path.Combine(destination, "cache/player-cache")) == "cache",
          "Updating changed disc files, saves, preferences or player caches");
    Directory.CreateDirectory(stage);
    File.WriteAllText(Path.Combine(state, "publish.pending"), transaction + "\nincomplete");
    File.WriteAllText(Path.Combine(state, "setup.pending"), "Installing");
    Directory.Move(active, previous);
    GameUpdate.Recover(destination);
    Check(!Installer.Ready(destination) && File.Exists(Path.Combine(active, "lollipop.exe")),
          "Recovery advertised an untested previous build as playable");
    File.Delete(Path.Combine(state, "setup.pending"));
    var fresh = Path.Combine(sandbox, "interrupted first publication");
    Installer.Initialize(fresh);
    var freshState = Path.Combine(fresh, "out/launcher");
    File.WriteAllText(Path.Combine(freshState, "setup.pending"), "Installing");
    File.WriteAllText(Path.Combine(freshState, "publish.pending"), transaction);
    GameUpdate.Recover(fresh);
    Check(File.Exists(Path.Combine(freshState, "setup.pending")) &&
              !File.Exists(Path.Combine(freshState, "publish.pending")),
          "An interruption before the first publication prevented a retry");
    foreach (var version in new[] { "first game", "repaired game" }) {
      var freshStage = GameUpdate.Stage(fresh);
      Directory.CreateDirectory(freshStage);
      foreach (var file in new[] { "lollipop.exe", "rexruntime.dll", "rexgpu-xenos.dll" })
        File.WriteAllText(Path.Combine(freshStage, file), version);
      File.WriteAllText(Path.Combine(freshState, "setup.pending"), "Installing");
      GameUpdate.Publish(fresh, false);
      Check(!File.Exists(Path.Combine(freshState, "setup.pending")) &&
                File.ReadAllText(Path.Combine(fresh, "out/build/win-amd64-release/lollipop.exe")) == version,
            "A tested first installation or repair was not published as ready");
    }
  }

  private static void DownloadTest(Tools tools, RecordedProgress progress, string sandbox) {
    var archive = Path.Combine(sandbox, "package.zip");
    var bytes = new byte[128 * 1024];
    new Random(42).NextBytes(bytes);
    using (var zip = ZipFile.Open(archive, ZipArchiveMode.Create)) {
      using (var stream = zip.CreateEntry("program.exe").Open())
          stream.Write(bytes, 0, bytes.Length);
    }
    var payload = File.ReadAllBytes(archive);
    var hash = Tools.HashFile(archive);
    var requests = new List<string>();
    var listener = new TcpListener(IPAddress.Loopback, 0);
    listener.Start();
    var url = "http://127.0.0.1:" + ((IPEndPoint)listener.LocalEndpoint).Port;
    Exception serverError = null;
    var server = new Thread(delegate() {
      bool dropped = false;
      try {
        while (true) {
          using (var client = listener.AcceptTcpClient()) using (var stream = client.GetStream()) {
            stream.ReadTimeout = 5000;
            var reader = new StreamReader(stream, Encoding.ASCII, false, 1024, true);
            var request = reader.ReadLine();
            string line, headers = "";
            while (!String.IsNullOrEmpty(line = reader.ReadLine()))
              headers += line + "\n";
            reader.Dispose();
            lock (requests) requests.Add(request + "\n" + headers);
            var range = Regex.Match(headers, @"(?im)^Range:\s*bytes=(\d+)-");
            var offset = range.Success && !request.Contains("ignore.zip")
                             ? Int32.Parse(range.Groups[1].Value)
                             : 0;
            if (request.Contains("invalid.zip") && range.Success) {
              var rejected =
                  Encoding.ASCII.GetBytes("HTTP/1.1 416 Range Not Satisfiable\r\nContent-Length: " +
                                          "0\r\nConnection: close\r\n\r\n");
              stream.Write(rejected, 0, rejected.Length);
              continue;
            }
            var response = "HTTP/1.1 " + (offset > 0 ? "206 Partial Content" : "200 OK") +
                           "\r\nContent-Length: " + (payload.Length - offset) + "\r\n";
            if (offset > 0)
              response += "Content-Range: bytes " + offset + "-" + (payload.Length - 1) + "/" +
                          payload.Length + "\r\n";
            var header = Encoding.ASCII.GetBytes(response + "Connection: close\r\n\r\n");
            stream.Write(header, 0, header.Length);
            if (request.Contains("drop.zip") && !dropped) {
              stream.Write(payload, 0, 4096);
              dropped = true;
            } else {
              for (var position = offset; position < payload.Length; position += 8192) {
                stream.Write(payload, position, Math.Min(8192, payload.Length - position));
                Thread.Sleep(15);
              }
            }
          }
        }
      } catch (SocketException) {
      } catch (InvalidOperationException) {
      } catch (Exception error) {
        serverError = error;
      }
    });
    server.IsBackground = true;
    server.Start();
    try {
      var cache = Path.Combine(sandbox, "download cache [1]");
      var bad = new Package("Bad", url + "/bad.zip", new string('0', 64), "program.exe");
      Fails(delegate { tools.Get(bad, cache); }, "Unverified archive was accepted");
      Check(!Directory.EnumerateFileSystemEntries(cache).Any(), "Unverified download was kept");
      var resume = new Package("Resume", url + "/drop.zip", hash, "program.exe");
      var partial = Path.Combine(cache, "Resume-" + hash.Substring(0, 12) + ".download");
      bool failed = false;
      try {
        tools.Get(resume, cache);
      } catch (IOException) {
        failed = true;
      } catch (WebException) {
        failed = true;
      }
      Check(failed && new FileInfo(partial).Length == 4096,
            "Interrupted download was not retained");
      var program = tools.Get(resume, cache);
      Check(File.ReadAllBytes(program).SequenceEqual(bytes) && !File.Exists(partial),
            "Resumed package was not verified and published");
      Check(requests.Any(request => request.Contains("Range: bytes=4096-")),
            "Download did not resume");
      foreach (var name in new[] { "ignore", "invalid" }) {
        var package = new Package(name, url + "/" + name + ".zip", hash, "program.exe");
        File.WriteAllBytes(Path.Combine(cache, name + "-" + hash.Substring(0, 12) + ".download"),
                           payload.Take(4096).ToArray());
        Check(File.ReadAllBytes(tools.Get(package, cache)).SequenceEqual(bytes),
              "Resume fallback failed: " + name);
      }
      var count = requests.Count;
      Check(tools.Get(resume, cache) == program && count == requests.Count, "Cache was not reused");
      Check(progress.Updates.Any(update => update.Activity == "Downloading Resume" &&
                                           update.Percent > 0 && update.Status.Contains("MiB/s")),
            "Measured download progress was lost");
    } finally {
      listener.Stop();
      Check(server.Join(5000), "Download fixture did not stop");
    }
    if (serverError != null)
      throw serverError;
  }

  private static void LauncherTest(string sandbox, string executable) {
    SynchronizationContext.SetSynchronizationContext(new DispatcherSynchronizationContext());
    var destination = Path.Combine(sandbox, "play folder's [1] & data");
    Installer.Initialize(destination);
    foreach (var folder in new[] { "game/AshGame", "game/Engine", "out/build/win-amd64-release",
                                   "user" })
      Directory.CreateDirectory(Path.Combine(destination, folder));
    File.WriteAllText(Path.Combine(destination, "game/default.xex"), "fixture");
    File.WriteAllText(Path.Combine(destination, "user/save"), "preserve");
    File.Copy(executable, Path.Combine(destination, "out/build/win-amd64-release/lollipop.exe"));
    var pending = Path.Combine(destination, "out/launcher/setup.pending");
    File.WriteAllText(pending, "Installing");
    Check(!Installer.Ready(destination), "Incomplete build exposed Play");
    var resuming = new Launcher(sandbox, Path.Combine(destination, "Play Lollipop Chainsaw.exe"));
    try {
      Check(resuming.Destination == destination && resuming.Screen == "Update",
            "Installed launcher forgot its folder after an interrupted update");
    } finally { resuming.Window.Close(); }
    var launcher = new Launcher(sandbox, executable);
    try {
      Check(!launcher.Find<Button>("Primary").IsEnabled, "Installer accepted missing inputs");
      foreach (var name in new[] { "Cover", "Paper" }) {
        var art = (System.Windows.Media.Imaging.BitmapSource)launcher
                      .Find<System.Windows.Media.ImageBrush>(name)
                      .ImageSource;
        Check(art.PixelWidth >= 512 && art.PixelHeight >= 512, "Embedded artwork did not load");
      }
      launcher.Destination = destination;
      launcher.Home();
      Check(launcher.Screen == "Update" && launcher.Find<Button>("Primary").IsEnabled,
            "Interrupted setup required the ISO again instead of offering repair");
      File.Delete(pending);
      launcher.Home();
      Check(launcher.Screen == "Ready" && !launcher.Find<Button>("Precompile").IsEnabled,
            "Old build cannot play or improperly permits shader preparation");
      var marker =
          Path.Combine(destination, "out/build/win-amd64-release/shader-precompile-v1.ready");
      File.WriteAllText(marker, "");
      launcher.Home();
      Check(launcher.Find<Button>("Precompile").IsEnabled, "Shader preparation was not enabled");
      launcher.Progress("Download", "42 / 100 MiB", 42);
      Check(!launcher.Find<ProgressBar>("Progress").IsIndeterminate &&
                launcher.Find<ProgressBar>("Progress").Value == 42,
            "Measured progress was lost");
      launcher.Progress("Working", "Unknown", -1);
      Check(launcher.Find<ProgressBar>("Progress").IsIndeterminate,
            "Unknown progress invented a percentage");
      var surface = (FrameworkElement)launcher.Window.Content;
      foreach (var size in new[] { new Size(1080, 680), new Size(860, 580) }) {
        foreach (var state in new[] { "Install", "Busy", "Ready", "Update", "Failure" }) {
          launcher.Show(state, "DiscTab", "Install your game",
                        "Choose your Xbox 360 ISO and an install folder.", "Install game",
                        "Choose another folder", null);
          surface.Measure(size);
          surface.Arrange(new Rect(size));
          surface.UpdateLayout();
          foreach (var name in new[] { "Primary", "Secondary", "Precompile", "Update", "Help" }) {
            var control = launcher.Find<Button>(name);
            if (control.Visibility != Visibility.Visible)
              continue;
            var point = control.TranslatePoint(new Point(0, 0), surface);
            Check(point.X >= 0 && point.Y >= 0 && point.X + control.ActualWidth <= size.Width + 1 &&
                      point.Y + control.ActualHeight <= size.Height + 1,
                  "Launcher action clipped: " + state + " " + name);
          }
        }
      }
      launcher.Start(true, true);
      Pump(launcher);
      Check(launcher.Screen == "Ready" && !File.Exists(Path.Combine(destination, "user/played")),
            "Shader preparation started gameplay");
      File.WriteAllText(Path.Combine(destination, "game/fail-shaders"), "");
      launcher.Start(true, true);
      Pump(launcher);
      Check(
          launcher.Screen == "Failure" && launcher.Find<Expander>("ErrorLog").IsExpanded &&
              launcher.Find<TextBox>("LogText").Text.Contains("Shader preparation did not finish"),
          "Shader failure did not expose the full log");
      launcher.Home();
      launcher.Start(true, false);
      Pump(launcher);
      Check(launcher.Screen == "Ready" &&
                File.ReadAllText(Path.Combine(destination, "user/played")) ==
                    Path.Combine(destination, "cache"),
            "Play did not use installation paths");
      Check(File.ReadAllText(Path.Combine(destination, "user/save")) == "preserve",
            "Play changed the existing save");
      launcher.Source = null;
      launcher.Start(false, false, true);
      Pump(launcher);
      Check(launcher.Screen == "Failure" && Installer.Ready(destination) &&
                launcher.Find<TextBox>("LogText").Text.Contains("This disc does not match") &&
                File.ReadAllText(Path.Combine(destination, "user/save")) == "preserve",
            "Update needed the ISO or damaged the installation after failed validation");
      File.WriteAllText(pending, "Installing");
      launcher.Start(false, false, true);
      Pump(launcher);
      Check(!Installer.Ready(destination) && File.Exists(pending),
            "A failed repair advertised an untested installation as playable");
      File.Delete(pending);
      using (var installationLock =
                 File.Open(Path.Combine(destination, "out/launcher/launcher.lock"),
                           FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None)) {
        var installer = new Installer(sandbox, destination, executable, new RecordedProgress());
        Fails(delegate { installer.Run(null, true, false); }, "Installation lock was ignored");
      }
      var workArea = new Size(800, 512);
      launcher.Home();
      launcher.FitWorkArea(workArea);
      launcher.Window.ShowInTaskbar = false;
      launcher.Window.ShowActivated = false;
      launcher.Window.Opacity = 0;
      launcher.Window.Show();
      launcher.Window.UpdateLayout();
      Check(launcher.Window.ActualWidth <= workArea.Width &&
                launcher.Window.ActualHeight <= workArea.Height,
            "Window minimum size overrides the display work area");
      foreach (var name in new[] { "Primary", "Secondary", "Precompile", "Update", "Help" }) {
        var control = launcher.Find<Button>(name);
        var point = control.TranslatePoint(new Point(0, 0), surface);
        Check(control.Visibility == Visibility.Visible && point.X >= 0 && point.Y >= 0 &&
                  point.X + control.ActualWidth <= surface.ActualWidth + 1 &&
                  point.Y + control.ActualHeight <= surface.ActualHeight + 1,
              "Ready action clipped in the real small-window layout: " + name);
      }
    } finally {
      launcher.Window.Close();
    }
  }

  private static void Pump(Launcher launcher) {
    var deadline = DateTime.UtcNow.AddSeconds(15);
    while (launcher.Busy && DateTime.UtcNow < deadline) {
      var frame = new DispatcherFrame();
      Dispatcher.CurrentDispatcher.BeginInvoke(DispatcherPriority.Background,
                                               new Action(delegate { frame.Continue = false; }));
      Dispatcher.PushFrame(frame);
      Thread.Sleep(15);
    }
    Check(!launcher.Busy, "Launcher did not finish the requested action");
  }
}
}
