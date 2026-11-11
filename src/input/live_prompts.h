#pragma once

namespace rex {
class Runtime;
}

namespace lollipop {
void install_live_prompt_updates(rex::Runtime& runtime);
void stop_live_prompt_updates();
} // namespace lollipop
