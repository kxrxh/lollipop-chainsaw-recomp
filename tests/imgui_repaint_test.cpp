#include "input_fixture.h"

#include <imgui.h>
#include <iostream>
#include <rex/ui/ui_system.h>

namespace {
class test_dialog final : public rex::ui::DialogBase {
public:
  bool NeedsContinuousPaint() const override { return visible; }
  bool Draw(ImGuiIO&) override {
    ++updates;
    if (visible) {
      ImGui::Begin("Settings test");
      ImGui::TextUnformatted("Visible settings keep repainting.");
      ImGui::End();
    }
    return true;
  }

  bool visible = false;
  int updates = 0;
};

class continuous_dialog final : public rex::ui::DialogBase {
  bool Draw(ImGuiIO&) override { return true; }
};
} // namespace

int main() try {
  input_fixture app("lollipop-imgui-repaint-test", true);
  auto& ui = app.GetUI();
  test_renderer renderer;
  test_dialog dialog;
  auto dialog_guard = ui.AddDialog(dialog);
  ui.ShowOverlay("debug", true);
  for (bool visible : {false, true, false, true, false}) {
    dialog.visible = visible;
    for (int i = 0; i < 2; ++i) {
      const auto updates = dialog.updates;
      ui.DrawOverlays(renderer);
      if (ui.NeedsContinuousPaint() != visible || dialog.updates != updates + 1) {
        std::cerr << "Hidden settings and the FPS overlay must update without extra presents; "
                     "visible dialogs must keep repainting.\n";
        return 2;
      }
    }
  }
  continuous_dialog continuous;
  auto continuous_guard = ui.AddDialog(continuous);
  ui.DrawOverlays(renderer);
  if (!ui.NeedsContinuousPaint()) {
    std::cerr << "Ordinary host dialogs must keep repainting without guest frames.\n";
    return 3;
  }
  return 0;
} catch (const std::exception& error) {
  std::cerr << error.what() << '\n';
  return 1;
}
