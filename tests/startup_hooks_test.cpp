#include "game/startup_hooks.h"

#include <array>

int main() {
  PPCRegister result{};
  result.u64 = 0x123456789ABCDEF0;
  if (lollipop::skip_startup_live_check(0, result) || result.u64 != 0x123456789ABCDEF0)
    return 1;
  lollipop::set_startup_live_check(0x40001000);
  for (uint64_t native : std::array<uint64_t, 3>{0, 1, 0x123456789ABCDEF0}) {
    for (uint32_t caller : std::array{0u, 0x40002000u}) {
      result.u64 = native;
      if (lollipop::skip_startup_live_check(caller, result) || result.u64 != native)
        return 2;
    }
  }
  if (!lollipop::skip_startup_live_check(0x40001000, result) || result.u64 != 1)
    return 3;
  lollipop::set_startup_live_check(0);
  result.u64 = 0;
  return lollipop::skip_startup_live_check(0x40001000, result) || result.u64 != 0 ? 4 : 0;
}
