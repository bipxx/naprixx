#include "annotation_bindings.hpp"

namespace nx {
namespace nb = nanobind;
namespace {

nb::object checked(PyObject* value) {
    if (!value) throw nb::python_error();
    return nb::steal<nb::object>(value);
}

nb::module_ metadata() { return nb::module_::import_("napari._pydantic_util"); }

nb::object global(nb::handle module, const char* name) {
    nb::object dictionary = module.attr("__dict__");
    PyObject* value = PyDict_GetItemString(dictionary.ptr(), name);
    if (value) return nb::borrow<nb::object>(value);
    return nb::module_::import_("builtins").attr(name);
}

bool contains(nb::handle sequence, nb::handle value) {
    int result = PySequence_Contains(sequence.ptr(), value.ptr());
    if (result < 0) throw nb::python_error();
    return result != 0;
}

// Temporary exception state at a Python protocol boundary. SetExcInfo steals
// the reference; no interpreter references live in the native iterator itself.
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

enum class Phase { initial, leaf, children };
struct InnerTypesIterator {
    Phase phase = Phase::initial;
    bool started = false;
    bool exhausted = false;
    bool running = false;
};

struct Running {
    InnerTypesIterator& state;
    explicit Running(InnerTypesIterator& value) : state(value) {
        if (state.running) throw nb::value_error("generator already executing");
        state.running = true;
    }
    ~Running() { state.running = false; }
};

nb::dict frame(nb::handle self) { return nb::cast<nb::dict>(self.attr("frame")); }

void discard_frame(nb::handle self) {
    nb::cast<InnerTypesIterator&>(self).exhausted = true;
    frame(self).clear();
}

nb::object optional_attribute(nb::handle object, const char* name) {
    PyObject* attribute = PyObject_GetAttrString(object.ptr(), name);
    if (attribute) return nb::steal<nb::object>(attribute);
    if (!PyErr_ExceptionMatches(PyExc_AttributeError)) throw nb::python_error();
    PyErr_Clear();
    return {};
}

void raise_exception(nb::handle exception, bool convert_stop_iteration) {
    if (convert_stop_iteration && PyErr_GivenExceptionMatches(exception.ptr(), PyExc_StopIteration)) {
        nb::object wrapped = checked(PyObject_CallFunction(
            PyExc_RuntimeError, "s", "generator raised StopIteration"));
        PyException_SetCause(wrapped.ptr(), exception.inc_ref().ptr());
        PyException_SetContext(wrapped.ptr(), exception.inc_ref().ptr());
        PyErr_SetObject(PyExc_RuntimeError, wrapped.ptr());
    } else {
        PyErr_SetObject(reinterpret_cast<PyObject*>(Py_TYPE(exception.ptr())), exception.ptr());
    }
    throw nb::python_error();
}

// Invalid object denotes iterator completion, including a delegated return
// value that the annotation algorithm intentionally ignores.
nb::object next_value(nb::handle iterator) {
    PyObject* value = PyIter_Next(iterator.ptr());
    if (!value && PyErr_Occurred()) throw nb::python_error();
    return nb::steal<nb::object>(value);
}

nb::object resume_delegate(nb::handle iterator, nb::handle sent) {
    if (sent.is_none()) return next_value(iterator);
    try {
        return iterator.attr("send")(sent);
    } catch (const nb::python_error& error) {
        if (!error.matches(PyExc_StopIteration)) throw;
        return {};
    }
}

nb::object advance(nb::handle self, nb::handle sent) {
    auto& state = nb::cast<InnerTypesIterator&>(self);
    nb::dict values = frame(self);
    if (state.phase == Phase::initial) {
        state.started = true;
        nb::module_ module = metadata();
        nb::object annotation = values["annotation"];
        nb::object origin = global(module, "get_origin")(annotation);
        values["origin"] = origin;
        nb::object args = global(module, "get_args")(annotation);
        values["args"] = args;
        nb::object children;
        if (contains(nb::make_tuple(global(module, "list"), global(module, "List")), origin)) {
            nb::object child = args[nb::int_(0)];
            children = nb::make_tuple(child);
        } else if (contains(nb::make_tuple(global(module, "dict"), global(module, "Dict")), origin)) {
            nb::object child = args[nb::int_(1)];
            children = nb::make_tuple(child);
        } else if (origin.is(global(module, "Annotated"))) {
            nb::object child = args[nb::int_(0)];
            children = nb::make_tuple(child);
        } else if (contains(nb::make_tuple(global(module, "UnionType"), global(module, "Union")), origin)) {
            children = args;
        } else if (!annotation.is(global(module, "NoneType"))) {
            state.phase = Phase::leaf;
            return annotation;
        } else {
            discard_frame(self);
            throw nb::stop_iteration();
        }
        values["children"] = checked(PyObject_GetIter(children.ptr()));
        state.phase = Phase::children;
    } else if (state.phase == Phase::leaf) {
        discard_frame(self);
        throw nb::stop_iteration();
    }

    nb::object delegate = values["delegate"];
    if (!delegate.is_none()) {
        nb::object value = resume_delegate(delegate, sent);
        if (value.is_valid()) return value;
        values["delegate"] = nb::none();
    }
    nb::object children = values["children"];
    for (;;) {
        nb::object child = next_value(children);
        if (!child.is_valid()) {
            discard_frame(self);
            throw nb::stop_iteration();
        }
        values["child"] = child;
        // This is the public facade (normally another native iterator), or an
        // explicitly installed plugin hook. Never call the original algorithm.
        nb::object nested = global(metadata(), "iter_inner_types")(child);
        delegate = checked(PyObject_GetIter(nested.ptr()));
        values["delegate"] = delegate;
        nb::object value = next_value(delegate);
        if (value.is_valid()) return value;
        values["delegate"] = nb::none();
    }
}

nb::object send_inner_types(nb::handle self, nb::handle sent) {
    auto& state = nb::cast<InnerTypesIterator&>(self);
    if (state.exhausted) throw nb::stop_iteration();
    if (!state.started && !sent.is_none())
        throw nb::type_error("can't send non-None value to a just-started generator");
    Running running(state);
    try {
        return advance(self, sent);
    } catch (const nb::python_error& error) {
        HandledException active(error.value());
        discard_frame(self);
        if (error.matches(PyExc_StopIteration)) raise_exception(error.value(), true);
        throw;
    }
}

void close_delegate(nb::handle self) {
    nb::dict values = frame(self);
    nb::object delegate = values["delegate"];
    if (!delegate.is_none()) {
        nb::object close = optional_attribute(delegate, "close");
        if (close.is_valid()) close();
    }
}

void close_inner_types(nb::handle self) {
    auto& state = nb::cast<InnerTypesIterator&>(self);
    if (state.exhausted) return;
    Running running(state);
    try {
        close_delegate(self);
    } catch (const nb::python_error&) {
        discard_frame(self);
        throw;
    }
    discard_frame(self);
}

nb::object normalize_throw(nb::args args) {
    if (args.size() < 1 || args.size() > 3) {
        PyErr_Format(PyExc_TypeError, "throw expected %s %d argument%s, got %zd",
                     args.size() < 1 ? "at least" : "at most",
                     args.size() < 1 ? 1 : 3, args.size() < 1 ? "" : "s", args.size());
        throw nb::python_error();
    }
    nb::object supplied = args[0];
    nb::object value = args.size() > 1 ? nb::borrow<nb::object>(args[1]) : nb::none();
    nb::object traceback = args.size() > 2 ? nb::borrow<nb::object>(args[2]) : nb::none();
    if (!traceback.is_none() && !PyTraceBack_Check(traceback.ptr()))
        throw nb::type_error("throw() third argument must be a traceback object");
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
        PyErr_Format(PyExc_TypeError,
                     "exceptions must be classes or instances deriving from BaseException, not %s",
                     Py_TYPE(supplied.ptr())->tp_name);
        throw nb::python_error();
    }
    if (!traceback.is_none() && PyException_SetTraceback(exception.ptr(), traceback.ptr()) < 0)
        throw nb::python_error();
    return exception;
}

nb::object throw_inner_types(nb::handle self, nb::args args) {
    if (args.size() < 1 || args.size() > 3) {
        normalize_throw(args); // Raises the generator protocol's argument error.
    }
    auto& state = nb::cast<InnerTypesIterator&>(self);
    if (state.exhausted) raise_exception(normalize_throw(args), false);
    Running running(state);
    try {
        bool completed = false;
        nb::object supplied = args[0];
        {
            if (PyErr_GivenExceptionMatches(supplied.ptr(), PyExc_GeneratorExit)) {
                close_delegate(self);
            } else {
                nb::object delegate = frame(self)["delegate"];
                if (!delegate.is_none()) {
                    nb::object handler = optional_attribute(delegate, "throw");
                    if (handler.is_valid()) {
                        try {
                            // CPython forwards the caller's original throw
                            // signature to a non-generator protocol object.
                            return checked(PyObject_CallObject(handler.ptr(), args.ptr()));
                        } catch (const nb::python_error& error) {
                            if (!error.matches(PyExc_StopIteration)) throw;
                            completed = true;
                            frame(self)["delegate"] = nb::none();
                        }
                    }
                }
            }
        }
        if (completed) return advance(self, nb::none());
        nb::object exception = normalize_throw(args);
        discard_frame(self);
        raise_exception(exception, true);
    } catch (const nb::python_error& error) {
        HandledException active(error.value());
        discard_frame(self);
        if (error.matches(PyExc_StopIteration)) raise_exception(error.value(), true);
        throw;
    }
}

nb::object inner_types(nb::handle annotation) {
    nb::object result = nb::cast(InnerTypesIterator{});
    nb::dict values;
    values["annotation"] = annotation;
    values["delegate"] = nb::none();
    result.attr("frame") = values;
    return result;
}

nb::object inner_type(nb::handle annotation) {
    nb::module_ module = metadata();
    nb::object iterator = global(module, "iter_inner_types")(annotation);
    nb::object values = global(module, "tuple")(iterator);
    nb::object union_type = global(module, "Union");
    return checked(PyObject_GetItem(union_type.ptr(), values.ptr()));
}

nb::object outer_type(nb::handle annotation) {
    nb::module_ module = metadata();
    nb::object origin = global(module, "get_origin")(annotation);
    nb::object args = global(module, "get_args")(annotation);
    if (contains(nb::make_tuple(global(module, "UnionType"), global(module, "Union")), origin)) {
        nb::list concrete;
        args = global(module, "get_args")(annotation);
        for (nb::handle value : nb::iter(args))
            if (!value.is(global(module, "NoneType"))) concrete.append(value);
        if (nb::len(concrete) == 1) {
            nb::object child = concrete[0];
            return global(module, "get_outer_type")(child);
        }
        return nb::none();
    }
    return origin.is_none() ? nb::borrow<nb::object>(annotation) : origin;
}

} // namespace

void bind_annotation_helpers(nb::module_& module) {
    nb::class_<InnerTypesIterator>(module, "InnerTypesIterator", nb::dynamic_attr(),
                                   nb::is_weak_referenceable())
        .def("__iter__", [](nb::object self) { return self; })
        .def("__next__", [](nb::handle self) { return send_inner_types(self, nb::none()); })
        .def("send", send_inner_types, nb::arg("value").none())
        .def("throw", throw_inner_types)
        .def("close", close_inner_types);
    module.def("iter_inner_types", inner_types, nb::arg("annotation").none());
    module.def("get_inner_type", inner_type, nb::arg("annotation").none());
    module.def("get_outer_type", outer_type, nb::arg("annotation").none());
    module.def("is_list_type", [](nb::handle annotation) {
        nb::module_ module = metadata();
        nb::object outer = global(module, "get_outer_type")(annotation);
        return outer.is(global(module, "list"));
    }, nb::arg("annotation").none());
}
} // namespace nx
