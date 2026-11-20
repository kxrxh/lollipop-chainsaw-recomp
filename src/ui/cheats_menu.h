#pragma once

#include <memory>

namespace rex::hid {
class IInputSystem;
}
namespace rex::ui {
class IDialog;
class IUISystem;
} // namespace rex::ui

namespace lollipop {
std::unique_ptr<rex::ui::IDialog> make_cheats_menu(rex::hid::IInputSystem& input);
void register_cheats_hotkey(rex::ui::IUISystem& ui);
bool cheats_menu_captures_input();
} // namespace lollipop
