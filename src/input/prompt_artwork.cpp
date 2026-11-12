#include "prompt_artwork.h"
#include "game/package_lzx.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <windows.h>

#include <bcrypt.h>
#include <objidl.h>

#include <gdiplus.h>

#include <rex/cvar.h>
#include <rex/filesystem/devices/host_path_device.h>
#include <rex/hash.h>
#include <rex/input/input_system.h>
#include <rex/logging.h>
#include <rex/perf/counter.h>
#include <rex/runtime.h>
#include <rex/string/utf8.h>
#include <rex/system/gpu_plugin.h>

namespace lollipop {
namespace {


uint32_t read32(std::span<const uint8_t> bytes, size_t offset) {
  if (offset > bytes.size() || bytes.size() - offset < 4)
    throw std::runtime_error("Truncated startup package");
  return (uint32_t(bytes[offset]) << 24) | (uint32_t(bytes[offset + 1]) << 16) |
         (uint32_t(bytes[offset + 2]) << 8) | bytes[offset + 3];
}

void write32(std::vector<uint8_t> &bytes, size_t offset, uint32_t value) {
  for (int i = 3; i >= 0; --i) {
    bytes.at(offset + i) = static_cast<uint8_t>(value);
    value >>= 8;
  }
}

// These are the UI_Button360 exports in the verified disc's Startup.xxx.
struct prompt {
  size_t offset;
  unsigned size;
  const char *flag;
  const char *label;
  bool depressed = false;
};
constexpr size_t prompt_sample_bytes = 64;

uint64_t prompt_hash(std::span<const uint8_t> texture) {
  const auto size = std::min(texture.size(), prompt_sample_bytes);
  const auto sample = texture.subspan((texture.size() - size) / 2, size);
  return XXH3_64bits(sample.data(), sample.size());
}
constexpr prompt prompts[] = {
    {0x13b3b3, 16737, "keybind_a", nullptr},
    {0x13f514, 16737, "keybind_a", nullptr, true},
    {0x143675, 16737, "keybind_b", nullptr},
    {0x1477d6, 16737, "keybind_b", nullptr, true},
    {0x14b937, 16737, "keybind_lstick_press", nullptr},
    {0x14fa98, 16737, "keybind_lstick_down", nullptr},
    {0x153bf9, 16737, "keybind_lstick_right", nullptr},
    {0x157d5a, 16737, "keybind_lstick_up", nullptr},
    {0x15bebb, 16737, "keybind_lstick_left", nullptr},
    {0x16001c, 16737, nullptr, "WASD"},
    {0x16417d, 16737, nullptr, "WASD"},
    {0x1682de, 65889, "keybind_right_trigger", nullptr},
    {0x17843f, 65889, "keybind_right_trigger", nullptr, true},
    {0x1885a0, 16737, "keybind_x", nullptr},
    {0x18c701, 16737, "keybind_x", nullptr, true},
    {0x190862, 16737, "keybind_y", nullptr},
    {0x1949c3, 16737, "keybind_y", nullptr, true},
    {0x198b24, 16737, "keybind_a", nullptr},
    {0x19cc85, 16737, "keybind_b", nullptr},
    {0x1a0de6, 16737, "keybind_back", nullptr},
    {0x1a4f47, 16737, "keybind_dpad_up", nullptr},
    {0x1a90a8, 16737, nullptr, "WASD"},
    {0x1ad209, 16737, "keybind_left_shoulder", nullptr},
    {0x1b136a, 16737, "keybind_left_trigger", nullptr},
    {0x1b54cb, 16737, "keybind_rstick_press", nullptr},
    {0x1b962c, 16737, "keybind_right_shoulder", nullptr},
    {0x1bd78d, 16737, "keybind_right_trigger", nullptr},
    {0x1c18ee, 16737, "keybind_start", nullptr},
    {0x1c5a4f, 16737, "keybind_x", nullptr},
    {0x1c9bb0, 16737, "keybind_y", nullptr},
};

std::wstring key_label(const prompt &p) {
  auto key = p.label ? std::string(p.label) : rex::cvar::GetFlagByName(p.flag);
  key = key.substr(0, key.find(','));
  if (key == "Space") key = "SPACE";
  if (key == "Escape") key = "ESC";
  if (key == "Return") key = "ENT";
  if (key == "Backspace") key = "BACK";
  if (key == "Shift") key = "SHIFT";
  if (key == "Tab") key = "TAB";
  if (key.empty()) key = "--";
  return std::wstring(key.begin(), key.end());
}

void comic_shading(Gdiplus::Graphics &g, Gdiplus::GraphicsPath &shape) {
  Gdiplus::RectF bounds;
  shape.GetBounds(&bounds);
  const auto state = g.Save();
  g.SetClip(&shape);
  Gdiplus::SolidBrush dots(Gdiplus::Color(45, 40, 22, 35));
  for (float y = bounds.Y + bounds.Height * .55f; y < bounds.GetBottom();
       y += 5)
    for (float x = bounds.X + 3; x < bounds.GetRight(); x += 5)
      g.FillEllipse(&dots, x + (int(y) % 2 ? 2.5f : 0), y, 1.3f, 1.3f);
  Gdiplus::Pen hatch(Gdiplus::Color(90, 35, 18, 29), 1.0f);
  for (float x = bounds.X + 5; x < bounds.GetRight(); x += 9)
    g.DrawLine(&hatch, x, bounds.GetBottom() - 1, x + 4,
               bounds.GetBottom() - 5);
  g.Restore(state);
}

void draw_key(Gdiplus::Graphics &g, const std::wstring &label, float x, float y,
              float width, float height) {
  Gdiplus::SolidBrush side(Gdiplus::Color(255, 91, 75, 82));
  Gdiplus::SolidBrush face(Gdiplus::Color(255, 250, 239, 211));
  Gdiplus::SolidBrush ink(Gdiplus::Color(255, 30, 16, 30));
  Gdiplus::Pen outline(Gdiplus::Color(255, 20, 13, 20), 3.8f);
  const Gdiplus::PointF edge[] = {{x + 5, y + 1},
                                  {x + width - 5, y - 1},
                                  {x + width, y + 5},
                                  {x + width - 1, y + height - 5},
                                  {x + width - 5, y + height},
                                  {x + 4, y + height + 1},
                                  {x - 1, y + height - 5},
                                  {x, y + 7}};
  Gdiplus::GraphicsPath top;
  top.AddPolygon(edge, int(std::size(edge)));
  g.TranslateTransform(2, 6);
  g.FillPath(&side, &top);
  g.DrawPath(&outline, &top);
  g.TranslateTransform(-2, -6);
  g.FillPath(&face, &top);
  comic_shading(g, top);
  Gdiplus::Pen highlight(Gdiplus::Color(255, 255, 254, 244), 2.5f);
  g.DrawLine(&highlight, x + 6, y + 6, x + width - 8, y + 4);
  g.DrawLine(&highlight, x + 4, y + 9, x + 4, y + height - 8);
  g.DrawPath(&outline, &top);
  Gdiplus::Pen scuff(Gdiplus::Color(255, 32, 21, 28), 1.1f);
  g.DrawLine(&scuff, x + width - 3, y + height - 14, x + width - 5,
             y + height - 5);
  g.DrawLine(&scuff, x + 4, y + height - 3, x + 12, y + height - 4);
  Gdiplus::FontFamily family(label.size() > 2 ? L"Impact" : L"Comic Sans MS");
  const auto style =
      label.size() > 2 ? Gdiplus::FontStyleRegular : Gdiplus::FontStyleBold;
  Gdiplus::Font measure_font(&family, 48.0f, style, Gdiplus::UnitPixel);
  Gdiplus::RectF bounds;
  g.MeasureString(label.c_str(), -1, &measure_font, Gdiplus::PointF(0, 0),
                  &bounds);
  const float font_size = 48.0f * std::min({1.0f, (width - 6) / bounds.Width,
                                            (height - 8) / bounds.Height});
  Gdiplus::Font font(&family, font_size, style, Gdiplus::UnitPixel);
  Gdiplus::StringFormat format;
  format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
  format.SetAlignment(Gdiplus::StringAlignmentCenter);
  format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
  g.DrawString(label.c_str(), -1, &font,
               Gdiplus::RectF(x + 1, y - 2, width - 2, height), &format, &ink);
}

void draw_mouse(Gdiplus::Graphics &g, const std::wstring &button) {
  Gdiplus::SolidBrush face(Gdiplus::Color(255, 238, 232, 214));
  const auto action_color = button == L"LMB" ? Gdiplus::Color(255, 245, 205, 53)
                            : button == L"RMB"
                                ? Gdiplus::Color(255, 69, 137, 204)
                                : Gdiplus::Color(255, 125, 190, 48);
  Gdiplus::SolidBrush active(action_color);
  Gdiplus::SolidBrush shadow(Gdiplus::Color(255, 78, 67, 73));
  Gdiplus::Pen outline(Gdiplus::Color(255, 24, 10, 24), 5.0f);
  Gdiplus::GraphicsPath body;
  body.AddBezier(64, 9, 40, 9, 27, 25, 27, 52);
  body.AddLine(27, 52, 27, 83);
  body.AddBezier(27, 83, 27, 127, 101, 127, 101, 83);
  body.AddLine(101, 83, 101, 52);
  body.AddBezier(101, 52, 101, 25, 88, 9, 64, 9);
  body.CloseFigure();
  g.TranslateTransform(3, 5);
  g.FillPath(&shadow, &body);
  g.DrawPath(&outline, &body);
  g.TranslateTransform(-3, -5);
  g.FillPath(&face, &body);
  comic_shading(g, body);
  Gdiplus::GraphicsPath left;
  left.AddBezier(64, 9, 40, 9, 27, 25, 27, 52);
  left.AddLine(27, 52, 64, 52);
  left.AddLine(64, 52, 64, 9);
  left.CloseFigure();
  Gdiplus::GraphicsPath right;
  right.AddBezier(64, 9, 88, 9, 101, 25, 101, 52);
  right.AddLine(101, 52, 64, 52);
  right.AddLine(64, 52, 64, 9);
  right.CloseFigure();
  if (button == L"LMB") {
    g.FillPath(&active, &left);
    comic_shading(g, left);
  }
  if (button == L"RMB") {
    g.FillPath(&active, &right);
    comic_shading(g, right);
  }
  g.DrawLine(&outline, 28, 53, 100, 53);
  g.DrawLine(&outline, 64, 11, 64, 52);
  Gdiplus::SolidBrush rubber(Gdiplus::Color(255, 30, 16, 30));
  Gdiplus::GraphicsPath wheel;
  wheel.AddArc(57, 19, 14, 10, 180, 180);
  wheel.AddArc(57, 36, 14, 10, 0, 180);
  wheel.CloseFigure();
  g.FillPath(button == L"MMB" ? &active : &rubber, &wheel);
  Gdiplus::Pen wheel_outline(Gdiplus::Color(255, 24, 10, 24), 3.0f);
  g.DrawPath(&wheel_outline, &wheel);
  Gdiplus::Pen ridges(button == L"MMB" ? Gdiplus::Color(255, 30, 16, 30)
                                       : Gdiplus::Color(255, 255, 237, 198),
                      1.8f);
  for (int y : {26, 32, 38}) g.DrawLine(&ridges, 60, y, 68, y);
  Gdiplus::Pen shine(Gdiplus::Color(220, 255, 255, 242), 2.5f);
  g.DrawBezier(&shine, 34, 47, 34, 28, 43, 19, 54, 16);
  g.DrawBezier(&shine, 34, 67, 33, 86, 37, 96, 44, 100);
  g.DrawPath(&outline, &body);
}

void draw_movement(Gdiplus::Graphics &g, bool rotation, bool clockwise) {
  const float scale = rotation ? 0.78f : 1.0f;
  if (rotation) {
    g.TranslateTransform(13, 15);
    g.ScaleTransform(scale, scale);
  }
  draw_key(g, L"W", 46, 18, 32, 34);
  draw_key(g, L"A", 7, 60, 32, 34);
  draw_key(g, L"S", 46, 60, 32, 34);
  draw_key(g, L"D", 85, 60, 32, 34);
  if (rotation) {
    g.ScaleTransform(1 / scale, 1 / scale);
    g.TranslateTransform(-13, -15);
    Gdiplus::Pen arrow(Gdiplus::Color(255, 239, 44, 114), 6.0f);
    arrow.SetEndCap(Gdiplus::LineCapArrowAnchor);
    g.DrawArc(&arrow, 6, 3, 114, 117, clockwise ? 195.0f : -15.0f,
              clockwise ? 145.0f : -145.0f);
    g.DrawArc(&arrow, 6, 3, 114, 117, clockwise ? 15.0f : 165.0f,
              clockwise ? 145.0f : -145.0f);
  }
}

struct gdiplus_session {
  ULONG_PTR token = 0;
  gdiplus_session() {
    Gdiplus::GdiplusStartupInput input;
    if (Gdiplus::GdiplusStartup(&token, &input, nullptr) != Gdiplus::Ok)
      throw std::runtime_error(
          "Could not initialize keyboard artwork renderer");
  }
  ~gdiplus_session() { Gdiplus::GdiplusShutdown(token); }
};

size_t tiled_offset(unsigned x, unsigned y, unsigned pitch) {
  const unsigned macro = ((x >> 5) + (y >> 5) * ((pitch + 31) >> 5)) << 11;
  const unsigned micro = ((x & 7) + ((y & 14) << 2)) << 4;
  const unsigned offset =
      macro + ((micro & ~15u) << 1) + (micro & 15) + ((y & 1) << 4);
  return ((offset & ~511u) << 3) + ((y & 16) << 7) + ((offset & 448) << 2) +
         (((((y & 8) >> 2) + (x >> 3)) & 3) << 6) + (offset & 63);
}

std::array<uint8_t, 16> encode_dxt5(
    const std::array<Gdiplus::Color, 16> &pixels) {
  std::array<uint8_t, 16> block{};
  block[0] = 255;
  constexpr int alpha[] = {255, 0, 218, 182, 145, 109, 72, 36};
  uint64_t alpha_bits = 0;
  std::array<int, 3> lo{255, 255, 255}, hi{};
  for (unsigned i = 0; i < 16; ++i) {
    unsigned best = 0;
    for (unsigned a = 1; a < 8; ++a)
      if (std::abs(int(pixels[i].GetA()) - alpha[a]) <
          std::abs(int(pixels[i].GetA()) - alpha[best]))
        best = a;
    alpha_bits |= uint64_t(best) << (i * 3);
    if (!pixels[i].GetA()) continue;
    const int rgb[] = {pixels[i].GetR(), pixels[i].GetG(), pixels[i].GetB()};
    for (unsigned c = 0; c < 3; ++c) {
      lo[c] = std::min(lo[c], rgb[c]);
      hi[c] = std::max(hi[c], rgb[c]);
    }
  }
  for (unsigned i = 0; i < 6; ++i)
    block[2 + i] = static_cast<uint8_t>(alpha_bits >> (8 * i));
  auto rgb565 = [](const std::array<int, 3> &rgb) {
    return uint16_t(((rgb[0] >> 3) << 11) | ((rgb[1] >> 2) << 5) |
                    (rgb[2] >> 3));
  };
  const uint16_t endpoints[] = {rgb565(hi), rgb565(lo)};
  std::array<std::array<int, 3>, 4> palette{};
  for (unsigned e = 0; e < 2; ++e) {
    const auto v = endpoints[e];
    block[8 + e * 2] = static_cast<uint8_t>(v);
    block[9 + e * 2] = static_cast<uint8_t>(v >> 8);
    palette[e] = {int((v >> 11) * 255 / 31), int(((v >> 5) & 63) * 255 / 63),
                  int((v & 31) * 255 / 31)};
  }
  for (unsigned c = 0; c < 3; ++c) {
    palette[2][c] = (2 * palette[0][c] + palette[1][c]) / 3;
    palette[3][c] = (palette[0][c] + 2 * palette[1][c]) / 3;
  }
  uint32_t color_bits = 0;
  for (unsigned i = 0; i < 16; ++i) {
    const int rgb[] = {pixels[i].GetR(), pixels[i].GetG(), pixels[i].GetB()};
    unsigned best = 0;
    int distance = 3 * 256 * 256;
    for (unsigned p = 0; p < 4; ++p) {
      int error = 0;
      for (unsigned c = 0; c < 3; ++c)
        error += (rgb[c] - palette[p][c]) * (rgb[c] - palette[p][c]);
      if (error < distance) {
        best = p;
        distance = error;
      }
    }
    color_bits |= best << (i * 2);
  }
  for (unsigned i = 0; i < 4; ++i)
    block[12 + i] = static_cast<uint8_t>(color_bits >> (8 * i));
  // Xbox DXT payloads swap the bytes of each 16-bit word.
  for (unsigned i = 0; i < 16; i += 2) std::swap(block[i], block[i + 1]);
  return block;
}

void paint_prompt(std::vector<uint8_t> &package, const prompt &p) {
  const unsigned size = std::max(128u, read32(package, p.offset + 28));
  const auto label = key_label(p);
  Gdiplus::Bitmap image(size, size, PixelFormat32bppARGB);
  Gdiplus::Graphics graphics(&image);
  graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
  graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
  graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
  graphics.ScaleTransform(size / 128.0f, size / 128.0f);
  if (p.depressed) graphics.TranslateTransform(0, 6);
  if (label == L"LMB" || label == L"RMB" || label == L"MMB") {
    draw_mouse(graphics, label);
  } else if (label == L"WASD") {
    const bool rotation = p.offset == 0x16001c || p.offset == 0x16417d;
    draw_movement(graphics, rotation, p.offset == 0x16001c);
  } else {
    const bool wide = label.size() > 3;
    draw_key(graphics, label, wide ? 6.0f : 25.0f, wide ? 29.0f : 24.0f,
             wide ? 114.0f : 76.0f, wide ? 64.0f : 76.0f);
  }
  graphics.Flush();
  const size_t payload = p.offset + 325;
  const size_t payload_size = p.size - 353;
  std::fill_n(package.begin() + payload, payload_size, uint8_t(0));
  for (unsigned y = 0; y < size; y += 4) {
    for (unsigned x = 0; x < size; x += 4) {
      std::array<Gdiplus::Color, 16> pixels;
      for (unsigned i = 0; i < 16; ++i)
        if (image.GetPixel(x + i % 4, y + i / 4, &pixels[i]) != Gdiplus::Ok)
          throw std::runtime_error("Could not render keyboard artwork");
      const auto block = encode_dxt5(pixels);
      const auto offset = payload + tiled_offset(x / 4, y / 4, size / 4);
      if (offset + block.size() > payload + payload_size)
        throw std::runtime_error("Keyboard artwork exceeds texture allocation");
      std::copy(block.begin(), block.end(), package.begin() + offset);
    }
  }
  // The original 32px icons already reserve enough tiled DXT5 space for 128px.
  write32(package, p.offset + 28, size);
  write32(package, p.offset + 56, size);
  write32(package, p.offset + p.size - 28, size);
  write32(package, p.offset + p.size - 24, size);
  // Invisible RGB in a transparent corner keeps shared key artwork
  // identifiable.
  write32(package, payload + 8, static_cast<uint32_t>(p.offset));
}

void upscale_controller_prompt(std::vector<uint8_t> &package, const prompt &p) {
  if (read32(package, p.offset + 28) != 32) return;
  const size_t payload = p.offset + 325;
  const std::vector<uint8_t> original(package.begin() + payload,
                                      package.begin() + payload + 16384);
  for (unsigned y = 0; y < 32; ++y) {
    for (unsigned x = 0; x < 32; ++x) {
      std::array<uint8_t, 16> block;
      std::copy_n(original.begin() + tiled_offset(x / 4, y / 4, 8), 16,
                  block.begin());
      for (unsigned i = 0; i < 16; i += 2) std::swap(block[i], block[i + 1]);
      uint64_t alpha = 0;
      uint32_t color = 0;
      for (unsigned i = 0; i < 6; ++i)
        alpha |= uint64_t(block[2 + i]) << (8 * i);
      for (unsigned i = 0; i < 4; ++i)
        color |= uint32_t(block[12 + i]) << (8 * i);
      const unsigned pixel = (y % 4) * 4 + x % 4;
      const uint64_t a = (alpha >> (pixel * 3)) & 7;
      const uint32_t c = (color >> (pixel * 2)) & 3;
      alpha = 0;
      color = 0;
      for (unsigned i = 0; i < 16; ++i) {
        alpha |= a << (i * 3);
        color |= c << (i * 2);
      }
      for (unsigned i = 0; i < 6; ++i) block[2 + i] = uint8_t(alpha >> (8 * i));
      for (unsigned i = 0; i < 4; ++i)
        block[12 + i] = uint8_t(color >> (8 * i));
      for (unsigned i = 0; i < 16; i += 2) std::swap(block[i], block[i + 1]);
      std::copy(block.begin(), block.end(),
                package.begin() + payload + tiled_offset(x, y, 32));
    }
  }
  write32(package, p.offset + 28, 128);
  write32(package, p.offset + 56, 128);
  write32(package, p.offset + p.size - 28, 128);
  write32(package, p.offset + p.size - 24, 128);
}

class keyboard_game_device final : public rex::filesystem::HostPathDevice {
 public:
  keyboard_game_device(const std::filesystem::path &game,
                       const std::filesystem::path &cache)
      : HostPathDevice("\\Device\\LollipopGame", game, true),
        cooked_(rex::path_to_utf8(
            (game / "AshGame/CookedXbox360").lexically_normal())),
        cache_(cache) {}
  std::filesystem::path ResolveHostPath(
      const std::filesystem::path &path) const override {
    if (rex::string::utf8_equal_case(
            rex::path_to_utf8(path.parent_path().lexically_normal()),
            cooked_) ||
        path.filename().string().starts_with("Xbox360TOC")) {
      const auto cached = cache_ / path.filename();
      if (std::filesystem::is_regular_file(cached)) return cached;
    }
    return path;
  }

