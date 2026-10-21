#include "arrays.hpp"
#include "event_protocol.hpp"
#include <nanobind/stl/string.h>
#include <array>
#include <utility>

namespace nx {
namespace {
nb::object sw_check(PyObject* value) {
    if (!value) throw nb::python_error();
    return nb::steal<nb::object>(value);
}
nb::object sw_attr(nb::object value, const char* name) {
    return sw_check(PyObject_GetAttrString(value.ptr(), name));
}
nb::object sw_call0(nb::object function) { return sw_check(PyObject_CallNoArgs(function.ptr())); }
nb::object sw_call1(nb::object function, nb::object value) {
    return sw_check(PyObject_CallOneArg(function.ptr(), value.ptr()));
}
void sw_store(nb::object self, const char* name, nb::object value) {
    if (PyObject_SetAttrString(self.ptr(), name, value.ptr()) < 0) throw nb::python_error();
    self.reset(); value.reset();
}
void sw_assign(nb::object& target, nb::object value) {
    nb::object previous = std::move(target); target = std::move(value); previous.reset();
}
bool sw_truth(nb::object value) {
    int result = PyObject_IsTrue(value.ptr());
    if (result < 0) throw nb::python_error();
    return result != 0;
}
nb::object sw_item(nb::object self, nb::object key) { return sw_check(PyObject_GetItem(self.ptr(), key.ptr())); }
nb::object sw_optional(const nb::object& value) { return value.is_valid() ? value : nb::none(); }
struct StatusNamespace {
    nb::dict globals;
    nb::object builtins;
    explicit StatusNamespace(nb::dict value) : globals(std::move(value)), builtins(nb::borrow<nb::object>(PyEval_GetBuiltins())) {}
    nb::object get(const char* name) const {
        nb::object key = nb::str(name);
        PyObject* value = PyDict_GetItemWithError(globals.ptr(), key.ptr());
        if (value) return nb::borrow<nb::object>(value);
        if (PyErr_Occurred()) throw nb::python_error();
        value = PyObject_GetItem(builtins.ptr(), key.ptr());
        if (value) return nb::steal<nb::object>(value);
        if (!PyErr_ExceptionMatches(PyExc_KeyError)) throw nb::python_error();
        PyErr_Clear();
        nb::object message = sw_check(PyUnicode_FromFormat("name '%s' is not defined", name));
        nb::object error = sw_call1(nb::borrow<nb::object>(PyExc_NameError), std::move(message));
        sw_store(error, "name", key); PyErr_SetObject(PyExc_NameError, error.ptr()); throw nb::python_error();
    }
};
void sw_except_type(nb::handle value) {
    bool valid = PyExceptionClass_Check(value.ptr());
    if (PyTuple_Check(value.ptr())) {
        valid = true;
        for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(value.ptr()); ++i)
            if (!PyExceptionClass_Check(PyTuple_GET_ITEM(value.ptr(), i))) { valid = false; break; }
    }
    if (!valid) {
        PyErr_SetString(PyExc_TypeError, "catching classes that do not inherit from BaseException is not allowed");
        throw nb::python_error();
    }
}
nb::object sw_super(nb::object provider, nb::object self, const char* method) {
    return sw_attr(sw_call1(std::move(provider), std::move(self)), method);
}
// Only assigned source locals. Actual QThread/model dictionaries and Qt signal
// wrappers remain sole authorities; this is neither worker state nor a registry.
struct ViewerStatusLocals {
    nb::object viewer, res, e, active;
    ViewerStatusLocals() = default;
    ViewerStatusLocals(ViewerStatusLocals&&) = default;
    ~ViewerStatusLocals() { clear(); }
    void clear() {
        std::array<nb::object, 4> values = {std::move(viewer), std::move(res), std::move(e), std::move(active)};
        for (auto& value : values) value.reset();
    }
};
int sw_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(nb::handle(self))) {
        auto& s = *nb::inst_ptr<ViewerStatusLocals>(nb::handle(self));
        Py_VISIT(s.viewer.ptr()); Py_VISIT(s.res.ptr()); Py_VISIT(s.e.ptr()); Py_VISIT(s.active.ptr());
    }
    return 0;
}
int sw_clear(PyObject* self) {
    if (nb::inst_ready(nb::handle(self))) nb::inst_ptr<ViewerStatusLocals>(nb::handle(self))->clear();
    return 0;
}
PyType_Slot sw_slots[] = {{Py_tp_traverse, reinterpret_cast<void*>(sw_traverse)}, {Py_tp_clear, reinterpret_cast<void*>(sw_clear)}, {0, nullptr}};

