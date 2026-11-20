#pragma once
#include <rex/settings_registry.h>
#include <string>
#include <string_view>

namespace lollipop::settings {
using Source = rex::SettingLayer;

// Old command-line and profile names remain part of the game's public interface.
void register_settings();
std::string setting_name(std::string_view name);
rex::SettingBase* GetFlagInfo(std::string_view name);
std::string GetFlagByName(std::string_view name);
Source GetFlagSource(std::string_view name);
bool SetFlagByName(std::string_view name, std::string_view value);
bool SetFlagFromCommandLine(std::string_view name, std::string_view value);
template <typename T> T Query(std::string_view name) {
  T result{};
  rex::detail::ParseSetting(GetFlagByName(name), result);
  return result;
}
} // namespace lollipop::settings
