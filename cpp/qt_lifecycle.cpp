#include "qt_lifecycle.hpp"
#include "qt_focus_connection.hpp"

namespace nx {
namespace {
namespace nb = nanobind;

void drain_deferred_callbacks(nb::handle application, nb::handle core,
                              nb::handle deferred, nb::handle event_flags) {
    // Destruction during the first pass can queue slot-proxy cleanup. A
    // second pass releases those callbacks before interpreter finalization.
    for (int pass = 0; pass < 2; ++pass) {
        core.attr("sendPostedEvents")(nb::none(), deferred);
        application.attr("processEvents")(event_flags);
    }
}

void stop_window_workers(nb::handle windows) {
    PyObject* snapshot = PySequence_List(windows.ptr());
    if (!snapshot) throw nb::python_error();
    nb::list owners = nb::steal<nb::list>(snapshot);
    for (nb::handle window : owners) {
        try {
            window.attr("_stop_worker_threads")();
        } catch (const nb::python_error& error) {
            // A Qt wrapper may outlive its deleted C++ window. Preserve the
            // existing shutdown contract: suppress RuntimeError only.
            if (!error.matches(PyExc_RuntimeError)) throw;
        }
    }
}

std::size_t dispose_menu_actions(nb::handle application, nb::handle action_type,
                                nb::handle core, nb::handle deferred,
                                nb::handle event_flags) {
    if (application.is_none()) return 0;
    std::size_t queued = 0;
    nb::object actions = application.attr("findChildren")(action_type);
    for (nb::handle action : nb::iter(actions)) {
        nb::object owner = action.attr("_app");
        nb::object name = owner.attr("name");
        const int belongs = PyObject_RichCompareBool(name.ptr(), nb::str("napari").ptr(), Py_EQ);
        if (belongs < 0) throw nb::python_error();
        if (!belongs) continue;
        action.attr("deleteLater")();
        ++queued;
    }
    // Discard our snapshot before processing destruction callbacks. The
    // QApplication is the actual owner of these shared action singletons.
    actions = nb::object();
    if (queued) {
        drain_deferred_callbacks(application, core, deferred, event_flags);
    }
    return queued;
}

bool disconnect_focus_callback(nb::handle application, nb::handle callback) {
    if (application.is_none() || callback.is_none()) return false;
    try {
        nb::object result = application.attr("focusChanged").attr("disconnect")(callback);
        // PySide reports an already removed connection with False, while
        // PyQt raises and returns None after a successful disconnection.
        return result.ptr() != Py_False;
    } catch (const nb::python_error& error) {
        // The connection or its QApplication may have already been destroyed.
        if (!error.matches(PyExc_TypeError) && !error.matches(PyExc_RuntimeError)) throw;
        return false;
    }
}

void shutdown_open_windows(nb::handle windows) {
    stop_window_workers(windows);
    nb::module_ widgets = nb::module_::import_("qtpy.QtWidgets");
    nb::object application = widgets.attr("QApplication").attr("instance")();
    if (application.is_none()) return;
    bool disconnected = release_focus_connection(application);
    // Qt's signal proxy otherwise retains the Python handler and its entire
    // module namespace after Python has cleared the owning module object.
    // Only detach napari's connection, leaving embedded application handlers.
    PyObject* loop = PyDict_GetItemString(PyImport_GetModuleDict(),
                                        "napari._qt.qt_event_loop");
    if (!disconnected && loop && PyModule_Check(loop)) {
        PyObject* callback = PyDict_GetItemString(PyModule_GetDict(loop), "_focus_changed");
        if (callback) {
            nb::object owner = nb::borrow<nb::object>(callback);
            disconnected = disconnect_focus_callback(application, owner);
        }
    }
    nb::object action_type = nb::module_::import_("app_model.backends.qt._qaction")
                                .attr("QMenuItemAction");
    nb::module_ core = nb::module_::import_("qtpy.QtCore");
    nb::object deferred = core.attr("QEvent").attr("Type").attr("DeferredDelete");
    nb::object flags = core.attr("QEventLoop").attr("ProcessEventsFlag")
                          .attr("ExcludeUserInputEvents");
    const auto queued = dispose_menu_actions(application, action_type,
                                             core.attr("QCoreApplication"), deferred, flags);
    if (disconnected && !queued)
        drain_deferred_callbacks(application, core.attr("QCoreApplication"), deferred, flags);
}
} // namespace

void bind_qt_lifecycle(nb::module_& module) {
    module.def("qt_stop_window_workers", &stop_window_workers, nb::arg("windows").none());
    module.def("qt_dispose_menu_actions", &dispose_menu_actions,
               nb::arg("application").none(), nb::arg("action_type").none(),
               nb::arg("core").none(), nb::arg("deferred").none(),
               nb::arg("event_flags").none());
    module.def("qt_shutdown_open_windows", &shutdown_open_windows, nb::arg("windows").none());
    module.def("qt_disconnect_focus_callback", &disconnect_focus_callback,
               nb::arg("application").none(), nb::arg("callback").none());
}
} // namespace nx
