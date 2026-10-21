#include "vispy_controller_state.hpp"

#include <cstddef>
#include <structmember.h>

namespace nx {
namespace {
namespace nb = nanobind;

// The single exact instance dictionary owns controller and layer-node state.
// Generic attributes, descriptors, vars() and saved __dict__ aliases all use
// this storage. It exists in the allocation layout before any facade __init__.
// No per-field mirror, callback registry or nanobind instance header is added.
struct VispyControllerNamespace {
    PyObject_HEAD
    PyObject* dictionary;
    PyObject* weakrefs;
};

int namespace_traverse(PyObject* self, visitproc visit, void* arg) {
    const auto* owner = reinterpret_cast<const VispyControllerNamespace*>(self);
    Py_VISIT(owner->dictionary);
    Py_VISIT(Py_TYPE(self));
    // CPython's weak-reference list does not own its referents.
    return 0;
}

int namespace_clear(PyObject* self) {
    auto* owner = reinterpret_cast<VispyControllerNamespace*>(self);
    // Detach the edge before decref can run payload finalizers. An externally
    // retained dictionary remains intact, with all of its original aliases.
    Py_CLEAR(owner->dictionary);
    return 0;
}

int namespace_set_dictionary(PyObject* self, PyObject* value, void*) {
    if (value && !PyDict_Check(value)) {
        PyErr_Format(PyExc_TypeError,
                     "__dict__ must be set to a dictionary, not a '%.200s'",
                     Py_TYPE(value)->tp_name);
        return -1;
    }
    auto* owner = reinterpret_cast<VispyControllerNamespace*>(self);
    // NULL deletes the live dictionary, including before initialization.
    // Publish its replacement before releasing the prior owning edge.
    Py_XSETREF(owner->dictionary, Py_XNewRef(value));
    return 0;
}

PyObject* namespace_get_weakref(PyObject* self, void*) {
    const auto* owner = reinterpret_cast<const VispyControllerNamespace*>(self);
    return Py_NewRef(owner->weakrefs ? owner->weakrefs : Py_None);
}

PyMemberDef namespace_members[] = {
    {const_cast<char*>("__dictoffset__"), T_PYSSIZET,
     offsetof(VispyControllerNamespace, dictionary), READONLY, nullptr},
    {const_cast<char*>("__weaklistoffset__"), T_PYSSIZET,
     offsetof(VispyControllerNamespace, weakrefs), READONLY, nullptr},
    {nullptr, 0, 0, 0, nullptr}
};

PyGetSetDef namespace_getsets[] = {
    {const_cast<char*>("__dict__"), PyObject_GenericGetDict,
     namespace_set_dictionary, nullptr, nullptr},
    {const_cast<char*>("__weakref__"), namespace_get_weakref,
     nullptr, nullptr, nullptr},
    {nullptr, nullptr, nullptr, nullptr, nullptr}
};

PyType_Slot namespace_slots[] = {
    {Py_tp_members, namespace_members},
    {Py_tp_getset, namespace_getsets},
    {Py_tp_traverse, reinterpret_cast<void*>(namespace_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(namespace_clear)},
    {0, nullptr}
};
} // namespace

void bind_vispy_controller_state(nanobind::module_& module) {
    // Inherit object allocation/init, generic descriptor-aware attributes,
    // subtype destruction and default copy/reduction. Facade constructors and
    // cooperative super() continue to run normally above this CPython heap type.
    PyType_Spec spec = {
        "naparixx._native.VispyNamespaceBase",
        static_cast<int>(sizeof(VispyControllerNamespace)),
        0,
        Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HAVE_GC,
        namespace_slots
    };
    nb::object type = nb::steal<nb::object>(PyType_FromSpec(&spec));
    if (!type.is_valid()) throw nb::python_error();
    module.attr("VispyNamespaceBase") = type;
}
} // namespace nx
