#pragma once

namespace rex {
namespace kernel {
class TitleRuntime;
}
}

namespace lollipop {
void install_live_prompt_updates(rex::kernel::TitleRuntime& runtime);
void stop_live_prompt_updates();
} // namespace lollipop
