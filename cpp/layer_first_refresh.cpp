#include "arrays.hpp"
#include "collection_protocol.hpp"
#include <string>
#include <utility>

namespace nx {
namespace {

nb::object fr_checked(PyObject* value) {
    if (!value) throw nb::python_error();
    return nb::steal<nb::object>(value);
}
nb::object fr_attr(nb::object value, const char* name) {
    return fr_checked(PyObject_GetAttrString(value.ptr(), name));
}
nb::object fr_call0(nb::object callback) {
    return fr_checked(PyObject_CallNoArgs(callback.ptr()));
}
nb::object fr_call1(nb::object callback, nb::object value) {
    return fr_checked(PyObject_CallOneArg(callback.ptr(), value.ptr()));
}
nb::object fr_call2(nb::object callback, nb::object first, nb::object second) {
    return fr_checked(PyObject_CallFunctionObjArgs(callback.ptr(), first.ptr(), second.ptr(), nullptr));
}
nb::object fr_call3(nb::object callback, nb::object first, nb::object second, nb::object third) {
    return fr_checked(PyObject_CallFunctionObjArgs(callback.ptr(), first.ptr(), second.ptr(), third.ptr(), nullptr));
}
nb::object fr_call_kw(nb::object callback, nb::tuple arguments, nb::dict keywords) {
    return fr_checked(PyObject_Call(callback.ptr(), arguments.ptr(), keywords.ptr()));
}
bool fr_truth(nb::object value) {
    int result = PyObject_IsTrue(value.ptr());
    if (result < 0) throw nb::python_error();
    return result != 0;
}
nb::object fr_compare(nb::object first, nb::object second, int operation) {
    return fr_checked(PyObject_RichCompare(first.ptr(), second.ptr(), operation));
}
nb::object fr_literal(const char* value) {
    return fr_checked(PyUnicode_InternFromString(value));
}
bool fr_contains(nb::object container, nb::object key) {
    int result = PySequence_Contains(container.ptr(), key.ptr());
    if (result < 0) throw nb::python_error();
    return result != 0;
}
nb::object fr_item(nb::object value, nb::object index) {
    return fr_checked(PyObject_GetItem(value.ptr(), index.ptr()));
}
void fr_replace(nb::object& target, nb::object value) {
    nb::object previous = std::move(target);
    target = std::move(value);
    previous.reset();
}

struct RefreshNamespace {
    nb::dict globals;
    nb::object builtins;
    explicit RefreshNamespace(nb::dict namespace_)
        : globals(std::move(namespace_)), builtins(nb::borrow<nb::object>(PyEval_GetBuiltins())) {}
    nb::object get(const char* name) const {
        nb::object key = nb::str(name);
        PyObject* found = PyDict_GetItemWithError(globals.ptr(), key.ptr());
        if (found) return nb::borrow<nb::object>(found);
        if (PyErr_Occurred()) throw nb::python_error();
        found = PyObject_GetItem(builtins.ptr(), key.ptr());
        if (found) return nb::steal<nb::object>(found);
        if (!PyErr_ExceptionMatches(PyExc_KeyError)) throw nb::python_error();
        PyErr_Clear();
        nb::object message = fr_checked(PyUnicode_FromFormat("name '%s' is not defined", name));
        nb::object exception = fr_call1(nb::borrow<nb::object>(PyExc_NameError), std::move(message));
        if (PyObject_SetAttrString(exception.ptr(), "name", key.ptr()) < 0) throw nb::python_error();
        PyErr_SetObject(PyExc_NameError, exception.ptr());
        throw nb::python_error();
    }
};

// This per-call carrier retains only the source's reassigned local. Layer state
// stays in the actual object's existing namespace, without a native mirror.
struct LayerDataSliceLocals {
    nb::object world_to_data;
    LayerDataSliceLocals() = default;
    LayerDataSliceLocals(LayerDataSliceLocals&&) = default;
    ~LayerDataSliceLocals() { clear(); }
    void clear() {
        nb::object previous = std::move(world_to_data);
        previous.reset();
    }
};
int fr_locals_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) Py_VISIT(nb::inst_ptr<LayerDataSliceLocals>(self)->world_to_data.ptr());
    return 0;
}
int fr_locals_clear(PyObject* self) {
    if (nb::inst_ready(self)) nb::inst_ptr<LayerDataSliceLocals>(self)->clear();
    return 0;
}
PyType_Slot fr_locals_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(fr_locals_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(fr_locals_clear)}, {0, nullptr},
};

