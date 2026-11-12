using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Net;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;

namespace lollipop {
internal sealed class InstallProgress {
  internal string Activity, Status;
  internal int Percent = -1, Step, Total;
}

internal sealed class Package {
  internal readonly string Name, Url, Hash, File;
  internal Package(string name, string url, string hash, string file) {
    Name = name;
    Url = url;
    Hash = hash;
    File = file;
  }
}

internal sealed class Tools {
  internal static readonly Package Runtime = new Package(
      "Runtime",
      "https://download.visualstudio.microsoft.com/download/pr/" +
      "78d061c5-9ef3-42ab-98ed-3309d1095dc4/" +
      "9C02C9ED120F671B900E6B9471D890E3B2414117A7E53F4AE2E855466FA2B7C9/VC_redist.x64.exe",
      "9c02c9ed120f671b900e6b9471d890e3b2414117a7e53f4ae2e855466fa2b7c9", "VC_redist.x64.exe");
  internal static readonly Package Disc =
      new Package("Disc",
                  "https://github.com/XboxDev/extract-xiso/releases/download/build-202609111233/" +
                  "extract-xiso-Win64_Release.zip",
                  "fc891dfa72f08e3838a2f8b7ec34a8e4d61678da1cc8fee879677f00bf114ce3",
                  "artifacts/extract-xiso.exe");
  internal static readonly Package Git = new Package(
      "Git",
      "https://github.com/git-for-windows/git/releases/download/v2.56.1.windows.1/" +
      "MinGit-2.56.1-64-bit.zip",
      "4a92850b6e917da4fd010669b614b55dccf5b0ec333a534cc4be4700dfefdafd", "cmd/git.exe");
  internal static readonly Package LLVM =
      new Package("LLVM",
                  "https://github.com/llvm/llvm-project/releases/download/llvmorg-20.1.8/" +
                  "clang%2Bllvm-20.1.8-x86_64-pc-windows-msvc.tar.xz",
                  "f229769f11d6a6edc8ada599c0cda964b7dee6ab1a08c6cf9dd7f513e85b107f",
                  "clang+llvm-20.1.8-x86_64-pc-windows-msvc/bin/clang++.exe");
  internal static readonly Package Windows = new Package(
      "Windows",
      "https://download.visualstudio.microsoft.com/download/pr/" +
      "95995f57-8355-4ff5-8d90-a7e1669fb3b3/" +
      "310c13b8238876afb4e823a91112230ee2d2c5a45c431d4062af7aadedff9c4c/vs_BuildTools.exe",
      "310c13b8238876afb4e823a91112230ee2d2c5a45c431d4062af7aadedff9c4c", "vs_BuildTools.exe");

  private readonly IProgress<InstallProgress> progress;
  private readonly Action<string> log;
  internal readonly Dictionary<string, string> EnvironmentVariables =
      new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
  internal Tools(IProgress<InstallProgress> progress, Action<string> log) {
    this.progress = progress;
    this.log = log;
  }

  internal void Report(string activity, string status, int percent) {
    progress.Report(
        new InstallProgress { Activity = activity, Status = status, Percent = percent });
  }

  internal static string Quote(string argument) {
    return "\"" +
           Regex.Replace(Regex.Replace(argument, @"(\\*)""", "$1$1\\\""), @"(\\+)$", "$1$1") + "\"";
  }

  internal static string Hash(Stream stream) {
    using (var hash = SHA256.Create()) return BitConverter.ToString(hash.ComputeHash(stream))
        .Replace("-", "")
        .ToLowerInvariant();
  }
  internal static string HashFile(string file) {
    using (var stream = File.OpenRead(file)) return Hash(stream);
  }
  internal static string HashText(string text) {
    using (var stream = new MemoryStream(Encoding.UTF8.GetBytes(text))) return Hash(stream);
  }

  internal static void DeleteStage(string stage, string parent) {
    if (!String.Equals(Path.GetDirectoryName(Path.GetFullPath(stage)),
                       Path.GetFullPath(parent).TrimEnd('\\', '/'),
                       StringComparison.OrdinalIgnoreCase) ||
        !Regex.IsMatch(Path.GetFileName(stage), "^[0-9a-f]{32}$"))
      throw new IOException("Unexpected staging directory.");
    if (Directory.Exists(stage))
      Directory.Delete(stage, true);
  }

