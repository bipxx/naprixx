#include "arrays.hpp"
#include "event_protocol.hpp"
#include "iterator_frame.hpp"
#include <string>
#include <utility>

namespace nx {
namespace {
nb::object scalar_checked(PyObject* value) {
    if (!value) throw nb::python_error();
    return nb::steal<nb::object>(value);
}
nb::object scalar_attr(nb::object value, const char* name) {
    return scalar_checked(PyObject_GetAttrString(value.ptr(), name));
}
nb::object scalar_item(nb::object value, nb::object key) {
    return scalar_checked(PyObject_GetItem(value.ptr(), key.ptr()));
}
nb::object scalar_call0(nb::object callable) {
    return scalar_checked(PyObject_CallNoArgs(callable.ptr()));
}
nb::object scalar_call1(nb::object callable, nb::object value) {
    return scalar_checked(PyObject_CallOneArg(callable.ptr(), value.ptr()));
}
nb::object scalar_call2(nb::object callable, nb::object first, nb::object second) {
    return scalar_checked(PyObject_CallFunctionObjArgs(callable.ptr(), first.ptr(), second.ptr(), nullptr));
}
nb::object scalar_call3(nb::object callable, nb::object first, nb::object second, nb::object third) {
    return scalar_checked(PyObject_CallFunctionObjArgs(callable.ptr(), first.ptr(), second.ptr(), third.ptr(), nullptr));
}
nb::object scalar_call_kw(nb::object callable, nb::tuple args, const char* name, nb::object value) {
    nb::dict kwargs;
    kwargs[nb::str(name)] = std::move(value);
    return scalar_checked(PyObject_Call(callable.ptr(), args.ptr(), kwargs.ptr()));
}
void scalar_set_attr(nb::object target, const char* name, nb::object value) {
    if (PyObject_SetAttrString(target.ptr(), name, value.ptr()) < 0) throw nb::python_error();
}
void scalar_set(nb::handle target, nb::object key, nb::object value) {
    if (PyObject_SetItem(target.ptr(), key.ptr(), value.ptr()) < 0) throw nb::python_error();
}
bool scalar_truth(nb::object value) {
    const int result = PyObject_IsTrue(value.ptr());
    if (result < 0) throw nb::python_error();
    return result != 0;
}
nb::object scalar_compare(nb::object left, nb::object right, int operation) {
    return scalar_checked(PyObject_RichCompare(left.ptr(), right.ptr(), operation));
}
bool scalar_test(nb::object left, nb::object right, int operation) {
    return scalar_truth(scalar_compare(std::move(left), std::move(right), operation));
}
nb::object scalar_add(nb::object left, nb::object right) {
    return scalar_checked(PyNumber_Add(left.ptr(), right.ptr()));
}
nb::object scalar_subtract(nb::object left, nb::object right) {
    return scalar_checked(PyNumber_Subtract(left.ptr(), right.ptr()));
}
nb::object scalar_multiply(nb::object left, nb::object right) {
    return scalar_checked(PyNumber_Multiply(left.ptr(), right.ptr()));
}
nb::object scalar_iter(nb::object value) { return scalar_checked(PyObject_GetIter(value.ptr())); }
nb::object scalar_format(nb::object value) { return scalar_checked(PyObject_Format(value.ptr(), nb::str("").ptr())); }
nb::object scalar_concat(nb::object left, nb::object right) {
    return scalar_checked(PyUnicode_Concat(left.ptr(), right.ptr()));
}
[[noreturn]] void scalar_raise(nb::object error) {
    if (PyExceptionInstance_Check(error.ptr()))
        PyErr_SetObject(reinterpret_cast<PyObject*>(Py_TYPE(error.ptr())), error.ptr());
    else if (PyExceptionClass_Check(error.ptr())) PyErr_SetNone(error.ptr());
    else PyErr_SetString(PyExc_TypeError, "exceptions must derive from BaseException");
    throw nb::python_error();
}
struct ScalarNamespace {
    nb::dict globals;
    nb::object builtins;
    explicit ScalarNamespace(nb::dict ns)
        : globals(std::move(ns)), builtins(nb::borrow<nb::object>(PyEval_GetBuiltins())) {}
    ScalarNamespace(nb::dict ns, nb::object builtins_)
        : globals(std::move(ns)), builtins(std::move(builtins_)) {}
    nb::object get(const char* name) const {
        nb::object key = nb::str(name);
        PyObject* found = PyDict_GetItemWithError(globals.ptr(), key.ptr());
        if (found) return nb::borrow<nb::object>(found);
        if (PyErr_Occurred()) throw nb::python_error();
        found = PyObject_GetItem(builtins.ptr(), key.ptr());
        if (found) return nb::steal<nb::object>(found);
        if (!PyErr_ExceptionMatches(PyExc_KeyError)) throw nb::python_error();
        PyErr_Clear();
        nb::object text = scalar_checked(PyUnicode_FromFormat("name '%s' is not defined", name));
        nb::object error = scalar_call1(nb::borrow<nb::object>(PyExc_NameError), std::move(text));
        if (PyObject_SetAttrString(error.ptr(), "name", key.ptr()) < 0) throw nb::python_error();
        scalar_raise(std::move(error));
    }
    nb::object numpy(const char* name) const { return scalar_attr(get("np"), name); }
};
nb::object scalar_layer(nb::object self) { return scalar_attr(std::move(self), "layer"); }
nb::object scalar_slice_input(nb::object self) { return scalar_attr(scalar_layer(std::move(self)), "_slice_input"); }
bool scalar_instance(nb::object object, nb::object type, nb::object callable) {
    return scalar_truth(scalar_call2(std::move(callable), std::move(object), std::move(type)));
}
bool scalar_node_is(nb::object self, const char* type, const ScalarNamespace& ns) {
    nb::object callable = ns.get("isinstance");
    nb::object node = scalar_attr(std::move(self), "node");
    nb::object class_ = ns.get(type);
    return scalar_instance(std::move(node), std::move(class_), std::move(callable));
}
nb::object scalar_visibility(nb::object self) {
    nb::object empty = scalar_attr(scalar_attr(scalar_layer(self), "_slice"), "empty");
    if (scalar_truth(std::move(empty))) return nb::bool_(false);
    return scalar_attr(scalar_layer(std::move(self)), "visible");
}

// Only original assigned locals survive a complete-operation error. The
// facade retains this per-call owner, including across a saved traceback;
// there is no iterator, namespace, controller or independent state snapshot.
struct VispyScalarFieldLocals {
    nb::object child, i, d;
    nb::object parent, children, ndisplay, texture_format, data, node;
    nb::object downsample, scale, slices;
    void clear() {
        nb::object old_child = std::move(child), old_i = std::move(i), old_d = std::move(d);
        nb::object old_parent = std::move(parent), old_children = std::move(children);
        nb::object old_ndisplay = std::move(ndisplay), old_format = std::move(texture_format);
        nb::object old_data = std::move(data), old_node = std::move(node);
        nb::object old_downsample = std::move(downsample), old_scale = std::move(scale), old_slices = std::move(slices);
    }
};
int scalar_locals_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) {
        auto* state = nb::inst_ptr<VispyScalarFieldLocals>(self);
        Py_VISIT(state->child.ptr()); Py_VISIT(state->i.ptr()); Py_VISIT(state->d.ptr());
        Py_VISIT(state->parent.ptr()); Py_VISIT(state->children.ptr());
        Py_VISIT(state->ndisplay.ptr()); Py_VISIT(state->texture_format.ptr());
        Py_VISIT(state->data.ptr()); Py_VISIT(state->node.ptr());
        Py_VISIT(state->downsample.ptr()); Py_VISIT(state->scale.ptr()); Py_VISIT(state->slices.ptr());
    }
    return 0;
}
int scalar_locals_clear(PyObject* self) {
    if (nb::inst_ready(self)) nb::inst_ptr<VispyScalarFieldLocals>(self)->clear();
    return 0;
}
PyType_Slot scalar_locals_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(scalar_locals_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(scalar_locals_clear)}, {0, nullptr},
};
nb::object scalar_optional(nb::object value) { return value.is_valid() ? value : nb::none(); }
struct ScalarPair { nb::object first, second; };
ScalarPair scalar_unpack_pair(nb::object value) {
    nb::object iterator = scalar_iter(std::move(value));
    nb::object first = nb::steal<nb::object>(PyIter_Next(iterator.ptr()));
    if (!first.is_valid()) {
        if (PyErr_Occurred()) throw nb::python_error();
        throw nb::value_error("not enough values to unpack (expected 2, got 0)");
    }
    nb::object second = nb::steal<nb::object>(PyIter_Next(iterator.ptr()));
    if (!second.is_valid()) {
        if (PyErr_Occurred()) throw nb::python_error();
        throw nb::value_error("not enough values to unpack (expected 2, got 1)");
    }
    nb::object extra = nb::steal<nb::object>(PyIter_Next(iterator.ptr()));
    if (extra.is_valid()) throw nb::value_error("too many values to unpack (expected 2)");
    if (PyErr_Occurred()) throw nb::python_error();
    return {std::move(first), std::move(second)};
}

