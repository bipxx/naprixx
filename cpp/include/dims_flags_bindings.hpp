#pragma once

#include "application.hpp"
#include "arrays.hpp"

namespace nx {
nb::object dims_flag_record(nb::dict dictionary);
nb::object dims_flag_owner(nb::handle record);
void refresh_dims_flag_record(nb::handle record);
int dims_flag_truth(nb::handle value);
void bind_dims_flags(nb::module_& module);
}
