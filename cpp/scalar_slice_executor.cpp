#include "arrays.hpp"
#include "collection_protocol.hpp"
#include <array>
#include <string>
#include <utility>

namespace nx {
namespace {

nb::object se_checked(PyObject* value) {
    if (!value) throw nb::python_error();
    return nb::steal<nb::object>(value);
}
nb::object se_attr(nb::object value, const char* name) {
    return se_checked(PyObject_GetAttrString(value.ptr(), name));
}
nb::object se_call0(nb::object callback) { return se_checked(PyObject_CallNoArgs(callback.ptr())); }
nb::object se_call1(nb::object callback, nb::object value) {
    return se_checked(PyObject_CallOneArg(callback.ptr(), value.ptr()));
}
nb::object se_call2(nb::object callback, nb::object a, nb::object b) {
    return se_checked(PyObject_CallFunctionObjArgs(callback.ptr(), a.ptr(), b.ptr(), nullptr));
}
nb::object se_call3(nb::object callback, nb::object a, nb::object b, nb::object c) {
    return se_checked(PyObject_CallFunctionObjArgs(callback.ptr(), a.ptr(), b.ptr(), c.ptr(), nullptr));
}
nb::object se_kw(nb::object callback, nb::tuple args, nb::dict keywords) {
    return se_checked(PyObject_Call(callback.ptr(), args.ptr(), keywords.ptr()));
}
nb::object se_item(nb::object value, nb::object key) {
    return se_checked(PyObject_GetItem(value.ptr(), key.ptr()));
}
void se_set_item(nb::object value, nb::object key, nb::object replacement) {
    if (PyObject_SetItem(value.ptr(), key.ptr(), replacement.ptr()) < 0) throw nb::python_error();
}
bool se_truth(nb::object value) {
    int result = PyObject_IsTrue(value.ptr());
    if (result < 0) throw nb::python_error();
    return result != 0;
}
bool se_contains(nb::object value, nb::object item) {
    int result = PySequence_Contains(value.ptr(), item.ptr());
    if (result < 0) throw nb::python_error();
    return result != 0;
}
nb::object se_compare(nb::object a, nb::object b, int op) {
    return se_checked(PyObject_RichCompare(a.ptr(), b.ptr(), op));
}
nb::object se_add(nb::object a, nb::object b) { return se_checked(PyNumber_Add(a.ptr(), b.ptr())); }
nb::object se_subtract(nb::object a, nb::object b) { return se_checked(PyNumber_Subtract(a.ptr(), b.ptr())); }
nb::object se_multiply(nb::object a, nb::object b) { return se_checked(PyNumber_Multiply(a.ptr(), b.ptr())); }
void se_replace(nb::object& destination, nb::object value) {
    nb::object previous = std::move(destination);
    destination = std::move(value);
    previous.reset();
}
nb::object se_optional(const nb::object& value) { return value.is_valid() ? value : nb::none(); }
nb::object se_literal(const char* value) { return se_checked(PyUnicode_InternFromString(value)); }
struct ExecutorNamespace {
    nb::dict globals;
    nb::object builtins;
    explicit ExecutorNamespace(nb::dict value)
        : globals(std::move(value)), builtins(nb::borrow<nb::object>(PyEval_GetBuiltins())) {}
    ExecutorNamespace(nb::dict value, nb::object builtin_values)
        : globals(std::move(value)), builtins(std::move(builtin_values)) {}
    nb::object get(const char* name) const {
        nb::object key = nb::str(name);
        PyObject* value = PyDict_GetItemWithError(globals.ptr(), key.ptr());
        if (value) return nb::borrow<nb::object>(value);
        if (PyErr_Occurred()) throw nb::python_error();
        value = PyObject_GetItem(builtins.ptr(), key.ptr());
        if (value) return nb::steal<nb::object>(value);
        if (!PyErr_ExceptionMatches(PyExc_KeyError)) throw nb::python_error();
        PyErr_Clear();
        nb::object message = se_checked(PyUnicode_FromFormat("name '%s' is not defined", name));
        nb::object error = se_call1(nb::borrow<nb::object>(PyExc_NameError), std::move(message));
        if (PyObject_SetAttrString(error.ptr(), "name", key.ptr()) < 0) throw nb::python_error();
        PyErr_SetObject(PyExc_NameError, error.ptr());
        throw nb::python_error();
    }
    nb::object numpy(const char* name) const { return se_attr(get("np"), name); }
};

// Source local bindings only. The actual request/model dictionary, provider
// objects and Image's genuine data/axis cells remain the sole authorities.
struct ExecutorLocals {
    bool data_is_cell = false;
    nb::object order, data, image, ndim, tile_to_data, scale, d, translate, disp_slice, data_slice;
    nb::object thumbnail_data_slice, thumbnail_data, thumbnail, slice_arr, slices, pt, max_idx, low, high;
    nb::object dim, point, m_left, m_right, func, underscore;
    ExecutorLocals() = default;
    ExecutorLocals(ExecutorLocals&&) = default;
    ~ExecutorLocals() { clear(); }
    void clear() {
        data_is_cell = false;
        std::array<nb::object, 25> old = {
            std::move(order), std::move(data), std::move(image), std::move(ndim), std::move(tile_to_data),
            std::move(scale), std::move(d), std::move(translate), std::move(disp_slice), std::move(data_slice),
            std::move(thumbnail_data_slice), std::move(thumbnail_data), std::move(thumbnail), std::move(slice_arr),
            std::move(slices), std::move(pt), std::move(max_idx), std::move(low), std::move(high), std::move(dim),
            std::move(point), std::move(m_left), std::move(m_right), std::move(func), std::move(underscore)};
        for (auto& edge : old) edge.reset();
    }
};
int se_locals_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) {
        const auto& s = *nb::inst_ptr<ExecutorLocals>(self);
        Py_VISIT(s.order.ptr()); Py_VISIT(s.data.ptr()); Py_VISIT(s.image.ptr()); Py_VISIT(s.ndim.ptr());
        Py_VISIT(s.tile_to_data.ptr()); Py_VISIT(s.scale.ptr()); Py_VISIT(s.d.ptr()); Py_VISIT(s.translate.ptr());
        Py_VISIT(s.disp_slice.ptr()); Py_VISIT(s.data_slice.ptr()); Py_VISIT(s.thumbnail_data_slice.ptr());
        Py_VISIT(s.thumbnail_data.ptr()); Py_VISIT(s.thumbnail.ptr()); Py_VISIT(s.slice_arr.ptr()); Py_VISIT(s.slices.ptr());
        Py_VISIT(s.pt.ptr()); Py_VISIT(s.max_idx.ptr()); Py_VISIT(s.low.ptr()); Py_VISIT(s.high.ptr()); Py_VISIT(s.dim.ptr());
        Py_VISIT(s.point.ptr()); Py_VISIT(s.m_left.ptr()); Py_VISIT(s.m_right.ptr()); Py_VISIT(s.func.ptr()); Py_VISIT(s.underscore.ptr());
    }
    return 0;
}
template <typename State> int se_clear(PyObject* self) {
    if (nb::inst_ready(self)) nb::inst_ptr<State>(self)->clear();
    return 0;
}
PyType_Slot se_locals_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(se_locals_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(se_clear<ExecutorLocals>)}, {0, nullptr},
};
nb::object se_data_property(const ExecutorLocals& s) {
    if (!s.data_is_cell) return se_optional(s.data);
    if (!s.data.is_valid()) return nb::none();
    PyObject* value = PyCell_GET(s.data.ptr());
    return value ? nb::borrow<nb::object>(value) : nb::none();
}
nb::object se_getter_cell(nb::handle getter) {
    if (!PyFunction_Check(getter.ptr())) throw nb::type_error("cell getter must be a Python function");
    PyObject* closure = PyFunction_GetClosure(getter.ptr());
    if (!closure || PyTuple_GET_SIZE(closure) != 1 || !PyCell_Check(PyTuple_GET_ITEM(closure, 0)))
        throw nb::type_error("cell getter must retain one genuine cell");
    return nb::borrow<nb::object>(PyTuple_GET_ITEM(closure, 0));
}

