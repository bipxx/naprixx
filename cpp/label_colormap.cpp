#include "arrays.hpp"
#include <utility>

namespace nx {
namespace {

nb::object texture_checked(PyObject* value) {
    if (!value) throw nb::python_error();
    return nb::steal<nb::object>(value);
}

nb::object texture_attr(nb::object value, const char* name) {
    return texture_checked(PyObject_GetAttrString(value.ptr(), name));
}

nb::object texture_item(nb::object value, nb::object key) {
    return texture_checked(PyObject_GetItem(value.ptr(), key.ptr()));
}

nb::object texture_call1(nb::object callable, nb::object value) {
    return texture_checked(PyObject_CallOneArg(callable.ptr(), value.ptr()));
}

nb::object texture_call2(nb::object callable, nb::object first, nb::object second) {
    return texture_checked(PyObject_CallFunctionObjArgs(
        callable.ptr(), first.ptr(), second.ptr(), nullptr));
}

nb::object texture_call3(nb::object callable, nb::object first,
                         nb::object second, nb::object third) {
    return texture_checked(PyObject_CallFunctionObjArgs(
        callable.ptr(), first.ptr(), second.ptr(), third.ptr(), nullptr));
}

nb::object texture_call4(nb::object callable, nb::object first,
                         nb::object second, nb::object third, nb::object fourth) {
    return texture_checked(PyObject_CallFunctionObjArgs(
        callable.ptr(), first.ptr(), second.ptr(), third.ptr(), fourth.ptr(), nullptr));
}

bool texture_truth(nb::object value) {
    const int result = PyObject_IsTrue(value.ptr());
    if (result < 0) throw nb::python_error();
    return result != 0;
}

nb::object texture_compare(nb::object first, nb::object second, int comparison) {
    return texture_checked(PyObject_RichCompare(first.ptr(), second.ptr(), comparison));
}

nb::object texture_add(nb::object first, nb::object second) {
    return texture_checked(PyNumber_Add(first.ptr(), second.ptr()));
}

nb::object texture_subtract(nb::object first, nb::object second) {
    return texture_checked(PyNumber_Subtract(first.ptr(), second.ptr()));
}

struct TextureNamespace {
    nb::dict globals;
    nb::object builtins;

    explicit TextureNamespace(nb::dict globals_)
        : globals(std::move(globals_)),
          builtins(nb::borrow<nb::object>(PyEval_GetBuiltins())) {}

    nb::object get(const char* name) const {
        nb::object key = nb::str(name);
        PyObject* found = PyDict_GetItemWithError(globals.ptr(), key.ptr());
        if (found) return nb::borrow<nb::object>(found);
        if (PyErr_Occurred()) throw nb::python_error();
        found = PyObject_GetItem(builtins.ptr(), key.ptr());
        if (found) return nb::steal<nb::object>(found);
        if (!PyErr_ExceptionMatches(PyExc_KeyError)) throw nb::python_error();
        PyErr_Clear();
        nb::object text = texture_checked(PyUnicode_FromFormat("name '%s' is not defined", name));
        nb::object error = texture_checked(PyObject_CallOneArg(PyExc_NameError, text.ptr()));
        if (PyObject_SetAttrString(error.ptr(), "name", key.ptr()) < 0) throw nb::python_error();
        PyErr_SetObject(PyExc_NameError, error.ptr());
        throw nb::python_error();
    }

    nb::object numpy(const char* name) const {
        return texture_attr(get("np"), name);
    }

