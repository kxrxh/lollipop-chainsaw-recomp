#include "game/game_bridge.h"
#include "game/native_quit_dialog.h"
#include "testing/test_bridge.h"

#include <array>
#include <atomic>
#include <thread>
#include <windows.h>

#include "input_fixture.h"
#include "settings/runtime_settings.h"

class bridge_settings final : public rex::SettingOwner {
public:
  bridge_settings() : SettingOwner("test") {}
  REX_SETTING(bool, bridge_test_flag, true, "Test hot setting");
  REX_SETTING(rex::i32, bridge_test_restart, 1, "Test restart setting", rex::kRestart);
};

using lollipop::execute_test_command;
using lollipop::test_controls;

bool check_game_bridge() {
  using lollipop::game_bridge;
  if (game_bridge::valid_address(0) || game_bridge::valid_address(0x3FFFFFFF) ||
      !game_bridge::valid_address(0x40000000) || !game_bridge::valid_address(0xBFFFFFFC) ||
      game_bridge::valid_address(0xBFFFFFFD) || game_bridge::valid_address(0xFFFFFFFC) ||
      game_bridge::valid_address(0x40000000, 0xFFFFFFFF) ||
      !game_bridge::read_string(nullptr, 0x40000000).empty())
    return false;
  // Reserve the guest address range without allocating physical memory for it.
  auto* memory =
      static_cast<unsigned char*>(VirtualAlloc(nullptr, 0xC0000000ull, MEM_RESERVE, PAGE_NOACCESS));
  if (!memory)
    return false;
  struct release {
    void* address;
    ~release() { VirtualFree(address, 0, MEM_RELEASE); }
  } reservation{memory};
  if (!VirtualAlloc(memory + 0x40000000, 4096, MEM_COMMIT, PAGE_READWRITE))
    return false;
  auto put = [&](uint32_t at, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
      memory[at + i] = static_cast<unsigned char>(value >> (24 - 8 * i));
  };
  constexpr uint32_t header = 0x40000000, text = header + 128;
  put(header, text);
  put(header + 4, 5);
  put(header + 8, 5);
  for (unsigned i = 0; i < 4; ++i)
    memory[text + i * 2 + 1] = "Quit"[i];
  if (game_bridge::read_string(memory, header) != "Quit")
    return false;
  memory[text + 8] = 1;
  if (!game_bridge::read_string(memory, header).empty())
    return false;
  memory[text + 8] = 0;
  memory[text] = 1;
  if (!game_bridge::read_string(memory, header).empty())
    return false;
  memory[text] = 0;
  put(header + 4, 513);
  if (!game_bridge::read_string(memory, header).empty())
    return false;
  put(header + 4, 5);
  put(header, 0xBFFFFFFE);
  if (!game_bridge::read_string(memory, header).empty() ||
      !game_bridge::read_string(memory, 0xC0000000).empty())
    return false;

  lollipop::native_quit_dialog dialog;
  std::atomic<unsigned> accepted{};
  {
    std::array<std::jthread, 8> callers;
    for (auto& caller : callers)
      caller = std::jthread([&] {
        if (dialog.request())
          ++accepted;
      });
  }
  dialog.dismiss();
  return accepted == 1 && dialog.status() == lollipop::quit_dialog_state::requested &&
         dialog.selection() == 0 && !dialog.request();
}

