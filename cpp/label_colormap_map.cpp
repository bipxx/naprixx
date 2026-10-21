#include "arrays.hpp"
#include <utility>

namespace nx {
namespace {

nb::object map_checked(PyObject* value) {
    if (!value) throw nb::python_error();
    return nb::steal<nb::object>(value);
}
nb::object map_attr(nb::object value, const char* name) {
    return map_checked(PyObject_GetAttrString(value.ptr(), name));
}
nb::object map_item(nb::object value, nb::object key) {
    return map_checked(PyObject_GetItem(value.ptr(), key.ptr()));
}
nb::object map_call0(nb::object callable) {
    return map_checked(PyObject_CallNoArgs(callable.ptr()));
}
nb::object map_call1(nb::object callable, nb::object value) {
    return map_checked(PyObject_CallOneArg(callable.ptr(), value.ptr()));
}
nb::object map_call2(nb::object callable, nb::object first, nb::object second) {
    return map_checked(PyObject_CallFunctionObjArgs(
        callable.ptr(), first.ptr(), second.ptr(), nullptr));
}
nb::object map_call4(nb::object callable, nb::object first, nb::object second,
                     nb::object third, nb::object fourth) {
    return map_checked(PyObject_CallFunctionObjArgs(
        callable.ptr(), first.ptr(), second.ptr(), third.ptr(), fourth.ptr(), nullptr));
}
nb::object map_call_kw(nb::object callable, nb::tuple arguments,
                       const char* keyword, nb::object value) {
    nb::dict keywords;
    keywords[nb::str(keyword)] = value;
    return map_checked(PyObject_Call(callable.ptr(), arguments.ptr(), keywords.ptr()));
}
bool map_truth(nb::object value) {
    const int result = PyObject_IsTrue(value.ptr());
    if (result < 0) throw nb::python_error();
    return result != 0;
}
bool map_contains(nb::object container, nb::object value) {
    const int result = PySequence_Contains(container.ptr(), value.ptr());
    if (result < 0) throw nb::python_error();
    return result != 0;
}
nb::object map_compare(nb::object first, nb::object second, int operation) {
    return map_checked(PyObject_RichCompare(first.ptr(), second.ptr(), operation));
}
bool map_test(nb::object first, nb::object second, int operation) {
    return map_truth(map_compare(std::move(first), std::move(second), operation));
}
nb::object map_add(nb::object first, nb::object second) {
    return map_checked(PyNumber_Add(first.ptr(), second.ptr()));
}
nb::object map_subtract(nb::object first, nb::object second) {
    return map_checked(PyNumber_Subtract(first.ptr(), second.ptr()));
}
void map_set(nb::handle target, nb::object key, nb::object value) {
    if (PyObject_SetItem(target.ptr(), key.ptr(), value.ptr()) < 0)
        throw nb::python_error();
}

// Namespace references last only for the native expression that consumes
// them. Methods keep their original caches and named outer locals in Python.
struct MapNamespace {
    nb::dict globals;
    nb::object builtins;
    explicit MapNamespace(nb::dict namespace_)
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
        nb::object message = map_checked(PyUnicode_FromFormat("name '%s' is not defined", name));
        nb::object error = map_call1(nb::borrow<nb::object>(PyExc_NameError), std::move(message));
        if (PyObject_SetAttrString(error.ptr(), "name", key.ptr()) < 0) throw nb::python_error();
        PyErr_SetObject(PyExc_NameError, error.ptr());
        throw nb::python_error();
    }
    nb::object numpy(const char* name) const { return map_attr(get("np"), name); }
    nb::object accelerated(const char* name) const { return map_attr(get("_accel_cmap"), name); }
};

// A saved facade traceback retains only these two original loop locals. The
// iterator and expression temporaries are stack references, never authorities
// for self, cache, namespace, colors or the partially written output.
struct LabelColormapMapLocals {
    nb::object idx, value;
    void clear() {
        nb::object old_idx = std::move(idx), old_value = std::move(value);
        old_idx.reset();
        old_value.reset();
    }
};
int map_locals_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(nb::handle(self))) {
        auto* state = nb::inst_ptr<LabelColormapMapLocals>(nb::handle(self));
        Py_VISIT(state->idx.ptr());
        Py_VISIT(state->value.ptr());
    }
    return 0;
}
int map_locals_clear(PyObject* self) {
    if (nb::inst_ready(nb::handle(self)))
        nb::inst_ptr<LabelColormapMapLocals>(nb::handle(self))->clear();
    return 0;
}
PyType_Slot map_locals_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(map_locals_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(map_locals_clear)}, {0, nullptr},
};
nb::object map_optional(nb::object value) { return value.is_valid() ? value : nb::none(); }