 private:
  std::string cooked_;
  std::filesystem::path cache_;
};

}  // namespace

std::vector<uint8_t> pack_keyboard_prompts(
    std::span<const uint8_t> package, const std::filesystem::path &directory) {
  constexpr size_t chunk_size = 0x20000;
  const size_t chunks = (package.size() + chunk_size - 1) / chunk_size;
  std::vector<uint8_t> result(16 + chunks * 8);
  write32(result, 0, 0x9e2a83c1);
  write32(result, 4, chunk_size);
  write32(result, 12, static_cast<uint32_t>(package.size()));
  for (size_t chunk = 0; chunk < chunks; ++chunk) {
    const auto input = package.subspan(
        chunk * chunk_size,
        std::min(chunk_size, package.size() - chunk * chunk_size));
    const auto temp =
        directory / ("lzx-" + std::to_string(GetCurrentProcessId()));
    const auto input_path = temp.wstring() + L".bin",
               cab_path = temp.wstring() + L".cab";
    std::ofstream file(std::filesystem::path(input_path),
                       std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char *>(input.data()), input.size());
    file.close();
    if (!file) throw std::runtime_error("Could not write LZX input");
    std::array<wchar_t, MAX_PATH> system{};
    const auto length = GetSystemDirectoryW(system.data(), system.size());
    if (!length || length >= system.size())
      throw std::runtime_error("Could not locate Windows compressor");
    const auto executable =
        std::filesystem::path(std::wstring(system.data(), length)) /
        "makecab.exe";
    auto command = L"\"" + executable.wstring() +
                   L"\" /D CompressionType=LZX /D CompressionMemory=17 /V0 \"" +
                   input_path + L"\" \"" + cab_path + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr,
                        false, CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                        &process))
      throw std::runtime_error("Could not start Windows LZX compressor");
    const auto waited = WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exit_code = 1;
    GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (waited != WAIT_OBJECT_0 || exit_code)
      throw std::runtime_error("Windows LZX compression failed");
    std::ifstream cabinet(std::filesystem::path(cab_path),
                          std::ios::binary | std::ios::ate);
    if (!cabinet || cabinet.tellg() < 44 || cabinet.tellg() > 1024 * 1024)
      throw std::runtime_error("Invalid LZX cabinet");
    std::vector<uint8_t> cab(static_cast<size_t>(cabinet.tellg()));
    cabinet.seekg(0);
    cabinet.read(reinterpret_cast<char *>(cab.data()), cab.size());
    if (!cabinet) throw std::runtime_error("Could not read LZX cabinet");
    cabinet.close();
    auto le16 = [&](size_t p) -> uint32_t {
      if (p > cab.size() || cab.size() - p < 2)
        throw std::runtime_error("Truncated LZX cabinet");
      return cab[p] | (uint32_t(cab[p + 1]) << 8);
    };
    if (le16(0) != 0x534d || le16(2) != 0x4643 || le16(26) != 1 ||
        le16(30) != 0 || le16(42) != 0x1103)
      throw std::runtime_error("Unsupported LZX cabinet layout");
    size_t cursor = le16(36) | (le16(38) << 16);
    const auto frames = le16(40);
    std::vector<uint8_t> packed;
    for (unsigned frame = 0; frame < frames; ++frame) {
      const auto compressed = le16(cursor + 4), size = le16(cursor + 6);
      cursor += 8;
      if (!compressed || size > 32768 || cursor > cab.size() ||
          compressed > cab.size() - cursor)
        throw std::runtime_error("Invalid LZX cabinet frame");
      if (frame + 1 == frames || size < 32768) {
        packed.push_back(255);
        packed.push_back(static_cast<uint8_t>(size >> 8));
        packed.push_back(static_cast<uint8_t>(size));
      }
      packed.push_back(static_cast<uint8_t>(compressed >> 8));
      packed.push_back(static_cast<uint8_t>(compressed));
      packed.insert(packed.end(), cab.begin() + cursor,
                    cab.begin() + cursor + compressed);
      cursor += compressed;
    }
    std::filesystem::remove(input_path);
    std::filesystem::remove(cab_path);
    packed.resize(packed.size() + 9);
    std::vector<uint8_t> decoded(input.size());
    if (!decode_package_lzx(packed, decoded) ||
        !std::equal(decoded.begin(), decoded.end(), input.begin()))
      throw std::runtime_error("Keyboard package failed its LZX round-trip check");
    write32(result, 16 + chunk * 8, static_cast<uint32_t>(packed.size()));
    write32(result, 20 + chunk * 8, static_cast<uint32_t>(input.size()));
    result.insert(result.end(), packed.begin(), packed.end());
  }
  write32(result, 8, static_cast<uint32_t>(result.size() - 16 - chunks * 8));
  result.resize((result.size() + 32767) & ~size_t(32767));
  return result;
}
std::unique_ptr<rex::filesystem::Device> make_keyboard_game_device(
    const std::filesystem::path &game, const std::filesystem::path &cache) {
  return std::make_unique<keyboard_game_device>(game, cache);
}

