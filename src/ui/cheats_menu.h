#pragma once

#include <memory>

namespace rex::input {
class InputSystem;
}
namespace rex::ui {
class ImGuiDialog;
class ImGuiDrawer;
} // namespace rex::ui

namespace lollipop {
std::unique_ptr<rex::ui::ImGuiDialog> make_cheats_menu(rex::ui::ImGuiDrawer& drawer,
                                                       rex::input::InputSystem& input);
bool cheats_menu_captures_input();
} // namespace lollipop
