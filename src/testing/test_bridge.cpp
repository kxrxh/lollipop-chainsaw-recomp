#include "test_bridge.h"
#include "game/cheats.h"
#include "game/game_settings.h"
#include "game/native_quit_dialog.h"

#include <array>
#include <fstream>
#include <imgui.h>
#include <imgui_internal.h>
#include <sstream>
#include <stdexcept>
#include <windows.h>

#include "settings/runtime_settings.h"
#include <renderdoc_app.h>
#include <rex/gpu/graphics_system.h>
#include <rex/hid/input_system.h>
#include <rex/ui/ui_system.h>

namespace lollipop {
namespace {
std::string capture_frame(const std::filesystem::path& path) {
  auto* graphics = REX_INTERFACE(rex::gpu::IGraphicsSystem);
  if (!graphics)
    return "error no graphics system";
  auto image = graphics->CaptureGuestFrame();
  if (!image)
    return "error " + image.error().GetDescription();
  if (!image->width || !image->height || image->width > 8192 || image->height > 8192 ||
      image->rgba.size() != size_t(image->width) * image->height * 4)
    return "error invalid capture dimensions";

  BITMAPFILEHEADER header{};
  BITMAPINFOHEADER info{};
  header.bfType = 0x4D42;
  header.bfOffBits = sizeof(header) + sizeof(info);
  header.bfSize = header.bfOffBits + DWORD(image->rgba.size());
  info.biSize = sizeof(info);
  info.biWidth = LONG(image->width);
  info.biHeight = -LONG(image->height);
  info.biPlanes = 1;
  info.biBitCount = 32;
  for (size_t offset = 0; offset < image->rgba.size(); offset += 4)
    std::swap(image->rgba[offset], image->rgba[offset + 2]);
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file.write(reinterpret_cast<const char*>(&header), sizeof(header));
  file.write(reinterpret_cast<const char*>(&info), sizeof(info));
  file.write(reinterpret_cast<const char*>(image->rgba.data()), image->rgba.size());
  file.close();
  if (!file)
    return "error capture write failed";
  return "ok " + std::to_string(image->width) + "x" + std::to_string(image->height);
}

std::string control_gpu_capture(bool start) {
  const auto module = GetModuleHandleW(L"renderdoc.dll");
  const auto get_api =
      module ? reinterpret_cast<pRENDERDOC_GetAPI>(GetProcAddress(module, "RENDERDOC_GetAPI"))
             : nullptr;
  void* api_pointer = nullptr;
  if (!get_api || !get_api(eRENDERDOC_API_Version_1_0_0, &api_pointer) || !api_pointer)
    return "error launch this test under RenderDoc first";
  const auto* api = static_cast<const RENDERDOC_API_1_0_0*>(api_pointer);
  if (start) {
    api->StartFrameCapture(nullptr, nullptr);
    return "ok GPU capture started";
  }
  return api->EndFrameCapture(nullptr, nullptr) ? "ok GPU capture saved"
                                                : "error GPU capture failed";
}
} // namespace

rex::hid::PadState test_controls::read_pad(bool keyboard) {
  std::lock_guard lock(mutex);
  auto& value = keyboard ? state : gamepad_state;
  auto& release = keyboard ? release_at : gamepad_release_at;
  if (release != std::chrono::steady_clock::time_point{} &&
      std::chrono::steady_clock::now() >= release) {
    value = {};
    release = {};
  }
  return value;
}

std::string execute_test_command(test_controls& controls,
                                 const std::string& command) {
  if (command.size() > 512) return "error command too long";
  if (command.find_first_of("\r\n\0", 0, 3) != std::string::npos)
    return "error command must be a single line";
  std::istringstream words(command);
  std::string operation;
  words >> operation;
  if (operation == "game_cheats_state") {
    if (!(words >> std::ws).eof())
      return "error game_cheats_state takes no arguments";
    return "ok " + game_cheats_state();
  }
  if (operation == "game_console") {
    std::string value;
    std::getline(words >> std::ws, value);
    return queue_game_console(value);
  }
  if (operation == "game_console_state") {
    if (!(words >> std::ws).eof())
      return "error game_console_state takes no arguments";
    return "ok " + game_console_state();
  }
  if (operation == "game_settings_state") {
    if (!(words >> std::ws).eof()) return "error game_settings_state takes no arguments";
    return "ok " + game_settings_state();
  }
  if (operation == "quit_state") {
    if (!(words >> std::ws).eof()) return "error quit_state takes no arguments";
    constexpr const char* names[] = {"idle", "requested", "opening", "active", "cancelled", "confirmed", "failed"};
    return "ok " + std::string(names[static_cast<unsigned>(native_quit().status())]);
  }
  if (operation == "quit_selection") {
    if (!(words >> std::ws).eof()) return "error quit_selection takes no arguments";
    return "ok " + std::to_string(native_quit().selection());
  }
  if (operation == "ui_key") {
    std::string key;
    int down = -1;
    words >> key >> down;
    const std::pair<const char*, ImGuiKey> keys[] = {
        {"enter", ImGuiKey_Enter}, {"escape", ImGuiKey_Escape},
        {"backspace", ImGuiKey_Backspace}, {"space", ImGuiKey_Space}, {"tab", ImGuiKey_Tab},
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
    int lt = 0, rt = 0;
    bool valid = bool(words >> buttons >> lx >> ly >> rx >> ry >> duration);
    if (valid && !(words >> std::ws).eof()) {
      valid = bool(words >> lt >> rt) && (words >> std::ws).eof();
    }
    if (!valid || buttons < 0 || buttons > 65535 || lx < -32768 || lx > 32767 || ly < -32768 ||
        ly > 32767 || rx < -32768 || rx > 32767 || ry < -32768 || ry > 32767 || duration < 0 ||
        duration > 10000 || lt < 0 || lt > 255 || rt < 0 || rt > 255) {
      return "error input expects buttons lx ly rx ry duration_ms (0..10000) [lt rt] (0..255)";
    }
    std::lock_guard lock(controls.mutex);
    const bool keyboard = controls.keyboard_input.load();
    auto& state = keyboard ? controls.state : controls.gamepad_state;
    auto& release = keyboard ? controls.release_at : controls.gamepad_release_at;
    state.buttons = uint16_t(buttons);
    state.thumb_lx = int16_t(lx);
    state.thumb_ly = int16_t(ly);
    state.thumb_rx = int16_t(rx);
    state.thumb_ry = int16_t(ry);
    state.left_trigger = uint8_t(lt);
    state.right_trigger = uint8_t(rt);
    release = std::chrono::steady_clock::now() + std::chrono::milliseconds(duration);
    return "ok input";
  }
  std::string name, value;
  words >> name;
  const auto* flag = settings::GetFlagInfo(name);
  if (!flag) return "error unknown setting";
  if (operation == "get" && (words >> std::ws).eof()) {
    return "ok " + settings::GetFlagByName(name);
  }
  if (operation == "set" && words >> value && (words >> std::ws).eof()) {
    if (flag->GetConstraints().life != rex::SettingLife::kLive) {
      return "error setting requires restart or is a command";
    }
    if (!settings::SetFlagByName(name, value))
      return "error invalid value";
    return "ok " + settings::GetFlagByName(name);
  }
  return "error expected get, set, or input";
}

std::jthread start_test_bridge(rex::hid::IInputSystem& input, rex::ui::IUISystem& ui,
                               const std::filesystem::path& directory, std::function<void()> quit) {
  std::filesystem::create_directories(directory);
  auto controls = std::make_shared<test_controls>();
  input.SetTestInput([controls](bool& keyboard) {
    keyboard = controls->keyboard_input.load();
    return controls->read_pad(keyboard);
  });
  return std::jthread([controls, &input, &ui, directory,
                       quit = std::move(quit)](std::stop_token stop) {
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
        auto execute = [controls, &input, &ui, directory, id, command, quit] {
          if (!controls->active.load())
            return;
          std::string result;
          if (command == "input_state") {
            const auto guest = input.GetState(0);
            const auto overlay = input.GetStateForUI(0);
            const auto game = guest ? guest->state : rex::hid::PadState{};
            const auto draw = overlay.value_or(rex::hid::PadState{});
            constexpr unsigned disconnected = 1167;
            result = "ok status=" + std::to_string(guest ? 0 : disconnected) +
                     " guest_buttons=" + std::to_string(game.buttons) +
                     " guest_lt=" + std::to_string(game.left_trigger) +
                     " guest_rt=" + std::to_string(game.right_trigger) +
                     " ui_status=" + std::to_string(overlay ? 0 : disconnected) +
                     " ui_buttons=" + std::to_string(draw.buttons) +
                     " ui_lt=" + std::to_string(draw.left_trigger) +
                     " ui_rt=" + std::to_string(draw.right_trigger) +
                     " hints=" + (input.IsKeyboardActive() ? "keyboard" : "gamepad");
          } else if (command == "presentation_state") {
            const auto* graphics = REX_INTERFACE(rex::gpu::IGraphicsSystem);
            result = graphics ? std::string("ok dither=") +
                                    (graphics->GetAppliedDither() ? "true" : "false")
                              : "error no graphics system";
          } else if (command == "gpu_capture_start" || command == "gpu_capture_stop") {
            result = control_gpu_capture(command == "gpu_capture_start");
          } else if (command == "capture") {
            result = capture_frame(directory / "capture.bmp");
          } else if (command == "ui_state") {
            result = "ok ";
            for (const auto* window : ui.GetImGuiIO().Ctx->Windows) {
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
              auto& io = ui.GetImGuiIO();
              io.AddMousePosEvent(float(event[0]), float(event[1]));
              io.AddMouseButtonEvent(0, event[2] != 0);
              controls->mouse_event.reset();
            } else if (result == "ok key") {
              std::lock_guard lock(controls->mutex);
              const auto event = *controls->key_event;
              ui.GetImGuiIO().AddKeyEvent(static_cast<ImGuiKey>(event[0]), event[1] != 0);
              controls->key_event.reset();
            }
          }
          std::ofstream response(directory / "response.txt", std::ios::trunc);
          response << id << '\n' << result << '\n';
          response.close();
          if (command == "quit")
            quit();
        };
        // Controller timing must not depend on the UI's render queue.
        if (command.starts_with("input ") || command.starts_with("get ") ||
            command == "input_state") {
          execute();
        } else {
          ui.PostTask(std::move(execute));
        }
      }
      last_request = request;
      first_poll = false;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    controls->active.store(false);
    input.SetTestInput({});
  });
}

}  // namespace lollipop
