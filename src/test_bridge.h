#pragma once

#include <chrono>
#include <atomic>
#include <array>
#include <optional>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <rex/input/input_driver.h>
#include <rex/input/device_assignment.h>

namespace rex::ui {
class WindowedAppContext;
class ImGuiDrawer;
class Presenter;
class Window;
}  // namespace rex::ui
namespace rex::input {
class InputSystem;
}

struct test_controls {
  std::atomic<bool> active{true};
  std::atomic<bool> keyboard_input{true};
  std::mutex mutex;
  rex::input::X_INPUT_STATE state{};
  rex::input::X_INPUT_STATE gamepad_state{};
  std::optional<std::array<int, 3>> mouse_event;
  std::optional<std::array<int, 2>> key_event;
  std::chrono::steady_clock::time_point release_at{};
  std::chrono::steady_clock::time_point gamepad_release_at{};
};

std::string execute_test_command(test_controls& controls,
                                 const std::string& command);
std::unique_ptr<rex::input::InputDriver> make_test_driver(
    std::shared_ptr<test_controls> controls);
std::unique_ptr<rex::input::DeviceAssignment> make_test_assignment();
std::jthread start_test_bridge(rex::input::InputSystem& input,
                               rex::ui::WindowedAppContext& context,
                               const std::filesystem::path& directory,
                               rex::ui::ImGuiDrawer& drawer,
                               rex::ui::Presenter& presenter,
                               rex::ui::Window& window);
