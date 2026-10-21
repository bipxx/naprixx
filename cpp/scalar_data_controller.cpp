#include "arrays.hpp"
#include "collection_protocol.hpp"

#include <array>
#include <utility>

namespace nx {
namespace {

nb::object dc_checked(PyObject* value) {
    if (!value) throw nb::python_error();
    return nb::steal<nb::object>(value);
}
nb::object dc_attr(nb::object value, const char* name) {
    return dc_checked(PyObject_GetAttrString(value.ptr(), name));
}
nb::object dc_item(nb::object value, nb::object key) {
    return dc_checked(PyObject_GetItem(value.ptr(), key.ptr()));
}
void dc_store(nb::object self, const char* name, nb::object value) {
    if (PyObject_SetAttrString(self.ptr(), name, value.ptr()) < 0) throw nb::python_error();
}
void dc_set_item(nb::object value, nb::object key, nb::object replacement) {
    if (PyObject_SetItem(value.ptr(), key.ptr(), replacement.ptr()) < 0) throw nb::python_error();
}
nb::object dc_call0(nb::object callback) {
    return dc_checked(PyObject_CallNoArgs(callback.ptr()));
}
nb::object dc_call1(nb::object callback, nb::object first) {
    return dc_checked(PyObject_CallOneArg(callback.ptr(), first.ptr()));
}
nb::object dc_call2(nb::object callback, nb::object first, nb::object second) {
    return dc_checked(PyObject_CallFunctionObjArgs(callback.ptr(), first.ptr(), second.ptr(), nullptr));
}
nb::object dc_call3(nb::object callback, nb::object first, nb::object second, nb::object third) {
    return dc_checked(PyObject_CallFunctionObjArgs(callback.ptr(), first.ptr(), second.ptr(), third.ptr(), nullptr));
}
nb::object dc_kw(nb::object callback, nb::tuple args, nb::dict keywords) {
    return dc_checked(PyObject_Call(callback.ptr(), args.ptr(), keywords.ptr()));
}
bool dc_truth(nb::object value) {
    const int result = PyObject_IsTrue(value.ptr());
    if (result < 0) throw nb::python_error();
    return result != 0;
}
nb::object dc_compare(nb::object first, nb::object second, int op) {
    return dc_checked(PyObject_RichCompare(first.ptr(), second.ptr(), op));
}
nb::object dc_subtract(nb::object first, nb::object second) {
    return dc_checked(PyNumber_Subtract(first.ptr(), second.ptr()));
}
void dc_replace(nb::object& destination, nb::object value) {
    nb::object previous = std::move(destination);
    destination = std::move(value);
    previous.reset();
}
nb::object dc_optional(const nb::object& value) {
    return value.is_valid() ? value : nb::none();
}
nb::object dc_all_slice() {
    return dc_checked(PySlice_New(Py_None, Py_None, Py_None));
}

struct ControllerNamespace {
    nb::dict globals;
    nb::object builtins;
    explicit ControllerNamespace(nb::dict value)
        : globals(std::move(value)), builtins(nb::borrow<nb::object>(PyEval_GetBuiltins())) {}
    nb::object get(const char* name) const {
        nb::object key = nb::str(name);
        PyObject* value = PyDict_GetItemWithError(globals.ptr(), key.ptr());
        if (value) return nb::borrow<nb::object>(value);
        if (PyErr_Occurred()) throw nb::python_error();
        value = PyObject_GetItem(builtins.ptr(), key.ptr());
        if (value) return nb::steal<nb::object>(value);
        if (!PyErr_ExceptionMatches(PyExc_KeyError)) throw nb::python_error();
        PyErr_Clear();
        nb::object message = dc_checked(PyUnicode_FromFormat("name '%s' is not defined", name));
        nb::object error = dc_call1(nb::borrow<nb::object>(PyExc_NameError), std::move(message));
        if (PyObject_SetAttrString(error.ptr(), "name", key.ptr()) < 0) throw nb::python_error();
        PyErr_SetObject(PyExc_NameError, error.ptr());
        throw nb::python_error();
    }
    nb::object numpy(const char* name) const { return dc_attr(get("np"), name); }
};

// These edges are only the original assigned locals of four complete bodies.
// Parameters, canonical model dictionaries, caches, callbacks and namespaces
// are not duplicated here. Every field is detached before any release.
struct ControllerLocals {
    enum class Layout { Empty, Locked, Viewport, SliceInput, Request } layout = Layout::Empty;
    nb::object n_levels, displayed_axes, shape_at_level, corners, locked, old_level;
    nb::object level, scaled_corners, max_coords, display_shape, new_level, level_changed;
    nb::object changed, slice_input, data_slice;
    ControllerLocals() = default;
    ControllerLocals(ControllerLocals&&) = default;
    ~ControllerLocals() { clear(); }
    void clear() {
        std::array<nb::object, 15> previous = {
            std::move(n_levels), std::move(displayed_axes), std::move(shape_at_level),
            std::move(corners), std::move(locked), std::move(old_level), std::move(level),
            std::move(scaled_corners), std::move(max_coords), std::move(display_shape),
            std::move(new_level), std::move(level_changed), std::move(changed),
            std::move(slice_input), std::move(data_slice),
        };
        const Layout source_layout = layout;
        layout = Layout::Empty;
        if (source_layout == Layout::Viewport) {
            for (size_t index : {4u, 5u, 3u, 6u, 7u, 8u, 9u, 10u, 11u}) previous[index].reset();
        } else if (source_layout == Layout::SliceInput) {
            previous[12].reset();
        } else if (source_layout == Layout::Request) {
            previous[13].reset(); previous[14].reset();
        }
        // Locked's populated slots already have original source-local order.
        for (auto& edge : previous) edge.reset();
    }
};
int dc_locals_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) {
        const auto& s = *nb::inst_ptr<ControllerLocals>(self);
        Py_VISIT(s.n_levels.ptr()); Py_VISIT(s.displayed_axes.ptr()); Py_VISIT(s.shape_at_level.ptr());
        Py_VISIT(s.corners.ptr()); Py_VISIT(s.locked.ptr()); Py_VISIT(s.old_level.ptr());
        Py_VISIT(s.level.ptr()); Py_VISIT(s.scaled_corners.ptr()); Py_VISIT(s.max_coords.ptr());
        Py_VISIT(s.display_shape.ptr()); Py_VISIT(s.new_level.ptr()); Py_VISIT(s.level_changed.ptr());
        Py_VISIT(s.changed.ptr()); Py_VISIT(s.slice_input.ptr()); Py_VISIT(s.data_slice.ptr());
    }
    return 0;
}
int dc_locals_clear(PyObject* self) {
    if (nb::inst_ready(self)) nb::inst_ptr<ControllerLocals>(self)->clear();
    return 0;
}
PyType_Slot dc_locals_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(dc_locals_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(dc_locals_clear)}, {0, nullptr},
};

