#include "arrays.hpp"
#include "collection_protocol.hpp"
#include <array>
#include <string>
#include <utility>

namespace nx {
namespace {

nb::object di_checked(PyObject* value) {
    if (!value) throw nb::python_error();
    return nb::steal<nb::object>(value);
}
nb::object di_attr(nb::object value, const char* name) {
    return di_checked(PyObject_GetAttrString(value.ptr(), name));
}
nb::object di_call0(nb::object callback) {
    return di_checked(PyObject_CallNoArgs(callback.ptr()));
}
nb::object di_call1(nb::object callback, nb::object value) {
    return di_checked(PyObject_CallOneArg(callback.ptr(), value.ptr()));
}
nb::object di_call2(nb::object callback, nb::object first, nb::object second) {
    return di_checked(PyObject_CallFunctionObjArgs(callback.ptr(), first.ptr(), second.ptr(), nullptr));
}
nb::object di_call3(nb::object callback, nb::object first, nb::object second, nb::object third) {
    return di_checked(PyObject_CallFunctionObjArgs(callback.ptr(), first.ptr(), second.ptr(), third.ptr(), nullptr));
}
bool di_truth(nb::object value) {
    int result = PyObject_IsTrue(value.ptr());
    if (result < 0) throw nb::python_error();
    return result != 0;
}
nb::object di_compare(nb::object first, nb::object second, int operation) {
    return di_checked(PyObject_RichCompare(first.ptr(), second.ptr(), operation));
}
nb::object di_multiply(nb::object first, nb::object second) {
    return di_checked(PyNumber_Multiply(first.ptr(), second.ptr()));
}
void di_replace(nb::object& target, nb::object value) {
    nb::object previous = std::move(target);
    target = std::move(value);
    previous.reset();
}
nb::object di_optional(const nb::object& value) {
    return value.is_valid() ? value : nb::none();
}
struct DaskNamespace {
    nb::dict globals;
    nb::object builtins;
    explicit DaskNamespace(nb::dict value)
        : globals(std::move(value)), builtins(nb::borrow<nb::object>(PyEval_GetBuiltins())) {}
    DaskNamespace(nb::dict value, nb::object builtins_)
        : globals(std::move(value)), builtins(std::move(builtins_)) {}
    nb::object builtin(const char* name) const {
        nb::object key = nb::str(name);
        PyObject* result = PyObject_GetItem(builtins.ptr(), key.ptr());
        if (result) return nb::steal<nb::object>(result);
        if (!PyErr_ExceptionMatches(PyExc_KeyError)) throw nb::python_error();
        PyErr_Clear();
        if (std::string(name) == "__import__") {
            PyErr_SetString(PyExc_ImportError, "__import__ not found");
            throw nb::python_error();
        }
        nb::object message = di_checked(PyUnicode_FromFormat("name '%s' is not defined", name));
        nb::object error = di_call1(nb::borrow<nb::object>(PyExc_NameError), std::move(message));
        if (PyObject_SetAttrString(error.ptr(), "name", key.ptr()) < 0) throw nb::python_error();
        PyErr_SetObject(PyExc_NameError, error.ptr());
        throw nb::python_error();
    }
    nb::object get(const char* name) const {
        nb::object key = nb::str(name);
        PyObject* result = PyDict_GetItemWithError(globals.ptr(), key.ptr());
        if (result) return nb::borrow<nb::object>(result);
        if (PyErr_Occurred()) throw nb::python_error();
        return builtin(name);
    }
    nb::object import_module(const char* name, nb::tuple fromlist) const {
        nb::object importer = builtin("__import__");
        PyObject* locals = PyEval_GetLocals();
        if (!locals) {
            if (PyErr_Occurred()) throw nb::python_error();
            locals = Py_None;
        }
        nb::object module_name = nb::str(name);
        nb::object level = nb::int_(0);
        return di_checked(PyObject_CallFunctionObjArgs(importer.ptr(), module_name.ptr(),
            globals.ptr(), locals, fromlist.ptr(), level.ptr(), nullptr));
    }
};

// IMPORT_NAME/IMPORT_FROM are CPython protocol primitives, including the actual
// executing import hook and the partially initialized sys.modules fallback.
nb::object di_import_from(nb::handle module, const char* name) {
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
    nb::object full_name = di_checked(PyUnicode_FromFormat("%U.%s", module_name.ptr(), name));
    result = PyImport_GetModule(full_name.ptr());
    if (result) return nb::steal<nb::object>(result);
    if (PyErr_Occurred()) throw nb::python_error();
    nb::object filename = nb::steal<nb::object>(PyObject_GetAttrString(module.ptr(), "__file__"));
    if (!filename.is_valid() || !PyUnicode_Check(filename.ptr())) {
        PyErr_Clear();
        filename = nb::none();
    }
    nb::object message;
    if (filename.is_none())
        message = di_checked(PyUnicode_FromFormat("cannot import name '%s' from '%U' (unknown location)", name, module_name.ptr()));
    else
        message = di_checked(PyUnicode_FromFormat("cannot import name '%s' from '%U' (%U)", name, module_name.ptr(), filename.ptr()));
    PyErr_SetImportError(message.ptr(), module_name.ptr(), filename.ptr());
    throw nb::python_error();
}

// Only source local bindings are retained here; _DASK_CACHE, actual decorator
// wrappers and the genuine _cache cell are the existing authorities.
struct DaskResizeLocals {
    nb::object nbytes, virtual_memory, avail;
    DaskResizeLocals() = default;
    DaskResizeLocals(DaskResizeLocals&&) = default;
    ~DaskResizeLocals() { clear(); }
    void clear() {
        std::array<nb::object, 3> previous = {
            std::move(nbytes), std::move(virtual_memory), std::move(avail),
        };
        for (auto& value : previous) value.reset();
    }
};
struct DaskCollectionLocals {
    nb::object i;
    DaskCollectionLocals() = default;
    DaskCollectionLocals(DaskCollectionLocals&&) = default;
    ~DaskCollectionLocals() { clear(); }
    void clear() { nb::object previous = std::move(i); previous.reset(); }
};
struct DaskSlicingLocals {
    nb::object opts, cfg, c;
    DaskSlicingLocals() = default;
    DaskSlicingLocals(DaskSlicingLocals&&) = default;
    ~DaskSlicingLocals() { clear(); }
    void clear() {
        std::array<nb::object, 3> previous = {
            std::move(opts), std::move(cfg), std::move(c),
        };
        for (auto& value : previous) value.reset();
    }
};
int di_resize_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) {
        const auto& s = *nb::inst_ptr<DaskResizeLocals>(self);
        Py_VISIT(s.nbytes.ptr()); Py_VISIT(s.virtual_memory.ptr()); Py_VISIT(s.avail.ptr());
    }
    return 0;
}
int di_collection_locals_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) Py_VISIT(nb::inst_ptr<DaskCollectionLocals>(self)->i.ptr());
    return 0;
}
int di_slicing_locals_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) {
        const auto& s = *nb::inst_ptr<DaskSlicingLocals>(self);
        Py_VISIT(s.opts.ptr()); Py_VISIT(s.cfg.ptr()); Py_VISIT(s.c.ptr());
    }
    return 0;
}
template <typename State> int di_clear(PyObject* self) {
    if (nb::inst_ready(self)) nb::inst_ptr<State>(self)->clear();
    return 0;
}
PyType_Slot di_resize_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(di_resize_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(di_clear<DaskResizeLocals>)}, {0, nullptr},
};
PyType_Slot di_collection_locals_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(di_collection_locals_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(di_clear<DaskCollectionLocals>)}, {0, nullptr},
};
PyType_Slot di_slicing_locals_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(di_slicing_locals_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(di_clear<DaskSlicingLocals>)}, {0, nullptr},
};