// Exact unpack protocols. All nested values are obtained before the three
// named assignments; the enclosing dim assignment precedes this operation.
nb::tuple se_triple(nb::handle supplied) {
    nb::object iterator = nb::steal<nb::object>(PyObject_GetIter(supplied.ptr()));
    if (!iterator.is_valid()) {
        if (PyErr_ExceptionMatches(PyExc_TypeError) && !Py_TYPE(supplied.ptr())->tp_iter && !PySequence_Check(supplied.ptr()))
            PyErr_Format(PyExc_TypeError, "cannot unpack non-iterable %.200s object", Py_TYPE(supplied.ptr())->tp_name);
        throw nb::python_error();
    }
    std::array<nb::object, 3> values;
    for (Py_ssize_t i = 0; i < 3; ++i) {
        values[static_cast<size_t>(i)] = nb::steal<nb::object>(PyIter_Next(iterator.ptr()));
        if (!values[static_cast<size_t>(i)].is_valid()) {
            if (PyErr_Occurred()) throw nb::python_error();
            PyErr_Format(PyExc_ValueError, "not enough values to unpack (expected 3, got %zd)", i);
            throw nb::python_error();
        }
    }
    nb::object extra = nb::steal<nb::object>(PyIter_Next(iterator.ptr()));
    if (extra.is_valid()) throw nb::value_error("too many values to unpack (expected 3)");
    if (PyErr_Occurred()) throw nb::python_error();
    return nb::make_tuple(values[0], values[1], values[2]);
}

// The active profile uses CPython 3.12 inlined comprehensions. Their '_' slot
// is restored to UNBOUND before either later source callbacks or re-raising.
void se_all_slices(nb::object values, ExecutorNamespace& ns, ExecutorLocals& s, nb::object& destination) {
    nb::object iterator = se_checked(PyObject_GetIter(values.ptr()));
    values.reset();
    nb::object result = nb::list();
    try {
        while (true) {
            PyObject* next = PyIter_Next(iterator.ptr());
            if (!next) { if (PyErr_Occurred()) throw nb::python_error(); break; }
            se_replace(s.underscore, nb::steal<nb::object>(next));
            nb::object callback = ns.get("slice");
            nb::object value = se_call1(std::move(callback), nb::none());
            if (PyList_Append(result.ptr(), value.ptr()) < 0) throw nb::python_error();
        }
    } catch (const nb::python_error&) {
        // The original inlined cleanup has no PUSH_EXC_INFO: retain the
        // caller's handled exception while restoring the local slot.
        iterator.reset();
        result.reset();
        s.underscore.reset();
        throw;
    } catch (...) { iterator.reset(); result.reset(); s.underscore.reset(); throw; }
    iterator.reset();
    // Publish STORE_FAST's list before restoring the prior UNBOUND '_'.
    se_replace(destination, std::move(result));
    s.underscore.reset();
}

