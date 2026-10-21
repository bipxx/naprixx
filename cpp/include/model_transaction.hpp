#pragma once

#include <nanobind/nanobind.h>

namespace nx {
void bind_model_transaction(nanobind::module_& module);
bool has_model_transaction_owner(nanobind::handle model);
void attach_transaction_diagnostics(nanobind::handle mapping, nanobind::handle owner);
}
