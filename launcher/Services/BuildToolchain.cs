using Microsoft.Win32;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;

namespace lollipop;

internal sealed class BuildToolchain {
  internal static readonly Package CMakePackage = new(
      "CMake",
      "https://github.com/Kitware/CMake/releases/download/v3.31.8/cmake-3.31.8-windows-x86_64.zip",
      "81aa9964dbabd71fe02e7ec50472fd3ad56138c49944515ece9001efbff8d719",
      "cmake-3.31.8-windows-x86_64/bin/cmake.exe");
  internal static readonly Package NinjaPackage =
      new("Ninja", "https://github.com/ninja-build/ninja/releases/download/v1.12.1/ninja-win.zip",
          "f550fec705b6d6ff58f2db3c374c2277a37691678d6aba463adcbb129108467a", "ninja.exe");
  internal static readonly Package HeadersPackage =
      new("MSVCHeaders",
          "https://download.visualstudio.microsoft.com/download/pr/" +
              "c610cd8c-801b-44b8-a80a-82cc382aeb43/" +
              "852382a9aa73502b7849c1bcadfb603ba7175c4e8b60e6aba03c7de711d4ece5/" +
              "Microsoft.VC.14.44.17.14.CRT.Headers.base.vsix",
          "852382a9aa73502b7849c1bcadfb603ba7175c4e8b60e6aba03c7de711d4ece5",
          "Contents/VC/Tools/MSVC/14.44.35207/include/vector");
  internal static readonly Package LibrariesPackage =
      new("MSVCLibraries",
          "https://download.visualstudio.microsoft.com/download/pr/" +
              "67cf767c-5e71-47c2-a54a-cd5631e28942/" +
              "50d68caa9bba363d6209bf5fc8a572f013c9f370f6e203f36f04e3d84f5083ca/" +
              "Microsoft.VC.14.44.17.14.CRT.x64.OneCore.Desktop.base.vsix",
          "50d68caa9bba363d6209bf5fc8a572f013c9f370f6e203f36f04e3d84f5083ca",
          "Contents/VC/Tools/MSVC/14.44.35207/lib/onecore/x64/msvcrt.lib");
  internal static readonly Package SdkHeadersPackage =
      new("WindowsSDKHeaders",
          "https://api.nuget.org/v3-flatcontainer/microsoft.windows.sdk.cpp/10.0.26100.7705/" +
              "microsoft.windows.sdk.cpp.10.0.26100.7705.nupkg",
          "ff455660bee071a744715a9665666fa29c07681579d3d815959a9eec5a556424",
          "c/Include/10.0.26100.0/um/Windows.h");
  internal static readonly Package SdkLibrariesPackage = new(
      "WindowsSDKLibraries",
      "https://api.nuget.org/v3-flatcontainer/microsoft.windows.sdk.cpp.x64/10.0.26100.7705/" +
          "microsoft.windows.sdk.cpp.x64.10.0.26100.7705.nupkg",
      "ad6ce90ff94010699d29548b3c2b1e52c89eb81143e2199155d9a5c38001b52b", "c/um/x64/kernel32.Lib");
  internal static readonly Package RuntimePackage =
      new("CppRuntime",
          "https://download.visualstudio.microsoft.com/download/pr/" +
              "45d3b8dd-bced-4b37-9974-142f748d710c/" +
              "4aaf54db0bfc9435f7c3660e1a00237a4b556042bfeea64bde44c2e0194e6ee5/" +
              "Microsoft.VC.14.44.17.14.CRT.Redist.X64.base.vsix",
          "4aaf54db0bfc9435f7c3660e1a00237a4b556042bfeea64bde44c2e0194e6ee5",
          "Contents/VC/Redist/MSVC/14.44.35112/x64/Microsoft.VC143.CRT/msvcp140.dll");
  private const string SdkVersion = "10.0.26100.0";
  private static readonly string[] RuntimeNames = [
    "msvcp140.dll", "msvcp140_1.dll", "msvcp140_2.dll", "msvcp140_atomic_wait.dll",
    "msvcp140_codecvt_ids.dll", "vcruntime140.dll", "vcruntime140_1.dll",
    "vcruntime140_threads.dll", "concrt140.dll"
  ];
  private static readonly string[] RequiredRuntimeNames =
      ["msvcp140.dll", "msvcp140_atomic_wait.dll", "vcruntime140.dll", "vcruntime140_1.dll"];
  internal string Git = "", Compiler = "", CMake = "", Ninja = "", RuntimeDirectory = "";
  internal readonly List<string> ConfigureArguments = [];

