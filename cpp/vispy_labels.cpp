#include "arrays.hpp"
#include <array>
#include <initializer_list>
#include <utility>

namespace nx {
namespace {

nb::object vl_checked(PyObject* value) {
    if (!value) throw nb::python_error();
    return nb::steal<nb::object>(value);
}
nb::object vl_attr(nb::object value, const char* name) {
    return vl_checked(PyObject_GetAttrString(value.ptr(), name));
}
nb::object vl_item(nb::object value, nb::object key) {
    return vl_checked(PyObject_GetItem(value.ptr(), key.ptr()));
}
nb::object vl_call0(nb::object callable) {
    return vl_checked(PyObject_CallNoArgs(callable.ptr()));
}
nb::object vl_call1(nb::object callable, nb::object value) {
    return vl_checked(PyObject_CallOneArg(callable.ptr(), value.ptr()));
}
nb::object vl_call2(nb::object callable, nb::object first, nb::object second) {
    return vl_checked(PyObject_CallFunctionObjArgs(callable.ptr(), first.ptr(), second.ptr(), nullptr));
}
nb::object vl_call3(nb::object callable, nb::object first, nb::object second, nb::object third) {
    return vl_checked(PyObject_CallFunctionObjArgs(
        callable.ptr(), first.ptr(), second.ptr(), third.ptr(), nullptr));
}
nb::object vl_call_kw(nb::object callable, nb::tuple args, nb::dict keywords) {
    return vl_checked(PyObject_Call(callable.ptr(), args.ptr(), keywords.ptr()));
}
bool vl_truth(nb::object value) {
    const int result = PyObject_IsTrue(value.ptr());
    if (result < 0) throw nb::python_error();
    return result != 0;
}
nb::object vl_compare(nb::object first, nb::object second, int operation) {
    return vl_checked(PyObject_RichCompare(first.ptr(), second.ptr(), operation));
}
bool vl_test(nb::object first, nb::object second, int operation) {
    return vl_truth(vl_compare(std::move(first), std::move(second), operation));
}
nb::object vl_add(nb::object first, nb::object second) {
    return vl_checked(PyNumber_Add(first.ptr(), second.ptr()));
}
nb::object vl_power2(nb::object value) {
    nb::object exponent = nb::int_(2);
    return vl_checked(PyNumber_Power(value.ptr(), exponent.ptr(), Py_None));
}
nb::object vl_format(nb::object value) {
    nb::object format = nb::str("");
    return vl_checked(PyObject_Format(value.ptr(), format.ptr()));
}
nb::object vl_concat(nb::object first, nb::object second) {
    return vl_checked(PyUnicode_Concat(first.ptr(), second.ptr()));
}
nb::object vl_repr(nb::object value) { return vl_checked(PyObject_Repr(value.ptr())); }
nb::object vl_prefix_slice(nb::object value, nb::object end) {
    nb::object slice = vl_checked(PySlice_New(Py_None, end.ptr(), Py_None));
    return vl_item(std::move(value), std::move(slice));
}
void vl_store(nb::handle self, const char* name, nb::object value) {
    if (PyObject_SetAttrString(self.ptr(), name, value.ptr()) < 0) throw nb::python_error();
}
void vl_set(nb::handle target, nb::object key, nb::object value) {
    if (PyObject_SetItem(target.ptr(), key.ptr(), value.ptr()) < 0) throw nb::python_error();
}
[[noreturn]] void vl_raise(nb::object error) {
    if (PyExceptionInstance_Check(error.ptr()))
        PyErr_SetObject(reinterpret_cast<PyObject*>(Py_TYPE(error.ptr())), error.ptr());
    else if (PyExceptionClass_Check(error.ptr())) PyErr_SetNone(error.ptr());
    else PyErr_SetString(PyExc_TypeError, "exceptions must derive from BaseException");
    throw nb::python_error();
}

// Fresh expression lookup only: no native controller stores this namespace,
// a cache, a node snapshot or a substitute for the shared Python attributes.
struct LabelsNamespace {
    nb::dict globals;
    nb::object builtins;
    explicit LabelsNamespace(nb::dict namespace_)
        : globals(std::move(namespace_)), builtins(nb::borrow<nb::object>(PyEval_GetBuiltins())) {}
    nb::object get(const char* name) const {
        nb::object key = nb::str(name);
        PyObject* value = PyDict_GetItemWithError(globals.ptr(), key.ptr());
        if (value) return nb::borrow<nb::object>(value);
        if (PyErr_Occurred()) throw nb::python_error();
        value = PyObject_GetItem(builtins.ptr(), key.ptr());
        if (value) return nb::steal<nb::object>(value);
        if (!PyErr_ExceptionMatches(PyExc_KeyError)) throw nb::python_error();
        PyErr_Clear();
        nb::object message = vl_checked(PyUnicode_FromFormat("name '%s' is not defined", name));
        nb::object error = vl_call1(nb::borrow<nb::object>(PyExc_NameError), std::move(message));
        if (PyObject_SetAttrString(error.ptr(), "name", key.ptr()) < 0) throw nb::python_error();
        PyErr_SetObject(PyExc_NameError, error.ptr());
        throw nb::python_error();
    }
    nb::object numpy(const char* name) const { return vl_attr(get("np"), name); }
};

void vl_merge_keywords(nb::handle callable, nb::dict& keywords, nb::object mapping) {
    if (_PyDict_MergeEx(keywords.ptr(), mapping.ptr(), 2) >= 0) return;
    if (PyErr_ExceptionMatches(PyExc_AttributeError)) {
        PyErr_Clear();
        nb::object label = vl_checked(_PyObject_FunctionStr(callable.ptr()));
        PyErr_Format(PyExc_TypeError, "%U argument after ** must be a mapping, not %.200s",
                     label.ptr(), Py_TYPE(mapping.ptr())->tp_name);
    } else if (PyErr_ExceptionMatches(PyExc_KeyError)) {
        PyObject *type = nullptr, *value = nullptr, *traceback = nullptr;
        PyErr_Fetch(&type, &value, &traceback);
        PyErr_NormalizeException(&type, &value, &traceback);
        nb::object old_type = nb::steal<nb::object>(type);
        nb::object old_value = nb::steal<nb::object>(value);
        nb::object old_traceback = nb::steal<nb::object>(traceback);
        nb::object key = vl_item(vl_attr(old_value, "args"), nb::int_(0));
        nb::object label = vl_checked(_PyObject_FunctionStr(callable.ptr()));
        PyErr_Format(PyExc_TypeError, "%U got multiple values for keyword argument '%S'",
                     label.ptr(), key.ptr());
    }
    throw nb::python_error();
}

// Only the original two assigned loop locals survive a saved facade traceback.
struct VispyLabelsTextureLocals {
    nb::object key, value;
    void clear() {
        nb::object old_key = std::move(key), old_value = std::move(value);
        old_key.reset(); old_value.reset();
    }
};
int vl_locals_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(nb::handle(self))) {
        auto* state = nb::inst_ptr<VispyLabelsTextureLocals>(nb::handle(self));
        Py_VISIT(state->key.ptr()); Py_VISIT(state->value.ptr());
    }
    return 0;
}
int vl_locals_clear(PyObject* self) {
    if (nb::inst_ready(nb::handle(self)))
        nb::inst_ptr<VispyLabelsTextureLocals>(nb::handle(self))->clear();
    return 0;
}
PyType_Slot vl_locals_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(vl_locals_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(vl_locals_clear)}, {0, nullptr},
};
nb::object vl_optional(nb::object value) { return value.is_valid() ? value : nb::none(); }
struct LabelsPair { nb::object first, second; };
LabelsPair vl_unpack_pair(nb::object item) {
    nb::object iterator = vl_checked(PyObject_GetIter(item.ptr()));
    item.reset();
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

nb::object vispy_labels_cache_deepcopy(nb::object self, nb::dict globals) {
    LabelsNamespace namespace_(std::move(globals));
    return vl_call0(vl_call1(namespace_.get("type"), std::move(self)));
}
nb::object vl_minimum(nb::object self, nb::object dtype, nb::dict globals, const char* attribute) {
    LabelsNamespace namespace_(std::move(globals));
    nb::object integer = namespace_.get("int");
    nb::object texture = vl_attr(self, "_data_to_texture");
    nb::object cast = vl_attr(std::move(dtype), "type");
    nb::object value = vl_attr(std::move(self), attribute);
    nb::object converted = vl_call1(std::move(cast), std::move(value));
    nb::object result = vl_call1(std::move(texture), std::move(converted));
    return vl_call1(std::move(integer), std::move(result));
}
nb::object vispy_labels_selection_minimum(nb::object self, nb::object dtype, nb::dict globals) {
    return vl_minimum(std::move(self), std::move(dtype), std::move(globals), "selection");
}
nb::object vispy_labels_background_minimum(nb::object self, nb::object dtype, nb::dict globals) {
    return vl_minimum(std::move(self), std::move(dtype), std::move(globals), "background_value");
}
nb::object vispy_labels_direct_selection_minimum(nb::object self, nb::object dtype, nb::dict globals) {
    LabelsNamespace namespace_(std::move(globals));
    nb::object integer = namespace_.get("int");
    nb::object texture = namespace_.get("_cast_labels_data_to_texture_dtype_direct");
    nb::object cast = vl_attr(std::move(dtype), "type");
    nb::object value = vl_attr(self, "selection");
    nb::object converted = vl_call1(std::move(cast), std::move(value));
    nb::object result = vl_call2(std::move(texture), std::move(converted), std::move(self));
    return vl_call1(std::move(integer), std::move(result));
}

nb::object vispy_labels_direct_colors() {
    nb::list colors;
    colors.append(nb::str("w")); colors.append(nb::str("w"));
    return colors;
}
nb::dict vl_colormap_init_keywords() {
    nb::dict keywords;
    keywords["controls"] = nb::none();
    keywords["interpolation"] = nb::str("zero");
    return keywords;
}
nb::object vispy_labels_label_super_init(nb::object callback) {
    nb::dict keywords;
    keywords["colors"] = vispy_labels_direct_colors();
    keywords["controls"] = nb::none();
    keywords["interpolation"] = nb::str("zero");
    return vl_call_kw(std::move(callback), nb::tuple(), std::move(keywords));
}
nb::object vispy_labels_direct_super_init(nb::object callback, nb::object colors) {
    return vl_call_kw(std::move(callback), nb::make_tuple(std::move(colors)), vl_colormap_init_keywords());
}
nb::object vispy_labels_label_shader(nb::object view_dtype, nb::dict globals) {
    LabelsNamespace namespace_(std::move(globals));
    if (vl_test(vl_attr(view_dtype, "itemsize"), nb::int_(1), Py_EQ))
        return namespace_.get("auto_lookup_shader_uint8");
    if (vl_test(vl_attr(view_dtype, "itemsize"), nb::int_(2), Py_EQ))
        return namespace_.get("auto_lookup_shader_uint16");
    nb::object factory = namespace_.get("ValueError");
    nb::object message = vl_concat(nb::str("Cannot use dtype "), vl_format(std::move(view_dtype)));
    message = vl_concat(std::move(message), nb::str(" with LabelVispyColormap"));
    vl_raise(vl_call1(std::move(factory), std::move(message)));
}
nb::object vispy_labels_label_selection(nb::object colormap, nb::object raw_dtype) {
    return vl_call1(vl_attr(std::move(colormap), "_selection_as_minimum_dtype"), std::move(raw_dtype));
}
void vispy_labels_label_set_glsl(nb::object self, nb::object shader, nb::object colormap,
                                nb::object selection, nb::dict globals) {
    LabelsNamespace namespace_(std::move(globals));
    nb::object replace = vl_attr(std::move(shader), "replace");
    nb::object string = namespace_.get("str");
    nb::object length = namespace_.get("len");
    nb::object colors = vl_attr(colormap, "colors");
    nb::object count = vl_call1(std::move(length), std::move(colors));
    nb::object text = vl_call1(std::move(string), std::move(count));
    text = vl_call2(std::move(replace), nb::str("$color_map_size"), std::move(text));
    replace = vl_attr(std::move(text), "replace");
    string = namespace_.get("str");
    nb::object use_selection = vl_attr(std::move(colormap), "use_selection");
    text = vl_call0(vl_attr(vl_call1(std::move(string), std::move(use_selection)), "lower"));
    text = vl_call2(std::move(replace), nb::str("$use_selection"), std::move(text));
    replace = vl_attr(std::move(text), "replace");
    text = vl_call1(namespace_.get("str"), std::move(selection));
    text = vl_call2(std::move(replace), nb::str("$selection"), std::move(text));
    vl_store(self, "glsl_map", std::move(text));
}
nb::object vispy_labels_direct_shader(nb::object multi, nb::dict globals) {
    LabelsNamespace namespace_(std::move(globals));
    if (vl_truth(std::move(multi))) return namespace_.get("direct_lookup_shader_many");
    return namespace_.get("direct_lookup_shader");
}
void vispy_labels_direct_set_glsl(nb::object self, nb::object shader, nb::object use_selection,
                                 nb::object selection, nb::object scale, nb::object color_map_size,
                                 nb::dict globals) {
    LabelsNamespace namespace_(std::move(globals));
    nb::object replace = vl_attr(std::move(shader), "replace");
    nb::object text = vl_call0(vl_attr(vl_call1(namespace_.get("str"), std::move(use_selection)), "lower"));
    text = vl_call2(std::move(replace), nb::str("$use_selection"), std::move(text));
    replace = vl_attr(std::move(text), "replace");
    text = vl_call1(namespace_.get("str"), std::move(selection));
    text = vl_call2(std::move(replace), nb::str("$selection"), std::move(text));
    replace = vl_attr(std::move(text), "replace");
    text = vl_call1(namespace_.get("str"), std::move(scale));
    text = vl_call2(std::move(replace), nb::str("$scale"), std::move(text));
    replace = vl_attr(std::move(text), "replace");
    text = vl_call1(namespace_.get("str"), std::move(color_map_size));
    text = vl_call2(std::move(replace), nb::str("$color_map_size"), std::move(text));
    vl_store(self, "glsl_map", std::move(text));
}

nb::object vispy_labels_build_allocate(nb::object color_dict, nb::object max_size, nb::dict globals) {
    LabelsNamespace namespace_(std::move(globals));
    if (vl_test(vl_call1(namespace_.get("len"), color_dict), nb::int_(1 << 23), Py_GT)) {
        nb::object factory = namespace_.get("ValueError");
        nb::object count = vl_call1(namespace_.get("len"), color_dict);
        nb::object message = vl_concat(nb::str("Cannot map more than 2**23 colors because of float32 precision. Got "), vl_format(std::move(count)));
        vl_raise(vl_call1(std::move(factory), std::move(message)));
    }
    nb::object count = vl_call1(namespace_.get("len"), color_dict);
    nb::object capacity = vl_power2(max_size);
    if (vl_test(std::move(count), std::move(capacity), Py_GT)) {
        nb::object factory = namespace_.get("ValueError");
        nb::object message = vl_concat(nb::str("Cannot create a 2D texture holding more than "), vl_format(max_size));
        message = vl_concat(std::move(message), nb::str("**2="));
        message = vl_concat(std::move(message), vl_format(vl_power2(max_size)));
        message = vl_concat(std::move(message), nb::str(" colors.Got "));
        count = vl_call1(namespace_.get("len"), color_dict);
        message = vl_concat(std::move(message), vl_format(std::move(count)));
        vl_raise(vl_call1(std::move(factory), std::move(message)));
    }
    nb::object zeros = namespace_.numpy("zeros");
    nb::object minimum = namespace_.get("min");
    count = vl_call1(namespace_.get("len"), color_dict);
    nb::object rows = vl_call2(std::move(minimum), std::move(count), max_size);
    nb::object ceil = vl_attr(namespace_.get("math"), "ceil");
    count = vl_call1(namespace_.get("len"), std::move(color_dict));
    nb::object divided = vl_checked(PyNumber_TrueDivide(count.ptr(), max_size.ptr()));
    count.reset(); max_size.reset();
    nb::object columns = vl_call1(std::move(ceil), std::move(divided));
    nb::tuple shape = nb::make_tuple(std::move(rows), std::move(columns), nb::int_(4));
    nb::object dtype = namespace_.numpy("float32");
    nb::dict keywords;
    keywords["dtype"] = std::move(dtype);
    return vl_call_kw(std::move(zeros), nb::make_tuple(std::move(shape)), std::move(keywords));
}
void vispy_labels_build_fill(nb::object data, nb::object color_dict, VispyLabelsTextureLocals& owner) {
    nb::object items = vl_call0(vl_attr(std::move(color_dict), "items"));
    nb::object iterator = vl_checked(PyObject_GetIter(items.ptr()));
    items.reset();
    for (;;) {
        PyObject* next = PyIter_Next(iterator.ptr());
        if (!next) { if (PyErr_Occurred()) throw nb::python_error(); break; }
        LabelsPair pair = vl_unpack_pair(nb::steal<nb::object>(next));
        owner.key = std::move(pair.first);
        owner.value = std::move(pair.second);
        nb::object extent = vl_item(vl_attr(data, "shape"), nb::int_(0));
        nb::object row = vl_checked(PyNumber_Remainder(owner.key.ptr(), extent.ptr()));
        extent.reset();
        extent = vl_item(vl_attr(data, "shape"), nb::int_(0));
        nb::object column = vl_checked(PyNumber_FloorDivide(owner.key.ptr(), extent.ptr()));
        extent.reset();
        vl_set(data, nb::make_tuple(std::move(row), std::move(column)), owner.value);
    }
}
nb::object vispy_labels_select_texture(nb::object colormap, nb::object view_dtype, nb::object raw_dtype) {
    if (vl_test(vl_attr(raw_dtype, "itemsize"), nb::int_(2), Py_GT))
        return vl_call1(vl_attr(std::move(colormap), "_get_mapping_from_cache"), std::move(view_dtype));
    return vl_call1(vl_attr(std::move(colormap), "_get_mapping_from_cache"), std::move(raw_dtype));
}
nb::object vispy_labels_reshape_texture(nb::object color_texture, nb::object raw_dtype,
                                       nb::object view_dtype, nb::dict globals) {
    if (color_texture.is_none()) {
        LabelsNamespace namespace_(std::move(globals));
        nb::object factory = namespace_.get("ValueError");
        nb::object message = vl_concat(nb::str("Cannot build a texture for dtype raw_dtype="), vl_repr(std::move(raw_dtype)));
        message = vl_concat(std::move(message), nb::str(" and view_dtype="));
        message = vl_concat(std::move(message), vl_repr(std::move(view_dtype)));
        vl_raise(vl_call1(std::move(factory), std::move(message)));
    }
    return vl_call3(vl_attr(std::move(color_texture), "reshape"), nb::int_(256), nb::int_(-1), nb::int_(4));
}

void vispy_labels_initialize(nb::object self) { vl_store(self, "_colormap_dtypes", nb::none()); }
nb::object vispy_labels_super_init(nb::object callback, nb::object layer, nb::object node,
                                  nb::object texture_format, nb::object kwargs, nb::dict globals) {
    LabelsNamespace namespace_(std::move(globals));
    nb::dict keywords;
    keywords["node"] = std::move(node);
    keywords["texture_format"] = std::move(texture_format);
    keywords["layer_node_class"] = namespace_.get("LabelLayerNode");
    vl_merge_keywords(callback, keywords, std::move(kwargs));
    return vl_call_kw(std::move(callback), nb::make_tuple(std::move(layer)), std::move(keywords));
}
void vispy_labels_connect_events(nb::object self) {
    const char* events[] = {"labels_update", "selected_label", "show_selected_label", "iso_gradient_mode", "data"};
    const char* callbacks[] = {"_on_partial_labels_update", "_on_colormap_change", "_on_colormap_change", "_on_iso_gradient_mode_change", "_on_colormap_change"};
    for (size_t i = 0; i < 5; ++i) {
        nb::object event = vl_attr(vl_attr(vl_attr(self, "layer"), "events"), events[i]);
        nb::object connect = vl_attr(std::move(event), "connect");
        nb::object callback = vl_attr(self, callbacks[i]);
        vl_call1(std::move(connect), std::move(callback));
    }
}
nb::object vl_slice_image(nb::object self, const char* name) {
    return vl_attr(vl_attr(vl_attr(vl_attr(std::move(self), "layer"), "_slice"), "image"), name);
}
nb::object vispy_labels_current_dtypes(nb::object self) {
    nb::object raw = vl_attr(vl_slice_image(self, "raw"), "dtype");
    nb::object view = vl_attr(vl_slice_image(std::move(self), "view"), "dtype");
    return nb::make_tuple(std::move(raw), std::move(view));
}
bool vispy_labels_dtypes_changed(nb::object self, nb::object dtypes) {
    nb::object previous = vl_attr(std::move(self), "_colormap_dtypes");
    return vl_test(std::move(dtypes), std::move(previous), Py_NE);
}
nb::object vispy_labels_notify_colormap(nb::object self) { return vl_call0(vl_attr(std::move(self), "_on_colormap_change")); }
bool vispy_labels_is_volume(nb::object self, nb::dict globals) {
    LabelsNamespace namespace_(std::move(globals));
    nb::object callable = namespace_.get("isinstance");
    nb::object node = vl_attr(std::move(self), "node");
    nb::object type = namespace_.get("VolumeNode");
    return vl_truth(vl_call2(std::move(callable), std::move(node), std::move(type)));
}
nb::object vispy_labels_rendering(nb::object self) { return vl_attr(vl_attr(std::move(self), "layer"), "rendering"); }
void vispy_labels_set_rendering(nb::object self, nb::object rendering) {
    nb::object method;
    if (vl_test(rendering, nb::str("translucent"), Py_NE)) method = std::move(rendering);
    else method = nb::str("translucent_categorical");
    nb::object node = vl_attr(std::move(self), "node");
    vl_store(node, "method", std::move(method));
}
bool vispy_labels_ignore_event(nb::object self, nb::object event) {
    if (event.is_none()) return false;
    if (!vl_test(vl_attr(std::move(event), "type"), nb::str("selected_label"), Py_EQ)) return false;
    return !vl_truth(vl_attr(vl_attr(std::move(self), "layer"), "show_selected_label"));
}
nb::object vispy_labels_colormap(nb::object self) { return vl_attr(vl_attr(std::move(self), "layer"), "colormap"); }
nb::object vispy_labels_auto_mode(nb::object colormap, nb::dict globals) {
    LabelsNamespace namespace_(std::move(globals));
    nb::object callable = namespace_.get("isinstance");
    nb::object type = namespace_.get("CyclicLabelColormap");
    return vl_call2(std::move(callable), std::move(colormap), std::move(type));
}
nb::object vispy_labels_view_dtype(nb::object self) { return vl_attr(vl_slice_image(std::move(self), "view"), "dtype"); }
nb::object vispy_labels_raw_dtype(nb::object self) { return vl_attr(vl_slice_image(std::move(self), "raw"), "dtype"); }
bool vispy_labels_uses_auto_texture(nb::object auto_mode, nb::object raw_dtype) {
    if (vl_truth(std::move(auto_mode))) return true;
    return vl_test(vl_attr(std::move(raw_dtype), "itemsize"), nb::int_(2), Py_LE);
}
bool vispy_labels_wide_dtype(nb::object raw_dtype) { return vl_test(vl_attr(std::move(raw_dtype), "itemsize"), nb::int_(2), Py_GT); }
nb::object vispy_labels_clone_cyclic(nb::object colormap, nb::dict globals) {
    LabelsNamespace namespace_(std::move(globals));
    nb::object constructor = namespace_.get("CyclicLabelColormap");
    nb::object dumped = vl_call0(vl_attr(std::move(colormap), "model_dump"));
    nb::dict keywords;
    vl_merge_keywords(constructor, keywords, std::move(dumped));
    return vl_call_kw(std::move(constructor), nb::tuple(), std::move(keywords));
}
void vispy_labels_update_background(nb::object colormap, nb::object raw_dtype) {
    nb::object value = vl_call1(vl_attr(colormap, "_background_as_minimum_dtype"), std::move(raw_dtype));
    vl_store(colormap, "background_value", std::move(value));
}
nb::object vispy_labels_select_colormap_texture(nb::object colormap, nb::object view_dtype,
                                               nb::object raw_dtype, nb::dict globals) {
    LabelsNamespace namespace_(std::move(globals));
    return vl_call3(namespace_.get("_select_colormap_texture"), std::move(colormap), std::move(view_dtype), std::move(raw_dtype));
}
void vispy_labels_publish_auto_cmap(nb::object self, nb::object colormap, nb::object view_dtype,
                                   nb::object raw_dtype, nb::dict globals) {
    LabelsNamespace namespace_(std::move(globals));
    nb::object constructor = namespace_.get("LabelVispyColormap");
    nb::dict keywords;
    keywords["view_dtype"] = std::move(view_dtype);
    keywords["raw_dtype"] = std::move(raw_dtype);
    nb::object result = vl_call_kw(std::move(constructor), nb::make_tuple(std::move(colormap)), std::move(keywords));
    nb::object node = vl_attr(std::move(self), "node");
    vl_store(node, "cmap", std::move(result));
}
void vispy_labels_publish_texture(nb::object self, nb::object color_texture, nb::dict globals) {
    LabelsNamespace namespace_(std::move(globals));
    nb::object constructor = namespace_.get("Texture2D");
    nb::dict keywords;
    keywords["internalformat"] = nb::str("rgba32f");
    keywords["interpolation"] = nb::str("nearest");
    nb::object texture = vl_call_kw(std::move(constructor), nb::make_tuple(std::move(color_texture)), std::move(keywords));
    nb::object program = vl_attr(vl_attr(std::move(self), "node"), "shared_program");
    vl_set(program, nb::str("texture2D_values"), std::move(texture));
}
void vispy_labels_publish_texture_data(nb::object self, nb::object color_texture) { vl_store(self, "texture_data", std::move(color_texture)); }
bool vispy_labels_not_auto(nb::object auto_mode) { return !vl_truth(std::move(auto_mode)); }
nb::object vispy_labels_colors_dict(nb::object colormap) {
    return vl_item(vl_call0(vl_attr(std::move(colormap), "_values_mapping_to_minimum_values_set")), nb::int_(1));
}
nb::object vispy_labels_max_texture_size(nb::dict globals) {
    return vl_item(vl_call0(LabelsNamespace(std::move(globals)).get("get_max_texture_sizes")), nb::int_(0));
}
nb::object vispy_labels_build_texture(nb::object color_dict, nb::object max_size, nb::dict globals) {
    return vl_call2(LabelsNamespace(std::move(globals)).get("build_textures_from_dict"), std::move(color_dict), std::move(max_size));
}
nb::object vispy_labels_direct_dtype(nb::object self, nb::object raw_dtype, nb::dict globals) {
    LabelsNamespace namespace_(std::move(globals));
    nb::object callable = namespace_.get("_texture_dtype");
    nb::object count = vl_attr(vl_attr(vl_attr(std::move(self), "layer"), "_direct_colormap"), "_num_unique_colors");
    count = vl_add(std::move(count), nb::int_(2));
    return vl_call2(std::move(callable), std::move(count), std::move(raw_dtype));
}
bool vispy_labels_dtype_integer(nb::object dtype, nb::dict globals) {
    LabelsNamespace namespace_(std::move(globals));
    nb::object callable = namespace_.get("issubclass");
    nb::object type = vl_attr(std::move(dtype), "type");
    nb::object integer = namespace_.numpy("integer");
    return vl_truth(vl_call2(std::move(callable), std::move(type), std::move(integer)));
}
nb::object vispy_labels_dtype_scale(nb::object dtype, nb::dict globals) {
    return vl_attr(vl_call1(LabelsNamespace(std::move(globals)).numpy("iinfo"), std::move(dtype)), "max");
}
nb::object vispy_labels_float_scale() { return nb::float_(1.0); }
void vispy_labels_publish_direct_cmap(nb::object self, nb::object colormap, nb::object scale,
                                     nb::object val_texture, nb::dict globals) {
    LabelsNamespace namespace_(std::move(globals));
    nb::object constructor = namespace_.get("DirectLabelVispyColormap");
    nb::dict keywords;
    keywords["use_selection"] = vl_attr(colormap, "use_selection");
    keywords["selection"] = vl_attr(std::move(colormap), "selection");
    keywords["scale"] = std::move(scale);
    keywords["color_map_size"] = vl_item(vl_attr(val_texture, "shape"), nb::int_(0));
    nb::object columns = vl_item(vl_attr(std::move(val_texture), "shape"), nb::int_(1));
    keywords["multi"] = vl_compare(std::move(columns), nb::int_(1), Py_GT);
    nb::object result = vl_call_kw(std::move(constructor), nb::tuple(), std::move(keywords));
    nb::object node = vl_attr(std::move(self), "node");
    vl_store(node, "cmap", std::move(result));
}
void vispy_labels_publish_lut_shape(nb::object self, nb::object val_texture) {
    nb::object shape = vl_prefix_slice(vl_attr(std::move(val_texture), "shape"), nb::int_(2));
    nb::object program = vl_attr(vl_attr(std::move(self), "node"), "shared_program");
    vl_set(program, nb::str("LUT_shape"), std::move(shape));
}
void vispy_labels_publish_fallback_cmap(nb::object self, nb::object colormap, nb::dict globals) {
    LabelsNamespace namespace_(std::move(globals));
    nb::object constructor = namespace_.get("VispyColormap");
    nb::object args = vl_checked(PySequence_Tuple(colormap.ptr()));
    colormap.reset();
    nb::object result = vl_checked(PyObject_CallObject(constructor.ptr(), args.ptr()));
    constructor.reset(); args.reset();
    nb::object node = vl_attr(std::move(self), "node");
    vl_store(node, "cmap", std::move(result));
}
void vispy_labels_publish_dtypes(nb::object self, nb::object raw_dtype, nb::object view_dtype) {
    vl_store(self, "_colormap_dtypes", nb::make_tuple(std::move(raw_dtype), std::move(view_dtype)));
}
void vispy_labels_set_iso(nb::object self) {
    nb::object mode = vl_attr(vl_attr(self, "layer"), "iso_gradient_mode");
    nb::object node = vl_attr(std::move(self), "node");
    vl_store(node, "iso_gradient_mode", std::move(mode));
}
bool vispy_labels_loaded(nb::object self) { return vl_truth(vl_attr(vl_attr(std::move(self), "layer"), "loaded")); }
nb::object vispy_labels_raw_displayed(nb::object self) { return vl_slice_image(std::move(self), "raw"); }
nb::object vispy_labels_ndims(nb::object event, nb::dict globals) {
    LabelsNamespace namespace_(std::move(globals));
    nb::object length = namespace_.get("len");
    return vl_call1(std::move(length), vl_attr(std::move(event), "offset"));
}
bool vispy_labels_partial_mismatch(nb::object self, nb::object raw_displayed, nb::object ndims) {
    nb::object shape = vl_attr(vl_attr(vl_attr(std::move(self), "node"), "_texture"), "shape");
    nb::object first = vl_prefix_slice(std::move(shape), ndims);
    nb::object second = vl_prefix_slice(vl_attr(std::move(raw_displayed), "shape"), std::move(ndims));
    return vl_test(std::move(first), std::move(second), Py_NE);
}
nb::object vispy_labels_refresh(nb::object self) { return vl_call0(vl_attr(vl_attr(std::move(self), "layer"), "refresh")); }
nb::object vispy_labels_partial_upload(nb::object self, nb::object event) {
    nb::object callable = vl_attr(vl_attr(vl_attr(std::move(self), "node"), "_texture"), "scale_and_set_data");
    nb::object data = vl_attr(event, "data");
    nb::dict keywords;
    keywords["copy"] = nb::bool_(false);
    keywords["offset"] = vl_attr(std::move(event), "offset");
    return vl_call_kw(std::move(callable), nb::make_tuple(std::move(data)), std::move(keywords));
}
nb::object vispy_labels_update(nb::object self) { return vl_call0(vl_attr(vl_attr(std::move(self), "node"), "update")); }
nb::object vispy_labels_notify_iso(nb::object self) { return vl_call0(vl_attr(std::move(self), "_on_iso_gradient_mode_change")); }
void vispy_labels_node_init(nb::object self, nb::object custom_node, nb::object texture_format) {
    vl_store(self, "_custom_node", std::move(custom_node));
    vl_call1(vl_attr(std::move(self), "_setup_nodes"), std::move(texture_format));
}
void vispy_labels_setup_nodes(nb::object self, nb::object texture_format, nb::dict globals) {
    LabelsNamespace namespace_(std::move(globals));
    nb::object constructor = namespace_.get("LabelNode");
    nb::object data;
    bool empty = texture_format.is_none();
    if (!empty) empty = vl_test(texture_format, nb::str("auto"), Py_EQ);
    if (empty) data = nb::none();
    else {
        nb::object zeros = namespace_.numpy("zeros");
        nb::tuple shape = nb::make_tuple(nb::int_(1), nb::int_(1));
        nb::object format = namespace_.get("get_dtype_from_vispy_texture_format");
        nb::object dtype = vl_call1(std::move(format), texture_format);
        nb::dict keywords;
        keywords["dtype"] = std::move(dtype);
        data = vl_call_kw(std::move(zeros), nb::make_tuple(std::move(shape)), std::move(keywords));
    }
    nb::dict keywords;
    keywords["method"] = nb::str("auto");
    keywords["texture_format"] = texture_format;
    nb::object result = vl_call_kw(std::move(constructor), nb::make_tuple(std::move(data)), std::move(keywords));
    vl_store(self, "_image_node", std::move(result));

    constructor = namespace_.get("VolumeNode");
    nb::object zeros = namespace_.numpy("zeros");
    nb::tuple shape = nb::make_tuple(nb::int_(1), nb::int_(1), nb::int_(1));
    nb::object format = namespace_.get("get_dtype_from_vispy_texture_format");
    nb::object dtype = vl_call1(std::move(format), texture_format);
    keywords = nb::dict();
    keywords["dtype"] = std::move(dtype);
    data = vl_call_kw(std::move(zeros), nb::make_tuple(std::move(shape)), std::move(keywords));
    nb::list clim;
    clim.append(nb::int_(0)); clim.append(nb::int_((1 << 23) - 1));
    keywords = nb::dict();
    keywords["clim"] = std::move(clim);
    keywords["texture_format"] = std::move(texture_format);
    keywords["interpolation"] = nb::str("nearest");
    result = vl_call_kw(std::move(constructor), nb::make_tuple(std::move(data)), std::move(keywords));
    vl_store(self, "_volume_node", std::move(result));
}
nb::object vispy_labels_choose_node(nb::object self, nb::object ndisplay) {
    if (vl_test(std::move(ndisplay), nb::int_(2), Py_EQ)) return vl_attr(std::move(self), "_image_node");
    return vl_attr(std::move(self), "_volume_node");
}
bool vispy_labels_node_needs_reset(nb::object res, nb::object dtype, nb::dict globals) {
    if (!vl_test(vl_attr(res, "texture_format"), nb::str("auto"), Py_NE)) return false;
    if (dtype.is_none()) return false;
    LabelsNamespace namespace_(std::move(globals));
    nb::object mapping = namespace_.get("_VISPY_FORMAT_TO_DTYPE");
    nb::object format = vl_attr(std::move(res), "texture_format");
    nb::object mapped = vl_item(std::move(mapping), std::move(format));
    return vl_test(std::move(mapped), std::move(dtype), Py_NE);
}
nb::object vispy_labels_rebuild_nodes(nb::object self, nb::object dtype, nb::dict globals) {
    LabelsNamespace namespace_(std::move(globals));
    nb::object callback = vl_attr(std::move(self), "_setup_nodes");
    nb::object format = vl_item(namespace_.get("_DTYPE_TO_VISPY_FORMAT"), std::move(dtype));
    return vl_call1(std::move(callback), std::move(format));
}
nb::object vispy_labels_get_node_again(nb::object self, nb::object ndisplay, nb::object dtype) {
    return vl_call2(vl_attr(std::move(self), "get_node"), std::move(ndisplay), std::move(dtype));
}

// This per-call carrier holds only the source's assigned locals. The Python
// facade retains it if an exception saves its traceback; it never becomes
// controller state or another authority for nodes, namespaces or caches.
struct VispyLabelsOperationLocals : VispyLabelsTextureLocals {
    enum class Layout { Empty, Label, Direct, Build, Select, Data, Rendering, Colormap, Partial, Node };
    Layout layout = Layout::Empty;
    nb::object shader, selection, colors, data, color_texture, dtypes, rendering;
    nb::object colormap, auto_mode, view_dtype, raw_dtype, color_dict, max_size;
    nb::object val_texture, dtype, scale, raw_displayed, ndims, res;

    VispyLabelsOperationLocals() = default;
    VispyLabelsOperationLocals(VispyLabelsOperationLocals&&) = default;
    ~VispyLabelsOperationLocals() { clear(); }

    void clear() {
        std::array<nb::object, 21> old = {
            std::move(shader), std::move(selection), std::move(colors), std::move(data),
            std::move(key), std::move(value), std::move(color_texture), std::move(dtypes),
            std::move(rendering), std::move(colormap), std::move(auto_mode), std::move(view_dtype),
            std::move(raw_dtype), std::move(color_dict), std::move(max_size), std::move(val_texture),
            std::move(dtype), std::move(scale), std::move(raw_displayed), std::move(ndims), std::move(res),
        };
        const Layout previous = layout;
        layout = Layout::Empty;
        // Detach every edge before any release; then follow this source body's
        // assigned-local order rather than the storage order of the union.
        const auto release = [&](std::initializer_list<size_t> order) {
            for (size_t index : order) old[index].reset();
        };
        switch (previous) {
            case Layout::Label: release({0, 1}); break;
            case Layout::Direct: release({2, 0}); break;
            case Layout::Build: release({3, 4, 5}); break;
            case Layout::Select: release({6}); break;
            case Layout::Data: release({7}); break;
            case Layout::Rendering: release({8}); break;
            case Layout::Colormap: release({9, 10, 11, 12, 6, 13, 14, 15, 16, 17}); break;
            case Layout::Partial: release({18, 19}); break;
            case Layout::Node: release({20}); break;
            case Layout::Empty: break;
        }
        for (nb::object& value : old) value.reset();
    }
};
int vl_operation_locals_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) {
        const auto& state = *nb::inst_ptr<VispyLabelsOperationLocals>(self);
        Py_VISIT(state.shader.ptr()); Py_VISIT(state.selection.ptr()); Py_VISIT(state.colors.ptr());
        Py_VISIT(state.data.ptr()); Py_VISIT(state.key.ptr()); Py_VISIT(state.value.ptr());
        Py_VISIT(state.color_texture.ptr()); Py_VISIT(state.dtypes.ptr()); Py_VISIT(state.rendering.ptr());
        Py_VISIT(state.colormap.ptr()); Py_VISIT(state.auto_mode.ptr()); Py_VISIT(state.view_dtype.ptr());
        Py_VISIT(state.raw_dtype.ptr()); Py_VISIT(state.color_dict.ptr()); Py_VISIT(state.max_size.ptr());
        Py_VISIT(state.val_texture.ptr()); Py_VISIT(state.dtype.ptr()); Py_VISIT(state.scale.ptr());
        Py_VISIT(state.raw_displayed.ptr()); Py_VISIT(state.ndims.ptr()); Py_VISIT(state.res.ptr());
    }
    return 0;
}
int vl_operation_locals_clear(PyObject* self) {
    if (nb::inst_ready(self)) nb::inst_ptr<VispyLabelsOperationLocals>(self)->clear();
    return 0;
}
PyType_Slot vl_operation_locals_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(vl_operation_locals_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(vl_operation_locals_clear)}, {0, nullptr},
};