nb::object labelmap_clone(nb::object self) {
    // LOAD_METHOD __class__ precedes model_dump and keyword expansion.
    nb::object constructor = map_attr(self, "__class__");
    nb::object mapping = map_call0(map_attr(std::move(self), "model_dump"));
    nb::dict keywords;
    // The source ** expansion rejects duplicate keys supplied by a mapping.
    // Use the interpreter's mapping-merge primitive, not dict replacement.
    if (_PyDict_MergeEx(keywords.ptr(), mapping.ptr(), 2) < 0) {
        if (PyErr_ExceptionMatches(PyExc_AttributeError)) {
            PyErr_Clear();
            nb::object label = map_checked(_PyObject_FunctionStr(constructor.ptr()));
            PyErr_Format(PyExc_TypeError, "%U argument after ** must be a mapping, not %.200s",
                         label.ptr(), Py_TYPE(mapping.ptr())->tp_name);
        } else if (PyErr_ExceptionMatches(PyExc_KeyError)) {
            PyObject *type = nullptr, *value = nullptr, *traceback = nullptr;
            PyErr_Fetch(&type, &value, &traceback);
            PyErr_NormalizeException(&type, &value, &traceback);
            nb::object error_type = nb::steal<nb::object>(type);
            nb::object error_value = nb::steal<nb::object>(value);
            nb::object error_traceback = nb::steal<nb::object>(traceback);
            nb::object arguments = map_attr(error_value, "args");
            nb::object key = map_item(std::move(arguments), nb::int_(0));
            nb::object label = map_checked(_PyObject_FunctionStr(constructor.ptr()));
            PyErr_Format(PyExc_TypeError, "%U got multiple values for keyword argument '%S'",
                         label.ptr(), key.ptr());
        }
        throw nb::python_error();
    }
    mapping.reset();
    nb::tuple arguments;
    return map_checked(PyObject_Call(constructor.ptr(), arguments.ptr(), keywords.ptr()));
}
void labelmap_disable_selection(nb::object cmap) {
    if (PyObject_SetAttrString(cmap.ptr(), "use_selection", Py_False) < 0)
        throw nb::python_error();
}
nb::object labelmap_cache_target(nb::object self, nb::object data_dtype, nb::dict globals) {
    MapNamespace namespace_(std::move(globals));
    nb::object callable = namespace_.get("_texture_dtype");
    nb::object count = map_attr(std::move(self), "_num_unique_colors");
    return map_call2(std::move(callable), std::move(count), std::move(data_dtype));
}
nb::object labelmap_cache_key(nb::object data_dtype, nb::object target_dtype) {
    return nb::make_tuple(std::move(data_dtype), std::move(target_dtype));
}
bool labelmap_cache_needs_fill(nb::object self, nb::object key, nb::object data_dtype) {
    // Release the membership payload before reading itemsize, like POP_JUMP.
    if (map_contains(map_attr(std::move(self), "_cache_mapping"), std::move(key))) return false;
    return map_test(map_attr(std::move(data_dtype), "itemsize"), nb::int_(2), Py_LE);
}
nb::object labelmap_cache_data(nb::object data_dtype, nb::object target_dtype, nb::dict globals) {
    MapNamespace namespace_(std::move(globals));
    nb::object arange = namespace_.numpy("arange");
    nb::object iinfo = namespace_.numpy("iinfo");
    nb::object limit = map_attr(map_call1(std::move(iinfo), target_dtype), "max");
    nb::object count = map_add(std::move(limit), nb::int_(1));
    nb::object data = map_call_kw(std::move(arange), nb::make_tuple(std::move(count)),
                                  "dtype", std::move(target_dtype));
    return map_call1(map_attr(std::move(data), "astype"), std::move(data_dtype));
}
void labelmap_cache_publish(nb::object self, nb::object key, nb::object data) {
    nb::object mapped = map_call1(map_attr(self, "_map_without_cache"), std::move(data));
    nb::object cache = map_attr(std::move(self), "_cache_mapping");
    map_set(cache, std::move(key), std::move(mapped));
}
nb::object labelmap_cache_get(nb::object self, nb::object key) {
    return map_call1(map_attr(map_attr(std::move(self), "_cache_mapping"), "get"), std::move(key));
}
nb::object labelmap_cache(nb::object self, nb::object values) {
    nb::object callable = map_attr(std::move(self), "_get_mapping_from_cache");
    nb::object dtype = map_attr(std::move(values), "dtype");
    return map_call1(std::move(callable), std::move(dtype));
}
bool labelmap_cache_present(nb::object mapper) { return !mapper.is_none(); }
nb::object labelmap_index(nb::object mapper, nb::object values) {
    return map_item(std::move(mapper), std::move(values));
}
nb::object labelmap_uncached(nb::object self, nb::object values) {
    return map_call1(map_attr(std::move(self), "_map_without_cache"), std::move(values));
}
bool labelmap_is_float(nb::object values) {
    return map_test(map_attr(map_attr(std::move(values), "dtype"), "kind"), nb::str("f"), Py_EQ);
}
nb::object labelmap_to_int64(nb::object values, nb::dict globals) {
    MapNamespace namespace_(std::move(globals));
    nb::object callable = map_attr(std::move(values), "astype");
    nb::object dtype = namespace_.numpy("int64");
    return map_call1(std::move(callable), std::move(dtype));
}
void labelmap_mask(nb::object self, nb::object values, nb::object mapped) {
    nb::object selection = map_attr(std::move(self), "selection");
    nb::object condition = map_compare(std::move(values), std::move(selection), Py_NE);
    map_set(mapped, std::move(condition), nb::int_(0));
}
nb::object labelmap_reshape(nb::object mapped, nb::object original_shape, nb::dict globals) {
    MapNamespace namespace_(std::move(globals));
    nb::object callable = namespace_.numpy("reshape");
    nb::object shape = map_add(std::move(original_shape), nb::make_tuple(nb::int_(4)));
    return map_call2(std::move(callable), std::move(mapped), std::move(shape));
}
nb::object labelmap_cyclic_texture(nb::object self, nb::object values, nb::dict globals) {
    MapNamespace namespace_(std::move(globals));
    nb::object callable = namespace_.accelerated("zero_preserving_modulo_numpy");
    nb::object length = namespace_.get("len");
    nb::object colors = map_attr(self, "colors");
    nb::object count = map_subtract(map_call1(std::move(length), std::move(colors)), nb::int_(1));
    nb::object dtype = map_attr(values, "dtype");
    nb::object background = map_attr(std::move(self), "background_value");
    return map_call4(std::move(callable), std::move(values), std::move(count),
                      std::move(dtype), std::move(background));
}
nb::object labelmap_cyclic_lookup(nb::object self, nb::object texture_dtype_values) {
    return map_item(map_attr(std::move(self), "colors"), std::move(texture_dtype_values));
}
void labelmap_cyclic_zero(nb::object mapped, nb::object texture_dtype_values) {
    nb::object condition = map_compare(std::move(texture_dtype_values), nb::int_(0), Py_EQ);
    map_set(mapped, std::move(condition), nb::int_(0));
}
nb::object labelmap_to_int(nb::object values, nb::dict globals) {
    return map_call1(MapNamespace(std::move(globals)).get("int"), std::move(values));
}
bool labelmap_is_int(nb::object values, nb::dict globals) {
    MapNamespace namespace_(std::move(globals));
    nb::object callable = namespace_.get("isinstance");
    nb::object type = namespace_.get("int");
    return map_truth(map_call2(std::move(callable), std::move(values), std::move(type)));
}
bool labelmap_is_sequence(nb::object values, nb::dict globals) {
    MapNamespace namespace_(std::move(globals));
    nb::object callable = namespace_.get("isinstance");
    nb::object first = namespace_.get("list");
    nb::object second = namespace_.get("tuple");
    nb::object type = map_checked(PyNumber_Or(first.ptr(), second.ptr()));
    first.reset(); second.reset();
    return map_truth(map_call2(std::move(callable), std::move(values), std::move(type)));
}
nb::object labelmap_to_array(nb::object values, nb::dict globals) {
    return map_call1(MapNamespace(std::move(globals)).numpy("array"), std::move(values));
}
[[noreturn]] void map_raise(nb::object error) {
    if (PyExceptionInstance_Check(error.ptr())) {
        PyErr_SetObject(reinterpret_cast<PyObject*>(Py_TYPE(error.ptr())), error.ptr());
    } else if (PyExceptionClass_Check(error.ptr())) {
        PyErr_SetNone(error.ptr());
    } else {
        PyErr_SetString(PyExc_TypeError, "exceptions must derive from BaseException");
    }
    throw nb::python_error();
}
void labelmap_direct_validate(nb::object values, nb::dict globals) {
    MapNamespace namespace_(std::move(globals));
    nb::object callable = namespace_.get("isinstance");
    nb::object array_type = namespace_.numpy("ndarray");
    bool invalid = !map_truth(map_call2(std::move(callable), values, std::move(array_type)));
    if (!invalid)
        invalid = map_contains(nb::str("fU"), map_attr(map_attr(std::move(values), "dtype"), "kind"));
    if (invalid) {
        nb::object factory = namespace_.get("TypeError");
        map_raise(map_call1(std::move(factory), nb::str("DirectLabelColormap can only be used with int")));
    }
}
bool labelmap_scalar_hidden(nb::object self, nb::object values) {
    if (!map_truth(map_attr(self, "use_selection"))) return false;
    return map_test(std::move(values), map_attr(std::move(self), "selection"), Py_NE);
}
nb::object labelmap_transparent(nb::dict globals) {
    MapNamespace namespace_(std::move(globals));
    nb::object callable = namespace_.numpy("array");
    nb::tuple color = nb::make_tuple(nb::int_(0), nb::int_(0), nb::int_(0), nb::int_(0));
    return map_call1(std::move(callable), std::move(color));
}
nb::object labelmap_scalar_color(nb::object self, nb::object values) {
    nb::object callable = map_attr(map_attr(self, "color_dict"), "get");
    nb::object default_color = map_attr(std::move(self), "default_color");
    return map_call2(std::move(callable), std::move(values), std::move(default_color));
}
nb::object labelmap_direct_texture(nb::object values, nb::object colormap, nb::dict globals) {
    MapNamespace namespace_(std::move(globals));
    nb::object callable = namespace_.accelerated("labels_raw_to_texture_direct");
    return map_call2(std::move(callable), std::move(values), std::move(colormap));
}
nb::object labelmap_precast(nb::object self, nb::object values, nb::object apply_selection) {
    nb::object callable = map_attr(std::move(self), "_map_precast");
    return map_call_kw(std::move(callable), nb::make_tuple(std::move(values)),
                        "apply_selection", std::move(apply_selection));
}
nb::object labelmap_direct_clone(nb::object self) {
    return map_call0(map_attr(std::move(self), "_cmap_without_selection"));
}
nb::object labelmap_precast_zero(nb::object values, nb::dict globals) {
    MapNamespace namespace_(std::move(globals));
    nb::object callable = namespace_.numpy("zeros");
    nb::object shape = map_add(map_attr(std::move(values), "shape"), nb::make_tuple(nb::int_(4)));
    nb::object dtype = namespace_.numpy("float32");
    return map_call_kw(std::move(callable), nb::make_tuple(std::move(shape)), "dtype", std::move(dtype));
}
nb::object labelmap_precast_colors(nb::object self, nb::object apply_selection) {
    nb::object callable = map_attr(std::move(self), "_values_mapping_to_minimum_values_set");
    return map_item(map_call1(std::move(callable), std::move(apply_selection)), nb::int_(1));
}
void labelmap_precast_fill(nb::object values, nb::object mapped, nb::object colors,
                           nb::dict globals, LabelColormapMapLocals& locals_owner) {
    MapNamespace namespace_(std::move(globals));
    nb::object callable = namespace_.numpy("ndindex");
    nb::object shape = map_attr(values, "shape");
    nb::object iterator = map_checked(PyObject_GetIter(
        map_call1(std::move(callable), std::move(shape)).ptr()));
    for (;;) {
        PyObject* next = PyIter_Next(iterator.ptr());
        if (!next) {
            if (PyErr_Occurred()) throw nb::python_error();
            break;
        }
        locals_owner.idx = nb::steal<nb::object>(next);
        locals_owner.value = map_item(values, locals_owner.idx);
        nb::object color = map_item(colors, locals_owner.value);
        map_set(mapped, locals_owner.idx, std::move(color));
    }
}

} // namespace

