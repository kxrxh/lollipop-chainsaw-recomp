#include "test_bridge.h"
#include "game_settings.h"

#include <array>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <windows.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <rex/cvar.h>
#include <rex/input/input_system.h>
#include <rex/ui/windowed_app_context.h>
#include <rex/ui/imgui_drawer.h>
#include <rex/ui/presenter.h>
#include <rex/ui/renderdoc_api.h>
#include <rex/ui/window.h>

namespace {
using rex::X_RESULT;
using rex::X_STATUS;
constexpr auto device_id =
    static_cast<rex::input::DeviceId>(0x4C4F4C4C49544553);
constexpr auto gamepad_id =
    static_cast<rex::input::DeviceId>(0x4C4F4C4C49544554);

class test_driver final : public rex::input::InputDriver {
 public:
  explicit test_driver(std::shared_ptr<test_controls> controls)
      : InputDriver(nullptr, 0), controls_(std::move(controls)) {}
  rex::X_STATUS Setup() override { return X_STATUS_SUCCESS; }
  void EnumerateDevices(std::vector<rex::input::DeviceInfo>& out) override {
    rex::input::DeviceInfo device;
    device.id = device_id;
    device.name = "Lollipop test keyboard/mouse";
    device.synthetic = true;
    out.push_back(device);
    device.id = gamepad_id;
    device.name = "Lollipop test gamepad";
    device.synthetic = false;
    out.push_back(std::move(device));
  }
  rex::X_RESULT GetDeviceState(rex::input::DeviceId id,
                               rex::input::X_INPUT_STATE* state) override {
    if (!state) return X_ERROR_BAD_ARGUMENTS;
    std::lock_guard lock(controls_->mutex);
    auto& value = id == gamepad_id ? controls_->gamepad_state : controls_->state;
    auto& release = id == gamepad_id ? controls_->gamepad_release_at : controls_->release_at;
    if (release != std::chrono::steady_clock::time_point{} &&
        std::chrono::steady_clock::now() >= release) {
      value.gamepad = {};
      value.packet_number = uint32_t(value.packet_number) + 1;
      release = {};
    }
    *state = value;
    return X_ERROR_SUCCESS;
  }
  rex::X_RESULT GetDeviceCapabilities(
      rex::input::DeviceId, uint32_t,
      rex::input::X_INPUT_CAPABILITIES* caps) override {
    if (!caps) return X_ERROR_BAD_ARGUMENTS;
    *caps = {};
    caps->type = 1;
    caps->sub_type = 1;
    caps->gamepad.buttons = 0xFFFF;
    caps->gamepad.thumb_lx = caps->gamepad.thumb_ly = 32767;
    caps->gamepad.thumb_rx = caps->gamepad.thumb_ry = 32767;
    return X_ERROR_SUCCESS;
  }
  rex::X_RESULT SetDeviceVibration(rex::input::DeviceId,
                                   rex::input::X_INPUT_VIBRATION*) override {
    return X_ERROR_SUCCESS;
  }
  rex::X_RESULT GetDeviceKeystroke(rex::input::DeviceId, uint32_t,
                                   rex::input::X_INPUT_KEYSTROKE*) override {
    return X_ERROR_EMPTY;
  }

 private:
  std::shared_ptr<test_controls> controls_;
};
std::string capture_frame(rex::ui::Presenter& presenter,
                          const std::filesystem::path& path) {
  rex::ui::RawImage image;
  if (!presenter.CaptureGuestOutput(image)) return "error no guest image";
  if (!image.width || !image.height || image.width > 8192 ||
      image.height > 8192 || image.stride < image.width * 4 ||
      image.data.size() < (image.height - 1) * image.stride + image.width * 4) {
    return "error invalid capture dimensions";
  }
  BITMAPFILEHEADER file_header{};
  BITMAPINFOHEADER info{};
  file_header.bfType = 0x4D42;
  file_header.bfOffBits = sizeof(file_header) + sizeof(info);
  file_header.bfSize = file_header.bfOffBits + image.width * image.height * 4;
  info.biSize = sizeof(info);
  info.biWidth = LONG(image.width);
  info.biHeight = -LONG(image.height);
  info.biPlanes = 1;
  info.biBitCount = 32;
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file.write(reinterpret_cast<const char*>(&file_header), sizeof(file_header));
  file.write(reinterpret_cast<const char*>(&info), sizeof(info));
  for (uint32_t y = 0; y < image.height; ++y) {
    auto* row = image.data.data() + y * image.stride;
    for (uint32_t x = 0; x < image.width; ++x)
      std::swap(row[x * 4], row[x * 4 + 2]);
    file.write(reinterpret_cast<const char*>(row), image.width * 4);
  }
  file.close();
  if (!file) return "error capture write failed";
  return "ok " + std::to_string(image.width) + "x" +
         std::to_string(image.height);
}
}  // namespace

