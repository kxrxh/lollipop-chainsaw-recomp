#include "main_menu.h"
#include "game/package_lzx.h"
#include "settings_resources.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <rex/hash.h>
#include <rex/ui/image_decode.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <windows.h>

#include <bcrypt.h>

namespace lollipop {
namespace {
using bytes = std::vector<uint8_t>;

uint32_t read(std::span<const uint8_t> data, size_t at, size_t count = 4, bool big = true) {
  if (at > data.size() || count > data.size() - at)
    throw std::runtime_error("Truncated native menu package");
  uint32_t value = 0;
  for (size_t i = 0; i < count; ++i)
    value = (value << 8) | data[at + (big ? i : count - i - 1)];
  return value;
}
void put(bytes& data, size_t at, uint32_t value, size_t count = 4, bool big = true) {
  for (size_t i = 0; i < count; ++i) {
    data.at(at + (big ? count - i - 1 : i)) = uint8_t(value);
    value >>= 8;
  }
}
void number(bytes& data, uint32_t value, size_t count = 2) {
  const auto at = data.size();
  data.resize(at + count);
  put(data, at, value, count, false);
}
void append(bytes& to, std::span<const uint8_t> from) {
  to.insert(to.end(), from.begin(), from.end());
}
void string(bytes& data, std::string_view text) {
  data.insert(data.end(), text.begin(), text.end());
  data.push_back(0);
}
void tag(bytes& data, unsigned type, std::span<const uint8_t> body) {
  number(data, (type << 6) | 63);
  number(data, uint32_t(body.size()), 4);
  append(data, body);
}
struct bits {
  bytes data;
  unsigned used = 0;
  void add(uint32_t value, unsigned count) {
    while (count) {
      if (!used)
        data.push_back(0);
      data.back() |= ((value >> --count) & 1) << (7 - used);
      used = (used + 1) % 8;
    }
  }
};
bytes matrix(int x, int y, bool bitmap = false) {
  bits b;
  b.add(bitmap, 1);
  if (bitmap) {
    b.add(22, 5);
    b.add(18.75 * 65536, 22);
    b.add(20 * 65536, 22);
  }
  b.add(0, 1);
  b.add(16, 5);
  b.add(x, 16);
  b.add(y, 16);
  return b.data;
}
bytes slot_shape() {
  bytes shape;
  number(shape, 401);
  bits bounds;
  bounds.add(16, 5);
  for (int coordinate : {-3200, 1600, -800, 800})
    bounds.add(coordinate, 16);
  append(shape, bounds.data);
  shape.insert(shape.end(), {1, 0x41});
  number(shape, 402);
  append(shape, matrix(-3200, -800, true));
  shape.insert(shape.end(), {0, 0x10});
  bits edges;
  edges.add(5, 6); // MoveTo and FillStyle1.
  edges.add(16, 5);
  edges.add(-3200, 16);
  edges.add(-800, 16);
  edges.add(1, 1);
  for (const auto [vertical, delta] :
       {std::pair{false, 4800}, {true, 1600}, {false, -4800}, {true, -1600}}) {
    edges.add(3, 2);
    edges.add(12, 4);
    edges.add(0, 1);
    edges.add(vertical, 1);
    edges.add(delta, 14);
  }
  edges.add(0, 6);
  append(shape, edges.data);
  return shape;
}
bytes slot_bitmap(std::span<const uint8_t> png) {
  int width = 0, height = 0;
  const auto rgba = rex::ui::DecodeImageRGBA(png.data(), png.size(), width, height);
  if (width != 256 || height != 80 || rgba.size() != 256 * 80 * 4)
    throw std::runtime_error("Native Quit Game artwork must be 256x80 RGBA");
  bytes argb;
  for (size_t i = 0; i < rgba.size(); i += 4) {
    const auto alpha = rgba[i + 3];
    argb.push_back(alpha);
    for (size_t c = 0; c < 3; ++c)
      argb.push_back(uint8_t((rgba[i + c] * alpha + 127) / 255));
  }
  bytes bitmap;
  number(bitmap, 402);
  bitmap.push_back(5);
  number(bitmap, width);
  number(bitmap, height);
  bitmap.insert(bitmap.end(), {0x78, 0x01});
  uint32_t a = 1, b = 0;
  for (size_t at = 0; at < argb.size();) {
    const auto count = std::min(size_t(65535), argb.size() - at);
    bitmap.push_back(at + count == argb.size()); // Uncompressed DEFLATE block.
    number(bitmap, uint32_t(count));
    number(bitmap, uint32_t(count) ^ 0xffff);
    for (auto byte : std::span(argb).subspan(at, count)) {
      bitmap.push_back(byte);
      a = (a + byte) % 65521;
      b = (b + a) % 65521;
    }
    at += count;
  }
  const auto checksum = bitmap.size();
  bitmap.resize(checksum + 4);
  put(bitmap, checksum, (b << 16) | a);
  return bitmap;
}

// AVM1 actions use the existing Scaleform button class, callbacks and animations.
struct actions {
  bytes data;
  void op(uint8_t code) { data.push_back(code); }
  void action(uint8_t code, const bytes& body) {
    op(code);
    number(data, uint32_t(body.size()));
    append(data, body);
  }
  void push(std::string_view value) {
    bytes body{0};
    string(body, value);
    action(0x96, body);
  }
  void push(int value) {
    bytes body{7};
    number(body, value, 4);
    action(0x96, body);
  }
  void boolean(bool value) { action(0x96, {5, uint8_t(value)}); }
  void get(std::string_view name) {
    push(name);
    op(0x1c);
  }
  void member(std::string_view name) {
    push(name);
    op(0x4e);
  }
  void call(std::string_view name) {
    push(0);
    push(name);
    op(0x3d);
    op(0x17);
  }
  void set(std::string_view object, std::string_view property, bool value) {
    get(object);
    push(property);
    boolean(value);
    op(0x4f);
  }
  void save(std::string_view name, std::string_view original) {
    push(name);
    get(original);
    op(0x1d);
  }
  void function(std::string_view name, const actions& body) {
    bytes header;
    string(header, name);
    number(header, 0);
    number(header, uint32_t(body.data.size()));
    action(0x9b, header);
    append(data, body.data);
  }
  void branch(const actions& yes, const actions& no = {}) {
    bytes offset;
    number(offset, uint32_t(no.data.size() + 5));
    action(0x9d, offset);
    append(data, no.data);
    offset.clear();
    number(offset, uint32_t(yes.data.size()));
    action(0x99, offset);
    append(data, yes.data);
  }
  void listen(std::string_view button, std::string_view handler) {
    push(handler);
    get("this");
    push("focusIn");
    push(3);
    get(button);
    push("addEventListener");
    op(0x52);
    op(0x17);
  }
};
actions enable_quit() {
  actions a;
  for (auto button : {"But_2", "But_2_L", "But_2_R"})
    a.set(button, "disabled", false);
  return a;
}
bytes quit_actions() {
  actions script, focus, side, select, yes, no, restore;
  script.save("_lollipopSelect", "SelectAction");
  script.save("_lollipopReturn", "ReturnPauseSelect");
  focus.push("mFocusID");
  focus.push(2);
  focus.op(0x1d);
  focus.call("MovFocusButtonEvent");
  script.function("MovButtonFocus_2", focus);
  side.push("bSeInit");
  side.boolean(false);
  side.op(0x1d);
  side.set("But_2", "focused", true);
  script.function("MovSideButtonFocus_2", side);
  select.get("mFocusID");
  select.push(2);
  select.op(0x49);
  select.get("bPushOn");
  select.op(0x12);
  select.op(0x10);
  bytes command;
  string(command, "FSCommand:FsOptionOpen");
  string(command, "FsQuitGame");
  yes.action(0x83, command);
  no.call("_lollipopSelect");
  select.branch(yes, no);
  script.function("SelectAction", select);
  restore.call("_lollipopReturn");
  append(restore.data, enable_quit().data);
  script.function("ReturnPauseSelect", restore);
  append(script.data, enable_quit().data);
  script.listen("But_2", "MovButtonFocus_2");
  script.listen("But_2_L", "MovSideButtonFocus_2");
  script.listen("But_2_R", "MovSideButtonFocus_2");
  actions below_voice, below_difficulty;
  auto position = [](actions& a, std::string_view object, std::string_view below) {
    a.get(object);
    a.push("_y");
    a.get(below);
    a.member("_y");
    a.push(75);
    a.op(0x47);
    a.op(0x4f);
  };
  for (auto suffix : {"", "_L", "_R"}) {
    position(below_voice, "But_2" + std::string(suffix), "But_5" + std::string(suffix));
    position(below_difficulty, "But_2" + std::string(suffix), "But_4" + std::string(suffix));
  }
  position(below_voice, "SelectMode.Sel_2", "SelectMode.Sel_5");
  position(below_difficulty, "SelectMode.Sel_2", "SelectMode.Sel_4");
  script.get("bVoice");
  script.branch(below_voice, below_difficulty);
  script.op(0);
  return script.data;
}

struct swf_tag {
  unsigned type;
  size_t start, body, end;
};
swf_tag next_tag(std::span<const uint8_t> swf, size_t start) {
  const auto header = read(swf, start, 2, false);
  const auto body = start + ((header & 63) == 63 ? 6 : 2);
  const auto size = (header & 63) == 63 ? read(swf, start + 2, 4, false) : header & 63;
  if (body > swf.size() || size > swf.size() - body)
    throw std::runtime_error("Invalid native menu SWF tag");
  return {header >> 6, start, body, body + size};
}
bytes patch_movie(std::span<const uint8_t> swf, std::span<const uint8_t> artwork) {
  const size_t start = 8 + (5 + 4 * (swf[8] >> 3) + 7) / 8 + 4;
  bytes result(swf.begin(), swf.begin() + start), slot;
  unsigned frame = 1, changed = 0;
  for (size_t at = start; at < swf.size();) {
    const auto t = next_tag(swf, at);
    auto body = swf.subspan(t.body, t.end - t.body);
    if (t.type == 39 && read(body, 0, 2, false) == 220) {
      slot.assign(body.begin(), body.end());
      put(slot, 0, 400, 2, false);
      unsigned replacements = 0;
      for (size_t child = 4; child < slot.size();) {
        const auto placement = next_tag(slot, child);
        if (placement.type == 26 && (slot[placement.body] & 2) &&
            read(slot, placement.body + 3, 2, false) == 219) {
          put(slot, placement.body + 3, 401, 2, false);
          ++replacements;
        }
        child = placement.end;
      }
      if (replacements != 1)
        throw std::runtime_error("Unsupported native Options animation");
    }
    if (t.type == 39 && read(body, 0, 2, false) == 232) {
      if (slot.empty())
        throw std::runtime_error("Missing native Options animation");
      tag(result, 36, slot_bitmap(artwork));
      tag(result, 32, slot_shape());
      tag(result, 39, slot);
      bytes parent(body.begin(), body.begin() + 4);
      for (size_t child = 4; child < body.size();) {
        const auto item = next_tag(body, child);
        if (item.type == 1 && changed == 0) {
          bytes placement{0x26};
          number(placement, 39);
          number(placement, 400);
          append(placement, matrix(-10, 3600));
          string(placement, "Sel_2");
          tag(parent, 26, placement);
          ++changed;
        }
        append(parent, body.subspan(child, item.end - child));
        child = item.end;
      }
      tag(result, t.type, parent);
    } else if (t.type == 12 && frame == 3) {
      if (body.empty() || body.back() != 0)
        throw std::runtime_error("Invalid title actions");
      bytes code(body.begin(), body.end() - 1);
      append(code, quit_actions());
      tag(result, t.type, code);
      ++changed;
    } else
      append(result, swf.subspan(t.start, t.end - t.start));
    if (t.type == 1)
      ++frame;
    at = t.end;
  }
  if (changed != 2)
    throw std::runtime_error("Unsupported native title menu");
  put(result, 4, uint32_t(result.size()), 4, false);
  return result;
}
bytes load(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input || input.tellg() <= 0 || input.tellg() > 16 * 1024 * 1024)
    throw std::runtime_error("Could not read native menu package");
  bytes data(size_t(input.tellg()));
  input.seekg(0);
  if (!input.read(reinterpret_cast<char*>(data.data()), data.size()))
    throw std::runtime_error("Could not read native menu package");
  return data;
}
} // namespace

