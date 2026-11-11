#include "timing_hooks.h"

#include <fstream>
#include <string>

#include <rex/cvar.h>

int main(int argc, char **argv) {
  PPCRegister interval{};
  PPCRegister limit{};
  interval.u64 = 2;
  limit.f64 = 30.0;
  lollipop_present_interval(interval);
  lollipop_tick_limit(limit);
  if (interval.u64 != 1 || limit.f64 != 60.0) {
    return 1;
  }
  if (!rex::cvar::SetFlagByName("lollipop_60fps", "false")) {
    return 2;
  }
  interval.u64 = 2;
  limit.f64 = 30.0;
  lollipop_present_interval(interval);
  lollipop_tick_limit(limit);
  if (interval.u64 != 2 || limit.f64 != 30.0)
    return 3;
  if (argc != 2 ||
      !rex::cvar::SetFlagByName("lollipop_tick_trace_csv", argv[1]))
    return 4;
  PPCRegister engine{};
  PPCRegister delta{};
  engine.u64 = 0x12345678;
  delta.f64 = 1.0 / 60.0;
  for (int i = 0; i < 60; ++i)
    lollipop_tick_trace(engine, delta);
  if (engine.u64 != 0x12345678 || delta.f64 != 1.0 / 60.0)
    return 5;
  std::ifstream trace{argv[1]};
  std::string line;
  if (!std::getline(trace, line) ||
      line != "host_us,engine,delta_s,previous_gpu_frame_us")
    return 6;
  int rows = 0;
  while (std::getline(trace, line)) {
    if (line.find(",305419896,0.0166666667,") == std::string::npos)
      return 7;
    ++rows;
  }
  return rows == 60 ? 0 : 8;
}
