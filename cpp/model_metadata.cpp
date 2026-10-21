#include "model_metadata.hpp"

namespace nx {
namespace nb = nanobind;
namespace {

nb::object checked(PyObject* value) {
    if (!value) throw nb::python_error();
    return nb::steal<nb::object>(value);
}

nb::object global(const char* name) {
    // Resolve the public module aliases each time, preserving plugin overrides.
    // No interpreter-owned references are retained in native static state.
    nb::module_ module = nb::module_::import_("napari.utils.events.evented_model");
    nb::dict namespace_values = module.attr("__dict__");
    PyObject* value = PyDict_GetItemString(namespace_values.ptr(), name);
    if (value) return nb::borrow<nb::object>(value);
    return nb::module_::import_("builtins").attr(name);
}

bool truth(nb::handle value) {
    const int result = PyObject_IsTrue(value.ptr());
    if (result < 0) throw nb::python_error();
    return result != 0;
}

bool contains(nb::handle values, nb::handle key) {
    const int result = PySequence_Contains(values.ptr(), key.ptr());
    if (result < 0) throw nb::python_error();
    return result != 0;
}

bool instance(nb::handle value, nb::handle type) {
    const int result = PyObject_IsInstance(value.ptr(), type.ptr());
    if (result < 0) throw nb::python_error();
    return result != 0;
}

nb::object optional_attribute(nb::handle value, const char* name) {
    nb::object result = nb::steal<nb::object>(PyObject_GetAttrString(value.ptr(), name));
    if (!result.is_valid()) {
        if (!PyErr_ExceptionMatches(PyExc_AttributeError)) throw nb::python_error();
        PyErr_Clear();
    }
    return result;
}

void set_attribute(nb::handle target, const char* name, nb::handle value) {
    if (PyObject_SetAttrString(target.ptr(), name, value.ptr()) < 0)
        throw nb::python_error();
}

void set_item(nb::handle target, nb::handle key, nb::handle value) {
    if (PyObject_SetItem(target.ptr(), key.ptr(), value.ptr()) < 0)
        throw nb::python_error();
}

nb::tuple pair(nb::handle supplied) {
    nb::object iterator = nb::steal<nb::object>(PyObject_GetIter(supplied.ptr()));
    if (!iterator.is_valid()) {
        if (PyErr_ExceptionMatches(PyExc_TypeError) && !Py_TYPE(supplied.ptr())->tp_iter
            && !PySequence_Check(supplied.ptr())) {
            PyErr_Format(PyExc_TypeError, "cannot unpack non-iterable %.200s object",
                         Py_TYPE(supplied.ptr())->tp_name);
        }
        throw nb::python_error();
    }
    nb::object first = nb::steal<nb::object>(PyIter_Next(iterator.ptr()));
    if (!first.is_valid()) {
        if (PyErr_Occurred()) throw nb::python_error();
        PyErr_SetString(PyExc_ValueError, "not enough values to unpack (expected 2, got 0)");
        throw nb::python_error();
    }
    nb::object second = nb::steal<nb::object>(PyIter_Next(iterator.ptr()));
    if (!second.is_valid()) {
        if (PyErr_Occurred()) throw nb::python_error();
        PyErr_SetString(PyExc_ValueError, "not enough values to unpack (expected 2, got 1)");
        throw nb::python_error();
    }
    nb::object extra = nb::steal<nb::object>(PyIter_Next(iterator.ptr()));
    if (extra.is_valid()) {
        PyErr_SetString(PyExc_ValueError, "too many values to unpack (expected 2)");
        throw nb::python_error();
    }
    if (PyErr_Occurred()) throw nb::python_error();
    return nb::make_tuple(first, second);
}

nb::dict mapping_copy(nb::handle supplied) {
    nb::dict result;
    // This is the mapping-unpack protocol used by {**mapping}, including its
    // dict-subclass behavior, rather than the iterable accepted by dict().
    if (PyDict_Update(result.ptr(), supplied.ptr()) < 0) {
        if (PyErr_ExceptionMatches(PyExc_AttributeError)) {
            PyErr_Clear();
            PyErr_Format(PyExc_TypeError, "'%s' object is not a mapping",
                         Py_TYPE(supplied.ptr())->tp_name);
        }
        throw nb::python_error();
    }
    return result;
}

void add_dependency(nb::handle dependencies, nb::handle field, nb::handle name) {
    nb::object setdefault = dependencies.attr("setdefault");
    nb::object empty = global("set")();
    nb::object values = setdefault(field, empty);
    values.attr("add")(name);
}

void update_from_code(nb::handle cls, nb::handle name, nb::handle property,
                      nb::handle dependencies, nb::handle previous) {
    nb::object visited = nb::borrow<nb::object>(previous);
    nb::object getter = property.attr("fget");
    nb::object code = getter.attr("__code__");
    nb::object names = code.attr("co_names");
    for (nb::handle candidate : nb::iter(names)) {
        nb::object fields = cls.attr("model_fields");
        if (contains(fields, candidate)) {
            add_dependency(dependencies, candidate, name);
        } else {
            nb::object properties = cls.attr("__properties__");
            if (contains(properties, candidate) && !contains(visited, candidate)) {
                visited = checked(PyNumber_Add(visited.ptr(), nb::make_tuple(candidate).ptr()));
                properties = cls.attr("__properties__");
                nb::object child = checked(PyObject_GetItem(properties.ptr(), candidate.ptr()));
                // Recursive calls use the public helper alias, just as the
                // original implementation does. An installed plugin hook is
                // a dependency adapter, not an original-algorithm fallback.
                global("_update_dependents_from_property_code")(
                    cls, name, child, dependencies, visited);
            }
        }
    }
}

nb::dict field_dependents(nb::handle cls) {
    nb::object properties = cls.attr("__properties__");
    if (!truth(properties)) return nb::dict();
    nb::dict dependencies;
    nb::object config = cls.attr("model_config");
    nb::object explicit_dependencies = config.attr("get")("dependencies");
    if (truth(explicit_dependencies)) {
        nb::object items = explicit_dependencies.attr("items")();
        for (nb::handle supplied : nb::iter(items)) {
            nb::tuple entry = pair(supplied);
            nb::object name = entry[0];
            nb::object fields = entry[1];
            properties = cls.attr("__properties__");
            if (!contains(properties, name)) {
                nb::object prefix = nb::str("Fields with dependencies must be properties. ");
                nb::object represented = checked(PyObject_Repr(name.ptr()));
                nb::object message = checked(PyUnicode_Concat(prefix.ptr(), represented.ptr()));
                message = checked(PyUnicode_Concat(message.ptr(), nb::str(" is not.").ptr()));
                PyErr_SetObject(PyExc_ValueError, message.ptr());
                throw nb::python_error();
            }
            for (nb::handle field : nb::iter(fields)) {
                nb::object model_fields = cls.attr("model_fields");
                if (!contains(model_fields, field)) {
                    nb::object warn = global("warnings").attr("warn");
                    nb::object formatted = checked(PyObject_Format(field.ptr(), nb::str("").ptr()));
                    nb::object message = checked(PyUnicode_Concat(
                        nb::str("Unrecognized field dependency: ").ptr(), formatted.ptr()));
                    warn(message);
                }
                add_dependency(dependencies, field, name);
            }
        }
    } else {
        properties = cls.attr("__properties__");
        nb::object items = properties.attr("items")();
        for (nb::handle supplied : nb::iter(items)) {
            nb::tuple entry = pair(supplied);
            nb::object name = entry[0];
            nb::object property = entry[1];
            global("_update_dependents_from_property_code")(cls, name, property, dependencies);
        }
    }
    return dependencies;
}

nb::object return_annotation(nb::handle property) {
    nb::object getter = property.attr("fget");
    nb::object annotations = getter.attr("__annotations__");
    return checked(PyObject_GetItem(annotations.ptr(), nb::str("return").ptr()));
}

void prepare_class(nb::handle cls, nb::handle namespace_values) {
    nb::object getattr = global("getattr");
    nb::object empty = global("set")();
    nb::object excluded = getattr(cls, "__non_evented_properties__", empty);
    nb::object inherited = global("getattr")(cls, "__eq_operators__", nb::dict());
    nb::dict operators = mapping_copy(inherited);
    set_attribute(cls, "__eq_operators__", operators);
    nb::object fields = cls.attr("model_fields");
    nb::object items = fields.attr("items")();
    for (nb::handle supplied : nb::iter(items)) {
        nb::tuple entry = pair(supplied);
        nb::object name = entry[0];
        nb::object field = entry[1];
        nb::object get_outer_type = global("get_outer_type");
        nb::object annotation = field.attr("annotation");
        nb::object type = get_outer_type(annotation);
        nb::object comparator = global("pick_equality_operator")(type);
        nb::object current_operators = cls.attr("__eq_operators__");
        set_item(current_operators, name, comparator);
        if (optional_attribute(type, "_json_encode").is_valid()) {
            // hasattr and the subsequent attribute read are separate protocol
            // operations; descriptor side effects and errors must be preserved.
            nb::object encoder = type.attr("_json_encode");
            nb::object config = cls.attr("model_config");
            nb::object encoders = checked(PyObject_GetItem(config.ptr(), nb::str("json_encoders").ptr()));
            set_item(encoders, type, encoder);
            nb::object base = global("EventedModel");
            config = base.attr("model_config");
            encoders = checked(PyObject_GetItem(config.ptr(), nb::str("json_encoders").ptr()));
            set_item(encoders, type, encoder);
        }
    }
    inherited = global("getattr")(cls, "__properties__", nb::dict());
    nb::dict properties = mapping_copy(inherited);
    set_attribute(cls, "__properties__", properties);
    items = namespace_values.attr("items")();
    for (nb::handle supplied : nb::iter(items)) {
        nb::tuple entry = pair(supplied);
        nb::object name = entry[0];
        nb::object property = entry[1];
        if (contains(excluded, name)) continue;
        if (!instance(property, global("property"))) continue;
        nb::object current_properties = cls.attr("__properties__");
        set_item(current_properties, name, property);
        nb::object getter = property.attr("fget");
        if (!optional_attribute(getter, "__annotations__").is_valid()) continue;
        getter = property.attr("fget");
        nb::object annotations = getter.attr("__annotations__");
        if (!contains(annotations, nb::str("return"))) continue;
        nb::object return_type = return_annotation(property);
        nb::object string_type = global("str");
        if (instance(return_type, string_type)) continue;
        nb::object comparator = global("pick_equality_operator")(return_annotation(property));
        nb::object current_operators = cls.attr("__eq_operators__");
        set_item(current_operators, name, comparator);
    }
    nb::object dependents = global("_get_field_dependents")(cls);
    set_attribute(cls, "__field_dependents__", dependents);
}

} // namespace

void bind_model_metadata(nb::module_& module) {
    module.def("model_prepare_class", prepare_class, nb::arg("cls").none(),
               nb::arg("namespace").none());
    module.def("model_field_dependents", field_dependents, nb::arg("cls").none());
    module.def("model_update_property_dependents", update_from_code, nb::arg("cls").none(),
               nb::arg("prop_name").none(), nb::arg("prop").none(), nb::arg("deps").none(),
               nb::arg("visited").none() = nb::tuple());
}
} // namespace nx
