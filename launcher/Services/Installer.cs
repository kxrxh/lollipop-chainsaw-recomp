using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;

namespace lollipop;
internal sealed class Installer {
  private const string GameHash =
      "a687f47d537b011600146c665e59f5d2b4d4243284799d84fee7da45cdd2df26";
  private const string SdkCommit = "bd833a2ab69d305f99ddf57418fc38ffec8af93d";
  private readonly string kit, destination, executable;
  private readonly IProgress<InstallProgress> progress;
  private Tools tools = null!;
  private BuildToolchain toolchain = null!;
  private int step;
  private string Game {
    get { return Path.Combine(destination, "game"); }
  }
  private string Build {
    get { return Path.Combine(destination, "out/build/win-amd64-release"); }
  }
  private string Packages {
    get { return Path.Combine(destination, "out/tools/packages"); }
  }

  internal Installer(string kit, string destination, string executable,
                     IProgress<InstallProgress> progress) {
    this.kit = kit;
    this.destination = Path.GetFullPath(destination);
    this.executable = executable;
    this.progress = progress;
  }

  internal static bool Ready(string? folder) {
    return !String.IsNullOrEmpty(folder) &&
           File.Exists(Path.Combine(folder, "out/build/win-amd64-release/lollipop.exe")) &&
           File.Exists(Path.Combine(folder, "game/default.xex")) &&
           Directory.Exists(Path.Combine(folder, "game/AshGame")) &&
           Directory.Exists(Path.Combine(folder, "game/Engine")) &&
           !File.Exists(Path.Combine(folder, "out/launcher/setup.pending"));
  }

  internal static void Initialize(string folder) {
    var marker = Path.Combine(folder, "out/launcher/install.id");
    if (File.Exists(marker) && File.ReadAllText(marker) == GameUpdate.Identity)
      return;
    foreach (var name in new[] { "out", "game", "user", "cache", LauncherInstallation.FileName,
                                 LauncherInstallation.LegacyFileName }) {
      var path = Path.Combine(folder, name);
      if (File.Exists(path) || Directory.Exists(path))
        throw new IOException("This folder already contains game or setup files. Choose a new " +
                              "folder for this installation.");
    }
    Directory.CreateDirectory(Path.GetDirectoryName(marker)!);
    File.WriteAllText(marker, GameUpdate.Identity);
  }

  internal static void ValidateGame(string folder) {
    var xex = Path.Combine(folder, "default.xex");
    if (!File.Exists(xex) || Tools.HashFile(xex) != GameHash)
      throw new InvalidDataException("This disc does not match the supported Xbox 360 edition " +
                                     "(media ID 31BE13D9, version 0.0.0.2).");
    foreach (var name in new[] { "AshGame", "Engine" }) {
      if (!Directory.Exists(Path.Combine(folder, name)))
        throw new InvalidDataException("Missing game directory: " + name +
                                       ". Check that your disc was fully extracted.");
    }
  }

  private void Step(string title, string description, int total) {
    step++;
    progress.Report(
        new InstallProgress { Activity = title, Status = description, Step = step, Total = total });
  }

