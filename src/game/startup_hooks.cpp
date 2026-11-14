#include "startup_hooks.h"

#include <atomic>
#include <cstddef>
#include <rex/logging.h>
#include <rex/system/thread_state.h>
#include <vector>
#include <windows.h>

namespace lollipop {
namespace {
std::atomic<uint32_t> startup_live_check{};
} // namespace

bool prefetch_recompiled_code() {
  auto* module = reinterpret_cast<std::byte*>(GetModuleHandleW(nullptr));
  std::vector<WIN32_MEMORY_RANGE_ENTRY> ranges;
  size_t bytes = 0;
  MEMORY_BASIC_INFORMATION region{};
  constexpr DWORD executable =
      PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
  for (auto* address = module;
       module && VirtualQuery(address, &region, sizeof(region)) == sizeof(region) &&
       region.AllocationBase == module;
       address = static_cast<std::byte*>(region.BaseAddress) + region.RegionSize) {
    if (region.State == MEM_COMMIT && (region.Protect & executable) &&
        !(region.Protect & (PAGE_GUARD | PAGE_NOACCESS))) {
      ranges.push_back({region.BaseAddress, region.RegionSize});
      bytes += region.RegionSize;
    }
  }
  // Demand paging the large translated executable can stall first-use moves and effects.
  if (ranges.empty() ||
      !PrefetchVirtualMemory(GetCurrentProcess(), ranges.size(), ranges.data(), 0)) {
    REXLOG_WARN("Could not prefetch recompiled code (Windows error {})", GetLastError());
    return false;
  }
  REXLOG_INFO("Prefetched {} MiB of recompiled code", bytes / (1024 * 1024));
  return true;
}

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
