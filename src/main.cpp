#include <memory>
#include <stdexcept>

#include "lollipop_init.h"

#include <rex/rex_app.h>
#include <rex/cvar.h>
#include <rex/perf/counter.h>
#include <rex/input/input_system.h>
#include <rex/system/interfaces/graphics.h>
#include <rex/system/gpu_plugin.h>

#include "test_bridge.h"

REXCVAR_DEFINE_STRING(lollipop_test_directory, "", "Lollipop",
                      "Opt-in local console and controller test directory")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

REXCVAR_DEFINE_STRING(lollipop_gpu_backend, "any", "Lollipop", "GPU backend")
    .allowed({"any", "d3d12", "vulkan"})
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

class lollipop_app final : public rex::ReXApp {
 public:
  explicit lollipop_app(rex::ui::WindowedAppContext& context)
      : ReXApp(context, "lollipop", PPCImageConfig) {}

  static std::unique_ptr<rex::ui::WindowedApp> create(
      rex::ui::WindowedAppContext& context) {
    return std::make_unique<lollipop_app>(context);
  }

 protected:
  void OnPostSetup() override {
    rex::perf::SetCsvLogPath(rex::cvar::GetFlagByName("perf_log_csv"));
    const auto& directory = REXCVAR_GET(lollipop_test_directory);
    if (!directory.empty()) {
      auto* input =
          dynamic_cast<rex::input::InputSystem*>(runtime()->input_system());
      if (!input)
        throw std::runtime_error("Test bridge needs the SDK input system");
      auto* presenter = runtime()->graphics_system()->presenter();
      if (!presenter || !imgui_drawer())
        throw std::runtime_error("Test bridge needs presentation");
      test_worker_ = start_test_bridge(*input, app_context(), directory,
                                       *imgui_drawer(), *presenter, *window());
    }
  }

  void OnShutdown() override {
    if (test_worker_.joinable()) {
      test_worker_.request_stop();
      test_worker_.join();
    }
  }

  void OnPreSetup(rex::RuntimeConfig& config) override {
    config.gpu_plugin = "xenos";
    config.graphics = rex::system::LoadGpuPlugin(
        config.gpu_plugin, REXCVAR_GET(lollipop_gpu_backend));
    if (!config.graphics)
      throw std::runtime_error("Could not load the requested GPU backend");
  }

 private:
  std::jthread test_worker_;
};

REX_DEFINE_APP(lollipop, lollipop_app::create)
