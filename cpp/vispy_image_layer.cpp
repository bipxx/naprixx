#include "arrays.hpp"
#include "collection_protocol.hpp"
#include <array>
#include <initializer_list>
#include <utility>

namespace nx {
namespace {

nb::object vi_checked(PyObject* value) {
    if (!value) throw nb::python_error();
    return nb::steal<nb::object>(value);
}
nb::object vi_attr(nb::object value, const char* name) {
    return vi_checked(PyObject_GetAttrString(value.ptr(), name));
}
nb::object vi_item(nb::object value, nb::object key) {
    return vi_checked(PyObject_GetItem(value.ptr(), key.ptr()));
}
nb::object vi_call0(nb::object callback) {
    return vi_checked(PyObject_CallNoArgs(callback.ptr()));
}
nb::object vi_call1(nb::object callback, nb::object value) {
    return vi_checked(PyObject_CallOneArg(callback.ptr(), value.ptr()));
}
nb::object vi_call2(nb::object callback, nb::object first, nb::object second) {
    return vi_checked(PyObject_CallFunctionObjArgs(callback.ptr(), first.ptr(), second.ptr(), nullptr));
}
nb::object vi_call3(nb::object callback, nb::object first, nb::object second, nb::object third) {
    return vi_checked(PyObject_CallFunctionObjArgs(callback.ptr(), first.ptr(), second.ptr(), third.ptr(), nullptr));
}
nb::object vi_call_kw(nb::object callback, nb::tuple arguments, nb::dict keywords) {
    return vi_checked(PyObject_Call(callback.ptr(), arguments.ptr(), keywords.ptr()));
}
void vi_store(nb::handle self, const char* name, nb::object value) {
    if (PyObject_SetAttrString(self.ptr(), name, value.ptr()) < 0) throw nb::python_error();
}
void vi_set(nb::handle self, nb::object key, nb::object value) {
    if (PyObject_SetItem(self.ptr(), key.ptr(), value.ptr()) < 0) throw nb::python_error();
}
bool vi_truth(nb::object value) {
    const int result = PyObject_IsTrue(value.ptr());
    if (result < 0) throw nb::python_error();
    return result != 0;
}
bool vi_test(nb::object first, nb::object second, int operation) {
    return vi_truth(vi_checked(PyObject_RichCompare(first.ptr(), second.ptr(), operation)));
}
bool vi_contains(nb::object container, nb::object value) {
    const int result = PySequence_Contains(container.ptr(), value.ptr());
    if (result < 0) throw nb::python_error();
    return result != 0;
}
nb::object vi_subtract(nb::object first, nb::object second) {
    return vi_checked(PyNumber_Subtract(first.ptr(), second.ptr()));
}
nb::object vi_divide(nb::object first, nb::object second) {
    return vi_checked(PyNumber_TrueDivide(first.ptr(), second.ptr()));
}
nb::object vi_format(nb::object value) {
    return vi_checked(PyObject_Format(value.ptr(), nb::str("").ptr()));
}
nb::object vi_concat(nb::object first, nb::object second) {
    return vi_checked(PyUnicode_Concat(first.ptr(), second.ptr()));
}
[[noreturn]] void vi_raise(nb::object error) {
    if (PyExceptionInstance_Check(error.ptr()))
        PyErr_SetObject(reinterpret_cast<PyObject*>(Py_TYPE(error.ptr())), error.ptr());
    else if (PyExceptionClass_Check(error.ptr())) PyErr_SetNone(error.ptr());
    else PyErr_SetString(PyExc_TypeError, "exceptions must derive from BaseException");
    throw nb::python_error();
}
nb::object vi_layer(nb::object self) { return vi_attr(std::move(self), "layer"); }
nb::object vi_node(nb::object self) { return vi_attr(std::move(self), "node"); }
nb::object vi_slice_input(nb::object self) { return vi_attr(vi_layer(std::move(self)), "_slice_input"); }
nb::object vi_optional(const nb::object& value) { return value.is_valid() ? value : nb::none(); }

struct ImageNamespace {
    nb::dict globals;
    nb::object builtins;
    explicit ImageNamespace(nb::dict namespace_)
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
        nb::object message = vi_checked(PyUnicode_FromFormat("name '%s' is not defined", name));
        nb::object error = vi_call1(nb::borrow<nb::object>(PyExc_NameError), std::move(message));
        if (PyObject_SetAttrString(error.ptr(), "name", key.ptr()) < 0) throw nb::python_error();
        vi_raise(std::move(error));
    }
    nb::object numpy(const char* name) const { return vi_attr(get("np"), name); }
};

struct ImagePair { nb::object first, second; };
ImagePair vi_unpack_pair(nb::object value) {
    nb::object iterator = vi_checked(PyObject_GetIter(value.ptr()));
    value.reset();
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

// A carrier of original assigned locals, never a controller state mirror.
// The constructor parameter is transferred here because its source body can
// reassign it; the facade consequently releases its old parameter binding.
struct VispyImageLocals {
    enum class Layout { Empty, NodeInitialize, NodeGet, AxisOrder, IsoThreshold, Colormap };
    Layout layout = Layout::Empty;
    nb::object texture_format, M2D, s0, s1, res, current_order, cmin, cmax, cmap_args;
    VispyImageLocals() = default;
    VispyImageLocals(VispyImageLocals&&) = default;
    ~VispyImageLocals() { clear(); }
    void clear() {
        std::array<nb::object, 9> old = {
            std::move(texture_format), std::move(M2D), std::move(s0), std::move(s1),
            std::move(res), std::move(current_order), std::move(cmin), std::move(cmax),
            std::move(cmap_args),
        };
        const Layout previous = layout;
        layout = Layout::Empty;
        const auto release = [&](std::initializer_list<size_t> order) {
            for (size_t index : order) old[index].reset();
        };
        switch (previous) {
            case Layout::NodeInitialize: release({0}); break;
            case Layout::NodeGet: release({1, 2, 3, 4}); break;
            case Layout::AxisOrder: release({5}); break;
            case Layout::IsoThreshold: release({6, 7}); break;
            case Layout::Colormap: release({8}); break;
            case Layout::Empty: break;
        }
        for (nb::object& value : old) value.reset();
    }
};
int vi_locals_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) {
        const auto& state = *nb::inst_ptr<VispyImageLocals>(self);
        Py_VISIT(state.texture_format.ptr()); Py_VISIT(state.M2D.ptr());
        Py_VISIT(state.s0.ptr()); Py_VISIT(state.s1.ptr()); Py_VISIT(state.res.ptr());
        Py_VISIT(state.current_order.ptr()); Py_VISIT(state.cmin.ptr());
        Py_VISIT(state.cmax.ptr()); Py_VISIT(state.cmap_args.ptr());
    }
    return 0;
}
int vi_locals_clear(PyObject* self) {
    if (nb::inst_ready(self)) nb::inst_ptr<VispyImageLocals>(self)->clear();
    return 0;
}
PyType_Slot vi_locals_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(vi_locals_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(vi_locals_clear)}, {0, nullptr},
};

