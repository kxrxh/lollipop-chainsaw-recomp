#pragma once
#include <cstdint>
#include <string>

struct PPCContext;
namespace rex {
class Runtime;
}
namespace lollipop {

void update_game_settings(rex::Runtime& runtime, PPCContext& context);
void game_settings_loaded();
bool game_settings_ready();
void save_game_settings();
void replace_game_options(uint32_t player);
void cancel_game_options(uint32_t player);
std::string game_settings_state();
std::string queue_game_console(const std::string& command);
std::string game_console_state();

} // namespace lollipop
