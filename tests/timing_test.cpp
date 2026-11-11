#include "game/timing_hooks.h"

#include <fstream>
#include <string>

#include <rex/cvar.h>

using lollipop::configure_frame_rate;
using lollipop::set_settings_pause;

int main(int argc, char **argv) {
  if (argc == 2 && std::string_view(argv[1]).starts_with("--")) {
    const bool explicit_fps = std::string_view(argv[1]) == "--fps-cli";
    rex::cvar::SetFlagFromCommandLine("lollipop_60fps", "false");
    if (explicit_fps) {
      rex::cvar::SetFlagFromCommandLine("lollipop_fps", "120");
      rex::cvar::SetFlagFromCommandLine("video_mode_refresh_rate", "75");
    }
    configure_frame_rate();
    PPCRegister interval{}, limit{};
    interval.u64 = 2;
    limit.f64 = 24.0;
    lollipop_present_interval(interval);
    lollipop_tick_limit(limit);
    return interval.u64 == (explicit_fps ? 1 : 2) && limit.f64 == (explicit_fps ? 120.0 : 24.0) &&
        rex::cvar::Query<double>("video_mode_refresh_rate") == (explicit_fps ? 75.0 : 60.0) ? 0 : 14;
  }
  PPCRegister paused{};
  for (bool settings_open : {false, true, true, false}) {
    set_settings_pause(settings_open);
    for (uint64_t native_pause : {0ull, 1ull}) {
      paused.u64 = native_pause;
      lollipop_settings_pause(paused);
      if (paused.u64 != (settings_open ? 1ull : native_pause)) return 9;
    }
  }
  PPCRegister interval{};
  PPCRegister limit{};
  configure_frame_rate();
  interval.u64 = 2;
  limit.f64 = 30.0;
  lollipop_present_interval(interval);
  lollipop_tick_limit(limit);
  if (interval.u64 != 1 || limit.f64 != 60.0) {
    return 1;
  }
  for (int fps : {30, 60, 90, 120}) {
    if (!rex::cvar::SetFlagByName("lollipop_fps", std::to_string(fps))) return 10;
    configure_frame_rate();
    interval.u64 = 2;
    limit.f64 = 30.0;
    lollipop_present_interval(interval);
    lollipop_tick_limit(limit);
    if (interval.u64 != (fps == 30 ? 2 : 1) || limit.f64 != double(fps) ||
        rex::cvar::Query<double>("video_mode_refresh_rate") != double(fps > 60 ? fps : 60)) return 11;
  }
  for (const char* invalid : {"0", "45", "121", "90.5", "nan"})
    if (rex::cvar::SetFlagByName("lollipop_fps", invalid)) return 12;
  // Saving a new limit must not change only half of the active timing setup.
  rex::cvar::SetFlagByName("lollipop_fps", "90");
  lollipop_tick_limit(limit);
  if (limit.f64 != 120.0) return 13;
  rex::cvar::SetFlagFromCommandLine("lollipop_60fps", "false");
  configure_frame_rate();
  interval.u64 = 2;
  limit.f64 = 24.0;
  lollipop_present_interval(interval);
  lollipop_tick_limit(limit);
  if (interval.u64 != 2 || limit.f64 != 24.0)
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
