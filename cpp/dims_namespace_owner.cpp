#include "dims_namespace_owner.hpp"
#include "dims_dictionary.hpp"
#include "dims_flags_bindings.hpp"

#include <nanobind/stl/string.h>
#include <array>
#include <cstring>
#include <exception>
#include <new>
#include <utility>

namespace nx {
namespace {
constexpr const char* storage_slot = "_naparixx_dims_owner";
constexpr const char* names[] = {
    "_native_state", "_native_candidate", "_native_boundary_depth",
    "_native_overrides", "_native_projection", "_native_dictionary_record",
    "_native_flag_record", "_native_flags"
};

struct DimsNamespaceOwner {
    std::array<nb::object, 8> values;

    DimsNamespaceOwner() {
        for (auto& value : values) value = nb::none();
        values[2] = nb::int_(0);
        values[3] = nb::dict();
    }
    DimsNamespaceOwner(DimsNamespaceOwner&&) noexcept = default;
    DimsNamespaceOwner(const DimsNamespaceOwner&) = delete;
    ~DimsNamespaceOwner() { clear(); }

    void clear() noexcept {
        // Publish the absence of every edge before any decref runs finalizers.
        auto previous = std::move(values);
    }
    nb::object get(unsigned index) const {
        return values[index].is_valid() ? values[index] : nb::none();
    }
};

unsigned index_of(nb::handle key) {
    if (PyUnicode_Check(key.ptr())) {
        const auto name = nb::cast<std::string>(key);
        for (unsigned index = 0; index < std::size(names); ++index)
            if (name == names[index]) return index;
    }
    PyErr_SetObject(PyExc_KeyError, key.ptr());
    throw nb::python_error();
}

nb::object existing_storage(nb::handle model) {
    nb::str name(storage_slot);
    auto value = nb::steal<nb::object>(PyObject_GenericGetAttr(model.ptr(), name.ptr()));
    if (!value.is_valid()) {
        if (!PyErr_ExceptionMatches(PyExc_AttributeError)) throw nb::python_error();
        PyErr_Clear();
    }
    return value;
}

DimsNamespaceOwner* owned(nb::handle storage) {
    if (!storage.is_valid() || !nb::isinstance<DimsNamespaceOwner>(storage) ||
        !nb::inst_ready(storage)) return nullptr;
    return nb::inst_ptr<DimsNamespaceOwner>(storage);
}

int storage_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(nb::handle(self)))
        for (const auto& value : nb::inst_ptr<DimsNamespaceOwner>(nb::handle(self))->values)
            Py_VISIT(value.ptr());
    return 0;
}
int storage_clear(PyObject* self) {
    if (nb::inst_ready(nb::handle(self)))
        nb::inst_ptr<DimsNamespaceOwner>(nb::handle(self))->clear();
    return 0;
}
PyType_Slot storage_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(storage_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(storage_clear)},
    {0, nullptr}
};

PyObject* get_storage(PyObject* model, void*) {
    try {
        nb::object storage = existing_storage(nb::handle(model));
        if (!owned(storage)) {
            PyErr_Format(PyExc_AttributeError, "'%.100s' object has no attribute '_native_storage'",
                         Py_TYPE(model)->tp_name);
            return nullptr;
        }
        return storage.release().ptr();
    } catch (nb::python_error& error) {
        error.restore();
        return nullptr;
    } catch (const std::bad_alloc&) {
        PyErr_NoMemory();
        return nullptr;
    } catch (const std::exception& error) {
        PyErr_SetString(PyExc_RuntimeError, error.what());
        return nullptr;
    }
}