  private static string Parent(string file, int count = 1) {
    for (var i = 0; i < count; ++i)
      file = Path.GetDirectoryName(file)!;
    return file;
  }

  internal static bool RuntimeAvailable(string directory) => RequiredRuntimeNames.All(name => {
    var file = Path.Combine(directory, name);
    if (!File.Exists(file))
      return false;
    var version = FileVersionInfo.GetVersionInfo(file);
    return new Version(version.FileMajorPart, version.FileMinorPart, version.FileBuildPart) >=
           new Version(14, 44, 35211);
  });

  internal static string PrepareRuntime(Tools tools, string cache, bool useInstalled = true) {
    if (useInstalled && RuntimeAvailable(Environment.SystemDirectory)) {
      tools.Report("C++ runtime", "Using installed runtime files without changing the system", 100);
      return Environment.SystemDirectory;
    }
    return Parent(tools.Get(RuntimePackage, cache, false));
  }

  internal static void CopyRuntime(string source, string destination) {
    if (!RequiredRuntimeNames.All(name => File.Exists(Path.Combine(source, name))))
      throw new IOException(
          "The C++ runtime files are incomplete. Retry to download the missing runtime package.");
    Directory.CreateDirectory(destination);
    foreach (var name in RuntimeNames) {
      var file = Path.Combine(source, name);
      if (File.Exists(file))
        File.Copy(file, Path.Combine(destination, name), true);
    }
  }

