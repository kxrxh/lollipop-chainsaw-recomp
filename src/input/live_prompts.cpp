#include "live_prompts.h"
#include "game/game_menus.h"
#include "settings/runtime_settings.h"
#include <rex/kernel/function_dispatcher.h>

#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>

#include <rex/hid/input_system.h>
#include <rex/kernel/heap.h>
#include <rex/kernel/thread_state.h>
#include <rex/kernel/title_runtime.h>
#include <rex/logging/log_category.h>

namespace {
std::atomic<rex::kernel::TitleRuntime*> prompt_runtime{nullptr};
PPCFunc* advance_original = nullptr;
uint32_t advance_address = 0;
struct movie_state {
  uint32_t player;
  unsigned frames = 0;
  bool title = false;
};
std::unordered_map<uint32_t, movie_state> movies;
std::recursive_mutex movie_mutex;

void advance_movie(PPCContext& ctx, uint8_t* base) {
  const uint32_t view = ctx.r3.u32;
  PPCFunc* original;
  {
    std::lock_guard lock(movie_mutex);
    original = advance_original;
  }
  original(ctx, base);
  auto* runtime = prompt_runtime.load();
  if (!runtime) return;
  // Native settings can wait on guest threads that also need the runtime lock.
  lollipop::update_game_ui(*runtime, ctx);
  if (!lollipop::settings::Query<bool>("mnk_mode"))
    return;
  // Advance can unload its own movie; check ownership again before touching it.
  movie_state movie;
  {
    std::lock_guard lock(movie_mutex);
    auto found = movies.find(view);
    if (found == movies.end()) return;
    movie = found->second;
    ++found->second.frames;
  }
  if (!movie.title && movie.frames >= 4 && movie.frames % 60 != 0) return;
  const auto memory = runtime->GetMembase();
  auto read = [&](uint32_t address) {
    return rex::memory::load_and_swap<uint32_t>(memory.TranslateVirtual(address));
  };
  const auto native_movie = read(movie.player + 60);
  if (native_movie < 65536 || (native_movie & 3) ||
      read(native_movie + 92) != movie.player ||
      read(native_movie + 52) != view)
    return;
  auto* thread = rex::kernel::ThreadState::Get();
  auto* dispatcher = &runtime->GetFunctionDispatcher();
  const auto vtable = read(movie.player);
  const auto getter = read(vtable + 328), setter = read(vtable + 348);
  if (!dispatcher->GetFunction(getter) || !dispatcher->GetFunction(setter))
    return;

  const PPCContext saved = ctx;
  ctx.r1.u64 -= 512;
  const auto scratch = ctx.r1.u32 + 80;
  auto write = [&](uint32_t address, uint32_t value) {
    rex::memory::store_and_swap<uint32_t>(memory.TranslateVirtual(address), value);
  };
  auto string = [&](uint32_t header, uint32_t data, std::u16string_view value) {
    write(header, data);
    write(header + 4, uint32_t(value.size() + 1));
    write(header + 8, uint32_t(value.size() + 1));
    for (size_t i = 0; i <= value.size(); ++i)
      rex::memory::store_and_swap<uint16_t>(memory.TranslateVirtual(data + uint32_t(i * 2)),
                                            i == value.size() ? 0 : value[i]);
  };
  write(ctx.r1.u32, saved.r1.u32);
  std::memset(memory.TranslateVirtual(scratch), 0, 36);
  string(scratch + 12, scratch + 48, u"_root.GameStart.txstart.txt.str.text");
  uint64_t get_args[] = {scratch, movie.player, scratch + 12};
  dispatcher->ExecuteTrap(thread, getter, get_args, 3);
  const auto data = read(scratch), count = read(scratch + 4);
  std::u16string value;
  if (data && count && count < 256) {
    for (uint32_t i = 0; i + 1 < count; ++i)
      value.push_back(rex::memory::load_and_swap<uint16_t>(memory.TranslateVirtual(data + i * 2)));
  }
  const bool title = !value.empty();
  if (title) {
    auto* input = REX_INTERFACE(rex::hid::IInputSystem);
    const std::u16string_view wanted =
        input->IsKeyboardActive() ? u"$AshGame.StartTitle.TITLE_START"
                                  : u"$AshGame.StartTitle.TITLE_START_GAMEPAD";
    if (value != wanted) {
      string(scratch + 24, scratch + 144, wanted);
      uint64_t set_args[] = {movie.player, scratch + 12, scratch + 24};
      dispatcher->ExecuteTrap(thread, setter, set_args, 3);
    }
    std::lock_guard lock(movie_mutex);
    auto current = movies.find(view);
    if (current != movies.end() && current->second.player == movie.player) {
      if (!current->second.title)
        REXLOG_INFO("Found live title prompt on movie {:08X}", view);
      current->second.title = true;
    }
  }
  if (data) {
    uint64_t free_args[] = {data};
    dispatcher->ExecuteTrap(thread, 0x82252130, free_args, 1);
  }
  ctx = saved;
  ctx.fpscr.RestoreGuestBits(saved.fpscr.csr);
}
}  // namespace

namespace lollipop {

void install_live_prompt_updates(rex::kernel::TitleRuntime& runtime) {
  std::lock_guard lock(movie_mutex);
  movies.clear();
  advance_original = nullptr;
  advance_address = 0;
  prompt_runtime.store(&runtime);
}

void stop_live_prompt_updates() { prompt_runtime.store(nullptr); }

}  // namespace lollipop

void lollipop_watch_movie(PPCRegister& player, PPCRegister& path) {
  lollipop::game_movie_loaded(player.u32, path.u32);
  auto* runtime = prompt_runtime.load();
  if (!runtime) return;
  const auto memory = runtime->GetMembase();
  auto read = [&](uint32_t address) {
    return rex::memory::load_and_swap<uint32_t>(memory.TranslateVirtual(address));
  };
  const auto native_movie = read(player.u32 + 60);
  if (!native_movie) return;
  const auto view = read(native_movie + 52);
  if (!view) return;
  const auto function = read(read(view) + 148);
  auto* dispatcher = &runtime->GetFunctionDispatcher();
  std::lock_guard lock(movie_mutex);
  if (!advance_original) {
    auto* original = dispatcher->GetFunction(function);
    if (!original || !dispatcher->SetFunction(function, advance_movie))
      return;
    advance_original = original;
    advance_address = function;
    REXLOG_INFO("Watching live GFx advance {:08X}", function);
  }
  if (function == advance_address) movies[view] = {player.u32};
}

void lollipop_unwatch_movie(PPCRegister& player, PPCRegister& unload) {
  if (unload.u32) lollipop::game_movie_unloaded(player.u32);
  if (!unload.u32 || !prompt_runtime.load()) return;
  std::lock_guard lock(movie_mutex);
  std::erase_if(movies, [&](const auto& movie) {
    return movie.second.player == player.u32;
  });
}
