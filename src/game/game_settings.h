#pragma once
#include <cstdint>
#include <string>

namespace lollipop {
class game_bridge;

void update_game_settings(game_bridge& game);
void game_settings_loaded();
bool game_settings_ready();
void save_game_settings();
void replace_game_options(uint32_t player);
void cancel_game_options(uint32_t player);
bool game_options_closing();
std::string game_settings_state();
std::string queue_game_console(const std::string& command);
std::string game_console_state();

} // namespace lollipop
