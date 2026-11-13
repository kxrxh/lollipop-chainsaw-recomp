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
using System.Windows.Documents;
using System.Windows.Input;
using System.Windows.Threading;

namespace lollipop;
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
    if (args.Length > 0 && args[0] == "--git-progress") {
      Console.Error.WriteLine("Resolving deltas: 100% (35/35), done.");
      return 0;
    }
    if (args.Length > 0 && args[0] == "--configure") {
      Console.WriteLine("-- Detecting CXX compiler ABI info");
      Thread.Sleep(150);
      Console.WriteLine("-- Configuring done");
      return 0;
    }
    if (args.Any(arg => arg.StartsWith("--game_data_root=")))
      return FixtureGame(args);
    var app = new App();
    app.InitializeComponent();
    app.ShutdownMode = ShutdownMode.OnExplicitShutdown;
    var sandbox =
        Path.Combine(Path.GetTempPath(), "lollipop-setup-" + Guid.NewGuid().ToString("N"));
    Directory.CreateDirectory(sandbox);
    try {
      App.SetTaskbarIdentity();
      IntPtr appId;
      Marshal.ThrowExceptionForHR(GetCurrentProcessExplicitAppUserModelID(out appId));
      try {
        Check(Marshal.PtrToStringUni(appId) == "LollipopChainsaw.Recomp.Launcher",
              "Launcher lost its taskbar identity");
      } finally {
        Marshal.FreeCoTaskMem(appId);
      }
      var progress = new RecordedProgress();
      var log = new List<string>();
      var tools = new Tools(progress, delegate(string line) { lock (log) log.Add(line); });
      var executable = Environment.ProcessPath!;
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
      tools.Run(executable, sandbox, new[] { "--git-progress" }, true);
      var afterGit = progress.Updates.Count;
      tools.Run(executable, sandbox, new[] { "--configure" }, true,
                activity: "Configuring game tools");
      var configuring = progress.Updates.Skip(afterGit).ToArray();
      Check(configuring.Length >= 3 && configuring[0].Percent == -1 &&
                configuring.All(update => update.Activity == "Configuring game tools" &&
                                          update.Percent == -1) &&
                configuring.Any(update => update.Status == "-- Detecting CXX compiler ABI info"),
            "A completed Git percentage survived into compiler configuration or hid its output");
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
      ReleaseTest(args[0]);
      MarkdownTests.Run();
      GraphicsTest(sandbox, args.Length > 1 ? args[1] : null);
      LauncherInstallationTests.Run(sandbox);
      LauncherTest(sandbox, executable);
      Console.WriteLine("Standalone launcher checks passed.");
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
    Check(id != GameUpdate.SdkId(kit, "commit-a"),
          "New SDK patches reused an old patched checkout");
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
                     FileShare.None)) Fails(delegate {
      GameUpdate.CheckClosed(destination, "setup.exe");
    }, "A running or locked game was accepted for update");
    using (File.Open(Path.Combine(active, "UPPER.DLL"), FileMode.Open, FileAccess.Read,
                     FileShare.None)) Fails(delegate {
      GameUpdate.CheckClosed(destination, "setup.exe");
    }, "A locked runtime with an uppercase extension was ignored");
    var stage = GameUpdate.Stage(destination);
    Directory.CreateDirectory(stage);
    File.WriteAllText(Path.Combine(stage, "lollipop.exe"), "new game");
    Fails(delegate { GameUpdate.Publish(destination, true); },
          "Incomplete update replaced the old game");
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
              Releases.InstalledVersion(destination) == Releases.GameVersion &&
              File.ReadAllText(Path.Combine(active, "lollipop.toml")) == "custom key bindings",
          "Published update lost the game, version or advanced key bindings");
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
    foreach (var invalid in new[] { "../escape", "", transaction + "\n",
                                    transaction + "\nother" }) {
      File.WriteAllText(Path.Combine(state, "publish.pending"), invalid);
      Fails(delegate { GameUpdate.Recover(destination); }, "Unsafe recovery path was accepted");
    }
    File.Delete(Path.Combine(state, "publish.pending"));
    Check(File.ReadAllText(Path.Combine(destination, "game/default.xex")) == "disc" &&
              File.ReadAllText(Path.Combine(destination, "user/save")) == "save" &&
              File.ReadAllText(Path.Combine(destination, "user/pc-settings.toml")) ==
                  "preferences" &&
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
                File.ReadAllText(Path.Combine(fresh, "out/build/win-amd64-release/lollipop.exe")) ==
                    version,
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
    Exception? serverError = null;
    var server = new Thread(delegate() {
      bool dropped = false;
      try {
        while (true) {
          using (var client = listener.AcceptTcpClient()) using (var stream = client.GetStream()) {
            stream.ReadTimeout = 5000;
            var reader = new StreamReader(stream, Encoding.ASCII, false, 1024, true);
            var request =
                reader.ReadLine() ?? throw new IOException("Missing HTTP fixture request");
            string ? line;
            string headers = "";
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

  private static void ReleaseTest(string root) {
    Check(Releases.GameVersion == File.ReadAllText(Path.Combine(root, "VERSION")).Trim() &&
              Releases.LauncherVersion ==
                  File.ReadAllText(Path.Combine(root, "launcher/VERSION")).Trim(),
          "Launcher and bundled recomp versions did not use their independent version files");
    Check(Releases.IsNewer("v0.10.0", "0.9.9") && Releases.IsNewer("v0.1.0", "0.1.0-dev") &&
              !Releases.IsNewer("v0.1.0", "0.1.0") && !Releases.IsNewer("v0.1.0", "0.2.0-dev") &&
              !Releases.IsNewer("v1.0.0", "1.0.0+build"),
          "Stable release comparison lost numeric order or development precedence");
    var release = Releases.Parse("{\"tag_name\":\"v0.2.0\",\"body\":\"Fixes\\nAnd more\"}");
    Check(release.Tag == "v0.2.0" && release.Body == "Fixes\nAnd more",
          "Release notes were not decoded");
    foreach (var invalid in new[] { "null", "{broken", "{}",
                                    "{\"tag_name\":\"v0.2.0\",\"draft\":true}",
                                    "{\"tag_name\":\"v0.2.0\",\"prerelease\":true}",
                                    "{\"tag_name\":\"../../unexpected\"}",
                                    "{\"tag_name\":\"v0.2.0-beta\"}" })
      Fails(delegate { Releases.Parse(invalid); }, "Invalid release was accepted: " + invalid);
  }

  private static void GraphicsTest(string sandbox, string? gameSettingsTest) {
    var destination = Path.Combine(sandbox, "graphics player's [1] game");
    Installer.Initialize(destination);
    foreach (var directory in new[] { "game/AshGame", "game/Engine",
                                      "out/build/win-amd64-release" })
      Directory.CreateDirectory(Path.Combine(destination, directory));
    File.WriteAllText(Path.Combine(destination, "game/default.xex"), "fixture");
    var game = Path.Combine(destination, "out/build/win-amd64-release/lollipop.exe");
    File.WriteAllText(game, "fixture");
    var settings = GameSettings.Load(destination);
    var path = GameSettings.PathFor(destination);
    Check(!File.Exists(path) && settings.Values["window_size"] == "2560x1440" &&
              settings.Values["swap_post_effect"] == "none",
          "Reading settings wrote a profile or changed startup defaults");
    var selected = new Dictionary<string, string>(settings.Values);
    settings.Save(destination, Environment.ProcessPath!, selected);
    Check(File.ReadAllText(path).Contains("window_height = \"1440\"") &&
              !File.ReadAllText(path).Contains("lollipop_music_volume"),
          "New profile wrote native save defaults");
    var original = "# Keep my settings\r\nwindow_width = \"1920\"\r\nwindow_height = \"1200\"\r\n" +
                   "resolution_scale = \"4\"\r\nlollipop_music_volume = \"6\"\r\nmnk_sensitivity " +
                   "= \"22.5\"\r\n" + "lollipop_60fps = \"false\"\r\n# Last comment";
    File.WriteAllText(path, original);
    settings = GameSettings.Load(destination);
    Check(settings.Values["window_size"] == "1920x1200" &&
              settings.Values["resolution_scale"] == "4" && settings.Values["lollipop_fps"] == "30",
          "Custom dimensions or legacy FPS were lost");
    selected = new Dictionary<string, string>(settings.Values);
    selected["window_size"] = "1920x1080";
    selected["fullscreen"] = "false";
    selected["vsync"] = "false";
    selected["present_letterbox"] = "false";
    selected["resolution_scale"] = "3";
    selected["swap_post_effect"] = "fxaa";
    selected["anisotropic_override"] = "4";
    selected["lollipop_fps"] = "90";
    selected["present_dither"] = "true";
    settings.Save(destination, Environment.ProcessPath!, selected);
    var saved = File.ReadAllText(path);
    Check(saved.Contains("lollipop_music_volume = \"6\"\r\nmnk_sensitivity = \"22.5\"\r\n") &&
              saved.Contains("# Last comment\r\n") && !saved.Contains("lollipop_60fps"),
          "Graphics save changed audio, controls, comments or failed FPS migration");
    if (gameSettingsTest != null)
      new Tools(new RecordedProgress(), Console.Error.WriteLine)
          .Run(gameSettingsTest, sandbox, new[] { path, "--launcher-profile" }, true);
    foreach (var invalid in new[] { "vsync = \"maybe\"", "window_width = \"-1\"",
                                    "resolution_scale = \"9\"", "lollipop_fps = \"45\"",
                                    "audio_mute = \"true\"\naudio_mute = \"false\"",
                                    "lollipop_60fps = \"true\"\nlollipop_fps = \"60\"", "[display]",
                                    "unknown = \"value\"" })
      Fails(delegate { new GameSettings(invalid); },
            "Malformed game profile was accepted: " + invalid);
    settings = GameSettings.Load(destination);
    var invalidSelected = new Dictionary<string, string>(selected);
    invalidSelected["window_size"] = "1920x-1";
    Fails(delegate { settings.Save(destination, Environment.ProcessPath!, invalidSelected); },
          "Invalid display size was saved");
    Check(File.ReadAllText(path) == saved, "Invalid save changed the profile");
    using (File.Open(Path.Combine(destination, "out/launcher/launcher.lock"), FileMode.OpenOrCreate,
                     FileAccess.ReadWrite, FileShare.None)) Fails(delegate {
      settings.Save(destination, Environment.ProcessPath!, selected);
    }, "Settings ignored an active installation lock");
    using (File.Open(game, FileMode.Open, FileAccess.Read, FileShare.None)) Fails(delegate {
      settings.Save(destination, Environment.ProcessPath!, selected);
    }, "Settings accepted a running game");
    using (File.Open(path, FileMode.Open, FileAccess.Read, FileShare.Read)) Fails(delegate {
      settings.Save(destination, Environment.ProcessPath!, selected);
    }, "Settings ignored a profile write failure");
    Check(File.ReadAllText(path) == saved &&
              !Directory.GetFiles(Path.GetDirectoryName(path)!, "pc-settings.toml.*.tmp").Any(),
          "Failed write changed the profile or left a partial file");
    File.AppendAllText(path, "# External edit\n");
    Fails(delegate { settings.Save(destination, Environment.ProcessPath!, selected); },
          "Stale settings overwrote an external edit");
    Check(File.ReadAllText(path).EndsWith("# External edit\n"), "External edit was lost");
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
      Check(resuming.Find<CheckBox>("DesktopShortcut").Visibility == Visibility.Visible,
            "Resuming an interrupted installation hid the desktop shortcut option");
    } finally {
      resuming.Window.Close();
    }
    var launcher = new Launcher(sandbox, executable);
    try {
      Check(launcher.CurrentPage == "Home" && launcher.Find<Button>("Primary").IsEnabled,
            "New launcher did not open the game overview");
      launcher.Find<Button>("Primary").RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
      Check(launcher.CurrentPage == "Manage" && !launcher.Find<Button>("Primary").IsEnabled,
            "Install action accepted missing inputs");
      Check(launcher.Find<CheckBox>("DesktopShortcut").IsChecked == true &&
                !launcher.Find<Button>("CreateDesktopShortcut").IsEnabled,
            "Setup lost its desktop shortcut option or accepted an uninstalled game");
      launcher.SetPage("Settings");
      Check(!launcher.Find<StackPanel>("SettingsFields").IsEnabled &&
                !launcher.Find<Button>("SaveSettings").IsEnabled,
            "Settings can be saved without a selected game");
      var brand = (System.Windows.Media.Imaging.BitmapSource)launcher
                      .Find<System.Windows.Controls.Image>("BrandIcon")
                      .Source;
      Check(brand.PixelWidth >= 256 && brand.PixelHeight >= 256,
            "Launcher enlarged a small icon frame instead of using the full-resolution logo");
      var title = (System.Windows.Media.Imaging.BitmapSource)launcher
                      .Find<System.Windows.Controls.Image>("GameTitle")
                      .Source;
      Check(title.PixelWidth >= 512 && title.PixelHeight >= 256,
            "Launcher title logo is missing or too small");
      var titlePixels = new System.Windows.Media.Imaging.FormatConvertedBitmap(
          title, System.Windows.Media.PixelFormats.Bgra32, null, 0);
      var titleCorner = new byte[4];
      foreach (var corner in new[] {
                 new Int32Rect(0, 0, 1, 1), new Int32Rect(title.PixelWidth - 1, 0, 1, 1),
                 new Int32Rect(0, title.PixelHeight - 1, 1, 1),
                 new Int32Rect(title.PixelWidth - 1, title.PixelHeight - 1, 1, 1)
               }) {
        titlePixels.CopyPixels(corner, titleCorner, 4, 0);
        Check(titleCorner[3] == 0, "Title logo has an opaque rectangular background");
      }
      foreach (var name in new[] { "Cover" }) {
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
      Check(launcher.Find<Button>("CreateDesktopShortcut").IsEnabled,
            "Manage game did not offer a desktop shortcut for an installed game");
      launcher.SetPage("Settings");
      Check(launcher.Find<StackPanel>("SettingsFields").IsEnabled &&
                launcher.Find<ScrollViewer>("PageScroll").Visibility == Visibility.Collapsed &&
                launcher.Find<Button>("SaveSettings").IsDefault &&
                !launcher.Find<Button>("Primary").IsDefault,
            "Installed game did not expose its settings page");
      var frameRate = launcher.Settings.Controls["lollipop_fps"];
      frameRate.SelectedItem =
          frameRate.Items.Cast<ComboBoxItem>().First(item => (string)item.Tag == "120");
      Check(launcher.Settings.Dirty && launcher.Find<Button>("SaveSettings").IsEnabled,
            "Changing a setting did not enable Save");
      launcher.SetPage("Home");
      launcher.SetPage("Settings");
      Check((string)((ComboBoxItem)frameRate.SelectedItem).Tag == "120",
            "Navigation discarded the settings draft");
      launcher.Find<Button>("SaveSettings").RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
      Check(!launcher.Settings.Dirty &&
                GameSettings.Load(destination).Values["lollipop_fps"] == "120",
            "Launcher did not save the selected frame rate");
      launcher.Find<Button>("RecommendedSettings")
          .RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
      Check(launcher.Settings.Dirty && (string)((ComboBoxItem)frameRate.SelectedItem).Tag == "60",
            "Recommended settings did not populate a draft");
      launcher.Find<Button>("ReloadSettings").RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
      Check(!launcher.Settings.Dirty && (string)((ComboBoxItem)frameRate.SelectedItem).Tag == "120",
            "Reload did not restore the saved choices");
      Check(launcher.Screen == "Ready" && !launcher.Find<Button>("Precompile").IsEnabled,
            "Old build cannot play or improperly permits shader preparation");
      Check(launcher.Find<TextBlock>("Location").Text == "" &&
                launcher.Find<TextBlock>("FolderName").Text == destination,
            "Game folder path leaked into the footer or disappeared from Manage game");
      Check(launcher.Find<TextBlock>("InstalledBuildVersion").Text ==
                "Installed recomp version: Unknown (older installation)",
            "Old installation was assigned the launcher's version");
      File.WriteAllText(Path.Combine(destination, "out/build/win-amd64-release/recomp-version.txt"),
                        "0.0.9");
      launcher.Home();
      Check(launcher.Find<TextBlock>("RecompVersion").Text == Releases.GameVersion &&
                launcher.Find<TextBlock>("LauncherVersion").Text ==
                    "Launcher version: " + Releases.LauncherVersion &&
                launcher.Find<TextBlock>("InstalledBuildVersion").Text ==
                    "Installed recomp version: 0.0.9",
            "Installed game and launcher versions were conflated");
      launcher.Source = "fixture.iso";
      launcher.Find<Button>("UpdatesNav").RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
      Check(launcher.CurrentPage == "Updates" && launcher.Destination == destination &&
                launcher.Source == "fixture.iso" &&
                launcher.Find<FrameworkElement>("Inputs").Visibility == Visibility.Collapsed,
            "Navigation lost the selected installation or exposed ISO import for a ready game");
      launcher.ShowRelease(null);
      Check(launcher.Find<TextBlock>("UpdateStatus").Text == "No public releases yet.",
            "Missing releases were advertised as up to date");
      launcher.ShowRelease(Releases.Parse("{\"tag_name\":\"v99.0.0\",\"body\":\"Update notes\"}"));
      Check(launcher.Find<TextBlock>("UpdateStatus").Text.Contains("v99.0.0 is available") &&
                new TextRange(launcher.Find<RichTextBox>("ReleaseNotes").Document.ContentStart,
                              launcher.Find<RichTextBox>("ReleaseNotes").Document.ContentEnd)
                        .Text.Trim() == "Update notes",
            "Available release did not show its version and notes");
      launcher.SetPage("Manage");
      var helpOverlay = launcher.Find<Border>("HelpOverlay");
      launcher.Find<Button>("Help").RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
      Check(helpOverlay.Visibility == Visibility.Visible &&
                !launcher.Find<Grid>("LauncherContent").IsEnabled &&
                !launcher.Find<Button>("Primary").IsEnabled &&
                !launcher.Find<Button>("Primary").IsDefault &&
                FocusManager.GetFocusedElement(launcher.Find<Border>("HelpDialog")) ==
                    launcher.Find<Button>("HelpDone") &&
                KeyboardNavigation.GetTabNavigation(launcher.Find<Border>("HelpDialog")) ==
                    KeyboardNavigationMode.Cycle,
            "Help did not contain focus and disable underlying launcher actions");
      var helpSurface = (FrameworkElement)launcher.Window.Content;
      foreach (var size in new[] { new Size(1200, 780), new Size(800, 512) }) {
        helpSurface.Measure(size);
        helpSurface.Arrange(new Rect(size));
        helpSurface.UpdateLayout();
        foreach (var name in new[] { "HelpDialog", "HelpClose", "HelpDone", "HelpScroll" }) {
          var control = launcher.Find<FrameworkElement>(name);
          var point = control.TranslatePoint(new Point(0, 0), helpSurface);
          Check(point.X >= 0 && point.Y >= 0 && point.X + control.ActualWidth <= size.Width + 1 &&
                    point.Y + control.ActualHeight <= size.Height + 1 && control.ActualHeight > 0,
                "Help modal clipped: " + name);
        }
      }
      launcher.Find<Button>("HelpClose").RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
      Check(helpOverlay.Visibility == Visibility.Collapsed &&
                launcher.Find<Button>("Primary").IsEnabled && launcher.CurrentPage == "Manage" &&
                launcher.Source == "fixture.iso" && launcher.Destination == destination,
            "Closing help changed the selected installation or disabled Play");
      launcher.SetHelp(true);
      launcher.Find<Button>("HelpDone").RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
      Check(helpOverlay.Visibility == Visibility.Collapsed &&
                launcher.Find<Button>("Primary").IsDefault,
            "Help confirmation did not restore the launcher default action");
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
      foreach (var size in new[] { new Size(1200, 740), new Size(900, 600), new Size(800, 512) }) {
        foreach (var page in new[] { "Home", "Manage", "Settings", "Updates" }) {
          launcher.CurrentPage = page;
          foreach (var state in new[] { "Install", "Busy", "Ready", "Update", "Failure" }) {
            launcher.Show(state, "Install your game",
                          "Choose your Xbox 360 ISO and an install folder.", "Install game",
                          "Choose another folder", null);
            if (state == "Busy") {
              launcher.SetHelp(true);
              launcher.SetHelp(false);
              Check(!launcher.Find<Button>("HomeNav").IsEnabled &&
                        !launcher.Find<Button>("Primary").IsEnabled &&
                        !launcher.Find<Button>("CreateDesktopShortcut").IsEnabled &&
                        !launcher.Find<CheckBox>("DesktopShortcut").IsEnabled &&
                        !launcher.Find<Button>("SaveSettings").IsEnabled,
                    "Closing help re-enabled actions during an operation");
            }
            surface.Measure(size);
            surface.Arrange(new Rect(size));
            surface.UpdateLayout();
            foreach (var name in new[] { "Primary", "Secondary", "HomeNav", "ManageNav",
                                         "SettingsNav", "UpdatesNav", "Help", "Minimize",
                                         "Maximize", "CloseWindow" }) {
              var control = launcher.Find<Button>(name);
              if (control.Visibility != Visibility.Visible)
                continue;
              var point = control.TranslatePoint(new Point(0, 0), surface);
              Check(point.X >= 0 && point.Y >= 0 &&
                        point.X + control.ActualWidth <= size.Width + 1 &&
                        point.Y + control.ActualHeight <= size.Height + 1,
                    "Launcher action clipped: " + page + " " + state + " " + name);
            }
            if (page == "Settings") {
              if (state == "Ready")
                Check(launcher.Find<ScrollViewer>("SettingsScroll").ActualHeight >= 70,
                      "Small settings page cannot show a complete field");
              foreach (var name in new[] { "SaveSettings", "ReloadSettings",
                                           "RecommendedSettings" }) {
                var control = launcher.Find<Button>(name);
                var point = control.TranslatePoint(new Point(0, 0), surface);
                Check(point.X >= 0 && point.Y >= 0 &&
                          point.X + control.ActualWidth <= size.Width + 1 &&
                          point.Y + control.ActualHeight <= size.Height + 1,
                      "Settings action clipped: " + name);
              }
            }
          }
        }
      }
      launcher.Home();
      Check(launcher.Window.AllowsTransparency &&
                ((System.Windows.Media.SolidColorBrush)launcher.Window.Background).Color.A == 0,
            "Opaque native window leaves square pixels around the rounded frame");
      var cornerImage = new System.Windows.Media.Imaging.RenderTargetBitmap(
          800, 512, 96, 96, System.Windows.Media.PixelFormats.Pbgra32);
      cornerImage.Render(surface);
      var cornerPixel = new byte[4];
      foreach (var corner in new[] { new Int32Rect(0, 0, 1, 1), new Int32Rect(799, 0, 1, 1),
                                     new Int32Rect(0, 511, 1, 1), new Int32Rect(799, 511, 1, 1) }) {
        cornerImage.CopyPixels(corner, cornerPixel, 4, 0);
        Check(cornerPixel[3] == 0, "Launcher painted outside a rounded corner");
      }
      launcher.Window.WindowState = WindowState.Maximized;
      surface.UpdateLayout();
      Check(launcher.Find<Border>("WindowFrame").CornerRadius == new CornerRadius(0),
            "Maximized frame retained rounded screen corners");
      launcher.Window.WindowState = WindowState.Normal;
      var pageScroll = launcher.Find<ScrollViewer>("PageScroll");
      var scrollbar = (System.Windows.Controls.Primitives.ScrollBar)pageScroll.Template.FindName(
          "PART_VerticalScrollBar", pageScroll);
      Check(scrollbar.Style == launcher.Window.FindResource(
                                   typeof(System.Windows.Controls.Primitives.ScrollBar)) &&
                scrollbar.Width == 8,
            "Page scrollbar fell back to the default Windows theme");
      var notes = launcher.Find<RichTextBox>("ReleaseNotes");
      var notesScroll = (ScrollViewer)notes.Template.FindName("PART_ContentHost", notes);
      var notesBar = (System.Windows.Controls.Primitives.ScrollBar)notesScroll.Template.FindName(
          "PART_VerticalScrollBar", notesScroll);
      Check(notesBar.Style == scrollbar.Style && notesBar.Width == 8,
            "Release notes scrollbar fell back to a wide or unthemed scrollbar");
      launcher.Start(true, true);
      Pump(launcher);
      Check(launcher.Screen == "Ready" && !File.Exists(Path.Combine(destination, "user/played")),
            "Shader preparation did not return to Ready: " + launcher.Screen + " / " +
                launcher.Find<TextBlock>("Description").Text + " / " +
                launcher.Find<TextBox>("LogText").Text);
      File.WriteAllText(Path.Combine(destination, "game/fail-shaders"), "");
      launcher.Start(true, true);
      Pump(launcher);
      Check(
          launcher.Screen == "Failure" && launcher.Find<Expander>("ErrorLog").IsExpanded &&
              launcher.Find<TextBox>("LogText").Text.Contains("Shader preparation did not finish"),
          "Shader failure did not expose the full log");
      launcher.Home();
      frameRate.SelectedItem =
          frameRate.Items.Cast<ComboBoxItem>().First(item => (string)item.Tag == "90");
      using (File.Open(Path.Combine(destination, "out/launcher/launcher.lock"),
                       FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None)) {
        launcher.Start(true, false);
        Check(!launcher.Busy && launcher.CurrentPage == "Settings" && launcher.Settings.Dirty &&
                  !File.Exists(Path.Combine(destination, "user/played")),
              "Play continued after an unsaved settings failure");
      }
      launcher.Start(true, false);
      Pump(launcher);
      Check(launcher.Screen == "Ready" && !launcher.Settings.Dirty &&
                GameSettings.Load(destination).Values["lollipop_fps"] == "90" &&
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
      launcher.SetHelp(true);
      var escape = new KeyEventArgs(
          Keyboard.PrimaryDevice, PresentationSource.FromVisual(launcher.Window),
          Environment.TickCount, Key.Escape) { RoutedEvent = Keyboard.PreviewKeyDownEvent };
      launcher.Window.RaiseEvent(escape);
      Check(escape.Handled && helpOverlay.Visibility == Visibility.Collapsed &&
                launcher.Window.IsVisible,
            "Escape did not dismiss help inside the launcher");
      Check(launcher.Window.ActualWidth <= workArea.Width &&
                launcher.Window.ActualHeight <= workArea.Height,
            "Window minimum size overrides the display work area");
      foreach (var name in new[] { "Primary", "Secondary", "HomeNav", "ManageNav", "SettingsNav",
                                   "UpdatesNav", "Help", "Minimize", "Maximize", "CloseWindow" }) {
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
