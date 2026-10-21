// Tracing behavior adapted from napari; retained BSD notices are in licenses/.
#include "event_bindings.hpp"
#include "event_protocol.hpp"

namespace nx {
namespace {
using namespace events;

nb::object debug_global(const char* name) {
    nb::module_ module = nb::module_::import_("napari.utils.events.debugging");
    PyObject* dictionary = PyModule_GetDict(module.ptr());
    if (!dictionary) throw nb::python_error();
    PyObject* value = PyDict_GetItemString(dictionary, name);
    if (value) return nb::borrow<nb::object>(value);
    return nb::module_::import_("builtins").attr(name);
}

nb::object shorten_filename(nb::handle supplied) {
    nb::object replace = supplied.attr("replace");
    nb::object name = replace(debug_global("_SP"), ".../site-packages");
    replace = name.attr("replace");
    name = replace(debug_global("_STD_LIB"), ".../python");
    replace = name.attr("replace");
    return replace(debug_global("ROOT_DIR"), "napari");
}

void set_tracing(nb::handle enabled, nb::handle cfg) {
    nb::module_ module = nb::module_::import_("napari.utils.events.event");
    nb::object callback;
    if (truth(enabled)) {
        callback = nb::module_::import_("napari.utils.events.debugging").attr("log_event_stack");
        if (!cfg.is_none()) callback = global("partial")(callback, nb::arg("cfg") = cfg);
    } else callback = global("_noop");
    set_attr(module, "_log_event_stack", callback);
}

void environment_tracing() {
    nb::object value = global("os").attr("getenv")("NAPARI_DEBUG_EVENTS", "");
    value = value.attr("lower")();
    if (contains(nb::make_tuple("1", "true"), value))
        global("set_event_tracing_enabled")(nb::bool_(true));
}

nb::object trace_pop(nb::handle event, nb::handle cfg) {
    nb::object depth = cfg.attr("_cur_depth");
    depth = checked(PyNumber_InPlaceSubtract(depth.ptr(), nb::int_(1).ptr()));
    set_attr(cfg, "_cur_depth", depth);
    return event.attr("_sources").attr("pop")();
}

void trace_event(nb::handle event, nb::handle cfg) {
    nb::object include = cfg.attr("include_events");
    if (truth(include)) {
        nb::object name = event.attr("type");
        include = cfg.attr("include_events");
        if (!contains(include, name)) return;
    } else {
        nb::object name = event.attr("type");
        nb::object exclude = cfg.attr("exclude_events");
        if (contains(exclude, name)) return;
    }
    nb::object type = debug_global("type");
    nb::object source = event.attr("source");
    type = type(source);
    nb::object source_name = type.attr("__name__");
    include = cfg.attr("include_emitters");
    if (truth(include)) {
        include = cfg.attr("include_emitters");
        if (!contains(include, source_name)) return;
    } else {
        nb::object exclude = cfg.attr("exclude_emitters");
        if (contains(exclude, source_name)) return;
    }
    nb::list values;
    nb::object kwargs = event.attr("_kwargs");
    nb::object items = kwargs.attr("items")();
    for (nb::handle supplied : nb::iter(items)) {
        nb::tuple entry = pair(supplied);
        nb::object key = entry[0];
        nb::object value = entry[1];
        values.append(message({formatted(key), nb::str("="), formatted(value)}));
    }
    nb::object joined = nb::str(",").attr("join")(values);
    nb::object name = event.attr("type");
    nb::list lines;
    lines.append(message({formatted(source_name), nb::str(".events."), formatted(name),
                          nb::str("("), joined, nb::str(")")}));
    nb::object stack = debug_global("inspect").attr("stack")(0);
    nb::object depth = cfg.attr("stack_depth");
    nb::object visible = slice(stack, nb::int_(2), add(nb::int_(2), depth));
    for (nb::handle frame : nb::iter(visible)) {
        nb::object shorten = debug_global("_shorten_fname");
        nb::object filename = frame.attr("filename");
        filename = shorten(filename);
        nb::object owner = nb::str("");
        nb::object locals = frame.attr("frame").attr("f_locals");
        if (contains(locals, nb::str("self"))) {
            type = debug_global("type");
            locals = frame.attr("frame").attr("f_locals");
            type = type(item(locals, nb::str("self")));
            owner = add(type.attr("__name__"), nb::str("."));
        }
        nb::object line = frame.attr("lineno");
        nb::object function = frame.attr("function");
        lines.append(message({nb::str("  \""), formatted(filename), nb::str("\", line "),
                              formatted(line), nb::str(", in "), formatted(owner), formatted(function)}));
    }
    lines.append(nb::str(""));
    nb::object reversed = debug_global("reversed")(stack);
    for (nb::handle frame : nb::iter(reversed)) {
        nb::object locals = frame.attr("frame").attr("f_locals");
        if (!contains(locals, nb::str("self"))) continue;
        type = debug_global("type");
        locals = frame.attr("frame").attr("f_locals");
        type = type(item(locals, nb::str("self")));
        nb::object module = type.attr("__module__");
        if (!truth(module)) module = nb::str("");
        if (truth(module.attr("startswith")("napari"))) {
            nb::object owner = type.attr("__name__");
            nb::object function = frame.attr("function");
            nb::object trigger = message({formatted(owner), nb::str("."), formatted(function), nb::str("()")});
            lines.attr("insert")(1, message({nb::str("  was triggered by "), trigger, nb::str(", via:")}));
            break;
        }
    }
    depth = cfg.attr("_cur_depth");
    if (!truth(depth)) {
        nb::list prefixed;
        prefixed.append(checked(PyNumber_Multiply(nb::str("\xe2\x94\x80").ptr(), nb::int_(79).ptr())));
        prefixed.append(nb::str(""));
        for (nb::handle line : lines) prefixed.append(line);
        lines = prefixed;
    } else if (!truth(cfg.attr("nesting_allowance"))) return;
    nb::object print = debug_global("print");
    nb::object indent = debug_global("indent");
    joined = nb::str("\n").attr("join")(lines);
    depth = cfg.attr("_cur_depth");
    nb::object prefix = checked(PyNumber_Multiply(nb::str("  ").ptr(), depth.ptr()));
    print(indent(joined, prefix));
    nb::object callback = debug_global("_make_pop_source")(event, cfg);
    set_attr(event, "_pop_source", callback);
    depth = cfg.attr("_cur_depth");
    depth = checked(PyNumber_InPlaceAdd(depth.ptr(), nb::int_(1).ptr()));
    set_attr(cfg, "_cur_depth", depth);
}
} // namespace

void bind_event_tracing(nb::module_& module) {
    module.def("event_tracing_set", set_tracing, nb::arg("enabled").none(), nb::arg("cfg").none());
    module.def("event_tracing_environment", environment_tracing);
    module.def("event_trace", trace_event, nb::arg("event").none(), nb::arg("cfg").none());
    module.def("event_trace_pop", trace_pop, nb::arg("event").none(), nb::arg("cfg").none());
    module.def("event_shorten_filename", shorten_filename, nb::arg("filename").none());
}
} // namespace nx
