#include "game/timing_hooks.h"

#include <cmath>
#include <fstream>
#include <string>
#include <thread>

#include "settings/runtime_settings.h"
#include "settings_fixture.h"

using lollipop::configure_frame_rate;
using lollipop::set_settings_pause;

int main(int argc, char **argv) {
  lollipop::testing::settings_fixture fixture;
  fixture.add("video_mode_refresh_rate", "60");
  if (argc == 2 && std::string_view(argv[1]).starts_with("--")) {
    const bool explicit_fps = std::string_view(argv[1]) == "--fps-cli";
    lollipop::settings::SetFlagFromCommandLine("lollipop_60fps", "false");
    if (explicit_fps) {
      lollipop::settings::SetFlagFromCommandLine("lollipop_fps", "120");
      lollipop::settings::SetFlagFromCommandLine("video_mode_refresh_rate", "75");
    }
    configure_frame_rate();
    PPCRegister interval{}, limit{};
    interval.u64 = 2;
    limit.f64 = 24.0;
    lollipop_present_interval(interval);
    lollipop_tick_limit(limit);
    return interval.u64 == (explicit_fps ? 1 : 2) && limit.f64 == (explicit_fps ? 120.0 : 24.0) &&
                   lollipop::settings::Query<double>("video_mode_refresh_rate") ==
                       (explicit_fps ? 75.0 : 60.0)
               ? 0
               : 14;
  }
  PPCRegister paused{};
  if (lollipop::is_engine_tick_thread()) return 15;
  PPCRegister engine{}, delta{};
  lollipop_tick_trace(engine, delta);
  bool worker_is_engine = true;
  std::thread worker([&] { worker_is_engine = lollipop::is_engine_tick_thread(); });
  worker.join();
  if (!lollipop::is_engine_tick_thread() || worker_is_engine)
    return 16;
  for (bool settings_open : {false, true}) {
    for (bool cheats_open : {false, true}) {
      set_settings_pause(settings_open);
      lollipop::set_cheats_pause(cheats_open);
      for (uint64_t native_pause : {0ull, 1ull}) {
        paused.u64 = native_pause;
        lollipop_settings_pause(paused);
        if (paused.u64 != ((settings_open || cheats_open) ? 1ull : native_pause))
          return 17;
      }
    }
  }
  set_settings_pause(false);
  lollipop::set_cheats_pause(false);
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
    if (!lollipop::settings::SetFlagByName("lollipop_fps", std::to_string(fps)))
      return 10;
    PPCRegister minimum{};
    minimum.f64 = 0.016f;
    lollipop_trail_minimum_step(minimum);
    if (std::abs(minimum.f64 * fps - double(0.016f) * 30) > 1e-7)
      return 18;
    // Over one native frame the solver must retain the same velocity and add the same impulse.
    for (const double momentum : {0.0, 0.8, 1.0}) {
      double velocity = 5.0;
      for (int i = 0; i < fps / 30; ++i) {
        PPCRegister step{}, x{}, y{}, z{};
        step.f64 = x.f64 = 1.0 / fps;
        y.f64 = -x.f64;
        z.f64 = 2 * x.f64;
        lollipop::scale_trail_inertia(momentum, step, x, y, z);
        if (y.f64 != -x.f64 || z.f64 != 2 * x.f64)
          return 19;
        velocity = (velocity * momentum * step.f64 + x.f64) * fps;
      }
      if (std::abs(velocity - (5.0 * momentum + 1.0)) > 1e-6)
        return 20;
    }
    for (double dt : {0.0, fps == 30 ? 0.016 : 0.066}) {
      PPCRegister step{}, x{}, y{}, z{};
      step.f64 = dt;
      x.f64 = y.f64 = z.f64 = 1;
      lollipop::scale_trail_inertia(0.8, step, x, y, z);
      if (step.f64 != dt || x.f64 != 1 || y.f64 != 1 || z.f64 != 1)
        return 21;
    }
    interval.u64 = 2;
    limit.f64 = 30.0;
    lollipop_present_interval(interval);
    lollipop_tick_limit(limit);
    if (interval.u64 != (fps == 30 ? 2 : 1) || limit.f64 != double(fps) ||
        lollipop::settings::Query<double>("video_mode_refresh_rate") != double(fps > 60 ? fps : 60))
      return 11;
  }
  for (const char* invalid : {"0", "45", "121", "90.5", "nan"})
    if (lollipop::settings::SetFlagByName("lollipop_fps", invalid))
      return 12;
  // Live changes update both timing hooks and the guest refresh rate together.
  lollipop::settings::SetFlagByName("lollipop_fps", "90");
  lollipop_tick_limit(limit);
  interval.u64 = 2;
  lollipop_present_interval(interval);
  if (limit.f64 != 90.0 || interval.u64 != 1 ||
      lollipop::settings::Query<double>("video_mode_refresh_rate") != 90.0)
    return 13;
  lollipop::settings::SetFlagFromCommandLine("lollipop_60fps", "false");
  configure_frame_rate();
  interval.u64 = 2;
  limit.f64 = 24.0;
  lollipop_present_interval(interval);
  lollipop_tick_limit(limit);
  if (interval.u64 != 2 || limit.f64 != 24.0)
    return 3;
  if (argc != 2 || !lollipop::settings::SetFlagByName("lollipop_tick_trace_csv", argv[1]))
    return 4;

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
