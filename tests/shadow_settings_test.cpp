#include "game/shadow_settings.h"

#include "settings/runtime_settings.h"
#include <cmath>
#include <iostream>
#include <limits>
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
  const auto* flag = lollipop::settings::GetFlagInfo("lollipop_shadow_resolution");
  check(flag && flag->GetConstraints().life == rex::SettingLife::kLive &&
            lollipop::settings::GetFlagByName(flag->GetName()) == "0",
        "Original shadows are the live default");
  for (const auto* bad : {"-1", "128", "300", "2048", "512.0", "invalid"})
    check(!lollipop::settings::SetFlagByName(flag->GetName(), bad),
          "Reject unsupported launch values");
  reset();
  lollipop::initialize_shadow_settings(memory.base);
  lollipop::update_shadow_settings(memory.base);
  auto light_limit = [](uint32_t maximum) {
    PPCRegister light{};
    light.u64 = maximum;
    lollipop_shadow_light_limit(light);
    return light.u32;
  };
  check(read(0x833696A8) == 1024 && read(0x83369740) == 1024 && light_limit(1024) == 128 &&
            rex::memory::load_and_swap<float>(memory.base + 0x833696B0) == 1.27324f,
        "Original limits light resolution while projection samples the allocated target size");
  // All transitions share one native baseline and one set of allocated targets.
  for (const auto wanted : {1024, 256, 512, 0, 1024, 512, 256, 0}) {
    check(lollipop::settings::SetFlagByName(flag->GetName(), std::to_string(wanted)),
          "Accept shadow resolution");
    const auto render_before = read(0x83369740);
    lollipop::update_shadow_settings(memory.base);
    check(read(0x83369740) == render_before,
          "Live updates leave the render copy to the native render-thread command");
    const auto expected = wanted ? wanted : 128;
    check(read(0x833696A8) == 1024 && light_limit(1024) == uint32_t(expected) &&
              light_limit(64) == 64 &&
              std::abs(rex::memory::load_and_swap<float>(memory.base + 0x833696B0) -
                       1.27324f * float(expected) / 128.0f) < 0.0001f,
          "Live quality switches and Original restore density without compounding");
    put(0x83369740, 1024u); // Simulate the queued native settings copy.
    check(!lollipop::update_shadow_settings(memory.base),
          "Unchanged quality needs no render command");
    put(0x83369740, 1u);
    check(lollipop::update_shadow_settings(memory.base) && read(0x83369740) == 1,
          "A stale render copy requests a native update without a game-thread write");
    put(0x83369740, 1024u);
    for (int reload = 0; reload < 2; ++reload) {
      reset();
      lollipop::update_shadow_settings(memory.base);
      check(read(0x833696A8) == 1024 && light_limit(1024) == uint32_t(expected) &&
                light_limit(64) == 64 &&
                std::abs(rex::memory::load_and_swap<float>(memory.base + 0x833696B0) -
                         1.27324f * float(expected) / 128.0f) < 0.0001f,
            "Quality survives native reloads without compounding");
    }
  }
  lollipop::settings::SetFlagByName(flag->GetName(), "1024");
  reset();
  lollipop::initialize_shadow_settings(memory.base);
  check(read(0x833696A8) == 1024 && read(0x83369740) == 1024,
        "Startup overrides initialize both copies before target allocation");
  for (const auto invalid_density : {0.0f, -1.0f, std::numeric_limits<float>::infinity(),
                                     std::numeric_limits<float>::quiet_NaN()}) {
    reset();
    put(0x833696B0, invalid_density);
    lollipop::initialize_shadow_settings(memory.base);
    lollipop::update_shadow_settings(memory.base);
    check(read(0x833696A8) == 128 && light_limit(2048) == 2048,
          "Invalid native density cannot produce a shadow override");
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