    nb::object accelerated(const char* name) const {
        return texture_attr(get("_accel_cmap"), name);
    }
};

bool texture_integer(nb::handle data, const TextureNamespace& namespace_) {
    // isinstance is looked up each time, as in the application expression.
    nb::object callable = namespace_.get("isinstance");
    nb::object integer = namespace_.numpy("integer");
    return texture_truth(texture_call2(std::move(callable),
        nb::borrow<nb::object>(data), std::move(integer)));
}

nb::object label_cyclic_data_to_texture(nb::object colormap, nb::object values,
                                       nb::dict globals) {
    TextureNamespace namespace_(std::move(globals));
    nb::object callable = namespace_.get("_cast_labels_data_to_texture_dtype_auto");
    return texture_call2(std::move(callable), std::move(values), std::move(colormap));
}

nb::object label_direct_data_to_texture(nb::object colormap, nb::object values,
                                       nb::dict globals) {
    TextureNamespace namespace_(std::move(globals));
    nb::object callable = namespace_.get("_cast_labels_data_to_texture_dtype_direct");
    return texture_call2(std::move(callable), std::move(values), std::move(colormap));
}

nb::object label_convert_small_ints_unsigned(nb::object data, nb::dict globals) {
    TextureNamespace namespace_(std::move(globals));
    nb::object itemsize = texture_attr(texture_attr(data, "dtype"), "itemsize");
    if (texture_truth(texture_compare(std::move(itemsize), nb::int_(1), Py_EQ))) {
        nb::object callable = texture_attr(data, "view");
        nb::object dtype = namespace_.numpy("uint8");
        return texture_call1(std::move(callable), std::move(dtype));
    }
    itemsize = texture_attr(texture_attr(data, "dtype"), "itemsize");
    if (texture_truth(texture_compare(std::move(itemsize), nb::int_(2), Py_EQ))) {
        nb::object callable = texture_attr(data, "view");
        nb::object dtype = namespace_.numpy("uint16");
        return texture_call1(std::move(callable), std::move(dtype));
    }
    return data;
}

// Every original assignment returns to the facade before the next provider
// call. Its named locals therefore retain their original traceback ownership;
// native booleans alone select the facade's assignment/return branches.
nb::object label_texture_shape(nb::object data, nb::dict globals) {
    TextureNamespace namespace_(std::move(globals));
    nb::object callable = namespace_.numpy("shape");
    return texture_call1(std::move(callable), std::move(data));
}

nb::object label_texture_array(nb::object data, nb::dict globals) {
    TextureNamespace namespace_(std::move(globals));
    nb::object callable = namespace_.numpy("atleast_1d");
    return texture_call1(std::move(callable), std::move(data));
}

bool label_texture_small(nb::object data) {
    nb::object itemsize = texture_attr(std::move(data), "itemsize");
    return texture_truth(texture_compare(std::move(itemsize), nb::int_(2), Py_LE));
}

bool label_texture_is_integer(nb::object data, nb::dict globals) {
    return texture_integer(data, TextureNamespace(std::move(globals)));
}

nb::object label_auto_texture_small(nb::object data, nb::dict globals) {
    TextureNamespace namespace_(std::move(globals));
    nb::object callable = namespace_.get("_convert_small_ints_to_unsigned");
    return texture_call1(std::move(callable), std::move(data));
}

nb::object label_auto_texture_num_colors(nb::object colormap, nb::dict globals) {
    TextureNamespace namespace_(std::move(globals));
    nb::object callable = namespace_.get("len");
    nb::object colors = texture_attr(std::move(colormap), "colors");
    nb::object length = texture_call1(std::move(callable), std::move(colors));
    return texture_subtract(std::move(length), nb::int_(1));
}

nb::object label_auto_texture_modulo(nb::dict globals) {
    return TextureNamespace(std::move(globals)).accelerated("zero_preserving_modulo");
}

nb::object label_auto_texture_choose_modulo(nb::object data, nb::object modulo,
                                           nb::dict globals) {
    TextureNamespace namespace_(std::move(globals));
    if (texture_integer(data, namespace_))
        return namespace_.accelerated("zero_preserving_modulo_numpy");
    return modulo;
}

nb::object label_auto_texture_dtype(nb::object num_colors, nb::dict globals) {
    TextureNamespace namespace_(std::move(globals));
    nb::object callable = namespace_.accelerated("minimum_dtype_for_labels");
    nb::object count = texture_add(std::move(num_colors), nb::int_(1));
    return texture_call1(std::move(callable), std::move(count));
}

bool label_auto_texture_use_selection(nb::object colormap) {
    return texture_truth(texture_attr(std::move(colormap), "use_selection"));
}

nb::object label_auto_texture_selection(nb::object colormap, nb::object num_colors,
                                        nb::object dtype, nb::dict globals) {
    TextureNamespace namespace_(std::move(globals));
    nb::object callable = namespace_.accelerated("zero_preserving_modulo_numpy");
    nb::object array_callable = namespace_.numpy("array");
    nb::list selections;
    selections.append(texture_attr(std::move(colormap), "selection"));
    nb::object array = texture_call1(std::move(array_callable), std::move(selections));
    return texture_call3(std::move(callable), std::move(array),
        std::move(num_colors), std::move(dtype));
}

nb::object label_auto_texture_selected(nb::object data_arr, nb::object colormap,
                                      nb::object selection_in_texture,
                                      nb::object dtype, nb::dict globals) {
    TextureNamespace namespace_(std::move(globals));
    nb::object callable = namespace_.numpy("where");
    nb::object selection = texture_attr(std::move(colormap), "selection");
    nb::object condition = texture_compare(std::move(data_arr), std::move(selection), Py_EQ);
    nb::object zero_callable = texture_attr(std::move(dtype), "type");
    nb::object zero = texture_call1(std::move(zero_callable), nb::int_(0));
    return texture_call3(std::move(callable), std::move(condition),
        std::move(selection_in_texture), std::move(zero));
}

nb::object label_auto_texture_unselected(nb::object modulo, nb::object data_arr,
                                        nb::object num_colors, nb::object dtype,
                                        nb::object colormap) {
    nb::object background = texture_attr(std::move(colormap), "background_value");
    return texture_call4(std::move(modulo), std::move(data_arr),
        std::move(num_colors), std::move(dtype), std::move(background));
}

nb::object label_auto_texture_result(nb::object data, nb::object dtype,
                                     nb::object converted, nb::object original_shape,
                                     nb::dict globals) {
    TextureNamespace namespace_(std::move(globals));
    if (texture_integer(data, namespace_)) {
        nb::object callable = texture_attr(std::move(dtype), "type");
        nb::object first = texture_item(std::move(converted), nb::int_(0));
        return texture_call1(std::move(callable), std::move(first));
    }
    nb::object callable = namespace_.numpy("reshape");
    return texture_call2(std::move(callable), std::move(converted), std::move(original_shape));
}

nb::object label_cast_texture_direct_prepare(nb::object data, nb::dict globals) {
    TextureNamespace namespace_(std::move(globals));
    nb::object callable = namespace_.get("_convert_small_ints_to_unsigned");
    return texture_call1(std::move(callable), std::move(data));
}

nb::object label_direct_texture_mapper(nb::object direct_colormap) {
    return texture_item(texture_attr(std::move(direct_colormap),
        "_label_mapping_and_color_dict"), nb::int_(0));
}

nb::object label_direct_texture_dtype(nb::object direct_colormap, nb::dict globals) {
    TextureNamespace namespace_(std::move(globals));
    nb::object callable = namespace_.accelerated("minimum_dtype_for_labels");
    nb::object count = texture_add(texture_attr(std::move(direct_colormap),
        "_num_unique_colors"), nb::int_(2));
    return texture_call1(std::move(callable), std::move(count));
}

nb::object label_direct_texture_scalar(nb::object data, nb::object mapper,
                                      nb::object target_dtype, nb::dict globals) {
    TextureNamespace namespace_(std::move(globals));
    nb::object callable = texture_attr(std::move(target_dtype), "type");
    nb::object get = texture_attr(std::move(mapper), "get");
    nb::object integer = namespace_.get("int");
    nb::object key = texture_call1(std::move(integer), std::move(data));
    nb::object unknown = namespace_.accelerated("MAPPING_OF_UNKNOWN_VALUE");
    nb::object mapped = texture_call2(std::move(get), std::move(key), std::move(unknown));
    return texture_call1(std::move(callable), std::move(mapped));
}

nb::object label_direct_texture_array(nb::object array_data, nb::object direct_colormap,
                                     nb::object original_shape, nb::dict globals) {
    TextureNamespace namespace_(std::move(globals));
    nb::object reshape = namespace_.numpy("reshape");
    nb::object mapper = namespace_.accelerated("labels_raw_to_texture_direct");
    nb::object mapped = texture_call2(std::move(mapper), std::move(array_data),
        std::move(direct_colormap));
    return texture_call2(std::move(reshape), std::move(mapped), std::move(original_shape));
}

nb::object label_texture_dtype(nb::object num_colors, nb::object dtype,
                               nb::dict globals) {
    TextureNamespace namespace_(std::move(globals));
    nb::object itemsize = texture_attr(dtype, "itemsize");
    if (texture_truth(texture_compare(std::move(itemsize), nb::int_(1), Py_EQ))) {
        nb::object callable = namespace_.numpy("dtype");
        nb::object unsigned_type = namespace_.numpy("uint8");
        return texture_call1(std::move(callable), std::move(unsigned_type));
    }
    itemsize = texture_attr(dtype, "itemsize");
    if (texture_truth(texture_compare(std::move(itemsize), nb::int_(2), Py_EQ))) {
        nb::object callable = namespace_.numpy("dtype");
        nb::object unsigned_type = namespace_.numpy("uint16");
        return texture_call1(std::move(callable), std::move(unsigned_type));
    }
    nb::object callable = namespace_.accelerated("minimum_dtype_for_labels");
    return texture_call1(std::move(callable), std::move(num_colors));
}
} // namespace

void bind_label_colormap(nb::module_& m) {
    m.def("label_cyclic_data_to_texture", &label_cyclic_data_to_texture,
          nb::arg("colormap").none(), nb::arg("values").none(), nb::arg("namespace"));
    m.def("label_direct_data_to_texture", &label_direct_data_to_texture,
          nb::arg("colormap").none(), nb::arg("values").none(), nb::arg("namespace"));
    m.def("label_convert_small_ints_unsigned", &label_convert_small_ints_unsigned,
          nb::arg("data").none(), nb::arg("namespace"));
    m.def("label_cast_texture_direct_prepare", &label_cast_texture_direct_prepare,
          nb::arg("data").none(), nb::arg("namespace"));
    m.def("label_texture_dtype", &label_texture_dtype,
          nb::arg("num_colors").none(), nb::arg("dtype").none(), nb::arg("namespace"));
    m.def("label_texture_shape", &label_texture_shape,
          nb::arg("data").none(), nb::arg("namespace"));
    m.def("label_texture_array", &label_texture_array,
          nb::arg("data").none(), nb::arg("namespace"));
    m.def("label_texture_small", &label_texture_small, nb::arg("data").none());
    m.def("label_texture_is_integer", &label_texture_is_integer,
          nb::arg("data").none(), nb::arg("namespace"));
    m.def("label_auto_texture_small", &label_auto_texture_small,
          nb::arg("data").none(), nb::arg("namespace"));
    m.def("label_auto_texture_num_colors", &label_auto_texture_num_colors,
          nb::arg("colormap").none(), nb::arg("namespace"));
    m.def("label_auto_texture_modulo", &label_auto_texture_modulo, nb::arg("namespace"));
    m.def("label_auto_texture_choose_modulo", &label_auto_texture_choose_modulo,
          nb::arg("data").none(), nb::arg("modulo").none(), nb::arg("namespace"));
    m.def("label_auto_texture_dtype", &label_auto_texture_dtype,
          nb::arg("num_colors").none(), nb::arg("namespace"));
    m.def("label_auto_texture_use_selection", &label_auto_texture_use_selection,
          nb::arg("colormap").none());
    m.def("label_auto_texture_selection", &label_auto_texture_selection,
          nb::arg("colormap").none(), nb::arg("num_colors").none(),
          nb::arg("dtype").none(), nb::arg("namespace"));
    m.def("label_auto_texture_selected", &label_auto_texture_selected,
          nb::arg("data_arr").none(), nb::arg("colormap").none(),
          nb::arg("selection_in_texture").none(), nb::arg("dtype").none(), nb::arg("namespace"));
    m.def("label_auto_texture_unselected", &label_auto_texture_unselected,
          nb::arg("modulo").none(), nb::arg("data_arr").none(), nb::arg("num_colors").none(),
          nb::arg("dtype").none(), nb::arg("colormap").none());
    m.def("label_auto_texture_result", &label_auto_texture_result,
          nb::arg("data").none(), nb::arg("dtype").none(), nb::arg("converted").none(),
          nb::arg("original_shape").none(), nb::arg("namespace"));
    m.def("label_direct_texture_mapper", &label_direct_texture_mapper,
          nb::arg("direct_colormap").none());
    m.def("label_direct_texture_dtype", &label_direct_texture_dtype,
          nb::arg("direct_colormap").none(), nb::arg("namespace"));
    m.def("label_direct_texture_scalar", &label_direct_texture_scalar,
          nb::arg("data").none(), nb::arg("mapper").none(),
          nb::arg("target_dtype").none(), nb::arg("namespace"));
    m.def("label_direct_texture_array", &label_direct_texture_array,
          nb::arg("array_data").none(), nb::arg("direct_colormap").none(),
          nb::arg("original_shape").none(), nb::arg("namespace"));
}
} // namespace nx
