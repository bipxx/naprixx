#include "arrays.hpp"
#include "collection_protocol.hpp"

#include <array>
#include <cstddef>
#include <structmember.h>
#include <utility>

namespace nx {
namespace {

nb::object md_checked(PyObject* value) {
    if (!value) throw nb::python_error();
    return nb::steal<nb::object>(value);
}
nb::object md_attr(nb::object value, const char* name) {
    return md_checked(PyObject_GetAttrString(value.ptr(), name));
}
nb::object md_item(nb::object value, nb::object key) {
    return md_checked(PyObject_GetItem(value.ptr(), key.ptr()));
}
nb::object md_call0(nb::object callback) {
    return md_checked(PyObject_CallNoArgs(callback.ptr()));
}
nb::object md_call1(nb::object callback, nb::object value) {
    return md_checked(PyObject_CallOneArg(callback.ptr(), value.ptr()));
}
nb::object md_call2(nb::object callback, nb::object a, nb::object b) {
    return md_checked(PyObject_CallFunctionObjArgs(callback.ptr(), a.ptr(), b.ptr(), nullptr));
}
nb::object md_call3(nb::object callback, nb::object a, nb::object b, nb::object c) {
    return md_checked(PyObject_CallFunctionObjArgs(callback.ptr(), a.ptr(), b.ptr(), c.ptr(), nullptr));
}
bool md_truth(nb::object value) {
    const int result = PyObject_IsTrue(value.ptr());
    if (result < 0) throw nb::python_error();
    return result != 0;
}
void md_replace(nb::object& destination, nb::object value) {
    nb::object previous = std::move(destination);
    destination = std::move(value);
    previous.reset();
}
nb::object md_optional(const nb::object& value) {
    return value.is_valid() ? value : nb::none();
}
[[noreturn]] void md_raise(nb::object exception) {
    if (PyExceptionClass_Check(exception.ptr())) exception = md_call0(std::move(exception));
    if (!PyExceptionInstance_Check(exception.ptr()))
        throw nb::type_error("exceptions must derive from BaseException");
    collections::raise_exception(exception, false);
}

struct MultiScaleNamespace {
    nb::dict globals;
    nb::object builtins;
    explicit MultiScaleNamespace(nb::dict value)
        : globals(std::move(value)), builtins(nb::borrow<nb::object>(PyEval_GetBuiltins())) {}

