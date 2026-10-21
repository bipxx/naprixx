#pragma once

#include <nanobind/nanobind.h>

namespace nx {
void bind_transform_state_owner(nanobind::module_& module);
void bind_transform_family_policy(nanobind::module_& module);
void bind_transform_objects(nanobind::module_& module);
void bind_transform_family_backend(nanobind::module_& module);
}
