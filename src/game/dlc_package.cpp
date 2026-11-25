#include "game/dlc_package.h"

#include <algorithm>
#include <format>
#include <fstream>
#include <stdexcept>

#include <rex/filesystem/file_system_factory.h>
#include <rex/filesystem/stfs.h>

namespace lollipop {
namespace {
void write_bytes(const std::filesystem::path& path, const void* bytes, size_t size) {
  std::ofstream file(path, std::ios::binary);
  if (!file.write(static_cast<const char*>(bytes), size) || !file.flush())
    throw std::runtime_error("Could not write the prepared DLC package.");
}
} // namespace

void prepare_dlc_package(const std::filesystem::path& source,
                         const std::filesystem::path& staging) {
  auto* factory = REX_INTERFACE(rex::filesystem::IFileSystemFactory);
  if (!factory)
    throw std::runtime_error("The DLC reader is unavailable. Repair the game first.");
  auto opened = factory->OpenContainer(source);
  if (!opened)
    throw std::runtime_error(opened.error().GetDescription());
  const auto& container = *opened.value();
  const auto* header = container.GetHeader();
  if (!header || header->metadata.execution_info.title_id != 0x565707D0 ||
      header->metadata.content_type != rex::kernel::XContentType::kMarketplaceContent ||
      container.GetType() != rex::filesystem::ContainerType::kSTFS)
    throw std::runtime_error("Choose Xbox 360 DLC for Lollipop Chainsaw (565707D0).");
  if (!container.Find("ASHGAMEINFO.INI"))
    throw std::runtime_error("This package does not contain Lollipop Chainsaw costume data.");
  size_t total = 0;
  if (container.GetNodes().size() > 4096)
    throw std::runtime_error("The costume package contains too many files.");
  for (const auto& node : container.GetNodes()) {
    if (node.info.size > 256 * 1024 * 1024 - total)
      throw std::runtime_error("The costume package is too large to extract.");
    total += static_cast<size_t>(node.info.size);
  }
  if (std::filesystem::exists(staging))
    throw std::runtime_error("The DLC staging directory already exists.");

  std::filesystem::create_directories(staging);
  auto extracted = container.Extract(staging / "content");
  if (!extracted)
    throw std::runtime_error(extracted.error().GetDescription());

  rex::kernel::XCONTENT_AGGREGATE_DATA data{};
  data.device_id = 1;
  data.content_type = rex::kernel::XContentType::kMarketplaceContent;
  data.title_id = 0x565707D0;
  data.xuid = 0;
  data.SetDisplayName(header->metadata.GetDisplayName(rex::kernel::XLanguage::kEnglish));
  std::string id;
  for (const auto byte : header->header.content_id)
    id += std::format("{:02X}", byte);
  data.SetFileName(id);
  // Preserve the package's license bits exactly as the SDK content installer does.
  rex::u32 mask = 0;
  for (const auto& license : header->header.licenses)
    if (license.license_flags)
      mask |= license.license_bits;
  std::ofstream file(staging / "content.header", std::ios::binary);
  if (!file.write(reinterpret_cast<const char*>(&data), sizeof(data)) ||
      !file.write(reinterpret_cast<const char*>(&mask), sizeof(mask)) || !file.flush())
    throw std::runtime_error("Could not write the DLC header.");
  const auto thumbnail_size =
      std::min<size_t>(header->metadata.thumbnail_size, sizeof(header->metadata.thumbnail));
  if (thumbnail_size)
    write_bytes(staging / "content" / "__thumbnail.png", header->metadata.thumbnail,
                thumbnail_size);
}
} // namespace lollipop