    nb::object from_builtins(nb::object key, const char* name) const {
        PyObject* value = PyObject_GetItem(builtins.ptr(), key.ptr());
        if (value) return nb::steal<nb::object>(value);
        if (!PyErr_ExceptionMatches(PyExc_KeyError)) throw nb::python_error();
        PyErr_Clear();
        nb::object message = md_checked(PyUnicode_FromFormat("name '%s' is not defined", name));
        nb::object error = md_call1(nb::borrow<nb::object>(PyExc_NameError), std::move(message));
        if (PyObject_SetAttrString(error.ptr(), "name", key.ptr()) < 0) throw nb::python_error();
        PyErr_SetObject(PyExc_NameError, error.ptr());
        throw nb::python_error();
    }
    nb::object get(const char* name) const {
        nb::object key = nb::str(name);
        PyObject* value = PyDict_GetItemWithError(globals.ptr(), key.ptr());
        if (value) return nb::borrow<nb::object>(value);
        if (PyErr_Occurred()) throw nb::python_error();
        return from_builtins(std::move(key), name);
    }
    // The two source module statements use LOAD_NAME rather than a function's
    // LOAD_GLOBAL. The real module dictionary remains their publication target.
    nb::object name(const char* text) const {
        nb::object key = nb::str(text);
        PyObject* value = PyObject_GetItem(globals.ptr(), key.ptr());
        if (value) return nb::steal<nb::object>(value);
        if (!PyErr_ExceptionMatches(PyExc_KeyError)) throw nb::python_error();
        PyErr_Clear();
        return from_builtins(std::move(key), text);
    }
};

// Policy-free physical allocation: ordinary attributes and all saved
// vars()/__dict__ aliases share this exact dictionary. The source list(data)
// result is the only backing payload; there is no vector or field mirror.
struct MultiScaleStorage {
    PyObject_HEAD
    PyObject* dictionary;
    PyObject* weakrefs;
};
int md_namespace_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(reinterpret_cast<MultiScaleStorage*>(self)->dictionary);
    Py_VISIT(Py_TYPE(self));
    return 0;
}
int md_namespace_clear(PyObject* self) {
    // Detach the owning edge without emptying a retained dictionary payload.
    Py_CLEAR(reinterpret_cast<MultiScaleStorage*>(self)->dictionary);
    return 0;
}
int md_namespace_setdict(PyObject* self, PyObject* value, void*) {
    if (value && !PyDict_Check(value)) {
        PyErr_Format(PyExc_TypeError,
                     "__dict__ must be set to a dictionary, not a '%.200s'",
                     Py_TYPE(value)->tp_name);
        return -1;
    }
    auto* owner = reinterpret_cast<MultiScaleStorage*>(self);
    Py_XSETREF(owner->dictionary, Py_XNewRef(value));
    return 0;
}
PyObject* md_namespace_weakref(PyObject* self, void*) {
    const auto* owner = reinterpret_cast<const MultiScaleStorage*>(self);
    return Py_NewRef(owner->weakrefs ? owner->weakrefs : Py_None);
}
PyMemberDef md_namespace_members[] = {
    {const_cast<char*>("__dictoffset__"), T_PYSSIZET,
     offsetof(MultiScaleStorage, dictionary), READONLY, nullptr},
    {const_cast<char*>("__weaklistoffset__"), T_PYSSIZET,
     offsetof(MultiScaleStorage, weakrefs), READONLY, nullptr},
    {nullptr, 0, 0, 0, nullptr},
};
PyGetSetDef md_namespace_getsets[] = {
    {const_cast<char*>("__dict__"), PyObject_GenericGetDict, md_namespace_setdict, nullptr, nullptr},
    {const_cast<char*>("__weakref__"), md_namespace_weakref, nullptr, nullptr, nullptr},
    {nullptr, nullptr, nullptr, nullptr, nullptr},
};
PyType_Slot md_namespace_slots[] = {
    {Py_tp_members, md_namespace_members},
    {Py_tp_getset, md_namespace_getsets},
    {Py_tp_traverse, reinterpret_cast<void*>(md_namespace_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(md_namespace_clear)},
    {0, nullptr},
};

// Only original assigned locals survive through a saved facade traceback.
// The transient iterator, callable and self/namespace are not carrier roots.
struct MultiScaleInitLocals {
    nb::object d;
    MultiScaleInitLocals() = default;
    MultiScaleInitLocals(MultiScaleInitLocals&&) = default;
    ~MultiScaleInitLocals() { clear(); }
    void clear() {
        nb::object previous = std::move(d);
        previous.reset();
    }
};
struct LayerDataProtocolErrorLocals {
    nb::object annotations, needed, missing, message;
    LayerDataProtocolErrorLocals() = default;
    LayerDataProtocolErrorLocals(LayerDataProtocolErrorLocals&&) = default;
    ~LayerDataProtocolErrorLocals() { clear(); }
    void clear() {
        std::array<nb::object, 4> previous = {
            std::move(annotations), std::move(needed), std::move(missing), std::move(message),
        };
        for (auto& value : previous) value.reset();
    }
};
int md_init_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) Py_VISIT(nb::inst_ptr<MultiScaleInitLocals>(self)->d.ptr());
    return 0;
}
int md_error_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) {
        const auto& owner = *nb::inst_ptr<LayerDataProtocolErrorLocals>(self);
        Py_VISIT(owner.annotations.ptr());
        Py_VISIT(owner.needed.ptr());
        Py_VISIT(owner.missing.ptr());
        Py_VISIT(owner.message.ptr());
    }
    return 0;
}
template <typename Owner> int md_locals_clear(PyObject* self) {
    if (nb::inst_ready(self)) nb::inst_ptr<Owner>(self)->clear();
    return 0;
}
PyType_Slot md_init_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(md_init_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(md_locals_clear<MultiScaleInitLocals>)},
    {0, nullptr},
};
PyType_Slot md_error_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(md_error_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(md_locals_clear<LayerDataProtocolErrorLocals>)},
    {0, nullptr},
};

