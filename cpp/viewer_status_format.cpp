#include "arrays.hpp"
#include <array>
#include <utility>

namespace nx {
namespace {

nb::object sf_checked(PyObject* value) {
    if (!value) throw nb::python_error();
    return nb::steal<nb::object>(value);
}
nb::object sf_attr(nb::object value, const char* name) {
    return sf_checked(PyObject_GetAttrString(value.ptr(), name));
}
nb::object sf_item(nb::object value, nb::object key) {
    return sf_checked(PyObject_GetItem(value.ptr(), key.ptr()));
}
nb::object sf_call1(nb::object callback, nb::object argument) {
    return sf_checked(PyObject_CallOneArg(callback.ptr(), argument.ptr()));
}
nb::object sf_call2(nb::object callback, nb::object first, nb::object second) {
    return sf_checked(PyObject_CallFunctionObjArgs(callback.ptr(), first.ptr(), second.ptr(), nullptr));
}
nb::object sf_keywords(nb::object callback, nb::tuple arguments, nb::dict keywords) {
    return sf_checked(PyObject_Call(callback.ptr(), arguments.ptr(), keywords.ptr()));
}
bool sf_truth(nb::object value) {
    const int result = PyObject_IsTrue(value.ptr());
    if (result < 0) throw nb::python_error();
    return result != 0;
}
nb::object sf_add(nb::object first, nb::object second) {
    return sf_checked(PyNumber_Add(first.ptr(), second.ptr()));
}
nb::object sf_format(nb::object value) {
    return sf_checked(PyObject_Format(value.ptr(), nullptr));
}
nb::object sf_optional(const nb::object& value) { return value.is_valid() ? value : nb::none(); }

struct StatusFormatNamespace {
    nb::dict globals;
    nb::object builtins;
    explicit StatusFormatNamespace(nb::dict namespace_)
        : globals(std::move(namespace_)), builtins(nb::borrow<nb::object>(PyEval_GetBuiltins())) {}
    nb::object get(const char* name) const {
        nb::object key = nb::str(name);
        PyObject* result = PyDict_GetItemWithError(globals.ptr(), key.ptr());
        if (result) return nb::borrow<nb::object>(result);
        if (PyErr_Occurred()) throw nb::python_error();
        result = PyObject_GetItem(builtins.ptr(), key.ptr());
        if (result) return nb::steal<nb::object>(result);
        if (!PyErr_ExceptionMatches(PyExc_KeyError)) throw nb::python_error();
        PyErr_Clear();
        nb::object message = sf_checked(PyUnicode_FromFormat("name '%s' is not defined", name));
        nb::object exception = sf_call1(nb::borrow<nb::object>(PyExc_NameError), std::move(message));
        if (PyObject_SetAttrString(exception.ptr(), "name", key.ptr()) < 0) throw nb::python_error();
        PyErr_SetObject(PyExc_NameError, exception.ptr());
        throw nb::python_error();
    }
    nb::object numpy(const char* name) const { return sf_attr(get("np"), name); }
};

// Only original assigned/reassigned function locals.  Application and module
// state stay in the actual public objects and executing namespace.
struct ViewerStatusFormatLocals {
    enum class Layout { Format, Strings };
    Layout layout = Layout::Format;
    nb::object value, precision, format_mode, position, pos_str, val_str;
    ViewerStatusFormatLocals() = default;
    ViewerStatusFormatLocals(ViewerStatusFormatLocals&&) = default;
    ~ViewerStatusFormatLocals() { clear(); }
    void clear() {
        std::array<nb::object, 6> old = {
            std::move(value), std::move(precision), std::move(format_mode),
            std::move(position), std::move(pos_str), std::move(val_str),
        };
        if (layout == Layout::Strings) {
            // position, value, precision are original arguments, followed by
            // format_mode, pos_str and val_str in the original local layout.
            for (size_t index : {size_t(3), size_t(0), size_t(1), size_t(2), size_t(4), size_t(5)})
                old[index].reset();
        } else {
            for (nb::object& value_ : old) value_.reset();
        }
    }
};
int sf_locals_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) {
        const auto& state = *nb::inst_ptr<ViewerStatusFormatLocals>(self);
        Py_VISIT(state.value.ptr()); Py_VISIT(state.precision.ptr());
        Py_VISIT(state.format_mode.ptr()); Py_VISIT(state.position.ptr());
        Py_VISIT(state.pos_str.ptr()); Py_VISIT(state.val_str.ptr());
    }
    return 0;
}
int sf_locals_clear(PyObject* self) {
    if (nb::inst_ready(self)) nb::inst_ptr<ViewerStatusFormatLocals>(self)->clear();
    return 0;
}
PyType_Slot sf_locals_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(sf_locals_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(sf_locals_clear)}, {0, nullptr},
};

