#include "arrays.hpp"
#include "collection_protocol.hpp"

#include <array>
#include <utility>

namespace nx {
namespace {

nb::object vp_checked(PyObject* value) {
    if (!value) throw nb::python_error();
    return nb::steal<nb::object>(value);
}
nb::object vp_attr(nb::object value, const char* name) {
    return vp_checked(PyObject_GetAttrString(value.ptr(), name));
}
nb::object vp_item(nb::object value, nb::object key) {
    return vp_checked(PyObject_GetItem(value.ptr(), key.ptr()));
}
void vp_set_attr(nb::object value, const char* name, nb::object replacement) {
    if (PyObject_SetAttrString(value.ptr(), name, replacement.ptr()) < 0) throw nb::python_error();
}
void vp_set_item(nb::object value, nb::object key, nb::object replacement) {
    if (PyObject_SetItem(value.ptr(), key.ptr(), replacement.ptr()) < 0) throw nb::python_error();
}
nb::object vp_call0(nb::object callback) {
    return vp_checked(PyObject_CallNoArgs(callback.ptr()));
}
nb::object vp_call1(nb::object callback, nb::object value) {
    return vp_checked(PyObject_CallOneArg(callback.ptr(), value.ptr()));
}
nb::object vp_call2(nb::object callback, nb::object a, nb::object b) {
    return vp_checked(PyObject_CallFunctionObjArgs(callback.ptr(), a.ptr(), b.ptr(), nullptr));
}
nb::object vp_call3(nb::object callback, nb::object a, nb::object b, nb::object c) {
    return vp_checked(PyObject_CallFunctionObjArgs(callback.ptr(), a.ptr(), b.ptr(), c.ptr(), nullptr));
}
nb::object vp_kw(nb::object callback, nb::tuple args, nb::dict kwargs) {
    return vp_checked(PyObject_Call(callback.ptr(), args.ptr(), kwargs.ptr()));
}
nb::tuple vp_pack1(nb::object value) { return nb::make_tuple(value); }
void vp_append(nb::list& values, nb::object value) { values.append(value); }
bool vp_truth(nb::object value) {
    const int result = PyObject_IsTrue(value.ptr());
    if (result < 0) throw nb::python_error();
    return result != 0;
}
nb::object vp_compare(nb::object a, nb::object b, int op) {
    return vp_checked(PyObject_RichCompare(a.ptr(), b.ptr(), op));
}
nb::object vp_subtract(nb::object a, nb::object b) {
    return vp_checked(PyNumber_Subtract(a.ptr(), b.ptr()));
}
nb::object vp_add(nb::object a, nb::object b) {
    return vp_checked(PyNumber_Add(a.ptr(), b.ptr()));
}
nb::object vp_divide(nb::object a, nb::object b) {
    return vp_checked(PyNumber_TrueDivide(a.ptr(), b.ptr()));
}
nb::object vp_slice(nb::object start = nb::none(), nb::object stop = nb::none()) {
    return vp_checked(PySlice_New(start.ptr(), stop.ptr(), Py_None));
}
nb::object vp_literal(const char* text) {
    return vp_checked(PyUnicode_InternFromString(text));
}
void vp_replace(nb::object& destination, nb::object value) {
    nb::object previous = std::move(destination);
    destination = std::move(value);
    previous.reset();
}
nb::object vp_optional(const nb::object& value) {
    return value.is_valid() ? value : nb::none();
}
struct ViewportNamespace {
    nb::dict globals;
    nb::object builtins;
    explicit ViewportNamespace(nb::dict value)
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
        nb::object message = vp_checked(PyUnicode_FromFormat("name '%s' is not defined", name));
        nb::object error = vp_call1(nb::borrow<nb::object>(PyExc_NameError), std::move(message));
        if (PyObject_SetAttrString(error.ptr(), "name", key.ptr()) < 0) throw nb::python_error();
        PyErr_SetObject(PyExc_NameError, error.ptr());
        throw nb::python_error();
    }
    nb::object numpy(const char* name) const { return vp_attr(get("np"), name); }
};

// These per-call references are original assigned locals, not model state.
// All persistent state remains in the existing live Layer/state dictionary.
struct LayerViewportLocals {
    nb::object world_to_data, slice_input, world_slice, order_array, order, prev;
    nb::object ndim, old_ndim, keep_axes, new_axes, changed, displayed_axes;
    nb::object all_corners, data_corners, data_bbox, data_bbox_int, corners;
    nb::object displayed_extent, data_bbox_clipped;
    LayerViewportLocals() = default;
    LayerViewportLocals(LayerViewportLocals&&) = default;
    ~LayerViewportLocals() { clear(); }
    void clear() {
        std::array<nb::object, 19> old = {
            std::move(world_to_data), std::move(slice_input), std::move(world_slice),
            std::move(order_array), std::move(order), std::move(prev), std::move(ndim),
            std::move(old_ndim), std::move(keep_axes), std::move(new_axes), std::move(changed),
            std::move(displayed_axes), std::move(all_corners), std::move(data_corners),
            std::move(data_bbox), std::move(data_bbox_int), std::move(corners),
            std::move(displayed_extent), std::move(data_bbox_clipped),
        };
        for (auto& value : old) value.reset();
    }
};
struct ScalarViewportPolicyLocals {
    enum class CleanupLayout { Default, Expand } cleanup_layout = CleanupLayout::Default;
    nb::object scaled_shape, exceeds_threshold, is_downsampled, active_dims, locations;
    nb::object requested_shape, level, corners, chunk_sizes, boundaries, chunk_size;
    nb::object chunks, layout, read_chunk, grid_origin, shape, expanded, axis;
    nb::object axis_size, axis_max, start, stop, start_chunk, stop_chunk;
    ScalarViewportPolicyLocals() = default;
    ScalarViewportPolicyLocals(ScalarViewportPolicyLocals&&) = default;
    ~ScalarViewportPolicyLocals() { clear(); }
    void clear() {
        std::array<nb::object, 24> old = {
            std::move(scaled_shape), std::move(exceeds_threshold), std::move(is_downsampled),
            std::move(active_dims), std::move(locations), std::move(requested_shape),
            std::move(level), std::move(corners), std::move(chunk_sizes), std::move(boundaries),
            std::move(chunk_size), std::move(chunks), std::move(layout), std::move(read_chunk),
            std::move(grid_origin), std::move(shape), std::move(expanded), std::move(axis),
            std::move(axis_size), std::move(axis_max), std::move(start), std::move(stop),
            std::move(start_chunk), std::move(stop_chunk),
        };
        const CleanupLayout source_layout = cleanup_layout;
        cleanup_layout = CleanupLayout::Default;
        if (source_layout == CleanupLayout::Expand) {
            for (size_t index : {11u, 15u, 16u, 17u, 18u, 9u, 19u, 20u, 21u, 22u, 23u})
                old[index].reset();
        }
        for (auto& value : old) value.reset();
    }
};
int vp_layer_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) {
        const auto& s = *nb::inst_ptr<LayerViewportLocals>(self);
        Py_VISIT(s.world_to_data.ptr()); Py_VISIT(s.slice_input.ptr()); Py_VISIT(s.world_slice.ptr());
        Py_VISIT(s.order_array.ptr()); Py_VISIT(s.order.ptr()); Py_VISIT(s.prev.ptr());
        Py_VISIT(s.ndim.ptr()); Py_VISIT(s.old_ndim.ptr()); Py_VISIT(s.keep_axes.ptr());
        Py_VISIT(s.new_axes.ptr()); Py_VISIT(s.changed.ptr()); Py_VISIT(s.displayed_axes.ptr());
        Py_VISIT(s.all_corners.ptr()); Py_VISIT(s.data_corners.ptr()); Py_VISIT(s.data_bbox.ptr());
        Py_VISIT(s.data_bbox_int.ptr()); Py_VISIT(s.corners.ptr()); Py_VISIT(s.displayed_extent.ptr());
        Py_VISIT(s.data_bbox_clipped.ptr());
    }
    return 0;
}
int vp_policy_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) {
        const auto& s = *nb::inst_ptr<ScalarViewportPolicyLocals>(self);
        Py_VISIT(s.scaled_shape.ptr()); Py_VISIT(s.exceeds_threshold.ptr()); Py_VISIT(s.is_downsampled.ptr());
        Py_VISIT(s.active_dims.ptr()); Py_VISIT(s.locations.ptr()); Py_VISIT(s.requested_shape.ptr());
        Py_VISIT(s.level.ptr()); Py_VISIT(s.corners.ptr()); Py_VISIT(s.chunk_sizes.ptr());
        Py_VISIT(s.boundaries.ptr()); Py_VISIT(s.chunk_size.ptr()); Py_VISIT(s.chunks.ptr());
        Py_VISIT(s.layout.ptr()); Py_VISIT(s.read_chunk.ptr()); Py_VISIT(s.grid_origin.ptr());
        Py_VISIT(s.shape.ptr()); Py_VISIT(s.expanded.ptr()); Py_VISIT(s.axis.ptr());
        Py_VISIT(s.axis_size.ptr()); Py_VISIT(s.axis_max.ptr()); Py_VISIT(s.start.ptr());
        Py_VISIT(s.stop.ptr()); Py_VISIT(s.start_chunk.ptr()); Py_VISIT(s.stop_chunk.ptr());
    }
    return 0;
}
template <typename Owner> int vp_clear(PyObject* self) {
    if (nb::inst_ready(self)) nb::inst_ptr<Owner>(self)->clear();
    return 0;
}
PyType_Slot vp_layer_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(vp_layer_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(vp_clear<LayerViewportLocals>)}, {0, nullptr},
};
PyType_Slot vp_policy_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(vp_policy_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(vp_clear<ScalarViewportPolicyLocals>)}, {0, nullptr},
};

