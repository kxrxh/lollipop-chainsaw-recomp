#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace lollipop {

std::vector<uint8_t> make_main_menu(std::span<const uint8_t> package,
                                    std::span<const uint8_t> artwork);
void cache_main_menu(const std::filesystem::path& game, const std::filesystem::path& cache);

} // namespace lollipop