  internal static void Extract(ZipArchive archive, string destination) {
    var prefix = Path.GetFullPath(destination).TrimEnd('\\', '/') + Path.DirectorySeparatorChar;
    foreach (var entry in archive.Entries) {
      var target = Path.GetFullPath(Path.Combine(prefix, entry.FullName));
      if (!target.StartsWith(prefix, StringComparison.OrdinalIgnoreCase))
        throw new InvalidDataException("Archive path escapes its destination.");
      if (entry.FullName.EndsWith("/") || entry.FullName.EndsWith("\\"))
        Directory.CreateDirectory(target);
      else {
        Directory.CreateDirectory(Path.GetDirectoryName(target));
        entry.ExtractToFile(target);
      }
    }
  }

  internal int Run(string program, string directory, IEnumerable<string> arguments, bool required,
                   Action<string> output = null, Action poll = null) {
    return RunStart(new ProcessStartInfo(program, String.Join(" ", arguments.Select(Quote))),
                    directory, required, output, poll);
  }

  internal int RunStart(ProcessStartInfo start, string directory, bool required,
                        Action<string> output = null, Action poll = null) {
    start.WorkingDirectory = directory;
    start.UseShellExecute = false;
    start.CreateNoWindow = true;
    start.RedirectStandardOutput = true;
    start.RedirectStandardError = true;
    foreach (var variable in EnvironmentVariables)
      start.EnvironmentVariables[variable.Key] = variable.Value;
    log(start.FileName + " " + start.Arguments);
    using (var process = new Process { StartInfo = start }) {
      var outputLock = new object();
      Exception outputError = null;
      DataReceivedEventHandler receive = delegate(object sender, DataReceivedEventArgs args) {
        if (args.Data == null)
          return;
        lock (outputLock) {
          if (outputError != null)
            return;
          try {
            log(args.Data);
            if (output != null)
              output(args.Data);
            var compile = Regex.Match(args.Data, @"^\[(\d+)/(\d+)\]");
            var git = Regex.Match(args.Data,
                                  @"(Receiving objects|Resolving deltas|Updating files):\s*(\d+)%");
            if (compile.Success && Double.Parse(compile.Groups[2].Value) > 0)
              Report("Compiling", args.Data,
                     (int)Math.Min(100, 100 * Double.Parse(compile.Groups[1].Value) /
                                            Double.Parse(compile.Groups[2].Value)));
            else if (git.Success)
              Report(git.Groups[1].Value, args.Data,
                     Math.Min(100, Int32.Parse(git.Groups[2].Value)));
          } catch (Exception error) {
            outputError = error;
          }
        }
      };
      process.OutputDataReceived += receive;
      process.ErrorDataReceived += receive;
      process.Start();
      process.BeginOutputReadLine();
      process.BeginErrorReadLine();
      if (poll != null)
        do {
          poll();
        } while (!process.WaitForExit(100));
      process.WaitForExit();
      if (outputError != null)
        throw new IOException("Could not read or save tool output.", outputError);
      if (required && process.ExitCode != 0)
        throw new IOException(Path.GetFileName(start.FileName) + " failed (code " +
                              process.ExitCode + "). Open the setup log for details, then retry.");
      return process.ExitCode;
    }
  }

  internal string Capture(string program, string directory, params string[] arguments) {
    var lines = new List<string>();
    Run(program, directory, arguments, true,
        delegate(string line) { lock (lines) lines.Add(line); });
    return String.Join("\n", lines).Trim();
  }

  internal static bool Compatible(string name, string version) {
    var match = Regex.Match(version, @"^git version (\d+\.\d+\.\d+)");
    return name == "Git"
               ? match.Success && new Version(match.Groups[1].Value) >= new Version(2, 36, 0)
               : name == "LLVM" && Regex.IsMatch(version, @"clang version 20\.1\.8\b");
  }

