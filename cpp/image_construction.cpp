#include "arrays.hpp"
#include <array>
#include <utility>

namespace nx {
namespace {

nb::object ic_checked(PyObject* value) {
    if (!value) throw nb::python_error();
    return nb::steal<nb::object>(value);
}
nb::object ic_attr(nb::object value, const char* name) {
    return ic_checked(PyObject_GetAttrString(value.ptr(), name));
}
nb::object ic_item(nb::object value, nb::object key) {
    return ic_checked(PyObject_GetItem(value.ptr(), key.ptr()));
}
nb::object ic_call0(nb::object callback) {
    return ic_checked(PyObject_CallNoArgs(callback.ptr()));
}
nb::object ic_call1(nb::object callback, nb::object value) {
    return ic_checked(PyObject_CallOneArg(callback.ptr(), value.ptr()));
}
nb::object ic_call2(nb::object callback, nb::object first, nb::object second) {
    return ic_checked(PyObject_CallFunctionObjArgs(callback.ptr(), first.ptr(), second.ptr(), nullptr));
}
nb::object ic_call3(nb::object callback, nb::object first, nb::object second, nb::object third) {
    return ic_checked(PyObject_CallFunctionObjArgs(callback.ptr(), first.ptr(), second.ptr(), third.ptr(), nullptr));
}
nb::object ic_call_kw(nb::object callback, nb::tuple arguments, nb::dict keywords) {
    return ic_checked(PyObject_Call(callback.ptr(), arguments.ptr(), keywords.ptr()));
}
void ic_store(nb::handle self, const char* name, nb::object value) {
    if (PyObject_SetAttrString(self.ptr(), name, value.ptr()) < 0) throw nb::python_error();
}
void ic_set(nb::object target, nb::object key, nb::object value) {
    if (PyObject_SetItem(target.ptr(), key.ptr(), value.ptr()) < 0) throw nb::python_error();
}
bool ic_truth(nb::object value) {
    const int result = PyObject_IsTrue(value.ptr());
    if (result < 0) throw nb::python_error();
    return result != 0;
}
nb::object ic_compare(nb::object first, nb::object second, int operation) {
    return ic_checked(PyObject_RichCompare(first.ptr(), second.ptr(), operation));
}
nb::object ic_subtract(nb::object first, nb::object second) {
    return ic_checked(PyNumber_Subtract(first.ptr(), second.ptr()));
}
nb::object ic_divide(nb::object first, nb::object second) {
    return ic_checked(PyNumber_TrueDivide(first.ptr(), second.ptr()));
}
nb::object ic_add(nb::object first, nb::object second) {
    return ic_checked(PyNumber_Add(first.ptr(), second.ptr()));
}
void ic_replace(nb::object& target, nb::object value) {
    nb::object previous = std::move(target);
    target = std::move(value);
    previous.reset();
}
nb::object ic_optional(const nb::object& value) {
    return value.is_valid() ? value : nb::none();
}
[[noreturn]] void ic_raise(nb::object error) {
    if (PyExceptionInstance_Check(error.ptr()))
        PyErr_SetObject(reinterpret_cast<PyObject*>(Py_TYPE(error.ptr())), error.ptr());
    else if (PyExceptionClass_Check(error.ptr())) PyErr_SetNone(error.ptr());
    else PyErr_SetString(PyExc_TypeError, "exceptions must derive from BaseException");
    throw nb::python_error();
}

struct ConstructionNamespace {
    nb::dict globals;
    nb::object builtins;
    explicit ConstructionNamespace(nb::dict namespace_)
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
        nb::object message = ic_checked(PyUnicode_FromFormat("name '%s' is not defined", name));
        nb::object error = ic_call1(nb::borrow<nb::object>(PyExc_NameError), std::move(message));
        if (PyObject_SetAttrString(error.ptr(), "name", key.ptr()) < 0) throw nb::python_error();
        ic_raise(std::move(error));
    }
    nb::object numpy(const char* name) const { return ic_attr(get("np"), name); }
};

// These are original fast-local bindings only, not copies of instance state.
// Reassigned parameters transfer here before the facade releases its binding.
struct ScalarFieldConstructionLocals {
    nb::object data, axis_labels, multiscale, name, ndim, scale, translate, units;
    nb::object xr_source, rgb, xr_metadata, displayed_axes;
    ScalarFieldConstructionLocals() = default;
    ScalarFieldConstructionLocals(ScalarFieldConstructionLocals&&) = default;
    ~ScalarFieldConstructionLocals() { clear(); }
    void clear() {
        std::array<nb::object, 12> old = {
            std::move(data), std::move(axis_labels), std::move(multiscale), std::move(name),
            std::move(ndim), std::move(scale), std::move(translate), std::move(units),
            std::move(xr_source), std::move(rgb), std::move(xr_metadata), std::move(displayed_axes),
        };
        for (nb::object& value : old) value.reset();
    }
};
struct ImageConstructionLocals {
    nb::object rgb, data_shape, dtype, cmin, cmax;
    ImageConstructionLocals() = default;
    ImageConstructionLocals(ImageConstructionLocals&&) = default;
    ~ImageConstructionLocals() { clear(); }
    void clear() {
        std::array<nb::object, 5> old = {
            std::move(rgb), std::move(data_shape), std::move(dtype), std::move(cmin), std::move(cmax),
        };
        for (nb::object& value : old) value.reset();
    }
};
struct IntensityConstructionLocals {
    nb::object ColorBarOverlay;
    IntensityConstructionLocals() = default;
    IntensityConstructionLocals(IntensityConstructionLocals&&) = default;
    ~IntensityConstructionLocals() { clear(); }
    void clear() { nb::object previous = std::move(ColorBarOverlay); previous.reset(); }
};

int ic_scalar_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) {
        const auto& s = *nb::inst_ptr<ScalarFieldConstructionLocals>(self);
        Py_VISIT(s.data.ptr()); Py_VISIT(s.axis_labels.ptr()); Py_VISIT(s.multiscale.ptr());
        Py_VISIT(s.name.ptr()); Py_VISIT(s.ndim.ptr()); Py_VISIT(s.scale.ptr());
        Py_VISIT(s.translate.ptr()); Py_VISIT(s.units.ptr()); Py_VISIT(s.xr_source.ptr());
        Py_VISIT(s.rgb.ptr()); Py_VISIT(s.xr_metadata.ptr()); Py_VISIT(s.displayed_axes.ptr());
    }
    return 0;
}
int ic_scalar_clear(PyObject* self) {
    if (nb::inst_ready(self)) nb::inst_ptr<ScalarFieldConstructionLocals>(self)->clear();
    return 0;
}
PyType_Slot ic_scalar_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(ic_scalar_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(ic_scalar_clear)}, {0, nullptr},
};
int ic_image_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) {
        const auto& s = *nb::inst_ptr<ImageConstructionLocals>(self);
        Py_VISIT(s.rgb.ptr()); Py_VISIT(s.data_shape.ptr()); Py_VISIT(s.dtype.ptr());
        Py_VISIT(s.cmin.ptr()); Py_VISIT(s.cmax.ptr());
    }
    return 0;
}
int ic_image_clear(PyObject* self) {
    if (nb::inst_ready(self)) nb::inst_ptr<ImageConstructionLocals>(self)->clear();
    return 0;
}
PyType_Slot ic_image_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(ic_image_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(ic_image_clear)}, {0, nullptr},
};
int ic_intensity_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) {
        const auto& s = *nb::inst_ptr<IntensityConstructionLocals>(self);
        Py_VISIT(s.ColorBarOverlay.ptr());
    }
    return 0;
}
int ic_intensity_clear(PyObject* self) {
    if (nb::inst_ready(self)) nb::inst_ptr<IntensityConstructionLocals>(self)->clear();
    return 0;
}
PyType_Slot ic_intensity_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(ic_intensity_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(ic_intensity_clear)}, {0, nullptr},
};

