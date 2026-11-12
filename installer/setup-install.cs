using Microsoft.Win32;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;

namespace lollipop {
internal sealed class Installer {
  private const string GameHash =
      "a687f47d537b011600146c665e59f5d2b4d4243284799d84fee7da45cdd2df26";
  private const string SdkCommit = "bd833a2ab69d305f99ddf57418fc38ffec8af93d";
  private readonly string kit, destination, executable;
  private readonly IProgress<InstallProgress> progress;
  private Tools tools;
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

  internal static bool Ready(string folder) {
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
    foreach (var name in new[] { "out", "game", "user", "cache", "Play Lollipop Chainsaw.exe" }) {
      var path = Path.Combine(folder, name);
      if (File.Exists(path) || Directory.Exists(path))
        throw new IOException("This folder already contains game or setup files. Choose a new " +
                              "folder for this installation.");
    }
    Directory.CreateDirectory(Path.GetDirectoryName(marker));
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

  internal void Run(string source, bool play, bool shaders, bool update = false) {
    var hadPlayableBuild = Ready(destination);
    if (update) {
      if (play || shaders || !GameUpdate.CanUpdate(destination))
        throw new IOException("Choose an installation created by this setup to update or repair it.");
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
          Step(shaders ? "Preparing shaders" : "Game is running",
               shaders ? "Compiling the recorded shader inventory for your graphics settings."
                       : "Close the game window to return to the launcher.",
               1);
          Play(shaders);
          return;
        }
        var pending = Path.Combine(state, "setup.pending");
        File.WriteAllText(pending, update ? "Updating" : "Installing");
        Step("Preparing Windows runtime", "Checking the Windows runtime required by the game.", 6);
        InstallRuntime();
        Step(Directory.Exists(Game) ? "Checking imported disc" : "Reading your disc",
             "Extracting and verifying your disc files.", 6);
        Import(source);
        Step("Preparing Windows tools",
             "Checking the C++ compiler, CMake and Windows SDK. Windows may ask for permission.",
             6);
        var vs = InstallWindowsTools();
        Step("Downloading build tools",
             "Reusing compatible Git and LLVM tools and downloading missing packages.", 6);
        var git = tools.Get(Tools.Git, Packages);
        var clangxx = tools.Get(Tools.LLVM, Packages);
        InitializeEnvironment(vs, git, clangxx);
        Step("Preparing game tools",
             "Downloading and compiling the game tools. This can take several minutes.", 6);
        var cmakeBin = Path.Combine(vs, "Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin");
        var cmake = Path.Combine(cmakeBin, "cmake.exe");
        var ninja =
            Path.Combine(vs, "Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe");
        foreach (var tool in new[] { cmake, Path.Combine(cmakeBin, "ctest.exe"), ninja })
          if (!File.Exists(tool))
            throw new IOException("Missing Visual Studio CMake/Ninja tool: " + tool);
        BuildSdk(git, cmake, ninja, clangxx);
        Step("Building your game", "Compiling your Windows game and running its final checks.", 6);
        BuildGame(cmake, ninja, clangxx, Path.Combine(cmakeBin, "ctest.exe"));
        if (!File.Exists(Path.Combine(GameUpdate.Stage(destination), "lollipop.exe")))
          throw new IOException("The build did not produce a playable game.");
        var launcher = Path.Combine(destination, "Play Lollipop Chainsaw.exe");
        if (!String.Equals(Path.GetFullPath(executable), Path.GetFullPath(launcher),
                           StringComparison.OrdinalIgnoreCase))
          File.Copy(executable, launcher, true);
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

  private void Import(string source) {
    if (Directory.Exists(Game)) {
      ValidateGame(Game);
      return;
    }
    var extractor = tools.Get(Tools.Disc, Packages);
    var stage = Path.Combine(destination, "import-" + Guid.NewGuid().ToString("N"));
    // Keep failed imports for diagnosis; never replace an existing game directory.
    tools.Run(extractor, destination, new[] { "-x", "-s", "-d", stage, source }, true);
    ValidateGame(stage);
    Directory.Move(stage, Game);
  }

  private void InstallPackage(Package package, string options) {
    var program = tools.Get(package, Packages);
    if (Tools.HashFile(program) != package.Hash)
      throw new InvalidDataException("The Windows installer failed its integrity check.");
    tools.Report("Installing " + package.Name + " components", "Waiting for the Windows installer",
                 -1);
    using (var process = Process.Start(new ProcessStartInfo(program, options) {
      UseShellExecute = true, Verb = "runas", WindowStyle = ProcessWindowStyle.Hidden
    })) {
      process.WaitForExit();
      if (process.ExitCode == 3010 || process.ExitCode == 1641)
        throw new IOException("Windows needs a restart. Restart your PC and select the same ISO " +
                              "and folder to continue.");
      if (process.ExitCode != 0)
        throw new IOException("Windows setup did not finish (code " + process.ExitCode +
                              "). Retry and allow the Windows approval prompt.");
    }
  }

  private void InstallRuntime() {
    using (var runtime = Registry.LocalMachine.OpenSubKey(
               @"SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64")) {
      Version version;
      if (runtime != null && Convert.ToInt32(runtime.GetValue("Installed", 0)) != 0 &&
          Version.TryParse(Convert.ToString(runtime.GetValue("Version", "")).TrimStart('v'),
                           out version) &&
          version >= new Version(14, 44, 35211, 0)) {
        tools.Report("Windows runtime", "Already installed", 100);
        return;
      }
    }
    InstallPackage(Tools.Runtime, "/install /quiet /norestart");
  }

  private string FindWindowsTools() {
    var finder = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86),
                              "Microsoft Visual Studio/Installer/vswhere.exe");
    if (!File.Exists(finder))
      return null;
    var vs = tools.Capture(finder, destination, "-latest", "-products", "*", "-requires",
                           "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
                           "Microsoft.VisualStudio.Component.VC.CMake.Project", "-property",
                           "installationPath");
    using (var roots = Registry.LocalMachine.OpenSubKey(
               @"SOFTWARE\Microsoft\Windows Kits\Installed Roots")) {
      var kits = roots == null ? "" : Convert.ToString(roots.GetValue("KitsRoot10", ""));
      var lib = Path.Combine(kits, "Lib");
      if (String.IsNullOrWhiteSpace(vs) ||
          !File.Exists(
              Path.Combine(vs, "Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe")) ||
          !Directory.Exists(lib) ||
          !Directory.GetDirectories(lib).Any(
              dir => File.Exists(Path.Combine(dir, "um/x64/kernel32.lib"))))
        return null;
    }
    return vs;
  }

