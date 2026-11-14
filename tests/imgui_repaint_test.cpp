#include <imgui.h>
#include <iostream>
#include <memory>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>
#include <rex/ui/immediate_drawer.h>
#include <rex/ui/overlay/achievement_toast.h>
#include <rex/ui/overlay/debug_overlay.h>
#include <rex/ui/window.h>
#include <rex/ui/windowed_app_context.h>

namespace {
using namespace rex::ui;

class test_context final : public WindowedAppContext {
  void NotifyUILoopOfPendingFunctions() override {}
  void PlatformQuitFromUIThread() override {}
};

class test_surface final : public Surface {
public:
  TypeIndex GetType() const override { return kTypeIndex_Win32Hwnd; }

private:
  bool GetSizeImpl(uint32_t& width, uint32_t& height) const override {
    width = 1280;
    height = 720;
    return true;
  }
};

class test_window final : public Window {
public:
  explicit test_window(WindowedAppContext& context) : Window(context, "Repaint test", 1280, 720) {}
  ~test_window() override { EnterDestructor(); }
  int paints = 0;

private:
  bool OpenImpl() override {
    WindowDestructionReceiver receiver(this);
    OnActualSizeUpdate(1280, 720, receiver);
    return true;
  }
  void RequestCloseImpl() override {}
  std::unique_ptr<Surface> CreateSurfaceImpl(Surface::TypeFlags) override {
    return std::make_unique<test_surface>();
  }
  void RequestPaintImpl() override { ++paints; }
};

class test_presenter final : public Presenter {
public:
  test_presenter() : Presenter({}) {}
  Surface::TypeFlags GetSupportedSurfaceTypes() const override {
    return Surface::kTypeFlag_Win32Hwnd;
  }
  bool CaptureGuestOutput(RawImage&) override { return false; }

private:
  SurfacePaintConnectResult
  ConnectOrReconnectPaintingToSurfaceFromUIThread(Surface&, uint32_t, uint32_t, bool,
                                                  bool& implicit_vsync) override {
    implicit_vsync = true;
    return SurfacePaintConnectResult::kSuccess;
  }
  void DisconnectPaintingFromSurfaceFromUIThreadImpl() override {}
  bool RefreshGuestOutputImpl(uint32_t, uint32_t, uint32_t,
                              std::function<bool(GuestOutputRefreshContext&)>, bool&) override {
    return true;
  }
  PaintResult PaintAndPresentImpl(bool) override { return PaintResult::kPresented; }
};

class test_texture final : public ImmediateTexture {
public:
  test_texture(uint32_t width, uint32_t height) : ImmediateTexture(width, height) {}
};

class test_drawer final : public ImmediateDrawer {
public:
  std::unique_ptr<ImmediateTexture> CreateTexture(uint32_t width, uint32_t height,
                                                  ImmediateTextureFilter, bool,
                                                  const uint8_t*) override {
    return std::make_unique<test_texture>(width, height);
  }
  void BeginDrawBatch(const ImmediateDrawBatch&) override {}
  void Draw(const ImmediateDraw&) override {}
  void EndDrawBatch() override {}
};

class test_dialog final : public ImGuiDialog {
public:
  explicit test_dialog(ImGuiDrawer& drawer) : ImGuiDialog(&drawer) {}
  bool IsContinuousRepaintNeeded() const override { return visible; }
  bool visible = false;
  int updates = 0;

private:
  void OnDraw(ImGuiIO&) override {
    ++updates;
    if (visible) {
      ImGui::SetNextWindowPos(ImVec2(10, 10));
      ImGui::SetNextWindowSize(ImVec2(200, 100));
      ImGui::Begin("Settings test");
      ImGui::TextUnformatted("Visible settings keep repainting.");
      ImGui::End();
    }
  }
};

class continuous_dialog final : public ImGuiDialog {
public:
  explicit continuous_dialog(ImGuiDrawer& drawer) : ImGuiDialog(&drawer) {}
};
} // namespace

int main() {
  test_context context;
  test_window window(context);
  test_presenter presenter;
  test_drawer immediate;
  if (!window.Open())
    return 1;
  window.SetPresenter(&presenter);
  ImGuiDrawer drawer(&window, 0);
  drawer.SetPresenterAndImmediateDrawer(&presenter, &immediate);
  test_dialog dialog(drawer);
  AppUIDrawContext frame(1280, 720);
  DebugOverlayDialog debug(&drawer, [] { return FrameStats{1000.0 / 60.0, 60.0, 1}; });
  AchievementToastDialog toast(&drawer, &immediate, nullptr);
  for (bool visible : {false, true, false, true, false}) {
    dialog.visible = visible;
    for (int i = 0; i < 2; ++i) {
      window.paints = 0;
      const auto updates = dialog.updates;
      drawer.Draw(frame);
      if ((window.paints > 0) != visible || dialog.updates != updates + 1) {
        std::cerr << "Hidden settings and the FPS overlay must update without extra presents; "
                     "visible dialogs must keep repainting.\n";
        return 2;
      }
    }
  }
  continuous_dialog continuous(drawer);
  window.paints = 0;
  drawer.Draw(frame);
  if (window.paints == 0) {
    std::cerr << "Ordinary host dialogs must keep repainting without guest frames.\n";
    return 3;
  }
}