void vispy_labels_label_init(nb::object self, nb::object colormap, nb::object view_dtype,
                             nb::object raw_dtype, nb::object super_bind, nb::dict globals,
                             VispyLabelsOperationLocals& owner) {
    owner.layout = VispyLabelsOperationLocals::Layout::Label;
    vispy_labels_label_super_init(vl_call1(std::move(super_bind), self));
    owner.shader = vispy_labels_label_shader(view_dtype, globals);
    owner.selection = vispy_labels_label_selection(colormap, raw_dtype);
    vispy_labels_label_set_glsl(std::move(self), owner.shader, std::move(colormap), owner.selection, std::move(globals));
}
void vispy_labels_direct_init(nb::object self, nb::object use_selection, nb::object selection,
                              nb::object scale, nb::object color_map_size, nb::object multi,
                              nb::object super_bind, nb::dict globals, VispyLabelsOperationLocals& owner) {
    owner.layout = VispyLabelsOperationLocals::Layout::Direct;
    owner.colors = vispy_labels_direct_colors();
    nb::object callback = vl_call1(std::move(super_bind), self);
    vispy_labels_direct_super_init(std::move(callback), owner.colors);
    owner.shader = vispy_labels_direct_shader(std::move(multi), globals);
    vispy_labels_direct_set_glsl(std::move(self), owner.shader, std::move(use_selection), std::move(selection),
                                std::move(scale), std::move(color_map_size), std::move(globals));
}
nb::object vispy_labels_build_from_dict(nb::object color_dict, nb::object max_size, nb::dict globals,
                                       VispyLabelsOperationLocals& owner) {
    owner.layout = VispyLabelsOperationLocals::Layout::Build;
    owner.data = vispy_labels_build_allocate(color_dict, std::move(max_size), std::move(globals));
    vispy_labels_build_fill(owner.data, std::move(color_dict), owner);
    return owner.data;
}
nb::object vispy_labels_select_texture_operation(nb::object colormap, nb::object view_dtype,
                                                nb::object raw_dtype, nb::dict globals,
                                                VispyLabelsOperationLocals& owner) {
    owner.layout = VispyLabelsOperationLocals::Layout::Select;
    owner.color_texture = vispy_labels_select_texture(std::move(colormap), view_dtype, raw_dtype);
    return vispy_labels_reshape_texture(owner.color_texture, std::move(raw_dtype), std::move(view_dtype), std::move(globals));
}
void vispy_labels_controller_init(nb::object self, nb::object layer, nb::object node,
                                  nb::object texture_format, nb::object kwargs,
                                  nb::object super_bind, nb::dict globals) {
    vispy_labels_initialize(self);
    // The binder contains only real zero-argument super() protocol capture.
    // Invoke it after the original initial store, not when the facade calls us.
    nb::object callback = vl_call1(std::move(super_bind), self);
    vispy_labels_super_init(std::move(callback), std::move(layer), std::move(node),
                            std::move(texture_format), std::move(kwargs), std::move(globals));
    vispy_labels_connect_events(std::move(self));
}
void vispy_labels_data_change(nb::object self, nb::object super_bind, nb::dict globals,
                              VispyLabelsOperationLocals& owner) {
    owner.layout = VispyLabelsOperationLocals::Layout::Data;
    vl_call0(vl_call1(std::move(super_bind), self));
    owner.dtypes = vispy_labels_current_dtypes(self);
    if (vispy_labels_dtypes_changed(self, owner.dtypes)) vispy_labels_notify_colormap(std::move(self));
}
void vispy_labels_rendering_change(nb::object self, nb::dict globals, VispyLabelsOperationLocals& owner) {
    owner.layout = VispyLabelsOperationLocals::Layout::Rendering;
    if (vispy_labels_is_volume(self, std::move(globals))) {
        owner.rendering = vispy_labels_rendering(self);
        vispy_labels_set_rendering(std::move(self), owner.rendering);
    }
}
void vispy_labels_colormap_change(nb::object self, nb::object event, nb::dict globals,
                                  VispyLabelsOperationLocals& owner) {
    owner.layout = VispyLabelsOperationLocals::Layout::Colormap;
    if (vispy_labels_ignore_event(self, std::move(event))) return;
    owner.colormap = vispy_labels_colormap(self);
    owner.auto_mode = vispy_labels_auto_mode(owner.colormap, globals);
    owner.view_dtype = vispy_labels_view_dtype(self);
    owner.raw_dtype = vispy_labels_raw_dtype(self);
    if (vispy_labels_uses_auto_texture(owner.auto_mode, owner.raw_dtype)) {
        if (vispy_labels_wide_dtype(owner.raw_dtype)) {
            owner.colormap = vispy_labels_clone_cyclic(owner.colormap, globals);
            vispy_labels_update_background(owner.colormap, owner.raw_dtype);
        }
        owner.color_texture = vispy_labels_select_colormap_texture(owner.colormap, owner.view_dtype, owner.raw_dtype, globals);
        vispy_labels_publish_auto_cmap(self, owner.colormap, owner.view_dtype, owner.raw_dtype, globals);
        vispy_labels_publish_texture(self, owner.color_texture, globals);
        vispy_labels_publish_texture_data(self, owner.color_texture);
    } else if (vispy_labels_not_auto(owner.auto_mode)) {
        owner.color_dict = vispy_labels_colors_dict(owner.colormap);
        owner.max_size = vispy_labels_max_texture_size(globals);
        owner.val_texture = vispy_labels_build_texture(owner.color_dict, owner.max_size, globals);
        owner.dtype = vispy_labels_direct_dtype(self, owner.raw_dtype, globals);
        if (vispy_labels_dtype_integer(owner.dtype, globals)) owner.scale = vispy_labels_dtype_scale(owner.dtype, globals);
        else owner.scale = vispy_labels_float_scale();
        vispy_labels_publish_direct_cmap(self, owner.colormap, owner.scale, owner.val_texture, globals);
        vispy_labels_publish_texture(self, owner.val_texture, globals);
        vispy_labels_publish_lut_shape(self, owner.val_texture);
    } else {
        vispy_labels_publish_fallback_cmap(self, owner.colormap, globals);
    }
    vispy_labels_publish_dtypes(std::move(self), owner.raw_dtype, owner.view_dtype);
}
void vispy_labels_iso_change(nb::object self, nb::dict globals) {
    if (vispy_labels_is_volume(self, std::move(globals))) vispy_labels_set_iso(std::move(self));
}
void vispy_labels_partial_update(nb::object self, nb::object event, nb::dict globals,
                                VispyLabelsOperationLocals& owner) {
    owner.layout = VispyLabelsOperationLocals::Layout::Partial;
    if (!vispy_labels_loaded(self)) return;
    owner.raw_displayed = vispy_labels_raw_displayed(self);
    owner.ndims = vispy_labels_ndims(event, std::move(globals));
    if (vispy_labels_partial_mismatch(self, owner.raw_displayed, owner.ndims)) {
        vispy_labels_refresh(std::move(self));
        return;
    }
    vispy_labels_partial_upload(self, std::move(event));
    vispy_labels_update(std::move(self));
}
void vispy_labels_reset(nb::object self, nb::object super_bind) {
    vl_call0(vl_call1(std::move(super_bind), self));
    vispy_labels_notify_colormap(self);
    vispy_labels_notify_iso(std::move(self));
}
nb::object vispy_labels_get_node(nb::object self, nb::object ndisplay, nb::object dtype,
                                nb::dict globals, VispyLabelsOperationLocals& owner) {
    owner.layout = VispyLabelsOperationLocals::Layout::Node;
    owner.res = vispy_labels_choose_node(self, ndisplay);
    if (vispy_labels_node_needs_reset(owner.res, dtype, globals)) {
        vispy_labels_rebuild_nodes(self, dtype, std::move(globals));
        return vispy_labels_get_node_again(std::move(self), std::move(ndisplay), std::move(dtype));
    }
    return owner.res;
}

} // namespace