  internal void Run(string? source, bool play, bool shaders, bool update = false) {
    var hadPlayableBuild = Ready(destination);
    if (update) {
      if (play || shaders || !GameUpdate.CanUpdate(destination))
        throw new IOException(
            "Choose an installation created by this setup to update or repair it.");
    } else if (!play) {
      if (!File.Exists(source) ||
          !String.Equals(Path.GetExtension(source), ".iso", StringComparison.OrdinalIgnoreCase))
        throw new IOException("Choose an Xbox 360 ISO file to install.");
      Initialize(destination);
    } else if (!Ready(destination))
      throw new IOException("This installation is incomplete. Run setup first.");
    var state = Path.Combine(destination, "out/launcher");
    Directory.CreateDirectory(state);
    FileStream installationLock;
    try {
      installationLock = File.Open(Path.Combine(state, "launcher.lock"), FileMode.OpenOrCreate,
                                   FileAccess.ReadWrite, FileShare.None);
    } catch (IOException) {
      throw new IOException("This installation is already in use or the folder is not writable. " +
                            "Close the other launcher or choose another folder.");
    }
    using (installationLock) using (var log = new StreamWriter(Path.Combine(state, "latest.log"),
                                                               false, new UTF8Encoding(false))) {
      log.AutoFlush = true;
      tools = new Tools(progress, delegate(string line) { lock (log) log.WriteLine(line); });
      try {
        log.WriteLine("Lollipop Chainsaw Recomp — standalone setup");
        if (update) {
          GameUpdate.CheckClosed(destination, executable);
          GameUpdate.Recover(destination);
          hadPlayableBuild = Ready(destination);
          ValidateGame(Game);
        }
        if (play) {
          if (!BuildToolchain.RuntimeAvailable(Build))
            BuildToolchain.CopyRuntime(BuildToolchain.PrepareRuntime(tools, Packages), Build);
          Step(shaders ? "Preparing shaders" : "Game is running",
               shaders ? "Compiling the recorded shader inventory for your graphics settings."
                       : "Close the game window to return to the launcher.",
               1);
          Play(shaders);
          return;
        }
        var pending = Path.Combine(state, "setup.pending");
        File.WriteAllText(pending, update ? "Updating" : "Installing");
        Step("Preparing runtime",
             "Checking existing C++ runtime files; missing files stay in this game folder.", 5);
        var runtime = BuildToolchain.PrepareRuntime(tools, Packages);
        tools.EnvironmentVariables["PATH"] =
            runtime + ";" + Environment.GetEnvironmentVariable("PATH");
        Step(Directory.Exists(Game) ? "Checking imported disc" : "Reading your disc",
             "Extracting and verifying your disc files.", 5);
        Import(source, runtime);
        Step("Preparing build tools",
             "Reusing compatible installed tools. Missing tools are downloaded into this game " +
                 "folder.",
             5);
        toolchain = BuildToolchain.Prepare(tools, Packages, destination, runtime);
        Step("Preparing game tools",
             "Downloading and compiling the game tools. This can take several minutes.", 5);
        BuildSdk(toolchain.Git, toolchain.CMake, toolchain.Ninja, toolchain.Compiler);
        Step("Building your game", "Compiling your Windows game and preparing its runtime files.",
             5);
        BuildGame(toolchain.CMake, toolchain.Ninja, toolchain.Compiler);
        BuildToolchain.CopyRuntime(runtime, GameUpdate.Stage(destination));
        if (!File.Exists(Path.Combine(GameUpdate.Stage(destination), "lollipop.exe")))
          throw new IOException("The build did not produce a playable game.");
        LauncherInstallation.Install(executable, destination);
        GameUpdate.Publish(destination, hadPlayableBuild);
        File.Delete(pending);
        tools.Report("Installation complete", "Ready to play", 100);
      } catch (Exception error) {
        log.WriteLine(error.ToString());
        if (update && hadPlayableBuild && File.Exists(Path.Combine(Build, "lollipop.exe")) &&
            !File.Exists(Path.Combine(state, "publish.pending")))
          File.Delete(Path.Combine(state, "setup.pending"));
        throw;
      }
    }
  }

  private void Import(string? source, string runtime) {
    if (Directory.Exists(Game)) {
      ValidateGame(Game);
      return;
    }
    if (String.IsNullOrWhiteSpace(source))
      throw new IOException("Choose an Xbox 360 ISO file to import.");
    var extractor = tools.Get(Tools.Disc, Packages);
    BuildToolchain.CopyRuntime(runtime, Path.GetDirectoryName(extractor)!);
    var stage = Path.Combine(destination, "import-" + Guid.NewGuid().ToString("N"));
    // Keep failed imports for diagnosis; never replace an existing game directory.
    tools.Run(extractor, destination, new[] { "-x", "-s", "-d", stage, source }, true);
    ValidateGame(stage);
    Directory.Move(stage, Game);
  }

