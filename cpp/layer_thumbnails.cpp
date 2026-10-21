#include "arrays.hpp"
#include "event_protocol.hpp"
#include <array>
#include <initializer_list>
#include <utility>

namespace nx {
namespace {

nb::object lt_checked(PyObject* value) {
    if (!value) throw nb::python_error();
    return nb::steal<nb::object>(value);
}
nb::object lt_attr(nb::object value, const char* name) {
    return lt_checked(PyObject_GetAttrString(value.ptr(), name));
}
nb::object lt_item(nb::object value, nb::object key) {
    return lt_checked(PyObject_GetItem(value.ptr(), key.ptr()));
}
void lt_store(nb::handle self, const char* name, nb::object value) {
    if (PyObject_SetAttrString(self.ptr(), name, value.ptr()) < 0) throw nb::python_error();
}
void lt_set_item(nb::handle value, nb::object key, nb::object item) {
    if (PyObject_SetItem(value.ptr(), key.ptr(), item.ptr()) < 0) throw nb::python_error();
}
nb::object lt_call0(nb::object callback) { return lt_checked(PyObject_CallNoArgs(callback.ptr())); }
nb::object lt_call1(nb::object callback, nb::object value) {
    return lt_checked(PyObject_CallOneArg(callback.ptr(), value.ptr()));
}
nb::object lt_call2(nb::object callback, nb::object first, nb::object second) {
    return lt_checked(PyObject_CallFunctionObjArgs(callback.ptr(), first.ptr(), second.ptr(), nullptr));
}
nb::object lt_call3(nb::object callback, nb::object first, nb::object second, nb::object third) {
    return lt_checked(PyObject_CallFunctionObjArgs(callback.ptr(), first.ptr(), second.ptr(), third.ptr(), nullptr));
}
nb::object lt_call_kw(nb::object callback, nb::tuple arguments, nb::dict keywords) {
    return lt_checked(PyObject_Call(callback.ptr(), arguments.ptr(), keywords.ptr()));
}
bool lt_truth(nb::object value) {
    const int result = PyObject_IsTrue(value.ptr());
    if (result < 0) throw nb::python_error();
    return result != 0;
}
bool lt_test(nb::object first, nb::object second, int operation) {
    return lt_truth(lt_checked(PyObject_RichCompare(first.ptr(), second.ptr(), operation)));
}
nb::object lt_compare(nb::object first, nb::object second, int operation) {
    return lt_checked(PyObject_RichCompare(first.ptr(), second.ptr(), operation));
}
bool lt_contains(nb::object container, nb::object value) {
    const int result = PySequence_Contains(container.ptr(), value.ptr());
    if (result < 0) throw nb::python_error();
    return result != 0;
}
nb::object lt_add(nb::object first, nb::object second) {
    return lt_checked(PyNumber_Add(first.ptr(), second.ptr()));
}
nb::object lt_subtract(nb::object first, nb::object second) {
    return lt_checked(PyNumber_Subtract(first.ptr(), second.ptr()));
}
nb::object lt_multiply(nb::object first, nb::object second) {
    return lt_checked(PyNumber_Multiply(first.ptr(), second.ptr()));
}
nb::object lt_divide(nb::object first, nb::object second) {
    return lt_checked(PyNumber_TrueDivide(first.ptr(), second.ptr()));
}
nb::object lt_power(nb::object first, nb::object second) {
    return lt_checked(PyNumber_Power(first.ptr(), second.ptr(), Py_None));
}
nb::object lt_format(nb::object value) {
    return lt_checked(PyObject_Format(value.ptr(), nullptr));
}
nb::object lt_iter(nb::object value) { return lt_checked(PyObject_GetIter(value.ptr())); }
nb::object lt_next(nb::handle iterator) {
    PyObject* next = PyIter_Next(iterator.ptr());
    if (!next && PyErr_Occurred()) throw nb::python_error();
    return nb::steal<nb::object>(next);
}
nb::object lt_optional(const nb::object& value) { return value.is_valid() ? value : nb::none(); }
[[noreturn]] void lt_raise(nb::object error) {
    if (PyExceptionClass_Check(error.ptr())) error = lt_call0(std::move(error));
    if (PyExceptionInstance_Check(error.ptr()))
        PyErr_SetObject(reinterpret_cast<PyObject*>(Py_TYPE(error.ptr())), error.ptr());
    else PyErr_SetString(PyExc_TypeError, "exceptions must derive from BaseException");
    throw nb::python_error();
}

struct ThumbnailNamespace {
    nb::dict globals;
    nb::object builtins;
    explicit ThumbnailNamespace(nb::dict namespace_)
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
        nb::object message = lt_checked(PyUnicode_FromFormat("name '%s' is not defined", name));
        nb::object error = lt_call1(nb::borrow<nb::object>(PyExc_NameError), std::move(message));
        if (PyObject_SetAttrString(error.ptr(), "name", key.ptr()) < 0) throw nb::python_error();
        lt_raise(std::move(error));
    }
    nb::object numpy(const char* name) const { return lt_attr(get("np"), name); }
};

struct ThumbnailPair { nb::object first, second; };
ThumbnailPair lt_unpack_pair(nb::object value) {
    nb::object iterator = lt_iter(value);
    nb::object first = lt_next(iterator);
    if (!first.is_valid()) throw nb::value_error("not enough values to unpack (expected 2, got 0)");
    nb::object second = lt_next(iterator);
    if (!second.is_valid()) throw nb::value_error("not enough values to unpack (expected 2, got 1)");
    nb::object extra = lt_next(iterator);
    if (extra.is_valid()) throw nb::value_error("too many values to unpack (expected 2)");
    iterator.reset();
    value.reset();
    return {std::move(first), std::move(second)};
}

// Per-call source local bindings only; application state stays in the actual
// Layer/Colormap objects, with no mirror. Canonical Layer allocation is a
// separate native-store decision; wider Colormap/EventedModel authority is open.
struct LayerThumbnailLocals {
    enum class Layout { Empty, Image, Labels, Convert, Colormap, Format, Contrast };
    Layout layout = Layout::Empty;
    nb::object image, dtype, raw_zoom_factor, new_shape, zoom_factor;
    nb::object downsampled, colormapped, alpha, low, high, color_range, color_array;
    nb::object imshape, thumbshape, out_dtype, out_max, in_kind, image_out;
    nb::object values, cols, indices, value, precision, format_mode, prev;
    LayerThumbnailLocals() = default;
    LayerThumbnailLocals(LayerThumbnailLocals&&) = default;
    ~LayerThumbnailLocals() { clear(); }
    void clear() {
        std::array<nb::object, 25> old = {
            std::move(image), std::move(dtype), std::move(raw_zoom_factor),
            std::move(new_shape), std::move(zoom_factor), std::move(downsampled),
            std::move(colormapped), std::move(alpha), std::move(low), std::move(high),
            std::move(color_range), std::move(color_array), std::move(imshape),
            std::move(thumbshape), std::move(out_dtype), std::move(out_max),
            std::move(in_kind), std::move(image_out), std::move(values),
            std::move(cols), std::move(indices), std::move(value),
            std::move(precision), std::move(format_mode), std::move(prev),
        };
        const Layout previous = layout;
        layout = Layout::Empty;
        const auto release = [&](std::initializer_list<size_t> order) {
            for (size_t index : order) old[index].reset();
        };
        switch (previous) {
            case Layout::Image: release({0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}); break;
            case Layout::Labels: release({0, 12, 13, 2, 3, 4, 5, 11}); break;
            case Layout::Convert: release({14, 15, 16, 17}); break;
            case Layout::Colormap: release({18, 19, 20}); break;
            case Layout::Format: release({21, 22, 23}); break;
            case Layout::Contrast: release({24}); break;
            case Layout::Empty: break;
        }
        for (nb::object& value : old) value.reset();
    }
};
int lt_locals_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) {
        const auto& state = *nb::inst_ptr<LayerThumbnailLocals>(self);
        Py_VISIT(state.image.ptr()); Py_VISIT(state.dtype.ptr());
        Py_VISIT(state.raw_zoom_factor.ptr()); Py_VISIT(state.new_shape.ptr());
        Py_VISIT(state.zoom_factor.ptr()); Py_VISIT(state.downsampled.ptr());
        Py_VISIT(state.colormapped.ptr()); Py_VISIT(state.alpha.ptr());
        Py_VISIT(state.low.ptr()); Py_VISIT(state.high.ptr());
        Py_VISIT(state.color_range.ptr()); Py_VISIT(state.color_array.ptr());
        Py_VISIT(state.imshape.ptr()); Py_VISIT(state.thumbshape.ptr());
        Py_VISIT(state.out_dtype.ptr()); Py_VISIT(state.out_max.ptr());
        Py_VISIT(state.in_kind.ptr()); Py_VISIT(state.image_out.ptr());
        Py_VISIT(state.values.ptr()); Py_VISIT(state.cols.ptr());
        Py_VISIT(state.indices.ptr()); Py_VISIT(state.value.ptr());
        Py_VISIT(state.precision.ptr()); Py_VISIT(state.format_mode.ptr());
        Py_VISIT(state.prev.ptr());
    }
    return 0;
}
int lt_locals_clear(PyObject* self) {
    if (nb::inst_ready(self)) nb::inst_ptr<LayerThumbnailLocals>(self)->clear();
    return 0;
}
PyType_Slot lt_locals_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(lt_locals_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(lt_locals_clear)}, {0, nullptr},
};

