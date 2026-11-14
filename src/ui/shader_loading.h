#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace rex::ui {
class ImGuiDialog;
class ImGuiDrawer;
} // namespace rex::ui

namespace lollipop {
struct shader_loading_state {
  std::mutex mutex;
  std::string stage = "Checking shader inventory";
  std::string error;
  uint32_t completed = 0, total = 0, failed = 0;
  bool compiled = false;
};
std::unique_ptr<rex::ui::ImGuiDialog>
make_shader_loading(rex::ui::ImGuiDrawer& drawer, std::shared_ptr<shader_loading_state> state,
                    std::function<void()> close);
} // namespace lollipop
