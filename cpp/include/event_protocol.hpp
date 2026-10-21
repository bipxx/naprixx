#pragma once

#include <nanobind/nanobind.h>
#include <initializer_list>
#include <string>

namespace nx::events {
namespace nb = nanobind;

inline nb::object checked(PyObject* value) {
    if (!value) throw nb::python_error();
    return nb::steal<nb::object>(value);
}

inline nb::object global(const char* name) {
    nb::module_ module = nb::module_::import_("napari.utils.events.event");
    PyObject* values = PyModule_GetDict(module.ptr());
    if (!values) throw nb::python_error();
    PyObject* value = PyDict_GetItemString(values, name);
    if (value) return nb::borrow<nb::object>(value);
    return nb::module_::import_("builtins").attr(name);
}

inline bool truth(nb::handle value) {
    const int result = PyObject_IsTrue(value.ptr());
    if (result < 0) throw nb::python_error();
    return result != 0;
}

inline bool instance(nb::handle value, nb::handle type) {
    const int result = PyObject_IsInstance(value.ptr(), type.ptr());
    if (result < 0) throw nb::python_error();
    return result != 0;
}

inline bool contains(nb::handle values, nb::handle value) {
    const int result = PySequence_Contains(values.ptr(), value.ptr());
    if (result < 0) throw nb::python_error();
    return result != 0;
}

inline bool compare(nb::handle left, nb::handle right, int operation = Py_EQ) {
    return truth(checked(PyObject_RichCompare(left.ptr(), right.ptr(), operation)));
}

inline nb::object item(nb::handle value, nb::handle key) {
    return checked(PyObject_GetItem(value.ptr(), key.ptr()));
}

inline nb::object index(nb::handle value, Py_ssize_t key) {
    return item(value, nb::int_(key));
}

inline void set_item(nb::handle value, nb::handle key, nb::handle replacement) {
    if (PyObject_SetItem(value.ptr(), key.ptr(), replacement.ptr()) < 0)
        throw nb::python_error();
}

inline void set_attr(nb::handle value, const char* name, nb::handle replacement) {
    if (PyObject_SetAttrString(value.ptr(), name, replacement.ptr()) < 0)
        throw nb::python_error();
}

inline nb::object optional_attribute(nb::handle value, const char* name) {
    nb::object result = nb::steal<nb::object>(PyObject_GetAttrString(value.ptr(), name));
    if (!result.is_valid()) {
        if (!PyErr_ExceptionMatches(PyExc_AttributeError)) throw nb::python_error();
        PyErr_Clear();
    }
    return result;
}

inline nb::object slice(nb::handle value, nb::handle start, nb::handle stop,
                        nb::handle step = nb::none()) {
    nb::object key = checked(PySlice_New(start.ptr(), stop.ptr(), step.ptr()));
    return item(value, key);
}

inline nb::object add(nb::handle left, nb::handle right) {
    return checked(PyNumber_Add(left.ptr(), right.ptr()));
}

inline nb::object subtract(nb::handle left, nb::handle right) {
    return checked(PyNumber_Subtract(left.ptr(), right.ptr()));
}

inline nb::object formatted(nb::handle value) {
    return checked(PyObject_Format(value.ptr(), nb::str("").ptr()));
}

inline nb::str message(std::initializer_list<nb::object> parts) {
    nb::list values;
    for (const auto& part : parts) values.append(part);
    return nb::cast<nb::str>(nb::str("").attr("join")(values));
}

[[noreturn]] inline void error(PyObject* type, nb::handle text) {
    PyErr_SetObject(type, text.ptr());
    throw nb::python_error();
}

inline void require(bool condition) {
    if (!condition) {
        PyErr_SetNone(PyExc_AssertionError);
        throw nb::python_error();
    }
}

inline nb::object call(nb::handle function, nb::tuple args, nb::handle kwargs) {
    return checked(PyObject_Call(function.ptr(), args.ptr(), kwargs.ptr()));
}

inline nb::object callable_name(nb::handle function) {
    nb::object name = optional_attribute(function, "__qualname__");
    if (!name.is_valid()) return checked(PyObject_Str(function.ptr()));
    nb::object module = optional_attribute(function, "__module__");
    if (module.is_valid() && !module.is_none()) {
        const int external = PyObject_RichCompareBool(module.ptr(), nb::str("builtins").ptr(), Py_NE);
        if (external < 0) throw nb::python_error();
        if (external) return checked(PyUnicode_FromFormat("%S.%S()", module.ptr(), name.ptr()));
    }
    return checked(PyUnicode_FromFormat("%S()", name.ptr()));
}

inline nb::dict keyword_mapping(nb::handle function, nb::handle mapping) {
    nb::dict result;
    try {
        if (PyDict_Check(mapping.ptr()) && Py_TYPE(mapping.ptr())->tp_iter == PyDict_Type.tp_iter) {
            if (PyDict_Update(result.ptr(), mapping.ptr()) < 0) throw nb::python_error();
        } else {
            nb::object keys = checked(PyMapping_Keys(mapping.ptr()));
            for (nb::handle key : nb::iter(keys)) {
                const int duplicate = PyDict_Contains(result.ptr(), key.ptr());
                if (duplicate < 0) throw nb::python_error();
                if (duplicate) error(PyExc_KeyError, key);
                nb::object value = item(mapping, key);
                if (PyDict_SetItem(result.ptr(), key.ptr(), value.ptr()) < 0) throw nb::python_error();
            }
        }
    } catch (const nb::python_error& exception) {
        if (exception.matches(PyExc_AttributeError)) {
            nb::object name = callable_name(function);
            PyErr_Format(PyExc_TypeError, "%U argument after ** must be a mapping, not %.200s",
                         name.ptr(), Py_TYPE(mapping.ptr())->tp_name);
            throw nb::python_error();
        }
        if (exception.matches(PyExc_KeyError)) {
            nb::object args = exception.value().attr("args");
            if (PyTuple_Check(args.ptr()) && PyTuple_Size(args.ptr()) == 1) {
                nb::object name = callable_name(function);
                PyErr_Format(PyExc_TypeError, "%U got multiple values for keyword argument '%S'",
                             name.ptr(), PyTuple_GetItem(args.ptr(), 0));
                throw nb::python_error();
            }
        }
        throw;
    }
    return result;
}

inline nb::tuple pair(nb::handle supplied) {
    nb::object iterator = nb::steal<nb::object>(PyObject_GetIter(supplied.ptr()));
    if (!iterator.is_valid()) {
        if (PyErr_ExceptionMatches(PyExc_TypeError) && !Py_TYPE(supplied.ptr())->tp_iter
            && !PySequence_Check(supplied.ptr()))
            PyErr_Format(PyExc_TypeError, "cannot unpack non-iterable %.200s object",
                         Py_TYPE(supplied.ptr())->tp_name);
        throw nb::python_error();
    }
    nb::object first = nb::steal<nb::object>(PyIter_Next(iterator.ptr()));
    if (!first.is_valid()) {
        if (PyErr_Occurred()) throw nb::python_error();
        error(PyExc_ValueError, nb::str("not enough values to unpack (expected 2, got 0)"));
    }
    nb::object second = nb::steal<nb::object>(PyIter_Next(iterator.ptr()));
    if (!second.is_valid()) {
        if (PyErr_Occurred()) throw nb::python_error();
        error(PyExc_ValueError, nb::str("not enough values to unpack (expected 2, got 1)"));
    }
    nb::object extra = nb::steal<nb::object>(PyIter_Next(iterator.ptr()));
    if (extra.is_valid())
        error(PyExc_ValueError, nb::str("too many values to unpack (expected 2)"));
    if (PyErr_Occurred()) throw nb::python_error();
    return nb::make_tuple(first, second);
}

struct HandledException {
    PyObject* previous = PyErr_GetHandledException();
    explicit HandledException(nb::handle exception) {
        Py_INCREF(exception.ptr());
        PyErr_SetExcInfo(nullptr, exception.ptr(), nullptr);
    }
    ~HandledException() { PyErr_SetExcInfo(nullptr, previous, nullptr); }
    HandledException(const HandledException&) = delete;
    HandledException& operator=(const HandledException&) = delete;
};
} // namespace nx::events
