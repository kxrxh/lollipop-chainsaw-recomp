#include "input/keyboard_controls.h"
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

#include <rex/cvar.h>
#include <rex/filesystem/vfs.h>
#include <rex/input/mnk/mnk_input_driver.h>
#include <rex/input/state_merge.h>
#include <rex/input/input_system.h>
#include <rex/ui/ui_event.h>

// The headless test does not load the GPU plugin that normally owns these flags.
REXCVAR_DEFINE_BOOL(readback_memexport, true, "GPU", "Test GPU readback default");
REXCVAR_DEFINE_BOOL(readback_memexport_fast, true, "GPU", "Test delayed readback default");

using rex::X_RESULT;
using rex::X_STATUS;

using lollipop::configure_keyboard_controls;
using lollipop::make_keyboard_prompts;
using lollipop::make_prompt_variants;
using lollipop::select_prompt_artwork;
using lollipop::make_keyboard_title;
using lollipop::pack_keyboard_prompts;
using lollipop::make_keyboard_game_device;
using lollipop::make_settings_prompt_artwork;

namespace {
void check(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}
}  // namespace

int main(int argc, char **argv) try {
  check(argc == 3, "Expected source and output paths");
  configure_keyboard_controls();
  check(rex::cvar::Query<bool>("readback_memexport") &&
            !rex::cvar::Query<bool>("readback_memexport_fast"),
        "Native prompt switching reads current GPU icon copies");
  check(rex::cvar::Query<double>("mnk_sensitivity") == 18.0,
        "Higher mouse camera sensitivity by default");
  rex::input::ActiveDeviceTracker tracker;
  const auto keyboard_id = static_cast<rex::input::DeviceId>(1);
  const auto gamepad_id = static_cast<rex::input::DeviceId>(2);
  rex::input::X_INPUT_GAMEPAD keyboard{}, gamepad{};
  keyboard.thumb_rx = 7000;
  tracker.Observe(0, keyboard_id, keyboard, true);
  check(tracker.Active(0) == keyboard_id,
        "Slow mouse movement selects keyboard hints");
  gamepad.thumb_lx = 200;
  tracker.Observe(0, gamepad_id, gamepad);
  check(tracker.Active(0) == keyboard_id,
        "Idle gamepad drift cannot steal hints");
  gamepad.buttons = 4096;
  tracker.Observe(0, gamepad_id, gamepad);
  check(tracker.Active(0) == gamepad_id,
        "Controller button selects gamepad hints");
  tracker.Observe(0, keyboard_id, keyboard, true);
  check(tracker.Active(0) == gamepad_id,
        "Held keyboard state cannot steal hints back");
  keyboard.buttons = 32768;
  tracker.Observe(0, keyboard_id, keyboard, true);
  tracker.Observe(0, gamepad_id, gamepad);
  check(tracker.Active(0) == keyboard_id,
        "New mouse press switches hints despite held pad button");
  gamepad.buttons = 0;
  gamepad.thumb_lx = 24000;
  tracker.Observe(0, gamepad_id, gamepad);
  check(tracker.Active(0) == gamepad_id,
        "Controller stick movement selects gamepad hints");
  gamepad.buttons = 4096;
  tracker.Observe(0, gamepad_id, gamepad);
  keyboard.buttons = 16384;
  tracker.Observe(0, keyboard_id, keyboard, true);
  gamepad.buttons = 0;
  tracker.Observe(0, gamepad_id, gamepad);
  check(tracker.Active(0) == keyboard_id,
        "Releasing a button on a held stick does not take over hints");
  gamepad.buttons = 4096;
  tracker.Observe(0, gamepad_id, gamepad);
  check(tracker.Active(0) == gamepad_id,
        "Repressing a pad button takes over even while its stick remains held");
  tracker.Forget(gamepad_id);
  check(tracker.Active(0) == rex::input::DeviceId::kInvalid,
        "Disconnect forgets active controller");
  check(rex::cvar::GetFlagByName("keybind_y") == "LMB", "Chainsaw default");
  rex::cvar::SetFlagByName("keybind_b", "Space,Backspace,J");
  configure_keyboard_controls();
  check(rex::cvar::GetFlagByName("keybind_b") == "Space,Backspace,J",
        "Preserve user bindings");
  rex::cvar::SetFlagByName("readback_memexport_fast", "true");
  rex::cvar::SetFlagByName("readback_memexport", "false");
  configure_keyboard_controls();
  check(rex::cvar::Query<bool>("readback_memexport_fast") &&
            !rex::cvar::Query<bool>("readback_memexport"),
        "Explicit renderer overrides are preserved");

  rex::input::mnk::MnkInputDriver driver(nullptr, 0);
  check(driver.Setup() == X_STATUS_SUCCESS, "Driver setup");
  std::vector<rex::input::DeviceInfo> devices;
  driver.EnumerateDevices(devices);
  check(devices.size() == 1, "Keyboard device");
  const auto id = devices[0].id;
  auto key = [&](rex::ui::VirtualKey vk, bool down) {
    rex::ui::KeyEvent event(nullptr, vk, 1, false, false, false, false, false);
    if (down)
      driver.OnKeyDown(event);
    else
      driver.OnKeyUp(event);
  };
  auto mouse = [&](rex::ui::MouseEvent::Button button, bool down) {
    rex::ui::MouseEvent event(nullptr, button, 0, 0);
    if (down)
      driver.OnMouseDown(event);
    else
      driver.OnMouseUp(event);
  };
  auto state = [&] {
    rex::input::X_INPUT_STATE value{};
    check(driver.GetDeviceState(id, &value) == X_ERROR_SUCCESS,
          "Read keyboard state");
    return value.gamepad;
  };
  using rex::ui::VirtualKey;
  using Button = rex::ui::MouseEvent::Button;
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
  driver.set_is_active_callback([] { return false; });
  check(state().buttons == 0 && state().thumb_ly == 0,
        "SDK dialogs suppress keyboard gameplay input");
  driver.set_is_active_callback([] { return true; });
  rex::ui::UISetupEvent focus;
  driver.OnLostFocus(focus);
  check(state().buttons == 0 && state().thumb_ly == 0,
        "Focus loss releases keys");
  driver.OnGotFocus(focus);
  check(state().buttons == 0 && state().thumb_ly == 0,
        "Focus regain stays released");
  rex::input::mnk::MnkInputDriver camera(nullptr, 0);
  rex::input::mnk::MnkInputDriver sensitivity(nullptr, 0);
  rex::ui::MouseEvent modest_motion(nullptr, Button::kLeft, 5, 0);
  sensitivity.OnMouseMove(modest_motion);
  rex::input::X_INPUT_STATE sensitive_mouse{};
  sensitivity.GetDeviceState(id, &sensitive_mouse);
  check(sensitive_mouse.gamepad.thumb_rx > 20000,
        "A small mouse movement produces a useful camera turn at the new "
        "default");
  rex::ui::MouseEvent motion(nullptr, Button::kLeft, 1, -1);
  camera.OnMouseMove(motion);
  rex::input::X_INPUT_STATE first_mouse{}, repeated_mouse{};
  camera.GetDeviceState(id, &first_mouse);
  camera.GetDeviceState(id, &repeated_mouse);
  check(first_mouse.gamepad.thumb_rx > 6553 &&
            first_mouse.gamepad.thumb_ry > 6553,
        "One-pixel mouse motion clears the game's 20 percent camera deadzone");
  check(first_mouse.gamepad.thumb_rx == repeated_mouse.gamepad.thumb_rx &&
            first_mouse.gamepad.thumb_ry == repeated_mouse.gamepad.thumb_ry,
        "Repeated guest/UI polls retain the same mouse stick sample");
  std::this_thread::sleep_for(std::chrono::milliseconds(25));
  camera.GetDeviceState(id, &repeated_mouse);
  check(repeated_mouse.gamepad.thumb_rx == 0 &&
            repeated_mouse.gamepad.thumb_ry == 0,
        "Camera stops when mouse motion stops");
  rex::ui::MouseEvent fast_motion(nullptr, Button::kLeft, 1000000, 1000000);
  camera.OnMouseMove(fast_motion);
  std::this_thread::sleep_for(std::chrono::milliseconds(25));
  camera.GetDeviceState(id, &repeated_mouse);
  check(repeated_mouse.gamepad.thumb_rx == 32767 &&
            repeated_mouse.gamepad.thumb_ry == -32767,
        "Fast mouse motion clamps to the controller stick range");
  camera.OnLostFocus(focus);
  camera.OnGotFocus(focus);
  camera.GetDeviceState(id, &repeated_mouse);
  check(repeated_mouse.gamepad.thumb_rx == 0 &&
            repeated_mouse.gamepad.thumb_ry == 0,
        "Focus changes discard cached mouse movement");
  rex::input::InputSystem source_tracking(nullptr);
  rex::input::NotifyInputActivity(false);
  key(VirtualKey::kH, true);
  check(source_tracking.IsKeyboardActive(),
        "An unbound key selects keyboard hints without a state poll");
  rex::input::NotifyInputActivity(false);
  key(VirtualKey::kH, true);
  check(!source_tracking.IsKeyboardActive(),
        "A held key repeat cannot steal hints from a gamepad");
  key(VirtualKey::kH, false);
  rex::ui::MouseEvent moved_again(nullptr, Button::kLeft, 1000001, 1000000);
  camera.OnMouseMove(moved_again);
  camera.OnMouseMove(fast_motion);
  check(source_tracking.IsKeyboardActive(),
        "Real mouse motion changes hints without a state poll");
  rex::cvar::SetFlagByName("mnk_mode", "false");
  devices.clear();
  driver.EnumerateDevices(devices);
  check(devices.empty(), "Controller-only mode disables keyboard");

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
  rex::cvar::SetFlagByName("keybind_lstick_down", "J");
  const auto remapped = make_keyboard_prompts(bytes);
  auto same_icon = [&](size_t offset) {
    return std::equal(artwork.begin() + offset + 325,
                      artwork.begin() + offset + 16737 - 28,
                      remapped.begin() + offset + 325);
  };
  check(!same_icon(0x14fa98), "Downward QTE uses the down binding");
  check(same_icon(0x157d5a), "Remapping down does not change the upward QTE");
  rex::cvar::SetFlagByName("keybind_lstick_down", "S");
  std::ofstream output(argv[2], std::ios::binary);
  output.write(reinterpret_cast<const char *>(artwork.data()), artwork.size());
  output.close();
  check(bool(output), "Write inspected artwork");
  const auto cache =
      std::filesystem::path(argv[2]).parent_path() / "keyboard-test-assets";
  std::filesystem::create_directories(cache);
  const auto compressed = pack_keyboard_prompts(artwork, cache);
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
  std::ofstream cached(cache / "Startup.xxx",
                       std::ios::binary | std::ios::trunc);
  cached.write(reinterpret_cast<const char *>(compressed.data()),
               compressed.size());
  cached.close();
  check(bool(cached), "Write compressed fixture");
  const auto game = std::filesystem::path(argv[1])
                        .parent_path()
                        .parent_path()
                        .parent_path()
                        .lexically_normal();
  auto device = make_keyboard_game_device(game, cache);
  check(device->Initialize(), "Initialize keyboard game device");
  rex::filesystem::VirtualFileSystem vfs;
  vfs.RegisterDevice(std::move(device));
  vfs.RegisterSymbolicLink("d:", "\\Device\\LollipopGame");
  auto *parent = vfs.ResolvePath("d:\\AshGame\\CookedXbox360");
  check(parent, "Resolve guest directory");
  for (bool relative : {false, true}) {
    rex::filesystem::File *file = nullptr;
    rex::filesystem::FileAction action{};
    check(vfs.OpenFile(relative ? parent : nullptr,
                       relative ? "Startup.xxx"
                                : "d:\\AshGame\\CookedXbox360\\Startup.xxx",
                       rex::filesystem::FileDisposition::kOpen,
                       rex::filesystem::FileAccess::kGenericRead, false, true,
                       &file, &action) == X_STATUS_SUCCESS,
          "Open keyboard artwork through the guest filesystem");
    std::array<uint8_t, 8> header{};
    size_t read = 0;
    const bool valid = file->entry()->size() == compressed.size() &&
                       file->ReadSync(header, 0, &read) == X_STATUS_SUCCESS &&
                       read == header.size() && header[4] == 0 &&
                       header[5] == 2 && header[6] == 0 && header[7] == 0;
    file->Destroy();
    check(valid, "Guest reads the generated package and its actual file size");
  }
  check(vfs.ResolvePath("d:\\default.xex")->size() == 20561920,
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
} catch (const std::exception &error) {
  std::cerr << error.what() << '\n';
  return 1;
}
