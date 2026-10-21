#include "event_state_owner.hpp"

#include <cstddef>
#include <structmember.h>

namespace nx {
namespace {
namespace nb = nanobind;

// One canonical instance dictionary retains emitter state, including the three
// subscription lists. Generic attributes and saved dictionaries share this
// storage; there is no subscription snapshot or auxiliary owning edge.
struct EventEmitterNamespace {
    PyObject_HEAD
    PyObject* dictionary;
    PyObject* weakrefs;
};

int namespace_traverse(PyObject* self, visitproc visit, void* arg) {
    const auto* owner = reinterpret_cast<const EventEmitterNamespace*>(self);
    Py_VISIT(owner->dictionary);
    Py_VISIT(Py_TYPE(self));
    // weakrefs is the non-owning CPython weak-reference list.
    return 0;
}

int namespace_clear(PyObject* self) {
    auto* owner = reinterpret_cast<EventEmitterNamespace*>(self);
    // Detach before payload finalizers run. Do not clear a saved dict itself.
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
    auto* owner = reinterpret_cast<EventEmitterNamespace*>(self);
    // NULL is deletion, including on an uninitialized object. Publish the new
    // dictionary before releasing the previous one and permitting reentry.
    Py_XSETREF(owner->dictionary, Py_XNewRef(value));
    return 0;
}

PyObject* namespace_get_weakref(PyObject* self, void*) {
    const auto* owner = reinterpret_cast<const EventEmitterNamespace*>(self);
    return Py_NewRef(owner->weakrefs ? owner->weakrefs : Py_None);
}

PyMemberDef namespace_members[] = {
    {const_cast<char*>("__dictoffset__"), T_PYSSIZET,
     offsetof(EventEmitterNamespace, dictionary), READONLY, nullptr},
    {const_cast<char*>("__weaklistoffset__"), T_PYSSIZET,
     offsetof(EventEmitterNamespace, weakrefs), READONLY, nullptr},
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

void bind_event_state_owner(nanobind::module_& module) {
    // Keep object allocation, subtype destruction and default copy/reduction.
    // A nanobind instance header here would change the default reduction layout
    // and object.__new__ contract of the public Python facade and its subtypes.
    PyType_Spec spec = {
        "naparixx._native.EventEmitterBase",
        static_cast<int>(sizeof(EventEmitterNamespace)),
        0,
        Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HAVE_GC,
        namespace_slots
    };
    nb::object type = nb::steal<nb::object>(PyType_FromSpec(&spec));
    if (!type.is_valid()) throw nb::python_error();
    module.attr("EventEmitterBase") = type;
}
} // namespace nx