struct ZeroIterator {
    bool started = false, running = false, closed = false;
    nb::object iterator, s, namespace_, builtins;
    ZeroIterator() = default;
    ZeroIterator(ZeroIterator&&) = default;
    ~ZeroIterator() { clear(); }
    void clear() {
        closed = true;
        std::array<nb::object, 4> previous = {
            std::move(iterator), std::move(s), std::move(namespace_), std::move(builtins),
        };
        for (auto& edge : previous) edge.reset();
    }
};
struct ZeroRunning {
    ZeroIterator& state;
    explicit ZeroRunning(ZeroIterator& value) : state(value) {
        if (state.running) throw nb::value_error("generator already executing");
        state.running = true;
    }
    ~ZeroRunning() { state.running = false; }
};
nb::object dc_zero_next(nb::handle self) {
    auto& state = *nb::inst_ptr<ZeroIterator>(self);
    ZeroRunning running(state);
    if (state.closed) throw nb::stop_iteration();
    state.started = true;
    try {
        PyObject* next = PyIter_Next(state.iterator.ptr());
        if (!next) {
            if (PyErr_Occurred()) throw nb::python_error();
            state.clear();
            throw nb::stop_iteration();
        }
        dc_replace(state.s, nb::steal<nb::object>(next));
        return dc_compare(state.s, nb::int_(0), Py_EQ);
    } catch (const nb::python_error& error) {
        events::HandledException active(error.value());
        state.closed = true;
        // A saved genuine adapter traceback retains the original suspended
        // iterator/local edges; close/exhaustion/GC releases them normally.
        if (error.matches(PyExc_StopIteration)) collections::raise_exception(error.value(), true);
        throw;
    } catch (...) { state.clear(); throw; }
}
void dc_zero_close(nb::handle self) {
    auto& state = *nb::inst_ptr<ZeroIterator>(self);
    ZeroRunning running(state);
    if (state.closed) return;
    if (!state.started) { state.clear(); return; }
    nb::object exit = dc_call0(nb::borrow<nb::object>(PyExc_GeneratorExit));
    events::HandledException active(exit);
    state.clear();
}
void dc_zero_throw(nb::handle self, nb::args args) {
    nb::object exception = collections::normalize_throw(args);
    auto& state = *nb::inst_ptr<ZeroIterator>(self);
    ZeroRunning running(state);
    const bool open = !state.closed;
    state.closed = true;
    events::HandledException active(exception);
    collections::raise_exception(exception, open);
}
int dc_zero_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) {
        const auto& state = *nb::inst_ptr<ZeroIterator>(self);
        Py_VISIT(state.iterator.ptr()); Py_VISIT(state.s.ptr());
        Py_VISIT(state.namespace_.ptr()); Py_VISIT(state.builtins.ptr());
    }
    return 0;
}
int dc_zero_clear(PyObject* self) {
    if (nb::inst_ready(self)) nb::inst_ptr<ZeroIterator>(self)->clear();
    return 0;
}
PyType_Slot dc_zero_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(dc_zero_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(dc_zero_clear)}, {0, nullptr},
};

