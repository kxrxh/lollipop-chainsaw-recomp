#include "game/shader_inventory.h"

#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) try {
  if (argc == 5 && std::string_view(argv[1]) == "collect") {
    lollipop::collect_shader_inventory(argv[2], argv[3], argv[4]);
  } else if (argc == 4 && std::string_view(argv[1]) == "scan") {
    lollipop::prepare_shader_inventory(argv[2], argv[3],
                                       "lollipop-pipelines-v1 d3d12-rov 20260226\n",
                                       [](auto stage, auto done, auto total) {
                                         std::cout << stage << ' ' << done << '/' << total << '\n';
                                       });
  } else {
    throw std::runtime_error(
        "Usage: lollipop_shader_tool scan <game> <cache> | collect <cache> <output> <coverage>");
  }
} catch (const std::exception& error) {
  std::cerr << error.what() << '\n';
  return 1;
}
