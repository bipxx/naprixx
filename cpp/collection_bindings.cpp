#include <nanobind/stl/string.h>
#include "collection_bindings.hpp"
#include "collection_protocol.hpp"
#include "iterator_frame.hpp"
#include <array>
#include <string>
#include <utility>

// Complete application policy for TypedMutableSequence and EventedList.
// The actual Python facade inherits the existing TransformNamespaceBase; this
// unit creates no independent container store, native solid base or callback
// registry. _list/_basetypes/_lookup/events remain the actual shared objects.
namespace nx {
namespace nb = nanobind;
namespace {
nb::object ct_check(PyObject* value) { if (!value) throw nb::python_error(); return nb::steal<nb::object>(value); }
nb::object ct_attr(nb::object object, const char* name) { return ct_check(PyObject_GetAttrString(object.ptr(), name)); }
nb::object ct_item(nb::object object, nb::object key) { return ct_check(PyObject_GetItem(object.ptr(), key.ptr())); }
nb::object ct_call0(nb::object fn) { return ct_check(PyObject_CallNoArgs(fn.ptr())); }
nb::object ct_call1(nb::object fn, nb::object a) { return ct_check(PyObject_CallOneArg(fn.ptr(), a.ptr())); }
nb::object ct_call2(nb::object fn, nb::object a, nb::object b) { return ct_check(PyObject_CallFunctionObjArgs(fn.ptr(), a.ptr(), b.ptr(), nullptr)); }
nb::object ct_kw(nb::object fn, nb::tuple args, nb::dict kwargs) { return ct_check(PyObject_Call(fn.ptr(), args.ptr(), kwargs.ptr())); }
void ct_store(nb::object self, const char* name, nb::object value) { if (PyObject_SetAttrString(self.ptr(), name, value.ptr()) < 0) throw nb::python_error(); }
void ct_set(nb::object object, nb::object key, nb::object value) { if (PyObject_SetItem(object.ptr(), key.ptr(), value.ptr()) < 0) throw nb::python_error(); }
void ct_replace(nb::object& target, nb::object value) { nb::object old = std::move(target); target = std::move(value); old.reset(); }
nb::object ct_optional(const nb::object& value) { return value.is_valid() ? value : nb::none(); }
nb::object ct_bin(nb::object a, nb::object b, binaryfunc op) { return ct_check(op(a.ptr(), b.ptr())); }
nb::object ct_cmp(nb::object a, nb::object b, int op) { return ct_check(PyObject_RichCompare(a.ptr(), b.ptr(), op)); }
bool ct_truth(nb::object value) { int r = PyObject_IsTrue(value.ptr()); if (r < 0) throw nb::python_error(); return r != 0; }
bool ct_test(nb::object a, nb::object b, int op) { return ct_truth(ct_cmp(std::move(a), std::move(b), op)); }
bool ct_contains(nb::object object, nb::object key) { int r = PySequence_Contains(object.ptr(), key.ptr()); if (r < 0) throw nb::python_error(); return r != 0; }
nb::object ct_iter(nb::object value) { return ct_check(PyObject_GetIter(value.ptr())); }
nb::object ct_next(nb::handle iterator) { PyObject* value = PyIter_Next(iterator.ptr()); if (!value && PyErr_Occurred()) throw nb::python_error(); return nb::steal<nb::object>(value); }
nb::str ct_name(const char* value) { PyObject* name=PyUnicode_InternFromString(value);if(!name)throw nb::python_error();return nb::steal<nb::str>(name); }
nb::object ct_repr(nb::object value) { return ct_check(PyObject_Repr(value.ptr())); }
nb::object ct_format(nb::object value) { return ct_check(PyObject_Format(value.ptr(), nullptr)); }
nb::object ct_concat(nb::object a, nb::object b) { return ct_check(PyUnicode_Concat(a.ptr(), b.ptr())); }
nb::object ct_lookup(nb::handle mapping, nb::handle key) {
    if (PyDict_CheckExact(mapping.ptr())) {
        PyObject* value = PyDict_GetItemWithError(mapping.ptr(), key.ptr());
        if (value) return nb::borrow<nb::object>(value);
        if (PyErr_Occurred()) throw nb::python_error();
        return nb::object();
    }
    PyObject* value = PyObject_GetItem(mapping.ptr(), key.ptr());
    if (value) return nb::steal<nb::object>(value);
    if (!PyErr_ExceptionMatches(PyExc_KeyError)) throw nb::python_error();
    PyErr_Clear(); return nb::object();
}
struct ContainerNamespace {
    nb::dict globals; nb::object builtins;
    explicit ContainerNamespace(nb::dict ns) : globals(std::move(ns)), builtins(nb::borrow<nb::object>(PyEval_GetBuiltins())) {}
    ContainerNamespace(nb::dict ns, nb::object b) : globals(std::move(ns)), builtins(std::move(b)) {}
    nb::object get(const char* name) const {
        nb::object key = ct_name(name), result = ct_lookup(globals, key);
        if (!result.is_valid()) result = ct_lookup(builtins, key);
        if (result.is_valid()) return result;
        nb::object error = ct_call1(nb::borrow<nb::object>(PyExc_NameError), ct_check(PyUnicode_FromFormat("name '%s' is not defined", name)));
        if (PyObject_SetAttrString(error.ptr(), "name", key.ptr()) < 0) throw nb::python_error();
        PyErr_SetObject(PyExc_NameError, error.ptr()); throw nb::python_error();
    }
};
nb::dict ct_legacy_namespace(bool evented) {
    nb::module_ module = nb::module_::import_(evented ? "napari.utils.events.containers._evented_list" : "napari.utils.events.containers._typed");
    return nb::borrow<nb::dict>(PyModule_GetDict(module.ptr()));
}
nb::object ct_exception_instance(nb::object error) {
    if (PyExceptionClass_Check(error.ptr())) {
        nb::object instance = ct_call0(error);
        if (!PyExceptionInstance_Check(instance.ptr())) {
            PyErr_Format(PyExc_TypeError, "calling %R should have returned an instance of BaseException, not %R", error.ptr(), reinterpret_cast<PyObject*>(Py_TYPE(instance.ptr())));
            throw nb::python_error();
        }
        error = std::move(instance);
    } else if (!PyExceptionInstance_Check(error.ptr())) {
        PyErr_SetString(PyExc_TypeError, "exceptions must derive from BaseException"); throw nb::python_error();
    }
    return error;
}
[[noreturn]] void ct_raise_value(nb::object error) {
    error = ct_exception_instance(std::move(error));
    PyErr_SetObject(reinterpret_cast<PyObject*>(Py_TYPE(error.ptr())), error.ptr()); throw nb::python_error();
}
[[noreturn]] void ct_raise(nb::object factory, nb::object message) { ct_raise_value(ct_call1(std::move(factory), std::move(message))); }
void ct_validate_except(nb::handle type) {
    bool valid = true;
    if (PyTuple_Check(type.ptr())) {
        for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(type.ptr()); ++i)
            if (!PyExceptionClass_Check(PyTuple_GET_ITEM(type.ptr(), i))) { valid = false; break; }
    } else valid = PyExceptionClass_Check(type.ptr()) != 0;
    if (!valid) { PyErr_SetString(PyExc_TypeError, "catching classes that do not inherit from BaseException is not allowed"); throw nb::python_error(); }
}
bool ct_matches(nb::handle error, nb::object type) { ct_validate_except(type); return PyErr_GivenExceptionMatches(error.ptr(), type.ptr()) != 0; }
nb::object ct_super(nb::object provider, nb::object self, const char* method, bool already_bound) {
    nb::object proxy = already_bound ? std::move(provider) : ct_call1(std::move(provider), std::move(self));
    return ct_attr(std::move(proxy), method);
}
nb::object ct_star(nb::object fn, nb::object args) {
    if (!PyTuple_CheckExact(args.ptr())) {
        if (!Py_TYPE(args.ptr())->tp_iter && !PySequence_Check(args.ptr())) {
            nb::object label = ct_check(_PyObject_FunctionStr(fn.ptr()));
            PyErr_Format(PyExc_TypeError, "%U argument after * must be an iterable, not %.200s", label.ptr(), Py_TYPE(args.ptr())->tp_name);
            throw nb::python_error();
        }
        nb::object tuple = ct_check(PySequence_Tuple(args.ptr()));
        args.reset(); args = std::move(tuple);
    }
    return ct_check(PyObject_CallObject(fn.ptr(), args.ptr()));
}
void ct_merge_kwargs(nb::handle callable, nb::dict& keywords, nb::handle mapping) {
    if (_PyDict_MergeEx(keywords.ptr(), mapping.ptr(), 2) >= 0) return;
    if (PyErr_ExceptionMatches(PyExc_AttributeError)) {
        PyErr_Clear(); nb::object label=ct_check(_PyObject_FunctionStr(callable.ptr()));
        PyErr_Format(PyExc_TypeError,"%U argument after ** must be a mapping, not %.200s",label.ptr(),Py_TYPE(mapping.ptr())->tp_name);
    } else if (PyErr_ExceptionMatches(PyExc_KeyError)) {
        PyObject *type=nullptr,*value=nullptr,*traceback=nullptr;
        PyErr_Fetch(&type,&value,&traceback); PyErr_NormalizeException(&type,&value,&traceback);
        nb::object error_type=nb::steal<nb::object>(type),error_value=nb::steal<nb::object>(value),error_traceback=nb::steal<nb::object>(traceback);
        if (!error_value.is_valid()) throw nb::python_error();
        nb::object key=ct_item(ct_attr(error_value,"args"),nb::int_(0)),label=ct_check(_PyObject_FunctionStr(callable.ptr()));
        PyErr_Format(PyExc_TypeError,"%U got multiple values for keyword argument '%S'",label.ptr(),key.ptr());
    }
    throw nb::python_error();
}
std::pair<nb::object, nb::object> ct_pair(nb::object value) {
    PyObject* raw = PyObject_GetIter(value.ptr());
    if (!raw) {
        if (PyErr_ExceptionMatches(PyExc_TypeError) && !Py_TYPE(value.ptr())->tp_iter && !PySequence_Check(value.ptr()))
            PyErr_Format(PyExc_TypeError, "cannot unpack non-iterable %.200s object", Py_TYPE(value.ptr())->tp_name);
        throw nb::python_error();
    }
    nb::object iterator = nb::steal<nb::object>(raw);
    nb::object first = ct_next(iterator);
    if (!first.is_valid()) { PyErr_SetString(PyExc_ValueError, "not enough values to unpack (expected 2, got 0)"); throw nb::python_error(); }
    nb::object second = ct_next(iterator);
    if (!second.is_valid()) { PyErr_SetString(PyExc_ValueError, "not enough values to unpack (expected 2, got 1)"); throw nb::python_error(); }
    nb::object extra = ct_next(iterator);
    if (extra.is_valid()) { extra.reset(); PyErr_SetString(PyExc_ValueError, "too many values to unpack (expected 2)"); throw nb::python_error(); }
    return {std::move(first), std::move(second)};
}
template <typename Body> void ct_with(nb::object manager, Body body) {
    nb::object enter = collections::context_method(manager, "__enter__", false);
    nb::object exit = collections::context_method(manager, "__exit__", true);
    manager.reset();
    ct_call0(std::move(enter));
    try { body(); }
    catch (const nb::python_error& original) {
        bool suppress;
        {
            events::HandledException handled(original.value());
            nb::object traceback = original.traceback(); if (!traceback.is_valid()) traceback = nb::none();
            nb::object result = ct_check(PyObject_CallFunctionObjArgs(exit.ptr(), original.type().ptr(), original.value().ptr(), traceback.ptr(), nullptr));
            // WITH_EXCEPT_START retains exit through suppression truth; POP_EXCEPT
            // restores the caller's handled state before saved-exit cleanup.
            suppress = ct_truth(std::move(result));
            traceback.reset();
        }
        if (!suppress) throw;
        exit.reset();
        return;
    }
    ct_check(PyObject_CallFunctionObjArgs(exit.ptr(), Py_None, Py_None, Py_None, nullptr));
}

struct TypedContainerLocals {
    nb::object lookup, result, _key, new_, copy, start, stop, convert, i, v;
    TypedContainerLocals() = default; TypedContainerLocals(TypedContainerLocals&&) = default;
    ~TypedContainerLocals() { clear(); }
    void clear() {
        std::array<nb::object, 10> old = {std::move(lookup), std::move(result), std::move(_key), std::move(new_), std::move(copy), std::move(start), std::move(stop), std::move(convert), std::move(i), std::move(v)};
        for (auto& edge : old) edge.reset();
    }
};
enum class EventedLayout { Init, Set, Indices, Delete, Move, Multiple };
struct EventedContainerLocals {
    EventedLayout layout = EventedLayout::Init;
    nb::object lookup, _events, value, old, v, indices, i, start, valid, parent, index, item, dest_index, move_plan, src, dest;
    EventedContainerLocals() = default; EventedContainerLocals(EventedContainerLocals&&) = default;
    ~EventedContainerLocals() { clear(); }
    void clear() {
        std::array<nb::object, 16> previous = {std::move(lookup), std::move(_events), std::move(value), std::move(old), std::move(v), std::move(indices), std::move(i), std::move(start), std::move(valid), std::move(parent), std::move(index), std::move(item), std::move(dest_index), std::move(move_plan), std::move(src), std::move(dest)};
        if (layout == EventedLayout::Move) { previous[12].reset(); previous[11].reset(); }
        for (auto& edge : previous) edge.reset();
    }
};
template <typename State> int ct_local_clear(PyObject* self) { if (nb::inst_ready(self)) nb::inst_ptr<State>(self)->clear(); return 0; }
int ct_typed_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self)); if (nb::inst_ready(self)) {
        const auto& s = *nb::inst_ptr<TypedContainerLocals>(self);
        Py_VISIT(s.lookup.ptr()); Py_VISIT(s.result.ptr()); Py_VISIT(s._key.ptr()); Py_VISIT(s.new_.ptr()); Py_VISIT(s.copy.ptr()); Py_VISIT(s.start.ptr()); Py_VISIT(s.stop.ptr()); Py_VISIT(s.convert.ptr()); Py_VISIT(s.i.ptr()); Py_VISIT(s.v.ptr());
    } return 0;
}
int ct_evented_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self)); if (nb::inst_ready(self)) {
        const auto& s = *nb::inst_ptr<EventedContainerLocals>(self);
        Py_VISIT(s.lookup.ptr()); Py_VISIT(s._events.ptr()); Py_VISIT(s.value.ptr()); Py_VISIT(s.old.ptr()); Py_VISIT(s.v.ptr()); Py_VISIT(s.indices.ptr()); Py_VISIT(s.i.ptr()); Py_VISIT(s.start.ptr()); Py_VISIT(s.valid.ptr()); Py_VISIT(s.parent.ptr()); Py_VISIT(s.index.ptr()); Py_VISIT(s.item.ptr()); Py_VISIT(s.dest_index.ptr()); Py_VISIT(s.move_plan.ptr()); Py_VISIT(s.src.ptr()); Py_VISIT(s.dest.ptr());
    } return 0;
}
PyType_Slot ct_typed_slots[] = {{Py_tp_traverse, reinterpret_cast<void*>(ct_typed_traverse)}, {Py_tp_clear, reinterpret_cast<void*>(ct_local_clear<TypedContainerLocals>)}, {0,nullptr}};
PyType_Slot ct_evented_slots[] = {{Py_tp_traverse, reinterpret_cast<void*>(ct_evented_traverse)}, {Py_tp_clear, reinterpret_cast<void*>(ct_local_clear<EventedContainerLocals>)}, {0,nullptr}};

