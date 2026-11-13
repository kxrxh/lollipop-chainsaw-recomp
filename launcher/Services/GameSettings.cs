using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;

namespace lollipop;

internal sealed class GameSettings {
  internal static readonly GraphicsField[] Fields = {
    new GraphicsField("window_size", "Window resolution",
                      "Borderless fullscreen uses your desktop resolution.", "2560x1440",
                      "1280x720|1280 × 720", "1600x900|1600 × 900", "1920x1080|1920 × 1080",
                      "2560x1440|2560 × 1440", "3840x2160|3840 × 2160"),
    new GraphicsField("fullscreen", "Display mode", "Use a window or fill the desktop.", "true",
                      "false|Windowed", "true|Borderless fullscreen"),
    new GraphicsField("vsync", "Vertical sync", "Reduces screen tearing.", "true", "false|Off",
                      "true|On"),
    new GraphicsField("present_letterbox", "Aspect ratio", "Keep the original picture proportions.",
                      "true", "true|Original proportions", "false|Stretch to window"),
    new GraphicsField("resolution_scale", "Render resolution",
                      "Higher internal resolution increases GPU usage.", "2",
                      "1|1280 × 720 / Native", "2|2560 × 1440 / 2x", "3|3840 × 2160 / 3x"),
    new GraphicsField("swap_post_effect", "Anti-aliasing", "Smooths jagged edges.", "none",
                      "none|Off", "fxaa|FXAA", "fxaa_extreme|FXAA / Strong"),
    new GraphicsField("anisotropic_override", "Texture filtering", "Sharper textures at an angle.",
                      "5", "-1|Original", "0|Off", "2|2x", "3|4x", "4|8x", "5|16x"),
    new GraphicsField("lollipop_fps", "Frame rate limit",
                      "90 and 120 FPS gameplay is still experimental.", "60",
                      "30|30 FPS / Original", "60|60 FPS", "90|90 FPS", "120|120 FPS"),
    new GraphicsField("present_dither", "Color dithering", "Reduces color banding.", "false",
                      "false|Off", "true|On")
  };
  private static readonly Regex Assignment =
      new Regex(@"\A\s*([A-Za-z_]\w*)\s+=\s*(?:""((?:\\.|[^""\\])*)""|([^\s""]+))\s*\z");
  internal readonly Dictionary<string, string> Values = new Dictionary<string, string>();
  private readonly string? original;

  internal GameSettings(string? text) {
    original = text;
    var stored = new Dictionary<string, string>();
    foreach (var line in (text ?? "").Split('\n')) {
      string name, value;
      if (!ReadLine(line, out name, out value))
        continue;
      if (stored.ContainsKey(name))
        throw new InvalidDataException("The game settings profile contains a duplicate setting: " +
                                       name);
      Validate(name, value);
      stored.Add(name, value);
    }
    foreach (var field in Fields) {
      string ? value;
      Values[field.Name] =
          field.Name == "window_size"
              ? (stored.ContainsKey("window_width") ? stored["window_width"] : "2560") + "x" +
                    (stored.ContainsKey("window_height") ? stored["window_height"] : "1440")
          : stored.TryGetValue(field.Name, out value) ? value
                                                      : field.Default;
    }
  }

  private static bool ReadLine(string line, out string name, out string value) {
    name = value = "";
    if (String.IsNullOrWhiteSpace(line) || line.TrimStart().StartsWith("#"))
      return false;
    var match = Assignment.Match(line);
    if (!match.Success)
      throw new InvalidDataException(
          "The game settings profile is malformed. The original file was kept.");
    name = match.Groups[1].Value;
    value = match.Groups[2].Success ? Regex.Replace(match.Groups[2].Value, @"\\(.)", "$1")
                                    : match.Groups[3].Value;
    if (name == "lollipop_60fps") {
      if (value != "true" && value != "false")
        throw new InvalidDataException("The legacy frame rate setting is invalid.");
      name = "lollipop_fps";
      value = value == "true" ? "60" : "30";
    }
    return true;
  }

