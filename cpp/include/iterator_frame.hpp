#pragma once

#include <nanobind/nanobind.h>
#include <utility>

namespace nx::collections {
namespace nb = nanobind;

// References used by a suspended native iterator belong to that iterator.
// The dictionary remains a live diagnostic view, with no independent snapshot.
struct IteratorFrame {
    nb::object frame_owner = nb::dict();

    nb::dict frame() const {
        return frame_owner.is_valid()
            ? nb::borrow<nb::dict>(frame_owner) : nb::dict();
    }

    void clear_frame() {
        if (frame_owner.is_valid()) PyDict_Clear(frame_owner.ptr());
    }

    void release_frame() {
        // Detach before releasing payloads: their finalizers may call back into
        // this iterator. Its gc_clear() has already marked it exhausted.
        nb::object previous = std::move(frame_owner);
        if (previous.is_valid()) PyDict_Clear(previous.ptr());
    }
};

template <typename State>
int iterator_frame_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(nb::handle(self)))
        Py_VISIT(nb::inst_ptr<State>(nb::handle(self))->frame_owner.ptr());
    return 0;
}

template <typename State>
int iterator_frame_clear(PyObject* self) {
    if (nb::inst_ready(nb::handle(self)))
        nb::inst_ptr<State>(nb::handle(self))->gc_clear();
    return 0;
}

template <typename State>
PyType_Slot* iterator_frame_slots() {
    static PyType_Slot slots[] = {
        {Py_tp_traverse, reinterpret_cast<void*>(iterator_frame_traverse<State>)},
        {Py_tp_clear, reinterpret_cast<void*>(iterator_frame_clear<State>)},
        {0, nullptr},
    };
    return slots;
}
} // namespace nx::collections
