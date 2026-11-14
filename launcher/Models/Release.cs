using System.Text.Json.Serialization;

namespace lollipop;

internal sealed class Release {
  [JsonPropertyName("tag_name")]
  public string Tag { get; init; } = "";
  [JsonPropertyName("body")]
  public string? Body { get; init; }
  [JsonPropertyName("draft")]
  public bool Draft { get; init; }
  [JsonPropertyName("prerelease")]
  public bool Prerelease { get; init; }
  [JsonPropertyName("assets")]
  public ReleaseAsset[]? Assets { get; init; }
}

internal sealed class ReleaseAsset {
  [JsonPropertyName("name")]
  public string Name { get; init; } = "";
  [JsonPropertyName("browser_download_url")]
  public string Url { get; init; } = "";
  [JsonPropertyName("digest")]
  public string? Digest { get; init; }
  [JsonPropertyName("size")]
  public long Size { get; init; }
}
