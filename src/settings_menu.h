#pragma once

#include <filesystem>
#include <memory>
#include <cstdint>

namespace rex {
class Runtime;
namespace ui {
class ImGuiDialog;
class ImGuiDrawer;
class ImmediateDrawer;
}
}

std::unique_ptr<rex::ui::ImGuiDialog> make_settings_menu(
    rex::ui::ImGuiDrawer& drawer, rex::ui::ImmediateDrawer& immediate, rex::Runtime& runtime,
    const std::filesystem::path& config, const std::filesystem::path& game_root);
void configure_settings_fonts(struct ImFontAtlas* atlas);
void set_settings_guest_memory(unsigned char* memory);
void settings_movie_loaded(uint32_t player, uint32_t path);
void settings_movie_unloaded(uint32_t player);