bool sf_isinstance(nb::object value, const char* class_name, const StatusFormatNamespace& ns) {
    nb::object callback = ns.get("isinstance");
    nb::object klass = ns.get(class_name);
    return sf_truth(sf_call2(std::move(callback), std::move(value), std::move(klass)));
}
bool sf_float_like(nb::object value, const StatusFormatNamespace& ns) {
    if (sf_isinstance(value, "float", ns)) return true;
    nb::object subtype = ns.numpy("issubdtype");
    nb::object type = ns.get("type");
    nb::object actual_type = sf_call1(std::move(type), std::move(value));
    nb::object floating = ns.numpy("floating");
    return sf_truth(sf_call2(std::move(subtype), std::move(actual_type), std::move(floating)));
}

nb::object viewer_status_format_feature(nb::object value, nb::dict namespace_) {
    StatusFormatNamespace ns(std::move(namespace_));
    if (sf_float_like(value, ns)) {
        nb::object callback = ns.get("format_float");
        return sf_call1(std::move(callback), std::move(value));
    }
    nb::object callback = ns.get("str");
    return sf_call1(std::move(callback), std::move(value));
}

nb::object viewer_status_format_value(nb::dict namespace_, nb::tuple literals,
                                    ViewerStatusFormatLocals& owner) {
    owner.layout = ViewerStatusFormatLocals::Layout::Format;
    StatusFormatNamespace ns(std::move(namespace_));
    if (owner.precision.is_none()) owner.precision = ns.get("DEFAULT_PRECISION");
    if (sf_isinstance(owner.value, "str", ns)) return owner.value;
    if (sf_isinstance(owner.value, "Iterable", ns)) {
        // Source order is '[' then the captured str.join callable/delimiter,
        // followed by the list comprehension's GET_ITER and lazy callbacks.
        nb::object open = nb::borrow<nb::object>(literals[0]);
        nb::object join = sf_attr(ns.get("str"), "join");
        nb::object separator = nb::borrow<nb::object>(literals[1]);
        nb::list formatted;
        nb::object iterator = sf_checked(PyObject_GetIter(owner.value.ptr()));
        nb::object v;
        try {
            for (;;) {
                PyObject* next = PyIter_Next(iterator.ptr());
                if (!next) {
                    if (PyErr_Occurred()) throw nb::python_error();
                    break;
                }
                // v is comprehension-local, restored to its previous unbound
                // state on the active CPython 3.12 profile.  It has no outer
                // carrier edge and its stack lease dies on normal/error exit.
                v = nb::steal<nb::object>(next);
                nb::object callback = ns.get("status_format");
                nb::tuple arguments = nb::make_tuple(v);
                nb::dict keywords;
                keywords["precision"] = owner.precision;
                keywords["format_mode"] = owner.format_mode;
                nb::object result = sf_keywords(std::move(callback), std::move(arguments), std::move(keywords));
                formatted.append(std::move(result));
            }
        } catch (...) {
            iterator.reset();
            formatted.reset();
            v.reset();
            throw;
        }
        iterator.reset();
        v.reset();
        nb::object inner = sf_call2(std::move(join), std::move(separator), std::move(formatted));
        nb::object first = sf_add(std::move(open), std::move(inner));
        nb::object close = nb::borrow<nb::object>(literals[2]);
        return sf_add(std::move(first), std::move(close));
    }
    if (owner.value.is_none()) return nb::borrow<nb::object>(literals[3]);
    if (sf_float_like(owner.value, ns)) {
        nb::object callback = ns.get("format_float");
        nb::tuple arguments = nb::make_tuple(owner.value);
        nb::dict keywords;
        keywords["precision"] = owner.precision;
        keywords["format_mode"] = owner.format_mode;
        return sf_keywords(std::move(callback), std::move(arguments), std::move(keywords));
    }
    nb::object callback = ns.get("str");
    return sf_call1(std::move(callback), owner.value);
}