nb::object scalar_data_slice_dtype(nb::object self) { return dc_attr(self, "dtype"); }
nb::object scalar_data_view(nb::object self) {
    return dc_attr(dc_attr(dc_attr(self, "_slice"), "image"), "view");
}
nb::object scalar_data_slice(nb::object self) {
    return dc_attr(dc_attr(self, "_slicing_state"), "_slice");
}
nb::object scalar_data_raw(nb::object self) { return dc_attr(self, "_data_raw"); }
nb::object scalar_data_data(nb::object self) { return dc_attr(self, "_data"); }
nb::object scalar_data_level(nb::object self) { return dc_attr(self, "_data_level"); }
nb::object scalar_data_locked_level(nb::object self) { return dc_attr(self, "_locked_data_level"); }

void scalar_data_set(nb::object self, nb::object data, nb::dict namespace_) {
    ControllerNamespace ns(std::move(namespace_));
    dc_store(self, "_data_raw", data);
    if (dc_truth(dc_attr(self, "multiscale"))) {
        dc_store(self, "_data", dc_call1(ns.get("MultiScaleData"), data));
    } else {
        dc_store(self, "_data", data);
    }
    dc_call0(dc_attr(self, "_reset_data_level"));
    dc_call0(dc_attr(self, "_reset_thumbnail_level_data"));
    dc_call0(dc_attr(self, "_update_dims"));
    {
        nb::object callback = dc_attr(dc_attr(self, "events"), "data");
        nb::dict keywords;
        keywords["value"] = dc_attr(self, "data");
        dc_kw(std::move(callback), nb::tuple(), std::move(keywords));
    }
    dc_call0(dc_attr(self, "_reset_editable"));
}
void scalar_data_set_level(nb::object self, nb::object level) {
    if (dc_truth(dc_compare(dc_attr(self, "_data_level"), level, Py_EQ))) return;
    dc_store(self, "_data_level", level);
    nb::object callback = dc_attr(self, "refresh");
    nb::dict keywords; keywords["extent"] = nb::bool_(false);
    dc_kw(std::move(callback), nb::tuple(), std::move(keywords));
}
void scalar_data_set_locked_level(nb::object self, nb::object level,
                                  nb::dict namespace_, ControllerLocals& owner) {
    owner.layout = ControllerLocals::Layout::Locked;
    ControllerNamespace ns(std::move(namespace_));
    if (!level.is_none()) {
        nb::object length = ns.get("len");
        dc_replace(owner.n_levels, dc_call1(std::move(length), dc_attr(self, "level_shapes")));
        if (dc_truth(dc_compare(level, nb::int_(0), Py_LT)) ||
            dc_truth(dc_compare(level, owner.n_levels, Py_GE))) return;
    }
    dc_store(self, "_locked_data_level", level);
    if (!level.is_none()) {
        dc_replace(owner.displayed_axes, dc_attr(dc_attr(self, "_slice_input"), "displayed"));
        {
            nb::object array = ns.numpy("array");
            dc_replace(owner.shape_at_level, dc_call1(std::move(array), dc_item(dc_attr(self, "level_shapes"), level)));
        }
        {
            nb::object zeros = ns.numpy("zeros");
            nb::tuple shape = nb::make_tuple(nb::int_(2), dc_attr(self, "ndim"));
            nb::dict keywords; keywords["dtype"] = ns.get("int");
            dc_replace(owner.corners, dc_kw(std::move(zeros), nb::make_tuple(std::move(shape)), std::move(keywords)));
        }
        {
            nb::object value = dc_subtract(dc_item(owner.shape_at_level, owner.displayed_axes), nb::int_(1));
            dc_set_item(owner.corners, nb::make_tuple(nb::int_(1), owner.displayed_axes), std::move(value));
        }
        dc_store(self, "corner_pixels", owner.corners);
        dc_store(self, "_data_level", level);
    } else {
        dc_call0(dc_attr(self, "_reset_data_level"));
    }
    {
        nb::object callback = dc_attr(self, "refresh");
        nb::dict keywords; keywords["extent"] = nb::bool_(false);
        dc_kw(std::move(callback), nb::tuple(), std::move(keywords));
    }
    dc_call0(dc_attr(dc_attr(self, "events"), "locked_data_level"));
}
void scalar_data_reset_level(nb::object self, nb::dict namespace_) {
    ControllerNamespace ns(std::move(namespace_));
    dc_store(self, "_locked_data_level", nb::none());
    nb::object check = ns.get("isinstance");
    nb::object data = dc_attr(self, "_data");
    nb::object type = ns.get("MultiScaleData");
    if (dc_truth(dc_call2(std::move(check), std::move(data), std::move(type)))) {
        nb::object length = ns.get("len");
        dc_store(self, "_data_level", dc_subtract(dc_call1(std::move(length), dc_attr(self, "_data")), nb::int_(1)));
    } else {
        dc_store(self, "_data_level", nb::int_(0));
    }
}