nb::object lt_first_two(nb::object value) {
    nb::object stop = nb::int_(2);
    nb::object slice = lt_checked(PySlice_New(Py_None, stop.ptr(), Py_None));
    return lt_item(std::move(value), std::move(slice));
}
nb::object lt_shape_two(nb::object value) { return lt_first_two(lt_attr(std::move(value), "shape")); }
nb::object lt_thumbnail_shape_two(nb::object self) { return lt_first_two(lt_attr(std::move(self), "_thumbnail_shape")); }
nb::object lt_alpha_key() { return nb::make_tuple(nb::ellipsis(), nb::int_(3)); }
bool lt_projection(nb::object self) {
    nb::object slice = lt_attr(self, "_slice_input");
    if (!lt_test(lt_attr(std::move(slice), "ndisplay"), nb::int_(3), Py_EQ)) return false;
    return lt_test(lt_attr(std::move(self), "ndim"), nb::int_(2), Py_GT);
}
void lt_project(LayerThumbnailLocals& owner, const ThumbnailNamespace& ns) {
    nb::object callback = ns.numpy("max");
    nb::dict keywords;
    keywords["axis"] = nb::int_(0);
    owner.image = lt_call_kw(std::move(callback), nb::make_tuple(owner.image), std::move(keywords));
}
nb::object lt_zoom(nb::object ndi, nb::object image, nb::object factors) {
    nb::object callback = lt_attr(std::move(ndi), "zoom");
    nb::dict keywords;
    keywords["prefilter"] = nb::bool_(false);
    keywords["order"] = nb::int_(0);
    return lt_call_kw(std::move(callback), nb::make_tuple(std::move(image), std::move(factors)), std::move(keywords));
}
void lt_alpha_multiply(nb::object array, nb::object self) {
    nb::object key = lt_alpha_key();
    nb::object alpha = lt_item(array, key);
    nb::object opacity = lt_attr(std::move(self), "opacity");
    nb::object updated = lt_checked(PyNumber_InPlaceMultiply(alpha.ptr(), opacity.ptr()));
    alpha.reset(); opacity.reset();
    lt_set_item(array, std::move(key), std::move(updated));
}
bool lt_uint8_layer(nb::object self, const ThumbnailNamespace& ns) {
    nb::object dtype = lt_attr(std::move(self), "dtype");
    nb::object uint8 = ns.numpy("uint8");
    return lt_test(std::move(dtype), std::move(uint8), Py_EQ);
}

