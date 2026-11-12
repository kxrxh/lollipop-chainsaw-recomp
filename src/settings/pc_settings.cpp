#include "pc_settings.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <windows.h>

#include <rex/cvar.h>
#include <rex/logging.h>

namespace lollipop {

std::span<const pc_setting> pc_settings() {
  static const pc_setting settings[] = {
      {"window_width", "Window resolution", "Fullscreen uses your desktop resolution.", "2560", 0,
       {{"1280", "1280 x 720"}, {"1600", "1600 x 900"}, {"1920", "1920 x 1080"}, {"2560", "2560 x 1440"}, {"3840", "3840 x 2160"}}},
      {"fullscreen", "Display mode", "Windowed or borderless fullscreen.", "true", 0,
       {{"false", "Windowed"}, {"true", "Borderless fullscreen"}}},
      {"vsync", "Vertical sync", "Reduces screen tearing.", "true", 0,
       {{"false", "Off"}, {"true", "On"}}},
      {"present_letterbox", "Keep aspect ratio", "Keeps the picture from stretching.", "true", 0,
       {{"true", "Original proportions"}, {"false", "Stretch to window"}}},
      {"resolution_scale", "Render resolution", "Sharper image, higher GPU usage.", "2", 1,
       {{"1", "1280 x 720  /  Native"}, {"2", "2560 x 1440  /  2x"}, {"3", "3840 x 2160  /  3x"}}},
      {"swap_post_effect", "Anti-aliasing", "Smooths jagged edges.", "fxaa", 1,
       {{"none", "Off"}, {"fxaa", "FXAA"}, {"fxaa_extreme", "FXAA  /  Strong"}}},
      {"anisotropic_override", "Texture filtering", "Sharper textures at an angle.", "5", 1,
       {{"-1", "Original"}, {"0", "Off"}, {"2", "2x"}, {"3", "4x"}, {"4", "8x"}, {"5", "16x"}}},
      {"lollipop_fps", "Frame rate", "Maximum frame rate.", "60", 1,
       {{"30", "Original  /  30 FPS"}, {"60", "60 FPS"}, {"90", "90 FPS"}, {"120", "120 FPS"}}},
      {"present_dither", "Color dithering", "Reduces color banding.", "false", 1,
       {{"false", "Off"}, {"true", "On"}}},
      {"mnk_mouse", "Mouse camera", "Control the camera with the mouse.", "true", 3,
       {{"false", "Off"}, {"true", "On"}}},
      {"mnk_sensitivity", "Mouse sensitivity", "Mouse camera speed.", "18", 3,
       {}, 0.01f, 50.0f, 0.5f},
      {"vibration", "Controller vibration", "Controller rumble.", "true", 4,
       {{"false", "Off"}, {"true", "On"}}},
      {"left_stick_deadzone_percentage", "Movement deadzone", "Increase if movement drifts.", "0", 4,
       {}, 0.0f, 1.0f, 0.01f},
      {"right_stick_deadzone_percentage", "Camera deadzone", "Increase if the camera drifts.", "0", 4,
       {}, 0.0f, 1.0f, 0.01f},
      {"lollipop_music_volume", "Music volume", "Music loudness.", "10", 2, {}, 0, 10, 1},
      {"lollipop_sfx_volume", "Effects volume", "Sound effects and voice loudness.", "10", 2, {}, 0, 10, 1},
      {"lollipop_subtitles", "Subtitles", "Show dialogue subtitles.", "false", 2,
       {{"false", "Off"}, {"true", "On"}}},
      {"audio_mute", "Mute all audio", "Silence all game audio.", "false", 2,
       {{"false", "Off"}, {"true", "On"}}},
      {"lollipop_camera_invert_y", "Camera up / down", "Vertical camera direction.", "false", 3,
       {{"false", "Normal"}, {"true", "Inverted"}}},
      {"lollipop_camera_invert_x", "Camera left / right", "Horizontal camera direction.", "false", 3,
       {{"false", "Normal"}, {"true", "Inverted"}}},
      {"lollipop_manual_aim", "Blaster aim", "Automatic targeting or manual aiming.", "false", 3,
       {{"false", "Auto"}, {"true", "Manual"}}},
  };
  return settings;
}

pc_values read_pc_settings() {
  pc_values values;
  for (const auto& setting : pc_settings())
    values[setting.name] = rex::cvar::GetFlagByName(setting.name);
  values["window_height"] = rex::cvar::GetFlagByName("window_height");
  return values;
}

bool validate_pc_settings(const pc_values& values) {
  if (values.size() != pc_settings().size() + 1) return false;
  for (const auto& setting : pc_settings()) {
    const auto it = values.find(setting.name);
    if (it == values.end()) return false;
    if (setting.choices.empty()) {
      float number = 0;
      const auto& text = it->second;
      const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), number);
      if (error != std::errc{} || end != text.data() + text.size() ||
          !std::isfinite(number) || number < setting.minimum || number > setting.maximum)
        return false;
      if (setting.step >= 1) {
        int integer;
        const auto [last, result] = std::from_chars(text.data(), text.data() + text.size(), integer);
        if (result != std::errc{} || last != text.data() + text.size()) return false;
      }
    } else if (std::ranges::none_of(setting.choices, [&](const auto& choice) {
                 return it->second == choice.value;
               })) {
      // Preserve custom window sizes and renderer values selected in advanced settings.
      const std::string_view name = setting.name;
      const auto& text = it->second;
      int number = 0;
      const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), number);
      if (error != std::errc{} || end != text.data() + text.size()) return false;
      if (name == "window_width") { if (number < 640 || number > 8192) return false; }
      else if (name == "resolution_scale") { if (number < 1 || number > 8) return false; }
      else if (name == "anisotropic_override") { if (number < -1 || number > 5) return false; }
      else return false;
    }
  }
  const auto height = values.find("window_height");
  if (height == values.end()) return false;
  int number = 0;
  const auto& text = height->second;
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), number);
  return error == std::errc{} && end == text.data() + text.size() && number >= 480 && number <= 8192;
}

