#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>

namespace rex {
class Runtime;
namespace filesystem {
class Device;
}
} // namespace rex

namespace lollipop {

// Confirm, back, previous/next page, navigation, settings; 128px RGBA.
std::array<std::vector<uint8_t>, 6> make_settings_prompt_artwork(std::span<const uint8_t> startup,
                                                                 bool keyboard);
std::vector<uint8_t> make_keyboard_prompts(std::span<const uint8_t> startup,
                                           bool controller = false);
struct prompt_variants {
  std::vector<uint8_t> keyboard, gamepad;
  uint64_t keyboard_hash, gamepad_hash;
};
std::vector<prompt_variants> make_prompt_variants(std::span<const uint8_t> keyboard,
                                                  std::span<const uint8_t> gamepad);
std::span<const uint8_t> select_prompt_artwork(std::span<const prompt_variants> variants,
                                               std::span<const uint8_t> texture, bool keyboard);
std::vector<uint8_t> make_keyboard_title(std::span<const uint8_t> coalesced);
std::vector<uint8_t> pack_keyboard_prompts(std::span<const uint8_t> package,
                                           const std::filesystem::path& directory);
std::unique_ptr<rex::filesystem::Device>
make_keyboard_game_device(const std::filesystem::path& game, const std::filesystem::path& cache);
void install_keyboard_prompts(rex::Runtime& runtime, const std::filesystem::path& game_root,
                              const std::filesystem::path& cache_root);

} // namespace lollipop
