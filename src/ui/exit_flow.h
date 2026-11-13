#pragma once
#include <cstdint>
#include <functional>

struct ImGuiIO;
namespace rex::input {
class InputSystem;
}

namespace lollipop {
void request_main_menu_exit();

class exit_flow {
public:
  exit_flow(rex::input::InputSystem& input, std::function<void()> quit);
  ~exit_flow();
  exit_flow(const exit_flow&) = delete;
  exit_flow& operator=(const exit_flow&) = delete;
  bool request(std::function<void(bool failed)> resume_settings = {});
  bool update(ImGuiIO& io);

private:
  rex::input::InputSystem& input_;
  std::function<void()> quit_;
  std::function<void(bool)> resume_settings_;
  uint16_t previous_buttons_ = 0;
  bool pending_ = false;
  bool finishing_ = false;
  bool failed_ = false;
};
} // namespace lollipop
