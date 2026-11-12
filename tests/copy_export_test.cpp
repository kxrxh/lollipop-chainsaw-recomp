#include <array>
#include <bit>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <utility>

#include <rex/graphics/util/draw.h>

int main() {
  using namespace rex::graphics;
  // Serialized register state for a 128-vertex, 8 KiB checkpoint copy.
  std::array<uint32_t, RegisterFile::kRegisterCount> words{};
  const auto fetch = XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 + 95 * 2;
  const auto c = XE_GPU_REG_SHADER_CONSTANT_000_X;
  words[fetch] = 0x155cc003;
  words[fetch + 1] = 0x10002002;
  words[c + 24] = 0x45ad5400;
  words[c + 25] = 0x4b000000;
  words[c + 26] = 0x4b021a01;
  words[c + 27] = 0x4b000400;
  words[c + 255 * 4 + 1] = std::bit_cast<uint32_t>(1.0f);
  words[c + 255 * 4 + 2] = std::bit_cast<uint32_t>(4.0f);
  words[c + 255 * 4 + 3] = std::bit_cast<uint32_t>(8.0f);
  words[XE_GPU_REG_VGT_MAX_VTX_INDX] = 0xffffff;
  const draw_util::MemExportRange range{0x05ad5400, 8192};
  auto matches = [&](uint32_t count) {
    return draw_util::IsLollipopCopyExport(std::bit_cast<RegisterFile>(words), count, range);
  };
  if (!matches(128) || matches(0) || matches(64) || matches(UINT32_MAX))
    return 1;

  // Each mutation independently invalidates the byte-for-byte copy.
  for (auto [index, mask] : std::initializer_list<std::pair<uint32_t, uint32_t>>{
           {fetch, 1},
           {fetch + 1, 1},
           {fetch + 1, 4},
           {c + 24, 1},
           {c + 25, 1},
           {c + 26, 1},
           {c + 27, 1},
           {c + 7 * 4, 0x3f800000},
           {c + 255 * 4 + 3, 0x00800000},
           {XE_GPU_REG_VGT_INDX_OFFSET, 1},
           {XE_GPU_REG_VGT_MIN_VTX_INDX, 1},
           {XE_GPU_REG_VGT_MAX_VTX_INDX, 0xffffff}}) {
    words[index] ^= mask;
    if (matches(128)) {
      std::cerr << "Unexpected copy match after changing register " << index << '\n';
      return 1;
    }
    words[index] ^= mask;
  }
}
