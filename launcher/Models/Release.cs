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
}