int set_fields(PyObject* model, PyObject* value, void*) {
    if (value && !PyDict_Check(value)) {
        PyErr_Format(PyExc_TypeError, "__dict__ must be set to a dictionary, not a '%.200s'",
                     Py_TYPE(value)->tp_name);
        return -1;
    }
    try {
        // Preserve BaseModel's physical dictionary: Pydantic and generic field
        // lookup use this pointer, including validation's temporary map swaps.
        PyObject** pointer = _PyObject_GetDictPtr(model);
        if (!pointer) {
            if (!PyErr_Occurred()) PyErr_SetString(PyExc_AttributeError, "This object has no __dict__");
            return -1;
        }
        nb::object previous = nb::steal<nb::object>(*pointer);
        *pointer = Py_XNewRef(value);
        nb::object holder = existing_storage(nb::handle(model));
        if (auto* storage = owned(holder)) {
            nb::object old_projection = std::move(storage->values[4]);
            storage->values[4] = nb::none();
            nb::object record = storage->get(5);
            if (!record.is_none()) {
                if (value) {
                    attach_dims_dictionary(record, nb::borrow<nb::dict>(value));
                    auto& state = nb::cast<app::DimsState&>(dims_dictionary_owner(record));
                    for (unsigned index = 0; index < 10; ++index)
                        state.external(static_cast<app::DimsField>(index), true);
                    refresh_dims_dictionary(record);
                    storage->values[4] = nb::steal<nb::object>(PyDictProxy_New(value));
                    if (!storage->values[4].is_valid()) throw nb::python_error();
                } else detach_dims_dictionary(record);
            }
        }
        // 'previous' pins the old physical map until every native record/view
        // observes the published replacement. Explicit saved aliases survive.
        return 0;
    } catch (nb::python_error& error) {
        error.restore();
    } catch (const std::bad_alloc&) {
        PyErr_NoMemory();
    } catch (const std::exception& error) {
        PyErr_SetString(PyExc_RuntimeError, error.what());
    }
    return -1;
}

PyGetSetDef namespace_getsets[] = {
    {const_cast<char*>("__dict__"), PyObject_GenericGetDict, set_fields, nullptr, nullptr},
    {const_cast<char*>("_native_storage"), get_storage, nullptr, nullptr, nullptr},
    {nullptr, nullptr, nullptr, nullptr, nullptr}
};
} // namespace

nb::object dims_namespace_storage(nb::handle model) {
    nb::object existing = existing_storage(model);
    if (owned(existing)) return existing;
    nb::str private_name("__pydantic_private__");
    nb::object declared = nb::steal<nb::object>(PyObject_GenericGetAttr(model.ptr(), private_name.ptr()));
    if (!declared.is_valid()) {
        if (!PyErr_ExceptionMatches(PyExc_AttributeError)) throw nb::python_error();
        PyErr_Clear();
        return nb::none();
    }
    if (declared.is_none()) return nb::none();
    nb::object storage = nb::cast(DimsNamespaceOwner{});
    nb::str slot(storage_slot);
    if (PyObject_GenericSetAttr(model.ptr(), slot.ptr(), storage.ptr()) < 0)
        throw nb::python_error();
    return storage;
}

void dims_namespace_private_changed(nb::handle model, nb::handle mapping) {
    nb::object holder = existing_storage(model);
    auto* storage = owned(holder);
    if (!storage) return;
    nb::object record = mapping.is_valid() && PyDict_Check(mapping.ptr())
        ? dims_flag_record(nb::borrow<nb::dict>(mapping)) : nb::none();
    nb::object flags = record.is_none() ? nb::none() : dims_flag_owner(record);
    nb::object previous_record = std::move(storage->values[6]);
    nb::object previous_flags = std::move(storage->values[7]);
    storage->values[6] = std::move(record);
    storage->values[7] = std::move(flags);
    if (!storage->values[6].is_none()) refresh_dims_flag_record(storage->values[6]);
}

void install_dims_namespace(nb::handle type) {
    auto* model_type = reinterpret_cast<PyTypeObject*>(type.ptr());
    for (auto* entry = namespace_getsets; entry->name; ++entry) {
        auto descriptor = nb::steal<nb::object>(
            PyDescr_NewGetSet(model_type, entry));
        if (!descriptor.is_valid()) throw nb::python_error();
        // type.__dict__ itself is read-only through metaclass setattr. Install
        // this heap type's instance descriptors and invalidate lookup caches.
        if (PyDict_SetItemString(model_type->tp_dict, entry->name, descriptor.ptr()) < 0)
            throw nb::python_error();
    }
    PyType_Modified(model_type);
}

void bind_dims_namespace(nb::module_& module) {
    nb::class_<DimsNamespaceOwner>(module, "DimsNamespaceOwner",
            nb::type_slots(storage_slots), nb::is_weak_referenceable())
        .def("__getitem__", [](const DimsNamespaceOwner& storage, nb::handle key) {
            return storage.get(index_of(key));
        }, nb::arg("key").none())
        .def("__setitem__", [](DimsNamespaceOwner& storage, nb::handle key, nb::object value) {
            storage.values[index_of(key)] = std::move(value);
        }, nb::arg("key").none(), nb::arg("value").none());
}
} // namespace nx
