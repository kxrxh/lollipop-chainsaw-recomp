#include "keyboard_controls.h"

#include <initializer_list>
#include <stdexcept>
#include <string>

#include <rex/cvar.h>

namespace lollipop {
namespace {
struct binding {
  const char* flag;
  const char* keys;
};
constexpr binding bindings[] = {
    {"mnk_mode", "true"},
    {"mnk_mouse", "true"},
    {"mnk_mouse_deadzone", "0.2"},
    {"mnk_sensitivity", "18.0"},
    {"mnk_gameplay_modifiers", "true"},
    {"keybind_a", "MMB,Return"},
    {"keybind_b", "Space,Backspace"},
    {"keybind_x", "RMB"},
    {"keybind_y", "LMB"},
    {"keybind_left_trigger", "Shift"},
    {"keybind_right_trigger", "V,Shift+LMB"},
    {"keybind_left_shoulder", "Q"},
    {"keybind_right_shoulder", "E"},
    {"keybind_lstick_press", "R"},
    {"keybind_rstick_press", "F"},
    {"keybind_back", "Tab"},
    {"keybind_start", "Escape"},
    {"keybind_dpad_up", "C,1"},
    {"keybind_dpad_down", "2"},
    {"keybind_dpad_left", "3"},
    {"keybind_dpad_right", "4"},
};
} // namespace

void configure_keyboard_controls() {
  for (const auto& bind : bindings) {
    if (rex::cvar::GetFlagSource(bind.flag) == rex::cvar::Source::kDefault &&
        !rex::cvar::SetFlagByName(bind.flag, bind.keys))
      throw std::runtime_error(std::string("Could not set keyboard binding: ") + bind.flag);
  }
  if (rex::cvar::Query<bool>("mnk_mode")) {
    // Native UI copies icons on the GPU; delayed readback leaves stale keyboard artwork on the CPU.
    for (const auto& bind :
         {binding{"readback_memexport", "true"}, binding{"readback_memexport_fast", "false"}}) {
      if (rex::cvar::GetFlagSource(bind.flag) == rex::cvar::Source::kDefault &&
          !rex::cvar::SetFlagByName(bind.flag, bind.keys))
        throw std::runtime_error(std::string("Could not synchronize prompt artwork: ") + bind.flag);
    }
  }
}

} // namespace lollipop