  private static string[] VisualStudios(Tools tools, string directory) {
    var finder = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86),
                              "Microsoft Visual Studio/Installer/vswhere.exe");
    if (!File.Exists(finder))
      return [];
    var paths = tools.Capture(finder, directory, "-all", "-products", "*", "-requires",
                              "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property",
                              "installationPath");
    return paths.Split(['\r', '\n'], StringSplitOptions.RemoveEmptyEntries)
        .Where(Directory.Exists)
        .ToArray();
  }

  internal static BuildToolchain Prepare(Tools tools, string cache, string directory,
                                         string runtime, bool useInstalled = true) {
    var result = new BuildToolchain { RuntimeDirectory = runtime };
    var inheritedPath = useInstalled
                            ? Environment.GetEnvironmentVariable("PATH") ?? ""
                            : Environment.SystemDirectory + ";" +
                                  Environment.GetFolderPath(Environment.SpecialFolder.Windows);
    var visualStudios = useInstalled ? VisualStudios(tools, directory) : [];
    var cmakeDirectories = visualStudios.SelectMany(
        vs => new[] { Path.Combine(vs, "Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin"),
                      Path.Combine(vs, "Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja") });
    tools.EnvironmentVariables["PATH"] =
        runtime + ";" + String.Join(';', cmakeDirectories) + ";" + inheritedPath;
    result.Git = tools.Get(Tools.Git, cache, useInstalled);
    result.Compiler = tools.Get(Tools.LLVM, cache, useInstalled);
    result.CMake = tools.Get(CMakePackage, cache, useInstalled);
    result.Ninja = tools.Get(NinjaPackage, cache, useInstalled);

    var vc = visualStudios.Select(vs => Path.Combine(vs, "VC/Tools/MSVC"))
                 .Where(Directory.Exists)
                 .SelectMany(Directory.GetDirectories)
                 .Where(path => Path.GetFileName(path).StartsWith("14.44.") &&
                                File.Exists(Path.Combine(path, "include/vector")) &&
                                File.Exists(Path.Combine(path, "lib/x64/msvcrt.lib")))
                 .OrderByDescending(path => path, StringComparer.OrdinalIgnoreCase)
                 .FirstOrDefault();
    var vcInclude =
        vc == null ? Parent(tools.Get(HeadersPackage, cache, false)) : Path.Combine(vc, "include");
    var vcLibrary = vc == null ? Parent(tools.Get(LibrariesPackage, cache, false))
                               : Path.Combine(vc, "lib/x64");
    string? sdk = null;
    if (useInstalled) {
      using var roots =
          Registry.LocalMachine.OpenSubKey(@"SOFTWARE\Microsoft\Windows Kits\Installed Roots");
      var candidate = roots?.GetValue("KitsRoot10") as string;
      if (candidate != null &&
          File.Exists(Path.Combine(candidate, "Include", SdkVersion, "um/Windows.h")) &&
          File.Exists(Path.Combine(candidate, "Include", SdkVersion, "ucrt/stdlib.h")) &&
          File.Exists(Path.Combine(candidate, "Lib", SdkVersion, "um/x64/kernel32.lib")) &&
          File.Exists(Path.Combine(candidate, "Lib", SdkVersion, "ucrt/x64/ucrt.lib")) &&
          File.Exists(Path.Combine(candidate, "bin", SdkVersion, "x64/rc.exe")) &&
          File.Exists(Path.Combine(candidate, "bin", SdkVersion, "x64/mt.exe")))
        sdk = candidate;
    }
    var sdkHeaders = sdk ?? Parent(tools.Get(SdkHeadersPackage, cache, false), 4);
    var sdkLibraries = sdk == null ? Parent(tools.Get(SdkLibrariesPackage, cache, false), 3)
                                   : Path.Combine(sdk, "Lib", SdkVersion);
    var includes =
        new[] { Path.Combine(Parent(result.Compiler, 2), "lib/clang/20/include"), vcInclude }
            .Concat(new[] { "ucrt", "shared", "um", "winrt", "cppwinrt" }.Select(
                name => Path.Combine(sdkHeaders, "Include", SdkVersion, name)))
            .ToArray();
    var libraries = new[] { vcLibrary, Path.Combine(sdkLibraries, "um/x64"),
                            Path.Combine(sdkLibraries, "ucrt/x64") };
    tools.EnvironmentVariables["INCLUDE"] = String.Join(';', includes);
    tools.EnvironmentVariables["LIB"] = String.Join(';', libraries);
    tools.EnvironmentVariables["LIBPATH"] = "";
    tools.EnvironmentVariables["VCToolsInstallDir"] = Parent(vcInclude);
    tools.EnvironmentVariables["VCINSTALLDIR"] = "";
    tools.EnvironmentVariables["WindowsSdkDir"] = sdkHeaders;
    tools.EnvironmentVariables["WindowsSDKVersion"] = SdkVersion;
    foreach (var variable in new[] { "CL", "_CL_", "LINK", "_LINK_", "CFLAGS", "CXXFLAGS",
                                     "LDFLAGS", "RCFLAGS" })
      tools.EnvironmentVariables[variable] = "";
    tools.EnvironmentVariables["PATH"] = String.Join(
        ';', new[] { runtime, Parent(result.Compiler), Parent(result.Git), Parent(result.CMake),
                     Parent(result.Ninja), Path.Combine(sdkHeaders, "bin", SdkVersion, "x64"),
                     inheritedPath });
    var flags = "-nostdinc -fms-compatibility-version=19.44 -fms-runtime-lib=dll " +
                String.Join(' ', includes.Select(path => "-isystem " + Tools.Quote(path)));
    var linkFlags = "-nostdlib -fuse-ld=lld " +
                    String.Join(' ', libraries.Select(path => "-Wl,/libpath:" + Tools.Quote(path)));
    result.ConfigureArguments.AddRange([
      "-DCMAKE_C_FLAGS=" + flags, "-DCMAKE_CXX_FLAGS=" + flags,
      "-DCMAKE_EXE_LINKER_FLAGS=" + linkFlags, "-DCMAKE_SHARED_LINKER_FLAGS=" + linkFlags,
      "-DCMAKE_MODULE_LINKER_FLAGS=" + linkFlags,
      "-DCMAKE_LINKER=" + Path.Combine(Parent(result.Compiler), "lld-link.exe"),
      "-DCMAKE_RC_COMPILER=" + Path.Combine(sdkHeaders, "bin", SdkVersion, "x64/rc.exe"),
      "-DCMAKE_MT=" + Path.Combine(sdkHeaders, "bin", SdkVersion, "x64/mt.exe")
    ]);
    for (var i = 0; i < result.ConfigureArguments.Count; ++i)
      result.ConfigureArguments[i] = result.ConfigureArguments[i].Replace('\\', '/');
    tools.Report("Build tools ready",
                 "Using existing tools and private packages; no system installation", 100);
    return result;
  }
}