void vi_expand_keywords(nb::handle callback, nb::dict& keywords, nb::object mapping) {
    if (_PyDict_MergeEx(keywords.ptr(), mapping.ptr(), 2) >= 0) return;
    if (PyErr_ExceptionMatches(PyExc_AttributeError)) {
        PyErr_Clear();
        nb::object label = vi_checked(_PyObject_FunctionStr(callback.ptr()));
        PyErr_Format(PyExc_TypeError, "%U argument after ** must be a mapping, not %.200s",
                     label.ptr(), Py_TYPE(mapping.ptr())->tp_name);
    } else if (PyErr_ExceptionMatches(PyExc_KeyError)) {
        PyObject *type = nullptr, *value = nullptr, *traceback = nullptr;
        PyErr_Fetch(&type, &value, &traceback);
        PyErr_NormalizeException(&type, &value, &traceback);
        nb::object old_type = nb::steal<nb::object>(type);
        nb::object old_value = nb::steal<nb::object>(value);
        nb::object old_traceback = nb::steal<nb::object>(traceback);
        nb::object key = vi_item(vi_attr(old_value, "args"), nb::int_(0));
        nb::object label = vi_checked(_PyObject_FunctionStr(callback.ptr()));
        PyErr_Format(PyExc_TypeError, "%U got multiple values for keyword argument '%S'", label.ptr(), key.ptr());
    }
    throw nb::python_error();
}