  private static void Validate(string name, string value) {
    double scalar;
    bool valid;
    switch (name) {
    case "window_width":
      valid = Integer(value, 640, 8192);
      break;
    case "window_height":
      valid = Integer(value, 480, 8192);
      break;
    case "resolution_scale":
      valid = Integer(value, 1, 8);
      break;
    case "anisotropic_override":
      valid = Integer(value, -1, 5);
      break;
    case "lollipop_fps":
      valid = new[] { "30", "60", "90", "120" }.Contains(value);
      break;
    case "swap_post_effect":
      valid = new[] { "none", "fxaa", "fxaa_extreme" }.Contains(value);
      break;
    case "mnk_sensitivity":
    case "left_stick_deadzone_percentage":
    case "right_stick_deadzone_percentage":
      valid =
          Regex.IsMatch(value, @"\A-?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?\z") &&
          Double.TryParse(value, NumberStyles.Float, CultureInfo.InvariantCulture, out scalar) &&
          !Double.IsNaN(scalar) && !Double.IsInfinity(scalar) &&
          (name == "mnk_sensitivity" ? scalar >= 0.01 && scalar <= 50 : scalar >= 0 && scalar <= 1);
      break;
    case "lollipop_music_volume":
    case "lollipop_sfx_volume":
      valid = Integer(value, 0, 10);
      break;
    default:
      valid = new[] { "fullscreen",
                      "vsync",
                      "present_letterbox",
                      "present_dither",
                      "mnk_mouse",
                      "vibration",
                      "lollipop_subtitles",
                      "audio_mute",
                      "lollipop_camera_invert_y",
                      "lollipop_camera_invert_x",
                      "lollipop_manual_aim" }
                  .Contains(name) &&
              (value == "true" || value == "false");
      break;
    }
    if (!valid)
      throw new InvalidDataException("Unsupported game setting: " + name + " = " + value +
                                     ". The original file was kept.");
  }

  private static bool Integer(string text, int minimum, int maximum) {
    int value;
    return Regex.IsMatch(text ?? "", @"\A-?\d+\z") && Int32.TryParse(text, out value) &&
           value >= minimum && value <= maximum;
  }

  internal static string PathFor(string destination) {
    return Path.Combine(destination, "user/pc-settings.toml");
  }
  private static string? Read(string path) {
    return File.Exists(path) ? File.ReadAllText(path, new UTF8Encoding(false, true)) : null;
  }
  internal static GameSettings Load(string destination) {
    return new GameSettings(Read(PathFor(destination)));
  }

  internal void Save(string destination, string launcher, IDictionary<string, string> selected) {
    if (selected.Count != Fields.Length || Fields.Any(field => !selected.ContainsKey(field.Name)))
      throw new InvalidDataException("Choose valid graphics and display settings.");
    var edited = new Dictionary<string, string>();
    foreach (var pair in selected) {
      if (pair.Key == "window_size") {
        var size = (pair.Value ?? "").Split('x');
        if (size.Length != 2)
          throw new InvalidDataException("Choose a valid window resolution.");
        edited.Add("window_width", size[0]);
        edited.Add("window_height", size[1]);
      } else {
        edited.Add(pair.Key,
                   pair.Value ?? throw new InvalidDataException("Choose a valid setting."));
      }
    }
    foreach (var pair in edited)
      Validate(pair.Key, pair.Value);
    if (!Installer.Ready(destination) && !GameUpdate.CanUpdate(destination))
      throw new IOException("Locate an installed game before saving settings.");
    var state = Path.Combine(destination, "out/launcher");
    Directory.CreateDirectory(state);
    using (var installationLock =
               File.Open(Path.Combine(state, "launcher.lock"), FileMode.OpenOrCreate,
                         FileAccess.ReadWrite, FileShare.None)) {
      try {
        GameUpdate.CheckClosed(destination, launcher);
      } catch (IOException error) {
        throw new IOException("Close the game and its other launcher before saving settings.",
                              error);
      }
      var path = PathFor(destination);
      if (Read(path) != original)
        throw new IOException(
            "The game settings changed outside this page. Reload them before saving.");
      var output = new StringBuilder();
      var written = new HashSet<string>();
      var newline = original != null && original.Contains("\r\n") ? "\r\n" : "\n";
      foreach (var line in Regex.Split(original ?? "", "(?<=\n)")) {
        string name, value;
        if (ReadLine(line, out name, out value) && edited.ContainsKey(name)) {
          output.Append(name + " = \"" + edited[name] + "\"");
          if (line.EndsWith("\n"))
            output.Append(newline);
          written.Add(name);
        } else {
          output.Append(line);
        }
      }
      foreach (var pair in edited) {
        if (written.Contains(pair.Key))
          continue;
        if (output.Length > 0 && output[output.Length - 1] != '\n')
          output.Append(newline);
        output.Append(pair.Key + " = \"" + pair.Value + "\"" + newline);
      }
      Directory.CreateDirectory(Path.GetDirectoryName(path)!);
      var temporary = path + "." + Guid.NewGuid().ToString("N") + ".tmp";
      try {
        File.WriteAllText(temporary, output.ToString(), new UTF8Encoding(false));
        if (File.Exists(path))
          File.Replace(temporary, path, null);
        else
          File.Move(temporary, path);
      } finally {
        File.Delete(temporary);
      }
    }
  }
}