std::vector<uint8_t> make_main_menu(std::span<const uint8_t> source,
                                    std::span<const uint8_t> artwork) {
  constexpr std::array<uint8_t, 32> expected{0x1e, 0x72, 0xa4, 0x2b, 0x3c, 0xe4, 0xbb, 0x49,
                                             0x5e, 0x0d, 0x71, 0xd4, 0x4b, 0x79, 0x1b, 0xf2,
                                             0xc8, 0xf9, 0x80, 0x37, 0xd1, 0x36, 0x50, 0x71,
                                             0xce, 0x65, 0xed, 0xdc, 0x9a, 0x0f, 0x5a, 0x11};
  std::array<uint8_t, 32> digest{};
  if (source.size() != 3276800 ||
      BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, const_cast<uint8_t*>(source.data()),
                 ULONG(source.size()), digest.data(), ULONG(digest.size())) < 0 ||
      digest != expected)
    throw std::runtime_error("Quit Game requires the verified disc's MENU_PL.xxx");
  bytes package(8331940);
  std::copy_n(source.begin(), 0x809, package.begin());
  for (size_t chunk = 0; chunk < 11; ++chunk) {
    const auto summary = 117 + chunk * 16;
    size_t output = read(source, summary), remaining = read(source, summary + 4);
    const auto packed = read(source, summary + 8);
    if (read(source, packed) != 0x9e2a83c1 || read(source, packed + 4) != 0x20000 ||
        read(source, packed + 12) != remaining)
      throw std::runtime_error("Unsupported menu compression");
    const auto blocks = (remaining + 0x1ffff) / 0x20000;
    size_t cursor = packed + 16 + blocks * 8;
    for (size_t block = 0; block < blocks; ++block) {
      const auto count = read(source, packed + 16 + block * 8);
      const auto size = read(source, packed + 20 + block * 8);
      if (cursor > source.size() || count > source.size() - cursor || output > package.size() ||
          size > package.size() - output || size > remaining ||
          !decode_package_lzx(source.subspan(cursor, count),
                              std::span(package).subspan(output, size)))
        throw std::runtime_error("Could not decode native menu");
      cursor += count;
      output += size;
      remaining -= size;
    }
    if (remaining)
      throw std::runtime_error("Incomplete native menu");
  }
  // Keep all existing export/bulk-data offsets; append only the changed movie.
  constexpr size_t export_record = 33378, movie_export = 2296941, movie_size = 116623;
  constexpr size_t movie_header = 157, swf_size = 116398;
  const auto movie =
      patch_movie(std::span(package).subspan(movie_export + movie_header, swf_size), artwork);
  put(package, export_record + 32, uint32_t(movie_size + movie.size() - swf_size));
  put(package, export_record + 36, uint32_t(package.size()));
  bytes object(package.begin() + movie_export, package.begin() + movie_export + movie_header);
  put(object, 145, uint32_t(movie.size() + 4));
  put(object, 153, uint32_t(movie.size()));
  append(object, movie);
  append(object, std::span(package).subspan(movie_export + movie_header + swf_size,
                                            movie_size - movie_header - swf_size));
  append(package, object);
  put(package, 21, read(package, 21) & ~0x02000000u);
  put(package, 109, 0);
  put(package, 113, 0);
  std::copy(package.begin() + 293, package.begin() + 0x809, package.begin() + 117);
  std::fill(package.begin() + 0x809 - 176, package.begin() + 0x809, 0);
  return package;
}

