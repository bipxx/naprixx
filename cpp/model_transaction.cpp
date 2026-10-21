#include "model_transaction.hpp"
#include "dims_namespace_owner.hpp"

#include <utility>

namespace nx {
namespace {
namespace nb = nanobind;
constexpr const char* owner_slot = "_naparixx_transaction_owner";

// The exact Pydantic private map is canonical storage, including the live
// transaction queue. Do not retain a second queue reference: external aliases
// may replace/delete a map entry directly, releasing its payload immediately.
struct ModelTransactionOwner {
    nb::object private_mapping;

    explicit ModelTransactionOwner(nb::object value)
        : private_mapping(std::move(value)) {}
    ModelTransactionOwner(ModelTransactionOwner&&) noexcept = default;
    ModelTransactionOwner(const ModelTransactionOwner&) = delete;
    ~ModelTransactionOwner() { clear(); }

    void replace(nb::object value, nb::handle model) {
        nb::object previous = std::move(private_mapping);
        private_mapping = std::move(value);
        dims_namespace_private_changed(model, private_mapping);
        // Publish before previous's finalizers can reenter the model facade.
    }
    void erase(nb::handle model) {
        nb::object previous = std::move(private_mapping);
        dims_namespace_private_changed(model, nb::handle());
    }
    void clear() noexcept {
        nb::object previous = std::move(private_mapping);
        // Detach the owning edge before releasing payloads. Never clear the
        // dictionary itself: saved external aliases still own its contents.
    }
};

int owner_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (!nb::inst_ready(nb::handle(self))) return 0;
    const auto* owner = nb::inst_ptr<ModelTransactionOwner>(nb::handle(self));
    Py_VISIT(owner->private_mapping.ptr());
    return 0;
}

int owner_clear(PyObject* self) {
    if (nb::inst_ready(nb::handle(self)))
        nb::inst_ptr<ModelTransactionOwner>(nb::handle(self))->clear();
    return 0;
}

PyType_Slot owner_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(owner_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(owner_clear)},
    {0, nullptr}
};

nb::object existing_owner(nb::handle model) {
    nb::object key = nb::str(owner_slot);
    nb::object result = nb::steal<nb::object>(
        PyObject_GenericGetAttr(model.ptr(), key.ptr()));
    if (!result.is_valid()) {
        if (!PyErr_ExceptionMatches(PyExc_AttributeError)) throw nb::python_error();
        PyErr_Clear();
    }
    return result;
}

ModelTransactionOwner* owned(nb::handle value) {
    if (!value.is_valid() || !nb::isinstance<ModelTransactionOwner>(value) ||
        !nb::inst_ready(value)) return nullptr;
    return nb::inst_ptr<ModelTransactionOwner>(value);
}

[[noreturn]] void missing_private(nb::handle model) {
    PyErr_Format(PyExc_AttributeError,
                 "'%.100s' object has no attribute '__pydantic_private__'",
                 Py_TYPE(model.ptr())->tp_name);
    throw nb::python_error();
}

nb::object get_private(nb::handle model) {
    nb::object holder = existing_owner(model);
    auto* owner = owned(holder);
    if (!owner || !owner->private_mapping.is_valid()) missing_private(model);
    nb::object mapping = owner->private_mapping;
    attach_transaction_diagnostics(mapping, holder);
    return mapping;
}

void set_private(nb::handle model, nb::object value) {
    nb::object holder = existing_owner(model);
    if (auto* owner = owned(holder)) {
        owner->replace(std::move(value), model);
        return;
    }
    nb::object replacement = nb::cast(ModelTransactionOwner(std::move(value)));
    nb::object key = nb::str(owner_slot);
    if (PyObject_GenericSetAttr(model.ptr(), key.ptr(), replacement.ptr()) < 0)
        throw nb::python_error();
}

void delete_private(nb::handle model) {
    nb::object holder = existing_owner(model);
    auto* owner = owned(holder);
    if (!owner || !owner->private_mapping.is_valid()) missing_private(model);
    owner->erase(model);
}
} // namespace

bool has_model_transaction_owner(nanobind::handle model) {
    return owned(existing_owner(model)) != nullptr;
}

void bind_model_transaction(nanobind::module_& module) {
    nb::class_<ModelTransactionOwner>(module, "ModelTransactionOwner",
            nb::is_weak_referenceable(), nb::type_slots(owner_slots))
        .def_prop_ro("private_mapping", [](const ModelTransactionOwner& owner) -> nb::object {
            return owner.private_mapping.is_valid() ? owner.private_mapping : nb::none();
        });
    module.def("model_private_get", &get_private);
    module.def("model_private_set", &set_private, nb::arg("model"), nb::arg("value").none());
    module.def("model_private_delete", &delete_private);
    module.def("model_transaction_owner", [](nb::handle model) {
        nb::object result = existing_owner(model);
        if (!owned(result)) missing_private(model);
        return result;
    });
}
} // namespace nx
