#pragma once

// Native records own shared C++ state and an explicitly GC-traversed dictionary
// handle. The independent core contains no interpreter references.
#include "application.hpp"
#include "arrays.hpp"

namespace nx {
nb::object make_dims_dictionary_record(const app::DimsState& initial);
nb::object dims_dictionary_owner(nb::handle record);
void attach_dims_dictionary(nb::handle record, nb::dict dictionary);
void detach_dims_dictionary(nb::handle record);
void pause_dims_dictionary(nb::handle record, int depth);
void refresh_dims_dictionary(nb::handle record);
void sync_dims_dictionary_state(const app::DimsState& state);
void bind_dims_dictionary(nb::module_& module);
}