void multiscale_init(nb::object self, nb::object data, nb::dict namespace_, MultiScaleInitLocals& owner) {
    MultiScaleNamespace ns(std::move(namespace_));
    nb::object list = ns.get("list");
    nb::object payload = md_call1(std::move(list), data);
    const int stored = PyObject_SetAttrString(self.ptr(), "_data", payload.ptr());
    payload.reset();
    if (stored < 0) throw nb::python_error();

    if (!md_truth(md_attr(self, "_data"))) {
        nb::object error_type = ns.get("ValueError");
        md_raise(md_call1(std::move(error_type), nb::str("Multiscale data must be a (non-empty) sequence")));
    }

    nb::object backing = md_attr(self, "_data");
    nb::object iterator = md_checked(PyObject_GetIter(backing.ptr()));
    backing.reset();
    while (true) {
        PyObject* next = PyIter_Next(iterator.ptr());
        if (!next) {
            if (PyErr_Occurred()) throw nb::python_error();
            break;
        }
        md_replace(owner.d, nb::steal<nb::object>(next));
        md_call1(ns.get("assert_protocol"), owner.d);
    }
}

nb::object multiscale_size(nb::object self) {
    nb::object data = md_attr(std::move(self), "_data");
    nb::object first = md_item(std::move(data), nb::int_(0));
    return md_attr(std::move(first), "size");
}
nb::object multiscale_ndim(nb::object self) {
    nb::object data = md_attr(std::move(self), "_data");
    nb::object first = md_item(std::move(data), nb::int_(0));
    return md_attr(std::move(first), "ndim");
}
nb::object multiscale_eq(nb::object self, nb::object other) {
    nb::object data = md_attr(std::move(self), "_data");
    return md_checked(PyObject_RichCompare(data.ptr(), other.ptr(), Py_EQ));
}
nb::object multiscale_add(nb::object self, nb::object other) {
    nb::object data = md_attr(std::move(self), "_data");
    return md_checked(PyNumber_Add(data.ptr(), other.ptr()));
}
nb::object multiscale_mul(nb::object self, nb::object other) {
    nb::object data = md_attr(std::move(self), "_data");
    return md_checked(PyNumber_Multiply(data.ptr(), other.ptr()));
}
nb::object multiscale_rmul(nb::object self, nb::object other) {
    nb::object data = md_attr(std::move(self), "_data");
    return md_checked(PyNumber_Multiply(other.ptr(), data.ptr()));
}
nb::object multiscale_array(nb::object self, nb::dict namespace_) {
    MultiScaleNamespace ns(std::move(namespace_));
    nb::object asarray = md_attr(ns.get("np"), "asarray");
    nb::object backing = md_attr(std::move(self), "_data");
    nb::object value = md_item(std::move(backing), nb::int_(-1));
    return md_call1(std::move(asarray), std::move(value));
}

