#include "exit_flow.h"
#include "game/native_quit_dialog.h"
#include "game/timing_hooks.h"
#include <atomic>
#include <imgui.h>
#include <rex/input/input_system.h>

namespace lollipop {
namespace {
std::atomic<bool> main_menu_pending{};

bool released(rex::input::InputSystem& input, const ImGuiIO& io) {
  rex::input::X_INPUT_STATE state{};
  input.GetStateForUI(0, &state);
  return !(state.gamepad.buttons & 0x3010) && !ImGui::IsKeyDown(ImGuiKey_Enter) &&
         !ImGui::IsKeyDown(ImGuiKey_Escape) && !ImGui::IsKeyDown(ImGuiKey_Backspace) &&
         !ImGui::IsKeyDown(ImGuiKey_Space) && !io.MouseDown[0];
}
} // namespace

void request_main_menu_exit() { main_menu_pending.store(true); }

exit_flow::exit_flow(rex::input::InputSystem& input, std::function<void()> quit)
    : input_(input), quit_(std::move(quit)) {}

exit_flow::~exit_flow() {
  if (pending_) {
    input_.RemoveUIInputBlocker();
    set_settings_pause(false);
  }
}

bool exit_flow::request(std::function<void(bool)> resume_settings) {
  if (pending_ || native_quit().status() != quit_dialog_state::idle)
    return false;
  resume_settings_ = std::move(resume_settings);
  pending_ = true;
  previous_buttons_ = 0xFFFF;
  input_.AddUIInputBlocker();
  set_settings_pause(true);
  return true;
}

bool exit_flow::update(ImGuiIO& io) {
  if (main_menu_pending.exchange(false) && !pending_)
    request();
  if (!pending_)
    return false;
  ImGui::SetNextFrameWantCaptureKeyboard(true);
  ImGui::SetNextFrameWantCaptureMouse(true);
  auto& dialog = native_quit();
  const auto state = dialog.status();
  if (finishing_) {
    if (!released(input_, io))
      return true;
    dialog.dismiss();
    if (resume_settings_)
      resume_settings_(failed_);
    else
      set_settings_pause(false);
    input_.RemoveUIInputBlocker();
    pending_ = finishing_ = false;
    resume_settings_ = {};
  } else if (state == quit_dialog_state::idle) {
    if (released(input_, io))
      dialog.request();
  } else if (state == quit_dialog_state::active) {
    // ImGui capture silences guest controller polling; read the UI path and keep gameplay blocked.
    rex::input::X_INPUT_STATE controller{};
    input_.GetStateForUI(0, &controller);
    uint16_t buttons = controller.gamepad.buttons;
    if (controller.gamepad.thumb_ly > 16000)
      buttons |= 1;
    if (controller.gamepad.thumb_ly < -16000)
      buttons |= 2;
    if (controller.gamepad.thumb_lx < -16000)
      buttons |= 4;
    if (controller.gamepad.thumb_lx > 16000)
      buttons |= 8;
    const uint16_t pressed = buttons & ~previous_buttons_;
    previous_buttons_ = buttons;
    const int direction = int(bool(pressed & 10) || ImGui::IsKeyPressed(ImGuiKey_RightArrow) ||
                              ImGui::IsKeyPressed(ImGuiKey_DownArrow)) -
                          int(bool(pressed & 5) || ImGui::IsKeyPressed(ImGuiKey_LeftArrow) ||
                              ImGui::IsKeyPressed(ImGuiKey_UpArrow));
    if (direction)
      dialog.queue_input(direction > 0 ? quit_dialog_input::no : quit_dialog_input::yes);
    if ((pressed & 0x1000) || ImGui::IsKeyPressed(ImGuiKey_Enter, false))
      dialog.queue_input(quit_dialog_input::accept);
    if ((pressed & 0x2010) || ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
        ImGui::IsKeyPressed(ImGuiKey_Backspace, false) ||
        ImGui::IsKeyPressed(ImGuiKey_Space, false))
      dialog.queue_input(quit_dialog_input::cancel);
    if (ImGui::IsMouseClicked(0) && io.DisplaySize.x > 0 && io.DisplaySize.y > 0)
      dialog.queue_input(quit_dialog_input::mouse, io.MousePos.x / io.DisplaySize.x,
                         io.MousePos.y / io.DisplaySize.y);
  } else if (state == quit_dialog_state::confirmed) {
    quit_();
  } else if (state == quit_dialog_state::cancelled || state == quit_dialog_state::failed) {
    finishing_ = true;
    failed_ = state == quit_dialog_state::failed;
  }
  return true;
}
} // namespace lollipop
