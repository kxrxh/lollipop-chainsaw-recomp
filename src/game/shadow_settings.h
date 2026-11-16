#pragma once

#include <cstdint>
#include <rex/ppc/context.h>

namespace lollipop {

void initialize_shadow_settings(uint8_t* memory);
bool update_shadow_settings(uint8_t* memory);

} // namespace lollipop

// Generated guest code calls these symbols in the global namespace.
void lollipop_initialize_shadows();
void lollipop_update_shadows();
void lollipop_shadow_capacity(PPCRegister& resolution);
