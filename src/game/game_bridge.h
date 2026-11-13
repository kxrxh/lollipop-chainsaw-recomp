#pragma once

#include <cstdint>
#include <rex/ppc/context.h>
#include <string>
#include <utility>

namespace rex {
class Runtime;
}

namespace lollipop {

// Scoped access on the engine tick thread; restores the caller's PPC context.
class game_bridge {
public:
  game_bridge(rex::Runtime& runtime, PPCContext& context);
  ~game_bridge();
  game_bridge(const game_bridge&) = delete;
  game_bridge& operator=(const game_bridge&) = delete;

  static std::string read_string(const unsigned char* memory, uint32_t address);
  static bool valid_address(uint32_t address, uint32_t size = 4);
  uint32_t read(uint32_t address) const;
  uint32_t world_info() const;
  void write(uint32_t address, uint32_t value);
  uint32_t find(const std::string& name);
  uint32_t invoke(uint32_t object, uint32_t function, uint32_t a = 0, uint32_t b = 0);
  void invoke_bools(uint32_t object, uint32_t function, const std::string& path, const char* first,
                    bool a, const char* second = nullptr, bool b = false);
  uint32_t property(uint32_t object, const std::string& name);
  bool boolean(uint32_t object, const std::string& name);
  void boolean(uint32_t object, const std::string& name, bool value);
  uint32_t call(uint32_t object, const std::string& name);
  void set_text(uint32_t object, const std::string& property_name, const char* value);
  void set_real_time(uint32_t movie);
  std::u16string movie_string(uint32_t movie, const char* path);
  void movie_bool(uint32_t movie, const char* path, bool value);
  void movie_call(uint32_t movie, const char* path);
  bool movie_hit(uint32_t movie, const char* path, float x, float y);
  std::pair<float, float> movie_point(uint32_t movie, float x, float y);
  void console(uint32_t actor, const std::string& command);
  void hide_legacy(uint32_t movie);
  void close_legacy(uint32_t movie);
  void consume_input();

private:
  uint32_t process(uint32_t object, uint32_t function);
  void string(uint32_t header, uint32_t text, const char* value);
  rex::Runtime& runtime_;
  PPCContext& context_;
  PPCContext saved_;
  unsigned char* memory_;
  uint32_t scratch_;
};

} // namespace lollipop