void bind_label_colormap_map(nb::module_& module) {
    nb::class_<LabelColormapMapLocals>(module, "LabelColormapMapLocals",
        nb::is_weak_referenceable(), nb::type_slots(map_locals_slots))
        .def_prop_ro("idx", [](const LabelColormapMapLocals& s) { return map_optional(s.idx); })
        .def_prop_ro("value", [](const LabelColormapMapLocals& s) { return map_optional(s.value); });
    module.def("labelmap_loop_locals", []() { return LabelColormapMapLocals{}; });
    module.def("labelmap_clone", &labelmap_clone, nb::arg("self").none());
    module.def("labelmap_disable_selection", &labelmap_disable_selection, nb::arg("cmap").none());
    module.def("labelmap_cache_target", &labelmap_cache_target, nb::arg("self").none(), nb::arg("data_dtype").none(), nb::arg("namespace"));
    module.def("labelmap_cache_key", &labelmap_cache_key, nb::arg("data_dtype").none(), nb::arg("target_dtype").none());
    module.def("labelmap_cache_needs_fill", &labelmap_cache_needs_fill, nb::arg("self").none(), nb::arg("key").none(), nb::arg("data_dtype").none());
    module.def("labelmap_cache_data", &labelmap_cache_data, nb::arg("data_dtype").none(), nb::arg("target_dtype").none(), nb::arg("namespace"));
    module.def("labelmap_cache_publish", &labelmap_cache_publish, nb::arg("self").none(), nb::arg("key").none(), nb::arg("data").none());
    module.def("labelmap_cache_get", &labelmap_cache_get, nb::arg("self").none(), nb::arg("key").none());
    module.def("labelmap_cache", &labelmap_cache, nb::arg("self").none(), nb::arg("values").none());
    module.def("labelmap_cache_present", &labelmap_cache_present, nb::arg("mapper").none());
    module.def("labelmap_index", &labelmap_index, nb::arg("mapper").none(), nb::arg("values").none());
    module.def("labelmap_uncached", &labelmap_uncached, nb::arg("self").none(), nb::arg("values").none());
    module.def("labelmap_is_float", &labelmap_is_float, nb::arg("values").none());
    module.def("labelmap_to_int64", &labelmap_to_int64, nb::arg("values").none(), nb::arg("namespace"));
    module.def("labelmap_mask", &labelmap_mask, nb::arg("self").none(), nb::arg("values").none(), nb::arg("mapped").none());
    module.def("labelmap_reshape", &labelmap_reshape, nb::arg("mapped").none(), nb::arg("original_shape").none(), nb::arg("namespace"));
    module.def("labelmap_cyclic_texture", &labelmap_cyclic_texture, nb::arg("self").none(), nb::arg("values").none(), nb::arg("namespace"));
    module.def("labelmap_cyclic_lookup", &labelmap_cyclic_lookup, nb::arg("self").none(), nb::arg("texture_dtype_values").none());
    module.def("labelmap_cyclic_zero", &labelmap_cyclic_zero, nb::arg("mapped").none(), nb::arg("texture_dtype_values").none());
    module.def("labelmap_to_int", &labelmap_to_int, nb::arg("values").none(), nb::arg("namespace"));
    module.def("labelmap_is_int", &labelmap_is_int, nb::arg("values").none(), nb::arg("namespace"));
    module.def("labelmap_is_sequence", &labelmap_is_sequence, nb::arg("values").none(), nb::arg("namespace"));
    module.def("labelmap_to_array", &labelmap_to_array, nb::arg("values").none(), nb::arg("namespace"));
    module.def("labelmap_direct_validate", &labelmap_direct_validate, nb::arg("values").none(), nb::arg("namespace"));
    module.def("labelmap_scalar_hidden", &labelmap_scalar_hidden, nb::arg("self").none(), nb::arg("values").none());
    module.def("labelmap_transparent", &labelmap_transparent, nb::arg("namespace"));
    module.def("labelmap_scalar_color", &labelmap_scalar_color, nb::arg("self").none(), nb::arg("values").none());
    module.def("labelmap_direct_texture", &labelmap_direct_texture, nb::arg("values").none(), nb::arg("colormap").none(), nb::arg("namespace"));
    module.def("labelmap_precast", &labelmap_precast, nb::arg("self").none(), nb::arg("values").none(), nb::arg("apply_selection").none());
    module.def("labelmap_direct_clone", &labelmap_direct_clone, nb::arg("self").none());
    module.def("labelmap_precast_zero", &labelmap_precast_zero, nb::arg("values").none(), nb::arg("namespace"));
    module.def("labelmap_precast_colors", &labelmap_precast_colors, nb::arg("self").none(), nb::arg("apply_selection").none());
    module.def("labelmap_precast_fill", &labelmap_precast_fill, nb::arg("values").none(), nb::arg("mapped").none(), nb::arg("colors").none(), nb::arg("namespace"), nb::arg("locals_owner"));
}
} // namespace nx
