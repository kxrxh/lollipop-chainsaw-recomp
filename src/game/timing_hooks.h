#pragma once

#include <rex/ppc.h>

namespace lollipop {
void configure_frame_rate();
void set_settings_pause(bool paused);
void set_cheats_pause(bool paused);
bool is_engine_tick_thread();
double engine_tick_delta();
void scale_trail_inertia(double momentum, PPCRegister& step, PPCRegister& x, PPCRegister& y,
                         PPCRegister& z);
} // namespace lollipop

// Generated guest code calls these symbols in the global namespace.
void lollipop_present_interval(PPCRegister& interval);
void lollipop_tick_limit(PPCRegister& limit);
void lollipop_tick_trace(PPCRegister& engine, PPCRegister& delta);
void lollipop_settings_pause(PPCRegister& paused);
void lollipop_trail_minimum_step(PPCRegister& minimum);
void lollipop_trail_inertia(PPCRegister& control, PPCRegister& step, PPCRegister& x, PPCRegister& y,
                            PPCRegister& z);