void layer_thumbnail_image(nb::object self, nb::object ndi, nb::dict globals, LayerThumbnailLocals& owner) {
    owner.layout = LayerThumbnailLocals::Layout::Image;
    ThumbnailNamespace ns(std::move(globals));
    if (lt_truth(lt_attr(lt_attr(self, "_slice"), "empty"))) {
        nb::object callback = ns.numpy("zeros");
        nb::object shape = lt_attr(self, "_thumbnail_shape");
        nb::object dtype = lt_attr(self, "dtype");
        nb::object thumbnail = lt_call2(std::move(callback), std::move(shape), std::move(dtype));
        lt_store(std::move(self), "thumbnail", std::move(thumbnail));
        return;
    }
    owner.image = lt_attr(lt_attr(lt_attr(self, "_slice"), "thumbnail"), "raw");
    if (lt_projection(self)) lt_project(owner, ns);
    {
        nb::object callback = ns.numpy("dtype");
        nb::object dtype = lt_attr(owner.image, "dtype");
        owner.dtype = lt_call1(std::move(callback), std::move(dtype));
    }
    {
        nb::object callback = ns.numpy("dtype");
        nb::object float16 = ns.numpy("float16");
        nb::object half = lt_call1(std::move(callback), std::move(float16));
        nb::list types;
        types.append(half); half.reset();
        if (lt_contains(std::move(types), owner.dtype)) {
            callback = lt_attr(owner.image, "astype");
            nb::object float32 = ns.numpy("float32");
            owner.image = lt_call1(std::move(callback), std::move(float32));
        }
    }
    {
        nb::object callback = ns.numpy("divide");
        nb::object thumbshape = lt_thumbnail_shape_two(self);
        nb::object imshape = lt_shape_two(owner.image);
        nb::object result = lt_call2(std::move(callback), std::move(thumbshape), std::move(imshape));
        owner.raw_zoom_factor = lt_call0(lt_attr(std::move(result), "min"));
    }
    {
        nb::object clip = ns.numpy("clip");
        nb::object array = ns.numpy("array");
        nb::object imshape = lt_shape_two(owner.image);
        nb::object shape_array = lt_call1(std::move(array), std::move(imshape));
        nb::object scaled = lt_multiply(owner.raw_zoom_factor, std::move(shape_array));
        nb::object maximum = lt_thumbnail_shape_two(self);
        owner.new_shape = lt_call3(std::move(clip), std::move(scaled), nb::int_(1), std::move(maximum));
    }
    {
        nb::object callback = ns.get("tuple");
        nb::object imshape = lt_shape_two(owner.image);
        nb::object factors = lt_divide(owner.new_shape, std::move(imshape));
        owner.zoom_factor = lt_call1(std::move(callback), std::move(factors));
    }
    if (lt_truth(lt_attr(self, "rgb"))) {
        nb::object callback = lt_attr(ndi, "zoom");
        nb::object factors = lt_add(owner.zoom_factor, nb::make_tuple(nb::int_(1)));
        nb::dict keywords;
        keywords["prefilter"] = nb::bool_(false);
        keywords["order"] = nb::int_(0);
        owner.downsampled = lt_call_kw(std::move(callback), nb::make_tuple(owner.image, std::move(factors)), std::move(keywords));
        nb::object shape = lt_attr(owner.image, "shape");
        if (lt_test(lt_item(std::move(shape), nb::int_(2)), nb::int_(4), Py_EQ)) {
            owner.colormapped = lt_call1(ns.numpy("copy"), owner.downsampled);
            nb::object alpha = lt_item(owner.downsampled, lt_alpha_key());
            nb::object opacity = lt_attr(self, "opacity");
            nb::object value = lt_multiply(std::move(alpha), std::move(opacity));
            lt_set_item(owner.colormapped, lt_alpha_key(), std::move(value));
            if (lt_uint8_layer(self, ns)) {
                callback = lt_attr(owner.colormapped, "astype");
                nb::object uint8 = ns.numpy("uint8");
                owner.colormapped = lt_call1(std::move(callback), std::move(uint8));
            }
        } else {
            if (lt_uint8_layer(self, ns)) {
                callback = ns.numpy("full");
                nb::object shape = lt_add(lt_shape_two(owner.downsampled), nb::make_tuple(nb::int_(1)));
                nb::object integer = ns.get("int");
                nb::object opacity = lt_attr(self, "opacity");
                nb::object alpha = lt_multiply(nb::int_(255), std::move(opacity));
                nb::object fill = lt_call1(std::move(integer), std::move(alpha));
                nb::object uint8 = ns.numpy("uint8");
                nb::dict keywords;
                keywords["dtype"] = std::move(uint8);
                owner.alpha = lt_call_kw(std::move(callback), nb::make_tuple(std::move(shape), std::move(fill)), std::move(keywords));
                callback = lt_attr(owner.downsampled, "astype");
                uint8 = ns.numpy("uint8");
                owner.downsampled = lt_call1(std::move(callback), std::move(uint8));
            } else {
                callback = ns.numpy("full");
                nb::object shape = lt_add(lt_shape_two(owner.downsampled), nb::make_tuple(nb::int_(1)));
                nb::object opacity = lt_attr(self, "opacity");
                owner.alpha = lt_call2(std::move(callback), std::move(shape), std::move(opacity));
            }
            callback = ns.numpy("concatenate");
            nb::list parts;
            parts.append(owner.downsampled); parts.append(owner.alpha);
            nb::dict keywords;
            keywords["axis"] = nb::int_(2);
            owner.colormapped = lt_call_kw(std::move(callback), nb::make_tuple(std::move(parts)), std::move(keywords));
        }
    } else {
        owner.downsampled = lt_zoom(ndi, owner.image, owner.zoom_factor);
        ThumbnailPair limits = lt_unpack_pair(lt_attr(self, "contrast_limits"));
        owner.low = std::move(limits.first);
        owner.high = std::move(limits.second);
        nb::object callback = ns.numpy("issubdtype");
        nb::object dtype = lt_attr(owner.downsampled, "dtype");
        nb::object integer = ns.numpy("integer");
        if (lt_truth(lt_call2(std::move(callback), std::move(dtype), std::move(integer)))) {
            callback = ns.get("max");
            nb::object iinfo = ns.numpy("iinfo");
            dtype = lt_attr(owner.downsampled, "dtype");
            nb::object minimum = lt_attr(lt_call1(std::move(iinfo), std::move(dtype)), "min");
            owner.low = lt_call2(std::move(callback), owner.low, std::move(minimum));
            callback = ns.get("min");
            iinfo = ns.numpy("iinfo");
            dtype = lt_attr(owner.downsampled, "dtype");
            nb::object maximum = lt_attr(lt_call1(std::move(iinfo), std::move(dtype)), "max");
            owner.high = lt_call2(std::move(callback), owner.high, std::move(maximum));
        }
        owner.downsampled = lt_call3(ns.numpy("clip"), owner.downsampled, owner.low, owner.high);
        owner.color_range = lt_subtract(owner.high, owner.low);
        if (lt_test(owner.color_range, nb::int_(0), Py_NE)) {
            nb::object shifted = lt_subtract(owner.downsampled, owner.low);
            owner.downsampled = lt_divide(std::move(shifted), owner.color_range);
        }
        nb::object gamma = lt_attr(self, "gamma");
        owner.downsampled = lt_power(owner.downsampled, std::move(gamma));
        callback = lt_attr(lt_attr(self, "colormap"), "map");
        nb::object flat = lt_call0(lt_attr(owner.downsampled, "ravel"));
        owner.color_array = lt_call1(std::move(callback), std::move(flat));
        callback = lt_attr(owner.color_array, "reshape");
        nb::object shape = lt_attr(owner.downsampled, "shape");
        nb::object dimensions = lt_checked(PySequence_Tuple(shape.ptr()));
        shape.reset();
        nb::object rgba_shape = lt_checked(PySequence_Concat(dimensions.ptr(), nb::make_tuple(nb::int_(4)).ptr()));
        dimensions.reset();
        owner.colormapped = lt_call1(std::move(callback), std::move(rgba_shape));
        lt_alpha_multiply(owner.colormapped, self);
    }
    lt_store(std::move(self), "thumbnail", owner.colormapped);
}

