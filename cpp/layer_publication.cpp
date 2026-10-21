#include "arrays.hpp"

#include <array>
#include <cstddef>
#include <structmember.h>
#include <utility>

namespace nx {
namespace {

// The physical allocation owns the single live Layer or slicing-state
// dictionary before facade constructors write any fields. Generic Python
// attributes, descriptors and dictionary aliases use this same storage.
struct LayerNamespace {
    PyObject_HEAD
    PyObject* dictionary;
    PyObject* weakrefs;
};

int lp_namespace_traverse(PyObject* self, visitproc visit, void* arg) {
    const auto* owner = reinterpret_cast<const LayerNamespace*>(self);
    Py_VISIT(owner->dictionary);
    Py_VISIT(Py_TYPE(self));
    return 0;
}

int lp_namespace_clear(PyObject* self) {
    auto* owner = reinterpret_cast<LayerNamespace*>(self);
    Py_CLEAR(owner->dictionary);
    return 0;
}

int lp_namespace_set_dict(PyObject* self, PyObject* value, void*) {
    if (value && !PyDict_Check(value)) {
        PyErr_Format(PyExc_TypeError,
                     "__dict__ must be set to a dictionary, not a '%.200s'",
                     Py_TYPE(value)->tp_name);
        return -1;
    }
    auto* owner = reinterpret_cast<LayerNamespace*>(self);
    Py_XSETREF(owner->dictionary, Py_XNewRef(value));
    return 0;
}

PyObject* lp_namespace_weakrefs(PyObject* self, void*) {
    const auto* owner = reinterpret_cast<const LayerNamespace*>(self);
    return Py_NewRef(owner->weakrefs ? owner->weakrefs : Py_None);
}

PyMemberDef lp_namespace_members[] = {
    {const_cast<char*>("__dictoffset__"), T_PYSSIZET,
     offsetof(LayerNamespace, dictionary), READONLY, nullptr},
    {const_cast<char*>("__weaklistoffset__"), T_PYSSIZET,
     offsetof(LayerNamespace, weakrefs), READONLY, nullptr},
    {nullptr, 0, 0, 0, nullptr}
};

PyGetSetDef lp_namespace_getsets[] = {
    {const_cast<char*>("__dict__"), PyObject_GenericGetDict,
     lp_namespace_set_dict, nullptr, nullptr},
    {const_cast<char*>("__weakref__"), lp_namespace_weakrefs,
     nullptr, nullptr, nullptr},
    {nullptr, nullptr, nullptr, nullptr, nullptr}
};

PyType_Slot lp_namespace_slots[] = {
    {Py_tp_members, lp_namespace_members},
    {Py_tp_getset, lp_namespace_getsets},
    {Py_tp_traverse, reinterpret_cast<void*>(lp_namespace_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(lp_namespace_clear)},
    {0, nullptr}
};

nb::object lp_checked(PyObject* value) {
    if (!value) throw nb::python_error();
    return nb::steal<nb::object>(value);
}

nb::object lp_attr(nb::handle value, const char* name) {
    return lp_checked(PyObject_GetAttrString(value.ptr(), name));
}

nb::object lp_item(nb::handle value, nb::handle key) {
    return lp_checked(PyObject_GetItem(value.ptr(), key.ptr()));
}

nb::object lp_call0(nb::object callable) {
    return lp_checked(PyObject_CallNoArgs(callable.ptr()));
}

nb::object lp_call1(nb::object callable, nb::object value) {
    return lp_checked(PyObject_CallOneArg(callable.ptr(), value.ptr()));
}

nb::object lp_call2(nb::object callable, nb::object a, nb::object b) {
    return lp_checked(PyObject_CallFunctionObjArgs(
        callable.ptr(), a.ptr(), b.ptr(), nullptr));
}

nb::object lp_call_kw(nb::object callable, nb::tuple args, nb::dict kwargs) {
    return lp_checked(PyObject_Call(callable.ptr(), args.ptr(), kwargs.ptr()));
}

nb::object lp_global(nb::dict& globals, const char* name) {
    nb::str key(name);
    PyObject* value = PyDict_GetItemWithError(globals.ptr(), key.ptr());
    if (value) return nb::borrow<nb::object>(value);
    if (PyErr_Occurred()) throw nb::python_error();
    value = PyObject_GetItem(PyEval_GetBuiltins(), key.ptr());
    if (value) return nb::steal<nb::object>(value);
    if (!PyErr_ExceptionMatches(PyExc_KeyError)) throw nb::python_error();
    PyErr_Clear();
    nb::object message = lp_checked(PyUnicode_FromFormat("name '%s' is not defined", name));
    nb::object error = lp_call1(nb::borrow<nb::object>(PyExc_NameError), std::move(message));
    if (PyObject_SetAttrString(error.ptr(), "name", key.ptr()) < 0) throw nb::python_error();
    PyErr_SetObject(PyExc_NameError, error.ptr());
    throw nb::python_error();
}

bool lp_truth(nb::object value) {
    const int result = PyObject_IsTrue(value.ptr());
    if (result < 0) throw nb::python_error();
    return result != 0;
}

void lp_replace(nb::object& field, nb::object value) {
    nb::object old = std::move(field);
    field = std::move(value);
    old.reset();
}

// Original assigned locals are separate from persistent Layer state. The
// facade transfers its reassignable thumbnail parameter before entering the
// complete operation. Every edge is visible to cyclic GC and is released in
// original local order, after detaching the complete carrier.
struct LayerPublicationLocals {
    nb::object thumbnail, padding_needed, pad_amounts, background, f_dest, f_source;
    LayerPublicationLocals() = default;
    LayerPublicationLocals(LayerPublicationLocals&&) = default;
    ~LayerPublicationLocals() { clear(); }
    void clear() {
        std::array<nb::object, 6> previous = {
            std::move(thumbnail), std::move(padding_needed), std::move(pad_amounts),
            std::move(background), std::move(f_dest), std::move(f_source),
        };
        for (auto& value : previous) value.reset();
    }
};

int lp_locals_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) {
        const auto& state = *nb::inst_ptr<LayerPublicationLocals>(self);
        Py_VISIT(state.thumbnail.ptr());
        Py_VISIT(state.padding_needed.ptr());
        Py_VISIT(state.pad_amounts.ptr());
        Py_VISIT(state.background.ptr());
        Py_VISIT(state.f_dest.ptr());
        Py_VISIT(state.f_source.ptr());
    }
    return 0;
}

int lp_locals_clear(PyObject* self) {
    if (nb::inst_ready(self)) nb::inst_ptr<LayerPublicationLocals>(self)->clear();
    return 0;
}

PyType_Slot lp_locals_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(lp_locals_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(lp_locals_clear)},
    {0, nullptr}
};

