#include "app/application.h"

#include <chrono>
#include <fstream>
#include <future>
#include <stdexcept>
#include <windows.h>

#include <rex/app/title_app.h>
#include <rex/gpu/graphics_system.h>
#include <rex/hid/input_system.h>
#include <rex/kernel/title_paths.h>
#include <rex/path_settings.h>
#include <rex/platform/file.h>
#include <rex/platform/perf.h>
#include <rex/string/text.h>
#include <rex/ui/ui_system.h>

#include "game/game_menus.h"
#include "game/shader_inventory.h"
#include "game/startup_hooks.h"
#include "game/timing_hooks.h"
#include "input/keyboard_controls.h"
#include "input/live_prompts.h"
#include "input/prompt_artwork.h"
#include "lollipop_init.h"
#include "settings/pc_settings.h"
#include "settings/runtime_settings.h"
#include "testing/test_bridge.h"
#include "ui/cheats_menu.h"
#include "ui/settings_menu.h"
#include "ui/shader_loading.h"

namespace lollipop {
namespace {
class lollipop_app final : public rex::app::TitleApp {
public:
  lollipop_app() : TitleApp("lollipop", kPPCImageConfig) {}

protected:
  rex::Result<void> OnConfigure(rex::SettingsRegistry& registry) override {
    settings::register_settings();
    constexpr auto layer = rex::SettingLayer::kTitle;
    // The graphics driver freezes its selection before OnInit.
    REX_TRY_VOID(registry.SetText(layer, "gpu.d3d12.render_target_path", "rov"));
    // NVIDIA's bindless path can hang during junkyard texture-cache churn.
    REX_TRY_VOID(registry.SetText(layer, "gpu.d3d12.bindless", "false"));
    // Preserve paths used by builds launched directly before the SDK upgrade.
    auto& paths = REX_PATHS();
    REX_TRY_VOID(paths.user_root.SetText(
        layer, rex::string::PathToUTF8(rex::platform::GetUserFolder() / "lollipop")));
    REX_TRY_VOID(paths.cache_root.SetText(layer, "{app.user_root}/cache"));
    REX_TRY_VOID(paths.config_file.SetText(
        layer, rex::string::PathToUTF8(rex::platform::GetExecutableFolder() / "lollipop.toml")));
    return registry.SetText(rex::SettingLayer::kTitle, "ui.bind.settings", "");
  }

  rex::Result<void> OnInit() override {
    auto& ui = GetUI();
    if (settings::Query<bool>("window_hidden")) {
      if (auto window = ui.GetNativeWindow(); window)
        ShowWindow(static_cast<HWND>(window->window), SW_HIDE);
    }
    const auto config = REX_PATHS().user_root.Get() / "pc-settings.toml";
    configure_pc_settings(config);
    configure_frame_rate();
    configure_keyboard_controls();
    configure_settings_fonts(&ui.GetFonts());
    register_cheats_hotkey(ui);
    ui.AddDialog(make_settings_menu(GetInput(), config,
                                    GetTitleRuntime().GetTitlePaths().game_root.Get(),
                                    [this] { RequestShutdown(); }));
    ui.AddDialog(make_cheats_menu(GetInput()));
    return rex::Ok();
  }

  rex::Result<void> OnTitleLaunch(rex::kernel::TitleRuntime& title) override {
    try {
      const auto game = title.GetTitlePaths().game_root.Get();
      const auto cache = REX_PATHS().cache_root.Get();
      install_game_ui_assets(title, game, cache);
      install_live_prompt_updates(title);
      set_game_ui_memory(title.GetMembase().GetBase());
      rex::platform::SetCSVLogPath(settings::GetFlagByName("perf_log_csv"));
      prefetch_recompiled_code();

      const bool prepare_only = settings::Query<bool>("lollipop_precompile_only");
      const bool can_prepare = GetGraphics().GetDriverName() == "d3d12" &&
                               settings::Query<bool>("store_shaders") && !cache.empty() &&
                               settings::GetFlagByName("render_target_path_d3d12") == "rov";
      if (prepare_only && !can_prepare)
        throw std::runtime_error("Shader preparation requires a cache directory and the D3D12 ROV "
                                 "renderer with shader storage enabled.");
      if (can_prepare) {
        REX_TRY_VOID(prepare_shaders(title, game, cache));
        if (prepare_only)
          RequestShutdown();
      }
      const auto directory = settings::GetFlagByName("lollipop_test_directory");
      if (!directory.empty() && !rex::app::IsShutdownRequested())
        test_worker_ = start_test_bridge(GetInput(), GetUI(), rex::string::to_path(directory),
                                         [this] { RequestShutdown(); });
      return rex::Ok();
    } catch (const std::exception& ex) {
      return rex::Err(rex::ErrorCategory::kRuntime, ex.what());
    }
  }