// Existing .frame remains the sole suspended-local view, not container state.
struct IteratorState : collections::IteratorFrame {
    bool started = false, exhausted = false, running = false, pending = false, source_semantics = true;
    nb::object namespace_, builtins;
    void clear(bool release = false) {
        exhausted = true; pending = false;
        nb::object old_namespace = std::move(namespace_), old_builtins = std::move(builtins);
        if (release) release_frame(); else clear_frame();
        old_namespace.reset(); old_builtins.reset();
    }
    void gc_clear() { clear(true); }
};
struct MoveIterator : IteratorState {};
struct CompletionIterator : IteratorState {};
enum class ExpressionKind { Types, Sum };
struct ExpressionIterator : IteratorState { ExpressionKind kind = ExpressionKind::Types; };
struct ContainerRunning {
    IteratorState& state;
    explicit ContainerRunning(IteratorState& s) : state(s) { if (state.running) throw nb::value_error("generator already executing"); state.running = true; }
    ~ContainerRunning() { state.running = false; }
};
nb::object ct_cell(nb::object getter) {
    if (!PyFunction_Check(getter.ptr())) throw nb::type_error("binding getter must be a genuine function");
    PyObject* cells = PyFunction_GetClosure(getter.ptr());
    if (!cells || PyTuple_GET_SIZE(cells) != 1) throw nb::type_error("binding getter must capture exactly one original cell");
    return nb::borrow<nb::object>(PyTuple_GET_ITEM(cells, 0));
}
nb::object ct_cell_value(nb::handle cell, const char* name) {
    nb::object result = nb::steal<nb::object>(PyCell_Get(cell.ptr()));
    if (result.is_valid()) return result;
    if (PyErr_Occurred()) throw nb::python_error();
    nb::object message=ct_check(PyUnicode_FromFormat("cannot access free variable '%s' where it is not associated with a value in enclosing scope",name));
    nb::object error=ct_call1(nb::borrow<nb::object>(PyExc_NameError),std::move(message));
    if(PyObject_SetAttrString(error.ptr(),"name",ct_name(name).ptr())<0)throw nb::python_error();
    PyErr_SetObject(PyExc_NameError,error.ptr());throw nb::python_error();
}
void ct_cell_store(nb::handle cell, nb::object value) { if (PyCell_Set(cell.ptr(), value.ptr()) < 0) throw nb::python_error(); }
nb::object ct_generator(nb::object factory, nb::object delegate, const char* qualified) {
    nb::object result = ct_call1(std::move(factory), std::move(delegate));
    if (PyObject_SetAttrString(result.ptr(), "__name__", nb::str("<genexpr>").ptr()) < 0 || PyObject_SetAttrString(result.ptr(), "__qualname__", nb::str(qualified).ptr()) < 0) throw nb::python_error();
    return result;
}
nb::object ct_expression(ExpressionKind kind, nb::object iterator, nb::object cell, ContainerNamespace& ns, nb::object factory, const char* qualified) {
    ExpressionIterator state; state.kind = kind; state.namespace_ = ns.globals; state.builtins = ns.builtins;
    nb::dict frame; frame["iterator"] = std::move(iterator); frame["cell"] = std::move(cell); state.frame_owner = std::move(frame);
    nb::object delegate = nb::cast(std::move(state), nb::rv_policy::move);
    if (!factory.is_valid()) return delegate;
    return ct_generator(std::move(factory), std::move(delegate), qualified);
}
nb::object ct_advance_expression(nb::handle self, ExpressionIterator& state) {
    state.started = true; nb::dict frame = state.frame();
    nb::object next = ct_next(frame["iterator"]); if (!next.is_valid()) return nb::object();
    frame["current"] = std::move(next);
    ContainerNamespace ns(nb::borrow<nb::dict>(state.namespace_), state.builtins);
    if (state.kind == ExpressionKind::Types) {
        nb::object fn = ns.get("isinstance"); nb::object value = ct_cell_value(frame["cell"], "e");
        return ct_call2(std::move(fn), std::move(value), frame["current"]);
    }
    nb::object left = frame["current"], right = ct_cell_value(frame["cell"], "src");
    return ct_cmp(std::move(left), std::move(right), Py_LE);
}
nb::object ct_advance_completion(nb::handle self, CompletionIterator& state) {
    state.started = true; nb::dict frame = state.frame();
    nb::object next = ct_next(frame["iterator"]); if (!next.is_valid()) return nb::object();
    frame["x"] = std::move(next);
    ContainerNamespace ns(nb::borrow<nb::dict>(state.namespace_), state.builtins);
    nb::object sequence = frame.contains("self_cell") ? ct_cell_value(frame["self_cell"], "self") : nb::object(frame["self"]);
    nb::object lookup = ct_attr(std::move(sequence), "_lookup");
    nb::object converter = ct_item(std::move(lookup), ns.get("str"));
    return ct_call1(std::move(converter), frame["x"]);
}
nb::object ct_slice_range(nb::object self, nb::object key, ContainerNamespace& ns) {
    nb::object range = ns.get("range"), indices = ct_attr(std::move(key), "indices");
    nb::object count = ct_call1(ns.get("len"), std::move(self));
    nb::object args = ct_call1(std::move(indices), std::move(count));
    return ct_star(std::move(range), std::move(args));
}
nb::object ct_advance_move(nb::handle self_, MoveIterator& state) {
    nb::dict frame = state.frame(); ContainerNamespace ns(nb::borrow<nb::dict>(state.namespace_), state.builtins);
    if (!state.started) {
        // Initialization belongs to the original generator's first resume.
        state.started = true;
        nb::object fn = ns.get("isinstance");
        nb::object destination = frame["destination"], slice = ns.get("slice");
        if (ct_truth(ct_call2(std::move(fn), std::move(destination), std::move(slice))))
            ct_raise(ns.get("TypeError"), frame["destination_message"]);
        frame["to_move"] = nb::list();
        nb::object iterator = ct_iter(frame["sources"]);
        while (nb::object next = ct_next(iterator)) {
            frame["idx"] = std::move(next);
            nb::object instance = ns.get("isinstance"), idx = frame["idx"], kind = ns.get("slice");
            if (ct_truth(ct_call2(std::move(instance), std::move(idx), std::move(kind)))) {
                nb::object extend = ct_attr(frame["to_move"], "extend"), list = ns.get("list");
                nb::object indices = ct_slice_range(frame["self"], frame["idx"], ns);
                ct_call1(std::move(extend), ct_call1(std::move(list), std::move(indices)));
            } else {
                instance = ns.get("isinstance"); idx = frame["idx"]; kind = ns.get("int");
                if (ct_truth(ct_call2(std::move(instance), std::move(idx), std::move(kind))))
                    ct_call1(ct_attr(frame["to_move"], "append"), frame["idx"]);
                else {
                    nb::object error = ns.get("TypeError"), type = ct_call1(ns.get("type"), frame["idx"]);
                    ct_raise(std::move(error), ct_concat(nb::str("Can only move integer or slice indices, not "), ct_format(std::move(type))));
                }
            }
        }
        iterator.reset();
        nb::object list = ns.get("list"), fromkeys = ct_attr(ns.get("dict"), "fromkeys");
        nb::object unique = ct_call1(std::move(list), ct_call1(std::move(fromkeys), frame["to_move"]));
        frame["to_move"] = std::move(unique);
        if (ct_test(frame["destination"], nb::int_(0), Py_LT)) {
            nb::object count = ct_call1(ns.get("len"), frame["self"]);
            nb::object plus = ct_bin(std::move(count), nb::int_(1), PyNumber_Add);
            frame["destination"] = ct_bin(frame["destination"], std::move(plus), PyNumber_InPlaceAdd);
        }
        frame["d_inc"] = nb::int_(0); frame["popped"] = nb::list();
        nb::object entries = ct_call1(ns.get("enumerate"), frame["to_move"]);
        frame["entries"] = ct_iter(std::move(entries));
    }
    auto after_yield = [&] {
        nb::object append = ct_attr(frame["popped"], "append");
        ct_call1(std::move(append), ct_cell_value(frame["source"], "src"));
        if (ct_test(frame["destination"], ct_cell_value(frame["source"], "src"), Py_LE))
            frame["d_inc"] = ct_bin(frame["d_inc"], nb::int_(1), PyNumber_InPlaceAdd);
    };
    if (state.pending) { state.pending = false; after_yield(); }
    while (nb::object next = ct_next(frame["entries"])) {
        auto values = ct_pair(std::move(next));
        frame["i"] = std::move(values.first);
        ct_cell_store(frame["source"], std::move(values.second));
        if (ct_test(ct_cell_value(frame["source"], "src"), frame["destination"], Py_NE)) {
            nb::object previous_src = ct_cell_value(frame["source"], "src");
            nb::object sum = ns.get("sum");
            nb::object iterator = ct_iter(frame["popped"]);
            nb::object factory=frame.contains("generator_factory")?nb::object(frame["generator_factory"]):nb::object();
            nb::object generator = ct_expression(ExpressionKind::Sum, std::move(iterator), frame["source"], ns, std::move(factory), "EventedList._move_plan.<locals>.<genexpr>");
            nb::object removed = ct_call1(std::move(sum), std::move(generator));
            ct_cell_store(frame["source"], ct_bin(std::move(previous_src), std::move(removed), PyNumber_InPlaceSubtract));
            if (ct_test(ct_cell_value(frame["source"], "src"), frame["destination"], Py_GE))
                ct_cell_store(frame["source"], ct_bin(ct_cell_value(frame["source"], "src"), frame["i"], PyNumber_InPlaceAdd));
            nb::object source = ct_cell_value(frame["source"], "src");
            nb::object destination = ct_bin(frame["destination"], frame["d_inc"], PyNumber_Add);
            state.pending = true; return nb::make_tuple(std::move(source), std::move(destination));
        }
        after_yield();
    }
    return nb::object();
}
template <typename State, typename Advance> nb::object ct_resume(nb::handle self, nb::object sent, Advance advance) {
    auto& state = *nb::inst_ptr<State>(self);
    if (state.exhausted) throw nb::stop_iteration();
    ContainerRunning active(state);
    if (!state.started && !sent.is_none()) throw nb::type_error("can't send non-None value to a just-started generator");
    try {
        nb::object result = advance(self, state);
        if (result.is_valid()) return result;
        state.clear(); throw nb::stop_iteration();
    } catch (const nb::python_error& failure) {
        events::HandledException handled(failure.value());
        if (state.source_semantics) state.exhausted = true; else state.clear();
        if (failure.matches(PyExc_StopIteration)) collections::raise_exception(failure.value(), true);
        throw;
    } catch (...) { state.clear(); throw; }
}
template <typename State> void ct_close(nb::handle self) {
    auto& state = *nb::inst_ptr<State>(self);
    if (state.exhausted) return;
    ContainerRunning active(state);
    if (!state.started) { state.clear(); return; }
    nb::object exit = ct_call0(nb::borrow<nb::object>(PyExc_GeneratorExit));
    events::HandledException handled(exit); state.clear();
}
template <typename State> void ct_throw(nb::handle self, nb::args args) {
    nb::object exception = collections::normalize_throw(args);
    auto& state = *nb::inst_ptr<State>(self); ContainerRunning active(state);
    bool open = !state.exhausted;
    if (state.source_semantics) state.exhausted = true; else state.clear();
    events::HandledException handled(exception); collections::raise_exception(exception, open);
}
template <typename State> int ct_iterator_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self)); if (nb::inst_ready(self)) {
        const auto& s = *nb::inst_ptr<State>(self);
        Py_VISIT(s.frame_owner.ptr()); Py_VISIT(s.namespace_.ptr()); Py_VISIT(s.builtins.ptr());
    } return 0;
}
template <typename State> int ct_iterator_clear(PyObject* self) { if (nb::inst_ready(self)) nb::inst_ptr<State>(self)->gc_clear(); return 0; }
template <typename State> void ct_finalize(PyObject* self) noexcept {
    if (!nb::inst_ready(self) || nb::inst_ptr<State>(self)->exhausted) return;
    PyObject *type=nullptr,*value=nullptr,*trace=nullptr; PyErr_Fetch(&type,&value,&trace);
    try { ct_close<State>(self); }
    catch (nb::python_error& error) { error.restore(); PyErr_WriteUnraisable(self); }
    catch (const std::exception& error) { PyErr_SetString(PyExc_RuntimeError,error.what()); PyErr_WriteUnraisable(self); }
    catch (...) { PyErr_SetString(PyExc_RuntimeError,"failed to close container iterator"); PyErr_WriteUnraisable(self); }
    PyErr_Restore(type,value,trace);
}
template <typename State> PyType_Slot* ct_iterator_slots() {
    static PyType_Slot slots[]={{Py_tp_traverse,reinterpret_cast<void*>(ct_iterator_traverse<State>)},{Py_tp_clear,reinterpret_cast<void*>(ct_iterator_clear<State>)},{Py_tp_finalize,reinterpret_cast<void*>(ct_finalize<State>)},{0,nullptr}};
    return slots;
}
template <typename State> destructor& ct_base_dealloc() { static destructor value=nullptr; return value; }
template <typename State> void ct_iterator_dealloc(PyObject* self) noexcept {
    PyObject_GC_UnTrack(self); PyObject_ClearWeakRefs(self); PyObject_GC_Track(self);
    if (PyObject_CallFinalizerFromDealloc(self)<0) return;
    ct_base_dealloc<State>()(self);
}
nb::object ct_make_completion(nb::object self, nb::object iterator, ContainerNamespace& ns, nb::object self_cell, bool source_semantics) {
    CompletionIterator state; state.source_semantics=source_semantics; state.namespace_=ns.globals; state.builtins=ns.builtins;
    nb::dict frame; frame["iterator"]=std::move(iterator);
    if (self_cell.is_valid()) frame["self_cell"]=std::move(self_cell); else frame["self"]=std::move(self);
    state.frame_owner=std::move(frame); return nb::cast(std::move(state),nb::rv_policy::move);
}
nb::object ct_make_move(nb::object self, nb::object sources, nb::object destination, ContainerNamespace& ns, nb::object cell, nb::object factory, nb::object destination_message, bool source_semantics) {
    MoveIterator state; state.source_semantics=source_semantics; state.namespace_=ns.globals; state.builtins=ns.builtins;
    nb::dict frame; frame["self"]=std::move(self);frame["sources"]=std::move(sources);frame["destination"]=std::move(destination);frame["source"]=std::move(cell);if(factory.is_valid())frame["generator_factory"]=std::move(factory);frame["destination_message"]=std::move(destination_message);
    state.frame_owner=std::move(frame);return nb::cast(std::move(state),nb::rv_policy::move);
}

