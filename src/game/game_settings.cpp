#include "game_settings.h"
#include <array>
#include <atomic>
#include <bit>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <string>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/thread_state.h>

REXCVAR_DEFINE_INT32(lollipop_music_volume, 10, "Lollipop", "Music volume, 0 to 10").range(0, 10);
REXCVAR_DEFINE_INT32(lollipop_sfx_volume, 10, "Lollipop", "Sound effects volume, 0 to 10").range(0, 10);
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
constexpr const char* flags[] = {"lollipop_music_volume", "lollipop_sfx_volume", "lollipop_subtitles",
    "lollipop_camera_invert_y", "lollipop_camera_invert_x", "lollipop_manual_aim", "vibration"};
constexpr const char* getters[] = {"ASHUS_GetGameBGMVolume", "ASHUS_GetGameSEVolume",
    "ASHUS_GetSubTitleDisp", "ASHUS_GetPlayerCameraMode", "ASHUS_GetPlayerAimingMode",
    "ASHUS_GetPlayerVibMode"};

class guest_calls {
 public:
  guest_calls(rex::Runtime& runtime, PPCContext& context)
      : runtime_(runtime), context_(context), saved_(context), memory_(runtime.virtual_membase()) {
    context_.r1.u64 -= 2048;
    write(context_.r1.u32, saved_.r1.u32);
    scratch_ = context_.r1.u32 + 128;
  }
  ~guest_calls() { context_ = saved_; context_.fpscr.restoreGuestBits(saved_.fpscr.csr); }
  uint32_t read(uint32_t at) const { return rex::memory::load_and_swap<uint32_t>(memory_ + at); }
  void write(uint32_t at, uint32_t value) { rex::memory::store_and_swap<uint32_t>(memory_ + at, value); }
  uint32_t find(const std::string& name) {
    const auto text = scratch_ + 256;
    if (name.size() > 256) return 0;
    for (size_t i = 0; i <= name.size(); ++i)
      rex::memory::store_and_swap<uint16_t>(memory_ + text + i * 2, i == name.size() ? 0 : name[i]);
    uint64_t args[] = {0, 0, text, 0};
    return uint32_t(runtime_.function_dispatcher()->ExecuteTrap(
        rex::runtime::ThreadState::Get(), 0x822AE758, args, 4));
  }
  uint32_t invoke(uint32_t object, uint32_t function, uint32_t a = 0, uint32_t b = 0) {
    std::memset(memory_ + scratch_, 0, 128);
    write(scratch_, a); write(scratch_ + 4, b);
    return process(object, function);
  }
  void invoke_bools(uint32_t object, uint32_t function, const std::string& path,
                    const char* first, bool a, const char* second = nullptr, bool b = false) {
    std::memset(memory_ + scratch_, 0, 128);
    auto put = [&](const char* name, bool value) {
      const auto property = find(path + "." + name);
      if (!property) return false;
      const auto offset = read(property + 84), mask = read(property + 116);
      if (offset > 124 || !mask || (mask & (mask - 1))) return false;
      if (value) write(scratch_ + offset, read(scratch_ + offset) | mask);
      return true;
    };
    if (!put(first, a) || (second && !put(second, b))) return;
    process(object, function);
  }
  uint32_t process(uint32_t object, uint32_t function) {
    const auto size = rex::memory::load_and_swap<uint16_t>(memory_ + function + 138);
    assert(size <= 128);
    if (size > 128) return 0;
    const auto process_event = read(read(object) + 248);
    uint64_t args[] = {object, function, scratch_, 0};
    runtime_.function_dispatcher()->ExecuteTrap(rex::runtime::ThreadState::Get(), process_event, args, 4);
    const auto offset = rex::memory::load_and_swap<uint16_t>(memory_ + function + 140);
    return offset <= 124 ? read(scratch_ + offset) : 0;
  }
  void hide_legacy(uint32_t movie) {
    const auto header = scratch_ + 32, text = scratch_ + 512;
    string(header, text, "_root._visible");
    uint64_t hide[] = {movie, header, 0};
    runtime_.function_dispatcher()->ExecuteTrap(rex::runtime::ThreadState::Get(), read(read(movie) + 340), hide, 3);
  }
  void close_legacy(uint32_t movie) {
    // Let Start/Load return before executing the original close action.
    const auto header = scratch_ + 32, text = scratch_ + 512;
    std::memset(memory_ + scratch_ + 64, 0, 88);
    string(header, text, "_root.gotoAndStop");
    const auto array = scratch_ + 64, argument = scratch_ + 96, result = scratch_ + 128;
    write(array, argument); write(array + 4, 1); write(array + 8, 1);
    const auto type = find("GFxUI.GFxMoviePlayer.ASValue.Type");
    const auto number = find("GFxUI.GFxMoviePlayer.ASValue.N");
    if (!type || !number) return;
    const auto type_offset = read(type + 84), number_offset = read(number + 84);
    if (type_offset > 23 || number_offset > 20) return;
    memory_[argument + type_offset] = 2;  // AS_Number.
    write(argument + number_offset, std::bit_cast<uint32_t>(17.0f));  // Original fsOptionClose action.
    uint64_t close[] = {result, movie, header, array};
    runtime_.function_dispatcher()->ExecuteTrap(rex::runtime::ThreadState::Get(), read(read(movie) + 396), close, 4);
    REXLOG_DEBUG("Original Options close action requested");
  }
 private:
  rex::Runtime& runtime_;
  PPCContext& context_;
  PPCContext saved_;
  uint8_t* memory_;
  uint32_t scratch_;
  void string(uint32_t header, uint32_t text, const char* value) {
    const auto length = std::strlen(value);
    for (size_t i = 0; i <= length; ++i)
      rex::memory::store_and_swap<uint16_t>(memory_ + text + i * 2, value[i]);
    write(header, text); write(header + 4, uint32_t(length + 1)); write(header + 8, uint32_t(length + 1));
  }
};
}  // namespace