void vispy_scalar_create_layer_node(nb::object self, nb::object node,
    nb::object texture_format, nb::object layer_node_class) {
    nb::object value = scalar_call_kw(std::move(layer_node_class), nb::make_tuple(std::move(node)),
                                      "texture_format", std::move(texture_format));
    scalar_set_attr(std::move(self), "_layer_node", std::move(value));
}
nb::object vispy_scalar_call_base_init(nb::object callback, nb::object self,
    nb::object layer, nb::object kwargs) {
    nb::object getter = scalar_attr(scalar_attr(std::move(self), "_layer_node"), "get_node");
    nb::object node = scalar_call1(std::move(getter), nb::int_(2));
    nb::dict keywords;
    if (_PyDict_MergeEx(keywords.ptr(), kwargs.ptr(), 2) < 0) throw nb::python_error();
    kwargs.reset();
    return scalar_checked(PyObject_Call(callback.ptr(), nb::make_tuple(std::move(layer), std::move(node)).ptr(), keywords.ptr()));
}
void scalar_connect(nb::object self, const char* event, const char* callback, bool plane) {
    nb::object root = scalar_layer(self);
    if (plane) root = scalar_attr(std::move(root), "plane");
    nb::object connect = scalar_attr(scalar_attr(scalar_attr(std::move(root), "events"), event), "connect");
    scalar_call1(std::move(connect), scalar_attr(std::move(self), callback));
}
void vispy_scalar_finish_init(nb::object self, nb::dict globals) {
    ScalarNamespace ns(std::move(globals));
    scalar_set_attr(self, "_array_like", nb::bool_(true));
    nb::object data = scalar_call1(ns.numpy("empty"), nb::int_(0));
    scalar_set_attr(self, "_data", std::move(data));
    scalar_connect(self, "rendering", "_on_rendering_change", false);
    scalar_connect(self, "depiction", "_on_depiction_change", false);
    scalar_connect(self, "colormap", "_on_colormap_change", false);
    scalar_connect(self, "position", "_on_plane_position_change", true);
    scalar_connect(self, "thickness", "_on_plane_thickness_change", true);
    scalar_connect(self, "normal", "_on_plane_normal_change", true);
    scalar_connect(self, "custom_interpolation_kernel_2d", "_on_custom_interpolation_kernel_2d_change", false);
    scalar_call0(scalar_attr(self, "_on_display_change"));
    scalar_call0(scalar_attr(self, "reset"));
    scalar_call0(scalar_attr(std::move(self), "_on_data_change"));
}
nb::object vispy_scalar_parent(nb::object self) { return scalar_attr(scalar_attr(std::move(self), "node"), "parent"); }
nb::object vispy_scalar_children(nb::object self, nb::dict globals) {
    ScalarNamespace ns(std::move(globals));
    nb::object callable = ns.get("list");
    nb::object children = scalar_attr(scalar_attr(std::move(self), "node"), "children");
    return scalar_call1(std::move(callable), std::move(children));
}
void vispy_scalar_detach(nb::object self) { scalar_set_attr(scalar_attr(std::move(self), "node"), "parent", nb::none()); }
nb::object vispy_scalar_ndisplay(nb::object self) { return scalar_attr(scalar_slice_input(std::move(self)), "ndisplay"); }
nb::object vispy_scalar_candidate_node(nb::object self, nb::object data, nb::object ndisplay, nb::dict globals) {
    ScalarNamespace ns(std::move(globals));
    nb::object callable = scalar_attr(scalar_attr(std::move(self), "_layer_node"), "get_node");
    nb::object getter = ns.get("getattr");
    nb::object dtype = scalar_call3(std::move(getter), data, nb::str("dtype"), nb::none());
    getter = ns.get("getattr");
    nb::object shape = scalar_call3(std::move(getter), std::move(data), nb::str("shape"), nb::none());
    return scalar_call3(std::move(callable), std::move(ndisplay), std::move(dtype), std::move(shape));
}
void vispy_scalar_display_node(nb::object self, nb::object data, nb::object ndisplay, nb::dict globals) {
    nb::object node = vispy_scalar_candidate_node(self, std::move(data), std::move(ndisplay), std::move(globals));
    scalar_set_attr(std::move(self), "node", std::move(node));
}
nb::object vispy_scalar_texture_format(nb::object self) { return scalar_attr(scalar_attr(std::move(self), "node"), "texture_format"); }
bool vispy_scalar_needs_blank(nb::object data) { return data.is_none(); }
nb::object vispy_scalar_blank_data(nb::object ndisplay, nb::object texture_format, nb::dict globals) {
    ScalarNamespace ns(std::move(globals));
    nb::object zeros = ns.numpy("zeros");
    nb::object shape = scalar_multiply(nb::make_tuple(nb::int_(1)), std::move(ndisplay));
    nb::object dtype = scalar_call1(ns.get("get_dtype_from_vispy_texture_format"), std::move(texture_format));
    return scalar_call_kw(std::move(zeros), nb::make_tuple(std::move(shape)), "dtype", std::move(dtype));
}
void vispy_scalar_display_finish(nb::object self, nb::object data, nb::object parent,
    nb::object children, nb::dict globals, VispyScalarFieldLocals& owner) {
    ScalarNamespace ns(std::move(globals));
    nb::object visible = scalar_visibility(self);
    scalar_set_attr(scalar_attr(self, "node"), "visible", std::move(visible));
    scalar_call1(scalar_attr(scalar_attr(self, "node"), "set_data"), std::move(data));
    scalar_set_attr(scalar_attr(self, "node"), "parent", std::move(parent));
    nb::object order = scalar_attr(self, "order");
    scalar_set_attr(scalar_attr(self, "node"), "order", std::move(order));
    nb::object iterator = scalar_iter(std::move(children));
    for (;;) {
        PyObject* next = PyIter_Next(iterator.ptr());
        if (!next) { if (PyErr_Occurred()) throw nb::python_error(); break; }
        owner.child = nb::steal<nb::object>(next);
        nb::object callable = ns.get("isinstance");
        nb::object old_parent = scalar_attr(owner.child, "parent");
        nb::object type = ns.get("TiledImageNode");
        if (!scalar_instance(std::move(old_parent), std::move(type), std::move(callable))) {
            nb::object node = scalar_attr(self, "node");
            scalar_set_attr(owner.child, "parent", std::move(node));
        }
    }
    iterator.reset();
    scalar_call0(scalar_attr(std::move(self), "reset"));
}
void vispy_scalar_source_data(nb::object self) {
    nb::object data = scalar_attr(scalar_layer(self), "_data_view");
    scalar_set_attr(std::move(self), "_data", std::move(data));
}
nb::object vispy_scalar_fixed_data(nb::object self, nb::dict globals) {
    ScalarNamespace ns(std::move(globals));
    nb::object callable = ns.get("fix_data_dtype");
    nb::object data = scalar_attr(scalar_layer(std::move(self)), "_data_view");
    return scalar_call1(std::move(callable), std::move(data));
}
bool vispy_scalar_needs_expand(nb::object self, nb::object ndisplay) {
    return scalar_test(std::move(ndisplay), scalar_attr(scalar_layer(std::move(self)), "ndim"), Py_GT);
}
nb::object vispy_scalar_expand(nb::object self, nb::object data, nb::object ndisplay) {
    nb::object callable = scalar_attr(data, "reshape");
    nb::object ndim = scalar_attr(scalar_layer(std::move(self)), "ndim");
    nb::object padding = scalar_multiply(nb::make_tuple(nb::int_(1)), scalar_subtract(std::move(ndisplay), std::move(ndim)));
    nb::object shape = scalar_add(std::move(padding), scalar_attr(std::move(data), "shape"));
    return scalar_call1(std::move(callable), std::move(shape));
}
bool vispy_scalar_needs_downsample_2d(nb::object self, nb::object ndisplay, nb::object node, nb::dict globals) {
    if (scalar_attr(self, "MAX_TEXTURE_SIZE_2D").is_none()) return false;
    if (!scalar_test(std::move(ndisplay), nb::int_(2), Py_EQ)) return false;
    ScalarNamespace ns(std::move(globals));
    nb::object callable = ns.get("isinstance");
    nb::object type = ns.get("LabelNode");
    return scalar_instance(std::move(node), std::move(type), std::move(callable));
}
bool vispy_scalar_needs_downsample_3d(nb::object self, nb::object ndisplay) {
    if (scalar_attr(std::move(self), "MAX_TEXTURE_SIZE_3D").is_none()) return false;
    return scalar_test(std::move(ndisplay), nb::int_(3), Py_EQ);
}
nb::object vispy_scalar_downsample_2d(nb::object self, nb::object data) {
    nb::object callable = scalar_attr(self, "downsample_texture");
    nb::object limit = scalar_attr(std::move(self), "MAX_TEXTURE_SIZE_2D");
    return scalar_call2(std::move(callable), std::move(data), std::move(limit));
}
nb::object vispy_scalar_downsample_3d(nb::object self, nb::object data) {
    nb::object callable = scalar_attr(self, "downsample_texture");
    nb::object limit = scalar_attr(std::move(self), "MAX_TEXTURE_SIZE_3D");
    return scalar_call2(std::move(callable), std::move(data), std::move(limit));
}
void vispy_scalar_upload_finish(nb::object self, nb::object data, nb::object ndisplay, nb::object node, nb::dict globals) {
    ScalarNamespace ns(std::move(globals));
    bool replace = false;
    if (scalar_test(ndisplay, nb::int_(3), Py_EQ)) {
        nb::object callable = ns.get("isinstance");
        nb::object type = ns.get("VolumeNode");
        replace = !scalar_instance(node, std::move(type), std::move(callable));
    }
    if (!replace && scalar_test(std::move(ndisplay), nb::int_(2), Py_EQ)) {
        nb::object callable = ns.get("isinstance");
        nb::object image = ns.get("ImageVisual"), tiled = ns.get("TiledImageNode");
        replace = !scalar_instance(node, nb::make_tuple(std::move(image), std::move(tiled)), std::move(callable));
    }
    if (!replace) replace = scalar_test(node, scalar_attr(self, "node"), Py_NE);
    if (replace) scalar_call1(scalar_attr(self, "_on_display_change"), std::move(data));
    else {
        scalar_call1(scalar_attr(node, "set_data"), std::move(data));
        nb::object visible = scalar_visibility(self);
        scalar_set_attr(node, "visible", std::move(visible));
    }
    scalar_call0(scalar_attr(std::move(self), "_on_matrix_change"));
    scalar_call0(scalar_attr(std::move(node), "update"));
}
void vispy_scalar_interpolation(nb::object self) {
    if (scalar_test(vispy_scalar_ndisplay(self), nb::int_(2), Py_EQ)) {
        nb::object value = scalar_attr(scalar_layer(self), "custom_interpolation_kernel_2d");
        scalar_set_attr(scalar_attr(std::move(self), "node"), "custom_kernel", std::move(value));
    }
}
void vispy_scalar_rendering(nb::object self, nb::dict globals) {
    ScalarNamespace ns(std::move(globals));
    if (scalar_node_is(self, "VolumeNode", ns)) {
        nb::object value = scalar_attr(scalar_layer(self), "rendering");
        scalar_set_attr(scalar_attr(std::move(self), "node"), "method", std::move(value));
    }
}
void vispy_scalar_depiction(nb::object self, nb::dict globals) {
    ScalarNamespace ns(std::move(globals));
    if (scalar_node_is(self, "VolumeNode", ns)) {
        nb::object callable = ns.get("str");
        nb::object depiction = scalar_attr(scalar_layer(self), "depiction");
        nb::object value = scalar_call1(std::move(callable), std::move(depiction));
        scalar_set_attr(scalar_attr(std::move(self), "node"), "raycasting_mode", std::move(value));
    }
}
void vispy_scalar_plane(nb::object self, const char* field, nb::dict globals) {
    ScalarNamespace ns(std::move(globals));
    if (scalar_node_is(self, "VolumeNode", ns)) {
        nb::object value = scalar_attr(scalar_attr(scalar_layer(self), "plane"), field);
        std::string target = std::string("plane_") + field;
        scalar_set_attr(scalar_attr(std::move(self), "node"), target.c_str(), std::move(value));
    }
}
nb::object vispy_scalar_call_base(nb::object callback) { return scalar_call0(std::move(callback)); }
void vispy_scalar_abstract_colormap(nb::dict globals) { scalar_raise(ScalarNamespace(std::move(globals)).get("NotImplementedError")); }
void vispy_scalar_reset_callbacks(nb::object self) {
    for (const char* name : {"_on_rendering_change", "_on_depiction_change", "_on_plane_position_change",
                            "_on_plane_normal_change", "_on_plane_thickness_change", "_on_custom_interpolation_kernel_2d_change"})
        scalar_call0(scalar_attr(self, name));
}
bool vispy_scalar_needs_downsample(nb::object data, nb::object maximum, nb::dict globals) {
    ScalarNamespace ns(std::move(globals));
    nb::object any = ns.numpy("any"), greater = ns.numpy("greater");
    nb::object shape = scalar_attr(std::move(data), "shape");
    return scalar_truth(scalar_call1(std::move(any), scalar_call2(std::move(greater), std::move(shape), std::move(maximum))));
}
void vispy_scalar_warn_downsample(nb::object self, nb::object data, nb::object maximum, nb::dict globals) {
    ScalarNamespace ns(std::move(globals));
    const bool multiscale = scalar_truth(scalar_attr(scalar_layer(self), "multiscale"));
    nb::object callable = multiscale ? ns.get("ValueError") : scalar_attr(ns.get("warnings"), "warn");
    nb::object message = nb::str(multiscale ? "Shape of individual tiles in multiscale " : "data shape ");
    message = scalar_concat(std::move(message), scalar_format(scalar_attr(std::move(data), "shape")));
    message = scalar_concat(std::move(message), nb::str(multiscale ? " cannot exceed GL_MAX_TEXTURE_SIZE " : " exceeds GL_MAX_TEXTURE_SIZE "));
    message = scalar_concat(std::move(message), scalar_format(std::move(maximum)));
    message = scalar_concat(std::move(message), nb::str(multiscale ? ". Rendering is currently in " : " in at least one axis and will be downsampled. Rendering is currently in "));
    message = scalar_concat(std::move(message), scalar_format(vispy_scalar_ndisplay(std::move(self))));
    message = scalar_concat(std::move(message), nb::str("D mode."));
    nb::object result = scalar_call1(std::move(callable), std::move(message));
    if (multiscale) scalar_raise(std::move(result));
}
nb::object vispy_scalar_downsample_steps(nb::object data, nb::object maximum, nb::dict globals) {
    ScalarNamespace ns(std::move(globals));
    nb::object ceil = ns.numpy("ceil"), divide = ns.numpy("divide");
    nb::object shape = scalar_attr(std::move(data), "shape");
    nb::object quotient = scalar_call2(std::move(divide), std::move(shape), std::move(maximum));
    nb::object rounded = scalar_call1(std::move(ceil), std::move(quotient));
    nb::object astype = scalar_attr(std::move(rounded), "astype");
    nb::object dtype = ns.get("int");
    return scalar_call1(std::move(astype), std::move(dtype));
}
nb::object vispy_scalar_downsample_scale(nb::object self, nb::dict globals) {
    ScalarNamespace ns(std::move(globals));
    nb::object callable = ns.numpy("ones");
    nb::object ndim = scalar_attr(scalar_layer(std::move(self)), "ndim");
    return scalar_call1(std::move(callable), std::move(ndim));
}
void vispy_scalar_fill_downsample_scale(nb::object self, nb::object downsample, nb::object scale,
    nb::dict globals, VispyScalarFieldLocals& owner) {
    ScalarNamespace ns(std::move(globals));
    nb::object callable = ns.get("enumerate");
    nb::object displayed = scalar_attr(scalar_slice_input(std::move(self)), "displayed");
    nb::object iterator = scalar_iter(scalar_call1(std::move(callable), std::move(displayed)));
    for (;;) {
        PyObject* next = PyIter_Next(iterator.ptr());
        if (!next) { if (PyErr_Occurred()) throw nb::python_error(); break; }
        ScalarPair pair = scalar_unpack_pair(nb::steal<nb::object>(next));
        owner.i = std::move(pair.first); owner.d = std::move(pair.second);
        nb::object value = scalar_item(downsample, owner.i);
        scalar_set(scale, owner.d, std::move(value));
    }
}
void vispy_scalar_publish_downsample_scale(nb::object self, nb::object scale) {
    nb::object transform = scalar_item(scalar_attr(scalar_layer(self), "_transforms"), nb::str("tile2data"));
    scalar_set_attr(std::move(transform), "scale", std::move(scale));
    scalar_call0(scalar_attr(std::move(self), "_on_matrix_change"));
}