  private void BuildSdk(string git, string cmake, string ninja, string clangxx) {
    var id = GameUpdate.SdkId(kit, SdkCommit);
    var source = Path.Combine(destination, "out/tools/rexglue-source-" + id);
    if (!Directory.Exists(source)) {
      var stage = Path.Combine(destination, "out/tools/sdk-clone-" + Guid.NewGuid().ToString("N"));
      tools.Run(git, destination,
                new[] { "clone", "--progress", "--branch", "v0.10.1", "--depth", "1",
                        "https://github.com/rexglue/rexglue-sdk.git", stage },
                true);
      Directory.Move(stage, source);
    }
    if (tools.Capture(git, destination, "-C", source, "rev-parse", "HEAD") != SdkCommit)
      throw new IOException("ReXGlue source must be checked out at v0.10.1 (bd833a2).");
    tools.Run(git, destination,
              new[] { "-C",
                      source,
                      "submodule",
                      "update",
                      "--init",
                      "--progress",
                      "--depth",
                      "1",
                      "--jobs",
                      "8",
                      "thirdparty/cli11",
                      "thirdparty/libmspack",
                      "thirdparty/FFmpeg",
                      "thirdparty/tomlplusplus",
                      "thirdparty/simde",
                      "thirdparty/xxHash",
                      "thirdparty/spdlog",
                      "thirdparty/fmt",
                      "thirdparty/utfcpp",
                      "thirdparty/imgui",
                      "thirdparty/sdl3",
                      "thirdparty/o1heap",
                      "thirdparty/inja" },
              true);
    foreach (var name in new[] { "rexglue-fixes.patch", "rexglue-shader-precompile.patch" }) {
      var patch = Path.Combine(kit, "patches/" + name);
      if (tools.Run(git, destination,
                    new[] { "-C", source, "apply", "--reverse", "--check", patch }, false) == 0)
        continue;
      tools.Run(git, destination, new[] { "-C", source, "apply", "--check", patch }, true);
      tools.Run(git, destination, new[] { "-C", source, "apply", patch }, true);
    }
    var build = Path.Combine(destination, "out/build/sdk-" + id + "-portable");
    var configure = new List<string>();
    if (BuildSourceChanged(build, source))
      configure.Add("--fresh");
    configure.AddRange(
        new[] { "-S", source, "-B", build, "-G", "Ninja", "-DCMAKE_MAKE_PROGRAM=" + ninja,
                "-DCMAKE_C_COMPILER=" + Path.Combine(Path.GetDirectoryName(clangxx)!, "clang.exe"),
                "-DCMAKE_CXX_COMPILER=" + clangxx,
                "-DCMAKE_INSTALL_PREFIX=" + Path.Combine(destination, "out/sdk"),
                "-DCMAKE_BUILD_TYPE=Release", "-DREXGLUE_ENABLE_TRACY=OFF",
                "-DREXGLUE_USE_VULKAN=OFF", "-DREXGLUE_BUILD_TESTS=OFF",
                "-DSDL_TEST_LIBRARY=OFF" });
    configure.AddRange(toolchain.ConfigureArguments);
    tools.Run(cmake, destination, configure, true, activity: "Configuring game tools");
    tools.Run(cmake, destination,
              new[] { "--build", build, "--target", "install", "--parallel", "6" }, true,
              activity: "Building game tools");
    BuildToolchain.CopyRuntime(toolchain.RuntimeDirectory,
                               Path.Combine(destination, "out/sdk/bin"));
  }

  internal static string Manifest(string template, string game, string generated) {
    var paths = new Dictionary<string, string> {
      { "game_root", Path.GetFullPath(game).Replace('\\', '/') },
      { "file_path", Path.GetFullPath(Path.Combine(game, "default.xex")).Replace('\\', '/') },
      { "out_directory_path", Path.GetFullPath(generated).Replace('\\', '/') }
    };
    foreach (var pair in paths)
      template = Regex.Replace(template, "(?m)^" + pair.Key + @"\s*=.*$",
                               match => pair.Key + " = \"" + pair.Value + "\"");
    return template;
  }

  internal static bool BuildSourceChanged(string build, string source) {
    var cache = Path.Combine(build, "CMakeCache.txt");
    if (!File.Exists(cache))
      return false;
    var previous =
        Regex.Match(File.ReadAllText(cache), @"(?m)^CMAKE_HOME_DIRECTORY:INTERNAL=(.+)\r?$");
    return previous.Success &&
           !String.Equals(Path.GetFullPath(previous.Groups[1].Value.Trim()),
                          Path.GetFullPath(source), StringComparison.OrdinalIgnoreCase);
  }