bool game_settings_ready() { return ready.load(); }
void game_settings_loaded() { loaded.store(true); }
void save_game_settings() { save_pending.store(true); }
void replace_game_options(uint32_t player) {
  legacy_close_after.store((std::chrono::steady_clock::now() + std::chrono::milliseconds(300)).time_since_epoch().count());
  hidden_movie.store(0);
  legacy_movie.store(player);
}
void cancel_game_options(uint32_t player) { legacy_movie.compare_exchange_strong(player, 0); }
std::string game_settings_state() {
  std::lock_guard lock(state_mutex);
  std::string result = ready.load() ? "ready" : "loading";
  for (size_t i = 0; i < engine_values.size(); ++i)
    result += " " + std::string(flags[i]) + "=" + std::to_string(engine_values[i]);
  return result;
}

void update_game_settings(rex::Runtime& runtime, PPCContext& context) {
  static auto next = std::chrono::steady_clock::time_point{};
  const auto now = std::chrono::steady_clock::now();
  const auto movie = legacy_movie.load();
  const bool hide = movie && hidden_movie.load() != movie;
  const bool close = movie && now.time_since_epoch().count() >= legacy_close_after.load();
  if (!hide && !close && now < next) return;
  next = now + std::chrono::milliseconds(250);
  guest_calls guest(runtime, context);
  if (hide) { guest.hide_legacy(movie); hidden_movie.store(movie); }
  if (close && legacy_movie.exchange(0) == movie) guest.close_legacy(movie);
  // The startup UI exists before the game has read its native save.
  if (!loaded.load()) return;
  auto valid = [](uint32_t pointer) { return pointer >= 0x40000000 && pointer < 0xBFFFF000; };
  const auto world = guest.read(0x83353B5C);
  if (!valid(world)) return;
  const auto level = guest.read(world + 80);
  if (!valid(level)) return;
  const auto actors = guest.read(level + 60);
  if (!valid(actors)) return;
  const auto info = guest.read(actors);
  if (!valid(info)) return;
  const auto game_property = guest.find("Engine.WorldInfo.Game");
  const auto ui_property = guest.find("AshGame.ASH_GameInfo.mInterface");
  if (!game_property || !ui_property) return;
  const auto game = guest.read(info + guest.read(game_property + 84));
  if (!valid(game) || !valid(guest.read(game + guest.read(ui_property + 84)))) return;
  const auto object = guest.find("AshGame.Default__ASH_GFxOptionMenu");
  if (!object) return;
  std::array<uint32_t, 6> functions{};
  for (size_t i = 0; i < functions.size(); ++i) {
    functions[i] = guest.find(std::string("AshGame.ASH_GFxOptionMenu.") + getters[i]);
    if (!functions[i]) return;
  }
  std::array<int, 7> values{};
  for (size_t i = 0; i < 2; ++i) {
    const auto volume = std::bit_cast<float>(guest.invoke(object, functions[i]));
    if (!std::isfinite(volume) || volume < 0 || volume > 10) return;
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
        rex::cvar::SetFlagByName(flags[i], i < 2 ? std::to_string(values[i]) : values[i] ? "true" : "false");
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
  constexpr const char* setters[] = {"ASHUS_SetGameBGMVolume", "ASHUS_SetGameSeVolume",
      "ASHUS_SetSubTitleDisp", "ASHUS_SetOptionMenuCameraMode", "ASHUS_SetPlayerAimingMode",
      "ASHUS_SetOptionMenuVibMode"};
  auto set = [&](size_t index, uint32_t a, uint32_t b = 0) {
    const auto path = std::string("AshGame.ASH_GFxOptionMenu.") + setters[index];
    const auto function = guest.find(path);
    if (!function) return;
    if (index < 2) guest.invoke(object, function, a);
    else if (index == 2) guest.invoke_bools(object, function, path, "bSub", a != 0);
    else if (index == 3) guest.invoke_bools(object, function, path, "bUpDownRev", a != 0, "bLeftRightRev", b != 0);
    else if (index == 4) guest.invoke_bools(object, function, path, "bAimDir", a != 0);
    else guest.invoke_bools(object, function, path, "bVibfunction", a != 0);
  };
  for (size_t i = 0; i < 2; ++i)
    if (values[i] != wanted[i]) set(i, std::bit_cast<uint32_t>(float(wanted[i])));
  if (values[2] != wanted[2]) set(2, wanted[2]);
  if (values[3] != wanted[3] || values[4] != wanted[4]) set(3, wanted[3], wanted[4]);
  if (values[5] != wanted[5]) set(4, wanted[5]);
  if (values[6] != wanted[6]) set(5, wanted[6]);
  if (save_pending.exchange(false)) {
    const auto close = guest.find("AshGame.ASH_GFxOptionMenu.OnClose");
    if (close) guest.invoke(object, close);
  }
}

}  // namespace lollipop