bool cache_keyboard_prompts(std::span<const uint8_t> package,
                            const std::filesystem::path& directory) {
  const auto path = directory / "Startup.xxx";
  const auto stamp = directory / "Startup.lzx-v1";
  const auto source_hash = XXH3_64bits(package.data(), package.size());
  uint64_t saved_source{}, saved_packed{};
  if (std::ifstream(stamp) >> saved_source >> saved_packed && saved_source == source_hash) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    const auto size = file.tellg();
    if (size > 0 && size <= 4 * 1024 * 1024) {
      std::vector<uint8_t> bytes(static_cast<size_t>(size));
      file.seekg(0);
      if (file.read(reinterpret_cast<char*>(bytes.data()), bytes.size()) &&
          XXH3_64bits(bytes.data(), bytes.size()) == saved_packed)
        return true;
    }
  }
  const auto packed = pack_keyboard_prompts(package, directory);
  auto publish = [&](const std::filesystem::path& target, const char* data, size_t size) {
    auto temporary = target;
    temporary += "." + std::to_string(GetCurrentProcessId()) + ".tmp";
    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    out.write(data, size);
    out.close();
    if (!out || !MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING))
      throw std::runtime_error("Could not publish keyboard artwork cache");
  };
  publish(path, reinterpret_cast<const char*>(packed.data()), packed.size());
  const auto hashes =
      std::to_string(source_hash) + " " + std::to_string(XXH3_64bits(packed.data(), packed.size()));
  publish(stamp, hashes.data(), hashes.size());
  return false;
}

