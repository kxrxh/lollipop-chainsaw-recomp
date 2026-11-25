#pragma once

#include <filesystem>

namespace lollipop {
// Prepares a package in a new staging directory; the launcher publishes it after success.
void prepare_dlc_package(const std::filesystem::path& source, const std::filesystem::path& staging);
} // namespace lollipop