struct VispyDownsampleSlicesIterator : collections::IteratorFrame {
    bool started = false, running = false, closed = false;
    void gc_clear() { closed = true; release_frame(); }
};
struct ScalarRunning {
    VispyDownsampleSlicesIterator& state;
    explicit ScalarRunning(VispyDownsampleSlicesIterator& value) : state(value) {
        if (state.running) throw nb::value_error("generator already executing");
        state.running = true;
    }
    ~ScalarRunning() { state.running = false; }
};
[[noreturn]] void scalar_generator_raise(nb::object error, bool convert_stop) {
    if (convert_stop && PyErr_GivenExceptionMatches(error.ptr(), PyExc_StopIteration)) {
        nb::object converted = scalar_checked(PyObject_CallFunction(PyExc_RuntimeError, "s", "generator raised StopIteration"));
        PyException_SetCause(converted.ptr(), error.inc_ref().ptr());
        PyException_SetContext(converted.ptr(), error.inc_ref().ptr());
        scalar_raise(std::move(converted));
    }
    scalar_raise(std::move(error));
}
nb::object scalar_slices_next(nb::handle self) {
    auto& state = *nb::inst_ptr<VispyDownsampleSlicesIterator>(self);
    ScalarRunning running(state);
    state.started = true;
    if (state.closed) throw nb::stop_iteration();
    try {
        nb::dict frame = state.frame();
        nb::object iterator = frame[".0"];
        PyObject* next = PyIter_Next(iterator.ptr());
        if (!next) {
            if (PyErr_Occurred()) throw nb::python_error();
            state.gc_clear(); throw nb::stop_iteration();
        }
        nb::object ds = nb::steal<nb::object>(next);
        frame["ds"] = ds;
        nb::object globals = frame["namespace"], builtins = frame["builtins"];
        ScalarNamespace ns(nb::borrow<nb::dict>(globals), std::move(builtins));
        return scalar_call3(ns.get("slice"), nb::none(), nb::none(), std::move(ds));
    } catch (const nb::python_error& error) {
        events::HandledException active(error.value());
        state.gc_clear();
        if (!error.matches(PyExc_StopIteration)) throw;
        scalar_generator_raise(nb::borrow<nb::object>(error.value()), true);
    } catch (...) { state.gc_clear(); throw; }
}
void scalar_slices_close(nb::handle self) {
    auto& state = *nb::inst_ptr<VispyDownsampleSlicesIterator>(self);
    ScalarRunning running(state);
    if (state.closed) return;
    nb::object exit = scalar_call0(nb::borrow<nb::object>(PyExc_GeneratorExit));
    events::HandledException active(exit);
    state.gc_clear();
}
nb::object scalar_throw_exception(nb::args args) {
    if (args.size() < 1 || args.size() > 3) {
        PyErr_Format(PyExc_TypeError, "throw expected %s %d argument%s, got %zd",
            args.size() < 1 ? "at least" : "at most", args.size() < 1 ? 1 : 3,
            args.size() < 1 ? "" : "s", args.size());
        throw nb::python_error();
    }
    nb::object supplied = args[0];
    nb::object value = args.size() > 1 ? nb::borrow<nb::object>(args[1]) : nb::none();
    nb::object trace = args.size() > 2 ? nb::borrow<nb::object>(args[2]) : nb::none();
    if (!trace.is_none() && !PyTraceBack_Check(trace.ptr())) throw nb::type_error("throw() third argument must be a traceback object");
    nb::object error;
    if (PyExceptionInstance_Check(supplied.ptr())) {
        if (!value.is_none()) throw nb::type_error("instance exception may not have a separate value");
        error = std::move(supplied);
    } else if (PyExceptionClass_Check(supplied.ptr())) {
        PyErr_SetObject(supplied.ptr(), value.ptr());
        PyObject *type = nullptr, *exception = nullptr, *traceback = nullptr;
        PyErr_Fetch(&type, &exception, &traceback); PyErr_NormalizeException(&type, &exception, &traceback);
        Py_XDECREF(type); Py_XDECREF(traceback); error = nb::steal<nb::object>(exception);
        if (PyErr_Occurred()) throw nb::python_error();
    } else {
        PyErr_Format(PyExc_TypeError, "exceptions must be classes or instances deriving from BaseException, not %s", Py_TYPE(supplied.ptr())->tp_name);
        throw nb::python_error();
    }
    if (!trace.is_none() && PyException_SetTraceback(error.ptr(), trace.ptr()) < 0) throw nb::python_error();
    return error;
}
void scalar_slices_throw(nb::handle self, nb::args args) {
    nb::object error = scalar_throw_exception(args);
    auto& state = *nb::inst_ptr<VispyDownsampleSlicesIterator>(self);
    ScalarRunning running(state);
    const bool open = !state.closed;
    events::HandledException active(error);
    state.gc_clear();
    scalar_generator_raise(std::move(error), open);
}
nb::object vispy_scalar_downsample_slices(nb::object downsample, nb::dict globals) {
    ScalarNamespace ns(std::move(globals));
    nb::object callable = ns.get("tuple");
    nb::object iterator = scalar_iter(std::move(downsample));
    VispyDownsampleSlicesIterator state;
    state.frame()[".0"] = std::move(iterator);
    state.frame()["namespace"] = ns.globals;
    state.frame()["builtins"] = ns.builtins;
    return scalar_call1(std::move(callable), nb::cast(std::move(state), nb::rv_policy::move));
}
nb::object vispy_scalar_take_slices(nb::object data, nb::object slices) { return scalar_item(std::move(data), std::move(slices)); }
nb::object vispy_scalar_dtype_from_format(nb::object format, nb::dict globals) {
    ScalarNamespace ns(std::move(globals));
    nb::object callable = scalar_attr(ns.get("_VISPY_FORMAT_TO_DTYPE"), "get");
    nb::object dtype = ns.numpy("dtype");
    nb::object type = ns.numpy("float32");
    nb::object fallback = scalar_call1(std::move(dtype), std::move(type));
    return scalar_call2(std::move(callable), std::move(format), std::move(fallback));
}

