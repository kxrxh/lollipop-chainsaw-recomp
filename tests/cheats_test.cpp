#include "game/cheats.h"
#include "game/game_bridge.h"

#include <chrono>
#include <iostream>
#include <map>
#include <rex/cvar.h>
#include <rex/runtime.h>
#include <stdexcept>
#include <thread>

namespace {
constexpr uint32_t player = 0x40001000, pawn = 0x40002000, world = 0x40003000;
constexpr uint32_t juliet = 0x40004000, hud = 0x40005000, medals = 0x40006000;
std::map<uint32_t, uint32_t> memory;
bool has_world = true, god = false, missing_setter = false;
uint32_t gold = 7, platinum = 3;
int guest_calls = 0;

void check(bool value, const char* message) {
  if (!value)
    throw std::runtime_error(message);
}
} // namespace

// A small reflected-game fixture keeps tests independent of disc data and native saves.
namespace lollipop {
game_bridge::game_bridge(rex::Runtime& runtime, PPCContext& context)
    : runtime_(runtime), context_(context), saved_(context), memory_(nullptr), scratch_(0) {}
game_bridge::~game_bridge() = default;
bool game_bridge::valid_address(uint32_t address, uint32_t size) {
  return address >= 0x40000000 && size <= 0x80000000 && address <= 0xC0000000u - size;
}
uint32_t game_bridge::world_info() const { return has_world ? world : 0; }
uint32_t game_bridge::read(uint32_t address) const { return memory[address]; }
void game_bridge::write(uint32_t address, uint32_t value) { memory[address] = value; }
bool game_bridge::boolean(uint32_t, const std::string&) { return god; }
void game_bridge::boolean(uint32_t, const std::string&, bool value) { god = value; }
uint32_t game_bridge::find(const std::string& name) {
  if (name == "AshGame.ASH_JulietPawn")
    return juliet;
  if (name.ends_with("SetMedalDirect"))
    return missing_setter ? 0 : 1;
  if (name.ends_with("SetPlaMedalDirect"))
    return 2;
  if (name.ends_with("SetHeartSoulPoint"))
    return 3;
  throw std::runtime_error("Unexpected reflected function: " + name);
}
uint32_t game_bridge::property(uint32_t object, const std::string& name) {
  const std::map<std::string, uint32_t> offsets = {
      {"Engine.Controller.Pawn", 100},
      {"Engine.Pawn.Health", 104},
      {"Engine.Pawn.HealthMax", 108},
      {"Engine.WorldInfo.Game", 112},
      {"AshGame.ASH_JulietPawn.HeartSoulPoint", 116},
      {"AshGame.ASH_JulietPawn.HeartSoulPointMax", 120}};
  return object + offsets.at(name);
}
uint32_t game_bridge::call(uint32_t, const std::string& name) {
  ++guest_calls;
  if (name.ends_with("GetALocalPlayerController"))
    return player;
  if (name.ends_with("GetUI"))
    return hud;
  if (name.ends_with("GetHUDMedalCount"))
    return medals;
  if (name.ends_with("GetMedalCount"))
    return gold;
  if (name.ends_with("GetPlaMedalCount"))
    return platinum;
  throw std::runtime_error("Unexpected native call: " + name);
}
uint32_t game_bridge::invoke(uint32_t, uint32_t function, uint32_t a, uint32_t) {
  ++guest_calls;
  if (function == 1)
    gold = a;
  else if (function == 2)
    platinum = a;
  else if (function == 3)
    memory[pawn + 116] = a;
  else
    throw std::runtime_error("Invalid function must never be invoked");
  return 0;
}
} // namespace lollipop

int main() try {
  rex::Runtime runtime({});
  PPCContext context{};
  lollipop::game_bridge game(runtime, context);
  auto update = [&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(110));
    lollipop::update_game_cheats(game);
  };
  for (const auto& option : lollipop::cheat_options())
    check(!rex::cvar::Query<bool>(option.flag), "Cheats must default off");
  memory[player + 100] = pawn;
  memory[pawn + 52] = juliet + 256;
  memory[juliet + 256 + 64] = juliet; // Accept derived Juliet classes too.
  memory[pawn + 104] = 25;
  memory[pawn + 108] = 100;
  memory[pawn + 120] = 35;
  memory[world + 112] = world + 256;
  update();
  check(!god && memory[pawn + 104] == 25 && gold == 7 && platinum == 3,
        "Disabled cheats must leave gameplay untouched");
  for (const auto& option : lollipop::cheat_options())
    rex::cvar::SetFlagByName(option.flag, "true");
  update();
  check(god && memory[pawn + 104] == 100 && gold == 9999999 && platinum == 999 &&
            memory[pawn + 116] == 35,
        "Enable must protect health and refill both currencies and Star Soul");
  memory[pawn + 104] = 0;
  gold -= 100;
  memory[pawn + 116] = 0;
  update();
  check(memory[pawn + 104] == 0 && gold == 9999999 && memory[pawn + 116] == 35,
        "Refill spent resources without resurrecting dead players");
  lollipop::disable_cheats();
  update();
  check(!god && gold == 9999999, "All off must restore damage without rewinding currency");
  god = true;
  rex::cvar::SetFlagByName("lollipop_cheat_health", "true");
  update();
  lollipop::disable_cheats();
  update();
  check(god, "Preserve native god mode that was already set before cheats");
  god = false;
  rex::cvar::SetFlagByName("lollipop_cheat_health", "true");
  update();
  has_world = false;
  update();
  lollipop::disable_cheats();
  has_world = true;
  update();
  check(!god, "Restore a surviving controller after a temporary world transition");
  memory[pawn + 52] = juliet + 512;
  rex::cvar::SetFlagByName("lollipop_cheat_gold", "true");
  gold = 42;
  update();
  check(gold == 42, "Do not modify a menu pawn or another character");
  memory[pawn + 52] = juliet;
  missing_setter = true;
  update();
  check(lollipop::game_cheats_state().starts_with("Cheats unavailable:"),
        "Report missing reflection without crashing or invoking zero");
  missing_setter = false;
  update();
  check(gold == 9999999 && lollipop::game_cheats_state().starts_with("Ready."),
        "Recover after a loading-time failure");
  const auto calls = guest_calls;
  lollipop::update_game_cheats(game);
  check(guest_calls == calls, "Rate limit native calls between engine movie advances");
  return 0;
} catch (const std::exception& error) {
  std::cerr << error.what() << '\n';
  return 1;
}