void layer_refresh(nb::object self, nb::object event, nb::object thumbnail,
                   nb::object data_displayed, nb::object highlight, nb::object extent,
                   nb::object force, nb::dict namespace_, nb::object blocked_message,
                   nb::object refresh_message) {
    RefreshNamespace ns(std::move(namespace_));
    if (fr_truth(fr_attr(self, "_refresh_blocked"))) {
        nb::object callback = fr_attr(ns.get("logger"), "debug");
        fr_call2(std::move(callback), std::move(blocked_message), self);
        return;
    }
    nb::object callback = fr_attr(ns.get("logger"), "debug");
    fr_call2(std::move(callback), std::move(refresh_message), self);

    bool asynchronous = fr_truth(fr_attr(fr_attr(fr_call0(ns.get("get_settings")), "experimental"), "async_"));
    if (asynchronous) asynchronous = fr_truth(data_displayed);
    if (asynchronous) {
        callback = fr_attr(self, "_refresh_sync");
        nb::dict keywords;
        keywords["extent"] = extent;
        keywords["force"] = nb::bool_(true);
        fr_call_kw(std::move(callback), nb::tuple(), std::move(keywords));
        callback = fr_attr(fr_attr(self, "events"), "reload");
        nb::dict reload_keywords;
        reload_keywords["layer"] = self;
        fr_call_kw(std::move(callback), nb::tuple(), std::move(reload_keywords));
    } else if (fr_truth(fr_attr(self, "visible"))) {
        fr_call0(fr_attr(self, "set_view_slice"));
        callback = fr_attr(self, "_refresh_sync");
        nb::dict keywords;
        keywords["thumbnail"] = thumbnail;
        keywords["data_displayed"] = data_displayed;
        keywords["highlight"] = highlight;
        keywords["extent"] = extent;
        keywords["force"] = force;
        fr_call_kw(std::move(callback), nb::tuple(), std::move(keywords));
    }
}

void layer_clear_extent(nb::object self) {
    nb::object key = fr_literal("extent");
    nb::object dictionary = fr_attr(self, "__dict__");
    if (fr_contains(std::move(dictionary), std::move(key))) {
        if (PyObject_DelAttrString(self.ptr(), "extent") < 0) throw nb::python_error();
    }
    key = fr_literal("_extent_augmented");
    dictionary = fr_attr(self, "__dict__");
    if (fr_contains(std::move(dictionary), std::move(key))) {
        if (PyObject_DelAttrString(self.ptr(), "_extent_augmented") < 0) throw nb::python_error();
    }
    fr_call0(fr_attr(fr_attr(self, "events"), "_extent_augmented"));
}

void layer_set_view_slice(nb::object self) {
    fr_call0(fr_attr(fr_attr(std::move(self), "_slicing_state"), "set_view_slice"));
}

void layer_slicing_set_view_slice(nb::object self) {
    nb::object context = fr_call0(fr_attr(self, "dask_optimized_slicing"));
    nb::object enter = collections::context_method(context, "__enter__", false);
    nb::object exit = collections::context_method(context, "__exit__", true);
    PyObject* entered = PyObject_CallNoArgs(enter.ptr());
    enter.reset();
    context.reset();
    fr_checked(entered).reset();
    try {
        fr_call0(fr_attr(self, "_set_view_slice"));
    } catch (const nb::python_error& original) {
        collections::HandledException active(original.value());
        nb::object traceback = original.traceback();
        if (!traceback.is_valid()) traceback = nb::none();
        try {
            if (!fr_truth(fr_call3(exit, nb::borrow<nb::object>(original.type()),
                                  nb::borrow<nb::object>(original.value()), std::move(traceback)))) throw;
        } catch (const nb::python_error& cleanup) {
            if (cleanup.value().ptr() != original.value().ptr())
                PyException_SetContext(cleanup.value().ptr(), original.value().inc_ref().ptr());
            throw;
        }
        return;
    }
    fr_call3(std::move(exit), nb::none(), nb::none(), nb::none());
}