nb::object viewer_status_generate_strings(nb::dict namespace_, nb::tuple literals,
                                        ViewerStatusFormatLocals& owner) {
    owner.layout = ViewerStatusFormatLocals::Layout::Strings;
    StatusFormatNamespace ns(std::move(namespace_));
    if (!owner.position.is_none()) {
        nb::object asarray = ns.numpy("asarray");
        owner.position = sf_call1(std::move(asarray), owner.position);
        nb::object subtype = ns.numpy("issubdtype");
        nb::object dtype = sf_attr(owner.position, "dtype");
        nb::object integer = ns.numpy("integer");
        const bool integral = sf_truth(sf_call2(std::move(subtype), std::move(dtype), std::move(integer)));
        owner.format_mode = nb::borrow<nb::object>(literals[integral ? 0 : 1]);
        nb::object callback = ns.get("status_format");
        nb::tuple arguments = nb::make_tuple(owner.position);
        nb::dict keywords;
        keywords["precision"] = owner.precision;
        keywords["format_mode"] = owner.format_mode;
        owner.pos_str = sf_keywords(std::move(callback), std::move(arguments), std::move(keywords));
    } else owner.pos_str = nb::borrow<nb::object>(literals[2]);
    owner.val_str = nb::borrow<nb::object>(literals[2]);
    if (!owner.value.is_none()) {
        const bool tuple_value = sf_isinstance(owner.value, "tuple", ns);
        bool multiscale = false;
        if (tuple_value) {
            nb::object no_value = nb::borrow<nb::object>(literals[3]);
            nb::object comparison = sf_checked(PyObject_RichCompare(owner.value.ptr(), no_value.ptr(), Py_NE));
            multiscale = sf_truth(std::move(comparison));
        }
        if (multiscale) {
            nb::object callback = ns.get("status_format");
            nb::object level = sf_item(owner.value, nb::int_(0));
            owner.val_str = sf_format(sf_call1(std::move(callback), std::move(level)));
            if (!sf_item(owner.value, nb::int_(1)).is_none()) {
                nb::object comma = nb::borrow<nb::object>(literals[4]);
                nb::object format = ns.get("status_format");
                nb::object value = sf_item(owner.value, nb::int_(1));
                nb::object tail = sf_format(sf_call1(std::move(format), std::move(value)));
                nb::tuple pieces = nb::make_tuple(std::move(comma), std::move(tail));
                nb::object empty = nb::borrow<nb::object>(literals[2]);
                nb::object addition = sf_checked(PyUnicode_Join(empty.ptr(), pieces.ptr()));
                // BUILD_STRING pops the last formatted piece first.
                pieces.reset();
                owner.val_str = sf_checked(PyNumber_InPlaceAdd(owner.val_str.ptr(), addition.ptr()));
            }
        } else {
            nb::object callback = ns.get("status_format");
            owner.val_str = sf_format(sf_call1(std::move(callback), owner.value));
        }
    }
    return nb::make_tuple(owner.pos_str, owner.val_str);
}

} // namespace

void bind_viewer_status_format(nb::module_& module) {
    nb::class_<ViewerStatusFormatLocals>(module, "ViewerStatusFormatLocals", nb::is_weak_referenceable(), nb::type_slots(sf_locals_slots))
        .def_prop_ro("value", [](const ViewerStatusFormatLocals& s) { return sf_optional(s.value); })
        .def_prop_ro("precision", [](const ViewerStatusFormatLocals& s) { return sf_optional(s.precision); })
        .def_prop_ro("format_mode", [](const ViewerStatusFormatLocals& s) { return sf_optional(s.format_mode); })
        .def_prop_ro("position", [](const ViewerStatusFormatLocals& s) { return sf_optional(s.position); })
        .def_prop_ro("pos_str", [](const ViewerStatusFormatLocals& s) { return sf_optional(s.pos_str); })
        .def_prop_ro("val_str", [](const ViewerStatusFormatLocals& s) { return sf_optional(s.val_str); });
    module.def("viewer_status_format_locals", [](nb::object value, nb::object precision, nb::object format_mode) {
        ViewerStatusFormatLocals owner;
        owner.value = std::move(value); owner.precision = std::move(precision); owner.format_mode = std::move(format_mode);
        return owner;
    }, nb::arg("value").none(), nb::arg("precision").none(), nb::arg("format_mode").none());
    module.def("viewer_status_strings_locals", [](nb::object position, nb::object value, nb::object precision) {
        ViewerStatusFormatLocals owner;
        owner.layout = ViewerStatusFormatLocals::Layout::Strings;
        owner.position = std::move(position); owner.value = std::move(value); owner.precision = std::move(precision);
        return owner;
    }, nb::arg("position").none(), nb::arg("value").none(), nb::arg("precision").none());
    module.def("viewer_status_format_feature", &viewer_status_format_feature, nb::arg("value").none(), nb::arg("namespace"));
    module.def("viewer_status_format_value", &viewer_status_format_value, nb::arg("namespace"), nb::arg("literals"), nb::arg("locals_owner"));
    module.def("viewer_status_generate_strings", &viewer_status_generate_strings, nb::arg("namespace"), nb::arg("literals"), nb::arg("locals_owner"));
}

} // namespace nx
