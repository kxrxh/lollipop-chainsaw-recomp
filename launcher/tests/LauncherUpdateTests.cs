using System;
using System.Diagnostics;
using System.IO;
using System.Threading;

namespace lollipop;

internal static class LauncherUpdateTests {
  private static void Fails(Action action) {
    try {
      action();
    } catch (IOException) {
      return;
    } catch (InvalidDataException) {
      return;
    }
    throw new Exception("An invalid launcher update was accepted");
  }

  private static void WaitFor(Func<bool> ready) {
    if (!SpinWait.SpinUntil(ready, TimeSpan.FromSeconds(15)))
      throw new Exception("The launcher update fixture timed out");
  }

  internal static void Run(string sandbox) {
    var url = Releases.Page + "/download/v99.0.0/" + LauncherUpdate.AssetName;
    var digest = "sha256:" + new string('a', 64);
    Release Release(string name, string download, string? hash, long size = 10) => new() {
      Tag = "v99.0.0", Assets = [new() { Name = name, Url = download, Digest = hash, Size = size }]
    };
    var asset = LauncherUpdate.Asset(Release(LauncherUpdate.AssetName, url, digest));
    if (asset.Url != url || asset.Digest != digest)
      throw new Exception("The release launcher asset was lost");
    foreach (var release in new[] {
               new Release { Tag = "v99.0.0" }, Release("unrelated.exe", url, digest),
               Release(LauncherUpdate.AssetName, url.Replace("https:", "http:"), digest),
               Release(LauncherUpdate.AssetName, url.Replace("github.com/", "github.com.evil/"),
                       digest),
               Release(LauncherUpdate.AssetName, url, null),
               Release(LauncherUpdate.AssetName, url, "sha256:bad"),
               Release(LauncherUpdate.AssetName, url, digest, 0),
               new Release { Tag = "v99.0.0", Assets = [asset, asset] }
             })
      Fails(() => LauncherUpdate.Asset(release));
    var parsed = Releases.Parse("{\"tag_name\":\"v99.0.0\",\"assets\":[{\"name\":\"" +
                                LauncherUpdate.AssetName + "\",\"browser_download_url\":\"" + url +
                                "\",\"digest\":\"" + digest + "\",\"size\":10}]}");
    LauncherUpdate.Asset(parsed);
    Fails(() => LauncherUpdate.CheckVersion(Environment.ProcessPath!, "99.0.0"));
    LauncherUpdate.CheckVersion(Environment.ProcessPath!, "0.0.1");

    var folder = Path.Combine(sandbox, "updated player's [1] Ж launcher");
    Directory.CreateDirectory(folder);
    var target = Path.Combine(folder, LauncherInstallation.FileName);
    var downloaded = Path.Combine(folder, "download.exe");
    File.WriteAllText(target, "old launcher");
    File.WriteAllText(downloaded, "new launcher");
    Fails(() => LauncherUpdate.Replace(downloaded, target, new string('0', 64)));
    var hash = Tools.HashFile(downloaded);
    var cache = Path.Combine(folder, "updates");
    var cached = Path.Combine(cache, "Launcher-" + hash[..12], LauncherInstallation.FileName);
    Directory.CreateDirectory(Path.GetDirectoryName(cached)!);
    File.WriteAllText(cached, "damaged cached launcher");
    var cachedAsset = new ReleaseAsset { Url = url, Digest = "sha256:" + hash,
                                         Size = new FileInfo(downloaded).Length };
    var tools = new Tools(new RecordedProgress(),
                          _ => {});
    Fails(() => LauncherUpdate.Download(cachedAsset, tools, cache));
    Directory.CreateDirectory(Path.GetDirectoryName(cached)!);
    File.Copy(downloaded, cached);
    if (LauncherUpdate.Download(cachedAsset, tools, cache) != cached)
      throw new Exception("A damaged launcher cache prevented a verified retry");
    using (File.Open(target, FileMode.Open, FileAccess.Read, FileShare.Read))
        Fails(() => LauncherUpdate.Replace(downloaded, target, hash));
    if (File.ReadAllText(target) != "old launcher" ||
        Directory.GetFiles(folder, "*.tmp").Length != 0)
      throw new Exception(
          "A failed self-update damaged the current launcher or left partial files");
    LauncherUpdate.Replace(downloaded, target, hash);
    if (Tools.HashFile(target) != hash)
      throw new Exception("The verified launcher was not published");

    File.Copy(Environment.ProcessPath!, target, true);
    File.Copy(Environment.ProcessPath!, downloaded, true);
    hash = Tools.HashFile(downloaded);
    var location = Path.Combine(folder, "launcher-data/game-location.txt");
    GameLocation.Save(location, sandbox);
    var ready = Path.Combine(folder, "parent-ready");
    var exit = Path.Combine(folder, "parent-exit");
    var marker = Path.Combine(folder, "launcher-restarted");
    var start = new ProcessStartInfo(target) { UseShellExecute = false, CreateNoWindow = true };
    start.ArgumentList.Add("--update-parent");
    start.ArgumentList.Add(ready);
    start.ArgumentList.Add(exit);
    using var parent = Process.Start(start)!;
    Process? helper = null;
    try {
      WaitFor(() => File.Exists(ready));
      var before = File.GetLastWriteTimeUtc(target);
      var helperPath = Path.Combine(folder, "updater.exe");
      File.Copy(Environment.ProcessPath!, helperPath);
      var update =
          new ProcessStartInfo(helperPath) { UseShellExecute = false, CreateNoWindow = true };
      foreach (var argument in new[] { LauncherUpdate.Command, target, parent.Id.ToString(),
                                       downloaded, hash })
        update.ArgumentList.Add(argument);
      helper = Process.Start(update)!;
      Thread.Sleep(200);
      if (helper.HasExited || File.Exists(marker) || File.GetLastWriteTimeUtc(target) != before)
        throw new Exception("The updater did not wait for the running launcher to exit");
      File.WriteAllText(exit, "close");
      WaitFor(() => helper.HasExited);
      WaitFor(() => File.Exists(marker));
      try {
        using var restarted = Process.GetProcessById(Int32.Parse(File.ReadAllText(marker)));
        if (!restarted.WaitForExit(5000))
          throw new Exception("The restarted fixture did not exit");
      } catch (ArgumentException) {
        // The restarted fixture already exited.
      }
      if (helper.ExitCode != 0 || Tools.HashFile(target) != hash ||
          GameLocation.Load(location) != sandbox)
        throw new Exception("The self-update did not restart or lost the selected installation");
    } finally {
      File.WriteAllText(exit, "close");
      if (helper != null) {
        if (!helper.WaitForExit(5000))
          helper.Kill(true);
        helper.Dispose();
      }
      if (!parent.WaitForExit(5000))
        parent.Kill(true);
    }
  }
}