nb::object di_bytes(DaskNamespace& ns, DaskResizeLocals& owner, nb::object fraction) {
    nb::object integer = ns.get("int");
    nb::object total = di_attr(di_call0(owner.virtual_memory), "total");
    return di_call1(std::move(integer), di_multiply(std::move(total), std::move(fraction)));
}
void di_resize(DaskNamespace& ns, DaskResizeLocals& owner) {
    nb::object callback = di_attr(di_attr(ns.get("_DASK_CACHE"), "cache"), "resize");
    di_call1(std::move(callback), owner.nbytes);
}
nb::object dask_resize_cache(nb::object mem_fraction, nb::dict namespace_, DaskResizeLocals& owner) {
    DaskNamespace ns(std::move(namespace_));
    nb::object module = ns.import_module("psutil", nb::make_tuple(nb::str("virtual_memory")));
    di_replace(owner.virtual_memory, di_import_from(module, "virtual_memory"));
    module.reset();
    if (owner.nbytes.is_none() && !mem_fraction.is_none())
        di_replace(owner.nbytes, di_bytes(ns, owner, mem_fraction));
    di_replace(owner.avail, di_attr(di_attr(ns.get("_DASK_CACHE"), "cache"), "available_bytes"));
    if (di_truth(di_compare(owner.avail, nb::int_(1), Py_EQ))) {
        if (owner.nbytes.is_none()) {
            nb::object integer = ns.get("int");
            nb::object total = di_attr(di_call0(owner.virtual_memory), "total");
            nb::object fraction = ns.get("_DEFAULT_MEM_FRACTION");
            di_replace(owner.nbytes, di_call1(std::move(integer), di_multiply(std::move(total), std::move(fraction))));
        }
        di_resize(ns, owner);
    } else if (!owner.nbytes.is_none()) {
        nb::object available = di_attr(di_attr(ns.get("_DASK_CACHE"), "cache"), "available_bytes");
        if (di_truth(di_compare(owner.nbytes, std::move(available), Py_NE))) di_resize(ns, owner);
    }
    return ns.get("_DASK_CACHE");
}

