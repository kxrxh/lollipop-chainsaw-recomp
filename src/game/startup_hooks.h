#pragma once

#include <cstdint>
#include <rex/ppc.h>

namespace lollipop {
bool prefetch_recompiled_code();
void set_startup_live_check(uint32_t function);
bool skip_startup_live_check(uint32_t caller, PPCRegister& result);
} // namespace lollipop

// Generated guest code calls this symbol in the global namespace.
bool lollipop_skip_startup_live_check(PPCRegister& frame, PPCRegister& result);
