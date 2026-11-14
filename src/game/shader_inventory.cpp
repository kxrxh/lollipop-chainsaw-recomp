#include "shader_inventory.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <windows.h>

#include <rex/graphics/format/ucode.h>
#include <rex/hash.h>

#include "game/package_lzx.h"

namespace lollipop {
namespace {
constexpr size_t description_size = 64;
constexpr size_t max_file_size = 512 * 1024 * 1024;
constexpr std::array<uint8_t, 8> shader_header{'X', 'E', 'S', 'H', 0x20, 0x20, 0x12, 0x19};
constexpr std::array<uint8_t, 12> pipeline_header{'X', 'E', 'P',  'S',  'D',  'X',
                                                  'R', 'O', 0x20, 0x26, 0x02, 0x26};
using descriptions = std::map<uint64_t, std::array<uint8_t, description_size>>;
constexpr uint64_t loading_copy_hash = 0x896985ccfaf3c47aull;

shader_program make_loading_copy_shader() {
  namespace u = rex::graphics::ucode;
  using instruction = std::array<uint32_t, 3>;
  std::array<instruction, 28> code{};
  const auto exec = [](uint32_t address, uint32_t count, uint32_t sequence = 0,
                       uint32_t fetches = 0, bool end = false) {
    const uint32_t cache = (1u << fetches) - 1;
    const uint32_t low = address | (count << 12) | (sequence << 16) | ((cache & 15) << 28);
    const uint32_t high =
        (uint32_t(end ? u::ControlFlowOpcode::kExecEnd : u::ControlFlowOpcode::kExec) << 12) |
        (1u << 9) | (cache >> 4);
    return uint64_t(low) | (uint64_t(high) << 32);
  };
  const auto alloc = [](u::AllocType type, uint32_t count = 0) {
    return uint64_t(count) | (uint64_t(type) << 41) |
           (uint64_t(u::ControlFlowOpcode::kAlloc) << 44);
  };
  // Five pairs of 48-bit control-flow instructions precede the fetch/ALU instructions.
  const std::array flow{exec(5, 6, 0x555, 6),
                        exec(11, 4, 0x25, 2),
                        alloc(u::AllocType::kMemory, 2),
                        exec(15, 6),
                        alloc(u::AllocType::kMemory, 2),
                        exec(21, 5),
                        alloc(u::AllocType::kVsPosition),
                        exec(26, 1),
                        alloc(u::AllocType::kVsInterpolators),
                        exec(5, 0, 0, 0, true)};
  for (size_t i = 0; i < flow.size(); i += 2)
    code[i / 2] = {uint32_t(flow[i]), uint32_t(flow[i] >> 32) | (uint32_t(flow[i + 1]) << 16),
                   uint32_t(flow[i + 1] >> 16)};
  // Load eight 64-bit elements per vertex from fetch constant 95, undoing 8-in-32 swapping.
  constexpr uint32_t yxwz = 1 | (3 << 6) | (2 << 9);
  for (uint32_t i = 0; i < 8; ++i) {
    const bool full = i % 4 == 0;
    code[5 + i] = {(8 - i) << 12 | (1 << 19) | (31 << 20) | (2 << 25) | (full ? 7u << 27 : 0),
                   yxwz | (1 << 13) | (26 << 16) | (full ? 0 : 1u << 30), 16 | (i * 2 << 8)};
  }
  constexpr uint32_t temp1 = 1u << 31, temp2 = 1u << 30, temp3 = 1u << 29;
  constexpr uint32_t xxxx = (3 << 2) | (2 << 4) | (1 << 6);
  const auto alu = [](u::AluVectorOpcode op, uint32_t destination, uint32_t sources,
                      uint32_t swizzles = 0, bool exporting = true, uint32_t mask = 15) {
    return instruction{destination | (uint32_t(exporting) << 15) | (mask << 16) |
                           (uint32_t(u::AluScalarOpcode::kRetainPrev) << 26),
                       swizzles, sources | (uint32_t(op) << 24)};
  };
  // Rebase the export index by c7.x, then emit two groups of four memory exports.
  code[13] = alu(u::AluVectorOpcode::kAdd, 0, (7 << 8) | temp1 | temp3,
                 (xxxx << 16) | (xxxx << 8) | (1 << 25), false, 1);
  code[14] = {1 << 20 | uint32_t(u::AluScalarOpcode::kMulsc0) << 26, 0,
              255 | uint32_t(u::AluVectorOpcode::kMax) << 24 | temp1 | temp2};
  for (uint32_t group = 0; group < 2; ++group) {
    const uint32_t first = 15 + group * 6;
    code[first] =
        alu(u::AluVectorOpcode::kMad, 32, 6 | (255 << 8) | temp1, (xxxx << 16) | (0x60 << 8));
    for (uint32_t i = 0; i < 4; ++i) {
      const uint32_t source = 8 - group * 4 - i;
      code[first + 1 + i] = alu(u::AluVectorOpcode::kMax, 33 + i,
                                (source << 16) | (source << 8) | temp1 | temp2 | temp3);
    }
  }
  code[20] = {1 << 20 | uint32_t(u::AluScalarOpcode::kAddsc0) << 26, 3 << 6,
              255 | uint32_t(u::AluVectorOpcode::kMax) << 24 | temp1 | temp2};
  code[26] = alu(u::AluVectorOpcode::kSgt, 62, (128 << 16) | (255 << 8) | temp1 | temp3,
                 (xxxx << 16) | (xxxx << 8) | (1 << 26));
  shader_program program{loading_copy_hash, true, {}};
  for (const auto& instruction : code)
    for (uint32_t word : instruction)
      for (unsigned byte = 0; byte < 4; ++byte)
        program.code.push_back(uint8_t(word >> (24 - byte * 8)));
  if (XXH3_64bits(program.code.data(), program.code.size()) != loading_copy_hash)
    throw std::runtime_error("Generated loading copy shader fingerprint mismatch");
  return program;
}

bool read_line(std::istream& file, std::string& line) {
  if (!std::getline(file, line))
    return false;
  if (line.ends_with('\r'))
    line.pop_back();
  return true;
}

uint32_t be32(std::span<const uint8_t> data, size_t offset) {
  return uint32_t(data[offset]) << 24 | uint32_t(data[offset + 1]) << 16 |
         uint32_t(data[offset + 2]) << 8 | data[offset + 3];
}
uint32_t le32(std::span<const uint8_t> data, size_t offset) {
  return uint32_t(data[offset]) | uint32_t(data[offset + 1]) << 8 |
         uint32_t(data[offset + 2]) << 16 | uint32_t(data[offset + 3]) << 24;
}
uint64_t le64(std::span<const uint8_t> data, size_t offset) {
  return uint64_t(le32(data, offset)) | uint64_t(le32(data, offset + 4)) << 32;
}
void append32(std::vector<uint8_t>& data, uint32_t word) {
  for (unsigned i = 0; i < 4; ++i)
    data.push_back(uint8_t(word >> (8 * i)));
}
void append64(std::vector<uint8_t>& data, uint64_t word) {
  append32(data, uint32_t(word));
  append32(data, uint32_t(word >> 32));
}
std::vector<uint8_t> read_file(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file || file.tellg() < 0 || uint64_t(file.tellg()) > max_file_size)
    throw std::runtime_error("Could not read shader input: " + path.string());
  std::vector<uint8_t> data(size_t(file.tellg()));
  file.seekg(0);
  if (!file.read(reinterpret_cast<char*>(data.data()), data.size()))
    throw std::runtime_error("Incomplete shader input: " + path.string());
  return data;
}
void publish(const std::filesystem::path& path, std::span<const uint8_t> data) {
  if (!path.parent_path().empty())
    std::filesystem::create_directories(path.parent_path());
  auto temporary = path;
  temporary += ".tmp";
  std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
  file.write(reinterpret_cast<const char*>(data.data()), data.size());
  file.close();
  if (!file)
    throw std::runtime_error("Could not write shader inventory: " + temporary.string());
  for (unsigned attempt = 0;; ++attempt) {
    if (MoveFileExW(temporary.c_str(), path.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
      return;
    const auto error = GetLastError();
    // A held destination can report Access denied during replacement.
    if (attempt == 9 || (error != ERROR_SHARING_VIOLATION && error != ERROR_LOCK_VIOLATION &&
                         error != ERROR_ACCESS_DENIED))
      throw std::runtime_error("Could not publish shader inventory (Windows error " +
                               std::to_string(error) + "): " + path.string());
    Sleep(25);
  }
}
void add_programs(std::map<uint64_t, shader_program>& programs,
                  std::vector<shader_program> additions) {
  for (auto& program : additions) {
    const auto found = programs.find(program.hash);
    if (found != programs.end()) {
      if (found->second.vertex != program.vertex || found->second.code != program.code)
        throw std::runtime_error("Shader fingerprint collision");
    } else {
      programs.emplace(program.hash, std::move(program));
    }
  }
}
std::vector<uint8_t> serialize_programs(const std::map<uint64_t, shader_program>& programs) {
  std::vector<uint8_t> data(shader_header.begin(), shader_header.end());
  for (const auto& [hash, program] : programs) {
    append64(data, hash);
    append32(data, uint32_t(program.code.size() / 4) | (uint32_t(!program.vertex) << 31));
    data.insert(data.end(), program.code.begin(), program.code.end());
  }
  return data;
}
descriptions read_pipelines(std::span<const uint8_t> data) {
  descriptions result;
  if (data.size() < pipeline_header.size() ||
      !std::ranges::equal(data.first(pipeline_header.size()), pipeline_header) ||
      (data.size() - pipeline_header.size()) % (description_size + 8))
    throw std::runtime_error("Unsupported or incomplete D3D12 ROV pipeline cache");
  for (size_t offset = pipeline_header.size(); offset < data.size();
       offset += description_size + 8) {
    const auto description = data.subspan(offset + 8, description_size);
    const auto hash = XXH3_64bits(description.data(), description.size());
    if (hash != le64(data, offset))
      throw std::runtime_error("Pipeline cache checksum mismatch");
    std::ranges::copy(description, result[hash].begin());
  }
  return result;
}
descriptions parse_inventory(std::string_view text) {
  std::istringstream file{std::string(text)};
  std::string line;
  if (!read_line(file, line) || line != "lollipop-pipelines-v1 d3d12-rov 20260226")
    throw std::runtime_error("Unsupported pipeline inventory");
  descriptions result;
  while (read_line(file, line)) {
    if (line.empty() || line.starts_with('#') || line.starts_with("variant ") ||
        line.starts_with("executable "))
      continue;
    if (line.size() != description_size * 2)
      throw std::runtime_error("Invalid pipeline inventory record");
    std::array<uint8_t, description_size> description{};
    for (size_t i = 0; i < description.size(); ++i) {
      unsigned value = 0;
      const char* begin = line.data() + i * 2;
      const auto [end, error] = std::from_chars(begin, begin + 2, value, 16);
      if (error != std::errc{} || end != begin + 2)
        throw std::runtime_error("Invalid pipeline inventory encoding");
      description[i] = uint8_t(value);
    }
    result.emplace(XXH3_64bits(description.data(), description.size()), description);
  }
  return result;
}
void apply_executable(std::string_view text, std::span<const uint8_t> image,
                      std::map<uint64_t, shader_program>& programs) {
  std::istringstream file{std::string(text)};
  std::string line;
  while (read_line(file, line)) {
    if (!line.starts_with("executable "))
      continue;
    std::istringstream fields(line);
    std::string tag;
    uint64_t hash = 0, offset = 0, size = 0;
    unsigned flags = 0;
    fields >> tag >> std::hex >> hash >> offset >> size >> flags;
    if (!fields || !(fields >> std::ws).eof() || flags > 3 || !size || size % 4 ||
        offset > image.size() || size > image.size() - offset)
      throw std::runtime_error("Invalid executable shader location");
    const auto source = image.subspan(size_t(offset), size_t(size));
    std::vector<uint8_t> code(source.begin(), source.end());
    if (flags & 2)
      for (size_t i = 0; i < code.size(); i += 4)
        std::reverse(code.begin() + i, code.begin() + i + 4);
    if (XXH3_64bits(code.data(), code.size()) != hash)
      throw std::runtime_error("Executable shader does not match the supported disc");
    add_programs(programs, {{hash, bool(flags & 1), std::move(code)}});
  }
}
void apply_variants(std::string_view text, std::map<uint64_t, shader_program>& programs) {
  std::istringstream file{std::string(text)};
  std::string line;
  while (read_line(file, line)) {
    if (!line.starts_with("variant "))
      continue;
    std::istringstream fields(line);
    std::string tag;
    uint64_t target = 0, source = 0;
    fields >> tag >> std::hex >> target >> source;
    if (!fields || !target || !programs.contains(source))
      throw std::runtime_error("Missing base shader for recorded variant");
    auto variant = programs.at(source);
    variant.hash = target;
    uint32_t index, mask, bits;
    size_t previous = 0, count = 0;
    while (fields >> std::ws && !fields.eof()) {
      if (!(fields >> index >> mask >> bits) || index >= variant.code.size() / 4 ||
          (count && index <= previous) || !mask || (bits & ~mask))
        throw std::runtime_error("Invalid shader variant patch");
      const uint32_t word = (be32(variant.code, index * 4) & ~mask) | bits;
      for (unsigned byte = 0; byte < 4; ++byte)
        variant.code[index * 4 + byte] = uint8_t(word >> (24 - byte * 8));
      previous = index;
      ++count;
    }
    if (!count || count > variant.code.size() / 8 ||
        XXH3_64bits(variant.code.data(), variant.code.size()) != target)
      throw std::runtime_error("Shader variant checksum mismatch");
    add_programs(programs, {std::move(variant)});
  }
}
void scan_package(std::span<const uint8_t> data, std::map<uint64_t, shader_program>& programs) {
  add_programs(programs, scan_shader_containers(data));
  std::vector<uint8_t> plain;
  constexpr std::array<uint8_t, 8> tag{0x9e, 0x2a, 0x83, 0xc1, 0, 2, 0, 0};
  size_t search = 0;
  while (search + 16 <= data.size()) {
    const auto match = std::ranges::search(data.subspan(search), tag);
    if (match.empty())
      break;
    const size_t offset = size_t(match.begin() - data.begin());
    search = offset + 8;
    const size_t packed = be32(data, offset + 8), decoded = be32(data, offset + 12);
    if (!packed || !decoded || decoded > max_file_size - plain.size())
      throw std::runtime_error("Invalid compressed shader package size");
    const size_t blocks = (decoded + 0x1ffff) / 0x20000;
    size_t cursor = offset + 16 + blocks * 8;
    if (cursor > data.size() || packed > data.size() - cursor)
      throw std::runtime_error("Incomplete compressed shader package");
    const size_t start = plain.size();
    plain.resize(start + decoded);
    size_t written = 0, consumed = 0;
    for (size_t block = 0; block < blocks; ++block) {
      const size_t source_size = be32(data, offset + 16 + block * 8);
      const size_t target_size = be32(data, offset + 20 + block * 8);
      if (!source_size || target_size != std::min(decoded - written, size_t(0x20000)) ||
          source_size > packed - consumed ||
          !decode_package_lzx(data.subspan(cursor, source_size),
                              std::span(plain).subspan(start + written, target_size)))
        throw std::runtime_error("Could not decode shader package at " + std::to_string(cursor) +
                                 " (" + std::to_string(source_size) + "/" +
                                 std::to_string(target_size) + " bytes)");
      cursor += source_size;
      consumed += source_size;
      written += target_size;
    }
    if (consumed != packed || written != decoded)
      throw std::runtime_error("Shader package block sizes do not match");
    search = cursor;
  }
  add_programs(programs, scan_shader_containers(plain));
}
} // namespace

static std::vector<shader_program> scan_raw_containers(std::span<const uint8_t> data) {
  std::vector<shader_program> programs;
  for (size_t offset = 0; offset + 28 <= data.size(); ++offset) {
    const uint32_t signature = be32(data, offset);
    if (signature != 0x102a1100 && signature != 0x102a1101)
      continue;
    const auto bytes = data.subspan(offset);
    const size_t virtual_size = be32(bytes, 4), physical_size = be32(bytes, 8);
    const size_t header = be32(bytes, 24);
    const bool vertex = (signature & 1) != 0;
    if (virtual_size > bytes.size() || physical_size > bytes.size() - virtual_size ||
        header > virtual_size || (vertex ? 40u : 32u) > virtual_size - header)
      continue;
    const size_t start = be32(bytes, header), length = be32(bytes, header + 4);
    if (!length || length % 4 || start > physical_size || length > physical_size - start)
      continue;
    const auto code = bytes.subspan(virtual_size + start, length);
    programs.push_back({XXH3_64bits(code.data(), code.size()), vertex,
                        std::vector<uint8_t>(code.begin(), code.end())});
  }
  return programs;
}

std::vector<shader_program> scan_shader_containers(std::span<const uint8_t> data) {
  auto programs = scan_raw_containers(data);
  for (size_t offset = 0; offset + 22 <= data.size(); ++offset) {
    if (data[offset + 8] != 0xff)
      continue;
    const size_t decoded = be32(data, offset), packed = be32(data, offset + 4);
    if (decoded < 64 || decoded > 0x8000 || packed < 14 || packed > data.size() - offset - 8 ||
        decoded != (size_t(data[offset + 9]) << 8 | data[offset + 10]) ||
        packed != (size_t(data[offset + 11]) << 8 | data[offset + 12]) + 14)
      continue;
    std::vector<uint8_t> container(decoded);
    if (!decode_package_lzx(data.subspan(offset + 8, packed), container))
      continue;
    auto found = scan_raw_containers(container);
    programs.insert(programs.end(), std::make_move_iterator(found.begin()),
                    std::make_move_iterator(found.end()));
  }
  return programs;
}

std::vector<shader_program> read_shader_library(std::span<const uint8_t> data) {
  if (data.size() < shader_header.size() ||
      !std::ranges::equal(data.first(shader_header.size()), shader_header))
    throw std::runtime_error("Unsupported shader library");
  std::vector<shader_program> programs;
  size_t offset = shader_header.size();
  while (offset < data.size()) {
    if (data.size() - offset < 12)
      throw std::runtime_error("Incomplete shader library record");
    const auto hash = le64(data, offset);
    const uint32_t count_type = le32(data, offset + 8);
    const uint64_t length = uint64_t(count_type & 0x7fffffff) * 4;
    offset += 12;
    if (!length || length > data.size() - offset)
      throw std::runtime_error("Invalid shader library size");
    const auto code = data.subspan(offset, size_t(length));
    if (XXH3_64bits(code.data(), code.size()) != hash)
      throw std::runtime_error("Shader library checksum mismatch");
    programs.push_back(
        {hash, (count_type >> 31) == 0, std::vector<uint8_t>(code.begin(), code.end())});
    offset += size_t(length);
  }
  return programs;
}

void prepare_shader_inventory(const std::filesystem::path& game, const std::filesystem::path& cache,
                              std::string_view inventory, const shader_progress& progress,
                              std::span<const uint8_t> executable) {
  const auto directory = cache / "shaders/shareable";
  const auto library = cache / "shaders/disc-library.xsh";
  const auto identity = cache / "shaders/disc-library.id";
  std::vector<std::filesystem::path> files;
  std::string fingerprint = "lollipop-disc-shaders-v2\n";
  for (const auto& entry : std::filesystem::recursive_directory_iterator(game / "AshGame")) {
    if (!entry.is_regular_file() || (entry.path().extension() != ".xxx" &&
                                     entry.path().filename() != "GlobalShaderCache-Xbox360.bin"))
      continue;
    files.push_back(entry.path());
  }
  std::ranges::sort(files);
  for (const auto& path : files)
    fingerprint +=
        path.lexically_relative(game).generic_string() + ':' +
        std::to_string(std::filesystem::file_size(path)) + ':' +
        std::to_string(std::filesystem::last_write_time(path).time_since_epoch().count()) + '\n';
  const auto stamp = std::to_string(XXH3_64bits(fingerprint.data(), fingerprint.size()));
  std::map<uint64_t, shader_program> programs;
  std::ifstream stamp_file(identity);
  std::string previous;
  stamp_file >> previous;
  stamp_file.close();
  if (previous == stamp && std::filesystem::is_regular_file(library)) {
    try {
      add_programs(programs, read_shader_library(read_file(library)));
    } catch (const std::runtime_error&) {
      programs.clear();
      progress("Rebuilding disc shader cache", 0, 0);
    }
  }
  if (programs.empty()) {
    uint32_t completed = 0;
    progress("Reading game shaders", completed, uint32_t(files.size()));
    for (const auto& path : files) {
      try {
        scan_package(read_file(path), programs);
      } catch (const std::exception& error) {
        throw std::runtime_error(path.filename().string() + ": " + error.what());
      }
      progress("Reading game shaders", ++completed, uint32_t(files.size()));
    }
    if (programs.empty())
      throw std::runtime_error("No supported shaders were found on this disc");
    publish(library, serialize_programs(programs));
    publish(identity, {reinterpret_cast<const uint8_t*>(stamp.data()), stamp.size()});
  }
  const size_t disc_programs = programs.size();
  add_programs(programs, scan_shader_containers(executable));
  apply_executable(inventory, executable, programs);
  auto disc = programs;
  const auto runtime_library = directory / "565707D0.xsh";
  if (std::filesystem::is_regular_file(runtime_library)) {
    try {
      add_programs(programs, read_shader_library(read_file(runtime_library)));
    } catch (const std::runtime_error&) {
      programs = disc;
      progress("Rebuilding recorded shader cache", 0, 0);
    }
  }
  std::ostringstream locations;
  {
    std::istringstream records{std::string(inventory)};
    std::string line;
    while (read_line(records, line))
      if (line.starts_with("executable "))
        locations << line << '\n';
  }
  for (const auto& [hash, program] : programs) {
    if (disc.contains(hash) || executable.empty())
      continue;
    auto found = std::ranges::search(executable, program.code);
    bool swapped = false;
    if (found.empty()) {
      auto code = program.code;
      for (size_t i = 0; i < code.size(); i += 4)
        std::reverse(code.begin() + i, code.begin() + i + 4);
      found = std::ranges::search(executable, code);
      swapped = true;
    }
    if (found.empty())
      continue;
    const auto offset = found.begin() - executable.begin();
    locations << "executable " << std::hex << hash << ' ' << offset << ' ' << program.code.size()
              << ' ' << (unsigned(program.vertex) | (unsigned(swapped) << 1)) << '\n';
    disc.emplace(hash, program);
  }
  if (disc.size() != disc_programs)
    publish(library, serialize_programs(disc));
  const auto executable_locations = locations.str();
  if (!executable_locations.empty())
    publish(cache / "shaders/executable-inventory.txt",
            {reinterpret_cast<const uint8_t*>(executable_locations.data()),
             executable_locations.size()});
  auto pipelines = parse_inventory(inventory);
  if (std::ranges::any_of(
          pipelines, [](const auto& entry) { return le64(entry.second, 0) == loading_copy_hash; }))
    add_programs(programs, {make_loading_copy_shader()});
  apply_variants(inventory, programs);
  const auto missing_shader = [&](const auto& entry) {
    for (size_t offset : {size_t(0), size_t(16)}) {
      const auto shader = le64(entry.second, offset);
      if (shader && !programs.contains(shader))
        return true;
    }
    return false;
  };
  if (std::ranges::any_of(pipelines, missing_shader))
    throw std::runtime_error("Pipeline inventory needs a shader variant not yet collected");
  const auto pipeline_file = directory / "565707D0.rov.d3d12.xpso";
  if (std::filesystem::is_regular_file(pipeline_file)) {
    try {
      auto cached = read_pipelines(read_file(pipeline_file));
      std::erase_if(cached, missing_shader);
      pipelines.merge(cached);
    } catch (const std::runtime_error&) {
      progress("Rebuilding recorded pipeline cache", 0, 0);
    }
  }
  publish(runtime_library, serialize_programs(programs));
  std::vector<uint8_t> data(pipeline_header.begin(), pipeline_header.end());
  for (const auto& [hash, description] : pipelines) {
    append64(data, hash);
    data.insert(data.end(), description.begin(), description.end());
  }
  publish(pipeline_file, data);
  progress("Game shader inventory ready", uint32_t(programs.size()), uint32_t(programs.size()));
}

void collect_shader_inventory(const std::filesystem::path& cache,
                              const std::filesystem::path& output, std::string_view coverage) {
  if (coverage.empty() || coverage.find_first_of("\r\n") != std::string_view::npos)
    throw std::runtime_error("Describe the scenes covered by this collection");
  const auto source = cache / "shaders/shareable/565707D0.rov.d3d12.xpso";
  auto pipelines = read_pipelines(read_file(source));
  std::string previous_text;
  if (std::filesystem::is_regular_file(output)) {
    const auto previous = read_file(output);
    previous_text.assign(reinterpret_cast<const char*>(previous.data()), previous.size());
    pipelines.merge(parse_inventory(previous_text));
  }
  std::map<uint64_t, shader_program> disc;
  add_programs(disc, read_shader_library(read_file(cache / "shaders/disc-library.xsh")));
  add_programs(disc, {make_loading_copy_shader()});
  auto observed = disc;
  add_programs(observed, read_shader_library(read_file(cache / "shaders/shareable/565707D0.xsh")));
  apply_variants(previous_text, observed);
  std::string text =
      "lollipop-pipelines-v1 d3d12-rov 20260226\n# Coverage: " + std::string(coverage) + '\n';
  {
    std::istringstream previous(previous_text);
    std::string line;
    while (read_line(previous, line))
      if (line.starts_with("# Coverage: ") && line != "# Coverage: " + std::string(coverage))
        text += line + '\n';
  }
  const auto executable_locations = cache / "shaders/executable-inventory.txt";
  if (std::filesystem::is_regular_file(executable_locations)) {
    const auto bytes = read_file(executable_locations);
    text.append(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  }
  std::map<uint64_t, bool> needed;
  for (const auto& [hash, description] : pipelines)
    for (size_t offset : {size_t(0), size_t(16)})
      if (const auto shader = le64(description, offset))
        needed.emplace(shader, true);
  for (const auto& [hash, unused] : needed) {
    if (disc.contains(hash))
      continue;
    if (!observed.contains(hash))
      throw std::runtime_error("Recorded pipeline has no shader program");
    const auto& variant = observed.at(hash);
    const shader_program* base = nullptr;
    size_t differences = variant.code.size() / 8 + 1;
    for (const auto& [candidate_hash, candidate] : disc) {
      if (candidate.vertex != variant.vertex || candidate.code.size() != variant.code.size())
        continue;
      size_t changed = 0;
      for (size_t offset = 0; offset < variant.code.size(); offset += 4)
        changed += be32(variant.code, offset) != be32(candidate.code, offset);
      if (changed < differences) {
        differences = changed;
        base = &candidate;
      }
    }
    if (!base || differences > variant.code.size() / 8)
      throw std::runtime_error("Cannot reconstruct recorded shader " + std::to_string(hash) +
                               " from this disc");
    std::ostringstream record;
    record << "variant " << std::hex << hash << ' ' << base->hash;
    for (size_t offset = 0; offset < variant.code.size(); offset += 4) {
      const uint32_t word = be32(variant.code, offset), original = be32(base->code, offset);
      if (word != original)
        record << ' ' << offset / 4 << ' ' << (word ^ original) << ' '
               << (word & (word ^ original));
    }
    text += record.str() + '\n';
  }
  constexpr char hex[] = "0123456789abcdef";
  for (const auto& [hash, description] : pipelines) {
    for (uint8_t byte : description) {
      text += hex[byte >> 4];
      text += hex[byte & 15];
    }
    text += '\n';
  }
  publish(output, {reinterpret_cast<const uint8_t*>(text.data()), text.size()});
}
} // namespace lollipop
