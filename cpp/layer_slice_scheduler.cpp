#include "arrays.hpp"
#include "collection_protocol.hpp"

#include <structmember.h>
#include <array>
#include <cstddef>
#include <exception>
#include <string>
#include <utility>

// STAGED ONLY: this translation unit has no build or module integration.
// Complete handwritten _LayerSlicer operations. Real executor/future/lock
// objects and the physical canonical instance dictionary remain authoritative.
// Retain the existing fork's one-shot-layer materialization constraint.
namespace nx {
namespace {

nb::object ls_checked(PyObject* value) {
    if (!value) throw nb::python_error();
    return nb::steal<nb::object>(value);
}
nb::object ls_attr(nb::object value, const char* name) {
    return ls_checked(PyObject_GetAttrString(value.ptr(), name));
}
nb::object ls_item(nb::object value, nb::object key) {
    return ls_checked(PyObject_GetItem(value.ptr(), key.ptr()));
}
void ls_store(nb::object self, const char* name, nb::object value) {
    if (PyObject_SetAttrString(self.ptr(), name, value.ptr()) < 0) throw nb::python_error();
}
void ls_set_item(nb::object target, nb::object key, nb::object value) {
    if (PyObject_SetItem(target.ptr(), key.ptr(), value.ptr()) < 0) throw nb::python_error();
}
nb::object ls_call0(nb::object callback) { return ls_checked(PyObject_CallNoArgs(callback.ptr())); }
nb::object ls_call1(nb::object callback, nb::object first) {
    return ls_checked(PyObject_CallOneArg(callback.ptr(), first.ptr()));
}
nb::object ls_call2(nb::object callback, nb::object first, nb::object second) {
    return ls_checked(PyObject_CallFunctionObjArgs(callback.ptr(), first.ptr(), second.ptr(), nullptr));
}
nb::object ls_call3(nb::object callback, nb::object first, nb::object second, nb::object third) {
    return ls_checked(PyObject_CallFunctionObjArgs(callback.ptr(), first.ptr(), second.ptr(), third.ptr(), nullptr));
}
nb::object ls_kw(nb::object callback, nb::tuple args, nb::dict keywords) {
    return ls_checked(PyObject_Call(callback.ptr(), args.ptr(), keywords.ptr()));
}
bool ls_truth(nb::object value) {
    const int result = PyObject_IsTrue(value.ptr());
    if (result < 0) throw nb::python_error();
    return result != 0;
}
nb::object ls_compare(nb::object left, nb::object right, int operation) {
    return ls_checked(PyObject_RichCompare(left.ptr(), right.ptr(), operation));
}
void ls_replace(nb::object& target, nb::object value) {
    nb::object previous = std::move(target);
    target = std::move(value);
    previous.reset();
}
nb::object ls_optional(const nb::object& value) { return value.is_valid() ? value : nb::none(); }
nb::object ls_assigned(const nb::object& value, const char* name) {
    if (value.is_valid()) return value;
    PyErr_Format(PyExc_UnboundLocalError, "cannot access local variable '%s' where it is not associated with a value", name);
    throw nb::python_error();
}
[[noreturn]] void ls_raise(nb::object exception) {
    if (PyExceptionClass_Check(exception.ptr())) exception = ls_call0(std::move(exception));
    if (!PyExceptionInstance_Check(exception.ptr())) throw nb::type_error("exceptions must derive from BaseException");
    PyErr_SetObject(reinterpret_cast<PyObject*>(Py_TYPE(exception.ptr())), exception.ptr());
    throw nb::python_error();
}
struct SchedulerNamespace {
    nb::dict globals;
    nb::object builtins;
    explicit SchedulerNamespace(nb::dict value)
        : globals(std::move(value)), builtins(nb::borrow<nb::object>(PyEval_GetBuiltins())) {}
    SchedulerNamespace(nb::dict value, nb::object source_builtins)
        : globals(std::move(value)), builtins(std::move(source_builtins)) {}
    nb::object builtin(const char* name) const {
        nb::object key = nb::str(name);
        PyObject* found = PyObject_GetItem(builtins.ptr(), key.ptr());
        if (found) return nb::steal<nb::object>(found);
        if (!PyErr_ExceptionMatches(PyExc_KeyError)) throw nb::python_error();
        PyErr_Clear();
        if (std::string(name) == "__import__") {
            PyErr_SetString(PyExc_ImportError, "__import__ not found");
            throw nb::python_error();
        }
        nb::object message = ls_checked(PyUnicode_FromFormat("name '%s' is not defined", name));
        nb::object error = ls_call1(nb::borrow<nb::object>(PyExc_NameError), std::move(message));
        if (PyObject_SetAttrString(error.ptr(), "name", key.ptr()) < 0) throw nb::python_error();
        ls_raise(std::move(error));
    }
    nb::object get(const char* name) const {
        PyObject* found = PyDict_GetItemString(globals.ptr(), name);
        if (found) return nb::borrow<nb::object>(found);
        if (PyErr_Occurred()) throw nb::python_error();
        return builtin(name);
    }
    nb::object import_module(const char* name, nb::tuple fromlist) const {
        nb::object importer = builtin("__import__");
        PyObject* source_locals = PyEval_GetLocals();
        if (!source_locals) {
            if (PyErr_Occurred()) throw nb::python_error();
            source_locals = Py_None;
        }
        nb::object module_name = nb::str(name), locals = nb::borrow<nb::object>(source_locals), level = nb::int_(0);
        return ls_checked(PyObject_CallFunctionObjArgs(importer.ptr(), module_name.ptr(), globals.ptr(),
            locals.ptr(), fromlist.ptr(), level.ptr(), nullptr));
    }
};
nb::object ls_import_from(nb::handle module, const char* name) {
    PyObject* result = PyObject_GetAttrString(module.ptr(), name);
    if (result) return nb::steal<nb::object>(result);
    if (!PyErr_ExceptionMatches(PyExc_AttributeError)) throw nb::python_error();
    PyErr_Clear();
    nb::object module_name = nb::steal<nb::object>(PyObject_GetAttrString(module.ptr(), "__name__"));
    if (!module_name.is_valid() || !PyUnicode_Check(module_name.ptr())) {
        PyErr_Clear();
        PyErr_Format(PyExc_ImportError, "cannot import name '%s' from '<unknown module name>' (unknown location)", name);
        throw nb::python_error();
    }
    nb::object full_name = ls_checked(PyUnicode_FromFormat("%U.%s", module_name.ptr(), name));
    result = PyImport_GetModule(full_name.ptr());
    if (result) return nb::steal<nb::object>(result);
    if (PyErr_Occurred()) throw nb::python_error();
    nb::object filename = nb::steal<nb::object>(PyObject_GetAttrString(module.ptr(), "__file__"));
    if (!filename.is_valid() || !PyUnicode_Check(filename.ptr())) { PyErr_Clear(); filename = nb::none(); }
    nb::object message = filename.is_none()
        ? ls_checked(PyUnicode_FromFormat("cannot import name '%s' from '%U' (unknown location)", name, module_name.ptr()))
        : ls_checked(PyUnicode_FromFormat("cannot import name '%s' from '%U' (%U)", name, module_name.ptr(), filename.ptr()));
    PyErr_SetImportError(message.ptr(), module_name.ptr(), filename.ptr());
    throw nb::python_error();
}

// Policy-free physical namespace: no executor/task/state fields live twice.
struct SchedulerDictionary { PyObject_HEAD PyObject* dictionary; PyObject* weakrefs; };
int ls_dictionary_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(reinterpret_cast<SchedulerDictionary*>(self)->dictionary); Py_VISIT(Py_TYPE(self)); return 0;
}
int ls_dictionary_clear(PyObject* self) { Py_CLEAR(reinterpret_cast<SchedulerDictionary*>(self)->dictionary); return 0; }
int ls_dictionary_set(PyObject* self, PyObject* value, void*) {
    if (value && !PyDict_Check(value)) {
        PyErr_Format(PyExc_TypeError, "__dict__ must be set to a dictionary, not a '%.200s'", Py_TYPE(value)->tp_name);
        return -1;
    }
    Py_XSETREF(reinterpret_cast<SchedulerDictionary*>(self)->dictionary, Py_XNewRef(value)); return 0;
}
PyObject* ls_dictionary_weakrefs(PyObject* self, void*) {
    PyObject* refs = reinterpret_cast<SchedulerDictionary*>(self)->weakrefs;
    return Py_NewRef(refs ? refs : Py_None);
}
PyMemberDef ls_dictionary_members[] = {
    {const_cast<char*>("__dictoffset__"), T_PYSSIZET, offsetof(SchedulerDictionary, dictionary), READONLY, nullptr},
    {const_cast<char*>("__weaklistoffset__"), T_PYSSIZET, offsetof(SchedulerDictionary, weakrefs), READONLY, nullptr},
    {nullptr, 0, 0, 0, nullptr},
};
PyGetSetDef ls_dictionary_getsets[] = {
    {const_cast<char*>("__dict__"), PyObject_GenericGetDict, ls_dictionary_set, nullptr, nullptr},
    {const_cast<char*>("__weakref__"), ls_dictionary_weakrefs, nullptr, nullptr, nullptr},
    {nullptr, nullptr, nullptr, nullptr, nullptr},
};
PyType_Slot ls_dictionary_slots[] = {
    {Py_tp_members, ls_dictionary_members}, {Py_tp_getset, ls_dictionary_getsets},
    {Py_tp_traverse, reinterpret_cast<void*>(ls_dictionary_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(ls_dictionary_clear)}, {0, nullptr},
};

// GC-visible source locals only; never persistent scheduler state. The facade
// keeps one carrier so original assignments survive a saved error traceback.
struct SchedulerLocals {
    enum class Layout { Empty, Wait, Submit, Slice, Done, Remove, Find } layout = Layout::Empty;
    nb::object futures, _, not_done_futures, existing_task, requests, sync_layers;
    nb::object layer, request, weak_layer, task, result, exception, notification_manager;
    nb::object k_layers, v_task, layer_set, weak_task_layers, task_layers, w;
    SchedulerLocals() = default;
    SchedulerLocals(SchedulerLocals&&) = default;
    ~SchedulerLocals() { clear(); }
    void clear() {
        std::array<nb::object, 19> old = {
            std::move(futures), std::move(_), std::move(not_done_futures), std::move(existing_task),
            std::move(requests), std::move(sync_layers), std::move(layer), std::move(request),
            std::move(weak_layer), std::move(task), std::move(result), std::move(exception),
            std::move(notification_manager), std::move(k_layers), std::move(v_task),
            std::move(layer_set), std::move(weak_task_layers), std::move(task_layers), std::move(w),
        };
        const Layout source = layout; layout = Layout::Empty;
        if (source == Layout::Submit) {
            for (size_t i : {3u, 4u, 5u, 6u, 7u, 8u, 9u}) old[i].reset();
        } else if (source == Layout::Done) {
            for (size_t i : {4u, 11u, 8u, 7u, 6u, 12u}) old[i].reset();
        } else if (source == Layout::Remove) {
            old[13].reset(); old[14].reset();
        } else if (source == Layout::Find) {
            for (size_t i : {15u, 16u, 9u, 17u, 18u}) old[i].reset();
        }
        for (auto& value : old) value.reset();
    }
};
int ls_locals_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) {
        const auto& s = *nb::inst_ptr<SchedulerLocals>(self);
        Py_VISIT(s.futures.ptr()); Py_VISIT(s._.ptr()); Py_VISIT(s.not_done_futures.ptr());
        Py_VISIT(s.existing_task.ptr()); Py_VISIT(s.requests.ptr()); Py_VISIT(s.sync_layers.ptr());
        Py_VISIT(s.layer.ptr()); Py_VISIT(s.request.ptr()); Py_VISIT(s.weak_layer.ptr()); Py_VISIT(s.task.ptr());
        Py_VISIT(s.result.ptr()); Py_VISIT(s.exception.ptr()); Py_VISIT(s.notification_manager.ptr());
        Py_VISIT(s.k_layers.ptr()); Py_VISIT(s.v_task.ptr()); Py_VISIT(s.layer_set.ptr());
        Py_VISIT(s.weak_task_layers.ptr()); Py_VISIT(s.task_layers.ptr()); Py_VISIT(s.w.ptr());
    }
    return 0;
}
int ls_locals_clear(PyObject* self) { if (nb::inst_ready(self)) nb::inst_ptr<SchedulerLocals>(self)->clear(); return 0; }
PyType_Slot ls_locals_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(ls_locals_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(ls_locals_clear)}, {0, nullptr},
};

