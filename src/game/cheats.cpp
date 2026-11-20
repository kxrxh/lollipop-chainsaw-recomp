#include "cheats.h"
#include "game/game_bridge.h"

#include "settings/runtime_settings.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <mutex>
#include <rex/logging/log_category.h>
#include <stdexcept>

namespace lollipop {
namespace {
constexpr cheat_option options[] = {
    {"lollipop_cheat_health", "UNLIMITED HEALTH",
     "Blocks normal damage and restores health. Scripted failures still apply."},
    {"lollipop_cheat_gold", "UNLIMITED GOLD MEDALS",
     "Keeps gold medals at 9,999,999. Medals and purchases can be saved."},
    {"lollipop_cheat_platinum", "UNLIMITED PLATINUM MEDALS",
     "Keeps platinum medals at 999. Medals and purchases can be saved."},
    {"lollipop_cheat_soul", "FULL STAR SOUL GAUGE",
     "Refills Star Soul so you can activate it again. Does not force activation."}};
std::mutex state_mutex;
std::string state = "Waiting for gameplay.";

void publish(std::string value) {
  std::lock_guard lock(state_mutex);
  state = std::move(value);
}

bool is_juliet(game_bridge& game, uint32_t pawn) {
  if (!game_bridge::valid_address(pawn, 56))
    return false;
  const auto juliet = game.find("AshGame.ASH_JulietPawn");
  auto type = game.read(pawn + 52);
  for (int depth = 0; type >= 0x10000 && type < 0xBFFFFFBC && depth < 32; ++depth) {
    if (type == juliet)
      return true;
    type = game.read(type + 64);
  }
  return false;
}
} // namespace

std::span<const cheat_option> cheat_options() { return options; }

void disable_cheats() {
  for (const auto& option : options)
    lollipop::settings::SetFlagByName(option.flag, "false");
}

std::string game_cheats_state() {
  std::lock_guard lock(state_mutex);
  return state;
}

void update_game_cheats(game_bridge& game) {
  static auto next = std::chrono::steady_clock::time_point{};
  static uint32_t protected_controller = 0;
  static bool original_god_mode = false;
  const auto now = std::chrono::steady_clock::now();
  if (now < next)
    return;
  next = now + std::chrono::milliseconds(100);
  std::array<bool, std::size(options)> enabled{};
  for (size_t i = 0; i < enabled.size(); ++i)
    enabled[i] = lollipop::settings::Query<bool>(options[i].flag);
  if (std::ranges::none_of(enabled, [](bool value) { return value; }) && !protected_controller) {
    publish("Cheats are off. Toggles reset on restart.");
    return;
  }
  try {
    const auto info = game.world_info();
    if (!info) {
      publish("Waiting for gameplay.");
      return;
    }
    const auto player = game.call(info, "Engine.Actor.GetALocalPlayerController");
    if (!game_bridge::valid_address(player)) {
      publish("Waiting for gameplay.");
      return;
    }
    if (protected_controller && protected_controller != player)
      protected_controller = 0; // Travel can destroy the old controller.
    if (!enabled[0] && protected_controller == player) {
      game.boolean(player, "Engine.Controller.bGodMode", original_god_mode);
      protected_controller = 0;
    }
    const auto pawn = game.read(game.property(player, "Engine.Controller.Pawn"));
    if (!is_juliet(game, pawn)) {
      publish("Waiting for Juliet. Toggles apply when gameplay starts.");
      return;
    }
    if (enabled[0]) {
      if (protected_controller != player) {
        original_god_mode = game.boolean(player, "Engine.Controller.bGodMode");
        protected_controller = player;
      }
      game.boolean(player, "Engine.Controller.bGodMode", true);
      const auto health = game.property(pawn, "Engine.Pawn.Health");
      const auto maximum = game.read(game.property(pawn, "Engine.Pawn.HealthMax"));
      if (game.read(health) > 0 && maximum > 0 && maximum < 1000000)
        game.write(health, maximum);
    }
    if (enabled[1] || enabled[2]) {
      const auto ash = game.read(game.property(info, "Engine.WorldInfo.Game"));
      if (!game_bridge::valid_address(ash))
        throw std::runtime_error("Game is still loading.");
      const auto hud = game.call(ash, "AshGame.ASH_GameInfo.GetUI");
      if (!game_bridge::valid_address(hud))
        throw std::runtime_error("HUD is still loading.");
      const auto medals = game.call(hud, "AshGame.ASH_IngameHUD.GetHUDMedalCount");
      if (!game_bridge::valid_address(medals))
        throw std::runtime_error("Medal HUD is still loading.");
      for (size_t i = 0; i < 2; ++i) {
        if (!enabled[i + 1])
          continue;
        const auto prefix = std::string("AshGame.ASH_GFxHUDMedalCount.");
        const uint32_t maximum = i ? 999 : 9999999;
        if (game.call(medals, prefix + (i ? "GetPlaMedalCount" : "GetMedalCount")) != maximum) {
          const auto function = game.find(prefix + (i ? "SetPlaMedalDirect" : "SetMedalDirect"));
          if (!function)
            throw std::runtime_error("Medal setter is unavailable.");
          game.invoke(medals, function, maximum);
        }
      }
    }
    if (enabled[3]) {
      const auto current = game.read(game.property(pawn, "AshGame.ASH_JulietPawn.HeartSoulPoint"));
      const auto maximum =
          game.read(game.property(pawn, "AshGame.ASH_JulietPawn.HeartSoulPointMax"));
      if (maximum > 0 && maximum < 1000000 && current != maximum) {
        const auto function = game.find("AshGame.ASH_JulietPawn.SetHeartSoulPoint");
        if (!function)
          throw std::runtime_error("Star Soul setter is unavailable.");
        game.invoke(pawn, function, maximum);
      }
    }
    publish("Ready. Changes take effect immediately; toggles reset on restart.");
  } catch (const std::exception& error) {
    const auto message = std::string("Cheats unavailable: ") + error.what();
    if (game_cheats_state() != message)
      REXLOG_WARN("{}", message);
    publish(message);
  }
}
} // namespace lollipop