std::vector<uint8_t> make_keyboard_prompts(std::span<const uint8_t> startup,
                                           bool controller) {
  constexpr std::array<uint8_t, 32> expected = {
      0x44, 0x19, 0x7b, 0x07, 0x5e, 0x3f, 0x8f, 0x98, 0x6f, 0x51, 0x99,
      0xb5, 0xe7, 0xbf, 0xfc, 0xa9, 0xb0, 0xa7, 0x1b, 0x2a, 0xb3, 0x8b,
      0x0b, 0x6a, 0xd8, 0xfb, 0xe3, 0x85, 0xd0, 0xf4, 0x5a, 0x35};
  std::array<uint8_t, 32> digest{};
  if (startup.size() != 294912 ||
      BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0,
                 const_cast<uint8_t *>(startup.data()),
                 static_cast<ULONG>(startup.size()), digest.data(),
                 digest.size()) < 0 ||
      digest != expected)
    throw std::runtime_error(
        "Keyboard prompts require the verified disc's Startup.xxx");
  const auto total = read32(startup, 12);
  if (read32(startup, 0) != 0x9e2a83c1 || read32(startup, 4) != 0x20000 ||
      total != 1893850)
    throw std::runtime_error("Unsupported startup compression");
  const auto blocks = (total + 0x1ffff) / 0x20000;
  size_t cursor = 16 + blocks * 8;
  std::vector<uint8_t> package(total);
  size_t output = 0;
  for (unsigned block = 0; block < blocks; ++block) {
    const auto packed_size = read32(startup, 16 + block * 8);
    const auto size = read32(startup, 20 + block * 8);
    if (cursor > startup.size() || packed_size > startup.size() - cursor ||
        size > package.size() - output)
      throw std::runtime_error("Invalid startup compression bounds");
    const auto packed = startup.subspan(cursor, packed_size);
    if (!decode_package_lzx(packed, std::span(package).subspan(output, size)))
      throw std::runtime_error("Could not decompress startup artwork");
    cursor += packed_size;
    output += size;
  }
  if (output != total) throw std::runtime_error("Incomplete startup artwork");
  gdiplus_session renderer;
  for (const auto &p : prompts) {
    if (controller)
      upscale_controller_prompt(package, p);
    else
      paint_prompt(package, p);
  }
  return package;
}

