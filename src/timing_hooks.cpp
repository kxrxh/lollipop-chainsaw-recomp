#include "timing_hooks.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/perf/counter.h>

REXCVAR_DEFINE_BOOL(
    lollipop_60fps, true, "Lollipop",
    "Use one vblank per frame and set the engine limit to 60 FPS");

REXCVAR_DEFINE_STRING(lollipop_tick_trace_csv, "", "Lollipop",
                      "Opt-in candidate engine Tick timing log")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

void lollipop_present_interval(PPCRegister &interval) {
  if (REXCVAR_GET(lollipop_60fps)) {
    interval.u64 = 1;
  }
}

void lollipop_tick_limit(PPCRegister &limit) {
  if (REXCVAR_GET(lollipop_60fps)) {
    limit.f64 = 60.0;
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