nb::object layer_viewport_slice_indices(nb::object self, nb::object slice_input,
    nb::object dims, nb::dict namespace_, LayerViewportLocals& s) {
    ViewportNamespace ns(std::move(namespace_));
    vp_replace(s.world_to_data, vp_attr(vp_attr(vp_attr(self, "layer"), "_data_to_world"), "inverse"));
    nb::object apply = ns.get("apply_units_to_transform");
    nb::object transform = s.world_to_data;
    nb::object units = vp_attr(std::move(dims), "units");
    vp_replace(s.world_to_data, vp_call2(std::move(apply), std::move(transform), std::move(units)));
    return vp_call1(vp_attr(std::move(slice_input), "data_slice"), s.world_to_data);
}
void layer_viewport_state_update_dims(nb::object self) {
    nb::object with_ndim = vp_attr(vp_attr(self, "_slice_input"), "with_ndim");
    nb::object ndim = vp_attr(self, "ndim");
    nb::object slice_input = vp_call1(std::move(with_ndim), std::move(ndim));
    vp_set_attr(std::move(self), "_slice_input", std::move(slice_input));
}
nb::object layer_viewport_set_input_from_dims(nb::object self, nb::object dims,
    nb::object force, LayerViewportLocals& s) {
    vp_replace(s.slice_input, vp_call1(vp_attr(self, "make_slice_input"), dims));
    nb::object units = vp_attr(std::move(dims), "units");
    vp_set_attr(self, "_units", std::move(units));
    return vp_call2(vp_attr(std::move(self), "set_slice_input"), s.slice_input, std::move(force));
}
bool layer_viewport_set_input(nb::object self, nb::object slice_input,
    nb::object force, nb::dict namespace_) {
    ViewportNamespace ns(std::move(namespace_));
    if (vp_truth(vp_compare(vp_attr(self, "_slice_input"), slice_input, Py_EQ)) && !vp_truth(force))
        return false;
    nb::object deepcopy = vp_attr(ns.get("copy"), "deepcopy");
    nb::object copied = vp_call1(std::move(deepcopy), std::move(slice_input));
    vp_set_attr(self, "_slice_input", std::move(copied));
    if (vp_truth(vp_attr(vp_attr(self, "layer"), "visible")) && !vp_truth(force))
        vp_call0(vp_attr(std::move(self), "set_view_slice"));
    return true;
}
nb::object layer_viewport_state_ndim(nb::object self) {
    return vp_attr(vp_attr(std::move(self), "layer"), "ndim");
}
nb::object layer_viewport_make_input(nb::object self, nb::object dims,
    nb::dict namespace_, LayerViewportLocals& s) {
    ViewportNamespace ns(std::move(namespace_));
    nb::object thick = ns.get("_ThickNDSlice");
    nb::object float_ = ns.get("float");
    nb::object from_dims = vp_attr(vp_item(std::move(thick), std::move(float_)), "from_dims");
    vp_replace(s.world_slice, vp_call1(std::move(from_dims), dims));
    if (vp_attr(dims, "order").is_none()) {
        nb::object arange = ns.numpy("arange");
        vp_replace(s.order_array, vp_call1(std::move(arange), vp_attr(dims, "ndim")));
    } else {
        nb::object asarray = ns.numpy("asarray");
        vp_replace(s.order_array, vp_call1(std::move(asarray), vp_attr(dims, "order")));
    }
    nb::object tuple = ns.get("tuple");
    nb::object map = vp_attr(self, "_world_to_layer_dims");
    nb::dict map_kwargs;
    map_kwargs["world_dims"] = s.order_array;
    map_kwargs["ndim_world"] = vp_attr(dims, "ndim");
    vp_replace(s.order, vp_call1(std::move(tuple), vp_kw(std::move(map), nb::tuple(), std::move(map_kwargs))));
    nb::object cls = ns.get("_SliceInput");
    nb::dict kwargs;
    kwargs["ndisplay"] = vp_attr(std::move(dims), "ndisplay");
    nb::object negative = vp_checked(PyNumber_Negative(vp_attr(self, "ndim").ptr()));
    kwargs["world_slice"] = vp_item(s.world_slice, vp_slice(std::move(negative)));
    negative = vp_checked(PyNumber_Negative(vp_attr(self, "ndim").ptr()));
    kwargs["order"] = vp_item(s.order, vp_slice(std::move(negative)));
    return vp_kw(std::move(cls), nb::tuple(), std::move(kwargs));
}
nb::object layer_viewport_state_world_dims(nb::object self, nb::object world_dims, nb::object ndim_world) {
    nb::object callback = vp_attr(self, "_world_to_layer_dims_impl");
    nb::object ndim = vp_attr(std::move(self), "ndim");
    return vp_call3(std::move(callback), std::move(world_dims), std::move(ndim_world), std::move(ndim));
}
void layer_viewport_units_set(nb::object self, nb::object units, LayerViewportLocals& s) {
    vp_replace(s.prev, vp_attr(self, "units"));
    nb::object target = vp_item(vp_attr(self, "_transforms"), vp_literal("data2physical"));
    vp_set_attr(std::move(target), "units", std::move(units));
    if (vp_truth(vp_compare(vp_attr(self, "units"), s.prev, Py_NE))) {
        vp_call0(vp_attr(self, "_clear_extent"));
        nb::object refresh = vp_attr(self, "refresh");
        nb::dict kwargs;
        kwargs["extent"] = nb::bool_(false);
        vp_kw(std::move(refresh), nb::tuple(), std::move(kwargs));
        vp_call0(vp_attr(vp_attr(std::move(self), "events"), "units"));
    }
}
void layer_viewport_update_dims(nb::object self, nb::dict namespace_, LayerViewportLocals& s) {
    ViewportNamespace ns(std::move(namespace_));
    vp_replace(s.ndim, vp_call0(vp_attr(self, "_get_ndim")));
    vp_replace(s.old_ndim, vp_attr(self, "_ndim"));
    if (vp_truth(vp_compare(s.old_ndim, s.ndim, Py_GT))) {
        nb::object range = ns.get("range");
        nb::object offset = vp_subtract(s.old_ndim, s.ndim);
        vp_replace(s.keep_axes, vp_call2(std::move(range), std::move(offset), s.old_ndim));
        nb::object set_slice = vp_attr(vp_attr(self, "_transforms"), "set_slice");
        vp_set_attr(self, "_transforms", vp_call1(std::move(set_slice), s.keep_axes));
    } else if (vp_truth(vp_compare(s.old_ndim, s.ndim, Py_LT))) {
        nb::object range = ns.get("range");
        nb::object count = vp_subtract(s.ndim, s.old_ndim);
        vp_replace(s.new_axes, vp_call1(std::move(range), std::move(count)));
        nb::object expand = vp_attr(vp_attr(self, "_transforms"), "expand_dims");
        vp_set_attr(self, "_transforms", vp_call1(std::move(expand), s.new_axes));
    }
    vp_set_attr(self, "_ndim", s.ndim);
    vp_call0(vp_attr(vp_attr(self, "_slicing_state"), "update_dims"));
    vp_call0(vp_attr(std::move(self), "refresh"));
}
void layer_viewport_slice_dims(nb::object self, nb::object dims, nb::object force,
    nb::dict namespace_, nb::object debug_message, LayerViewportLocals& s) {
    ViewportNamespace ns(std::move(namespace_));
    nb::object debug = vp_attr(ns.get("logger"), "debug");
    vp_kw(std::move(debug), nb::make_tuple(std::move(debug_message), self, dims, force), nb::dict());
    nb::object set_input = vp_attr(vp_attr(self, "_slicing_state"), "set_slice_input_from_dims");
    vp_replace(s.changed, vp_call2(std::move(set_input), std::move(dims), force));
    if (vp_truth(force) || vp_truth(s.changed)) {
        nb::object refresh = vp_attr(std::move(self), "_refresh_sync");
        nb::dict kwargs;
        kwargs["data_displayed"] = nb::bool_(true);
        kwargs["thumbnail"] = nb::bool_(true);
        kwargs["highlight"] = nb::bool_(true);
        kwargs["extent"] = nb::bool_(true);
        vp_kw(std::move(refresh), nb::tuple(), std::move(kwargs));
    }
}
void layer_viewport_update_draw(nb::object self, nb::object scale_factor,
    nb::object corner_pixels_displayed, nb::object shape_threshold,
    nb::dict namespace_, LayerViewportLocals& s) {
    ViewportNamespace ns(std::move(namespace_));
    vp_set_attr(self, "scale_factor", std::move(scale_factor));
    vp_replace(s.displayed_axes, vp_attr(vp_attr(self, "_slice_input"), "displayed"));
    nb::object list = ns.get("list");
    nb::object product = vp_attr(ns.get("itertools"), "product");
    nb::object transpose = vp_attr(std::move(corner_pixels_displayed), "T");
    nb::tuple args = nb::steal<nb::tuple>(PySequence_Tuple(transpose.ptr()));
    if (!args.is_valid()) throw nb::python_error();
    transpose.reset();
    nb::object combinations = vp_checked(PyObject_CallObject(product.ptr(), args.ptr()));
    product.reset(); args.reset();
    vp_replace(s.all_corners, vp_call1(std::move(list), std::move(combinations)));
    nb::object chain = vp_item(vp_attr(self, "_transforms"), vp_slice(nb::int_(1)));
    nb::object set_slice = vp_attr(vp_attr(std::move(chain), "simplified"), "set_slice");
    nb::object inverse = vp_attr(vp_call1(std::move(set_slice), s.displayed_axes), "inverse");
    vp_replace(s.data_corners, vp_call1(std::move(inverse), s.all_corners));
    nb::object stack = ns.numpy("stack");
    nb::object min = ns.numpy("min");
    nb::dict min_kwargs;
    min_kwargs["axis"] = nb::int_(0);
    nb::object lower = vp_kw(std::move(min), nb::make_tuple(s.data_corners), std::move(min_kwargs));
    nb::object max = ns.numpy("max");
    nb::dict max_kwargs;
    max_kwargs["axis"] = nb::int_(0);
    nb::object upper = vp_kw(std::move(max), nb::make_tuple(s.data_corners), std::move(max_kwargs));
    nb::list bbox;
    vp_append(bbox, std::move(lower)); vp_append(bbox, std::move(upper));
    vp_replace(s.data_bbox, vp_call1(std::move(stack), std::move(bbox)));
    stack = ns.numpy("stack");
    nb::object floor = ns.numpy("floor");
    lower = vp_call1(std::move(floor), vp_item(s.data_bbox, nb::int_(0)));
    nb::object ceil = ns.numpy("ceil");
    upper = vp_call1(std::move(ceil), vp_item(s.data_bbox, nb::int_(1)));
    bbox = nb::list(); vp_append(bbox, std::move(lower)); vp_append(bbox, std::move(upper));
    nb::object astype = vp_attr(vp_call1(std::move(stack), std::move(bbox)), "astype");
    vp_replace(s.data_bbox_int, vp_call1(std::move(astype), ns.get("int")));
    vp_call3(vp_attr(std::move(self), "_update_level_and_corners"), s.data_bbox_int,
        std::move(shape_threshold), s.displayed_axes);
}
void layer_viewport_update_corners(nb::object self, nb::object data_bbox_int,
    nb::object, nb::object displayed_axes, nb::dict namespace_, LayerViewportLocals& s) {
    ViewportNamespace ns(std::move(namespace_));
    nb::object zeros = ns.numpy("zeros");
    nb::object shape = nb::make_tuple(nb::int_(2), vp_attr(self, "ndim"));
    nb::dict kwargs;
    kwargs["dtype"] = ns.get("int");
    vp_replace(s.corners, vp_kw(std::move(zeros), vp_pack1(std::move(shape)), std::move(kwargs)));
    nb::object data_extent = vp_attr(vp_attr(self, "extent"), "data");
    nb::object key = nb::make_tuple(vp_slice(), displayed_axes);
    vp_replace(s.displayed_extent, vp_item(std::move(data_extent), std::move(key)));
    nb::object all = ns.numpy("all");
    nb::object isnan = ns.numpy("isnan");
    if (!vp_truth(vp_call1(std::move(all), vp_call1(std::move(isnan), s.displayed_extent)))) {
        nb::object clip = ns.numpy("clip");
        nb::object lower = vp_item(s.displayed_extent, nb::int_(0));
        nb::object upper = vp_item(s.displayed_extent, nb::int_(1));
        vp_replace(s.data_bbox_clipped,
            vp_call3(std::move(clip), std::move(data_bbox_int), std::move(lower), std::move(upper)));
        vp_set_item(s.corners, nb::make_tuple(vp_slice(), std::move(displayed_axes)), s.data_bbox_clipped);
    }
    vp_set_attr(std::move(self), "corner_pixels", s.corners);
}
void layer_viewport_reset_editable(nb::object self) {
    vp_set_attr(std::move(self), "editable", nb::bool_(true));
}
void layer_viewport_editable_set(nb::object self, nb::object editable) {
    if (vp_truth(vp_compare(vp_attr(self, "_editable"), editable, Py_EQ))) return;
    vp_set_attr(self, "_editable", std::move(editable));
    vp_call0(vp_attr(self, "_on_editable_changed"));
    vp_call0(vp_attr(vp_attr(std::move(self), "events"), "editable"));
}
void layer_viewport_scale_factor_set(nb::object self, nb::object scale_factor) {
    if (vp_truth(vp_compare(vp_attr(self, "_scale_factor"), scale_factor, Py_NE))) {
        vp_set_attr(self, "_scale_factor", std::move(scale_factor));
        vp_call0(vp_attr(vp_attr(std::move(self), "events"), "scale_factor"));
    }
}
nb::object layer_viewport_slice_input(nb::object self) {
    return vp_attr(vp_attr(std::move(self), "_slicing_state"), "_slice_input");
}
nb::object layer_viewport_editable(nb::object self) { return vp_attr(std::move(self), "_editable"); }
nb::object layer_viewport_scale_factor(nb::object self) { return vp_attr(std::move(self), "_scale_factor"); }
nb::object layer_viewport_world_dims(nb::object self, nb::object world_dims, nb::object ndim_world) {
    nb::object callback = vp_attr(vp_attr(std::move(self), "_slicing_state"), "_world_to_layer_dims");
    nb::dict kwargs;
    kwargs["world_dims"] = std::move(world_dims);
    kwargs["ndim_world"] = std::move(ndim_world);
    return vp_kw(std::move(callback), nb::tuple(), std::move(kwargs));
}

