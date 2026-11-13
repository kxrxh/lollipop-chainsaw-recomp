#include "game_menus.h"
#include "game/game_bridge.h"
#include "game/game_settings.h"
#include "game/native_quit_dialog.h"
#include "game/timing_hooks.h"
#include "ui/exit_flow.h"
#include <atomic>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc.h>

namespace lollipop {
namespace {
std::atomic<unsigned char*> guest_memory{};
}
void set_game_ui_memory(unsigned char* memory) { guest_memory.store(memory); }

void update_game_ui(rex::Runtime& runtime, PPCContext& context) {
  // Loading movies also advance on a worker while the engine serializes objects.
  if (!is_engine_tick_thread())
    return;
  game_bridge game(runtime, context);
  // StaticFindObject checks these flags before touching the object table.
  if (game.read(0x83351964) || game.read(0x83340FB0))
    return;
  update_game_settings(game);
  if (!game_options_closing())
    native_quit().update(game);
}

void game_movie_loaded(uint32_t player, uint32_t path) {
  const auto name = game_bridge::read_string(guest_memory.load(), path);
  REXLOG_DEBUG("Native menu movie loaded: {}", name);
  if (name == "UIScene_StartTitle.ASH_UI_TitleMenu") {
    game_settings_loaded();
    native_quit().track_main_menu(player);
  }
  if (name != "UIScane_Option.ASH_UI_OptionMenu")
    return;
  replace_game_options(player);
  rex::cvar::SetFlagByName("lollipop_pc_settings", "true");
}

void game_movie_unloaded(uint32_t player) {
  cancel_game_options(player);
  native_quit().untrack_main_menu(player);
}

} // namespace lollipop

// UGFxFSCmdHandler_Kismet::execFSCommand: r6 is the decoded command FString.
void lollipop_options_command(PPCRegister& command) {
  const auto name = lollipop::game_bridge::read_string(lollipop::guest_memory.load(), command.u32);
  REXLOG_DEBUG("Menu command: {}", name);
}

bool lollipop_quit_command(PPCRegister& command, PPCRegister& argument, PPCRegister& result) {
  if (lollipop::game_bridge::read_string(lollipop::guest_memory.load(), command.u32) !=
          "FsOptionOpen" ||
      lollipop::game_bridge::read_string(lollipop::guest_memory.load(), argument.u32) !=
          "FsQuitGame")
    return false;
  lollipop::request_main_menu_exit();
  result.u64 = 1;
  return true;
}
