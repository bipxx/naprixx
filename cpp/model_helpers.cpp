#include "model_helpers.hpp"
#include "annotation_bindings.hpp"
#include "model_metadata.hpp"

namespace nx {
namespace nb = nanobind;
namespace {

nb::object checked(PyObject* value) {
    if (!value) throw nb::python_error();
    return nb::steal<nb::object>(value);
}

bool truth(nb::handle value) {
    const int result = PyObject_IsTrue(value.ptr());
    if (result < 0) throw nb::python_error();
    return result != 0;
}

bool instance(nb::handle value, nb::handle type) {
    const int result = PyObject_IsInstance(value.ptr(), type.ptr());
    if (result < 0) throw nb::python_error();
    return result != 0;
}

bool contains(nb::handle values, nb::handle key) {
    const int result = PySequence_Contains(values.ptr(), key.ptr());
    if (result < 0) throw nb::python_error();
    return result != 0;
}

void set_attr(nb::handle target, nb::handle name, nb::handle value) {
    if (PyObject_SetAttr(target.ptr(), name.ptr(), value.ptr()) < 0)
        throw nb::python_error();
}

void set_item(nb::handle target, nb::handle name, nb::handle value) {
    if (PyObject_SetItem(target.ptr(), name.ptr(), value.ptr()) < 0)
        throw nb::python_error();
}

nb::object evented_type() {
    return nb::module_::import_("napari.utils.events.evented_model").attr("EventedModel");
}

nb::object fields(nb::handle model) {
    nb::object type = model.attr("__class__");
    return type.attr("model_fields");
}

nb::tuple pair(nb::handle value) {
    nb::object iterator = nb::steal<nb::object>(PyObject_GetIter(value.ptr()));
    if (!iterator.is_valid()) {
        if (PyErr_ExceptionMatches(PyExc_TypeError) && !Py_TYPE(value.ptr())->tp_iter
            && !PySequence_Check(value.ptr())) {
            PyErr_Format(PyExc_TypeError, "cannot unpack non-iterable %.200s object",
                         Py_TYPE(value.ptr())->tp_name);
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

nb::object context_method(nb::handle context, const char* name, bool exit) {
    // A with statement looks up special methods on the actual type, binds
    // descriptors, and captures __exit__ before invoking __enter__. Instance
    // attributes and metaclass attribute hooks do not participate.
    PyTypeObject* type = Py_TYPE(context.ptr());
    nb::tuple mro = nb::borrow<nb::tuple>(type->tp_mro);
    for (nb::handle base : mro) {
#if PY_VERSION_HEX >= 0x030C0000
        // Static builtin type namespaces are interpreter-local since 3.12;
        // their raw tp_dict may be null even though the namespace exists.
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

void initialize_events(nb::handle model) {
    nb::object events = model.attr("_events");
    events.attr("source") = model;
    nb::list names;
    nb::object model_fields = fields(model);
    nb::object items = model_fields.attr("items")();
    for (nb::handle item : nb::iter(items)) {
        nb::tuple values = pair(item);
        nb::object name = values[0];
        nb::object field = values[1];
        if (!truth(field.attr("frozen"))) names.append(name);
    }
    nb::object properties = nb::module_::import_("builtins").attr("list")(
        model.attr("__properties__"));
    for (nb::handle name : nb::iter(properties)) names.append(name);
    nb::object keywords = nb::module_::import_("builtins").attr("dict")
                              .attr("fromkeys")(names);
    events = model.attr("_events");
    nb::object add = events.attr("add");
    nb::tuple args;
    checked(PyObject_Call(add.ptr(), args.ptr(), keywords.ptr()));
    // Validators may recreate children; preserve the public override hook.
    model.attr("_reset_event_source")();
}

void reset_sources(nb::handle model) {
    nb::object events = model.attr("events");
    events.attr("source") = model;
    nb::object model_fields = fields(model);
    for (nb::handle name : nb::iter(model_fields)) {
        nb::object child = checked(PyObject_GetAttr(model.ptr(), name.ptr()));
        if (instance(child, evented_type())) {
            child.attr("_reset_event_source")();
        } else {
            events = model.attr("events");
            nb::object emitters = events.attr("emitters");
            if (contains(emitters, name)) {
                events = model.attr("events");
                nb::object emitter = checked(PyObject_GetAttr(events.ptr(), name.ptr()));
                emitter.attr("source") = model;
            }
        }
    }
}

nb::dict defaults(nb::handle object) {
    nb::object base = nb::module_::import_("napari.utils.events.evented_model").attr("BaseModel");
    nb::object model_fields = instance(object, base) ? fields(object)
                                                   : nb::object(object.attr("model_fields"));
    nb::object metaclass = nb::module_::import_("napari.utils.events.evented_model")
                               .attr("ModelMetaclass");
    nb::object items = model_fields.attr("items")();
    nb::dict result;
    for (nb::handle item : nb::iter(items)) {
        nb::tuple values = pair(item);
        nb::object name = values[0];
        nb::object field = values[1];
        if (truth(field.attr("exclude"))) continue;
        nb::object value = field.attr("get_default")();
        nb::object annotation = field.attr("annotation");
        nb::object type = nb::module_::import_("napari.utils.events.evented_model")
                              .attr("get_inner_type")(annotation);
        if (value.is_none()) {
            if (instance(type, metaclass)) {
                // Retain the public helper override used by custom schema adapters.
                nb::object helper = nb::module_::import_("napari.utils.events.evented_model")
                                        .attr("get_defaults");
                value = helper(type);
            } else {
                try {
                    value = field.attr("get_default")(nb::arg("call_default_factory") = true,
                                                       nb::arg("validated_data") = nb::dict());
                } catch (const nb::python_error& error) {
                    if (!error.matches(PyExc_TypeError)) throw;
                    value = field.attr("get_default")(nb::arg("call_default_factory") = true);
                }
            }
        }
        result[name] = value;
    }
    return result;
}

void reset_model(nb::handle model) {
    nb::object default_values = model.attr("_defaults");
    nb::object items = default_values.attr("items")();
    for (nb::handle item : nb::iter(items)) {
        nb::tuple values = pair(item);
        nb::object name = values[0];
        nb::object value = values[1];
        nb::object child = checked(PyObject_GetAttr(model.ptr(), name.ptr()));
        if (instance(child, evented_type())) {
            child = checked(PyObject_GetAttr(model.ptr(), name.ptr()));
            child.attr("reset")();
        } else {
            nb::object config = model.attr("model_config");
            if (truth(config.attr("get")("frozen", false))) continue;
            nb::object model_fields = fields(model);
            nb::object field = checked(PyObject_GetItem(model_fields.ptr(), name.ptr()));
            if (!truth(field.attr("frozen"))) set_attr(model, name, value);
        }
    }
}

void update_model(nb::handle model, nb::handle supplied, nb::handle recurse) {
    nb::object type = model.attr("__class__");
    nb::object values = nb::borrow<nb::object>(supplied);
    if (instance(values, type)) values = values.attr("model_dump")();
    nb::object dictionary = nb::module_::import_("builtins").attr("dict");
    if (!instance(values, dictionary)) {
        nb::object actual_type = nb::module_::import_("builtins").attr("type")(values);
        nb::str empty("");
        nb::object rendered = checked(PyObject_Format(actual_type.ptr(), empty.ptr()));
        nb::str prefix("Unsupported update from ");
        nb::object message = checked(PyUnicode_Concat(prefix.ptr(), rendered.ptr()));
        PyErr_SetObject(PyExc_TypeError, message.ptr());
        throw nb::python_error();
    }
    nb::object events = model.attr("events");
    nb::object context = events.attr("blocker")();
    nb::object enter = context_method(context, "__enter__", false);
    nb::object exit = context_method(context, "__exit__", true);
    nb::object block = enter();
    try {
        nb::object items = values.attr("items")();
        for (nb::handle item : nb::iter(items)) {
            nb::tuple pair_values = pair(item);
            nb::object name = pair_values[0];
            nb::object value = pair_values[1];
            nb::object field = checked(PyObject_GetAttr(model.ptr(), name.ptr()));
            if (instance(field, evented_type()) && truth(recurse)) {
                field.attr("update")(value, nb::arg("recurse") = recurse);
            } else {
                set_attr(model, name, value);
            }
        }
    } catch (const nb::python_error& original) {
        {
            HandledException active(original.value());
            nb::object traceback = original.traceback();
            if (!traceback.is_valid()) traceback = nb::none();
            try {
                nb::object suppressed = exit(original.type(), original.value(), traceback);
                if (!truth(suppressed)) throw;
            } catch (const nb::python_error& cleanup) {
                if (cleanup.value().ptr() != original.value().ptr()) {
                    Py_INCREF(original.value().ptr());
                    PyException_SetContext(cleanup.value().ptr(), original.value().ptr());
                }
                throw;
            }
        }
        // A provider context may suppress errors; it must still reach the
        // group-event decision, just as the Python with statement does.
        if (truth(block.attr("count"))) {
            nb::object event = nb::module_::import_("napari.utils.events.evented_model").attr("Event")(model);
            nb::object emitter = model.attr("events");
            emitter(event);
        }
        return;
    }
    exit(nb::none(), nb::none(), nb::none());
    if (truth(block.attr("count"))) {
        nb::object event = nb::module_::import_("napari.utils.events.evented_model").attr("Event")(model);
        nb::object emitter = model.attr("events");
        emitter(event);
    }
}

nb::object equal_models(nb::handle model, nb::handle other) {
    if (model.is(other)) return nb::bool_(true);
    nb::object evented = evented_type();
    if (!instance(other, evented)) {
        nb::object values = model.attr("model_dump")();
        return checked(PyObject_RichCompare(values.ptr(), other.ptr(), Py_EQ));
    }
    nb::object type = model.attr("__class__");
    nb::object other_type = other.attr("__class__");
    nb::object differs = checked(PyObject_RichCompare(type.ptr(), other_type.ptr(), Py_NE));
    if (truth(differs)) return nb::bool_(false);
    nb::object model_fields = fields(model);
    for (nb::handle name : nb::iter(model_fields)) {
        nb::object operators = model.attr("__eq_operators__");
        nb::object comparator = checked(PyObject_GetItem(operators.ptr(), name.ptr()));
        nb::object left = checked(PyObject_GetAttr(model.ptr(), name.ptr()));
        nb::object right = checked(PyObject_GetAttr(other.ptr(), name.ptr()));
        if (!truth(comparator(left, right))) return nb::bool_(false);
    }
    return nb::bool_(true);
}

nb::tuple enum_enter(nb::handle model, nb::handle as_values) {
    nb::object sentinel = nb::module_::import_("builtins").attr("object")();
    nb::object config = model.attr("model_config");
    nb::object before = config.attr("get")("use_enum_values", sentinel);
    config = model.attr("model_config");
    set_item(config, nb::str("use_enum_values"), as_values);
    return nb::make_tuple(model, before, sentinel);
}

void enum_leave(nb::tuple token) {
    nb::object model = token[0];
    nb::object before = token[1];
    nb::object sentinel = token[2];
    nb::object config = model.attr("model_config");
    nb::str key("use_enum_values");
    if (!before.is(sentinel)) set_item(config, key, before);
    else if (PyObject_DelItem(config.ptr(), key.ptr()) < 0) throw nb::python_error();
}

} // namespace

void bind_model_helpers(nb::module_& module) {
    bind_annotation_helpers(module);
    bind_model_metadata(module);
    module.def("model_initialize_events", initialize_events);
    module.def("model_reset_event_source", reset_sources);
    module.def("model_get_defaults", defaults);
    module.def("model_reset", reset_model);
    module.def("model_update", update_model, nb::arg("model"), nb::arg("values").none(),
               nb::arg("recurse").none() = true);
    module.def("model_equal", equal_models, nb::arg("model"), nb::arg("other").none());
    module.def("model_enums_enter", enum_enter, nb::arg("model"), nb::arg("as_values").none());
    module.def("model_enums_leave", enum_leave);
}
} // namespace nx
