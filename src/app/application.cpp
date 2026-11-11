#include "application.h"
#include <memory>
#include <stdexcept>

#include "lollipop_init.h"

#include <rex/cvar.h>
#include <rex/input/input_system.h>
#include <rex/perf/counter.h>
#include <rex/rex_app.h>
#include <rex/system/gpu_plugin.h>
#include <rex/system/interfaces/graphics.h>

#include "game/timing_hooks.h"
#include "input/keyboard_controls.h"
#include "input/live_prompts.h"
#include "input/prompt_artwork.h"
#include "settings/pc_settings.h"
#include "testing/test_bridge.h"
#include "ui/settings_menu.h"
#include <rex/ui/keybinds.h>

REXCVAR_DEFINE_STRING(lollipop_test_directory, "", "Lollipop",
                      "Opt-in local console and controller test directory")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

REXCVAR_DEFINE_STRING(lollipop_gpu_backend, "any", "Lollipop", "GPU backend")
    .allowed({"any", "d3d12", "vulkan"})
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace lollipop {
namespace {

class lollipop_app final : public rex::ReXApp {
public:
  explicit lollipop_app(rex::ui::WindowedAppContext& context)
      : ReXApp(context, "lollipop", PPCImageConfig) {}

protected:
  void OnConfigurePaths(rex::PathConfig& paths) override {
    settings_path_ = paths.user_data_root / "pc-settings.toml";
  }

  void OnConfigureFonts(ImFontAtlas* atlas) override { configure_settings_fonts(atlas); }

  void OnKeyDown(rex::ui::KeyEvent& event) override {
    if (rex::cvar::Query<bool>("lollipop_pc_settings") &&
        event.virtual_key() ==
            rex::ui::ParseVirtualKey(rex::cvar::GetFlagByName("bind_settings"))) {
      event.set_handled(true);
      return;
    }
    if (!event.prev_state())
      rex::ui::ProcessKeyEvent(event);
  }

  void OnCreateDialogs(rex::ui::ImGuiDrawer*) override {
    if (rex::cvar::GetFlagSource("bind_settings") == rex::cvar::Source::kDefault)
      rex::cvar::SetFlagByName("bind_settings", "F6");
    rex::ui::RegisterBind("bind_pc_settings", "F4", "Toggle PC settings", [] {
      rex::cvar::SetFlagByName("lollipop_pc_settings",
                               rex::cvar::Query<bool>("lollipop_pc_settings") ? "false" : "true");
    });
  }

  void OnLoadXexImage(std::string&) override {
    install_keyboard_prompts(*runtime(), game_data_root(), cache_root());
    install_live_prompt_updates(*runtime());
  }

  void OnPostSetup() override {
    set_settings_guest_memory(runtime()->virtual_membase());
    settings_menu_ = make_settings_menu(*imgui_drawer(), *immediate_drawer(), *runtime(),
                                        settings_path_, game_data_root());
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
    stop_live_prompt_updates();
    set_settings_guest_memory(nullptr);
    settings_menu_.reset();
    rex::ui::UnregisterBind("bind_pc_settings");
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
  std::filesystem::path settings_path_;
  std::unique_ptr<rex::ui::ImGuiDialog> settings_menu_;
  std::jthread test_worker_;
};

} // namespace

std::unique_ptr<rex::ui::WindowedApp> create_application(rex::ui::WindowedAppContext& context) {
  return std::make_unique<lollipop_app>(context);
}

} // namespace lollipop