std::array<std::vector<uint8_t>, 6> make_settings_prompt_artwork(
    std::span<const uint8_t> startup, bool keyboard) {
  auto package = make_keyboard_prompts(startup, true);
  gdiplus_session renderer;
  constexpr size_t offsets[] = {0x198b24, 0x19cc85, 0x1ad209, 0x1b962c,
                                0x1a4f47, 0x1c9bb0};
  constexpr const char* labels[] = {"ENT", "ESC", "TAB", "TAB", "WASD", "F4"};
  std::array<std::vector<uint8_t>, 6> result;
  for (size_t n = 0; n < result.size(); ++n) {
    const prompt p{offsets[n], 16737, nullptr, labels[n]};
    if (keyboard) paint_prompt(package, p);
    auto& rgba = result[n];
    rgba.resize(128 * 128 * 4);
    for (unsigned y = 0; y < 128; y += 4) {
      for (unsigned x = 0; x < 128; x += 4) {
        std::array<uint8_t, 16> block;
        std::copy_n(package.begin() + p.offset + 325 + tiled_offset(x / 4, y / 4, 32),
                    16, block.begin());
        for (unsigned i = 0; i < 16; i += 2) std::swap(block[i], block[i + 1]);
        std::array<unsigned, 8> alpha{block[0], block[1]};
        for (unsigned i = 2; i < (alpha[0] > alpha[1] ? 8u : 6u); ++i)
          alpha[i] = alpha[0] > alpha[1]
              ? ((8 - i) * alpha[0] + (i - 1) * alpha[1]) / 7
              : ((6 - i) * alpha[0] + (i - 1) * alpha[1]) / 5;
        if (alpha[0] <= alpha[1]) { alpha[6] = 0; alpha[7] = 255; }
        uint64_t alpha_bits = 0;
        for (unsigned i = 0; i < 6; ++i) alpha_bits |= uint64_t(block[i + 2]) << (i * 8);
        std::array<std::array<unsigned, 3>, 4> colors{};
        for (unsigned i = 0; i < 2; ++i) {
          const unsigned c = block[8 + i * 2] | (unsigned(block[9 + i * 2]) << 8);
          const unsigned r = c >> 11, g = (c >> 5) & 63, b = c & 31;
          colors[i] = {(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)};
        }
        for (unsigned c = 0; c < 3; ++c) {
          colors[2][c] = (2 * colors[0][c] + colors[1][c]) / 3;
          colors[3][c] = (colors[0][c] + 2 * colors[1][c]) / 3;
        }
        uint32_t color_bits = 0;
        for (unsigned i = 0; i < 4; ++i) color_bits |= uint32_t(block[12 + i]) << (i * 8);
        for (unsigned i = 0; i < 16; ++i) {
          const auto destination = ((y + i / 4) * 128 + x + i % 4) * 4;
          for (unsigned c = 0; c < 3; ++c)
            rgba[destination + c] = uint8_t(colors[(color_bits >> (i * 2)) & 3][c]);
          rgba[destination + 3] = uint8_t(alpha[(alpha_bits >> (i * 3)) & 7]);
        }
      }
    }
  }
  return result;
}

