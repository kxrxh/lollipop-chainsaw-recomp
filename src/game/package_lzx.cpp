#include "package_lzx.h"

#include <algorithm>
#include <cstring>
#include <new>
#include <vector>

#include <rex/system/lzx.h>
#include <rex/system/thread_state.h>

namespace {
constexpr size_t chunk_size = 128 * 1024;
constexpr size_t frame_size = 32 * 1024;
constexpr size_t max_packed = (chunk_size / frame_size) * (UINT16_MAX + 5) + 9;
} // namespace

bool lollipop::decode_package_lzx(std::span<const uint8_t> packed, std::span<uint8_t> output) {
  if (packed.size() <= 9 || packed.size() > max_packed || output.empty() ||
      output.size() > chunk_size)
    return false;
  // XMem consumes a five-byte terminator. UE excludes the last four bytes before decoding.
  if (!std::ranges::all_of(packed.last(9).first(5), [](auto b) { return b == 0; }))
    return false;
  packed = packed.first(packed.size() - 9);
  std::vector<uint8_t> stream;
  stream.reserve(packed.size());
  size_t remaining = output.size();
  while (!packed.empty()) {
    const bool extended = packed.front() == 0xff;
    const size_t header = extended ? 5 : 2;
    if (packed.size() < header)
      return false;
    const size_t decoded = extended ? (size_t(packed[1]) << 8 | packed[2]) : frame_size;
    const size_t encoded = size_t(packed[header - 2]) << 8 | packed[header - 1];
    if (!remaining || decoded != std::min(remaining, frame_size) || !encoded ||
        encoded > packed.size() - header)
      return false;
    const auto data = packed.subspan(header, encoded);
    stream.insert(stream.end(), data.begin(), data.end());
    remaining -= decoded;
    packed = packed.subspan(header + encoded);
  }
  return !remaining && lzx_decompress_xmem(stream.data(), stream.size(), output.data(),
                                           output.size(), chunk_size) == 0;
}

bool lollipop_async_package_lzx(PPCRegister& flags, PPCRegister& destination, PPCRegister& size,
                                PPCRegister& source, PPCRegister& packed_size) {
  if ((flags.u32 & 15) != 4 || !size.u32 || size.u32 > chunk_size || packed_size.u32 <= 9 ||
      packed_size.u32 > max_packed ||
      (source.u32 < uint64_t(destination.u32) + size.u32 &&
       destination.u32 < uint64_t(source.u32) + packed_size.u32))
    return false;
  auto* memory = rex::runtime::ThreadState::Get()->memory();
  auto accessible = [&](uint32_t address, uint32_t length, uint32_t access) {
    auto* heap = memory->LookupHeap(address);
    if (!heap || (heap->heap_type() != rex::memory::HeapType::kGuestVirtual &&
                  !(access == rex::memory::kMemoryProtectWrite &&
                    heap->heap_type() == rex::memory::HeapType::kGuestPhysical)))
      return false;
    rex::memory::HeapAllocationInfo region{};
    // QueryRegionInfo counts whole pages, including the leading partial page.
    return heap->QueryRegionInfo(address, &region) &&
           (region.state & rex::memory::kMemoryAllocationCommit) && (region.protect & access) &&
           length + address % heap->page_size() <= region.region_size;
  };
  if (!accessible(source.u32, packed_size.u32, rex::memory::kMemoryProtectRead) ||
      !accessible(destination.u32, size.u32, rex::memory::kMemoryProtectWrite))
    return false;
  const auto* input = memory->TranslateVirtual(source.u32);
  try {
    // A failed decode must leave the guest destination intact for its original decoder.
    std::vector<uint8_t> decoded(size.u32);
    if (!lollipop::decode_package_lzx({input, packed_size.u32}, decoded))
      return false;
    // Write through the guest alias so watched texture pages receive normal CPU-write faults.
    std::memcpy(memory->TranslateVirtual(destination.u32), decoded.data(), decoded.size());
    flags.u64 = 1;
    return true;
  } catch (const std::bad_alloc&) {
    return false;
  }
}
