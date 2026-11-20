#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>

namespace rex::hid {
class IInputSystem;
}
namespace rex::ui {
class IDialog;
}
struct ImFontAtlas;

namespace lollipop {

std::unique_ptr<rex::ui::IDialog> make_settings_menu(rex::hid::IInputSystem& input,
                                                     const std::filesystem::path& config,
                                                     const std::filesystem::path& game_root,
                                                     std::function<void()> quit);
void configure_settings_fonts(ImFontAtlas* atlas);

} // namespace lollipop
