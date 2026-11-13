#include "settings_menu.h"
#include "game/game_settings.h"
#include "game/timing_hooks.h"
#include "input/prompt_artwork.h"
#include "settings/pc_settings.h"
#include "settings_resources.h"
#include "ui/exit_flow.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <string>
#include <windows.h>

#include <imgui.h>
#include <rex/cvar.h>
#include <rex/input/input_system.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/ui/image_decode.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/immediate_drawer.h>

REXCVAR_DEFINE_BOOL(lollipop_pc_settings, false, "Lollipop", "Open PC settings");

namespace lollipop {
namespace {
std::array<ImFont*, 4> menu_fonts{};
constexpr ImU32 ink = IM_COL32(1, 1, 1, 255);
constexpr ImU32 paper = IM_COL32(245, 231, 194, 255);

std::unique_ptr<rex::ui::ImmediateTexture> load_art(rex::ui::ImmediateDrawer& drawer, int id) {
  const auto module = GetModuleHandleW(nullptr);
  const auto resource = FindResourceW(module, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10));
  if (!resource)
    throw std::runtime_error("Missing embedded PC settings artwork");
  const auto bytes = static_cast<const uint8_t*>(LockResource(LoadResource(module, resource)));
  if (!bytes)
    throw std::runtime_error("Could not load PC settings artwork");
  int width = 0, height = 0;
  const auto rgba =
      rex::ui::DecodeImageRGBA(bytes, SizeofResource(module, resource), width, height);
  if (rgba.empty())
    throw std::runtime_error("Could not decode PC settings artwork");
  auto texture = drawer.CreateTexture(width, height, rex::ui::ImmediateTextureFilter::kLinear,
                                      false, rgba.data());
  if (!texture)
    throw std::runtime_error("Could not upload PC settings artwork");
  return texture;
}

class settings_menu final : public rex::ui::ImGuiDialog {
public:
  settings_menu(rex::ui::ImGuiDrawer& drawer, rex::ui::ImmediateDrawer& immediate,
                rex::Runtime& runtime, std::filesystem::path config,
                const std::filesystem::path& game_root, std::function<void()> quit)
      : ImGuiDialog(&drawer),
        input_(*static_cast<rex::input::InputSystem*>(runtime.input_system())),
        config_(std::move(config)), launched_(read_pc_settings()), exit_(input_, std::move(quit)),
        panel_(load_art(immediate, IDR_SETTINGS_PANEL)),
        controls_(load_art(immediate, IDR_SETTINGS_CONTROLS)) {
    std::ifstream file(game_root / "AshGame/CookedXbox360/Startup.xxx",
                       std::ios::binary | std::ios::ate);
    if (!file || file.tellg() != 294912)
      throw std::runtime_error("Could not read settings button artwork");
    std::vector<uint8_t> startup(294912);
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(startup.data()), startup.size()))
      throw std::runtime_error("Could not read settings button artwork");
    for (unsigned device = 0; device < 2; ++device) {
      const auto images = make_settings_prompt_artwork(startup, device == 1);
      for (size_t i = 0; i < images.size(); ++i) {
        unsigned left = 128, top = 128, right = 0, bottom = 0;
        for (unsigned y = 0; y < 128; ++y)
          for (unsigned x = 0; x < 128; ++x) {
            if (images[i][(y * 128 + x) * 4 + 3] <= 16)
              continue;
            left = std::min(left, x);
            top = std::min(top, y);
            right = std::max(right, x + 1);
            bottom = std::max(bottom, y + 1);
          }
        if (right <= left || bottom <= top)
          throw std::runtime_error("Empty settings button artwork");
        prompt_bounds_[device][i] =
            ImVec4(left / 128.0f, top / 128.0f, right / 128.0f, bottom / 128.0f);
        prompts_[device][i] = immediate.CreateTexture(
            128, 128, rex::ui::ImmediateTextureFilter::kLinear, false, images[i].data());
        if (!prompts_[device][i])
          throw std::runtime_error("Could not upload settings button artwork");
      }
    }
  }
  ~settings_menu() override {
    if ((visible_ || release_pending_) && !suspended_)
      input_.RemoveUIInputBlocker();
    set_settings_pause(false);
  }