// The bound __exit__ is saved before __enter__ is invoked. In particular, its
// lease spans exception truth testing/handled-state cleanup, and cleanup errors
// propagate rather than becoming destructor warnings. No lock snapshots.
template <typename Body> void ls_with(nb::object manager, Body body) {
    nb::object enter = collections::context_method(manager, "__enter__", false);
    nb::object exit = collections::context_method(manager, "__exit__", true);
    manager.reset();
    PyObject* raw_entered = PyObject_CallNoArgs(enter.ptr());
    enter.reset();
    ls_checked(raw_entered).reset();
    try { body(); }
    catch (const nb::python_error& original) {
        events::HandledException handled(original.value());
        nb::object trace = original.traceback(); if (!trace.is_valid()) trace = nb::none();
        try {
            if (!ls_truth(ls_call3(exit, nb::borrow<nb::object>(original.type()),
                nb::borrow<nb::object>(original.value()), std::move(trace)))) throw;
        } catch (const nb::python_error& cleanup) {
            if (cleanup.value().ptr() != original.value().ptr())
                PyException_SetContext(cleanup.value().ptr(), original.value().inc_ref().ptr());
            throw;
        }
        return;
    }
    ls_call3(std::move(exit), nb::none(), nb::none(), nb::none());
}
std::array<nb::object, 2> ls_unpack_pair(nb::object source) {
    nb::object iterator = nb::steal<nb::object>(PyObject_GetIter(source.ptr()));
    if (!iterator.is_valid()) {
        if (PyErr_ExceptionMatches(PyExc_TypeError) && !Py_TYPE(source.ptr())->tp_iter && !PySequence_Check(source.ptr()))
            PyErr_Format(PyExc_TypeError, "cannot unpack non-iterable %.200s object", Py_TYPE(source.ptr())->tp_name);
        throw nb::python_error();
    }
    // Source result remains leased throughout every unpack next/error read.
    std::array<nb::object, 2> result;
    for (Py_ssize_t i = 0; i < 2; ++i) {
        result[static_cast<size_t>(i)] = nb::steal<nb::object>(PyIter_Next(iterator.ptr()));
        if (!result[static_cast<size_t>(i)].is_valid()) {
            if (PyErr_Occurred()) throw nb::python_error();
            PyErr_Format(PyExc_ValueError, "not enough values to unpack (expected 2, got %zd)", i);
            throw nb::python_error();
        }
    }
    nb::object extra = nb::steal<nb::object>(PyIter_Next(iterator.ptr()));
    if (extra.is_valid()) {
        extra.reset();
        PyErr_SetString(PyExc_ValueError, "too many values to unpack (expected 2)");
        throw nb::python_error();
    }
    if (PyErr_Occurred()) throw nb::python_error();
    return result;
}
nb::object ls_debug(SchedulerNamespace& ns) { return ls_attr(ns.get("logger"), "debug"); }
void ls_log_task(SchedulerNamespace& ns, const char* message, nb::object task) {
    nb::object debug = ls_debug(ns);
    nb::object identifier = ls_call1(ns.get("id"), std::move(task));
    ls_call2(std::move(debug), nb::str(message), std::move(identifier));
}

