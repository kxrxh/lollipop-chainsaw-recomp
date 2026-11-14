using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Net;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Threading;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;

namespace lollipop;

internal sealed class Tools {
  internal static readonly Package Disc =
      new Package("Disc",
                  "https://github.com/XboxDev/extract-xiso/releases/download/build-202609111233/" +
                      "extract-xiso-Win64_Release.zip",
                  "fc891dfa72f08e3838a2f8b7ec34a8e4d61678da1cc8fee879677f00bf114ce3",
                  "artifacts/extract-xiso.exe");

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

  internal static void MoveDirectory(string source, string destination) {
    for (var attempt = 0;; ++attempt) {
      try {
        Directory.Move(source, destination);
        return;
      } catch (IOException error) when (attempt < 5 && (error.HResult & 0xffff) is 5 or 32 or 33) {
        // Virus scanners can briefly lock files immediately after extraction.
        Thread.Sleep(100 << attempt);
      }
    }
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
        Directory.CreateDirectory(Path.GetDirectoryName(target)!);
        entry.ExtractToFile(target);
      }
    }
  }

  internal int Run(string program, string directory, IEnumerable<string> arguments, bool required,
                   Action<string>? output = null, Action? poll = null, string? activity = null) {
    return RunStart(new ProcessStartInfo(program, String.Join(" ", arguments.Select(Quote))),
                    directory, required, output, poll, activity);
  }

  internal int RunStart(ProcessStartInfo start, string directory, bool required,
                        Action<string>? output = null, Action? poll = null,
                        string? activity = null) {
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
      Exception? outputError = null;
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
            if (compile.Success && Double.Parse(compile.Groups[2].Value) > 0)
              Report("Compiling", args.Data,
                     (int)Math.Min(100, 100 * Double.Parse(compile.Groups[1].Value) /
                                            Double.Parse(compile.Groups[2].Value)));
            else if (activity != null && !String.IsNullOrWhiteSpace(args.Data))
              Report(activity, args.Data, -1);
          } catch (Exception error) {
            outputError = error;
          }
        }
      };
      process.OutputDataReceived += receive;
      process.ErrorDataReceived += receive;
      process.Start();
      Report(activity ?? "Running " + Path.GetFileNameWithoutExtension(start.FileName),
             "Waiting for tool output...", -1);
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
    if (name == "LLVM")
      return Regex.IsMatch(version, @"clang version 20\.1\.8\b");
    var match = Regex.Match(version, name == "CMake" ? @"^cmake version (\d+\.\d+\.\d+)"
                                                     : @"^(\d+\.\d+\.\d+)");
    return match.Success &&
           (name == "CMake"
                ? new Version(match.Groups[1].Value) >= new Version(3, 25, 0) &&
                      new Version(match.Groups[1].Value) < new Version(4, 0)
                : name == "Ninja" && new Version(match.Groups[1].Value) >= new Version(1, 11, 0) &&
                      new Version(match.Groups[1].Value) < new Version(2, 0));
  }

  private string? Installed(Package package, string directory) {
    var name = package.Name switch { "LLVM" => "clang++.exe", "CMake" => "cmake.exe",
                                     "Ninja" => "ninja.exe",
                                     _ => null };
    if (name is null)
      return null;
    var path = EnvironmentVariables.TryGetValue("PATH", out var configured)
                   ? configured
                   : Environment.GetEnvironmentVariable("PATH");
    var folders = (path ?? "").Split(';').ToList();
    if (package.Name == "LLVM")
      folders.Add(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles),
                               "LLVM/bin"));
    if (package.Name == "CMake")
      folders.Add(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles),
                               "CMake/bin"));
    foreach (var folder in folders) {
      if (String.IsNullOrWhiteSpace(folder))
        continue;
      var candidate = Path.Combine(folder.Trim('"'), name);
      if (!File.Exists(candidate) ||
          (package.Name == "LLVM" &&
           (!File.Exists(Path.Combine(Path.GetDirectoryName(candidate)!, "clang.exe")) ||
            !File.Exists(Path.Combine(Path.GetDirectoryName(candidate)!, "lld-link.exe")) ||
            !File.Exists(Path.Combine(Path.GetDirectoryName(candidate)!, "llvm-ar.exe")) ||
            !File.Exists(Path.Combine(Path.GetDirectoryName(candidate)!,
                                      "../lib/clang/20/include/stddef.h")))))
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
    using var client =
        new HttpClient(new SocketsHttpHandler { ConnectTimeout = TimeSpan.FromSeconds(
                                                    25) }) { Timeout = Timeout.InfiniteTimeSpan };
    using var request = new HttpRequestMessage(HttpMethod.Get, url);
    request.Headers.UserAgent.ParseAdd("LollipopChainsawRecomp-Launcher");
    if (offset > 0)
      request.Headers.Range = new RangeHeaderValue(offset, null);
    using var deadline = new CancellationTokenSource(TimeSpan.FromSeconds(30));
    using var response =
        client.Send(request, HttpCompletionOption.ResponseHeadersRead, deadline.Token);
    if (offset > 0 && response.StatusCode == HttpStatusCode.RequestedRangeNotSatisfiable) {
      File.Delete(file);
      Download(url, file, name);
      return;
    }
    if (response.StatusCode == HttpStatusCode.PartialContent) {
      var range = response.Content.Headers.ContentRange;
      if (range?.Unit != "bytes" || range.From != offset || range.Length is null)
        throw new IOException("The download server returned an invalid resume range.");
    } else if (response.StatusCode == HttpStatusCode.OK) {
      offset = 0;
    } else {
      response.EnsureSuccessStatusCode();
      throw new IOException("Unexpected download response: " + response.StatusCode);
    }
    var total = response.Content.Headers.ContentLength is long length ? offset + length : -1;
    var received = offset;
    var watch = Stopwatch.StartNew();
    long lastReport = -500;
    using var input = response.Content.ReadAsStream();
    using var output =
        new FileStream(file, offset > 0 ? FileMode.Append : FileMode.Create, FileAccess.Write);
    var buffer = new byte[65536];
    while (true) {
      deadline.CancelAfter(TimeSpan.FromSeconds(30));
      var count =
          input.ReadAsync(buffer.AsMemory(), deadline.Token).AsTask().GetAwaiter().GetResult();
      if (count == 0)
        break;
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
    if (total >= 0 && received != total)
      throw new IOException("Download interrupted. Retry to resume " + name + ".");
  }
  internal string Get(Package package, string cache, bool useInstalled = true,
                      Stream? contents = null) {
    Directory.CreateDirectory(cache);
    var folder = Path.Combine(cache, package.Name + "-" + package.Hash.Substring(0, 12));
    var program = Path.Combine(folder, package.File);
    if (File.Exists(program)) {
      Report(package.Name, "Using cached tools", 100);
      return program;
    }
    var installed = useInstalled ? Installed(package, cache) : null;
    if (installed != null) {
      log("Using installed " + package.Name + ": " + installed);
      return installed;
    }
    var stage = Path.Combine(cache, Guid.NewGuid().ToString("N"));
    Directory.CreateDirectory(stage);
    var download = folder + ".download";
    try {
      if (contents != null) {
        contents.Position = 0;
        using var file = File.Create(download);
        contents.CopyTo(file);
      } else if (!File.Exists(download) || HashFile(download) != package.Hash)
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
      else if (new[] { ".zip", ".vsix", ".nupkg" }.Any(
                   extension =>
                       package.Url.EndsWith(extension, StringComparison.OrdinalIgnoreCase))) {
        using (var archive = ZipFile.OpenRead(download)) Extract(archive, stage);
      } else
        Run(Path.Combine(Environment.SystemDirectory, "tar.exe"), stage,
            new[] { "-xf", download, "-C", stage }, true);
      if (!File.Exists(Path.Combine(stage, package.File)))
        throw new InvalidDataException("The " + package.Name + " download is incomplete.");
      MoveDirectory(stage, folder);
      File.Delete(download);
      return program;
    } finally {
      DeleteStage(stage, cache);
    }
  }
}