std::unique_ptr<rex::input::InputDriver> make_test_driver(
    std::shared_ptr<test_controls> controls) {
  return std::make_unique<test_driver>(std::move(controls));
}

std::unique_ptr<rex::input::DeviceAssignment> make_test_assignment() {
  return std::make_unique<rex::input::SharedAssignment>();
}

std::string execute_test_command(test_controls& controls,
                                 const std::string& command) {
  if (command.size() > 512) return "error command too long";
  std::istringstream words(command);
  std::string operation;
  words >> operation;
  if (operation == "game_settings_state") {
    if (!(words >> std::ws).eof()) return "error game_settings_state takes no arguments";
    return "ok " + game_settings_state();
  }
  if (operation == "ui_key") {
    std::string key;
    int down = -1;
    words >> key >> down;
    const std::pair<const char*, ImGuiKey> keys[] = {
        {"enter", ImGuiKey_Enter}, {"escape", ImGuiKey_Escape},
        {"backspace", ImGuiKey_Backspace}, {"tab", ImGuiKey_Tab},
        {"up", ImGuiKey_UpArrow}, {"down", ImGuiKey_DownArrow},
        {"left", ImGuiKey_LeftArrow}, {"right", ImGuiKey_RightArrow}};
    for (const auto& [name, code] : keys) {
      if (key != name) continue;
      if (!words || !(words >> std::ws).eof() || down < 0 || down > 1) break;
      std::lock_guard lock(controls.mutex);
      controls.key_event = std::array{int(code), down};
      return "ok key";
    }
    return "error ui_key expects a navigation key and down (0 or 1)";
  }
  if (operation == "input_device") {
    std::string device;
    if (!(words >> device) || !(words >> std::ws).eof() ||
        (device != "keyboard" && device != "gamepad"))
      return "error input_device expects keyboard or gamepad";
    controls.keyboard_input.store(device == "keyboard");
    return "ok " + device;
  }
  if (operation == "ui_mouse") {
    int x, y, down;
    if (!(words >> x >> y >> down) || !(words >> std::ws).eof() || x < 0 ||
        x > 8192 || y < 0 || y > 8192 || down < 0 || down > 1) {
      return "error ui_mouse expects x y down (0 or 1)";
    }
    std::lock_guard lock(controls.mutex);
    controls.mouse_event = std::array{x, y, down};
    return "ok mouse";
  }
  if (operation == "input") {
    int buttons, lx, ly, rx, ry, duration;
    if (!(words >> buttons >> lx >> ly >> rx >> ry >> duration) ||
        !(words >> std::ws).eof() || buttons < 0 || buttons > 65535 ||
        lx < -32768 || lx > 32767 || ly < -32768 || ly > 32767 || rx < -32768 ||
        rx > 32767 || ry < -32768 || ry > 32767 || duration < 0 ||
        duration > 10000) {
      return "error input expects buttons lx ly rx ry duration_ms (0..10000)";
    }
    std::lock_guard lock(controls.mutex);
    auto& state = controls.keyboard_input.load() ? controls.state : controls.gamepad_state;
    auto& release = controls.keyboard_input.load() ? controls.release_at : controls.gamepad_release_at;
    state.packet_number = uint32_t(state.packet_number) + 1;
    state.gamepad.buttons = uint16_t(buttons);
    state.gamepad.thumb_lx = int16_t(lx);
    state.gamepad.thumb_ly = int16_t(ly);
    state.gamepad.thumb_rx = int16_t(rx);
    state.gamepad.thumb_ry = int16_t(ry);
    release =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(duration);
    return "ok input";
  }
  std::string name, value;
  words >> name;
  const auto* flag = rex::cvar::GetFlagInfo(name);
  if (!flag) return "error unknown setting";
  if (operation == "get" && (words >> std::ws).eof()) {
    return "ok " + rex::cvar::GetFlagByName(name);
  }
  if (operation == "set" && words >> value && (words >> std::ws).eof()) {
    if (flag->lifecycle != rex::cvar::Lifecycle::kHotReload ||
        flag->type == rex::cvar::FlagType::Command) {
      return "error setting requires restart or is a command";
    }
    if (!rex::cvar::SetFlagByName(name, value)) return "error invalid value";
    return "ok " + rex::cvar::GetFlagByName(name);
  }
  return "error expected get, set, or input";
}

