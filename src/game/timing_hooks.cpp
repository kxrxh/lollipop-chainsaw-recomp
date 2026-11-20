#include "timing_hooks.h"

#include <chrono>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>

#include "settings/runtime_settings.h"
#include <rex/kernel/heap.h>
#include <rex/kernel/thread_state.h>
#include <rex/logging/log_category.h>
#include <rex/memory.h>
#include <rex/platform/perf.h>
#include <rex/string/text.h>

namespace {
std::atomic<bool> settings_paused{false};
std::atomic<bool> cheats_paused{false};
std::atomic<int> frame_rate{60};
thread_local bool engine_tick_thread = false;
thread_local double tick_delta = 0;

void apply_frame_rate(int fps) {
  frame_rate.store(fps, std::memory_order_relaxed);
  const auto source = lollipop::settings::GetFlagSource("video_mode_refresh_rate");
  if (source == lollipop::settings::Source::kDefault ||
      source == lollipop::settings::Source::kRuntime)
    lollipop::settings::SetFlagByName("video_mode_refresh_rate", std::to_string(std::max(60, fps)));
}
} // namespace

bool lollipop::is_engine_tick_thread() { return engine_tick_thread; }
double lollipop::engine_tick_delta() { return tick_delta; }

void lollipop::configure_frame_rate() {
  using lollipop::settings::Source;
  const auto source = lollipop::settings::GetFlagSource("lollipop_fps");
  const auto legacy = lollipop::settings::GetFlagSource("lollipop_60fps");
  if (source == Source::kDefault ||
      (legacy == Source::kCommandLine && source != Source::kCommandLine) ||
      (legacy == Source::kEnvironment && source != Source::kCommandLine &&
       source != Source::kEnvironment))
    lollipop::settings::SetFlagByName(
        "lollipop_fps", lollipop::settings::Query<bool>("lollipop_60fps") ? "60" : "30");
  static const auto watching =
      static_cast<rex::Setting<int32_t>*>(lollipop::settings::GetFlagInfo("lollipop_fps"))
          ->OnChange([](int32_t fps) { apply_frame_rate(fps); });
  (void)watching;
  apply_frame_rate(lollipop::settings::Query<int32_t>("lollipop_fps"));
}

void lollipop::set_settings_pause(bool paused) {
  settings_paused.store(paused, std::memory_order_relaxed);
}

void lollipop::set_cheats_pause(bool paused) {
  cheats_paused.store(paused, std::memory_order_relaxed);
}

void lollipop_settings_pause(PPCRegister& paused) {
  if (settings_paused.load(std::memory_order_relaxed) ||
      cheats_paused.load(std::memory_order_relaxed))
    paused.u64 = 1;
}

void lollipop_present_interval(PPCRegister& interval) {
  if (frame_rate.load(std::memory_order_relaxed) > 30) {
    interval.u64 = 1;
  }
}

void lollipop_tick_limit(PPCRegister& limit) {
  const auto fps = frame_rate.load(std::memory_order_relaxed);
  if (fps > 30) {
    limit.f64 = double(fps);
  }
}

void lollipop_trail_minimum_step(PPCRegister& minimum) {
  minimum.f64 *= 30.0 / frame_rate.load(std::memory_order_relaxed);
}

void lollipop::scale_trail_inertia(double momentum, PPCRegister& step, PPCRegister& x,
                                   PPCRegister& y, PPCRegister& z) {
  const double frames = 30.0 * step.f64;
  if (!(frames > 0 && frames < 1 && momentum > 0 && momentum <= 1) ||
      frame_rate.load(std::memory_order_relaxed) == 30)
    return;

  // The guest stores velocity with one 30 Hz momentum factor already applied.
  const double retained = std::pow(momentum, frames);
  step.f64 *= retained / momentum;
  // Fractional steps of v' = momentum * v + impulse must compose to one native step.
  const double impulse = momentum == 1 ? frames : (1 - retained) / (1 - momentum);
  x.f64 *= impulse;
  y.f64 *= impulse;
  z.f64 *= impulse;
}

void lollipop_trail_inertia(PPCRegister& control, PPCRegister& step, PPCRegister& x, PPCRegister& y,
                            PPCRegister& z) {
  constexpr uint32_t momentum_offset = 0x150;
  if (control.u32 < 0x10000 || control.u32 > 0xC0000000 - momentum_offset - sizeof(float))
    return;
  const auto* base = rex::kernel::ThreadState::Get()->GetAddressSpace()->GetVirtualMembase();
  const auto momentum = rex::memory::load_and_swap<float>(base + control.u32 + momentum_offset);
  lollipop::scale_trail_inertia(momentum, step, x, y, z);
}

void lollipop_tick_trace(PPCRegister &engine, PPCRegister &delta) {
  engine_tick_thread = true;
  tick_delta = delta.f64;
  static const auto* trace_path = static_cast<rex::Setting<std::string>*>(
      lollipop::settings::GetFlagInfo("lollipop_tick_trace_csv"));
  const auto path = trace_path->Get();
  if (path.empty())
    return;
  static std::mutex mutex;
  std::lock_guard lock(mutex);
  static std::ofstream trace{rex::string::to_path(path)};
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
  trace << now << ',' << engine.u32 << ',' << std::setprecision(9) << delta.f64 << ','
        << rex::platform::GetSnapshotCounter(rex::platform::CounterID::kFrameTimeUs) << '\n';
  if (++rows % 60 == 0)
    trace.flush();
}
