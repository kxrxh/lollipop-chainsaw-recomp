#pragma once

namespace rex::kernel {
class TitleRuntime;
}

namespace lollipop {
class game_bridge;
void request_costume_dlc_load();
void load_installed_costumes(rex::kernel::TitleRuntime& runtime, game_bridge& game);
} // namespace lollipop