std::vector<prompt_variants> make_prompt_variants(
    std::span<const uint8_t> keyboard, std::span<const uint8_t> gamepad) {
  if (keyboard.size() != 1893850 || gamepad.size() != 1893850)
    throw std::runtime_error("Invalid prompt variant package size");
  std::vector<prompt_variants> result;
  for (const auto &p : prompts) {
    const auto k = keyboard.subspan(p.offset + 325, p.size - 353);
    const auto g = gamepad.subspan(p.offset + 325, p.size - 353);
    result.push_back({{k.begin(), k.end()}, {g.begin(), g.end()}, prompt_hash(k), prompt_hash(g)});
  }
  return result;
}

std::span<const uint8_t> select_prompt_artwork(
    std::span<const prompt_variants> variants, std::span<const uint8_t> texture,
    bool keyboard) {
  const auto hash = prompt_hash(texture);
  for (const auto &variant : variants) {
    const auto &current = keyboard ? variant.gamepad : variant.keyboard;
    const auto current_hash =
        keyboard ? variant.gamepad_hash : variant.keyboard_hash;
    if (hash == current_hash && std::ranges::equal(texture, current))
      return keyboard ? variant.keyboard : variant.gamepad;
  }
  return {};
}

std::vector<uint8_t> make_keyboard_title(std::span<const uint8_t> coalesced) {
  size_t cursor = 0;
  auto count = [&] {
    const auto value = read32(coalesced, cursor);
    cursor += 4;
    if (value > 100000)
      throw std::runtime_error("Invalid coalesced entry count");
    return value;
  };
  auto string = [&] {
    const auto length = std::bit_cast<int32_t>(read32(coalesced, cursor));
    cursor += 4;
    if (length < -1048576 || length > 1048576)
      throw std::runtime_error("Invalid coalesced string length");
    const size_t chars = length < 0 ? -length : length;
    const size_t width = length < 0 ? 2 : 1;
    if (chars * width > coalesced.size() - cursor)
      throw std::runtime_error("Truncated coalesced string");
    std::u16string result;
    for (size_t i = 0; i < chars; ++i) {
      const auto ch =
          coalesced[cursor + i * width] |
          (width == 2 ? uint16_t(coalesced[cursor + i * width + 1]) << 8 : 0);
      result.push_back(static_cast<char16_t>(ch));
    }
    cursor += chars * width;
    if (chars) {
      if (result.back())
        throw std::runtime_error("Unterminated coalesced string");
      result.pop_back();
    }
    return result;
  };
  size_t begin = 0, end = 0, entry_count_offset = 0;
  uint32_t title_entries = 0;
  std::u16string text, gamepad_text;
  const auto files = count();
  for (unsigned file = 0; file < files; ++file) {
    string();
    const auto sections = count();
    for (unsigned section = 0; section < sections; ++section) {
      const auto name = string();
      const auto entries_offset = cursor;
      const auto entries = count();
      for (unsigned entry = 0; entry < entries; ++entry) {
        const auto key = string();
        const auto value_begin = cursor;
        auto value = string();
        if (name == u"StartTitle" && key == u"TITLE_START") {
          if (end) throw std::runtime_error("Duplicate title prompt");
          const auto start = value.find(u"START");
          if (start == std::u16string::npos)
            throw std::runtime_error("Unsupported title prompt text");
          auto label = key_label({0, 0, "keybind_start", nullptr});
          if (label == L"ENT") label = L"ENTER";
          gamepad_text = value;
          value.replace(start, 5, std::u16string(label.begin(), label.end()));
          entry_count_offset = entries_offset;
          title_entries = entries;
          begin = value_begin;
          end = cursor;
          text = std::move(value);
        }
      }
    }
  }
  if (!end || cursor != coalesced.size())
    throw std::runtime_error("Unsupported coalesced localization");
  std::vector<uint8_t> result(coalesced.begin(), coalesced.begin() + begin);
  auto append = [&](std::u16string_view value) {
    const auto length_offset = result.size();
    result.resize(result.size() + 4);
    write32(result, length_offset,
            static_cast<uint32_t>(-int32_t(value.size() + 1)));
    for (char16_t ch : value) {
      result.push_back(static_cast<uint8_t>(ch));
      result.push_back(static_cast<uint8_t>(ch >> 8));
    }
    result.insert(result.end(), {0, 0});
  };
  append(text);
  append(u"TITLE_START_GAMEPAD");
  append(gamepad_text);
  write32(result, entry_count_offset, title_entries + 1);
  result.insert(result.end(), coalesced.begin() + end, coalesced.end());
  return result;
}