nb::object scalar_executor_request(nb::object self, nb::dict namespace_) {
    ExecutorNamespace ns(std::move(namespace_));
    if (se_truth(se_call0(se_attr(self, "_slice_out_of_bounds")))) {
        nb::object factory = se_attr(ns.get("_ScalarFieldSliceResponse"), "make_empty");
        nb::dict kwargs;
        kwargs["slice_input"] = se_attr(self, "slice_input");
        kwargs["rgb"] = se_attr(self, "rgb");
        kwargs["request_id"] = se_attr(self, "id");
        kwargs["dtype"] = se_attr(self, "dtype");
        return se_kw(std::move(factory), nb::tuple(), std::move(kwargs));
    }
    nb::object manager = se_call0(se_attr(self, "dask_indexer"));
    nb::object enter = collections::context_method(manager, "__enter__", false);
    nb::object exit = collections::context_method(manager, "__exit__", true);
    PyObject* entered = PyObject_CallNoArgs(enter.ptr());
    enter.reset(); manager.reset();
    se_checked(entered).reset();
    nb::object result;
    try {
        if (se_truth(se_attr(self, "multiscale"))) result = se_call0(se_attr(self, "_call_multi_scale"));
        else result = se_call0(se_attr(self, "_call_single_scale"));
    } catch (const nb::python_error& original) {
        events::HandledException active(original.value());
        nb::object traceback = original.traceback();
        if (!traceback.is_valid()) traceback = nb::none();
        try {
            // Keep the saved bound exit alive through truth and handled-state
            // cleanup. It can be the only remaining owner of a fresh manager.
            if (!se_truth(se_call3(exit, nb::borrow<nb::object>(original.type()),
                nb::borrow<nb::object>(original.value()), std::move(traceback)))) throw;
        } catch (const nb::python_error& cleanup) {
            if (cleanup.value().ptr() != original.value().ptr())
                PyException_SetContext(cleanup.value().ptr(), original.value().inc_ref().ptr());
            throw;
        }
        return nb::none();
    }
    se_call3(std::move(exit), nb::none(), nb::none(), nb::none());
    return result;
}
nb::object scalar_executor_single(nb::object self, nb::dict namespace_, ExecutorLocals& s) {
    ExecutorNamespace ns(std::move(namespace_));
    se_replace(s.order, se_call0(se_attr(self, "_get_order")));
    nb::object project = se_attr(self, "_project_thick_slice");
    nb::object data = se_attr(self, "data_at_data_level");
    nb::object data_slice = se_attr(self, "data_slice");
    se_replace(s.data, se_call2(std::move(project), std::move(data), std::move(data_slice)));
    se_replace(s.data, se_call2(ns.numpy("transpose"), s.data, s.order));
    se_replace(s.image, se_call1(se_attr(ns.get("_ScalarFieldView"), "from_view"), s.data));
    se_replace(s.ndim, se_attr(se_attr(self, "slice_input"), "ndim"));
    nb::object affine = ns.get("Affine");
    nb::dict transform_kwargs;
    transform_kwargs["name"] = se_literal("tile2data");
    transform_kwargs["linear_matrix"] = se_call1(ns.numpy("eye"), s.ndim);
    transform_kwargs["ndim"] = s.ndim;
    se_replace(s.tile_to_data, se_kw(std::move(affine), nb::tuple(), std::move(transform_kwargs)));
    nb::object response = ns.get("_ScalarFieldSliceResponse");
    nb::dict kwargs;
    kwargs["image"] = s.image; kwargs["thumbnail"] = s.image; kwargs["tile_to_data"] = s.tile_to_data;
    kwargs["slice_input"] = se_attr(self, "slice_input"); kwargs["request_id"] = se_attr(self, "id");
    return se_kw(std::move(response), nb::tuple(), std::move(kwargs));
}
nb::object scalar_executor_multi(nb::object self, nb::dict namespace_, ExecutorLocals& s) {
    ExecutorNamespace ns(std::move(namespace_));
    nb::object ones = ns.numpy("ones");
    nb::object ndim = se_attr(se_attr(self, "slice_input"), "ndim");
    se_replace(s.scale, se_call1(std::move(ones), std::move(ndim)));
    {
        nb::object iterator = se_checked(PyObject_GetIter(se_attr(se_attr(self, "slice_input"), "displayed").ptr()));
        while (true) {
            PyObject* next = PyIter_Next(iterator.ptr());
            if (!next) { if (PyErr_Occurred()) throw nb::python_error(); break; }
            se_replace(s.d, nb::steal<nb::object>(next));
            nb::object factors = se_attr(self, "downsample_factors");
            nb::object level = se_attr(self, "data_level");
            nb::object value = se_item(se_item(std::move(factors), std::move(level)), s.d);
            nb::object target = s.scale;
            nb::object key = s.d;
            se_set_item(std::move(target), std::move(key), std::move(value));
        }
    }
    se_replace(s.data, se_attr(self, "data_at_data_level"));
    nb::object zeros = ns.numpy("zeros");
    ndim = se_attr(se_attr(self, "slice_input"), "ndim");
    se_replace(s.translate, se_call1(std::move(zeros), std::move(ndim)));
    se_all_slices(se_attr(s.data, "shape"), ns, s, s.disp_slice);
    if (se_truth(se_compare(se_attr(se_attr(self, "slice_input"), "ndisplay"), nb::int_(2), Py_EQ))) {
        nb::object iterator = se_checked(PyObject_GetIter(se_attr(se_attr(self, "slice_input"), "displayed").ptr()));
        while (true) {
            PyObject* next = PyIter_Next(iterator.ptr());
            if (!next) { if (PyErr_Occurred()) throw nb::python_error(); break; }
            se_replace(s.d, nb::steal<nb::object>(next));
            nb::object slice = ns.get("slice");
            nb::object corners = se_attr(self, "corner_pixels");
            nb::object start = se_item(std::move(corners), nb::make_tuple(nb::int_(0), s.d));
            corners = se_attr(self, "corner_pixels");
            nb::object stop = se_add(se_item(std::move(corners), nb::make_tuple(nb::int_(1), s.d)), nb::int_(1));
            nb::object value = se_call3(std::move(slice), std::move(start), std::move(stop), nb::int_(1));
            nb::object target = s.disp_slice;
            nb::object key = s.d;
            se_set_item(std::move(target), std::move(key), std::move(value));
        }
        iterator.reset();
        nb::object corners = se_attr(self, "corner_pixels");
        nb::object first_corner = se_item(std::move(corners), nb::int_(0));
        se_replace(s.translate, se_multiply(std::move(first_corner), s.scale));
    }
    nb::object affine = ns.get("Affine");
    nb::dict transform_kwargs;
    transform_kwargs["name"] = se_literal("tile2data"); transform_kwargs["scale"] = s.scale;
    transform_kwargs["translate"] = s.translate; transform_kwargs["ndim"] = se_attr(se_attr(self, "slice_input"), "ndim");
    se_replace(s.tile_to_data, se_kw(std::move(affine), nb::tuple(), std::move(transform_kwargs)));
    nb::object data_container = s.data;
    nb::object tuple = ns.get("tuple");
    nb::object data_key = se_call1(std::move(tuple), s.disp_slice);
    se_replace(s.data, se_item(std::move(data_container), std::move(data_key)));
    nb::object level_slice = se_attr(self, "_thick_slice_at_level");
    nb::object level = se_attr(self, "data_level");
    se_replace(s.data_slice, se_call1(std::move(level_slice), std::move(level)));
    se_replace(s.data, se_call2(se_attr(self, "_project_thick_slice"), s.data, s.data_slice));
    se_replace(s.order, se_call0(se_attr(self, "_get_order")));
    se_replace(s.data, se_call2(ns.numpy("transpose"), s.data, s.order));
    se_replace(s.image, se_call1(se_attr(ns.get("_ScalarFieldView"), "from_view"), s.data));
    level_slice = se_attr(self, "_thick_slice_at_level");
    level = se_attr(self, "thumbnail_level");
    se_replace(s.thumbnail_data_slice, se_call1(std::move(level_slice), std::move(level)));
    nb::object thumbnail_project = se_attr(self, "_project_thick_slice");
    nb::object thumbnail_data = se_attr(self, "data_at_thumbnail_level");
    se_replace(s.thumbnail_data, se_call2(std::move(thumbnail_project), std::move(thumbnail_data), s.thumbnail_data_slice));
    se_replace(s.thumbnail_data, se_call2(ns.numpy("transpose"), s.thumbnail_data, s.order));
    se_replace(s.thumbnail, se_call1(se_attr(ns.get("_ScalarFieldView"), "from_view"), s.thumbnail_data));
    nb::object response = ns.get("_ScalarFieldSliceResponse");
    nb::dict kwargs;
    kwargs["image"] = s.image; kwargs["thumbnail"] = s.thumbnail; kwargs["tile_to_data"] = s.tile_to_data;
    kwargs["slice_input"] = se_attr(self, "slice_input"); kwargs["request_id"] = se_attr(self, "id");
    return se_kw(std::move(response), nb::tuple(), std::move(kwargs));
}
nb::object scalar_executor_at_level(nb::object self, nb::object level, nb::dict namespace_, ExecutorLocals& s) {
    ExecutorNamespace ns(std::move(namespace_));
    se_replace(s.slice_arr, se_call0(se_attr(se_attr(self, "data_slice"), "as_array")));
    nb::object lhs = s.slice_arr;
    nb::object factors = se_attr(self, "downsample_factors");
    nb::object rhs = se_item(std::move(factors), level);
    nb::object divided = se_checked(PyNumber_InPlaceTrueDivide(lhs.ptr(), rhs.ptr()));
    lhs.reset(); rhs.reset();
    se_replace(s.slice_arr, std::move(divided));
    nb::object clip = ns.numpy("clip");
    nb::object point = se_item(s.slice_arr, nb::int_(0));
    nb::object shapes = se_attr(self, "level_shapes");
    nb::object maximum = se_subtract(se_item(std::move(shapes), level), nb::int_(1));
    nb::object clipped = se_call3(std::move(clip), std::move(point), nb::int_(0), std::move(maximum));
    se_set_item(s.slice_arr, nb::int_(0), std::move(clipped));
    nb::object constructor = se_attr(ns.get("_ThickNDSlice"), "from_array");
    return se_call1(std::move(constructor), s.slice_arr);
}
nb::object scalar_executor_project_thick(nb::object self, nb::object data, nb::object data_slice,
    nb::dict namespace_, ExecutorLocals& s) {
    ExecutorNamespace ns(std::move(namespace_));
    if (se_truth(se_compare(se_attr(self, "projection_mode"), se_literal("none"), Py_EQ))) {
        nb::object select = se_attr(self, "_point_to_slices");
        nb::object point = se_attr(data_slice, "point");
        se_replace(s.slices, se_call1(std::move(select), std::move(point)));
        nb::object asarray = ns.numpy("asarray");
        nb::object selected = se_item(data, s.slices);
        return se_call1(std::move(asarray), std::move(selected));
    }
    nb::object select = se_attr(self, "_data_slice_to_slices");
    nb::object displayed = se_attr(se_attr(self, "slice_input"), "displayed");
    se_replace(s.slices, se_call2(std::move(select), data_slice, std::move(displayed)));
    // Capture the actual override before array materialization callbacks can
    // replace it on the instance, as the original CALL's receiver operand does.
    nb::object project = se_attr(self, "_project_slice");
    nb::dict kwargs;
    nb::object asarray = ns.numpy("asarray");
    nb::object selected = se_item(data, s.slices);
    kwargs["data"] = se_call1(std::move(asarray), std::move(selected));
    nb::object tuple = ns.get("tuple");
    nb::object hidden = se_attr(se_attr(self, "slice_input"), "not_displayed");
    kwargs["axis"] = se_call1(std::move(tuple), std::move(hidden));
    kwargs["mode"] = se_attr(self, "projection_mode");
    return se_kw(std::move(project), nb::tuple(), std::move(kwargs));
}
[[noreturn]] void se_raise(nb::object exception) {
    if (PyExceptionClass_Check(exception.ptr())) exception = se_call0(std::move(exception));
    if (!PyExceptionInstance_Check(exception.ptr())) throw nb::type_error("exceptions must derive from BaseException");
    collections::raise_exception(exception, false);
}
void scalar_executor_project_abstract(nb::object, nb::object, nb::object, nb::dict namespace_) {
    ExecutorNamespace ns(std::move(namespace_)); se_raise(ns.get("NotImplementedError"));
}
void scalar_executor_labels_project(nb::object, nb::object, nb::object, nb::dict namespace_) {
    ExecutorNamespace ns(std::move(namespace_)); se_raise(ns.get("NotImplementedError"));
}
nb::object scalar_executor_order(nb::object self, nb::dict namespace_, ExecutorLocals& s) {
    ExecutorNamespace ns(std::move(namespace_));
    nb::object reorder = ns.get("reorder_after_dim_reduction");
    nb::object displayed = se_attr(se_attr(self, "slice_input"), "displayed");
    se_replace(s.order, se_call1(std::move(reorder), std::move(displayed)));
    if (se_truth(se_attr(self, "rgb"))) {
        // BUILD_TUPLE_UNPACK obtains the iterable before evaluating max(order).
        nb::object values = nb::steal<nb::object>(PySequence_List(s.order.ptr()));
        if (!values.is_valid()) {
            if (PyErr_ExceptionMatches(PyExc_TypeError) && !Py_TYPE(s.order.ptr())->tp_iter && !PySequence_Check(s.order.ptr()))
                PyErr_Format(PyExc_TypeError, "Value after * must be an iterable, not %.200s", Py_TYPE(s.order.ptr())->tp_name);
            throw nb::python_error();
        }
        nb::object maximum = ns.get("max");
        nb::object last = se_add(se_call1(std::move(maximum), s.order), nb::int_(1));
        if (PyList_Append(values.ptr(), last.ptr()) < 0) throw nb::python_error();
        last.reset();
        return se_checked(PyList_AsTuple(values.ptr()));
    }
    return s.order;
}
nb::object scalar_executor_out_of_bounds(nb::object self, nb::dict namespace_, ExecutorLocals& s) {
    ExecutorNamespace ns(std::move(namespace_));
    nb::object iterator = se_checked(PyObject_GetIter(se_attr(se_attr(self, "slice_input"), "not_displayed").ptr()));
    while (true) {
        PyObject* next = PyIter_Next(iterator.ptr());
        if (!next) { if (PyErr_Occurred()) throw nb::python_error(); break; }
        se_replace(s.d, nb::steal<nb::object>(next));
        se_replace(s.pt, se_item(se_attr(se_attr(self, "data_slice"), "point"), s.d));
        nb::object shapes = se_attr(self, "level_shapes");
        nb::object first = se_item(std::move(shapes), nb::int_(0));
        nb::object current = se_item(std::move(first), s.d);
        se_replace(s.max_idx, se_subtract(std::move(current), nb::int_(1)));
        if (se_truth(se_compare(se_attr(self, "projection_mode"), se_literal("none"), Py_EQ))) {
            nb::object round = ns.numpy("round");
            if (se_truth(se_compare(se_call1(std::move(round), s.pt), nb::int_(0), Py_LT))) return nb::bool_(true);
            round = ns.numpy("round");
            if (se_truth(se_compare(se_call1(std::move(round), s.pt), s.max_idx, Py_GT))) return nb::bool_(true);
        } else {
            se_replace(s.pt, se_item(se_attr(se_attr(self, "data_slice"), "point"), s.d));
            nb::object round = ns.numpy("round");
            nb::object point = s.pt;
            nb::object left = se_item(se_attr(se_attr(self, "data_slice"), "margin_left"), s.d);
            se_replace(s.low, se_call1(std::move(round), se_subtract(std::move(point), std::move(left))));
            round = ns.numpy("round");
            point = s.pt;
            nb::object right = se_item(se_attr(se_attr(self, "data_slice"), "margin_right"), s.d);
            se_replace(s.high, se_call1(std::move(round), se_add(std::move(point), std::move(right))));
            if (se_truth(se_compare(s.high, nb::int_(0), Py_LT))) return nb::bool_(true);
            if (se_truth(se_compare(s.low, s.max_idx, Py_GT))) return nb::bool_(true);
        }
    }
    return nb::bool_(false);
}

