#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <string_view>
#include <vector>

namespace lollipop {
struct shader_program {
  uint64_t hash = 0;
  bool vertex = false;
  std::vector<uint8_t> code;
};
using shader_progress = std::function<void(std::string_view, uint32_t, uint32_t)>;

std::vector<shader_program> scan_shader_containers(std::span<const uint8_t> data);
std::vector<shader_program> read_shader_library(std::span<const uint8_t> data);
void prepare_shader_inventory(const std::filesystem::path& game, const std::filesystem::path& cache,
                              std::string_view inventory, const shader_progress& progress,
                              std::span<const uint8_t> executable = {});
void collect_shader_inventory(const std::filesystem::path& cache,
                              const std::filesystem::path& output, std::string_view coverage);
} // namespace lollipop