std::array<nb::object, 2> dc_unpack_pair(nb::object value) {
    nb::object iterator = nb::steal<nb::object>(PyObject_GetIter(value.ptr()));
    if (!iterator.is_valid()) {
        if (PyErr_ExceptionMatches(PyExc_TypeError) && !Py_TYPE(value.ptr())->tp_iter && !PySequence_Check(value.ptr()))
            PyErr_Format(PyExc_TypeError, "cannot unpack non-iterable %.200s object", Py_TYPE(value.ptr())->tp_name);
        throw nb::python_error();
    }
    // UNPACK_SEQUENCE keeps its source result alive until unpacking has
    // completed (including an error). Release this parameter on return,
    // before the caller publishes either original named local.
    std::array<nb::object, 2> values;
    for (Py_ssize_t i = 0; i < 2; ++i) {
        values[static_cast<size_t>(i)] = nb::steal<nb::object>(PyIter_Next(iterator.ptr()));
        if (!values[static_cast<size_t>(i)].is_valid()) {
            if (PyErr_Occurred()) throw nb::python_error();
            PyErr_Format(PyExc_ValueError, "not enough values to unpack (expected 2, got %zd)", i);
            throw nb::python_error();
        }
    }
    nb::object extra = nb::steal<nb::object>(PyIter_Next(iterator.ptr()));
    if (extra.is_valid()) throw nb::value_error("too many values to unpack (expected 2)");
    if (PyErr_Occurred()) throw nb::python_error();
    return values;
}
nb::object dc_zeros_for_layer(nb::object self, ControllerNamespace& ns) {
    nb::object zeros = ns.numpy("zeros");
    nb::tuple shape = nb::make_tuple(nb::int_(2), dc_attr(self, "ndim"));
    nb::dict keywords; keywords["dtype"] = ns.get("int");
    return dc_kw(std::move(zeros), nb::make_tuple(std::move(shape)), std::move(keywords));
}
nb::object dc_level_extent(nb::object self, nb::object level,
                          nb::object displayed_axes, ControllerNamespace& ns) {
    nb::object take = ns.numpy("take");
    nb::object shape = dc_attr(dc_item(dc_attr(self, "data"), std::move(level)), "shape");
    return dc_subtract(dc_call2(std::move(take), std::move(shape), std::move(displayed_axes)), nb::int_(1));
}
void dc_refresh_viewport(nb::object self) {
    nb::object callback = dc_attr(self, "refresh");
    nb::dict keywords;
    keywords["extent"] = nb::bool_(false);
    keywords["thumbnail"] = nb::bool_(false);
    dc_kw(std::move(callback), nb::tuple(), std::move(keywords));
}
bool dc_any_zero(ControllerNamespace& ns, const nb::object& display_shape,
                 nb::object generator_factory) {
    nb::object consumer = ns.get("any");
    nb::object iterator = dc_checked(PyObject_GetIter(display_shape.ptr()));
    ZeroIterator state;
    state.iterator = std::move(iterator);
    state.namespace_ = ns.globals;
    state.builtins = ns.builtins;
    nb::object delegate = nb::cast(std::move(state));
    nb::object generator = dc_call1(std::move(generator_factory), std::move(delegate));
    dc_store(generator, "__name__", nb::str("<genexpr>"));
    dc_store(generator, "__qualname__", nb::str("ScalarFieldBase._update_level_and_corners.<locals>.<genexpr>"));
    return dc_truth(dc_call1(std::move(consumer), std::move(generator)));
}
void scalar_data_update_viewport(nb::object self, nb::object data_bbox_int,
                                 nb::object shape_threshold, nb::object displayed_axes,
                                 nb::dict namespace_, nb::object super_provider,
                                 nb::object generator_factory, ControllerLocals& owner) {
    owner.layout = ControllerLocals::Layout::Viewport;
    ControllerNamespace ns(std::move(namespace_));
    if (!dc_truth(dc_attr(self, "multiscale"))) {
        nb::object callback = dc_call1(std::move(super_provider), self);
        dc_call3(std::move(callback), data_bbox_int, shape_threshold, displayed_axes);
        return;
    }
    if (!dc_attr(self, "_locked_data_level").is_none()) {
        dc_replace(owner.locked, dc_attr(self, "_locked_data_level"));
        dc_replace(owner.old_level, dc_attr(self, "_data_level"));
        dc_store(self, "_data_level", owner.locked);
        dc_replace(owner.corners, dc_zeros_for_layer(self, ns));
        {
            nb::object value = dc_level_extent(self, owner.locked, displayed_axes, ns);
            dc_set_item(owner.corners, nb::make_tuple(nb::int_(1), displayed_axes), std::move(value));
        }
        dc_store(self, "corner_pixels", owner.corners);
        if (dc_truth(dc_compare(owner.old_level, owner.locked, Py_NE))) dc_refresh_viewport(self);
    } else if (dc_truth(dc_compare(dc_attr(dc_attr(self, "_slice_input"), "ndisplay"), nb::int_(2), Py_EQ))) {
        {
            nb::object callback = ns.get("compute_multiscale_level_and_corners");
            nb::object factors = dc_item(dc_attr(self, "downsample_factors"), nb::make_tuple(dc_all_slice(), displayed_axes));
            auto pair = dc_unpack_pair(dc_call3(std::move(callback), data_bbox_int, shape_threshold, std::move(factors)));
            dc_replace(owner.level, std::move(pair[0]));
            dc_replace(owner.scaled_corners, std::move(pair[1]));
        }
        dc_replace(owner.corners, dc_zeros_for_layer(self, ns));
        dc_replace(owner.max_coords, dc_level_extent(self, owner.level, displayed_axes, ns));
        {
            nb::object clip = ns.numpy("clip");
            nb::object value = dc_call3(std::move(clip), owner.scaled_corners, nb::int_(0), owner.max_coords);
            dc_set_item(owner.corners, nb::make_tuple(dc_all_slice(), displayed_axes), std::move(value));
        }
        {
            nb::object consumer = ns.get("tuple");
            nb::object left = dc_item(owner.corners, nb::make_tuple(nb::int_(1), displayed_axes));
            nb::object right = dc_item(owner.corners, nb::make_tuple(nb::int_(0), displayed_axes));
            dc_replace(owner.display_shape, dc_call1(std::move(consumer), dc_subtract(std::move(left), std::move(right))));
        }
        if (dc_any_zero(ns, owner.display_shape, std::move(generator_factory))) return;
        {
            nb::object callback = ns.get("expand_corners_to_chunk_boundaries");
            nb::object data = dc_item(dc_attr(self, "data"), owner.level);
            dc_replace(owner.corners, dc_call3(std::move(callback), owner.corners, std::move(data), displayed_axes));
        }
        bool update = dc_truth(dc_compare(dc_attr(self, "data_level"), owner.level, Py_NE));
        if (!update) {
            nb::object any = ns.numpy("any");
            nb::object left = dc_item(owner.corners, nb::make_tuple(nb::int_(0), displayed_axes));
            nb::object right = dc_item(dc_attr(self, "corner_pixels"), nb::make_tuple(nb::int_(0), displayed_axes));
            update = dc_truth(dc_call1(std::move(any), dc_compare(std::move(left), std::move(right), Py_LT)));
        }
        if (!update) {
            nb::object any = ns.numpy("any");
            nb::object left = dc_item(owner.corners, nb::make_tuple(nb::int_(1), displayed_axes));
            nb::object right = dc_item(dc_attr(self, "corner_pixels"), nb::make_tuple(nb::int_(1), displayed_axes));
            update = dc_truth(dc_call1(std::move(any), dc_compare(std::move(left), std::move(right), Py_GT)));
        }
        if (update) {
            dc_store(self, "_data_level", owner.level);
            dc_store(self, "corner_pixels", owner.corners);
            dc_refresh_viewport(self);
        }
    } else {
        {
            nb::object length = ns.get("len");
            dc_replace(owner.new_level, dc_subtract(dc_call1(std::move(length), dc_attr(self, "level_shapes")), nb::int_(1)));
        }
        dc_replace(owner.level_changed, dc_compare(dc_attr(self, "_data_level"), owner.new_level, Py_NE));
        dc_store(self, "_data_level", owner.new_level);
        dc_replace(owner.corners, dc_zeros_for_layer(self, ns));
        {
            nb::object value = dc_level_extent(self, owner.new_level, displayed_axes, ns);
            dc_set_item(owner.corners, nb::make_tuple(nb::int_(1), displayed_axes), std::move(value));
        }
        dc_store(self, "corner_pixels", owner.corners);
        if (dc_truth(owner.level_changed)) dc_refresh_viewport(self);
    }
}