enum class ExecutorIteratorKind { Point, Singleton, Selection };
struct ExecutorIterator {
    ExecutorIteratorKind kind = ExecutorIteratorKind::Point;
    bool started = false, running = false, closed = false;
    nb::object iterator, current, getter, namespace_, builtins;
    ExecutorIterator() = default;
    ExecutorIterator(ExecutorIterator&&) = default;
    ~ExecutorIterator() { clear(); }
    void clear() {
        closed = true;
        std::array<nb::object, 5> old = {std::move(iterator), std::move(current), std::move(getter),
            std::move(namespace_), std::move(builtins)};
        for (auto& edge : old) edge.reset();
    }
};
struct ExecutorRunning {
    ExecutorIterator& s;
    explicit ExecutorRunning(ExecutorIterator& state) : s(state) {
        if (s.running) throw nb::value_error("generator already executing");
        s.running = true;
    }
    ~ExecutorRunning() { s.running = false; }
};
nb::object se_iterator_next(nb::handle self) {
    auto& s = *nb::inst_ptr<ExecutorIterator>(self);
    ExecutorRunning active(s);
    if (s.closed) throw nb::stop_iteration();
    s.started = true;
    try {
        PyObject* next = PyIter_Next(s.iterator.ptr());
        if (!next) {
            if (PyErr_Occurred()) throw nb::python_error();
            s.clear(); throw nb::stop_iteration();
        }
        se_replace(s.current, nb::steal<nb::object>(next));
        ExecutorNamespace ns(nb::borrow<nb::dict>(s.namespace_), s.builtins);
        if (s.kind == ExecutorIteratorKind::Point) {
            nb::object isnan = ns.numpy("isnan");
            if (se_truth(se_call1(std::move(isnan), s.current))) return se_call1(ns.get("slice"), nb::none());
            nb::object integer = ns.get("int");
            nb::object round = ns.numpy("round");
            nb::object rounded = se_call1(std::move(round), s.current);
            return se_call1(std::move(integer), std::move(rounded));
        }
        if (s.kind == ExecutorIteratorKind::Singleton) {
            nb::object data = se_call0(s.getter);
            nb::object shape = se_attr(std::move(data), "shape");
            nb::object dimension = se_item(std::move(shape), s.current);
            return se_compare(std::move(dimension), nb::int_(1), Py_EQ);
        }
        nb::object axes = se_call0(s.getter);
        if (!se_contains(std::move(axes), s.current)) return se_call1(ns.get("slice"), nb::none());
        return nb::int_(0);
    } catch (const nb::python_error& error) {
        events::HandledException handled(error.value());
        s.closed = true;
        // The genuine adapter's saved error traceback can retain these same
        // assigned comprehension edges. Exhaustion/close/GC detaches them.
        if (error.matches(PyExc_StopIteration)) collections::raise_exception(error.value(), true);
        throw;
    } catch (...) { s.clear(); throw; }
}
void se_iterator_close(nb::handle self) {
    auto& s = *nb::inst_ptr<ExecutorIterator>(self);
    ExecutorRunning active(s);
    if (s.closed) return;
    if (!s.started) { s.clear(); return; }
    nb::object exit = se_call0(nb::borrow<nb::object>(PyExc_GeneratorExit));
    events::HandledException handled(exit); s.clear();
}
void se_iterator_throw(nb::handle self, nb::args args) {
    nb::object exception = collections::normalize_throw(args);
    auto& s = *nb::inst_ptr<ExecutorIterator>(self);
    ExecutorRunning active(s);
    bool open = !s.closed;
    s.closed = true;
    events::HandledException handled(exception);
    collections::raise_exception(exception, open);
}
int se_iterator_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) {
        const auto& s = *nb::inst_ptr<ExecutorIterator>(self);
        Py_VISIT(s.iterator.ptr()); Py_VISIT(s.current.ptr()); Py_VISIT(s.getter.ptr());
        Py_VISIT(s.namespace_.ptr()); Py_VISIT(s.builtins.ptr());
    }
    return 0;
}
void se_iterator_finalize(PyObject* self) noexcept {
    if (!nb::inst_ready(self) || nb::inst_ptr<ExecutorIterator>(self)->closed) return;
    PyObject *type = nullptr, *value = nullptr, *trace = nullptr;
    PyErr_Fetch(&type, &value, &trace);
    try { se_iterator_close(self); }
    catch (nb::python_error& error) { error.restore(); PyErr_WriteUnraisable(self); }
    catch (const std::exception& error) { PyErr_SetString(PyExc_RuntimeError, error.what()); PyErr_WriteUnraisable(self); }
    catch (...) { PyErr_SetString(PyExc_RuntimeError, "failed to close scalar slice iterator"); PyErr_WriteUnraisable(self); }
    PyErr_Restore(type, value, trace);
}
PyType_Slot se_iterator_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(se_iterator_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(se_clear<ExecutorIterator>)},
    {Py_tp_finalize, reinterpret_cast<void*>(se_iterator_finalize)}, {0, nullptr},
};
destructor se_iterator_base_dealloc = nullptr;
void se_iterator_dealloc(PyObject* self) noexcept {
    PyObject_GC_UnTrack(self); PyObject_ClearWeakRefs(self); PyObject_GC_Track(self);
    if (PyObject_CallFinalizerFromDealloc(self) < 0) return;
    se_iterator_base_dealloc(self);
}
nb::object se_generator(ExecutorIteratorKind kind, nb::object iterator, nb::object getter,
    ExecutorNamespace& ns, nb::object factory, const char* qualname) {
    ExecutorIterator state;
    state.kind = kind; state.iterator = std::move(iterator); state.getter = std::move(getter);
    state.namespace_ = ns.globals; state.builtins = ns.builtins;
    nb::object delegate = nb::cast(std::move(state), nb::rv_policy::move);
    nb::object generator = se_call1(std::move(factory), std::move(delegate));
    nb::object name = nb::str("<genexpr>"), qualified = nb::str(qualname);
    if (PyObject_SetAttrString(generator.ptr(), "__name__", name.ptr()) < 0 ||
        PyObject_SetAttrString(generator.ptr(), "__qualname__", qualified.ptr()) < 0) throw nb::python_error();
    return generator;
}
nb::object scalar_executor_point_to_slices(nb::object point, nb::dict namespace_, nb::object generator_factory) {
    ExecutorNamespace ns(std::move(namespace_));
    nb::object tuple = ns.get("tuple");
    nb::object iterator = se_checked(PyObject_GetIter(point.ptr()));
    nb::object generator = se_generator(ExecutorIteratorKind::Point, std::move(iterator), nb::object(),
        ns, std::move(generator_factory), "_ScalarFieldSliceRequest._point_to_slices.<locals>.<genexpr>");
    return se_call1(std::move(tuple), std::move(generator));
}
nb::object scalar_executor_data_to_slices(nb::object data_slice, nb::object dims_displayed,
    nb::dict namespace_, ExecutorLocals& s) {
    ExecutorNamespace ns(std::move(namespace_));
    nb::object range = ns.get("range");
    nb::object ndim = se_attr(data_slice, "ndim");
    se_all_slices(se_call1(std::move(range), std::move(ndim)), ns, s, s.slices);
    nb::object enumerate = ns.get("enumerate");
    nb::object iterator = se_checked(PyObject_GetIter(se_call1(std::move(enumerate), data_slice).ptr()));
    while (true) {
        nb::object item = nb::steal<nb::object>(PyIter_Next(iterator.ptr()));
        if (!item.is_valid()) { if (PyErr_Occurred()) throw nb::python_error(); break; }
        nb::tuple pair = events::pair(item); item.reset();
        se_replace(s.dim, nb::borrow<nb::object>(pair[0]));
        nb::object inner = nb::borrow<nb::object>(pair[1]); pair.reset();
        nb::tuple triple = se_triple(inner); inner.reset();
        se_replace(s.point, nb::borrow<nb::object>(triple[0]));
        se_replace(s.m_left, nb::borrow<nb::object>(triple[1]));
        se_replace(s.m_right, nb::borrow<nb::object>(triple[2])); triple.reset();
        if (se_contains(dims_displayed, s.dim)) continue;
        nb::object maximum = ns.get("max");
        nb::object integer = ns.get("int");
        nb::object round = ns.numpy("round");
        nb::object difference = se_subtract(s.point, s.m_left);
        nb::object rounded = se_call1(std::move(round), std::move(difference));
        nb::object low = se_call1(std::move(integer), std::move(rounded));
        se_replace(s.low, se_call2(std::move(maximum), std::move(low), nb::int_(0)));
        maximum = ns.get("max"); integer = ns.get("int"); round = ns.numpy("round");
        nb::object sum = se_add(s.point, s.m_right);
        rounded = se_call1(std::move(round), std::move(sum));
        nb::object high = se_call1(std::move(integer), std::move(rounded));
        se_replace(s.high, se_call2(std::move(maximum), std::move(high), nb::int_(0)));
        nb::object isclose = ns.numpy("isclose");
        nb::object high_argument = s.high;
        sum = se_add(s.point, s.m_right);
        if (se_truth(se_call2(std::move(isclose), std::move(high_argument), std::move(sum))))
            se_replace(s.high, se_checked(PyNumber_InPlaceAdd(s.high.ptr(), nb::int_(1).ptr())));
        if (se_truth(se_compare(s.low, s.high, Py_EQ)))
            se_replace(s.high, se_checked(PyNumber_InPlaceAdd(s.high.ptr(), nb::int_(1).ptr())));
        nb::object slice = ns.get("slice");
        nb::object value = se_call2(std::move(slice), s.low, s.high);
        nb::object target = s.slices;
        nb::object key = s.dim;
        se_set_item(std::move(target), std::move(key), std::move(value));
    }
    nb::object tuple = ns.get("tuple");
    return se_call1(std::move(tuple), s.slices);
}
nb::object scalar_executor_image_project(nb::dict namespace_, nb::object data_getter, nb::object axis_getter,
    nb::object mode, nb::object generator_factory, ExecutorLocals& s) {
    ExecutorNamespace ns(std::move(namespace_));
    // Retain the genuine source cell, without a parallel value or any writes.
    se_replace(s.data, se_getter_cell(data_getter)); s.data_is_cell = true;
    se_getter_cell(axis_getter);
    nb::object all = ns.get("all");
    nb::object axes = se_call0(axis_getter);
    nb::object iterator = se_checked(PyObject_GetIter(axes.ptr())); axes.reset();
    nb::object singleton = se_generator(ExecutorIteratorKind::Singleton, std::move(iterator), data_getter,
        ns, generator_factory, "_ImageSliceRequest._project_slice.<locals>.<genexpr>");
    if (se_truth(se_call1(std::move(all), std::move(singleton)))) {
        // BINARY_SUBSCR's receiver is loaded before tuple/range/data.ndim.
        nb::object receiver = se_call0(data_getter);
        nb::object tuple = ns.get("tuple");
        nb::object range = ns.get("range");
        nb::object ndim = se_attr(se_call0(data_getter), "ndim");
        nb::object values = se_call1(std::move(range), std::move(ndim));
        iterator = se_checked(PyObject_GetIter(values.ptr())); values.reset();
        nb::object selections = se_generator(ExecutorIteratorKind::Selection, std::move(iterator), axis_getter,
            ns, generator_factory, "_ImageSliceRequest._project_slice.<locals>.<genexpr>");
        nb::object key = se_call1(std::move(tuple), std::move(selections));
        return se_item(std::move(receiver), std::move(key));
    }
    const char* method = nullptr;
    nb::object enum_value = se_attr(ns.get("ImageProjectionMode"), "SUM");
    if (se_truth(se_compare(mode, std::move(enum_value), Py_EQ))) method = "sum";
    else {
        enum_value = se_attr(ns.get("ImageProjectionMode"), "MEAN");
        if (se_truth(se_compare(mode, std::move(enum_value), Py_EQ))) method = "mean";
        else {
            enum_value = se_attr(ns.get("ImageProjectionMode"), "MAX");
            if (se_truth(se_compare(mode, std::move(enum_value), Py_EQ))) method = "max";
            else {
                enum_value = se_attr(ns.get("ImageProjectionMode"), "MIN");
                if (se_truth(se_compare(mode, std::move(enum_value), Py_EQ))) method = "min";
            }
        }
    }
    if (!method) {
        nb::object exception_class = ns.get("NotImplementedError");
        nb::object formatted = se_checked(PyObject_Format(mode.ptr(), nb::str("").ptr()));
        nb::object message = se_checked(PyUnicode_Concat(nb::str("unimplemented projection: ").ptr(), formatted.ptr()));
        formatted.reset();
        nb::object error = se_call1(std::move(exception_class), std::move(message));
        se_raise(std::move(error));
    }
    se_replace(s.func, ns.numpy(method));
    nb::object callback = s.func;
    nb::object data = se_call0(data_getter);
    nb::object tuple = ns.get("tuple");
    axes = se_call0(axis_getter);
    nb::object axis_tuple = se_call1(std::move(tuple), std::move(axes));
    return se_call2(std::move(callback), std::move(data), std::move(axis_tuple));
}

} // namespace

