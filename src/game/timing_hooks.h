#pragma once

#include <rex/ppc.h>

namespace lollipop {
void configure_frame_rate();
void set_settings_pause(bool paused);
} // namespace lollipop

// Generated guest code calls these symbols in the global namespace.
void lollipop_present_interval(PPCRegister& interval);
void lollipop_tick_limit(PPCRegister& limit);
void lollipop_tick_trace(PPCRegister& engine, PPCRegister& delta);
void lollipop_settings_pause(PPCRegister& paused);