void install_keyboard_prompts(rex::Runtime &runtime,
                              const std::filesystem::path &game_root,
                              const std::filesystem::path &cache_root) {
  if (!rex::cvar::Query<bool>("mnk_mode")) return;
  const auto source = game_root / "AshGame/CookedXbox360/Startup.xxx";
  std::ifstream file(source, std::ios::binary | std::ios::ate);
  if (!file || file.tellg() != 294912)
    throw std::runtime_error(
        "Could not read the verified Startup.xxx for keyboard prompts");
  std::vector<uint8_t> packed(294912);
  file.seekg(0);
  if (!file.read(reinterpret_cast<char *>(packed.data()), packed.size()))
    throw std::runtime_error("Could not read startup artwork");
  const auto rendered = make_keyboard_prompts(packed);
  auto variants =
      make_prompt_variants(rendered, make_keyboard_prompts(packed, true));
  auto *input = dynamic_cast<rex::input::InputSystem *>(runtime.input_system());
  if (!input)
    throw std::runtime_error("Prompt switching needs the SDK input system");
  rex::system::SetTextureDataCallback(
      [input, variants = std::move(variants),
       profile = !rex::cvar::GetFlagByName("perf_log_csv").empty()](
          uint32_t width, uint32_t height, uint32_t format,
          std::span<const uint8_t> data) -> std::span<const uint8_t> {
        if (width != height || (width != 128 && width != 256) || format != 20)
          return {};
        const bool keyboard = input->IsKeyboardActive();
        if (!profile)
          return select_prompt_artwork(variants, data, keyboard);
        const auto start = std::chrono::steady_clock::now();
        const auto replacement = select_prompt_artwork(variants, data, keyboard);
        using namespace rex::perf;
        IncrementCounter(CounterId::kPromptMatchNs,
                         std::chrono::duration_cast<std::chrono::nanoseconds>(
                             std::chrono::steady_clock::now() - start)
                             .count());
        IncrementCounter(CounterId::kPromptCandidates);
        IncrementCounter(CounterId::kPromptHashBytes, std::min(data.size(), prompt_sample_bytes));
        if (!replacement.empty())
          IncrementCounter(CounterId::kPromptReplacements);
        return replacement;
      });
  const auto directory = cache_root / "keyboard-prompts" /
                         std::to_string(XXH3_64bits(rendered.data(), rendered.size()));
  std::filesystem::create_directories(directory);
  const auto cache_start = std::chrono::steady_clock::now();
  const bool reused = cache_keyboard_prompts(rendered, directory);
  REXLOG_INFO("Keyboard prompt package {} in {} ms", reused ? "reused" : "built",
              std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - cache_start)
                  .count());
  for (const auto &entry :
       std::filesystem::directory_iterator(source.parent_path())) {
    const auto filename = entry.path().filename().string();
    if (!filename.starts_with("Coalesced_") || !filename.ends_with(".bin"))
      continue;
    std::ifstream config(entry.path(), std::ios::binary | std::ios::ate);
    if (!config || config.tellg() < 0 || config.tellg() > 4 * 1024 * 1024)
      throw std::runtime_error("Could not read title localization");
    std::vector<uint8_t> original(static_cast<size_t>(config.tellg()));
    config.seekg(0);
    if (!config.read(reinterpret_cast<char *>(original.data()),
                     original.size()))
      throw std::runtime_error("Could not read title localization");
    const auto localized = make_keyboard_title(original);
    std::ofstream title(directory / entry.path().filename(),
                        std::ios::binary | std::ios::trunc);
    title.write(reinterpret_cast<const char *>(localized.data()),
                localized.size());
    title.close();
    if (!title)
      throw std::runtime_error("Could not cache keyboard title prompt");
  }
  // The async loader trusts TOC sizes instead of querying the overridden files.
  for (const auto &entry :
       std::filesystem::directory_iterator(game_root / "AshGame")) {
    const auto filename = entry.path().filename().string();
    if (!filename.starts_with("Xbox360TOC") ||
        entry.path().extension() != ".txt")
      continue;
    std::ifstream input(entry.path());
    std::ofstream toc(directory / entry.path().filename(), std::ios::trunc);
    if (!input || !toc)
      throw std::runtime_error("Could not prepare keyboard asset TOC");
    std::string line;
    while (std::getline(input, line)) {
      std::istringstream fields(line);
      uint64_t size, unpacked;
      std::string path;
      if (fields >> size >> unpacked >> path) {
        const auto name = path.substr(path.find_last_of('\\') + 1);
        const auto cached = directory / name;
        if (std::filesystem::is_regular_file(cached))
          line.replace(0, line.find(' '),
                       std::to_string(std::filesystem::file_size(cached)));
      }
      toc << line << '\n';
    }
    toc.close();
    if (!input.eof() || !toc)
      throw std::runtime_error("Could not cache keyboard asset TOC");
  }
  auto device = make_keyboard_game_device(game_root, directory);
  if (!device->Initialize())
    throw std::runtime_error("Could not mount keyboard artwork");
  auto *vfs = runtime.file_system();
  if (!vfs->RegisterDevice(std::move(device)) ||
      !vfs->UnregisterSymbolicLink("game:") ||
      !vfs->UnregisterSymbolicLink("d:") ||
      !vfs->RegisterSymbolicLink("game:", "\\Device\\LollipopGame") ||
      !vfs->RegisterSymbolicLink("d:", "\\Device\\LollipopGame"))
    throw std::runtime_error("Could not install keyboard artwork override");
}

}  // namespace lollipop
