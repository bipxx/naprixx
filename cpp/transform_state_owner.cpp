#include <nanobind/nanobind.h>

#include <cstddef>
#include <structmember.h>

// STAGED STORAGE IMPLEMENTATION ONLY: this file is not a build input or an
// accepted Transform-family conversion. No application source is changed.
//
// Source contract
//  * Transform remains the genuine Python API class with its original methods,
//    signatures, class dictionaries and psygnal Signal descriptor. Its proposed
//    sole native base is TransformNamespaceBase. ScaleTranslate, Affine and
//    CompositeAffine inherit that same allocation; TransformChain retains
//    EventedList[_T], Transform, Generic[_T] and ordinary cooperative MRO.
//  * The exact live dictionary is the only authority for every original field:
//    func, _inverse_func, name, _cache_dict; scale/translate; Affine/Composite
//    fields; and Chain's _list, _basetypes, _lookup, events and plugin attributes.
//    Original arrays, list payloads and dictionaries retain their identities.
//    Generic attributes, descriptors, vars(), __dict__, saved aliases and dict
//    subclass replacements must all address this one owning pointer. There is
//    no per-field mirror, native array/vector snapshot or callback registry.
//  * A native allocation contains the dictionary/weakref slots before __init__.
//    Dictionary payload creation remains CPython's normal lazy operation. This
//    base intercepts neither construction nor initialization and adds no
//    __getattribute__, __setattr__, __new__, __init__, copy or reduction policy.
//    object.__new__, explicit allocation without init, subclass slots/hooks,
//    descriptor failures and inherited copy/deepcopy/pickle paths remain normal.
//    Restore retains actual field/cache graph aliases; the original success or
//    error is the oracle, including unpicklable original lookup callbacks.
//  * Replacement publishes the new pointer before releasing the previous edge.
//    Deletion and GC detach the pointer without clearing a retained dictionary
//    payload. GC visits that dictionary and the actual heap type; weakrefs are
//    nonowning. Inherited CPython allocation/subtype destruction handles the
//    ordinary weakref, subclass finalizer and resurrection protocols.
//  * Executing-global lookup, deferred genuine lexical super, adapter/dispatcher
//    policy, exact numerical/provider domains and private frame/code contracts
//    remain named work in the complete owner. This storage draft closes none of
//    those responsibilities and makes no installed compatibility claim.
//
// Exact proposed integration changes (not performed by this staging file)
//  1. Promote this file to cpp/transform_state_owner.cpp without changing its
//     implementation; add that one TU to the native target in CMakeLists.txt.
//  2. Declare void nx::bind_transform_state_owner(nanobind::module_&) in the
//     root-owned cpp/include/transform_bindings.hpp.
//  3. Call nx::bind_transform_state_owner(m) in cpp/module.cpp before
//     nx::bind_transform_objects(m). This publishes the type without importing
//     the application, registering callbacks or creating transform instances.
//  4. Change only the base list to
//         class Transform(_native.TransformNamespaceBase):
//     in src/napari/utils/transforms/transforms.py. Keep the other four class
//     bases, Signal descriptor, source methods and metadata intact for this
//     allocation change; wider complete-operation edits have separate scope.
//  5. Add TransformNamespaceBase with __dict__: dict[str, Any] and __weakref__: Any
//     to src/naparixx/_native.pyi. There are no callable exports or field APIs.
//  6. Require installed allocation/Generic/Chain cooperation, live-dictionary,
//     alias, descriptor, copy/restore and weakref/GC comparisons before accepting
//     this physical authority. Reuse the existing native operations and math.

namespace nx {
namespace {
namespace nb = nanobind;

// Exact policy-free physical layout used by the accepted Vispy namespace and
// MultiScale namespace owners. This is a CPython heap type, not an nb instance.
struct TransformNamespace {
    PyObject_HEAD
    PyObject* dictionary;
    PyObject* weakrefs;
};

int transform_namespace_traverse(PyObject* self, visitproc visit, void* arg) {
    const auto* owner = reinterpret_cast<const TransformNamespace*>(self);
    Py_VISIT(owner->dictionary);
    Py_VISIT(Py_TYPE(self));
    return 0;
}

int transform_namespace_clear(PyObject* self) {
    // Py_CLEAR detaches the owning edge before payload finalizers can run.
    // An externally retained dictionary and every entry/alias remain intact.
    Py_CLEAR(reinterpret_cast<TransformNamespace*>(self)->dictionary);
    return 0;
}

int transform_namespace_set_dictionary(PyObject* self, PyObject* value, void*) {
    if (value && !PyDict_Check(value)) {
        PyErr_Format(PyExc_TypeError,
                     "__dict__ must be set to a dictionary, not a '%.200s'",
                     Py_TYPE(value)->tp_name);
        return -1;
    }
    auto* owner = reinterpret_cast<TransformNamespace*>(self);
    // NULL implements normal deletion. Py_XSETREF publishes the replacement
    // before its old edge is released; exact dictionaries/subclasses are kept.
    Py_XSETREF(owner->dictionary, Py_XNewRef(value));
    return 0;
}

PyObject* transform_namespace_get_weakref(PyObject* self, void*) {
    const auto* owner = reinterpret_cast<const TransformNamespace*>(self);
    return Py_NewRef(owner->weakrefs ? owner->weakrefs : Py_None);
}

PyMemberDef transform_namespace_members[] = {
    {const_cast<char*>("__dictoffset__"), T_PYSSIZET,
     offsetof(TransformNamespace, dictionary), READONLY, nullptr},
    {const_cast<char*>("__weaklistoffset__"), T_PYSSIZET,
     offsetof(TransformNamespace, weakrefs), READONLY, nullptr},
    {nullptr, 0, 0, 0, nullptr},
};

PyGetSetDef transform_namespace_getsets[] = {
    {const_cast<char*>("__dict__"), PyObject_GenericGetDict,
     transform_namespace_set_dictionary, nullptr, nullptr},
    {const_cast<char*>("__weakref__"), transform_namespace_get_weakref,
     nullptr, nullptr, nullptr},
    {nullptr, nullptr, nullptr, nullptr, nullptr},
};

PyType_Slot transform_namespace_slots[] = {
    {Py_tp_members, transform_namespace_members},
    {Py_tp_getset, transform_namespace_getsets},
    {Py_tp_traverse, reinterpret_cast<void*>(transform_namespace_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(transform_namespace_clear)},
    {0, nullptr},
};
} // namespace

void bind_transform_state_owner(nanobind::module_& module) {
    // Inherit object allocation/init, descriptor-aware generic attributes,
    // subtype destruction and default copy/reduction; add physical storage only.
    PyType_Spec spec = {
        "naparixx._native.TransformNamespaceBase",
        static_cast<int>(sizeof(TransformNamespace)),
        0,
        Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HAVE_GC,
        transform_namespace_slots,
    };
    nb::object type = nb::steal<nb::object>(PyType_FromSpec(&spec));
    if (!type.is_valid()) throw nb::python_error();
    module.attr("TransformNamespaceBase") = type;
}
} // namespace nx
