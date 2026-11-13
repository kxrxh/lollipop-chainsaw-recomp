#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <vector>

namespace lollipop {
class game_bridge;

enum class quit_dialog_state { idle, requested, opening, active, cancelled, confirmed, failed };
enum class quit_dialog_input { yes, no, accept, cancel, mouse };

class native_quit_dialog {
public:
  bool request();
  quit_dialog_state status() const;
  unsigned selection() const;
  void queue_input(quit_dialog_input input, float x = 0, float y = 0);
  void dismiss();
  void track_main_menu(uint32_t player);
  void untrack_main_menu(uint32_t player);
  void update(game_bridge& game);

private:
  struct control {
    quit_dialog_input input;
    float x, y;
  };
  std::atomic<quit_dialog_state> state_{};
  std::atomic<uint32_t> main_menu_{};
  std::atomic<unsigned> selection_{};
  uint32_t movie_{}, parent_{};
  std::chrono::steady_clock::time_point deadline_{};
  std::mutex input_mutex_;
  std::vector<control> inputs_;
};

native_quit_dialog& native_quit();
} // namespace lollipop
