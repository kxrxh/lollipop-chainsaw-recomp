#include "settings/pc_settings.h"

#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <rex/cvar.h>

REXCVAR_DEFINE_BOOL(async_shader_compilation, true, "GPU", "Test renderer default");
REXCVAR_DEFINE_STRING(render_target_path_d3d12, "", "GPU", "Test renderer selection");

static void check(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}

using lollipop::pc_settings;
using lollipop::pc_values;
using lollipop::validate_pc_settings;
using lollipop::save_pc_settings;
using lollipop::configure_pc_settings;

int main(int argc, char** argv) try {
  check(argc == 2 || argc == 3, "Expected test config path");
  // GPU cvars live in a plugin; register only the missing ones for this storage test.
  for (const auto& setting : pc_settings()) {
    if (rex::cvar::GetFlagInfo(setting.name)) continue;
    auto value = std::make_shared<std::string>(setting.recommended);
    rex::cvar::RegisterFlag({.name = setting.name, .type = rex::cvar::FlagType::String,
        .setter = [value](std::string_view text) { *value = text; return true; },
        .getter = [value] { return *value; }, .default_value = *value});
  }
  pc_values values;
  for (const auto& setting : pc_settings()) values[setting.name] = setting.recommended;
  values["window_height"] = "1440";
  check(validate_pc_settings(values), "Recommended profile validates");
  const std::filesystem::path path = argv[1];
  if (argc == 3 && std::string_view(argv[2]) == "--launcher-profile") {
    configure_pc_settings(path);
    const pc_values expected{
        {"window_width", "1920"},     {"window_height", "1080"},      {"fullscreen", "false"},
        {"vsync", "false"},           {"present_letterbox", "false"}, {"resolution_scale", "3"},
        {"swap_post_effect", "fxaa"}, {"anisotropic_override", "4"},  {"lollipop_fps", "90"},
        {"present_dither", "true"},   {"lollipop_music_volume", "6"}};
    for (const auto& [name, value] : expected)
      check(rex::cvar::GetFlagByName(name) == value,
            ("Launcher setting not applied: " + name + " = " + rex::cvar::GetFlagByName(name))
                .c_str());
    check(std::stof(rex::cvar::GetFlagByName("mnk_sensitivity")) == 22.5f,
          "Launcher preserves mouse sensitivity");
    check(rex::cvar::GetFlagSource("lollipop_subtitles") == rex::cvar::Source::kDefault,
          "Launcher leaves missing native settings eligible for import");
    return 0;
  }
  if (argc == 3 && (std::string_view(argv[2]) == "--shader-default" ||
                    std::string_view(argv[2]) == "--shader-cli")) {
    const bool explicit_async = std::string_view(argv[2]) == "--shader-cli";
    if (explicit_async)
      rex::cvar::SetFlagFromCommandLine("async_shader_compilation", "true");
    if (explicit_async)
      rex::cvar::SetFlagFromCommandLine("render_target_path_d3d12", "rtv");
    configure_pc_settings(path);
    check(rex::cvar::Query<bool>("async_shader_compilation") == explicit_async,
          "Wait for uncached shaders by default; explicit diagnostic launch flag wins");
    check(rex::cvar::GetFlagByName("render_target_path_d3d12") == (explicit_async ? "rtv" : "rov"),
          "Direct launches use the precompilable renderer; explicit diagnostic selection wins");
    return 0;
  }
  if (argc == 3) {
    std::filesystem::create_directories(path.parent_path());
    const bool duplicate = std::string_view(argv[2]) == "--duplicate";
    const bool legacy60 = std::string_view(argv[2]) == "--legacy-60";
    const bool precedence = std::string_view(argv[2]) == "--fps-precedence";
    if (precedence)
      rex::cvar::SetFlagFromCommandLine("lollipop_fps", "120");
    {
      std::ofstream file(path);
      file << "vsync = \"false\"\n";
      if (duplicate) file << "lollipop_music_volume = \"6\"\nlollipop_music_volume = \"4\"\n";
      else file << "lollipop_60fps = \"" << (legacy60 ? "true" : "false") << "\"\n";
    }
    configure_pc_settings(path);
    check(rex::cvar::GetFlagByName("vsync") == (duplicate ? "true" : "false") &&
          rex::cvar::GetFlagByName("lollipop_music_volume") == "10" &&
          rex::cvar::GetFlagSource("lollipop_music_volume") == rex::cvar::Source::kDefault,
          "Missing game settings stay eligible for native import; duplicate profiles cannot partially apply");
    check(rex::cvar::GetFlagByName("lollipop_fps") == (precedence ? "120" : legacy60 || duplicate ? "60" : "30"),
          "Legacy FPS profiles migrate; explicit FPS launch flag wins");
    return 0;
  }
  std::string error;
  check(save_pc_settings(path, values, error), "Can write settings");
  for (const char* fps : {"30", "60", "90", "120"}) {
    auto selected = values;
    selected["lollipop_fps"] = fps;
    check(save_pc_settings(path, selected, error), "All frame limits can be saved");
  }
  check(save_pc_settings(path, values, error), "Restore recommended profile");
  const std::pair<const char*, const char*> invalid_values[] = {
      {"mnk_sensitivity", "nan"}, {"mnk_sensitivity", "51"},
      {"lollipop_music_volume", "11"}, {"lollipop_sfx_volume", "-1"},
      {"lollipop_music_volume", "5.5"}, {"lollipop_subtitles", "maybe"},
      {"lollipop_fps", "45"}, {"lollipop_fps", "121"}, {"lollipop_fps", "90.5"},
      {"swap_post_effect", "dlss"}, {"window_height", "-1"}, {"vsync", "true\nsecret = 1"}};
  for (const auto& [name, bad] : invalid_values) {
    auto invalid = values;
    invalid[name] = bad;
    check(!save_pc_settings(path, invalid, error), "Reject invalid input before saving");
  }
  auto unknown = values;
  unknown["game_data_root"] = "unexpected";
  check(!validate_pc_settings(unknown), "Only curated settings can be saved");
  rex::cvar::SetFlagFromCommandLine("window_width", "1920");
  configure_pc_settings(path);
  check(rex::cvar::GetFlagByName("window_width") == "1920", "Explicit launch flag wins");
  check(rex::cvar::GetFlagByName("swap_post_effect") == "fxaa", "Graphics profile round trips");
  check(rex::cvar::Query<int32_t>("window_height") == 1440, "Invalid writes preserved the last good file");
  {
    std::ofstream file(path);
    file << "audio_mute = \"true\"\nwindow_height = \"-1\"\n";
  }
  const auto mute = rex::cvar::GetFlagByName("audio_mute");
  configure_pc_settings(path);
  check(rex::cvar::GetFlagByName("audio_mute") == mute, "Malformed profile cannot partially apply");
  check(rex::cvar::Query<int32_t>("window_height") == 1440, "Malformed profile leaves valid settings intact");
} catch (const std::exception& error) {
  std::cerr << error.what() << '\n';
  return 1;
}
