#include "settings/pc_settings.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>

#include <rex/path_settings.h>

#include "settings/runtime_settings.h"

#include "settings_fixture.h"

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
  lollipop::testing::settings_fixture fixture;
  fixture.add("async_shader_compilation", "true");
  fixture.add("render_target_path_d3d12", "");
  fixture.add("window_height", "720");
  fixture.add("resolution_scale_y", "1");
  for (const auto& option : pc_settings())
    fixture.add(option.name, option.recommended);
  pc_values values;
  for (const auto& setting : pc_settings()) values[setting.name] = setting.recommended;
  values["window_height"] = "1440";
  check(validate_pc_settings(values), "Recommended profile validates");
  check(values.contains("lollipop_shadow_resolution") &&
            values.at("lollipop_shadow_resolution") == "0",
        "Existing profiles retain original shadow resolution by default");
  for (const auto* name : {"lollipop_film_grain", "lollipop_depth_of_field", "lollipop_vignette"})
    check(values.at(name) == "true" && lollipop::settings::Query<bool>(name),
          "Missing effect settings preserve the original rendering");
  const std::filesystem::path path = argv[1];
  if (argc == 3 && std::string_view(argv[2]) == "--launcher-profile") {
    configure_pc_settings(path);
    const pc_values expected{{"window_width", "1920"},       {"window_height", "1080"},
                             {"fullscreen", "false"},        {"vsync", "false"},
                             {"present_letterbox", "false"}, {"resolution_scale", "3"},
                             {"swap_post_effect", "fxaa"},   {"anisotropic_override", "4"},
                             {"lollipop_fps", "90"},         {"present_dither", "true"},
                             {"lollipop_music_volume", "6"}, {"lollipop_shadow_resolution", "512"},
                             {"lollipop_film_grain", "false"}, {"lollipop_depth_of_field", "true"},
                             {"lollipop_vignette", "false"}};
    for (const auto& [name, value] : expected)
      check(lollipop::settings::GetFlagByName(name) == value,
            ("Launcher setting not applied: " + name + " = " +
             lollipop::settings::GetFlagByName(name))
                .c_str());
    check(std::stof(lollipop::settings::GetFlagByName("mnk_sensitivity")) == 22.5f,
          "Launcher preserves mouse sensitivity");
    check(lollipop::settings::GetFlagSource("lollipop_subtitles") ==
              lollipop::settings::Source::kDefault,
          "Launcher leaves missing native settings eligible for import");
    return 0;
  }
  if (argc == 3 && (std::string_view(argv[2]) == "--shader-default" ||
                    std::string_view(argv[2]) == "--shader-cli")) {
    const bool explicit_async = std::string_view(argv[2]) == "--shader-cli";
    if (explicit_async)
      lollipop::settings::SetFlagFromCommandLine("async_shader_compilation", "true");
    if (explicit_async)
      lollipop::settings::SetFlagFromCommandLine("render_target_path_d3d12", "rtv");
    configure_pc_settings(path);
    check(lollipop::settings::Query<bool>("async_shader_compilation") == explicit_async,
          "Wait for uncached shaders by default; explicit diagnostic launch flag wins");
    check(lollipop::settings::GetFlagByName("render_target_path_d3d12") ==
              (explicit_async ? "rtv" : "rov"),
          "Direct launches use the precompilable renderer; explicit diagnostic selection wins");
    return 0;
  }
  if (argc == 3) {
    std::filesystem::create_directories(path.parent_path());
    const bool duplicate = std::string_view(argv[2]) == "--duplicate";
    const bool legacy60 = std::string_view(argv[2]) == "--legacy-60";
    const bool precedence = std::string_view(argv[2]) == "--fps-precedence";
    if (precedence)
      lollipop::settings::SetFlagFromCommandLine("lollipop_fps", "120");
    {
      std::ofstream file(path);
      file << "vsync = \"false\"\n";
      if (duplicate) file << "lollipop_music_volume = \"6\"\nlollipop_music_volume = \"4\"\n";
      else file << "lollipop_60fps = \"" << (legacy60 ? "true" : "false") << "\"\n";
    }
    configure_pc_settings(path);
    check(lollipop::settings::GetFlagByName("vsync") == (duplicate ? "true" : "false") &&
              lollipop::settings::GetFlagByName("lollipop_music_volume") == "10" &&
              lollipop::settings::GetFlagSource("lollipop_music_volume") ==
                  lollipop::settings::Source::kDefault,
          "Missing game settings stay eligible for native import; duplicate profiles cannot "
          "partially apply");
    check(lollipop::settings::GetFlagByName("lollipop_fps") == (precedence              ? "120"
                                                                : legacy60 || duplicate ? "60"
                                                                                        : "30"),
          "Legacy FPS profiles migrate; explicit FPS launch flag wins");
    return 0;
  }
  auto& paths = REX_PATHS();
  using rex::SettingLayer;
  check(paths.user_root.SetText(SettingLayer::kTitle, "legacy user").has_value(),
        "Accept the legacy user directory default");
  check(paths.cache_root.SetText(SettingLayer::kTitle, "{app.user_root}/cache").has_value(),
        "Accept the legacy cache directory default");
  check(paths.user_root.SetText(SettingLayer::kCommandLine, "profile \xD0\x96").has_value(),
        "Accept a UTF-8 user directory from the command line");
  const std::filesystem::path unicode_user(u"profile \u0416");
  check(paths.user_root.Get() == unicode_user && paths.cache_root.Get() == unicode_user / "cache",
        "The explicit user directory and derived cache path round trip");

  std::string error;
  check(save_pc_settings(path, values, error), "Can write settings");
  for (const char* fps : {"30", "60", "90", "120"}) {
    auto selected = values;
    selected["lollipop_fps"] = fps;
    check(save_pc_settings(path, selected, error), "All frame limits can be saved");
  }
  for (const char* resolution : {"0", "256", "512", "1024"}) {
    auto selected = values;
    selected["lollipop_shadow_resolution"] = resolution;
    check(save_pc_settings(path, selected, error), "All shadow resolutions can be saved");
  }
  values["lollipop_shadow_resolution"] = "1024";
  values["lollipop_sfx_volume"] = "7";
  check(save_pc_settings(path, values, error), "Restore selected profile");
  const std::pair<const char*, const char*> invalid_values[] = {
      {"mnk_sensitivity", "nan"},
      {"mnk_sensitivity", "51"},
      {"lollipop_music_volume", "11"},
      {"lollipop_sfx_volume", "-1"},
      {"lollipop_music_volume", "5.5"},
      {"lollipop_subtitles", "maybe"},
      {"lollipop_fps", "45"},
      {"lollipop_fps", "121"},
      {"lollipop_fps", "90.5"},
      {"lollipop_shadow_resolution", "-1"},
      {"lollipop_shadow_resolution", "2048"},
      {"lollipop_shadow_resolution", "512.0"},
      {"lollipop_shadow_resolution", "300"},
      {"lollipop_film_grain", "2"},
      {"lollipop_depth_of_field", "2"},
      {"lollipop_vignette", "2"},
      {"swap_post_effect", "dlss"},
      {"window_height", "-1"},
      {"vsync", "true\nsecret = 1"}};
  for (const auto& [name, bad] : invalid_values) {
    auto invalid = values;
    invalid[name] = bad;
    check(!save_pc_settings(path, invalid, error), "Reject invalid input before saving");
  }
  auto unknown = values;
  unknown["game_data_root"] = "unexpected";
  check(!validate_pc_settings(unknown), "Only curated settings can be saved");
  {
    std::ofstream file(path, std::ios::app);
    file << "lollipop_gpu_backend = \"vulkan\"\n";
  }
  lollipop::settings::SetFlagFromCommandLine("window_width", "1920");
  (void)REX_SETTINGS().SetText(SettingLayer::kCommandLine, "resolution_scale", "3");
  lollipop::settings::SetFlagFromCommandLine("resolution_scale_y", "4");
  configure_pc_settings(path);
  check(lollipop::settings::GetFlagByName("resolution_scale_x") == "3" &&
            lollipop::settings::GetFlagByName("resolution_scale_y") == "4",
        "Legacy uniform scale overrides the profile and preserves an explicit per-axis value");
  check(lollipop::settings::GetFlagByName("window_width") == "1920", "Explicit launch flag wins");
  check(lollipop::settings::GetFlagByName("swap_post_effect") == "fxaa",
        "Graphics profile round trips");
  check(lollipop::settings::GetFlagByName("lollipop_shadow_resolution") == "1024",
        "Shadow resolution round trips");
  check(lollipop::settings::Query<int32_t>("window_height") == 1440,
        "Invalid writes preserved the last good file");
  check(lollipop::settings::GetFlagByName("lollipop_sfx_volume") == "7",
        "Retired renderer selection must not discard the remaining profile");
  check(save_pc_settings(path, lollipop::read_pc_settings(), error), "Save migrated profile");
  {
    std::ifstream file(path);
    const std::string saved((std::istreambuf_iterator<char>(file)), {});
    check(saved.find("lollipop_gpu_backend") == std::string::npos,
          "Saving settings removes the retired renderer selection");
  }
  {
    std::ofstream file(path);
    file << "audio_mute = \"true\"\nwindow_height = \"-1\"\n";
  }
  const auto mute = lollipop::settings::GetFlagByName("audio_mute");
  configure_pc_settings(path);
  check(lollipop::settings::GetFlagByName("audio_mute") == mute,
        "Malformed profile cannot partially apply");
  check(lollipop::settings::Query<int32_t>("window_height") == 1440,
        "Malformed profile leaves valid settings intact");
} catch (const std::exception& error) {
  std::cerr << error.what() << '\n';
  return 1;
}
