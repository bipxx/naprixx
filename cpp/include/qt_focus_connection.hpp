#pragma once

#include <nanobind/nanobind.h>

namespace nx {
void bind_qt_focus_connection(nanobind::module_& module);
bool release_focus_connection(nanobind::handle application);
}