void layer_thumbnail_labels(nb::object self, nb::object ndi, nb::dict globals, LayerThumbnailLocals& owner) {
    owner.layout = LayerThumbnailLocals::Layout::Labels;
    if (!lt_truth(lt_attr(lt_attr(self, "_slicing_state"), "loaded"))) return;
    if (lt_truth(lt_attr(lt_attr(self, "_slice"), "empty"))) return;
    ThumbnailNamespace ns(std::move(globals));
    owner.image = lt_attr(lt_attr(lt_attr(self, "_slice"), "thumbnail"), "raw");
    if (lt_projection(self)) lt_project(owner, ns);
    nb::object callback = ns.numpy("array");
    nb::object shape = lt_shape_two(owner.image);
    owner.imshape = lt_call1(std::move(callback), std::move(shape));
    callback = ns.numpy("array");
    shape = lt_thumbnail_shape_two(self);
    owner.thumbshape = lt_call1(std::move(callback), std::move(shape));
    callback = ns.numpy("min");
    nb::object factors = lt_divide(owner.thumbshape, owner.imshape);
    owner.raw_zoom_factor = lt_call1(std::move(callback), std::move(factors));
    callback = ns.numpy("clip");
    nb::object scaled = lt_multiply(owner.raw_zoom_factor, owner.imshape);
    nb::dict keywords;
    keywords["a_min"] = nb::int_(1);
    keywords["a_max"] = owner.thumbshape;
    owner.new_shape = lt_call_kw(std::move(callback), nb::make_tuple(std::move(scaled)), std::move(keywords));
    callback = ns.get("tuple");
    factors = lt_divide(owner.new_shape, owner.imshape);
    owner.zoom_factor = lt_call1(std::move(callback), std::move(factors));
    owner.downsampled = lt_zoom(ndi, owner.image, owner.zoom_factor);
    callback = lt_attr(lt_attr(self, "colormap"), "map");
    owner.color_array = lt_call1(std::move(callback), owner.downsampled);
    lt_alpha_multiply(owner.color_array, self);
    lt_store(std::move(self), "thumbnail", owner.color_array);
}

