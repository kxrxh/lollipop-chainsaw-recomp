#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>

namespace rex {
class Runtime;
namespace ui {
class ImGuiDialog;
class ImGuiDrawer;
class ImmediateDrawer;
} // namespace ui
} // namespace rex

struct ImFontAtlas;

namespace lollipop {

std::unique_ptr<rex::ui::ImGuiDialog>
make_settings_menu(rex::ui::ImGuiDrawer& drawer, rex::ui::ImmediateDrawer& immediate,
                   rex::Runtime& runtime, const std::filesystem::path& config,
                   const std::filesystem::path& game_root, std::function<void()> quit);
void configure_settings_fonts(ImFontAtlas* atlas);

} // namespace lollipop
