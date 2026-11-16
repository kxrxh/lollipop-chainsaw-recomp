#include "game/cheats.h"
#include "game/native_quit_dialog.h"
#include "game/timing_hooks.h"
#include "testing/test_bridge.h"
#include "ui/cheats_menu.h"

#include <imgui.h>
#include <iostream>
#include <rex/cvar.h>
#include <rex/input/input_system.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/window.h>
#include <rex/ui/windowed_app_context.h>
#include <stdexcept>

REXCVAR_DEFINE_BOOL(lollipop_pc_settings, false, "Test", "Independent settings window");

namespace {
void check(bool condition, const char* message) {
  if (!condition)
    throw std::runtime_error(message);
}
class test_context final : public rex::ui::WindowedAppContext {
  void NotifyUILoopOfPendingFunctions() override {}
  void PlatformQuitFromUIThread() override {}
};
class test_window final : public rex::ui::Window {
public:
  explicit test_window(test_context& context) : Window(context, "Cheats test", 1280, 720) {}
  ~test_window() override { EnterDestructor(); }

private:
  bool OpenImpl() override { return false; }
  void RequestCloseImpl() override {}
  std::unique_ptr<rex::ui::Surface> CreateSurfaceImpl(rex::ui::Surface::TypeFlags) override {
    return {};
  }
  void RequestPaintImpl() override {}
};
bool paused() {
  PPCRegister value{};
  lollipop_settings_pause(value);
  return value.u64 != 0;
}
} // namespace

int main() try {
  test_context context;
  test_window window(context);
  rex::ui::ImGuiDrawer drawer(&window, 0);
  auto& io = drawer.GetIO();
  io.DisplaySize = ImVec2(1280, 720);
  io.DeltaTime = 1.0f / 60;
  io.Fonts->Build();
  rex::input::InputSystem input(nullptr);
  auto controls = std::make_shared<lollipop::test_controls>();
  input.AddDriver(lollipop::make_test_driver(controls));
  input.SetDeviceAssignment(lollipop::make_test_assignment());
  auto menu = lollipop::make_cheats_menu(drawer, input);
  auto frame = [&] {
    ImGui::NewFrame();
    menu->Draw();
    ImGui::Render();
  };
  auto f6 = [&] {
    rex::ui::KeyEvent key(&window, rex::ui::VirtualKey::kF6, 1, false, false, false, false, false);
    check(rex::ui::ProcessKeyEvent(key), "F6 must be registered");
    frame();
  };
  auto key = [&](ImGuiKey code, bool down) {
    io.AddKeyEvent(code, down);
    frame();
  };
  auto buttons = [&](uint16_t value) {
    {
      std::lock_guard lock(controls->mutex);
      controls->state.gamepad.buttons = value;
    }
    frame();
  };
  frame();
  check(!menu->IsContinuousRepaintNeeded() && !paused(), "Hidden cheats must not pause or repaint");
  f6();
  check(lollipop::cheats_menu_captures_input() && paused() &&
            !rex::cvar::Query<bool>("lollipop_pc_settings"),
        "F6 opens its own window");
  key(ImGuiKey_Enter, true);
  check(rex::cvar::Query<bool>("lollipop_cheat_health"), "Enter toggles the selected cheat");
  frame();
  check(rex::cvar::Query<bool>("lollipop_cheat_health"), "Holding Enter must not repeat");
  key(ImGuiKey_Enter, false);
  for (int i = 0; i < 4; ++i) {
    buttons(2);
    buttons(0);
  }
  buttons(0x1000);
  check(!rex::cvar::Query<bool>("lollipop_cheat_health"), "Controller A activates All Off");
  buttons(0);
  key(ImGuiKey_Escape, true);
  check(paused() && menu->IsContinuousRepaintNeeded(), "Wait for the closing key's release");
  key(ImGuiKey_Escape, false);
  check(!paused() && !lollipop::cheats_menu_captures_input(), "Release resumes gameplay");
  rex::cvar::SetFlagByName("lollipop_pc_settings", "true");
  lollipop::set_settings_pause(true);
  f6();
  f6();
  check(paused() && rex::cvar::Query<bool>("lollipop_pc_settings"),
        "Closing cheats must preserve an existing settings window and its pause");
  lollipop::set_settings_pause(false);
  f6();
  buttons(0x2000);
  check(paused(), "Controller B must wait for release too");
  buttons(0);
  check(!paused(), "Controller release resumes gameplay");
  f6();
  menu.reset();
  check(!paused() && !lollipop::cheats_menu_captures_input(),
        "Destruction releases pause and capture");
  return 0;
} catch (const std::exception& error) {
  std::cerr << error.what() << '\n';
  return 1;
}