// These entrypoints own the whole source operation. The existing helpers
// remain available for compatibility, but facades no longer choose stages.
void vispy_scalar_initialize(nb::object self, nb::object layer, nb::object node,
    nb::object texture_format, nb::object layer_node_class, nb::object kwargs,
    nb::object base_factory, nb::dict globals) {
    vispy_scalar_create_layer_node(self, std::move(node), std::move(texture_format), std::move(layer_node_class));
    // The lexical Python factory performs only the original actual zero-arg
    // super lookup, at this point after _layer_node has been published.
    nb::object callback = scalar_call1(std::move(base_factory), self);
    vispy_scalar_call_base_init(std::move(callback), self, std::move(layer), std::move(kwargs));
    // Callback, base result and call temporaries have already been released.
    vispy_scalar_finish_init(std::move(self), std::move(globals));
}
void vispy_scalar_display_change(nb::object self, nb::dict globals, VispyScalarFieldLocals& owner) {
    if (!owner.data.is_valid()) throw nb::value_error("locals_owner has no data binding");
    owner.parent = vispy_scalar_parent(self);
    owner.children = vispy_scalar_children(self, globals);
    vispy_scalar_detach(self);
    owner.ndisplay = vispy_scalar_ndisplay(self);
    vispy_scalar_display_node(self, owner.data, owner.ndisplay, globals);
    if (vispy_scalar_needs_blank(owner.data)) {
        owner.texture_format = vispy_scalar_texture_format(self);
        owner.data = vispy_scalar_blank_data(owner.ndisplay, owner.texture_format, globals);
    }
    vispy_scalar_display_finish(std::move(self), owner.data, owner.parent, owner.children,
                               std::move(globals), owner);
}
void vispy_scalar_data_change(nb::object self, nb::dict globals, VispyScalarFieldLocals& owner) {
    vispy_scalar_source_data(self);
    owner.data = vispy_scalar_fixed_data(self, globals);
    owner.ndisplay = vispy_scalar_ndisplay(self);
    owner.node = vispy_scalar_candidate_node(self, owner.data, owner.ndisplay, globals);
    if (vispy_scalar_needs_expand(self, owner.ndisplay))
        owner.data = vispy_scalar_expand(self, owner.data, owner.ndisplay);
    if (vispy_scalar_needs_downsample_2d(self, owner.ndisplay, owner.node, globals))
        owner.data = vispy_scalar_downsample_2d(self, owner.data);
    if (vispy_scalar_needs_downsample_3d(self, owner.ndisplay))
        owner.data = vispy_scalar_downsample_3d(self, owner.data);
    vispy_scalar_upload_finish(std::move(self), owner.data, owner.ndisplay, owner.node, std::move(globals));
}
nb::object vispy_scalar_downsample_texture(nb::object self, nb::object maximum,
    nb::dict globals, VispyScalarFieldLocals& owner) {
    if (!owner.data.is_valid()) throw nb::value_error("locals_owner has no data binding");
    if (vispy_scalar_needs_downsample(owner.data, maximum, globals)) {
        vispy_scalar_warn_downsample(self, owner.data, maximum, globals);
        owner.downsample = vispy_scalar_downsample_steps(owner.data, std::move(maximum), globals);
        owner.scale = vispy_scalar_downsample_scale(self, globals);
        vispy_scalar_fill_downsample_scale(self, owner.downsample, owner.scale, globals, owner);
        vispy_scalar_publish_downsample_scale(std::move(self), owner.scale);
        owner.slices = vispy_scalar_downsample_slices(owner.downsample, std::move(globals));
        owner.data = vispy_scalar_take_slices(owner.data, owner.slices);
    }
    return owner.data;
}
void vispy_scalar_reset(nb::object self, nb::object base_factory) {
    nb::object callback = scalar_call1(std::move(base_factory), self);
    vispy_scalar_call_base(std::move(callback));
    vispy_scalar_reset_callbacks(std::move(self));
}
} // namespace

