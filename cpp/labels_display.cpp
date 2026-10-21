#include "arrays.hpp"
#include <utility>

namespace nx {
namespace {

nb::object display_checked(PyObject* value) {
    if (!value) throw nb::python_error();
    return nb::steal<nb::object>(value);
}

nb::object display_lookup(nb::handle mapping, nb::handle key) {
    if (PyDict_CheckExact(mapping.ptr())) {
        PyObject* value = PyDict_GetItemWithError(mapping.ptr(), key.ptr());
        if (value) return nb::borrow<nb::object>(value);
        if (PyErr_Occurred()) throw nb::python_error();
        return {};
    }
    PyObject* value = PyObject_GetItem(mapping.ptr(), key.ptr());
    if (value) return nb::steal<nb::object>(value);
    if (!PyErr_ExceptionMatches(PyExc_KeyError)) throw nb::python_error();
    PyErr_Clear();
    return {};
}

nb::object display_global(nb::dict namespace_, const char* name) {
    nb::object key = nb::str(name);
    nb::object value = display_lookup(namespace_, key);
    if (value.ptr()) return value;
    nb::object builtins = nb::borrow<nb::object>(PyEval_GetBuiltins());
    value = display_lookup(builtins, key);
    if (value.ptr()) return value;
    nb::object message = display_checked(PyUnicode_FromFormat("name '%s' is not defined", name));
    nb::object error = display_checked(PyObject_CallOneArg(PyExc_NameError, message.ptr()));
    if (PyObject_SetAttrString(error.ptr(), "name", key.ptr()) < 0) throw nb::python_error();
    PyErr_SetObject(PyExc_NameError, error.ptr());
    throw nb::python_error();
}

// Expression helpers consume temporary operands before the next source
// expression. The Python facades separately retain the original fast locals.
nb::object display_attr(nb::object value, const char* name) {
    return display_checked(PyObject_GetAttrString(value.ptr(), name));
}

nb::object display_item(nb::object value, nb::object key) {
    return display_checked(PyObject_GetItem(value.ptr(), key.ptr()));
}

nb::object display_call0(nb::object callable) {
    return display_checked(PyObject_CallNoArgs(callable.ptr()));
}

nb::object display_call1(nb::object callable, nb::object argument) {
    return display_checked(PyObject_CallOneArg(callable.ptr(), argument.ptr()));
}

nb::object display_call2(nb::object callable, nb::object first, nb::object second) {
    return display_checked(PyObject_CallFunctionObjArgs(callable.ptr(), first.ptr(), second.ptr(), nullptr));
}

nb::object display_call3(nb::object callable, nb::object first, nb::object second, nb::object third) {
    return display_checked(PyObject_CallFunctionObjArgs(callable.ptr(), first.ptr(), second.ptr(), third.ptr(), nullptr));
}

nb::object display_call_kwargs(nb::object callable, nb::object arguments, nb::dict keywords) {
    return display_checked(PyObject_Call(callable.ptr(), arguments.ptr(), keywords.ptr()));
}

using DisplayBinary = PyObject* (*)(PyObject*, PyObject*);

nb::object display_binary(DisplayBinary operation, nb::object first, nb::object second) {
    return display_checked(operation(first.ptr(), second.ptr()));
}

nb::object display_compare(nb::object first, nb::object second, int operation) {
    return display_checked(PyObject_RichCompare(first.ptr(), second.ptr(), operation));
}

bool display_truth(nb::handle value) {
    int result = PyObject_IsTrue(value.ptr());
    if (result < 0) throw nb::python_error();
    return result != 0;
}

bool display_test(nb::object first, nb::object second, int operation) {
    return display_truth(display_compare(std::move(first), std::move(second), operation));
}

nb::object display_iter(nb::object value) {
    return display_checked(PyObject_GetIter(value.ptr()));
}

nb::object display_next(nb::handle iterator) {
    PyObject* value = PyIter_Next(iterator.ptr());
    if (!value && PyErr_Occurred()) throw nb::python_error();
    return nb::steal<nb::object>(value);
}

std::pair<nb::object, nb::object> display_unpack_pair(nb::object pair) {
    PyObject* raw_iterator = PyObject_GetIter(pair.ptr());
    if (!raw_iterator) {
        if (PyErr_ExceptionMatches(PyExc_TypeError) && !Py_TYPE(pair.ptr())->tp_iter &&
            !PySequence_Check(pair.ptr())) {
            PyErr_Format(PyExc_TypeError, "cannot unpack non-iterable %.200s object", Py_TYPE(pair.ptr())->tp_name);
        }
        throw nb::python_error();
    }
    nb::object iterator = nb::steal<nb::object>(raw_iterator);
    nb::object first = display_next(iterator);
    if (!first.ptr()) {
        PyErr_SetString(PyExc_ValueError, "not enough values to unpack (expected 2, got 0)");
        throw nb::python_error();
    }
    nb::object second = display_next(iterator);
    if (!second.ptr()) {
        PyErr_SetString(PyExc_ValueError, "not enough values to unpack (expected 2, got 1)");
        throw nb::python_error();
    }
    nb::object extra = display_next(iterator);
    if (extra.ptr()) {
        PyErr_SetString(PyExc_ValueError, "too many values to unpack (expected 2)");
        throw nb::python_error();
    }
    return {std::move(first), std::move(second)};
}

nb::object display_zip(nb::object first, nb::object second, nb::dict namespace_) {
    nb::object callable = display_global(namespace_, "zip");
    nb::object arguments = nb::make_tuple(first, second);
    nb::dict keywords;
    keywords["strict"] = nb::bool_(false);
    return display_call_kwargs(std::move(callable), std::move(arguments), std::move(keywords));
}

nb::object labels_display_slice(nb::object raw, nb::object data_slice, nb::dict namespace_) {
    if (!data_slice.is_none()) return data_slice;
    nb::object tuple_factory = display_global(namespace_, "tuple");
    nb::object iterator = display_iter(display_attr(raw, "shape"));
    nb::list slices;
    nb::object size;
    while (nb::object next = display_next(iterator)) {
        size = std::move(next);
        slices.append(display_call2(display_global(namespace_, "slice"), nb::int_(0), size));
    }
    return display_call1(std::move(tuple_factory), std::move(slices));
}

nb::object labels_display_contour(nb::object layer, nb::object labels, nb::object data_slice) {
    return display_call2(display_attr(layer, "_calculate_contour"), labels, data_slice);
}

nb::object labels_display_select(nb::object labels, nb::object data_slice, nb::object sliced_labels) {
    if (sliced_labels.is_none()) return display_item(labels, data_slice);
    return sliced_labels;
}

nb::object labels_display_texture(nb::object layer, nb::object sliced_labels) {
    nb::object colormap = display_attr(layer, "colormap");
    return display_call1(display_attr(std::move(colormap), "_data_to_texture"), sliced_labels);
}

nb::object display_zero_array(nb::object zeros, nb::object normalized) {
    nb::object arguments = nb::make_tuple(0);
    nb::dict keywords;
    keywords["dtype"] = normalized;
    return display_call_kwargs(std::move(zeros), std::move(arguments), std::move(keywords));
}

nb::object labels_slice_dtype(nb::object layer, nb::dict namespace_) {
    nb::object mapper = display_attr(display_attr(layer, "colormap"), "_data_to_texture");
    nb::object zeros = display_attr(display_global(namespace_, "np"), "zeros");
    nb::object normalize = display_global(namespace_, "normalize_dtype");
    nb::object dtype = display_attr(layer, "dtype");
    nb::object normalized = display_call1(std::move(normalize), std::move(dtype));
    nb::object zero = display_zero_array(std::move(zeros), std::move(normalized));
    nb::object mapped = display_call1(std::move(mapper), std::move(zero));
    return display_attr(std::move(mapped), "dtype");
}

nb::tuple labels_contour_offset(nb::object layer, nb::object labels, nb::dict namespace_) {
    if (display_test(display_attr(layer, "contour"), nb::int_(1), Py_LT))
        return nb::make_tuple(false, nb::none());
    if (display_test(display_attr(labels, "ndim"), nb::int_(2), Py_GT)) {
        nb::object warnings = display_global(namespace_, "warnings");
        display_call1(display_attr(std::move(warnings), "warn"),
                      nb::str("Contours are not displayed during 3D rendering"));
        return nb::make_tuple(false, nb::none());
    }
    nb::object maximum = display_global(namespace_, "max");
    nb::object integer = display_global(namespace_, "int");
    nb::object thickness = display_attr(layer, "contour");
    nb::object converted = display_call1(std::move(integer), std::move(thickness));
    nb::object offset = display_call2(std::move(maximum), nb::int_(1), std::move(converted));
    return nb::make_tuple(true, offset);
}

nb::object labels_contour_expand(nb::object labels, nb::object data_slice,
                                 nb::object offset, nb::dict namespace_) {
    nb::object expand = display_global(namespace_, "expand_slice");
    nb::object shape = display_attr(labels, "shape");
    return display_call3(std::move(expand), data_slice, std::move(shape), offset);
}

nb::object labels_contour_compute(nb::object layer, nb::object labels,
                                  nb::object expanded_slice, nb::dict namespace_) {
    nb::object get_contours = display_global(namespace_, "get_contours");
    nb::object selected = display_item(labels, expanded_slice);
    nb::object thickness = display_attr(layer, "contour");
    nb::object colormap = display_attr(layer, "colormap");
    nb::object background = display_attr(std::move(colormap), "background_value");
    return display_call3(std::move(get_contours), std::move(selected),
                         std::move(thickness), std::move(background));
}

nb::object labels_contour_delta(nb::object data_slice, nb::object expanded_slice, nb::dict namespace_) {
    nb::object tuple_factory = display_global(namespace_, "tuple");
    nb::object iterator = display_iter(display_zip(data_slice, expanded_slice, namespace_));
    nb::list slices;
    nb::object first, second;
    while (nb::object pair = display_next(iterator)) {
        auto values = display_unpack_pair(std::move(pair));
        first = std::move(values.first);
        second = std::move(values.second);
        nb::object factory = display_global(namespace_, "slice");
        nb::object first_start = display_attr(first, "start");
        nb::object second_start = display_attr(second, "start");
        nb::object start = display_binary(PyNumber_Subtract, std::move(first_start), std::move(second_start));
        nb::object first_stop = display_attr(first, "stop");
        second_start = display_attr(second, "start");
        nb::object stop = display_binary(PyNumber_Subtract, std::move(first_stop), std::move(second_start));
        slices.append(display_call2(std::move(factory), std::move(start), std::move(stop)));
    }
    return display_call1(std::move(tuple_factory), std::move(slices));
}

nb::object labels_contour_select(nb::object sliced_labels, nb::object delta_slice) {
    return display_item(sliced_labels, delta_slice);
}

nb::object labels_contour_structure(nb::object ndi, nb::object labels) {
    nb::object generate = display_attr(ndi, "generate_binary_structure");
    nb::object ndim = display_attr(labels, "ndim");
    return display_call2(std::move(generate), std::move(ndim), nb::int_(1));
}

nb::object labels_contour_thicken(nb::object ndi, nb::object struct_elem,
                                  nb::object thickness, nb::dict namespace_) {
    nb::object iterate = display_attr(ndi, "iterate_structure");
    nb::object iterated = display_call2(std::move(iterate), struct_elem, thickness);
    nb::object astype = display_attr(std::move(iterated), "astype");
    return display_call1(std::move(astype), display_global(namespace_, "bool"));
}

nb::object labels_contour_dilate(nb::object ndi, nb::object labels, nb::object struct_elem) {
    nb::object dilate = display_attr(ndi, "grey_dilation");
    nb::object arguments = nb::make_tuple(labels);
    nb::dict keywords;
    keywords["footprint"] = struct_elem;
    return display_call_kwargs(std::move(dilate), std::move(arguments), std::move(keywords));
}

nb::object labels_contour_erode(nb::object ndi, nb::object labels, nb::object thick_struct_elem) {
    nb::object erode = display_attr(ndi, "grey_erosion");
    nb::object arguments = nb::make_tuple(labels);
    nb::dict keywords;
    keywords["footprint"] = thick_struct_elem;
    return display_call_kwargs(std::move(erode), std::move(arguments), std::move(keywords));
}

nb::object labels_contour_mask(nb::object dilated_labels, nb::object eroded_labels) {
    return display_compare(dilated_labels, eroded_labels, Py_EQ);
}

nb::object labels_contour_copy(nb::object labels) {
    return display_call0(display_attr(labels, "copy"));
}

nb::object labels_contour_fill(nb::object contours, nb::object not_boundaries, nb::object background_label) {
    if (PyObject_SetItem(contours.ptr(), not_boundaries.ptr(), background_label.ptr()) < 0)
        throw nb::python_error();
    return contours;
}

nb::object display_expand_bound(nb::object slice_, nb::object max_size, nb::object offset,
                                nb::dict namespace_, bool stop) {
    nb::object maximum = display_global(namespace_, "max");
    nb::object minimum = display_global(namespace_, "min");
    nb::object bound = display_attr(slice_, stop ? "stop" : "start");
    nb::object shifted = display_binary(stop ? PyNumber_Add : PyNumber_Subtract,
                                        std::move(bound), offset);
    nb::object clamped = display_call2(std::move(minimum), max_size, std::move(shifted));
    return display_call2(std::move(maximum), nb::int_(0), std::move(clamped));
}

nb::object labels_expand_slice(nb::object axes_slice, nb::object shape,
                               nb::object offset, nb::dict namespace_) {
    nb::object tuple_factory = display_global(namespace_, "tuple");
    nb::object iterator = display_iter(display_zip(axes_slice, shape, namespace_));
    nb::list expanded;
    nb::object slice_, max_size;
    while (nb::object pair = display_next(iterator)) {
        auto values = display_unpack_pair(std::move(pair));
        slice_ = std::move(values.first);
        max_size = std::move(values.second);
        nb::object factory = display_global(namespace_, "slice");
        nb::object start = display_expand_bound(slice_, max_size, offset, namespace_, false);
        nb::object stop = display_expand_bound(slice_, max_size, offset, namespace_, true);
        nb::object step = display_attr(slice_, "step");
        expanded.append(display_call3(std::move(factory), std::move(start), std::move(stop), std::move(step)));
    }
    return display_call1(std::move(tuple_factory), std::move(expanded));
}

} // namespace

void bind_labels_display(nb::module_& m) {
    m.def("labels_display_slice", &labels_display_slice, nb::arg("raw").none(),
          nb::arg("data_slice").none(), nb::arg("namespace"));
    m.def("labels_display_contour", &labels_display_contour, nb::arg("layer").none(),
          nb::arg("labels").none(), nb::arg("data_slice").none());
    m.def("labels_display_select", &labels_display_select, nb::arg("labels").none(),
          nb::arg("data_slice").none(), nb::arg("sliced_labels").none());
    m.def("labels_display_texture", &labels_display_texture, nb::arg("layer").none(),
          nb::arg("sliced_labels").none());
    m.def("labels_slice_dtype", &labels_slice_dtype, nb::arg("layer").none(), nb::arg("namespace"));
    m.def("labels_contour_offset", &labels_contour_offset, nb::arg("layer").none(),
          nb::arg("labels").none(), nb::arg("namespace"));
    m.def("labels_contour_expand", &labels_contour_expand, nb::arg("labels").none(),
          nb::arg("data_slice").none(), nb::arg("offset").none(), nb::arg("namespace"));
    m.def("labels_contour_compute", &labels_contour_compute, nb::arg("layer").none(),
          nb::arg("labels").none(), nb::arg("expanded_slice").none(), nb::arg("namespace"));
    m.def("labels_contour_delta", &labels_contour_delta, nb::arg("data_slice").none(),
          nb::arg("expanded_slice").none(), nb::arg("namespace"));
    m.def("labels_contour_select", &labels_contour_select, nb::arg("sliced_labels").none(),
          nb::arg("delta_slice").none());
    m.def("labels_contour_structure", &labels_contour_structure, nb::arg("ndi").none(), nb::arg("labels").none());
    m.def("labels_contour_thicken", &labels_contour_thicken, nb::arg("ndi").none(),
          nb::arg("struct_elem").none(), nb::arg("thickness").none(), nb::arg("namespace"));
    m.def("labels_contour_dilate", &labels_contour_dilate, nb::arg("ndi").none(),
          nb::arg("labels").none(), nb::arg("struct_elem").none());
    m.def("labels_contour_erode", &labels_contour_erode, nb::arg("ndi").none(),
          nb::arg("labels").none(), nb::arg("thick_struct_elem").none());
    m.def("labels_contour_mask", &labels_contour_mask, nb::arg("dilated_labels").none(),
          nb::arg("eroded_labels").none());
    m.def("labels_contour_copy", &labels_contour_copy, nb::arg("labels").none());
    m.def("labels_contour_fill", &labels_contour_fill, nb::arg("contours").none(),
          nb::arg("not_boundaries").none(), nb::arg("background_label").none());
    m.def("labels_expand_slice", &labels_expand_slice, nb::arg("axes_slice").none(),
          nb::arg("shape").none(), nb::arg("offset").none(), nb::arg("namespace"));
}

} // namespace nx