nb::object lp_optional(const nb::object& value) {
    return value.is_valid() ? value : nb::none();
}

void layer_slicing_set_loaded(nb::handle self, nb::object loaded) {
    if (lp_truth(lp_call1(lp_attr(lp_attr(self, "_load_state"), "set_loaded"),
                          std::move(loaded)))) {
        lp_call0(lp_attr(lp_attr(self, "loaded_data"), "emit"));
    }
}

nb::object layer_thumbnail_get(nb::handle self) {
    return lp_attr(self, "_thumbnail");
}

nb::object lp_padding(nb::object padding_needed) {
    nb::object iterator = lp_checked(PyObject_GetIter(padding_needed.ptr()));
    padding_needed.reset();
    nb::list amounts;
    nb::object item;
    while (true) {
        nb::object next = nb::steal<nb::object>(PyIter_Next(iterator.ptr()));
        if (!next.is_valid()) {
            if (PyErr_Occurred()) throw nb::python_error();
            break;
        }
        lp_replace(item, std::move(next));
        nb::object left = lp_checked(PyNumber_FloorDivide(item.ptr(), nb::int_(2).ptr()));
        nb::object plus = lp_checked(PyNumber_Add(item.ptr(), nb::int_(1).ptr()));
        nb::object right = lp_checked(PyNumber_FloorDivide(plus.ptr(), nb::int_(2).ptr()));
        nb::tuple pair = nb::make_tuple(left, right);
        if (PyList_Append(amounts.ptr(), pair.ptr()) < 0) throw nb::python_error();
    }
    return amounts;
}

nb::object lp_zeros(nb::handle self, nb::dict& globals) {
    nb::object zeros = lp_attr(lp_global(globals, "np"), "zeros");
    nb::object shape = lp_attr(self, "_thumbnail_shape");
    nb::object dtype = lp_attr(lp_global(globals, "np"), "uint8");
    nb::dict kwargs;
    kwargs["dtype"] = dtype;
    return lp_call_kw(std::move(zeros), nb::make_tuple(shape), std::move(kwargs));
}

