#include "native_quit_dialog.h"
#include "game/game_bridge.h"
#include <cmath>
#include <rex/logging.h>
#include <stdexcept>

namespace lollipop {
native_quit_dialog& native_quit() {
  static native_quit_dialog dialog;
  return dialog;
}
void native_quit_dialog::update(game_bridge& guest) {
  const auto state = state_.load();
  if (state != quit_dialog_state::requested && state != quit_dialog_state::opening &&
      state != quit_dialog_state::active)
    return;
  try {
    if (state == quit_dialog_state::requested) {
      movie_ = parent_ = 0;
      selection_.store(0);
      {
        std::lock_guard lock(input_mutex_);
        inputs_.clear();
      }
      deadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(8);
      const auto info = guest.world_info();
      if (!info)
        throw std::runtime_error("No native world");
      const auto game = guest.read(guest.property(info, "Engine.WorldInfo.Game"));
      const auto ui = guest.read(guest.property(game, "AshGame.ASH_GameInfo.mInterface"));
      guest.call(ui, "AshGame.ASH_IngameHUD.InitUIHUD_SystemWindow");
      movie_ = guest.call(ui, "AshGame.ASH_IngameHUD.GetUISystem");
      if (!movie_)
        throw std::runtime_error("Native system window could not be created");
      parent_ = main_menu_.load();
      if (parent_) {
        guest.movie_bool(parent_, "_root.bPushOn", true);
        guest.movie_call(parent_, "_root.ButtonSelectAfter");
      }
      // An unused scene ID keeps the original restart and retire actions out of this dialog.
      guest.write(guest.property(movie_, "AshGame.ASH_GFxSystemWindow.mSysScaneID"), 255);
      guest.write(guest.property(movie_, "AshGame.ASH_GFxSystemWindow.mSysTypeID"), 0);
      guest.boolean(movie_, "AshGame.ASH_GFxSystemWindow.mRanningflg", true);
      guest.boolean(movie_, "AshGame.ASH_GFxSystemWindow.mSuccess", false);
      guest.boolean(movie_, "AshGame.ASH_GFxSystemWindow.mbJude", false);
      if (!guest.call(movie_, "AshGame.ASH_GFxMoviePlayer.Start"))
        throw std::runtime_error("Native system window could not be started");
      guest.set_text(movie_, "AshGame.ASH_GFxSystemWindow.mSysMess", "Quit the game?");
      guest.set_real_time(movie_);
      state_.store(quit_dialog_state::opening);
    } else if (state == quit_dialog_state::opening) {
      if (guest.movie_string(movie_, "_root.mc_window.Bset_0.mc_buttonNo.disabled") == u"false") {
        guest.movie_bool(movie_, "_root.mc_window.Bset_0.mc_buttonNo.focused", true);
        if (guest.movie_string(movie_, "_root.mc_window.mJude") == u"2")
          state_.store(quit_dialog_state::active);
      }
      if (state_.load() == quit_dialog_state::opening &&
          std::chrono::steady_clock::now() >= deadline_)
        throw std::runtime_error("Native system window did not become ready");
    } else if (guest.boolean(movie_, "AshGame.ASH_GFxSystemWindow.mSuccess")) {
      const bool yes = guest.boolean(movie_, "AshGame.ASH_GFxSystemWindow.mbJude");
      guest.consume_input();
      if (parent_ && parent_ == main_menu_.load())
        guest.movie_call(parent_, "_root.ASHAS_SetReturnMenu");
      state_.store(yes ? quit_dialog_state::confirmed : quit_dialog_state::cancelled);
    } else {
      std::vector<control> inputs;
      {
        std::lock_guard lock(input_mutex_);
        inputs.swap(inputs_);
      }
      for (const auto& input : inputs) {
        if (input.input == quit_dialog_input::yes || input.input == quit_dialog_input::no) {
          guest.movie_bool(movie_,
                           input.input == quit_dialog_input::yes
                               ? "_root.mc_window.Bset_0.mc_buttonYes.focused"
                               : "_root.mc_window.Bset_0.mc_buttonNo.focused",
                           true);
        } else if (input.input == quit_dialog_input::accept) {
          guest.movie_call(movie_, "_root.mc_window.SelectAction");
        } else if (input.input == quit_dialog_input::cancel) {
          guest.movie_call(movie_, "_root.mc_window.CancelAction");
        } else {
          const auto [x, y] = guest.movie_point(movie_, input.x, input.y);
          const bool yes =
              guest.movie_hit(movie_, "_root.mc_window.Bset_0.mc_buttonYes.hitTest", x, y);
          const bool no =
              guest.movie_hit(movie_, "_root.mc_window.Bset_0.mc_buttonNo.hitTest", x, y);
          if (yes || no) {
            guest.movie_bool(movie_,
                             yes ? "_root.mc_window.Bset_0.mc_buttonYes.focused"
                                 : "_root.mc_window.Bset_0.mc_buttonNo.focused",
                             true);
            guest.movie_call(movie_, "_root.mc_window.SelectAction");
          }
        }
      }
      const auto selected = guest.movie_string(movie_, "_root.mc_window.mJude");
      selection_.store(selected == u"1" ? 1 : selected == u"2" ? 2 : 0);
    }
  } catch (const std::exception& error) {
    REXLOG_ERROR("Native quit confirmation failed: {}", error.what());
    try {
      if (movie_ && guest.boolean(movie_, "GFxUI.GFxMoviePlayer.bMovieIsOpen")) {
        const auto close = guest.find("GFxUI.GFxMoviePlayer.Close");
        if (close)
          guest.invoke(movie_, close, 1);
      }
      if (parent_ && parent_ == main_menu_.load())
        guest.movie_call(parent_, "_root.ASHAS_SetReturnMenu");
    } catch (const std::exception& cleanup_error) {
      REXLOG_ERROR("Native quit cleanup failed: {}", cleanup_error.what());
    }
    state_.store(quit_dialog_state::failed);
  }
}
bool native_quit_dialog::request() {
  auto expected = quit_dialog_state::idle;
  return state_.compare_exchange_strong(expected, quit_dialog_state::requested);
}
quit_dialog_state native_quit_dialog::status() const { return state_.load(); }
unsigned native_quit_dialog::selection() const { return selection_.load(); }
void native_quit_dialog::queue_input(quit_dialog_input input, float x, float y) {
  if (state_.load() != quit_dialog_state::active || !std::isfinite(x) || !std::isfinite(y) ||
      x < 0 || x > 1 || y < 0 || y > 1)
    return;
  std::lock_guard lock(input_mutex_);
  inputs_.push_back({input, x, y});
}
void native_quit_dialog::dismiss() {
  const auto state = state_.load();
  if (state == quit_dialog_state::cancelled || state == quit_dialog_state::confirmed ||
      state == quit_dialog_state::failed)
    state_.store(quit_dialog_state::idle);
}
void native_quit_dialog::track_main_menu(uint32_t player) { main_menu_.store(player); }
void native_quit_dialog::untrack_main_menu(uint32_t player) {
  main_menu_.compare_exchange_strong(player, 0);
}

} // namespace lollipop
