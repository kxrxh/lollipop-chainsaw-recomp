#include "game/package_lzx.h"
#include "game/shader_inventory.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <rex/hash.h>

namespace {
void check(bool value, const char* message) {
  if (!value)
    throw std::runtime_error(message);
}
void be32(std::vector<uint8_t>& data, size_t offset, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i)
    data[offset + i] = uint8_t(value >> (24 - i * 8));
}
void le32(std::vector<uint8_t>& data, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i)
    data.push_back(uint8_t(value >> (i * 8)));
}
void le64(std::vector<uint8_t>& data, uint64_t value) {
  le32(data, uint32_t(value));
  le32(data, uint32_t(value >> 32));
}
void write(const std::filesystem::path& path, std::span<const uint8_t> data) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream file(path, std::ios::binary);
  file.write(reinterpret_cast<const char*>(data.data()), data.size());
  check(bool(file), "Write synthetic shader fixture");
}
template <class F> void rejects(F function) {
  try {
    function();
  } catch (const std::runtime_error&) {
    return;
  }
  throw std::runtime_error("Malformed shader data was accepted");
}
std::vector<uint8_t> container() {
  std::vector<uint8_t> data(88);
  be32(data, 0, 0x102a1101);
  be32(data, 4, 76);
  be32(data, 8, 12);
  be32(data, 24, 36);
  be32(data, 40, 12);
  for (unsigned i = 76; i < 88; ++i)
    data[i] = uint8_t(i);
  return data;
}
std::vector<uint8_t> compress(std::span<const uint8_t> input) {
  std::vector<uint8_t> stream;
  bool first = true;
  for (const auto part : {input.first(5), input.subspan(5)}) {
    const unsigned bits = first ? 28 : 27;
    const uint32_t header = ((3u << 24) | uint32_t(part.size())) << (32 - bits);
    stream.insert(stream.end(), {uint8_t(header >> 16), uint8_t(header >> 24), uint8_t(header),
                                 uint8_t(header >> 8)});
    for (int i = 0; i < 3; ++i)
      le32(stream, 1);
    stream.insert(stream.end(), part.begin(), part.end());
    first = false;
  }
  std::vector<uint8_t> result{0xff, uint8_t(input.size() >> 8), uint8_t(input.size()),
                              uint8_t(stream.size() >> 8), uint8_t(stream.size())};
  result.insert(result.end(), stream.begin(), stream.end());
  result.resize(result.size() + 9);
  return result;
}
std::vector<uint8_t> library(std::span<const uint8_t> code) {
  std::vector<uint8_t> data{'X', 'E', 'S', 'H', 0x20, 0x20, 0x12, 0x19};
  le64(data, XXH3_64bits(code.data(), code.size()));
  le32(data, uint32_t(code.size() / 4));
  data.insert(data.end(), code.begin(), code.end());
  return data;
}
} // namespace