// A genuine force_sync facade remains @contextmanager and consists solely of
// `yield from scheduler_force_sync(self, globals())`. Native owns the suspended
// original self/prev references, the mutation, and all finally decisions.
struct ForceSyncIterator {
    enum class Stage { Created, Suspended, Closed } stage = Stage::Created;
    bool running = false;
    nb::object self, prev, namespace_, builtins;
    ForceSyncIterator() = default;
    ForceSyncIterator(ForceSyncIterator&&) = default;
    ~ForceSyncIterator() { clear(); }
    void clear() {
        stage = Stage::Closed;
        std::array<nb::object, 4> old = {std::move(self), std::move(prev), std::move(namespace_), std::move(builtins)};
        for (auto& edge : old) edge.reset();
    }
};
struct ForceRunning {
    ForceSyncIterator& state;
    explicit ForceRunning(ForceSyncIterator& value) : state(value) {
        if (state.running) throw nb::value_error("generator already executing");
        state.running = true;
    }
    ~ForceRunning() { state.running = false; }
};
void ls_force_finally(ForceSyncIterator& state) { ls_store(state.self, "_force_sync", state.prev); }
nb::object ls_force_next(nb::handle self) {
    auto& state = *nb::inst_ptr<ForceSyncIterator>(self);
    ForceRunning running(state);
    if (state.stage == ForceSyncIterator::Stage::Closed) throw nb::stop_iteration();
    try {
        if (state.stage == ForceSyncIterator::Stage::Created) {
            ls_replace(state.prev, ls_attr(state.self, "_force_sync"));
            ls_store(state.self, "_force_sync", nb::bool_(true));
            state.stage = ForceSyncIterator::Stage::Suspended;
            return nb::none();
        }
        state.stage = ForceSyncIterator::Stage::Closed;
        ls_force_finally(state);
    } catch (const nb::python_error& error) {
        state.stage = ForceSyncIterator::Stage::Closed;
        // On error, the genuine adapter traceback keeps these source locals.
        // No roots outlive the adapter/cyclic GC or its saved error traceback.
        if (error.matches(PyExc_StopIteration)) collections::raise_exception(error.value(), true);
        throw;
    }
    state.clear(); throw nb::stop_iteration();
}
void ls_force_close(nb::handle self) {
    auto& state = *nb::inst_ptr<ForceSyncIterator>(self);
    ForceRunning running(state);
    if (state.stage == ForceSyncIterator::Stage::Closed) return;
    const bool suspended = state.stage == ForceSyncIterator::Stage::Suspended;
    state.stage = ForceSyncIterator::Stage::Closed;
    nb::object exit = ls_call0(nb::borrow<nb::object>(PyExc_GeneratorExit));
    events::HandledException handled(exit);
    try { if (suspended) ls_force_finally(state); }
    catch (const nb::python_error& cleanup) {
        if (cleanup.matches(PyExc_StopIteration)) collections::raise_exception(cleanup.value(), true);
        throw;
    }
    state.clear();
}
void ls_force_throw(nb::handle self, nb::args args) {
    nb::object exception = collections::normalize_throw(args);
    auto& state = *nb::inst_ptr<ForceSyncIterator>(self);
    ForceRunning running(state);
    const bool open = state.stage != ForceSyncIterator::Stage::Closed;
    const bool suspended = state.stage == ForceSyncIterator::Stage::Suspended;
    state.stage = ForceSyncIterator::Stage::Closed;
    if (suspended) {
        events::HandledException handled(exception);
        try { ls_force_finally(state); }
        catch (const nb::python_error& cleanup) {
            if (cleanup.value().ptr() != exception.ptr()) PyException_SetContext(cleanup.value().ptr(), exception.inc_ref().ptr());
            if (cleanup.matches(PyExc_StopIteration)) collections::raise_exception(cleanup.value(), true);
            throw;
        }
    }
    // Retain original suspended locals only while a genuine saved error frame
    // retains its adapter. Created/previously closed generators have no locals.
    if (!suspended) state.clear();
    collections::raise_exception(exception, open);
}
int ls_force_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) {
        const auto& s = *nb::inst_ptr<ForceSyncIterator>(self);
        Py_VISIT(s.self.ptr()); Py_VISIT(s.prev.ptr()); Py_VISIT(s.namespace_.ptr()); Py_VISIT(s.builtins.ptr());
    }
    return 0;
}
int ls_force_clear(PyObject* self) { if (nb::inst_ready(self)) nb::inst_ptr<ForceSyncIterator>(self)->clear(); return 0; }
void ls_force_finalize(PyObject* self) noexcept {
    if (!nb::inst_ready(self) || nb::inst_ptr<ForceSyncIterator>(self)->stage == ForceSyncIterator::Stage::Closed) return;
    PyObject *type = nullptr, *value = nullptr, *trace = nullptr;
    PyErr_Fetch(&type, &value, &trace);
    try { ls_force_close(self); }
    catch (nb::python_error& error) { error.restore(); PyErr_WriteUnraisable(self); }
    catch (const std::exception& error) { PyErr_SetString(PyExc_RuntimeError, error.what()); PyErr_WriteUnraisable(self); }
    catch (...) { PyErr_SetString(PyExc_RuntimeError, "failed to close force_sync context"); PyErr_WriteUnraisable(self); }
    PyErr_Restore(type, value, trace);
}
PyType_Slot ls_force_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(ls_force_traverse)}, {Py_tp_clear, reinterpret_cast<void*>(ls_force_clear)},
    {Py_tp_finalize, reinterpret_cast<void*>(ls_force_finalize)}, {0, nullptr},
};
destructor ls_force_base_dealloc = nullptr;
void ls_force_dealloc(PyObject* self) noexcept {
    PyObject_GC_UnTrack(self); PyObject_ClearWeakRefs(self); PyObject_GC_Track(self);
    if (PyObject_CallFinalizerFromDealloc(self) < 0) return;
    ls_force_base_dealloc(self);
}

