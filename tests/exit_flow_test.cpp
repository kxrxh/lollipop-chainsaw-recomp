#include "game/native_quit_dialog.h"
#include "game/timing_hooks.h"
#include "ui/exit_flow.h"

#include "input_fixture.h"
#include <imgui.h>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
using lollipop::quit_dialog_input;
using lollipop::quit_dialog_state;
quit_dialog_state dialog_state = quit_dialog_state::idle;
std::vector<quit_dialog_input> commands;
bool paused = false;

void check(bool condition, const char* message) {
  if (!condition)
    throw std::runtime_error(message);
}

} // namespace

// Replace only the guest window; exercise the real host exit flow and input system.
namespace lollipop {
native_quit_dialog& native_quit() {
  static native_quit_dialog dialog;
  return dialog;
}
bool native_quit_dialog::request() {
  dialog_state = quit_dialog_state::requested;
  return true;
}
quit_dialog_state native_quit_dialog::status() const { return dialog_state; }
void native_quit_dialog::dismiss() { dialog_state = quit_dialog_state::idle; }
void native_quit_dialog::queue_input(quit_dialog_input input, float, float) {
  if (dialog_state == quit_dialog_state::active)
    commands.push_back(input);
}
void set_settings_pause(bool value) { paused = value; }
} // namespace lollipop

int main() {
  try {
    const std::unique_ptr<ImGuiContext, decltype(&ImGui::DestroyContext)> context(
        ImGui::CreateContext(), ImGui::DestroyContext);
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1280, 720);
    io.DeltaTime = 1.0f / 60;
    io.Fonts->Build();
    input_fixture app("lollipop-exit-flow-test");
    auto& input = app.GetInput();
    auto& pad = app.pad();
    pad.set_connected(true);
    int quit = 0, resumed = 0;
    lollipop::exit_flow flow(input, [&] { ++quit; });
    auto frame = [&] {
      ImGui::NewFrame();
      const bool pending = flow.update(io);
      ImGui::EndFrame();
      return pending;
    };
    pad.state.buttons = 0x1000;
    check(flow.request([&](bool failed) {
      check(!failed, "Cancellation must resume settings without an error");
      ++resumed;
    }),
          "Request quit from settings");
    frame();
    check(paused && dialog_state == quit_dialog_state::idle, "Wait for opening A to release");
    pad.state.buttons = 0;
    frame();
    check(dialog_state == quit_dialog_state::requested, "Release must open the native dialog");
    dialog_state = quit_dialog_state::active;
    frame();
    check(io.WantCaptureMouse && io.WantCaptureKeyboard, "Exercise input under ImGui capture");
    for (const auto [button, command] : {std::pair{0x0004, quit_dialog_input::yes},
                                         {0x0008, quit_dialog_input::no},
                                         {0x0001, quit_dialog_input::yes},
                                         {0x0002, quit_dialog_input::no},
                                         {0x1000, quit_dialog_input::accept},
                                         {0x2000, quit_dialog_input::cancel},
                                         {0x0010, quit_dialog_input::cancel}}) {
      commands.clear();
      pad.state.buttons = button;
      frame();
      check(commands == std::vector{command}, "Captured controller input must reach the dialog");
      frame();
      check(commands.size() == 1, "Held buttons must not repeat accept or cancel");
      pad.state.buttons = 0;
      frame();
    }
    for (const int axis : {-24000, 24000}) {
      commands.clear();
      pad.state.thumb_lx = axis;
      frame();
      check(commands == std::vector{axis < 0 ? quit_dialog_input::yes : quit_dialog_input::no},
            "The controller stick must select Yes and No under capture");
      pad.state.thumb_lx = 0;
      frame();
    }
    pad.state.buttons = 0x2000;
    auto guest = input.GetState(0).value().state;
    check(guest.buttons == 0, "Block guest input even when the driver ignores capture");
    dialog_state = quit_dialog_state::cancelled;
    frame();
    frame();
    check(resumed == 0 && paused, "Wait for held B before returning to settings");
    pad.state.buttons = 0;
    frame();
    check(resumed == 1 && dialog_state == quit_dialog_state::idle, "Resume settings once");
    check(!frame(), "Cancellation must finish the exit flow");
    pad.state.buttons = 0x1000;
    guest = input.GetState(0).value().state;
    check(guest.buttons == 0x1000, "Cancellation must release its input blocker");
    pad.state.buttons = 0;
    lollipop::request_main_menu_exit();
    frame();
    dialog_state = quit_dialog_state::confirmed;
    frame();
    check(quit == 1, "Native confirmation must close the game");
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
