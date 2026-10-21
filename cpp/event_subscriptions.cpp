// Event system adapted from napari/VisPy. BSD-3-Clause attribution and terms
// are retained in src/napari/utils/events/event.py and licenses/.
#include "event_bindings.hpp"
#include "event_protocol.hpp"

namespace nx {
namespace {
using namespace events;

nb::object is_core(nb::handle callback, nb::handle core) {
    nb::object value = nb::borrow<nb::object>(callback);
    if (instance(value, global("partial"))) value = value.attr("func");
    if (!instance(value, global("tuple"))) {
        try {
            nb::object module = value.attr("__module__");
            nb::object startswith = module.attr("startswith");
            return startswith(message({formatted(core), nb::str(".")}));
        } catch (const nb::python_error& exception) {
            if (!exception.matches(PyExc_AttributeError)) throw;
            return nb::bool_(false);
        }
    }
    nb::object reference = index(value, 0);
    nb::object object = reference();
    if (object.is_none()) return nb::bool_(false);
    try {
        nb::object module = object.attr("__module__");
        nb::object startswith = module.attr("startswith");
        return startswith(message({formatted(core), nb::str(".")}));
    } catch (const nb::python_error& exception) {
        if (!exception.matches(PyExc_AttributeError)) throw;
        return nb::bool_(false);
    }
}

nb::tuple proper_name(nb::handle callback) {
    require(truth(global("inspect").attr("ismethod")(callback)));
    nb::object object = callback.attr("__self__");
    nb::object hasattr = global("hasattr");
    nb::object name = callback.attr("__name__");
    bool search = !truth(hasattr(object, name));
    if (!search) {
        nb::object getattr = global("getattr");
        name = callback.attr("__name__");
        nb::object found = getattr(object, name);
        search = compare(found, callback, Py_NE);
    }
    if (search) {
        nb::object names = global("dir")(object);
        for (nb::handle candidate : nb::iter(names)) {
            nb::object method = global("getattr")(object, candidate);
            if (truth(global("inspect").attr("ismethod")(method)) && compare(method, callback))
                return nb::make_tuple(object, candidate);
        }
        error(PyExc_RuntimeError, message({nb::str("During bind method "), formatted(callback),
              nb::str(" of object "), formatted(object), nb::str(" an error happen")}));
    }
    name = callback.attr("__name__");
    return nb::make_tuple(object, name);
}

nb::object positional_parameter(nb::handle parameter) {
    nb::object kind = parameter.attr("kind");
    nb::list positional;
    nb::object type = global("inspect").attr("Parameter");
    positional.append(type.attr("POSITIONAL_ONLY"));
    type = global("inspect").attr("Parameter");
    positional.append(type.attr("POSITIONAL_OR_KEYWORD"));
    if (!contains(positional, kind)) return nb::bool_(false);
    nb::object value = parameter.attr("default");
    type = global("inspect").attr("Parameter");
    nb::object empty = type.attr("empty");
    return checked(PyObject_RichCompare(value.ptr(), empty.ptr(), Py_EQ));
}

bool check_signature(nb::handle function) {
    nb::object signature = global("inspect").attr("signature")(function);
    nb::object list = global("list");
    nb::object parameters = signature.attr("parameters");
    nb::object values = parameters.attr("values")();
    nb::object snapshot = list(values);
    nb::object total = nb::int_(0);
    nb::object predicate = global("_is_pos_arg");
    for (nb::handle parameter : nb::iter(snapshot)) total = add(total, predicate(parameter));
    if (compare(total, nb::int_(1), Py_GT))
        error(PyExc_RuntimeError, nb::str("Binning function cannot have more than one positional argument"));
    parameters = signature.attr("parameters");
    values = parameters.attr("values")();
    for (nb::handle parameter : nb::iter(values)) {
        nb::object kind = parameter.attr("kind");
        nb::list accepted;
        for (const char* name : {"POSITIONAL_ONLY", "POSITIONAL_OR_KEYWORD", "VAR_POSITIONAL"}) {
            nb::object type = global("inspect").attr("Parameter");
            accepted.append(type.attr(name));
        }
        if (contains(accepted, kind)) return true;
    }
    return false;
}

nb::tuple normalize(nb::handle emitter, nb::handle callback) {
    nb::object value = nb::borrow<nb::object>(callback);
    if (truth(global("inspect").attr("ismethod")(value)))
        value = emitter.attr("_get_proper_name")(value);
    if (instance(value, global("tuple"))) {
        nb::object first = index(value, 0);
        nb::object reference_type = global("weakref").attr("ref");
        if (!instance(first, reference_type)) {
            nb::object reference = global("weakref").attr("ref")(index(value, 0));
            nb::object tail = slice(value, nb::int_(1), nb::none());
            nb::list parts;
            parts.append(reference);
            for (nb::handle part : nb::iter(tail)) parts.append(part);
            value = global("tuple")(parts);
        }
    }
    if (instance(callback, global("Callable"))) {
        nb::object pass = emitter.attr("_check_signature")(callback);
        return nb::make_tuple(value, pass);
    }
    nb::object reference = index(value, 0);
    nb::object object = reference();
    if (object.is_none()) return nb::make_tuple(value, nb::bool_(false));
    nb::object getattr = global("getattr");
    nb::object name = index(value, 1);
    nb::object function = getattr(object, name);
    nb::object pass = emitter.attr("_check_signature")(function);
    return nb::make_tuple(value, pass);
}

nb::object connect(nb::handle emitter, nb::handle supplied, nb::handle ref,
                   nb::handle position, nb::handle before, nb::handle after,
                   nb::handle until) {
    nb::object callbacks = emitter.attr("callbacks");
    nb::object references = emitter.attr("callback_refs");
    nb::object result = emitter.attr("_normalize_cb")(supplied);
    nb::tuple normalized = pair(result);
    nb::object callback = normalized[0];
    nb::object pass_event = normalized[1];
    if (contains(callbacks, callback)) return nb::none();

    nb::object reference;
    if (instance(ref, global("bool"))) {
        if (truth(ref)) {
            if (instance(callback, global("tuple"))) reference = index(callback, 1);
            else if (truth(global("hasattr")(callback, "__name__"))) reference = callback.attr("__name__");
            else {
                nb::object type = callback.attr("__class__");
                reference = type.attr("__name__");
            }
        } else reference = nb::none();
    } else if (instance(ref, global("str"))) reference = nb::borrow<nb::object>(ref);
    else error(PyExc_TypeError, nb::str("ref must be a bool or string"));
    nb::object current_references = emitter.attr("_callback_refs");
    if (!reference.is_none() && contains(current_references, reference))
        error(PyExc_ValueError, message({nb::str("ref \""), formatted(reference), nb::str("\" is not unique")}));
    if (!contains(nb::make_tuple("first", "last"), position))
        error(PyExc_ValueError, message({nb::str("position must be \"first\" or \"last\", not "), formatted(position)}));

    nb::object current_callbacks = emitter.attr("_callbacks");
    nb::object enumerate = global("enumerate");
    nb::object indexed = enumerate(current_callbacks);
    nb::list core_indices;
    for (nb::handle supplied_entry : nb::iter(indexed)) {
        nb::tuple entry = pair(supplied_entry);
        nb::object i = entry[0];
        nb::object value = entry[1];
        if (truth(emitter.attr("_is_core_callback")(value, "napari"))) core_indices.append(i);
    }
    nb::object count = truth(core_indices) ? add(global("max")(core_indices), nb::int_(1))
                                          : nb::object(nb::int_(0));
    nb::object lower = count;
    nb::object upper;
    if (truth(emitter.attr("_is_core_callback")(callback, "napari"))) {
        lower = nb::int_(0);
        upper = count;
    } else upper = global("len")(references);

    nb::list bounds;
    unsigned ri = 0;
    for (nb::handle given : nb::make_tuple(before, after)) {
        nb::object criteria = nb::borrow<nb::object>(given);
        if (criteria.is_none() || compare(criteria, nb::list())) {
            bounds.append(ri == 0 ? upper : lower);
        } else {
            if (!instance(criteria, global("list"))) {
                nb::list wrapped;
                wrapped.append(criteria);
                criteria = wrapped;
            }
            for (nb::handle criterion : nb::iter(criteria)) {
                nb::object zip = global("zip")(references, callbacks, nb::arg("strict") = false);
                nb::object matches = nb::int_(0);
                for (nb::handle supplied_entry : nb::iter(zip)) {
                    nb::tuple entry = pair(supplied_entry);
                    nb::list choices;
                    choices.append(entry[0]);
                    choices.append(entry[1]);
                    if (contains(choices, criterion)) matches = add(matches, nb::int_(1));
                }
                if (compare(matches, nb::int_(1), Py_NE))
                    error(PyExc_ValueError, message({nb::str("criteria \""), formatted(criteria),
                        nb::str("\" is in the current callback list "), formatted(matches),
                        nb::str(" times:\n"), formatted(references), nb::str("\n"), formatted(callbacks)}));
            }
            nb::list matches;
            nb::object zip = global("zip")(references, callbacks, nb::arg("strict") = false);
            nb::object candidates = global("enumerate")(zip);
            for (nb::handle supplied_entry : nb::iter(candidates)) {
                nb::tuple entry = pair(supplied_entry);
                nb::object i = entry[0];
                nb::tuple values = pair(entry[1]);
                nb::object name = values[0];
                nb::object value = values[1];
                if (contains(criteria, value) || contains(criteria, name)) matches.append(i);
            }
            bounds.append(ri == 0 ? index(matches, 0) : add(index(matches, -1), nb::int_(1)));
        }
        ++ri;
    }
    nb::object upper_bound = bounds[0];
    nb::object lower_bound = bounds[1];
    if (compare(upper_bound, lower_bound, Py_LT))
        error(PyExc_RuntimeError, message({nb::str("cannot place callback before \""), formatted(before),
            nb::str("\" and after \""), formatted(after), nb::str("\" for callbacks: "), formatted(references)}));
    nb::object i = compare(position, nb::str("first")) ? lower_bound : upper_bound;
    emitter.attr("_callbacks").attr("insert")(i, callback);
    emitter.attr("_callback_refs").attr("insert")(i, reference);
    emitter.attr("_callback_pass_event").attr("insert")(i, pass_event);
    if (!until.is_none()) {
        nb::object method = until.attr("connect");
        nb::object partial = global("partial");
        nb::object disconnect = emitter.attr("disconnect");
        method(partial(disconnect, callback));
    }
    return nb::borrow<nb::object>(supplied);
}

void disconnect(nb::handle emitter, nb::handle supplied) {
    if (supplied.is_none()) {
        set_attr(emitter, "_callbacks", nb::list());
        set_attr(emitter, "_callback_refs", nb::list());
        set_attr(emitter, "_callback_pass_event", nb::list());
        return;
    }
    nb::object callable = global("Callable");
    nb::object tuple = global("tuple");
    nb::object accepted = checked(PyNumber_Or(callable.ptr(), tuple.ptr()));
    if (instance(supplied, accepted)) {
        nb::object result = emitter.attr("_normalize_cb")(supplied);
        nb::tuple normalized = pair(result);
        nb::object callback = normalized[0];
        nb::object callbacks = emitter.attr("_callbacks");
        if (contains(callbacks, callback)) {
            callbacks = emitter.attr("_callbacks");
            nb::object i = callbacks.attr("index")(callback);
            emitter.attr("_callbacks").attr("pop")(i);
            emitter.attr("_callback_refs").attr("pop")(i);
            emitter.attr("_callback_pass_event").attr("pop")(i);
        }
        return;
    }
    nb::list removed;
    nb::object callbacks = emitter.attr("_callbacks");
    nb::object indexed = global("enumerate")(callbacks);
    for (nb::handle supplied_entry : nb::iter(indexed)) {
        nb::tuple entry = pair(supplied_entry);
        nb::object i = entry[0];
        nb::object callback = entry[1];
        if (!instance(callback, global("Sequence"))) continue;
        nb::object first = index(callback, 0);
        nb::object reference_type = global("weakref").attr("ref");
        if (!instance(first, reference_type)) continue;
        first = index(callback, 0);
        nb::object object = first();
        bool remove = object.is(supplied);
        if (!remove) {
            first = index(callback, 0);
            object = first();
            remove = object.is_none();
        }
        if (remove) removed.append(i);
    }
    nb::object reversed = slice(removed, nb::none(), nb::none(), nb::int_(-1));
    for (nb::handle i : nb::iter(reversed)) {
        emitter.attr("_callbacks").attr("pop")(i);
        emitter.attr("_callback_refs").attr("pop")(i);
        emitter.attr("_callback_pass_event").attr("pop")(i);
    }
}
} // namespace

void bind_event_subscriptions(nb::module_& module) {
    module.def("event_is_core_callback", is_core, nb::arg("callback").none(), nb::arg("core").none());
    module.def("event_proper_callback_name", proper_name, nb::arg("callback").none());
    module.def("event_positional_parameter", positional_parameter, nb::arg("parameter").none());
    module.def("event_check_signature", check_signature, nb::arg("function").none());
    module.def("event_normalize_callback", normalize, nb::arg("emitter").none(), nb::arg("callback").none());
    module.def("event_connect", connect, nb::arg("emitter").none(), nb::arg("callback").none(),
               nb::arg("ref").none(), nb::arg("position").none(), nb::arg("before").none(),
               nb::arg("after").none(), nb::arg("until").none());
    module.def("event_disconnect", disconnect, nb::arg("emitter").none(), nb::arg("callback").none());
}
} // namespace nx