nb::object scalar_executor_request_compat(nb::object self, nb::dict namespace_) {
    return scalar_executor_request(std::move(self), std::move(namespace_));
}
nb::object scalar_executor_single_compat(nb::object self, nb::dict namespace_) {
    nb::object owner = nb::cast(ExecutorLocals(), nb::rv_policy::move);
    return scalar_executor_single(std::move(self), std::move(namespace_), *nb::inst_ptr<ExecutorLocals>(owner));
}
nb::object scalar_executor_multi_compat(nb::object self, nb::dict namespace_) {
    nb::object owner = nb::cast(ExecutorLocals(), nb::rv_policy::move);
    return scalar_executor_multi(std::move(self), std::move(namespace_), *nb::inst_ptr<ExecutorLocals>(owner));
}
nb::object scalar_executor_order_compat(nb::object self, nb::dict namespace_) {
    nb::object owner = nb::cast(ExecutorLocals(), nb::rv_policy::move);
    return scalar_executor_order(std::move(self), std::move(namespace_), *nb::inst_ptr<ExecutorLocals>(owner));
}
nb::object scalar_executor_project_compat(nb::object self, nb::object data, nb::object data_slice, nb::dict namespace_) {
    nb::object owner = nb::cast(ExecutorLocals(), nb::rv_policy::move);
    return scalar_executor_project_thick(std::move(self), std::move(data), std::move(data_slice),
        std::move(namespace_), *nb::inst_ptr<ExecutorLocals>(owner));
}
nb::object scalar_executor_image_project_compat(nb::object data, nb::object axis, nb::object mode, nb::dict namespace_) {
    ExecutorNamespace ns(namespace_);
    nb::object factory = ns.get("_scalar_executor_legacy_image_glue");
    nb::object glue = se_call2(std::move(factory), std::move(data), std::move(axis));
    nb::tuple functions = se_triple(glue); glue.reset();
    nb::object data_getter = nb::borrow<nb::object>(functions[0]);
    nb::object axis_getter = nb::borrow<nb::object>(functions[1]);
    nb::object generator_factory = nb::borrow<nb::object>(functions[2]); functions.reset();
    nb::object owner = nb::cast(ExecutorLocals(), nb::rv_policy::move);
    return scalar_executor_image_project(std::move(namespace_), std::move(data_getter), std::move(axis_getter),
        std::move(mode), std::move(generator_factory), *nb::inst_ptr<ExecutorLocals>(owner));
}