void layer_image_contrast_limits(nb::object self, nb::object contrast_limits, nb::dict globals, LayerThumbnailLocals& owner) {
    owner.layout = LayerThumbnailLocals::Layout::Contrast;
    ThumbnailNamespace ns(std::move(globals));
    nb::object mixin = ns.get("IntensityVisualizationMixin");
    nb::object descriptor = lt_attr(std::move(mixin), "contrast_limits");
    nb::object callback = lt_attr(std::move(descriptor), "fset");
    lt_call2(std::move(callback), self, std::move(contrast_limits));
    callback = ns.numpy("allclose");
    nb::object coerce = ns.get("_coerce_contrast_limits");
    nb::object current = lt_attr(self, "contrast_limits");
    nb::object coerced = lt_attr(lt_call1(std::move(coerce), std::move(current)), "contrast_limits");
    current = lt_attr(self, "contrast_limits");
    if (lt_truth(lt_call2(std::move(callback), std::move(coerced), std::move(current)))) return;
    owner.prev = lt_attr(self, "_auto_contrast");
    lt_store(self, "_auto_contrast", nb::bool_(false));
    try {
        callback = lt_attr(self, "refresh");
        nb::dict keywords;
        keywords["highlight"] = nb::bool_(false);
        keywords["extent"] = nb::bool_(false);
        lt_call_kw(std::move(callback), nb::tuple(), std::move(keywords));
    } catch (const nb::python_error& original) {
        events::HandledException active(original.value());
        try { lt_store(self, "_auto_contrast", owner.prev); }
        catch (const nb::python_error& cleanup) {
            if (cleanup.value().ptr() != original.value().ptr())
                PyException_SetContext(cleanup.value().ptr(), original.value().inc_ref().ptr());
            throw;
        }
        throw;
    }
    lt_store(std::move(self), "_auto_contrast", owner.prev);
}

