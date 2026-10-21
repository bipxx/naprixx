// Event system adapted from napari/VisPy; retained BSD notices are in
// src/napari/utils/events/event.py and licenses/.
#include "event_bindings.hpp"
#include "event_protocol.hpp"

namespace nx {
namespace {
using namespace events;

nb::object prepare_event(nb::handle emitter, nb::tuple args, nb::dict kwargs) {
    if (args.size() == 1 && !truth(kwargs)) {
        nb::object value = args[0];
        if (instance(value, global("Event"))) {
            nb::object type = emitter.attr("event_class");
            require(instance(value, type));
            return value;
        }
    }
    if (args.size() == 0) {
        nb::object defaults = emitter.attr("default_args");
        nb::object values = defaults.attr("copy")();
        values.attr("update")(kwargs);
        nb::object type = emitter.attr("event_class");
        nb::dict keywords = keyword_mapping(type, values);
        return call(type, nb::tuple(), keywords);
    }
    error(PyExc_ValueError, nb::str("Event emitters can be called with an Event instance or with keyword arguments only."));
}

void invoke(nb::handle emitter, nb::handle callback, nb::handle event) {
    try {
        if (event.is_none()) nb::borrow<nb::object>(callback)();
        else nb::borrow<nb::object>(callback)(event);
    } catch (const nb::python_error& exception) {
        if (!exception.matches(PyExc_Exception)) throw;
        HandledException active(exception.value());
        if (instance(exception.value(), global("RuntimeError"))) {
            nb::object text = global("str")(exception.value());
            if (contains(text, nb::str("C++"))) {
                text = global("str")(exception.value());
                if (truth(text.attr("endswith")(nb::make_tuple("has been deleted", "already deleted.")))) {
                    emitter.attr("disconnect")(callback);
                    return;
                }
            }
        }
        nb::object handler = global("_handle_exception");
        nb::object ignore = emitter.attr("ignore_callback_errors");
        nb::object printing = emitter.attr("print_callback_errors");
        handler(ignore, printing, emitter, nb::arg("cb_event") = nb::make_tuple(callback, event));
    }
}

void finish_emission(nb::handle emitter, nb::handle event) {
    set_attr(emitter, "_emitting", nb::bool_(false));
    nb::object source = event.attr("_pop_source")();
    nb::object expected = emitter.attr("source");
    if (!source.is(expected)) error(PyExc_RuntimeError, nb::str("Event source-stack mismatch."));
}

nb::object emit(nb::handle emitter, nb::tuple args, nb::dict kwargs) {
    nb::object blocked = emitter.attr("_blocked");
    nb::object prepare = emitter.attr("_prepare_event");
    nb::object event = call(prepare, args, kwargs);
    nb::object push = event.attr("_push_source");
    nb::object source = emitter.attr("source");
    push(source);
    set_attr(emitter, "_emitting", nb::bool_(true));
    try {
        nb::object count = blocked.attr("get")(nb::none(), 0);
        if (compare(count, nb::int_(0), Py_GT)) {
            nb::object counter = emitter.attr("_block_counter");
            nb::list values;
            values.append(nb::none());
            counter.attr("update")(values);
        } else {
            global("_log_event_stack")(event);
            nb::list removed;
            nb::object callbacks = emitter.attr("_callbacks");
            callbacks = slice(callbacks, nb::none(), nb::none());
            nb::object pass_event = emitter.attr("_callback_pass_event");
            pass_event = slice(pass_event, nb::none(), nb::none());
            nb::object pairs = global("zip")(callbacks, pass_event, nb::arg("strict") = false);
            for (nb::handle supplied : nb::iter(pairs)) {
                nb::tuple entry = pair(supplied);
                nb::object callback = entry[0];
                nb::object pass = entry[1];
                if (instance(callback, global("tuple"))) {
                    nb::object reference = index(callback, 0);
                    nb::object object = reference();
                    if (object.is_none()) {
                        removed.append(callback);
                        continue;
                    }
                    nb::object old = callback;
                    nb::object getattr = global("getattr");
                    nb::object name = index(callback, 1);
                    callback = getattr(object, name, nb::none());
                    if (callback.is_none()) {
                        nb::object warn = global("warnings").attr("warn");
                        name = index(old, 1);
                        nb::str text = message({nb::str("Problem with function "), formatted(name),
                            nb::str(" of "), formatted(object), nb::str(" connected to event "), formatted(emitter)});
                        warn(text, nb::arg("stacklevel") = 2, nb::arg("category") = global("RuntimeWarning"));
                        continue;
                    }
                    callback = global("cast")(global("Callback"), callback);
                }
                count = blocked.attr("get")(callback, 0);
                if (compare(count, nb::int_(0), Py_GT)) {
                    nb::object counter = emitter.attr("_block_counter");
                    nb::list values;
                    values.append(callback);
                    counter.attr("update")(values);
                    continue;
                }
                nb::object method = emitter.attr("_invoke_callback");
                method(callback, truth(pass) ? event : nb::object(nb::none()));
                nb::object stopped = event.attr("blocked");
                if (truth(stopped)) break;
            }
            for (nb::handle callback : removed) emitter.attr("disconnect")(callback);
        }
    } catch (const nb::python_error& exception) {
        HandledException active(exception.value());
        finish_emission(emitter, event);
        throw;
    } catch (...) {
        finish_emission(emitter, event);
        throw;
    }
    finish_emission(emitter, event);
    return event;
}

nb::object blocked(nb::handle emitter, nb::handle callback) {
    nb::object values = emitter.attr("_blocked");
    nb::object count = values.attr("get")(callback, 0);
    return checked(PyObject_RichCompare(count.ptr(), nb::int_(0).ptr(), Py_GT));
}

void block(nb::handle emitter, nb::handle callback) {
    nb::object values = emitter.attr("_blocked");
    nb::object count = values.attr("get")(callback, 0);
    count = add(count, nb::int_(1));
    values = emitter.attr("_blocked");
    set_item(values, callback, count);
}

void unblock(nb::handle emitter, nb::handle callback) {
    nb::object values = emitter.attr("_blocked");
    bool invalid = !contains(values, callback);
    if (!invalid) {
        values = emitter.attr("_blocked");
        invalid = compare(item(values, callback), nb::int_(0));
    }
    if (invalid)
        error(PyExc_RuntimeError, message({nb::str("Cannot unblock "), formatted(emitter),
            nb::str(" for callback "), formatted(callback), nb::str("; emitter was not previously blocked.")}));
    values = emitter.attr("_blocked");
    nb::object count = subtract(item(values, callback), nb::int_(1));
    values = emitter.attr("_blocked");
    if (compare(count, nb::int_(0)) && !callback.is_none()) {
        if (PyObject_DelItem(values.ptr(), callback.ptr()) < 0) throw nb::python_error();
    } else set_item(values, callback, count);
}
} // namespace

void bind_event_delivery(nb::module_& module) {
    module.def("event_prepare", prepare_event);
    module.def("event_emit", emit);
    module.def("event_invoke_callback", invoke, nb::arg("emitter").none(),
               nb::arg("callback").none(), nb::arg("event").none());
    module.def("event_is_blocked", blocked, nb::arg("emitter").none(), nb::arg("callback").none());
    module.def("event_block", block, nb::arg("emitter").none(), nb::arg("callback").none());
    module.def("event_unblock", unblock, nb::arg("emitter").none(), nb::arg("callback").none());
}
} // namespace nx
