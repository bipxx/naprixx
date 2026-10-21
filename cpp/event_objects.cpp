// Event system adapted from napari/VisPy; retained BSD notices are in
// src/napari/utils/events/event.py and licenses/.
#include "event_bindings.hpp"
#include "event_protocol.hpp"
#include "event_state_owner.hpp"

namespace nx {
namespace {
using namespace events;

void initialize_event(nb::handle event, nb::handle type, nb::handle native, nb::dict kwargs) {
    set_attr(event, "_sources", nb::list());
    set_attr(event, "_handled", nb::bool_(false));
    set_attr(event, "_blocked", nb::bool_(false));
    set_attr(event, "_type", type);
    set_attr(event, "_native", native);
    set_attr(event, "_kwargs", kwargs);
    nb::object items = kwargs.attr("items")();
    for (nb::handle supplied : nb::iter(items)) {
        nb::tuple entry = pair(supplied);
        nb::object name = entry[0];
        nb::object value = entry[1];
        global("setattr")(event, name, value);
    }
}

nb::object get_event(nb::handle event, const char* name) {
    if (std::string(name) == "source") {
        nb::object sources = event.attr("_sources");
        if (!truth(sources)) return nb::none();
        sources = event.attr("_sources");
        return index(sources, -1);
    }
    return event.attr((std::string("_") + name).c_str());
}

void set_event_flag(nb::handle event, const char* name, nb::handle supplied) {
    nb::object value = global("bool")(supplied);
    set_attr(event, (std::string("_") + name).c_str(), value);
}

nb::object event_repr(nb::handle event) {
    nb::module_ module = nb::module_::import_("napari.utils.events.event");
    nb::object depth = global("_event_repr_depth");
    depth = checked(PyNumber_InPlaceAdd(depth.ptr(), nb::int_(1).ptr()));
    set_attr(module, "_event_repr_depth", depth);
    nb::list attributes;
    bool abbreviated = false;
    try {
        depth = global("_event_repr_depth");
        abbreviated = compare(depth, nb::int_(2), Py_GT);
        if (!abbreviated) {
            nb::object names = global("dir")(event);
            for (nb::handle name : nb::iter(names)) {
                if (truth(name.attr("startswith")("_"))) continue;
                nb::object hasattr = global("hasattr");
                nb::object type = global("type")(event);
                if (!truth(hasattr(type, name))) continue;
                nb::object getattr = global("getattr");
                type = global("type")(event);
                nb::object property = getattr(type, name);
                if (!instance(property, global("property"))) continue;
                nb::object value = global("getattr")(event, name);
                nb::object representation = checked(PyObject_Repr(value.ptr()));
                attributes.append(message({formatted(name), nb::str("="), representation}));
            }
        }
    } catch (const nb::python_error& exception) {
        HandledException active(exception.value());
        depth = global("_event_repr_depth");
        depth = checked(PyNumber_InPlaceSubtract(depth.ptr(), nb::int_(1).ptr()));
        set_attr(module, "_event_repr_depth", depth);
        throw;
    }
    depth = global("_event_repr_depth");
    depth = checked(PyNumber_InPlaceSubtract(depth.ptr(), nb::int_(1).ptr()));
    set_attr(module, "_event_repr_depth", depth);
    if (abbreviated) return nb::str("<...>");
    nb::object type = event.attr("__class__");
    nb::object name = type.attr("__name__");
    nb::object joined = nb::str(" ").attr("join")(attributes);
    return message({nb::str("<"), formatted(name), nb::str(" "), joined, nb::str(">")});
}

void initialize_counter(nb::handle counter) {
    nb::object value = global("weakref").attr("WeakKeyDictionary")();
    set_attr(counter, "_counter", value);
    set_attr(counter, "_nonecount", nb::int_(0));
}

void update_counter(nb::handle counter, nb::handle iterable) {
    for (nb::handle value : nb::iter(iterable)) {
        if (value.is_none()) {
            nb::object count = counter.attr("_nonecount");
            count = checked(PyNumber_InPlaceAdd(count.ptr(), nb::int_(1).ptr()));
            set_attr(counter, "_nonecount", count);
        } else {
            nb::object count = counter.attr("get")(value, 0);
            count = add(count, nb::int_(1));
            nb::object values = counter.attr("_counter");
            set_item(values, value, count);
        }
    }
}

nb::object get_counter(nb::handle counter, nb::handle key, nb::handle fallback) {
    if (key.is_none()) return counter.attr("_nonecount");
    nb::object values = counter.attr("_counter");
    return values.attr("get")(key, fallback);
}

void initialize_emitter(nb::handle emitter, nb::handle source, nb::handle type,
                        nb::handle event_class) {
    set_attr(emitter, "_callbacks", nb::list());
    set_attr(emitter, "_callback_refs", nb::list());
    set_attr(emitter, "_callback_pass_event", nb::list());
    nb::dict blocked;
    blocked[nb::none()] = nb::int_(0);
    set_attr(emitter, "_blocked", blocked);
    nb::object counter = global("_WeakCounter")();
    set_attr(emitter, "_block_counter", counter);
    set_attr(emitter, "_emitting", nb::bool_(false));
    set_attr(emitter, "source", source);
    set_attr(emitter, "default_args", nb::dict());
    if (!type.is_none()) {
        nb::object defaults = emitter.attr("default_args");
        set_item(defaults, nb::str("type_name"), type);
    }
    require(truth(global("inspect").attr("isclass")(event_class)));
    set_attr(emitter, "event_class", event_class);
    set_attr(emitter, "_ignore_callback_errors", nb::bool_(false));
    set_attr(emitter, "print_callback_errors", nb::str("reminders"));
}

nb::object get_emitter(nb::handle emitter, const char* name) {
    const std::string field(name);
    if (field == "source") {
        nb::object source = emitter.attr("_source");
        if (source.is_none()) return nb::none();
        source = emitter.attr("_source");
        return source();
    }
    if (field == "callbacks" || field == "callback_refs") {
        nb::object tuple = global("tuple");
        nb::object values = emitter.attr(("_" + field).c_str());
        return tuple(values);
    }
    return emitter.attr(("_" + field).c_str());
}

void set_emitter(nb::handle emitter, const char* name, nb::handle supplied) {
    const std::string field(name);
    if (field == "source") {
        nb::object value = supplied.is_none() ? nb::object(nb::none())
                            : nb::object(global("weakref").attr("ref")(supplied));
        set_attr(emitter, "_source", value);
        return;
    }
    if (field == "print_callback_errors"
        && !contains(nb::make_tuple("first", "reminders", "always", "never"), supplied))
        error(PyExc_ValueError, nb::str("print_callback_errors must be \"first\", \"reminders\", \"always\", or \"never\""));
    set_attr(emitter, ("_" + field).c_str(), supplied);
}

void initialize_blocker(nb::handle blocker, nb::handle target, nb::handle callback) {
    set_attr(blocker, "target", target);
    set_attr(blocker, "callback", callback);
    nb::object counter = target.attr("_block_counter");
    nb::object count = counter.attr("get")(callback, 0);
    set_attr(blocker, "_base_count", count);
}

nb::object blocker_count(nb::handle blocker) {
    nb::object target = blocker.attr("target");
    nb::object counter = target.attr("_block_counter");
    nb::object callback = blocker.attr("callback");
    nb::object count = counter.attr("get")(callback, 0);
    nb::object base = blocker.attr("_base_count");
    return subtract(count, base);
}

nb::object blocker_enter(nb::handle blocker) {
    nb::object target = blocker.attr("target");
    nb::object block = target.attr("block");
    nb::object callback = blocker.attr("callback");
    block(callback);
    return nb::borrow<nb::object>(blocker);
}

void blocker_exit(nb::handle blocker) {
    nb::object target = blocker.attr("target");
    nb::object unblock = target.attr("unblock");
    nb::object callback = blocker.attr("callback");
    unblock(callback);
}

// Remaining Event helpers used by Notification/Event subclasses. These operate
// on the actual Python receiver and executing globals; no storage/layout change.
nb::object event_object_string(nb::handle event) {
    return event.attr("__class__").attr("__name__");
}

nb::object event_object_global_object(nb::dict namespace_) {
    PyObject* supplied = PyDict_GetItemString(namespace_.ptr(), "object");
    if (supplied) return nb::borrow<nb::object>(supplied);
    if (PyErr_Occurred()) throw nb::python_error();
    nb::object key = nb::str("object");
    supplied = PyObject_GetItem(PyEval_GetBuiltins(), key.ptr());
    if (supplied) return nb::steal<nb::object>(supplied);
    if (!PyErr_ExceptionMatches(PyExc_KeyError)) throw nb::python_error();
    PyErr_Clear();
    nb::object error = checked(PyObject_CallOneArg(
        PyExc_NameError, nb::str("name 'object' is not defined").ptr()));
    set_attr(error, "name", key);
    PyErr_SetObject(PyExc_NameError, error.ptr());
    throw nb::python_error();
}

nb::object event_object_getattribute(nb::handle event, nb::handle name,
                                    nb::dict namespace_) {
    nb::object getter = event_object_global_object(std::move(namespace_))
                            .attr("__getattribute__");
    return getter(event, name);
}

} // namespace

void bind_event_objects(nb::module_& module) {
    bind_event_state_owner(module);
    module.def("event_initialize", initialize_event, nb::arg("event").none(),
               nb::arg("type").none(), nb::arg("native").none(), nb::arg("kwargs"));
    module.def("event_get", get_event);
    module.def("event_set_flag", set_event_flag, nb::arg("event").none(), nb::arg("name"), nb::arg("value").none());
    module.def("event_repr", event_repr);
    module.def("event_push_source", [](nb::handle event, nb::handle source) {
        event.attr("_sources").attr("append")(source);
    }, nb::arg("event").none(), nb::arg("source").none());
    module.def("event_pop_source", [](nb::handle event) {
        return event.attr("_sources").attr("pop")();
    });
    module.def("event_counter_initialize", initialize_counter);
    module.def("event_counter_update", update_counter, nb::arg("counter").none(), nb::arg("iterable").none());
    module.def("event_counter_get", get_counter, nb::arg("counter").none(), nb::arg("key").none(), nb::arg("default").none());
    module.def("event_emitter_initialize", initialize_emitter, nb::arg("emitter").none(),
               nb::arg("source").none(), nb::arg("type").none(), nb::arg("event_class").none());
    module.def("event_emitter_get", get_emitter);
    module.def("event_emitter_set", set_emitter, nb::arg("emitter").none(), nb::arg("name"), nb::arg("value").none());
    module.def("event_make_blocker", [](nb::handle emitter, nb::handle callback) {
        return global("EventBlocker")(emitter, callback);
    }, nb::arg("emitter").none(), nb::arg("callback").none());
    module.def("event_blocker_initialize", initialize_blocker, nb::arg("blocker").none(), nb::arg("target").none(), nb::arg("callback").none());
    module.def("event_blocker_count", blocker_count);
    module.def("event_blocker_enter", blocker_enter);
    module.def("event_blocker_exit", blocker_exit);
    module.def("event_blocker_all_initialize", [](nb::handle blocker, nb::handle target) {
        set_attr(blocker, "target", target);
    });
    module.def("event_blocker_all_enter", [](nb::handle blocker) {
        blocker.attr("target").attr("block_all")();
    });
    module.def("event_blocker_all_exit", [](nb::handle blocker) {
        blocker.attr("target").attr("unblock_all")();
    });
    module.def("event_object_string", event_object_string,
               nb::arg("event").none());
    module.def("event_object_getattribute", event_object_getattribute,
               nb::arg("event").none(), nb::arg("name").none(), nb::arg("namespace_"));

}
} // namespace nx
