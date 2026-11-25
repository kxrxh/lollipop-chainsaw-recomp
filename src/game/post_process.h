#pragma once
#include <cstdint>
#include <rex/ppc/context.h>

namespace lollipop {
void disable_depth_of_field(uint8_t* base, uint32_t proxy);
bool use_white_vignette_mask(const uint8_t* base, uint32_t material, uint32_t cache);
void disable_aim_vignette(uint8_t* base, uint32_t material, uint32_t cache);
} // namespace lollipop

void lollipop_film_grain(PPCRegister& strength);
void lollipop_depth_of_field(PPCRegister& proxy);
void lollipop_aim_vignette(PPCRegister& material, PPCRegister& cache);
void lollipop_scene_vignette(PPCRegister& material, PPCRegister& cache, PPCRegister& slot_offset,
                             PPCRegister& texture);
