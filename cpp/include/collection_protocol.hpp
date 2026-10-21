#pragma once

#include "event_protocol.hpp"
#include <nanobind/nanobind.h>

namespace nx::collections {
namespace nb = nanobind;
using events::checked;
using events::truth;
using events::instance;
using events::contains;
using events::compare;
using events::item;
using events::set_item;
using events::set_attr;
using events::optional_attribute;
using events::add;
using events::subtract;
using events::formatted;
using events::message;
using events::error;
using events::pair;
using events::HandledException;

inline nb::object global(const char* module_name, const char* name) {
    nb::module_ module = nb::module_::import_(module_name);
    PyObject* value = PyDict_GetItemString(PyModule_GetDict(module.ptr()), name);
    return value ? nb::borrow<nb::object>(value) : nb::module_::import_("builtins").attr(name);
}
inline nb::object type_of(nb::handle value) {
    return nb::borrow<nb::object>(reinterpret_cast<PyObject*>(Py_TYPE(value.ptr())));
}
inline nb::object object_repr(nb::handle value) { return checked(PyObject_Repr(value.ptr())); }
inline nb::object length(nb::handle value) { return nb::int_(nb::len(value)); }
inline nb::object inplace_add(nb::handle left, nb::handle right) { return checked(PyNumber_InPlaceAdd(left.ptr(), right.ptr())); }
inline nb::object inplace_subtract(nb::handle left, nb::handle right) { return checked(PyNumber_InPlaceSubtract(left.ptr(), right.ptr())); }
inline nb::object next_value(nb::handle iterator) {
    PyObject* value = PyIter_Next(iterator.ptr());
    if (!value && PyErr_Occurred()) throw nb::python_error();
    return nb::steal<nb::object>(value);
}

inline nb::object context_method(nb::handle context, const char* name, bool exit) {
    PyTypeObject* type = Py_TYPE(context.ptr());
    nb::tuple mro = nb::borrow<nb::tuple>(type->tp_mro);
    for (nb::handle base : mro) {
#if PY_VERSION_HEX >= 0x030C0000
        // PyType_GetDict returns a new reference, unlike the legacy tp_dict.
        nb::object dictionary = nb::steal<nb::object>(
            PyType_GetDict(reinterpret_cast<PyTypeObject*>(base.ptr())));
#else
        nb::object dictionary = nb::borrow<nb::object>(
            reinterpret_cast<PyTypeObject*>(base.ptr())->tp_dict);
#endif
        if (!dictionary.is_valid()) continue;
        PyObject* found = PyDict_GetItemString(dictionary.ptr(), name);
        if (!found) continue;
        nb::object method = nb::borrow<nb::object>(found);
        descrgetfunc bind = Py_TYPE(found)->tp_descr_get;
        return bind ? checked(bind(found, context.ptr(), reinterpret_cast<PyObject*>(type)))
                    : method;
    }
    PyErr_Format(PyExc_TypeError, "'%s' object does not support the context manager protocol%s",
                 type->tp_name, exit ? " (missed __exit__ method)" : "");
    throw nb::python_error();
}
template <typename Function> void with_context(nb::handle context, Function body) {
    nb::object enter = context_method(context, "__enter__", false);
    nb::object exit = context_method(context, "__exit__", true);
    enter();
    try { body(); }
    catch (const nb::python_error& original) {
        HandledException active(original.value());
        nb::object traceback = original.traceback();
        if (!traceback.is_valid()) traceback = nb::none();
        try {
            if (!truth(exit(original.type(), original.value(), traceback))) throw;
        } catch (const nb::python_error& cleanup) {
            if (cleanup.value().ptr() != original.value().ptr())
                PyException_SetContext(cleanup.value().ptr(), original.value().inc_ref().ptr());
            throw;
        }
        return;
    }
    exit(nb::none(), nb::none(), nb::none());
}

[[noreturn]] inline void raise_exception(nb::handle exception, bool convert_stop) {
    if (convert_stop && PyErr_GivenExceptionMatches(exception.ptr(), PyExc_StopIteration)) {
        nb::object converted = checked(PyObject_CallFunction(PyExc_RuntimeError, "s", "generator raised StopIteration"));
        PyException_SetCause(converted.ptr(), exception.inc_ref().ptr());
        PyException_SetContext(converted.ptr(), exception.inc_ref().ptr());
        PyErr_SetObject(PyExc_RuntimeError, converted.ptr());
    } else PyErr_SetObject(reinterpret_cast<PyObject*>(Py_TYPE(exception.ptr())), exception.ptr());
    throw nb::python_error();
}
inline nb::object normalize_throw(nb::args args) {
    if (args.size() < 1 || args.size() > 3) {
        PyErr_Format(PyExc_TypeError, "throw expected %s %d argument%s, got %zd",
                     args.size() < 1 ? "at least" : "at most", args.size() < 1 ? 1 : 3,
                     args.size() < 1 ? "" : "s", args.size());
        throw nb::python_error();
    }
    nb::object supplied = args[0];
    nb::object value = args.size() > 1 ? nb::borrow<nb::object>(args[1]) : nb::none();
    nb::object traceback = args.size() > 2 ? nb::borrow<nb::object>(args[2]) : nb::none();
    if (!traceback.is_none() && !PyTraceBack_Check(traceback.ptr())) throw nb::type_error("throw() third argument must be a traceback object");
    nb::object exception;
    if (PyExceptionInstance_Check(supplied.ptr())) {
        if (!value.is_none()) throw nb::type_error("instance exception may not have a separate value");
        exception = supplied;
    } else if (PyExceptionClass_Check(supplied.ptr())) {
        PyErr_SetObject(supplied.ptr(), value.ptr());
        PyObject *type = nullptr, *normalized = nullptr, *trace = nullptr;
        PyErr_Fetch(&type, &normalized, &trace);
        PyErr_NormalizeException(&type, &normalized, &trace);
        nb::object owned_type = nb::steal<nb::object>(type);
        exception = nb::steal<nb::object>(normalized);
        Py_XDECREF(trace);
        if (PyErr_Occurred()) throw nb::python_error();
    } else {
        PyErr_Format(PyExc_TypeError, "exceptions must be classes or instances deriving from BaseException, not %s", Py_TYPE(supplied.ptr())->tp_name);
        throw nb::python_error();
    }
    if (!traceback.is_none() && PyException_SetTraceback(exception.ptr(), traceback.ptr()) < 0) throw nb::python_error();
    return exception;
}

struct IteratorState { bool started = false, exhausted = false, running = false; };
struct Running {
    IteratorState& state;
    explicit Running(IteratorState& value) : state(value) {
        if (state.running) throw nb::value_error("generator already executing");
        state.running = true;
    }
    ~Running() { state.running = false; }
};
inline nb::dict frame(nb::handle self) { return nb::cast<nb::dict>(self.attr("frame")); }
inline void discard(nb::handle self, IteratorState& state) { state.exhausted = true; frame(self).clear(); }

template <typename State> void close(nb::handle self) {
    auto& state = nb::cast<State&>(self);
    if (state.exhausted) return;
    Running active(state);
    discard(self, state);
}
template <typename State> nb::object throw_into(nb::handle self, nb::args args) {
    auto& state = nb::cast<State&>(self);
    Running active(state);
    nb::object exception = normalize_throw(args);
    const bool exhausted = state.exhausted;
    discard(self, state);
    raise_exception(exception, !exhausted);
}
template <typename State, typename Advance> nb::object resume(nb::handle self, nb::object sent, Advance advance) {
    auto& state = nb::cast<State&>(self);
    if (state.exhausted) throw nb::stop_iteration();
    Running active(state);
    if (!state.started && !sent.is_none()) throw nb::type_error("can't send non-None value to a just-started generator");
    nb::object result;
    try { result = advance(self, state); }
    catch (const nb::python_error& failure) {
        discard(self, state);
        if (failure.matches(PyExc_StopIteration)) raise_exception(failure.value(), true);
        throw;
    }
    if (!result.is_valid()) { discard(self, state); throw nb::stop_iteration(); }
    return result;
}
} // namespace nx::collections