void scheduler_init(nb::object self, nb::dict namespace_) {
    SchedulerNamespace ns(std::move(namespace_));
    {
        nb::object factory = ns.get("EmitterGroup");
        nb::dict kwargs; kwargs["source"] = self; kwargs["ready"] = ns.get("Event");
        ls_store(self, "events", ls_kw(std::move(factory), nb::tuple(), std::move(kwargs)));
    }
    {
        nb::object factory = ns.get("ThreadPoolExecutor");
        nb::dict kwargs; kwargs["max_workers"] = nb::int_(1);
        ls_store(self, "_executor", ls_kw(std::move(factory), nb::tuple(), std::move(kwargs)));
    }
    ls_store(self, "_force_sync", nb::bool_(!ls_truth(ls_attr(ls_attr(ls_call0(ns.get("get_settings")), "experimental"), "async_"))));
    ls_store(self, "_layers_to_task", nb::dict());
    ls_store(self, "_task_to_requests", nb::dict());
    ls_store(self, "_lock_futures_dicts", ls_call0(ns.get("RLock")));
}
nb::object scheduler_force_sync(nb::object self, nb::dict namespace_) {
    ForceSyncIterator state; state.self = std::move(self); state.namespace_ = std::move(namespace_);
    state.builtins = nb::borrow<nb::object>(PyEval_GetBuiltins());
    return nb::cast(std::move(state), nb::rv_policy::move);
}
void scheduler_wait_idle(nb::object self, nb::object timeout, nb::dict namespace_, SchedulerLocals& s) {
    s.layout = SchedulerLocals::Layout::Wait; SchedulerNamespace ns(std::move(namespace_));
    ls_replace(s.futures, ls_call0(ls_attr(ls_attr(self, "_layers_to_task"), "values")));
    nb::object wait = ns.get("wait"); nb::dict kwargs; kwargs["timeout"] = timeout;
    auto values = ls_unpack_pair(ls_kw(std::move(wait), nb::make_tuple(s.futures), std::move(kwargs)));
    ls_replace(s._, std::move(values[0])); ls_replace(s.not_done_futures, std::move(values[1]));
    if (ls_truth(ls_compare(ls_call1(ns.get("len"), s.not_done_futures), nb::int_(0), Py_GT))) {
        nb::object factory = ns.get("TimeoutError");
        nb::object count = ls_call1(ns.get("len"), s.not_done_futures);
        nb::object count_text = ls_checked(PyObject_Format(count.ptr(), nb::str("").ptr())); count.reset();
        nb::object timeout_text = ls_checked(PyObject_Format(timeout.ptr(), nb::str("").ptr()));
        nb::object message = ls_checked(PyUnicode_FromFormat("Slicing %U tasks did not complete within timeout (%Us).", count_text.ptr(), timeout_text.ptr()));
        count_text.reset(); timeout_text.reset();
        ls_raise(ls_call1(std::move(factory), std::move(message)));
    }
}
nb::object scheduler_submit(nb::object self, nb::object layers, nb::object dims,
                            nb::object force, nb::dict namespace_, SchedulerLocals& s) {
    s.layout = SchedulerLocals::Layout::Submit; SchedulerNamespace ns(std::move(namespace_));
    ls_kw(ls_debug(ns), nb::make_tuple(nb::str("_LayerSlicer.submit: layers=%s, dims=%s, force=%s"), layers, dims, force), nb::dict());
    // Deliberate accepted fork contract: one-shot inputs survive both task
    // search and dispatch. This is not a second persistent scheduler authority.
    nb::object all_layers = ls_checked(PySequence_Tuple(layers.ptr()));
    ls_replace(s.existing_task, ls_call1(ls_attr(self, "_find_existing_task"), all_layers));
    if (ls_truth(s.existing_task)) {
        ls_log_task(ns, "Cancelling task %s", s.existing_task);
        ls_call0(ls_attr(s.existing_task, "cancel"));
    }
    ls_replace(s.requests, nb::dict()); ls_replace(s.sync_layers, nb::list());
    {
        nb::object iterator = ls_checked(PyObject_GetIter(all_layers.ptr()));
        for (;;) {
            nb::object next = nb::steal<nb::object>(PyIter_Next(iterator.ptr()));
            if (!next.is_valid()) { if (PyErr_Occurred()) throw nb::python_error(); break; }
            ls_replace(s.layer, std::move(next));
            nb::object check = ns.get("isinstance");
            nb::object state = ls_attr(s.layer, "_slicing_state");
            nb::object type = ns.get("_AsyncSliceable");
            if (ls_truth(ls_call2(std::move(check), std::move(state), std::move(type))) &&
                !ls_truth(ls_attr(self, "_force_sync")) && ls_truth(ls_attr(s.layer, "visible"))) {
                ls_call2(ls_debug(ns), nb::str("Making async slice request for %s"), s.layer);
                ls_replace(s.request, ls_call1(ls_attr(ls_attr(s.layer, "_slicing_state"), "_make_slice_request"), dims));
                ls_replace(s.weak_layer, ls_call1(ls_attr(ns.get("weakref"), "ref"), s.layer));
                ls_set_item(s.requests, s.weak_layer, s.request);
                nb::object callback = ls_attr(ls_attr(s.layer, "_slicing_state"), "_set_unloaded_slice_id");
                ls_call1(std::move(callback), ls_attr(s.request, "id"));
            } else {
                ls_call2(ls_debug(ns), nb::str("Sync slicing for %s"), s.layer);
                ls_call1(ls_attr(s.sync_layers, "append"), s.layer);
            }
        }
    }
    ls_replace(s.task, nb::none());
    if (ls_truth(ls_compare(ls_call1(ns.get("len"), s.requests), nb::int_(0), Py_GT))) {
        ls_log_task(ns, "Submitting task %s", s.task);
        nb::object submit = ls_attr(ls_attr(self, "_executor"), "submit");
        nb::object callback = ls_attr(self, "_slice_layers");
        ls_replace(s.task, ls_call2(std::move(submit), std::move(callback), s.requests));
        ls_with(ls_attr(self, "_lock_futures_dicts"), [&] {
            nb::object value = s.task;
            nb::object target = ls_attr(self, "_layers_to_task");
            nb::object key = ls_call1(ns.get("tuple"), s.requests);
            ls_set_item(std::move(target), std::move(key), std::move(value));
            value = s.requests; target = ls_attr(self, "_task_to_requests");
            ls_set_item(std::move(target), s.task, std::move(value));
        });
        nb::object add_done = ls_attr(s.task, "add_done_callback");
        ls_call1(std::move(add_done), ls_attr(self, "_on_slice_done"));
    }
    {
        nb::object iterator = ls_checked(PyObject_GetIter(s.sync_layers.ptr()));
        for (;;) {
            nb::object next = nb::steal<nb::object>(PyIter_Next(iterator.ptr()));
            if (!next.is_valid()) { if (PyErr_Occurred()) throw nb::python_error(); break; }
            ls_replace(s.layer, std::move(next));
            nb::object callback = ls_attr(s.layer, "_slice_dims");
            nb::dict kwargs; kwargs["dims"] = dims; kwargs["force"] = force;
            ls_kw(std::move(callback), nb::tuple(), std::move(kwargs));
        }
    }
    return s.task;
}
void scheduler_shutdown(nb::object self, nb::dict namespace_) {
    SchedulerNamespace ns(std::move(namespace_));
    ls_call1(ls_debug(ns), nb::str("_LayerSlicer.shutdown"));
    nb::object callback = ls_attr(ls_attr(self, "_executor"), "shutdown");
    nb::dict kwargs; kwargs["wait"] = nb::bool_(true); kwargs["cancel_futures"] = nb::bool_(true);
    ls_kw(std::move(callback), nb::tuple(), std::move(kwargs));
    ls_call0(ls_attr(ls_attr(self, "events"), "disconnect"));
    ls_call0(ls_attr(ls_attr(ls_attr(self, "events"), "ready"), "disconnect"));
}
nb::object scheduler_slice_layers(nb::object self, nb::object requests, nb::dict namespace_, SchedulerLocals& s) {
    s.layout = SchedulerLocals::Layout::Slice; SchedulerNamespace ns(std::move(namespace_));
    ls_call2(ls_debug(ns), nb::str("_LayerSlicer._slice_layers: %s"), requests);
    nb::object iterator = ls_checked(PyObject_GetIter(ls_call0(ls_attr(requests, "items")).ptr()));
    nb::dict result;
    try {
        for (;;) {
            nb::object next = nb::steal<nb::object>(PyIter_Next(iterator.ptr()));
            if (!next.is_valid()) { if (PyErr_Occurred()) throw nb::python_error(); break; }
            auto pair = ls_unpack_pair(std::move(next));
            ls_replace(s.layer, std::move(pair[0])); ls_replace(s.request, std::move(pair[1]));
            nb::object response = ls_call0(s.request);
            ls_set_item(result, s.layer, std::move(response));
        }
    } catch (...) {
        iterator.reset(); result.reset(); s.request.reset(); s.layer.reset(); throw;
    }
    iterator.reset(); ls_replace(s.result, std::move(result));
    // CPython's inlined comprehension publishes result before restoring its
    // temporary iteration bindings. Those bindings are not persistent locals.
    s.layer.reset(); s.request.reset();
    nb::object ready = ls_attr(ls_attr(self, "events"), "ready");
    nb::dict kwargs; kwargs["value"] = s.result;
    ls_kw(std::move(ready), nb::tuple(), std::move(kwargs));
    return s.result;
}
void scheduler_done(nb::object self, nb::object task, nb::dict namespace_, SchedulerLocals& s) {
    s.layout = SchedulerLocals::Layout::Done; SchedulerNamespace ns(std::move(namespace_));
    ls_log_task(ns, "_LayerSlicer._on_slice_done: %s", task);
    ls_with(ls_attr(self, "_lock_futures_dicts"), [&] {
        ls_replace(s.requests, ls_call2(ls_attr(ls_attr(self, "_task_to_requests"), "pop"), task, nb::none()));
    });
    if (!ls_truth(ls_call1(ls_attr(self, "_try_to_remove_task"), task))) ls_log_task(ns, "Task not found: %s", task);
    if (ls_truth(ls_call0(ls_attr(task, "cancelled")))) {
        ls_log_task(ns, "Cancelled task: %s", task); return;
    }
    ls_replace(s.exception, ls_call0(ls_attr(task, "exception")));
    if (ls_truth(s.exception)) {
        ls_log_task(ns, "Task failed: %s", task);
        if (ls_truth(ls_assigned(s.requests, "requests"))) {
            nb::object iterator = ls_checked(PyObject_GetIter(ls_call0(ls_attr(s.requests, "items")).ptr()));
            for (;;) {
                nb::object next = nb::steal<nb::object>(PyIter_Next(iterator.ptr()));
                if (!next.is_valid()) { if (PyErr_Occurred()) throw nb::python_error(); break; }
                auto pair = ls_unpack_pair(std::move(next));
                ls_replace(s.weak_layer, std::move(pair[0])); ls_replace(s.request, std::move(pair[1]));
                ls_replace(s.layer, ls_call0(s.weak_layer));
                if (ls_truth(s.layer)) {
                    nb::object callback = ls_attr(ls_attr(s.layer, "_slicing_state"), "_update_loaded_slice_id");
                    ls_call1(std::move(callback), ls_attr(s.request, "id"));
                }
            }
        }
        {
            nb::object imported = ns.import_module("napari.utils.notifications", nb::make_tuple(nb::str("notification_manager")));
            ls_replace(s.notification_manager, ls_import_from(imported, "notification_manager"));
        }
        nb::object callback = ls_attr(s.notification_manager, "receive_error");
        nb::object type = ls_call1(ns.get("type"), s.exception);
        ls_call3(std::move(callback), std::move(type), s.exception, ls_attr(s.exception, "__traceback__"));
    }
}
nb::object scheduler_remove_task(nb::object self, nb::object task, nb::dict namespace_, SchedulerLocals& s) {
    s.layout = SchedulerLocals::Layout::Remove; SchedulerNamespace ns(std::move(namespace_));
    bool removed = false;
    ls_with(ls_attr(self, "_lock_futures_dicts"), [&] {
        nb::object iterator = ls_checked(PyObject_GetIter(ls_call0(ls_attr(ls_attr(self, "_layers_to_task"), "items")).ptr()));
        for (;;) {
            nb::object next = nb::steal<nb::object>(PyIter_Next(iterator.ptr()));
            if (!next.is_valid()) { if (PyErr_Occurred()) throw nb::python_error(); break; }
            auto pair = ls_unpack_pair(std::move(next));
            ls_replace(s.k_layers, std::move(pair[0])); ls_replace(s.v_task, std::move(pair[1]));
            if (ls_truth(ls_compare(s.v_task, task, Py_EQ))) {
                nb::object target = ls_attr(self, "_layers_to_task");
                if (PyObject_DelItem(target.ptr(), s.k_layers.ptr()) < 0) throw nb::python_error();
                removed = true; return;
            }
        }
    });
    return nb::bool_(removed);
}
nb::object ls_task_layer_set(nb::object weak_layers, SchedulerLocals& s) {
    nb::object iterator = ls_checked(PyObject_GetIter(weak_layers.ptr()));
    weak_layers.reset();
    nb::object result = ls_checked(PySet_New(nullptr));
    try {
        for (;;) {
            nb::object next = nb::steal<nb::object>(PyIter_Next(iterator.ptr()));
            if (!next.is_valid()) { if (PyErr_Occurred()) throw nb::python_error(); break; }
            ls_replace(s.w, std::move(next));
            nb::object layer = ls_call0(s.w);
            if (PySet_Add(result.ptr(), layer.ptr()) < 0) throw nb::python_error();
        }
    } catch (...) { iterator.reset(); result.reset(); s.w.reset(); throw; }
    iterator.reset(); s.w.reset(); return result;
}
nb::object scheduler_find_task(nb::object self, nb::object layers, nb::dict namespace_, SchedulerLocals& s) {
    s.layout = SchedulerLocals::Layout::Find; SchedulerNamespace ns(std::move(namespace_));
    nb::object found = nb::none();
    ls_with(ls_attr(self, "_lock_futures_dicts"), [&] {
        ls_replace(s.layer_set, ls_call1(ns.get("set"), layers));
        nb::object iterator = ls_checked(PyObject_GetIter(ls_call0(ls_attr(ls_attr(self, "_layers_to_task"), "items")).ptr()));
        for (;;) {
            nb::object next = nb::steal<nb::object>(PyIter_Next(iterator.ptr()));
            if (!next.is_valid()) { if (PyErr_Occurred()) throw nb::python_error(); break; }
            auto pair = ls_unpack_pair(std::move(next));
            ls_replace(s.weak_task_layers, std::move(pair[0])); ls_replace(s.task, std::move(pair[1]));
            nb::object task_layers = ls_task_layer_set(s.weak_task_layers, s);
            nb::object none_set = ls_checked(PySet_New(nullptr));
            if (PySet_Add(none_set.ptr(), Py_None) < 0) throw nb::python_error();
            nb::object difference = ls_checked(PyNumber_Subtract(task_layers.ptr(), none_set.ptr()));
            task_layers.reset(); none_set.reset();
            ls_replace(s.task_layers, std::move(difference));
            if (ls_truth(ls_call1(ls_attr(s.task_layers, "issubset"), s.layer_set))) {
                ls_call2(ls_debug(ns), nb::str("Found existing task for %s"), s.task_layers);
                found = s.task; return;
            }
        }
    });
    return found;
}

} // namespace