nb::object vi_float_array(const ImageNamespace& ns) {
    nb::object callback = ns.numpy("array");
    nb::list row;
    row.append(nb::float_(0.0));
    nb::list data;
    data.append(row);
    row.reset();
    nb::object dtype = ns.numpy("float32");
    nb::tuple arguments = nb::make_tuple(data);
    data.reset();
    nb::dict keywords;
    keywords["dtype"] = dtype;
    dtype.reset();
    return vi_call_kw(std::move(callback), std::move(arguments), std::move(keywords));
}
nb::object vi_float_volume(const ImageNamespace& ns) {
    nb::object callback = ns.numpy("zeros");
    nb::tuple shape = nb::make_tuple(nb::int_(1), nb::int_(1), nb::int_(1));
    nb::object dtype = ns.numpy("float32");
    nb::tuple arguments = nb::make_tuple(shape);
    shape.reset();
    nb::dict keywords;
    keywords["dtype"] = dtype;
    dtype.reset();
    return vi_call_kw(std::move(callback), std::move(arguments), std::move(keywords));
}

void vispy_image_node_initialize(nb::object self, nb::object custom_node,
    nb::dict globals, VispyImageLocals& owner) {
    if (!owner.texture_format.is_valid()) throw nb::value_error("locals_owner has no texture_format binding");
    owner.layout = VispyImageLocals::Layout::NodeInitialize;
    ImageNamespace ns(std::move(globals));
    if (vi_test(owner.texture_format, nb::str("auto"), Py_EQ)) {
        nb::object needle = nb::str("texture_float");
        nb::object extensions = vi_call0(ns.get("get_gl_extensions"));
        if (!vi_contains(std::move(extensions), std::move(needle))) owner.texture_format = nb::none();
    }
    vi_store(self, "_custom_node", std::move(custom_node));
    ImagePair maxima = vi_unpack_pair(vi_call0(ns.get("get_max_texture_sizes")));
    vi_store(self, "MAX_TEXTURE_SIZE_2D", std::move(maxima.first));
    vi_store(self, "MAX_TEXTURE_SIZE_3D", std::move(maxima.second));
    {
        nb::object callback = ns.get("ImageNode");
        nb::object data;
        if (owner.texture_format.is_none() || vi_test(owner.texture_format, nb::str("auto"), Py_EQ))
            data = nb::none();
        else data = vi_float_array(ns);
        nb::tuple arguments = nb::make_tuple(data);
        data.reset();
        nb::dict keywords;
        keywords["method"] = nb::str("auto");
        keywords["texture_format"] = owner.texture_format;
        nb::object node = vi_call_kw(std::move(callback), std::move(arguments), std::move(keywords));
        vi_store(self, "_image_node", std::move(node));
    }
    {
        nb::object callback = ns.get("TiledImageNode");
        nb::object data = vi_float_array(ns);
        nb::tuple arguments = nb::make_tuple(data);
        data.reset();
        nb::object maximum = vi_attr(self, "MAX_TEXTURE_SIZE_2D");
        nb::dict keywords;
        keywords["tile_size"] = maximum;
        maximum.reset();
        keywords["texture_format"] = owner.texture_format;
        nb::object node = vi_call_kw(std::move(callback), std::move(arguments), std::move(keywords));
        vi_store(self, "_tiledimage_node", std::move(node));
    }
    {
        nb::object callback = ns.get("VolumeNode");
        nb::object data = vi_float_volume(ns);
        nb::tuple arguments = nb::make_tuple(data);
        data.reset();
        nb::list clim;
        clim.append(nb::int_(0)); clim.append(nb::int_(1));
        nb::dict keywords;
        keywords["clim"] = clim;
        clim.reset();
        keywords["texture_format"] = owner.texture_format;
        nb::object node = vi_call_kw(std::move(callback), std::move(arguments), std::move(keywords));
        vi_store(self, "_volume_node", std::move(node));
    }
}

nb::object vi_automatic_formats() {
    nb::object formats = vi_checked(PyFrozenSet_New(nullptr));
    if (PySet_Add(formats.ptr(), nb::str("auto").ptr()) < 0 ||
        PySet_Add(formats.ptr(), Py_None) < 0) throw nb::python_error();
    return formats;
}

