#pragma once

#include <array>
#include <atomic>
#include <optional>
#include <stdexcept>
#include <thread>

#include <imgui.h>
#include <rex/app/app.h>
#include <rex/driver_module.h>
#include <rex/driver_registration.h>
#include <rex/hid/input_driver.h>
#include <rex/hid/input_system.h>
#include <rex/overlay_renderer.h>
#include <rex/ui/ui_driver.h>

class input_fixture final : public rex::app::App {
public:
  explicit input_fixture(const char* name, bool ui = false) : App(name) {
    const std::string args[] = {ui ? "--ui.driver=test-window" : "--ui.driver=none",
                                "--apu.driver=none", "--hid.drivers=test-pad"};
    if (auto result = Init(rex::CLIArgList::Parse(args)); !result) {
      Shutdown();
      throw std::runtime_error(result.error().message);
    }
  }

  ~input_fixture() override { Shutdown(); }

  struct test_pad final : rex::hid::IInputDriver {
    std::string_view GetName() const override { return "test-pad"; }
    rex::Result<void> Initialize(rex::IDriverContext&) override { return {}; }
    void Shutdown() override {}
    rex::u64 GetDeviceRevision() const override { return revision; }
    void EnumerateDevices(std::vector<rex::hid::PadDevice>& devices) override {
      if (in_call.fetch_add(1) != 0)
        concurrent.store(true);
      std::this_thread::yield();
      if (connected)
        devices.push_back({.id = {1}, .name = "Test gamepad"});
      in_call.fetch_sub(1);
    }
    rex::Result<rex::hid::PadState> GetDeviceState(rex::hid::PadDeviceID) override {
      if (in_call.fetch_add(1) != 0)
        concurrent.store(true);
      std::this_thread::yield();
      in_call.fetch_sub(1);
      return state;
    }
    rex::Result<rex::hid::PadCapabilities> GetDeviceCapabilities(rex::hid::PadDeviceID) override {
      return rex::hid::PadCapabilities{};
    }
    rex::Result<void> SetDeviceVibration(rex::hid::PadDeviceID,
                                         const rex::hid::PadVibration&) override {
      return {};
    }

    void set_connected(bool value) {
      connected = value;
      ++revision;
    }

    rex::hid::PadState state;
    bool connected = false;
    rex::u64 revision = 1;
    std::atomic<bool> concurrent{false};
    std::atomic<int> in_call{0};
  };

  void send_key(rex::VirtualKey key, bool down) {
    pending_event_ = rex::InputEvent{
        .type = down ? rex::InputEvent::Type::kKeyDown : rex::InputEvent::Type::kKeyUp, .key = key};
    RunFrame();
  }

  test_pad& pad() { return *active_pad_; }

private:
  static inline test_pad* active_pad_ = nullptr;
  static std::unique_ptr<rex::IDriver> create_pad() {
    auto pad = std::make_unique<test_pad>();
    active_pad_ = pad.get();
    return pad;
  }

  static inline std::optional<rex::InputEvent> pending_event_;

  class test_window final : public rex::ui::IUIDriver {
  public:
    std::string_view GetName() const override { return "test-window"; }
    rex::Result<void> Initialize(rex::IDriverContext&) override { return {}; }
    void Shutdown() override {}
    bool HasDisplay() const override { return true; }
    rex::Result<void> OpenWindow(const rex::ui::WindowConfig&) override { return {}; }
    void CloseUIWindow() override {}
    void SetTitle(std::string_view) override {}
    void SetSize(rex::u32, rex::u32) override {}
    void SetLimits(rex::u32, rex::u32, rex::ui::AspectRatio) override {}
    void SetFullscreen(bool, bool, rex::i32, rex::u32, rex::u32) override {}
    void RaiseWindow() override {}
    void SetCursorShape(bool, rex::ui::CursorShape) override {}
    void SetPointerMode(rex::PointerMode) override {}
    void CaptureMouse(bool) override {}
    rex::f32 GetContentScale() const override { return 1.0f; }
    rex::ui::WindowSize GetWindowSize() const override { return {1280, 720}; }
    bool HasFocus() const override { return true; }
    void SetTextInputActive(bool) override {}
    rex::Result<rex::NativeWindow> GetNativeWindow() const override { return rex::NativeWindow{}; }
    bool WaitEvents(rex::ui::IUIEventListener& listener, rex::u32) override {
      if (pending_event_) {
        listener.OnInputEvent(*pending_event_);
        pending_event_.reset();
      }
      return true;
    }
    void WakeUp() override {}
    rex::Result<rex::u32> ShowMessageBox(rex::MessageType, std::string_view, std::string_view,
                                         std::span<const std::string>) override {
      return 0;
    }
    rex::Result<std::vector<std::filesystem::path>>
    ShowFileDialog(const rex::ui::FileDialogConfig&) override {
      return std::vector<std::filesystem::path>{};
    }
    std::vector<rex::ui::Monitor> GetDisplays() override { return {}; }
    std::vector<rex::ui::HostLocale> GetPreferredLocales() override { return {}; }
    std::string GetClipboardText() override { return {}; }
    void SetClipboardText(std::string_view) override {}
  };

  static std::unique_ptr<rex::IDriver> create_window() { return std::make_unique<test_window>(); }

  class pad_module final : public rex::IModule, public rex::IDriverModule {
  public:
    std::string_view GetName() const override { return "test_pad"; }
    rex::Result<void> Connect(rex::IInterfaceFactory&) override { return {}; }
    void Disconnect() override {}
    rex::Result<void> Init() override { return {}; }
    void Shutdown() override {}
    rex::IInterface* QueryInterface(std::string_view name, rex::u32 version) override {
      return rex::FindModuleInterface<rex::IDriverModule>(*this, name, version);
    }
    std::span<const rex::DriverRegistration> GetDrivers() const override {
      static constexpr rex::DriverRegistration registrations[] = {
          {rex::DriverType::kInput, "test-pad", {}, &create_pad},
          {rex::DriverType::kUI, "test-window", {}, &create_window}};
      return registrations;
    }
  };

  void OnRegisterModules(std::vector<rex::IModule*>& modules) override {
    modules.push_back(&module_);
  }

  pad_module module_;
};

class test_renderer final : public rex::IOverlayRenderer {
  void RenderOverlays(ImDrawData& data) override {
    if (data.Textures)
      for (auto* texture : *data.Textures) {
        if (texture->Status == ImTextureStatus_WantCreate)
          texture->SetTexID(1);
        texture->SetStatus(ImTextureStatus_OK);
      }
  }
};