void status_init(nb::object self, nb::object viewer, nb::object parent, nb::dict namespace_, nb::object super_provider, nb::object object_name) {
    StatusNamespace ns(std::move(namespace_));
    nb::object init = sw_super(std::move(super_provider), self, "__init__");
    nb::dict kwargs; kwargs["parent"] = parent;
    sw_check(PyObject_Call(init.ptr(), nb::tuple().ptr(), kwargs.ptr())); init.reset(); kwargs.reset();
    nb::object factory = ns.get("ref"); sw_store(self, "viewer_ref", sw_call1(std::move(factory), viewer));
    factory = ns.get("Event"); sw_store(self, "_need_status_update", sw_call0(std::move(factory)));
    sw_call0(sw_attr(sw_attr(self, "_need_status_update"), "clear"));
    sw_store(self, "_terminate", nb::bool_(false)); sw_store(self, "_allow_start", nb::bool_(true));
    nb::object setter = sw_attr(self, "setObjectName"); sw_call1(std::move(setter), std::move(object_name));
}
void status_trigger(nb::object self, nb::dict) { sw_call0(sw_attr(sw_attr(self, "_need_status_update"), "set")); }
void status_run(nb::object self, nb::dict) {
    while (!sw_truth(sw_attr(self, "_terminate"))) {
        bool unavailable = sw_call0(sw_attr(self, "viewer_ref")).is_none();
        if (unavailable) return;
        if (sw_truth(sw_call0(sw_attr(sw_attr(self, "_need_status_update"), "is_set")))) {
            sw_call0(sw_attr(sw_attr(self, "_need_status_update"), "clear"));
            sw_call0(sw_attr(self, "calculate_status"));
        } else sw_call0(sw_attr(sw_attr(self, "_need_status_update"), "wait"));
    }
}
void status_calculate(nb::object self, nb::dict namespace_, ViewerStatusLocals& s) {
    StatusNamespace ns(std::move(namespace_));
    sw_assign(s.viewer, sw_call0(sw_attr(self, "viewer_ref")));
    if (s.viewer.is_none()) return;
    try { sw_assign(s.res, sw_call0(sw_attr(s.viewer, "_calc_status_from_cursor"))); }
    catch (nb::python_error& caught) {
        // Python appends the genuine calculate_status facade frame before
        // selecting a handler. Its self and locals_owner retain source leases.
        caught.restore();
        PyFrameObject* frame = PyEval_GetFrame();
        if (!frame) {
            PyErr_SetString(PyExc_RuntimeError, "status calculation requires its genuine Python facade frame");
            throw nb::python_error();
        }
        if (PyTraceBack_Here(frame) < 0) throw nb::python_error();
        nb::python_error error;
        {
            events::HandledException handled(error.value());
            nb::object kind = ns.get("Exception"); sw_except_type(kind);
            if (!PyErr_GivenExceptionMatches(error.value().ptr(), kind.ptr())) throw error;
            kind.reset();
            sw_assign(s.e, nb::borrow<nb::object>(error.value()));
            try {
                nb::object dispatch = sw_attr(ns.get("notification_manager"), "dispatch");
                nb::object from_exception = sw_attr(ns.get("Notification"), "from_exception");
                nb::object notification = sw_call1(std::move(from_exception), s.e);
                sw_call1(std::move(dispatch), std::move(notification));
            } catch (...) { s.e.reset(); throw; }
        }
        // POP_EXCEPT precedes DELETE_FAST e on this successful return.
        s.e.reset(); return;
    }
    nb::object emit = sw_attr(sw_attr(self, "status_and_tooltip_changed"), "emit");
    sw_call1(std::move(emit), s.res);
}
void status_toggle(nb::object self, nb::object event, nb::dict) {
    if (sw_truth(sw_attr(event, "value"))) sw_call0(sw_attr(sw_attr(self, "status_thread"), "start"));
    else sw_call0(sw_attr(sw_attr(self, "status_thread"), "terminate"));
}
void status_deliver(nb::object self, nb::object status_and_tooltip, nb::dict, ViewerStatusLocals& s) {
    if (status_and_tooltip.is_none()) return;
    nb::object value = sw_item(status_and_tooltip, nb::int_(0));
    nb::object viewer = sw_attr(sw_attr(self, "_qt_viewer"), "viewer"); sw_store(std::move(viewer), "status", std::move(value));
    value = sw_item(status_and_tooltip, nb::int_(1));
    nb::object tooltip = sw_attr(sw_attr(sw_attr(self, "_qt_viewer"), "viewer"), "tooltip"); sw_store(std::move(tooltip), "text", std::move(value));
    sw_assign(s.active, sw_attr(sw_attr(sw_attr(sw_attr(sw_attr(self, "_qt_viewer"), "viewer"), "layers"), "selection"), "active"));
    if (!s.active.is_none()) {
        value = sw_attr(s.active, "help"); viewer = sw_attr(sw_attr(self, "_qt_viewer"), "viewer"); sw_store(std::move(viewer), "help", std::move(value));
    }
}
void status_asv(nb::dict namespace_, nb::object generator_factory, nb::object env_name, nb::object env_true) {
    StatusNamespace ns(std::move(namespace_));
    nb::object get = sw_attr(sw_attr(ns.get("os"), "environ"), "get");
    nb::object value = sw_call1(std::move(get), std::move(env_name));
    bool enabled = sw_truth(sw_check(PyObject_RichCompare(value.ptr(), env_true.ptr(), Py_EQ))); value.reset();
    if (!enabled) return;
    nb::object function = sw_call0(std::move(generator_factory));
    sw_store(function, "__name__", nb::str("<lambda>")); sw_store(function, "__qualname__", nb::str("<lambda>"));
    sw_store(ns.get("StatusChecker"), "start", std::move(function));
}
} // namespace
void bind_viewer_status_workers(nb::module_& m) {
    nb::class_<ViewerStatusLocals>(m, "ViewerStatusLocals", nb::type_slots(sw_slots), nb::is_weak_referenceable())
        .def_prop_ro("viewer", [](const ViewerStatusLocals& s) { return sw_optional(s.viewer); })
        .def_prop_ro("res", [](const ViewerStatusLocals& s) { return sw_optional(s.res); })
        .def_prop_ro("e", [](const ViewerStatusLocals& s) { return sw_optional(s.e); })
        .def_prop_ro("active", [](const ViewerStatusLocals& s) { return sw_optional(s.active); });
    m.def("viewer_status_locals", [] { return ViewerStatusLocals(); });
    m.def("viewer_status_init", &status_init, nb::arg("self").none(), nb::arg("viewer").none(), nb::arg("parent").none(), nb::arg("namespace"), nb::arg("super_provider").none(), nb::arg("object_name").none());
    m.def("viewer_status_trigger", &status_trigger, nb::arg("self").none(), nb::arg("namespace"));
    m.def("viewer_status_run", &status_run, nb::arg("self").none(), nb::arg("namespace"));
    m.def("viewer_status_calculate", &status_calculate, nb::arg("self").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    m.def("viewer_status_toggle", &status_toggle, nb::arg("self").none(), nb::arg("event").none(), nb::arg("namespace"));
    m.def("viewer_status_deliver", &status_deliver, nb::arg("self").none(), nb::arg("status_and_tooltip").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    m.def("viewer_status_asv", &status_asv, nb::arg("namespace"), nb::arg("generator_factory").none(), nb::arg("env_name").none(), nb::arg("env_true").none());
}
} // namespace nx
