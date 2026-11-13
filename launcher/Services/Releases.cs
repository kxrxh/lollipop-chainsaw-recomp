using System;
using System.IO;
using System.Net;
using System.Text.RegularExpressions;
using System.Text.Json;
using System.Net.Http;

namespace lollipop;

internal static class Releases {
  internal const string Page = "https://github.com/kxrxh/lollipop-chainsaw-recomp/releases";
  internal const string Endpoint =
      "https://api.github.com/repos/kxrxh/lollipop-chainsaw-recomp/releases/latest";
  internal static readonly string GameVersion = Resource("VERSION").Trim();
  internal static readonly string LauncherVersion = Resource("LAUNCHER_VERSION").Trim();

  internal static string Resource(string name) {
    using (var reader = new StreamReader(LauncherPackage.Resource(name))) {
      return reader.ReadToEnd();
    }
  }

  private static System.Version StableVersion(string value) {
    var match = Regex.Match(value, @"\Av?(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)\z");
    System.Version ? version;
    if (!match.Success || !System.Version.TryParse(value.TrimStart('v'), out version))
      throw new InvalidDataException("The release has an unsupported version tag.");
    return version;
  }

  internal static bool IsNewer(string latest, string current) {
    var release = StableVersion(latest);
    var installed = StableVersion(current.Split('-', '+')[0]);
    var comparison = release.CompareTo(installed);
    return comparison > 0 || (comparison == 0 && current.Contains("-"));
  }

  internal static Release Parse(string json) {
    Release ? release;
    try {
      release = JsonSerializer.Deserialize<Release>(json);
    } catch (JsonException error) {
      throw new InvalidDataException("The release response could not be read.", error);
    }
    if (release == null || release.Draft || release.Prerelease)
      throw new InvalidDataException("No stable release was returned.");
    StableVersion(release.Tag);
    return release;
  }

  internal static Release? Latest() {
    using var client = new HttpClient { Timeout = TimeSpan.FromSeconds(15) };
    client.DefaultRequestHeaders.UserAgent.ParseAdd("Lollipop-Chainsaw-Recomp-Launcher/" +
                                                    LauncherVersion);
    client.DefaultRequestHeaders.Accept.ParseAdd("application/vnd.github+json");
    using var response = client.GetAsync(Endpoint).GetAwaiter().GetResult();
    if (response.StatusCode == HttpStatusCode.NotFound)
      return null;
    response.EnsureSuccessStatusCode();
    return Parse(response.Content.ReadAsStringAsync().GetAwaiter().GetResult());
  }

  internal static string InstalledVersion(string? destination) {
    if (String.IsNullOrEmpty(destination))
      return "Not installed";
    try {
      var version = File.ReadAllText(Path.Combine(destination,
                                                  "out/build/win-amd64-release/recomp-version.txt"))
                        .Trim();
      if (version.Length > 0 && version.Length <= 80 && !Regex.IsMatch(version, @"\s"))
        return version;
    } catch (IOException) {
    } catch (UnauthorizedAccessException) {
    }
    return "Unknown (older installation)";
  }
}