enum class PolicyIteratorKind { NonPositive, TranslatedOrigin };
struct ScalarViewportPolicyIterator {
    PolicyIteratorKind kind = PolicyIteratorKind::NonPositive;
    nb::object iterator, current, namespace_, builtins;
    bool started = false, running = false, closed = false;
    ScalarViewportPolicyIterator() = default;
    ScalarViewportPolicyIterator(ScalarViewportPolicyIterator&&) = default;
    ~ScalarViewportPolicyIterator() { clear(); }
    void clear() {
        closed = true;
        std::array<nb::object, 4> old = {
            std::move(iterator), std::move(current), std::move(namespace_), std::move(builtins),
        };
        for (auto& value : old) value.reset();
    }
};
struct PolicyRunning {
    ScalarViewportPolicyIterator& state;
    explicit PolicyRunning(ScalarViewportPolicyIterator& s) : state(s) {
        if (s.running) throw nb::value_error("generator already executing");
        s.running = true;
    }
    ~PolicyRunning() { state.running = false; }
};
nb::object vp_iterator_next(nb::handle self) {
    auto& s = *nb::inst_ptr<ScalarViewportPolicyIterator>(self);
    PolicyRunning running(s);
    if (s.closed) throw nb::stop_iteration();
    s.started = true;
    try {
        PyObject* next = PyIter_Next(s.iterator.ptr());
        if (!next) {
            if (PyErr_Occurred()) throw nb::python_error();
            s.clear(); throw nb::stop_iteration();
        }
        vp_replace(s.current, nb::steal<nb::object>(next));
        if (s.kind == PolicyIteratorKind::NonPositive)
            return vp_compare(s.current, nb::int_(0), Py_LE);
        nb::object allowed = nb::make_tuple(nb::int_(0), nb::none());
        const int contains = PySequence_Contains(allowed.ptr(), s.current.ptr());
        if (contains < 0) throw nb::python_error();
        return nb::bool_(!contains);
    } catch (const nb::python_error& error) {
        events::HandledException active(error.value());
        s.closed = true;
        if (error.matches(PyExc_StopIteration)) collections::raise_exception(error.value(), true);
        throw;
    } catch (...) { s.clear(); throw; }
}
void vp_iterator_close(nb::handle self) {
    auto& s = *nb::inst_ptr<ScalarViewportPolicyIterator>(self);
    PolicyRunning running(s);
    if (s.closed) return;
    if (!s.started) { s.clear(); return; }
    nb::object exit = vp_call0(nb::borrow<nb::object>(PyExc_GeneratorExit));
    events::HandledException active(exit);
    s.clear();
}
void vp_iterator_throw(nb::handle self, nb::args args) {
    nb::object exception = collections::normalize_throw(args);
    auto& s = *nb::inst_ptr<ScalarViewportPolicyIterator>(self);
    PolicyRunning running(s);
    const bool was_open = !s.closed;
    s.closed = true;
    events::HandledException active(exception);
    collections::raise_exception(exception, was_open);
}
int vp_iterator_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) {
        const auto& s = *nb::inst_ptr<ScalarViewportPolicyIterator>(self);
        Py_VISIT(s.iterator.ptr()); Py_VISIT(s.current.ptr());
        Py_VISIT(s.namespace_.ptr()); Py_VISIT(s.builtins.ptr());
    }
    return 0;
}
void vp_iterator_finalize(PyObject* self) noexcept {
    if (!nb::inst_ready(self) || nb::inst_ptr<ScalarViewportPolicyIterator>(self)->closed) return;
    PyObject *type = nullptr, *value = nullptr, *trace = nullptr;
    PyErr_Fetch(&type, &value, &trace);
    try { vp_iterator_close(self); }
    catch (nb::python_error& error) { error.restore(); PyErr_WriteUnraisable(self); }
    catch (const std::exception& error) { PyErr_SetString(PyExc_RuntimeError, error.what()); PyErr_WriteUnraisable(self); }
    catch (...) { PyErr_SetString(PyExc_RuntimeError, "failed to close viewport policy iterator"); PyErr_WriteUnraisable(self); }
    PyErr_Restore(type, value, trace);
}
PyType_Slot vp_iterator_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(vp_iterator_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(vp_clear<ScalarViewportPolicyIterator>)},
    {Py_tp_finalize, reinterpret_cast<void*>(vp_iterator_finalize)}, {0, nullptr},
};
destructor vp_iterator_base_dealloc = nullptr;
void vp_iterator_dealloc(PyObject* self) noexcept {
    PyObject_GC_UnTrack(self); PyObject_ClearWeakRefs(self); PyObject_GC_Track(self);
    if (PyObject_CallFinalizerFromDealloc(self) < 0) return;
    vp_iterator_base_dealloc(self);
}
nb::object vp_generator(PolicyIteratorKind kind, nb::object iterable,
    ViewportNamespace& ns, nb::object generator_factory, const char* qualname) {
    ScalarViewportPolicyIterator s;
    s.kind = kind;
    s.iterator = vp_checked(PyObject_GetIter(iterable.ptr()));
    iterable.reset();
    s.namespace_ = ns.globals; s.builtins = ns.builtins;
    nb::object delegate = nb::cast(std::move(s), nb::rv_policy::move);
    nb::object generator = vp_call1(std::move(generator_factory), std::move(delegate));
    nb::object name = nb::str("<genexpr>"), qualified = nb::str(qualname);
    if (PyObject_SetAttrString(generator.ptr(), "__name__", name.ptr()) < 0 ||
        PyObject_SetAttrString(generator.ptr(), "__qualname__", qualified.ptr()) < 0) throw nb::python_error();
    return generator;
}
void vp_validate_except(nb::handle value) {
    if (PyTuple_Check(value.ptr())) {
        for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(value.ptr()); ++i) {
            if (!PyExceptionClass_Check(PyTuple_GET_ITEM(value.ptr(), i))) {
                PyErr_SetString(PyExc_TypeError, "catching classes that do not inherit from BaseException is not allowed");
                throw nb::python_error();
            }
        }
    } else if (!PyExceptionClass_Check(value.ptr())) {
        PyErr_SetString(PyExc_TypeError, "catching classes that do not inherit from BaseException is not allowed");
        throw nb::python_error();
    }
}
bool vp_matches(const nb::python_error& error, nb::object types) {
    vp_validate_except(types);
    return PyErr_GivenExceptionMatches(error.type().ptr(), types.ptr()) != 0;
}