nb::object scalar_data_set_slice_input(nb::object self, nb::object slice_input,
                                      nb::object force, nb::dict namespace_,
                                      nb::object super_provider, ControllerLocals& owner) {
    owner.layout = ControllerLocals::Layout::SliceInput;
    ControllerNamespace ns(std::move(namespace_));
    {
        nb::object callback = dc_call1(std::move(super_provider), self);
        dc_replace(owner.changed, dc_call2(std::move(callback), slice_input, force));
    }
    if (dc_truth(owner.changed) && !dc_truth(dc_attr(dc_attr(self, "layer"), "visible"))) {
        nb::object factory = dc_attr(ns.get("_ScalarFieldSliceResponse"), "make_empty");
        nb::dict keywords;
        keywords["slice_input"] = dc_attr(self, "_slice_input");
        {
            nb::object length = ns.get("len");
            nb::object shape = dc_attr(dc_attr(dc_attr(self, "layer"), "data"), "shape");
            nb::object count = dc_call1(std::move(length), std::move(shape));
            keywords["rgb"] = dc_compare(std::move(count), dc_attr(self, "ndim"), Py_NE);
        }
        keywords["dtype"] = dc_call0(dc_attr(dc_attr(self, "layer"), "_slice_dtype"));
        dc_store(self, "_slice", dc_kw(std::move(factory), nb::tuple(), std::move(keywords)));
    }
    return owner.changed;
}
nb::object scalar_data_make_request(nb::object self, nb::object dims, ControllerLocals& owner) {
    owner.layout = ControllerLocals::Layout::Request;
    dc_replace(owner.slice_input, dc_call1(dc_attr(self, "make_slice_input"), dims));
    dc_replace(owner.data_slice, dc_call2(dc_attr(self, "_slice_indices"), owner.slice_input, dims));
    nb::object callback = dc_attr(self, "_make_slice_request_internal");
    nb::dict keywords;
    keywords["slice_input"] = owner.slice_input;
    keywords["data_slice"] = owner.data_slice;
    keywords["dask_indexer"] = dc_attr(self, "dask_optimized_slicing");
    return dc_kw(std::move(callback), nb::tuple(), std::move(keywords));
}
void scalar_data_image_set(nb::object self, nb::object data, nb::dict namespace_) {
    ControllerNamespace ns(std::move(namespace_));
    dc_call2(dc_attr(dc_attr(ns.get("ScalarFieldBase"), "data"), "fset"), self, data);
    if (dc_truth(dc_attr(self, "auto_contrast"))) dc_call0(dc_attr(self, "reset_contrast_limits"));
}

} // namespace