nb::object lt_shift_amount(nb::object data, bool signed_input) {
    nb::object itemsize = lt_attr(lt_attr(std::move(data), "dtype"), "itemsize");
    nb::object bytes = lt_subtract(std::move(itemsize), nb::int_(1));
    nb::object bits = lt_multiply(std::move(bytes), nb::int_(8));
    return signed_input ? lt_subtract(std::move(bits), nb::int_(1)) : std::move(bits);
}
nb::object layer_convert_to_uint8(nb::object data, nb::dict globals, LayerThumbnailLocals& owner) {
    owner.layout = LayerThumbnailLocals::Layout::Convert;
    ThumbnailNamespace ns(std::move(globals));
    nb::object callback = ns.numpy("dtype");
    nb::object uint8 = ns.numpy("uint8");
    owner.out_dtype = lt_call1(std::move(callback), std::move(uint8));
    callback = ns.numpy("iinfo");
    owner.out_max = lt_attr(lt_call1(std::move(callback), owner.out_dtype), "max");
    if (lt_test(lt_attr(data, "dtype"), owner.out_dtype, Py_EQ)) return data;
    owner.in_kind = lt_attr(lt_attr(data, "dtype"), "kind");
    if (lt_test(owner.in_kind, nb::str("b"), Py_EQ)) {
        nb::object converted = lt_call1(lt_attr(data, "astype"), owner.out_dtype);
        return lt_multiply(std::move(converted), nb::int_(255));
    }
    if (lt_test(owner.in_kind, nb::str("f"), Py_EQ)) {
        owner.image_out = lt_call2(ns.numpy("multiply"), data, owner.out_max);
        callback = ns.numpy("rint");
        nb::dict keywords;
        keywords["out"] = owner.image_out;
        lt_call_kw(std::move(callback), nb::make_tuple(owner.image_out), std::move(keywords));
        callback = ns.numpy("clip");
        keywords = nb::dict();
        keywords["out"] = owner.image_out;
        lt_call_kw(std::move(callback), nb::make_tuple(owner.image_out, nb::int_(0), owner.out_max), std::move(keywords));
        callback = ns.numpy("nan_to_num");
        keywords = nb::dict();
        keywords["copy"] = nb::bool_(false);
        owner.image_out = lt_call_kw(std::move(callback), nb::make_tuple(owner.image_out), std::move(keywords));
        return lt_call1(lt_attr(owner.image_out, "astype"), owner.out_dtype);
    }
    if (lt_contains(nb::str("ui"), owner.in_kind)) {
        if (lt_test(owner.in_kind, nb::str("u"), Py_EQ)) {
            nb::object maximum = lt_call0(lt_attr(data, "max"));
            if (lt_test(std::move(maximum), owner.out_max, Py_LT))
                return lt_call1(lt_attr(data, "astype"), owner.out_dtype);
            callback = ns.numpy("right_shift");
            nb::object shift = lt_shift_amount(data, false);
            nb::object shifted = lt_call2(std::move(callback), data, std::move(shift));
            return lt_call1(lt_attr(std::move(shifted), "astype"), owner.out_dtype);
        }
        callback = ns.numpy("maximum");
        nb::dict keywords;
        keywords["out"] = data;
        lt_call_kw(std::move(callback), nb::make_tuple(data, nb::int_(0)), std::move(keywords));
        nb::object dtype = lt_attr(data, "dtype");
        nb::object int8 = ns.numpy("int8");
        if (lt_test(std::move(dtype), std::move(int8), Py_EQ)) {
            nb::object doubled = lt_multiply(data, nb::int_(2));
            callback = lt_attr(std::move(doubled), "astype");
            uint8 = ns.numpy("uint8");
            return lt_call1(std::move(callback), std::move(uint8));
        }
        nb::object maximum = lt_call0(lt_attr(data, "max"));
        if (lt_test(std::move(maximum), owner.out_max, Py_LT))
            return lt_call1(lt_attr(data, "astype"), owner.out_dtype);
        callback = ns.numpy("right_shift");
        nb::object shift = lt_shift_amount(data, true);
        nb::object shifted = lt_call2(std::move(callback), data, std::move(shift));
        return lt_call1(lt_attr(std::move(shifted), "astype"), owner.out_dtype);
    }
    lt_raise(ns.get("NotImplementedError"));
}