nb::object scalar_viewport_level(nb::object requested_shape, nb::object shape_threshold,
    nb::object downsample_factors, nb::dict namespace_, ScalarViewportPolicyLocals& s) {
    ViewportNamespace ns(std::move(namespace_));
    vp_replace(s.scaled_shape, vp_divide(requested_shape, downsample_factors));
    vp_replace(s.exceeds_threshold, vp_compare(requested_shape, shape_threshold, Py_GT));
    nb::object any = ns.numpy("any");
    nb::object different = vp_compare(downsample_factors, nb::int_(1), Py_GT);
    nb::dict any_kwargs;
    any_kwargs["axis"] = nb::int_(0);
    vp_replace(s.is_downsampled, vp_kw(std::move(any), vp_pack1(std::move(different)), std::move(any_kwargs)));
    vp_replace(s.active_dims, vp_checked(PyNumber_And(s.exceeds_threshold.ptr(), s.is_downsampled.ptr())));
    if (!vp_truth(vp_call1(ns.numpy("any"), s.active_dims))) return nb::int_(0);
    nb::object flatnonzero = ns.numpy("flatnonzero");
    nb::object all = ns.numpy("all");
    nb::object over = vp_compare(s.scaled_shape, std::move(shape_threshold), Py_GT);
    nb::object selection = vp_item(std::move(over), nb::make_tuple(vp_slice(), s.active_dims));
    nb::dict all_kwargs;
    all_kwargs["axis"] = nb::int_(1);
    nb::object valid = vp_kw(std::move(all), vp_pack1(std::move(selection)), std::move(all_kwargs));
    vp_replace(s.locations, vp_call1(std::move(flatnonzero), std::move(valid)));
    if (vp_truth(vp_attr(s.locations, "size"))) {
        nb::object int_ = ns.get("int");
        return vp_call1(std::move(int_), vp_item(s.locations, nb::int_(-1)));
    }
    return nb::int_(0);
}
nb::object scalar_viewport_level_corners(nb::object corner_pixels, nb::object shape_threshold,
    nb::object downsample_factors, nb::dict namespace_, ScalarViewportPolicyLocals& s) {
    ViewportNamespace ns(std::move(namespace_));
    nb::object high = vp_item(corner_pixels, nb::int_(1));
    nb::object low = vp_item(corner_pixels, nb::int_(0));
    vp_replace(s.requested_shape, vp_subtract(std::move(high), std::move(low)));
    vp_replace(s.level, vp_call3(ns.get("compute_multiscale_level"), s.requested_shape,
        std::move(shape_threshold), downsample_factors));
    nb::object factors = vp_item(std::move(downsample_factors), s.level);
    vp_replace(s.corners, vp_divide(std::move(corner_pixels), std::move(factors)));
    nb::object array = ns.numpy("array");
    nb::object floor = ns.numpy("floor");
    low = vp_call1(std::move(floor), vp_item(s.corners, nb::int_(0)));
    nb::object ceil = ns.numpy("ceil");
    high = vp_call1(std::move(ceil), vp_item(s.corners, nb::int_(1)));
    nb::list bounds; vp_append(bounds, std::move(low)); vp_append(bounds, std::move(high));
    nb::object astype = vp_attr(vp_call1(std::move(array), std::move(bounds)), "astype");
    vp_replace(s.corners, vp_call1(std::move(astype), ns.get("int")));
    return nb::make_tuple(s.level, s.corners);
}