struct ConstructionPair { nb::object first, second; };
ConstructionPair ic_unpack_pair(nb::object value) {
    nb::object iterator = ic_checked(PyObject_GetIter(value.ptr()));
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

ScalarFieldConstructionLocals scalar_field_init_locals(
    nb::object data, nb::object axis_labels, nb::object multiscale, nb::object name,
    nb::object ndim, nb::object scale, nb::object translate, nb::object units) {
    ScalarFieldConstructionLocals owner;
    owner.data = std::move(data); owner.axis_labels = std::move(axis_labels);
    owner.multiscale = std::move(multiscale); owner.name = std::move(name);
    owner.ndim = std::move(ndim); owner.scale = std::move(scale);
    owner.translate = std::move(translate); owner.units = std::move(units);
    return owner;
}
ImageConstructionLocals image_init_locals(nb::object rgb) {
    ImageConstructionLocals owner;
    owner.rgb = std::move(rgb);
    return owner;
}

void scalar_field_init(
    nb::object self, nb::object affine, nb::object blending, nb::object cache,
    nb::object custom_interpolation_kernel_2d, nb::object depiction,
    nb::object experimental_clipping_planes, nb::object metadata, nb::object opacity,
    nb::object plane, nb::object projection_mode, nb::object rendering, nb::object rotate,
    nb::object shear, nb::object visible, nb::dict globals,
    ScalarFieldConstructionLocals& owner, nb::object bind_super) {
    ConstructionNamespace ns(std::move(globals));
    if (owner.name.is_none() && !owner.data.is_none())
        ic_replace(owner.name, ic_call1(ns.get("magic_name"), owner.data));

    {
        nb::object callback = ns.get("isinstance");
        nb::object type = ic_attr(ns.get("types"), "GeneratorType");
        if (ic_truth(ic_call2(std::move(callback), owner.data, std::move(type))))
            ic_replace(owner.data, ic_call1(ns.get("list"), owner.data));
    }
    {
        nb::object callback = ns.get("getattr");
        nb::object dimensions = ic_call3(std::move(callback), owner.data, nb::str("ndim"), nb::int_(2));
        if (ic_truth(ic_compare(std::move(dimensions), nb::int_(2), Py_LT)))
            ic_raise(ic_call1(ns.get("ValueError"), nb::str("Image data must have at least 2 dimensions.")));
    }
    ic_store(self, "_data_raw", owner.data);
    if (owner.multiscale.is_none()) {
        ConstructionPair values = ic_unpack_pair(ic_call1(ns.get("guess_multiscale"), owner.data));
        ic_replace(owner.multiscale, std::move(values.first));
        ic_replace(owner.data, std::move(values.second));
    } else if (ic_truth(owner.multiscale)) {
        nb::object callback = ns.get("isinstance");
        nb::object type = ns.get("MultiScaleData");
        if (!ic_truth(ic_call2(std::move(callback), owner.data, std::move(type))))
            ic_replace(owner.data, ic_call1(ns.get("MultiScaleData"), owner.data));
    }
    if (owner.ndim.is_none()) {
        nb::object callback = ns.get("len");
        nb::object shape = ic_attr(owner.data, "shape");
        ic_replace(owner.ndim, ic_call1(std::move(callback), std::move(shape)));
    }
    ic_store(self, "_data", owner.data);
    {
        nb::object callback = ns.get("isinstance");
        nb::object list_type = ns.get("list");
        nb::object tuple_type = ns.get("tuple");
        nb::object multi_type = ns.get("MultiScaleData");
        nb::tuple types = nb::make_tuple(list_type, tuple_type, multi_type);
        list_type.reset(); tuple_type.reset(); multi_type.reset();
        const bool sequence = ic_truth(ic_call2(std::move(callback), owner.data, std::move(types)));
        ic_replace(owner.xr_source, sequence ? ic_item(owner.data, nb::int_(0)) : owner.data);
    }
    {
        nb::object callback = ns.get("len");
        nb::object shape = ic_attr(owner.xr_source, "shape");
        nb::object dimensions = ic_call1(std::move(callback), std::move(shape));
        ic_replace(owner.rgb, ic_compare(std::move(dimensions), owner.ndim, Py_NE));
    }
    {
        nb::object callback = ns.get("_get_xr_metadata");
        nb::dict keywords;
        keywords["rgb"] = owner.rgb; keywords["axis_labels"] = owner.axis_labels;
        keywords["scale"] = owner.scale; keywords["translate"] = owner.translate;
        keywords["units"] = owner.units;
        nb::object value = ic_call_kw(std::move(callback), nb::make_tuple(owner.xr_source), std::move(keywords));
        ic_replace(owner.xr_metadata, std::move(value));
    }
    ic_replace(owner.axis_labels, ic_attr(owner.xr_metadata, "axis_labels"));
    ic_replace(owner.scale, ic_attr(owner.xr_metadata, "scale"));
    ic_replace(owner.translate, ic_attr(owner.xr_metadata, "translate"));
    ic_replace(owner.units, ic_attr(owner.xr_metadata, "units"));

    // The provider is pure lexical-super binding. Bind before loading arguments.
    {
        nb::object callback = ic_call1(std::move(bind_super), self);
        nb::tuple arguments = nb::make_tuple(owner.data, owner.ndim);
        nb::dict keywords;
        keywords["affine"] = affine; keywords["axis_labels"] = owner.axis_labels;
        keywords["blending"] = blending; keywords["cache"] = cache;
        keywords["experimental_clipping_planes"] = experimental_clipping_planes;
        keywords["metadata"] = metadata; keywords["multiscale"] = owner.multiscale;
        keywords["name"] = owner.name; keywords["opacity"] = opacity;
        keywords["projection_mode"] = projection_mode; keywords["scale"] = owner.scale;
        keywords["shear"] = shear; keywords["rotate"] = rotate;
        keywords["translate"] = owner.translate; keywords["units"] = owner.units;
        keywords["visible"] = visible;
        ic_call_kw(std::move(callback), std::move(arguments), std::move(keywords));
    }
    {
        nb::object callback = ic_attr(ic_attr(self, "events"), "add");
        nb::dict keywords;
        keywords["attenuation"] = ns.get("Event");
        keywords["custom_interpolation_kernel_2d"] = ns.get("Event");
        keywords["depiction"] = ns.get("Event");
        keywords["locked_data_level"] = ns.get("Event");
        nb::object warning = ns.get("WarningEmitter");
        nb::dict warning_keywords;
        warning_keywords["type_name"] = nb::str("select");
        nb::object emitter = ic_call_kw(std::move(warning), nb::make_tuple(nb::str(
            "'layer.events.interpolation' is deprecated please use `interpolation2d` and `interpolation3d`")),
            std::move(warning_keywords));
        keywords["interpolation"] = std::move(emitter);
        keywords["interpolation2d"] = ns.get("Event");
        keywords["interpolation3d"] = ns.get("Event");
        keywords["iso_threshold"] = ns.get("Event");
        keywords["plane"] = ns.get("Event"); keywords["rendering"] = ns.get("Event");
        ic_call_kw(std::move(callback), nb::tuple(), std::move(keywords));
    }
    ic_store(self, "_array_like", nb::bool_(true));
    ic_store(self, "_locked_data_level", nb::none());
    ic_store(self, "_data", owner.data);
    {
        nb::object callback = ns.get("isinstance");
        nb::object type = ns.get("MultiScaleData");
        const bool multiscale = ic_truth(ic_call2(std::move(callback), owner.data, std::move(type)));
        nb::object level = multiscale
            ? ic_subtract(ic_call1(ns.get("len"), owner.data), nb::int_(1)) : nb::int_(0);
        ic_store(self, "_data_level", std::move(level));
    }
    ic_replace(owner.displayed_axes, ic_attr(ic_attr(self, "_slice_input"), "displayed"));
    {
        nb::object callback = ns.numpy("array");
        nb::object shapes = ic_attr(self, "level_shapes");
        nb::object array = ic_call1(std::move(callback), std::move(shapes));
        nb::object level = ic_attr(self, "_data_level");
        nb::object row = ic_item(std::move(array), std::move(level));
        nb::object selected = ic_item(std::move(row), owner.displayed_axes);
        nb::object value = ic_subtract(std::move(selected), nb::int_(1));
        nb::object corner = ic_item(ic_attr(self, "corner_pixels"), nb::int_(1));
        ic_set(std::move(corner), owner.displayed_axes, std::move(value));
    }
    ic_call0(ic_attr(self, "_reset_thumbnail_level_data"));
    {
        nb::object callback = ns.get("SlicingPlane");
        nb::dict keywords;
        keywords["thickness"] = nb::int_(1);
        nb::object plane_value = ic_call_kw(std::move(callback), nb::tuple(), std::move(keywords));
        ic_store(self, "_plane", std::move(plane_value));
    }
    ic_store(self, "_should_calc_clims", nb::bool_(false));
    ic_store(self, "rendering", rendering);
    ic_store(self, "depiction", depiction);
    if (!plane.is_none()) ic_store(self, "plane", plane);
    {
        nb::object callback = ns.get("connect_no_arg");
        nb::object plane_events = ic_attr(ic_attr(self, "plane"), "events");
        nb::object events = ic_attr(self, "events");
        ic_call3(std::move(callback), std::move(plane_events), std::move(events), nb::str("plane"));
    }
    ic_store(self, "custom_interpolation_kernel_2d", custom_interpolation_kernel_2d);
}

void scalar_field_post_init(nb::object self) { ic_call0(ic_attr(std::move(self), "refresh")); }

void scalar_slicing_state_init(nb::object self, nb::object layer, nb::object data,
                               nb::object cache, nb::dict globals, nb::object bind_super) {
    ConstructionNamespace ns(std::move(globals));
    {
        nb::object callback = ic_call1(std::move(bind_super), self);
        ic_call3(std::move(callback), layer, data, cache);
    }
    {
        nb::object callback = ns.get("Affine");
        nb::object ones_callback = ns.numpy("ones");
        nb::object ndim = ic_attr(self, "ndim");
        nb::object ones = ic_call1(std::move(ones_callback), std::move(ndim));
        nb::object zeros_callback = ns.numpy("zeros");
        ndim = ic_attr(self, "ndim");
        nb::object zeros = ic_call1(std::move(zeros_callback), std::move(ndim));
        nb::tuple arguments = nb::make_tuple(ones, zeros);
        ones.reset(); zeros.reset();
        nb::dict keywords;
        keywords["name"] = nb::str("tile2data");
        nb::object transforms = ic_call_kw(std::move(callback), std::move(arguments), std::move(keywords));
        ic_store(self, "transforms", std::move(transforms));
    }
    {
        nb::object callback = ic_attr(ns.get("_ScalarFieldSliceResponse"), "make_empty");
        nb::dict keywords;
        keywords["slice_input"] = ic_attr(self, "_slice_input");
        nb::object length_callback = ns.get("len");
        nb::object shape = ic_attr(ic_attr(ic_attr(self, "layer"), "data"), "shape");
        nb::object dimensions = ic_call1(std::move(length_callback), std::move(shape));
        nb::object ndim = ic_attr(self, "ndim");
        keywords["rgb"] = ic_compare(std::move(dimensions), std::move(ndim), Py_NE);
        keywords["dtype"] = ic_call0(ic_attr(ic_attr(self, "layer"), "_slice_dtype"));
        nb::object slice = ic_call_kw(std::move(callback), nb::tuple(), std::move(keywords));
        ic_store(self, "_slice", std::move(slice));
    }
}

void image_init(
    nb::object self, nb::object data, nb::object affine, nb::object attenuation,
    nb::object auto_contrast, nb::object axis_labels, nb::object blending, nb::object cache,
    nb::object colormap, nb::object contrast_limits, nb::object custom_interpolation_kernel_2d,
    nb::object depiction, nb::object experimental_clipping_planes, nb::object gamma,
    nb::object interpolation2d, nb::object interpolation3d, nb::object iso_threshold,
    nb::object locked_data_level, nb::object metadata, nb::object multiscale,
    nb::object name, nb::object opacity, nb::object plane, nb::object projection_mode,
    nb::object rendering, nb::object rotate, nb::object scale, nb::object shear,
    nb::object translate, nb::object units, nb::object visible, nb::dict globals,
    ImageConstructionLocals& owner, nb::object bind_super) {
    ConstructionNamespace ns(std::move(globals));
    const bool has_shape = ic_truth(ic_call2(ns.get("hasattr"), data, nb::str("shape")));
    ic_replace(owner.data_shape, has_shape ? ic_attr(data, "shape") : ic_attr(ic_item(data, nb::int_(0)), "shape"));
    if (ic_truth(owner.rgb)) {
        nb::object callback = ns.get("guess_rgb");
        nb::dict keywords;
        keywords["min_side_len"] = nb::int_(0);
        if (!ic_truth(ic_call_kw(std::move(callback), nb::make_tuple(owner.data_shape), std::move(keywords))))
            ic_raise(ic_call1(ns.get("ValueError"), nb::str(
                "'rgb' was set to True but data does not have suitable dimensions.")));
    }
    if (owner.rgb.is_none()) ic_replace(owner.rgb, ic_call1(ns.get("guess_rgb"), owner.data_shape));
    ic_store(self, "rgb", owner.rgb);
    {
        nb::object callback = ic_call1(std::move(bind_super), self);
        nb::tuple arguments = nb::make_tuple(data);
        nb::dict keywords;
        keywords["affine"] = affine; keywords["axis_labels"] = axis_labels;
        keywords["blending"] = blending; keywords["cache"] = cache;
        keywords["custom_interpolation_kernel_2d"] = custom_interpolation_kernel_2d;
        keywords["depiction"] = depiction;
        keywords["experimental_clipping_planes"] = experimental_clipping_planes;
        keywords["metadata"] = metadata; keywords["multiscale"] = multiscale;
        keywords["name"] = name;
        const bool rgb = ic_truth(owner.rgb);
        nb::object ndim = ic_call1(ns.get("len"), owner.data_shape);
        if (rgb) ndim = ic_subtract(std::move(ndim), nb::int_(1));
        keywords["ndim"] = std::move(ndim);
        keywords["opacity"] = opacity; keywords["plane"] = plane;
        keywords["projection_mode"] = projection_mode; keywords["rendering"] = rendering;
        keywords["rotate"] = rotate; keywords["scale"] = scale; keywords["shear"] = shear;
        keywords["translate"] = translate; keywords["units"] = units; keywords["visible"] = visible;
        ic_call_kw(std::move(callback), std::move(arguments), std::move(keywords));
    }
    ic_store(self, "rgb", owner.rgb);
    nb::object cmap = ic_call1(ns.get("ensure_colormap"), colormap);
    ic_store(self, "_colormap", std::move(cmap));
    ic_store(self, "_gamma", gamma);
    nb::object interpolation = ic_attr(ns.get("Interpolation"), "NEAREST");
    ic_store(self, "_interpolation2d", std::move(interpolation));
    interpolation = ic_attr(ns.get("Interpolation"), "NEAREST");
    ic_store(self, "_interpolation3d", std::move(interpolation));
    ic_store(self, "interpolation2d", interpolation2d);
    ic_store(self, "interpolation3d", interpolation3d);
    ic_store(self, "_attenuation", attenuation);
    if (contrast_limits.is_none()) {
        nb::object callback = ns.get("isinstance");
        nb::object type = ns.numpy("ndarray");
        if (!ic_truth(ic_call2(std::move(callback), data, std::move(type)))) {
            nb::object normalize = ns.get("normalize_dtype");
            nb::object getter = ns.get("getattr");
            nb::object fallback = ns.numpy("float32");
            nb::object dtype_value = ic_call3(std::move(getter), data, nb::str("dtype"), std::move(fallback));
            ic_replace(owner.dtype, ic_call1(std::move(normalize), std::move(dtype_value)));
            nb::object subtype = ns.numpy("issubdtype");
            nb::object integer = ns.numpy("integer");
            nb::object limits = ic_truth(ic_call2(std::move(subtype), owner.dtype, std::move(integer)))
                ? ic_call1(ns.get("get_dtype_limits"), owner.dtype) : nb::make_tuple(nb::int_(0), nb::int_(1));
            ic_store(self, "contrast_limits_range", std::move(limits));
            nb::object uint8 = ns.numpy("uint8");
            nb::object calculate = ic_compare(owner.dtype, std::move(uint8), Py_NE);
            ic_store(self, "_should_calc_clims", std::move(calculate));
        } else {
            nb::object limits = ic_call0(ic_attr(self, "_calc_data_range"));
            ic_store(self, "contrast_limits_range", std::move(limits));
        }
    } else ic_store(self, "contrast_limits_range", contrast_limits);
    nb::object limits = ic_attr(self, "contrast_limits_range");
    ic_store(self, "_contrast_limits", std::move(limits));
    limits = ic_attr(self, "_contrast_limits");
    ic_store(self, "contrast_limits", std::move(limits));
    ic_store(self, "auto_contrast", auto_contrast);
    if (iso_threshold.is_none()) {
        ConstructionPair values = ic_unpack_pair(ic_attr(self, "contrast_limits_range"));
        ic_replace(owner.cmin, std::move(values.first));
        ic_replace(owner.cmax, std::move(values.second));
        nb::object first = owner.cmin;
        nb::object difference = ic_subtract(owner.cmax, owner.cmin);
        nb::object half = ic_divide(std::move(difference), nb::int_(2));
        nb::object value = ic_add(std::move(first), std::move(half));
        ic_store(self, "_iso_threshold", std::move(value));
    } else ic_store(self, "_iso_threshold", iso_threshold);
    if (!locked_data_level.is_none()) ic_store(self, "locked_data_level", locked_data_level);
}

nb::object image_get_layer_slicing_state(nb::object self, nb::object data,
                                        nb::object cache, nb::dict globals) {
    ConstructionNamespace ns(std::move(globals));
    return ic_call3(ns.get("_ImageSlicingState"), std::move(self), std::move(data), std::move(cache));
}

// IMPORT_FROM keeps its module operand alive through the local assignment.
// A missing attribute may name a partially initialized sys.modules submodule.
nb::object ic_import_from(nb::handle module, const char* name) {
    PyObject* result = PyObject_GetAttrString(module.ptr(), name);
    if (result) return nb::steal<nb::object>(result);
    if (!PyErr_ExceptionMatches(PyExc_AttributeError)) throw nb::python_error();
    PyErr_Clear();
    nb::object module_name = nb::steal<nb::object>(PyObject_GetAttrString(module.ptr(), "__name__"));
    if (!module_name.is_valid() || !PyUnicode_Check(module_name.ptr())) {
        PyErr_Clear();
        PyErr_Format(PyExc_ImportError, "cannot import name '%s' from '<unknown module name>' (unknown location)", name);
        throw nb::python_error();
    }
    nb::object full_name = ic_checked(PyUnicode_FromFormat("%U.%s", module_name.ptr(), name));
    result = PyImport_GetModule(full_name.ptr());
    if (result) return nb::steal<nb::object>(result);
    if (PyErr_Occurred()) throw nb::python_error();
    nb::object filename = nb::steal<nb::object>(PyObject_GetAttrString(module.ptr(), "__file__"));
    if (!filename.is_valid() || !PyUnicode_Check(filename.ptr())) {
        PyErr_Clear();
        filename = nb::none();
    }
    nb::object message;
    if (filename.is_none())
        message = ic_checked(PyUnicode_FromFormat("cannot import name '%s' from '%U' (unknown location)", name, module_name.ptr()));
    else
        message = ic_checked(PyUnicode_FromFormat("cannot import name '%s' from '%U' (%U)", name, module_name.ptr(), filename.ptr()));
    PyErr_SetImportError(message.ptr(), module_name.ptr(), filename.ptr());
    throw nb::python_error();
}

void intensity_init(nb::object self, nb::object args, nb::object kwargs,
                     nb::object empty_contrast, nb::dict globals,
                     IntensityConstructionLocals& owner, nb::object bind_super) {
    ConstructionNamespace ns(std::move(globals));
    {
        nb::object callback = ic_call1(std::move(bind_super), self);
        nb::dict expanded = nb::steal<nb::dict>(ic_checked(PyDict_Copy(kwargs.ptr())).release().ptr());
        ic_call_kw(std::move(callback), nb::borrow<nb::tuple>(args), std::move(expanded));
    }
    {
        nb::object callback = ic_attr(ic_attr(self, "events"), "add");
        nb::dict keywords;
        keywords["contrast_limits"] = ns.get("Event");
        keywords["contrast_limits_range"] = ns.get("Event");
        keywords["gamma"] = ns.get("Event"); keywords["colormap"] = ns.get("Event");
        keywords["auto_contrast"] = ns.get("Event");
        ic_call_kw(std::move(callback), nb::tuple(), std::move(keywords));
    }
    ic_store(self, "_gamma", nb::float_(1.0));
    ic_store(self, "_colormap_name", nb::str(""));
    ic_store(self, "_contrast_limits_msg", nb::str(""));
    ic_store(self, "_contrast_limits", empty_contrast);
    ic_store(self, "_contrast_limits_range", empty_contrast);
    ic_store(self, "_auto_contrast_source", nb::str("slice"));
    ic_store(self, "_auto_contrast", nb::bool_(false));
    {
        // IMPORT_NAME uses the executing function's builtins, not LOAD_GLOBAL.
        nb::object importer = nb::steal<nb::object>(PyObject_GetItem(ns.builtins.ptr(), nb::str("__import__").ptr()));
        if (!importer.is_valid()) {
            if (PyErr_ExceptionMatches(PyExc_KeyError)) {
                PyErr_Clear();
                PyErr_SetString(PyExc_ImportError, "__import__ not found");
            }
            throw nb::python_error();
        }
        PyObject* raw_locals = PyEval_GetLocals();
        if (!raw_locals && PyErr_Occurred()) throw nb::python_error();
        nb::object locals = raw_locals ? nb::borrow<nb::object>(raw_locals) : nb::none();
        nb::tuple arguments = nb::make_tuple(nb::str("napari.components.overlays"), ns.globals,
            locals, nb::make_tuple(nb::str("ColorBarOverlay")), nb::int_(0));
        locals.reset();
        nb::object imported = ic_checked(PyObject_CallObject(importer.ptr(), arguments.ptr()));
        importer.reset(); arguments.reset();
        ic_replace(owner.ColorBarOverlay, ic_import_from(imported, "ColorBarOverlay"));
        imported.reset();
    }
    {
        nb::object callback = ic_attr(ic_attr(self, "_overlays"), "update");
        nb::object overlay = ic_call0(owner.ColorBarOverlay);
        nb::dict additions;
        additions["colorbar"] = std::move(overlay);
        ic_call1(std::move(callback), std::move(additions));
    }
}

} // namespace