struct DaskCollectionIterator {
    bool started = false, running = false, closed = false;
    nb::object iterator, locals_owner, namespace_, builtins;
    DaskCollectionIterator() = default;
    DaskCollectionIterator(DaskCollectionIterator&&) = default;
    ~DaskCollectionIterator() { clear(); }
    void clear() {
        closed = true;
        std::array<nb::object, 4> previous = {
            std::move(iterator), std::move(locals_owner), std::move(namespace_), std::move(builtins),
        };
        for (auto& value : previous) value.reset();
    }
};
struct DaskSlicingIterator {
    bool started = false, running = false, closed = false;
    nb::object locals_owner, cache_getter, namespace_, builtins, config_exit, cache_exit;
    DaskSlicingIterator() = default;
    DaskSlicingIterator(DaskSlicingIterator&&) = default;
    ~DaskSlicingIterator() { clear(); }
    void clear() {
        closed = true;
        std::array<nb::object, 6> previous = {
            std::move(locals_owner), std::move(cache_getter), std::move(namespace_),
            std::move(builtins), std::move(cache_exit), std::move(config_exit),
        };
        for (auto& value : previous) value.reset();
    }
};
template <typename State> struct DaskRunning {
    State& state;
    explicit DaskRunning(State& value) : state(value) {
        if (state.running) throw nb::value_error("generator already executing");
        state.running = true;
    }
    ~DaskRunning() { state.running = false; }
};

nb::object di_collection_next(nb::handle self) {
    auto& state = *nb::inst_ptr<DaskCollectionIterator>(self);
    DaskRunning<DaskCollectionIterator> running(state);
    if (state.closed) throw nb::stop_iteration();
    state.started = true;
    try {
        PyObject* next = PyIter_Next(state.iterator.ptr());
        if (!next) {
            if (PyErr_Occurred()) throw nb::python_error();
            state.clear();
            throw nb::stop_iteration();
        }
        auto& owner = *nb::inst_ptr<DaskCollectionLocals>(state.locals_owner);
        di_replace(owner.i, nb::steal<nb::object>(next));
        DaskNamespace ns(nb::borrow<nb::dict>(state.namespace_), state.builtins);
        return di_call1(di_attr(ns.get("dask"), "is_dask_collection"), owner.i);
    } catch (const nb::python_error& error) {
        collections::HandledException active(error.value());
        state.clear();
        if (error.matches(PyExc_StopIteration)) collections::raise_exception(error.value(), true);
        throw;
    } catch (...) { state.clear(); throw; }
}
void di_collection_close(nb::handle self) {
    auto& state = *nb::inst_ptr<DaskCollectionIterator>(self);
    DaskRunning<DaskCollectionIterator> running(state);
    if (state.closed) return;
    if (!state.started) { state.clear(); return; }
    nb::object exit = di_call0(nb::borrow<nb::object>(PyExc_GeneratorExit));
    collections::HandledException active(exit);
    state.clear();
}
void di_collection_throw(nb::handle self, nb::args args) {
    nb::object exception = collections::normalize_throw(args);
    auto& state = *nb::inst_ptr<DaskCollectionIterator>(self);
    DaskRunning<DaskCollectionIterator> running(state);
    const bool open = !state.closed;
    collections::HandledException active(exception);
    state.clear();
    collections::raise_exception(exception, open);
}