void sequence_init_context(nb::object self, nb::object data, nb::object basetype, nb::dict namespace_, TypedContainerLocals& s) {
    ContainerNamespace ns(std::move(namespace_));
    if (s.lookup.is_none()) ct_replace(s.lookup, nb::dict());
    ct_store(self,"_list",nb::list());
    nb::object instance=ns.get("isinstance"), kind=ns.get("Sequence");
    nb::object bases;
    if (ct_truth(ct_call2(std::move(instance),basetype,std::move(kind)))) bases=ct_call1(ns.get("tuple"),basetype);
    else bases=nb::make_tuple(basetype);
    ct_store(self,"_basetypes",std::move(bases));
    ct_store(self,"_lookup",ct_call0(ct_attr(s.lookup,"copy")));
    ct_call1(ct_attr(self,"extend"),std::move(data));
}
nb::object sequence_len_context(nb::object self, nb::dict namespace_) { ContainerNamespace ns(std::move(namespace_)); nb::object fn=ns.get("len"); return ct_call1(std::move(fn),ct_attr(std::move(self),"_list")); }
nb::object sequence_repr_context(nb::object self, nb::dict namespace_) { ContainerNamespace ns(std::move(namespace_)); nb::object fn=ns.get("repr"); return ct_call1(std::move(fn),ct_attr(std::move(self),"_list")); }
nb::object sequence_equal_context(nb::object self, nb::object other, nb::dict) { return ct_cmp(ct_attr(std::move(self),"_list"),std::move(other),Py_EQ); }
nb::object sequence_hash_context(nb::object self, nb::dict namespace_) { ContainerNamespace ns(std::move(namespace_)); return ct_call1(ns.get("id"),std::move(self)); }
void sequence_setitem_context(nb::object self, nb::object key, nb::object value, nb::dict namespace_, nb::object error_message) {
    ContainerNamespace ns(std::move(namespace_)); nb::object fn=ns.get("isinstance"), type=ns.get("slice"); nb::object replacement;
    if (ct_truth(ct_call2(std::move(fn),key,std::move(type)))) {
        fn=ns.get("isinstance");type=ns.get("Iterable");
        if (!ct_truth(ct_call2(std::move(fn),value,std::move(type)))) ct_raise(ns.get("TypeError"),std::move(error_message));
        nb::object iterator=ct_iter(value); nb::list validated;
        while (nb::object v=ct_next(iterator)) validated.append(ct_call1(ct_attr(self,"_type_check"),std::move(v)));
        iterator.reset(); replacement=std::move(validated);
    } else replacement=ct_call1(ct_attr(self,"_type_check"),value);
    nb::object target=ct_attr(self,"_list");ct_set(std::move(target),std::move(key),std::move(replacement));
}
void sequence_insert_context(nb::object self, nb::object index, nb::object value, nb::dict) {
    nb::object fn=ct_attr(ct_attr(self,"_list"),"insert");nb::object check=ct_attr(self,"_type_check");
    nb::object checked=ct_call1(std::move(check),std::move(value));ct_call2(std::move(fn),std::move(index),std::move(checked));
}
nb::object sequence_contains_context(nb::object self, nb::object key, nb::dict namespace_, nb::object super_provider, bool bound=false) {
    ContainerNamespace ns(std::move(namespace_));nb::object type=ct_call1(ns.get("type"),key);nb::object lookup=ct_attr(self,"_lookup");
    bool indexed=ct_contains(std::move(lookup),std::move(type));
    if (indexed) {
        try { nb::object target=self,fn=ct_attr(self,"index");nb::object index=ct_call1(std::move(fn),key);ct_item(std::move(target),std::move(index)); }
        catch (const nb::python_error& error) { events::HandledException handled(error.value());if(ct_matches(error.value(),ns.get("ValueError"))) return nb::bool_(false);throw; }
        return nb::bool_(true);
    }
    nb::object fn=ct_super(std::move(super_provider),self,"__contains__",bound);return ct_call1(std::move(fn),std::move(key));
}
nb::object sequence_getitem_context(nb::object self, nb::object key, nb::dict namespace_, TypedContainerLocals& s) {
    ContainerNamespace ns(std::move(namespace_));nb::object type=ct_call1(ns.get("type"),key),lookup=ct_attr(self,"_lookup");
    if (ct_contains(std::move(lookup),std::move(type))) {
        try { nb::object fn=ct_attr(self,"__getitem__"),index=ct_call1(ct_attr(self,"index"),key);return ct_call1(std::move(fn),std::move(index)); }
        catch (const nb::python_error& error) {
            events::HandledException handled(error.value());if(!ct_matches(error.value(),ns.get("ValueError")))throw;
            nb::object factory=ns.get("KeyError"),str=ns.get("str");nb::object converted=ct_call1(std::move(factory),ct_call1(std::move(str),nb::borrow<nb::object>(error.value())));
            converted=ct_exception_instance(std::move(converted));
            PyException_SetCause(converted.ptr(),error.value().inc_ref().ptr());PyException_SetContext(converted.ptr(),error.value().inc_ref().ptr());ct_raise_value(std::move(converted));
        }
    }
    ct_replace(s.result,ct_item(ct_attr(self,"_list"),key));
    nb::object fn=ns.get("isinstance"),list=ns.get("list");
    if(ct_truth(ct_call2(std::move(fn),s.result,std::move(list))))return ct_call1(ct_attr(self,"__newlike__"),s.result);
    return s.result;
}
void sequence_delitem_context(nb::object self, nb::object key, nb::dict namespace_, TypedContainerLocals& s) {
    ContainerNamespace ns(std::move(namespace_));nb::object type=ct_call1(ns.get("type"),key),lookup=ct_attr(self,"_lookup");
    ct_replace(s._key,ct_contains(std::move(lookup),std::move(type))?ct_call1(ct_attr(self,"index"),key):key);
    nb::object target=ct_attr(self,"_list");if(PyObject_DelItem(target.ptr(),s._key.ptr())<0)throw nb::python_error();
}
nb::object sequence_type_check_context(nb::object self, nb::dict namespace_, nb::object e_cell, nb::object generator_factory) {
    ContainerNamespace ns(std::move(namespace_));
    if (ct_truth(ct_attr(self,"_basetypes"))) {
        nb::object any=ns.get("any"),bases=ct_attr(self,"_basetypes");nb::object iterator=ct_iter(std::move(bases));
        nb::object generator=ct_expression(ExpressionKind::Types,std::move(iterator),e_cell,ns,std::move(generator_factory),"TypedMutableSequence._type_check.<locals>.<genexpr>");
        if (!ct_truth(ct_call1(std::move(any),std::move(generator)))) {
            nb::object error=ns.get("TypeError");nb::object type=ct_call1(ns.get("type"),ct_cell_value(e_cell,"e"));
            nb::object first=ct_concat(nb::str("Cannot add object with type "),ct_repr(std::move(type)));
            nb::object middle=ct_concat(std::move(first),nb::str(" to TypedList expecting type "));
            nb::object message=ct_concat(std::move(middle),ct_repr(ct_attr(self,"_basetypes")));
            ct_raise(std::move(error),std::move(message));
        }
    }
    return ct_cell_value(e_cell,"e");
}
nb::object sequence_newlike_context(nb::object self, nb::object iterable, nb::dict, TypedContainerLocals& s) {
    ct_replace(s.new_,ct_call0(ct_attr(self,"__class__")));
    ct_store(s.new_,"_basetypes",ct_attr(self,"_basetypes"));ct_store(s.new_,"_lookup",ct_call0(ct_attr(ct_attr(self,"_lookup"),"copy")));
    ct_call1(ct_attr(s.new_,"extend"),std::move(iterable));return s.new_;
}
nb::object sequence_copy_context(nb::object self, nb::dict) { return ct_call1(ct_attr(self,"__newlike__"),self); }
nb::object sequence_add_context(nb::object self, nb::object other, nb::dict, TypedContainerLocals& s) { ct_replace(s.copy,ct_call0(ct_attr(self,"copy")));ct_call1(ct_attr(s.copy,"extend"),std::move(other));return s.copy; }
nb::object sequence_iadd_context(nb::object self, nb::object other, nb::dict) { ct_call1(ct_attr(self,"extend"),std::move(other));return self; }
nb::object sequence_radd_context(nb::object self, nb::object other, nb::dict namespace_) { ContainerNamespace ns(std::move(namespace_));return ct_bin(std::move(other),ct_call1(ns.get("list"),std::move(self)),PyNumber_Add); }
nb::object sequence_index_context(nb::object self, nb::object value, nb::dict namespace_, TypedContainerLocals& s) {
    ContainerNamespace ns(std::move(namespace_));
    if(!s.start.is_none() && ct_test(s.start,nb::int_(0),Py_LT)) {
        nb::object max=ns.get("max"),len=ns.get("len"),count=ct_call1(std::move(len),self);
        nb::object adjusted=ct_bin(std::move(count),s.start,PyNumber_Add);ct_replace(s.start,ct_call2(std::move(max),std::move(adjusted),nb::int_(0)));
    }
    if(!s.stop.is_none() && ct_test(s.stop,nb::int_(0),Py_LT)) {
        nb::object previous=s.stop,count=ct_call1(ns.get("len"),self);ct_replace(s.stop,ct_bin(std::move(previous),std::move(count),PyNumber_InPlaceAdd));
    }
    nb::object get=ct_attr(ct_attr(self,"_lookup"),"get"),type=ct_call1(ns.get("type"),value),noop=ns.get("_noop");
    ct_replace(s.convert,ct_call2(std::move(get),std::move(type),std::move(noop)));
    nb::object indices=ct_call2(ct_attr(self,"_iter_indices"),s.start,s.stop),iterator=ct_iter(std::move(indices));
    while(nb::object next=ct_next(iterator)) {
        ct_replace(s.i,std::move(next));nb::object fn=s.convert,selected=ct_item(self,s.i);ct_replace(s.v,ct_call1(std::move(fn),std::move(selected)));
        if(s.v.ptr()==value.ptr() || ct_test(s.v,value,Py_EQ))return s.i;
    }
    iterator.reset();nb::object error=ns.get("ValueError"),message=ct_concat(ct_repr(value),nb::str(" is not in list"));ct_raise(std::move(error),std::move(message));
}
nb::object sequence_indices_context(nb::object self, nb::object start, nb::object stop, nb::dict namespace_) {
    ContainerNamespace ns(std::move(namespace_));nb::object range=ns.get("range");nb::object end=stop.is_none()?ct_call1(ns.get("len"),self):stop;
    return ct_call2(std::move(range),std::move(start),std::move(end));
}
nb::object sequence_completions_context(nb::dict namespace_, nb::object self_cell, nb::object generator_factory) {
    ContainerNamespace ns(std::move(namespace_));nb::object str=ns.get("str"),lookup=ct_attr(ct_cell_value(self_cell,"self"),"_lookup");
    if(!ct_contains(std::move(lookup),std::move(str)))return nb::none();
    nb::object iterator=ct_iter(ct_cell_value(self_cell,"self"));
    if (!generator_factory.is_valid()) {
        // The old private entry uses only the existing pure generator bridge;
        // membership and eager iterator policy still belong to this engine.
        nb::object factory=ns.get("_make_key_completions");
        return ct_call2(std::move(factory),ct_cell_value(self_cell,"self"),std::move(iterator));
    }
    nb::object delegate=ct_make_completion(nb::object(),std::move(iterator),ns,std::move(self_cell),true);
    return ct_generator(std::move(generator_factory),std::move(delegate),"TypedMutableSequence._ipython_key_completions_.<locals>.<genexpr>");
}
void evented_init_context(nb::object self, nb::object data, nb::object basetype, nb::dict namespace_, nb::object super_provider, nb::tuple event_names, EventedContainerLocals& s, bool bound=false) {
    ContainerNamespace ns(std::move(namespace_));if(s.lookup.is_none())ct_replace(s.lookup,nb::dict());
    nb::dict emitters;for(nb::handle name:event_names)emitters[name]=nb::none();ct_replace(s._events,std::move(emitters));
    nb::object has=ns.get("hasattr");bool existing=ct_truth(ct_call2(std::move(has),self,ct_name("events")));
    if(existing) { nb::object fn=ns.get("isinstance"),group=ct_attr(self,"events"),type=ns.get("EmitterGroup");existing=ct_truth(ct_call2(std::move(fn),std::move(group),std::move(type))); }
    if(existing) {
        nb::object fn=ct_attr(ct_attr(self,"events"),"add");nb::dict kwargs=nb::cast<nb::dict>(ct_check(PyDict_Copy(s._events.ptr())));ct_kw(std::move(fn),nb::tuple(),std::move(kwargs));
    } else {
        nb::object fn=ns.get("EmitterGroup");nb::dict kwargs;kwargs[ct_name("source")]=self;kwargs[ct_name("auto_connect")]=nb::bool_(false);
        ct_merge_kwargs(fn,kwargs,s._events);ct_store(self,"events",ct_kw(std::move(fn),nb::tuple(),std::move(kwargs)));
    }
    nb::object fn=ct_super(std::move(super_provider),self,"__init__",bound);nb::tuple args=nb::make_tuple(data);nb::dict kwargs;kwargs[ct_name("basetype")]=basetype;kwargs[ct_name("lookup")]=s.lookup;
    ct_kw(std::move(fn),std::move(args),std::move(kwargs));
}
void evented_setitem_context(nb::object self, nb::object key, nb::dict namespace_, nb::object super_provider, nb::object error_message, EventedContainerLocals& s, bool bound=false) {
    ContainerNamespace ns(std::move(namespace_));ct_replace(s.old,ct_item(ct_attr(self,"_list"),key));
    nb::object fn=ns.get("isinstance"),type=ns.get("slice");
    if(ct_truth(ct_call2(std::move(fn),key,std::move(type)))) {
        fn=ns.get("isinstance");type=ns.get("Iterable");if(!ct_truth(ct_call2(std::move(fn),s.value,std::move(type))))ct_raise(ns.get("TypeError"),std::move(error_message));
        ct_replace(s.value,ct_call1(ns.get("list"),s.value));if(ct_test(s.value,s.old,Py_EQ))return;
        {
            nb::object iterator=ct_iter(s.value);nb::list validated;
            while(nb::object v=ct_next(iterator))validated.append(ct_call1(ct_attr(self,"_type_check"),std::move(v)));
        }
        if(!ct_attr(key,"step").is_none()) {
            nb::object list=ns.get("list"),range=ct_slice_range(self,key,ns);ct_replace(s.indices,ct_call1(std::move(list),std::move(range)));
            nb::object left=ct_call1(ns.get("len"),s.value),right=ct_call1(ns.get("len"),s.indices);
            if(!ct_test(std::move(left),std::move(right),Py_EQ)) {
                nb::object error=ns.get("ValueError"),first=ct_format(ct_call1(ns.get("len"),s.value));
                nb::object message=ct_concat(ct_concat(nb::str("attempt to assign sequence of size "),std::move(first)),nb::str(" to extended slice of size "));
                message=ct_concat(std::move(message),ct_format(ct_call1(ns.get("len"),s.indices)));ct_raise(std::move(error),std::move(message));
            }
            nb::object zip=ns.get("zip");nb::tuple args=nb::make_tuple(s.indices,s.value);nb::dict kwargs;kwargs[ct_name("strict")]=nb::bool_(false);
            nb::object iterator=ct_iter(ct_kw(std::move(zip),std::move(args),std::move(kwargs)));
            while(nb::object next=ct_next(iterator)) { auto values=ct_pair(std::move(next));ct_replace(s.i,std::move(values.first));ct_replace(s.v,std::move(values.second));ct_call2(ct_attr(self,"__setitem__"),s.i,s.v); }
        } else {
            if(PyObject_DelItem(self.ptr(),key.ptr())<0)throw nb::python_error();
            nb::object start=ct_attr(key,"start");if(!ct_truth(start))start=nb::int_(0);ct_replace(s.start,std::move(start));
            nb::object iterator=ct_iter(ct_call1(ns.get("enumerate"),s.value));
            while(nb::object next=ct_next(iterator)) { auto values=ct_pair(std::move(next));ct_replace(s.i,std::move(values.first));ct_replace(s.v,std::move(values.second));nb::object insert=ct_attr(self,"insert"),index=ct_bin(s.start,s.i,PyNumber_Add);ct_call2(std::move(insert),std::move(index),s.v); }
        }
    } else {
        if(s.value.ptr()==s.old.ptr())return;
        ct_call2(ct_super(std::move(super_provider),self,"__setitem__",bound),key,s.value);
        nb::object changed=ct_attr(ct_attr(self,"events"),"changed");nb::dict kwargs;kwargs[ct_name("index")]=key;kwargs[ct_name("old_value")]=s.old;kwargs[ct_name("value")]=s.value;ct_kw(std::move(changed),nb::tuple(),std::move(kwargs));
    }
}
nb::object evented_delitem_indices_context(nb::object self, nb::object key, nb::dict namespace_, EventedContainerLocals& s) {
    ContainerNamespace ns(std::move(namespace_));nb::object fn=ns.get("isinstance"),type=ns.get("int");nb::list result;
    if(ct_truth(ct_call2(std::move(fn),key,std::move(type)))) {
        nb::object position=ct_test(key,nb::int_(0),Py_GE)?key:ct_bin(key,ct_call1(ns.get("len"),self),PyNumber_Add);result.append(nb::make_tuple(self,std::move(position)));return result;
    }
    fn=ns.get("isinstance");type=ns.get("slice");
    if(ct_truth(ct_call2(std::move(fn),key,std::move(type)))) {
        nb::object iterator=ct_iter(ct_slice_range(self,key,ns));while(nb::object i=ct_next(iterator))result.append(nb::make_tuple(self,std::move(i)));return result;
    }
    nb::object keytype=ct_call1(ns.get("type"),key),lookup=ct_attr(self,"_lookup");
    if(ct_contains(std::move(lookup),std::move(keytype))) { result.append(nb::make_tuple(self,ct_call1(ct_attr(self,"index"),key)));return result; }
    nb::object integer=ns.get("int"),slice=ns.get("slice");
    // CPython3.11-3.14 BUILD_SET captures both operands, then inserts/releases
    // them in source order; later operands are released even after add failure.
    nb::set valid;
    int add_error=PySet_Add(valid.ptr(),integer.ptr());integer.reset();
    if(add_error==0)add_error=PySet_Add(valid.ptr(),slice.ptr());slice.reset();
    if(add_error!=0){valid.reset();throw nb::python_error();}
    nb::object merge=ct_attr(std::move(valid),"union"),set=ns.get("set"),values=ct_attr(self,"_lookup");ct_replace(s.valid,ct_call1(std::move(merge),ct_call1(std::move(set),std::move(values))));
    nb::object error=ns.get("TypeError"),message=ct_concat(ct_concat(nb::str("Deletion index must be "),ct_repr(s.valid)),nb::str(", got "));
    message=ct_concat(std::move(message),ct_format(ct_call1(ns.get("type"),key)));ct_raise(std::move(error),std::move(message));
}
void evented_delitem_context(nb::object self, nb::object key, nb::dict namespace_, EventedContainerLocals& s) {
    ContainerNamespace ns(std::move(namespace_));nb::object sort=ns.get("sorted"),indices=ct_call1(ct_attr(self,"_delitem_indices"),key);nb::dict kwargs;kwargs[ct_name("reverse")]=nb::bool_(true);
    nb::object iterator=ct_iter(ct_kw(std::move(sort),nb::make_tuple(std::move(indices)),std::move(kwargs)));
    while(nb::object next=ct_next(iterator)) {
        auto values=ct_pair(std::move(next));ct_replace(s.parent,std::move(values.first));ct_replace(s.index,std::move(values.second));
        nb::object removing=ct_attr(ct_attr(s.parent,"events"),"removing");nb::dict kw;kw[ct_name("index")]=s.index;ct_kw(std::move(removing),nb::tuple(),std::move(kw));
        nb::object disconnect=ct_attr(self,"_disconnect_child_emitters"),selected=ct_item(s.parent,s.index);ct_call1(std::move(disconnect),std::move(selected));
        ct_replace(s.item,ct_call1(ct_attr(ct_attr(s.parent,"_list"),"pop"),s.index));ct_call1(ct_attr(self,"_process_delete_item"),s.item);
        nb::object removed=ct_attr(ct_attr(s.parent,"events"),"removed");kw=nb::dict();kw[ct_name("index")]=s.index;kw[ct_name("value")]=s.item;ct_kw(std::move(removed),nb::tuple(),std::move(kw));
    }
}
void evented_insert_context(nb::object self, nb::object index, nb::object value, nb::dict, nb::object super_provider, bool bound=false) {
    nb::object inserting=ct_attr(ct_attr(self,"events"),"inserting");nb::dict kw;kw[ct_name("index")]=index;ct_kw(std::move(inserting),nb::tuple(),std::move(kw));
    ct_call2(ct_super(std::move(super_provider),self,"insert",bound),index,value);
    nb::object inserted=ct_attr(ct_attr(self,"events"),"inserted");kw=nb::dict();kw[ct_name("index")]=index;kw[ct_name("value")]=value;ct_kw(std::move(inserted),nb::tuple(),std::move(kw));
    ct_call1(ct_attr(self,"_connect_child_emitters"),std::move(value));
}
void evented_reemit_context(nb::object self, nb::object event, nb::dict namespace_) {
    ContainerNamespace ns(std::move(namespace_));nb::object has=ns.get("hasattr");
    if(!ct_truth(ct_call2(std::move(has),event,ct_name("index")))) {
        nb::object factory=ct_attr(ns.get("contextlib"),"suppress"),type=ns.get("ValueError");nb::object context=ct_call1(std::move(factory),std::move(type));
        ct_with(std::move(context),[&] { nb::object fn=ct_attr(self,"index"),source=ct_attr(event,"source");ct_store(event,"index",ct_call1(std::move(fn),std::move(source))); });
    }
    ct_call1(ct_attr(self,"events"),std::move(event));
}
void evented_disconnect_context(nb::object self, nb::object child, nb::dict namespace_) {
    ContainerNamespace ns(std::move(namespace_));nb::object fn=ns.get("isinstance"),type=ns.get("SupportsEvents");
    if(ct_truth(ct_call2(std::move(fn),child,std::move(type)))) { nb::object disconnect=ct_attr(ct_attr(child,"events"),"disconnect"),reemitter=ct_attr(self,"_reemit_child_event");ct_call1(std::move(disconnect),std::move(reemitter)); }
}
void evented_connect_context(nb::object self, nb::object child, nb::dict namespace_) {
    ContainerNamespace ns(std::move(namespace_));nb::object fn=ns.get("isinstance"),type=ns.get("SupportsEvents");
    if(ct_truth(ct_call2(std::move(fn),child,std::move(type)))) {
        if(ct_attr(ct_attr(child,"events"),"source").is_none())ct_store(ct_attr(child,"events"),"source",child);
        nb::object connect=ct_attr(ct_attr(child,"events"),"connect"),reemitter=ct_attr(self,"_reemit_child_event");ct_call1(std::move(connect),std::move(reemitter));
    }
}
nb::object evented_move_context(nb::object self, nb::object source, nb::dict namespace_, EventedContainerLocals& s) {
    ContainerNamespace ns(std::move(namespace_));
    if(ct_test(s.dest_index,nb::int_(0),Py_LT)) { nb::object previous=s.dest_index,count=ct_call1(ns.get("len"),self);ct_replace(s.dest_index,ct_bin(std::move(previous),ct_bin(std::move(count),nb::int_(1),PyNumber_Add),PyNumber_InPlaceAdd)); }
    nb::object plus=ct_bin(source,nb::int_(1),PyNumber_Add);nb::tuple unchanged=nb::make_tuple(source,std::move(plus));
    if(ct_contains(std::move(unchanged),s.dest_index))return nb::bool_(false);
    nb::object moving=ct_attr(ct_attr(self,"events"),"moving");nb::dict kw;kw[ct_name("index")]=source;kw[ct_name("new_index")]=s.dest_index;ct_kw(std::move(moving),nb::tuple(),std::move(kw));
    ct_replace(s.item,ct_call1(ct_attr(ct_attr(self,"_list"),"pop"),source));
    if(ct_test(s.dest_index,source,Py_GT))ct_replace(s.dest_index,ct_bin(s.dest_index,nb::int_(1),PyNumber_InPlaceSubtract));
    ct_call2(ct_attr(ct_attr(self,"_list"),"insert"),s.dest_index,s.item);
    nb::object moved=ct_attr(ct_attr(self,"events"),"moved");kw=nb::dict();kw[ct_name("index")]=source;kw[ct_name("new_index")]=s.dest_index;kw[ct_name("value")]=s.item;ct_kw(std::move(moved),nb::tuple(),std::move(kw));
    nb::object reordered=ct_attr(ct_attr(self,"events"),"reordered");kw=nb::dict();kw[ct_name("value")]=self;ct_kw(std::move(reordered),nb::tuple(),std::move(kw));return nb::bool_(true);
}
nb::object evented_move_multiple_context(nb::object self, nb::object sources, nb::object destination, nb::dict namespace_, nb::object log_message, EventedContainerLocals& s) {
    ContainerNamespace ns(std::move(namespace_));nb::object debug=ct_attr(ns.get("logger"),"debug");nb::dict extra;extra[ct_name("sources")]=sources;extra[ct_name("dest_index")]=destination;nb::dict kw;kw[ct_name("extra")]=std::move(extra);ct_kw(std::move(debug),nb::make_tuple(std::move(log_message)),std::move(kw));
    nb::object list=ns.get("list"),plan=ct_call2(ct_attr(self,"_move_plan"),sources,destination);ct_replace(s.move_plan,ct_call1(std::move(list),std::move(plan)));
    nb::object context=ct_call0(ct_attr(ct_attr(ct_attr(self,"events"),"reordered"),"blocker"));
    ct_with(std::move(context),[&] { nb::object iterator=ct_iter(s.move_plan);while(nb::object next=ct_next(iterator)){auto values=ct_pair(std::move(next));ct_replace(s.src,std::move(values.first));ct_replace(s.dest,std::move(values.second));ct_call2(ct_attr(self,"move"),s.src,s.dest);} });
    nb::object reordered=ct_attr(ct_attr(self,"events"),"reordered");kw=nb::dict();kw[ct_name("value")]=self;ct_kw(std::move(reordered),nb::tuple(),std::move(kw));return ct_call1(ns.get("len"),s.move_plan);
}
void evented_reverse_context(nb::object self, nb::dict) { ct_call0(ct_attr(ct_attr(self,"_list"),"reverse"));nb::object reordered=ct_attr(ct_attr(self,"events"),"reordered");nb::dict kw;kw[ct_name("value")]=self;ct_kw(std::move(reordered),nb::tuple(),std::move(kw)); }
} // namespace