nb::object layer_slicing_data_slice(nb::object self, nb::dict namespace_, LayerDataSliceLocals& locals) {
    RefreshNamespace ns(std::move(namespace_));
    nb::object length = ns.get("len");
    nb::object not_displayed = fr_attr(fr_attr(self, "_slice_input"), "not_displayed");
    if (fr_truth(fr_compare(fr_call1(std::move(length), std::move(not_displayed)), nb::int_(0), Py_EQ))) {
        nb::object callback = fr_attr(ns.get("_ThickNDSlice"), "make_full");
        nb::object nan = fr_attr(ns.get("np"), "nan");
        nb::tuple point = nb::steal<nb::tuple>(PyTuple_Pack(1, nan.ptr()));
        if (!point.is_valid()) throw nb::python_error();
        nan.reset();
        nb::object ndim = fr_attr(self, "ndim");
        nb::object repeated = fr_checked(PyNumber_Multiply(point.ptr(), ndim.ptr()));
        ndim.reset();
        point.reset();
        nb::dict keywords;
        keywords["point"] = std::move(repeated);
        return fr_call_kw(std::move(callback), nb::tuple(), std::move(keywords));
    }

    fr_replace(locals.world_to_data, fr_attr(fr_attr(fr_attr(self, "layer"), "_data_to_world"), "inverse"));
    nb::object callback = ns.get("apply_units_to_transform");
    nb::object transform = locals.world_to_data;
    nb::object units = fr_attr(self, "_units");
    fr_replace(locals.world_to_data, fr_call2(std::move(callback), std::move(transform), std::move(units)));
    callback = fr_attr(fr_attr(self, "_slice_input"), "data_slice");
    nb::dict keywords;
    keywords["world_to_data"] = locals.world_to_data;
    return fr_call_kw(std::move(callback), nb::tuple(), std::move(keywords));
}

nb::object slice_input_data_slice(nb::object self, nb::object world_to_data,
                                  nb::dict namespace_, nb::object warning_message) {
    return slice_dataclass_data_slice_compat(
        std::move(self), std::move(world_to_data),
        std::move(namespace_), std::move(warning_message));
}

nb::object thick_slice_as_array(nb::object self, nb::dict namespace_) {
    RefreshNamespace ns(std::move(namespace_));
    nb::object callback = fr_attr(ns.get("np"), "array");
    nb::object point = fr_attr(self, "point");
    nb::object left = fr_attr(self, "margin_left");
    nb::object right = fr_attr(std::move(self), "margin_right");
    nb::list values = nb::steal<nb::list>(PyList_New(3));
    if (!values.is_valid()) throw nb::python_error();
    PyList_SET_ITEM(values.ptr(), 0, point.release().ptr());
    PyList_SET_ITEM(values.ptr(), 1, left.release().ptr());
    PyList_SET_ITEM(values.ptr(), 2, right.release().ptr());
    return fr_call1(std::move(callback), std::move(values));
}

nb::object thick_slice_from_array(nb::object cls, nb::object arr, nb::dict namespace_) {
    RefreshNamespace ns(std::move(namespace_));
    nb::object callback = ns.get("tuple");
    nb::object point = fr_call1(std::move(callback), fr_item(arr, nb::int_(0)));
    callback = ns.get("tuple");
    nb::object left = fr_call1(std::move(callback), fr_item(arr, nb::int_(1)));
    callback = ns.get("tuple");
    nb::object right = fr_call1(std::move(callback), fr_item(std::move(arr), nb::int_(2)));
    nb::dict keywords;
    keywords["point"] = std::move(point);
    keywords["margin_left"] = std::move(left);
    keywords["margin_right"] = std::move(right);
    return fr_call_kw(std::move(cls), nb::tuple(), std::move(keywords));
}

} // namespace

void bind_layer_first_refresh(nb::module_& module) {
    nb::class_<LayerDataSliceLocals>(module, "LayerDataSliceLocals", nb::is_weak_referenceable(), nb::type_slots(fr_locals_slots))
        .def_prop_ro("world_to_data", [](const LayerDataSliceLocals& state) {
            return state.world_to_data.is_valid() ? state.world_to_data : nb::none();
        });
    module.def("layer_data_slice_locals", [] { return LayerDataSliceLocals{}; });
    module.def("layer_refresh", &layer_refresh,
        nb::arg("self").none(), nb::arg("event").none(), nb::arg("thumbnail").none(),
        nb::arg("data_displayed").none(), nb::arg("highlight").none(), nb::arg("extent").none(),
        nb::arg("force").none(), nb::arg("namespace"), nb::arg("blocked_message").none(),
        nb::arg("refresh_message").none());
    module.def("layer_clear_extent", &layer_clear_extent, nb::arg("self").none());
    module.def("layer_set_view_slice", &layer_set_view_slice, nb::arg("self").none());
    module.def("layer_slicing_set_view_slice", &layer_slicing_set_view_slice, nb::arg("self").none());
    module.def("layer_slicing_data_slice", &layer_slicing_data_slice,
        nb::arg("self").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("slice_input_data_slice", &slice_input_data_slice,
        nb::arg("self").none(), nb::arg("world_to_data").none(), nb::arg("namespace"),
        nb::arg("warning_message").none());
    module.def("thick_slice_as_array", &thick_slice_as_array, nb::arg("self").none(), nb::arg("namespace"));
    module.def("thick_slice_from_array", &thick_slice_from_array,
        nb::arg("cls").none(), nb::arg("arr").none(), nb::arg("namespace"));
}

} // namespace nx
