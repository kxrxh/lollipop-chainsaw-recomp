#include "shader_loading.h"

#include <algorithm>
#include <cmath>

#include <imgui.h>
#include <rex/ui/imgui_dialog.h>

namespace lollipop {
namespace {
class shader_loading final : public rex::ui::ImGuiDialog {
public:
  shader_loading(rex::ui::ImGuiDrawer& drawer, std::shared_ptr<shader_loading_state> state,
                 std::function<void()> close)
      : ImGuiDialog(&drawer), state_(std::move(state)), close_(std::move(close)) {}

private:
  void OnDraw(ImGuiIO& io) override {
    std::string stage, error;
    uint32_t completed, total;
    {
      std::lock_guard lock(state_->mutex);
      stage = state_->stage;
      error = state_->error;
      completed = state_->completed;
      total = state_->total;
    }
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.13f, 0.09f, 0.13f, 1));
    ImGui::Begin("Preparing game shaders", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings);
    const float scale =
        std::clamp(std::min(io.DisplaySize.x / 1280, io.DisplaySize.y / 720), 0.65f, 2.0f);
    const float width = std::min(io.DisplaySize.x * 0.82f, 720 * scale);
    auto* font = ImGui::GetFont();
    for (auto* candidate : io.Fonts->Fonts) {
      if (candidate->LegacySize >= 32) {
        font = candidate;
        break;
      }
    }
    ImGui::PushFont(font, 18 * scale);
    ImGui::SetCursorPos(ImVec2((io.DisplaySize.x - width) * 0.5f, io.DisplaySize.y * 0.36f));
    ImGui::BeginGroup();
    ImGui::TextColored(ImVec4(1, 0.47f, 0.69f, 1), "LOLLIPOP CHAINSAW RECOMP");
    ImGui::Spacing();
    ImGui::PushFont(font, 32 * scale);
    ImGui::TextUnformatted(error.empty() ? "Preparing shaders" : "Shader preparation failed");
    ImGui::PopFont();
    ImGui::Spacing();
    if (error.empty()) {
      ImGui::TextUnformatted(stage.c_str());
      const auto cursor = ImGui::GetCursorScreenPos();
      const ImVec2 center(cursor.x + 12 * scale, cursor.y + 14 * scale);
      auto* draw = ImGui::GetWindowDrawList();
      const float rotation = float(std::fmod(ImGui::GetTime(), 1.0)) * 6.2831853f;
      draw->PathArcTo(center, 9 * scale, rotation, rotation + 4.712389f, 24);
      draw->PathStroke(ImGui::GetColorU32(ImVec4(1, 0.47f, 0.69f, 1)), 0, 3 * scale);
      ImGui::Dummy(ImVec2(30 * scale, 28 * scale));
      ImGui::SameLine();
      ImGui::TextUnformatted("Preparing for smoother gameplay...");
      ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.82f, 0.13f, 0.4f, 1));
      const std::string count =
          total ? std::to_string(completed) + " / " + std::to_string(total) : "Checking";
      ImGui::ProgressBar(total ? float(completed) / total : 0, ImVec2(width, 26 * scale),
                         count.c_str());
      ImGui::PopStyleColor();
      ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
      ImGui::TextWrapped("Shaders are cached for future launches. First-time preparation can take "
                         "several minutes. Please wait until preparation finishes.");
      ImGui::PopTextWrapPos();
    } else {
      ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
      ImGui::TextUnformatted(error.c_str());
      ImGui::PopTextWrapPos();
    }
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.65f, 0.1f, 0.3f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.17f, 0.4f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.6f, 0.07f, 0.27f, 1));
    if (ImGui::Button(error.empty() ? "Cancel" : "Close", ImVec2(120 * scale, 36 * scale)) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape))
      close_();
    ImGui::PopStyleColor(3);
    ImGui::PopFont();
    ImGui::EndGroup();
    ImGui::End();
    ImGui::PopStyleColor();
  }
  std::shared_ptr<shader_loading_state> state_;
  std::function<void()> close_;
};
} // namespace

std::unique_ptr<rex::ui::ImGuiDialog>
make_shader_loading(rex::ui::ImGuiDrawer& drawer, std::shared_ptr<shader_loading_state> state,
                    std::function<void()> close) {
  return std::make_unique<shader_loading>(drawer, std::move(state), std::move(close));
}
} // namespace lollipop