nb::object layer_colormap_map(nb::object self, nb::dict globals, LayerThumbnailLocals& owner) {
    owner.layout = LayerThumbnailLocals::Layout::Colormap;
    if (!owner.values.is_valid()) throw nb::value_error("locals_owner has no values binding");
    ThumbnailNamespace ns(std::move(globals));
    owner.values = lt_call1(ns.numpy("atleast_1d"), owner.values);
    nb::object interpolation = lt_attr(self, "interpolation");
    nb::object linear = lt_attr(ns.get("ColormapInterpolationMode"), "LINEAR");
    const bool use_linear = lt_test(std::move(interpolation), std::move(linear), Py_EQ);
    if (use_linear) {
        nb::object range = ns.get("range");
        nb::object iterator = lt_iter(lt_call1(std::move(range), nb::int_(4)));
        nb::list columns;
        nb::object i;
        for (;;) {
            nb::object next = lt_next(iterator);
            if (!next.is_valid()) break;
            i = std::move(next);
            nb::object callback = ns.numpy("interp");
            nb::object controls = lt_attr(self, "controls");
            nb::object colors = lt_attr(self, "colors");
            nb::object slice = lt_checked(PySlice_New(Py_None, Py_None, Py_None));
            nb::object channel = lt_item(std::move(colors), nb::make_tuple(std::move(slice), i));
            nb::object column = lt_call3(std::move(callback), owner.values, std::move(controls), std::move(channel));
            columns.append(column);
        }
        iterator.reset(); i.reset();
        owner.cols = std::move(columns);
        nb::object callback = ns.numpy("stack");
        nb::dict keywords;
        keywords["axis"] = nb::int_(-1);
        owner.cols = lt_call_kw(std::move(callback), nb::make_tuple(owner.cols), std::move(keywords));
    } else {
        interpolation = lt_attr(self, "interpolation");
        nb::object zero = lt_attr(ns.get("ColormapInterpolationMode"), "ZERO");
        const bool use_zero = lt_test(std::move(interpolation), std::move(zero), Py_EQ);
        if (!use_zero) {
            nb::object error_type = ns.get("ValueError");
            lt_raise(lt_call1(std::move(error_type), nb::str("Unrecognized Colormap Interpolation Mode")));
        }
        nb::object clip = ns.numpy("clip");
        nb::object search = ns.numpy("searchsorted");
        nb::object controls = lt_attr(self, "controls");
        nb::dict keywords;
        keywords["side"] = nb::str("right");
        nb::object found = lt_call_kw(std::move(search), nb::make_tuple(std::move(controls), owner.values), std::move(keywords));
        nb::object index = lt_subtract(std::move(found), nb::int_(1));
        nb::object length = ns.get("len");
        nb::object colors = lt_attr(self, "colors");
        nb::object count = lt_call1(std::move(length), std::move(colors));
        nb::object maximum = lt_subtract(std::move(count), nb::int_(1));
        owner.indices = lt_call3(std::move(clip), std::move(index), nb::int_(0), std::move(maximum));
        colors = lt_attr(self, "colors");
        nb::object cast = lt_attr(owner.indices, "astype");
        nb::object int32 = ns.numpy("int32");
        nb::object indices = lt_call1(std::move(cast), std::move(int32));
        owner.cols = lt_item(std::move(colors), std::move(indices));
    }
    owner.values = lt_item(owner.values, nb::make_tuple(nb::ellipsis(), nb::none()));
    {
        nb::object where = ns.numpy("where");
        nb::object mask = lt_call1(ns.numpy("isnan"), owner.values);
        nb::object nan_color = lt_attr(self, "nan_color");
        owner.cols = lt_call3(std::move(where), std::move(mask), std::move(nan_color), owner.cols);
    }
    if (!lt_attr(self, "high_color").is_none()) {
        nb::object where = ns.numpy("where");
        nb::object mask = lt_compare(owner.values, nb::int_(1), Py_GE);
        nb::object high_color = lt_attr(self, "high_color");
        owner.cols = lt_call3(std::move(where), std::move(mask), std::move(high_color), owner.cols);
    }
    if (!lt_attr(self, "low_color").is_none()) {
        nb::object where = ns.numpy("where");
        nb::object mask = lt_compare(owner.values, nb::int_(0), Py_LE);
        nb::object low_color = lt_attr(self, "low_color");
        owner.cols = lt_call3(std::move(where), std::move(mask), std::move(low_color), owner.cols);
    }
    return owner.cols;
}

nb::object layer_format_float(nb::dict globals, LayerThumbnailLocals& owner) {
    owner.layout = LayerThumbnailLocals::Layout::Format;
    if (!owner.value.is_valid() || !owner.precision.is_valid() || !owner.format_mode.is_valid())
        throw nb::value_error("locals_owner has no formatting parameter bindings");
    ThumbnailNamespace ns(std::move(globals));
    if (owner.precision.is_none()) owner.precision = ns.get("DEFAULT_PRECISION");
    nb::object precision = lt_format(owner.precision);
    nb::object mode = lt_format(owner.format_mode);
    nb::tuple parts = nb::make_tuple(nb::str("0."), std::move(precision), std::move(mode));
    nb::object specification = lt_checked(PyUnicode_Join(nb::str("").ptr(), parts.ptr()));
    parts.reset();
    return lt_checked(PyObject_Format(owner.value.ptr(), specification.ptr()));
}

} // namespace