void layer_thumbnail_set(nb::handle self, nb::dict globals, LayerPublicationLocals& owner) {
    nb::object shape = lp_attr(owner.thumbnail, "shape");
    const int empty = PySequence_Contains(shape.ptr(), nb::int_(0).ptr());
    if (empty < 0) throw nb::python_error();
    shape.reset();
    if (empty) lp_replace(owner.thumbnail, lp_zeros(self, globals));

    nb::object dtype = lp_attr(owner.thumbnail, "dtype");
    nb::object uint8 = lp_attr(lp_global(globals, "np"), "uint8");
    nb::object different = lp_checked(PyObject_RichCompare(dtype.ptr(), uint8.ptr(), Py_NE));
    dtype.reset();
    uint8.reset();
    if (lp_truth(std::move(different))) {
        lp_replace(owner.thumbnail,
            lp_call1(lp_global(globals, "convert_to_uint8"), owner.thumbnail));
    }

    nb::object subtract = lp_attr(lp_global(globals, "np"), "subtract");
    nb::object target_shape = lp_attr(self, "_thumbnail_shape");
    nb::object actual_shape = lp_attr(owner.thumbnail, "shape");
    lp_replace(owner.padding_needed,
        lp_call2(std::move(subtract), std::move(target_shape), std::move(actual_shape)));
    lp_replace(owner.pad_amounts, lp_padding(owner.padding_needed));

    nb::object pad = lp_attr(lp_global(globals, "np"), "pad");
    nb::dict kwargs;
    kwargs["mode"] = nb::str("constant");
    lp_replace(owner.thumbnail,
        lp_call_kw(std::move(pad), nb::make_tuple(owner.thumbnail, owner.pad_amounts),
                   std::move(kwargs)));
    lp_replace(owner.background, lp_zeros(self, globals));
    nb::tuple alpha_index = nb::make_tuple(nb::borrow<nb::object>(Py_Ellipsis), nb::int_(3));
    if (PyObject_SetItem(owner.background.ptr(), alpha_index.ptr(), nb::int_(255).ptr()) < 0)
        throw nb::python_error();

    nb::object alpha = lp_item(owner.thumbnail, alpha_index);
    nb::tuple expand_index = nb::make_tuple(nb::borrow<nb::object>(Py_Ellipsis), nb::none());
    nb::object expanded = lp_item(alpha, expand_index);
    alpha.reset();
    lp_replace(owner.f_dest, lp_checked(PyNumber_TrueDivide(expanded.ptr(), nb::int_(255).ptr())));
    expanded.reset();
    lp_replace(owner.f_source, lp_checked(PyNumber_Subtract(nb::int_(1).ptr(), owner.f_dest.ptr())));
    nb::object dest_product = lp_checked(PyNumber_Multiply(owner.thumbnail.ptr(), owner.f_dest.ptr()));
    nb::object source_product = lp_checked(PyNumber_Multiply(owner.background.ptr(), owner.f_source.ptr()));
    nb::object blended = lp_checked(PyNumber_Add(dest_product.ptr(), source_product.ptr()));
    dest_product.reset();
    source_product.reset();
    lp_replace(owner.thumbnail, std::move(blended));

    nb::object astype = lp_attr(owner.thumbnail, "astype");
    nb::object result = lp_call1(std::move(astype), lp_attr(lp_global(globals, "np"), "uint8"));
    if (PyObject_SetAttrString(self.ptr(), "_thumbnail", result.ptr()) < 0) throw nb::python_error();
    result.reset();
    lp_call0(lp_attr(lp_attr(self, "events"), "thumbnail"));
}

} // namespace

void bind_layer_publication(nb::module_& module) {
    PyType_Spec spec = {
        "naparixx._native.LayerNamespaceBase",
        static_cast<int>(sizeof(LayerNamespace)),
        0,
        Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HAVE_GC,
        lp_namespace_slots
    };
    nb::object type = nb::steal<nb::object>(PyType_FromSpec(&spec));
    if (!type.is_valid()) throw nb::python_error();
    module.attr("LayerNamespaceBase") = type;

    nb::class_<LayerPublicationLocals>(module, "LayerPublicationLocals",
        nb::is_weak_referenceable(), nb::type_slots(lp_locals_slots))
        .def_prop_ro("thumbnail", [](const LayerPublicationLocals& s) { return lp_optional(s.thumbnail); })
        .def_prop_ro("padding_needed", [](const LayerPublicationLocals& s) { return lp_optional(s.padding_needed); })
        .def_prop_ro("pad_amounts", [](const LayerPublicationLocals& s) { return lp_optional(s.pad_amounts); })
        .def_prop_ro("background", [](const LayerPublicationLocals& s) { return lp_optional(s.background); })
        .def_prop_ro("f_dest", [](const LayerPublicationLocals& s) { return lp_optional(s.f_dest); })
        .def_prop_ro("f_source", [](const LayerPublicationLocals& s) { return lp_optional(s.f_source); });
    module.def("layer_publication_locals", [](nb::object thumbnail) {
        LayerPublicationLocals owner;
        owner.thumbnail = std::move(thumbnail);
        return owner;
    }, nb::arg("thumbnail").none());
    module.def("layer_slicing_set_loaded", &layer_slicing_set_loaded,
        nb::arg("self").none(), nb::arg("loaded").none());
    module.def("layer_thumbnail_get", &layer_thumbnail_get, nb::arg("self").none());
    module.def("layer_thumbnail_set", &layer_thumbnail_set,
        nb::arg("self").none(), nb::arg("namespace"), nb::arg("locals_owner"));
}
} // namespace nx