  private void BuildGame(string cmake, string ninja, string clangxx) {
    ValidateGame(Game);
    var build = Path.Combine(destination, "out/build/game-compile");
    Directory.CreateDirectory(build);
    var sdk = Path.Combine(destination, "out/sdk");
    var generated = Path.Combine(destination, "out/generated");
    var manifest = Path.Combine(build, "codegen.toml");
    File.WriteAllText(
        manifest,
        Manifest(File.ReadAllText(Path.Combine(kit, "lollipop_manifest.toml")), Game, generated),
        new UTF8Encoding(false));
    tools.Run(Path.Combine(sdk, "bin/rexglue.exe"), destination, new[] { "codegen", manifest },
              true, activity: "Translating game code");
    var configure = new List<string>();
    if (BuildSourceChanged(build, kit))
      configure.Add("--fresh");
    configure.AddRange(
        new[] { "-S", kit, "-B", build, "-G", "Ninja", "-DCMAKE_MAKE_PROGRAM=" + ninja,
                "-DCMAKE_CXX_COMPILER=" + clangxx, "-DCMAKE_PREFIX_PATH=" + sdk,
                "-Drexglue_DIR=" + Path.Combine(sdk, "lib/cmake/rexglue"),
                "-DLOLLIPOP_GAME_DIR=" + Game, "-DLOLLIPOP_GENERATED_DIR=" + generated,
                "-DLOLLIPOP_MANIFEST=" + manifest, "-DBUILD_TESTING=OFF",
                "-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=" + GameUpdate.Stage(destination),
                "-DCMAKE_BUILD_TYPE=Release" });
    configure.AddRange(toolchain.ConfigureArguments);
    tools.Run(cmake, destination, configure, true, activity: "Configuring the game");
    tools.Run(cmake, destination,
              new[] { "--build", build, "--target", "lollipop", "--parallel", "6" }, true,
              activity: "Building the game");
  }

  private void Play(bool shaders) {
    var cache = Path.Combine(destination, "cache");
    var arguments =
        new List<string> { "--game_data_root=" + Game,
                           "--user_data_root=" + Path.Combine(destination, "user"),
                           "--cache_root=" + cache, "--d3d12_pipeline_creation_threads=4",
                           "--render_target_path_d3d12=rov" };
    var report = Path.Combine(cache, "shader-precompile-progress.txt");
    Action? poll = null;
    if (shaders) {
      if (!File.Exists(Path.Combine(Build, "shader-precompile-v1.ready")))
        throw new IOException("Update or rebuild this installation before preparing shaders.");
      Directory.CreateDirectory(cache);
      File.WriteAllText(report, "");
      arguments.AddRange(new[] { "--window_hidden=true", "--lollipop_precompile_only=true",
                                 "--lollipop_shader_progress=" + report,
                                 "--log_file=" + Path.Combine(cache, "shader-precompile.log") });
      string previous = "";
      poll = delegate {
        string status;
        try {
          using (var reader = new StreamReader(File.Open(report, FileMode.Open, FileAccess.Read,
                                                         FileShare.ReadWrite | FileShare.Delete))) {
            status = reader.ReadToEnd();
          }
        } catch (IOException) {
          return;
        }
        if (status == previous)
          return;
        previous = status;
        var match = Regex.Match(status, @"^(\d+) (\d+) (\d+) ([^\r\n]+)\r?\n$");
        if (!match.Success)
          return;
        var total = Double.Parse(match.Groups[2].Value);
        tools.Report(
            match.Groups[4].Value, match.Groups[1].Value + " / " + match.Groups[2].Value,
            total > 0 ? (int)Math.Min(100, 100 * Double.Parse(match.Groups[1].Value) / total) : -1);
      };
    }
    tools.Run(Path.Combine(Build, "lollipop.exe"), destination, arguments, true, null, poll);
    if (shaders && !Regex.IsMatch(File.ReadAllText(report), @"^\d+ \d+ 0 Complete\r?\n$"))
      throw new IOException(
          "Shader preparation did not finish. Check cache/shader-precompile.log before retrying.");
  }
}
