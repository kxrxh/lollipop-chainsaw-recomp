#pragma once

#include "settings/runtime_settings.h"
#include <map>
#include <memory>
#include <stdexcept>
#include <vector>

namespace lollipop::testing {
// Isolated unit tests do not start every SDK module that owns a title setting.
class settings_fixture {
public:
  void add(std::string_view name, std::string value) {
    if (settings::GetFlagInfo(name))
      return;
    auto full = settings::setting_name(name);
    auto dot = full.rfind('.');
    if (dot == full.npos)
      throw std::invalid_argument(full);
    auto owner = std::make_unique<rex::SettingOwner>(full.substr(0, dot));
    auto item = std::make_unique<rex::Setting<std::string>>(owner.get(), full.substr(dot + 1),
                                                            value, "Test setting");
    auto ok = REX_SETTINGS().Register(*owner);
    if (!ok)
      throw std::runtime_error(ok.error().GetDescription());
    owners_.push_back(std::move(owner));
    values_.push_back(std::move(item));
  }

private:
  std::vector<std::unique_ptr<rex::SettingOwner>> owners_;
  std::vector<std::unique_ptr<rex::Setting<std::string>>> values_;
};
} // namespace lollipop::testing