nb::object vispy_image_get_node(nb::object self, nb::object ndisplay, nb::object dtype,
    nb::object shape, nb::dict globals, VispyImageLocals& owner) {
    owner.layout = VispyImageLocals::Layout::NodeGet;
    if (!vi_attr(self, "_custom_node").is_none()) return vi_attr(self, "_custom_node");
    owner.M2D = vi_attr(self, "MAX_TEXTURE_SIZE_2D");
    bool tiled = false;
    if (vi_test(ndisplay, nb::int_(2), Py_EQ) &&
        (Py_TYPE(shape.ptr())->tp_flags & Py_TPFLAGS_SEQUENCE) != 0) {
        const Py_ssize_t size = PyObject_Size(shape.ptr());
        if (size < 0) throw nb::python_error();
        if (size >= 2) {
            // The wildcard starred pattern consumes no tail. Captures are
            // assigned only after its two indexed loads both succeed.
            nb::object s0 = vi_item(shape, nb::int_(0));
            nb::object s1 = vi_item(shape, nb::int_(1));
            owner.s0 = std::move(s0);
            owner.s1 = std::move(s1);
            tiled = vi_test(owner.s0, owner.M2D, Py_GT) || vi_test(owner.s1, owner.M2D, Py_GT);
        }
    }
    if (tiled) owner.res = vi_attr(self, "_tiledimage_node");
    else if (vi_test(ndisplay, nb::int_(2), Py_EQ)) owner.res = vi_attr(self, "_image_node");
    else owner.res = vi_attr(self, "_volume_node");
    nb::object format = vi_attr(owner.res, "texture_format");
    if (!vi_contains(vi_automatic_formats(), std::move(format)) && !dtype.is_none()) {
        ImageNamespace ns(std::move(globals));
        nb::object formats = ns.get("_VISPY_FORMAT_TO_DTYPE");
        nb::object key = vi_attr(owner.res, "texture_format");
        nb::object actual_dtype = vi_item(std::move(formats), std::move(key));
        if (vi_test(std::move(actual_dtype), dtype, Py_NE)) {
            nb::object error_type = ns.get("ValueError");
            nb::object message = vi_concat(nb::str("dtype "), vi_format(dtype));
            message = vi_concat(std::move(message), nb::str(" does not match texture_format="));
            message = vi_concat(std::move(message), vi_format(vi_attr(owner.res, "texture_format")));
            vi_raise(vi_call1(std::move(error_type), std::move(message)));
        }
    }
    return owner.res;
}

void vi_connect(nb::object self, const char* emitter_name, const char* callback_name) {
    nb::object events = vi_attr(vi_layer(self), "events");
    nb::object emitter = vi_attr(std::move(events), emitter_name);
    nb::object connect = vi_attr(std::move(emitter), "connect");
    nb::object callback = vi_attr(std::move(self), callback_name);
    vi_call1(std::move(connect), std::move(callback));
}
void vi_call_override(nb::object self, const char* name) {
    vi_call0(vi_attr(std::move(self), name));
}
void vi_call_base(nb::object self, nb::object base_factory) {
    nb::object callback = vi_call1(std::move(base_factory), std::move(self));
    vi_call0(std::move(callback));
}
void vispy_image_initialize(nb::object self, nb::object layer, nb::object node,
    nb::object texture_format, nb::object layer_node_class, nb::object kwargs,
    nb::object base_factory) {
    vi_store(self, "_last_order", nb::none());
    {
        nb::object callback = vi_call1(std::move(base_factory), self);
        nb::tuple arguments = nb::make_tuple(layer);
        layer.reset();
        nb::dict keywords;
        keywords["node"] = node;
        node.reset();
        keywords["texture_format"] = texture_format;
        texture_format.reset();
        keywords["layer_node_class"] = layer_node_class;
        layer_node_class.reset();
        vi_expand_keywords(callback, keywords, std::move(kwargs));
        vi_call_kw(std::move(callback), std::move(arguments), std::move(keywords));
    }
    vi_connect(self, "interpolation2d", "_on_interpolation_change");
    vi_connect(self, "interpolation3d", "_on_interpolation_change");
    vi_connect(self, "contrast_limits", "_on_contrast_limits_change");
    vi_connect(self, "gamma", "_on_gamma_change");
    vi_connect(self, "iso_threshold", "_on_iso_threshold_change");
    vi_connect(self, "attenuation", "_on_attenuation_change");
    vi_connect(self, "set_data", "_on_axis_order_change");
    vi_call_override(self, "_on_display_change");
    vi_call_override(self, "reset");
    vi_call_override(std::move(self), "_on_data_change");
}