// The manager and consumed enter callback are transient. Only a successfully
// entered context's saved bound exit survives suspension, as in BEFORE_WITH.
nb::object di_enter(nb::object manager, nb::object& saved_exit) {
    nb::object enter = collections::context_method(manager, "__enter__", false);
    nb::object exit = collections::context_method(manager, "__exit__", true);
    PyObject* result = PyObject_CallNoArgs(enter.ptr());
    enter.reset();
    manager.reset();
    nb::object entered = di_checked(result);
    saved_exit = std::move(exit);
    return entered;
}
nb::object di_exception(const nb::python_error& error) {
    nb::object value = nb::borrow<nb::object>(error.value());
    nb::object trace = error.traceback();
    if (trace.is_valid() && PyException_SetTraceback(value.ptr(), trace.ptr()) < 0)
        throw nb::python_error();
    return value;
}
void di_unwind_one(nb::object& saved_exit, nb::object& pending) {
    if (!saved_exit.is_valid()) return;
    nb::object exit = std::move(saved_exit);
    nb::object replacement;
    bool suppressed = false;
    if (pending.is_valid()) {
        // Retain exit through result truth and POP_EXCEPT/handled-state cleanup.
        collections::HandledException active(pending);
        try {
            nb::object traceback = nb::steal<nb::object>(PyException_GetTraceback(pending.ptr()));
            if (!traceback.is_valid()) {
                if (PyErr_Occurred()) throw nb::python_error();
                traceback = nb::none();
            }
            suppressed = di_truth(di_call3(exit,
                nb::borrow<nb::object>(reinterpret_cast<PyObject*>(Py_TYPE(pending.ptr()))),
                pending, std::move(traceback)));
        } catch (const nb::python_error& cleanup) {
            if (cleanup.value().ptr() != pending.ptr())
                PyException_SetContext(cleanup.value().ptr(), pending.inc_ref().ptr());
            replacement = di_exception(cleanup);
        }
    } else {
        try { di_call3(std::move(exit), nb::none(), nb::none(), nb::none()); }
        catch (const nb::python_error& cleanup) { replacement = di_exception(cleanup); }
    }
    if (replacement.is_valid()) di_replace(pending, std::move(replacement));
    else if (suppressed) pending.reset();
    exit.reset();
}
[[noreturn]] void di_slicing_finish(DaskSlicingIterator& state, nb::object pending) {
    di_unwind_one(state.cache_exit, pending);
    di_unwind_one(state.config_exit, pending);
    if (pending.is_valid()) {
        collections::HandledException active(pending);
        state.clear();
        collections::raise_exception(pending, true);
    }
    state.clear();
    throw nb::stop_iteration();
}
nb::object di_slicing_next(nb::handle self) {
    auto& state = *nb::inst_ptr<DaskSlicingIterator>(self);
    DaskRunning<DaskSlicingIterator> running(state);
    if (state.closed) throw nb::stop_iteration();
    if (state.started) di_slicing_finish(state, nb::object());
    state.started = true;
    nb::object pending;
    try {
        auto& owner = *nb::inst_ptr<DaskSlicingLocals>(state.locals_owner);
        DaskNamespace ns(nb::borrow<nb::dict>(state.namespace_), state.builtins);
        nb::dict options;
        options["optimization.fuse.active"] = nb::bool_(false);
        di_replace(owner.opts, std::move(options));
        nb::object setter = di_attr(di_attr(ns.get("dask"), "config"), "set");
        di_replace(owner.cfg, di_enter(di_call1(std::move(setter), owner.opts), state.config_exit));
        // This pure getter reads the genuine cell at the original LOAD_DEREF.
        di_replace(owner.c, di_enter(di_call0(state.cache_getter), state.cache_exit));
        return nb::make_tuple(owner.cfg, owner.c);
    } catch (const nb::python_error& error) {
        pending = di_exception(error);
    } catch (...) { state.clear(); throw; }
    di_slicing_finish(state, std::move(pending));
}
void di_slicing_close(nb::handle self) {
    auto& state = *nb::inst_ptr<DaskSlicingIterator>(self);
    DaskRunning<DaskSlicingIterator> running(state);
    if (state.closed) return;
    if (!state.started) { state.clear(); return; }
    nb::object exit = di_call0(nb::borrow<nb::object>(PyExc_GeneratorExit));
    try { di_slicing_finish(state, std::move(exit)); }
    catch (const nb::builtin_exception& error) {
        if (error.type() == nb::exception_type::stop_iteration) return;
        throw;
    }
    catch (const nb::python_error& error) {
        if (error.matches(PyExc_GeneratorExit)) return;
        throw;
    }
}
void di_slicing_throw(nb::handle self, nb::args args) {
    nb::object exception = collections::normalize_throw(args);
    auto& state = *nb::inst_ptr<DaskSlicingIterator>(self);
    DaskRunning<DaskSlicingIterator> running(state);
    if (state.closed) collections::raise_exception(exception, false);
    if (!state.started) {
        collections::HandledException active(exception);
        state.clear();
        collections::raise_exception(exception, true);
    }
    di_slicing_finish(state, std::move(exception));
}

