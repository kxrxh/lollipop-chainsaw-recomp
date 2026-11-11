#pragma once

#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace lollipop {

struct pc_choice {
  const char* value;
  const char* label;
};
struct pc_setting {
  const char* name;
  const char* label;
  const char* help;
  const char* recommended;
  int page;
  std::vector<pc_choice> choices;
  float minimum = 0, maximum = 0, step = 0;
};
using pc_values = std::map<std::string, std::string>;

std::span<const pc_setting> pc_settings();
pc_values read_pc_settings();
bool validate_pc_settings(const pc_values& values);
bool save_pc_settings(const std::filesystem::path& path, const pc_values& values,
                      std::string& error);
void configure_pc_settings(const std::filesystem::path& path);

} // namespace lollipop
