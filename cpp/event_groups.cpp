// Event system adapted from napari/VisPy; retained BSD notices are in
// src/napari/utils/events/event.py and licenses/.
#include "event_bindings.hpp"
#include "event_protocol.hpp"

namespace nx {
namespace {
using namespace events;

void initialize_group(nb::handle group, nb::handle source, nb::handle auto_connect,
                      nb::handle children, nb::dict emitters) {
    global("EventEmitter").attr("__init__")(group, source);
    set_attr(group, "auto_connect", auto_connect);
    set_attr(group, "_connect_children", children);
    set_attr(group, "auto_connect_format", nb::str("on_%s"));
    set_attr(group, "_emitters", nb::dict());
    set_attr(group, "_emitters_connected", nb::bool_(false));
    nb::object method = group.attr("add");
    call(method, nb::tuple(), emitters);
}

void add_emitters(nb::handle group, nb::handle supplied_auto, nb::dict kwargs) {
    nb::object auto_connect = nb::borrow<nb::object>(supplied_auto);
    if (auto_connect.is_none()) auto_connect = group.attr("auto_connect");
    for (auto entry : kwargs) {
        nb::handle name = entry.first;
        nb::object emitters = group.attr("_emitters");
        if (contains(emitters, name))
            error(PyExc_ValueError, message({nb::str("EmitterGroup already has an emitter named '"),
                formatted(name), nb::str("'")}));
        if (truth(global("hasattr")(group, name)))
            error(PyExc_ValueError, message({nb::str("The name '"), formatted(name),
                nb::str("' cannot be used as an emitter; it is already an attribute of EmitterGroup")}));
    }
    nb::object items = kwargs.attr("items")();
    for (nb::handle supplied : nb::iter(items)) {
        nb::tuple entry = pair(supplied);
        nb::object name = entry[0];
        nb::object emitter = entry[1];
        if (emitter.is_none()) emitter = global("Event");
        if (truth(global("inspect").attr("isclass")(emitter))
            && truth(global("issubclass")(emitter, global("Event")))) {
            nb::object create = global("EventEmitter");
            nb::object source = group.attr("source");
            emitter = create(nb::arg("source") = source, nb::arg("type_name") = name,
                             nb::arg("event_class") = emitter);
        } else if (!instance(emitter, global("EventEmitter"))) {
            error(PyExc_RuntimeError, message({nb::str("Emitter must be specified as either an EventEmitter instance or Event subclass. (got "),
                formatted(name), nb::str("="), formatted(emitter), nb::str(")")}));
        }
        nb::object source = group.attr("source");
        set_attr(emitter, "source", source);
        global("setattr")(group, name, emitter);
        nb::object emitters = group.attr("_emitters");
        set_item(emitters, name, emitter);
        if (truth(auto_connect)) {
            source = group.attr("source");
            if (!source.is_none()) {
                nb::object hasattr = global("hasattr");
                source = group.attr("source");
                nb::object format = group.attr("auto_connect_format");
                nb::object method_name = checked(PyNumber_Remainder(format.ptr(), name.ptr()));
                if (truth(hasattr(source, method_name))) {
                    nb::object connect = emitter.attr("connect");
                    source = group.attr("source");
                    format = group.attr("auto_connect_format");
                    method_name = checked(PyNumber_Remainder(format.ptr(), name.ptr()));
                    connect(nb::make_tuple(source, method_name));
                }
            }
        }
        nb::object connected = group.attr("_emitters_connected");
        if (truth(connected)) emitter.attr("connect")(group);
    }
}

void connect_children(nb::handle group, nb::handle connect) {
    if (truth(connect)) {
        for (nb::handle name : nb::iter(group)) {
            nb::object emitter = item(group, name);
            if (!instance(emitter, global("WarningEmitter"))) {
                emitter = item(group, name);
                emitter.attr("connect")(group);
            }
        }
    } else {
        for (nb::handle name : nb::iter(group)) {
            nb::object emitter = item(group, name);
            emitter.attr("disconnect")(group);
        }
    }
    set_attr(group, "_emitters_connected", connect);
}

nb::object connect_group(nb::handle group, nb::handle callback, nb::handle ref,
                          nb::handle position, nb::handle before, nb::handle after) {
    nb::object callbacks = group.attr("_callbacks");
    nb::object count = global("len")(callbacks);
    bool first = compare(count, nb::int_(0));
    nb::object children = group.attr("_connect_children");
    if (truth(children)) group.attr("_connect_emitters")(nb::bool_(true));
    nb::object result = global("EventEmitter").attr("connect")(
        group, callback, ref, position, before, after);
    if (first) {
        nb::object hasattr = global("hasattr");
        nb::object source = group.attr("source");
        if (truth(hasattr(source, "first_callback_connect"))) {
            source = group.attr("source");
            source.attr("first_callback_connect")();
        }
    }
    return result;
}

nb::object disconnect_group(nb::handle group, nb::handle callback) {
    nb::object result = global("EventEmitter").attr("disconnect")(group, callback);
    nb::object callbacks = group.attr("_callbacks");
    nb::object count = global("len")(callbacks);
    if (compare(count, nb::int_(0))) {
        group.attr("_connect_emitters")(nb::bool_(false));
        nb::object hasattr = global("hasattr");
        nb::object source = group.attr("source");
        if (truth(hasattr(source, "last_callback_disconnect"))) {
            source = group.attr("source");
            source.attr("last_callback_disconnect")();
        }
    }
    return result;
}

void block_group(nb::handle group, bool block) {
    const char* method = block ? "block" : "unblock";
    group.attr(method)();
    nb::object emitters = group.attr("_emitters");
    nb::object values = emitters.attr("values")();
    for (nb::handle emitter : nb::iter(values)) emitter.attr(method)();
}

void set_group_errors(nb::handle group, nb::handle ignore) {
    nb::object property = global("EventEmitter").attr("ignore_callback_errors");
    property.attr("fset")(group, ignore);
    nb::object emitters = group.attr("_emitters");
    nb::object values = emitters.attr("values")();
    for (nb::handle emitter : nb::iter(values)) {
        if (instance(emitter, global("EventEmitter"))) set_attr(emitter, "ignore_callback_errors", ignore);
        else if (instance(emitter, global("EmitterGroup"))) emitter.attr("ignore_callback_errors_all")(ignore);
    }
}

void initialize_warning(nb::handle emitter, nb::handle defining_class, nb::handle message,
                        nb::handle category, nb::handle level, nb::tuple args, nb::dict kwargs) {
    nb::object base = global("super")(defining_class, emitter);
    nb::object method = base.attr("__init__");
    call(method, args, kwargs);
    set_attr(emitter, "_message", message);
    set_attr(emitter, "_warned", nb::bool_(false));
    set_attr(emitter, "_category", category);
    set_attr(emitter, "_stacklevel", level);
}

void warn_callback(nb::handle emitter, nb::handle callback) {
    nb::object warned = emitter.attr("_warned");
    if (truth(warned)) return;
    if (instance(callback, global("tuple"))) {
        nb::object getattr = global("getattr");
        nb::object object = index(callback, 0);
        nb::object name = index(callback, 1);
        if (getattr(object, name, nb::none()).is_none()) return;
    }
    // The original method imports warnings locally, independently of the
    // module's warning alias used by ordinary emitter delivery.
    nb::object warn = nb::module_::import_("warnings").attr("warn");
    nb::object message = emitter.attr("_message");
    nb::object category = emitter.attr("_category");
    nb::object level = emitter.attr("_stacklevel");
    warn(message, nb::arg("category") = category, nb::arg("stacklevel") = level);
    set_attr(emitter, "_warned", nb::bool_(true));
}

nb::object warning_connect(nb::handle emitter, nb::handle callback, nb::tuple args, nb::dict kwargs) {
    emitter.attr("_warn")(callback);
    nb::object method = global("EventEmitter").attr("connect");
    nb::tuple prefix = nb::make_tuple(emitter, callback);
    nb::tuple packed = nb::cast<nb::tuple>(add(prefix, args));
    return call(method, packed, kwargs);
}

nb::object warning_invoke(nb::handle emitter, nb::handle callback, nb::handle event) {
    emitter.attr("_warn")(callback);
    return global("EventEmitter").attr("_invoke_callback")(emitter, callback, event);
}

void initialize_renamed(nb::handle emitter, nb::handle defining_class, nb::handle name,
                        nb::tuple args, nb::dict kwargs) {
    nb::object base = global("super")(defining_class, emitter);
    nb::object method = base.attr("__init__");
    call(method, args, kwargs);
    nb::object parts = name.attr("split")(".");
    nb::object values = global("list")(parts);
    nb::object count = global("len")(values);
    if (compare(count, nb::int_(1), Py_LT))
        error(PyExc_ValueError, nb::str("not enough values to unpack (expected at least 1, got 0)"));
    nb::object last = values.attr("pop")();
    set_attr(emitter, "_new_name_path", values);
    set_attr(emitter, "_new_name", last);
    set_attr(emitter, "_connected", nb::bool_(false));
}

nb::object new_emitter(nb::handle emitter) {
    nb::object target = emitter.attr("source");
    nb::object source = emitter.attr("source");
    if (source.is_none()) {
        nb::object name = emitter.attr("_new_name");
        error(PyExc_RuntimeError, message({nb::str("Cannot connect to renamed emitter "),
            formatted(name), nb::str(" because source is None")}));
    }
    nb::object path = emitter.attr("_new_name_path");
    for (nb::handle attribute : nb::iter(path)) target = global("getattr")(target, attribute);
    nb::object getattr = global("getattr");
    nb::object events = target.attr("events");
    nb::object name = emitter.attr("_new_name");
    return getattr(events, name);
}

void renamed_connect(nb::handle emitter, nb::handle defining_class, nb::handle callback,
                     nb::tuple args, nb::dict kwargs) {
    nb::object connected = emitter.attr("_connected");
    if (!truth(connected)) {
        nb::object target = emitter.attr("_get_new_emitter")();
        target.attr("connect")(emitter);
        set_attr(emitter, "_connected", nb::bool_(true));
    }
    nb::object base = global("super")(defining_class, emitter);
    nb::object method = base.attr("connect");
    nb::tuple prefix = nb::make_tuple(callback);
    nb::tuple packed = nb::cast<nb::tuple>(add(prefix, args));
    call(method, packed, kwargs);
}

void renamed_disconnect(nb::handle emitter, nb::handle defining_class, nb::handle callback) {
    nb::object base = global("super")(defining_class, emitter);
    base.attr("disconnect")(callback);
    nb::object callbacks = emitter.attr("callbacks");
    if (!truth(callbacks)) {
        nb::object target = emitter.attr("_get_new_emitter")();
        target.attr("disconnect")(emitter);
        set_attr(emitter, "_connected", nb::bool_(false));
    }
}
} // namespace

void bind_event_groups(nb::module_& module) {
    module.def("event_group_initialize", initialize_group, nb::arg("group").none(), nb::arg("source").none(),
               nb::arg("auto_connect").none(), nb::arg("children").none(), nb::arg("emitters"));
    module.def("event_group_add", add_emitters, nb::arg("group").none(), nb::arg("auto_connect").none(), nb::arg("emitters"));
    module.def("event_group_connect", connect_group, nb::arg("group").none(), nb::arg("callback").none(),
               nb::arg("ref").none(), nb::arg("position").none(), nb::arg("before").none(), nb::arg("after").none());
    module.def("event_group_disconnect", disconnect_group, nb::arg("group").none(), nb::arg("callback").none());
    module.def("event_group_connect_children", connect_children, nb::arg("group").none(), nb::arg("connect").none());
    module.def("event_group_block_all", [](nb::handle group) { block_group(group, true); });
    module.def("event_group_unblock_all", [](nb::handle group) { block_group(group, false); });
    module.def("event_group_set_errors", set_group_errors, nb::arg("group").none(), nb::arg("ignore").none());
    module.def("event_group_get_errors", [](nb::handle group, nb::handle defining_class) -> nb::object {
        return global("super")(defining_class, group).attr("ignore_callback_errors");
    });
    module.def("event_group_get", [](nb::handle group, nb::handle name) {
        return item(group.attr("_emitters"), name);
    }, nb::arg("group").none(), nb::arg("name").none());
    module.def("event_group_set", [](nb::handle group, nb::handle name, nb::handle emitter) {
        nb::object method = group.attr("add");
        nb::dict kwargs;
        set_item(kwargs, name, emitter);
        return call(method, nb::tuple(), kwargs);
    }, nb::arg("group").none(), nb::arg("name").none(), nb::arg("emitter").none());
    module.def("event_group_emitters", [](nb::handle group) -> nb::object { return group.attr("_emitters"); });
    module.def("event_group_iter", [](nb::handle group) {
        nb::object values = group.attr("_emitters");
        return checked(PyObject_GetIter(values.ptr()));
    });
    module.def("event_make_blocker_all", [](nb::handle group) { return global("EventBlockerAll")(group); });
    module.def("event_warning_initialize", initialize_warning, nb::arg("emitter").none(), nb::arg("defining_class").none(),
               nb::arg("message").none(), nb::arg("category").none(), nb::arg("level").none(), nb::arg("args"), nb::arg("kwargs"));
    module.def("event_warning_warn", warn_callback, nb::arg("emitter").none(), nb::arg("callback").none());
    module.def("event_warning_connect", warning_connect, nb::arg("emitter").none(), nb::arg("callback").none(), nb::arg("args"), nb::arg("kwargs"));
    module.def("event_warning_invoke", warning_invoke, nb::arg("emitter").none(), nb::arg("callback").none(), nb::arg("event").none());
    module.def("event_renamed_initialize", initialize_renamed, nb::arg("emitter").none(), nb::arg("defining_class").none(),
               nb::arg("name").none(), nb::arg("args"), nb::arg("kwargs"));
    module.def("event_renamed_target", new_emitter);
    module.def("event_renamed_connect", renamed_connect, nb::arg("emitter").none(), nb::arg("defining_class").none(),
               nb::arg("callback").none(), nb::arg("args"), nb::arg("kwargs"));
    module.def("event_renamed_disconnect", renamed_disconnect, nb::arg("emitter").none(), nb::arg("defining_class").none(), nb::arg("callback").none());
}
} // namespace nx