void bind_layer_thumbnails(nb::module_& module) {
    nb::class_<LayerThumbnailLocals>(module, "LayerThumbnailLocals", nb::is_weak_referenceable(), nb::type_slots(lt_locals_slots))
        .def_prop_ro("image", [](const LayerThumbnailLocals& s) { return lt_optional(s.image); })
        .def_prop_ro("dtype", [](const LayerThumbnailLocals& s) { return lt_optional(s.dtype); })
        .def_prop_ro("raw_zoom_factor", [](const LayerThumbnailLocals& s) { return lt_optional(s.raw_zoom_factor); })
        .def_prop_ro("new_shape", [](const LayerThumbnailLocals& s) { return lt_optional(s.new_shape); })
        .def_prop_ro("zoom_factor", [](const LayerThumbnailLocals& s) { return lt_optional(s.zoom_factor); })
        .def_prop_ro("downsampled", [](const LayerThumbnailLocals& s) { return lt_optional(s.downsampled); })
        .def_prop_ro("colormapped", [](const LayerThumbnailLocals& s) { return lt_optional(s.colormapped); })
        .def_prop_ro("alpha", [](const LayerThumbnailLocals& s) { return lt_optional(s.alpha); })
        .def_prop_ro("low", [](const LayerThumbnailLocals& s) { return lt_optional(s.low); })
        .def_prop_ro("high", [](const LayerThumbnailLocals& s) { return lt_optional(s.high); })
        .def_prop_ro("color_range", [](const LayerThumbnailLocals& s) { return lt_optional(s.color_range); })
        .def_prop_ro("color_array", [](const LayerThumbnailLocals& s) { return lt_optional(s.color_array); })
        .def_prop_ro("imshape", [](const LayerThumbnailLocals& s) { return lt_optional(s.imshape); })
        .def_prop_ro("thumbshape", [](const LayerThumbnailLocals& s) { return lt_optional(s.thumbshape); })
        .def_prop_ro("out_dtype", [](const LayerThumbnailLocals& s) { return lt_optional(s.out_dtype); })
        .def_prop_ro("out_max", [](const LayerThumbnailLocals& s) { return lt_optional(s.out_max); })
        .def_prop_ro("in_kind", [](const LayerThumbnailLocals& s) { return lt_optional(s.in_kind); })
        .def_prop_ro("image_out", [](const LayerThumbnailLocals& s) { return lt_optional(s.image_out); })
        .def_prop_ro("values", [](const LayerThumbnailLocals& s) { return lt_optional(s.values); })
        .def_prop_ro("cols", [](const LayerThumbnailLocals& s) { return lt_optional(s.cols); })
        .def_prop_ro("indices", [](const LayerThumbnailLocals& s) { return lt_optional(s.indices); })
        .def_prop_ro("value", [](const LayerThumbnailLocals& s) { return lt_optional(s.value); })
        .def_prop_ro("precision", [](const LayerThumbnailLocals& s) { return lt_optional(s.precision); })
        .def_prop_ro("format_mode", [](const LayerThumbnailLocals& s) { return lt_optional(s.format_mode); })
        .def_prop_ro("prev", [](const LayerThumbnailLocals& s) { return lt_optional(s.prev); });
    module.def("layer_thumbnail_locals", [] { return LayerThumbnailLocals{}; });
    module.def("layer_colormap_locals", [](nb::object values) {
        LayerThumbnailLocals state;
        state.layout = LayerThumbnailLocals::Layout::Colormap;
        state.values = std::move(values);
        return state;
    }, nb::arg("values").none());
    module.def("layer_format_locals", [](nb::object value, nb::object precision, nb::object format_mode) {
        LayerThumbnailLocals state;
        state.layout = LayerThumbnailLocals::Layout::Format;
        state.value = std::move(value);
        state.precision = std::move(precision);
        state.format_mode = std::move(format_mode);
        return state;
    }, nb::arg("value").none(), nb::arg("precision").none(), nb::arg("format_mode").none());
    module.def("layer_thumbnail_image", &layer_thumbnail_image,
        nb::arg("self").none(), nb::arg("ndi").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("layer_thumbnail_labels", &layer_thumbnail_labels,
        nb::arg("self").none(), nb::arg("ndi").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("layer_image_contrast_limits", &layer_image_contrast_limits,
        nb::arg("self").none(), nb::arg("contrast_limits").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("layer_convert_to_uint8", &layer_convert_to_uint8,
        nb::arg("data").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("layer_colormap_map", &layer_colormap_map,
        nb::arg("self").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("layer_format_float", &layer_format_float, nb::arg("namespace"), nb::arg("locals_owner"));
}

} // namespace nx
