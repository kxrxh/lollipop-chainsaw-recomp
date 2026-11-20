#include "input/keyboard_controls.h"
#include "game/package_lzx.h"
#include "input/prompt_artwork.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

#include "input_fixture.h"
#include "settings/runtime_settings.h"
#include "settings_fixture.h"

#include <rex/filesystem/file_system_factory.h>

namespace settings = lollipop::settings;

using lollipop::cache_keyboard_prompts;
using lollipop::configure_keyboard_controls;
using lollipop::make_keyboard_prompts;
using lollipop::make_keyboard_title;
using lollipop::make_prompt_variants;
using lollipop::make_settings_prompt_artwork;
using lollipop::select_prompt_artwork;

namespace {
void check(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}
}  // namespace

int main(int argc, char** argv) try {
  check(argc == 3, "Expected source and output paths");
  // A four-byte uncompressed LZX block, followed by the XMem terminator and UE trailer.
  std::vector<uint8_t> block = {0xff, 0, 4, 0, 20,  0,   0x30, 0x40, 0, 1, 0, 0, 0, 1, 0, 0, 0,
                                1,    0, 0, 0, 't', 'e', 's',  't',  0, 0, 0, 0, 0, 1, 2, 3, 4};
  std::array<uint8_t, 4> decoded{};
  check(lollipop::decode_package_lzx(block, decoded) &&
            decoded == std::array<uint8_t, 4>{'t', 'e', 's', 't'},
        "LZX frame and opaque trailer");
  for (size_t i = 0; i < block.size(); ++i)
    check(!lollipop::decode_package_lzx(std::span(block).first(i), decoded), "Truncated LZX frame");
  for (size_t i : {2, 4, 25}) {
    ++block[i];
    check(!lollipop::decode_package_lzx(block, decoded), "Invalid LZX frame bounds or terminator");
    --block[i];
  }
  input_fixture app("lollipop-keyboard-controls-test");
  lollipop::testing::settings_fixture fixture;
  fixture.add("readback_memexport", "true");
  fixture.add("readback_memexport_fast", "true");
  auto& input = app.GetInput();
  configure_keyboard_controls();
  check(settings::Query<bool>("readback_memexport") &&
            !settings::Query<bool>("readback_memexport_fast"),
        "Native prompt switching reads current GPU icon copies");
  check(settings::Query<double>("mnk_sensitivity") == 18.0,
        "Higher mouse camera sensitivity by default");
  check(settings::GetFlagByName("keybind_y") == "LMB", "Chainsaw default");
  settings::SetFlagByName("keybind_b", "Space,Backspace,J");
  configure_keyboard_controls();
  check(settings::GetFlagByName("keybind_b") == "Space,Backspace,J", "Preserve user bindings");
  settings::SetFlagByName("readback_memexport_fast", "true");
  settings::SetFlagByName("readback_memexport", "false");
  configure_keyboard_controls();
  check(settings::Query<bool>("readback_memexport_fast") &&
            !settings::Query<bool>("readback_memexport"),
        "Explicit renderer overrides are preserved");

  using rex::InputEvent;
  using rex::VirtualKey;
  using Button = rex::MouseButton;
  auto key = [&](VirtualKey vk, bool down) {
    InputEvent event{.type = down ? InputEvent::Type::kKeyDown : InputEvent::Type::kKeyUp,
                     .key = vk};
    input.OnInputEvent(event);
  };
  auto mouse = [&](Button button, bool down) {
    InputEvent event{.type = down ? InputEvent::Type::kButtonDown : InputEvent::Type::kButtonUp,
                     .button = button};
    input.OnInputEvent(event);
  };
  auto state = [&] {
    const auto value = input.GetState(0);
    check(bool(value), "Read keyboard state");
    return value->state;
  };
  auto focus = [&](bool focused) {
    InputEvent event{.type =
                         focused ? InputEvent::Type::kFocusGained : InputEvent::Type::kFocusLost};
    input.OnInputEvent(event);
  };
  auto move = [&](float x, float y) {
    InputEvent event{.type = InputEvent::Type::kMotion, .x = x, .y = y, .dx = x, .dy = y};
    input.OnInputEvent(event);
  };
  key(VirtualKey::kW, true);
  mouse(Button::kLeft, true);
  check(state().thumb_ly > 0 && state().buttons == 0x8000,
        "Move and chainsaw together");
  key(VirtualKey::kShift, true);
  auto aim = state();
  check(aim.thumb_ly > 0 && aim.left_trigger == 255 &&
            aim.right_trigger == 255 && aim.buttons == 0,
        "Shift aim preserves movement and fires without a chainsaw swing");
  key(VirtualKey::kShift, false);
  check(state().buttons == 0x8000 && state().right_trigger == 0,
        "Release aim restores chainsaw");
  mouse(Button::kLeft, false);
  mouse(Button::kRight, true);
  check(state().buttons == 0x4000, "Pom-pom bash");
  mouse(Button::kRight, false);
  mouse(Button::kMiddle, true);
  check(state().buttons == 0x1000, "Low slash");
  mouse(Button::kMiddle, false);
  key(VirtualKey::kReturn, true);
  check(state().buttons == 0x1000, "Enter confirms menus");
  key(VirtualKey::kReturn, false);
  key(VirtualKey::kSpace, true);
  check(state().buttons == 0x2000 && state().thumb_ly > 0,
        "Dodge while moving");
  input.SetInputCaptured(false);
  check(state().buttons == 0 && state().thumb_ly == 0,
        "SDK dialogs suppress keyboard gameplay input");
  input.SetInputCaptured(true);
  focus(false);
  check(state().buttons == 0 && state().thumb_ly == 0, "Focus loss releases keys");
  focus(true);
  check(state().buttons == 0 && state().thumb_ly == 0, "Focus regain stays released");
  const auto sample = [&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(18));
    return state();
  };
  input.SetInputCaptured(false);
  input.SetInputCaptured(true);
  move(5, 0);
  check(state().thumb_rx > 20000, "A small mouse movement produces a useful camera turn");
  input.SetInputCaptured(false);
  input.SetInputCaptured(true);
  move(1, -1);
  const auto first_mouse = state();
  const auto repeated_mouse = input.GetStateForUI(0).value();
  check(first_mouse.thumb_rx > 6553 && first_mouse.thumb_ry > 6553,
        "One-pixel mouse motion clears the game's 20 percent camera deadzone");
  check(first_mouse.thumb_rx == repeated_mouse.thumb_rx &&
            first_mouse.thumb_ry == repeated_mouse.thumb_ry,
        "Repeated guest/UI polls retain the same mouse stick sample");
  check(sample().thumb_rx == 0, "Camera stops when mouse motion stops");
  move(1000000, 1000000);
  auto fast = sample();
  check(fast.thumb_rx == 32767 && fast.thumb_ry == -32767,
        "Fast mouse motion clamps to the controller stick range");
  focus(false);
  focus(true);
  const auto reset = state();
  check(reset.thumb_rx == 0 && reset.thumb_ry == 0, "Focus changes discard cached mouse movement");

  auto& pad = app.pad();
  pad.set_connected(true);
  move(1, -1);
  check(input.IsKeyboardActive(), "Real mouse motion selects keyboard hints without a state poll");
  pad.state.thumb_lx = 200;
  state();
  check(input.IsKeyboardActive(), "Idle gamepad drift cannot steal hints");
  pad.state.buttons = 4096;
  state();
  check(!input.IsKeyboardActive(), "Controller button selects gamepad hints");
  key(VirtualKey::kH, true);
  state();
  check(input.IsKeyboardActive(),
        "An unbound key selects keyboard despite a held controller button");
  pad.state.thumb_lx = 24000;
  state();
  check(!input.IsKeyboardActive(), "Controller stick movement selects gamepad hints");
  key(VirtualKey::kH, true);
  check(!input.IsKeyboardActive(), "A held key repeat cannot steal hints from a gamepad");
  key(VirtualKey::kH, false);
  mouse(Button::kLeft, true);
  pad.state.buttons = 0;
  state();
  check(input.IsKeyboardActive(), "Releasing a button on a held stick does not take over hints");
  pad.state.buttons = 4096;
  state();
  check(!input.IsKeyboardActive(), "Repressing a pad button takes over while a stick stays held");
  pad.set_connected(false);
  settings::SetFlagByName("mnk_mode", "false");
  check(state().buttons == 0, "Controller-only mode disables keyboard");

  unsigned localizations = 0;
  for (const auto &entry : std::filesystem::directory_iterator(
           std::filesystem::path(argv[1]).parent_path())) {
    const auto name = entry.path().filename().string();
    if (!name.starts_with("Coalesced_") || !name.ends_with(".bin")) continue;
    std::ifstream file(entry.path(), std::ios::binary | std::ios::ate);
    check(bool(file), "Read title fixture");
    std::vector<uint8_t> original(static_cast<size_t>(file.tellg()));
    file.seekg(0);
    file.read(reinterpret_cast<char *>(original.data()), original.size());
    const auto title = make_keyboard_title(original);
    check(title.size() > original.size(),
          "Title localization provides separate keyboard and gamepad entries");
    const uint8_t keyboard[] = {'E', 0, 'S', 0, 'C', 0};
    check(std::search(title.begin(), title.end(), std::begin(keyboard),
                      std::end(keyboard)) != title.end(),
          "Title contains keyboard key");
    original.pop_back();
    bool truncated = false;
    try {
      make_keyboard_title(original);
    } catch (const std::runtime_error &) {
      truncated = true;
    }
    check(truncated, "Reject truncated localization");
    ++localizations;
  }
  check(localizations == 16, "Verify all eight languages and full variants");

  std::ifstream source(argv[1], std::ios::binary | std::ios::ate);
  check(source && source.tellg() == 294912, "Read startup fixture");
  std::vector<uint8_t> bytes(294912);
  source.seekg(0);
  source.read(reinterpret_cast<char *>(bytes.data()), bytes.size());
  auto artwork = make_keyboard_prompts(bytes);
  const auto controller_artwork = make_keyboard_prompts(bytes, true);
  const auto menu_pad = make_settings_prompt_artwork(bytes, false);
  const auto menu_keys = make_settings_prompt_artwork(bytes, true);
  for (size_t i = 0; i < menu_pad.size(); ++i) {
    check(menu_pad[i].size() == 128 * 128 * 4 && menu_keys[i].size() == 128 * 128 * 4,
          "Settings prompts are complete RGBA textures");
    check(menu_pad[i] != menu_keys[i], "Settings use device-specific artwork");
    size_t visible = 0, transparent = 0;
    for (size_t p = 3; p < menu_pad[i].size(); p += 4) {
      visible += menu_pad[i][p] > 128;
      transparent += menu_pad[i][p] == 0;
    }
    check(visible > 1000 && transparent > 1000, "Native settings icons retain shape and transparency");
  }
  auto channel_total = [](const auto& rgba, size_t channel) {
    uint64_t total = 0;
    for (size_t i = 0; i < rgba.size(); i += 4) total += uint64_t(rgba[i + channel]) * rgba[i + 3];
    return total;
  };
  check(channel_total(menu_pad[0], 1) > channel_total(menu_pad[0], 0) &&
        channel_total(menu_pad[1], 0) > channel_total(menu_pad[1], 1),
        "Native confirm stays green and back stays red after Xbox texture decoding");
  const auto variants = make_prompt_variants(artwork, controller_artwork);
  bool rejected_variants = false;
  try {
    make_prompt_variants(artwork, std::span(controller_artwork).first(100));
  } catch (const std::runtime_error &) {
    rejected_variants = true;
  }
  check(rejected_variants, "Reject truncated prompt variant packages");
  check(variants.size() == 30,
        "All shared prompt textures have both input variants");
  for (const auto &variant : variants) {
    check(std::ranges::equal(
              select_prompt_artwork(variants, variant.keyboard, false),
              variant.gamepad),
          "Keyboard artwork switches to its original controller icon");
    check(std::ranges::equal(
              select_prompt_artwork(variants, variant.gamepad, true),
              variant.keyboard),
          "Controller artwork switches back to the matching key");
    check(select_prompt_artwork(variants, variant.keyboard, true).empty() &&
              select_prompt_artwork(variants, variant.gamepad, false).empty(),
          "Unchanged input does not repeatedly upload prompt textures");
  }
  std::vector<uint8_t> unrelated(16384, 123);
  check(select_prompt_artwork(variants, unrelated, false).empty(),
        "Unrelated game textures are never replaced");
  auto reused = variants.front().keyboard;
  reused.front() ^= 1;
  check(select_prompt_artwork(variants, reused, false).empty(),
        "A matching sample cannot replace different bytes at a reused address");
  reused.front() ^= 1;
  check(
      std::ranges::equal(select_prompt_artwork(variants, reused, false), variants.front().gamepad),
      "New prompt data at the same address is recognized");
  check(artwork.size() == 1893850,
        "Startup decompressed without changing package offsets");
  // Keep the original texture format, dimensions and payload allocation.
  constexpr size_t a = 0x198b24;
  check(artwork[a + 151] == 0x32 && artwork[a + 150] == 1,
        "Prompt texture format");
  check(artwork[a + 31] == 128 && artwork[a + 59] == 128,
        "Menu icons render at 128 pixels");
  check(artwork[a + 87] == 32 && artwork[a + 115] == 32,
        "Menu icons retain their original logical size");
  settings::SetFlagByName("keybind_lstick_down", "J");
  const auto remapped = make_keyboard_prompts(bytes);
  auto same_icon = [&](size_t offset) {
    return std::equal(artwork.begin() + offset + 325,
                      artwork.begin() + offset + 16737 - 28,
                      remapped.begin() + offset + 325);
  };
  check(!same_icon(0x14fa98), "Downward QTE uses the down binding");
  check(same_icon(0x157d5a), "Remapping down does not change the upward QTE");
  settings::SetFlagByName("keybind_lstick_down", "S");
  std::ofstream output(argv[2], std::ios::binary);
  output.write(reinterpret_cast<const char *>(artwork.data()), artwork.size());
  output.close();
  check(bool(output), "Write inspected artwork");
  const auto cache =
      std::filesystem::path(argv[2]).parent_path() / "keyboard-test-assets";
  std::filesystem::create_directories(cache);
  const auto path = cache / "Startup.xxx";
  std::ofstream(path, std::ios::binary | std::ios::trunc).put('!');
  check(!cache_keyboard_prompts(artwork, cache), "Repair corrupt or incomplete cache");
  check(cache_keyboard_prompts(artwork, cache), "Reuse the validated compressed package");
  check(!cache_keyboard_prompts(remapped, cache), "Rebinding invalidates the cache");
  check(!cache_keyboard_prompts(artwork, cache), "Restore original bindings");
  std::ifstream packed(path, std::ios::binary);
  const std::vector<uint8_t> compressed{std::istreambuf_iterator<char>(packed), {}};
  auto be32 = [&](size_t pos) {
    return (uint32_t(compressed.at(pos)) << 24) |
           (uint32_t(compressed.at(pos + 1)) << 16) |
           (uint32_t(compressed.at(pos + 2)) << 8) | compressed.at(pos + 3);
  };
  const auto chunks = (be32(12) + 131071) / 131072;
  size_t cursor = 16 + chunks * 8, total = 0;
  for (unsigned chunk = 0; chunk < chunks; ++chunk) {
    const auto size = be32(16 + chunk * 8);
    const size_t end =
        cursor + size - 4 - 5;  // UE3 and XMem each strip a trailer.
    size_t decoded = 0;
    while (cursor < end) {
      size_t header = compressed.at(cursor) == 255 ? 5 : 2;
      check(cursor + header <= end, "Native XMem packet header bounds");
      const size_t bytes = (size_t(compressed[cursor + header - 2]) << 8) |
                           compressed[cursor + header - 1];
      decoded +=
          header == 5
              ? ((size_t(compressed[cursor + 1]) << 8) | compressed[cursor + 2])
              : 32768;
      cursor += header;
      check(bytes && bytes <= end - cursor,
            "Native XMem packet payload bounds");
      cursor += bytes;
    }
    check(cursor == end && decoded == be32(20 + chunk * 8),
          "Native XMem consumes exactly one chunk");
    cursor += 9;
    total += size;
  }
  check(total == be32(8), "Compressed package size table");
  const auto game = std::filesystem::path(argv[1])
                        .parent_path()
                        .parent_path()
                        .parent_path()
                        .lexically_normal();
  auto* factory = REX_INTERFACE(rex::filesystem::IFileSystemFactory);
  check(factory, "Connect guest filesystem factory");
  auto vfs = factory->Create();
  const auto write_time = std::filesystem::file_time_type::clock::now() - std::chrono::hours(24);
  std::filesystem::last_write_time(path, write_time);
  lollipop::mount_game_ui_assets(*vfs, game, cache);
  const rex::filesystem::MountEntry backing_mount{.path = "\\Device\\Artwork", .source = cache};
  check(bool(vfs->Mount(backing_mount)),
        "Mount generated artwork directly for metadata comparison");
  const auto backing = vfs->Stat("\\Device\\Artwork\\Startup.xxx").value();
  auto matches_backing = [&](const rex::filesystem::FileStat& info) {
    return info.name == "Startup.xxx" && info.size == compressed.size() &&
           info.size == backing.size && info.allocation_size == backing.allocation_size &&
           info.create_time == backing.create_time && info.write_time == backing.write_time;
  };
  check(matches_backing(vfs->Stat("game:\\AshGame\\CookedXbox360\\Startup.xxx").value()),
        "The game alias reports the replacement metadata");
  auto parent = vfs->Open("d:\\AshGame\\CookedXbox360", {});
  check(bool(parent), "Resolve guest directory");
  auto listing = parent->file->List("Startup.xxx").value();
  const auto listed = listing->Next();
  check(listed && matches_backing(*listed) && !listing->Next(),
        "The original directory lists the replacement metadata once");
  for (bool relative : {false, true}) {
    const rex::filesystem::OpenOptions options{.require_file = true};
    auto open = relative ? vfs->OpenRelative(*parent->file, "Startup.xxx", options)
                         : vfs->Open("d:\\AshGame\\CookedXbox360\\Startup.xxx", options);
    check(bool(open), "Open keyboard artwork through the guest filesystem");
    std::array<uint8_t, 8> header{};
    const bool valid = matches_backing(open->file->GetInfo()) &&
                       open->file->Read(header, 0).value() == header.size() && header[4] == 0 &&
                       header[5] == 2 && header[6] == 0 && header[7] == 0;
    check(valid, "Guest reads the generated package and its actual file size");
  }
  check(vfs->Stat("d:\\default.xex").value().size == 20561920,
        "Unmodified files still resolve from the disc dump");
  bytes[200] ^= 1;
  bool rejected = false;
  try {
    make_keyboard_prompts(bytes);
  } catch (const std::runtime_error &) {
    rejected = true;
  }
  check(rejected, "Reject unsupported startup packages before patching");
  return 0;
} catch (const std::exception& error) {
  std::cerr << error.what() << '\n';
  return 1;
}