nb::object md_format(nb::object value) {
    return md_checked(PyObject_Format(value.ptr(), nullptr));
}
nb::object md_repr_format(nb::object value) {
    nb::object text = md_checked(PyObject_Repr(value.ptr()));
    value.reset();
    return md_format(std::move(text));
}
template <std::size_t Count> nb::object md_join(std::array<nb::object, Count>& parts) {
    nb::tuple sequence = nb::steal<nb::tuple>(PyTuple_New(static_cast<Py_ssize_t>(Count)));
    if (!sequence.is_valid()) throw nb::python_error();
    for (std::size_t i = 0; i < Count; ++i)
        PyTuple_SET_ITEM(sequence.ptr(), static_cast<Py_ssize_t>(i), parts[i].release().ptr());
    nb::str separator("");
    return md_checked(PyUnicode_Join(separator.ptr(), sequence.ptr()));
}
nb::object multiscale_repr(nb::object self, nb::dict namespace_) {
    MultiScaleNamespace ns(std::move(namespace_));
    std::array<nb::object, 9> parts;
    parts[0] = nb::str("<MultiScaleData at ");
    nb::object hex = ns.get("hex");
    nb::object id = ns.get("id");
    nb::object identity = md_call1(std::move(id), self);
    parts[1] = md_format(md_call1(std::move(hex), std::move(identity)));
    parts[2] = nb::str(". ");
    parts[3] = md_format(md_call1(ns.get("len"), self));
    parts[4] = nb::str(" levels, '");
    parts[5] = md_format(md_attr(self, "dtype"));
    parts[6] = nb::str("', shapes: ");
    parts[7] = md_format(md_attr(std::move(self), "shapes"));
    parts[8] = nb::str(">");
    return md_join(parts);
}

void layer_data_assert_protocol(nb::object obj, nb::object protocol, nb::dict namespace_) {
    MultiScaleNamespace ns(std::move(namespace_));
    nb::object isinstance = ns.get("isinstance");
    if (!md_truth(md_call2(std::move(isinstance), obj, protocol))) {
        md_call2(ns.get("_raise_protocol_error"), std::move(obj), std::move(protocol));
    }
}
void layer_data_raise_protocol_error(nb::object obj, nb::object protocol,
                                    nb::dict namespace_, LayerDataProtocolErrorLocals& owner) {
    MultiScaleNamespace ns(std::move(namespace_));
    nb::object getattr = ns.get("getattr");
    md_replace(owner.annotations,
        md_call3(std::move(getattr), protocol, nb::str("__annotations__"), nb::dict()));

    nb::object set = ns.get("set");
    nb::object dir = ns.get("dir");
    nb::object names = md_call1(std::move(dir), protocol);
    nb::object declared = md_call1(std::move(set), std::move(names));
    nb::object union_ = md_attr(std::move(declared), "union");
    nb::object all_names = md_call1(std::move(union_), owner.annotations);
    nb::object excluded = ns.get("_OBJ_NAMES");
    nb::object needed = md_checked(PyNumber_Subtract(all_names.ptr(), excluded.ptr()));
    all_names.reset();
    excluded.reset();
    md_replace(owner.needed, std::move(needed));

    set = ns.get("set");
    dir = ns.get("dir");
    names = md_call1(std::move(dir), obj);
    nb::object present = md_call1(std::move(set), std::move(names));
    nb::object missing = md_checked(PyNumber_Subtract(owner.needed.ptr(), present.ptr()));
    present.reset();
    md_replace(owner.missing, std::move(missing));

    std::array<nb::object, 6> parts;
    parts[0] = nb::str("Object of type ");
    nb::object type = ns.get("type");
    nb::object object_type = md_call1(std::move(type), obj);
    parts[1] = md_repr_format(md_attr(std::move(object_type), "__name__"));
    parts[2] = nb::str(" does not implement ");
    parts[3] = md_repr_format(md_attr(std::move(protocol), "__name__"));
    parts[4] = nb::str(" Protocol.\nMissing methods: ");
    parts[5] = md_repr_format(owner.missing);
    md_replace(owner.message, md_join(parts));

    nb::object type_error = ns.get("TypeError");
    md_raise(md_call1(std::move(type_error), owner.message));
}
void layer_data_protocol_init_names(nb::dict namespace_) {
    MultiScaleNamespace ns(std::move(namespace_));
    nb::object set = ns.name("set");
    nb::object dir = ns.name("dir");
    nb::object protocol = ns.name("Protocol");
    nb::object names = md_call1(std::move(dir), std::move(protocol));
    nb::object excluded = md_call1(std::move(set), std::move(names));
    nb::str key("_OBJ_NAMES");
    const int stored = PyObject_SetItem(ns.globals.ptr(), key.ptr(), excluded.ptr());
    excluded.reset();
    if (stored < 0) throw nb::python_error();

    nb::object update = md_attr(ns.name("_OBJ_NAMES"), "update");
    nb::object additions = md_checked(PySet_New(nullptr));
    for (const char* name : {"__annotations__", "__dict__", "__weakref__"}) {
        nb::str item(name);
        if (PySet_Add(additions.ptr(), item.ptr()) < 0) throw nb::python_error();
    }
    md_call1(std::move(update), std::move(additions));
}

} // namespace