bool vi_is_node(nb::object self, const ImageNamespace& ns, const char* name) {
    nb::object instance = ns.get("isinstance");
    nb::object node = vi_node(std::move(self));
    nb::object type = ns.get(name);
    return vi_truth(vi_call2(std::move(instance), std::move(node), std::move(type)));
}
void vispy_image_axis_order(nb::object self, nb::dict globals, VispyImageLocals& owner) {
    owner.layout = VispyImageLocals::Layout::AxisOrder;
    owner.current_order = vi_attr(vi_slice_input(self), "order");
    if (vi_test(owner.current_order, vi_attr(self, "_last_order"), Py_NE)) {
        ImageNamespace ns(std::move(globals));
        if (vi_is_node(self, ns, "TiledImageNode"))
            vi_call_override(vi_node(self), "handle_axis_change");
        vi_store(std::move(self), "_last_order", owner.current_order);
    }
}
void vispy_image_interpolation(nb::object self) {
    nb::object value;
    if (vi_test(vi_attr(vi_slice_input(self), "ndisplay"), nb::int_(2), Py_EQ))
        value = vi_attr(vi_layer(self), "interpolation2d");
    else value = vi_attr(vi_layer(self), "interpolation3d");
    vi_store(vi_node(std::move(self)), "interpolation", std::move(value));
}
void vispy_image_rendering(nb::object self, nb::object base_factory) {
    vi_call_base(self, std::move(base_factory));
    vi_call_override(self, "_on_attenuation_change");
    vi_call_override(std::move(self), "_on_iso_threshold_change");
}
void vispy_image_colormap(nb::object self, nb::dict globals) {
    ImageNamespace ns(std::move(globals));
    nb::object callback = ns.get("_napari_cmap_to_vispy");
    nb::object colormap = vi_attr(vi_layer(self), "colormap");
    nb::object value = vi_call1(std::move(callback), std::move(colormap));
    vi_store(vi_node(std::move(self)), "cmap", std::move(value));
}
void vispy_image_cutoffs(nb::object self, nb::dict globals) {
    ImageNamespace ns(std::move(globals));
    if (!vi_is_node(self, ns, "VolumeNode")) return;
    nb::object blending = vi_attr(vi_layer(self), "blending");
    nb::object translucent = vi_attr(ns.get("Blending"), "TRANSLUCENT");
    nb::object no_depth = vi_attr(ns.get("Blending"), "TRANSLUCENT_NO_DEPTH");
    PyObject* raw_modes = PySet_New(nullptr);
    if (!raw_modes) {
        no_depth.reset(); translucent.reset();
        throw nb::python_error();
    }
    nb::object modes = nb::steal<nb::object>(raw_modes);
    int status = PySet_Add(modes.ptr(), translucent.ptr());
    translucent.reset();
    if (status >= 0) status = PySet_Add(modes.ptr(), no_depth.ptr());
    no_depth.reset();
    if (status < 0) throw nb::python_error();
    if (vi_contains(std::move(modes), std::move(blending))) {
        nb::object texture = vi_attr(vi_node(self), "_texture");
        nb::object normalized = vi_attr(std::move(texture), "clim_normalized");
        nb::object cutoff = vi_item(std::move(normalized), nb::int_(0));
        vi_store(vi_node(self), "mip_cutoff", std::move(cutoff));
        texture = vi_attr(vi_node(self), "_texture");
        normalized = vi_attr(std::move(texture), "clim_normalized");
        cutoff = vi_item(std::move(normalized), nb::int_(1));
        vi_store(vi_node(std::move(self)), "minip_cutoff", std::move(cutoff));
    } else {
        vi_store(vi_node(self), "mip_cutoff", nb::none());
        vi_store(vi_node(std::move(self)), "minip_cutoff", nb::none());
    }
}
void vispy_image_contrast_limits(nb::object self, nb::dict globals) {
    ImageNamespace ns(std::move(globals));
    nb::object callback = ns.get("_coerce_contrast_limits");
    nb::object limits = vi_attr(vi_layer(self), "contrast_limits");
    nb::object value = vi_attr(vi_call1(std::move(callback), std::move(limits)), "contrast_limits");
    vi_store(vi_node(self), "clim", std::move(value));
    vi_call_override(self, "_update_mip_minip_cutoff");
    vi_call_override(std::move(self), "_on_iso_threshold_change");
}
void vispy_image_blending(nb::object self, nb::object base_factory) {
    vi_call_base(self, std::move(base_factory));
    vi_call_override(std::move(self), "_update_mip_minip_cutoff");
}
void vispy_image_gamma(nb::object self) {
    nb::object value = vi_attr(vi_layer(self), "gamma");
    vi_store(vi_node(std::move(self)), "gamma", std::move(value));
}
void vispy_image_iso_threshold(nb::object self, nb::dict globals, VispyImageLocals& owner) {
    owner.layout = VispyImageLocals::Layout::IsoThreshold;
    ImageNamespace ns(std::move(globals));
    if (!vi_is_node(self, ns, "VolumeNode")) return;
    if (vi_truth(vi_attr(vi_attr(vi_node(self), "_texture"), "is_normalized"))) {
        ImagePair limits = vi_unpack_pair(vi_attr(vi_layer(self), "contrast_limits_range"));
        owner.cmin = std::move(limits.first);
        owner.cmax = std::move(limits.second);
        nb::object threshold = vi_attr(vi_layer(self), "iso_threshold");
        nb::object numerator = vi_subtract(std::move(threshold), owner.cmin);
        nb::object denominator = vi_subtract(owner.cmax, owner.cmin);
        nb::object value = vi_divide(std::move(numerator), std::move(denominator));
        vi_store(vi_node(std::move(self)), "threshold", std::move(value));
    } else {
        nb::object value = vi_attr(vi_layer(self), "iso_threshold");
        vi_store(vi_node(std::move(self)), "threshold", std::move(value));
    }
}
void vispy_image_attenuation(nb::object self, nb::dict globals) {
    ImageNamespace ns(std::move(globals));
    if (vi_is_node(self, ns, "VolumeNode")) {
        nb::object value = vi_attr(vi_layer(self), "attenuation");
        vi_store(vi_node(std::move(self)), "attenuation", std::move(value));
    }
}
void vispy_image_reset(nb::object self, nb::object base_factory) {
    vi_call_base(self, std::move(base_factory));
    vi_call_override(self, "_on_interpolation_change");
    vi_call_override(self, "_on_colormap_change");
    vi_call_override(self, "_on_contrast_limits_change");
    vi_call_override(std::move(self), "_on_gamma_change");
}

