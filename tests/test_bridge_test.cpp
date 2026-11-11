#include "test_bridge.h"

#include <rex/cvar.h>
#include <rex/input/input_system.h>

using rex::X_RESULT;

REXCVAR_DEFINE_BOOL(bridge_test_flag, true, "Test", "Test hot setting");
REXCVAR_DEFINE_INT32(bridge_test_restart, 1, "Test", "Test restart setting")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

int main() {
  auto controls = std::make_shared<test_controls>();
  const auto run = [&](const std::string& s) {
    return execute_test_command(*controls, s);
  };
  if (run("set bridge_test_flag false") != "ok false" ||
      run("get bridge_test_flag") != "ok false")
    return 1;
  for (const auto* invalid :
       {"get missing", "set bridge_test_restart 2", "input 4096 0 0 0 0 10001",
        "input 4096 32768 0 0 0 100", "input 4096 0 0 0 0 -1", "input garbage",
        "input 0 0 0 0 0 1 extra", "ui_mouse -1 0 1", "ui_mouse 1 1 2"}) {
    if (!run(invalid).starts_with("error")) return 2;
  }
  auto driver = make_test_driver(controls);
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
  if (assigned.size() != 1 || assigned.front() != devices.back().id) return 7;
  assignment->DevicesForUser(1, assigned);
  if (!assigned.empty()) return 8;
  assignment->OnDevicesChanged({devices.back()});
  assignment->DevicesForUser(0, assigned);
  if (assigned.size() != 1 || assigned.front() != devices.back().id) return 9;
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
  if (run("input 4096 12345 -23456 20000 0 1000") != "ok input") return 3;
  driver->GetDeviceState({}, &state);
  if (state.gamepad.buttons != 4096 || state.gamepad.thumb_lx != 12345 ||
      state.gamepad.thumb_ly != -23456)
    return 4;
  {
    std::lock_guard lock(controls->mutex);
    controls->release_at =
        std::chrono::steady_clock::time_point{} + std::chrono::milliseconds(1);
  }
  driver->GetDeviceState({}, &state);
  if (state.gamepad.buttons != 0 || state.gamepad.thumb_lx != 0) return 5;
  rex::input::InputSystem input(nullptr);
  input.AddDriver(make_test_driver(controls));
  input.SetDeviceAssignment(make_test_assignment());
  run("input 16 0 0 0 0 5000");
  if (input.GetState(0, &state) != X_ERROR_SUCCESS || state.gamepad.buttons != 16)
    return 11;
  input.AddUIInputBlocker();
  input.GetState(0, &state);
  if (state.gamepad.buttons != 0) return 12;
  input.GetStateForUI(0, &state);
  if (state.gamepad.buttons != 16) return 13;
  input.RemoveUIInputBlocker();
  input.GetState(0, &state);
  if (state.gamepad.buttons != 0) return 14;
  {
    std::lock_guard lock(controls->mutex);
    controls->release_at =
        std::chrono::steady_clock::time_point{} + std::chrono::milliseconds(1);
  }
  input.GetState(0, &state);
  run("input 16 0 0 0 0 5000");
  input.GetState(0, &state);
  return state.gamepad.buttons == 16 ? 0 : 15;
}
