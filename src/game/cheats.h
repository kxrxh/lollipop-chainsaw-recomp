#pragma once

#include <span>
#include <string>

namespace lollipop {
class game_bridge;

struct cheat_option {
  const char* flag;
  const char* label;
  const char* help;
};

std::span<const cheat_option> cheat_options();
void disable_cheats();
void update_game_cheats(game_bridge& game);
std::string game_cheats_state();
} // namespace lollipop
