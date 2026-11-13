#pragma once
#include <cstdint>

struct PPCContext;
namespace rex {
class Runtime;
}

namespace lollipop {
void update_game_ui(rex::Runtime& runtime, PPCContext& context);
void set_game_ui_memory(unsigned char* memory);
void game_movie_loaded(uint32_t player, uint32_t path);
void game_movie_unloaded(uint32_t player);
} // namespace lollipop
