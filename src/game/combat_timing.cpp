#include "combat_timing.h"
#include "game/timing_hooks.h"

#include <cmath>
#include <rex/memory.h>
#include <rex/system/thread_state.h>
#include <string_view>

namespace {
bool valid(uint32_t address, uint32_t size) {
  return address >= 0x10000 && size <= 0xC0000000 && address <= 0xC0000000 - size;
}

uint32_t read(const uint8_t* memory, uint32_t address) {
  return rex::memory::load_and_swap<uint32_t>(memory + address);
}

bool named(const uint8_t* memory, uint32_t object, std::string_view name) {
  if (!valid(object, 52))
    return false;
  const auto index = read(memory, object + 44);
  const auto names = read(memory, 0x83367208);
  const auto count = read(memory, 0x8336720C);
  if (index >= count || count > 1000000 || !valid(names, count * 4))
    return false;
  const auto entry = read(memory, names + index * 4);
  if (!valid(entry, 8 + uint32_t(name.size() + 1) * 2))
    return false;
  const bool wide = (read(memory, entry) & 1) != 0;
  for (size_t i = 0; i <= name.size(); ++i) {
    const auto actual = wide ? rex::memory::load_and_swap<uint16_t>(memory + entry + 8 + i * 2)
                             : memory[entry + 8 + i];
    if (actual != (i == name.size() ? 0 : uint8_t(name[i])))
      return false;
  }
  return true;
}

bool function(const uint8_t* memory, uint32_t node, std::string_view name, std::string_view owner) {
  return named(memory, node, name) && named(memory, read(memory, node + 40), owner);
}
} // namespace

namespace lollipop {
void correct_finisher_multiply(uint8_t* memory, uint32_t frame, double tick_delta, PPCRegister&,
                               PPCRegister& right) {
  if (!valid(frame, 32) || !std::isfinite(tick_delta) || tick_delta <= 0)
    return;
  // FFrame: Node, Object, Code and Locals; UStruct::Script starts at +76.
  const auto node = read(memory, frame + 16);
  if (!valid(node, 84))
    return;
  const auto offset = read(memory, frame + 24) - read(memory, node + 76);
  const auto size = read(memory, node + 80);
  // Loaded bytecode sizes and expression ends in the supported disc's AshGame package.
  if (size == 1281 && offset == 0x342 &&
      function(memory, node, "PlayerTick", "ChainsawHitBrakeCtrl")) {
    // The native 0.5 * 0.03 decay was applied once per tick, independent of elapsed time.
    right.f64 = float(0.9 * tick_delta);
  }
}

void correct_attack_homing(uint8_t* memory, uint32_t frame, uint32_t current) {
  if (!valid(frame, 28) || !valid(current, 12))
    return;
  const auto node = read(memory, frame + 16);
  if (!valid(node, 84) || read(memory, node + 80) != 729 ||
      !function(memory, node, "CalcAutoDistAttack", "ASH_JulietPawn"))
    return;
  const auto pawn = read(memory, frame + 20);
  if (!valid(pawn, 0x60))
    return;
  // Start from the collision-resolved Location, so blocked homing never accumulates a catch-up gap.
  for (uint32_t axis = 0; axis < 12; axis += 4)
    rex::memory::store_and_swap<uint32_t>(memory + current + axis,
                                          read(memory, pawn + 0x54 + axis));
}
} // namespace lollipop

void lollipop_finisher_multiply(PPCRegister& frame, PPCRegister& left, PPCRegister& right) {
  lollipop::correct_finisher_multiply(rex::runtime::ThreadState::Get()->memory()->virtual_membase(),
                                      frame.u32, lollipop::engine_tick_delta(), left, right);
}

void lollipop_attack_homing(PPCRegister& frame, PPCRegister& current) {
  lollipop::correct_attack_homing(rex::runtime::ThreadState::Get()->memory()->virtual_membase(),
                                  frame.u32, current.u32);
}
