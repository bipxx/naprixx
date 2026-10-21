#include "qt_focus_connection.hpp"

#include <stdexcept>
#include <utility>

namespace nx {
namespace {
namespace nb = nanobind;
constexpr const char* record_attribute = "_naparixx_focus_connection";

struct QtFocusConnection {
    nb::object callback;
    nb::object connection;
    bool releasing = false;

    explicit QtFocusConnection(nb::object value) : callback(std::move(value)) {}
    QtFocusConnection(QtFocusConnection&&) noexcept = default;
    QtFocusConnection(const QtFocusConnection&) = delete;
    ~QtFocusConnection() { clear(); }

    void clear() noexcept {
        releasing = true;
        // Detach both edges before either payload can run a finalizer.
        nb::object previous_callback = std::move(callback);
        nb::object previous_connection = std::move(connection);
    }
};

int record_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (!nb::inst_ready(nb::handle(self))) return 0;
    const auto* record = nb::inst_ptr<QtFocusConnection>(nb::handle(self));
    Py_VISIT(record->callback.ptr());
    Py_VISIT(record->connection.ptr());
    return 0;
}

int record_clear(PyObject* self) {
    if (nb::inst_ready(nb::handle(self)))
        nb::inst_ptr<QtFocusConnection>(nb::handle(self))->clear();
    return 0;
}

PyType_Slot record_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(record_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(record_clear)},
    {0, nullptr}
};

nb::object existing_record(nb::handle application) {
    auto result = nb::steal<nb::object>(
        PyObject_GetAttrString(application.ptr(), record_attribute));
    if (!result.is_valid()) {
        if (!PyErr_ExceptionMatches(PyExc_AttributeError)) throw nb::python_error();
        PyErr_Clear();
    }
    return result;
}

QtFocusConnection* owned_record(nb::handle value) {
    if (!value.is_valid() || !nb::isinstance<QtFocusConnection>(value) ||
        !nb::inst_ready(value)) return nullptr;
    return nb::inst_ptr<QtFocusConnection>(value);
}

void detach_record(nb::handle application, nb::handle expected) {
    nb::object current = existing_record(application);
    if (current.is_valid() && current.is(expected) &&
        PyObject_DelAttrString(application.ptr(), record_attribute) < 0)
        throw nb::python_error();
}

void connect_focus_callback(nb::handle application, nb::object callback) {
    nb::object previous = existing_record(application);
    if (auto* record = owned_record(previous)) {
        if (record->callback.is_valid()) {
            if (!record->releasing && record->callback.is(callback)) return;
            throw std::runtime_error("a focus callback is already connected");
        }
    } else if (previous.is_valid() && !previous.is_none()) {
        throw std::runtime_error("the focus connection record is already occupied");
    }

    nb::object holder = nb::cast(QtFocusConnection(callback));
    auto& record = *nb::inst_ptr<QtFocusConnection>(holder);
    nb::object signal = application.attr("focusChanged");
    record.connection = signal.attr("connect")(callback);
    try {
        if (PyObject_SetAttrString(application.ptr(), record_attribute, holder.ptr()) < 0)
            throw nb::python_error();
    } catch (...) {
        // A failed setter may have published before raising. Roll back only
        // our callback and our record while preserving the original failure.
        try { signal.attr("disconnect")(callback); } catch (...) { }
        try { detach_record(application, holder); } catch (...) { }
        record.clear();
        throw;
    }
}
} // namespace

bool release_focus_connection(nb::handle application) {
    if (application.is_none()) return false;
    nb::object holder = existing_record(application);
    auto* record = owned_record(holder);
    if (!record || !record->callback.is_valid() || record->releasing) return false;

    record->releasing = true;
    try {
        try {
            application.attr("focusChanged").attr("disconnect")(record->callback);
        } catch (const nb::python_error& error) {
            // Qt may already have destroyed the connection or its receiver.
            if (!error.matches(PyExc_TypeError) && !error.matches(PyExc_RuntimeError)) throw;
        }
        detach_record(application, holder);
    } catch (...) {
        record->releasing = false;
        throw;
    }
    record->clear();
    return true;
}

void bind_qt_focus_connection(nb::module_& module) {
    nb::class_<QtFocusConnection>(module, "QtFocusConnection",
            nb::is_weak_referenceable(), nb::type_slots(record_slots))
        .def_prop_ro("callback", [](const QtFocusConnection& record) -> nb::object {
            return record.callback.is_valid() ? record.callback : nb::none();
        })
        .def_prop_ro("connection", [](const QtFocusConnection& record) -> nb::object {
            return record.connection.is_valid() ? record.connection : nb::none();
        });
    module.def("qt_connect_focus_callback", &connect_focus_callback,
               nb::arg("application"), nb::arg("callback").none());
    module.def("qt_release_focus_connection", &release_focus_connection,
               nb::arg("application").none());
}
} // namespace nx
