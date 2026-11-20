#include "game/cheats.h"
#include "game/native_quit_dialog.h"
#include "game/timing_hooks.h"
#include "input_fixture.h"
#include "settings/runtime_settings.h"
#include "ui/cheats_menu.h"

#include <imgui.h>
#include <iostream>
#include <rex/ui/ui_system.h>
#include <stdexcept>

namespace {
void check(bool condition, const char* message) {
  if (!condition)
    throw std::runtime_error(message);
}
bool paused() {
  PPCRegister value{};
  lollipop_settings_pause(value);
  return value.u64 != 0;
}
} // namespace

int main() try {
  input_fixture app("lollipop-cheats-menu-test", true);
  lollipop::settings::register_settings();
  auto& pad = app.pad();
  pad.set_connected(true);
  auto& ui = app.GetUI();
  auto& io = ui.GetImGuiIO();
  test_renderer renderer;
  lollipop::register_cheats_hotkey(ui);
  auto menu = lollipop::make_cheats_menu(app.GetInput());
  auto binding = ui.AddDialog(*menu);
  auto frame = [&] { ui.DrawOverlays(renderer); };
  auto f6 = [&] {
    app.send_key(rex::VirtualKey::kF6, true);
    frame();
    app.send_key(rex::VirtualKey::kF6, false);
    frame();
  };
  auto key = [&](ImGuiKey code, bool down) {
    io.AddKeyEvent(code, down);
    frame();
  };
  auto buttons = [&](uint16_t value) {
    pad.state.buttons = value;
    frame();
  };
  frame();
  check(!menu->NeedsContinuousPaint() && !paused(), "Hidden cheats must not pause or repaint");
  f6();
  check(lollipop::cheats_menu_captures_input() && paused() &&
            !lollipop::settings::Query<bool>("lollipop_pc_settings"),
        "F6 opens its own window");
  key(ImGuiKey_Enter, true);
  check(lollipop::settings::Query<bool>("lollipop_cheat_health"),
        "Enter toggles the selected cheat");
  frame();
  check(lollipop::settings::Query<bool>("lollipop_cheat_health"), "Holding Enter must not repeat");
  key(ImGuiKey_Enter, false);
  for (int i = 0; i < 4; ++i) {
    buttons(2);
    buttons(0);
  }
  buttons(0x1000);
  check(!lollipop::settings::Query<bool>("lollipop_cheat_health"),
        "Controller A activates All Off");
  buttons(0);
  frame();
  check(io.WantCaptureKeyboard, "Exercise F6 while ImGui owns the keyboard");
  f6();
  check(!lollipop::settings::Query<bool>("lollipop_cheats_menu"),
        "F6 closes cheats under ImGui keyboard capture");
  frame();
  f6();
  frame();
  key(ImGuiKey_Escape, true);
  check(paused() && menu->NeedsContinuousPaint(), "Wait for the closing key's release");
  key(ImGuiKey_Escape, false);
  check(!paused() && !lollipop::cheats_menu_captures_input(), "Release resumes gameplay");
  lollipop::settings::SetFlagByName("lollipop_pc_settings", "true");
  lollipop::set_settings_pause(true);
  f6();
  f6();
  check(paused() && lollipop::settings::Query<bool>("lollipop_pc_settings"),
        "Closing cheats must preserve an existing settings window and its pause");
  lollipop::set_settings_pause(false);
  f6();
  buttons(0x2000);
  check(paused(), "Controller B must wait for release too");
  buttons(0);
  check(!paused(), "Controller release resumes gameplay");
  f6();
  binding.Reset();
  menu.reset();
  check(!paused() && !lollipop::cheats_menu_captures_input(),
        "Destruction releases pause and capture");
  return 0;
} catch (const std::exception& error) {
  std::cerr << error.what() << '\n';
  return 1;
}
