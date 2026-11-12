#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <rex/cvar.h>
#include <rex/input/input_system.h>

using rex::X_RESULT;
using rex::X_STATUS;

class test_driver final : public rex::input::InputDriver {
 public:
  test_driver() : InputDriver(nullptr, 0) {}
  X_STATUS Setup() override { return X_STATUS_SUCCESS; }
  void EnumerateDevices(std::vector<rex::input::DeviceInfo>& out) override {
    if (inside_.fetch_add(1) != 0) {
      concurrent.store(true);
    }
    std::this_thread::yield();
    rex::input::DeviceInfo device;
    device.id = static_cast<rex::input::DeviceId>(1);
    device.name = std::string(160, 'x');
    out.push_back(device);
    inside_.fetch_sub(1);
  }
  X_RESULT GetDeviceState(rex::input::DeviceId, rex::input::X_INPUT_STATE* state) override {
    *state = {};
    if (is_active())
      state->gamepad = gamepad;
    return X_ERROR_SUCCESS;
  }
  X_RESULT GetDeviceStateForUI(rex::input::DeviceId, rex::input::X_INPUT_STATE* state) override {
    *state = {};
    state->gamepad = gamepad;
    return X_ERROR_SUCCESS;
  }
  rex::input::X_INPUT_GAMEPAD gamepad{};
  X_RESULT GetDeviceCapabilities(rex::input::DeviceId, uint32_t,
                                 rex::input::X_INPUT_CAPABILITIES* caps) override {
    *caps = {};
    caps->gamepad.thumb_lx = caps->gamepad.thumb_ly = 32767;
    caps->gamepad.thumb_rx = caps->gamepad.thumb_ry = 32767;
    return X_ERROR_SUCCESS;
  }
  X_RESULT SetDeviceVibration(rex::input::DeviceId,
                              rex::input::X_INPUT_VIBRATION*) override {
    return X_ERROR_SUCCESS;
  }
  X_RESULT GetDeviceKeystroke(rex::input::DeviceId, uint32_t,
                              rex::input::X_INPUT_KEYSTROKE*) override {
    return X_ERROR_EMPTY;
  }
  std::atomic<bool> concurrent{false};

 private:
  std::atomic<int> inside_{0};
};

int main() {
  rex::input::InputSystem input(nullptr);
  auto driver = std::make_unique<test_driver>();
  auto* observed = driver.get();
  input.AddDriver(std::move(driver));
  input.SetDeviceAssignment(std::make_unique<rex::input::SlotAssignment>());
  observed->gamepad.buttons = 4096;
  input.SetActiveCallback([] { return false; });
  input.AddUIInputBlocker();
  rex::input::X_INPUT_STATE guest{}, ui{};
  if (input.GetState(0, &guest) != X_ERROR_SUCCESS || guest.gamepad.buttons != 0 ||
      input.GetStateForUI(0, &ui) != X_ERROR_SUCCESS || ui.gamepad.buttons != 4096) return 2;
  input.RemoveUIInputBlocker();
  if (input.GetState(0, &guest) != X_ERROR_SUCCESS || guest.gamepad.buttons != 0) return 3;
  input.SetActiveCallback([] { return true; });
  if (input.GetState(0, &guest) != X_ERROR_SUCCESS || guest.gamepad.buttons != 4096) return 4;
  rex::cvar::SetFlagByName("left_stick_deadzone_percentage", "0.25");
  rex::cvar::SetFlagByName("right_stick_deadzone_percentage", "0.25");
  for (int16_t x : {-1, 0, 1}) {
    for (int16_t y : {-1, 0, 1}) {
      for (int16_t magnitude : {4000, 6000, 20000}) {
        observed->gamepad.thumb_lx = observed->gamepad.thumb_rx = x * magnitude;
        observed->gamepad.thumb_ly = observed->gamepad.thumb_ry = y * magnitude;
        input.GetState(0, &guest);
        input.GetStateForUI(0, &ui);
        const bool filtered = magnitude == 4000 || (magnitude == 6000 && (!x || !y));
        const int expected_x = filtered ? 0 : x * magnitude;
        const int expected_y = filtered ? 0 : y * magnitude;
        for (const auto& state : {guest, ui}) {
          if (state.gamepad.thumb_lx != expected_x || state.gamepad.thumb_rx != expected_x ||
              state.gamepad.thumb_ly != expected_y || state.gamepad.thumb_ry != expected_y)
            return 7;
        }
      }
    }
  }
  observed->gamepad.thumb_lx = -32768;
  observed->gamepad.thumb_ly = 32767;
  observed->gamepad.thumb_rx = 32767;
  observed->gamepad.thumb_ry = -32768;
  rex::cvar::SetFlagByName("left_stick_deadzone_percentage", "1");
  rex::cvar::SetFlagByName("right_stick_deadzone_percentage", "0");
  input.GetState(0, &guest);
  if (guest.gamepad.thumb_lx != 0 || guest.gamepad.thumb_ly != 0 ||
      guest.gamepad.thumb_rx != 32767 || guest.gamepad.thumb_ry != -32768)
    return 5;
  rex::cvar::SetFlagByName("left_stick_deadzone_percentage", "0");
  rex::cvar::SetFlagByName("right_stick_deadzone_percentage", "1");
  input.GetStateForUI(0, &ui);
  if (ui.gamepad.thumb_lx != -32768 || ui.gamepad.thumb_ly != 32767 || ui.gamepad.thumb_rx != 0 ||
      ui.gamepad.thumb_ry != 0)
    return 6;
  rex::cvar::SetFlagByName("right_stick_deadzone_percentage", "0");
  std::vector<std::jthread> threads;
  for (int thread = 0; thread < 4; ++thread) {
    threads.emplace_back([&input] {
      for (int iteration = 0; iteration < 2000; ++iteration) {
        rex::input::X_INPUT_STATE state{};
        rex::input::X_INPUT_CAPABILITIES caps{};
        input.GetState(0, &state);
        input.GetCapabilities(0, 0, &caps);
      }
    });
  }
  threads.clear();
  return observed->concurrent.load() ? 1 : 0;
}
