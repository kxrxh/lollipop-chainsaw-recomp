#pragma once

#include <cstdint>

namespace lollipop {

void initialize_shadow_settings(uint8_t* memory);
void update_shadow_settings(uint8_t* memory);

} // namespace lollipop

// Generated guest code calls these symbols in the global namespace.
void lollipop_initialize_shadows();
void lollipop_update_shadows();