int di_collection_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) {
        const auto& s = *nb::inst_ptr<DaskCollectionIterator>(self);
        Py_VISIT(s.iterator.ptr()); Py_VISIT(s.locals_owner.ptr());
        Py_VISIT(s.namespace_.ptr()); Py_VISIT(s.builtins.ptr());
    }
    return 0;
}
int di_slicing_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) {
        const auto& s = *nb::inst_ptr<DaskSlicingIterator>(self);
        Py_VISIT(s.locals_owner.ptr()); Py_VISIT(s.cache_getter.ptr());
        Py_VISIT(s.namespace_.ptr()); Py_VISIT(s.builtins.ptr());
        Py_VISIT(s.config_exit.ptr()); Py_VISIT(s.cache_exit.ptr());
    }
    return 0;
}
template <typename State, void (*Close)(nb::handle)> void di_finalize(PyObject* self) noexcept {
    if (!nb::inst_ready(self) || nb::inst_ptr<State>(self)->closed) return;
    PyObject *type = nullptr, *value = nullptr, *traceback = nullptr;
    PyErr_Fetch(&type, &value, &traceback);
    try { Close(self); }
    catch (nb::python_error& error) { error.restore(); PyErr_WriteUnraisable(self); }
    catch (const std::exception& error) { PyErr_SetString(PyExc_RuntimeError, error.what()); PyErr_WriteUnraisable(self); }
    catch (...) { PyErr_SetString(PyExc_RuntimeError, "failed to close Dask iterator"); PyErr_WriteUnraisable(self); }
    PyErr_Restore(type, value, traceback);
}
PyType_Slot di_collection_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(di_collection_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(di_clear<DaskCollectionIterator>)},
    {Py_tp_finalize, reinterpret_cast<void*>(di_finalize<DaskCollectionIterator, di_collection_close>)}, {0, nullptr},
};
PyType_Slot di_slicing_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(di_slicing_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(di_clear<DaskSlicingIterator>)},
    {Py_tp_finalize, reinterpret_cast<void*>(di_finalize<DaskSlicingIterator, di_slicing_close>)}, {0, nullptr},
};
destructor di_collection_base_dealloc = nullptr, di_slicing_base_dealloc = nullptr;
void di_collection_dealloc(PyObject* self) noexcept {
    PyObject_GC_UnTrack(self);
    PyObject_ClearWeakRefs(self);
    PyObject_GC_Track(self);
    if (PyObject_CallFinalizerFromDealloc(self) < 0) return;
    di_collection_base_dealloc(self);
}
void di_slicing_dealloc(PyObject* self) noexcept {
    PyObject_GC_UnTrack(self);
    PyObject_ClearWeakRefs(self);
    PyObject_GC_Track(self);
    if (PyObject_CallFinalizerFromDealloc(self) < 0) return;
    di_slicing_base_dealloc(self);
}