void bind_scalar_data_controller(nb::module_& module) {
    nb::class_<ControllerLocals>(module, "ScalarDataControllerLocals", nb::is_weak_referenceable(),
        nb::type_slots(dc_locals_slots))
        .def_prop_ro("n_levels", [](const ControllerLocals& s) { return dc_optional(s.n_levels); })
        .def_prop_ro("displayed_axes", [](const ControllerLocals& s) { return dc_optional(s.displayed_axes); })
        .def_prop_ro("shape_at_level", [](const ControllerLocals& s) { return dc_optional(s.shape_at_level); })
        .def_prop_ro("corners", [](const ControllerLocals& s) { return dc_optional(s.corners); })
        .def_prop_ro("locked", [](const ControllerLocals& s) { return dc_optional(s.locked); })
        .def_prop_ro("old_level", [](const ControllerLocals& s) { return dc_optional(s.old_level); })
        .def_prop_ro("level", [](const ControllerLocals& s) { return dc_optional(s.level); })
        .def_prop_ro("scaled_corners", [](const ControllerLocals& s) { return dc_optional(s.scaled_corners); })
        .def_prop_ro("max_coords", [](const ControllerLocals& s) { return dc_optional(s.max_coords); })
        .def_prop_ro("display_shape", [](const ControllerLocals& s) { return dc_optional(s.display_shape); })
        .def_prop_ro("new_level", [](const ControllerLocals& s) { return dc_optional(s.new_level); })
        .def_prop_ro("level_changed", [](const ControllerLocals& s) { return dc_optional(s.level_changed); })
        .def_prop_ro("changed", [](const ControllerLocals& s) { return dc_optional(s.changed); })
        .def_prop_ro("slice_input", [](const ControllerLocals& s) { return dc_optional(s.slice_input); })
        .def_prop_ro("data_slice", [](const ControllerLocals& s) { return dc_optional(s.data_slice); });
    nb::class_<ZeroIterator>(module, "ScalarDataZeroIterator", nb::is_weak_referenceable(),
        nb::type_slots(dc_zero_slots))
        .def("__iter__", [](nb::handle self) { return nb::borrow<nb::object>(self); })
        .def("__next__", &dc_zero_next)
        .def("send", [](nb::handle self, nb::object value) {
            const auto& state = *nb::inst_ptr<ZeroIterator>(self);
            if (!state.started && !state.closed && !value.is_none())
                throw nb::type_error("can't send non-None value to a just-started generator");
            return dc_zero_next(self);
        })
        .def("throw", &dc_zero_throw)
        .def("close", &dc_zero_close);
    module.def("scalar_data_controller_locals", [] { return ControllerLocals(); });
    module.def("scalar_data_slice_dtype", &scalar_data_slice_dtype, nb::arg("self").none());
    module.def("scalar_data_view", &scalar_data_view, nb::arg("self").none());
    module.def("scalar_data_slice", &scalar_data_slice, nb::arg("self").none());
    module.def("scalar_data_raw", &scalar_data_raw, nb::arg("self").none());
    module.def("scalar_data_data", &scalar_data_data, nb::arg("self").none());
    module.def("scalar_data_level", &scalar_data_level, nb::arg("self").none());
    module.def("scalar_data_locked_level", &scalar_data_locked_level, nb::arg("self").none());
    module.def("scalar_data_set", &scalar_data_set, nb::arg("self").none(), nb::arg("data").none(), nb::arg("namespace"));
    module.def("scalar_data_set_level", &scalar_data_set_level, nb::arg("self").none(), nb::arg("level").none());
    module.def("scalar_data_set_locked_level", &scalar_data_set_locked_level,
        nb::arg("self").none(), nb::arg("level").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("scalar_data_reset_level", &scalar_data_reset_level, nb::arg("self").none(), nb::arg("namespace"));
    module.def("scalar_data_update_viewport", &scalar_data_update_viewport,
        nb::arg("self").none(), nb::arg("data_bbox_int").none(), nb::arg("shape_threshold").none(),
        nb::arg("displayed_axes").none(), nb::arg("namespace"), nb::arg("super_provider").none(),
        nb::arg("generator_factory").none(), nb::arg("locals_owner"));
    module.def("scalar_data_set_slice_input", &scalar_data_set_slice_input,
        nb::arg("self").none(), nb::arg("slice_input").none(), nb::arg("force").none(),
        nb::arg("namespace"), nb::arg("super_provider").none(), nb::arg("locals_owner"));
    module.def("scalar_data_make_request", &scalar_data_make_request,
        nb::arg("self").none(), nb::arg("dims").none(), nb::arg("locals_owner"));
    module.def("scalar_data_image_set", &scalar_data_image_set,
        nb::arg("self").none(), nb::arg("data").none(), nb::arg("namespace"));
}
} // namespace nx
