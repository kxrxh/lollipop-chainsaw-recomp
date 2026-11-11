#pragma once

#include "lollipop_pch.h"

void lollipop_present_interval(PPCRegister &interval);
void lollipop_tick_limit(PPCRegister &limit);
void lollipop_tick_trace(PPCRegister &engine, PPCRegister &delta);