private:
  void show(bool visible) {
    if (visible == visible_)
      return;
    if (visible) {
      draft_ = read_pc_settings();
      saved_ = draft_;
      game_ready_seen_ = game_settings_ready();
      status_.clear();
      discard_ = false;
      selected_ = 0;
      previous_buttons_ = 0xFFFF;
      if (!release_pending_)
        input_.AddUIInputBlocker();
      release_pending_ = false;
      set_settings_pause(true);
    } else {
      if (preview_)
        revert();
      // The keyboard driver is neutral while the UI captures it; wait for the
      // closing key's release so it cannot become a fresh guest button press.
      release_pending_ = ImGui::IsKeyDown(ImGuiKey_Enter) || ImGui::IsKeyDown(ImGuiKey_Escape) ||
                         ImGui::IsKeyDown(ImGuiKey_Backspace);
      if (!release_pending_)
        input_.RemoveUIInputBlocker();
      if (!release_pending_)
        set_settings_pause(false);
    }
    visible_ = visible;
    rex::cvar::SetFlagByName("lollipop_pc_settings", visible ? "true" : "false");
  }

  void revert() {
    for (const auto* name : {"window_width", "window_height", "fullscreen"})
      rex::cvar::SetFlagByName(name, saved_.at(name));
    preview_ = false;
    status_ = "Display restored.";
  }

  void commit() {
    if (!save_pc_settings(config_, draft_, status_)) {
      if (preview_)
        revert();
      status_ = "Save failed. Previous settings kept.";
      return;
    }
    for (const auto& [name, value] : draft_)
      if (rex::cvar::GetFlagByName(name) != value)
        rex::cvar::SetFlagByName(name, value);
    saved_ = draft_;
    save_game_settings();
    preview_ = false;
    status_ = restart_needed() ? "Saved. Restart to apply graphics changes." : "Saved.";
  }

  bool restart_needed() const {
    for (const auto& [name, value] : saved_) {
      const auto* flag = rex::cvar::GetFlagInfo(name);
      if (flag && flag->lifecycle == rex::cvar::Lifecycle::kRequiresRestart &&
          value != launched_.at(name))
        return true;
    }
    return false;
  }

  void apply() {
    if (!game_settings_ready()) {
      status_ = "Game settings are still loading.";
      return;
    }
    if (!validate_pc_settings(draft_)) {
      status_ = "Please choose valid settings before applying.";
      return;
    }
    bool display_changed = false;
    for (const auto* name : {"window_width", "window_height", "fullscreen"})
      display_changed |= draft_.at(name) != saved_.at(name);
    if (display_changed) {
      preview_ = true;
      deadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(15);
      for (const auto* name : {"window_width", "window_height", "fullscreen"})
        rex::cvar::SetFlagByName(name, draft_.at(name));
    } else {
      commit();
    }
  }

  void adjust(const pc_setting& setting, int direction) {
    status_.clear();
    auto& value = draft_.at(setting.name);
    if (setting.choices.empty()) {
      const auto number =
          std::clamp(std::stof(value) + direction * setting.step, setting.minimum, setting.maximum);
      value = setting.step >= 1 ? std::to_string(int(number)) : std::to_string(number);
    } else {
      auto it =
          std::ranges::find_if(setting.choices, [&](const auto& c) { return value == c.value; });
      int index = it == setting.choices.end() ? 0 : int(it - setting.choices.begin());
      const auto count = int(setting.choices.size());
      value = setting.choices[(index + direction + count) % count].value;
      if (std::string_view(setting.name) == "window_width")
        draft_["window_height"] = std::to_string(std::stoi(value) * 9 / 16);
    }
  }

  void text(ImDrawList& draw, ImVec2 pos, float size, ImU32 color, const char* value) {
    auto* font = ImGui::GetFont();
    for (auto* candidate : menu_fonts) {
      if (!candidate)
        continue;
      font = candidate;
      if (candidate->LegacySize >= size)
        break;
    }
    pos = ImVec2(std::round(pos.x), std::round(pos.y));
    draw.AddText(font, size, pos, color, value);
  }

  void strip(ImDrawList& draw, ImVec2 start, ImVec2 end, int band) {
    constexpr float bands[][2] = {{0.088f, 0.286f}, {0.402f, 0.600f}, {0.714f, 0.912f}};
    assert(band >= 0 && band < 3);
    assert(end.x - start.x >= end.y - start.y);
    const auto [top, bottom] = bands[band];
    const float cap = (end.y - start.y) / 2;
    const float u_cap = (bottom - top) * controls_->height / controls_->width / 2;
    const float x[] = {start.x, start.x + cap, end.x - cap, end.x};
    const float u[] = {0.025f, 0.025f + u_cap, 0.975f - u_cap, 0.975f};
    for (int i = 0; i < 3; ++i)
      draw.AddImage(reinterpret_cast<ImTextureID>(controls_.get()), ImVec2(x[i], start.y),
                    ImVec2(x[i + 1], end.y), ImVec2(u[i], top), ImVec2(u[i + 1], bottom));
  }

  void icon(ImDrawList& draw, int prompt, ImVec2 start, float size) {
    const auto device = input_.IsKeyboardActive() ? 1 : 0;
    const auto& texture = prompts_[device][prompt];
    const auto uv = prompt_bounds_[device][prompt];
    const float ratio = (uv.z - uv.x) / (uv.w - uv.y);
    const float width = size * std::min(ratio, 1.0f), height = size / std::max(ratio, 1.0f);
    start.x += (size - width) / 2;
    start.y += (size - height) / 2;
    draw.AddImage(reinterpret_cast<ImTextureID>(texture.get()), start,
                  ImVec2(start.x + width, start.y + height), ImVec2(uv.x, uv.y),
                  ImVec2(uv.z, uv.w));
  }

  void OnDraw(ImGuiIO& io) override {
    if (exit_.update(io) || suspended_)
      return;
    if (release_pending_ && !ImGui::IsKeyDown(ImGuiKey_Enter) &&
        !ImGui::IsKeyDown(ImGuiKey_Escape) && !ImGui::IsKeyDown(ImGuiKey_Backspace)) {
      input_.RemoveUIInputBlocker();
      release_pending_ = false;
      set_settings_pause(false);
    }
    const bool requested = rex::cvar::Query<bool>("lollipop_pc_settings");
    if (visible_ && !requested && draft_ != saved_ && !preview_) {
      discard_ = true;
      rex::cvar::SetFlagByName("lollipop_pc_settings", "true");
    } else {
      show(requested);
    }
    if (!visible_)
      return;
    if (!game_ready_seen_ && game_settings_ready()) {
      const auto current = read_pc_settings();
      for (auto& [name, value] : saved_) {
        if (draft_.at(name) == value)
          draft_[name] = current.at(name);
        value = current.at(name);
      }
      game_ready_seen_ = true;
    }
    ImGui::SetNextFrameWantCaptureKeyboard(true);
    ImGui::SetNextFrameWantCaptureMouse(true);

    rex::input::X_INPUT_STATE state{};
    input_.GetStateForUI(0, &state);
    uint16_t buttons = state.gamepad.buttons;
    if (state.gamepad.thumb_ly > 16000)
      buttons |= 1;
    if (state.gamepad.thumb_ly < -16000)
      buttons |= 2;
    if (state.gamepad.thumb_lx < -16000)
      buttons |= 4;
    if (state.gamepad.thumb_lx > 16000)
      buttons |= 8;
    uint16_t pressed = buttons & ~previous_buttons_;
    const auto now = std::chrono::steady_clock::now();
    if ((buttons & 15) != (previous_buttons_ & 15)) {
      repeat_at_ = now + std::chrono::milliseconds(350);
    } else if ((buttons & 15) && now >= repeat_at_) {
      pressed |= buttons & 15;
      repeat_at_ = now + std::chrono::milliseconds(100);
    }
    previous_buttons_ = buttons;
    auto key = [](ImGuiKey k) {
      return ImGui::IsKeyPressed(k, k != ImGuiKey_Enter && k != ImGuiKey_Escape &&
                                        k != ImGuiKey_Backspace);
    };
    const bool back = (pressed & 0x2010) || key(ImGuiKey_Escape) || key(ImGuiKey_Backspace);
    const bool accept = (pressed & 0x1000) || key(ImGuiKey_Enter);
    const bool modal_was_open = preview_ || discard_;
    const int horizontal = int(bool(pressed & 8) || key(ImGuiKey_RightArrow) || key(ImGuiKey_D)) -
                           int(bool(pressed & 4) || key(ImGuiKey_LeftArrow) || key(ImGuiKey_A));
    const int vertical = int(bool(pressed & 2) || key(ImGuiKey_DownArrow) || key(ImGuiKey_S)) -
                         int(bool(pressed & 1) || key(ImGuiKey_UpArrow) || key(ImGuiKey_W));
    if (!modal_was_open) {
      int turn = int(bool(pressed & 0x200)) - int(bool(pressed & 0x100));
      if (key(ImGuiKey_Tab))
        turn = io.KeyShift ? -1 : 1;
      if (turn) {
        page_ = (page_ + turn + 5) % 5;
        selected_ = 0;
        status_.clear();
      }
    }

    const float scale = std::min(io.DisplaySize.x / 1280.0f, io.DisplaySize.y / 720.0f);
    const ImVec2 origin((io.DisplaySize.x - 1160 * scale) / 2,
                        (io.DisplaySize.y - 650 * scale) / 2);
    auto point = [&](float x, float y) {
      return ImVec2(origin.x + x * scale, origin.y + y * scale);
    };
    ImGui::SetNextWindowPos(origin);
    ImGui::SetNextWindowSize(ImVec2(1160 * scale, 650 * scale));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::Begin("PC SETTINGS", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoScrollWithMouse);
    auto& draw = *ImGui::GetWindowDrawList();
    draw.AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(9, 7, 5, 190));
    auto button = [&](const char* id, const char* label, float x, float y, float w, float h,
                      bool active) {
      ImGui::SetCursorScreenPos(point(x, y));
      const bool clicked = ImGui::InvisibleButton(id, ImVec2(w * scale, h * scale));
      const bool hover = ImGui::IsItemHovered() && input_.IsKeyboardActive();
      strip(draw, point(x, y), point(x + w, y + h), active || hover ? 0 : 1);
      text(draw, point(x + 17, y + 5), 26 * scale, active || hover ? ink : paper, label);
      return clicked;
    };
    draw.AddImage(reinterpret_cast<ImTextureID>(panel_.get()), point(0, 0), point(1160, 650));
    bool save = false, reset = false, close = back, quit = false;
    // Modal controls overlap these rows; disabled items must not claim their mouse hits.
    ImGui::BeginDisabled(modal_was_open);
    const char* pages[] = {"DISPLAY", "GRAPHICS", "AUDIO", "CAMERA", "CONTROLS"};
    for (int p = 0; p < 5; ++p)
      if (button(pages[p], pages[p], 30.0f + p * 222, 139, 212, 37, page_ == p) &&
          !modal_was_open) {
        page_ = p;
        selected_ = 0;
        status_.clear();
      }

    std::vector<const pc_setting*> rows;
    for (const auto& setting : pc_settings())
      if (setting.page == page_)
        rows.push_back(&setting);
    const int count = int(rows.size());
    if (!modal_was_open) {
      selected_ = (selected_ + vertical + count + 4) % (count + 4);
      if (selected_ >= count && horizontal)
        selected_ = count + (selected_ - count + horizontal + 4) % 4;
      if (vertical)
        status_.clear();
    }
    const pc_setting* help = selected_ < count ? rows[selected_] : nullptr;
    for (int i = 0; i < count; ++i) {
      const auto& setting = *rows[i];
      const float y = 198.0f + i * 41;
      ImGui::SetCursorScreenPos(point(30, y));
      const bool clicked = ImGui::InvisibleButton(setting.name, ImVec2(1100 * scale, 37 * scale));
      const bool hover = ImGui::IsItemHovered() && input_.IsKeyboardActive();
      if (hover)
        help = &setting;
      const bool active = selected_ == i;
      if (active || hover)
        strip(draw, point(30, y), point(1130, y + 37), 0);
      const auto color = active || hover ? ink : paper;
      text(draw, point(48, y + 4), 28 * scale, color, setting.label);
      std::string value = draft_.at(setting.name);
      const auto found =
          std::ranges::find_if(setting.choices, [&](const auto& c) { return value == c.value; });
      if (found != setting.choices.end())
        value = found->label;
      if (setting.choices.empty()) {
        const float number = std::stof(value);
        char formatted[48];
        if (setting.step >= 1)
          std::snprintf(formatted, sizeof(formatted), "%.0f %%", number * 10);
        else if (setting.maximum <= 1)
          std::snprintf(formatted, sizeof(formatted), "%.0f %%", number * 100);
        else
          std::snprintf(formatted, sizeof(formatted), "%.1f", number);
        value = formatted;
      }
      text(draw, point(602, y + 4), 28 * scale, color, ("<  " + value + "  >").c_str());
      if (!modal_was_open) {
        if (clicked) {
          selected_ = i;
          adjust(setting, io.MousePos.x < point(840, 0).x ? -1 : 1);
        } else if (active && (horizontal || accept))
          adjust(setting, horizontal ? horizontal : 1);
      }
    }
    std::string caption = status_;
    if (caption.empty()) {
      if (restart_needed())
        caption = "Restart to apply graphics changes.";
      else if (help) {
        caption = help->help;
        const auto* flag = rex::cvar::GetFlagInfo(help->name);
        if (flag && flag->lifecycle == rex::cvar::Lifecycle::kRequiresRestart)
          caption += " Restart required.";
      } else {
        constexpr const char* actions[] = {"Save changes.", "Restore this page's defaults.",
                                           "Return to the game.", "Close the game."};
        caption = actions[selected_ - count];
      }
    }
    text(draw, point(43, 482), 24 * scale, ink, caption.c_str());
    if (draft_ != saved_)
      text(draw, point(1010, 177), 20 * scale, paper, "UNSAVED");
    save = button("apply", "APPLY", 30, 540, 260, 40, selected_ == count);
    reset = button("defaults", "DEFAULTS", 310, 540, 260, 40, selected_ == count + 1);
    close |= button("back", "BACK", 590, 540, 260, 40, selected_ == count + 2);
    quit = button("quit", "QUIT GAME", 870, 540, 260, 40, selected_ == count + 3);
    ImGui::EndDisabled();
    icon(draw, 4, point(32, 601), 36 * scale);
    text(draw, point(82, 605), 26 * scale, ink, "Move");
    icon(draw, 2, point(312, 601), 36 * scale);
    if (!input_.IsKeyboardActive())
      icon(draw, 3, point(354, 601), 36 * scale);
    text(draw, point(input_.IsKeyboardActive() ? 362 : 404, 605), 26 * scale, ink, "Pages");
    icon(draw, 0, point(592, 601), 36 * scale);
    text(draw, point(642, 605), 26 * scale, ink, "Select");
    icon(draw, 1, point(872, 601), 36 * scale);
    text(draw, point(922, 605), 26 * scale, ink, "Back");
    if (!modal_was_open) {
      if (accept && selected_ == count)
        save = true;
      if (accept && selected_ == count + 1)
        reset = true;
      if (accept && selected_ == count + 2)
        close = true;
      if (accept && selected_ == count + 3)
        quit = true;
      if (reset) {
        status_.clear();
        for (const auto& setting : pc_settings())
          if (setting.page == page_)
            draft_[setting.name] = setting.recommended;
        if (page_ == 0)
          draft_["window_height"] = "1440";
      }
      if (save)
        apply();
      if (close) {
        if (draft_ != saved_)
          discard_ = true;
        else
          show(false);
      }
      if (quit && exit_.request([this](bool failed) {
            suspended_ = false;
            previous_buttons_ = 0xFFFF;
            input_.AddUIInputBlocker();
            if (failed)
              status_ = "Could not open the native quit confirmation.";
          })) {
        input_.RemoveUIInputBlocker();
        suspended_ = true;
      }
    }
    if (preview_ || discard_) {
      if (!modal_was_open)
        modal_no_ = false;
      else if (horizontal || vertical)
        modal_no_ = (horizontal ? horizontal : vertical) > 0;
      draw.AddRectFilled(point(10, 131), point(1150, 590), IM_COL32(12, 9, 7, 235));
      strip(draw, point(94, 204), point(1066, 325), 2);
      const int remaining = int(std::ceil(
          std::chrono::duration<float>(deadline_ - std::chrono::steady_clock::now()).count()));
      text(draw, point(120, preview_ ? 226 : 247), 30 * scale, ink,
           preview_ ? "KEEP DISPLAY SETTINGS?" : "DISCARD CHANGES?");
      if (preview_) {
        const auto message = "Revert in " + std::to_string(std::max(0, remaining)) + "s.";
        text(draw, point(124, 271), 26 * scale, ink, message.c_str());
      }
      const bool yes =
          button("yes", preview_ ? "KEEP" : "DISCARD", 120, 340, 430, 48, !modal_no_) ||
          (accept && modal_was_open && !modal_no_);
      const bool no = button("no", preview_ ? "REVERT" : "CANCEL", 575, 340, 460, 48, modal_no_) ||
                      (modal_was_open && (back || (accept && modal_no_)));
      icon(draw, 0, point(modal_no_ ? 983 : 498, 346), 36 * scale);
      if (!modal_no_)
        icon(draw, 1, point(983, 346), 36 * scale);
      if (preview_) {
        if (no || remaining <= 0)
          revert();
        else if (yes)
          commit();
      } else {
        if (yes) {
          discard_ = false;
          show(false);
        } else if (no)
          discard_ = false;
      }
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
  }

  rex::input::InputSystem& input_;
  std::filesystem::path config_;
  pc_values launched_, draft_, saved_;
  exit_flow exit_;
  bool visible_ = false, preview_ = false, discard_ = false, release_pending_ = false;
  bool modal_no_ = false;
  bool suspended_ = false;
  bool game_ready_seen_ = false;
  int page_ = 0, selected_ = 0;
  uint16_t previous_buttons_ = 0;
  std::chrono::steady_clock::time_point deadline_{}, repeat_at_{};
  std::string status_;
  std::unique_ptr<rex::ui::ImmediateTexture> panel_, controls_;
  std::array<std::array<std::unique_ptr<rex::ui::ImmediateTexture>, 5>, 2> prompts_;
  std::array<std::array<ImVec4, 5>, 2> prompt_bounds_;
};
} // namespace

void configure_settings_fonts(ImFontAtlas* atlas) {
  std::array<wchar_t, MAX_PATH> windows{};
  const auto length = GetWindowsDirectoryW(windows.data(), static_cast<UINT>(windows.size()));
  if (!length || length >= windows.size())
    return;
  const auto menu = std::filesystem::path(windows.data()) / "Fonts" / "arialbd.ttf";
  constexpr float sizes[] = {32, 48, 64, 96};
  for (size_t i = 0; i < menu_fonts.size(); ++i) {
    if (std::filesystem::exists(menu))
      menu_fonts[i] = atlas->AddFontFromFileTTF(menu.string().c_str(), sizes[i]);
  }
}

std::unique_ptr<rex::ui::ImGuiDialog>
make_settings_menu(rex::ui::ImGuiDrawer& drawer, rex::ui::ImmediateDrawer& immediate,
                   rex::Runtime& runtime, const std::filesystem::path& config,
                   const std::filesystem::path& game_root, std::function<void()> quit) {
  return std::make_unique<settings_menu>(drawer, immediate, runtime, config, game_root,
                                         std::move(quit));
}

} // namespace lollipop
