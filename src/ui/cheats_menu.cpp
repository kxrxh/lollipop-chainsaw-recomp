#include "cheats_menu.h"
#include "game/cheats.h"
#include "game/native_quit_dialog.h"
#include "game/timing_hooks.h"

#include <algorithm>
#include <cmath>
#include <imgui.h>
#include <rex/cvar.h>
#include <rex/input/input_system.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/keybinds.h>

REXCVAR_DEFINE_BOOL(lollipop_cheats_menu, false, "Lollipop", "Open the in-game cheats window");

namespace lollipop {
namespace {
bool captures_input = false;
constexpr ImU32 paper = IM_COL32(250, 236, 209, 255);
constexpr ImU32 pink = IM_COL32(255, 76, 137, 255);
constexpr ImU32 ink = IM_COL32(24, 15, 23, 255);

class cheats_menu final : public rex::ui::ImGuiDialog {
public:
  cheats_menu(rex::ui::ImGuiDrawer& drawer, rex::input::InputSystem& input)
      : ImGuiDialog(&drawer), input_(input) {
    rex::ui::RegisterBind("bind_cheats", "F6", "Toggle cheats window", [] {
      rex::cvar::SetFlagByName("lollipop_cheats_menu",
                               rex::cvar::Query<bool>("lollipop_cheats_menu") ? "false" : "true");
    });
  }
  ~cheats_menu() override {
    rex::ui::UnregisterBind("bind_cheats");
    rex::cvar::SetFlagByName("lollipop_cheats_menu", "false");
    release();
  }
  bool IsContinuousRepaintNeeded() const override { return visible_ || blocked_; }

private:
  bool closing_input_down() {
    rex::input::X_INPUT_STATE state{};
    input_.GetStateForUI(0, &state);
    return (state.gamepad.buttons & 0x3030) || ImGui::IsKeyDown(ImGuiKey_Enter) ||
           ImGui::IsKeyDown(ImGuiKey_Space) || ImGui::IsKeyDown(ImGuiKey_Escape) ||
           ImGui::IsKeyDown(ImGuiKey_Backspace) || ImGui::IsKeyDown(ImGuiKey_F6) ||
           ImGui::IsMouseDown(0);
  }
  void release() {
    if (blocked_)
      input_.RemoveUIInputBlocker();
    blocked_ = captures_input = false;
    set_cheats_pause(false);
  }
  void close() {
    visible_ = false;
    rex::cvar::SetFlagByName("lollipop_cheats_menu", "false");
    if (!closing_input_down())
      release();
  }
  void OnDraw(ImGuiIO& io) override {
    const bool requested = rex::cvar::Query<bool>("lollipop_cheats_menu");
    if (native_quit().status() != quit_dialog_state::idle) {
      close();
      return;
    }
    if (!requested && visible_)
      close();
    if (!visible_ && blocked_ && !closing_input_down())
      release();
    if (requested && !visible_) {
      visible_ = true;
      selected_ = 0;
      previous_buttons_ = 0xFFFF;
      if (!blocked_)
        input_.AddUIInputBlocker();
      blocked_ = captures_input = true;
      set_cheats_pause(true);
    }
    if (!visible_)
      return;
    ImGui::SetNextFrameWantCaptureKeyboard(true);
    ImGui::SetNextFrameWantCaptureMouse(true);
    rex::input::X_INPUT_STATE state{};
    input_.GetStateForUI(0, &state);
    uint16_t buttons = state.gamepad.buttons;
    if (state.gamepad.thumb_ly > 16000)
      buttons |= 1;
    if (state.gamepad.thumb_ly < -16000)
      buttons |= 2;
    const uint16_t pressed = buttons & ~previous_buttons_;
    previous_buttons_ = buttons;
    const auto options = cheat_options();
    const int count = int(options.size());
    const int direction = int(bool(pressed & 2) || ImGui::IsKeyPressed(ImGuiKey_DownArrow) ||
                              ImGui::IsKeyPressed(ImGuiKey_S)) -
                          int(bool(pressed & 1) || ImGui::IsKeyPressed(ImGuiKey_UpArrow) ||
                              ImGui::IsKeyPressed(ImGuiKey_W));
    selected_ = (selected_ + direction + count + 2) % (count + 2);
    const bool accept = (pressed & 0x1000) || ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
                        ImGui::IsKeyPressed(ImGuiKey_Space, false);
    const bool adjust = (pressed & 12) || ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false) ||
                        ImGui::IsKeyPressed(ImGuiKey_RightArrow, false);
    bool back = (pressed & 0x2030) || ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
                ImGui::IsKeyPressed(ImGuiKey_Backspace, false);