nb::object vispy_image_gl_extensions(nb::dict globals) {
    ImageNamespace ns(std::move(globals));
    nb::object context = vi_call0(ns.get("_opengl_context"));
    nb::object enter = collections::context_method(context, "__enter__", false);
    nb::object exit = collections::context_method(context, "__exit__", true);
    context.reset();
    vi_call0(std::move(enter));
    nb::object result;
    try {
        nb::object callback = vi_attr(ns.get("gl"), "glGetParameter");
        nb::object parameter = vi_attr(ns.get("gl"), "GL_EXTENSIONS");
        result = vi_call1(std::move(callback), std::move(parameter));
    } catch (const nb::python_error& original) {
        events::HandledException active(original.value());
        nb::object traceback = original.traceback();
        if (!traceback.is_valid()) traceback = nb::none();
        try {
            if (!vi_truth(vi_call3(exit, nb::borrow<nb::object>(original.type()),
                                 nb::borrow<nb::object>(original.value()), std::move(traceback)))) throw;
        } catch (const nb::python_error& cleanup) {
            if (cleanup.value().ptr() != original.value().ptr())
                PyException_SetContext(cleanup.value().ptr(), original.value().inc_ref().ptr());
            throw;
        }
        return nb::none();
    }
    vi_call3(std::move(exit), nb::none(), nb::none(), nb::none());
    return result;
}

