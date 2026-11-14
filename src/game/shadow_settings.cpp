#include "shadow_settings.h"

#include <bit>
#include <cmath>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/system/thread_state.h>

REXCVAR_DEFINE_INT32(lollipop_shadow_resolution, 0, "Lollipop",
                     "Character shadow resolution: 0 = original, 256, 512 or 1024")
    .allowed({"0", "256", "512", "1024"})
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

namespace lollipop {
namespace {
// GSystemSettings fields and the render-thread settings copy in the supported executable.
constexpr uint32_t max_shadow_resolution = 0x833696A8;
constexpr uint32_t shadow_texels_per_pixel = 0x833696B0;
constexpr uint32_t render_max_shadow_resolution = 0x83369740;
int32_t resolution = 0;
uint32_t texels_per_pixel = 0;
} // namespace

void update_shadow_settings(uint8_t* memory) {
  if (!resolution)
    return;
  for (const auto address :
       {max_shadow_resolution, render_max_shadow_resolution, shadow_texels_per_pixel}) {
    const auto value = address == shadow_texels_per_pixel ? texels_per_pixel : uint32_t(resolution);
    if (rex::memory::load_and_swap<uint32_t>(memory + address) != value)
      rex::memory::store_and_swap<uint32_t>(memory + address, value);
  }
}

void initialize_shadow_settings(uint8_t* memory) {
  resolution = 0;
  const auto wanted = REXCVAR_GET(lollipop_shadow_resolution);
  const auto original = rex::memory::load_and_swap<int32_t>(memory + max_shadow_resolution);
  const auto density = rex::memory::load_and_swap<float>(memory + shadow_texels_per_pixel);
  if (!wanted || wanted <= original || original <= 0 || !std::isfinite(density) || density <= 0)
    return;
  resolution = wanted;
  // Raise density with the cap so distance-based selection does not round shadows back down.
  texels_per_pixel = std::bit_cast<uint32_t>(density * (float(wanted) / float(original)));
  update_shadow_settings(memory);
  REXLOG_INFO("Character shadow resolution: {} (original {}), {} texels per pixel", wanted,
              original, std::bit_cast<float>(texels_per_pixel));
}

} // namespace lollipop

void lollipop_initialize_shadows() {
  lollipop::initialize_shadow_settings(
      rex::runtime::ThreadState::Get()->memory()->virtual_membase());
}

void lollipop_update_shadows() {
  lollipop::update_shadow_settings(rex::runtime::ThreadState::Get()->memory()->virtual_membase());
}