void vp_collect_chunk_sizes(nb::object values, ViewportNamespace& ns, ScalarViewportPolicyLocals& s) {
    nb::object iterator = vp_checked(PyObject_GetIter(values.ptr()));
    values.reset();
    nb::object result = nb::list(), size;
    try {
        while (true) {
            PyObject* next = PyIter_Next(iterator.ptr());
            if (!next) { if (PyErr_Occurred()) throw nb::python_error(); break; }
            vp_replace(size, nb::steal<nb::object>(next));
            nb::object index = vp_attr(ns.get("operator"), "index");
            nb::object converted = vp_call1(std::move(index), size);
            if (PyList_Append(result.ptr(), converted.ptr()) < 0) throw nb::python_error();
        }
    } catch (...) {
        iterator.reset(); result.reset(); size.reset(); throw;
    }
    iterator.reset();
    // CPython 3.12 publishes chunk_sizes before restoring size to UNBOUND.
    vp_replace(s.chunk_sizes, std::move(result));
    size.reset();
}
nb::object scalar_viewport_chunk_boundaries(nb::object axis_chunks, nb::object axis_size,
    nb::dict namespace_, nb::object generator_factory, ScalarViewportPolicyLocals& s) {
    ViewportNamespace ns(std::move(namespace_));
    nb::object isinstance = ns.get("isinstance");
    nb::object sequence = ns.get("Sequence");
    bool explicit_sizes = vp_truth(vp_call2(std::move(isinstance), axis_chunks, std::move(sequence)));
    if (explicit_sizes) {
        isinstance = ns.get("isinstance");
        nb::object str = ns.get("str");
        explicit_sizes = !vp_truth(vp_call2(std::move(isinstance), axis_chunks, std::move(str)));
    }
    if (explicit_sizes) {
        try { vp_collect_chunk_sizes(std::move(axis_chunks), ns, s); }
        catch (const nb::python_error& original) {
            events::HandledException active(original.value());
            if (vp_matches(original, ns.get("TypeError"))) return nb::none();
            throw;
        }
        nb::object any = ns.get("any");
        nb::object generator = vp_generator(PolicyIteratorKind::NonPositive, s.chunk_sizes,
            ns, std::move(generator_factory), "_chunk_boundaries.<locals>.<genexpr>");
        if (vp_truth(vp_call1(std::move(any), std::move(generator)))) return nb::none();
        nb::object concatenate = ns.numpy("concatenate");
        nb::list zero; zero.append(nb::int_(0));
        nb::object accumulated = vp_call1(ns.numpy("cumsum"), s.chunk_sizes);
        nb::list arrays; vp_append(arrays, std::move(zero)); vp_append(arrays, std::move(accumulated));
        vp_replace(s.boundaries, vp_call1(std::move(concatenate), std::move(arrays)));
        if (vp_truth(vp_compare(vp_item(s.boundaries, nb::int_(-1)), std::move(axis_size), Py_NE)))
            return nb::none();
        return s.boundaries;
    }
    try {
        vp_replace(s.chunk_size, vp_call1(vp_attr(ns.get("operator"), "index"), std::move(axis_chunks)));
    } catch (const nb::python_error& original) {
        events::HandledException active(original.value());
        if (vp_matches(original, ns.get("TypeError"))) return nb::none();
        throw;
    }
    if (vp_truth(vp_compare(s.chunk_size, nb::int_(0), Py_LE))) return nb::none();
    nb::object append = ns.numpy("append");
    nb::object arange = ns.numpy("arange");
    nb::object boundaries = vp_call3(std::move(arange), nb::int_(0), axis_size, s.chunk_size);
    return vp_call2(std::move(append), std::move(boundaries), std::move(axis_size));
}
nb::object scalar_viewport_chunks_metadata(nb::object data, nb::dict namespace_,
    nb::object generator_factory, ScalarViewportPolicyLocals& s) {
    ViewportNamespace ns(std::move(namespace_));
    vp_replace(s.chunks, vp_call3(ns.get("getattr"), data, vp_literal("chunks"), nb::none()));
    if (!s.chunks.is_none()) return s.chunks;
    vp_replace(s.layout, vp_call3(ns.get("getattr"), std::move(data), vp_literal("chunk_layout"), nb::none()));
    vp_replace(s.read_chunk, vp_call3(ns.get("getattr"), s.layout, vp_literal("read_chunk"), nb::none()));
    if (s.read_chunk.is_none()) return nb::none();
    nb::object origin = vp_call3(ns.get("getattr"), s.layout, vp_literal("grid_origin"), nb::none());
    if (vp_truth(origin)) vp_replace(s.grid_origin, std::move(origin));
    else { origin.reset(); vp_replace(s.grid_origin, nb::tuple()); }
    nb::object any = ns.get("any");
    nb::object generator = vp_generator(PolicyIteratorKind::TranslatedOrigin, s.grid_origin,
        ns, std::move(generator_factory), "_chunks_metadata.<locals>.<genexpr>");
    if (vp_truth(vp_call1(std::move(any), std::move(generator)))) return nb::none();
    return vp_attr(s.read_chunk, "shape");
}
nb::object scalar_viewport_expand_chunks(nb::object corners, nb::object data, nb::object axes,
    nb::dict namespace_, ScalarViewportPolicyLocals& s) {
    s.cleanup_layout = ScalarViewportPolicyLocals::CleanupLayout::Expand;
    ViewportNamespace ns(std::move(namespace_));
    vp_replace(s.chunks, vp_call1(ns.get("_chunks_metadata"), data));
    vp_replace(s.shape, vp_call3(ns.get("getattr"), std::move(data), vp_literal("shape"), nb::none()));
    if (s.chunks.is_none() || s.shape.is_none()) return corners;
    try {
        nb::object chunk_len = vp_call1(ns.get("len"), s.chunks);
        nb::object shape_len = vp_call1(ns.get("len"), s.shape);
        if (vp_truth(vp_compare(std::move(chunk_len), std::move(shape_len), Py_NE))) return corners;
    } catch (const nb::python_error& original) {
        events::HandledException active(original.value());
        if (vp_matches(original, ns.get("TypeError"))) return corners;
        throw;
    }
    nb::object array = ns.numpy("array");
    nb::dict kwargs; kwargs["copy"] = nb::bool_(true);
    vp_replace(s.expanded, vp_kw(std::move(array), nb::make_tuple(corners), std::move(kwargs)));
    nb::object iterator = vp_checked(PyObject_GetIter(axes.ptr()));
    while (true) {
        PyObject* next = PyIter_Next(iterator.ptr());
        if (!next) { if (PyErr_Occurred()) throw nb::python_error(); break; }
        vp_replace(s.axis, nb::steal<nb::object>(next));
        try {
            nb::object index = vp_attr(ns.get("operator"), "index");
            vp_replace(s.axis_size, vp_call1(std::move(index), vp_item(s.shape, s.axis)));
            nb::object boundaries = ns.get("_chunk_boundaries");
            nb::object chunk = vp_item(s.chunks, s.axis);
            vp_replace(s.boundaries, vp_call2(std::move(boundaries), std::move(chunk), s.axis_size));
        } catch (const nb::python_error& original) {
            events::HandledException active(original.value());
            nb::object index_error = ns.get("IndexError");
            nb::object type_error = ns.get("TypeError");
            if (vp_matches(original, nb::make_tuple(std::move(index_error), std::move(type_error)))) return corners;
            throw;
        }
        if (s.boundaries.is_none()) return corners;
        vp_replace(s.axis_max, vp_subtract(vp_item(s.boundaries, nb::int_(-1)), nb::int_(1)));
        nb::object clip = ns.numpy("clip");
        nb::object start = vp_item(s.expanded, nb::make_tuple(nb::int_(0), s.axis));
        vp_replace(s.start, vp_call3(std::move(clip), std::move(start), nb::int_(0), s.axis_max));
        clip = ns.numpy("clip");
        nb::object stop = vp_item(s.expanded, nb::make_tuple(nb::int_(1), s.axis));
        vp_replace(s.stop, vp_call3(std::move(clip), std::move(stop), nb::int_(0), s.axis_max));
        nb::object search = ns.numpy("searchsorted");
        nb::dict start_kwargs; start_kwargs["side"] = vp_literal("right");
        nb::object start_chunk = vp_kw(std::move(search), nb::make_tuple(s.boundaries, s.start), std::move(start_kwargs));
        vp_replace(s.start_chunk, vp_subtract(std::move(start_chunk), nb::int_(1)));
        search = ns.numpy("searchsorted");
        nb::dict stop_kwargs; stop_kwargs["side"] = vp_literal("right");
        nb::object stop_chunk = vp_kw(std::move(search), nb::make_tuple(s.boundaries, s.stop), std::move(stop_kwargs));
        vp_replace(s.stop_chunk, vp_subtract(std::move(stop_chunk), nb::int_(1)));
        nb::object lower = vp_item(s.boundaries, s.start_chunk);
        vp_set_item(s.expanded, nb::make_tuple(nb::int_(0), s.axis), std::move(lower));
        nb::object upper = vp_subtract(vp_item(s.boundaries, vp_add(s.stop_chunk, nb::int_(1))), nb::int_(1));
        vp_set_item(s.expanded, nb::make_tuple(nb::int_(1), s.axis), std::move(upper));
    }
    return s.expanded;
}

} // namespace