void bind_scalar_slice_executor(nb::module_& module) {
    nb::class_<ExecutorLocals>(module, "ScalarSliceExecutorLocals", nb::is_weak_referenceable(), nb::type_slots(se_locals_slots))
        .def_prop_ro("order", [](const ExecutorLocals& s) { return se_optional(s.order); })
        .def_prop_ro("data", &se_data_property)
        .def_prop_ro("image", [](const ExecutorLocals& s) { return se_optional(s.image); })
        .def_prop_ro("ndim", [](const ExecutorLocals& s) { return se_optional(s.ndim); })
        .def_prop_ro("tile_to_data", [](const ExecutorLocals& s) { return se_optional(s.tile_to_data); })
        .def_prop_ro("scale", [](const ExecutorLocals& s) { return se_optional(s.scale); })
        .def_prop_ro("d", [](const ExecutorLocals& s) { return se_optional(s.d); })
        .def_prop_ro("translate", [](const ExecutorLocals& s) { return se_optional(s.translate); })
        .def_prop_ro("disp_slice", [](const ExecutorLocals& s) { return se_optional(s.disp_slice); })
        .def_prop_ro("data_slice", [](const ExecutorLocals& s) { return se_optional(s.data_slice); })
        .def_prop_ro("thumbnail_data_slice", [](const ExecutorLocals& s) { return se_optional(s.thumbnail_data_slice); })
        .def_prop_ro("thumbnail_data", [](const ExecutorLocals& s) { return se_optional(s.thumbnail_data); })
        .def_prop_ro("thumbnail", [](const ExecutorLocals& s) { return se_optional(s.thumbnail); })
        .def_prop_ro("slice_arr", [](const ExecutorLocals& s) { return se_optional(s.slice_arr); })
        .def_prop_ro("slices", [](const ExecutorLocals& s) { return se_optional(s.slices); })
        .def_prop_ro("pt", [](const ExecutorLocals& s) { return se_optional(s.pt); })
        .def_prop_ro("max_idx", [](const ExecutorLocals& s) { return se_optional(s.max_idx); })
        .def_prop_ro("low", [](const ExecutorLocals& s) { return se_optional(s.low); })
        .def_prop_ro("high", [](const ExecutorLocals& s) { return se_optional(s.high); })
        .def_prop_ro("dim", [](const ExecutorLocals& s) { return se_optional(s.dim); })
        .def_prop_ro("point", [](const ExecutorLocals& s) { return se_optional(s.point); })
        .def_prop_ro("m_left", [](const ExecutorLocals& s) { return se_optional(s.m_left); })
        .def_prop_ro("m_right", [](const ExecutorLocals& s) { return se_optional(s.m_right); })
        .def_prop_ro("func", [](const ExecutorLocals& s) { return se_optional(s.func); })
        .def_prop_ro("_", [](const ExecutorLocals& s) { return se_optional(s.underscore); });
    nb::class_<ExecutorIterator> iterator_type(module, "ScalarSliceExecutorIterator", nb::is_weak_referenceable(), nb::type_slots(se_iterator_slots));
    iterator_type.def("__iter__", [](nb::handle self) { return nb::borrow<nb::object>(self); })
        .def("__next__", &se_iterator_next)
        .def("send", [](nb::handle self, nb::object value) {
            const auto& s = *nb::inst_ptr<ExecutorIterator>(self);
            if (s.running) throw nb::value_error("generator already executing");
            if (!s.started && !value.is_none()) throw nb::type_error("can't send non-None value to a just-started generator");
            return se_iterator_next(self);
        }, nb::arg("value").none())
        .def("close", &se_iterator_close).def("throw", &se_iterator_throw);
    auto* iterator_heap_type = reinterpret_cast<PyTypeObject*>(iterator_type.ptr());
    se_iterator_base_dealloc = iterator_heap_type->tp_dealloc;
    iterator_heap_type->tp_dealloc = se_iterator_dealloc;
    PyType_Modified(iterator_heap_type);
    module.def("scalar_executor_locals", [] { return ExecutorLocals(); });
    module.def("scalar_executor_request", &scalar_executor_request, nb::arg("self").none(), nb::arg("namespace"));
    module.def("scalar_executor_single", &scalar_executor_single, nb::arg("self").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("scalar_executor_multi", &scalar_executor_multi, nb::arg("self").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("scalar_executor_at_level", &scalar_executor_at_level, nb::arg("self").none(), nb::arg("level").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("scalar_executor_project_thick", &scalar_executor_project_thick, nb::arg("self").none(), nb::arg("data").none(), nb::arg("data_slice").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("scalar_executor_project_abstract", &scalar_executor_project_abstract, nb::arg("data").none(), nb::arg("axis").none(), nb::arg("mode").none(), nb::arg("namespace"));
    module.def("scalar_executor_order", &scalar_executor_order, nb::arg("self").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("scalar_executor_out_of_bounds", &scalar_executor_out_of_bounds, nb::arg("self").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("scalar_executor_point_to_slices", &scalar_executor_point_to_slices, nb::arg("point").none(), nb::arg("namespace"), nb::arg("generator_factory").none());
    module.def("scalar_executor_data_to_slices", &scalar_executor_data_to_slices, nb::arg("data_slice").none(), nb::arg("dims_displayed").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("scalar_executor_image_project", &scalar_executor_image_project, nb::arg("namespace"), nb::arg("data_getter").none(), nb::arg("axis_getter").none(), nb::arg("mode").none(), nb::arg("generator_factory").none(), nb::arg("locals_owner"));
    module.def("scalar_executor_labels_project", &scalar_executor_labels_project, nb::arg("data").none(), nb::arg("axis").none(), nb::arg("mode").none(), nb::arg("namespace"));
}
} // namespace nx
