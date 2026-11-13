#include "game_settings.h"
#include "game/game_bridge.h"
#include "game/startup_hooks.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <mutex>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <stdexcept>
#include <string>
#include <utility>

REXCVAR_DEFINE_INT32(lollipop_music_volume, 10, "Lollipop", "Music volume, 0 to 10").range(0, 10);
REXCVAR_DEFINE_INT32(lollipop_sfx_volume, 10, "Lollipop", "Sound effects volume, 0 to 10")
    .range(0, 10);
REXCVAR_DEFINE_BOOL(lollipop_subtitles, false, "Lollipop", "Subtitles");
REXCVAR_DEFINE_BOOL(lollipop_camera_invert_y, false, "Lollipop", "Invert vertical camera");
REXCVAR_DEFINE_BOOL(lollipop_camera_invert_x, false, "Lollipop", "Invert horizontal camera");
REXCVAR_DEFINE_BOOL(lollipop_manual_aim, false, "Lollipop", "Manual blaster aim");

namespace lollipop {
namespace {
std::atomic<bool> ready{};
std::atomic<bool> loaded{};
std::atomic<bool> save_pending{};
std::atomic<uint32_t> legacy_movie{};
std::atomic<uint32_t> hidden_movie{};
std::atomic<int64_t> legacy_close_after{};
std::mutex state_mutex;
std::array<int, 7> engine_values{};
std::mutex console_mutex;
std::string console_pending;
std::string console_result = "idle";
constexpr const char* flags[] = {"lollipop_music_volume",
                                 "lollipop_sfx_volume",
                                 "lollipop_subtitles",
                                 "lollipop_camera_invert_y",
                                 "lollipop_camera_invert_x",
                                 "lollipop_manual_aim",
                                 "vibration"};
constexpr const char* getters[] = {"ASHUS_GetGameBGMVolume",    "ASHUS_GetGameSEVolume",
                                   "ASHUS_GetSubTitleDisp",     "ASHUS_GetPlayerCameraMode",
                                   "ASHUS_GetPlayerAimingMode", "ASHUS_GetPlayerVibMode"};

} // namespace

bool game_settings_ready() { return ready.load(); }
void game_settings_loaded() { loaded.store(true); }
void save_game_settings() { save_pending.store(true); }
void replace_game_options(uint32_t player) {
  legacy_close_after.store((std::chrono::steady_clock::now() + std::chrono::milliseconds(300))
                               .time_since_epoch()
                               .count());
  hidden_movie.store(0);
  legacy_movie.store(player);
}
void cancel_game_options(uint32_t player) { legacy_movie.compare_exchange_strong(player, 0); }
bool game_options_closing() { return legacy_movie.load() != 0; }
std::string game_settings_state() {
  std::lock_guard lock(state_mutex);
  std::string result = ready.load() ? "ready" : "loading";
  for (size_t i = 0; i < engine_values.size(); ++i)
    result += " " + std::string(flags[i]) + "=" + std::to_string(engine_values[i]);
  return result;
}

std::string queue_game_console(const std::string& command) {
  if (command.empty() || command.size() > 240 ||
      command.find_first_of("\r\n\0", 0, 3) != std::string::npos ||
      std::ranges::any_of(command, [](unsigned char c) { return c < 32 || c > 126; }))
    return "error console expects 1..240 printable ASCII characters";
  std::lock_guard lock(console_mutex);
  if (console_result == "pending" || console_result == "running")
    return "error console busy";
  console_pending = command;
  console_result = "pending";
  return "ok queued";
}

std::string game_console_state() {
  std::lock_guard lock(console_mutex);
  return console_result;
}

void update_game_settings(game_bridge& guest) {
  static auto next = std::chrono::steady_clock::time_point{};
  const auto now = std::chrono::steady_clock::now();
  const auto movie = legacy_movie.load();
  const bool hide = movie && hidden_movie.load() != movie;
  const bool close = movie && now.time_since_epoch().count() >= legacy_close_after.load();
  const bool update_settings = hide || close || now >= next;
  if (!update_settings)
    return;
  next = now + std::chrono::milliseconds(250);
  if (hide) {
    guest.hide_legacy(movie);
    hidden_movie.store(movie);
  }
  if (close && legacy_movie.exchange(0) == movie)
    guest.close_legacy(movie);
  static bool startup_check_found = false;
  if (!startup_check_found) {
    const auto function = guest.find("AshGame.ASH_SystemInit.XBLCheck_Start");
    if (function) {
      set_startup_live_check(function);
      startup_check_found = true;
    }
  }
  if (!loaded.load()) {
    std::lock_guard lock(console_mutex);
    if (console_pending.empty())
      return;
  }
  auto valid = [](uint32_t pointer) { return pointer >= 0x40000000 && pointer < 0xBFFFF000; };
  const auto info = guest.world_info();
  if (!info)
    return;
  std::string command;
  {
    std::lock_guard lock(console_mutex);
    command = std::exchange(console_pending, {});
    if (!command.empty())
      console_result = "running";
  }
  if (!command.empty()) {
    std::string result = "complete";
    try {
      guest.console(info, command);
    } catch (const std::exception& error) {
      result = "error " + std::string(error.what());
    }
    std::lock_guard lock(console_mutex);
    console_result = std::move(result);
  }
  // The startup UI exists before the game has read its native save.
  if (!loaded.load())
    return;
  const auto game_property = guest.find("Engine.WorldInfo.Game");
  const auto ui_property = guest.find("AshGame.ASH_GameInfo.mInterface");
  if (!game_property || !ui_property)
    return;
  const auto game = guest.read(info + guest.read(game_property + 84));
  if (!valid(game) || !valid(guest.read(game + guest.read(ui_property + 84))))
    return;
  const auto object = guest.find("AshGame.Default__ASH_GFxOptionMenu");
  if (!object)
    return;
  std::array<uint32_t, 6> functions{};
  for (size_t i = 0; i < functions.size(); ++i) {
    functions[i] = guest.find(std::string("AshGame.ASH_GFxOptionMenu.") + getters[i]);
    if (!functions[i])
      return;
  }
  std::array<int, 7> values{};
  for (size_t i = 0; i < 2; ++i) {
    const auto volume = std::bit_cast<float>(guest.invoke(object, functions[i]));
    if (!std::isfinite(volume) || volume < 0 || volume > 10)
      return;
    values[i] = int(std::round(volume));
  }
  values[2] = guest.invoke(object, functions[2]) != 0;
  values[3] = guest.invoke(object, functions[3], 0) != 0;
  values[4] = guest.invoke(object, functions[3], 1) != 0;
  values[5] = guest.invoke(object, functions[4]) != 0;
  values[6] = guest.invoke(object, functions[5]) != 0;
  if (!ready.load()) {
    for (size_t i = 0; i < values.size(); ++i)
      if (rex::cvar::GetFlagSource(flags[i]) == rex::cvar::Source::kDefault)
        rex::cvar::SetFlagByName(flags[i], i < 2       ? std::to_string(values[i])
                                           : values[i] ? "true"
                                                       : "false");
    ready.store(true);
    REXLOG_INFO("Original game settings imported into PC settings");
  }
  {
    std::lock_guard lock(state_mutex);
    engine_values = values;
  }
  std::array<int, 7> wanted{};
  for (size_t i = 0; i < wanted.size(); ++i) {
    const auto value = rex::cvar::GetFlagByName(flags[i]);
    wanted[i] = i < 2 ? std::stoi(value) : value == "true";
  }
  constexpr const char* setters[] = {"ASHUS_SetGameBGMVolume",    "ASHUS_SetGameSeVolume",
                                     "ASHUS_SetSubTitleDisp",     "ASHUS_SetOptionMenuCameraMode",
                                     "ASHUS_SetPlayerAimingMode", "ASHUS_SetOptionMenuVibMode"};
  auto set = [&](size_t index, uint32_t a, uint32_t b = 0) {
    const auto path = std::string("AshGame.ASH_GFxOptionMenu.") + setters[index];
    const auto function = guest.find(path);
    if (!function)
      return;
    if (index < 2)
      guest.invoke(object, function, a);
    else if (index == 2)
      guest.invoke_bools(object, function, path, "bSub", a != 0);
    else if (index == 3)
      guest.invoke_bools(object, function, path, "bUpDownRev", a != 0, "bLeftRightRev", b != 0);
    else if (index == 4)
      guest.invoke_bools(object, function, path, "bAimDir", a != 0);
    else
      guest.invoke_bools(object, function, path, "bVibfunction", a != 0);
  };
  for (size_t i = 0; i < 2; ++i)
    if (values[i] != wanted[i])
      set(i, std::bit_cast<uint32_t>(float(wanted[i])));
  if (values[2] != wanted[2])
    set(2, wanted[2]);
  if (values[3] != wanted[3] || values[4] != wanted[4])
    set(3, wanted[3], wanted[4]);
  if (values[5] != wanted[5])
    set(4, wanted[5]);
  if (values[6] != wanted[6])
    set(5, wanted[6]);
  if (save_pending.exchange(false)) {
    const auto close = guest.find("AshGame.ASH_GFxOptionMenu.OnClose");
    if (close)
      guest.invoke(object, close);
  }
}

} // namespace lollipop
