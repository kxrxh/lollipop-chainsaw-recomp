#include "game/dlc_package.h"

#include <iostream>
#include <stdexcept>
#include <string_view>

#include <rex/app/app.h>
#include <rex/string/text.h>

namespace {
class dlc_reader final : public rex::app::App {
public:
  explicit dlc_reader(const std::filesystem::path& staging) : App("lollipop-dlc") {
    const std::string args[] = {"--ui.driver=none",
                                "--gpu.driver=none",
                                "--apu.driver=none",
                                "--hid.drivers=none",
                                "--app.root=" + rex::string::PathToUTF8(staging.parent_path()),
                                "--config_file=" +
                                    rex::string::PathToUTF8(staging.parent_path() / "reader.toml")};
    if (auto initialized = Init(rex::CLIArgList::Parse(args)); !initialized) {
      Shutdown();
      throw std::runtime_error(initialized.error().GetDescription());
    }
  }
  ~dlc_reader() override { Shutdown(); }
};
} // namespace

int wmain(int argc, wchar_t** argv) try {
  if (argc != 4 || std::wstring_view(argv[1]) != L"prepare")
    throw std::runtime_error("Usage: lollipop_dlc_tool prepare <package> <new staging folder>");
  dlc_reader reader(argv[3]);
  lollipop::prepare_dlc_package(argv[2], argv[3]);
  return 0;
} catch (const std::exception& error) {
  std::cerr << error.what() << '\n';
  return 1;
}
