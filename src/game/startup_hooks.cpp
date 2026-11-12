#include "startup_hooks.h"

#include <atomic>
#include <rex/logging.h>
#include <rex/system/thread_state.h>

namespace lollipop {
namespace {
std::atomic<uint32_t> startup_live_check{};
} // namespace

void set_startup_live_check(uint32_t function) { startup_live_check.store(function); }

bool skip_startup_live_check(uint32_t caller, PPCRegister& result) {
  const auto function = startup_live_check.load();
  if (!function || caller != function)
    return false;
  // Only XBLCheck_Start takes its completed path; the actual login remains offline.
  result.u64 = 1;
  REXLOG_INFO("Skipped startup Xbox LIVE connection check");
  return true;
}
} // namespace lollipop

bool lollipop_skip_startup_live_check(PPCRegister& frame, PPCRegister& result) {
  const auto* memory = rex::runtime::ThreadState::Get()->memory();
  const auto caller = rex::memory::load_and_swap<uint32_t>(
      memory->TranslateVirtual(frame.u32 + 16)); // FFrame::Node.
  return lollipop::skip_startup_live_check(caller, result);
}
