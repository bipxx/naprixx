#include "arrays.hpp"

#include <utility>

namespace nx {
namespace {
nb::object checked(PyObject* value) {
    if (!value) throw nb::python_error();
    return nb::steal<nb::object>(value);
}
bool truth(nb::handle value) {
    const int result = PyObject_IsTrue(value.ptr());
    if (result < 0) throw nb::python_error();
    return result != 0;
}
bool equal(nb::handle a, nb::handle b) {
    const int result = PyObject_RichCompareBool(a.ptr(), b.ptr(), Py_EQ);
    if (result < 0) throw nb::python_error();
    return result != 0;
}
nb::tuple tuple(nb::handle value) { return nb::cast<nb::tuple>(checked(PySequence_Tuple(value.ptr()))); }
class Lock {
public:
    explicit Lock(nb::handle value) : object(nb::borrow<nb::object>(value)) { object.attr("__enter__")(); }
    ~Lock() {
        PyObject* result = PyObject_CallMethod(object.ptr(), "__exit__", "OOO", Py_None, Py_None, Py_None);
        if (result) Py_DECREF(result);
        else PyErr_WriteUnraisable(object.ptr());
    }
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;
private:
    nb::object object;
};
nb::object find_task(nb::handle self, nb::handle layers) {
    Lock guard(self.attr("_lock_futures_dicts"));
    auto layer_set = checked(PySet_New(layers.ptr()));
    auto tasks = nb::borrow<nb::dict>(self.attr("_layers_to_task"));
    for (auto entry : tasks) {
        auto task_layers = checked(PySet_New(nullptr));
        for (auto weak_layer : nb::iter(entry.first)) {
            auto layer = nb::borrow<nb::object>(weak_layer)();
            if (!layer.is_none() && PySet_Add(task_layers.ptr(), layer.ptr()) < 0) throw nb::python_error();
        }
        if (truth(task_layers.attr("issubset")(layer_set))) return nb::borrow<nb::object>(entry.second);
    }
    return nb::none();
}
bool remove_task(nb::handle self, nb::handle task) {
    Lock guard(self.attr("_lock_futures_dicts"));
    auto tasks = nb::borrow<nb::dict>(self.attr("_layers_to_task"));
    for (auto entry : tasks) if (equal(entry.second, task)) {
        if (PyDict_DelItem(tasks.ptr(), entry.first.ptr()) < 0) throw nb::python_error();
        return true;
    }
    return false;
}
nb::object submit(nb::handle self, nb::handle layers, nb::handle dims, bool force) {
    // Snapshot one-shot iterators before searching pending tasks or dispatching.
    auto all_layers = tuple(layers);
    auto task = self.attr("_find_existing_task")(all_layers);
    if (truth(task)) task.attr("cancel")();
    nb::object weakref = nb::module_::import_("weakref").attr("ref");
    nb::object async_type = nb::module_::import_("napari.components._layer_slicer").attr("_AsyncSliceable");
    nb::dict requests;
    nb::list sync;
    for (auto layer : all_layers) {
        nb::object state = layer.attr("_slicing_state");
        const int is_async = PyObject_IsInstance(state.ptr(), async_type.ptr());
        if (is_async < 0) throw nb::python_error();
        if (is_async && !truth(self.attr("_force_sync")) && truth(layer.attr("visible"))) {
            auto request = state.attr("_make_slice_request")(dims);
            requests[weakref(layer)] = request;
            state.attr("_set_unloaded_slice_id")(request.attr("id"));
        } else sync.append(layer);
    }
    task = nb::none();
    if (!requests.empty()) {
        task = self.attr("_executor").attr("submit")(self.attr("_slice_layers"), requests);
        {
            Lock guard(self.attr("_lock_futures_dicts"));
            self.attr("_layers_to_task")[tuple(requests)] = task;
            self.attr("_task_to_requests")[task] = requests;
        }
        task.attr("add_done_callback")(self.attr("_on_slice_done"));
    }
    for (auto layer : sync) layer.attr("_slice_dims")(nb::arg("dims") = dims, nb::arg("force") = force);
    return task;
}
nb::dict slice_layers(nb::handle self, nb::dict requests) {
    nb::dict result;
    for (auto entry : requests) result[entry.first] = nb::borrow<nb::object>(entry.second)();
    self.attr("events").attr("ready")(nb::arg("value") = result);
    return result;
}
void done(nb::handle self, nb::handle task) {
    nb::object requests;
    {
        Lock guard(self.attr("_lock_futures_dicts"));
        requests = self.attr("_task_to_requests").attr("pop")(task, nb::none());
    }
    self.attr("_try_to_remove_task")(task);
    if (truth(task.attr("cancelled")())) return;
    auto error = task.attr("exception")();
    if (!truth(error)) return;
    if (truth(requests)) for (auto entry : nb::cast<nb::dict>(requests)) {
        auto layer = nb::borrow<nb::object>(entry.first)();
        if (truth(layer)) layer.attr("_slicing_state").attr("_update_loaded_slice_id")(entry.second.attr("id"));
    }
    nb::module_::import_("napari.utils.notifications").attr("notification_manager").attr("receive_error")(
        nb::borrow<nb::object>(reinterpret_cast<PyObject*>(Py_TYPE(error.ptr()))), error, error.attr("__traceback__"));
}
void wait_idle(nb::handle self, nb::handle timeout) {
    nb::tuple futures;
    {
        Lock guard(self.attr("_lock_futures_dicts"));
        futures = tuple(self.attr("_layers_to_task").attr("values")());
    }
    auto result = nb::module_::import_("concurrent.futures").attr("wait")(futures, nb::arg("timeout") = timeout);
    nb::object remaining = result[nb::int_(1)];
    if (nb::len(remaining)) {
        PyErr_Format(PyExc_TimeoutError, "Slicing %zd tasks did not complete within timeout (%Rs).",
            static_cast<Py_ssize_t>(nb::len(remaining)), timeout.ptr());
        throw nb::python_error();
    }
}
void shutdown(nb::handle self) {
    self.attr("_executor").attr("shutdown")(nb::arg("wait") = true, nb::arg("cancel_futures") = true);
    self.attr("events").attr("disconnect")();
    self.attr("events").attr("ready").attr("disconnect")();
}

struct ResponsePair {
    nb::object weak_layer;
    nb::object response;
};
ResponsePair unpack_response_pair(nb::handle pair) {
    PyObject* raw_iterator = PyObject_GetIter(pair.ptr());
    if (!raw_iterator) {
        if (PyErr_ExceptionMatches(PyExc_TypeError) &&
                !Py_TYPE(pair.ptr())->tp_iter && !PySequence_Check(pair.ptr())) {
            PyErr_Format(PyExc_TypeError, "cannot unpack non-iterable %.200s object",
                         Py_TYPE(pair.ptr())->tp_name);
        }
        throw nb::python_error();
    }
    nb::object iterator = nb::steal<nb::object>(raw_iterator);
    auto next = [&](int count) {
        PyObject* value = PyIter_Next(iterator.ptr());
        if (!value) {
            if (!PyErr_Occurred()) {
                PyErr_Format(PyExc_ValueError,
                             "not enough values to unpack (expected 2, got %d)", count);
            }
            throw nb::python_error();
        }
        return nb::steal<nb::object>(value);
    };
    nb::object weak_layer = next(0);
    nb::object response = next(1);
    nb::object extra = nb::steal<nb::object>(PyIter_Next(iterator.ptr()));
    if (extra.ptr()) {
        // Python unpacking drops the extra value before raising ValueError.
        extra = nb::object();
        PyErr_SetString(PyExc_ValueError, "too many values to unpack (expected 2)");
        throw nb::python_error();
    }
    if (PyErr_Occurred()) throw nb::python_error();
    return {std::move(weak_layer), std::move(response)};
}

nb::object namespace_item(nb::handle mapping, nb::handle key) {
    if (PyDict_CheckExact(mapping.ptr())) {
        PyObject* value = PyDict_GetItemWithError(mapping.ptr(), key.ptr());
        if (value) return nb::borrow<nb::object>(value);
        if (PyErr_Occurred()) throw nb::python_error();
        return nb::object();
    }
    PyObject* value = PyObject_GetItem(mapping.ptr(), key.ptr());
    if (value) return nb::steal<nb::object>(value);
    if (!PyErr_ExceptionMatches(PyExc_KeyError)) throw nb::python_error();
    PyErr_Clear();
    return nb::object();
}

nb::object executing_global(nb::dict namespace_, const char* name) {
    nb::object key = nb::str(name);
    nb::object value = namespace_item(namespace_, key);
    if (value.ptr()) return value;
    // The calling facade's frame retains its original builtins independently
    // of later changes to the namespace's __builtins__ entry or sys.modules.
    nb::object builtins = nb::borrow<nb::object>(PyEval_GetBuiltins());
    value = namespace_item(builtins, key);
    if (value.ptr()) return value;
    nb::object message = checked(PyUnicode_FromFormat("name '%s' is not defined", name));
    nb::object error = checked(PyObject_CallOneArg(PyExc_NameError, message.ptr()));
    if (PyObject_SetAttrString(error.ptr(), "name", key.ptr()) < 0) throw nb::python_error();
    PyErr_SetObject(PyExc_NameError, error.ptr());
    throw nb::python_error();
}

void qt_viewer_apply_slice_responses(nb::handle viewer, nb::handle event, nb::dict namespace_) {
    [[maybe_unused]] nb::object viewer_owner = nb::borrow<nb::object>(viewer);
    nb::object event_owner = nb::borrow<nb::object>(event);
    nb::object responses = event_owner.attr("value");
    executing_global(namespace_, "logging").attr("getLogger")("napari")
        .attr("debug")("QtViewer._on_slice_ready: %s", responses);
    nb::object iterator = checked(PyObject_GetIter(responses.attr("items")().ptr()));
    // Match Python loop locals: the previous values survive the next iteration
    // and are replaced only after the new pair has been unpacked successfully.
    nb::object weak_layer, response, layer;
    for (;;) {
        PyObject* raw_pair = PyIter_Next(iterator.ptr());
        if (!raw_pair) {
            if (PyErr_Occurred()) throw nb::python_error();
            break;
        }
        nb::object pair = nb::steal<nb::object>(raw_pair);
        auto values = unpack_response_pair(pair);
        weak_layer = std::move(values.weak_layer);
        response = std::move(values.response);
        layer = weak_layer();
        if (!truth(layer)) continue;
        layer.attr("_slicing_state").attr("_update_slice_response")(response);
        // Response and loaded callbacks can replace the layer's slicing state.
        // Retrieve the loaded hook again after applying the response.
        {
            nb::object loaded = layer.attr("_slicing_state").attr("_update_loaded_slice_id");
            nb::object result = loaded(response.attr("request_id"));
            loaded = nb::object();
            result = nb::object();
        }
        layer.attr("events").attr("set_data")();
        layer.attr("_refresh_sync")(
            nb::arg("data_displayed") = false, nb::arg("thumbnail") = true,
            nb::arg("highlight") = true, nb::arg("extent") = false);
    }
}

void layer_update_loaded_slice_id(nb::handle state, nb::handle id) {
    nb::object owner = nb::borrow<nb::object>(state);
    nb::object slice_id = nb::borrow<nb::object>(id);
    bool notify;
    {
        nb::object changed = owner.attr("_load_state").attr("finish")(slice_id);
        notify = truth(changed);
    }
    if (notify) owner.attr("loaded_data").attr("emit")();
}

void layer_set_unloaded_slice_id(nb::handle state, nb::handle id) {
    nb::object owner = nb::borrow<nb::object>(state);
    nb::object slice_id = nb::borrow<nb::object>(id);
    bool notify;
    {
        nb::object changed = owner.attr("_load_state").attr("begin")(slice_id);
        notify = truth(changed);
    }
    if (notify) owner.attr("loaded_data").attr("emit")();
}

void layer_refresh_sync(nb::handle layer, nb::dict namespace_, nb::handle thumbnail, nb::handle data_displayed,
                        nb::handle highlight, nb::handle extent, nb::handle force) {
    nb::object owner = nb::borrow<nb::object>(layer);
    nb::object thumbnail_flag = nb::borrow<nb::object>(thumbnail);
    nb::object data_flag = nb::borrow<nb::object>(data_displayed);
    nb::object highlight_flag = nb::borrow<nb::object>(highlight);
    nb::object extent_flag = nb::borrow<nb::object>(extent);
    nb::object force_flag = nb::borrow<nb::object>(force);
    executing_global(namespace_, "logger")
        .attr("debug")("Layer._refresh_sync: %s", owner);
    const bool visible = truth(owner.attr("visible"));
    if (!visible && !truth(force_flag)) return;
    if (truth(extent_flag)) owner.attr("_clear_extent")();
    if (truth(data_flag)) owner.attr("events").attr("set_data")();
    if (truth(thumbnail_flag)) owner.attr("_update_thumbnail")();
    if (truth(highlight_flag)) owner.attr("_set_highlight")(nb::arg("force") = true);
}
} // namespace

void bind_orchestration(nb::module_& m) {
    m.def("qt_viewer_apply_slice_responses", &qt_viewer_apply_slice_responses,
          nb::arg("viewer").none(), nb::arg("event").none(), nb::arg("namespace"));
    m.def("layer_update_loaded_slice_id", &layer_update_loaded_slice_id,
          nb::arg("state").none(), nb::arg("id").none());
    m.def("layer_set_unloaded_slice_id", &layer_set_unloaded_slice_id,
          nb::arg("state").none(), nb::arg("id").none());
    m.def("layer_refresh_sync", &layer_refresh_sync, nb::arg("layer").none(), nb::arg("namespace"),
          nb::arg("thumbnail").none() = false, nb::arg("data_displayed").none() = false,
          nb::arg("highlight").none() = false, nb::arg("extent").none() = false,
          nb::arg("force").none() = false);
}
} // namespace nx
