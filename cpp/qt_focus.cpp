#include "qt_focus.hpp"

namespace nx {
namespace {
namespace nb = nanobind;

nb::object checked(PyObject* value) {
    if (!value) throw nb::python_error();
    return nb::steal<nb::object>(value);
}

bool truth(nb::handle value) {
    const int result = PyObject_IsTrue(value.ptr());
    if (result < 0) throw nb::python_error();
    return result != 0;
}

nb::object global(nb::dict namespace_, const char* name) {
    if (PyObject* value = PyDict_GetItemString(namespace_.ptr(), name))
        return nb::borrow<nb::object>(value);
    PyObject* builtin_source = PyDict_GetItemString(namespace_.ptr(), "__builtins__");
    nb::object builtins = builtin_source ? nb::borrow<nb::object>(builtin_source)
                                        : nb::module_::import_("builtins");
    PyObject* dictionary = PyModule_Check(builtins.ptr())
                               ? PyModule_GetDict(builtins.ptr()) : builtins.ptr();
    if (PyDict_Check(dictionary)) {
        if (PyObject* value = PyDict_GetItemString(dictionary, name))
            return nb::borrow<nb::object>(value);
    }
    PyErr_Format(PyExc_NameError, "name '%s' is not defined", name);
    throw nb::python_error();
}

void focus_changed(nb::handle old, nb::handle next, nb::dict namespace_) {
    if (!old.is_none() && !next.is_none()) return;
    if (old.is_none() && next.is_none()) return;
    {
        nb::object isinstance = global(namespace_, "isinstance");
        nb::object widget_type = global(namespace_, "QWidget");
        if (!truth(isinstance(old, widget_type))) return;
    }

    // Keep the original guard order: a normal QWidget check rejects None
    // before the focus-gained branch, and window() precedes the lazy import.
    const bool start_timer = old.is_none();
    nb::object window = (start_timer ? next : old).attr("window")();
    nb::object main_window_type = nb::module_::import_("napari._qt.qt_main_window")
                                     .attr("_QtMainWindow");
    {
        nb::object isinstance = global(namespace_, "isinstance");
        if (!truth(isinstance(window, main_window_type))) return;
    }
    nb::object notifications = global(namespace_, "NapariQtNotification").attr("_instances");
    if (!truth(notifications)) return;
    if (start_timer) {
        nb::object notification = checked(PyObject_GetItem(notifications.ptr(), nb::int_(-1).ptr()));
        notification.attr("timer_start")();
    } else {
        // Use the provider's live iterator: timer callbacks may mutate its
        // collection or recursively deliver another focus transition.
        nb::object iterator = checked(PyObject_GetIter(notifications.ptr()));
        while (PyObject* value = PyIter_Next(iterator.ptr())) {
            nb::object notification = nb::steal<nb::object>(value);
            notification.attr("timer_stop")();
        }
        if (PyErr_Occurred()) throw nb::python_error();
    }
}
} // namespace

void bind_qt_focus(nb::module_& module) {
    module.def("qt_focus_changed", &focus_changed, nb::arg("old").none(),
               nb::arg("new").none(), nb::arg("namespace"));
}
} // namespace nx