void bind_multiscale_data(nb::module_& module) {
    // Reuse ordinary object allocation, subtype destruction and reduction.
    // This heap type has no nanobind instance header or constructor policy.
    PyType_Spec spec = {
        "naparixx._native.MultiScaleNamespaceBase",
        static_cast<int>(sizeof(MultiScaleStorage)),
        0,
        Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HAVE_GC,
        md_namespace_slots,
    };
    nb::object type = md_checked(PyType_FromSpec(&spec));
    module.attr("MultiScaleNamespaceBase") = type;

    nb::class_<MultiScaleInitLocals>(module, "MultiScaleInitLocals",
        nb::is_weak_referenceable(), nb::type_slots(md_init_slots))
        .def_prop_ro("d", [](const MultiScaleInitLocals& s) { return md_optional(s.d); });
    nb::class_<LayerDataProtocolErrorLocals>(module, "LayerDataProtocolErrorLocals",
        nb::is_weak_referenceable(), nb::type_slots(md_error_slots))
        .def_prop_ro("annotations", [](const LayerDataProtocolErrorLocals& s) { return md_optional(s.annotations); })
        .def_prop_ro("needed", [](const LayerDataProtocolErrorLocals& s) { return md_optional(s.needed); })
        .def_prop_ro("missing", [](const LayerDataProtocolErrorLocals& s) { return md_optional(s.missing); })
        .def_prop_ro("message", [](const LayerDataProtocolErrorLocals& s) { return md_optional(s.message); });

    module.def("multiscale_init", &multiscale_init,
        nb::arg("self").none(), nb::arg("data").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("multiscale_size", &multiscale_size, nb::arg("self").none());
    module.def("multiscale_ndim", &multiscale_ndim, nb::arg("self").none());
    module.def("multiscale_eq", &multiscale_eq, nb::arg("self").none(), nb::arg("other").none());
    module.def("multiscale_add", &multiscale_add, nb::arg("self").none(), nb::arg("other").none());
    module.def("multiscale_mul", &multiscale_mul, nb::arg("self").none(), nb::arg("other").none());
    module.def("multiscale_rmul", &multiscale_rmul, nb::arg("self").none(), nb::arg("other").none());
    module.def("multiscale_array", &multiscale_array, nb::arg("self").none(), nb::arg("namespace"));
    module.def("multiscale_repr", &multiscale_repr, nb::arg("self").none(), nb::arg("namespace"));
    module.def("layer_data_assert_protocol", &layer_data_assert_protocol,
        nb::arg("obj").none(), nb::arg("protocol").none(), nb::arg("namespace"));
    module.def("layer_data_raise_protocol_error", &layer_data_raise_protocol_error,
        nb::arg("obj").none(), nb::arg("protocol").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("layer_data_protocol_init_names", &layer_data_protocol_init_names, nb::arg("namespace"));
    module.def("multiscale_init_locals", [] { return MultiScaleInitLocals(); });
    module.def("layer_data_protocol_error_locals", [] { return LayerDataProtocolErrorLocals(); });
}
} // namespace nx
