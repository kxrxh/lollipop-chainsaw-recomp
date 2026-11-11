#pragma once

#include <memory>

namespace rex::ui {
class WindowedApp;
class WindowedAppContext;
} // namespace rex::ui

namespace lollipop {
std::unique_ptr<rex::ui::WindowedApp> create_application(rex::ui::WindowedAppContext& context);
} // namespace lollipop
