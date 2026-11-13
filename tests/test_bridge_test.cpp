#include "game/game_bridge.h"
#include "game/native_quit_dialog.h"
#include "testing/test_bridge.h"

#include <array>
#include <atomic>
#include <thread>
#include <windows.h>

#include <rex/cvar.h>
#include <rex/input/input_system.h>

using rex::X_RESULT;

REXCVAR_DEFINE_BOOL(bridge_test_flag, true, "Test", "Test hot setting");
REXCVAR_DEFINE_INT32(bridge_test_restart, 1, "Test", "Test restart setting")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

using lollipop::execute_test_command;
using lollipop::make_test_assignment;
using lollipop::make_test_driver;
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
  auto controls = std::make_shared<test_controls>();
  const auto run = [&](const std::string& s) { return execute_test_command(*controls, s); };
  if (!run("game_settings_state").starts_with("ok loading ") ||
      !run("game_settings_state extra").starts_with("error"))
    return 24;
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
  if (run("set bridge_test_flag false") != "ok false" || run("get bridge_test_flag") != "ok false")
    return 1;
  for (const auto* invalid :
       {"get missing", "set bridge_test_restart 2", "input 4096 0 0 0 0 10001",
        "input 4096 32768 0 0 0 100", "input 4096 0 0 0 0 -1", "input garbage",
        "input 0 0 0 0 0 1 extra", "input 0 0 0 0 0 1 255", "input 0 0 0 0 0 1 -1 0",
        "input 0 0 0 0 0 1 256 0", "input 0 0 0 0 0 1 0 -1", "input 0 0 0 0 0 1 0 256",
        "input 0 0 0 0 0 1 0 0 extra", "ui_mouse -1 0 1", "ui_mouse 1 1 2"}) {
    if (!run(invalid).starts_with("error"))
      return 2;
  }
  auto driver = make_test_driver(controls);
  rex::input::X_INPUT_CAPABILITIES caps{};
  if (driver->GetDeviceCapabilities({}, 0, &caps) != X_ERROR_SUCCESS ||
      caps.gamepad.left_trigger != 255 || caps.gamepad.right_trigger != 255)
    return 26;
  std::vector<rex::input::DeviceInfo> device_kind;
  if (run("input_device gamepad") != "ok gamepad")
    return 16;
  driver->EnumerateDevices(device_kind);
  if (device_kind.size() != 2 || !device_kind[0].synthetic || device_kind[1].synthetic)
    return 17;
  if (run("input_device keyboard") != "ok keyboard" ||
      !run("input_device invalid").starts_with("error"))
    return 18;
  auto assignment = make_test_assignment();
  std::vector<rex::input::DeviceInfo> devices;
  driver->EnumerateDevices(devices);
  auto other = devices.front();
  other.id = static_cast<rex::input::DeviceId>(1234);
  other.synthetic = false;
  devices.insert(devices.begin(), other);
  assignment->OnDevicesChanged(devices);
  std::vector<rex::input::DeviceId> assigned;
  assignment->DevicesForUser(0, assigned);
  if (assigned.size() != 3 || assigned[0] != other.id || assigned[1] != devices[1].id ||
      assigned[2] != devices[2].id)
    return 7;
  assignment->DevicesForUser(1, assigned);
  if (!assigned.empty())
    return 8;
  assignment->OnDevicesChanged({devices.back()});
  assignment->DevicesForUser(0, assigned);
  if (assigned.size() != 1 || assigned.front() != devices.back().id)
    return 9;
  run("input 0 0 0 0 0 1000");
  {
    std::jthread producer([&] {
      for (int i = 0; i < 1000; ++i) {
        run("input 0 20000 -20000 0 0 1000");
        run("input 0 -20000 20000 0 0 1000");
      }
    });
    for (int i = 0; i < 5000; ++i) {
      rex::input::X_INPUT_STATE snapshot{};
      driver->GetDeviceState({}, &snapshot);
      if (int16_t(snapshot.gamepad.thumb_lx) + int16_t(snapshot.gamepad.thumb_ly) != 0)
        return 10;
    }
  }
  if (run("ui_mouse 1046 733 1") != "ok mouse" ||
      controls->mouse_event != std::optional{std::array{1046, 733, 1}})
    return 6;
  rex::input::X_INPUT_STATE state{};
  if (run("input 4096 12345 -23456 20000 0 1000 255 128") != "ok input")
    return 3;
  driver->GetDeviceState({}, &state);
  if (state.gamepad.buttons != 4096 || state.gamepad.thumb_lx != 12345 ||
      state.gamepad.thumb_ly != -23456 || state.gamepad.left_trigger != 255 ||
      state.gamepad.right_trigger != 128)
    return 4;
  {
    std::lock_guard lock(controls->mutex);
    controls->release_at = std::chrono::steady_clock::time_point{} + std::chrono::milliseconds(1);
  }
  driver->GetDeviceState({}, &state);
  if (state.gamepad.buttons != 0 || state.gamepad.thumb_lx != 0 ||
      state.gamepad.left_trigger != 0 || state.gamepad.right_trigger != 0)
    return 5;
  run("input_device gamepad");
  run("input 0 0 0 0 0 1000 128 255");
  driver->GetDeviceState(device_kind[1].id, &state);
  if (state.gamepad.left_trigger != 128 || state.gamepad.right_trigger != 255)
    return 27;
  driver->GetDeviceState(device_kind[0].id, &state);
  if (state.gamepad.left_trigger != 0 || state.gamepad.right_trigger != 0)
    return 28;
  run("input 0 0 0 0 0 1000");
  driver->GetDeviceState(device_kind[1].id, &state);
  if (state.gamepad.left_trigger != 0 || state.gamepad.right_trigger != 0)
    return 29;
  run("input_device keyboard");
  rex::input::InputSystem input(nullptr);
  input.AddDriver(make_test_driver(controls));
  input.SetDeviceAssignment(make_test_assignment());
  run("input 16 0 0 0 0 5000");
  if (input.GetState(0, &state) != X_ERROR_SUCCESS || state.gamepad.buttons != 16)
    return 11;
  if (!input.IsKeyboardActive())
    return 19;
  input.AddUIInputBlocker();
  input.GetState(0, &state);
  if (state.gamepad.buttons != 0)
    return 12;
  input.GetStateForUI(0, &state);
  if (state.gamepad.buttons != 16)
    return 13;
  input.RemoveUIInputBlocker();
  input.GetState(0, &state);
  if (state.gamepad.buttons != 0)
    return 14;
  {
    std::lock_guard lock(controls->mutex);
    controls->release_at = std::chrono::steady_clock::time_point{} + std::chrono::milliseconds(1);
  }
  input.GetState(0, &state);
  run("input 16 0 0 0 0 5000");
  input.GetState(0, &state);
  if (state.gamepad.buttons != 16)
    return 15;
  run("input_device gamepad");
  run("input 4096 0 0 0 0 5000");
  input.GetState(0, &state);
  if (input.IsKeyboardActive())
    return 20;
  run("input_device keyboard");
  run("input 16384 0 0 0 0 5000");
  input.GetState(0, &state);
  if (!input.IsKeyboardActive())
    return 21;
  run("input_device gamepad");
  run("input 8192 0 0 0 0 5000");
  input.GetState(0, &state);
  if (input.IsKeyboardActive())
    return 22;
  run("input 0 0 0 0 0 0");
  run("input_device keyboard");
  run("input 0 0 0 7000 0 5000");
  input.GetState(0, &state);
  if (!input.IsKeyboardActive())
    return 23;
  run("input_device gamepad");
  run("input 0 0 0 0 0 5000 255 128");
  input.GetState(0, &state);
  if (state.gamepad.left_trigger != 255 || state.gamepad.right_trigger != 128)
    return 30;
  if (input.IsKeyboardActive())
    return 37;
  input.AddUIInputBlocker();
  input.GetState(0, &state);
  if (state.gamepad.left_trigger != 0 || state.gamepad.right_trigger != 0)
    return 31;
  input.GetStateForUI(0, &state);
  if (state.gamepad.left_trigger != 255 || state.gamepad.right_trigger != 128)
    return 32;
  input.RemoveUIInputBlocker();
  input.GetState(0, &state);
  if (state.gamepad.left_trigger != 0 || state.gamepad.right_trigger != 0)
    return 33;
  run("input 0 0 0 0 0 5000 0 200");
  input.GetState(0, &state);
  if (state.gamepad.left_trigger != 0 || state.gamepad.right_trigger != 0)
    return 34;
  run("input 0 0 0 0 0 5000 255 29");
  input.GetState(0, &state);
  if (state.gamepad.left_trigger != 255 || state.gamepad.right_trigger != 29)
    return 35;
  run("input 0 0 0 0 0 5000 255 30");
  input.GetState(0, &state);
  if (state.gamepad.left_trigger != 255 || state.gamepad.right_trigger != 30)
    return 36;
  run("input 0 0 0 0 0 5000 0 29");
  input.AddUIInputBlocker();
  input.RemoveUIInputBlocker();
  run("input 0 0 0 0 0 5000 0 255");
  input.GetState(0, &state);
  return state.gamepad.right_trigger == 255 ? 0 : 38;
}