  private string Installed(Package package, string directory) {
    if (package != Git && package != LLVM)
      return null;
    var name = package == Git ? "git.exe" : "clang++.exe";
    var folders = (Environment.GetEnvironmentVariable("PATH") ?? "").Split(';').ToList();
    if (package == LLVM)
      folders.Add(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles),
                               "LLVM/bin"));
    foreach (var folder in folders) {
      if (String.IsNullOrWhiteSpace(folder))
        continue;
      var candidate = Path.Combine(folder.Trim('"'), name);
      if (!File.Exists(candidate) ||
          (package == LLVM &&
           !File.Exists(Path.Combine(Path.GetDirectoryName(candidate), "clang.exe"))))
        continue;
      try {
        if (Compatible(package.Name, Capture(candidate, directory, "--version")))
          return candidate;
      } catch (System.ComponentModel.Win32Exception) {
      } catch (IOException) {
      }
    }
    return null;
  }

  internal void Download(string url, string file, string name) {
    var offset = File.Exists(file) ? new FileInfo(file).Length : 0;
    var request = (HttpWebRequest)WebRequest.Create(url);
    request.Timeout = 25000;
    request.ReadWriteTimeout = 30000;
    request.UserAgent = "LollipopChainsawRecomp-Setup";
    if (offset > 0)
      request.AddRange(offset);
    HttpWebResponse response;
    try {
      response = (HttpWebResponse)request.GetResponse();
    } catch (WebException error) {
      using (var failed = error.Response as HttpWebResponse) {
        if (offset <= 0 || failed == null ||
            failed.StatusCode != HttpStatusCode.RequestedRangeNotSatisfiable)
          throw;
      }
      File.Delete(file);
      Download(url, file, name);
      return;
    }
    using (response) {
      if (response.StatusCode == HttpStatusCode.PartialContent) {
        if (!Regex.IsMatch(response.Headers["Content-Range"] ?? "",
                           @"^bytes " + offset + @"-\d+/\d+$"))
          throw new IOException("The download server returned an invalid resume range.");
      } else if (response.StatusCode == HttpStatusCode.OK)
        offset = 0;
      else
        throw new IOException("Unexpected download response: " + response.StatusCode);
      var total = response.ContentLength < 0 ? -1 : offset + response.ContentLength;
      var received = offset;
      var watch = Stopwatch.StartNew();
      long lastReport = -500;
      using (var input = response.GetResponseStream()) using (
          var output = new FileStream(file, offset > 0 ? FileMode.Append : FileMode.Create,
                                      FileAccess.Write)) {
        var buffer = new byte[65536];
        int count;
        while ((count = input.Read(buffer, 0, buffer.Length)) > 0) {
          output.Write(buffer, 0, count);
          received += count;
          if (watch.ElapsedMilliseconds - lastReport < 200 && received != total)
            continue;
          lastReport = watch.ElapsedMilliseconds;
          var status = (received / 1048576.0).ToString("N1") + " MiB";
          if (total > 0)
            status += " / " + (total / 1048576.0).ToString("N1") + " MiB";
          status += "  |  " +
                    ((received - offset) / Math.Max(watch.Elapsed.TotalSeconds, 0.1) / 1048576)
                        .ToString("N1") +
                    " MiB/s";
          Report("Downloading " + name, status,
                 total > 0 ? (int)Math.Min(100, 100.0 * received / total) : -1);
        }
      }
      if (total >= 0 && received != total)
        throw new IOException("Download interrupted. Retry to resume " + name + ".");
    }
  }

  internal string Get(Package package, string cache) {
    Directory.CreateDirectory(cache);
    var folder = Path.Combine(cache, package.Name + "-" + package.Hash.Substring(0, 12));
    var program = Path.Combine(folder, package.File);
    if (File.Exists(program)) {
      Report(package.Name, "Using cached tools", 100);
      return program;
    }
    var installed = Installed(package, cache);
    if (installed != null) {
      log("Using installed " + package.Name + ": " + installed);
      return installed;
    }
    var stage = Path.Combine(cache, Guid.NewGuid().ToString("N"));
    Directory.CreateDirectory(stage);
    var download = folder + ".download";
    try {
      if (!File.Exists(download) || HashFile(download) != package.Hash)
        Download(package.Url, download, package.Name);
      Report("Verifying " + package.Name, "Checking SHA-256", -1);
      if (HashFile(download) != package.Hash) {
        File.Delete(download);
        throw new InvalidDataException("The " + package.Name +
                                       " download failed its integrity check.");
      }
      Report("Unpacking " + package.Name, "Extracting verified download", -1);
      if (package.Url.EndsWith(".exe", StringComparison.OrdinalIgnoreCase))
        File.Copy(download, Path.Combine(stage, package.File));
      else if (package.Url.EndsWith(".zip", StringComparison.OrdinalIgnoreCase)) {
        using (var archive = ZipFile.OpenRead(download)) Extract(archive, stage);
      } else
        Run(Path.Combine(Environment.SystemDirectory, "tar.exe"), stage,
            new[] { "-xf", download, "-C", stage }, true);
      if (!File.Exists(Path.Combine(stage, package.File)))
        throw new InvalidDataException("The " + package.Name + " download is incomplete.");
      Directory.Move(stage, folder);
      File.Delete(download);
      return program;
    } finally {
      DeleteStage(stage, cache);
    }
  }
}
}
