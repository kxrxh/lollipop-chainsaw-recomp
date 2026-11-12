#pragma once

#include <cstdint>
#include <span>

namespace lollipop {
bool decode_package_lzx(std::span<const uint8_t> packed, std::span<uint8_t> output);
}
