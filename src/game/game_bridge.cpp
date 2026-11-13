#include "game_bridge.h"
#include <bit>
#include <cassert>
#include <cmath>
#include <cstring>
#include <rex/input/input_system.h>
#include <rex/logging.h>
#include <rex/memory.h>
#include <rex/runtime.h>
#include <rex/system/thread_state.h>
#include <stdexcept>

namespace lollipop {
game_bridge::game_bridge(rex::Runtime& runtime, PPCContext& context)
    : runtime_(runtime), context_(context), saved_(context), memory_(runtime.virtual_membase()) {
  context_.r1.u64 -= 2048;
  write(context_.r1.u32, saved_.r1.u32);
  scratch_ = context_.r1.u32 + 128;
}

game_bridge::~game_bridge() {
  context_ = saved_;
  context_.fpscr.restoreGuestBits(saved_.fpscr.csr);
}

bool game_bridge::valid_address(uint32_t address, uint32_t size) {
  return address >= 0x40000000 && size <= 0x80000000 && address <= 0xC0000000u - size;
}

uint32_t game_bridge::read(uint32_t at) const {
  return rex::memory::load_and_swap<uint32_t>(memory_ + at);
}

uint32_t game_bridge::world_info() const {
  const auto world = read(0x83353B5C);
  if (!valid_address(world, 84))
    return 0;
  const auto level = read(world + 80);
  if (!valid_address(level, 64))
    return 0;
  const auto actors = read(level + 60);
  if (!valid_address(actors))
    return 0;
  const auto info = read(actors);
  return valid_address(info) ? info : 0;
}

void game_bridge::write(uint32_t at, uint32_t value) {
  rex::memory::store_and_swap<uint32_t>(memory_ + at, value);
}

uint32_t game_bridge::find(const std::string& name) {
  const auto text = scratch_ + 256;
  if (name.size() > 256)
    return 0;
  for (size_t i = 0; i <= name.size(); ++i)
    rex::memory::store_and_swap<uint16_t>(memory_ + text + i * 2, i == name.size() ? 0 : name[i]);
  uint64_t args[] = {0, 0, text, 0};
  return uint32_t(runtime_.function_dispatcher()->ExecuteTrap(rex::runtime::ThreadState::Get(),
                                                              0x822AE758, args, 4));
}

uint32_t game_bridge::invoke(uint32_t object, uint32_t function, uint32_t a, uint32_t b) {
  std::memset(memory_ + scratch_, 0, 128);
  write(scratch_, a);
  write(scratch_ + 4, b);
  return process(object, function);
}

void game_bridge::invoke_bools(uint32_t object, uint32_t function, const std::string& path,
                               const char* first, bool a, const char* second, bool b) {
  std::memset(memory_ + scratch_, 0, 128);
  auto put = [&](const char* name, bool value) {
    const auto property = find(path + "." + name);
    if (!property)
      return false;
    const auto offset = read(property + 84), mask = read(property + 116);
    if (offset > 124 || !mask || (mask & (mask - 1)))
      return false;
    if (value)
      write(scratch_ + offset, read(scratch_ + offset) | mask);
    return true;
  };
  if (!put(first, a) || (second && !put(second, b)))
    return;
  process(object, function);
}

uint32_t game_bridge::process(uint32_t object, uint32_t function) {
  // Reflected functions can live in the game's low-address static allocations.
  if (!valid_address(object) || !function)
    throw std::runtime_error(
        fmt::format("Invalid native object {:08X} or function {:08X}", object, function));
  const auto size = rex::memory::load_and_swap<uint16_t>(memory_ + function + 138);
  assert(size <= 128);
  if (size > 128)
    return 0;
  const auto process_event = read(read(object) + 248);
  uint64_t args[] = {object, function, scratch_, 0};
  runtime_.function_dispatcher()->ExecuteTrap(rex::runtime::ThreadState::Get(), process_event, args,
                                              4);
  const auto offset = rex::memory::load_and_swap<uint16_t>(memory_ + function + 140);
  return offset <= 124 ? read(scratch_ + offset) : 0;
}

uint32_t game_bridge::property(uint32_t object, const std::string& name) {
  const auto field = find(name);
  if (!field)
    throw std::runtime_error("Missing native dialog property: " + name);
  const auto offset = read(field + 84);
  if (offset > 0x100000 || !valid_address(object, offset + 12))
    throw std::runtime_error("Invalid native property: " + name);
  return object + offset;
}

bool game_bridge::boolean(uint32_t object, const std::string& name) {
  const auto field = find(name);
  if (!field)
    throw std::runtime_error("Missing native dialog property: " + name);
  const auto mask = read(field + 116);
  if (!mask || (mask & (mask - 1)))
    throw std::runtime_error("Invalid native boolean: " + name);
  return (read(property(object, name)) & mask) != 0;
}

void game_bridge::boolean(uint32_t object, const std::string& name, bool value) {
  const auto field = find(name);
  if (!field)
    throw std::runtime_error("Missing native dialog property: " + name);
  const auto at = property(object, name), mask = read(field + 116);
  if (!mask || (mask & (mask - 1)))
    throw std::runtime_error("Invalid native boolean: " + name);
  write(at, value ? read(at) | mask : read(at) & ~mask);
}

void game_bridge::set_text(uint32_t object, const std::string& property_name, const char* value) {
  const auto target = property(object, property_name);
  if (std::strlen(value) > 255)
    throw std::runtime_error("Native text is too long");
  std::memset(memory_ + scratch_, 0, 128);
  string(scratch_ + 32, scratch_ + 512, "_root._lollipopHostText");
  string(scratch_ + 44, scratch_ + 768, value);
  uint64_t set[] = {object, scratch_ + 32, scratch_ + 44};
  runtime_.function_dispatcher()->ExecuteTrap(rex::runtime::ThreadState::Get(),
                                              read(read(object) + 348), set, 3);
  uint64_t get[] = {scratch_, object, scratch_ + 32};
  runtime_.function_dispatcher()->ExecuteTrap(rex::runtime::ThreadState::Get(),
                                              read(read(object) + 328), get, 3);
  const auto data = read(scratch_), count = read(scratch_ + 4), capacity = read(scratch_ + 8);
  if (!valid_address(data, count * 2) || count != std::strlen(value) + 1 || capacity < count) {
    if (valid_address(data)) {
      uint64_t free[] = {data};
      runtime_.function_dispatcher()->ExecuteTrap(rex::runtime::ThreadState::Get(), 0x82252130,
                                                  free, 1);
    }
    throw std::runtime_error("Could not create native text");
  }
  if (const auto previous = read(target)) {
    uint64_t free[] = {previous};
    runtime_.function_dispatcher()->ExecuteTrap(rex::runtime::ThreadState::Get(), 0x82252130, free,
                                                1);
  }
  // Transfer the getter's engine-owned FString to the dialog, which frees it normally.
  write(target, data);
  write(target + 4, count);
  write(target + 8, capacity);
}

uint32_t game_bridge::call(uint32_t object, const std::string& name) {
  const auto function = find(name);
  if (!function)
    throw std::runtime_error("Missing native dialog function: " + name);
  return invoke(object, function);
}

void game_bridge::set_real_time(uint32_t movie) {
  const auto function = find("GFxUI.GFxMoviePlayer.SetTimingMode");
  if (!function)
    throw std::runtime_error("Native movie timing is unavailable");
  std::memset(memory_ + scratch_, 0, 128);
  memory_[scratch_] = 1; // TM_Real, as used by OpenSystemWindow while the game is paused.
  process(movie, function);
}

std::u16string game_bridge::movie_string(uint32_t movie, const char* path) {
  std::memset(memory_ + scratch_, 0, 128);
  string(scratch_ + 32, scratch_ + 512, path);
  uint64_t args[] = {scratch_, movie, scratch_ + 32};
  runtime_.function_dispatcher()->ExecuteTrap(rex::runtime::ThreadState::Get(),
                                              read(read(movie) + 328), args, 3);
  const auto data = read(scratch_), count = read(scratch_ + 4);
  std::u16string result;
  if (count && count < 256 && valid_address(data, count * 2))
    for (uint32_t i = 0; i + 1 < count; ++i)
      result += rex::memory::load_and_swap<uint16_t>(memory_ + data + i * 2);
  if (data) {
    uint64_t free[] = {data};
    runtime_.function_dispatcher()->ExecuteTrap(rex::runtime::ThreadState::Get(), 0x82252130, free,
                                                1);
  }
  return result;
}

void game_bridge::movie_bool(uint32_t movie, const char* path, bool value) {
  string(scratch_ + 32, scratch_ + 512, path);
  uint64_t args[] = {movie, scratch_ + 32, value};
  runtime_.function_dispatcher()->ExecuteTrap(rex::runtime::ThreadState::Get(),
                                              read(read(movie) + 340), args, 3);
}

void game_bridge::movie_call(uint32_t movie, const char* path) {
  std::memset(memory_ + scratch_, 0, 160);
  string(scratch_ + 32, scratch_ + 512, path);
  uint64_t args[] = {scratch_ + 128, movie, scratch_ + 32, scratch_ + 64};
  runtime_.function_dispatcher()->ExecuteTrap(rex::runtime::ThreadState::Get(),
                                              read(read(movie) + 396), args, 4);
}

bool game_bridge::movie_hit(uint32_t movie, const char* path, float x, float y) {
  const auto type = find("GFxUI.GFxMoviePlayer.ASValue.Type");
  const auto number = find("GFxUI.GFxMoviePlayer.ASValue.N");
  const auto boolean = find("GFxUI.GFxMoviePlayer.ASValue.B");
  if (!type || !number || !boolean)
    throw std::runtime_error("Missing native hit-test types");
  const auto t = read(type + 84), n = read(number + 84), b = read(boolean + 84);
  if (t >= 24 || n > 20 || b > 20)
    throw std::runtime_error("Invalid native hit-test layout");
  std::memset(memory_ + scratch_, 0, 224);
  string(scratch_ + 32, scratch_ + 512, path);
  write(scratch_ + 64, scratch_ + 96);
  write(scratch_ + 68, 2);
  write(scratch_ + 72, 2);
  for (unsigned i = 0; i < 2; ++i) {
    memory_[scratch_ + 96 + i * 24 + t] = 2;
    write(scratch_ + 96 + i * 24 + n, std::bit_cast<uint32_t>(i ? y : x));
  }
  uint64_t args[] = {scratch_ + 192, movie, scratch_ + 32, scratch_ + 64};
  runtime_.function_dispatcher()->ExecuteTrap(rex::runtime::ThreadState::Get(),
                                              read(read(movie) + 396), args, 4);
  return (read(scratch_ + 192 + b) & read(boolean + 116)) != 0;
}

std::pair<float, float> game_bridge::movie_point(uint32_t movie, float x, float y) {
  const auto function = find("GFxUI.GFxMoviePlayer.GetVisibleFrameRect");
  if (!function)
    throw std::runtime_error("Native dialog viewport is unavailable");
  invoke(movie, function);
  const float x0 = std::bit_cast<float>(read(scratch_)),
              y0 = std::bit_cast<float>(read(scratch_ + 4));
  const float x1 = std::bit_cast<float>(read(scratch_ + 8)),
              y1 = std::bit_cast<float>(read(scratch_ + 12));
  if (!std::isfinite(x0) || !std::isfinite(y0) || !std::isfinite(x1) || !std::isfinite(y1) ||
      x1 <= x0 || y1 <= y0)
    throw std::runtime_error("Invalid native dialog viewport");
  return {x0 + x * (x1 - x0), y0 + y * (y1 - y0)};
}

void game_bridge::console(uint32_t actor, const std::string& command) {
  const auto get_player = find("Engine.Actor.GetALocalPlayerController");
  const auto player = get_player ? invoke(actor, get_player) : 0;
  const bool has_player = player >= 0x40000000 && player < 0xBFFFF000;
  const auto function =
      find(has_player ? "Engine.PlayerController.ConsoleCommand" : "Engine.Actor.ConsoleCommand");
  if (!function)
    throw std::runtime_error("Native console is unavailable");
  std::memset(memory_ + scratch_, 0, 128);
  string(scratch_, scratch_ + 512, command.c_str());
  write(scratch_ + 12, 1);
  const auto result = process(has_player ? player : actor, function);
  if (result) {
    uint64_t args[] = {result};
    runtime_.function_dispatcher()->ExecuteTrap(rex::runtime::ThreadState::Get(), 0x82252130, args,
                                                1);
  }
}

void game_bridge::hide_legacy(uint32_t movie) {
  const auto header = scratch_ + 32, text = scratch_ + 512;
  string(header, text, "_root._visible");
  uint64_t hide[] = {movie, header, 0};
  runtime_.function_dispatcher()->ExecuteTrap(rex::runtime::ThreadState::Get(),
                                              read(read(movie) + 340), hide, 3);
}

void game_bridge::close_legacy(uint32_t movie) {
  // Let Start/Load return before executing the original close action.
  const auto header = scratch_ + 32, text = scratch_ + 512;
  std::memset(memory_ + scratch_ + 64, 0, 88);
  string(header, text, "_root.gotoAndStop");
  const auto array = scratch_ + 64, argument = scratch_ + 96, result = scratch_ + 128;
  write(array, argument);
  write(array + 4, 1);
  write(array + 8, 1);
  const auto type = find("GFxUI.GFxMoviePlayer.ASValue.Type");
  const auto number = find("GFxUI.GFxMoviePlayer.ASValue.N");
  if (!type || !number)
    return;
  const auto type_offset = read(type + 84), number_offset = read(number + 84);
  if (type_offset > 23 || number_offset > 20)
    return;
  memory_[argument + type_offset] = 2;                             // AS_Number.
  write(argument + number_offset, std::bit_cast<uint32_t>(17.0f)); // Original fsOptionClose action.
  uint64_t close[] = {result, movie, header, array};
  runtime_.function_dispatcher()->ExecuteTrap(rex::runtime::ThreadState::Get(),
                                              read(read(movie) + 396), close, 4);
  REXLOG_DEBUG("Original Options close action requested");
}

void game_bridge::string(uint32_t header, uint32_t text, const char* value) {
  const auto length = std::strlen(value);
  if (length > 255)
    throw std::runtime_error("Native string exceeds scratch space");
  for (size_t i = 0; i <= length; ++i)
    rex::memory::store_and_swap<uint16_t>(memory_ + text + i * 2, value[i]);
  write(header, text);
  write(header + 4, uint32_t(length + 1));
  write(header + 8, uint32_t(length + 1));
}

void game_bridge::consume_input() {
  auto& input = *static_cast<rex::input::InputSystem*>(runtime_.input_system());
  input.AddUIInputBlocker();
  input.RemoveUIInputBlocker();
}
std::string game_bridge::read_string(const unsigned char* memory, uint32_t address) {
  if (!memory || address < 0x40000000 || address > 0xBFFFFFF0)
    return {};
  auto read = [&](uint32_t at) {
    return (uint32_t(memory[at]) << 24) | (uint32_t(memory[at + 1]) << 16) |
           (uint32_t(memory[at + 2]) << 8) | memory[at + 3];
  };
  const auto data = read(address), count = read(address + 4);
  if (!count || count > 512 || data < 0x40000000 || data > 0xC0000000u - count * 2)
    return {};
  if (memory[data + (count - 1) * 2] || memory[data + (count - 1) * 2 + 1])
    return {};
  std::string value;
  for (uint32_t i = 0; i + 1 < count; ++i) {
    if (memory[data + i * 2] || memory[data + i * 2 + 1] > 127)
      return {};
    value += char(memory[data + i * 2 + 1]);
  }
  return value;
}

} // namespace lollipop
