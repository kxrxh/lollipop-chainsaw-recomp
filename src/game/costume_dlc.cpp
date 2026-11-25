#include "game/costume_dlc.h"

#include <atomic>

#include <rex/filesystem/file_system.h>
#include <rex/kernel/title_runtime.h>
#include <rex/logging/log_category.h>
#include <rex/xam/content_manager.h>

#include "game/game_bridge.h"

namespace lollipop {
namespace {
std::atomic<bool> pending{};
bool loaded = false; // Engine tick thread only.
} // namespace

void request_costume_dlc_load() { pending.store(true); }

void load_installed_costumes(rex::kernel::TitleRuntime& runtime, game_bridge& game) {
  if (!pending.exchange(false) || loaded)
    return;
  loaded = true;
  auto& content = runtime.GetContentManager();
  const auto packages = content.ListContent(1, 0, rex::kernel::XContentType::kMarketplaceContent);
  if (packages.empty())
    return;
  const auto manager = game.find("Transient.DownloadableContentManager_0");
  const auto install = game.find("Engine.DownloadableContentManager.InstallDLC");
  if (!manager || !install) {
    REXLOG_WARN("The native costume DLC manager is unavailable");
    return;
  }
  for (const auto& package : packages) {
    constexpr auto root = "lollipopcostume";
    // Use the runtime's normal content open before handing the mounted bundle to UE3.
    const auto opened = content.OpenContent(root, 0, package);
    if (!opened) {
      REXLOG_WARN("Could not open costume DLC {}: {}", package.GetFileName(),
                  opened.error().GetDescription());
      continue;
    }
    const rex::ScopeGuard close([&] { (void)content.CloseContent(root); });
    // UE3 prefixes config filenames with D:\; route that virtual folder to the open package.
    auto& files = runtime.GetFileSystem();
    constexpr auto link = "\\Device\\LollipopGame\\LollipopDlc";
    const auto linked = files.Link(link, "lollipopcostume:");
    if (!linked) {
      REXLOG_WARN("Could not link costume DLC {}", package.GetFileName());
      continue;
    }
    const rex::ScopeGuard unlink([&] { (void)files.Unlink(link); });
    // The stock enumerator requires an Xbox LIVE login even for locally installed content.
    if (game.install_costume_dlc(manager, install, package.GetFileName(), "LollipopDlc\\"))
      REXLOG_INFO("Loaded installed costume DLC {} through the native manager",
                  package.GetFileName());
    else
      REXLOG_WARN("The native manager could not load costume DLC {}", package.GetFileName());
  }
}
} // namespace lollipop