void cache_main_menu(const std::filesystem::path& game, const std::filesystem::path& cache) {
  const auto resource =
      FindResourceW(nullptr, MAKEINTRESOURCEW(IDR_MAIN_MENU_QUIT), MAKEINTRESOURCEW(10));
  const auto handle = resource ? LoadResource(nullptr, resource) : nullptr;
  if (!handle)
    throw std::runtime_error("Missing native Quit Game artwork");
  const auto artwork = std::span(static_cast<const uint8_t*>(LockResource(handle)),
                                 SizeofResource(nullptr, resource));
  if (!artwork.data())
    throw std::runtime_error("Could not read native Quit Game artwork");
  const auto source = load(game / "AshGame/CookedXbox360/MENU_PL.xxx");
  const auto source_hash = XXH3_64bits(source.data(), source.size());
  const auto art_hash = XXH3_64bits(artwork.data(), artwork.size());
  const auto path = cache / "MENU_PL.xxx", stamp = cache / "MENU.native-v3";
  uint64_t old_source{}, old_art{}, old_output{};
  if (std::ifstream(stamp) >> old_source >> old_art >> old_output && old_source == source_hash &&
      old_art == art_hash && std::filesystem::is_regular_file(path)) {
    try {
      const auto cached = load(path);
      if (XXH3_64bits(cached.data(), cached.size()) == old_output)
        return;
    } catch (const std::runtime_error&) {
      // An unreadable or incomplete cache can be rebuilt from the disc.
    }
  }
  std::filesystem::create_directories(cache);
  const auto patched = make_main_menu(source, artwork);
  auto publish = [](const std::filesystem::path& target, const char* data, size_t size) {
    auto temporary = target;
    temporary += "." + std::to_string(GetCurrentProcessId()) + ".tmp";
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    file.write(data, size);
    file.close();
    if (!file || !MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING))
      throw std::runtime_error("Could not cache native Quit Game menu");
  };
  publish(path, reinterpret_cast<const char*>(patched.data()), patched.size());
  const auto hashes = std::to_string(source_hash) + " " + std::to_string(art_hash) + " " +
                      std::to_string(XXH3_64bits(patched.data(), patched.size()));
  publish(stamp, hashes.data(), hashes.size());
}
} // namespace lollipop
