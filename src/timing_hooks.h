#pragma once

#include "lollipop_pch.h"

void configure_frame_rate();
void lollipop_present_interval(PPCRegister &interval);
void lollipop_tick_limit(PPCRegister &limit);
void lollipop_tick_trace(PPCRegister &engine, PPCRegister &delta);
void set_settings_pause(bool paused);
void lollipop_settings_pause(PPCRegister &paused);