int main() {
  if (!check_game_bridge())
    return 27;
  input_fixture app("lollipop-test-bridge");
  bridge_settings settings;
  if (!REX_SETTINGS().Register(settings))
    return 100;
  auto controls = std::make_shared<test_controls>();
  const auto run = [&](const std::string& s) { return execute_test_command(*controls, s); };
  if (!run("game_settings_state").starts_with("ok loading ") ||
      !run("game_settings_state extra").starts_with("error"))
    return 24;
  if (run("game_cheats_state") != "ok Waiting for gameplay." ||
      !run("game_cheats_state extra").starts_with("error"))
    return 39;
  if (run("quit_state") != "ok idle" || run("quit_selection") != "ok 0" ||
      !run("quit_state extra").starts_with("error") ||
      !run("quit_selection extra").starts_with("error"))
    return 28;
  if (run("game_console_state") != "ok idle" || !run("game_console").starts_with("error") ||
      !run("game_console " + std::string(241, 'x')).starts_with("error") ||
      !run("game_console test\nquit").starts_with("error") ||
      run("game_console stat fps") != "ok queued" || run("game_console_state") != "ok pending" ||
      !run("game_console stat fps").starts_with("error"))
    return 25;
  if (run("ui_key enter 1") != "ok key" || !controls->key_event || (*controls->key_event)[1] != 1 ||
      run("ui_key enter 0") != "ok key" || run("ui_key enter 2").starts_with("ok") ||
      run("ui_key missing 1").starts_with("ok") || run("ui_key enter 1 extra").starts_with("ok"))
    return 23;
  if (run("ui_key space 1") != "ok key" || run("ui_key space 0") != "ok key")
    return 29;
  if (run("set test.bridge_test_flag false") != "ok false" ||
      run("get test.bridge_test_flag") != "ok false")
    return 1;
  for (const auto* invalid :
       {"get missing", "set test.bridge_test_restart 2", "input 4096 0 0 0 0 10001",
        "input 4096 32768 0 0 0 100", "input 4096 0 0 0 0 -1", "input garbage",
        "input 0 0 0 0 0 1 extra", "input 0 0 0 0 0 1 255", "input 0 0 0 0 0 1 -1 0",
        "input 0 0 0 0 0 1 256 0", "input 0 0 0 0 0 1 0 -1", "input 0 0 0 0 0 1 0 256",
        "input 0 0 0 0 0 1 0 0 extra", "ui_mouse -1 0 1", "ui_mouse 1 1 2"}) {
    if (!run(invalid).starts_with("error"))
      return 2;
  }
  if (run("input_device gamepad") != "ok gamepad" ||
      !run("input_device invalid").starts_with("error") ||
      run("input_device keyboard") != "ok keyboard")
    return 18;
  run("input 0 0 0 0 0 1000");
  {
    std::jthread producer([&] {
      for (int i = 0; i < 1000; ++i) {
        run("input 0 20000 -20000 0 0 1000");
        run("input 0 -20000 20000 0 0 1000");
      }
    });
    for (int i = 0; i < 5000; ++i) {
      const auto snapshot = controls->read_pad(true);
      if (int16_t(snapshot.thumb_lx) + int16_t(snapshot.thumb_ly) != 0)
        return 10;
    }
  }
  if (run("ui_mouse 1046 733 1") != "ok mouse" ||
      controls->mouse_event != std::optional{std::array{1046, 733, 1}})
    return 6;
  rex::hid::PadState state{};
  if (run("input 4096 12345 -23456 20000 0 1000 255 128") != "ok input")
    return 3;
  state = controls->read_pad(true);
  if (state.buttons != 4096 || state.thumb_lx != 12345 || state.thumb_ly != -23456 ||
      state.left_trigger != 255 || state.right_trigger != 128)
    return 4;
  {
    std::lock_guard lock(controls->mutex);
    controls->release_at = std::chrono::steady_clock::time_point{} + std::chrono::milliseconds(1);
  }
  state = controls->read_pad(true);
  if (state.buttons != 0 || state.thumb_lx != 0 || state.left_trigger != 0 ||
      state.right_trigger != 0)
    return 5;
  run("input_device gamepad");
  run("input 0 0 0 0 0 1000 128 255");
  state = controls->read_pad(false);
  if (state.left_trigger != 128 || state.right_trigger != 255)
    return 27;
  state = controls->read_pad(true);
  if (state.left_trigger != 0 || state.right_trigger != 0)
    return 28;
  run("input 0 0 0 0 0 1000");
  state = controls->read_pad(false);
  if (state.left_trigger != 0 || state.right_trigger != 0)
    return 29;
  run("input_device keyboard");
  auto& input = app.GetInput();
  input.SetTestInput([controls](bool& keyboard) {
    keyboard = controls->keyboard_input.load();
    return controls->read_pad(keyboard);
  });
  run("input 16 0 0 0 0 5000");
  if (input.GetState(0).value().state.buttons != 16)
    return 11;
  if (!input.IsKeyboardActive())
    return 19;
  input.AcquireUIBlock();
  state = input.GetState(0).value().state;
  if (state.buttons != 0)
    return 12;
  state = input.GetStateForUI(0).value();
  if (state.buttons != 16)
    return 13;
  input.ReleaseUIBlock();
  state = input.GetState(0).value().state;
  if (state.buttons != 0)
    return 14;
  {
    std::lock_guard lock(controls->mutex);
    controls->release_at = std::chrono::steady_clock::time_point{} + std::chrono::milliseconds(1);
  }
  state = input.GetState(0).value().state;
  run("input 16 0 0 0 0 5000");
  state = input.GetState(0).value().state;
  if (state.buttons != 16)
    return 15;
  run("input_device gamepad");
  run("input 4096 0 0 0 0 5000");
  state = input.GetState(0).value().state;
  if (input.IsKeyboardActive())
    return 20;
  run("input_device keyboard");
  run("input 16384 0 0 0 0 5000");
  state = input.GetState(0).value().state;
  if (!input.IsKeyboardActive())
    return 21;
  run("input_device gamepad");
  run("input 8192 0 0 0 0 5000");
  state = input.GetState(0).value().state;
  if (input.IsKeyboardActive())
    return 22;
  run("input 0 0 0 0 0 0");
  run("input_device keyboard");
  run("input 0 0 0 7000 0 5000");
  state = input.GetState(0).value().state;
  if (!input.IsKeyboardActive())
    return 23;
  run("input_device gamepad");
  run("input 0 0 0 0 0 5000 255 128");
  state = input.GetState(0).value().state;
  if (state.left_trigger != 255 || state.right_trigger != 128)
    return 30;
  if (input.IsKeyboardActive())
    return 37;
  input.AcquireUIBlock();
  state = input.GetState(0).value().state;
  if (state.left_trigger != 0 || state.right_trigger != 0)
    return 31;
  state = input.GetStateForUI(0).value();
  if (state.left_trigger != 255 || state.right_trigger != 128)
    return 32;
  input.ReleaseUIBlock();
  state = input.GetState(0).value().state;
  if (state.left_trigger != 0 || state.right_trigger != 0)
    return 33;
  run("input 0 0 0 0 0 5000 0 200");
  state = input.GetState(0).value().state;
  if (state.left_trigger != 0 || state.right_trigger != 0)
    return 34;
  run("input 0 0 0 0 0 5000 255 29");
  state = input.GetState(0).value().state;
  if (state.left_trigger != 255 || state.right_trigger != 29)
    return 35;
  run("input 0 0 0 0 0 5000 255 30");
  state = input.GetState(0).value().state;
  if (state.left_trigger != 255 || state.right_trigger != 30)
    return 36;
  run("input 0 0 0 0 0 5000 0 29");
  input.AcquireUIBlock();
  input.ReleaseUIBlock();
  run("input 0 0 0 0 0 5000 0 255");
  state = input.GetState(0).value().state;
  return state.right_trigger == 255 ? 0 : 38;
}
