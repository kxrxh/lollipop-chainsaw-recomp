#include "input_fixture.h"

#include <iostream>

int main() try {
  input_fixture app("lollipop-input-test");
  auto& input = app.GetInput();
  auto* observed = &app.pad();
  observed->set_connected(true);
  observed->state.buttons = 0x3010; // A, B and Start can close settings.
  observed->state.left_trigger = observed->state.right_trigger = 255;
  input.AcquireUIBlock();
  rex::hid::PadState guest{}, ui{};
  if (input.GetState(0).value().state.buttons != 0 ||
      input.GetStateForUI(0).value().buttons != 0x3010)
    return 2;
  input.ReleaseUIBlock();
  if (input.GetState(0).value().state.buttons != 0)
    return 3;
  guest = input.GetState(0).value().state;
  if (guest.buttons != 0 || guest.left_trigger != 0 || guest.right_trigger != 0)
    return 4;
  // New actions still work while the closing buttons remain held.
  observed->state.buttons = 0xB010;
  guest = input.GetState(0).value().state;
  if (guest.buttons != 0x8000)
    return 8;
  observed->state.buttons = 0x2010;
  guest = input.GetState(0).value().state;
  if (guest.buttons != 0)
    return 9;
  observed->state.buttons = 0x3010;
  guest = input.GetState(0).value().state;
  if (guest.buttons != 0x1000)
    return 10;
  // A physical release under capture must also clear only that trigger's mask.
  observed->state.left_trigger = 0;
  guest = input.GetState(0).value().state;
  observed->state.left_trigger = 255;
  guest = input.GetState(0).value().state;
  if (guest.buttons != 0x1000 || guest.left_trigger != 255 || guest.right_trigger != 0)
    return 11;
  observed->state = {};
  guest = input.GetState(0).value().state;
  observed->state.buttons = 0x3010;
  observed->state.left_trigger = observed->state.right_trigger = 255;
  guest = input.GetState(0).value().state;
  if (guest.buttons != 0x3010 || guest.left_trigger != 255 || guest.right_trigger != 255)
    return 12;
  observed->state = {};
  observed->state.buttons = 0x1000;
  input.AcquireUIBlock();
  input.ReleaseUIBlock();
  observed->state = {};
  if (input.GetState(0).value().state.buttons != 0)
    return 13;
  (void)input.GetSettings().left_stick_deadzone.Set(0.25);
  (void)input.GetSettings().right_stick_deadzone.Set(0.25);
  for (int16_t x : {-1, 0, 1}) {
    for (int16_t y : {-1, 0, 1}) {
      for (int16_t magnitude : {4000, 6000, 20000}) {
        observed->state.thumb_lx = observed->state.thumb_rx = x * magnitude;
        observed->state.thumb_ly = observed->state.thumb_ry = y * magnitude;
        guest = input.GetState(0).value().state;
        ui = input.GetStateForUI(0).value();
        const bool filtered = magnitude == 4000 || (magnitude == 6000 && (!x || !y));
        const int expected_x = filtered ? 0 : x * magnitude;
        const int expected_y = filtered ? 0 : y * magnitude;
        for (const auto& state : {guest, ui}) {
          if (state.thumb_lx != expected_x || state.thumb_rx != expected_x ||
              state.thumb_ly != expected_y || state.thumb_ry != expected_y)
            return 7;
        }
      }
    }
  }
  observed->state.thumb_lx = -32768;
  observed->state.thumb_ly = 32767;
  observed->state.thumb_rx = 32767;
  observed->state.thumb_ry = -32768;
  (void)input.GetSettings().left_stick_deadzone.Set(1);
  (void)input.GetSettings().right_stick_deadzone.Set(0);
  guest = input.GetState(0).value().state;
  if (guest.thumb_lx != 0 || guest.thumb_ly != 0 || guest.thumb_rx != 32767 ||
      guest.thumb_ry != -32768)
    return 5;
  (void)input.GetSettings().left_stick_deadzone.Set(0);
  (void)input.GetSettings().right_stick_deadzone.Set(1);
  ui = input.GetStateForUI(0).value();
  if (ui.thumb_lx != -32768 || ui.thumb_ly != 32767 || ui.thumb_rx != 0 || ui.thumb_ry != 0)
    return 6;
  (void)input.GetSettings().right_stick_deadzone.Set(0);
  std::vector<std::jthread> threads;
  for (int thread = 0; thread < 4; ++thread) {
    threads.emplace_back([&input] {
      for (int iteration = 0; iteration < 2000; ++iteration) {
        (void)input.GetState(0);
        (void)input.GetStateForUI(0);
        (void)input.GetCapabilities(0);
      }
    });
  }
  threads.clear();
  return observed->concurrent.load() ? 1 : 0;
} catch (const std::exception& error) {
  std::cerr << error.what() << '\n';
  return 100;
}