nb::object dask_collection_iterator(nb::object iterator, nb::dict namespace_, DaskCollectionLocals& owner) {
    DaskCollectionIterator state;
    state.iterator = std::move(iterator);
    state.locals_owner = nb::cast(&owner, nb::rv_policy::reference);
    state.namespace_ = std::move(namespace_);
    state.builtins = nb::borrow<nb::object>(PyEval_GetBuiltins());
    return nb::cast(std::move(state), nb::rv_policy::move);
}
nb::object dask_slicing_iterator(nb::object cache_getter, nb::dict namespace_, DaskSlicingLocals& owner) {
    DaskSlicingIterator state;
    state.locals_owner = nb::cast(&owner, nb::rv_policy::reference);
    state.cache_getter = std::move(cache_getter);
    state.namespace_ = std::move(namespace_);
    state.builtins = nb::borrow<nb::object>(PyEval_GetBuiltins());
    return nb::cast(std::move(state), nb::rv_policy::move);
}
void di_generator_names(nb::handle value, const char* name, const char* qualname) {
    nb::object public_name = nb::str(name), public_qualname = nb::str(qualname);
    if (PyObject_SetAttrString(value.ptr(), "__name__", public_name.ptr()) < 0 ||
        PyObject_SetAttrString(value.ptr(), "__qualname__", public_qualname.ptr()) < 0)
        throw nb::python_error();
}
nb::object dask_is_data(nb::object data, nb::object generator_factory, nb::dict namespace_) {
    DaskNamespace ns(std::move(namespace_));
    if (di_truth(di_call1(di_attr(ns.get("dask"), "is_dask_collection"), data))) return nb::bool_(true);
    nb::object isinstance = ns.get("isinstance");
    nb::object sequence = di_attr(di_attr(ns.get("collections"), "abc"), "Sequence");
    nb::object selected = di_call2(std::move(isinstance), data, std::move(sequence));
    if (!di_truth(selected)) return selected;
    selected.reset();
    nb::object any = ns.get("any");
    nb::object function = di_call0(generator_factory);
    di_generator_names(function, "<genexpr>", "_is_dask_data.<locals>.<genexpr>");
    nb::object iterator = di_checked(PyObject_GetIter(data.ptr()));
    nb::object generator = di_call1(std::move(function), std::move(iterator));
    di_generator_names(generator, "<genexpr>", "_is_dask_data.<locals>.<genexpr>");
    return di_call1(std::move(any), std::move(generator));
}
nb::object dask_configure(nb::object data, nb::object cache, nb::object generator_factory, nb::dict namespace_) {
    DaskNamespace ns(std::move(namespace_));
    if (!di_truth(di_call1(ns.get("_is_dask_data"), data))) return di_attr(ns.get("contextlib"), "nullcontext");
    nb::object selected;
    if (di_truth(cache)) selected = di_call0(ns.get("resize_dask_cache"));
    else selected = di_call0(di_attr(ns.get("contextlib"), "nullcontext"));
    if (!PyFunction_Check(generator_factory.ptr())) throw nb::type_error("generator factory must be a Python function");
    PyObject* closure = PyFunction_GetClosure(generator_factory.ptr());
    if (!closure || PyTuple_GET_SIZE(closure) != 1 || !PyCell_Check(PyTuple_GET_ITEM(closure, 0)))
        throw nb::type_error("generator factory must retain the genuine _cache cell");
    if (PyCell_Set(PyTuple_GET_ITEM(closure, 0), selected.ptr()) < 0) throw nb::python_error();
    selected.reset();
    nb::object decorator = di_attr(ns.get("contextlib"), "contextmanager");
    nb::object function = di_call0(generator_factory);
    di_generator_names(function, "dask_optimized_slicing", "configure_dask.<locals>.dask_optimized_slicing");
    return di_call1(std::move(decorator), std::move(function));
}

} // namespace