int main(int argc, char** argv) try {
  check(argc == 2, "Expected isolated test directory");
  const auto raw = container();
  const auto programs = lollipop::scan_shader_containers(raw);
  check(programs.size() == 1 && programs[0].vertex && programs[0].code.size() == 12,
        "Extract complete shader container");
  auto malformed = raw;
  be32(malformed, 24, 0xffffffff);
  check(lollipop::scan_shader_containers(malformed).empty(), "Reject out-of-range shader header");
  auto packed = compress(raw);
  std::vector<uint8_t> decoded(raw.size());
  check(lollipop::decode_package_lzx(packed, decoded) && decoded == raw,
        "XMem odd uncompressed blocks have no CAB padding");
  std::vector<uint8_t> serialized(8);
  be32(serialized, 0, uint32_t(raw.size()));
  be32(serialized, 4, uint32_t(packed.size()));
  serialized.insert(serialized.end(), packed.begin(), packed.end());
  check(lollipop::scan_shader_containers(serialized).size() == 1,
        "Extract individually compressed Unreal shaders");
  const auto valid_library = library(programs[0].code);
  check(lollipop::read_shader_library(valid_library).size() == 1, "Read validated shader library");
  for (size_t size = 9; size < valid_library.size(); ++size)
    rejects([&] { lollipop::read_shader_library(std::span(valid_library).first(size)); });
  auto corrupt = valid_library;
  corrupt.back() ^= 1;
  rejects([&] { lollipop::read_shader_library(corrupt); });
  const std::filesystem::path root = argv[1];
  write(root / "game/AshGame/GlobalShaderCache-Xbox360.bin", serialized);
  const auto prepare = [&](std::string_view inventory) {
    lollipop::prepare_shader_inventory(
        root / "game", root / "cache", inventory, [](auto, auto completed, auto total) {
          check(completed <= total, "Progress uses real work counts");
        });
  };
  const std::string header = "lollipop-pipelines-v1 d3d12-rov 20260226\n";
  prepare(header);
  prepare(header);
  prepare("lollipop-pipelines-v1 d3d12-rov 20260226\r\n");
  rejects([&] { prepare(header + "not-a-pipeline\n"); });
  auto variant = programs[0].code;
  variant[5] ^= 3;
  write(root / "cache/shaders/shareable/565707D0.xsh", library(variant));
  std::vector<uint8_t> description(64);
  const auto hash = XXH3_64bits(variant.data(), variant.size());
  for (unsigned i = 0; i < 8; ++i)
    description[i] = uint8_t(hash >> (i * 8));
  std::vector<uint8_t> pipelines{'X', 'E', 'P', 'S', 'D', 'X', 'R', 'O', 0x20, 0x26, 0x02, 0x26};
  le64(pipelines, XXH3_64bits(description.data(), description.size()));
  pipelines.insert(pipelines.end(), description.begin(), description.end());
  write(root / "cache/shaders/shareable/565707D0.rov.d3d12.xpso", pipelines);
  const auto output = root / "collected.txt";
  lollipop::collect_shader_inventory(root / "cache", output, "Synthetic variant");
  lollipop::collect_shader_inventory(root / "cache", output, "Merged synthetic variant");
  std::ifstream file(output);
  const std::string inventory((std::istreambuf_iterator<char>(file)), {});
  check(inventory.find("variant ") != std::string::npos,
        "Collect reconstructable runtime variants");
  write(root / "cache/shaders/shareable/565707D0.xsh", valid_library);
  prepare(inventory);
  std::ifstream restored(root / "cache/shaders/shareable/565707D0.xsh", std::ios::binary);
  const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(restored)), {});
  restored.close();
  const auto rebuilt = lollipop::read_shader_library(bytes);
  check(std::ranges::any_of(rebuilt,
                            [&](const auto& p) { return p.hash == hash && p.code == variant; }),
        "Rebuild recorded variants from disc and metadata alone");
  rejects([&] { prepare(header + "variant 1 2 0 1 1\n"); });
  const auto cached_library = root / "cache/shaders/shareable/565707D0.xsh";
  const auto cached_pipelines = root / "cache/shaders/shareable/565707D0.rov.d3d12.xpso";
  for (const auto& path :
       {root / "cache/shaders/disc-library.xsh", cached_library, cached_pipelines}) {
    write(path, std::array<uint8_t, 1>{0});
    prepare(header);
    check(std::filesystem::file_size(path) > 1, "Rebuild a damaged local cache");
  }
  auto partial_library = library(variant);
  partial_library.push_back(0);
  write(cached_library, partial_library);
  write(cached_pipelines, pipelines);
  prepare(header);
  check(std::filesystem::file_size(cached_library) == valid_library.size() &&
            std::filesystem::file_size(cached_pipelines) == 12,
        "Discard pipelines whose shader was lost in an interrupted cache write");
  auto partial_pipelines = pipelines;
  partial_pipelines.push_back(0);
  write(cached_pipelines, partial_pipelines);
  prepare(header);
  check(std::filesystem::file_size(cached_pipelines) == 12,
        "Rebuild a pipeline cache with an incomplete tail");
  std::string missing_shader = header;
  constexpr char hex[] = "0123456789abcdef";
  for (uint8_t byte : description) {
    missing_shader += hex[byte >> 4];
    missing_shader += hex[byte & 15];
  }
  rejects([&] { prepare(missing_shader + '\n'); });
  return 0;
} catch (const std::exception& error) {
  fprintf(stderr, "%s\n", error.what());
  return 1;
}
