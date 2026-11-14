#include "game/shadow_settings.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <rex/cvar.h>
#include <rex/memory.h>
#include <stdexcept>
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
  check(memory.base && VirtualAlloc(memory.base + 0x83369000, 4096, MEM_COMMIT, PAGE_READWRITE),
        "Can allocate shadow settings guest page");
  auto put = [&](uint32_t address, auto value) {
    rex::memory::store_and_swap(memory.base + address, value);
  };
  auto read = [&](uint32_t address) {
    return rex::memory::load_and_swap<uint32_t>(memory.base + address);
  };
  auto reset = [&] {
    put(0x833696A8, 128u);
    put(0x83369740, 128u);
    put(0x833696B0, 1.27324f);
  };
  const auto* flag = rex::cvar::GetFlagInfo("lollipop_shadow_resolution");
  check(flag && flag->lifecycle == rex::cvar::Lifecycle::kRequiresRestart &&
            rex::cvar::GetFlagByName(flag->name) == "0",
        "Original shadows are the restart-only default");
  for (const auto* bad : {"-1", "128", "300", "2048", "512.0", "invalid"})
    check(!rex::cvar::SetFlagByName(flag->name, bad), "Reject unsupported launch values");
  reset();
  lollipop::initialize_shadow_settings(memory.base);
  lollipop::update_shadow_settings(memory.base);
  check(read(0x833696A8) == 128 && read(0x83369740) == 128 &&
            rex::memory::load_and_swap<float>(memory.base + 0x833696B0) == 1.27324f,
        "Original leaves native shadow settings intact");
  for (const auto wanted : {256, 512, 1024}) {
    reset();
    check(rex::cvar::SetFlagByName(flag->name, std::to_string(wanted)), "Accept shadow resolution");
    lollipop::initialize_shadow_settings(memory.base);
    // Native settings reloads must not undo the chosen resolution or compound its density.
    for (int reload = 0; reload < 2; ++reload) {
      reset();
      lollipop::update_shadow_settings(memory.base);
      check(read(0x833696A8) == wanted && read(0x83369740) == wanted &&
                std::abs(rex::memory::load_and_swap<float>(memory.base + 0x833696B0) -
                         1.27324f * float(wanted) / 128.0f) < 0.0001f,
            "Resolution and density survive native reloads without compounding");
    }
    rex::cvar::SetFlagByName(flag->name, "0");
    lollipop::update_shadow_settings(memory.base);
    check(read(0x833696A8) == wanted, "Menu changes wait until restart");
  }
  reset();
  lollipop::initialize_shadow_settings(memory.base);
  lollipop::update_shadow_settings(memory.base);
  check(read(0x833696A8) == 128, "Restart restores original shadows");
  rex::cvar::SetFlagByName(flag->name, "1024");
  for (const auto invalid_density : {0.0f, -1.0f, std::numeric_limits<float>::infinity(),
                                     std::numeric_limits<float>::quiet_NaN()}) {
    reset();
    put(0x833696B0, invalid_density);
    lollipop::initialize_shadow_settings(memory.base);
    lollipop::update_shadow_settings(memory.base);
    check(read(0x833696A8) == 128, "Invalid native density cannot produce a shadow override");
  }
  reset();
  put(0x833696A8, 0u);
  lollipop::initialize_shadow_settings(memory.base);
  lollipop::update_shadow_settings(memory.base);
  check(read(0x833696A8) == 0, "Uninitialized native settings are left alone");
} catch (const std::exception& error) {
  std::cerr << error.what() << '\n';
  return 1;
}
