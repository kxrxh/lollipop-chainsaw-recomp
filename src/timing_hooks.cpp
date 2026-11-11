#include "timing_hooks.h"

#include <chrono>
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/perf/counter.h>

REXCVAR_DEFINE_BOOL(
    lollipop_60fps, true, "Lollipop",
    "Legacy 30/60 FPS switch; use lollipop_fps instead")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_INT32(lollipop_fps, 60, "Lollipop", "Frame limit: 30, 60, 90 or 120 FPS")
    .allowed({"30", "60", "90", "120"})
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_STRING(lollipop_tick_trace_csv, "", "Lollipop",
                      "Opt-in candidate engine Tick timing log")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace {
std::atomic<bool> settings_paused{false};
std::atomic<int> frame_rate{60};
}

void configure_frame_rate() {
  using rex::cvar::Source;
  const auto source = rex::cvar::GetFlagSource("lollipop_fps");
  const auto legacy = rex::cvar::GetFlagSource("lollipop_60fps");
  if (source == Source::kDefault ||
      (legacy == Source::kCommandLine && source != Source::kCommandLine) ||
      (legacy == Source::kEnvironment && source != Source::kCommandLine && source != Source::kEnvironment))
    rex::cvar::SetFlagByName("lollipop_fps", REXCVAR_GET(lollipop_60fps) ? "60" : "30");
  const auto fps = REXCVAR_GET(lollipop_fps);
  frame_rate.store(fps, std::memory_order_relaxed);
  const auto refresh_source = rex::cvar::GetFlagSource("video_mode_refresh_rate");
  if (refresh_source == Source::kDefault || refresh_source == Source::kRuntime)
    rex::cvar::SetFlagByName("video_mode_refresh_rate", std::to_string(std::max(60, fps)));
}

void set_settings_pause(bool paused) {
  settings_paused.store(paused, std::memory_order_relaxed);
}

void lollipop_settings_pause(PPCRegister &paused) {
  if (settings_paused.load(std::memory_order_relaxed)) paused.u64 = 1;
}

void lollipop_present_interval(PPCRegister &interval) {
  if (frame_rate.load(std::memory_order_relaxed) > 30) {
    interval.u64 = 1;
  }
}

void lollipop_tick_limit(PPCRegister &limit) {
  const auto fps = frame_rate.load(std::memory_order_relaxed);
  if (fps > 30) {
    limit.f64 = double(fps);
  }
}

void lollipop_tick_trace(PPCRegister &engine, PPCRegister &delta) {
  const auto &path = REXCVAR_GET(lollipop_tick_trace_csv);
  if (path.empty())
    return;
  static std::mutex mutex;
  std::lock_guard lock(mutex);
  static std::ofstream trace{std::filesystem::path(path)};
  static bool initialized = false;
  static unsigned rows = 0;
  if (!initialized) {
    initialized = true;
    if (!trace)
      REXLOG_ERROR("Could not open engine Tick trace: {}", path);
    else
      trace << "host_us,engine,delta_s,previous_gpu_frame_us\n";
  }
  if (!trace)
    return;
  const auto now = std::chrono::duration_cast<std::chrono::microseconds>(
                       std::chrono::steady_clock::now().time_since_epoch())
                       .count();
  trace << now << ',' << engine.u32 << ',' << std::setprecision(9) << delta.f64
        << ','
        << rex::perf::GetSnapshotCounter(rex::perf::CounterId::kFrameTimeUs)
        << '\n';
  if (++rows % 60 == 0)
    trace.flush();
}