template <typename State, typename Advance> void ct_bind_iterator(nb::module_& m, const char* name, Advance advance) {
    auto cls=nb::class_<State>(m,name,nb::type_slots(ct_iterator_slots<State>()),nb::is_weak_referenceable())
        .def_prop_ro("frame",[](const State& s){return s.frame();})
        .def("__iter__",[](nb::object self){return self;})
        .def("__next__",[advance](nb::handle self){return ct_resume<State>(self,nb::none(),advance);})
        .def("send",[advance](nb::handle self,nb::object value){return ct_resume<State>(self,std::move(value),advance);},nb::arg("value").none())
        .def("throw",&ct_throw<State>).def("close",&ct_close<State>);
    auto* type=reinterpret_cast<PyTypeObject*>(cls.ptr());ct_base_dealloc<State>()=type->tp_dealloc;type->tp_dealloc=ct_iterator_dealloc<State>;PyType_Modified(type);
}
void bind_collections(nb::module_& m) {
    ct_bind_iterator<MoveIterator>(m,"ListMoveIterator",ct_advance_move);
    ct_bind_iterator<CompletionIterator>(m,"ListCompletionIterator",ct_advance_completion);
    ct_bind_iterator<ExpressionIterator>(m,"ContainerExpressionIterator",ct_advance_expression);
    nb::class_<TypedContainerLocals>(m,"TypedContainerLocals",nb::type_slots(ct_typed_slots),nb::is_weak_referenceable())
        .def_prop_ro("lookup",[](const TypedContainerLocals& s){return ct_optional(s.lookup);})
        .def_prop_ro("result",[](const TypedContainerLocals& s){return ct_optional(s.result);})
        .def_prop_ro("_key",[](const TypedContainerLocals& s){return ct_optional(s._key);})
        .def_prop_ro("new",[](const TypedContainerLocals& s){return ct_optional(s.new_);})
        .def_prop_ro("copy",[](const TypedContainerLocals& s){return ct_optional(s.copy);})
        .def_prop_ro("start",[](const TypedContainerLocals& s){return ct_optional(s.start);})
        .def_prop_ro("stop",[](const TypedContainerLocals& s){return ct_optional(s.stop);})
        .def_prop_ro("convert",[](const TypedContainerLocals& s){return ct_optional(s.convert);})
        .def_prop_ro("i",[](const TypedContainerLocals& s){return ct_optional(s.i);})
        .def_prop_ro("v",[](const TypedContainerLocals& s){return ct_optional(s.v);});
    nb::class_<EventedContainerLocals>(m,"EventedContainerLocals",nb::type_slots(ct_evented_slots),nb::is_weak_referenceable())
        .def_prop_ro("lookup",[](const EventedContainerLocals& s){return ct_optional(s.lookup);})
        .def_prop_ro("_events",[](const EventedContainerLocals& s){return ct_optional(s._events);})
        .def_prop_ro("value",[](const EventedContainerLocals& s){return ct_optional(s.value);})
        .def_prop_ro("old",[](const EventedContainerLocals& s){return ct_optional(s.old);})
        .def_prop_ro("v",[](const EventedContainerLocals& s){return ct_optional(s.v);})
        .def_prop_ro("indices",[](const EventedContainerLocals& s){return ct_optional(s.indices);})
        .def_prop_ro("i",[](const EventedContainerLocals& s){return ct_optional(s.i);})
        .def_prop_ro("start",[](const EventedContainerLocals& s){return ct_optional(s.start);})
        .def_prop_ro("valid",[](const EventedContainerLocals& s){return ct_optional(s.valid);})
        .def_prop_ro("parent",[](const EventedContainerLocals& s){return ct_optional(s.parent);})
        .def_prop_ro("index",[](const EventedContainerLocals& s){return ct_optional(s.index);})
        .def_prop_ro("item",[](const EventedContainerLocals& s){return ct_optional(s.item);})
        .def_prop_ro("dest_index",[](const EventedContainerLocals& s){return ct_optional(s.dest_index);})
        .def_prop_ro("move_plan",[](const EventedContainerLocals& s){return ct_optional(s.move_plan);})
        .def_prop_ro("src",[](const EventedContainerLocals& s){return ct_optional(s.src);})
        .def_prop_ro("dest",[](const EventedContainerLocals& s){return ct_optional(s.dest);});
    m.def("typed_container_locals",[](nb::object operation,nb::object lookup,nb::object start,nb::object stop){
        TypedContainerLocals s;std::string op=nb::cast<std::string>(operation);
        if(op=="init")s.lookup=std::move(lookup);if(op=="index"){s.start=std::move(start);s.stop=std::move(stop);}return s;
    },nb::arg("operation"),nb::arg("lookup").none(),nb::arg("start").none(),nb::arg("stop").none());
    m.def("evented_container_locals",[](nb::object operation,nb::object lookup,nb::object value,nb::object destination){
        EventedContainerLocals s;std::string op=nb::cast<std::string>(operation);
        if(op=="init"){s.layout=EventedLayout::Init;s.lookup=std::move(lookup);}else if(op=="setitem"){s.layout=EventedLayout::Set;s.value=std::move(value);}else if(op=="indices")s.layout=EventedLayout::Indices;else if(op=="delete")s.layout=EventedLayout::Delete;else if(op=="move"){s.layout=EventedLayout::Move;s.dest_index=std::move(destination);}else s.layout=EventedLayout::Multiple;return s;
    },nb::arg("operation"),nb::arg("lookup").none(),nb::arg("value").none(),nb::arg("dest_index").none());
    // Source-context overloads: each facade supplies its actual globals and
    // original cells/locals. There are no Python algorithm continuations.
    m.def("sequence_init",&sequence_init_context,nb::arg("self").none(),nb::arg("data").none(),nb::arg("basetype").none(),nb::arg("namespace"),nb::arg("locals_owner"));
    m.def("sequence_len",&sequence_len_context,nb::arg("self").none(),nb::arg("namespace"));
    m.def("sequence_repr",&sequence_repr_context,nb::arg("self").none(),nb::arg("namespace"));
    m.def("sequence_equal",&sequence_equal_context,nb::arg("self").none(),nb::arg("other").none(),nb::arg("namespace"));
    m.def("sequence_hash",&sequence_hash_context,nb::arg("self").none(),nb::arg("namespace"));
    m.def("sequence_setitem",&sequence_setitem_context,nb::arg("self").none(),nb::arg("key").none(),nb::arg("value").none(),nb::arg("namespace"),nb::arg("error_message").none());
    m.def("sequence_insert",&sequence_insert_context,nb::arg("self").none(),nb::arg("index").none(),nb::arg("value").none(),nb::arg("namespace"));
    m.def("sequence_contains",[](nb::object self,nb::object key,nb::dict ns,nb::object provider){return sequence_contains_context(std::move(self),std::move(key),std::move(ns),std::move(provider));},nb::arg("self").none(),nb::arg("key").none(),nb::arg("namespace"),nb::arg("super_provider").none());
    m.def("sequence_getitem",&sequence_getitem_context,nb::arg("self").none(),nb::arg("key").none(),nb::arg("namespace"),nb::arg("locals_owner"));
    m.def("sequence_delitem",&sequence_delitem_context,nb::arg("self").none(),nb::arg("key").none(),nb::arg("namespace"),nb::arg("locals_owner"));
    m.def("sequence_type_check",[](nb::object self,nb::dict ns,nb::object e_getter,nb::object factory){return sequence_type_check_context(std::move(self),std::move(ns),ct_cell(std::move(e_getter)),std::move(factory));},nb::arg("self").none(),nb::arg("namespace"),nb::arg("e_getter").none(),nb::arg("generator_factory").none());
    m.def("sequence_newlike",&sequence_newlike_context,nb::arg("self").none(),nb::arg("iterable").none(),nb::arg("namespace"),nb::arg("locals_owner"));
    m.def("sequence_copy",&sequence_copy_context,nb::arg("self").none(),nb::arg("namespace"));
    m.def("sequence_add",&sequence_add_context,nb::arg("self").none(),nb::arg("other").none(),nb::arg("namespace"),nb::arg("locals_owner"));
    m.def("sequence_iadd",&sequence_iadd_context,nb::arg("self").none(),nb::arg("other").none(),nb::arg("namespace"));
    m.def("sequence_radd",&sequence_radd_context,nb::arg("self").none(),nb::arg("other").none(),nb::arg("namespace"));
    m.def("sequence_index",&sequence_index_context,nb::arg("self").none(),nb::arg("value").none(),nb::arg("namespace"),nb::arg("locals_owner"));
    m.def("sequence_indices",&sequence_indices_context,nb::arg("self").none(),nb::arg("start").none(),nb::arg("stop").none(),nb::arg("namespace"));
    m.def("sequence_completions",[](nb::dict ns,nb::object self_getter,nb::object factory){return sequence_completions_context(std::move(ns),ct_cell(std::move(self_getter)),std::move(factory));},nb::arg("namespace"),nb::arg("self_getter").none(),nb::arg("generator_factory").none());
    m.def("sequence_completion_iterator",[](nb::object self,nb::object iterator,nb::dict namespace_,nb::object self_getter){ContainerNamespace ns(std::move(namespace_));return ct_make_completion(std::move(self),std::move(iterator),ns,ct_cell(std::move(self_getter)),true);},nb::arg("self").none(),nb::arg("iterator").none(),nb::arg("namespace"),nb::arg("self_getter").none());
    m.def("sequence_noop",[](nb::object value){return value;},nb::arg("value").none());
    m.def("evented_sequence_init",[](nb::object self,nb::object data,nb::object base,nb::dict ns,nb::object provider,nb::tuple names,EventedContainerLocals& s){evented_init_context(std::move(self),std::move(data),std::move(base),std::move(ns),std::move(provider),std::move(names),s);},nb::arg("self").none(),nb::arg("data").none(),nb::arg("basetype").none(),nb::arg("namespace"),nb::arg("super_provider").none(),nb::arg("event_names"),nb::arg("locals_owner"));
    m.def("evented_sequence_setitem",[](nb::object self,nb::object key,nb::dict ns,nb::object provider,nb::object message,EventedContainerLocals& s){evented_setitem_context(std::move(self),std::move(key),std::move(ns),std::move(provider),std::move(message),s);},nb::arg("self").none(),nb::arg("key").none(),nb::arg("namespace"),nb::arg("super_provider").none(),nb::arg("error_message").none(),nb::arg("locals_owner"));
    m.def("evented_sequence_delitem_indices",&evented_delitem_indices_context,nb::arg("self").none(),nb::arg("key").none(),nb::arg("namespace"),nb::arg("locals_owner"));
    m.def("evented_sequence_delitem",&evented_delitem_context,nb::arg("self").none(),nb::arg("key").none(),nb::arg("namespace"),nb::arg("locals_owner"));
    m.def("evented_sequence_process_delete",[](nb::object,nb::object){},nb::arg("self").none(),nb::arg("value").none());
    m.def("evented_sequence_insert",[](nb::object self,nb::object index,nb::object value,nb::dict ns,nb::object provider){evented_insert_context(std::move(self),std::move(index),std::move(value),std::move(ns),std::move(provider));},nb::arg("self").none(),nb::arg("index").none(),nb::arg("value").none(),nb::arg("namespace"),nb::arg("super_provider").none());
    m.def("evented_sequence_reemit",&evented_reemit_context,nb::arg("self").none(),nb::arg("event").none(),nb::arg("namespace"));
    m.def("evented_sequence_disconnect",&evented_disconnect_context,nb::arg("self").none(),nb::arg("child").none(),nb::arg("namespace"));
    m.def("evented_sequence_connect",&evented_connect_context,nb::arg("self").none(),nb::arg("child").none(),nb::arg("namespace"));
    m.def("evented_sequence_move",&evented_move_context,nb::arg("self").none(),nb::arg("src_index").none(),nb::arg("namespace"),nb::arg("locals_owner"));
    m.def("evented_sequence_move_multiple",&evented_move_multiple_context,nb::arg("self").none(),nb::arg("sources").none(),nb::arg("dest_index").none(),nb::arg("namespace"),nb::arg("log_message").none(),nb::arg("locals_owner"));
    m.def("evented_sequence_move_iterator",[](nb::object self,nb::object sources,nb::object destination,nb::dict namespace_,nb::object src_getter,nb::object factory,nb::object message){ContainerNamespace ns(std::move(namespace_));return ct_make_move(std::move(self),std::move(sources),std::move(destination),ns,ct_cell(std::move(src_getter)),std::move(factory),std::move(message),true);},nb::arg("self").none(),nb::arg("sources").none(),nb::arg("dest_index").none(),nb::arg("namespace"),nb::arg("src_getter").none(),nb::arg("generator_factory").none(),nb::arg("destination_message").none());
    m.def("evented_sequence_reverse",&evented_reverse_context,nb::arg("self").none(),nb::arg("namespace"));
    // Preserve all original private arities. These share the complete generic
    // engines; only their original canonical-module/cell/frame boundary is
    // bounded legacy interop, never a second application algorithm.
    m.def("sequence_init",[](nb::object self,nb::object data,nb::object base,nb::object lookup){TypedContainerLocals s;s.lookup=std::move(lookup);sequence_init_context(std::move(self),std::move(data),std::move(base),ct_legacy_namespace(false),s);},nb::arg("self").none(),nb::arg("data").none(),nb::arg("basetype").none(),nb::arg("lookup").none());
    m.def("sequence_len",[](nb::object self){return sequence_len_context(std::move(self),ct_legacy_namespace(false));},nb::arg("self").none());
    m.def("sequence_repr",[](nb::object self){return sequence_repr_context(std::move(self),ct_legacy_namespace(false));},nb::arg("self").none());
    m.def("sequence_equal",[](nb::object self,nb::object other){return sequence_equal_context(std::move(self),std::move(other),ct_legacy_namespace(false));},nb::arg("self").none(),nb::arg("other").none());
    m.def("sequence_hash",[](nb::object self){return sequence_hash_context(std::move(self),ct_legacy_namespace(false));},nb::arg("self").none());
    m.def("sequence_setitem",[](nb::object self,nb::object key,nb::object value){sequence_setitem_context(std::move(self),std::move(key),std::move(value),ct_legacy_namespace(false),nb::str("Can only assign an iterable to slice"));},nb::arg("self").none(),nb::arg("key").none(),nb::arg("value").none());
    m.def("sequence_insert",[](nb::object self,nb::object index,nb::object value){sequence_insert_context(std::move(self),std::move(index),std::move(value),ct_legacy_namespace(false));},nb::arg("self").none(),nb::arg("index").none(),nb::arg("value").none());
    m.def("sequence_contains",[](nb::object self,nb::object key,nb::object super){return sequence_contains_context(std::move(self),std::move(key),ct_legacy_namespace(false),std::move(super),true);},nb::arg("self").none(),nb::arg("key").none(),nb::arg("super"));
    m.def("sequence_getitem",[](nb::object self,nb::object key){TypedContainerLocals s;return sequence_getitem_context(std::move(self),std::move(key),ct_legacy_namespace(false),s);},nb::arg("self").none(),nb::arg("key").none());
    m.def("sequence_delitem",[](nb::object self,nb::object key){TypedContainerLocals s;sequence_delitem_context(std::move(self),std::move(key),ct_legacy_namespace(false),s);},nb::arg("self").none(),nb::arg("key").none());
    m.def("sequence_type_check",[](nb::object self,nb::object value){nb::object cell=ct_check(PyCell_New(value.ptr()));return sequence_type_check_context(std::move(self),ct_legacy_namespace(false),std::move(cell),nb::object());},nb::arg("self").none(),nb::arg("value").none());
    m.def("sequence_newlike",[](nb::object self,nb::object values){TypedContainerLocals s;return sequence_newlike_context(std::move(self),std::move(values),ct_legacy_namespace(false),s);},nb::arg("self").none(),nb::arg("values").none());
    m.def("sequence_copy",[](nb::object self){return sequence_copy_context(std::move(self),ct_legacy_namespace(false));},nb::arg("self").none());
    m.def("sequence_add",[](nb::object self,nb::object other){TypedContainerLocals s;return sequence_add_context(std::move(self),std::move(other),ct_legacy_namespace(false),s);},nb::arg("self").none(),nb::arg("other").none());
    m.def("sequence_iadd",[](nb::object self,nb::object other){return sequence_iadd_context(std::move(self),std::move(other),ct_legacy_namespace(false));},nb::arg("self").none(),nb::arg("other").none());
    m.def("sequence_radd",[](nb::object self,nb::object other){return sequence_radd_context(std::move(self),std::move(other),ct_legacy_namespace(false));},nb::arg("self").none(),nb::arg("other").none());
    m.def("sequence_index",[](nb::object self,nb::object value,nb::object start,nb::object stop){TypedContainerLocals s;s.start=std::move(start);s.stop=std::move(stop);return sequence_index_context(std::move(self),std::move(value),ct_legacy_namespace(false),s);},nb::arg("self").none(),nb::arg("value").none(),nb::arg("start").none(),nb::arg("stop").none());
    m.def("sequence_indices",[](nb::object self,nb::object start,nb::object stop){return sequence_indices_context(std::move(self),std::move(start),std::move(stop),ct_legacy_namespace(false));},nb::arg("self").none(),nb::arg("start").none(),nb::arg("stop").none());
    m.def("sequence_completions",[](nb::object self){return sequence_completions_context(ct_legacy_namespace(false),ct_check(PyCell_New(self.ptr())),nb::object());},nb::arg("self").none());
    m.def("sequence_completion_iterator",[](nb::object self,nb::object iterator){ContainerNamespace ns(ct_legacy_namespace(false));return ct_make_completion(std::move(self),std::move(iterator),ns,nb::object(),false);},nb::arg("self").none(),nb::arg("iterator").none());
    m.def("evented_sequence_init",[](nb::object self,nb::object data,nb::object base,nb::object lookup,nb::object super){EventedContainerLocals s;s.lookup=std::move(lookup);nb::tuple names=nb::make_tuple("inserting","inserted","removing","removed","moving","moved","changed","reordered");evented_init_context(std::move(self),std::move(data),std::move(base),ct_legacy_namespace(true),std::move(super),std::move(names),s,true);},nb::arg("self").none(),nb::arg("data").none(),nb::arg("basetype").none(),nb::arg("lookup").none(),nb::arg("super"));
    m.def("evented_sequence_setitem",[](nb::object self,nb::object key,nb::object value,nb::object super){EventedContainerLocals s;s.layout=EventedLayout::Set;s.value=std::move(value);evented_setitem_context(std::move(self),std::move(key),ct_legacy_namespace(true),std::move(super),nb::str("Can only assign an iterable to slice"),s,true);},nb::arg("self").none(),nb::arg("key").none(),nb::arg("value").none(),nb::arg("super"));
    m.def("evented_sequence_delitem_indices",[](nb::object self,nb::object key){EventedContainerLocals s;s.layout=EventedLayout::Indices;return evented_delitem_indices_context(std::move(self),std::move(key),ct_legacy_namespace(true),s);},nb::arg("self").none(),nb::arg("key").none());
    m.def("evented_sequence_delitem",[](nb::object self,nb::object key){EventedContainerLocals s;s.layout=EventedLayout::Delete;evented_delitem_context(std::move(self),std::move(key),ct_legacy_namespace(true),s);},nb::arg("self").none(),nb::arg("key").none());
    m.def("evented_sequence_insert",[](nb::object self,nb::object index,nb::object value,nb::object super){evented_insert_context(std::move(self),std::move(index),std::move(value),ct_legacy_namespace(true),std::move(super),true);},nb::arg("self").none(),nb::arg("index").none(),nb::arg("value").none(),nb::arg("super"));
    m.def("evented_sequence_reemit",[](nb::object self,nb::object event){evented_reemit_context(std::move(self),std::move(event),ct_legacy_namespace(true));},nb::arg("self").none(),nb::arg("event").none());
    m.def("evented_sequence_disconnect",[](nb::object self,nb::object child){evented_disconnect_context(std::move(self),std::move(child),ct_legacy_namespace(true));},nb::arg("self").none(),nb::arg("child").none());
    m.def("evented_sequence_connect",[](nb::object self,nb::object child){evented_connect_context(std::move(self),std::move(child),ct_legacy_namespace(true));},nb::arg("self").none(),nb::arg("child").none());
    m.def("evented_sequence_move",[](nb::object self,nb::object source,nb::object destination){EventedContainerLocals s;s.layout=EventedLayout::Move;s.dest_index=std::move(destination);return evented_move_context(std::move(self),std::move(source),ct_legacy_namespace(true),s);},nb::arg("self").none(),nb::arg("source").none(),nb::arg("destination").none());
    m.def("evented_sequence_move_multiple",[](nb::object self,nb::object sources,nb::object destination){EventedContainerLocals s;s.layout=EventedLayout::Multiple;return evented_move_multiple_context(std::move(self),std::move(sources),std::move(destination),ct_legacy_namespace(true),nb::str("move_multiple(sources={sources}, dest_index={dest_index})"),s);},nb::arg("self").none(),nb::arg("sources").none(),nb::arg("destination").none());
    m.def("evented_sequence_move_iterator",[](nb::object self,nb::object sources,nb::object destination){ContainerNamespace ns(ct_legacy_namespace(true));return ct_make_move(std::move(self),std::move(sources),std::move(destination),ns,ct_check(PyCell_New(nullptr)),nb::object(),nb::str("Destination index may not be a slice"),false);},nb::arg("self").none(),nb::arg("sources").none(),nb::arg("destination").none());
    m.def("evented_sequence_reverse",[](nb::object self){evented_reverse_context(std::move(self),ct_legacy_namespace(true));},nb::arg("self").none());
}
} // namespace nx
