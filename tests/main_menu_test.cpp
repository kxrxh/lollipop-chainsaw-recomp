#include "game/main_menu.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string_view>

namespace {
std::vector<uint8_t> load(const char* path) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    throw std::runtime_error("Missing menu fixture");
  return {std::istreambuf_iterator<char>(input), {}};
}
uint32_t read(const std::vector<uint8_t>& data, size_t at) {
  return (uint32_t(data.at(at)) << 24) | (uint32_t(data.at(at + 1)) << 16) |
         (uint32_t(data.at(at + 2)) << 8) | data.at(at + 3);
}
void check(bool condition, const char* message) {
  if (!condition)
    throw std::runtime_error(message);
}
} // namespace

int main(int argc, char** argv) {
  try {
    if (argc != 3)
      throw std::runtime_error("Expected game package and artwork");
    auto source = load(argv[1]);
    const auto artwork = load(argv[2]);
    const auto menu = lollipop::make_main_menu(source, artwork);
    check(menu.size() > 8331940, "The new movie must be appended without moving existing exports");
    check(read(menu, 109) == 0 && read(menu, 113) == 0, "Clear internal compression metadata");
    const auto offset = read(menu, 33378 + 36), size = read(menu, 33378 + 32);
    check(offset == 8331940 && offset + size == menu.size(), "Update only the menu export");
    const auto swf_size = read(menu, offset + 153);
    check(read(menu, offset + 145) == swf_size + 4 && size == swf_size + 225,
          "Update RawData property and movie sizes together");
    for (std::string_view symbol :
         {"FSCommand:FsOptionOpen", "FsQuitGame", "MovButtonFocus_2", "Sel_2"})
      check(!std::ranges::search(std::span(menu).subspan(offset), symbol).empty(),
            "Install native selection, focus animation and quit command");
    source[1000] ^= 1;
    bool rejected = false;
    try {
      lollipop::make_main_menu(source, artwork);
    } catch (const std::runtime_error&) {
      rejected = true;
    }
    check(rejected, "Reject a modified or unsupported game package");
    source[1000] ^= 1;
    rejected = false;
    try {
      lollipop::make_main_menu(source, std::span(artwork).first(16));
    } catch (const std::runtime_error&) {
      rejected = true;
    }
    check(rejected, "Reject malformed artwork");
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
