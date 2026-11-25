#include "runtime_settings.h"
#include <stdexcept>
#include <utility>

namespace lollipop::settings {
namespace {
struct game_options final : rex::SettingOwner {
  game_options() : SettingOwner("lollipop") {}
  REX_SETTING(bool, cheat_health, false, "Unlimited health");
  REX_SETTING(bool, cheat_gold, false, "Unlimited gold medals");
  REX_SETTING(bool, cheat_platinum, false, "Unlimited platinum medals");
  REX_SETTING(bool, cheat_soul, false, "Keep Star Soul gauge full");
  REX_SETTING(int32_t, music_volume, 10, "Music volume, 0 to 10", rex::Range{0, 10});
  REX_SETTING(int32_t, sfx_volume, 10, "Sound effects volume, 0 to 10", rex::Range{0, 10});
  REX_SETTING(bool, subtitles, false, "Subtitles");
  REX_SETTING(bool, camera_invert_y, false, "Invert vertical camera");
  REX_SETTING(bool, camera_invert_x, false, "Invert horizontal camera");
  REX_SETTING(bool, manual_aim, false, "Manual blaster aim");
  REX_SETTING(bool, film_grain, true, "Shows the original grain in rendered scenes.");
  REX_SETTING(bool, depth_of_field, true, "Blurs near or distant objects to simulate camera focus.");
  REX_SETTING(bool, vignette, true, "Darkens the edges of the gameplay picture.");
  REX_SETTING(int32_t, shadow_resolution, 0,
              "Character shadow resolution: 0 = original, 256, 512 or 1024",
              rex::Allowed{"0", "256", "512", "1024"});
  REX_SETTING(bool, legacy_60fps, true, "Legacy 30/60 FPS switch; use lollipop_fps instead",
              rex::kRestart);
  REX_SETTING(int32_t, fps, 60, "Frame limit: 30, 60, 90 or 120 FPS",
              rex::Allowed{"30", "60", "90", "120"});
  REX_SETTING(std::string, tick_trace_csv, "", "Opt-in candidate engine Tick timing log",
              rex::kStartupOnly);
  REX_SETTING(std::string, test_directory, "", "Opt-in local console and controller test directory",
              rex::kStartupOnly);
  REX_SETTING(bool, precompile_only, false, "Prepare shaders without starting the game",
              rex::kStartupOnly);
  REX_SETTING(std::string, shader_progress, "", "Shader preparation progress file",
              rex::kStartupOnly);
  REX_SETTING(int32_t, resolution_scale, 2, "Draw resolution scale on both axes", rex::Range{1, 8});
  REX_SETTING(bool, window_hidden, false, "Start the window hidden");
  REX_SETTING(bool, pc_settings, false, "Open PC settings");
  REX_SETTING(bool, cheats_menu, false, "Open the cheats menu");
  REX_SETTING(std::string, perf_log_csv, "", "Frame counter CSV path");
};
constexpr std::pair<std::string_view, std::string_view> aliases[] = {
    {"anisotropic_override", "gpu.anisotropic_override"},
    {"async_shader_compilation", "gpu.xenos.async_shader_compilation"},
    {"audio_mute", "apu.mute"},
    {"bind_cheats", "ui.bind.cheats"},
    {"d3d12_debug", "gpu.d3d12.debug"},
    {"d3d12_pipeline_collection", "gpu.d3d12.pipeline_collection"},
    {"d3d12_pipeline_creation_threads", "gpu.d3d12.pipeline_creation_threads"},
    {"frame_limit", "gpu.frame_limit"},
    {"fullscreen", "ui.fullscreen"},
    {"gpu_debug_markers", "gpu.d3d12.debug_markers"},
    {"left_stick_deadzone_percentage", "hid.left_stick_deadzone"},
    {"lollipop_60fps", "lollipop.legacy_60fps"},
    {"lollipop_gpu_backend", "gpu.driver"},
    {"mnk_gameplay_modifiers", "hid.mnk_gameplay_modifiers"},
    {"mnk_mode", "hid.mnk_mode"},
    {"mnk_mouse", "hid.mnk_mouse"},
    {"mnk_mouse_deadzone", "hid.mnk_mouse_deadzone"},
    {"mnk_sensitivity", "hid.mnk_sensitivity"},
    {"perf_log_csv", "lollipop.perf_log_csv"},
    {"present_dither", "gpu.xenos.present_dither"},
    {"present_letterbox", "gpu.xenos.present_letterbox"},
    {"readback_memexport", "gpu.xenos.readback_memexport"},
    {"readback_memexport_fast", "gpu.xenos.readback_memexport_fast"},
    {"render_target_path_d3d12", "gpu.d3d12.render_target_path"},
    {"resolution", "ui.resolution"},
    {"resolution_scale", "gpu.resolution_scale_x"},
    {"resolution_scale_x", "gpu.resolution_scale_x"},
    {"resolution_scale_y", "gpu.resolution_scale_y"},
    {"right_stick_deadzone_percentage", "hid.right_stick_deadzone"},
    {"store_shaders", "gpu.xenos.store_shaders"},
    {"swap_post_effect", "gpu.xenos.swap_post_effect"},
    {"vibration", "hid.vibration"},
    {"video_mode_refresh_rate", "ui.video_mode_refresh_rate"},
    {"vsync", "gpu.vsync"},
    {"window_height", "ui.window_height"},
    {"window_hidden", "lollipop.window_hidden"},
    {"window_width", "ui.window_width"},
};
} // namespace

void register_settings() {
  static game_options options;
  static const bool registered = [] {
    auto& registry = REX_SETTINGS();
    auto result = registry.Register(options);
    if (!result)
      throw std::runtime_error(result.error().GetDescription());
    for (auto* option : options.GetOwnedSettings()) {
      const auto& name = option->GetName();
      registry.AddAlias("lollipop_" + name.substr(9), name);
    }
    for (auto [old_name, name] : aliases)
      registry.AddAlias(old_name, name);
    // Old config files and launch arguments used a single scale for both axes.
    registry.AddAlias("resolution_scale", "lollipop.resolution_scale");
    return true;
  }();
  (void)registered;
}

std::string setting_name(std::string_view name) {
  for (auto [old_name, full_name] : aliases)
    if (name == old_name)
      return std::string(full_name);
  if (name.starts_with("keybind_"))
    return "hid." + std::string(name);
  if (name.starts_with("lollipop_"))
    return "lollipop." + std::string(name.substr(9));
  return std::string(name);
}

rex::SettingBase* GetFlagInfo(std::string_view name) {
  register_settings();
  return REX_SETTINGS().Find(setting_name(name)).value_or(nullptr);
}

std::string GetFlagByName(std::string_view name) {
  auto* setting = GetFlagInfo(name);
  return setting ? setting->GetText() : std::string{};
}

Source GetFlagSource(std::string_view name) {
  auto* setting = GetFlagInfo(name);
  return setting ? setting->GetLayer() : Source::kDefault;
}

bool SetFlagByName(std::string_view name, std::string_view value) {
  register_settings();
  auto& registry = REX_SETTINGS();
  if (name == "resolution_scale" && !registry.SetText("gpu.resolution_scale_y", value))
    return false;
  return registry.SetText(setting_name(name), value).has_value();
}

bool SetFlagFromCommandLine(std::string_view name, std::string_view value) {
  register_settings();
  return REX_SETTINGS().SetText(Source::kCommandLine, setting_name(name), value).has_value();
}
} // namespace lollipop::settings
