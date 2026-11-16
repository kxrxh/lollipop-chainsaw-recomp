#include "game/combat_timing.h"
#include "game/timing_hooks.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <rex/memory.h>
#include <stdexcept>
#include <string_view>
#include <windows.h>

namespace {
void check(bool value, const char* message) {
  if (!value)
    throw std::runtime_error(message);
}
struct guest_memory {
  uint8_t* base =
      static_cast<uint8_t*>(VirtualAlloc(nullptr, 0xC0000000ull, MEM_RESERVE, PAGE_NOACCESS));
  ~guest_memory() {
    if (base)
      VirtualFree(base, 0, MEM_RELEASE);
  }
};
} // namespace

int main() try {
  guest_memory memory;
  check(memory.base && VirtualAlloc(memory.base + 0x10000, 0x10000, MEM_COMMIT, PAGE_READWRITE) &&
            VirtualAlloc(memory.base + 0x83367000, 4096, MEM_COMMIT, PAGE_READWRITE),
        "Allocate synthetic guest objects and name table");
  const auto put = [&](uint32_t address, auto value) {
    rex::memory::store_and_swap(memory.base + address, value);
  };
  constexpr uint32_t frame = 0x10000, node = 0x11000, owner = 0x12000, locals = 0x13000;
  constexpr uint32_t script = 0x14000, pawn = 0x16000, current = 0x17000, names = 0x18000;
  put(0x83367208, names);
  put(0x8336720C, 2u);
  put(frame + 16, node);
  put(frame + 20, pawn);
  put(frame + 28, locals);
  put(node + 40, owner);
  put(node + 44, 0u);
  put(owner + 44, 1u);
  put(node + 76, script);
  const auto function = [&](std::string_view name, std::string_view parent, uint32_t size,
                            uint32_t offset) {
    put(node + 80, size);
    put(frame + 24, script + offset);
    for (unsigned i = 0; i < 2; ++i) {
      const auto entry = names + 256 + i * 256;
      put(names + i * 4, entry);
      put(entry, i); // Exercise both ASCII and wide FNames.
      const auto text = i ? parent : name;
      for (size_t j = 0; j <= text.size(); ++j) {
        const auto ch = j == text.size() ? 0 : uint8_t(text[j]);
        if (i)
          put(entry + 8 + uint32_t(j * 2), uint16_t(ch));
        else
          memory.base[entry + 8 + j] = uint8_t(ch);
      }
    }
  };
  PPCRegister left{}, right{};
  for (int fps : {30, 60, 90, 120}) {
    const double delta = 1.0 / fps;
    function("PlayerTick", "ChainsawHitBrakeCtrl", 1281, 0x342);
    left.f64 = 0.5;
    right.f64 = float(0.03);
    lollipop::correct_finisher_multiply(memory.base, frame, delta, left, right);
    check(left.f64 == 0.5 && std::abs(left.f64 * right.f64 * fps - 0.45) < 1e-6,
          "Rapid meter has the original 30 Hz decay per second at every frame rate");
    for (uint32_t offset : {0xEE0u, 0xF20u}) {
      function("UpdateChainsawMotionSpeed", "StateChainsawHitBrake", 5422, offset);
      for (double dilation : {1.0, 0.1, 0.6}) {
        for (bool triggered : {false, true}) {
          put(locals + 8, triggered ? 0x80000000u : 0u);
          double progress = 0;
          // The rapid-meter gate can keep inTrg set on consecutive frames.
          for (int tick = 0; tick < fps; ++tick) {
            left.f64 = float(delta * dilation);
            right.f64 = 1.5;
            const auto original = left.f64;
            lollipop::correct_finisher_multiply(memory.base, frame, delta, left, right);
            check(right.f64 == 1.5 && left.f64 == original,
                  "Held and triggered progress retain native elapsed time and slow motion");
            progress += left.f64 * right.f64;
          }
          check(std::abs(progress - dilation * 1.5) < 1e-6,
                "A continuous trigger gate preserves progress per second at every frame rate");
        }
      }
    }
  }
  for (double delta : {0.0, -1.0, std::numeric_limits<double>::infinity(),
                       std::numeric_limits<double>::quiet_NaN()}) {
    left.f64 = 0.5;
    right.f64 = 0.03;
    lollipop::correct_finisher_multiply(memory.base, frame, delta, left, right);
    check(left.f64 == 0.5 && right.f64 == 0.03, "Invalid tick timing changes no operands");
  }
  for (const auto parent : {"UnrelatedState", "ChainsawHitBrakeCtrl"}) {
    function("PlayerTick", parent, 1281, parent == "UnrelatedState" ? 0x342 : 0x343);
    left.f64 = 0.5;
    right.f64 = 0.03;
    lollipop::correct_finisher_multiply(memory.base, frame, 1.0 / 60, left, right);
    check(right.f64 == 0.03, "Other scripts and call sites retain native arithmetic");
  }
  function("CalcAutoDistAttack", "ASH_JulietPawn", 729, 0);
  for (int fps : {30, 60, 90, 120}) {
    float actual = 0, intended = 0;
    for (int tick = 0; tick < fps; ++tick) {
      put(pawn + 0x54, actual);
      put(pawn + 0x58, 2.0f);
      put(pawn + 0x5C, 3.0f);
      put(current, intended);
      lollipop::correct_attack_homing(memory.base, frame, current);
      const auto start = rex::memory::load_and_swap<float>(memory.base + current);
      check(start == actual && rex::memory::load_and_swap<float>(memory.base + current + 4) == 2 &&
                rex::memory::load_and_swap<float>(memory.base + current + 8) == 3,
            "Homing interpolates all axes from actual collision-resolved position");
      intended = start + (100 - start) * (5.0f / fps);
      actual = std::min(intended, 10.0f); // A wall blocks Juliet, but not the homing target.
      check(intended - actual <= 100 * (5.0f / fps), "Blocked movement has no accumulated gap");
    }
  }
  function("VInterpTo", "Object", 729, 0);
  put(current, 77.0f);
  lollipop::correct_attack_homing(memory.base, frame, current);
  check(rex::memory::load_and_swap<float>(memory.base + current) == 77,
        "Other vector interpolations are unchanged");
  PPCRegister engine{}, delta{};
  delta.f64 = 1.0 / 90;
  lollipop_tick_trace(engine, delta);
  check(lollipop::engine_tick_delta() == delta.f64,
        "Tick delta is available without tracing enabled");
  std::cout << "Combat timing and collision regression checks passed\n";
  return 0;
} catch (const std::exception& error) {
  std::cerr << error.what() << '\n';
  return 1;
}