  void ReportError(std::string_view message) override {
    if (!settings::Query<bool>("lollipop_precompile_only")) {
      TitleApp::ReportError(message);
      return;
    }
    App::ReportError(message);
    const auto path = settings::GetFlagByName("lollipop_shader_progress");
    if (!path.empty())
      std::ofstream(rex::string::to_path(path), std::ios::trunc) << "0 0 1 Failed\n";
  }

  void OnTitleExit(rex::kernel::TitleExitReason) override { RequestShutdown(); }

  void OnShutdown() override {
    test_worker_ = {};
    stop_live_prompt_updates();
    set_game_ui_memory(nullptr);
    rex::gpu::SetTextureDataCallback({});
    rex::platform::CloseCSV();
  }

private:
  rex::Result<void> prepare_shaders(rex::kernel::TitleRuntime& title,
                                    const std::filesystem::path& game,
                                    const std::filesystem::path& cache) {
    auto state = std::make_shared<shader_loading_state>();
    auto dialog = make_shader_loading(state, [this] { RequestShutdown(); });
    auto visible = GetUI().AddDialog(*dialog);
    const auto progress = settings::GetFlagByName("lollipop_shader_progress");
    auto last = std::chrono::steady_clock::time_point{};
    auto report = [&](std::string_view stage, uint32_t completed, uint32_t total,
                      uint32_t failed = 0) {
      std::lock_guard lock(state->mutex);
      if (stage == state->stage && completed < state->completed)
        return;
      state->stage = stage;
      state->completed = completed;
      state->total = total;
      state->failed = std::max(state->failed, failed);
      if (stage == "Complete")
        state->compiled = true;
      const auto now = std::chrono::steady_clock::now();
      if (!progress.empty() &&
          (completed == total || now - last > std::chrono::milliseconds(100))) {
        std::ofstream(rex::string::to_path(progress), std::ios::trunc)
            << completed << ' ' << total << ' ' << state->failed << ' ' << stage << '\n';
        last = now;
      }
    };
    auto& gpu = GetGraphics();
    gpu.SetShaderPrecompile(true, [&](auto stage, auto completed, auto total, auto failed) {
      using enum rex::gpu::ShaderPrecompileStage;
      report(stage == kShaders     ? "Compiling shader variants"
             : stage == kPipelines ? "Creating graphics pipelines"
                                   : "Complete",
             completed, total, failed);
    });
    rex::ScopeGuard reset([&] { gpu.SetShaderPrecompile(false, {}); });
    REX_TRY_VOID(title.AttachForLaunch());
    auto worker = std::async(std::launch::async, [&] {
      auto resource = FindResourceA(nullptr, MAKEINTRESOURCEA(104), RT_RCDATA);
      auto data = resource ? LoadResource(nullptr, resource) : nullptr;
      if (!data)
        throw std::runtime_error("The pipeline inventory is missing from this build.");
      std::string_view inventory(static_cast<const char*>(LockResource(data)),
                                 SizeofResource(nullptr, resource));
      prepare_shader_inventory(game, cache, inventory, report,
                               {title.GetMembase().TranslateVirtual(kPPCImageConfig.image_base),
                                kPPCImageConfig.image_size});
      auto prepared = gpu.PrepareShaderStorage();
      if (!prepared)
        throw std::runtime_error(prepared.error().GetDescription());
    });
    while (worker.wait_for(std::chrono::milliseconds(2)) != std::future_status::ready &&
           RunFrame()) {
    }
    try {
      worker.get();
      if (!state->compiled || state->failed)
        throw std::runtime_error("Some shader pipelines could not be prepared.");
    } catch (const std::exception& e) {
      report("Failed", 0, 0, 1);
      return rex::Err(rex::ErrorCategory::kRuntime, e.what());
    }
    return rex::Ok();
  }
  std::jthread test_worker_;
};
} // namespace

int run_application(const rex::CLIArgList& arguments) {
  lollipop_app app;
  if (app.Init(arguments) && app.LaunchTitle())
    app.Run();
  return app.Shutdown();
}
} // namespace lollipop
