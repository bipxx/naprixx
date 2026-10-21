#pragma once

#include <nanobind/nanobind.h>

namespace nx {
void bind_event_objects(nanobind::module_& module);
void bind_event_subscriptions(nanobind::module_& module);
void bind_event_delivery(nanobind::module_& module);
void bind_event_groups(nanobind::module_& module);
void bind_event_tracing(nanobind::module_& module);
} // namespace nx
