#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include <rex/hid/pad.h>

namespace rex::ui {
class IUISystem;
}
namespace rex::hid {
class IInputSystem;
}

namespace lollipop {

struct test_controls {
  std::atomic<bool> active{true};
  std::atomic<bool> keyboard_input{true};
  std::mutex mutex;
  rex::hid::PadState state{};
  rex::hid::PadState gamepad_state{};
  std::optional<std::array<int, 3>> mouse_event;
  std::optional<std::array<int, 2>> key_event;
  std::chrono::steady_clock::time_point release_at{};
  std::chrono::steady_clock::time_point gamepad_release_at{};
  rex::hid::PadState read_pad(bool keyboard);
};

std::string execute_test_command(test_controls& controls, const std::string& command);
std::jthread start_test_bridge(rex::hid::IInputSystem& input, rex::ui::IUISystem& ui,
                               const std::filesystem::path& directory, std::function<void()> quit);

} // namespace lollipop