bool save_pc_settings(const std::filesystem::path& path, const pc_values& values,
                      std::string& error) {
  if (!validate_pc_settings(values)) {
    error = "Invalid settings. Nothing was saved.";
    return false;
  }
  try {
    std::filesystem::create_directories(path.parent_path());
    auto temporary = path;
    temporary += ".tmp";
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    file << "# Lollipop Chainsaw PC settings\n";
    for (const auto& [name, value] : values)
      file << name << " = " << std::quoted(value) << '\n';
    file.close();
    if (!file || !MoveFileExW(temporary.c_str(), path.c_str(),
                             MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
      error = "Could not save settings. Check that your user folder is writable.";
      return false;
    }
    return true;
  } catch (const std::exception&) {
    error = "Could not save settings. Check that your user folder is writable.";
    return false;
  }
}

void configure_pc_settings(const std::filesystem::path& path) {
  std::ifstream file(path);
  if (file) {
    auto values = read_pc_settings();
    if (values["window_width"] == "0") values["window_width"] = "2560";
    if (values["window_height"] == "0") values["window_height"] = "1440";
    std::string line;
    std::set<std::string> provided;
    bool valid = true;
    while (std::getline(file, line)) {
      std::istringstream words(line);
      words >> std::ws;
      if (words.eof() || words.peek() == '#') continue;
      std::string name, value;
      char equals = 0;
      if (!(words >> name >> equals >> std::quoted(value)) || equals != '=' ||
          !(words >> std::ws).eof()) { valid = false; break; }
      if (name == "lollipop_60fps") {
        if (value != "true" && value != "false") { valid = false; break; }
        name = "lollipop_fps";
        value = value == "true" ? "60" : "30";
      }
      if (!values.contains(name) || !provided.insert(name).second) { valid = false; break; }
      values[name] = value;
    }
    if (valid && file.eof() && validate_pc_settings(values)) {
      for (const auto& [name, value] : values)
        if (provided.contains(name) && rex::cvar::GetFlagSource(name) <= rex::cvar::Source::kConfig)
          rex::cvar::SetFlagByName(name, value);
    } else {
      REXLOG_WARN("Ignoring invalid PC settings profile: {}", path.string());
    }
  }
  for (const auto& [name, value] : pc_values{{"window_width", "2560"},
                                             {"window_height", "1440"},
                                             {"resolution", "720p"},
                                             {"resolution_scale", "2"},
                                             {"anisotropic_override", "5"},
                                             {"swap_post_effect", "none"},
                                             {"render_target_path_d3d12", "rov"},
                                             {"async_shader_compilation", "false"}}) {
    if (rex::cvar::GetFlagSource(name) == rex::cvar::Source::kDefault)
      rex::cvar::SetFlagByName(name, value);
  }
}

}  // namespace lollipop