void bind_vispy_labels(nb::module_& module) {
    nb::class_<VispyLabelsOperationLocals>(module, "VispyLabelsOperationLocals",
        nb::is_weak_referenceable(), nb::type_slots(vl_operation_locals_slots))
        .def_prop_ro("shader", [](const VispyLabelsOperationLocals& s) { return vl_optional(s.shader); })
        .def_prop_ro("selection", [](const VispyLabelsOperationLocals& s) { return vl_optional(s.selection); })
        .def_prop_ro("colors", [](const VispyLabelsOperationLocals& s) { return vl_optional(s.colors); })
        .def_prop_ro("data", [](const VispyLabelsOperationLocals& s) { return vl_optional(s.data); })
        .def_prop_ro("key", [](const VispyLabelsOperationLocals& s) { return vl_optional(s.key); })
        .def_prop_ro("value", [](const VispyLabelsOperationLocals& s) { return vl_optional(s.value); })
        .def_prop_ro("color_texture", [](const VispyLabelsOperationLocals& s) { return vl_optional(s.color_texture); })
        .def_prop_ro("dtypes", [](const VispyLabelsOperationLocals& s) { return vl_optional(s.dtypes); })
        .def_prop_ro("rendering", [](const VispyLabelsOperationLocals& s) { return vl_optional(s.rendering); })
        .def_prop_ro("colormap", [](const VispyLabelsOperationLocals& s) { return vl_optional(s.colormap); })
        .def_prop_ro("auto_mode", [](const VispyLabelsOperationLocals& s) { return vl_optional(s.auto_mode); })
        .def_prop_ro("view_dtype", [](const VispyLabelsOperationLocals& s) { return vl_optional(s.view_dtype); })
        .def_prop_ro("raw_dtype", [](const VispyLabelsOperationLocals& s) { return vl_optional(s.raw_dtype); })
        .def_prop_ro("color_dict", [](const VispyLabelsOperationLocals& s) { return vl_optional(s.color_dict); })
        .def_prop_ro("max_size", [](const VispyLabelsOperationLocals& s) { return vl_optional(s.max_size); })
        .def_prop_ro("val_texture", [](const VispyLabelsOperationLocals& s) { return vl_optional(s.val_texture); })
        .def_prop_ro("dtype", [](const VispyLabelsOperationLocals& s) { return vl_optional(s.dtype); })
        .def_prop_ro("scale", [](const VispyLabelsOperationLocals& s) { return vl_optional(s.scale); })
        .def_prop_ro("raw_displayed", [](const VispyLabelsOperationLocals& s) { return vl_optional(s.raw_displayed); })
        .def_prop_ro("ndims", [](const VispyLabelsOperationLocals& s) { return vl_optional(s.ndims); })
        .def_prop_ro("res", [](const VispyLabelsOperationLocals& s) { return vl_optional(s.res); });
    module.def("vispy_labels_operation_locals", []() { return VispyLabelsOperationLocals{}; });
    module.def("vispy_labels_label_init", &vispy_labels_label_init, nb::arg("self").none(), nb::arg("colormap").none(), nb::arg("view_dtype").none(), nb::arg("raw_dtype").none(), nb::arg("super_bind").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("vispy_labels_direct_init", &vispy_labels_direct_init, nb::arg("self").none(), nb::arg("use_selection").none(), nb::arg("selection").none(), nb::arg("scale").none(), nb::arg("color_map_size").none(), nb::arg("multi").none(), nb::arg("super_bind").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("vispy_labels_build_from_dict", &vispy_labels_build_from_dict, nb::arg("color_dict").none(), nb::arg("max_size").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("vispy_labels_select_texture_operation", &vispy_labels_select_texture_operation, nb::arg("colormap").none(), nb::arg("view_dtype").none(), nb::arg("raw_dtype").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("vispy_labels_controller_init", &vispy_labels_controller_init, nb::arg("self").none(), nb::arg("layer").none(), nb::arg("node").none(), nb::arg("texture_format").none(), nb::arg("kwargs").none(), nb::arg("super_bind").none(), nb::arg("namespace"));
    module.def("vispy_labels_data_change", &vispy_labels_data_change, nb::arg("self").none(), nb::arg("super_bind").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("vispy_labels_rendering_change", &vispy_labels_rendering_change, nb::arg("self").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("vispy_labels_colormap_change", &vispy_labels_colormap_change, nb::arg("self").none(), nb::arg("event").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("vispy_labels_iso_change", &vispy_labels_iso_change, nb::arg("self").none(), nb::arg("namespace"));
    module.def("vispy_labels_partial_update", &vispy_labels_partial_update, nb::arg("self").none(), nb::arg("event").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("vispy_labels_reset", &vispy_labels_reset, nb::arg("self").none(), nb::arg("super_bind").none());
    module.def("vispy_labels_get_node", &vispy_labels_get_node, nb::arg("self").none(), nb::arg("ndisplay").none(), nb::arg("dtype").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    nb::class_<VispyLabelsTextureLocals>(module, "VispyLabelsTextureLocals",
        nb::is_weak_referenceable(), nb::type_slots(vl_locals_slots))
        .def_prop_ro("key", [](const VispyLabelsTextureLocals& s) { return vl_optional(s.key); })
        .def_prop_ro("value", [](const VispyLabelsTextureLocals& s) { return vl_optional(s.value); });
    module.def("vispy_labels_texture_loop_locals", []() { return VispyLabelsTextureLocals{}; });
    module.def("vispy_labels_cache_deepcopy", &vispy_labels_cache_deepcopy, nb::arg("self").none(), nb::arg("namespace"));
    module.def("vispy_labels_selection_minimum", &vispy_labels_selection_minimum, nb::arg("self").none(), nb::arg("dtype").none(), nb::arg("namespace"));
    module.def("vispy_labels_background_minimum", &vispy_labels_background_minimum, nb::arg("self").none(), nb::arg("dtype").none(), nb::arg("namespace"));
    module.def("vispy_labels_direct_selection_minimum", &vispy_labels_direct_selection_minimum, nb::arg("self").none(), nb::arg("dtype").none(), nb::arg("namespace"));
    module.def("vispy_labels_label_super_init", &vispy_labels_label_super_init, nb::arg("callback").none());
    module.def("vispy_labels_label_shader", &vispy_labels_label_shader, nb::arg("view_dtype").none(), nb::arg("namespace"));
    module.def("vispy_labels_label_selection", &vispy_labels_label_selection, nb::arg("colormap").none(), nb::arg("raw_dtype").none());
    module.def("vispy_labels_label_set_glsl", &vispy_labels_label_set_glsl, nb::arg("self").none(), nb::arg("shader").none(), nb::arg("colormap").none(), nb::arg("selection").none(), nb::arg("namespace"));
    module.def("vispy_labels_direct_colors", &vispy_labels_direct_colors);
    module.def("vispy_labels_direct_super_init", &vispy_labels_direct_super_init, nb::arg("callback").none(), nb::arg("colors").none());
    module.def("vispy_labels_direct_shader", &vispy_labels_direct_shader, nb::arg("multi").none(), nb::arg("namespace"));
    module.def("vispy_labels_direct_set_glsl", &vispy_labels_direct_set_glsl, nb::arg("self").none(), nb::arg("shader").none(), nb::arg("use_selection").none(), nb::arg("selection").none(), nb::arg("scale").none(), nb::arg("color_map_size").none(), nb::arg("namespace"));
    module.def("vispy_labels_build_allocate", &vispy_labels_build_allocate, nb::arg("color_dict").none(), nb::arg("max_size").none(), nb::arg("namespace"));
    module.def("vispy_labels_build_fill", &vispy_labels_build_fill, nb::arg("data").none(), nb::arg("color_dict").none(), nb::arg("locals_owner"));
    module.def("vispy_labels_select_texture", &vispy_labels_select_texture, nb::arg("colormap").none(), nb::arg("view_dtype").none(), nb::arg("raw_dtype").none());
    module.def("vispy_labels_reshape_texture", &vispy_labels_reshape_texture, nb::arg("color_texture").none(), nb::arg("raw_dtype").none(), nb::arg("view_dtype").none(), nb::arg("namespace"));
    module.def("vispy_labels_initialize", &vispy_labels_initialize, nb::arg("self").none());
    module.def("vispy_labels_super_init", &vispy_labels_super_init, nb::arg("callback").none(), nb::arg("layer").none(), nb::arg("node").none(), nb::arg("texture_format").none(), nb::arg("kwargs").none(), nb::arg("namespace"));
    module.def("vispy_labels_connect_events", &vispy_labels_connect_events, nb::arg("self").none());
    module.def("vispy_labels_current_dtypes", &vispy_labels_current_dtypes, nb::arg("self").none());
    module.def("vispy_labels_dtypes_changed", &vispy_labels_dtypes_changed, nb::arg("self").none(), nb::arg("dtypes").none());
    module.def("vispy_labels_notify_colormap", &vispy_labels_notify_colormap, nb::arg("self").none());
    module.def("vispy_labels_is_volume", &vispy_labels_is_volume, nb::arg("self").none(), nb::arg("namespace"));
    module.def("vispy_labels_rendering", &vispy_labels_rendering, nb::arg("self").none());
    module.def("vispy_labels_set_rendering", &vispy_labels_set_rendering, nb::arg("self").none(), nb::arg("rendering").none());
    module.def("vispy_labels_ignore_event", &vispy_labels_ignore_event, nb::arg("self").none(), nb::arg("event").none());
    module.def("vispy_labels_colormap", &vispy_labels_colormap, nb::arg("self").none());
    module.def("vispy_labels_auto_mode", &vispy_labels_auto_mode, nb::arg("colormap").none(), nb::arg("namespace"));
    module.def("vispy_labels_view_dtype", &vispy_labels_view_dtype, nb::arg("self").none());
    module.def("vispy_labels_raw_dtype", &vispy_labels_raw_dtype, nb::arg("self").none());
    module.def("vispy_labels_uses_auto_texture", &vispy_labels_uses_auto_texture, nb::arg("auto_mode").none(), nb::arg("raw_dtype").none());
    module.def("vispy_labels_wide_dtype", &vispy_labels_wide_dtype, nb::arg("raw_dtype").none());
    module.def("vispy_labels_clone_cyclic", &vispy_labels_clone_cyclic, nb::arg("colormap").none(), nb::arg("namespace"));
    module.def("vispy_labels_update_background", &vispy_labels_update_background, nb::arg("colormap").none(), nb::arg("raw_dtype").none());
    module.def("vispy_labels_select_colormap_texture", &vispy_labels_select_colormap_texture, nb::arg("colormap").none(), nb::arg("view_dtype").none(), nb::arg("raw_dtype").none(), nb::arg("namespace"));
    module.def("vispy_labels_publish_auto_cmap", &vispy_labels_publish_auto_cmap, nb::arg("self").none(), nb::arg("colormap").none(), nb::arg("view_dtype").none(), nb::arg("raw_dtype").none(), nb::arg("namespace"));
    module.def("vispy_labels_publish_texture", &vispy_labels_publish_texture, nb::arg("self").none(), nb::arg("color_texture").none(), nb::arg("namespace"));
    module.def("vispy_labels_publish_texture_data", &vispy_labels_publish_texture_data, nb::arg("self").none(), nb::arg("color_texture").none());
    module.def("vispy_labels_not_auto", &vispy_labels_not_auto, nb::arg("auto_mode").none());
    module.def("vispy_labels_colors_dict", &vispy_labels_colors_dict, nb::arg("colormap").none());
    module.def("vispy_labels_max_texture_size", &vispy_labels_max_texture_size, nb::arg("namespace"));
    module.def("vispy_labels_build_texture", &vispy_labels_build_texture, nb::arg("color_dict").none(), nb::arg("max_size").none(), nb::arg("namespace"));
    module.def("vispy_labels_direct_dtype", &vispy_labels_direct_dtype, nb::arg("self").none(), nb::arg("raw_dtype").none(), nb::arg("namespace"));
    module.def("vispy_labels_dtype_integer", &vispy_labels_dtype_integer, nb::arg("dtype").none(), nb::arg("namespace"));
    module.def("vispy_labels_dtype_scale", &vispy_labels_dtype_scale, nb::arg("dtype").none(), nb::arg("namespace"));
    module.def("vispy_labels_float_scale", &vispy_labels_float_scale);
    module.def("vispy_labels_publish_direct_cmap", &vispy_labels_publish_direct_cmap, nb::arg("self").none(), nb::arg("colormap").none(), nb::arg("scale").none(), nb::arg("val_texture").none(), nb::arg("namespace"));
    module.def("vispy_labels_publish_lut_shape", &vispy_labels_publish_lut_shape, nb::arg("self").none(), nb::arg("val_texture").none());
    module.def("vispy_labels_publish_fallback_cmap", &vispy_labels_publish_fallback_cmap, nb::arg("self").none(), nb::arg("colormap").none(), nb::arg("namespace"));
    module.def("vispy_labels_publish_dtypes", &vispy_labels_publish_dtypes, nb::arg("self").none(), nb::arg("raw_dtype").none(), nb::arg("view_dtype").none());
    module.def("vispy_labels_set_iso", &vispy_labels_set_iso, nb::arg("self").none());
    module.def("vispy_labels_loaded", &vispy_labels_loaded, nb::arg("self").none());
    module.def("vispy_labels_raw_displayed", &vispy_labels_raw_displayed, nb::arg("self").none());
    module.def("vispy_labels_ndims", &vispy_labels_ndims, nb::arg("event").none(), nb::arg("namespace"));
    module.def("vispy_labels_partial_mismatch", &vispy_labels_partial_mismatch, nb::arg("self").none(), nb::arg("raw_displayed").none(), nb::arg("ndims").none());
    module.def("vispy_labels_refresh", &vispy_labels_refresh, nb::arg("self").none());
    module.def("vispy_labels_partial_upload", &vispy_labels_partial_upload, nb::arg("self").none(), nb::arg("event").none());
    module.def("vispy_labels_update", &vispy_labels_update, nb::arg("self").none());
    module.def("vispy_labels_notify_iso", &vispy_labels_notify_iso, nb::arg("self").none());
    module.def("vispy_labels_node_init", &vispy_labels_node_init, nb::arg("self").none(), nb::arg("custom_node").none(), nb::arg("texture_format").none());
    module.def("vispy_labels_setup_nodes", &vispy_labels_setup_nodes, nb::arg("self").none(), nb::arg("texture_format").none(), nb::arg("namespace"));
    module.def("vispy_labels_choose_node", &vispy_labels_choose_node, nb::arg("self").none(), nb::arg("ndisplay").none());
    module.def("vispy_labels_node_needs_reset", &vispy_labels_node_needs_reset, nb::arg("res").none(), nb::arg("dtype").none(), nb::arg("namespace"));
    module.def("vispy_labels_rebuild_nodes", &vispy_labels_rebuild_nodes, nb::arg("self").none(), nb::arg("dtype").none(), nb::arg("namespace"));
    module.def("vispy_labels_get_node_again", &vispy_labels_get_node_again, nb::arg("self").none(), nb::arg("ndisplay").none(), nb::arg("dtype").none());
}
} // namespace nx
