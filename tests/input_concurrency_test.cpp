#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

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
  X_RESULT GetDeviceState(rex::input::DeviceId,
                          rex::input::X_INPUT_STATE* state) override {
    *state = {};
    if (is_active()) state->gamepad.buttons = buttons;
    return X_ERROR_SUCCESS;
  }
  X_RESULT GetDeviceStateForUI(rex::input::DeviceId,
                               rex::input::X_INPUT_STATE* state) override {
    *state = {};
    state->gamepad.buttons = buttons;
    return X_ERROR_SUCCESS;
  }
  uint16_t buttons = 0;
  X_RESULT GetDeviceCapabilities(
      rex::input::DeviceId, uint32_t,
      rex::input::X_INPUT_CAPABILITIES* caps) override {
    *caps = {};
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
  observed->buttons = 4096;
  input.SetActiveCallback([] { return false; });
  input.AddUIInputBlocker();
  rex::input::X_INPUT_STATE guest{}, ui{};
  if (input.GetState(0, &guest) != X_ERROR_SUCCESS || guest.gamepad.buttons != 0 ||
      input.GetStateForUI(0, &ui) != X_ERROR_SUCCESS || ui.gamepad.buttons != 4096) return 2;
  input.RemoveUIInputBlocker();
  if (input.GetState(0, &guest) != X_ERROR_SUCCESS || guest.gamepad.buttons != 0) return 3;
  input.SetActiveCallback([] { return true; });
  if (input.GetState(0, &guest) != X_ERROR_SUCCESS || guest.gamepad.buttons != 4096) return 4;
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
