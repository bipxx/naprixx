#pragma once

#include "arrays.hpp"

namespace nx {
// The supplied field/private maps remain exact Python protocol objects. Native
// storage owns implementation references; namespace publication updates them
// synchronously, before an obsolete payload can reenter the model.
nb::object dims_namespace_storage(nb::handle model);
void dims_namespace_private_changed(nb::handle model, nb::handle mapping);
void install_dims_namespace(nb::handle type);
void bind_dims_namespace(nb::module_& module);
}
