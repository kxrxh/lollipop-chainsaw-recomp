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
void set_settings_guest_memory(unsigned char* memory);
void settings_movie_loaded(uint32_t player, uint32_t path);
void settings_movie_unloaded(uint32_t player);

} // namespace lollipop
