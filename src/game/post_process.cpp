#include "game/post_process.h"
#include "settings/runtime_settings.h"

#include <array>
#include <cstring>
#include <rex/kernel/heap.h>
#include <rex/kernel/thread_state.h>
#include <rex/memory.h>

namespace {
uint8_t* guest_memory() {
  return rex::kernel::ThreadState::Get()->GetAddressSpace()->GetVirtualMembase();
}

const rex::Setting<bool>& option(const char* name) {
  return *static_cast<const rex::Setting<bool>*>(lollipop::settings::GetFlagInfo(name));
}

bool vignette_enabled() {
  static const auto& enabled = option("lollipop_vignette");
  return enabled.Get();
}

using guid = std::array<uint8_t, 16>;
// Cooked FMaterialResource IDs in Startup's shader cache. Each identifies a specific
// uniform layout, including the base and static-instance versions of the shared stage effect.
constexpr guid stage_material{0x34, 0x48, 0xcd, 0xc9, 0x44, 0x3b, 0x2b, 0xda,
                              0xf6, 0x26, 0x89, 0x86, 0x33, 0xe2, 0x6b, 0xa1};
constexpr guid stage_instance{0x4c, 0x0c, 0x77, 0x69, 0x44, 0xea, 0x7e, 0x5b,
                              0x5c, 0x6d, 0x10, 0xb5, 0xdc, 0xcc, 0x9a, 0x99};
constexpr guid aim_material{0x70, 0x83, 0xd1, 0x1a, 0x4e, 0x1f, 0x23, 0x15,
                            0x41, 0x22, 0x89, 0xb5, 0x78, 0xbd, 0xf6, 0xb1};

bool matches(const uint8_t* base, uint32_t material, const guid& id) {
  return std::memcmp(base + material + 96, id.data(), id.size()) == 0;
}

uint32_t scalars(const uint8_t* base, uint32_t cache) {
  return rex::memory::load_and_swap<uint32_t>(base + cache + 4);
}
} // namespace

namespace lollipop {
void disable_depth_of_field(uint8_t* base, uint32_t proxy) {
  // Match the native bEnableDOF=false path, which clears near and far blur independently of bloom.
  for (uint32_t offset : {20u, 28u})
    rex::memory::store_and_swap<uint32_t>(base + proxy + offset, 0);
}

bool use_white_vignette_mask(const uint8_t* base, uint32_t material, uint32_t cache) {
  // Only the pixel uniforms contain the mask. The vertex cache has no packed scalars.
  if (rex::memory::load_and_swap<uint32_t>(base + cache + 8) != 3 ||
      (!matches(base, material, stage_instance) && !matches(base, material, stage_material)))
    return false;
  // The stage shader also uses MaskMap to place the damage flash. Scalar 0 is Damage_Strength.
  return rex::memory::load_and_swap<float>(base + scalars(base, cache)) == 0.0f;
}

void disable_aim_vignette(uint8_t* base, uint32_t material, uint32_t cache) {
  if (rex::memory::load_and_swap<uint32_t>(base + cache + 8) == 2 &&
      matches(base, material, aim_material)) {
    // Scalar 3 is AimPower. Zero removes the radial blend and keeps the authored center treatment.
    rex::memory::store_and_swap<uint32_t>(base + scalars(base, cache) + 12, 0);
  }
}
} // namespace lollipop

void lollipop_film_grain(PPCRegister& strength) {
  static const auto& enabled = option("lollipop_film_grain");
  if (!enabled.Get())
    strength.f64 = 0;
}

void lollipop_depth_of_field(PPCRegister& proxy) {
  static const auto& enabled = option("lollipop_depth_of_field");
  if (!enabled.Get())
    lollipop::disable_depth_of_field(guest_memory(), proxy.u32);
}

void lollipop_aim_vignette(PPCRegister& material, PPCRegister& cache) {
  if (!vignette_enabled())
    lollipop::disable_aim_vignette(guest_memory(), material.u32, cache.u32);
}

void lollipop_scene_vignette(PPCRegister& material, PPCRegister& cache, PPCRegister& slot_offset,
                             PPCRegister& texture) {
  // MaskMap is texture slot zero. Null selects the original renderer's white texture.
  if (slot_offset.u32 == 0 && !vignette_enabled() &&
      lollipop::use_white_vignette_mask(guest_memory(), material.u32, cache.u32))
    texture.u64 = 0;
}