  private string InstallWindowsTools() {
    var vs = FindWindowsTools();
    if (vs != null)
      return vs;
    var options =
        "--quiet --wait --norestart --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 " +
        "--add Microsoft.VisualStudio.Component.VC.CMake.Project --add " +
        "Microsoft.VisualStudio.Component.Windows11SDK.26100";
    var finder = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86),
                              "Microsoft Visual Studio/Installer/vswhere.exe");
    if (File.Exists(finder)) {
      var installed = tools.Capture(finder, destination, "-latest", "-version", "[17,18)",
                                    "-products", "Microsoft.VisualStudio.Product.BuildTools",
                                    "-property", "installationPath");
      if (!String.IsNullOrWhiteSpace(installed))
        options = "modify --installPath " + Tools.Quote(installed) + " " + options;
    }
    InstallPackage(Tools.Windows, options);
    vs = FindWindowsTools();
    if (vs == null)
      throw new IOException(
          "Windows components are not ready yet. Restart your PC, then retry setup.");
    return vs;
  }

  private void InitializeEnvironment(string vs, string git, string clangxx) {
    var vcvars = Path.Combine(vs, "VC/Auxiliary/Build/vcvars64.bat");
    var lines = new List<string>();
    tools.RunStart(new ProcessStartInfo(Path.Combine(Environment.SystemDirectory, "cmd.exe"),
                                        "/d /v:off /s /c \"\"" + vcvars + "\" >nul && set\""),
                   destination, true, delegate(string line) { lock (lines) lines.Add(line); });
    foreach (var line in lines) {
      var separator = line.IndexOf('=');
      if (separator > 0)
        tools.EnvironmentVariables[line.Substring(0, separator)] = line.Substring(separator + 1);
    }
    string path;
    if (!tools.EnvironmentVariables.TryGetValue("PATH", out path))
      throw new IOException("Failed to initialize the Visual C++ environment.");
    tools.EnvironmentVariables["PATH"] =
        Path.GetDirectoryName(git) + ";" + Path.GetDirectoryName(clangxx) + ";" + path;
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
    var build = Path.Combine(destination, "out/build/sdk-" + id);
    tools.Run(
        cmake, destination,
        new[] { "-S", source, "-B", build, "-G", "Ninja", "-DCMAKE_MAKE_PROGRAM=" + ninja,
                "-DCMAKE_C_COMPILER=" + Path.Combine(Path.GetDirectoryName(clangxx), "clang.exe"),
                "-DCMAKE_CXX_COMPILER=" + clangxx,
                "-DCMAKE_INSTALL_PREFIX=" + Path.Combine(destination, "out/sdk"),
                "-DCMAKE_BUILD_TYPE=Release", "-DREXGLUE_ENABLE_TRACY=OFF",
                "-DREXGLUE_USE_VULKAN=OFF", "-DREXGLUE_BUILD_TESTS=OFF" },
        true);
    tools.Run(cmake, destination,
              new[] { "--build", build, "--target", "install", "--parallel", "6" }, true);
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

  private void BuildGame(string cmake, string ninja, string clangxx, string ctest) {
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
              true);
    var configure = new List<string>();
    var cache = Path.Combine(build, "CMakeCache.txt");
    if (File.Exists(cache)) {
      var previous =
          Regex.Match(File.ReadAllText(cache), @"(?m)^CMAKE_HOME_DIRECTORY:INTERNAL=(.+)\r?$");
      if (previous.Success &&
          !String.Equals(Path.GetFullPath(previous.Groups[1].Value.Trim()), Path.GetFullPath(kit),
                         StringComparison.OrdinalIgnoreCase))
        configure.Add("--fresh");
    }
    configure.AddRange(
        new[] { "-S", kit, "-B", build, "-G", "Ninja", "-DCMAKE_MAKE_PROGRAM=" + ninja,
                "-DCMAKE_CXX_COMPILER=" + clangxx, "-DCMAKE_PREFIX_PATH=" + sdk,
                "-Drexglue_DIR=" + Path.Combine(sdk, "lib/cmake/rexglue"),
                "-DLOLLIPOP_GAME_DIR=" + Game, "-DLOLLIPOP_GENERATED_DIR=" + generated,
                "-DLOLLIPOP_MANIFEST=" + manifest, "-DLOLLIPOP_SCRIPT_TESTS=OFF",
                "-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=" + GameUpdate.Stage(destination),
                "-DCMAKE_BUILD_TYPE=Release" });
    tools.Run(cmake, destination, configure, true);
    tools.Run(cmake, destination, new[] { "--build", build, "--parallel", "6" }, true);
    tools.Run(ctest, destination, new[] { "--test-dir", build, "--output-on-failure" }, true);
  }

  private void Play(bool shaders) {
    var cache = Path.Combine(destination, "cache");
    var arguments =
        new List<string> { "--game_data_root=" + Game,
                           "--user_data_root=" + Path.Combine(destination, "user"),
                           "--cache_root=" + cache, "--d3d12_pipeline_creation_threads=4",
                           "--render_target_path_d3d12=rov" };
    var report = Path.Combine(cache, "shader-precompile-progress.txt");
    Action poll = null;
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
          status = File.ReadAllText(report);
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
}
