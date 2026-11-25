#include "game/post_process.h"
#include "settings/runtime_settings.h"

#include <algorithm>
#include <array>
#include <iostream>
#include <rex/memory.h>
#include <stdexcept>

int main() try {
  using namespace lollipop;
  std::array<uint8_t, 512> memory{};
  const uint32_t material = 16, cache = 192, scalars = 256;
  const auto write = [&](uint32_t at, uint32_t value) {
    rex::memory::store_and_swap<uint32_t>(memory.data() + at, value);
  };
  const auto check = [](bool value, const char* message) {
    if (!value)
      throw std::runtime_error(message);
  };

  memory.fill(0x55);
  auto expected = memory;
  for (auto offset : {20, 28})
    std::fill_n(expected.begin() + offset, 4, 0);
  disable_depth_of_field(memory.data(), 0);
  check(memory == expected, "Disabling DOF must preserve bloom and other proxy fields");

  write(cache + 4, scalars);
  write(cache + 8, 3);
  write(scalars, 0);
  const auto mask = [&] { return use_white_vignette_mask(memory.data(), material, cache); };
  check(!mask(), "Other materials retain their texture");
  for (const auto& id : {std::array<uint32_t, 4>{0x3448cdc9, 0x443b2bda, 0xf6268986, 0x33e26ba1},
                         std::array<uint32_t, 4>{0x4c0c7769, 0x44ea7e5b, 0x5c6d10b5, 0xdccc9a99}}) {
    for (size_t i = 0; i < id.size(); ++i)
      write(material + 96 + 4 * i, id[i]);
    write(scalars, 0);
    check(mask(), "Both stage material permutations expose the scene mask");
    write(scalars, 0x3f000000);
    check(!mask(), "Nonzero damage retains its screen-edge cue");
  }

  const std::array<uint32_t, 4> aim{0x7083d11a, 0x4e1f2315, 0x412289b5, 0x78bdf6b1};
  for (size_t i = 0; i < aim.size(); ++i)
    write(material + 96 + 4 * i, aim[i]);
  write(cache + 8, 0);
  expected = memory;
  disable_aim_vignette(memory.data(), material, cache);
  check(memory == expected, "The empty vertex uniform cache is untouched");
  write(cache + 8, 2);
  expected = memory;
  std::fill_n(expected.begin() + scalars + 12, 4, 0);
  disable_aim_vignette(memory.data(), material, cache);
  check(memory == expected, "Aim vignette preserves brightness, saturation and opacity");

  PPCRegister grain{};
  for (bool enabled : {true, false, true}) {
    check(settings::SetFlagByName("lollipop_film_grain", enabled ? "true" : "false"), "Set grain");
    for (double authored : {0.0, 0.02, 0.05, 0.9}) {
      grain.f64 = authored;
      lollipop_film_grain(grain);
      check(grain.f64 == (enabled ? authored : 0.0),
            "Enabling grain restores each authored strength");
    }
  }
} catch (const std::exception& error) {
  std::cerr << error.what() << '\n';
  return 1;
}