void bind_scalar_viewport_policy(nb::module_& module) {
    nb::class_<LayerViewportLocals>(module, "LayerViewportLocals",
        nb::is_weak_referenceable(), nb::type_slots(vp_layer_slots))
        .def_prop_ro("world_to_data", [](const LayerViewportLocals& s) { return vp_optional(s.world_to_data); })
        .def_prop_ro("slice_input", [](const LayerViewportLocals& s) { return vp_optional(s.slice_input); })
        .def_prop_ro("world_slice", [](const LayerViewportLocals& s) { return vp_optional(s.world_slice); })
        .def_prop_ro("order_array", [](const LayerViewportLocals& s) { return vp_optional(s.order_array); })
        .def_prop_ro("order", [](const LayerViewportLocals& s) { return vp_optional(s.order); })
        .def_prop_ro("prev", [](const LayerViewportLocals& s) { return vp_optional(s.prev); })
        .def_prop_ro("ndim", [](const LayerViewportLocals& s) { return vp_optional(s.ndim); })
        .def_prop_ro("old_ndim", [](const LayerViewportLocals& s) { return vp_optional(s.old_ndim); })
        .def_prop_ro("keep_axes", [](const LayerViewportLocals& s) { return vp_optional(s.keep_axes); })
        .def_prop_ro("new_axes", [](const LayerViewportLocals& s) { return vp_optional(s.new_axes); })
        .def_prop_ro("changed", [](const LayerViewportLocals& s) { return vp_optional(s.changed); })
        .def_prop_ro("displayed_axes", [](const LayerViewportLocals& s) { return vp_optional(s.displayed_axes); })
        .def_prop_ro("all_corners", [](const LayerViewportLocals& s) { return vp_optional(s.all_corners); })
        .def_prop_ro("data_corners", [](const LayerViewportLocals& s) { return vp_optional(s.data_corners); })
        .def_prop_ro("data_bbox", [](const LayerViewportLocals& s) { return vp_optional(s.data_bbox); })
        .def_prop_ro("data_bbox_int", [](const LayerViewportLocals& s) { return vp_optional(s.data_bbox_int); })
        .def_prop_ro("corners", [](const LayerViewportLocals& s) { return vp_optional(s.corners); })
        .def_prop_ro("displayed_extent", [](const LayerViewportLocals& s) { return vp_optional(s.displayed_extent); })
        .def_prop_ro("data_bbox_clipped", [](const LayerViewportLocals& s) { return vp_optional(s.data_bbox_clipped); });
    nb::class_<ScalarViewportPolicyLocals>(module, "ScalarViewportPolicyLocals",
        nb::is_weak_referenceable(), nb::type_slots(vp_policy_slots))
        .def_prop_ro("scaled_shape", [](const ScalarViewportPolicyLocals& s) { return vp_optional(s.scaled_shape); })
        .def_prop_ro("exceeds_threshold", [](const ScalarViewportPolicyLocals& s) { return vp_optional(s.exceeds_threshold); })
        .def_prop_ro("is_downsampled", [](const ScalarViewportPolicyLocals& s) { return vp_optional(s.is_downsampled); })
        .def_prop_ro("active_dims", [](const ScalarViewportPolicyLocals& s) { return vp_optional(s.active_dims); })
        .def_prop_ro("locations", [](const ScalarViewportPolicyLocals& s) { return vp_optional(s.locations); })
        .def_prop_ro("requested_shape", [](const ScalarViewportPolicyLocals& s) { return vp_optional(s.requested_shape); })
        .def_prop_ro("level", [](const ScalarViewportPolicyLocals& s) { return vp_optional(s.level); })
        .def_prop_ro("corners", [](const ScalarViewportPolicyLocals& s) { return vp_optional(s.corners); })
        .def_prop_ro("chunk_sizes", [](const ScalarViewportPolicyLocals& s) { return vp_optional(s.chunk_sizes); })
        .def_prop_ro("boundaries", [](const ScalarViewportPolicyLocals& s) { return vp_optional(s.boundaries); })
        .def_prop_ro("chunk_size", [](const ScalarViewportPolicyLocals& s) { return vp_optional(s.chunk_size); })
        .def_prop_ro("chunks", [](const ScalarViewportPolicyLocals& s) { return vp_optional(s.chunks); })
        .def_prop_ro("layout", [](const ScalarViewportPolicyLocals& s) { return vp_optional(s.layout); })
        .def_prop_ro("read_chunk", [](const ScalarViewportPolicyLocals& s) { return vp_optional(s.read_chunk); })
        .def_prop_ro("grid_origin", [](const ScalarViewportPolicyLocals& s) { return vp_optional(s.grid_origin); })
        .def_prop_ro("shape", [](const ScalarViewportPolicyLocals& s) { return vp_optional(s.shape); })
        .def_prop_ro("expanded", [](const ScalarViewportPolicyLocals& s) { return vp_optional(s.expanded); })
        .def_prop_ro("axis", [](const ScalarViewportPolicyLocals& s) { return vp_optional(s.axis); })
        .def_prop_ro("axis_size", [](const ScalarViewportPolicyLocals& s) { return vp_optional(s.axis_size); })
        .def_prop_ro("axis_max", [](const ScalarViewportPolicyLocals& s) { return vp_optional(s.axis_max); })
        .def_prop_ro("start", [](const ScalarViewportPolicyLocals& s) { return vp_optional(s.start); })
        .def_prop_ro("stop", [](const ScalarViewportPolicyLocals& s) { return vp_optional(s.stop); })
        .def_prop_ro("start_chunk", [](const ScalarViewportPolicyLocals& s) { return vp_optional(s.start_chunk); })
        .def_prop_ro("stop_chunk", [](const ScalarViewportPolicyLocals& s) { return vp_optional(s.stop_chunk); });
    nb::class_<ScalarViewportPolicyIterator> iterator_type(module, "ScalarViewportPolicyIterator",
        nb::is_weak_referenceable(), nb::type_slots(vp_iterator_slots));
    iterator_type.def("__iter__", [](nb::handle self) { return nb::borrow<nb::object>(self); })
        .def("__next__", &vp_iterator_next)
        .def("send", [](nb::handle self, nb::object value) {
            const auto& s = *nb::inst_ptr<ScalarViewportPolicyIterator>(self);
            if (!s.started && !s.closed && !value.is_none())
                throw nb::type_error("can't send non-None value to a just-started generator");
            return vp_iterator_next(self);
        }, nb::arg("value").none())
        .def("throw", &vp_iterator_throw).def("close", &vp_iterator_close);
    auto* iterator_heap_type = reinterpret_cast<PyTypeObject*>(iterator_type.ptr());
    vp_iterator_base_dealloc = iterator_heap_type->tp_dealloc;
    iterator_heap_type->tp_dealloc = vp_iterator_dealloc;

    module.def("layer_viewport_locals", [] { return LayerViewportLocals(); });
    module.def("scalar_viewport_policy_locals", [] { return ScalarViewportPolicyLocals(); });
    module.def("layer_viewport_slice_indices", &layer_viewport_slice_indices,
        nb::arg("self").none(), nb::arg("slice_input").none(), nb::arg("dims").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("layer_viewport_state_update_dims", &layer_viewport_state_update_dims, nb::arg("self").none());
    module.def("layer_viewport_set_input_from_dims", &layer_viewport_set_input_from_dims,
        nb::arg("self").none(), nb::arg("dims").none(), nb::arg("force").none(), nb::arg("locals_owner"));
    module.def("layer_viewport_set_input", &layer_viewport_set_input,
        nb::arg("self").none(), nb::arg("slice_input").none(), nb::arg("force").none(), nb::arg("namespace"));
    module.def("layer_viewport_state_ndim", &layer_viewport_state_ndim, nb::arg("self").none());
    module.def("layer_viewport_make_input", &layer_viewport_make_input,
        nb::arg("self").none(), nb::arg("dims").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("layer_viewport_state_world_dims", &layer_viewport_state_world_dims,
        nb::arg("self").none(), nb::arg("world_dims").none(), nb::arg("ndim_world").none());
    module.def("layer_viewport_units_set", &layer_viewport_units_set,
        nb::arg("self").none(), nb::arg("units").none(), nb::arg("locals_owner"));
    module.def("layer_viewport_update_dims", &layer_viewport_update_dims,
        nb::arg("self").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("layer_viewport_slice_dims", &layer_viewport_slice_dims,
        nb::arg("self").none(), nb::arg("dims").none(), nb::arg("force").none(), nb::arg("namespace"), nb::arg("debug_message").none(), nb::arg("locals_owner"));
    module.def("layer_viewport_update_draw", &layer_viewport_update_draw,
        nb::arg("self").none(), nb::arg("scale_factor").none(), nb::arg("corner_pixels_displayed").none(), nb::arg("shape_threshold").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("layer_viewport_update_corners", &layer_viewport_update_corners,
        nb::arg("self").none(), nb::arg("data_bbox_int").none(), nb::arg("shape_threshold").none(), nb::arg("displayed_axes").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("layer_viewport_reset_editable", &layer_viewport_reset_editable, nb::arg("self").none());
    module.def("layer_viewport_editable_set", &layer_viewport_editable_set, nb::arg("self").none(), nb::arg("editable").none());
    module.def("layer_viewport_scale_factor_set", &layer_viewport_scale_factor_set, nb::arg("self").none(), nb::arg("scale_factor").none());
    module.def("layer_viewport_slice_input", &layer_viewport_slice_input, nb::arg("self").none());
    module.def("layer_viewport_editable", &layer_viewport_editable, nb::arg("self").none());
    module.def("layer_viewport_scale_factor", &layer_viewport_scale_factor, nb::arg("self").none());
    module.def("layer_viewport_world_dims", &layer_viewport_world_dims,
        nb::arg("self").none(), nb::arg("world_dims").none(), nb::arg("ndim_world").none());
    module.def("scalar_viewport_level", &scalar_viewport_level,
        nb::arg("requested_shape").none(), nb::arg("shape_threshold").none(), nb::arg("downsample_factors").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("scalar_viewport_level_corners", &scalar_viewport_level_corners,
        nb::arg("corner_pixels").none(), nb::arg("shape_threshold").none(), nb::arg("downsample_factors").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("scalar_viewport_chunk_boundaries", &scalar_viewport_chunk_boundaries,
        nb::arg("axis_chunks").none(), nb::arg("axis_size").none(), nb::arg("namespace"), nb::arg("generator_factory").none(), nb::arg("locals_owner"));
    module.def("scalar_viewport_chunks_metadata", &scalar_viewport_chunks_metadata,
        nb::arg("data").none(), nb::arg("namespace"), nb::arg("generator_factory").none(), nb::arg("locals_owner"));
    module.def("scalar_viewport_expand_chunks", &scalar_viewport_expand_chunks,
        nb::arg("corners").none(), nb::arg("data").none(), nb::arg("axes").none(), nb::arg("namespace"), nb::arg("locals_owner"));
}
} // namespace nx
