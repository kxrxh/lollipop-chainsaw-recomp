#pragma once

#include <cstdint>
#include <rex/ppc.h>

namespace lollipop {
void correct_finisher_multiply(uint8_t* memory, uint32_t frame, double tick_delta,
                               PPCRegister& left, PPCRegister& right);
void correct_attack_homing(uint8_t* memory, uint32_t frame, uint32_t current);
} // namespace lollipop

void lollipop_finisher_multiply(PPCRegister& frame, PPCRegister& left, PPCRegister& right);
void lollipop_attack_homing(PPCRegister& frame, PPCRegister& current);