    const float scale = std::min(io.DisplaySize.x / 1280.0f, io.DisplaySize.y / 720.0f);
    const ImVec2 origin((io.DisplaySize.x - 920 * scale) / 2, (io.DisplaySize.y - 570 * scale) / 2);
    auto point = [&](float x, float y) {
      return ImVec2(std::round(origin.x + x * scale), std::round(origin.y + y * scale));
    };
    ImGui::SetNextWindowPos(origin);
    ImGui::SetNextWindowSize(ImVec2(920 * scale, 570 * scale));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::Begin("CHEATS", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoScrollWithMouse);
    auto& draw = *ImGui::GetWindowDrawList();
    draw.PushClipRectFullScreen();
    draw.AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(12, 5, 15, 175));
    draw.AddRectFilled(point(7, 9), point(927, 579), IM_COL32(0, 0, 0, 170));
    draw.PopClipRect();
    draw.AddRectFilled(point(0, 0), point(920, 570), ink);
    draw.AddRect(point(0, 0), point(920, 570), paper, 0, 0, 2 * scale);
    draw.AddRectFilled(point(0, 0), point(920, 94), pink);
    auto text = [&](float x, float y, float size, ImU32 color, const char* value) {
      draw.AddText(ImGui::GetFont(), size * scale, point(x, y), color, value);
    };
    text(30, 12, 50, ink, "CHEATS");
    text(32, 67, 17, ink, "LOLLIPOP CHAINSAW  /  LIVE GAMEPLAY MODIFIERS");
    text(710, 31, 21, ink, "F6 / ESC  CLOSE");
    const char* help = selected_ < count ? options[selected_].help
                                         : "Changes apply immediately. Toggles reset on restart.";
    for (int i = 0; i < count; ++i) {
      const auto& option = options[i];
      const float y = 118.0f + i * 58;
      ImGui::SetCursorScreenPos(point(28, y));
      const bool clicked = ImGui::InvisibleButton(option.flag, ImVec2(864 * scale, 48 * scale));
      const bool hover = ImGui::IsItemHovered() && input_.IsKeyboardActive();
      if (hover)
        help = option.help;
      const bool active = selected_ == i || hover;
      if (active) {
        draw.AddRectFilled(point(28, y), point(892, y + 48), IM_COL32(66, 32, 49, 255));
        draw.AddRectFilled(point(28, y), point(33, y + 48), pink);
      }
      const bool enabled = rex::cvar::Query<bool>(option.flag);
      text(48, y + 10, 26, paper, option.label);
      draw.AddRectFilled(point(766, y + 7), point(874, y + 41),
                         enabled ? pink : IM_COL32(65, 54, 63, 255), 4 * scale);
      text(792, y + 11, 23, enabled ? ink : paper, enabled ? "ON" : "OFF");
      if (clicked || (selected_ == i && (accept || adjust))) {
        selected_ = i;
        rex::cvar::SetFlagByName(option.flag, enabled ? "false" : "true");
      }
    }
    text(32, 363, 18, paper, game_cheats_state().c_str());
    text(32, 393, 18, IM_COL32(255, 193, 92, 255),
         "Medals and purchases can be saved. All Off does not undo them.");
    text(32, 426, 18, paper, help);
    auto button = [&](const char* label, float x, int index) {
      ImGui::SetCursorScreenPos(point(x, 474));
      const bool clicked = ImGui::InvisibleButton(label, ImVec2(420 * scale, 44 * scale));
      const bool active = selected_ == index || ImGui::IsItemHovered();
      draw.AddRectFilled(point(x, 474), point(x + 420, 518), active ? pink : paper);
      text(x + 20, 482, 25, ink, label);
      return clicked || (selected_ == index && accept);
    };
    if (button("ALL OFF", 28, count))
      disable_cheats();
    back |= button("RESUME GAME", 472, count + 1);
    text(32, 536, 16, paper,
         input_.IsKeyboardActive()
             ? "UP / DOWN  Select     ENTER / SPACE  Toggle     F6 / ESC  Close"
             : "D-PAD / STICK  Select     A  Toggle     B / START  Close");
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    if (back)
      close();
  }

  rex::input::InputSystem& input_;
  bool visible_ = false, blocked_ = false;
  int selected_ = 0;
  uint16_t previous_buttons_ = 0;
};
} // namespace

bool cheats_menu_captures_input() {
  return captures_input || rex::cvar::Query<bool>("lollipop_cheats_menu");
}

std::unique_ptr<rex::ui::ImGuiDialog> make_cheats_menu(rex::ui::ImGuiDrawer& drawer,
                                                       rex::input::InputSystem& input) {
  return std::make_unique<cheats_menu>(drawer, input);
}
} // namespace lollipop