void bind_layer_slice_scheduler(nb::module_& module) {
    PyType_Spec spec = {"naparixx._native.SliceSchedulerNamespaceBase", static_cast<int>(sizeof(SchedulerDictionary)),
        0, Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HAVE_GC, ls_dictionary_slots};
    module.attr("SliceSchedulerNamespaceBase") = ls_checked(PyType_FromSpec(&spec));
    nb::class_<SchedulerLocals>(module, "LayerSliceSchedulerLocals", nb::is_weak_referenceable(), nb::type_slots(ls_locals_slots))
        .def_prop_ro("futures", [](const SchedulerLocals& s) { return ls_optional(s.futures); })
        .def_prop_ro("_", [](const SchedulerLocals& s) { return ls_optional(s._); })
        .def_prop_ro("not_done_futures", [](const SchedulerLocals& s) { return ls_optional(s.not_done_futures); })
        .def_prop_ro("existing_task", [](const SchedulerLocals& s) { return ls_optional(s.existing_task); })
        .def_prop_ro("requests", [](const SchedulerLocals& s) { return ls_optional(s.requests); })
        .def_prop_ro("sync_layers", [](const SchedulerLocals& s) { return ls_optional(s.sync_layers); })
        .def_prop_ro("layer", [](const SchedulerLocals& s) { return ls_optional(s.layer); })
        .def_prop_ro("request", [](const SchedulerLocals& s) { return ls_optional(s.request); })
        .def_prop_ro("weak_layer", [](const SchedulerLocals& s) { return ls_optional(s.weak_layer); })
        .def_prop_ro("task", [](const SchedulerLocals& s) { return ls_optional(s.task); })
        .def_prop_ro("result", [](const SchedulerLocals& s) { return ls_optional(s.result); })
        .def_prop_ro("exception", [](const SchedulerLocals& s) { return ls_optional(s.exception); })
        .def_prop_ro("notification_manager", [](const SchedulerLocals& s) { return ls_optional(s.notification_manager); })
        .def_prop_ro("k_layers", [](const SchedulerLocals& s) { return ls_optional(s.k_layers); })
        .def_prop_ro("v_task", [](const SchedulerLocals& s) { return ls_optional(s.v_task); })
        .def_prop_ro("layer_set", [](const SchedulerLocals& s) { return ls_optional(s.layer_set); })
        .def_prop_ro("weak_task_layers", [](const SchedulerLocals& s) { return ls_optional(s.weak_task_layers); })
        .def_prop_ro("task_layers", [](const SchedulerLocals& s) { return ls_optional(s.task_layers); })
        .def_prop_ro("w", [](const SchedulerLocals& s) { return ls_optional(s.w); });
    nb::class_<ForceSyncIterator> context(module, "LayerSliceForceSyncIterator", nb::is_weak_referenceable(), nb::type_slots(ls_force_slots));
    context.def("__iter__", [](nb::handle self) { return nb::borrow<nb::object>(self); })
        .def("__next__", &ls_force_next)
        .def("send", [](nb::handle self, nb::object value) {
            const auto& state = *nb::inst_ptr<ForceSyncIterator>(self);
            if (state.running) throw nb::value_error("generator already executing");
            if (state.stage == ForceSyncIterator::Stage::Created && !value.is_none())
                throw nb::type_error("can't send non-None value to a just-started generator");
            return ls_force_next(self);
        }, nb::arg("value").none())
        .def("throw", &ls_force_throw).def("close", &ls_force_close)
        .def_prop_ro("prev", [](const ForceSyncIterator& s) { return ls_optional(s.prev); });
    auto* context_type = reinterpret_cast<PyTypeObject*>(context.ptr());
    ls_force_base_dealloc = context_type->tp_dealloc; context_type->tp_dealloc = ls_force_dealloc;
    module.def("scheduler_locals", [] { return SchedulerLocals(); });
    module.def("scheduler_init", &scheduler_init, nb::arg("self").none(), nb::arg("namespace"));
    module.def("scheduler_force_sync", &scheduler_force_sync, nb::arg("self").none(), nb::arg("namespace"));
    module.def("scheduler_wait_idle", &scheduler_wait_idle,
        nb::arg("self").none(), nb::arg("timeout").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("scheduler_submit", &scheduler_submit,
        nb::arg("self").none(), nb::arg("layers").none(), nb::arg("dims").none(), nb::arg("force").none(),
        nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("scheduler_shutdown", &scheduler_shutdown, nb::arg("self").none(), nb::arg("namespace"));
    module.def("scheduler_slice_layers", &scheduler_slice_layers,
        nb::arg("self").none(), nb::arg("requests").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("scheduler_done", &scheduler_done,
        nb::arg("self").none(), nb::arg("task").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("scheduler_remove_task", &scheduler_remove_task,
        nb::arg("self").none(), nb::arg("task").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("scheduler_find_task", &scheduler_find_task,
        nb::arg("self").none(), nb::arg("layers").none(), nb::arg("namespace"), nb::arg("locals_owner"));
}
} // namespace nx