std::jthread start_test_bridge(rex::input::InputSystem& input,
                               rex::ui::WindowedAppContext& context,
                               const std::filesystem::path& directory,
                               rex::ui::ImGuiDrawer& drawer,
                               rex::ui::Presenter& presenter,
                               rex::ui::Window& window) {
  std::filesystem::create_directories(directory);
  auto controls = std::make_shared<test_controls>();
  input.AddDriver(make_test_driver(controls));
  input.SetDeviceAssignment(make_test_assignment());
  return std::jthread([controls, &input, &context, directory, &drawer, &presenter,
                       &window](std::stop_token stop) {
    std::string last_request;
    bool first_poll = true;
    while (!stop.stop_requested()) {
      std::ifstream file(directory / "command.txt", std::ios::binary);
      std::array<char, 640> buffer{};
      file.read(buffer.data(), buffer.size());
      std::string request(buffer.data(), size_t(file.gcount()));
      file.close();
      if (!first_poll && !request.empty() && request != last_request) {
        last_request = request;
        std::istringstream lines(request);
        std::string id, command;
        std::getline(lines, id);
        std::getline(lines, command);
        auto execute = [controls, &input, directory, id, command, &drawer,
                        &presenter, &window] {
          if (!controls->active.load()) return;
          std::string result;
          if (command == "input_state") {
            rex::input::X_INPUT_STATE guest{}, ui{};
            const auto status = input.GetState(0, &guest);
            const auto ui_status = input.GetStateForUI(0, &ui);
            result = "ok status=" + std::to_string(status) +
                     " guest_buttons=" + std::to_string(uint16_t(guest.gamepad.buttons)) +
                     " ui_status=" + std::to_string(ui_status) +
                     " ui_buttons=" + std::to_string(uint16_t(ui.gamepad.buttons)) +
                     " hints=" + (input.IsKeyboardActive() ? "keyboard" : "gamepad");
          } else if (command == "gpu_capture_start" || command == "gpu_capture_stop") {
            const auto capture = rex::ui::RenderDocAPI::CreateIfConnected();
            if (!capture) {
              result = "error launch this test under RenderDoc first";
            } else if (command == "gpu_capture_start") {
              capture->api_1_0_0()->StartFrameCapture(nullptr, nullptr);
              result = "ok GPU capture started";
            } else {
              result = capture->api_1_0_0()->EndFrameCapture(nullptr, nullptr)
                           ? "ok GPU capture saved"
                           : "error GPU capture failed";
            }
          } else if (command == "capture") {
            result = capture_frame(presenter, directory / "capture.bmp");
          } else if (command == "ui_state") {
            drawer.GetIO();
            result = "ok ";
            for (const auto* window : ImGui::GetCurrentContext()->Windows) {
              if (window->WasActive && !window->Hidden) {
                result += " | " + std::string(window->Name) + " " +
                          std::to_string(int(window->Pos.x)) + "," +
                          std::to_string(int(window->Pos.y)) + " " +
                          std::to_string(int(window->Size.x)) + "x" +
                          std::to_string(int(window->Size.y));
              }
            }
          } else if (command == "quit") {
            result = "ok quit";
          } else {
            result = execute_test_command(*controls, command);
            if (result == "ok mouse") {
              std::lock_guard lock(controls->mutex);
              const auto event = *controls->mouse_event;
              auto& io = drawer.GetIO();
              io.AddMousePosEvent(float(event[0]), float(event[1]));
              io.AddMouseButtonEvent(0, event[2] != 0);
              controls->mouse_event.reset();
            } else if (result == "ok key") {
              std::lock_guard lock(controls->mutex);
              const auto event = *controls->key_event;
              drawer.GetIO().AddKeyEvent(static_cast<ImGuiKey>(event[0]), event[1] != 0);
              controls->key_event.reset();
            }
          }
          std::ofstream response(directory / "response.txt", std::ios::trunc);
          response << id << '\n' << result << '\n';
          response.close();
          if (command == "quit") window.RequestClose();
        };
        // Controller timing must not depend on the UI's render queue.
        if (command.starts_with("input ") || command.starts_with("get ") ||
            command == "input_state") {
          execute();
        } else {
          context.CallInUIThreadDeferred(std::move(execute));
        }
      }
      last_request = request;
      first_poll = false;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    controls->active.store(false);
  });
}