void bind_vispy_scalar_field(nb::module_& m) {
    nb::class_<VispyScalarFieldLocals>(m, "VispyScalarFieldLocals", nb::is_weak_referenceable(), nb::type_slots(scalar_locals_slots))
        .def_prop_ro("child", [](const VispyScalarFieldLocals& s) { return scalar_optional(s.child); })
        .def_prop_ro("i", [](const VispyScalarFieldLocals& s) { return scalar_optional(s.i); })
        .def_prop_ro("d", [](const VispyScalarFieldLocals& s) { return scalar_optional(s.d); })
        .def_prop_ro("parent", [](const VispyScalarFieldLocals& s) { return scalar_optional(s.parent); })
        .def_prop_ro("children", [](const VispyScalarFieldLocals& s) { return scalar_optional(s.children); })
        .def_prop_ro("ndisplay", [](const VispyScalarFieldLocals& s) { return scalar_optional(s.ndisplay); })
        .def_prop_ro("texture_format", [](const VispyScalarFieldLocals& s) { return scalar_optional(s.texture_format); })
        .def_prop_ro("data", [](const VispyScalarFieldLocals& s) { return scalar_optional(s.data); })
        .def_prop_ro("node", [](const VispyScalarFieldLocals& s) { return scalar_optional(s.node); })
        .def_prop_ro("downsample", [](const VispyScalarFieldLocals& s) { return scalar_optional(s.downsample); })
        .def_prop_ro("scale", [](const VispyScalarFieldLocals& s) { return scalar_optional(s.scale); })
        .def_prop_ro("slices", [](const VispyScalarFieldLocals& s) { return scalar_optional(s.slices); });
    nb::class_<VispyDownsampleSlicesIterator>(m, "VispyDownsampleSlicesIterator", nb::is_weak_referenceable(),
        nb::type_slots(collections::iterator_frame_slots<VispyDownsampleSlicesIterator>()))
        .def("__iter__", [](nb::handle self) { return nb::borrow<nb::object>(self); })
        .def("__next__", &scalar_slices_next)
        .def("send", [](nb::handle self, nb::object value) {
            auto& s = *nb::inst_ptr<VispyDownsampleSlicesIterator>(self);
            if (s.closed) throw nb::stop_iteration();
            if (!s.started && !value.is_none()) throw nb::type_error("can't send non-None value to a just-started generator");
            return scalar_slices_next(self);
        }, nb::arg("value").none())
        .def("close", &scalar_slices_close).def("throw", &scalar_slices_throw);
    m.def("vispy_scalar_field_locals", []() { return VispyScalarFieldLocals{}; });
    m.def("vispy_scalar_field_data_locals", [](nb::object data) {
        VispyScalarFieldLocals owner;
        owner.data = std::move(data);
        return owner;
    }, nb::arg("data").none());
    m.def("vispy_scalar_create_layer_node", &vispy_scalar_create_layer_node, nb::arg("self").none(), nb::arg("node").none(), nb::arg("texture_format").none(), nb::arg("layer_node_class").none());
    m.def("vispy_scalar_call_base_init", &vispy_scalar_call_base_init, nb::arg("callback").none(), nb::arg("self").none(), nb::arg("layer").none(), nb::arg("kwargs").none());
    m.def("vispy_scalar_finish_init", &vispy_scalar_finish_init, nb::arg("self").none(), nb::arg("namespace"));
    m.def("vispy_scalar_parent", &vispy_scalar_parent, nb::arg("self").none());
    m.def("vispy_scalar_children", &vispy_scalar_children, nb::arg("self").none(), nb::arg("namespace"));
    m.def("vispy_scalar_detach", &vispy_scalar_detach, nb::arg("self").none());
    m.def("vispy_scalar_ndisplay", &vispy_scalar_ndisplay, nb::arg("self").none());
    m.def("vispy_scalar_display_node", &vispy_scalar_display_node, nb::arg("self").none(), nb::arg("data").none(), nb::arg("ndisplay").none(), nb::arg("namespace"));
    m.def("vispy_scalar_texture_format", &vispy_scalar_texture_format, nb::arg("self").none());
    m.def("vispy_scalar_needs_blank", &vispy_scalar_needs_blank, nb::arg("data").none());
    m.def("vispy_scalar_blank_data", &vispy_scalar_blank_data, nb::arg("ndisplay").none(), nb::arg("texture_format").none(), nb::arg("namespace"));
    m.def("vispy_scalar_display_finish", &vispy_scalar_display_finish, nb::arg("self").none(), nb::arg("data").none(), nb::arg("parent").none(), nb::arg("children").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    m.def("vispy_scalar_source_data", &vispy_scalar_source_data, nb::arg("self").none());
    m.def("vispy_scalar_fixed_data", &vispy_scalar_fixed_data, nb::arg("self").none(), nb::arg("namespace"));
    m.def("vispy_scalar_candidate_node", &vispy_scalar_candidate_node, nb::arg("self").none(), nb::arg("data").none(), nb::arg("ndisplay").none(), nb::arg("namespace"));
    m.def("vispy_scalar_needs_expand", &vispy_scalar_needs_expand, nb::arg("self").none(), nb::arg("ndisplay").none());
    m.def("vispy_scalar_expand", &vispy_scalar_expand, nb::arg("self").none(), nb::arg("data").none(), nb::arg("ndisplay").none());
    m.def("vispy_scalar_needs_downsample_2d", &vispy_scalar_needs_downsample_2d, nb::arg("self").none(), nb::arg("ndisplay").none(), nb::arg("node").none(), nb::arg("namespace"));
    m.def("vispy_scalar_needs_downsample_3d", &vispy_scalar_needs_downsample_3d, nb::arg("self").none(), nb::arg("ndisplay").none());
    m.def("vispy_scalar_downsample_2d", &vispy_scalar_downsample_2d, nb::arg("self").none(), nb::arg("data").none());
    m.def("vispy_scalar_downsample_3d", &vispy_scalar_downsample_3d, nb::arg("self").none(), nb::arg("data").none());
    m.def("vispy_scalar_upload_finish", &vispy_scalar_upload_finish, nb::arg("self").none(), nb::arg("data").none(), nb::arg("ndisplay").none(), nb::arg("node").none(), nb::arg("namespace"));
    m.def("vispy_scalar_interpolation", &vispy_scalar_interpolation, nb::arg("self").none());
    m.def("vispy_scalar_rendering", &vispy_scalar_rendering, nb::arg("self").none(), nb::arg("namespace"));
    m.def("vispy_scalar_depiction", &vispy_scalar_depiction, nb::arg("self").none(), nb::arg("namespace"));
    m.def("vispy_scalar_plane", &vispy_scalar_plane, nb::arg("self").none(), nb::arg("field"), nb::arg("namespace"));
    m.def("vispy_scalar_call_base", &vispy_scalar_call_base, nb::arg("callback").none());
    m.def("vispy_scalar_abstract_colormap", &vispy_scalar_abstract_colormap, nb::arg("namespace"));
    m.def("vispy_scalar_reset_callbacks", &vispy_scalar_reset_callbacks, nb::arg("self").none());
    m.def("vispy_scalar_needs_downsample", &vispy_scalar_needs_downsample, nb::arg("data").none(), nb::arg("maximum").none(), nb::arg("namespace"));
    m.def("vispy_scalar_warn_downsample", &vispy_scalar_warn_downsample, nb::arg("self").none(), nb::arg("data").none(), nb::arg("maximum").none(), nb::arg("namespace"));
    m.def("vispy_scalar_downsample_steps", &vispy_scalar_downsample_steps, nb::arg("data").none(), nb::arg("maximum").none(), nb::arg("namespace"));
    m.def("vispy_scalar_downsample_scale", &vispy_scalar_downsample_scale, nb::arg("self").none(), nb::arg("namespace"));
    m.def("vispy_scalar_fill_downsample_scale", &vispy_scalar_fill_downsample_scale, nb::arg("self").none(), nb::arg("downsample").none(), nb::arg("scale").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    m.def("vispy_scalar_publish_downsample_scale", &vispy_scalar_publish_downsample_scale, nb::arg("self").none(), nb::arg("scale").none());
    m.def("vispy_scalar_downsample_slices", &vispy_scalar_downsample_slices, nb::arg("downsample").none(), nb::arg("namespace"));
    m.def("vispy_scalar_take_slices", &vispy_scalar_take_slices, nb::arg("data").none(), nb::arg("slices").none());
    m.def("vispy_scalar_dtype_from_format", &vispy_scalar_dtype_from_format, nb::arg("format_str").none(), nb::arg("namespace"));
    m.def("vispy_scalar_initialize", &vispy_scalar_initialize, nb::arg("self").none(), nb::arg("layer").none(),
        nb::arg("node").none(), nb::arg("texture_format").none(), nb::arg("layer_node_class").none(),
        nb::arg("kwargs").none(), nb::arg("base_factory").none(), nb::arg("namespace"));
    m.def("vispy_scalar_display_change", &vispy_scalar_display_change,
        nb::arg("self").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    m.def("vispy_scalar_data_change", &vispy_scalar_data_change,
        nb::arg("self").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    m.def("vispy_scalar_downsample_texture", &vispy_scalar_downsample_texture,
        nb::arg("self").none(), nb::arg("maximum").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    m.def("vispy_scalar_reset", &vispy_scalar_reset, nb::arg("self").none(), nb::arg("base_factory").none());
}
} // namespace nx
