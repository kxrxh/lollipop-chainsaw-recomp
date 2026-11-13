#include "application.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <windows.h>

#include "lollipop_init.h"

#include <rex/cvar.h>
#include <rex/input/input_system.h>
#include <rex/perf/counter.h>
#include <rex/rex_app.h>
#include <rex/system/gpu_plugin.h>
#include <rex/system/interfaces/graphics.h>

#include "game/game_menus.h"
#include "game/shader_inventory.h"
#include "game/timing_hooks.h"
#include "input/keyboard_controls.h"
#include "input/live_prompts.h"
#include "input/prompt_artwork.h"
#include "settings/pc_settings.h"
#include "testing/test_bridge.h"
#include "ui/settings_menu.h"
#include "ui/shader_loading.h"
#include <rex/ui/keybinds.h>

REXCVAR_DEFINE_STRING(lollipop_test_directory, "", "Lollipop",
                      "Opt-in local console and controller test directory")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

REXCVAR_DEFINE_STRING(lollipop_gpu_backend, "any", "Lollipop", "GPU backend")
    .allowed({"any", "d3d12", "vulkan"})
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

REXCVAR_DEFINE_BOOL(lollipop_precompile_only, false, "Lollipop",
                    "Prepare shaders without starting the game")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_STRING(lollipop_shader_progress, "", "Lollipop", "Shader preparation progress file")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace lollipop {
namespace {

class lollipop_app final : public rex::ReXApp {
public:
  explicit lollipop_app(rex::ui::WindowedAppContext& context)
      : ReXApp(context, "lollipop", PPCImageConfig) {}

protected:
  bool ShouldInitializeShaderStorage() const override { return !shaders_prepared_; }

  void LaunchModule() override {
    if ((rex::cvar::GetFlagByName("render_target_path_d3d12") != "rov" ||
         !rex::cvar::Query<bool>("store_shaders")) &&
        !REXCVAR_GET(lollipop_precompile_only)) {
      ReXApp::LaunchModule();
      return;
    }
    loading_ = std::make_shared<shader_loading_state>();
    loading_->start_game = !REXCVAR_GET(lollipop_precompile_only);
    loading_screen_ =
        make_shader_loading(*imgui_drawer(), loading_, [this] { window()->RequestClose(); });
    auto report = [this](std::string_view stage, uint32_t completed, uint32_t total) {
      std::lock_guard lock(loading_->mutex);
      if (loading_->stage == stage && completed < loading_->completed)
        return;
      loading_->stage = stage;
      loading_->completed = completed;
      loading_->total = total;
      const auto& path = REXCVAR_GET(lollipop_shader_progress);
      const auto now = std::chrono::steady_clock::now();
      if (!path.empty() &&
          (completed == total || now - last_progress_ > std::chrono::milliseconds(100))) {
        std::ofstream file(path, std::ios::trunc);
        file << completed << ' ' << total << ' ' << loading_->failed << ' ' << stage << '\n';
        last_progress_ = now;
      }
    };
    rex::system::SetShaderPrecompileCallback(
        [this, report](auto stage, auto completed, auto total, auto failed) {
          {
            std::lock_guard lock(loading_->mutex);
            loading_->failed = std::max(loading_->failed, failed);
            if (stage == rex::system::ShaderPrecompileStage::kComplete)
              loading_->compiled = true;
          }
          report(stage == rex::system::ShaderPrecompileStage::kShaders ? "Compiling shader variants"
                 : stage == rex::system::ShaderPrecompileStage::kPipelines
                     ? "Creating graphics pipelines"
                     : "Complete",
                 completed, total);
        });
    shader_worker_ = std::jthread([this, report] {
      try {
        if (!rex::cvar::Query<bool>("store_shaders"))
          throw std::runtime_error("Shader preparation requires --store_shaders=true.");
        if (rex::cvar::GetFlagByName("render_target_path_d3d12") != "rov")
          throw std::runtime_error("Shader preparation currently requires the D3D12 ROV renderer.");
        const auto resource = FindResourceA(nullptr, MAKEINTRESOURCEA(104), RT_RCDATA);
        const auto data = resource ? LoadResource(nullptr, resource) : nullptr;
        if (!data)
          throw std::runtime_error("The pipeline inventory is missing from this build.");
        const auto inventory = std::string_view(static_cast<const char*>(LockResource(data)),
                                                SizeofResource(nullptr, resource));
        prepare_shader_inventory(
            game_data_root(), cache_root(), inventory, report,
            {runtime()->virtual_membase() + PPCImageConfig.image_base, PPCImageConfig.image_size});
        runtime()->graphics_system()->InitializeShaderStorage(cache_root(), 0x565707D0, true);
        {
          std::lock_guard lock(loading_->mutex);
          if (!loading_->compiled || loading_->failed)
            throw std::runtime_error(
                "Some shader pipelines could not be prepared. Check the runtime log.");
        }
        rex::system::SetShaderPrecompileCallback({});
        app_context().CallInUIThreadDeferred([this] {
          if (REXCVAR_GET(lollipop_precompile_only)) {
            window()->RequestClose();
            return;
          }
          {
            std::lock_guard lock(loading_->mutex);
            loading_->visible = false;
          }
          shaders_prepared_ = true;
          ReXApp::LaunchModule();
        });
      } catch (const std::exception& error) {
        REXLOG_ERROR("Shader preparation failed: {}", error.what());
        rex::system::SetShaderPrecompileCallback({});
        {
          std::lock_guard lock(loading_->mutex);
          loading_->error = error.what();
          loading_->failed = std::max(1u, loading_->failed);
        }
        report("Failed", 0, 0);
        if (REXCVAR_GET(lollipop_precompile_only))
          app_context().CallInUIThreadDeferred([this] { window()->RequestClose(); });
      }
    });
  }
  void OnConfigurePaths(rex::PathConfig& paths) override {
    settings_path_ = paths.user_data_root / "pc-settings.toml";
  }

  void OnConfigureFonts(ImFontAtlas* atlas) override { configure_settings_fonts(atlas); }

  void OnKeyDown(rex::ui::KeyEvent& event) override {
    if (loading_) {
      std::lock_guard lock(loading_->mutex);
      if (loading_->visible) {
        event.set_handled(true);
        return;
      }
    }
    if (!event.prev_state())
      rex::ui::ProcessKeyEvent(event);
  }

  void OnCreateDialogs(rex::ui::ImGuiDrawer*) override { rex::ui::UnregisterBind("bind_settings"); }

  void OnLoadXexImage(std::string&) override {
    install_game_ui_assets(*runtime(), game_data_root(), cache_root());
    install_live_prompt_updates(*runtime());
  }

  void OnPostSetup() override {
    set_game_ui_memory(runtime()->virtual_membase());
    settings_menu_ =
        make_settings_menu(*imgui_drawer(), *immediate_drawer(), *runtime(), settings_path_,
                           game_data_root(), [this] { window()->RequestClose(); });
    rex::perf::SetCsvLogPath(rex::cvar::GetFlagByName("perf_log_csv"));
    const auto& directory = REXCVAR_GET(lollipop_test_directory);
    if (!directory.empty()) {
      auto* input = dynamic_cast<rex::input::InputSystem*>(runtime()->input_system());
      if (!input)
        throw std::runtime_error("Test bridge needs the SDK input system");
      auto* presenter = runtime()->graphics_system()->presenter();
      if (!presenter || !imgui_drawer())
        throw std::runtime_error("Test bridge needs presentation");
      test_worker_ = start_test_bridge(*input, app_context(), directory, *imgui_drawer(),
                                       *presenter, *window());
    }
  }

  void OnShutdown() override {
    if (shader_worker_.joinable())
      shader_worker_.join();
    rex::system::SetShaderPrecompileCallback({});
    loading_screen_.reset();
    stop_live_prompt_updates();
    set_game_ui_memory(nullptr);
    settings_menu_.reset();
    rex::system::SetTextureDataCallback({});
    if (test_worker_.joinable()) {
      test_worker_.request_stop();
      test_worker_.join();
    }
  }

  void OnPreSetup(rex::RuntimeConfig& config) override {
    config.gpu_plugin = "xenos";
    config.graphics =
        rex::system::LoadGpuPlugin(config.gpu_plugin, REXCVAR_GET(lollipop_gpu_backend));
    if (!config.graphics)
      throw std::runtime_error("Could not load the requested GPU backend");
    configure_pc_settings(settings_path_);
    configure_frame_rate();
    configure_keyboard_controls();
  }

private:
  bool shaders_prepared_ = false;
  std::shared_ptr<shader_loading_state> loading_;
  std::unique_ptr<rex::ui::ImGuiDialog> loading_screen_;
  std::jthread shader_worker_;
  std::chrono::steady_clock::time_point last_progress_{};
  std::filesystem::path settings_path_;
  std::unique_ptr<rex::ui::ImGuiDialog> settings_menu_;
  std::jthread test_worker_;
};

} // namespace

std::unique_ptr<rex::ui::WindowedApp> create_application(rex::ui::WindowedAppContext& context) {
  return std::make_unique<lollipop_app>(context);
}

} // namespace lollipop