nb::object vispy_image_cmap_to_vispy(nb::object colormap, nb::dict globals, VispyImageLocals& owner) {
    owner.layout = VispyImageLocals::Layout::Colormap;
    owner.cmap_args = vi_call0(vi_attr(std::move(colormap), "model_dump"));
    vi_call1(vi_attr(owner.cmap_args, "pop"), nb::str("name"));
    nb::object bad_color = vi_call1(vi_attr(owner.cmap_args, "pop"), nb::str("nan_color"));
    vi_set(owner.cmap_args, nb::str("bad_color"), std::move(bad_color));
    ImageNamespace ns(std::move(globals));
    nb::object callback = ns.get("VispyColormap");
    nb::dict keywords;
    vi_expand_keywords(callback, keywords, owner.cmap_args);
    return vi_call_kw(std::move(callback), nb::tuple(), std::move(keywords));
}

} // namespace

void bind_vispy_image_layer(nb::module_& module) {
    nb::class_<VispyImageLocals>(module, "VispyImageLocals", nb::is_weak_referenceable(), nb::type_slots(vi_locals_slots))
        .def_prop_ro("texture_format", [](const VispyImageLocals& state) { return vi_optional(state.texture_format); })
        .def_prop_ro("M2D", [](const VispyImageLocals& state) { return vi_optional(state.M2D); })
        .def_prop_ro("s0", [](const VispyImageLocals& state) { return vi_optional(state.s0); })
        .def_prop_ro("s1", [](const VispyImageLocals& state) { return vi_optional(state.s1); })
        .def_prop_ro("res", [](const VispyImageLocals& state) { return vi_optional(state.res); })
        .def_prop_ro("current_order", [](const VispyImageLocals& state) { return vi_optional(state.current_order); })
        .def_prop_ro("cmin", [](const VispyImageLocals& state) { return vi_optional(state.cmin); })
        .def_prop_ro("cmax", [](const VispyImageLocals& state) { return vi_optional(state.cmax); })
        .def_prop_ro("cmap_args", [](const VispyImageLocals& state) { return vi_optional(state.cmap_args); });
    module.def("vispy_image_locals", [] { return VispyImageLocals{}; });
    module.def("vispy_image_format_locals", [](nb::object texture_format) {
        VispyImageLocals owner;
        owner.layout = VispyImageLocals::Layout::NodeInitialize;
        owner.texture_format = std::move(texture_format);
        return owner;
    }, nb::arg("texture_format").none());
    module.def("vispy_image_node_initialize", &vispy_image_node_initialize,
        nb::arg("self").none(), nb::arg("custom_node").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("vispy_image_get_node", &vispy_image_get_node,
        nb::arg("self").none(), nb::arg("ndisplay").none(), nb::arg("dtype").none(),
        nb::arg("shape").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("vispy_image_initialize", &vispy_image_initialize,
        nb::arg("self").none(), nb::arg("layer").none(), nb::arg("node").none(),
        nb::arg("texture_format").none(), nb::arg("layer_node_class").none(),
        nb::arg("kwargs").none(), nb::arg("base_factory").none());
    module.def("vispy_image_axis_order", &vispy_image_axis_order,
        nb::arg("self").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("vispy_image_interpolation", &vispy_image_interpolation, nb::arg("self").none());
    module.def("vispy_image_rendering", &vispy_image_rendering,
        nb::arg("self").none(), nb::arg("base_factory").none());
    module.def("vispy_image_colormap", &vispy_image_colormap, nb::arg("self").none(), nb::arg("namespace"));
    module.def("vispy_image_cutoffs", &vispy_image_cutoffs, nb::arg("self").none(), nb::arg("namespace"));
    module.def("vispy_image_contrast_limits", &vispy_image_contrast_limits, nb::arg("self").none(), nb::arg("namespace"));
    module.def("vispy_image_blending", &vispy_image_blending,
        nb::arg("self").none(), nb::arg("base_factory").none());
    module.def("vispy_image_gamma", &vispy_image_gamma, nb::arg("self").none());
    module.def("vispy_image_iso_threshold", &vispy_image_iso_threshold,
        nb::arg("self").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("vispy_image_attenuation", &vispy_image_attenuation, nb::arg("self").none(), nb::arg("namespace"));
    module.def("vispy_image_reset", &vispy_image_reset, nb::arg("self").none(), nb::arg("base_factory").none());
    module.def("vispy_image_gl_extensions", &vispy_image_gl_extensions, nb::arg("namespace"));
    module.def("vispy_image_cmap_to_vispy", &vispy_image_cmap_to_vispy,
        nb::arg("colormap").none(), nb::arg("namespace"), nb::arg("locals_owner"));
}
} // namespace nx