void bind_dask_indexing(nb::module_& module) {
    nb::class_<DaskResizeLocals>(module, "DaskResizeLocals", nb::is_weak_referenceable(), nb::type_slots(di_resize_slots))
        .def_prop_ro("nbytes", [](const DaskResizeLocals& s) { return di_optional(s.nbytes); })
        .def_prop_ro("virtual_memory", [](const DaskResizeLocals& s) { return di_optional(s.virtual_memory); })
        .def_prop_ro("avail", [](const DaskResizeLocals& s) { return di_optional(s.avail); });
    nb::class_<DaskCollectionLocals>(module, "DaskCollectionLocals", nb::is_weak_referenceable(), nb::type_slots(di_collection_locals_slots))
        .def_prop_ro("i", [](const DaskCollectionLocals& s) { return di_optional(s.i); });
    nb::class_<DaskSlicingLocals>(module, "DaskSlicingLocals", nb::is_weak_referenceable(), nb::type_slots(di_slicing_locals_slots))
        .def_prop_ro("opts", [](const DaskSlicingLocals& s) { return di_optional(s.opts); })
        .def_prop_ro("cfg", [](const DaskSlicingLocals& s) { return di_optional(s.cfg); })
        .def_prop_ro("c", [](const DaskSlicingLocals& s) { return di_optional(s.c); });
    nb::class_<DaskCollectionIterator> collection(module, "DaskCollectionIterator",
        nb::is_weak_referenceable(), nb::type_slots(di_collection_slots));
    collection.def("__iter__", [](nb::handle self) { return nb::borrow<nb::object>(self); })
        .def("__next__", &di_collection_next)
        .def("send", [](nb::handle self, nb::object value) {
            const auto& state = *nb::inst_ptr<DaskCollectionIterator>(self);
            if (state.running) throw nb::value_error("generator already executing");
            if (!state.closed && !state.started && !value.is_none())
                throw nb::type_error("can't send non-None value to a just-started generator");
            return di_collection_next(self);
        }, nb::arg("value").none())
        .def("throw", &di_collection_throw).def("close", &di_collection_close);
    nb::class_<DaskSlicingIterator> slicing(module, "DaskSlicingIterator",
        nb::is_weak_referenceable(), nb::type_slots(di_slicing_slots));
    slicing.def("__iter__", [](nb::handle self) { return nb::borrow<nb::object>(self); })
        .def("__next__", &di_slicing_next)
        .def("send", [](nb::handle self, nb::object value) {
            const auto& state = *nb::inst_ptr<DaskSlicingIterator>(self);
            if (state.running) throw nb::value_error("generator already executing");
            if (!state.closed && !state.started && !value.is_none())
                throw nb::type_error("can't send non-None value to a just-started generator");
            return di_slicing_next(self);
        }, nb::arg("value").none())
        .def("throw", &di_slicing_throw).def("close", &di_slicing_close);
    auto* collection_type = reinterpret_cast<PyTypeObject*>(collection.ptr());
    di_collection_base_dealloc = collection_type->tp_dealloc;
    collection_type->tp_dealloc = di_collection_dealloc;
    PyType_Modified(collection_type);
    auto* slicing_type = reinterpret_cast<PyTypeObject*>(slicing.ptr());
    di_slicing_base_dealloc = slicing_type->tp_dealloc;
    slicing_type->tp_dealloc = di_slicing_dealloc;
    PyType_Modified(slicing_type);
    module.def("dask_resize_locals", [](nb::object nbytes) {
        DaskResizeLocals owner;
        owner.nbytes = std::move(nbytes);
        return owner;
    }, nb::arg("nbytes").none());
    module.def("dask_resize_cache", &dask_resize_cache,
        nb::arg("mem_fraction").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("dask_is_data", &dask_is_data,
        nb::arg("data").none(), nb::arg("generator_factory").none(), nb::arg("namespace"));
    module.def("dask_collection_locals", [] { return DaskCollectionLocals(); });
    module.def("dask_collection_iterator", &dask_collection_iterator,
        nb::arg("iterator").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("dask_configure", &dask_configure,
        nb::arg("data").none(), nb::arg("cache").none(), nb::arg("generator_factory").none(), nb::arg("namespace"));
    module.def("dask_slicing_locals", [] { return DaskSlicingLocals(); });
    module.def("dask_slicing_iterator", &dask_slicing_iterator,
        nb::arg("cache_getter").none(), nb::arg("namespace"), nb::arg("locals_owner"));
}
} // namespace nx