void bind_image_construction(nb::module_& module) {
    nb::class_<ScalarFieldConstructionLocals>(module, "ScalarFieldConstructionLocals",
        nb::is_weak_referenceable(), nb::type_slots(ic_scalar_slots))
        .def_prop_ro("data", [](const ScalarFieldConstructionLocals& s) { return ic_optional(s.data); })
        .def_prop_ro("axis_labels", [](const ScalarFieldConstructionLocals& s) { return ic_optional(s.axis_labels); })
        .def_prop_ro("multiscale", [](const ScalarFieldConstructionLocals& s) { return ic_optional(s.multiscale); })
        .def_prop_ro("name", [](const ScalarFieldConstructionLocals& s) { return ic_optional(s.name); })
        .def_prop_ro("ndim", [](const ScalarFieldConstructionLocals& s) { return ic_optional(s.ndim); })
        .def_prop_ro("scale", [](const ScalarFieldConstructionLocals& s) { return ic_optional(s.scale); })
        .def_prop_ro("translate", [](const ScalarFieldConstructionLocals& s) { return ic_optional(s.translate); })
        .def_prop_ro("units", [](const ScalarFieldConstructionLocals& s) { return ic_optional(s.units); })
        .def_prop_ro("xr_source", [](const ScalarFieldConstructionLocals& s) { return ic_optional(s.xr_source); })
        .def_prop_ro("rgb", [](const ScalarFieldConstructionLocals& s) { return ic_optional(s.rgb); })
        .def_prop_ro("xr_metadata", [](const ScalarFieldConstructionLocals& s) { return ic_optional(s.xr_metadata); })
        .def_prop_ro("displayed_axes", [](const ScalarFieldConstructionLocals& s) { return ic_optional(s.displayed_axes); });
    nb::class_<ImageConstructionLocals>(module, "ImageConstructionLocals",
        nb::is_weak_referenceable(), nb::type_slots(ic_image_slots))
        .def_prop_ro("rgb", [](const ImageConstructionLocals& s) { return ic_optional(s.rgb); })
        .def_prop_ro("data_shape", [](const ImageConstructionLocals& s) { return ic_optional(s.data_shape); })
        .def_prop_ro("dtype", [](const ImageConstructionLocals& s) { return ic_optional(s.dtype); })
        .def_prop_ro("cmin", [](const ImageConstructionLocals& s) { return ic_optional(s.cmin); })
        .def_prop_ro("cmax", [](const ImageConstructionLocals& s) { return ic_optional(s.cmax); });
    nb::class_<IntensityConstructionLocals>(module, "IntensityConstructionLocals",
        nb::is_weak_referenceable(), nb::type_slots(ic_intensity_slots))
        .def_prop_ro("ColorBarOverlay", [](const IntensityConstructionLocals& s) { return ic_optional(s.ColorBarOverlay); });
    module.def("scalar_field_init_locals", &scalar_field_init_locals,
        nb::arg("data").none(), nb::arg("axis_labels").none(), nb::arg("multiscale").none(),
        nb::arg("name").none(), nb::arg("ndim").none(), nb::arg("scale").none(),
        nb::arg("translate").none(), nb::arg("units").none());
    module.def("image_init_locals", &image_init_locals, nb::arg("rgb").none());
    module.def("intensity_init_locals", [] { return IntensityConstructionLocals(); });
    module.def("scalar_field_init", &scalar_field_init,
        nb::arg("self").none(), nb::arg("affine").none(), nb::arg("blending").none(),
        nb::arg("cache").none(), nb::arg("custom_interpolation_kernel_2d").none(),
        nb::arg("depiction").none(), nb::arg("experimental_clipping_planes").none(),
        nb::arg("metadata").none(), nb::arg("opacity").none(), nb::arg("plane").none(),
        nb::arg("projection_mode").none(), nb::arg("rendering").none(), nb::arg("rotate").none(),
        nb::arg("shear").none(), nb::arg("visible").none(), nb::arg("namespace"),
        nb::arg("locals_owner"), nb::arg("bind_super").none());
    module.def("scalar_field_post_init", &scalar_field_post_init, nb::arg("self").none());
    module.def("scalar_slicing_state_init", &scalar_slicing_state_init,
        nb::arg("self").none(), nb::arg("layer").none(), nb::arg("data").none(),
        nb::arg("cache").none(), nb::arg("namespace"), nb::arg("bind_super").none());
    module.def("image_init", &image_init,
        nb::arg("self").none(), nb::arg("data").none(), nb::arg("affine").none(),
        nb::arg("attenuation").none(), nb::arg("auto_contrast").none(), nb::arg("axis_labels").none(),
        nb::arg("blending").none(), nb::arg("cache").none(), nb::arg("colormap").none(),
        nb::arg("contrast_limits").none(), nb::arg("custom_interpolation_kernel_2d").none(),
        nb::arg("depiction").none(), nb::arg("experimental_clipping_planes").none(),
        nb::arg("gamma").none(), nb::arg("interpolation2d").none(), nb::arg("interpolation3d").none(),
        nb::arg("iso_threshold").none(), nb::arg("locked_data_level").none(), nb::arg("metadata").none(),
        nb::arg("multiscale").none(), nb::arg("name").none(), nb::arg("opacity").none(),
        nb::arg("plane").none(), nb::arg("projection_mode").none(), nb::arg("rendering").none(),
        nb::arg("rotate").none(), nb::arg("scale").none(), nb::arg("shear").none(),
        nb::arg("translate").none(), nb::arg("units").none(), nb::arg("visible").none(),
        nb::arg("namespace"), nb::arg("locals_owner"), nb::arg("bind_super").none());
    module.def("image_get_layer_slicing_state", &image_get_layer_slicing_state,
        nb::arg("self").none(), nb::arg("data").none(), nb::arg("cache").none(), nb::arg("namespace"));
    module.def("intensity_init", &intensity_init,
        nb::arg("self").none(), nb::arg("args").none(), nb::arg("kwargs").none(),
        nb::arg("empty_contrast").none(), nb::arg("namespace"), nb::arg("locals_owner"),
        nb::arg("bind_super").none());
}
} // namespace nx
