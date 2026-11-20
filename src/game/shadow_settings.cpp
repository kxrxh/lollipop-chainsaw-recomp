#include "shadow_settings.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/thread_state.h>

REXCVAR_DEFINE_INT32(lollipop_shadow_resolution, 0, "Lollipop",
                     "Character shadow resolution: 0 = original, 256, 512 or 1024")
    .allowed({"0", "256", "512", "1024"})
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

namespace lollipop {
namespace {
// GSystemSettings fields and the render-thread settings copy in the supported executable.
constexpr uint32_t max_shadow_resolution = 0x833696A8;
constexpr uint32_t shadow_texels_per_pixel = 0x833696B0;
constexpr uint32_t render_max_shadow_resolution = 0x83369740;
constexpr uint32_t shadow_target_size = 1024;
int32_t original_resolution = 0;
float original_density = 0;
std::atomic<uint32_t> light_resolution_limit{0};
} // namespace

bool update_shadow_settings(uint8_t* memory) {
  if (!original_resolution)
    return false;
  const auto wanted = REXCVAR_GET(lollipop_shadow_resolution);
  const auto resolution = wanted > original_resolution ? wanted : original_resolution;
  light_resolution_limit.store(uint32_t(resolution), std::memory_order_relaxed);
  // Always derive density from the native baseline, including when restoring Original.
  const auto texels_per_pixel =
      std::bit_cast<uint32_t>(original_density * (float(resolution) / float(original_resolution)));
  bool changed = false;
  for (const auto address : {max_shadow_resolution, shadow_texels_per_pixel}) {
    // Projection shaders and allocation both read the native maximum as the texture size.
    const auto value = address == shadow_texels_per_pixel ? texels_per_pixel : shadow_target_size;
    if (rex::memory::load_and_swap<uint32_t>(memory + address) != value) {
      rex::memory::store_and_swap<uint32_t>(memory + address, value);
      changed = true;
    }
  }
  if (changed)
    REXLOG_INFO("Character shadow resolution applied: {}, {} texels per pixel", resolution,
                std::bit_cast<float>(texels_per_pixel));
  return changed || rex::memory::load_and_swap<uint32_t>(memory + render_max_shadow_resolution) !=
                        shadow_target_size;
}

void initialize_shadow_settings(uint8_t* memory) {
  original_resolution = 0;
  light_resolution_limit.store(0, std::memory_order_relaxed);
  const auto original = rex::memory::load_and_swap<int32_t>(memory + max_shadow_resolution);
  const auto density = rex::memory::load_and_swap<float>(memory + shadow_texels_per_pixel);
  if (original <= 0 || original > 1024 || !std::isfinite(density) || density <= 0)
    return;
  original_resolution = original;
  original_density = density;
  update_shadow_settings(memory);
  rex::memory::store_and_swap<int32_t>(
      memory + render_max_shadow_resolution,
      rex::memory::load_and_swap<int32_t>(memory + max_shadow_resolution));
}

} // namespace lollipop

void lollipop_initialize_shadows() {
  lollipop::initialize_shadow_settings(
      rex::runtime::ThreadState::Get()->memory()->virtual_membase());
}

void lollipop_update_shadows() {
  auto* thread = rex::runtime::ThreadState::Get();
  if (!lollipop::update_shadow_settings(thread->memory()->virtual_membase()))
    return;
  // FSystemSettings queues its snapshot to the render thread; never write that copy mid-frame.
  uint64_t args[] = {0};
  rex::system::kernel_state()->function_dispatcher()->ExecuteTrap(thread, 0x823CD8C8, args, 1);
}

void lollipop_shadow_light_limit(PPCRegister& light) {
  if (const auto limit = lollipop::light_resolution_limit.load(std::memory_order_relaxed))
    light.u64 = std::min(light.u32, limit);
}
