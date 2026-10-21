#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Python.h>
#ifndef Py_BUILD_CORE
#define Py_BUILD_CORE
#define LF_RESTORE_CORE
#endif
#include <internal/pycore_frame.h>
#if PY_VERSION_HEX >= 0x030C0000
#include <internal/pycore_interp.h>
#endif
#if PY_VERSION_HEX >= 0x030D0000
#include <internal/pycore_pyatomic_ft_wrappers.h>
#endif
#ifdef LF_RESTORE_CORE
#undef LF_RESTORE_CORE
#undef Py_BUILD_CORE
#endif
#include <Python.h>
#include <osdefs.h>
#ifdef MS_WINDOWS
#include <direct.h>
#else
#include <unistd.h>
#endif
#include "arrays.hpp"
#include <array>
#include <functional>
#include <initializer_list>
#include <string>
#include <utility>
#include <frameobject.h>

namespace nx {
namespace {
// Handwritten Labels history policy. Persistent values remain solely in the
// actual LayerNamespace dictionary; this carrier holds source locals only.
using LHExpr = std::function<nb::object()>;
#define LX(value) [&]() -> nb::object { return (value); }

nb::object lh_check(PyObject* value) {
    if (!value) throw nb::python_error();
    return nb::steal<nb::object>(value);
}
void lh_assign(nb::object& target, nb::object value) {
    nb::object previous = std::move(target);
    target = std::move(value);
    previous.reset();
}
nb::object lh_call(nb::object callable, std::initializer_list<LHExpr> positional = {},
                   std::initializer_list<LHExpr> keywords = {}, nb::object names = {}) {
    std::vector<nb::object> values;
    values.reserve(positional.size() + keywords.size());
    for (const auto& expression : positional) values.push_back(expression());
    for (const auto& expression : keywords) values.push_back(expression());
    if (keywords.size() && (!names.is_valid() || !PyTuple_CheckExact(names.ptr()) ||
        PyTuple_GET_SIZE(names.ptr()) != static_cast<Py_ssize_t>(keywords.size())))
        throw nb::type_error("Labels history source keyword metadata is missing");
    std::vector<PyObject*> operands;
    for (const auto& value : values) operands.push_back(value.ptr());
    PyObject* result = PyObject_Vectorcall(callable.ptr(), operands.data(),
                                          positional.size(), names.ptr());
    callable.reset();
    for (auto& value : values) value.reset();
    names.reset();
    return lh_check(result);
}
bool lh_truth(nb::object value) {
    int result = PyObject_IsTrue(value.ptr());
    value.reset();
    if (result < 0) throw nb::python_error();
    return result != 0;
}
nb::object lh_item(nb::object receiver, nb::object index) {
    PyObject* result = PyObject_GetItem(receiver.ptr(), index.ptr());
    receiver.reset(); index.reset();
    return lh_check(result);
}
nb::object lh_index(LHExpr receiver, LHExpr index) {
    nb::object target = receiver(), key = index();
    return lh_item(std::move(target), std::move(key));
}
void lh_set_item(LHExpr value, LHExpr receiver, LHExpr index) {
    nb::object result = value(), target = receiver(), key = index();
    int status = PyObject_SetItem(target.ptr(), key.ptr(), result.ptr());
    result.reset(); target.reset(); key.reset();
    if (status < 0) throw nb::python_error();
}
nb::object lh_iter(nb::object receiver) {
    PyObject* result = PyObject_GetIter(receiver.ptr());
    receiver.reset();
    return lh_check(result);
}
nb::object lh_next(nb::handle iterator) {
    PyObject* result = PyIter_Next(iterator.ptr());
    if (!result && PyErr_Occurred()) throw nb::python_error();
    return nb::steal<nb::object>(result);
}
std::array<nb::object, 3> lh_unpack_three(nb::object value) {
    PyObject* raw = PyObject_GetIter(value.ptr());
    if (!raw) {
        nb::python_error error;
        if (error.matches(PyExc_TypeError) && Py_TYPE(value.ptr())->tp_iter == nullptr &&
            !PySequence_Check(value.ptr())) {
            PyErr_Format(PyExc_TypeError, "cannot unpack non-iterable %.200s object",
                         Py_TYPE(value.ptr())->tp_name);
            throw nb::python_error();
        }
        throw error;
    }
    nb::object iterator = nb::steal<nb::object>(raw);
    std::array<nb::object, 3> values;
    for (size_t i = 0; i < values.size(); ++i) {
        values[i] = lh_next(iterator);
        if (!values[i].is_valid()) {
            PyErr_Format(PyExc_ValueError, "not enough values to unpack (expected 3, got %zu)", i);
            throw nb::python_error();
        }
    }
    nb::object extra = lh_next(iterator);
    if (extra.is_valid()) {
        PyErr_SetString(PyExc_ValueError, "too many values to unpack (expected 3)");
        throw nb::python_error();
    }
    iterator.reset(); value.reset();
    return values;
}

#define LH_FIELDS(X) \
    X(source_globals) X(source_builtins) X(source_metadata) X(source_code) X(binding) \
    X(self) X(event) X(item) X(value) X(before) X(after) X(undoing) X(atom) \
    X(volume_slice) X(region_data) X(mask) X(new_label) X(cls) \
    X(prev) X(history_item) X(indices) X(prev_values) X(next_values) X(ignored) \
    X(region) X(values)
struct LabelsHistoryLocals {
    int entry = -1;
#define LH_DECLARE(name) nb::object name;
    LH_FIELDS(LH_DECLARE)
#undef LH_DECLARE
    LabelsHistoryLocals() = default;
    LabelsHistoryLocals(LabelsHistoryLocals&&) = default;
    void clear() {
#define LH_CLEAR(name) name.reset();
        LH_FIELDS(LH_CLEAR)
#undef LH_CLEAR
    }
    ~LabelsHistoryLocals() { clear(); }
};
struct LabelsHistoryBinding {
    nb::object frame, shells, profiles;
    nb::object defining_globals, atom_class, atom_function, atom_globals, atom_builtins;
    LabelsHistoryBinding() = default;
    LabelsHistoryBinding(LabelsHistoryBinding&&) = default;
    void clear() {
        frame.reset(); shells.reset(); profiles.reset();
        defining_globals.reset(); atom_class.reset(); atom_function.reset();
        atom_globals.reset(); atom_builtins.reset();
    }
    ~LabelsHistoryBinding() { clear(); }
};
struct LabelsHistoryGenerator {
    nb::object context;
    int state = 0;
    bool running = false;
    LabelsHistoryGenerator() = default;
    LabelsHistoryGenerator(LabelsHistoryGenerator&&) = default;
    void clear() { context.reset(); state = 2; }
    ~LabelsHistoryGenerator() { clear(); }
};

int lh_locals_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) {
        const auto& o = *nb::inst_ptr<LabelsHistoryLocals>(self);
#define LH_VISIT(name) Py_VISIT(o.name.ptr());
        LH_FIELDS(LH_VISIT)
#undef LH_VISIT
    }
    return 0;
}
int lh_locals_clear(PyObject* self) {
    if (nb::inst_ready(self)) nb::inst_ptr<LabelsHistoryLocals>(self)->clear();
    return 0;
}
int lh_binding_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) {
        const auto& o = *nb::inst_ptr<LabelsHistoryBinding>(self);
        Py_VISIT(o.frame.ptr()); Py_VISIT(o.shells.ptr()); Py_VISIT(o.profiles.ptr());
        Py_VISIT(o.defining_globals.ptr()); Py_VISIT(o.atom_class.ptr());
        Py_VISIT(o.atom_function.ptr()); Py_VISIT(o.atom_globals.ptr()); Py_VISIT(o.atom_builtins.ptr());
    }
    return 0;
}
int lh_binding_clear(PyObject* self) {
    if (nb::inst_ready(self)) nb::inst_ptr<LabelsHistoryBinding>(self)->clear();
    return 0;
}
int lh_generator_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) Py_VISIT(nb::inst_ptr<LabelsHistoryGenerator>(self)->context.ptr());
    return 0;
}
int lh_generator_clear(PyObject* self) {
    if (nb::inst_ready(self)) nb::inst_ptr<LabelsHistoryGenerator>(self)->clear();
    return 0;
}
PyType_Slot lh_locals_slots[] = {{Py_tp_traverse, reinterpret_cast<void*>(lh_locals_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(lh_locals_clear)}, {0, nullptr}};
PyType_Slot lh_binding_slots[] = {{Py_tp_traverse, reinterpret_cast<void*>(lh_binding_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(lh_binding_clear)}, {0, nullptr}};
PyType_Slot lh_generator_slots[] = {{Py_tp_traverse, reinterpret_cast<void*>(lh_generator_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(lh_generator_clear)}, {0, nullptr}};

PyObject* lh_metadata_values(nb::handle metadata, int index) {
    if (!PyTuple_CheckExact(metadata.ptr()) || PyTuple_GET_SIZE(metadata.ptr()) != 2)
        throw nb::type_error("Labels history source declaration metadata changed");
    PyObject* values = PyTuple_GET_ITEM(metadata.ptr(), index);
    if (!PyTuple_CheckExact(values))
        throw nb::type_error("Labels history source declaration tuple is required");
    return values;
}
nb::object lh_name(LabelsHistoryLocals& o, const char* text) {
    PyObject* names = lh_metadata_values(o.source_metadata, 1);
    for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(names); ++i) {
        PyObject* value = PyTuple_GET_ITEM(names, i);
        if (PyUnicode_CheckExact(value) && PyUnicode_CompareWithASCIIString(value, text) == 0)
            return nb::borrow<nb::object>(value);
    }
    if (PyErr_Occurred()) throw nb::python_error();
    throw nb::type_error("Labels history original name metadata is missing");
}
nb::object lh_kw(LabelsHistoryLocals& o, const char* text) {
    PyObject* values = lh_metadata_values(o.source_metadata, 0);
    for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(values); ++i) {
        PyObject* value = PyTuple_GET_ITEM(values, i);
        if (PyTuple_CheckExact(value) && PyTuple_GET_SIZE(value) == 1) {
            PyObject* item = PyTuple_GET_ITEM(value, 0);
            if (PyUnicode_CheckExact(item) && PyUnicode_CompareWithASCIIString(item, text) == 0)
                return nb::borrow<nb::object>(value);
        }
    }
    if (PyErr_Occurred()) throw nb::python_error();
    throw nb::type_error("Labels history original keyword tuple is missing");
}
nb::object lh_global(LabelsHistoryLocals& o, const char* text) {
    nb::object name = lh_name(o, text);
    PyObject* value = PyDict_GetItemWithError(o.source_globals.ptr(), name.ptr());
    if (value) return nb::borrow<nb::object>(value);
    if (PyErr_Occurred()) throw nb::python_error();
    value = PyObject_GetItem(o.source_builtins.ptr(), name.ptr());
    if (value) return nb::steal<nb::object>(value);
    if (!PyErr_ExceptionMatches(PyExc_KeyError)) throw nb::python_error();
    PyErr_Clear();
    nb::object message = lh_check(PyUnicode_FromFormat("name '%s' is not defined", text));
    nb::object error = lh_call(nb::borrow<nb::object>(PyExc_NameError), {LX(message)});
    if (PyObject_SetAttrString(error.ptr(), "name", name.ptr()) < 0) throw nb::python_error();
    PyErr_SetObject(PyExc_NameError, error.ptr());
    throw nb::python_error();
}
nb::object lh_attr(LabelsHistoryLocals& o, nb::object receiver, const char* text) {
    nb::object name = lh_name(o, text);
    PyObject* result = PyObject_GetAttr(receiver.ptr(), name.ptr());
    receiver.reset(); name.reset();
    return lh_check(result);
}
void lh_publish(LabelsHistoryLocals& o, LHExpr value, const char* text) {
    nb::object result = value(), target = o.self, name = lh_name(o, text);
    int status = PyObject_SetAttr(target.ptr(), name.ptr(), result.ptr());
    result.reset(); target.reset(); name.reset();
    if (status < 0) throw nb::python_error();
}

nb::object lh_run(LabelsHistoryLocals& o) {
    auto G = [&](const char* key) { return lh_global(o, key); };
    auto A = [&](nb::object receiver, const char* key) { return lh_attr(o, std::move(receiver), key); };
    auto S = [&](const char* key) { return A(o.self, key); };
    switch (o.entry) {
    case 0:
        lh_publish(o, LX(lh_call(G("deque"), {}, {LX(S("_history_limit"))}, lh_kw(o, "maxlen"))), "_undo_history");
        lh_publish(o, LX(lh_call(G("deque"), {}, {LX(S("_history_limit"))}, lh_kw(o, "maxlen"))), "_redo_history");
        lh_publish(o, LX(lh_check(PyList_New(0))), "_staged_history");
        lh_publish(o, LX(nb::bool_(false)), "_block_history");
        return nb::none();
    case 2:
        if (lh_truth(S("_staged_history"))) {
            lh_call(S("_append_to_undo_history"), {LX(S("_staged_history"))});
            lh_publish(o, LX(lh_check(PyList_New(0))), "_staged_history");
        }
        return nb::none();
    case 3:
        lh_publish(o, LX(nb::bool_(true)), "_block_history");
        return nb::none();
    case 4:
        lh_publish(o, LX(nb::bool_(false)), "_block_history");
        lh_call(S("_commit_staged_history"));
        return nb::none();
    case 5: {
        nb::object iterator = lh_iter(lh_call(G("reversed"), {LX(S("_staged_history"))}));
        for (;;) {
            nb::object next = lh_next(iterator);
            if (!next.is_valid()) break;
            lh_assign(o.atom, std::move(next));
            if (lh_truth(lh_call(G("isinstance"), {LX(o.atom), LX(G("_MaskedPaintAtom"))}))) {
                lh_call(S("_replay_masked_atom"), {LX(o.atom)}, {LX(nb::bool_(true))}, lh_kw(o, "undoing"));
                continue;
            }
            auto parts = lh_unpack_three(o.atom);
            lh_assign(o.indices, std::move(parts[0]));
            lh_assign(o.prev_values, std::move(parts[1]));
            lh_assign(o.ignored, std::move(parts[2]));
            lh_set_item(LX(o.prev_values), LX(S("data")), LX(o.indices));
        }
        iterator.reset();
        lh_publish(o, LX(lh_check(PyList_New(0))), "_staged_history");
        lh_publish(o, LX(nb::bool_(false)), "_block_history");
        lh_call(S("refresh"));
        return nb::none();
    }
    case 6:
        lh_call(A(S("_undo_history"), "append"), {LX(o.item)});
        lh_call(A(S("events"), "paint"), {}, {LX(o.item)}, lh_kw(o, "value"));
        return nb::none();
    case 7:
        lh_call(A(S("_redo_history"), "clear"));
        if (lh_truth(S("_block_history")))
            lh_call(A(S("_staged_history"), "append"), {LX(o.value)});
        else
            lh_call(S("_append_to_undo_history"), {LX(([&]() {
                nb::object values = lh_check(PyList_New(1));
                PyList_SET_ITEM(values.ptr(), 0, o.value.inc_ref().ptr());
                return values;
            })())});
        return nb::none();
    case 8: {
        nb::object length = lh_call(G("len"), {LX(o.before)});
        PyObject* raw_test = PyObject_RichCompare(length.ptr(), nb::int_(0).ptr(), Py_EQ);
        length.reset();
        if (lh_truth(lh_check(raw_test))) return nb::none();
        lh_assign(o.history_item, lh_call(A(o.before, "pop")));
        lh_call(A(o.after, "append"), {LX(lh_call(G("list"), {LX(lh_call(G("reversed"), {LX(o.history_item)}))}))});
        nb::object iterator = lh_iter(lh_call(G("reversed"), {LX(o.history_item)}));
        for (;;) {
            nb::object next = lh_next(iterator);
            if (!next.is_valid()) break;
            lh_assign(o.atom, std::move(next));
            if (lh_truth(lh_call(G("isinstance"), {LX(o.atom), LX(G("_MaskedPaintAtom"))}))) {
                lh_call(S("_replay_masked_atom"), {LX(o.atom), LX(o.undoing)});
                continue;
            }
            auto parts = lh_unpack_three(o.atom);
            lh_assign(o.indices, std::move(parts[0]));
            lh_assign(o.prev_values, std::move(parts[1]));
            lh_assign(o.next_values, std::move(parts[2]));
            lh_set_item(LX(lh_truth(o.undoing) ? o.prev_values : o.next_values), LX(S("data")), LX(o.indices));
        }
        iterator.reset();
        lh_call(S("refresh"));
        return nb::none();
    }
    case 9:
        lh_assign(o.values, lh_truth(o.undoing) ? A(o.atom, "old_values") : A(o.atom, "new_value"));
        if (A(o.atom, "mask").is_none()) {
            lh_set_item(LX(o.values), LX(S("data")), LX(A(o.atom, "slice_key")));
            return nb::none();
        }
        lh_assign(o.region, lh_call(A(G("np"), "asarray"), {LX(lh_index(LX(S("data")), LX(A(o.atom, "slice_key"))))}));
        lh_set_item(LX(o.values), LX(o.region), LX(A(o.atom, "mask")));
        lh_set_item(LX(o.region), LX(S("data")), LX(A(o.atom, "slice_key")));
        return nb::none();
    case 10:
        lh_call(S("_load_history"), {LX(S("_undo_history")), LX(S("_redo_history"))},
                {LX(nb::bool_(true))}, lh_kw(o, "undoing"));
        return nb::none();
    case 11:
        lh_call(S("_load_history"), {LX(S("_redo_history")), LX(S("_undo_history"))},
                {LX(nb::bool_(false))}, lh_kw(o, "undoing"));
        return nb::none();
    case 12:
        if (lh_truth(lh_call(A(o.mask, "all"))))
            return lh_call(G("_MaskedPaintAtom"), {LX(o.volume_slice), LX(nb::none()),
                           LX(lh_call(A(o.region_data, "copy"))), LX(o.new_label)});
        return lh_call(G("_MaskedPaintAtom"), {LX(o.volume_slice), LX(o.mask),
                       LX(lh_item(o.region_data, o.mask)), LX(o.new_label)});
    case 13:
        return lh_call(G("_tuple_new"), {LX(o.cls), LX(([&]() {
            nb::object values = lh_check(PyTuple_New(4));
            const nb::object* entries[] = {&o.volume_slice, &o.mask, &o.values, &o.new_label};
            for (Py_ssize_t i = 0; i < 4; ++i) PyTuple_SET_ITEM(values.ptr(), i, entries[i]->inc_ref().ptr());
            return values;
        })())});
    default:
        throw nb::value_error("unknown Labels history source entry");
    }
}

struct LHHandledException {
    nb::object previous;
    explicit LHHandledException(nb::handle exception)
        : previous(nb::steal<nb::object>(PyErr_GetHandledException())) {
        PyErr_SetHandledException(exception.ptr());
    }
    ~LHHandledException() { PyErr_SetHandledException(previous.ptr()); }
};
struct LHRunning {
    LabelsHistoryGenerator& generator;
    explicit LHRunning(LabelsHistoryGenerator& value) : generator(value) {
        if (generator.running) throw nb::value_error("generator already executing");
        generator.running = true;
    }
    ~LHRunning() { generator.running = false; }
};
void lh_finally(LabelsHistoryGenerator& generator) {
    auto& o = *nb::inst_ptr<LabelsHistoryLocals>(generator.context.ptr());
    lh_publish(o, LX(o.prev), "_block_history");
}
[[noreturn]] void lh_generator_failure(nb::python_error& error) {
    if (error.matches(PyExc_StopIteration)) {
        LHHandledException handled(error.value());
        nb::object failure = lh_call(nb::borrow<nb::object>(PyExc_RuntimeError),
                                    {LX(nb::str("generator raised StopIteration"))});
        PyException_SetCause(failure.ptr(), error.value().inc_ref().ptr());
        PyException_SetContext(failure.ptr(), error.value().inc_ref().ptr());
        PyErr_SetObject(PyExc_RuntimeError, failure.ptr());
        throw nb::python_error();
    }
    throw error;
}
nb::object lh_generator_send(LabelsHistoryGenerator& generator, nb::object value) {
    LHRunning running(generator);
    if (generator.state == 2) throw nb::stop_iteration();
    if (generator.state == 0 && !value.is_none())
        throw nb::type_error("can't send non-None value to a just-started generator");
    auto& o = *nb::inst_ptr<LabelsHistoryLocals>(generator.context.ptr());
    if (generator.state == 0) {
        try {
            lh_assign(o.prev, lh_attr(o, o.self, "_block_history"));
            lh_publish(o, LX(nb::bool_(true)), "_block_history");
            generator.state = 1;
            return nb::none();
        } catch (nb::python_error& error) {
            generator.clear();
            lh_generator_failure(error);
        }
    }
    generator.state = 2;
    try {
        try {
            lh_call(lh_attr(o, o.self, "_commit_staged_history"));
        } catch (nb::python_error& error) {
            LHHandledException handled(error.value());
            lh_finally(generator);
            throw;
        }
        lh_finally(generator);
    } catch (nb::python_error& error) {
        generator.clear();
        lh_generator_failure(error);
    }
    generator.clear();
    throw nb::stop_iteration();
}
nb::object lh_throw_value(nb::args arguments) {
    if (arguments.size() < 1 || arguments.size() > 3)
        throw nb::type_error("throw expected 1 to 3 arguments");
    nb::object type = nb::borrow<nb::object>(arguments[0]);
    nb::object value = arguments.size() > 1 ? nb::borrow<nb::object>(arguments[1]) : nb::none();
    nb::object traceback = arguments.size() > 2 ? nb::borrow<nb::object>(arguments[2]) : nb::none();
    if (!traceback.is_none() && !PyTraceBack_Check(traceback.ptr()))
        throw nb::type_error("throw() third argument must be a traceback object");
    if (PyExceptionInstance_Check(type.ptr())) {
        if (!value.is_none()) throw nb::type_error("instance exception may not have a separate value");
        value = type;
    } else if (PyExceptionClass_Check(type.ptr())) {
        PyErr_SetObject(type.ptr(), value.ptr());
        PyObject *raw_type = nullptr, *raw_value = nullptr, *raw_tb = nullptr;
        PyErr_Fetch(&raw_type, &raw_value, &raw_tb);
        PyErr_NormalizeException(&raw_type, &raw_value, &raw_tb);
        nb::object normalized_type = nb::steal<nb::object>(raw_type);
        value = nb::steal<nb::object>(raw_value);
        nb::object normalized_tb = nb::steal<nb::object>(raw_tb);
        if (PyErr_Occurred()) throw nb::python_error();
    } else {
        PyErr_Format(PyExc_TypeError, "exceptions must be classes or instances deriving from BaseException, not %.200s",
                     Py_TYPE(type.ptr())->tp_name);
        throw nb::python_error();
    }
    if (!traceback.is_none() && PyException_SetTraceback(value.ptr(), traceback.ptr()) < 0)
        throw nb::python_error();
    return value;
}
nb::object lh_generator_throw(LabelsHistoryGenerator& generator, nb::args arguments) {
    LHRunning running(generator);
    nb::object exception = lh_throw_value(arguments);
    bool restore = generator.state == 1;
    generator.state = 2;
    PyErr_SetObject(reinterpret_cast<PyObject*>(Py_TYPE(exception.ptr())), exception.ptr());
    try {
        throw nb::python_error();
    } catch (nb::python_error& error) {
        try {
            LHHandledException handled(error.value());
            if (restore) lh_finally(generator);
        } catch (nb::python_error& final_error) {
            generator.clear();
            lh_generator_failure(final_error);
        }
        generator.clear();
        lh_generator_failure(error);
    }
}
nb::object lh_generator_close(LabelsHistoryGenerator& generator) {
    LHRunning running(generator);
    bool restore = generator.state == 1;
    generator.state = 2;
    if (restore) {
        nb::object exit = lh_call(nb::borrow<nb::object>(PyExc_GeneratorExit));
        try {
            LHHandledException handled(exit);
            lh_finally(generator);
        } catch (nb::python_error& error) {
            generator.clear();
            lh_generator_failure(error);
        }
    }
    generator.clear();
    return nb::none();
}

// Admission and source-frame metadata only. No original application CodeType
// is retained after in-place handoff; all default history control is above.
bool lh_profile_code(nb::handle code, nb::handle expected);
bool lh_profile_atom(nb::handle value, nb::handle expected) {
    if (!PyTuple_CheckExact(expected.ptr()) || PyTuple_GET_SIZE(expected.ptr()) < 1)
        throw nb::type_error("Labels history compiler metadata changed");
    const char* tag = PyUnicode_AsUTF8(PyTuple_GET_ITEM(expected.ptr(), 0));
    if (!tag) throw nb::python_error();
    std::string kind(tag);
    if (kind == "NONE") return value.ptr() == Py_None;
    if (kind == "CODE") return lh_profile_code(value, nb::handle(PyTuple_GET_ITEM(expected.ptr(), 1)));
    if (kind == "TUPLE") {
        if (!PyTuple_CheckExact(value.ptr())) return false;
        PyObject* items = PyTuple_GET_ITEM(expected.ptr(), 1);
        if (!PyTuple_CheckExact(items) || PyTuple_GET_SIZE(value.ptr()) != PyTuple_GET_SIZE(items)) return false;
        for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(items); ++i)
            if (!lh_profile_atom(nb::handle(PyTuple_GET_ITEM(value.ptr(), i)),
                                 nb::handle(PyTuple_GET_ITEM(items, i)))) return false;
        return true;
    }
    if (kind == "BOOL" && !PyBool_Check(value.ptr())) return false;
    if (kind == "INT" && !PyLong_CheckExact(value.ptr())) return false;
    if (kind == "STR" && !PyUnicode_CheckExact(value.ptr())) return false;
    if (kind == "FLOAT" && !PyFloat_CheckExact(value.ptr())) return false;
    PyObject* sample = PyTuple_GET_ITEM(expected.ptr(), 1);
    if (kind == "BYTES") {
        if (!PyBytes_CheckExact(value.ptr())) return false;
        nb::object encoded = lh_check(PyObject_CallMethod(value.ptr(), "hex", nullptr));
        int same = PyObject_RichCompareBool(encoded.ptr(), sample, Py_EQ);
        if (same < 0) throw nb::python_error();
        return same != 0;
    }
    int same = PyObject_RichCompareBool(value.ptr(), sample, Py_EQ);
    if (same < 0) throw nb::python_error();
    return same != 0;
}
bool lh_profile_code(nb::handle code, nb::handle expected) {
    if (!PyCode_Check(code.ptr())) return false;
    if (!PyTuple_CheckExact(expected.ptr()) || PyTuple_GET_SIZE(expected.ptr()) != 18)
        throw nb::type_error("Labels history complete compiler profile changed");
    const char* fields[] = {"co_argcount", "co_posonlyargcount", "co_kwonlyargcount",
        "co_nlocals", "co_stacksize", "co_flags", "co_code", "co_consts", "co_names",
        "co_varnames", "co_freevars", "co_cellvars", "co_name", "co_qualname",
        "co_firstlineno", "co_exceptiontable", "co_linetable", "co_filename"};
    for (Py_ssize_t i = 0; i < 18; ++i) {
        nb::object actual = lh_check(PyObject_GetAttrString(code.ptr(), fields[i]));
        if (!lh_profile_atom(actual, nb::handle(PyTuple_GET_ITEM(expected.ptr(), i)))) return false;
    }
    return true;
}
nb::object lh_rows(LabelsHistoryBinding& binding) {
    if (!PyTuple_CheckExact(binding.profiles.ptr()))
        throw nb::type_error("Labels history compiler profiles are required");
    for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(binding.profiles.ptr()); ++i) {
        PyObject* row = PyTuple_GET_ITEM(binding.profiles.ptr(), i);
        if (!PyTuple_CheckExact(row) || PyTuple_GET_SIZE(row) != 2)
            throw nb::type_error("Labels history version profile changed");
        long minor = PyLong_AsLong(PyTuple_GET_ITEM(row, 0));
        if (minor == -1 && PyErr_Occurred()) throw nb::python_error();
        if (minor == PY_MINOR_VERSION) {
            PyObject* profiles = PyTuple_GET_ITEM(row, 1);
            if (!PyTuple_CheckExact(profiles) || PyTuple_GET_SIZE(profiles) != 14)
                throw nb::type_error("Labels history fourteen declaration profiles are required");
            return nb::borrow<nb::object>(profiles);
        }
    }
    throw nb::value_error("Labels history admission has no reviewed interpreter profile");
}

// Scoped declaration provenance only. The actual typing/collections factory
// policies are still required owned Python callees, not approved primitives.
#define LH_SCOPE_FIELDS(X) \
    X(defining_globals) X(source_frame) X(factory_profiles) X(profile_rows) \
    X(atom_class) X(atom_function) X(atom_globals) X(atom_builtins) \
    X(typing_globals) X(collections_globals) X(helper) X(namedtuple) \
    X(helper_wrapper) X(namedtuple_wrapper) X(eval_wrapper) \
    X(eval_previous) X(eval_builtins) X(producer_eval) X(helper_frame) \
    X(created_function) X(created_globals) X(created_builtins)
struct LabelsHistoryAtomScope {
#define LH_SCOPE_DECLARE(name) nb::object name;
    LH_SCOPE_FIELDS(LH_SCOPE_DECLARE)
#undef LH_SCOPE_DECLARE
    unsigned long thread = 0;
    int phase = 0, forwarding = 0;
    bool active = false, finished = false, eval_had_global = false;
    LabelsHistoryAtomScope() = default;
    LabelsHistoryAtomScope(LabelsHistoryAtomScope&&) = default;
    void restore();
    void clear();
    ~LabelsHistoryAtomScope() { clear(); }
};

void lh_scope_restore_field(nb::handle scope, const char* key, nb::handle wrapper,
                            nb::handle previous, bool had_previous) {
    if (!scope.is_valid() || !wrapper.is_valid()) return;
    PyObject* current = PyDict_GetItemString(scope.ptr(), key);
    if (!current && PyErr_Occurred()) throw nb::python_error();
    if (current != wrapper.ptr()) return;
    int status = had_previous ? PyDict_SetItemString(scope.ptr(), key, previous.ptr())
                              : PyDict_DelItemString(scope.ptr(), key);
    if (status < 0) throw nb::python_error();
}
void LabelsHistoryAtomScope::restore() {
    // Mark inactive first: escaped/reentrant wrappers can only forward while
    // owned fields are being restored or their references are being released.
    active = false;
    lh_scope_restore_field(collections_globals, "eval", eval_wrapper, eval_previous, eval_had_global);
    lh_scope_restore_field(collections_globals, "namedtuple", namedtuple_wrapper, namedtuple, true);
    lh_scope_restore_field(typing_globals, "_make_nmtuple", helper_wrapper, helper, true);
    phase = 0;
    source_frame.reset(); helper_frame.reset();
    created_function.reset(); created_globals.reset(); created_builtins.reset();
    // Escaped wrappers retain this owner through their ordinary CFunction
    // m_self, while the finished scope does not keep wrapper roots itself.
    helper_wrapper.reset(); namedtuple_wrapper.reset(); eval_wrapper.reset();
    finished = true;
}
void LabelsHistoryAtomScope::clear() {
    PyObject *type = nullptr, *value = nullptr, *traceback = nullptr;
    PyErr_Fetch(&type, &value, &traceback);
    try { restore(); } catch (nb::python_error& error) { error.restore(); PyErr_Clear(); }
#define LH_SCOPE_CLEAR(name) name.reset();
    LH_SCOPE_FIELDS(LH_SCOPE_CLEAR)
#undef LH_SCOPE_CLEAR
    PyErr_Clear();
    PyErr_Restore(type, value, traceback);
}
int lh_scope_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) {
        const auto& scope = *nb::inst_ptr<LabelsHistoryAtomScope>(self);
#define LH_SCOPE_VISIT(name) Py_VISIT(scope.name.ptr());
        LH_SCOPE_FIELDS(LH_SCOPE_VISIT)
#undef LH_SCOPE_VISIT
    }
    return 0;
}
int lh_scope_clear(PyObject* self) {
    if (nb::inst_ready(self)) nb::inst_ptr<LabelsHistoryAtomScope>(self)->clear();
    return 0;
}
PyType_Slot lh_scope_slots[] = {{Py_tp_traverse, reinterpret_cast<void*>(lh_scope_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(lh_scope_clear)}, {0, nullptr}};

bool lh_scope_function(nb::handle function, nb::handle globals, nb::handle profile) {
    return PyFunction_Check(function.ptr()) && PyFunction_GetGlobals(function.ptr()) == globals.ptr() &&
           lh_profile_code(nb::handle(PyFunction_GetCode(function.ptr())), profile);
}
bool lh_scope_frame(PyFrameObject* frame, nb::handle globals, nb::handle profile) {
    if (!frame) return false;
    nb::object scope = lh_check(PyFrame_GetGlobals(frame));
    if (scope.ptr() != globals.ptr()) return false;
    nb::object code = lh_check(reinterpret_cast<PyObject*>(PyFrame_GetCode(frame)));
    return lh_profile_code(code, profile);
}
bool lh_scope_ancestry(LabelsHistoryAtomScope& scope) {
    nb::object frame = nb::borrow<nb::object>(reinterpret_cast<PyObject*>(PyEval_GetFrame()));
    while (frame.is_valid()) {
        if (frame.ptr() == scope.source_frame.ptr()) return true;
        frame = nb::steal<nb::object>(reinterpret_cast<PyObject*>(PyFrame_GetBack(
            reinterpret_cast<PyFrameObject*>(frame.ptr()))));
        if (!frame.is_valid() && PyErr_Occurred()) throw nb::python_error();
    }
    return false;
}
bool lh_scope_current(LabelsHistoryAtomScope& scope) {
    return scope.active && scope.thread == PyThread_get_thread_ident() && scope.forwarding == 0;
}
struct LHScopeForward {
    LabelsHistoryAtomScope& scope;
    bool counted;
    explicit LHScopeForward(LabelsHistoryAtomScope& value) : scope(value),
        counted(value.thread == PyThread_get_thread_ident()) { if (counted) ++scope.forwarding; }
    ~LHScopeForward() { if (counted) --scope.forwarding; }
};
nb::object lh_scope_call(nb::handle function, PyObject* args, PyObject* kwargs) {
    if (!function.is_valid()) throw nb::type_error("Labels factory delegate metadata was cleared");
    return lh_check(PyObject_Call(function.ptr(), args, kwargs));
}
void lh_scope_error() {
    try { throw; }
    catch (nb::python_error& error) { error.restore(); }
    catch (const nb::builtin_exception& error) {
        PyErr_SetString(error.type() == nb::exception_type::value_error ? PyExc_ValueError : PyExc_TypeError,
                        error.what());
    }
    catch (const std::bad_alloc&) { PyErr_NoMemory(); }
    catch (const std::exception& error) { PyErr_SetString(PyExc_RuntimeError, error.what()); }
}
PyObject* lh_factory_helper(PyObject* self, PyObject* args, PyObject* kwargs);
PyObject* lh_factory_namedtuple(PyObject* self, PyObject* args, PyObject* kwargs);
PyObject* lh_factory_eval(PyObject* self, PyObject* args, PyObject* kwargs);
PyMethodDef lh_factory_helper_def = {"_labels_history_make_nmtuple_scope",
    reinterpret_cast<PyCFunction>(lh_factory_helper), METH_VARARGS | METH_KEYWORDS, nullptr};
PyMethodDef lh_factory_namedtuple_def = {"_labels_history_namedtuple_scope",
    reinterpret_cast<PyCFunction>(lh_factory_namedtuple), METH_VARARGS | METH_KEYWORDS, nullptr};
PyMethodDef lh_factory_eval_def = {"_labels_history_eval_scope",
    reinterpret_cast<PyCFunction>(lh_factory_eval), METH_VARARGS | METH_KEYWORDS, nullptr};

PyObject* lh_factory_helper(PyObject* self, PyObject* args, PyObject* kwargs) {
    try {
        auto& scope = *nb::inst_ptr<LabelsHistoryAtomScope>(self);
        PyObject* name = PyTuple_GET_SIZE(args) ? PyTuple_GET_ITEM(args, 0) :
            (kwargs ? PyDict_GetItemString(kwargs, "name") : nullptr);
        bool target = name && PyUnicode_CheckExact(name) &&
            PyUnicode_CompareWithASCIIString(name, "_MaskedPaintAtom") == 0;
        nb::handle profile(PyTuple_GET_ITEM(scope.profile_rows.ptr(), 0));
        if (!lh_scope_current(scope) || scope.phase != 0 || !target || !lh_scope_ancestry(scope) ||
            !lh_scope_function(scope.helper, scope.typing_globals, profile) ||
            PyDict_GetItemString(scope.typing_globals.ptr(), "_make_nmtuple") != scope.helper_wrapper.ptr()) {
            LHScopeForward forwarding(scope);
            return lh_scope_call(scope.helper, args, kwargs).release().ptr();
        }
        // Observe the actual second helper only while the target source helper
        // runs. Its default/foreign body itself is invoked exactly once.
        PyObject* namedtuple = PyDict_GetItemString(scope.collections_globals.ptr(), "namedtuple");
        nb::handle named_profile(PyTuple_GET_ITEM(scope.profile_rows.ptr(), 1));
        if (!namedtuple || !lh_scope_function(nb::handle(namedtuple), scope.collections_globals, named_profile)) {
            LHScopeForward forwarding(scope);
            return lh_scope_call(scope.helper, args, kwargs).release().ptr();
        }
        scope.namedtuple = nb::borrow<nb::object>(namedtuple);
        scope.atom_class.reset(); scope.atom_function.reset(); scope.atom_globals.reset(); scope.atom_builtins.reset();
        if (PyDict_SetItemString(scope.collections_globals.ptr(), "namedtuple", scope.namedtuple_wrapper.ptr()) < 0)
            throw nb::python_error();
        scope.phase = 1;
        nb::object result;
        try { result = lh_scope_call(scope.helper, args, kwargs); }
        catch (...) {
            scope.phase = 0;
            lh_scope_restore_field(scope.collections_globals, "namedtuple", scope.namedtuple_wrapper, scope.namedtuple, true);
            throw;
        }
        scope.phase = 0;
        lh_scope_restore_field(scope.collections_globals, "namedtuple", scope.namedtuple_wrapper, scope.namedtuple, true);
        return result.release().ptr();
    } catch (...) { lh_scope_error(); return nullptr; }
}
PyObject* lh_factory_namedtuple(PyObject* self, PyObject* args, PyObject* kwargs) {
    try {
        auto& scope = *nb::inst_ptr<LabelsHistoryAtomScope>(self);
        PyFrameObject* frame = PyEval_GetFrame();
        nb::handle helper_profile(PyTuple_GET_ITEM(scope.profile_rows.ptr(), 0));
        nb::handle profile(PyTuple_GET_ITEM(scope.profile_rows.ptr(), 1));
        if (!lh_scope_current(scope) || scope.phase != 1 ||
            !lh_scope_frame(frame, scope.typing_globals, helper_profile) ||
            !lh_scope_function(scope.namedtuple, scope.collections_globals, profile) ||
            PyDict_GetItemString(scope.collections_globals.ptr(), "namedtuple") != scope.namedtuple_wrapper.ptr()) {
            LHScopeForward forwarding(scope);
            return lh_scope_call(scope.namedtuple, args, kwargs).release().ptr();
        }
        scope.helper_frame = nb::borrow<nb::object>(reinterpret_cast<PyObject*>(frame));
        scope.eval_builtins = lh_check(PyObject_GetAttrString(scope.namedtuple.ptr(), "__builtins__"));
        PyObject* eval = PyDict_GetItemString(scope.collections_globals.ptr(), "eval");
        scope.eval_had_global = eval != nullptr;
        scope.eval_previous = nb::borrow<nb::object>(eval);
        scope.created_function.reset(); scope.created_globals.reset(); scope.created_builtins.reset();
        if (PyDict_SetItemString(scope.collections_globals.ptr(), "eval", scope.eval_wrapper.ptr()) < 0)
            throw nb::python_error();
        scope.phase = 2;
        nb::object result;
        try { result = lh_scope_call(scope.namedtuple, args, kwargs); }
        catch (...) {
            scope.phase = 1;
            lh_scope_restore_field(scope.collections_globals, "eval", scope.eval_wrapper, scope.eval_previous, scope.eval_had_global);
            scope.helper_frame.reset();
            scope.created_function.reset(); scope.created_globals.reset(); scope.created_builtins.reset();
            throw;
        }
        scope.phase = 1;
        lh_scope_restore_field(scope.collections_globals, "eval", scope.eval_wrapper, scope.eval_previous, scope.eval_had_global);
        scope.helper_frame.reset();
        if (scope.created_function.is_valid() && PyType_Check(result.ptr())) {
            scope.atom_class = result;
            scope.atom_function = std::move(scope.created_function);
            scope.atom_globals = std::move(scope.created_globals);
            scope.atom_builtins = std::move(scope.created_builtins);
        }
        return result.release().ptr();
    } catch (...) { lh_scope_error(); return nullptr; }
}
PyObject* lh_factory_eval(PyObject* self, PyObject* args, PyObject* kwargs) {
    try {
        auto& scope = *nb::inst_ptr<LabelsHistoryAtomScope>(self);
        // A missing original global uses the actual retained factory builtin
        // table at this fresh source point. It is never a contents snapshot.
        nb::object callable = scope.eval_previous;
        if (!scope.eval_had_global) {
            PyObject* current = PyDict_GetItemString(scope.eval_builtins.ptr(), "eval");
            if (!current) {
                if (PyErr_Occurred()) throw nb::python_error();
                PyErr_SetString(PyExc_NameError, "name 'eval' is not defined");
                throw nb::python_error();
            }
            callable = nb::borrow<nb::object>(current);
        }
        PyFrameObject* frame = PyEval_GetFrame();
        nb::object parent = frame ? nb::steal<nb::object>(reinterpret_cast<PyObject*>(PyFrame_GetBack(frame))) : nb::object();
        nb::handle profile(PyTuple_GET_ITEM(scope.profile_rows.ptr(), 1));
        bool creation = lh_scope_current(scope) && scope.phase == 2 &&
            PyTuple_GET_SIZE(args) == 2 && (!kwargs || PyDict_Size(kwargs) == 0) &&
            PyDict_CheckExact(PyTuple_GET_ITEM(args, 1)) &&
            callable.ptr() == scope.producer_eval.ptr() &&
            parent.ptr() == scope.helper_frame.ptr() &&
            lh_scope_frame(frame, scope.collections_globals, profile) &&
            PyDict_GetItemString(scope.collections_globals.ptr(), "eval") == scope.eval_wrapper.ptr();
        nb::object result = lh_scope_call(callable, args, kwargs);
        if (creation && PyFunction_Check(result.ptr()) &&
            PyFunction_GetGlobals(result.ptr()) == PyTuple_GET_ITEM(args, 1)) {
            scope.created_function = result;
            scope.created_globals = nb::borrow<nb::object>(PyTuple_GET_ITEM(args, 1));
            scope.created_builtins = lh_check(PyObject_GetAttrString(result.ptr(), "__builtins__"));
        }
        return result.release().ptr();
    } catch (...) { lh_scope_error(); return nullptr; }
}

nb::object lh_factory_enter(nb::object self) {
    auto& scope = *nb::inst_ptr<LabelsHistoryAtomScope>(self.ptr());
    if (scope.active || scope.finished) throw nb::type_error("Labels atom metadata scope cannot be reused");
    PyFrameObject* frame = PyEval_GetFrame();
    if (!frame) throw nb::type_error("Labels atom declaration frame is required");
    scope.defining_globals = lh_check(PyFrame_GetGlobals(frame));
    scope.source_frame = nb::borrow<nb::object>(reinterpret_cast<PyObject*>(frame));
    scope.thread = PyThread_get_thread_ident();
    scope.active = true;
    PyObject* modules = PyImport_GetModuleDict();
    PyObject* typing = PyDict_GetItemString(modules, "typing");
    PyObject* collections = PyDict_GetItemString(modules, "collections");
    if (!typing || !collections || !PyModule_Check(typing) || !PyModule_Check(collections)) return self;
    scope.typing_globals = nb::borrow<nb::object>(PyModule_GetDict(typing));
    scope.collections_globals = nb::borrow<nb::object>(PyModule_GetDict(collections));
    if (PyDict_GetItemString(scope.typing_globals.ptr(), "collections") != collections) return self;
    PyObject* helper = PyDict_GetItemString(scope.typing_globals.ptr(), "_make_nmtuple");
    PyObject* namedtuple = PyDict_GetItemString(scope.collections_globals.ptr(), "namedtuple");
    if (!helper || !namedtuple) return self;
    // A minor version may have several separately reviewed stdlib distributions.
    // Select the complete actual pair; no field, namespace or provenance guard is relaxed.
    for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(scope.factory_profiles.ptr()); ++i) {
        PyObject* row = PyTuple_GET_ITEM(scope.factory_profiles.ptr(), i);
        long minor = PyLong_AsLong(PyTuple_GET_ITEM(row, 0));
        if (minor == -1 && PyErr_Occurred()) throw nb::python_error();
        if (minor != PY_MINOR_VERSION) continue;
        PyObject* profiles = PyTuple_GET_ITEM(row, 1);
        if (lh_scope_function(nb::handle(helper), scope.typing_globals,
                              nb::handle(PyTuple_GET_ITEM(profiles, 0))) &&
            lh_scope_function(nb::handle(namedtuple), scope.collections_globals,
                              nb::handle(PyTuple_GET_ITEM(profiles, 1)))) {
            scope.profile_rows = nb::borrow<nb::object>(profiles);
            break;
        }
    }
    if (!scope.profile_rows.is_valid()) return self;
    scope.helper = nb::borrow<nb::object>(helper);
    nb::object module_name = nb::str("naparixx._native");
    scope.helper_wrapper = lh_check(PyCFunction_NewEx(&lh_factory_helper_def, self.ptr(), module_name.ptr()));
    scope.namedtuple_wrapper = lh_check(PyCFunction_NewEx(&lh_factory_namedtuple_def, self.ptr(), module_name.ptr()));
    scope.eval_wrapper = lh_check(PyCFunction_NewEx(&lh_factory_eval_def, self.ptr(), module_name.ptr()));
    if (PyDict_SetItemString(scope.typing_globals.ptr(), "_make_nmtuple", scope.helper_wrapper.ptr()) < 0)
        throw nb::python_error();
    return self;
}
PyObject* lh_factory_scope_create(PyObject* declaration, PyObject* args) {
    try {
        if (PyTuple_GET_SIZE(args) != 1 || !PyTuple_CheckExact(PyTuple_GET_ITEM(args, 0)))
            throw nb::type_error("Labels atom factory compiler profiles are required");
        auto& metadata = *nb::inst_ptr<LabelsHistoryAtomScope>(declaration);
        LabelsHistoryAtomScope scope;
        scope.factory_profiles = nb::borrow<nb::object>(PyTuple_GET_ITEM(args, 0));
        scope.producer_eval = metadata.producer_eval;
        bool reviewed_minor = false;
        for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(scope.factory_profiles.ptr()); ++i) {
            PyObject* row = PyTuple_GET_ITEM(scope.factory_profiles.ptr(), i);
            if (!PyTuple_CheckExact(row) || PyTuple_GET_SIZE(row) != 2)
                throw nb::type_error("Labels atom factory version metadata changed");
            long minor = PyLong_AsLong(PyTuple_GET_ITEM(row, 0));
            if (minor == -1 && PyErr_Occurred()) throw nb::python_error();
            if (minor != PY_MINOR_VERSION) continue;
            PyObject* profiles = PyTuple_GET_ITEM(row, 1);
            if (!PyTuple_CheckExact(profiles) || PyTuple_GET_SIZE(profiles) != 2)
                throw nb::type_error("Labels atom two actual factory profiles are required");
            reviewed_minor = true;
        }
        if (!reviewed_minor) throw nb::value_error("Labels atom has no reviewed factory interpreter profile");
        return nb::cast(std::move(scope)).release().ptr();
    } catch (...) { lh_scope_error(); return nullptr; }
}
PyMethodDef lh_factory_scope_create_def = {"labels_history_atom_scope", lh_factory_scope_create, METH_VARARGS, nullptr};
void bind_labels_history_atom_scope_exports(nb::module_& module) {
    auto scope = nb::class_<LabelsHistoryAtomScope>(module, "LabelsHistoryAtomScope",
        nb::is_weak_referenceable(), nb::type_slots(lh_scope_slots));
    scope.def("__enter__", &lh_factory_enter);
    scope.def("__exit__", [](LabelsHistoryAtomScope& value, nb::object, nb::object, nb::object) {
        value.restore(); return false;
    }, nb::arg("exc_type").none(), nb::arg("exc_value").none(), nb::arg("traceback").none());
    LabelsHistoryAtomScope declaration;
    PyObject* builtins = PyDict_GetItemString(PyImport_GetModuleDict(), "builtins");
    if (builtins && PyModule_Check(builtins)) {
        PyObject* eval = PyDict_GetItemString(PyModule_GetDict(builtins), "eval");
        // Capture the actual canonical builtin producer as GC-visible
        // declaration metadata. Python/foreign creator outputs are never
        // authenticated by their returned function's code/name/shape.
        if (eval && PyCFunction_Check(eval) && PyCFunction_GET_SELF(eval) == builtins)
            declaration.producer_eval = nb::borrow<nb::object>(eval);
    }
    nb::object owner = nb::cast(std::move(declaration));
    nb::object module_name = nb::str("naparixx._native");
    module.attr("labels_history_atom_scope") = lh_check(PyCFunction_NewEx(
        &lh_factory_scope_create_def, owner.ptr(), module_name.ptr()));
}
#undef LH_SCOPE_FIELDS

PyObject* lh_frame(PyObject*, PyObject* arguments) {
    try {
        Py_ssize_t count = PyTuple_GET_SIZE(arguments);
        if (count == 1) {
            nb::handle value(PyTuple_GET_ITEM(arguments, 0));
            if (!nb::isinstance<LabelsHistoryLocals>(value) || !nb::inst_ready(value.ptr()))
                throw nb::type_error("Labels history actual parameter owner is required");
            auto& o = *nb::inst_ptr<LabelsHistoryLocals>(value.ptr());
            if (o.entry == 1) {
                LabelsHistoryGenerator generator;
                generator.context = nb::borrow<nb::object>(value);
                return nb::cast(std::move(generator)).release().ptr();
            }
            return lh_run(o).release().ptr();
        }
        if (count < 2 || !PyLong_CheckExact(PyTuple_GET_ITEM(arguments, 0)))
            throw nb::type_error("Labels history source entry and parameters are required");
        long entry = PyLong_AsLong(PyTuple_GET_ITEM(arguments, 0));
        if (entry == -1 && PyErr_Occurred()) throw nb::python_error();
        const int sizes[] = {2, 1, 1, 1, 1, 1, 2, 2, 4, 3, 1, 1, 4, 5};
        if (entry < 0 || entry > 13 || count != sizes[entry] + 1)
            throw nb::type_error("Labels history parameter declaration changed");
        PyFrameObject* frame = PyEval_GetFrame();
        if (!frame) throw nb::type_error("Labels history declaration source frame is required");
        LabelsHistoryLocals o;
        o.entry = static_cast<int>(entry);
        o.source_globals = lh_check(PyFrame_GetGlobals(frame));
        o.source_builtins = lh_check(PyFrame_GetBuiltins(frame));
        o.source_code = lh_check(reinterpret_cast<PyObject*>(PyFrame_GetCode(frame)));
        auto* code = reinterpret_cast<PyCodeObject*>(o.source_code.ptr());
        PyObject* constants = code->co_consts;
        o.source_metadata = nb::borrow<nb::object>(PyTuple_GET_ITEM(constants, PyTuple_GET_SIZE(constants) - 1));
        lh_metadata_values(o.source_metadata, 0);
        lh_metadata_values(o.source_metadata, 1);
        PyObject* binding = PyDict_GetItemString(o.source_globals.ptr(), "__naparixx_labels_history_binding__");
        if (!binding || !nb::isinstance<LabelsHistoryBinding>(nb::handle(binding)) || !nb::inst_ready(binding))
            throw nb::type_error("Labels history declaration binding is missing or changed");
        o.binding = nb::borrow<nb::object>(binding);
        auto p = [&](Py_ssize_t index) { return nb::borrow<nb::object>(PyTuple_GET_ITEM(arguments, index + 1)); };
        switch (entry) {
        case 0: o.self = p(0); o.event = p(1); break;
        case 6: o.self = p(0); o.item = p(1); break;
        case 7: o.self = p(0); o.value = p(1); break;
        case 8: o.self = p(0); o.before = p(1); o.after = p(2); o.undoing = p(3); break;
        case 9: o.self = p(0); o.atom = p(1); o.undoing = p(2); break;
        case 12: o.volume_slice = p(0); o.region_data = p(1); o.mask = p(2); o.new_label = p(3); break;
        case 13: o.cls = p(0); o.volume_slice = p(1); o.mask = p(2); o.values = p(3); o.new_label = p(4); break;
        default: o.self = p(0); break;
        }
        return nb::cast(std::move(o)).release().ptr();
    } catch (nb::python_error& error) {
        error.restore();
    } catch (const nb::builtin_exception& error) {
        PyErr_SetString(error.type() == nb::exception_type::value_error ? PyExc_ValueError : PyExc_TypeError,
                        error.what());
    } catch (const std::bad_alloc&) {
        PyErr_NoMemory();
    } catch (const std::exception& error) {
        PyErr_SetString(PyExc_RuntimeError, error.what());
    }
    return nullptr;
}
PyMethodDef lh_frame_definition = {"_labels_history_source_frame", lh_frame, METH_VARARGS, nullptr};

bool lh_native_code(nb::handle function) {
    if (!PyFunction_Check(function.ptr())) return false;
    auto* code = reinterpret_cast<PyCodeObject*>(PyFunction_GetCode(function.ptr()));
    for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(code->co_consts); ++i) {
        PyObject* value = PyTuple_GET_ITEM(code->co_consts, i);
        if (PyCFunction_Check(value) && PyCFunction_GET_SELF(value) == nullptr &&
            PyCFunction_GET_FUNCTION(value) == reinterpret_cast<PyCFunction>(lh_frame)) return true;
    }
    return false;
}
nb::object lh_shell_code(LabelsHistoryBinding& binding, int entry, nb::handle function) {
    PyObject* shell = PyTuple_GET_ITEM(binding.shells.ptr(), entry);
    nb::object code = lh_check(PyObject_GetAttrString(shell, "__code__"));
    nb::object constants = lh_check(PyObject_GetAttrString(code.ptr(), "co_consts"));
    auto* original = reinterpret_cast<PyCodeObject*>(PyFunction_GetCode(function.ptr()));
    nb::object replacement = lh_check(PyTuple_New(PyTuple_GET_SIZE(constants.ptr()) + 1));
    bool marker = false;
    for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(constants.ptr()); ++i) {
        PyObject* value = PyTuple_GET_ITEM(constants.ptr(), i);
        if (value == Py_Ellipsis) { value = binding.frame.ptr(); marker = true; }
        PyTuple_SET_ITEM(replacement.ptr(), i, Py_NewRef(value));
    }
    if (!marker) throw nb::type_error("Labels history declaration transport marker is missing");
    for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(original->co_consts); ++i)
        if (PyCode_Check(PyTuple_GET_ITEM(original->co_consts, i)))
            throw nb::type_error("Labels history selected metadata unexpectedly contains a nested executable body");
    nb::object metadata = lh_check(PyTuple_New(2));
    PyTuple_SET_ITEM(metadata.ptr(), 0, Py_NewRef(original->co_consts));
    PyTuple_SET_ITEM(metadata.ptr(), 1, Py_NewRef(original->co_names));
    PyTuple_SET_ITEM(replacement.ptr(), PyTuple_GET_SIZE(constants.ptr()), metadata.release().ptr());
    nb::dict kwargs;
    kwargs["co_consts"] = replacement;
    kwargs["co_filename"] = nb::borrow<nb::object>(original->co_filename);
    kwargs["co_name"] = nb::borrow<nb::object>(original->co_name);
    kwargs["co_qualname"] = nb::borrow<nb::object>(original->co_qualname);
    kwargs["co_firstlineno"] = nb::int_(original->co_firstlineno);
    kwargs["co_flags"] = nb::int_(original->co_flags);
    nb::object arguments = lh_check(PyTuple_New(0));
    nb::object replace = lh_check(PyObject_GetAttrString(code.ptr(), "replace"));
    return lh_check(PyObject_Call(replace.ptr(), arguments.ptr(), kwargs.ptr()));
}
const char* lh_method_names[] = {"_reset_history", "block_history", "_commit_staged_history",
    "_begin_stroke", "_commit_stroke", "_abort_stroke", "_append_to_undo_history",
    "_save_history", "_load_history", "_replay_masked_atom", "undo", "redo",
    "_history_atom_for_mask_paint"};
bool lh_source_function(nb::handle function, nb::handle profile,
                        nb::handle cls, nb::handle defining_globals) {
    if (!PyFunction_Check(function.ptr()) ||
        PyFunction_GetGlobals(function.ptr()) != defining_globals.ptr() ||
        !lh_profile_code(nb::handle(PyFunction_GetCode(function.ptr())), profile)) return false;
    PyObject* actual_class = PyDict_GetItemString(defining_globals.ptr(), "Labels");
    if (!actual_class && PyErr_Occurred()) throw nb::python_error();
    return actual_class == cls.ptr();
}
nb::object labels_history_bind(nb::object cls, nb::object atom, nb::tuple shells,
                               nb::tuple profiles, nb::object atom_metadata) {
    if (!PyType_Check(cls.ptr()) || !PyType_Check(atom.ptr()) || PyTuple_GET_SIZE(shells.ptr()) != 14)
        throw nb::type_error("Labels history actual classes and fourteen shells are required");
    for (Py_ssize_t i = 0; i < 14; ++i)
        if (!PyFunction_Check(PyTuple_GET_ITEM(shells.ptr(), i)))
            throw nb::type_error("Labels history metadata shells must be functions");
    if (!nb::isinstance<LabelsHistoryAtomScope>(atom_metadata) || !nb::inst_ready(atom_metadata.ptr()))
        throw nb::type_error("Labels history source-point atom metadata is required");
    const auto& captured = *nb::inst_ptr<LabelsHistoryAtomScope>(atom_metadata.ptr());
    if (captured.active || !captured.finished)
        throw nb::type_error("Labels history constructor capture scope must finish before admission");
    if (!captured.defining_globals.is_valid() || !PyDict_CheckExact(captured.defining_globals.ptr()))
        throw nb::type_error("Labels history actual defining namespace metadata is required");
    LabelsHistoryBinding binding;
    binding.defining_globals = captured.defining_globals;
    binding.atom_class = captured.atom_class;
    binding.atom_function = captured.atom_function;
    binding.atom_globals = captured.atom_globals;
    binding.atom_builtins = captured.atom_builtins;
    binding.shells = std::move(shells);
    binding.profiles = std::move(profiles);
    nb::object module_name = nb::str("naparixx._native");
    binding.frame = lh_check(PyCFunction_NewEx(&lh_frame_definition, nullptr, module_name.ptr()));
    nb::object rows = lh_rows(binding);
    nb::object binding_object = nb::cast(std::move(binding));
    auto& b = *nb::inst_ptr<LabelsHistoryBinding>(binding_object.ptr());
    std::vector<std::pair<nb::object, nb::object>> changes;
    std::vector<nb::object> globals;
    auto admit = [&](nb::object function, int entry, bool constructor) {
        if (lh_native_code(function)) return;
        nb::handle profile(PyTuple_GET_ITEM(rows.ptr(), entry));
        if (constructor) {
            if (!PyFunction_Check(function.ptr()) ||
                function.ptr() != b.atom_function.ptr() ||
                PyFunction_GetGlobals(function.ptr()) != b.atom_globals.ptr()) return;
            nb::object builtins = lh_check(PyObject_GetAttrString(function.ptr(), "__builtins__"));
            if (builtins.ptr() != b.atom_builtins.ptr() ||
                !lh_profile_code(nb::handle(PyFunction_GetCode(function.ptr())), profile)) return;
        } else if (!lh_source_function(function, profile, cls, b.defining_globals)) return;
        for (const auto& change : changes) if (change.first.ptr() == function.ptr()) return;
        nb::object scope = nb::borrow<nb::object>(PyFunction_GetGlobals(function.ptr()));
        globals.push_back(scope);
        changes.emplace_back(function, lh_shell_code(b, entry, function));
    };
    for (int entry = 0; entry < 13; ++entry) {
        nb::object function = lh_check(PyObject_GetAttrString(cls.ptr(), lh_method_names[entry]));
        if (entry != 1) { admit(std::move(function), entry, false); continue; }
        // contextlib helper/closure and its public metadata are never replaced.
        // Both the actual captured generator and an unchanged __wrapped__ alias
        // are admitted; deliberate foreign replacements remain real callbacks.
        if (PyFunction_Check(function.ptr())) {
            auto* code = reinterpret_cast<PyCodeObject*>(PyFunction_GetCode(function.ptr()));
            nb::object freevars = lh_check(PyObject_GetAttrString(reinterpret_cast<PyObject*>(code), "co_freevars"));
            PyObject* closure = PyFunction_GetClosure(function.ptr());
            if (closure && PyTuple_CheckExact(closure) &&
                PyTuple_GET_SIZE(closure) == PyTuple_GET_SIZE(freevars.ptr())) {
                for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(closure); ++i) {
                    PyObject* name = PyTuple_GET_ITEM(freevars.ptr(), i);
                    if (PyUnicode_CompareWithASCIIString(name, "func") == 0) {
                        nb::object captured = nb::steal<nb::object>(PyCell_Get(PyTuple_GET_ITEM(closure, i)));
                        if (captured.is_valid()) admit(std::move(captured), entry, false);
                        else if (PyErr_Occurred()) throw nb::python_error();
                    }
                }
            }
        }
        nb::object wrapped = nb::steal<nb::object>(PyObject_GetAttrString(function.ptr(), "__wrapped__"));
        if (wrapped.is_valid()) admit(std::move(wrapped), entry, false);
        else if (PyErr_ExceptionMatches(PyExc_AttributeError)) PyErr_Clear();
        else throw nb::python_error();
    }
    // Admit the captured genuine function even if a later class-creation
    // callback installed a foreign __new__; saved genuine aliases stay native.
    if (b.atom_function.is_valid()) admit(b.atom_function, 13, true);
    admit(lh_check(PyObject_GetAttrString(atom.ptr(), "__new__")), 13, true);
    for (auto& scope : globals)
        if (PyDict_SetItemString(scope.ptr(), "__naparixx_labels_history_binding__", binding_object.ptr()) < 0)
            throw nb::python_error();
    for (auto& change : changes)
        if (PyObject_SetAttrString(change.first.ptr(), "__code__", change.second.ptr()) < 0)
            throw nb::python_error();
    return binding_object;
}
nb::object lh_optional(const nb::object& value) { return value.is_valid() ? value : nb::none(); }
void bind_labels_history_exports(nb::module_& module) {
    auto locals = nb::class_<LabelsHistoryLocals>(module, "LabelsHistoryLocals",
        nb::is_weak_referenceable(), nb::type_slots(lh_locals_slots));
#define LH_PROPERTY(name) locals.def_prop_ro(#name, [](const LabelsHistoryLocals& o) { return lh_optional(o.name); });
    LH_FIELDS(LH_PROPERTY)
#undef LH_PROPERTY
    locals.def_prop_ro("_cls", [](const LabelsHistoryLocals& o) { return lh_optional(o.cls); });
    locals.def_prop_ro("old_values", [](const LabelsHistoryLocals& o) { return lh_optional(o.values); });
    locals.def_prop_ro("prev_indices", [](const LabelsHistoryLocals& o) { return lh_optional(o.indices); });
    locals.def_prop_ro("_", [](const LabelsHistoryLocals& o) { return lh_optional(o.ignored); });
    nb::class_<LabelsHistoryBinding>(module, "LabelsHistoryBinding",
        nb::is_weak_referenceable(), nb::type_slots(lh_binding_slots));
    auto generator = nb::class_<LabelsHistoryGenerator>(module, "LabelsHistoryGenerator",
        nb::is_weak_referenceable(), nb::type_slots(lh_generator_slots));
    generator.def("__iter__", [](nb::object self) { return self; });
    generator.def("__next__", [](LabelsHistoryGenerator& o) { return lh_generator_send(o, nb::none()); });
    generator.def("send", &lh_generator_send, nb::arg("value").none());
    generator.def("throw", &lh_generator_throw);
    generator.def("close", &lh_generator_close);
    bind_labels_history_atom_scope_exports(module);
    module.def("labels_history_bind", &labels_history_bind,
               nb::arg("cls"), nb::arg("atom"), nb::arg("shells"), nb::arg("profiles"),
               nb::arg("atom_metadata"));
}
// Handwritten edit/publication controls. Persistent values remain solely in
// the genuine Layer dictionary/data/backend/raw/view objects.
#define LE_LOCAL_FIELDS(X) \
    X(source_globals) X(source_builtins) X(source_metadata) \
    X(self) X(slice_key) X(mask) X(new_label) X(dims_to_paint) X(refresh) X(region_data) \
    X(data) X(volume_slice) X(visible_data) X(visible_mask) X(updated_slice) X(indices) X(value) \
    X(index) X(position_in_axes) X(indices_order) X(extra_axes) X(effective_mask) X(source_label) \
    X(keep_mask) X(update_slices) X(region_slices) X(view_slices) X(new_color) X(displayed_dims) \
    X(pt_not_disp) X(axis_slice) X(current_pos) X(sorted_dims) X(perm) X(slice_input) X(point) \
    X(contour_offset) X(dims_displayed) X(raw_displayed) X(offset) X(colors_sliced) \
    X(changed_indices) X(displayed_indices) X(visible_values) X(queries) X(filtered_index) \
    X(comp_item) X(comp_key) X(comp_value) X(d)
struct LabelsEditLocals {
    int entry = -1;
#define LE_DECLARE(name) nb::object name;
    LE_LOCAL_FIELDS(LE_DECLARE)
#undef LE_DECLARE
    LabelsEditLocals() = default;
    LabelsEditLocals(LabelsEditLocals&&) = default;
    void clear() {
        // Release real source locals in their declaration-slot order before
        // releasing the generation metadata. Cleared comprehension temporaries
        // remain empty; unused carrier fields never become persistent state.
        switch (entry) {
        case 0:
            self.reset(); slice_key.reset(); mask.reset(); new_label.reset(); dims_to_paint.reset();
            refresh.reset(); region_data.reset(); extra_axes.reset(); effective_mask.reset(); break;
        case 1:
            self.reset(); data.reset(); mask.reset(); new_label.reset(); volume_slice.reset();
            effective_mask.reset(); source_label.reset(); keep_mask.reset(); break;
        case 2: self.reset(); new_label.reset(); break;
        case 3:
            self.reset(); region_data.reset(); slice_key.reset(); mask.reset(); new_label.reset();
            update_slices.reset(); region_slices.reset(); view_slices.reset(); visible_data.reset();
            visible_mask.reset(); new_color.reset(); break;
        case 4:
            self.reset(); slice_key.reset(); displayed_dims.reset(); pt_not_disp.reset();
            region_slices.reset(); view_slices.reset(); d.reset(); axis_slice.reset(); current_pos.reset(); break;
        case 5:
            self.reset(); visible_data.reset(); visible_mask.reset(); displayed_dims.reset(); sorted_dims.reset(); perm.reset(); break;
        case 6: self.reset(); slice_input.reset(); point.reset(); break;
        case 7: self.reset(); updated_slice.reset(); contour_offset.reset(); break;
        case 8: self.reset(); updated_slice.reset(); break;
        case 9:
            self.reset(); dims_displayed.reset(); raw_displayed.reset(); updated_slice.reset();
            offset.reset(); colors_sliced.reset(); break;
        case 10:
            self.reset(); indices.reset(); value.reset(); refresh.reset(); pt_not_disp.reset();
            displayed_indices.reset(); visible_values.reset(); updated_slice.reset(); changed_indices.reset(); break;
        case 11: index.reset(); position_in_axes.reset(); queries.reset(); break;
        case 12: index.reset(); position_in_axes.reset(); indices_order.reset(); filtered_index.reset(); break;
        }
#define LE_CLEAR(name) name.reset();
        LE_LOCAL_FIELDS(LE_CLEAR)
#undef LE_CLEAR
    }
    ~LabelsEditLocals() { clear(); }
};
#define LE_EXPRESSION_FIELDS(X) \
    X(source_globals) X(source_builtins) X(source_metadata) X(iterator) \
    X(capture1) X(capture2) X(capture3) X(item) X(item2)
struct LabelsEditExpressionLocals {
    int entry = -1;
#define LE_EX_DECLARE(name) nb::object name;
    LE_EXPRESSION_FIELDS(LE_EX_DECLARE)
#undef LE_EX_DECLARE
    LabelsEditExpressionLocals() = default;
    LabelsEditExpressionLocals(LabelsEditExpressionLocals&&) = default;
    void clear() {
        // A genexpr frame owns its iterator and bound target before its free
        // cells. Drop those first, then the expression-specific captures.
        iterator.reset(); item.reset(); item2.reset();
        capture1.reset(); capture2.reset(); capture3.reset();
#define LE_EX_CLEAR(name) name.reset();
        LE_EXPRESSION_FIELDS(LE_EX_CLEAR)
#undef LE_EX_CLEAR
    }
    ~LabelsEditExpressionLocals() { clear(); }
};
struct LabelsEditExpressionGenerator {
    nb::object context;
    bool started = false, running = false, closed = false;
    LabelsEditExpressionGenerator() = default;
    LabelsEditExpressionGenerator(LabelsEditExpressionGenerator&&) = default;
    void clear() { context.reset(); closed = true; }
    ~LabelsEditExpressionGenerator() { clear(); }
};
nb::object le_name(nb::handle metadata, const char* text);
nb::object le_global(nb::handle globals, nb::handle builtins, nb::handle metadata, const char* text) {
    nb::object name = le_name(metadata, text);
    PyObject* value = PyDict_GetItemWithError(globals.ptr(), name.ptr());
    if (value) return nb::borrow<nb::object>(value);
    if (PyErr_Occurred()) throw nb::python_error();
    value = PyObject_GetItem(builtins.ptr(), name.ptr());
    if (value) return nb::steal<nb::object>(value);
    if (!PyErr_ExceptionMatches(PyExc_KeyError)) throw nb::python_error();
    PyErr_Clear();
    nb::object message = lh_check(PyUnicode_FromFormat("name '%s' is not defined", text));
    nb::object error = lh_call(nb::borrow<nb::object>(PyExc_NameError), {LX(message)});
    if (PyObject_SetAttrString(error.ptr(), "name", name.ptr()) < 0) throw nb::python_error();
    PyErr_SetObject(PyExc_NameError, error.ptr());
    throw nb::python_error();
}
nb::object le_attr(nb::handle metadata, nb::object value, const char* text) {
    nb::object name = le_name(metadata, text);
    PyObject* result = PyObject_GetAttr(value.ptr(), name.ptr());
    value.reset(); name.reset();
    return lh_check(result);
}
// These helpers are defined by the declaration/admission fragment. They read
// actual per-generation literals, never reconstructed keyword/string caches.
nb::object le_keyword(nb::handle metadata, const char* name);
nb::object le_literal(nb::handle metadata, const char* text);
nb::object le_expression_prepare(LabelsEditLocals& parent, int entry,
                                nb::object capture1 = {}, nb::object capture2 = {}, nb::object capture3 = {});
nb::object le_expression_start(nb::object function, nb::object iterator);
nb::object le_binary(nb::object left, nb::object right, int operation) {
    PyObject* result = operation == 0 ? PyNumber_Add(left.ptr(), right.ptr()) :
        operation == 1 ? PyNumber_Subtract(left.ptr(), right.ptr()) :
        operation == 2 ? PyNumber_Multiply(left.ptr(), right.ptr()) :
        PyNumber_InPlaceAnd(left.ptr(), right.ptr());
    left.reset(); right.reset();
    return lh_check(result);
}
nb::object le_compare(nb::object left, nb::object right, int operation) {
    PyObject* result = PyObject_RichCompare(left.ptr(), right.ptr(), operation);
    left.reset(); right.reset();
    return lh_check(result);
}
bool le_contains(nb::object receiver, nb::object value) {
    int result = PySequence_Contains(receiver.ptr(), value.ptr());
    receiver.reset(); value.reset();
    if (result < 0) throw nb::python_error();
    return result != 0;
}
std::array<nb::object, 2> le_unpack_two(nb::object value) {
    PyObject* raw = PyObject_GetIter(value.ptr());
    if (!raw) {
        nb::python_error error;
        if (error.matches(PyExc_TypeError) && Py_TYPE(value.ptr())->tp_iter == nullptr &&
            !PySequence_Check(value.ptr())) {
            PyErr_Format(PyExc_TypeError, "cannot unpack non-iterable %.200s object", Py_TYPE(value.ptr())->tp_name);
            throw nb::python_error();
        }
        throw error;
    }
    nb::object iterator = nb::steal<nb::object>(raw);
    std::array<nb::object, 2> values;
    for (size_t i = 0; i < 2; ++i) {
        values[i] = lh_next(iterator);
        if (!values[i].is_valid()) {
            PyErr_Format(PyExc_ValueError, "not enough values to unpack (expected 2, got %zu)", i);
            throw nb::python_error();
        }
    }
    nb::object extra = lh_next(iterator);
    if (extra.is_valid()) {
        PyErr_SetString(PyExc_ValueError, "too many values to unpack (expected 2)");
        throw nb::python_error();
    }
    iterator.reset(); value.reset();
    return values;
}
void le_comp(LabelsEditLocals& o, int kind, nb::object iterator, nb::object& destination) {
    // Each inline comprehension retains its bound variables across next().
    // Publish the normal result before restoring locals. Error stack cleanup
    // drops iterator and partial result before restoring the bound variables.
    nb::object result = lh_check(kind == 1 ? PyDict_New() : PyList_New(0));
    auto A = [&](nb::object value, const char* name) { return le_attr(o.source_metadata, std::move(value), name); };
    auto G = [&](const char* name) { return le_global(o.source_globals, o.source_builtins, o.source_metadata, name); };
    try {
        for (;;) {
            nb::object next = lh_next(iterator);
            if (!next.is_valid()) break;
            if (kind == 4) {
                auto parts = le_unpack_two(std::move(next));
                lh_assign(o.comp_key, std::move(parts[0]));
                lh_assign(o.comp_value, std::move(parts[1]));
                nb::object value = le_compare(lh_item(o.index, o.comp_key), o.comp_value, Py_EQ);
                if (PyList_Append(result.ptr(), value.ptr()) < 0) throw nb::python_error();
            } else {
                lh_assign(o.comp_item, std::move(next));
                if (kind == 1) {
                    nb::object key = o.comp_item, value = lh_item(o.point, o.comp_item);
                    if (PyObject_SetItem(result.ptr(), key.ptr(), value.ptr()) < 0) throw nb::python_error();
                } else {
                    nb::object value;
                    if (kind == 0) value = lh_call(A(o.sorted_dims, "index"), {LX(o.comp_item)});
                    else if (kind == 2) value = A(o.comp_item, "start");
                    else value = lh_call(A(lh_call(A(G("np"), "array"), {LX(o.comp_item)}), "flatten"));
                    if (PyList_Append(result.ptr(), value.ptr()) < 0) throw nb::python_error();
                }
            }
        }
        iterator.reset();
        lh_assign(destination, std::move(result));
        o.comp_value.reset(); o.comp_key.reset(); o.comp_item.reset();
    } catch (...) {
        iterator.reset(); result.reset();
        o.comp_value.reset(); o.comp_key.reset(); o.comp_item.reset();
        throw;
    }
}
nb::object le_expression_value(LabelsEditExpressionLocals& o) {
    auto A = [&](nb::object value, const char* name) { return le_attr(o.source_metadata, std::move(value), name); };
    auto G = [&](const char* name) { return le_global(o.source_globals, o.source_builtins, o.source_metadata, name); };
    for (;;) {
        nb::object next = lh_next(o.iterator);
        if (!next.is_valid()) return {};
        if (o.entry == 1) {
            auto parts = le_unpack_two(std::move(next));
            lh_assign(o.item, std::move(parts[0])); lh_assign(o.item2, std::move(parts[1]));
        } else lh_assign(o.item, std::move(next));
        switch (o.entry) {
        case 0:
            if (!le_contains(o.capture1, o.item)) return o.item;
            break;
        case 1:
            return lh_call(G("slice"), {
                LX(lh_call(G("min"), {LX(A(o.item, "start")), LX(A(o.item2, "start"))})),
                LX(lh_call(G("max"), {LX(A(o.item, "stop")), LX(A(o.item2, "stop"))}))});
        case 2: return lh_item(A(o.capture1, "_updated_slice"), o.item);
        case 3: return lh_item(o.item, o.capture1);
        case 4:
            return lh_call(G("slice"), {
                LX(lh_call(G("int"), {LX(lh_call(A(o.item, "min")))})),
                LX(le_binary(lh_call(G("int"), {LX(lh_call(A(o.item, "max")))}), nb::int_(1), 0))});
        case 5:
            if (!le_contains(o.capture2, o.item)) return lh_item(lh_item(o.capture1, o.item), o.capture3);
            break;
        default: throw nb::value_error("unknown Labels edit expression");
        }
    }
}
struct LERunning {
    LabelsEditExpressionGenerator& generator;
    explicit LERunning(LabelsEditExpressionGenerator& value) : generator(value) {
        if (generator.running) throw nb::value_error("generator already executing");
        generator.running = true;
    }
    ~LERunning() { generator.running = false; }
};
nb::object le_expression_send(LabelsEditExpressionGenerator& generator, nb::object value) {
    LERunning running(generator);
    if (generator.closed) throw nb::stop_iteration();
    if (!generator.started && !value.is_none())
        throw nb::type_error("can't send non-None value to a just-started generator");
    generator.started = true;
    try {
        auto& o = *nb::inst_ptr<LabelsEditExpressionLocals>(generator.context.ptr());
        nb::object result = le_expression_value(o);
        if (result.is_valid()) return result;
    } catch (nb::python_error& error) {
        generator.clear();
        lh_generator_failure(error);
    }
    generator.clear();
    throw nb::stop_iteration();
}
nb::object le_expression_throw(LabelsEditExpressionGenerator& generator, nb::args arguments) {
    LERunning running(generator);
    nb::object exception = lh_throw_value(arguments);
    // PEP479 applies only when throw resumes a suspended generator body.
    // Created, closed and exhausted generators propagate the exact injected
    // exception directly, including StopIteration, without entering the body.
    const bool suspended = generator.started && !generator.closed;
    generator.clear();
    PyErr_SetObject(reinterpret_cast<PyObject*>(Py_TYPE(exception.ptr())), exception.ptr());
    if (!suspended) throw nb::python_error();
    try { throw nb::python_error(); }
    catch (nb::python_error& error) { lh_generator_failure(error); }
}
nb::object le_expression_close(LabelsEditExpressionGenerator& generator) {
    LERunning running(generator);
    generator.clear();
    return nb::none();
}

nb::object le_run(LabelsEditLocals& o) {
    auto A = [&](nb::object value, const char* name) { return le_attr(o.source_metadata, std::move(value), name); };
    auto G = [&](const char* name) { return le_global(o.source_globals, o.source_builtins, o.source_metadata, name); };
    auto S = [&](const char* name) { return A(o.self, name); };
    auto N = [&](const char* name) { return A(G("np"), name); };
    auto K = [&](const char* name) { return le_keyword(o.source_metadata, name); };
    auto set = [&](const char* name, nb::object value) {
        nb::object key = le_name(o.source_metadata, name);
        if (PyObject_SetAttr(o.self.ptr(), key.ptr(), value.ptr()) < 0) throw nb::python_error();
    };
    switch (o.entry) {
    case 0: { // _paint_region_with_mask
        nb::object tuple = G("tuple");
        nb::object function = le_expression_prepare(o, 0, o.dims_to_paint);
        nb::object iterator = lh_iter(lh_call(G("range"), {LX(S("ndim"))}));
        lh_assign(o.extra_axes, lh_call(std::move(tuple), {LX(le_expression_start(std::move(function), std::move(iterator)))}));
        if (lh_truth(o.extra_axes)) {
            lh_assign(o.mask, lh_call(N("expand_dims"), {LX(o.mask), LX(o.extra_axes)}));
            if (!o.region_data.is_none())
                lh_assign(o.region_data, lh_call(N("expand_dims"), {LX(o.region_data), LX(o.extra_axes)}));
        }
        if (o.region_data.is_none())
            lh_assign(o.region_data, lh_call(N("asarray"), {LX(lh_index(LX(S("data")), LX(o.slice_key)))}));
        lh_assign(o.effective_mask, lh_call(S("_apply_mask_to_data"),
            {LX(o.region_data), LX(o.mask), LX(o.new_label), LX(o.slice_key)}));
        if (o.effective_mask.is_none()) return nb::none();
        lh_set_item(LX(o.region_data), LX(S("data")), LX(o.slice_key));
        lh_call(S("_refresh_caches_from_region"), {LX(o.region_data), LX(o.slice_key), LX(o.effective_mask), LX(o.new_label)});
        lh_call(S("_accumulate_updated_slice"), {LX(o.slice_key)});
        if (lh_truth(o.refresh)) lh_call(S("_partial_labels_refresh"));
        return nb::none();
    }
    case 1: // _apply_mask_to_data
        lh_assign(o.effective_mask, lh_call(N("array"), {LX(o.mask)}, {LX(nb::bool_(true))}, K("copy")));
        if (lh_truth(S("preserve_labels"))) {
            lh_assign(o.source_label, lh_call(S("_get_preserve_labels_source_label"), {LX(o.new_label)}));
            lh_assign(o.keep_mask, le_compare(o.data, o.source_label, Py_EQ));
            if (!lh_truth(lh_call(N("any"), {LX(o.keep_mask)}))) return nb::none();
            lh_assign(o.effective_mask, le_binary(o.effective_mask, o.keep_mask, 3));
        }
        lh_assign(o.effective_mask, le_binary(o.effective_mask, le_compare(o.data, o.new_label, Py_NE), 3));
        if (!lh_truth(lh_call(N("any"), {LX(o.effective_mask)}))) return nb::none();
        lh_call(S("_save_history"), {LX(lh_call(S("_history_atom_for_mask_paint"),
            {LX(o.volume_slice), LX(o.data), LX(o.effective_mask), LX(o.new_label)}))});
        lh_set_item(LX(o.new_label), LX(o.data), LX(o.effective_mask));
        return o.effective_mask;
    case 2: // _get_preserve_labels_source_label
        if (lh_truth(le_compare(o.new_label, A(S("colormap"), "background_value"), Py_EQ))) {
            if (!S("_prev_selected_label").is_none()) return S("_prev_selected_label");
            return S("selected_label");
        }
        return A(S("colormap"), "background_value");
    case 3: { // _refresh_caches_from_region
        if (!lh_truth(A(S("_slicing_state"), "loaded")) || lh_truth(A(S("_slice"), "empty"))) return nb::none();
        if (lh_truth(lh_call(G("isinstance"), {LX(S("data")), LX(N("ndarray"))})) &&
            lh_truth(lh_call(N("shares_memory"), {LX(S("data")), LX(A(A(S("_slice"), "image"), "view"))}))) return nb::none();
        lh_assign(o.update_slices, lh_call(S("_get_update_slices"), {LX(o.slice_key)}));
        if (o.update_slices.is_none()) return nb::none();
        auto parts = le_unpack_two(o.update_slices);
        lh_assign(o.region_slices, std::move(parts[0])); lh_assign(o.view_slices, std::move(parts[1]));
        lh_assign(o.visible_data, lh_index(LX(o.region_data), LX(lh_call(G("tuple"), {LX(o.region_slices)}))));
        lh_assign(o.visible_mask, lh_index(LX(o.mask), LX(lh_call(G("tuple"), {LX(o.region_slices)}))));
        parts = le_unpack_two(lh_call(S("_align_data_to_view"), {LX(o.visible_data), LX(o.visible_mask)}));
        lh_assign(o.visible_data, std::move(parts[0])); lh_assign(o.visible_mask, std::move(parts[1]));
        if (!(lh_truth(lh_call(G("isinstance"), {LX(S("data")), LX(N("ndarray"))})) &&
              lh_truth(lh_call(N("shares_memory"), {LX(S("data")), LX(A(A(S("_slice"), "image"), "raw"))}))))
            lh_set_item(LX(o.visible_data), LX(A(A(S("_slice"), "image"), "raw")),
                        LX(lh_call(G("tuple"), {LX(o.view_slices)})));
        if (lh_truth(le_compare(S("contour"), nb::int_(0), Py_GT))) return nb::none();
        lh_assign(o.new_color, lh_item(lh_call(A(S("colormap"), "_data_to_texture"), {
            LX(lh_call(N("array"), {LX(([&]() {
                nb::object list = lh_check(PyList_New(1));
                PyList_SET_ITEM(list.ptr(), 0, o.new_label.inc_ref().ptr()); return list;
            })())}, {LX(A(o.visible_data, "dtype"))}, K("dtype")))}), nb::int_(0)));
        if (lh_truth(lh_call(A(o.visible_mask, "all"))))
            lh_set_item(LX(o.new_color), LX(A(A(S("_slice"), "image"), "view")),
                        LX(lh_call(G("tuple"), {LX(o.view_slices)})));
        else
            lh_set_item(LX(o.new_color), LX(lh_index(LX(A(A(S("_slice"), "image"), "view")),
                        LX(lh_call(G("tuple"), {LX(o.view_slices)})))), LX(o.visible_mask));
        return nb::none();
    }
    case 4: { // _get_update_slices
        lh_assign(o.displayed_dims, A(S("_slice_input"), "displayed"));
        lh_assign(o.pt_not_disp, lh_call(S("_get_pt_not_disp")));
        nb::object element = lh_call(G("slice"), {LX(nb::none())});
        nb::object list = lh_check(PyList_New(1)); PyList_SET_ITEM(list.ptr(), 0, element.release().ptr());
        lh_assign(o.region_slices, le_binary(std::move(list), S("ndim"), 2));
        element = lh_call(G("slice"), {LX(nb::none())});
        list = lh_check(PyList_New(1)); PyList_SET_ITEM(list.ptr(), 0, element.release().ptr());
        lh_assign(o.view_slices, le_binary(std::move(list), lh_call(G("len"), {LX(o.displayed_dims)}), 2));
        nb::object iterator = lh_iter(lh_call(G("range"), {LX(S("ndim"))}));
        for (;;) {
            nb::object next = lh_next(iterator); if (!next.is_valid()) break;
            lh_assign(o.d, std::move(next)); lh_assign(o.axis_slice, lh_item(o.slice_key, o.d));
            if (le_contains(o.displayed_dims, o.d))
                lh_set_item(LX(o.axis_slice), LX(o.view_slices), LX(lh_call(A(o.displayed_dims, "index"), {LX(o.d)})));
            else {
                lh_assign(o.current_pos, lh_item(o.pt_not_disp, o.d));
                nb::object middle = o.current_pos;
                nb::object first = le_compare(A(o.axis_slice, "start"), middle, Py_LE);
                if (!lh_truth(std::move(first)) || !lh_truth(le_compare(std::move(middle), A(o.axis_slice, "stop"), Py_LT))) return nb::none();
                lh_set_item(LX(lh_call(G("int"), {LX(le_binary(o.current_pos, A(o.axis_slice, "start"), 1))})), LX(o.region_slices), LX(o.d));
            }
        }
        iterator.reset();
        nb::object result = lh_check(PyTuple_New(2));
        PyTuple_SET_ITEM(result.ptr(), 0, o.region_slices.inc_ref().ptr());
        PyTuple_SET_ITEM(result.ptr(), 1, o.view_slices.inc_ref().ptr());
        return result;
    }
    case 5: // _align_data_to_view
        lh_assign(o.displayed_dims, A(S("_slice_input"), "displayed"));
        lh_assign(o.sorted_dims, lh_call(G("sorted"), {LX(o.displayed_dims)}));
        if (lh_truth(le_compare(lh_call(G("list"), {LX(o.displayed_dims)}), o.sorted_dims, Py_NE))) {
            le_comp(o, 0, lh_iter(o.displayed_dims), o.perm);
            lh_assign(o.visible_data, lh_call(N("transpose"), {LX(o.visible_data), LX(o.perm)}));
            lh_assign(o.visible_mask, lh_call(N("transpose"), {LX(o.visible_mask), LX(o.perm)}));
        }
        { nb::object result = lh_check(PyTuple_New(2));
          PyTuple_SET_ITEM(result.ptr(), 0, o.visible_data.inc_ref().ptr());
          PyTuple_SET_ITEM(result.ptr(), 1, o.visible_mask.inc_ref().ptr()); return result; }
    case 6: // _get_pt_not_disp
        lh_assign(o.slice_input, A(S("_slice"), "slice_input"));
        lh_assign(o.point, lh_call(A(lh_call(N("round"), {
            LX(lh_call(S("world_to_data"), {LX(A(A(o.slice_input, "world_slice"), "point"))}))}), "astype"), {LX(G("int"))}));
        le_comp(o, 1, lh_iter(A(o.slice_input, "not_displayed")), o.pt_not_disp);
        return o.pt_not_disp;
    case 7: // _expand_updated_slice_for_contour
        if (lh_truth(le_compare(S("contour"), nb::int_(1), Py_LT))) return o.updated_slice;
        lh_assign(o.contour_offset, lh_call(G("max"), {LX(nb::int_(1)), LX(lh_call(G("int"), {LX(S("contour"))}))}));
        return lh_call(G("expand_slice"), {LX(o.updated_slice), LX(A(S("data"), "shape")), LX(o.contour_offset)});
    case 8: { // _accumulate_updated_slice
        lh_assign(o.updated_slice, lh_call(S("_expand_updated_slice_for_contour"), {LX(o.updated_slice)}));
        if (S("_updated_slice").is_none()) { set("_updated_slice", o.updated_slice); return nb::none(); }
        nb::object tuple = G("tuple"), function = le_expression_prepare(o, 1);
        nb::object iterator = lh_iter(lh_call(G("zip"), {LX(S("_updated_slice")), LX(o.updated_slice)}, {LX(nb::bool_(true))}, K("strict")));
        set("_updated_slice", lh_call(std::move(tuple), {LX(le_expression_start(std::move(function), std::move(iterator)))}));
        return nb::none();
    }
    case 9: { // _partial_labels_refresh
        if (S("_updated_slice").is_none() || !lh_truth(A(S("_slicing_state"), "loaded"))) return nb::none();
        lh_assign(o.dims_displayed, A(S("_slice_input"), "displayed"));
        lh_assign(o.raw_displayed, A(A(S("_slice"), "image"), "raw"));
        nb::object tuple = G("tuple"), function = le_expression_prepare(o, 2, o.self);
        nb::object iterator = lh_iter(o.dims_displayed);
        lh_assign(o.updated_slice, lh_call(std::move(tuple), {LX(le_expression_start(std::move(function), std::move(iterator)))}));
        le_comp(o, 2, lh_iter(o.updated_slice), o.offset);
        if (lh_truth(le_compare(S("contour"), nb::int_(0), Py_GT)))
            lh_assign(o.colors_sliced, lh_call(S("_raw_to_displayed"), {LX(o.raw_displayed)}, {LX(o.updated_slice)}, K("data_slice")));
        else lh_assign(o.colors_sliced, lh_index(LX(A(A(S("_slice"), "image"), "view")), LX(o.updated_slice)));
        lh_set_item(LX(o.colors_sliced), LX(A(A(S("_slice"), "image"), "view")), LX(o.updated_slice));
        lh_call(A(S("events"), "labels_update"), {}, {LX(o.colors_sliced), LX(o.offset)}, K("data,offset"));
        set("_updated_slice", nb::none());
        return nb::none();
    }
    case 10: { // data_setitem
        lh_assign(o.changed_indices, le_compare(lh_index(LX(S("data")), LX(o.indices)), o.value, Py_NE));
        nb::object tuple = G("tuple"), function = le_expression_prepare(o, 3, o.changed_indices);
        nb::object iterator = lh_iter(o.indices);
        lh_assign(o.indices, lh_call(std::move(tuple), {LX(le_expression_start(std::move(function), std::move(iterator)))}));
        if (lh_truth(lh_call(G("isinstance"), {LX(o.value), LX(G("Sequence"))})))
            lh_assign(o.value, lh_call(N("asarray"), {LX(o.value)}, {LX(A(A(A(S("_slice"), "image"), "raw"), "dtype"))}, K("dtype")));
        else lh_assign(o.value, lh_call(A(A(A(A(S("_slice"), "image"), "raw"), "dtype"), "type"), {LX(o.value)}));
        if (lh_truth(lh_call(G("isinstance"), {LX(o.value), LX(N("ndarray"))}))) lh_assign(o.value, lh_item(o.value, o.changed_indices));
        if (!lh_truth(o.indices) || lh_truth(le_compare(A(lh_item(o.indices, nb::int_(0)), "size"), nb::int_(0), Py_EQ))) return nb::none();
        lh_call(S("_save_history"), {LX(([&]() {
            nb::object result = lh_check(PyTuple_New(3));
            PyTuple_SET_ITEM(result.ptr(), 0, o.indices.inc_ref().ptr());
            PyTuple_SET_ITEM(result.ptr(), 1, lh_call(N("array"), {LX(lh_index(LX(S("data")), LX(o.indices)))}, {LX(nb::bool_(true))}, K("copy")).release().ptr());
            PyTuple_SET_ITEM(result.ptr(), 2, o.value.inc_ref().ptr()); return result;
        })())});
        lh_set_item(LX(o.value), LX(S("data")), LX(o.indices));
        lh_assign(o.pt_not_disp, lh_call(S("_get_pt_not_disp")));
        lh_assign(o.displayed_indices, lh_call(G("index_in_slice"), {LX(o.indices), LX(o.pt_not_disp), LX(A(A(S("_slice"), "slice_input"), "order"))}));
        if (lh_truth(lh_call(G("isinstance"), {LX(o.value), LX(N("ndarray"))})))
            lh_assign(o.visible_values, lh_index(LX(o.value), LX(lh_call(G("elements_in_slice"), {LX(o.indices), LX(o.pt_not_disp)}))));
        else lh_assign(o.visible_values, o.value);
        if (!(lh_truth(lh_call(G("isinstance"), {LX(S("data")), LX(N("ndarray"))})) ||
              lh_truth(lh_call(G("isinstance"), {LX(lh_call(G("getattr"), {LX(S("data")), LX(le_literal(o.source_metadata, "data")), LX(nb::none())})), LX(N("ndarray"))}))))
            lh_set_item(LX(o.visible_values), LX(A(A(S("_slice"), "image"), "raw")), LX(o.displayed_indices));
        if (!lh_truth(lh_call(G("isinstance"), {LX(S("data")), LX(N("ndarray"))}))) le_comp(o, 3, lh_iter(o.indices), o.indices);
        tuple = G("tuple"); function = le_expression_prepare(o, 4); iterator = lh_iter(o.indices);
        lh_assign(o.updated_slice, lh_call(std::move(tuple), {LX(le_expression_start(std::move(function), std::move(iterator)))}));
        if (lh_truth(le_compare(S("contour"), nb::int_(0), Py_EQ)))
            lh_set_item(LX(lh_call(A(S("colormap"), "_data_to_texture"), {LX(o.visible_values)})),
                        LX(A(A(S("_slice"), "image"), "view")), LX(o.displayed_indices));
        lh_call(S("_accumulate_updated_slice"), {LX(o.updated_slice)});
        if (o.refresh.ptr() == Py_True) lh_call(S("_partial_labels_refresh"));
        return nb::none();
    }
    case 11: // elements_in_slice
        le_comp(o, 4, lh_iter(lh_call(A(o.position_in_axes, "items"))), o.queries);
        return lh_call(A(N("logical_and"), "reduce"), {LX(o.queries)}, {LX(nb::int_(0))}, K("axis"));
    case 12: { // index_in_slice
        lh_assign(o.filtered_index, lh_call(G("elements_in_slice"), {LX(o.index), LX(o.position_in_axes)}));
        nb::object tuple = G("tuple"), function = le_expression_prepare(o, 5, o.index, o.position_in_axes, o.filtered_index);
        nb::object iterator = lh_iter(o.indices_order);
        return lh_call(std::move(tuple), {LX(le_expression_start(std::move(function), std::move(iterator)))});
    }
    default: throw nb::value_error("unknown Labels edit control");
    }
}

// Two genuine module declaration owners. Shells/profiles are inert transport
// metadata. No original application CodeType is retained after admission.
struct LabelsEditBinding {
    nb::object defining_globals, shells, profiles, frame;
    bool bound = false;
    LabelsEditBinding() = default;
    LabelsEditBinding(LabelsEditBinding&&) = default;
    void clear() { defining_globals.reset(); shells.reset(); profiles.reset(); frame.reset(); }
    ~LabelsEditBinding() { clear(); }
};
int le_locals_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) {
        const auto& o = *nb::inst_ptr<LabelsEditLocals>(self);
#define LE_VISIT(name) Py_VISIT(o.name.ptr());
        LE_LOCAL_FIELDS(LE_VISIT)
#undef LE_VISIT
    }
    return 0;
}
int le_locals_clear(PyObject* self) {
    if (nb::inst_ready(self)) nb::inst_ptr<LabelsEditLocals>(self)->clear();
    return 0;
}
int le_expression_locals_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) {
        const auto& o = *nb::inst_ptr<LabelsEditExpressionLocals>(self);
#define LE_EX_VISIT(name) Py_VISIT(o.name.ptr());
        LE_EXPRESSION_FIELDS(LE_EX_VISIT)
#undef LE_EX_VISIT
    }
    return 0;
}
int le_expression_locals_clear(PyObject* self) {
    if (nb::inst_ready(self)) nb::inst_ptr<LabelsEditExpressionLocals>(self)->clear();
    return 0;
}
int le_expression_generator_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) Py_VISIT(nb::inst_ptr<LabelsEditExpressionGenerator>(self)->context.ptr());
    return 0;
}
int le_expression_generator_clear(PyObject* self) {
    if (nb::inst_ready(self)) nb::inst_ptr<LabelsEditExpressionGenerator>(self)->clear();
    return 0;
}
int le_binding_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) {
        const auto& o = *nb::inst_ptr<LabelsEditBinding>(self);
        Py_VISIT(o.defining_globals.ptr()); Py_VISIT(o.shells.ptr());
        Py_VISIT(o.profiles.ptr()); Py_VISIT(o.frame.ptr());
    }
    return 0;
}
int le_binding_clear(PyObject* self) {
    if (nb::inst_ready(self)) nb::inst_ptr<LabelsEditBinding>(self)->clear();
    return 0;
}
PyType_Slot le_locals_slots[] = {{Py_tp_traverse, reinterpret_cast<void*>(le_locals_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(le_locals_clear)}, {0, nullptr}};
PyType_Slot le_expression_locals_slots[] = {{Py_tp_traverse, reinterpret_cast<void*>(le_expression_locals_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(le_expression_locals_clear)}, {0, nullptr}};
PyType_Slot le_expression_generator_slots[] = {{Py_tp_traverse, reinterpret_cast<void*>(le_expression_generator_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(le_expression_generator_clear)}, {0, nullptr}};
PyType_Slot le_binding_slots[] = {{Py_tp_traverse, reinterpret_cast<void*>(le_binding_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(le_binding_clear)}, {0, nullptr}};

nb::object le_find_name(nb::handle metadata, const char* text) {
    PyObject* names = lh_metadata_values(metadata, 1);
    for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(names); ++i) {
        PyObject* value = PyTuple_GET_ITEM(names, i);
        if (PyUnicode_CheckExact(value) && PyUnicode_CompareWithASCIIString(value, text) == 0)
            return nb::borrow<nb::object>(value);
    }
    if (PyErr_Occurred()) throw nb::python_error();
    PyObject* constants = lh_metadata_values(metadata, 0);
    for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(constants); ++i) {
        PyObject* child = PyTuple_GET_ITEM(constants, i);
        if (PyTuple_CheckExact(child) && PyTuple_GET_SIZE(child) == 2 &&
            PyTuple_CheckExact(PyTuple_GET_ITEM(child, 0)) && PyTuple_CheckExact(PyTuple_GET_ITEM(child, 1))) {
            nb::object found = le_find_name(nb::handle(child), text);
            if (found.is_valid()) return found;
        }
    }
    return {};
}
nb::object le_name(nb::handle metadata, const char* text) {
    nb::object result = le_find_name(metadata, text);
    if (!result.is_valid()) throw nb::type_error("Labels edit original source name metadata is missing");
    return result;
}
nb::object le_literal(nb::handle metadata, const char* text) {
    PyObject* constants = lh_metadata_values(metadata, 0);
    for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(constants); ++i) {
        PyObject* value = PyTuple_GET_ITEM(constants, i);
        if (PyUnicode_CheckExact(value) && PyUnicode_CompareWithASCIIString(value, text) == 0)
            return nb::borrow<nb::object>(value);
    }
    if (PyErr_Occurred()) throw nb::python_error();
    throw nb::type_error("Labels edit original source literal is missing");
}
nb::object le_keyword(nb::handle metadata, const char* text) {
    PyObject* constants = lh_metadata_values(metadata, 0);
    for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(constants); ++i) {
        PyObject* value = PyTuple_GET_ITEM(constants, i);
        if (!PyTuple_CheckExact(value)) continue;
        std::string joined;
        bool strings = true;
        for (Py_ssize_t j = 0; j < PyTuple_GET_SIZE(value); ++j) {
            PyObject* word = PyTuple_GET_ITEM(value, j);
            if (!PyUnicode_CheckExact(word)) { strings = false; break; }
            const char* spelling = PyUnicode_AsUTF8(word);
            if (!spelling) throw nb::python_error();
            if (j) joined += ',';
            joined += spelling;
        }
        if (strings && joined == text) return nb::borrow<nb::object>(value);
    }
    if (PyErr_Occurred()) throw nb::python_error();
    throw nb::type_error("Labels edit original keyword tuple is missing");
}
nb::object le_metadata(PyCodeObject* original) {
    // Child code is replaced by non-executable metadata at the same constant
    // position. Strings, number literals and keyword tuples remain identical.
    nb::object constants = lh_check(PyTuple_New(PyTuple_GET_SIZE(original->co_consts)));
    for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(original->co_consts); ++i) {
        PyObject* child = PyTuple_GET_ITEM(original->co_consts, i);
        nb::object value = PyCode_Check(child) ?
            le_metadata(reinterpret_cast<PyCodeObject*>(child)) : nb::borrow<nb::object>(child);
        PyTuple_SET_ITEM(constants.ptr(), i, value.release().ptr());
    }
    nb::object result = lh_check(PyTuple_New(2));
    PyTuple_SET_ITEM(result.ptr(), 0, constants.release().ptr());
    PyTuple_SET_ITEM(result.ptr(), 1, Py_NewRef(original->co_names));
    return result;
}
nb::object le_child_metadata(nb::handle metadata) {
    PyObject* constants = lh_metadata_values(metadata, 0);
    for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(constants); ++i) {
        PyObject* child = PyTuple_GET_ITEM(constants, i);
        if (PyTuple_CheckExact(child) && PyTuple_GET_SIZE(child) == 2 &&
            PyTuple_CheckExact(PyTuple_GET_ITEM(child, 0)) &&
            PyTuple_CheckExact(PyTuple_GET_ITEM(child, 1)))
            return nb::borrow<nb::object>(child);
    }
    throw nb::type_error("Labels edit original expression metadata is missing");
}
nb::object le_expression_prepare(LabelsEditLocals& parent, int entry,
                                nb::object capture1, nb::object capture2, nb::object capture3) {
    LabelsEditExpressionLocals expression;
    expression.entry = entry;
    expression.source_globals = parent.source_globals;
    expression.source_builtins = parent.source_builtins;
    // data_setitem contains two genexprs. Choose by the inert constant slot,
    // rather than retaining either original executable CodeType.
    PyObject* constants = lh_metadata_values(parent.source_metadata, 0);
    int ordinal = parent.entry == 10 && entry == 4 ? 1 : 0;
#if PY_MINOR_VERSION == 11
    // The intervening list comprehension is a separate compiler constant on
    // 3.11; 3.12+ inline it. Both spellings have independently frozen profiles.
    if (parent.entry == 10 && entry == 4) ordinal = 2;
#endif
    for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(constants); ++i) {
        PyObject* child = PyTuple_GET_ITEM(constants, i);
        if (PyTuple_CheckExact(child) && PyTuple_GET_SIZE(child) == 2 &&
            PyTuple_CheckExact(PyTuple_GET_ITEM(child, 0)) &&
            PyTuple_CheckExact(PyTuple_GET_ITEM(child, 1))) {
            if (ordinal-- == 0) { expression.source_metadata = nb::borrow<nb::object>(child); break; }
        }
    }
    if (!expression.source_metadata.is_valid()) throw nb::type_error("Labels edit expression metadata is missing");
    expression.capture1 = std::move(capture1);
    expression.capture2 = std::move(capture2);
    expression.capture3 = std::move(capture3);
    return nb::cast(std::move(expression));
}
nb::object le_expression_start(nb::object function, nb::object iterator) {
    auto& context = *nb::inst_ptr<LabelsEditExpressionLocals>(function.ptr());
    context.iterator = std::move(iterator);
    LabelsEditExpressionGenerator generator;
    generator.context = std::move(function);
    return nb::cast(std::move(generator));
}

PyObject* le_frame(PyObject*, PyObject* arguments) {
    try {
        const Py_ssize_t count = PyTuple_GET_SIZE(arguments);
        if (count == 1) {
            nb::handle owner(PyTuple_GET_ITEM(arguments, 0));
            if (!nb::isinstance<LabelsEditLocals>(owner) || !nb::inst_ready(owner.ptr()))
                throw nb::type_error("Labels edit actual parameter owner is required");
            return le_run(*nb::inst_ptr<LabelsEditLocals>(owner.ptr())).release().ptr();
        }
        if (count < 2 || !PyLong_CheckExact(PyTuple_GET_ITEM(arguments, 0)))
            throw nb::type_error("Labels edit source entry and parameters are required");
        long entry = PyLong_AsLong(PyTuple_GET_ITEM(arguments, 0));
        if (entry == -1 && PyErr_Occurred()) throw nb::python_error();
        const int sizes[] = {7, 5, 2, 5, 2, 3, 1, 2, 2, 1, 4, 2, 3};
        if (entry < 0 || entry > 12 || count != sizes[entry] + 1)
            throw nb::type_error("Labels edit parameter declaration changed");
        PyFrameObject* frame = PyEval_GetFrame();
        if (!frame) throw nb::type_error("Labels edit declaration source frame is required");
        nb::object code = lh_check(reinterpret_cast<PyObject*>(PyFrame_GetCode(frame)));
        PyObject* constants = reinterpret_cast<PyCodeObject*>(code.ptr())->co_consts;
        PyObject* metadata = PyTuple_GET_ITEM(constants, PyTuple_GET_SIZE(constants) - 1);
        LabelsEditLocals o;
        o.entry = static_cast<int>(entry);
        o.source_globals = lh_check(PyFrame_GetGlobals(frame));
        o.source_builtins = lh_check(PyFrame_GetBuiltins(frame));
        o.source_metadata = nb::borrow<nb::object>(metadata);
        lh_metadata_values(o.source_metadata, 0); lh_metadata_values(o.source_metadata, 1);
        auto p = [&](Py_ssize_t index) { return nb::borrow<nb::object>(PyTuple_GET_ITEM(arguments, index + 1)); };
        switch (entry) {
        case 0: o.self=p(0); o.slice_key=p(1); o.mask=p(2); o.new_label=p(3); o.dims_to_paint=p(4); o.refresh=p(5); o.region_data=p(6); break;
        case 1: o.self=p(0); o.data=p(1); o.mask=p(2); o.new_label=p(3); o.volume_slice=p(4); break;
        case 2: o.self=p(0); o.new_label=p(1); break;
        case 3: o.self=p(0); o.region_data=p(1); o.slice_key=p(2); o.mask=p(3); o.new_label=p(4); break;
        case 4: o.self=p(0); o.slice_key=p(1); break;
        case 5: o.self=p(0); o.visible_data=p(1); o.visible_mask=p(2); break;
        case 6: case 9: o.self=p(0); break;
        case 7: case 8: o.self=p(0); o.updated_slice=p(1); break;
        case 10: o.self=p(0); o.indices=p(1); o.value=p(2); o.refresh=p(3); break;
        case 11: o.index=p(0); o.position_in_axes=p(1); break;
        case 12: o.index=p(0); o.position_in_axes=p(1); o.indices_order=p(2); break;
        }
        return nb::cast(std::move(o)).release().ptr();
    } catch (...) { lh_scope_error(); return nullptr; }
}
PyMethodDef le_frame_definition = {"_labels_edit_source_frame", le_frame, METH_VARARGS, nullptr};

nb::object le_shell_code(LabelsEditBinding& binding, int entry, nb::handle function) {
    PyObject* shell = PyTuple_GET_ITEM(binding.shells.ptr(), entry);
    nb::object code = lh_check(PyObject_GetAttrString(shell, "__code__"));
    nb::object constants = lh_check(PyObject_GetAttrString(code.ptr(), "co_consts"));
    auto* original = reinterpret_cast<PyCodeObject*>(PyFunction_GetCode(function.ptr()));
    nb::object replacement = lh_check(PyTuple_New(PyTuple_GET_SIZE(constants.ptr()) + 1));
    bool marker = false;
    for (Py_ssize_t i=0; i<PyTuple_GET_SIZE(constants.ptr()); ++i) {
        PyObject* value = PyTuple_GET_ITEM(constants.ptr(), i);
        if (value == Py_Ellipsis) { value = binding.frame.ptr(); marker = true; }
        PyTuple_SET_ITEM(replacement.ptr(), i, Py_NewRef(value));
    }
    if (!marker) throw nb::type_error("Labels edit declaration transport marker is missing");
    // CodeType constants are not traversed by cyclic GC on supported Python
    // versions. Store only inert source literals here: never an owner, globals,
    // callable with owner self, carrier, original code or other mutable root.
    PyTuple_SET_ITEM(replacement.ptr(), PyTuple_GET_SIZE(constants.ptr()), le_metadata(original).release().ptr());
    nb::dict kwargs;
    kwargs["co_consts"] = replacement;
    kwargs["co_filename"] = nb::borrow<nb::object>(original->co_filename);
    kwargs["co_name"] = nb::borrow<nb::object>(original->co_name);
    kwargs["co_qualname"] = nb::borrow<nb::object>(original->co_qualname);
    kwargs["co_firstlineno"] = nb::int_(original->co_firstlineno);
    kwargs["co_flags"] = nb::int_(original->co_flags);
    nb::object args = lh_check(PyTuple_New(0));
    nb::object replace = lh_check(PyObject_GetAttrString(code.ptr(), "replace"));
    return lh_check(PyObject_Call(replace.ptr(), args.ptr(), kwargs.ptr()));
}
const char* le_method_names[] = {"_paint_region_with_mask", "_apply_mask_to_data",
    "_get_preserve_labels_source_label", "_refresh_caches_from_region", "_get_update_slices",
    "_align_data_to_view", "_get_pt_not_disp", "_expand_updated_slice_for_contour",
    "_accumulate_updated_slice", "_partial_labels_refresh", "data_setitem",
    "elements_in_slice", "index_in_slice"};
PyObject* le_capture_context(PyObject*, PyObject*) {
    try {
        PyFrameObject* frame = PyEval_GetFrame();
        if (!frame) throw nb::type_error("Labels edit actual module declaration frame is required");
        nb::object locals = lh_check(PyFrame_GetLocals(frame));
        LabelsEditBinding binding;
        binding.defining_globals = lh_check(PyFrame_GetGlobals(frame));
        if (locals.ptr() != binding.defining_globals.ptr())
            throw nb::type_error("Labels edit declaration context must be a genuine module frame");
        return nb::cast(std::move(binding)).release().ptr();
    } catch (...) { lh_scope_error(); return nullptr; }
}
PyMethodDef le_context_definition = {"labels_edit_context", le_capture_context, METH_NOARGS, nullptr};
nb::object labels_edit_bind(nb::object subject, nb::object owner, nb::tuple shells, nb::tuple profiles, bool indexing) {
    if (!nb::isinstance<LabelsEditBinding>(owner) || !nb::inst_ready(owner.ptr()) || PyTuple_GET_SIZE(shells.ptr()) != 13)
        throw nb::type_error("Labels edit declaration owner and thirteen shells are required");
    auto& binding = *nb::inst_ptr<LabelsEditBinding>(owner.ptr());
    if (binding.bound || !binding.defining_globals.is_valid()) throw nb::type_error("Labels edit declaration owner cannot be reused");
    PyFrameObject* frame = PyEval_GetFrame();
    if (!frame) throw nb::type_error("Labels edit actual binding module frame is required");
    nb::object actual_globals = lh_check(PyFrame_GetGlobals(frame));
    nb::object actual_locals = lh_check(PyFrame_GetLocals(frame));
    if (actual_globals.ptr() != binding.defining_globals.ptr() || actual_locals.ptr() != actual_globals.ptr())
        throw nb::type_error("Labels edit binding must run in its genuine defining module frame");
    if (indexing) {
        if (subject.ptr() != binding.defining_globals.ptr()) throw nb::type_error("Labels edit actual indexing namespace is required");
    } else {
        if (!PyType_Check(subject.ptr()) || PyDict_GetItemString(binding.defining_globals.ptr(), "Labels") != subject.ptr())
            throw nb::type_error("Labels edit actual defining class is required");
    }
    nb::object rows;
    for (Py_ssize_t i=0; i<PyTuple_GET_SIZE(profiles.ptr()); ++i) {
        PyObject* row = PyTuple_GET_ITEM(profiles.ptr(), i);
        if (!PyTuple_CheckExact(row) || PyTuple_GET_SIZE(row) != 2) throw nb::type_error("Labels edit version profiles changed");
        long minor = PyLong_AsLong(PyTuple_GET_ITEM(row, 0));
        if (minor == -1 && PyErr_Occurred()) throw nb::python_error();
        if (minor == PY_MINOR_VERSION) rows = nb::borrow<nb::object>(PyTuple_GET_ITEM(row, 1));
    }
    if (!rows.is_valid() || !PyTuple_CheckExact(rows.ptr()) || PyTuple_GET_SIZE(rows.ptr()) != 13)
        throw nb::value_error("Labels edit has no reviewed thirteen-function interpreter profile");
    for (Py_ssize_t i=0; i<13; ++i) if (!PyFunction_Check(PyTuple_GET_ITEM(shells.ptr(), i)))
        throw nb::type_error("Labels edit transport shells must be genuine functions");
    binding.shells = std::move(shells); binding.profiles = std::move(profiles);
    nb::object module_name = nb::str("naparixx._native");
    binding.frame = lh_check(PyCFunction_NewEx(&le_frame_definition, nullptr, module_name.ptr()));
    std::vector<std::pair<nb::object, nb::object>> changes;
    for (int entry = indexing ? 11 : 0; entry < (indexing ? 13 : 11); ++entry) {
        nb::object function = indexing ? lh_check(PyObject_GetItem(subject.ptr(), nb::str(le_method_names[entry]).ptr())) :
            lh_check(PyObject_GetAttrString(subject.ptr(), le_method_names[entry]));
        if (!PyFunction_Check(function.ptr()) || PyFunction_GetGlobals(function.ptr()) != binding.defining_globals.ptr()) continue;
        if (!lh_profile_code(nb::handle(PyFunction_GetCode(function.ptr())), nb::handle(PyTuple_GET_ITEM(rows.ptr(), entry)))) continue;
        changes.emplace_back(function, le_shell_code(binding, entry, function));
    }
    // The complete set of replacement codes is validated before the first
    // function mutation. Foreign overrides retain their code and metadata.
    for (auto& change : changes) if (PyObject_SetAttrString(change.first.ptr(), "__code__", change.second.ptr()) < 0)
        throw nb::python_error();
    binding.bound = true;
    return owner;
}
void bind_labels_edit_exports(nb::module_& module) {
    auto locals = nb::class_<LabelsEditLocals>(module, "LabelsEditLocals",
        nb::is_weak_referenceable(), nb::type_slots(le_locals_slots));
#define LE_PROPERTY(name) locals.def_prop_ro(#name, [](const LabelsEditLocals& o) { return lh_optional(o.name); });
    LE_LOCAL_FIELDS(LE_PROPERTY)
#undef LE_PROPERTY
    auto expressions = nb::class_<LabelsEditExpressionLocals>(module, "LabelsEditExpressionLocals",
        nb::is_weak_referenceable(), nb::type_slots(le_expression_locals_slots));
#define LE_EX_PROPERTY(name) expressions.def_prop_ro(#name, [](const LabelsEditExpressionLocals& o) { return lh_optional(o.name); });
    LE_EXPRESSION_FIELDS(LE_EX_PROPERTY)
#undef LE_EX_PROPERTY
    auto generator = nb::class_<LabelsEditExpressionGenerator>(module, "LabelsEditExpressionGenerator",
        nb::is_weak_referenceable(), nb::type_slots(le_expression_generator_slots));
    generator.def("__iter__", [](nb::object self) { return self; });
    generator.def("__next__", [](LabelsEditExpressionGenerator& o) { return le_expression_send(o, nb::none()); });
    generator.def("send", &le_expression_send, nb::arg("value").none());
    generator.def("throw", &le_expression_throw);
    generator.def("close", &le_expression_close);
    auto binding = nb::class_<LabelsEditBinding>(module, "LabelsEditBinding", nb::is_weak_referenceable(), nb::type_slots(le_binding_slots));
    binding.def_prop_ro("defining_globals", [](const LabelsEditBinding& o) { return lh_optional(o.defining_globals); });
    nb::object module_name = nb::str("naparixx._native");
    module.attr("labels_edit_context") = lh_check(PyCFunction_NewEx(&le_context_definition, nullptr, module_name.ptr()));
    module.def("labels_edit_bind", &labels_edit_bind, nb::arg("subject"), nb::arg("owner"),
        nb::arg("shells"), nb::arg("profiles"), nb::arg("indexing"));
}
#undef LE_EXPRESSION_FIELDS
#undef LE_LOCAL_FIELDS

#undef LX
#undef LH_FIELDS

} // namespace

Output<double> interpolate_coordinates(Input<double> old_coord, Input<double> new_coord,
                                       double brush_size) {
    if (new_coord.ndim() != 1 || !new_coord.shape(0))
        throw std::invalid_argument("coordinates must be nonempty vectors");
    const size_t dims = new_coord.shape(0);
    require_vector(old_coord, dims, "old and new coordinates must have equal length");
    if (!(brush_size > 0) || !std::isfinite(brush_size))
        throw std::invalid_argument("brush size must be finite and positive");
    double distance = 0;
    for (size_t c = 0; c < dims; ++c) {
        if (!std::isfinite(at(old_coord, c)) || !std::isfinite(at(new_coord, c)))
            throw std::invalid_argument("coordinates must be finite");
        distance = std::max(distance, std::abs(at(new_coord, c) - at(old_coord, c)));
    }
    const double steps_value = round_even(distance / brush_size * 4);
    if (!std::isfinite(steps_value) || steps_value >= static_cast<double>(std::numeric_limits<int64_t>::max()))
        throw std::overflow_error("brush interpolation requires too many points");
    const size_t steps = static_cast<size_t>(steps_value);
    const size_t rows = std::max<size_t>(steps, 1);
    auto out = allocate<double>({rows, dims});
    {
        nb::gil_scoped_release release;
        for (size_t c = 0; c < dims; ++c) {
            const double start = at(old_coord, c);
            const double end = at(new_coord, c);
            const double step = steps ? (end - start) / static_cast<double>(steps) : 0;
            for (size_t r = 0; r < rows; ++r)
                out.data()[r * dims + c] = steps ? start + (r + 1) * step : start;
            if (steps)
                out.data()[(rows - 1) * dims + c] = end;
        }
    }
    return out;
}

template <typename T>
nb::object first_nonzero_coordinate(Input<T> data, Input<double> start, Input<double> end) {
    const size_t dims = data.ndim();
    require_vector(start, dims, "ray start must match the data dimensions");
    require_vector(end, dims, "ray end must match the data dimensions");
    double length_sq = 0;
    std::vector<double> delta(dims);
    for (size_t c = 0; c < dims; ++c) {
        if (!data.shape(c))
            throw std::invalid_argument("cannot trace a ray through empty data");
        delta[c] = at(end, c) - at(start, c);
        length_sq += delta[c] * delta[c];
    }
    const double steps_value = round_even(std::sqrt(length_sq));
    if (!std::isfinite(steps_value) || steps_value >= static_cast<double>(std::numeric_limits<int64_t>::max()))
        throw std::invalid_argument("ray endpoints must be finite with representable length");
    const int64_t steps = static_cast<int64_t>(steps_value);
    std::vector<int64_t> coordinate(dims);
    bool found = false;
    {
        nb::gil_scoped_release release;
        for (int64_t step = 0; step <= steps; ++step) {
            int64_t offset = 0;
            for (size_t c = 0; c < dims; ++c) {
                double value = at(start, c);
                if (steps) {
                    value += static_cast<double>(step) * (delta[c] / static_cast<double>(steps));
                    if (step == steps) value = at(end, c);
                }
                value = std::clamp(round_even(value), 0.0, static_cast<double>(data.shape(c) - 1));
                coordinate[c] = static_cast<int64_t>(value);
                offset += coordinate[c] * data.stride(c);
            }
            if (read(data.data(), offset) != T(0)) {
                found = true;
                break;
            }
        }
    }
    if (!found) return nb::none();
    return from_vector(coordinate, {dims}).cast();
}

template <typename T>
void bind_ray(nb::module_& m) {
    m.def("first_nonzero_coordinate", &first_nonzero_coordinate<T>,
          nb::arg("data").noconvert(), nb::arg("start").noconvert(), nb::arg("end").noconvert());
}

void bind_labels(nb::module_& m) {
    m.def("interpolate_coordinates", &interpolate_coordinates,
          nb::arg("old_coord").noconvert(), nb::arg("new_coord").noconvert(), nb::arg("brush_size"));
    bind_ray<bool>(m);
    bind_ray<int8_t>(m); bind_ray<uint8_t>(m);
    bind_ray<int16_t>(m); bind_ray<uint16_t>(m);
    bind_ray<int32_t>(m); bind_ray<uint32_t>(m);
    bind_ray<int64_t>(m); bind_ray<uint64_t>(m);
    bind_ray<float>(m); bind_ray<double>(m);
    bind_labels_history_exports(m);
    bind_labels_edit_exports(m);
}
// Handwritten frontend application policy, staged for the complete50-body
// handoff. All persistent state is the genuine Layer/model/backend state.
// Source generation context contains inert metadata and actual G/B tables only.
#define LFF(value) [&]() -> nb::object { return (value); }
struct LFSource {
    nb::object globals, builtins, metadata;
    nb::object global(const char* name) const {
        return le_global(globals, builtins, metadata, name);
    }
    nb::object attr(nb::object value, const char* name) const {
        return le_attr(metadata, std::move(value), name);
    }
    nb::object keyword(const char* names) const { return le_keyword(metadata, names); }
};

std::vector<nb::object> lf_unpack(nb::object value, Py_ssize_t count) {
    PyObject* raw = PyObject_GetIter(value.ptr());
    if (!raw) {
        nb::python_error error;
        if (error.matches(PyExc_TypeError) && Py_TYPE(value.ptr())->tp_iter == nullptr &&
            !PySequence_Check(value.ptr())) {
            PyErr_Format(PyExc_TypeError, "cannot unpack non-iterable %.200s object", Py_TYPE(value.ptr())->tp_name);
            throw nb::python_error();
        }
        throw error;
    }
    nb::object iterator = nb::steal<nb::object>(raw);
    std::vector<nb::object> result;
    result.reserve(static_cast<size_t>(count));
    for (Py_ssize_t i = 0; i < count; ++i) {
        nb::object item = lh_next(iterator);
        if (!item.is_valid()) {
            PyErr_Format(PyExc_ValueError, "not enough values to unpack (expected %zd, got %zd)", count, i);
            throw nb::python_error();
        }
        result.push_back(std::move(item));
    }
    if (lh_next(iterator).is_valid()) {
        PyErr_Format(PyExc_ValueError, "too many values to unpack (expected %zd)", count);
        throw nb::python_error();
    }
    return result;
}

nb::object lf_round_coordinate_list(const LFSource& source, nb::object coord) {
    nb::object result = lh_check(PyList_New(0));
    nb::object iterator = lh_iter(coord);
    nb::object c;
    for (;;) {
        nb::object next = lh_next(iterator);
        if (!next.is_valid()) break;
        lh_assign(c, std::move(next));
        // int is resolved before round's callable and argument, just as the
        // source comprehension evaluates its nested call expression.
        nb::object integer = source.global("int");
        nb::object round = source.attr(source.global("np"), "round");
        nb::object rounded = lh_call(std::move(round), {LFF(c)});
        nb::object value = lh_call(std::move(integer), {LFF(rounded)});
        rounded.reset();
        if (PyList_Append(result.ptr(), value.ptr()) < 0) throw nb::python_error();
    }
    return result;
}

nb::object lf_paint(const LFSource& source, nb::object self, nb::object coord,
                    nb::object new_label, nb::object refresh) {
    auto S = [&](const char* name) { return source.attr(self, name); };
    lh_call(S("_validate_label_in_range"), {LFF(new_label)});
    auto shape_info = lf_unpack(lh_call(S("_get_shape_and_dims_to_paint")), 2);
    nb::object shape = shape_info[0], dims_to_paint = shape_info[1];
    shape_info.clear();
    nb::object slice_coord = lf_round_coordinate_list(source, coord);
    lh_call(S("_validate_non_painted_coord"), {LFF(slice_coord), LFF(dims_to_paint)});
    nb::object brush_info = lh_call(S("_get_brush_mask_and_bbox"),
        {LFF(slice_coord), LFF(dims_to_paint), LFF(shape)});
    if (brush_info.is_none()) return nb::none();
    auto parts = lf_unpack(brush_info, 3);
    nb::object mask = parts[0], min_vals = parts[1], max_vals = parts[2];
    parts.clear();
    nb::object slice_key = lh_call(S("_build_slice_key"),
        {LFF(slice_coord), LFF(dims_to_paint), LFF(min_vals), LFF(max_vals)});
    lh_call(S("_paint_region_with_mask"),
        {LFF(slice_key), LFF(mask), LFF(new_label), LFF(dims_to_paint), LFF(refresh)});
    return nb::none();
}

nb::object lf_fill(const LFSource& source, nb::object self, nb::object coord,
                   nb::object new_label, nb::object refresh) {
    auto S = [&](const char* name) { return source.attr(self, name); };
    lh_call(S("_validate_label_in_range"), {LFF(new_label)});
    auto shape_info = lf_unpack(lh_call(S("_get_shape_and_dims_to_paint")), 2);
    nb::object ignored = shape_info[0], dims_to_paint = shape_info[1];
    shape_info.clear();
    nb::object slice_coord = lf_round_coordinate_list(source, coord);
    lh_call(S("_validate_non_painted_coord"), {LFF(slice_coord), LFF(dims_to_paint)});
    nb::object fill_info = lh_call(S("_get_flood_mask_and_bbox"),
        {LFF(slice_coord), LFF(new_label), LFF(dims_to_paint)});
    if (fill_info.is_none()) return nb::none();
    auto parts = lf_unpack(fill_info, 4);
    nb::object mask = parts[0], min_vals = parts[1], max_vals = parts[2], region_data = parts[3];
    parts.clear();
    nb::object slice_key = lh_call(S("_build_slice_key"),
        {LFF(slice_coord), LFF(dims_to_paint), LFF(min_vals), LFF(max_vals)});
    lh_call(S("_paint_region_with_mask"),
        {LFF(slice_key), LFF(mask), LFF(new_label), LFF(dims_to_paint), LFF(refresh)},
        {LFF(region_data)}, source.keyword("region_data"));
    return nb::none();
}

nb::object lf_paint_polygon(const LFSource& source, nb::object self,
                            nb::object points, nb::object new_label) {
    auto S = [&](const char* name) { return source.attr(self, name); };
    lh_call(S("_validate_label_in_range"), {LFF(new_label)});
    auto shape_info = lf_unpack(lh_call(S("_get_shape_and_dims_to_paint")), 2);
    nb::object shape = shape_info[0], dims_to_paint = shape_info[1];
    shape_info.clear();
    points = lh_call(source.attr(source.global("np"), "array"),
        {LFF(points)}, {LFF(source.global("int"))}, source.keyword("dtype"));
    nb::object slice_coord = lh_call(source.attr(lh_item(points, nb::int_(0)), "tolist"));
    lh_call(S("_validate_non_painted_coord"), {LFF(slice_coord), LFF(dims_to_paint)});
    nb::object polygon_info = lh_call(S("_get_polygon_mask_and_bbox"),
        {LFF(points), LFF(dims_to_paint), LFF(shape)});
    if (polygon_info.is_none()) return nb::none();
    auto parts = lf_unpack(polygon_info, 3);
    nb::object mask = parts[0], min_vals = parts[1], max_vals = parts[2];
    parts.clear();
    nb::object slice_key = lh_call(S("_build_slice_key"),
        {LFF(slice_coord), LFF(dims_to_paint), LFF(min_vals), LFF(max_vals)});
    lh_call(S("_paint_region_with_mask"),
        {LFF(slice_key), LFF(mask), LFF(new_label), LFF(dims_to_paint)});
    return nb::none();
}

nb::object lf_draw(const LFSource& source, nb::object self, nb::object new_label,
                   nb::object last_cursor_coord, nb::object coordinates) {
    if (coordinates.is_none()) return nb::none();
    auto S = [&](const char* name) { return source.attr(self, name); };
    auto N = [&](const char* name) { return source.attr(source.global("np"), name); };
    nb::object interp_coord = lh_call(source.global("interpolate_coordinates"),
        {LFF(last_cursor_coord), LFF(coordinates), LFF(S("brush_size"))});
    nb::object iterator = lh_iter(interp_coord);
    nb::object c;
    for (;;) {
        nb::object next = lh_next(iterator);
        if (!next.is_valid()) break;
        lh_assign(c, std::move(next));
        nb::object ndisplay = source.attr(S("_slice_input"), "ndisplay");
        bool three_dimensions = lh_truth(le_compare(std::move(ndisplay), nb::int_(3), Py_EQ));
        if (three_dimensions) {
            // The source evaluates self.data before the tuple/round index.
            nb::object data = S("data");
            nb::object tuple = source.global("tuple");
            nb::object rounded = lh_call(N("round"), {LFF(c)});
            nb::object converted = lh_call(source.attr(rounded, "astype"), {LFF(source.global("int"))});
            nb::object index = lh_call(tuple, {LFF(converted)});
            if (lh_truth(le_compare(lh_item(data, index), nb::int_(0), Py_EQ))) continue;
        }
        int contains;
        {
            nb::object mode = S("_mode");
            nb::object modes = lh_check(PyList_New(2));
            PyList_SET_ITEM(modes.ptr(), 0, source.attr(source.global("Mode"), "PAINT").release().ptr());
            PyList_SET_ITEM(modes.ptr(), 1, source.attr(source.global("Mode"), "ERASE").release().ptr());
            contains = PySequence_Contains(modes.ptr(), mode.ptr());
            if (contains < 0) throw nb::python_error();
        }
        if (contains) {
            lh_call(S("paint"), {LFF(c), LFF(new_label)},
                    {LFF(nb::bool_(false))}, source.keyword("refresh"));
        } else {
            nb::object mode = S("_mode");
            nb::object fill = source.attr(source.global("Mode"), "FILL");
            bool filling = lh_truth(le_compare(std::move(mode), std::move(fill), Py_EQ));
            if (filling) lh_call(S("fill"), {LFF(c), LFF(new_label)},
                                {LFF(nb::bool_(false))}, source.keyword("refresh"));
        }
    }
    lh_call(S("_partial_labels_refresh"));
    return nb::none();
}
nb::object lf_brush_bbox(const LFSource& source, nb::object self,
                         nb::object coord, nb::object dims_to_paint, nb::object shape) {
    auto S = [&](const char* name) { return source.attr(self, name); };
    auto N = [&](const char* name) { return source.attr(source.global("np"), name); };
    auto binary = [](binaryfunc f, nb::object a, nb::object b) {
        return lh_check(f(a.ptr(), b.ptr()));
    };
    nb::object array = N("array");
    nb::object scale_list = lh_check(PyList_New(0));
    nb::object iterator = lh_iter(dims_to_paint), i;
    for (;;) {
        nb::object next = lh_next(iterator);
        if (!next.is_valid()) break;
        lh_assign(i, std::move(next));
        nb::object value = lh_item(S("scale"), i);
        if (PyList_Append(scale_list.ptr(), value.ptr()) < 0) throw nb::python_error();
    }
    iterator.reset(); i.reset();
    nb::object paint_scale = lh_call(std::move(array), {LFF(scale_list)},
        {LFF(source.global("float"))}, source.keyword("dtype"));
    scale_list.reset();
    nb::object n_edit = S("n_edit_dimensions"), ndim = S("ndim");
    bool fewer = lh_truth(le_compare(std::move(n_edit), std::move(ndim), Py_LT));
    nb::object coord_paint;
    if (fewer) {
        array = N("array");
        nb::object values = lh_check(PyList_New(0));
        iterator = lh_iter(dims_to_paint);
        for (;;) {
            nb::object next = lh_next(iterator);
            if (!next.is_valid()) break;
            lh_assign(i, std::move(next));
            nb::object value = lh_item(coord, i);
            if (PyList_Append(values.ptr(), value.ptr()) < 0) throw nb::python_error();
        }
        iterator.reset(); i.reset();
        coord_paint = lh_call(std::move(array), {LFF(values)});
    } else {
        coord_paint = lh_call(N("array"), {LFF(coord)});
    }
    nb::object floor = N("floor");
    nb::object half_brush = binary(PyNumber_TrueDivide, S("brush_size"), nb::int_(2));
    nb::object radius = binary(PyNumber_Add, lh_call(std::move(floor), {LFF(half_brush)}), nb::float_(0.5));
    half_brush.reset();
    nb::object normalized_scale = lh_call(N("abs"), {LFF(paint_scale)});
    nb::object minimum = lh_call(N("min"), {LFF(normalized_scale)});
    lh_assign(normalized_scale, binary(PyNumber_TrueDivide, normalized_scale, std::move(minimum)));
    nb::object quotient = binary(PyNumber_TrueDivide, radius, normalized_scale);
    nb::object rounded_radius = lh_call(N("floor"), {LFF(quotient)});
    quotient.reset();
    nb::object radius_pixels = lh_call(source.attr(std::move(rounded_radius), "astype"),
                                      {LFF(source.global("int"))});
    nb::object rounded_center = lh_call(N("round"), {LFF(coord_paint)});
    nb::object center = lh_call(source.attr(std::move(rounded_center), "astype"),
                               {LFF(source.global("int"))});
    nb::object min_vals = lh_call(N("maximum"),
        {LFF(binary(PyNumber_Subtract, center, radius_pixels)), LFF(nb::int_(0))});
    nb::object max_vals = lh_call(N("minimum"),
        {LFF(binary(PyNumber_Add, binary(PyNumber_Add, center, radius_pixels), nb::int_(1))), LFF(shape)});
    if (lh_truth(lh_call(N("any"), {LFF(le_compare(min_vals, max_vals, Py_GE))}))) return nb::none();
    nb::object bbox_shape = lh_call(source.global("tuple"),
                                   {LFF(binary(PyNumber_Subtract, max_vals, min_vals))});
    nb::object center_in_bbox = binary(PyNumber_Subtract, center, min_vals);
    nb::object brush_mask = lh_call(S("_create_brush_mask"),
        {LFF(bbox_shape), LFF(radius), LFF(center_in_bbox), LFF(normalized_scale)});
    if (!lh_truth(lh_call(N("any"), {LFF(brush_mask)}))) return nb::none();
    return nb::make_tuple(brush_mask, min_vals, max_vals);
}

nb::object lf_polygon_bbox(const LFSource& source, nb::object self,
                           nb::object points, nb::object dims_to_paint, nb::object shape) {
    auto N = [&](const char* name) { return source.attr(source.global("np"), name); };
    auto binary = [](binaryfunc f, nb::object a, nb::object b) { return lh_check(f(a.ptr(), b.ptr())); };
    nb::object count = lh_call(source.global("len"), {LFF(dims_to_paint)});
    if (lh_truth(le_compare(std::move(count), nb::int_(2), Py_NE))) {
        nb::object exception = source.global("NotImplementedError");
        nb::object message = le_literal(source.metadata, "Polygon painting is implemented only in 2D.");
        nb::object value = lh_call(exception, {LFF(message)});
        PyErr_SetObject(exception.ptr(), value.ptr());
        throw nb::python_error();
    }
    nb::object full = lh_check(PySlice_New(Py_None, Py_None, Py_None));
    nb::object key = nb::make_tuple(full, dims_to_paint);
    nb::object points2d = lh_item(points, key);
    key.reset(); full.reset();
    nb::object min_vals = lh_call(N("min"), {LFF(points2d)},
                                 {LFF(nb::int_(0))}, source.keyword("axis"));
    nb::object maximum = lh_call(N("max"), {LFF(points2d)},
                                {LFF(nb::int_(0))}, source.keyword("axis"));
    nb::object max_vals = binary(PyNumber_Add, std::move(maximum), nb::int_(1));
    lh_assign(min_vals, lh_call(N("maximum"), {LFF(min_vals), LFF(nb::int_(0))}));
    lh_assign(max_vals, lh_call(N("minimum"), {LFF(max_vals), LFF(shape)}));
    if (lh_truth(lh_call(N("any"), {LFF(le_compare(min_vals, max_vals, Py_GE))}))) return nb::none();
    nb::object bbox_shape = lh_call(source.global("list"),
                                   {LFF(binary(PyNumber_Subtract, max_vals, min_vals))});
    nb::object shifted = binary(PyNumber_Subtract, points2d, min_vals);
    nb::object mask = lh_call(source.attr(self, "_create_polygon_mask"), {LFF(shifted), LFF(bbox_shape)});
    if (!lh_truth(lh_call(N("any"), {LFF(mask)}))) return nb::none();
    return nb::make_tuple(mask, min_vals, max_vals);
}
#undef LFF

// Handwritten frontend policies. This fragment is not an installed handoff.
#define LFF(value) [&]() -> nb::object { return (value); }

void lf_store(const LFSource& source, nb::object target, const char* field,
              nb::object value) {
    nb::object name = le_name(source.metadata, field);
    int status = PyObject_SetAttr(target.ptr(), name.ptr(), value.ptr());
    target.reset(); name.reset(); value.reset();
    if (status < 0) throw nb::python_error();
}

nb::object lf_property_get(const LFSource& source, nb::object self, int entry) {
    // Entry IDs are private admission IDs, never application state.
    static const char* fields[] = {
        "_contiguous", "_n_edit_dimensions", "_brush_size", "_selected_label",
        "_show_selected_label", "_preserve_labels", "_colormap", "_ndim",
        "_cursor", "_cursor_size"
    };
    if (entry >= 0 && entry < 10) return source.attr(self, fields[entry]);
    if (entry == 10) {
        nb::object transforms = source.attr(self, "_transforms");
        nb::object key = le_literal(source.metadata, "data2physical");
        return source.attr(lh_item(transforms, key), "scale");
    }
    throw nb::type_error("invalid frontend getter admission ID");
}

nb::object lf_cursor_set(const LFSource& source, nb::object self,
                         nb::object value, bool size) {
    const char* public_name = size ? "cursor_size" : "cursor";
    const char* private_name = size ? "_cursor_size" : "_cursor";
    if (lh_truth(le_compare(value, source.attr(self, public_name), Py_EQ)))
        return nb::none();
    lf_store(source, self, private_name, value);
    lh_call(source.attr(source.attr(self, "events"), public_name), {},
            {LFF(value)}, source.keyword(public_name));
    return nb::none();
}

nb::object lf_brush_size_set(const LFSource& source, nb::object self,
                             nb::object brush_size) {
    nb::object converted = lh_call(source.global("int"), {LFF(brush_size)});
    lf_store(source, self, "_brush_size", std::move(converted));
    nb::object cursor_size = lh_call(source.attr(self, "_calculate_cursor_size"));
    lf_store(source, self, "cursor_size", std::move(cursor_size));
    lh_call(source.attr(source.attr(self, "events"), "brush_size"));
    return nb::none();
}

nb::object lf_calculate_cursor_size(const LFSource& source, nb::object self) {
    nb::object scale = source.attr(source.attr(self, "_data_to_world"), "scale");
    nb::object minimum = source.attr(source.global("np"), "min");
    nb::object values = lh_check(PyList_New(0));
    nb::object iterator = lh_iter(source.attr(source.attr(self, "_slice_input"), "displayed"));
    nb::object d;
    for (;;) {
        nb::object next = lh_next(iterator);
        if (!next.is_valid()) break;
        lh_assign(d, std::move(next));
        nb::object item = lh_call(source.global("abs"), {LFF(lh_item(scale, d))});
        if (PyList_Append(values.ptr(), item.ptr()) < 0) throw nb::python_error();
    }
    iterator.reset(); d.reset();
    nb::object min_scale = lh_call(std::move(minimum), {LFF(values)});
    values.reset();
    nb::object absolute = source.global("abs");
    nb::object brush_size = source.attr(self, "brush_size");
    nb::object product = lh_check(PyNumber_Multiply(brush_size.ptr(), min_scale.ptr()));
    brush_size.reset();
    return lh_call(std::move(absolute), {LFF(product)});
}

nb::object lf_selected_label_set(const LFSource& source, nb::object self,
                                nb::object selected_label) {
    auto S = [&](const char* name) { return source.attr(self, name); };
    if (lh_truth(le_compare(selected_label, S("selected_label"), Py_EQ))) return nb::none();
    lh_call(S("_validate_label_in_range"), {LFF(selected_label)});
    if (lh_truth(le_compare(selected_label, source.attr(S("colormap"), "background_value"), Py_EQ)))
        lf_store(source, self, "_prev_selected_label", S("selected_label"));
    else
        lf_store(source, self, "_prev_selected_label", nb::none());
    lf_store(source, S("colormap"), "selection", selected_label);
    lf_store(source, self, "_selected_label", selected_label);
    nb::object color = lh_call(S("get_color"), {LFF(selected_label)});
    lf_store(source, self, "_selected_color", std::move(color));
    lh_call(source.attr(S("events"), "selected_label"));
    if (lh_truth(S("show_selected_label")))
        lh_call(S("refresh"), {}, {LFF(nb::bool_(false))}, source.keyword("extent"));
    return nb::none();
}

nb::object lf_get_color(const LFSource& source, nb::object self, nb::object label) {
    auto S = [&](const char* name) { return source.attr(self, name); };
    if (lh_truth(le_compare(label, source.attr(S("colormap"), "background_value"), Py_EQ)))
        return nb::none();
    bool use_background = label.is_none();
    if (!use_background && lh_truth(S("show_selected_label")))
        use_background = lh_truth(le_compare(label, S("selected_label"), Py_NE));
    if (use_background) {
        nb::object map = source.attr(S("colormap"), "map");
        return lh_call(std::move(map), {LFF(source.attr(S("colormap"), "background_value"))});
    }
    return lh_call(source.attr(S("colormap"), "map"), {LFF(label)});
}

nb::object lf_dims_to_paint(const LFSource& source, nb::object self) {
    nb::object list = source.global("list");
    nb::object order = source.attr(source.attr(self, "_slice_input"), "order");
    nb::object n_edit = source.attr(self, "n_edit_dimensions");
    nb::object start = lh_check(PyNumber_Negative(n_edit.ptr()));
    n_edit.reset();
    nb::object slice = lh_check(PySlice_New(start.ptr(), Py_None, Py_None));
    start.reset();
    nb::object selected = lh_item(order, slice);
    order.reset(); slice.reset();
    return lh_call(std::move(list), {LFF(selected)});
}

nb::object lf_shape_and_dims(const LFSource& source, nb::object self) {
    nb::object dims_to_paint = lh_call(source.global("sorted"),
        {LFF(lh_call(source.attr(self, "_get_dims_to_paint")))});
    nb::object shape = lh_call(source.global("list"),
        {LFF(source.attr(source.attr(self, "data"), "shape"))});
    nb::object n_edit = source.attr(self, "n_edit_dimensions");
    nb::object ndim = source.attr(self, "ndim");
    if (lh_truth(le_compare(std::move(n_edit), std::move(ndim), Py_LT))) {
        nb::object projected = lh_check(PyList_New(0));
        nb::object iterator = lh_iter(dims_to_paint), i;
        for (;;) {
            nb::object next = lh_next(iterator);
            if (!next.is_valid()) break;
            lh_assign(i, std::move(next));
            nb::object value = lh_item(shape, i);
            if (PyList_Append(projected.ptr(), value.ptr()) < 0) throw nb::python_error();
        }
        iterator.reset(); i.reset();
        lh_assign(shape, std::move(projected));
    }
    return nb::make_tuple(shape, dims_to_paint);
}

nb::object lf_validate_label(const LFSource& source, nb::object self, nb::object label) {
    nb::object layer_dtype = lh_call(source.global("get_dtype"), {LFF(self)});
    nb::object dtype_lims = lh_call(source.global("get_dtype_limits"), {LFF(layer_dtype)});
    bool invalid = lh_truth(le_compare(lh_item(dtype_lims, nb::int_(0)), label, Py_GT));
    if (!invalid) invalid = lh_truth(le_compare(lh_item(dtype_lims, nb::int_(1)), label, Py_LT));
    if (invalid) {
        nb::object exception = source.global("WrongSelectedLabelError");
        nb::object value = lh_call(exception, {},
            {LFF(layer_dtype), LFF(label), LFF(lh_item(dtype_lims, nb::int_(0))),
             LFF(lh_item(dtype_lims, nb::int_(1)))},
            source.keyword("dtype,value,lower_bound,upper_bound"));
        // Python raise also accepts a non-exception object returned by a
        // substituted callable; normalize its error through the C API.
        if (!PyExceptionInstance_Check(value.ptr())) {
            PyErr_SetString(PyExc_TypeError, "exceptions must derive from BaseException");
            throw nb::python_error();
        }
        PyErr_SetObject(reinterpret_cast<PyObject*>(Py_TYPE(value.ptr())), value.ptr());
        throw nb::python_error();
    }
    return nb::none();
}
nb::object lf_build_slice_key(const LFSource& source, nb::object slice_coord,
                              nb::object dims_to_paint, nb::object min_vals,
                              nb::object max_vals) {
    nb::object slice_key_list = lh_check(PyList_New(0));
    nb::object iterator = lh_iter(slice_coord), coord;
    for (;;) {
        nb::object next = lh_next(iterator);
        if (!next.is_valid()) break;
        lh_assign(coord, std::move(next));
        nb::object slice = source.global("slice");
        nb::object start = lh_call(source.global("int"), {LFF(coord)});
        nb::object stop_int = lh_call(source.global("int"), {LFF(coord)});
        nb::object stop = lh_check(PyNumber_Add(stop_int.ptr(), nb::int_(1).ptr()));
        stop_int.reset();
        nb::object value = lh_call(std::move(slice), {LFF(start), LFF(stop)});
        if (PyList_Append(slice_key_list.ptr(), value.ptr()) < 0) throw nb::python_error();
    }
    iterator.reset(); coord.reset();
    iterator = lh_iter(lh_call(source.global("enumerate"), {LFF(dims_to_paint)}));
    nb::object i, dim;
    for (;;) {
        nb::object next = lh_next(iterator);
        if (!next.is_valid()) break;
        auto pair = lf_unpack(std::move(next), 2);
        lh_assign(i, std::move(pair[0])); lh_assign(dim, std::move(pair[1]));
        nb::object slice = source.global("slice");
        nb::object start = lh_call(source.global("int"), {LFF(lh_item(min_vals, i))});
        nb::object stop = lh_call(source.global("int"), {LFF(lh_item(max_vals, i))});
        nb::object value = lh_call(std::move(slice), {LFF(start), LFF(stop)});
        if (PyObject_SetItem(slice_key_list.ptr(), dim.ptr(), value.ptr()) < 0)
            throw nb::python_error();
    }
    return lh_call(source.global("tuple"), {LFF(slice_key_list)});
}
#undef LFF

// Full Python-facing policy is handwritten here, including non-fast-path inputs.
// Numerical NumPy operations remain calls to their actual provider objects.
#define LFF(value) [&]() -> nb::object { return (value); }
nb::object lf_get_dtype(const LFSource& source, nb::object layer) {
    nb::object layer_data = source.attr(layer, "data");
    if (!lh_truth(lh_call(source.global("isinstance"),
                         {LFF(layer_data), LFF(source.global("list"))}))) {
        nb::object wrapped = lh_check(PyList_New(1));
        PyList_SET_ITEM(wrapped.ptr(), 0, layer_data.inc_ref().ptr());
        lh_assign(layer_data, std::move(wrapped));
    }
    nb::object level = lh_item(layer_data, nb::int_(0));
    if (lh_truth(lh_call(source.global("hasattr"),
        {LFF(level), LFF(le_literal(source.metadata, "dtype"))})))
        return source.attr(lh_item(level, nb::int_(0)), "dtype");
    return lh_call(source.global("type"), {LFF(level)});
}

nb::object lf_mouse_coordinate(const LFSource& source, nb::object layer,
                               nb::object event) {
    nb::object ndim = lh_call(source.global("len"),
        {LFF(source.attr(source.attr(layer, "_slice_input"), "displayed"))});
    if (lh_truth(le_compare(ndim, nb::int_(2), Py_EQ)))
        return lh_call(source.attr(layer, "world_to_data"),
                       {LFF(source.attr(event, "position"))});
    auto pair = lf_unpack(lh_call(source.attr(layer, "get_ray_intersections"), {},
        {LFF(source.attr(event, "position")), LFF(source.attr(event, "view_direction")),
         LFF(source.attr(source.attr(layer, "_slice_input"), "displayed")),
         LFF(nb::bool_(true))},
        source.keyword("position,view_direction,dims_displayed,world")), 2);
    nb::object start = std::move(pair[0]), end = std::move(pair[1]);
    if (start.is_none() && end.is_none()) return nb::none();
    return lh_call(source.global("first_nonzero_coordinate"),
        {LFF(source.attr(layer, "data")), LFF(start), LFF(end)});
}

nb::object lf_pick(const LFSource& source, nb::object layer, nb::object event) {
    nb::object value = lh_call(source.attr(layer, "get_value"),
        {LFF(source.attr(event, "position"))},
        {LFF(source.attr(event, "view_direction")), LFF(source.attr(event, "dims_displayed")),
         LFF(nb::bool_(true))}, source.keyword("view_direction,dims_displayed,world"));
    if (!lh_truth(value)) lh_assign(value, nb::int_(0));
    lf_store(source, layer, "selected_label", std::move(value));
    return nb::none();
}

nb::object lf_first_nonzero(const LFSource& source, nb::object data,
                            nb::object start_point, nb::object end_point) {
    auto N = [&](const char* name) { return source.attr(source.global("np"), name); };
    nb::object shape = lh_call(N("asarray"), {LFF(source.attr(data, "shape"))});
    nb::object norm = source.attr(source.attr(source.global("np"), "linalg"), "norm");
    nb::object difference = lh_check(PyNumber_Subtract(end_point.ptr(), start_point.ptr()));
    nb::object length = lh_call(std::move(norm), {LFF(difference)});
    difference.reset();
    nb::object rounded = lh_call(N("round"), {LFF(length)});
    nb::object length_int = lh_call(source.attr(std::move(rounded), "astype"),
                                   {LFF(source.global("int"))});
    nb::object coords = lh_call(N("linspace"),
        {LFF(start_point), LFF(end_point),
         LFF(lh_check(PyNumber_Add(length_int.ptr(), nb::int_(1).ptr())))},
        {LFF(nb::bool_(true))}, source.keyword("endpoint"));
    nb::object clipped = lh_call(N("clip"),
        {LFF(lh_call(N("round"), {LFF(coords)})), LFF(nb::int_(0)),
         LFF(lh_check(PyNumber_Subtract(shape.ptr(), nb::int_(1).ptr())))});
    nb::object clipped_coords = lh_call(source.attr(std::move(clipped), "astype"),
                                       {LFF(source.global("int"))});
    nb::object nonzero = lh_call(N("flatnonzero"),
        {LFF(lh_item(data, lh_call(source.global("tuple"),
                                 {LFF(source.attr(clipped_coords, "T"))})))});
    nb::object count = lh_call(source.global("len"), {LFF(nonzero)});
    if (lh_truth(le_compare(std::move(count), nb::int_(0), Py_EQ))) return nb::none();
    return lh_item(clipped_coords, lh_item(nonzero, nb::int_(0)));
}

nb::object lf_interpolate(const LFSource& source, nb::object old_coord,
                          nb::object new_coord, nb::object brush_size) {
    auto N = [&](const char* name) { return source.attr(source.global("np"), name); };
    if (old_coord.is_none()) lh_assign(old_coord, new_coord);
    if (new_coord.is_none()) lh_assign(new_coord, old_coord);
    nb::object round = source.global("round"), maximum = source.global("max");
    nb::object absolute = source.global("abs");
    nb::object new_array = lh_call(N("array"), {LFF(new_coord)});
    nb::object old_array = lh_call(N("array"), {LFF(old_coord)});
    nb::object difference = lh_check(PyNumber_Subtract(new_array.ptr(), old_array.ptr()));
    new_array.reset(); old_array.reset();
    nb::object magnitude = lh_call(std::move(absolute), {LFF(difference)});
    difference.reset();
    nb::object extent = lh_call(std::move(maximum), {LFF(magnitude)});
    magnitude.reset();
    nb::object quotient = lh_check(PyNumber_TrueDivide(extent.ptr(), brush_size.ptr()));
    extent.reset();
    nb::object steps = lh_check(PyNumber_Multiply(quotient.ptr(), nb::int_(4).ptr()));
    quotient.reset();
    nb::object num_step = lh_call(std::move(round), {LFF(steps)});
    steps.reset();
    nb::object coords = lh_check(PyList_New(0));
    nb::object range = source.global("range");
    nb::object count = lh_call(source.global("len"), {LFF(new_coord)});
    nb::object iterator = lh_iter(lh_call(std::move(range), {LFF(count)})), i;
    count.reset();
    for (;;) {
        nb::object next = lh_next(iterator);
        if (!next.is_valid()) break;
        lh_assign(i, std::move(next));
        nb::object line = lh_call(N("linspace"),
            {LFF(lh_item(old_coord, i)), LFF(lh_item(new_coord, i))},
            {LFF(lh_call(source.global("int"),
                 {LFF(lh_check(PyNumber_Add(num_step.ptr(), nb::int_(1).ptr())))}))},
            source.keyword("num"));
        if (PyList_Append(coords.ptr(), line.ptr()) < 0) throw nb::python_error();
    }
    iterator.reset(); i.reset();
    lh_assign(coords, source.attr(lh_call(N("stack"), {LFF(coords)}), "T"));
    if (lh_truth(le_compare(lh_call(source.global("len"), {LFF(coords)}), nb::int_(1), Py_GT))) {
        nb::object start = nb::int_(1);
        nb::object slice = lh_check(PySlice_New(start.ptr(), Py_None, Py_None));
        lh_assign(coords, lh_item(coords, slice));
    }
    return coords;
}
#undef LFF

// Handwritten native control policy; source admission remains a separate gate.
#define LFF(value) [&]() -> nb::object { return (value); }

nb::object lf_format(nb::object value) {
    return lh_check(PyObject_Format(value.ptr(), nullptr));
}

[[noreturn]] void lf_raise_value(nb::object value) {
    if (PyExceptionClass_Check(value.ptr()))
        value = lh_check(PyObject_CallNoArgs(value.ptr()));
    if (!PyExceptionInstance_Check(value.ptr())) {
        PyErr_SetString(PyExc_TypeError, "exceptions must derive from BaseException");
        throw nb::python_error();
    }
    PyErr_SetObject(reinterpret_cast<PyObject*>(Py_TYPE(value.ptr())), value.ptr());
    throw nb::python_error();
}

nb::object lf_validate_non_painted(const LFSource& source, nb::object self,
                                  nb::object slice_coord, nb::object dims_to_paint) {
    nb::object iterator = lh_iter(lh_call(source.global("enumerate"), {LFF(slice_coord)}));
    nb::object i, coord, resolved;
    auto axis_size = [&]() {
        return lh_item(source.attr(source.attr(self, "data"), "shape"), i);
    };
    for (;;) {
        nb::object next = lh_next(iterator);
        if (!next.is_valid()) break;
        auto pair = lf_unpack(std::move(next), 2);
        lh_assign(i, std::move(pair[0])); lh_assign(coord, std::move(pair[1]));
        if (le_contains(dims_to_paint, i)) continue;
        if (lh_truth(le_compare(coord, nb::int_(0), Py_LT))) {
            nb::object size = axis_size();
            lh_assign(resolved, lh_check(PyNumber_Add(coord.ptr(), size.ptr())));
        } else {
            lh_assign(resolved, coord);
        }
        bool valid = lh_truth(le_compare(nb::int_(0), resolved, Py_LE));
        if (valid) valid = lh_truth(le_compare(resolved, axis_size(), Py_LT));
        if (!valid) {
            // Resolve the exception callable before evaluating its f-string.
            nb::object exception = source.global("IndexError");
            nb::object prefix = le_literal(source.metadata, "Coordinate ");
            nb::object formatted_coord = lf_format(coord);
            nb::object middle = le_literal(source.metadata, " for non-painted dimension ");
            nb::object formatted_i = lf_format(i);
            nb::object suffix = le_literal(source.metadata, " is out of bounds for axis with size ");
            nb::object formatted_size = lf_format(axis_size());
            nb::object period = le_literal(source.metadata, ".");
            nb::object parts = nb::make_tuple(prefix, formatted_coord, middle, formatted_i,
                                              suffix, formatted_size, period);
            nb::object empty = lh_check(PyUnicode_FromString(""));
            nb::object message = lh_check(PyUnicode_Join(empty.ptr(), parts.ptr()));
            lf_raise_value(lh_call(std::move(exception), {LFF(message)}));
        }
        if (PyObject_SetItem(slice_coord.ptr(), i.ptr(), resolved.ptr()) < 0)
            throw nb::python_error();
    }
    return nb::none();
}

nb::object lf_brush_mouse_init(const LFSource& source, nb::object self,
                              nb::object minimum) {
    lf_store(source, self, "min_brush_size", minimum);
    lf_store(source, self, "init_pos", nb::none());
    lf_store(source, self, "init_brush_size", nb::none());
    return nb::none();
}

nb::object lf_brush_modifiers_change(const LFSource& source, nb::object self) {
    nb::object setting = source.attr(source.attr(lh_call(source.global("get_settings")),
                                                "application"),
                                     "brush_size_on_mouse_move_modifiers");
    nb::object modifiers = lh_call(source.attr(source.attr(setting, "value"), "split"),
                                   {LFF(le_literal(source.metadata, "+"))});
    lf_store(source, self, "modifiers", std::move(modifiers));
    return nb::none();
}

nb::object lf_plain_real(const LFSource& source, nb::object value) {
    nb::object type = lh_call(source.global("type"), {LFF(value)});
    nb::object array_type = source.attr(source.global("np"), "ndarray");
    if (type.ptr() != array_type.ptr()) return nb::bool_(false);
    nb::object dtype = source.attr(value, "dtype");
    return nb::bool_(le_contains(source.global("_REAL_DTYPES"), std::move(dtype)));
}

// CHECK_EXC_MATCH accepts a class or a flat tuple of exception classes.
// PyErr_GivenExceptionMatches also accepts nested tuples, so its permissive
// matching cannot replace the bytecode's separate validation step.
bool lf_valid_exception_handler(nb::handle value) {
    if (PyTuple_Check(value.ptr())) {
        for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(value.ptr()); ++i)
            if (!PyExceptionClass_Check(PyTuple_GET_ITEM(value.ptr(), i)))
                return false;
        return true;
    }
    return PyExceptionClass_Check(value.ptr());
}
bool lf_matches_handler(nb::handle error, nb::object handler) {
    if (!lf_valid_exception_handler(handler)) {
        PyErr_SetString(PyExc_TypeError,
            "catching classes that do not inherit from BaseException is not allowed");
        throw nb::python_error();
    }
    return PyErr_GivenExceptionMatches(error.ptr(), handler.ptr()) != 0;
}

nb::object lf_mouse_press(const LFSource& source, nb::object obj, nb::object event) {
    nb::object iterator = lh_iter(source.attr(obj, "mouse_drag_callbacks"));
    nb::object callback, gen;
    for (;;) {
        nb::object next = lh_next(iterator);
        if (!next.is_valid()) break;
        lh_assign(callback, std::move(next));
        lh_assign(gen, lh_call(callback, {LFF(obj), LFF(event)}));
        if (lh_truth(lh_call(source.attr(source.global("inspect"), "isgenerator"), {LFF(gen)}))) {
            try {
                lh_call(source.global("next"), {LFF(gen)});
                nb::object registry = source.attr(obj, "_mouse_drag_gen");
                int status = PyObject_SetItem(registry.ptr(), callback.ptr(), gen.ptr());
                registry.reset();
                if (status < 0) throw nb::python_error();
                registry = source.attr(obj, "_persisted_mouse_event");
                status = PyObject_SetItem(registry.ptr(), gen.ptr(), event.ptr());
                registry.reset();
                if (status < 0) throw nb::python_error();
            } catch (nb::python_error& error) {
                LHHandledException handled(error.value());
                if (!lf_matches_handler(error.value(), source.global("StopIteration"))) throw;
            }
        }
    }
    return nb::none();
}

nb::object lf_mouse_move(const LFSource& source, nb::object obj, nb::object event) {
    nb::object move_callback, func, gen;
    if (!lh_truth(source.attr(event, "is_dragging"))) {
        nb::object iterator = lh_iter(source.attr(obj, "mouse_move_callbacks"));
        for (;;) {
            nb::object next = lh_next(iterator);
            if (!next.is_valid()) break;
            lh_assign(move_callback, std::move(next));
            lh_call(move_callback, {LFF(obj), LFF(event)});
        }
    }
    nb::object tuple = source.global("tuple");
    nb::object items = lh_call(source.attr(source.attr(obj, "_mouse_drag_gen"), "items"));
    nb::object snapshot = lh_call(std::move(tuple), {LFF(items)});
    items.reset();
    nb::object iterator = lh_iter(snapshot);
    snapshot.reset();
    for (;;) {
        nb::object next = lh_next(iterator);
        if (!next.is_valid()) break;
        auto pair = lf_unpack(std::move(next), 2);
        lh_assign(func, std::move(pair[0])); lh_assign(gen, std::move(pair[1]));
        lf_store(source, lh_item(source.attr(obj, "_persisted_mouse_event"), gen),
                 "__wrapped__", event);
        try {
            lh_call(source.global("next"), {LFF(gen)});
        } catch (nb::python_error& error) {
            LHHandledException handled(error.value());
            if (!lf_matches_handler(error.value(), source.global("StopIteration"))) throw;
            nb::object registry = source.attr(obj, "_mouse_drag_gen");
            int status = PyObject_DelItem(registry.ptr(), func.ptr());
            registry.reset();
            if (status < 0) throw nb::python_error();
            registry = source.attr(obj, "_persisted_mouse_event");
            status = PyObject_DelItem(registry.ptr(), gen.ptr());
            registry.reset();
            if (status < 0) throw nb::python_error();
        }
    }
    return nb::none();
}
nb::object lf_context_special(nb::object manager, const char* spelling) {
    nb::object name = lh_check(PyUnicode_FromString(spelling));
    PyObject* descriptor = _PyType_Lookup(Py_TYPE(manager.ptr()), name.ptr());
    if (!descriptor) {
        if (PyErr_Occurred()) throw nb::python_error();
        PyErr_Format(PyExc_TypeError, "'%.200s' object does not support the context manager protocol%s",
                     Py_TYPE(manager.ptr())->tp_name,
                     std::string(spelling) == "__exit__" ? " (missed __exit__ method)" : "");
        throw nb::python_error();
    }
    nb::object held = nb::borrow<nb::object>(descriptor);
    descrgetfunc get = Py_TYPE(descriptor)->tp_descr_get;
    if (!get) return held;
    return lh_check(get(held.ptr(), manager.ptr(), reinterpret_cast<PyObject*>(Py_TYPE(manager.ptr()))));
}

nb::object lf_mouse_release(const LFSource& source, nb::object obj, nb::object event) {
    nb::object tuple = source.global("tuple");
    nb::object items = lh_call(source.attr(source.attr(obj, "_mouse_drag_gen"), "items"));
    nb::object snapshot = lh_call(std::move(tuple), {LFF(items)});
    items.reset();
    nb::object iterator = lh_iter(snapshot), func, gen;
    snapshot.reset();
    for (;;) {
        nb::object next = lh_next(iterator);
        if (!next.is_valid()) break;
        auto pair = lf_unpack(std::move(next), 2);
        lh_assign(func, std::move(pair[0])); lh_assign(gen, std::move(pair[1]));
        lf_store(source, lh_item(source.attr(obj, "_persisted_mouse_event"), gen),
                 "__wrapped__", event);
        nb::object manager = lh_call(source.attr(source.global("contextlib"), "suppress"),
                                     {LFF(source.global("StopIteration"))});
        nb::object enter = lf_context_special(manager, "__enter__");
        nb::object exit = lf_context_special(manager, "__exit__");
        manager.reset();
        lh_call(std::move(enter));
        bool exceptional_exit = false;
        try {
            lh_call(source.global("next"), {LFF(gen)});
        } catch (nb::python_error& error) {
            exceptional_exit = true;
            // A Python CALL adds its active transport frame to an escaping
            // exception before WITH_EXCEPT_START exposes the traceback. The
            // native call must do this before invoking the actual __exit__.
            // Final admission must ensure this is the genuine replaced source
            // shell frame; the isolated probe only verifies observable presence.
            PyFrameObject* frame = PyEval_GetFrame();
            if (!frame) throw nb::type_error("native with transport requires a Python source frame");
            PyErr_SetObject(reinterpret_cast<PyObject*>(Py_TYPE(error.value().ptr())), error.value().ptr());
            if (PyTraceBack_Here(frame) < 0) throw nb::python_error();
            nb::python_error annotated;
            LHHandledException handled(error.value());
            nb::object type = nb::borrow<nb::object>(reinterpret_cast<PyObject*>(Py_TYPE(error.value().ptr())));
            nb::object traceback = nb::steal<nb::object>(PyException_GetTraceback(error.value().ptr()));
            if (!traceback.is_valid()) traceback = nb::none();
            nb::object result = lh_call(exit, {LFF(type), LFF(nb::borrow<nb::object>(error.value())), LFF(traceback)});
            if (!lh_truth(std::move(result))) throw;
        }
        if (!exceptional_exit)
            lh_call(exit, {LFF(nb::none()), LFF(nb::none()), LFF(nb::none())});
        exit.reset();
        nb::object registry = source.attr(obj, "_mouse_drag_gen");
        int status = PyObject_DelItem(registry.ptr(), func.ptr());
        registry.reset();
        if (status < 0) throw nb::python_error();
        registry = source.attr(obj, "_persisted_mouse_event");
        status = PyObject_DelItem(registry.ptr(), gen.ptr());
        registry.reset();
        if (status < 0) throw nb::python_error();
    }
    return nb::none();
}
#undef LFF

// Native mouse-stroke state machine. Python transports generator protocol only;
// all gates, source lookups, history entry/exit and resume policy are here.
#define LFF(value) [&]() -> nb::object { return (value); }
#define LF_DRAW_FIELDS(X) \
    X(globals) X(builtins) X(metadata) X(layer) X(event) X(brush_stroke) \
    X(coordinates) X(new_label) X(last_cursor_coord) X(exit)
struct LFDrawState {
#define LF_DECLARE(name) nb::object name;
    LF_DRAW_FIELDS(LF_DECLARE)
#undef LF_DECLARE
    int phase = 0; // created, press yielded, move yielded, closed
    bool active = false, running = false;
    LFDrawState() = default;
    LFDrawState(LFDrawState&&) = default;
    void clear() {
        phase = 3; active = false;
        // Release the genuine source locals in declaration-slot order.
        layer.reset(); event.reset(); brush_stroke.reset(); coordinates.reset();
        new_label.reset(); last_cursor_coord.reset(); exit.reset();
        globals.reset(); builtins.reset(); metadata.reset();
    }
    ~LFDrawState() { clear(); }
};
struct LFDrawRunning {
    LFDrawState& state;
    explicit LFDrawRunning(LFDrawState& value) : state(value) {
        if (state.running) throw nb::value_error("generator already executing");
        state.running = true;
    }
    ~LFDrawRunning() { state.running = false; }
};

// Attach the active Python transport frame before a with-exit callback can
// inspect an exception. Exact source-frame provenance is an admission gate.
void lf_draw_traceback(nb::handle exception) {
    PyFrameObject* frame = PyEval_GetFrame();
    if (!frame) throw nb::type_error("native generator requires an active Python transport frame");
    PyErr_SetObject(reinterpret_cast<PyObject*>(Py_TYPE(exception.ptr())), exception.ptr());
    if (PyTraceBack_Here(frame) < 0) throw nb::python_error();
    nb::python_error annotated;
}

bool lf_draw_exception_exit(LFDrawState& o, nb::handle exception) {
    if (!o.active) return false;
    o.active = false; // Exit is invoked exactly once, even if it raises.
    lf_draw_traceback(exception);
    LHHandledException handled(exception);
    nb::object type = nb::borrow<nb::object>(reinterpret_cast<PyObject*>(Py_TYPE(exception.ptr())));
    nb::object traceback = nb::steal<nb::object>(PyException_GetTraceback(exception.ptr()));
    if (!traceback.is_valid()) traceback = nb::none();
    nb::object result = lh_call(o.exit, {LFF(type), LFF(nb::borrow<nb::object>(exception)), LFF(traceback)});
    o.exit.reset();
    return lh_truth(std::move(result));
}

nb::object lf_draw_resume(LFDrawState& o) {
    LFSource source{o.globals, o.builtins, o.metadata};
    auto L = [&](const char* name) { return source.attr(o.layer, name); };
    if (o.phase == 0) {
        if (lh_truth(le_compare(L("cursor"), le_literal(o.metadata, "circle_frozen"), Py_EQ)))
            return {};
        lh_assign(o.brush_stroke, lh_item(L("_overlays"), le_literal(o.metadata, "brush_stroke")));
        bool reserved = lh_truth(source.attr(o.brush_stroke, "active"));
        if (!reserved && lh_truth(source.attr(o.brush_stroke, "enabled")))
            reserved = lh_truth(le_compare(source.attr(o.event, "button"), nb::int_(2), Py_EQ));
        if (reserved) return {};
        lh_assign(o.coordinates, lh_call(source.global("mouse_event_to_labels_coordinate"),
                                         {LFF(o.layer), LFF(o.event)}));
        nb::object mode = L("_mode");
        nb::object erase = source.attr(source.global("Mode"), "ERASE");
        if (lh_truth(le_compare(std::move(mode), std::move(erase), Py_EQ)))
            lh_assign(o.new_label, source.attr(L("colormap"), "background_value"));
        else
            lh_assign(o.new_label, L("selected_label"));
        nb::object manager = lh_call(L("block_history"));
        nb::object enter = lf_context_special(manager, "__enter__");
        lh_assign(o.exit, lf_context_special(manager, "__exit__"));
        manager.reset();
        lh_call(std::move(enter));
        o.active = true;
        lh_call(L("_draw"), {LFF(o.new_label), LFF(o.coordinates), LFF(o.coordinates)});
        o.phase = 1;
        return nb::none();
    }
    if (o.phase == 1) lh_assign(o.last_cursor_coord, o.coordinates);
    if (lh_truth(le_compare(source.attr(o.event, "type"), le_literal(o.metadata, "mouse_move"), Py_EQ))) {
        lh_assign(o.coordinates, lh_call(source.global("mouse_event_to_labels_coordinate"),
                                         {LFF(o.layer), LFF(o.event)}));
        if (!o.coordinates.is_none() || !o.last_cursor_coord.is_none())
            lh_call(L("_draw"), {LFF(o.new_label), LFF(o.last_cursor_coord), LFF(o.coordinates)});
        lh_assign(o.last_cursor_coord, o.coordinates);
        o.phase = 2;
        return nb::none();
    }
    o.active = false;
    lh_call(o.exit, {LFF(nb::none()), LFF(nb::none()), LFF(nb::none())});
    o.exit.reset();
    return {};
}

nb::object lf_draw_send(LFDrawState& o, nb::object value) {
    LFDrawRunning running(o);
    if (o.phase == 3) throw nb::stop_iteration();
    if (o.phase == 0 && !value.is_none())
        throw nb::type_error("can't send non-None value to a just-started generator");
    try {
        nb::object result;
        try {
            result = lf_draw_resume(o);
        } catch (nb::python_error& error) {
            if (!lf_draw_exception_exit(o, error.value())) throw;
        }
        if (result.is_valid()) return result;
    } catch (nb::python_error& error) {
        o.clear();
        lh_generator_failure(error);
    } catch (...) {
        o.clear();
        throw;
    }
    o.clear();
    throw nb::stop_iteration();
}

nb::object lf_draw_throw(LFDrawState& o, nb::args arguments) {
    LFDrawRunning running(o);
    nb::object exception = lh_throw_value(arguments);
    const bool suspended = o.phase == 1 || o.phase == 2;
    try {
        if (suspended && lf_draw_exception_exit(o, exception)) {
            o.clear();
            throw nb::stop_iteration();
        }
        PyErr_SetObject(reinterpret_cast<PyObject*>(Py_TYPE(exception.ptr())), exception.ptr());
        throw nb::python_error();
    } catch (nb::python_error& error) {
        o.clear();
        if (suspended) lh_generator_failure(error);
        throw;
    } catch (...) {
        o.clear();
        throw;
    }
}

nb::object lf_draw_close(LFDrawState& o) {
    LFDrawRunning running(o);
    if (o.phase == 3) return nb::none();
    try {
        if (o.active) {
            nb::object exception = lh_call(nb::borrow<nb::object>(PyExc_GeneratorExit));
            lf_draw_exception_exit(o, exception);
        }
    } catch (nb::python_error& error) {
        o.clear();
        lh_generator_failure(error);
    } catch (...) {
        o.clear();
        throw;
    }
    o.clear();
    return nb::none();
}

int lf_draw_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) {
        const auto& o = *nb::inst_ptr<LFDrawState>(self);
#define LF_VISIT(name) Py_VISIT(o.name.ptr());
        LF_DRAW_FIELDS(LF_VISIT)
#undef LF_VISIT
    }
    return 0;
}
int lf_draw_clear(PyObject* self) {
    if (nb::inst_ready(self)) nb::inst_ptr<LFDrawState>(self)->clear();
    return 0;
}
// The genuine Python generator owns finalization and injects GeneratorExit.
// A second finalizer on the state would compete with its owning generator in
// cycles and can reenter the owner while the state is already being closed.
PyType_Slot lf_draw_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(lf_draw_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(lf_draw_clear)}, {0, nullptr}};

nb::object lf_draw_state(LFSource source, nb::object layer, nb::object event) {
    LFDrawState o;
    o.globals = std::move(source.globals); o.builtins = std::move(source.builtins);
    o.metadata = std::move(source.metadata); o.layer = std::move(layer); o.event = std::move(event);
    return nb::cast(std::move(o));
}

void lf_draw_exports(nb::module_& m) {
    nb::class_<LFDrawState>(m, "DrawState", nb::is_weak_referenceable(), nb::type_slots(lf_draw_slots))
        .def("__iter__", [](nb::object self) { return self; })
        .def("__next__", [](LFDrawState& o) { return lf_draw_send(o, nb::none()); })
        .def("send", &lf_draw_send, nb::arg("value").none())
        .def("throw", &lf_draw_throw)
        .def("close", &lf_draw_close);
}
#undef LF_DRAW_FIELDS
#undef LFF

// Experimental frame transport. Canonical module admission and a frozen full
// profile of this shell are still required before any production mutation.
PyObject* lf_draw_frame(PyObject*, PyObject* arguments) {
    try {
        Py_ssize_t count = PyTuple_GET_SIZE(arguments);
        if (count == 1) {
            nb::handle state(PyTuple_GET_ITEM(arguments, 0));
            if (!nb::isinstance<LFDrawState>(state) || !nb::inst_ready(state.ptr()))
                throw nb::type_error("native draw state is required");
            nb::object exception = nb::steal<nb::object>(PyErr_GetHandledException());
            if (!exception.is_valid()) throw nb::type_error("draw throw requires the active transport exception");
            nb::object args = nb::make_tuple(exception);
            return lf_draw_throw(*nb::inst_ptr<LFDrawState>(state.ptr()),
                                 nb::borrow<nb::args>(args)).release().ptr();
        }
        if (count != 2) throw nb::type_error("native draw parameter transport changed");
        PyFrameObject* frame = PyEval_GetFrame();
        if (!frame) throw nb::type_error("native draw source frame is required");
        nb::object code = lh_check(reinterpret_cast<PyObject*>(PyFrame_GetCode(frame)));
        nb::object constants = lh_check(PyObject_GetAttrString(code.ptr(), "co_consts"));
        if (!PyTuple_CheckExact(constants.ptr()) || PyTuple_GET_SIZE(constants.ptr()) == 0)
            throw nb::type_error("native draw source metadata is required");
        nb::object metadata = nb::borrow<nb::object>(PyTuple_GET_ITEM(constants.ptr(), PyTuple_GET_SIZE(constants.ptr()) - 1));
        lh_metadata_values(metadata, 0); lh_metadata_values(metadata, 1);
        LFSource source{lh_check(PyFrame_GetGlobals(frame)), lh_check(PyFrame_GetBuiltins(frame)), std::move(metadata)};
        return lf_draw_state(std::move(source),
            nb::borrow<nb::object>(PyTuple_GET_ITEM(arguments, 0)),
            nb::borrow<nb::object>(PyTuple_GET_ITEM(arguments, 1))).release().ptr();
    } catch (nb::python_error& error) {
        error.restore(); return nullptr;
    } catch (const nb::builtin_exception& error) {
        // This is a direct CPython callback rather than a nanobind dispatcher.
        // Preserve the protocol's normal StopIteration completion instead of
        // routing it through a binder-only TypeError/ValueError translator.
        PyObject* type = PyExc_RuntimeError;
        switch (error.type()) {
        case nb::exception_type::stop_iteration: type = PyExc_StopIteration; break;
        case nb::exception_type::value_error: type = PyExc_ValueError; break;
        case nb::exception_type::type_error: type = PyExc_TypeError; break;
        case nb::exception_type::index_error: type = PyExc_IndexError; break;
        case nb::exception_type::key_error: type = PyExc_KeyError; break;
        case nb::exception_type::attribute_error: type = PyExc_AttributeError; break;
        case nb::exception_type::buffer_error: type = PyExc_BufferError; break;
        case nb::exception_type::import_error: type = PyExc_ImportError; break;
        default: break;
        }
        PyErr_SetString(type, error.what()); return nullptr;
    } catch (const std::bad_alloc&) {
        PyErr_NoMemory(); return nullptr;
    } catch (const std::exception& error) {
        PyErr_SetString(PyExc_RuntimeError, error.what()); return nullptr;
    }
}
PyMethodDef lf_draw_frame_definition = {"draw_frame", lf_draw_frame, METH_VARARGS, nullptr};

nb::object lf_prepare_draw_probe(nb::object function, nb::object shell) {
    if (!PyFunction_Check(function.ptr()) || !PyFunction_Check(shell.ptr()))
        throw nb::type_error("draw probe requires genuine functions");
    auto* original = reinterpret_cast<PyCodeObject*>(PyFunction_GetCode(function.ptr()));
    auto* transport = reinterpret_cast<PyCodeObject*>(PyFunction_GetCode(shell.ptr()));
    if (original->co_argcount != 2 || original->co_kwonlyargcount != 0 ||
        !(original->co_flags & CO_GENERATOR) || PyCode_GetNumFree(original) != 0 ||
        transport->co_argcount != 2 || transport->co_kwonlyargcount != 0 ||
        !(transport->co_flags & CO_GENERATOR) || PyCode_GetNumFree(transport) != 0)
        throw nb::type_error("draw probe requires two-parameter closure-free generator bodies");
    nb::object code = nb::borrow<nb::object>(reinterpret_cast<PyObject*>(transport));
    nb::object raw_constants = lh_check(PyObject_GetAttrString(code.ptr(), "co_consts"));
    Py_ssize_t count = PyTuple_GET_SIZE(raw_constants.ptr());
    nb::object constants = lh_check(PyTuple_New(count + 1));
    nb::object module_name = nb::str("naparixx._native");
    nb::object factory = lh_check(PyCFunction_NewEx(&lf_draw_frame_definition, nullptr, module_name.ptr()));
    bool saw_factory = false, saw_base = false, saw_stop = false;
    for (Py_ssize_t i = 0; i < count; ++i) {
        PyObject* value = PyTuple_GET_ITEM(raw_constants.ptr(), i);
        if (value == Py_Ellipsis) { value = factory.ptr(); saw_factory = true; }
        else if (PyLong_CheckExact(value)) {
            long marker = PyLong_AsLong(value);
            if (marker == -1 && PyErr_Occurred()) throw nb::python_error();
            if (marker == 0) { value = PyExc_BaseException; saw_base = true; }
            else if (marker == 1) { value = PyExc_StopIteration; saw_stop = true; }
            else throw nb::type_error("unexpected draw transport integer marker");
        } else if (value != Py_None) {
            throw nb::type_error("unexpected draw transport constant");
        }
        PyTuple_SET_ITEM(constants.ptr(), i, nb::borrow<nb::object>(value).release().ptr());
    }
    if (!saw_factory || !saw_base || !saw_stop) throw nb::type_error("draw transport markers are missing");
    PyTuple_SET_ITEM(constants.ptr(), count, le_metadata(original).release().ptr());
    nb::dict kwargs;
    kwargs["co_consts"] = constants;
    kwargs["co_filename"] = nb::borrow<nb::object>(original->co_filename);
    kwargs["co_name"] = nb::borrow<nb::object>(original->co_name);
    kwargs["co_qualname"] = nb::borrow<nb::object>(original->co_qualname);
    kwargs["co_firstlineno"] = nb::int_(original->co_firstlineno);
    kwargs["co_flags"] = nb::int_(original->co_flags);
    nb::object replace = lh_check(PyObject_GetAttrString(code.ptr(), "replace"));
    nb::object args = lh_check(PyTuple_New(0));
    return lh_check(PyObject_Call(replace.ptr(), args.ptr(), kwargs.ptr()));
}

// Lazy expression policies are native. Genuine Python generator transport is
// supplied separately; no original executable expression body is retained.
#define LFF(value) [&]() -> nb::object { return (value); }
#define LFX_FIELDS(X) X(globals) X(builtins) X(metadata) X(iterator) X(item) X(item2) X(cells)
struct LFExpressionState {
#define LFX_DECLARE(name) nb::object name;
    LFX_FIELDS(LFX_DECLARE)
#undef LFX_DECLARE
    int kind = -1;
    bool started = false, running = false, closed = false;
    LFExpressionState() = default;
    LFExpressionState(LFExpressionState&&) = default;
    void clear() {
        closed = true;
        iterator.reset(); item.reset(); item2.reset(); cells.reset();
        globals.reset(); builtins.reset(); metadata.reset();
    }
    ~LFExpressionState() { clear(); }
};
struct LFXRunning {
    LFExpressionState& o;
    explicit LFXRunning(LFExpressionState& value) : o(value) {
        if (o.running) throw nb::value_error("generator already executing");
        o.running = true;
    }
    ~LFXRunning() { o.running = false; }
};
nb::object lfx_cell(nb::handle cell, const char* name) {
    if (!PyCell_Check(cell.ptr())) throw nb::type_error("native expression requires genuine closure cells");
    PyObject* value = PyCell_Get(cell.ptr());
    if (value) return nb::steal<nb::object>(value);
    if (PyErr_Occurred()) throw nb::python_error();
    PyErr_Format(PyExc_NameError,
        "cannot access free variable '%s' where it is not associated with a value in enclosing scope", name);
    nb::python_error error;
    nb::object error_name = lh_check(PyUnicode_FromString(name));
    if (PyObject_SetAttrString(error.value().ptr(), "name", error_name.ptr()) < 0)
        throw nb::python_error();
    throw error;
}
nb::object lfx_capture(LFExpressionState& o, size_t slot, const char* name) {
    return lfx_cell(nb::handle(PyTuple_GET_ITEM(o.cells.ptr(), slot)), name);
}
nb::object lfx_value(LFExpressionState& o) {
    LFSource source{o.globals, o.builtins, o.metadata};
    for (;;) {
        nb::object next = lh_next(o.iterator);
        if (!next.is_valid()) return {};
        if (o.kind == 6) {
            auto pair = lf_unpack(std::move(next), 2);
            lh_assign(o.item, std::move(pair[0])); lh_assign(o.item2, std::move(pair[1]));
        } else lh_assign(o.item, std::move(next));
        switch (o.kind) {
        case 0: // slice(0, s)
            return lh_call(source.global("slice"), {LFF(nb::int_(0)), LFF(o.item)});
        case 1: { // ((grids[i] - center[i]) * normalized_scale[i]) ** 2
            nb::object grid = lh_item(lfx_capture(o, 0, "grids"), o.item);
            nb::object center = lh_item(lfx_capture(o, 1, "center"), o.item);
            nb::object delta = lh_check(PyNumber_Subtract(grid.ptr(), center.ptr()));
            grid.reset(); center.reset();
            nb::object scale = lh_item(lfx_capture(o, 2, "normalized_scale"), o.item);
            nb::object weighted = lh_check(PyNumber_Multiply(delta.ptr(), scale.ptr()));
            delta.reset(); scale.reset();
            return lh_check(PyNumber_Power(weighted.ptr(), nb::int_(2).ptr(), Py_None));
        }
        case 2: // j for j in range(mask.ndim) if j != i
            if (lh_truth(le_compare(o.item, lfx_capture(o, 0, "i"), Py_NE))) return o.item;
            break;
        case 3: // modifier in event.modifiers
            return nb::bool_(le_contains(source.attr(lfx_capture(o, 0, "event"), "modifiers"), o.item));
        case 4: return lh_item(lfx_capture(o, 0, "int_coord"), o.item);
        case 5: return lh_call(source.global("slice"), {LFF(nb::none())});
        case 6: return lh_call(source.global("slice"), {LFF(o.item), LFF(o.item2)});
        default: throw nb::type_error("unknown native frontend expression policy");
        }
    }
}
nb::object lfx_send(LFExpressionState& o, nb::object value) {
    LFXRunning running(o);
    if (o.closed) throw nb::stop_iteration();
    if (!o.started && !value.is_none())
        throw nb::type_error("can't send non-None value to a just-started generator");
    o.started = true;
    try {
        nb::object result = lfx_value(o);
        if (result.is_valid()) return result;
    } catch (nb::python_error& error) {
        o.clear(); lh_generator_failure(error);
    } catch (...) { o.clear(); throw; }
    o.clear(); throw nb::stop_iteration();
}
nb::object lfx_throw(LFExpressionState& o, nb::args arguments) {
    LFXRunning running(o);
    nb::object exception = lh_throw_value(arguments);
    bool suspended = o.started && !o.closed;
    o.clear();
    PyErr_SetObject(reinterpret_cast<PyObject*>(Py_TYPE(exception.ptr())), exception.ptr());
    try { throw nb::python_error(); }
    catch (nb::python_error& error) {
        if (suspended) lh_generator_failure(error);
        throw;
    }
}
nb::object lfx_close(LFExpressionState& o) {
    LFXRunning running(o); o.clear(); return nb::none();
}
int lfx_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) {
        const auto& o = *nb::inst_ptr<LFExpressionState>(self);
#define LFX_VISIT(name) Py_VISIT(o.name.ptr());
        LFX_FIELDS(LFX_VISIT)
#undef LFX_VISIT
    }
    return 0;
}
int lfx_clear(PyObject* self) {
    if (nb::inst_ready(self)) nb::inst_ptr<LFExpressionState>(self)->clear();
    return 0;
}
PyType_Slot lfx_slots[] = {{Py_tp_traverse, reinterpret_cast<void*>(lfx_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(lfx_clear)}, {0, nullptr}};
void lfx_exports(nb::module_& m) {
    nb::class_<LFExpressionState>(m, "ExpressionState", nb::is_weak_referenceable(), nb::type_slots(lfx_slots))
        .def("__iter__", [](nb::object self) { return self; })
        .def("__next__", [](LFExpressionState& o) { return lfx_send(o, nb::none()); })
        .def("send", &lfx_send, nb::arg("value").none())
        .def("throw", &lfx_throw).def("close", &lfx_close);
}

nb::object lfe_prepare(const LFSource& source, int kind, nb::object cells, nb::object transport);
nb::object lfe_start(nb::object function, nb::object iterator);
nb::object lfx_cells(std::initializer_list<nb::object> values) {
    nb::object cells = lh_check(PyTuple_New(static_cast<Py_ssize_t>(values.size())));
    Py_ssize_t i = 0;
    for (const auto& value : values)
        PyTuple_SET_ITEM(cells.ptr(), i++, value.inc_ref().ptr());
    return cells;
}

nb::object lf_create_brush_mask(const LFSource& source, nb::object shape, nb::object radius,
                                 nb::object center, nb::object normalized_scale, nb::object transport) {
    nb::object center_cell = lh_check(PyCell_New(center.ptr())); center.reset();
    nb::object scale_cell = lh_check(PyCell_New(normalized_scale.ptr())); normalized_scale.reset();
    nb::object grids_cell = lh_check(PyCell_New(nullptr));
    nb::object ndim = lh_call(source.global("len"), {LFF(shape)});
    nb::object ogrid = source.attr(source.global("np"), "ogrid");
    nb::object tuple = source.global("tuple");
    nb::object expression_function = lfe_prepare(source, 0, lfx_cells({}), transport);
    nb::object slices = lfe_start(std::move(expression_function), lh_iter(shape));
    nb::object key = lh_call(std::move(tuple), {LFF(slices)});
    slices.reset();
    nb::object grids = lh_item(ogrid, key);
    ogrid.reset(); key.reset();
    if (PyCell_Set(grids_cell.ptr(), grids.ptr()) < 0) throw nb::python_error();
    grids.reset();
    nb::object sum = source.global("sum");
    expression_function = lfe_prepare(source, 1, lfx_cells({grids_cell, center_cell, scale_cell}), transport);
    nb::object iterator = lh_iter(lh_call(source.global("range"), {LFF(ndim)}));
    nb::object distances = lfe_start(std::move(expression_function), std::move(iterator));
    nb::object dist_sq = lh_call(std::move(sum), {LFF(distances)});
    distances.reset();
    nb::object radius_sq = lh_check(PyNumber_Power(radius.ptr(), nb::int_(2).ptr(), Py_None));
    return le_compare(std::move(dist_sq), std::move(radius_sq), Py_LE);
}

nb::object lf_compute_mask_bbox(const LFSource& source, nb::object mask, nb::object transport) {
    auto N = [&](const char* name) { return source.attr(source.global("np"), name); };
    nb::object i_cell = lh_check(PyCell_New(nullptr));
    nb::object min_vals = lh_call(N("empty"), {LFF(source.attr(mask, "ndim"))},
                                  {LFF(source.global("int"))}, source.keyword("dtype"));
    nb::object max_vals = lh_call(N("empty"), {LFF(source.attr(mask, "ndim"))},
                                  {LFF(source.global("int"))}, source.keyword("dtype"));
    nb::object iterator = lh_iter(lh_call(source.global("range"), {LFF(source.attr(mask, "ndim"))}));
    nb::object axes, any_on_axis;
    for (;;) {
        nb::object next = lh_next(iterator);
        if (!next.is_valid()) break;
        if (PyCell_Set(i_cell.ptr(), next.ptr()) < 0) throw nb::python_error();
        next.reset();
        nb::object tuple = source.global("tuple");
        nb::object expression_function = lfe_prepare(source, 2, lfx_cells({i_cell}), transport);
        nb::object inner = lh_iter(lh_call(source.global("range"), {LFF(source.attr(mask, "ndim"))}));
        nb::object expression = lfe_start(std::move(expression_function), std::move(inner));
        lh_assign(axes, lh_call(std::move(tuple), {LFF(expression)}));
        expression.reset();
        lh_assign(any_on_axis, lh_call(N("any"), {LFF(mask)}, {LFF(axes)}, source.keyword("axis")));
        nb::object minimum = lh_call(N("argmax"), {LFF(any_on_axis)});
        nb::object index = lfx_cell(i_cell, "i");
        int status = PyObject_SetItem(min_vals.ptr(), index.ptr(), minimum.ptr());
        index.reset(); minimum.reset();
        if (status < 0) throw nb::python_error();
        nb::object length = lh_call(source.global("len"), {LFF(any_on_axis)});
        nb::object argmax = N("argmax");
        nb::object step = nb::int_(-1);
        nb::object reverse = lh_check(PySlice_New(Py_None, Py_None, step.ptr()));
        nb::object maximum = lh_call(std::move(argmax), {LFF(lh_item(any_on_axis, reverse))});
        reverse.reset(); step.reset();
        nb::object limit = lh_check(PyNumber_Subtract(length.ptr(), maximum.ptr()));
        length.reset(); maximum.reset();
        index = lfx_cell(i_cell, "i");
        status = PyObject_SetItem(max_vals.ptr(), index.ptr(), limit.ptr());
        index.reset(); limit.reset();
        if (status < 0) throw nb::python_error();
    }
    return nb::make_tuple(min_vals, max_vals);
}

nb::object lf_brush_mouse_call(const LFSource& source, nb::object self,
                              nb::object layer, nb::object event, nb::object transport) {
    nb::object event_cell = lh_check(PyCell_New(event.ptr())); event.reset();
    auto S = [&](const char* name) { return source.attr(self, name); };
    auto E = [&](const char* name) { return source.attr(lfx_cell(event_cell, "event"), name); };
    nb::object all = source.global("all");
    nb::object expression_function = lfe_prepare(source, 3, lfx_cells({event_cell}), transport);
    nb::object iterator = lh_iter(source.global("BRUSH_SIZE_ON_MOUSE_MOVE_MODIFIERS_PARTS"));
    nb::object expression = lfe_start(std::move(expression_function), std::move(iterator));
    bool matching = lh_truth(lh_call(std::move(all), {LFF(expression)}));
    expression.reset();
    if (matching) {
        nb::object pos = E("pos");
        if (S("init_pos").is_none()) {
            lf_store(source, self, "init_pos", pos);
            lf_store(source, self, "init_brush_size", source.attr(layer, "brush_size"));
            lf_store(source, layer, "cursor", le_literal(source.metadata, "circle_frozen"));
        } else {
            nb::object round = source.global("round");
            nb::object x = lh_item(pos, nb::int_(0));
            nb::object initial_x = lh_item(S("init_pos"), nb::int_(0));
            nb::object difference = lh_check(PyNumber_Subtract(x.ptr(), initial_x.ptr()));
            x.reset(); initial_x.reset();
            nb::object zoom = E("camera_zoom");
            nb::object quotient = lh_check(PyNumber_TrueDivide(difference.ptr(), zoom.ptr()));
            difference.reset(); zoom.reset();
            nb::object delta = lh_call(std::move(round), {LFF(quotient)});
            quotient.reset();
            nb::object initial = S("init_brush_size");
            nb::object new_size = lh_check(PyNumber_Add(initial.ptr(), delta.ptr()));
            initial.reset();
            nb::object bounded = lh_call(source.global("max"), {LFF(new_size), LFF(S("min_brush_size"))});
            lf_store(source, layer, "brush_size", bounded);
        }
    } else {
        lf_store(source, self, "init_pos", nb::none());
        if (lh_truth(le_compare(source.attr(layer, "cursor"), le_literal(source.metadata, "circle_frozen"), Py_EQ)))
            lf_store(source, layer, "cursor", le_literal(source.metadata, "circle"));
    }
    return nb::none();
}
#undef LFX_FIELDS
#undef LFF

#define LFF(value) [&]() -> nb::object { return (value); }
nb::object lf_join_parts(std::initializer_list<nb::object> parts) {
    nb::object values = lh_check(PyTuple_New(static_cast<Py_ssize_t>(parts.size())));
    Py_ssize_t i = 0;
    for (const auto& part : parts) PyTuple_SET_ITEM(values.ptr(), i++, part.inc_ref().ptr());
    nb::object empty = lh_check(PyUnicode_FromString(""));
    return lh_check(PyUnicode_Join(empty.ptr(), values.ptr()));
}

nb::object lf_wrong_label_init(const LFSource& source, nb::object self, nb::object dtype,
                               nb::object value, nb::object lower, nb::object upper,
                               nb::object message, nb::object class_cell) {
    lf_store(source, self, "dtype", dtype);
    lf_store(source, self, "value", value);
    lf_store(source, self, "lower_bound", lower);
    lf_store(source, self, "upper_bound", upper);
    nb::object prefix = le_literal(source.metadata, "The value ");
    nb::object formatted_value = lf_format(value);
    nb::object dtype_prefix = le_literal(source.metadata, " is out of bounds for dtype ");
    nb::object formatted_dtype = lf_format(dtype);
    nb::object lower_prefix = le_literal(source.metadata, " that allow for range [");
    nb::object lower_int = lh_call(source.global("int"), {LFF(lower)});
    nb::object formatted_lower = lf_format(std::move(lower_int));
    nb::object comma = le_literal(source.metadata, ", ");
    nb::object upper_int = lh_call(source.global("int"), {LFF(upper)});
    nb::object formatted_upper = lf_format(std::move(upper_int));
    nb::object suffix = le_literal(source.metadata, "].");
    nb::object text = lf_join_parts({prefix, formatted_value, dtype_prefix, formatted_dtype,
                                     lower_prefix, formatted_lower, comma, formatted_upper, suffix});
    prefix.reset(); formatted_value.reset(); dtype_prefix.reset(); formatted_dtype.reset();
    lower_prefix.reset(); formatted_lower.reset(); comma.reset(); formatted_upper.reset(); suffix.reset();
    if (lh_truth(message)) {
        nb::object formatted_message = lf_format(message);
        nb::object space = le_literal(source.metadata, " ");
        nb::object formatted_text = lf_format(text);
        lh_assign(text, lf_join_parts({formatted_message, space, formatted_text}));
    }
    lf_store(source, self, "text", text);
    nb::object super = source.global("super");
    nb::object cls;
#if PY_MINOR_VERSION >= 12
    // LOAD_SUPER_ATTR loads the real __class__ cell before it distinguishes
    // the builtin from an explicit foreign super callable.
    cls = lfx_cell(class_cell, "__class__");
#endif
    nb::object parent;
    if (super.ptr() == reinterpret_cast<PyObject*>(&PySuper_Type)) {
        // The genuine builtin's zero-argument operation uses the actual class
        // cell and first argument. Explicit foreign super callables retain
        // their source zero-argument call below.
        if (!PyCell_Check(class_cell.ptr())) throw nb::type_error("genuine __class__ cell is required");
#if PY_MINOR_VERSION < 12
        cls = nb::steal<nb::object>(PyCell_Get(class_cell.ptr()));
        if (!cls.is_valid()) {
            if (PyErr_Occurred()) throw nb::python_error();
            PyErr_SetString(PyExc_RuntimeError, "super(): empty __class__ cell");
            throw nb::python_error();
        }
#endif
        if (!PyType_Check(cls.ptr())) {
            PyErr_Format(PyExc_RuntimeError, "super(): __class__ is not a type (%s)", Py_TYPE(cls.ptr())->tp_name);
            throw nb::python_error();
        }
        parent = lh_call(std::move(super), {LFF(cls), LFF(self)});
    } else {
        parent = lh_call(std::move(super));
    }
    lh_call(source.attr(std::move(parent), "__init__"), {LFF(text)});
    return nb::none();
}
#undef LFF

// Handwritten factory and dispatch application policy. Original callbacks are
// read from their genuine cells only at the fallback point, never snapshotted.
#define LFF(value) [&]() -> nb::object { return (value); }
nb::object lf_interpolation_adapter_call(const LFSource& source, nb::object old_coord,
                                         nb::object new_coord, nb::object brush_size,
                                         nb::object original_cell) {
    auto N = [&](const char* name) { return source.attr(source.global("np"), name); };
    if (old_coord.is_none()) lh_assign(old_coord, new_coord);
    if (new_coord.is_none()) lh_assign(new_coord, old_coord);
    nb::object old = lh_call(N("asarray"), {LFF(old_coord)});
    nb::object fresh = lh_call(N("asarray"), {LFF(new_coord)});
    bool rejected = lh_truth(le_compare(source.attr(old, "ndim"), nb::int_(1), Py_NE));
    if (!rejected) rejected = lh_truth(le_compare(source.attr(fresh, "ndim"), nb::int_(1), Py_NE));
    if (!rejected) {
        nb::object left = source.attr(old, "shape");
        nb::object right = source.attr(fresh, "shape");
        rejected = lh_truth(le_compare(std::move(left), std::move(right), Py_NE));
    }
    if (!rejected) {
        nb::object left = source.attr(old, "dtype"), right = N("float64");
        rejected = lh_truth(le_compare(std::move(left), std::move(right), Py_NE));
    }
    if (!rejected) {
        nb::object left = source.attr(fresh, "dtype"), right = N("float64");
        rejected = lh_truth(le_compare(std::move(left), std::move(right), Py_NE));
    }
    if (!rejected) rejected = lh_truth(le_compare(source.attr(old, "size"), nb::int_(0), Py_EQ));
    if (!rejected) rejected = !lh_truth(lh_call(N("isfinite"), {LFF(brush_size)}));
    if (!rejected) rejected = lh_truth(le_compare(brush_size, nb::int_(0), Py_LE));
    if (!rejected) rejected = !lh_truth(lh_call(source.attr(lh_call(N("isfinite"), {LFF(old)}), "all")));
    if (!rejected) rejected = !lh_truth(lh_call(source.attr(lh_call(N("isfinite"), {LFF(fresh)}), "all")));
    if (rejected)
        return lh_call(lfx_cell(original_cell, "original"), {LFF(old_coord), LFF(new_coord), LFF(brush_size)});
    return lh_call(source.attr(source.global("_native"), "interpolate_coordinates"),
                   {LFF(old), LFF(fresh), LFF(brush_size)});
}

nb::object lf_ray_adapter_call(const LFSource& source, nb::object data,
                               nb::object start_point, nb::object end_point,
                               nb::object original_cell) {
    auto N = [&](const char* name) { return source.attr(source.global("np"), name); };
    nb::object start = lh_call(N("asarray"), {LFF(start_point)});
    nb::object end = lh_call(N("asarray"), {LFF(end_point)});
    bool rejected = !lh_truth(lh_call(source.global("_plain_real"), {LFF(data)}));
    if (!rejected) rejected = lh_truth(le_compare(source.attr(data, "ndim"), nb::int_(0), Py_EQ));
    if (!rejected) rejected = lh_truth(le_compare(source.attr(data, "size"), nb::int_(0), Py_EQ));
    if (!rejected) {
        nb::object shape = source.attr(start, "shape");
        rejected = lh_truth(le_compare(std::move(shape), nb::make_tuple(source.attr(data, "ndim")), Py_NE));
    }
    if (!rejected) {
        nb::object shape = source.attr(end, "shape");
        rejected = lh_truth(le_compare(std::move(shape), nb::make_tuple(source.attr(data, "ndim")), Py_NE));
    }
    if (!rejected) {
        nb::object dtype = source.attr(start, "dtype"), expected = N("float64");
        rejected = lh_truth(le_compare(std::move(dtype), std::move(expected), Py_NE));
    }
    if (!rejected) {
        nb::object dtype = source.attr(end, "dtype"), expected = N("float64");
        rejected = lh_truth(le_compare(std::move(dtype), std::move(expected), Py_NE));
    }
    if (!rejected) rejected = !lh_truth(lh_call(source.attr(lh_call(N("isfinite"), {LFF(start)}), "all")));
    if (!rejected) rejected = !lh_truth(lh_call(source.attr(lh_call(N("isfinite"), {LFF(end)}), "all")));
    if (rejected)
        return lh_call(lfx_cell(original_cell, "original"), {LFF(data), LFF(start_point), LFF(end_point)});
    nb::object result = lh_call(source.attr(source.global("_native"), "first_nonzero_coordinate"),
                                {LFF(data), LFF(start), LFF(end)});
    if (result.is_none()) return nb::none();
    return lh_call(source.attr(result, "astype"), {LFF(source.global("int"))},
                   {LFF(nb::bool_(false))}, source.keyword("copy"));
}

// Direct CPython entry points preserve native completion/error categories.
void lf_adapter_error() {
    try { throw; }
    catch (nb::python_error& error) { error.restore(); }
    catch (const nb::builtin_exception& error) {
        PyObject* type = PyExc_RuntimeError;
        switch (error.type()) {
        case nb::exception_type::stop_iteration: type = PyExc_StopIteration; break;
        case nb::exception_type::value_error: type = PyExc_ValueError; break;
        case nb::exception_type::type_error: type = PyExc_TypeError; break;
        case nb::exception_type::index_error: type = PyExc_IndexError; break;
        case nb::exception_type::key_error: type = PyExc_KeyError; break;
        case nb::exception_type::attribute_error: type = PyExc_AttributeError; break;
        default: break;
        }
        PyErr_SetString(type, error.what());
    }
    catch (const std::bad_alloc&) { PyErr_NoMemory(); }
    catch (const std::exception& error) { PyErr_SetString(PyExc_RuntimeError, error.what()); }
}
LFSource lf_adapter_frame_source() {
    PyFrameObject* frame = PyEval_GetFrame();
    if (!frame) throw nb::type_error("actual adapter source frame is required");
    nb::object code = lh_check(reinterpret_cast<PyObject*>(PyFrame_GetCode(frame)));
    nb::object constants = lh_check(PyObject_GetAttrString(code.ptr(), "co_consts"));
    Py_ssize_t count = PyTuple_GET_SIZE(constants.ptr());
    if (count < 1) throw nb::type_error("adapter original metadata is required");
    nb::object metadata = nb::borrow<nb::object>(PyTuple_GET_ITEM(constants.ptr(), count - 1));
    lh_metadata_values(metadata, 0); lh_metadata_values(metadata, 1);
    return {lh_check(PyFrame_GetGlobals(frame)), lh_check(PyFrame_GetBuiltins(frame)), std::move(metadata)};
}
PyObject* lf_adapter_frame(PyObject*, PyObject* args) {
    try {
        if (PyTuple_GET_SIZE(args) != 5 || !PyLong_CheckExact(PyTuple_GET_ITEM(args, 0)))
            throw nb::type_error("adapter parameter transport changed");
        long kind = PyLong_AsLong(PyTuple_GET_ITEM(args, 0));
        if (kind == -1 && PyErr_Occurred()) throw nb::python_error();
        auto A = [&](Py_ssize_t i) { return nb::borrow<nb::object>(PyTuple_GET_ITEM(args, i)); };
        nb::object supplier = A(4);
        if (!PyFunction_Check(supplier.ptr())) throw nb::type_error("actual adapter closure transport is required");
        PyObject* closure = PyFunction_GetClosure(supplier.ptr());
        if (!closure || PyTuple_GET_SIZE(closure) != 1 || !PyCell_Check(PyTuple_GET_ITEM(closure, 0)))
            throw nb::type_error("actual adapter original cell is required");
        nb::object original_cell = nb::borrow<nb::object>(PyTuple_GET_ITEM(closure, 0));
        LFSource source = lf_adapter_frame_source();
        if (kind == 0) return lf_interpolation_adapter_call(source, A(1), A(2), A(3), original_cell).release().ptr();
        if (kind == 1) return lf_ray_adapter_call(source, A(1), A(2), A(3), original_cell).release().ptr();
        throw nb::type_error("invalid adapter call entry");
    } catch (...) { lf_adapter_error(); return nullptr; }
}
PyMethodDef lf_adapter_frame_definition = {"adapter_frame", lf_adapter_frame, METH_VARARGS, nullptr};
PyObject* lf_generic_frame(PyObject*, PyObject* args) {
    try {
        if (PyTuple_GET_SIZE(args) != 4 || !PyLong_CheckExact(PyTuple_GET_ITEM(args, 0)))
            throw nb::type_error("generic helper parameter transport changed");
        long kind = PyLong_AsLong(PyTuple_GET_ITEM(args, 0));
        if (kind == -1 && PyErr_Occurred()) throw nb::python_error();
        auto A = [&](Py_ssize_t i) { return nb::borrow<nb::object>(PyTuple_GET_ITEM(args, i)); };
        LFSource source = lf_adapter_frame_source();
        if (kind == 0) return lf_interpolate(source, A(1), A(2), A(3)).release().ptr();
        if (kind == 1) return lf_first_nonzero(source, A(1), A(2), A(3)).release().ptr();
        throw nb::type_error("invalid generic helper entry");
    } catch (...) { lf_adapter_error(); return nullptr; }
}
PyMethodDef lf_generic_frame_definition = {"generic_frame", lf_generic_frame, METH_VARARGS, nullptr};

// Probe preparation accepts explicit source templates. Full exact-code and
// canonical declaration admission remain prerequisites for production use.
nb::object lf_adapter_shell_code(nb::handle shell, PyCodeObject* original, bool generic) {
    if (!PyCode_Check(shell.ptr())) throw nb::type_error("adapter shell code is required");
    auto* code = reinterpret_cast<PyCodeObject*>(shell.ptr());
    if (original->co_argcount != 3 || code->co_argcount != 3 ||
        PyCode_GetNumFree(original) != (generic ? 0 : 1) ||
        PyCode_GetNumFree(code) != (generic ? 0 : 1))
        throw nb::type_error("adapter probe requires the original three-argument closure ABI");
    nb::object constants = lh_check(PyTuple_New(PyTuple_GET_SIZE(code->co_consts) + 1));
    nb::object module_name = nb::str("naparixx._native");
    nb::object entry = lh_check(PyCFunction_NewEx(generic ? &lf_generic_frame_definition : &lf_adapter_frame_definition,
                                                 nullptr, module_name.ptr()));
    bool replaced = false;
    for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(code->co_consts); ++i) {
        PyObject* value = PyTuple_GET_ITEM(code->co_consts, i);
        if (value == Py_Ellipsis) { value = entry.ptr(); replaced = true; }
        PyTuple_SET_ITEM(constants.ptr(), i, nb::borrow<nb::object>(value).release().ptr());
    }
    if (!replaced) throw nb::type_error("adapter native entry marker is missing");
    PyTuple_SET_ITEM(constants.ptr(), PyTuple_GET_SIZE(code->co_consts), le_metadata(original).release().ptr());
    nb::dict kwargs;
    kwargs["co_consts"] = constants;
    kwargs["co_filename"] = nb::borrow<nb::object>(original->co_filename);
    kwargs["co_name"] = nb::borrow<nb::object>(original->co_name);
    kwargs["co_qualname"] = nb::borrow<nb::object>(original->co_qualname);
    kwargs["co_firstlineno"] = nb::int_(original->co_firstlineno);
    kwargs["co_flags"] = nb::int_(original->co_flags);
    nb::object replace = lh_check(PyObject_GetAttrString(shell.ptr(), "replace"));
    nb::object args = lh_check(PyTuple_New(0));
    return lh_check(PyObject_Call(replace.ptr(), args.ptr(), kwargs.ptr()));
}

nb::object lf_adapter_factory(const LFSource& source, nb::object original,
                               nb::object shell_code, PyCodeObject* original_call) {
    nb::object original_cell = lh_check(PyCell_New(original.ptr())); original.reset();
    nb::object decorator = lh_call(source.global("wraps"), {LFF(lfx_cell(original_cell, "original"))});
    nb::object code = lf_adapter_shell_code(shell_code, original_call, false);
    nb::object function = lh_check(PyFunction_NewWithQualName(code.ptr(), source.globals.ptr(), original_call->co_qualname));
    nb::object closure = nb::make_tuple(original_cell);
    if (PyFunction_SetClosure(function.ptr(), closure.ptr()) < 0) throw nb::python_error();
    return lh_call(std::move(decorator), {LFF(function)});
}
#undef LFF

// Handwritten import protocol and final frontend mask policies. Import locals
// are an explicit transport input, not an independently mutable app mirror.
#define LFF(value) [&]() -> nb::object { return (value); }
nb::object lfi_optional(nb::handle object, nb::handle name) {
    PyObject* value = nullptr;
#if PY_MINOR_VERSION >= 13
    int status = PyObject_GetOptionalAttr(object.ptr(), name.ptr(), &value);
#else
    int status = _PyObject_LookupAttr(object.ptr(), name.ptr(), &value);
#endif
    if (status < 0) throw nb::python_error();
    return nb::steal<nb::object>(value);
}
nb::object lfi_optional(nb::handle object, const char* name) {
    nb::object key = lh_check(PyUnicode_InternFromString(name));
    return lfi_optional(object, key);
}

nb::object lfi_import_name(const LFSource& source, const char* module,
                           nb::object fromlist, nb::object import_locals) {
    nb::object name = le_name(source.metadata, module);
    nb::object key = lh_check(PyUnicode_InternFromString("__import__"));
    PyObject* raw = PyObject_GetItem(source.builtins.ptr(), key.ptr());
    if (!raw) {
        if (PyErr_ExceptionMatches(PyExc_KeyError)) {
            PyErr_Clear(); PyErr_SetString(PyExc_ImportError, "__import__ not found");
        }
        throw nb::python_error();
    }
    return lh_call(nb::steal<nb::object>(raw),
        {LFF(name), LFF(source.globals), LFF(import_locals), LFF(fromlist), LFF(nb::int_(0))});
}

#if PY_MINOR_VERSION >= 13
// Exact interpreter configuration is needed here; sys.path and sys.flags may
// have been rebound by callbacks. This exported private ABI is version-gated.
extern "C" PyAPI_FUNC(const PyConfig*) _Py_GetConfig(void);
bool lfi_shadowing(nb::handle origin) {
    if (!origin.is_valid()) return false;
    const PyConfig* config = _Py_GetConfig();
    if (config->safe_path || !config->sys_path_0) return false;
    wchar_t buffer[MAXPATHLEN + 1];
    Py_ssize_t size = PyUnicode_AsWideChar(origin.ptr(), buffer, MAXPATHLEN);
    if (size < 0) throw nb::python_error();
    buffer[size] = L'\0';
    std::wstring root(buffer);
    auto position = root.find_last_of(SEP);
    if (position == std::wstring::npos) return false;
    if (root.substr(position + 1) == L"__init__.py") {
        root.resize(position); position = root.find_last_of(SEP);
        if (position == std::wstring::npos) return false;
    }
    root.resize(position);
    std::wstring path0(config->sys_path_0);
    if (path0.empty()) {
#ifdef MS_WINDOWS
        wchar_t cwd[MAXPATHLEN];
        if (!::_wgetcwd(cwd, MAXPATHLEN)) return false;
        path0 = cwd;
#else
        char cwd[MAXPATHLEN];
        if (!::getcwd(cwd, MAXPATHLEN)) return false;
        nb::object unicode = lh_check(PyUnicode_DecodeFSDefault(cwd));
        wchar_t wide[MAXPATHLEN];
        Py_ssize_t count = PyUnicode_AsWideChar(unicode.ptr(), wide, MAXPATHLEN - 1);
        if (count < 0) { PyErr_Clear(); return false; }
        wide[count] = L'\0'; path0 = wide;
#endif
    }
    return path0 == root;
}
#endif

struct LFIKeys {
    nb::object module_name = lh_check(PyUnicode_InternFromString("__name__"));
    nb::object spec = lh_check(PyUnicode_InternFromString("__spec__"));
    nb::object initializing = lh_check(PyUnicode_InternFromString("_initializing"));
};
[[noreturn]] void lfi_missing_member(nb::object imported, nb::object name,
                                     nb::object module_name, const LFIKeys& keys) {
    nb::object printable;
    nb::object origin, spec, message;
    bool initializing = false, shadowing = false, stdlib_shadow = false;
#if PY_MINOR_VERSION >= 13
    printable = module_name.is_valid() ? module_name : nb::str("<unknown module name>");
    spec = lfi_optional(imported, keys.spec);
    if (spec.is_valid()) {
        nb::object located = lfi_optional(spec, "has_location");
        if (located.is_valid() && lh_truth(std::move(located))) {
            origin = lfi_optional(spec, "origin");
            if (origin.is_valid() && !PyUnicode_Check(origin.ptr())) origin.reset();
        }
        shadowing = lfi_shadowing(origin);
        if (shadowing) {
            PyObject* modules = PySys_GetObject("stdlib_module_names");
            if (modules && PyAnySet_Check(modules)) {
                int contains = PySet_Contains(modules, printable.ptr());
                if (contains < 0) throw nb::python_error();
                stdlib_shadow = contains != 0;
            }
        }
        if (!origin.is_valid() && PyModule_Check(imported.ptr())) {
            origin = nb::steal<nb::object>(PyModule_GetFilenameObject(imported.ptr()));
            if (!origin.is_valid()) {
                if (!PyErr_ExceptionMatches(PyExc_SystemError)) throw nb::python_error();
                PyErr_Clear();
            }
        }
        if (!stdlib_shadow) {
            nb::object value = lfi_optional(spec, keys.initializing);
            if (value.is_valid()) initializing = lh_truth(std::move(value));
        }
    }
#else
    origin = nb::steal<nb::object>(PyModule_GetFilenameObject(imported.ptr()));
    printable = module_name.is_valid() ? module_name : nb::str("<unknown module name>");
    if (!origin.is_valid() || !PyUnicode_Check(origin.ptr())) {
        origin.reset(); PyErr_Clear();
    } else {
        // Older interpreters deliberately discard spec lookup/truth errors.
        PyObject* raw = PyObject_GetAttr(imported.ptr(), keys.spec.ptr());
        spec = nb::steal<nb::object>(raw);
        if (spec.is_valid()) {
            try {
#if PY_MINOR_VERSION == 11
                nb::object value = lh_check(PyObject_GetAttr(spec.ptr(), keys.initializing.ptr()));
#else
                nb::object value = lfi_optional(spec, keys.initializing);
#endif
                if (value.is_valid()) initializing = lh_truth(std::move(value));
            } catch (nb::python_error&) { PyErr_Clear(); }
        } else PyErr_Clear();
    }
#endif
    if (stdlib_shadow) {
        message = lh_check(PyUnicode_FromFormat(
            "cannot import name %R from %R (consider renaming %R since it has the same name as the standard library module named %R and prevents importing that standard library module)",
            name.ptr(), printable.ptr(), origin.ptr(), printable.ptr()));
    } else if (initializing && shadowing) {
        message = lh_check(PyUnicode_FromFormat(
            "cannot import name %R from %R (consider renaming %R if it has the same name as a library you intended to import)",
            name.ptr(), printable.ptr(), origin.ptr()));
    } else if (initializing && origin.is_valid()) {
        message = lh_check(PyUnicode_FromFormat(
            "cannot import name %R from partially initialized module %R (most likely due to a circular import) (%S)",
            name.ptr(), printable.ptr(), origin.ptr()));
    } else if (initializing) {
        message = lh_check(PyUnicode_FromFormat(
            "cannot import name %R from partially initialized module %R (most likely due to a circular import)",
            name.ptr(), printable.ptr()));
    } else if (origin.is_valid()) {
        message = lh_check(PyUnicode_FromFormat("cannot import name %R from %R (%S)",
                                                name.ptr(), printable.ptr(), origin.ptr()));
    } else {
        message = lh_check(PyUnicode_FromFormat("cannot import name %R from %R (unknown location)",
                                                name.ptr(), printable.ptr()));
    }
    PyErr_SetImportError(message.ptr(), module_name.ptr(), origin.ptr());
    nb::python_error error;
#if PY_MINOR_VERSION >= 12
    // CPython's diagnostic-only hidden field is not an independent state copy.
    auto* detail = reinterpret_cast<PyImportErrorObject*>(error.value().ptr());
    Py_XSETREF(detail->name_from, name.inc_ref().ptr());
#endif
    throw error;
}

nb::object lfi_import_from(nb::object imported, nb::object name) {
    // Create protocol identifiers before a failed name lookup can leave an
    // active exception. No identifier allocation may disturb that error path.
    LFIKeys keys;
    nb::object value = lfi_optional(imported, name);
    if (value.is_valid()) return value;
    nb::object module_name;
#if PY_MINOR_VERSION >= 13
    module_name = lfi_optional(imported, keys.module_name);
#else
    module_name = nb::steal<nb::object>(PyObject_GetAttr(imported.ptr(), keys.module_name.ptr()));
    // Preserve a pending lookup error until the diagnostic path replaces it.
#endif
    if (module_name.is_valid() && !PyUnicode_Check(module_name.ptr())) module_name.reset();
    if (module_name.is_valid()) {
        nb::object full_name = lh_check(PyUnicode_FromFormat("%U.%U", module_name.ptr(), name.ptr()));
        value = nb::steal<nb::object>(PyImport_GetModule(full_name.ptr()));
        if (value.is_valid()) return value;
        if (PyErr_Occurred()) throw nb::python_error();
    }
    lfi_missing_member(std::move(imported), std::move(name), std::move(module_name), keys);
}

nb::object lf_create_polygon_mask(const LFSource& source, nb::object points2d,
                                   nb::object shape, nb::object import_locals) {
    nb::object imported = lfi_import_name(source, "PIL", source.keyword("Image,ImageDraw"), import_locals);
    nb::object image = lfi_import_from(imported, le_name(source.metadata, "Image"));
    nb::object image_draw = lfi_import_from(imported, le_name(source.metadata, "ImageDraw"));
    imported.reset();
    nb::object create = source.attr(image, "new");
    nb::object mode = le_literal(source.metadata, "L");
    nb::object width = lh_item(shape, nb::int_(1)), height = lh_item(shape, nb::int_(0));
    nb::object dimensions = nb::make_tuple(width, height);
    width.reset(); height.reset();
    nb::object img = lh_call(std::move(create), {LFF(mode), LFF(dimensions), LFF(nb::int_(0))});
    mode.reset(); dimensions.reset();
    nb::object draw = lh_call(source.attr(image_draw, "Draw"), {LFF(img)});
    nb::object points_pil = lh_check(PyList_New(0));
    nb::object iterator = lh_iter(points2d), p;
    for (;;) {
        nb::object next = lh_next(iterator);
        if (!next.is_valid()) break;
        lh_assign(p, std::move(next));
        nb::object tuple = source.global("tuple");
        nb::object step = nb::int_(-1);
        nb::object reverse = lh_check(PySlice_New(Py_None, Py_None, step.ptr()));
        nb::object point = lh_call(std::move(tuple), {LFF(lh_item(p, reverse))});
        if (PyList_Append(points_pil.ptr(), point.ptr()) < 0) throw nb::python_error();
    }
    iterator.reset(); p.reset();
    lh_call(source.attr(draw, "polygon"), {LFF(points_pil)},
        {LFF(nb::int_(1)), LFF(nb::int_(1))}, source.keyword("outline,fill"));
    return lh_call(source.attr(source.global("np"), "array"), {LFF(img)},
                    {LFF(source.global("bool"))}, source.keyword("dtype"));
}

nb::object lf_flood_bbox(const LFSource& source, nb::object self, nb::object coord,
                         nb::object new_label, nb::object dims_to_paint,
                         nb::object transport, nb::object import_locals) {
    auto S = [&](const char* name) { return source.attr(self, name); };
    auto N = [&](const char* name) { return source.attr(source.global("np"), name); };
    nb::object int_coord_cell = lh_check(PyCell_New(nullptr));
    nb::object imported = lfi_import_name(source, "skimage.segmentation", source.keyword("flood"), import_locals);
    nb::object flood = lfi_import_from(imported, le_name(source.metadata, "flood"));
    imported.reset();
    nb::object tuple = source.global("tuple");
    nb::object rounded = lh_call(N("round"), {LFF(coord)});
    nb::object integers = lh_call(source.attr(std::move(rounded), "astype"), {LFF(source.global("int"))});
    nb::object int_coord = lh_call(std::move(tuple), {LFF(integers)});
    integers.reset();
    if (PyCell_Set(int_coord_cell.ptr(), int_coord.ptr()) < 0) throw nb::python_error();
    bool outside = lh_truth(lh_call(N("any"), {LFF(lh_call(N("less"), {LFF(int_coord), LFF(nb::int_(0))}))}));
    if (!outside) outside = lh_truth(lh_call(N("any"),
        {LFF(lh_call(N("greater_equal"), {LFF(int_coord), LFF(source.attr(S("data"), "shape"))}))}));
    if (outside) return nb::none();
    nb::object old_label = lh_call(source.attr(lh_call(N("asarray"), {LFF(lh_item(S("data"), int_coord))}), "item"));
    if (lh_truth(le_compare(old_label, new_label, Py_EQ))) return nb::none();
    nb::object source_label;
    if (lh_truth(S("preserve_labels"))) {
        source_label = lh_call(S("_get_preserve_labels_source_label"), {LFF(new_label)});
        if (lh_truth(le_compare(old_label, source_label, Py_NE))) return nb::none();
    }
    nb::object data_slice_list = lh_call(source.global("list"), {LFF(int_coord)});
    nb::object iterator = lh_iter(dims_to_paint), dim;
    for (;;) {
        nb::object next = lh_next(iterator);
        if (!next.is_valid()) break;
        lh_assign(dim, std::move(next));
        nb::object slice = lh_call(source.global("slice"), {LFF(nb::none())});
        if (PyObject_SetItem(data_slice_list.ptr(), dim.ptr(), slice.ptr()) < 0) throw nb::python_error();
    }
    iterator.reset();
    nb::object data_slice = lh_call(source.global("tuple"), {LFF(data_slice_list)});
    nb::object labels = lh_call(N("asarray"), {LFF(lh_item(S("data"), data_slice))});
    tuple = source.global("tuple");
    nb::object expression_function = lfe_prepare(source, 4, lfx_cells({int_coord_cell}), transport);
    nb::object expression = lfe_start(std::move(expression_function), lh_iter(dims_to_paint));
    nb::object slice_coord = lh_call(std::move(tuple), {LFF(expression)});
    expression.reset();
    nb::object mask;
    if (lh_truth(S("contiguous")))
        mask = lh_call(flood, {LFF(labels), LFF(slice_coord)}, {LFF(nb::int_(1))}, source.keyword("connectivity"));
    else mask = le_compare(labels, old_label, Py_EQ);
    nb::object mask_is_full = lh_call(source.attr(mask, "all"));
    nb::object bbox_slices, cropped_mask, min_vals, max_vals;
    if (lh_truth(mask_is_full)) {
        tuple = source.global("tuple");
        expression_function = lfe_prepare(source, 5, lfx_cells({}), transport);
        expression = lfe_start(std::move(expression_function), lh_iter(dims_to_paint));
        bbox_slices = lh_call(std::move(tuple), {LFF(expression)});
        expression.reset();
        cropped_mask = mask;
        min_vals = lh_call(N("zeros"), {LFF(source.attr(mask, "ndim"))},
                           {LFF(source.global("int"))}, source.keyword("dtype"));
        max_vals = lh_call(N("array"), {LFF(source.attr(mask, "shape"))});
    } else {
        auto pair = lf_unpack(lh_call(S("_compute_mask_bbox"), {LFF(mask)}), 2);
        min_vals = std::move(pair[0]); max_vals = std::move(pair[1]);
        tuple = source.global("tuple");
        expression_function = lfe_prepare(source, 6, lfx_cells({}), transport);
        iterator = lh_iter(lh_call(source.global("zip"), {LFF(min_vals), LFF(max_vals)},
                                   {LFF(nb::bool_(true))}, source.keyword("strict")));
        expression = lfe_start(std::move(expression_function), std::move(iterator));
        bbox_slices = lh_call(std::move(tuple), {LFF(expression)});
        expression.reset();
        cropped_mask = lh_item(mask, bbox_slices);
    }
    return nb::make_tuple(cropped_mask, min_vals, max_vals, lh_item(labels, bbox_slices));
}
#undef LFF

// Exact-function provider admission. The caller supplies reviewed complete
// compiler metadata; runtime compilation or executable original-body retention
// is prohibited. Foreign function bodies remain genuine external callbacks.
bool lf_grid_profile_code(nb::handle code, nb::handle expected);
bool lf_grid_profile_atom(nb::handle value, nb::handle expected) {
    if (!PyTuple_CheckExact(expected.ptr()) || PyTuple_GET_SIZE(expected.ptr()) < 1)
        throw nb::type_error("Grid profile atom is malformed");
    const char* tag = PyUnicode_AsUTF8(PyTuple_GET_ITEM(expected.ptr(), 0));
    if (!tag) throw nb::python_error();
    std::string kind(tag);
    Py_ssize_t length = PyTuple_GET_SIZE(expected.ptr());
    if (kind == "NONE") return length == 1 && value.ptr() == Py_None;
    if (kind == "ELLIPSIS") return length == 1 && value.ptr() == Py_Ellipsis;
    if (kind == "COMPLEX") {
        if (length != 3 || !PyComplex_CheckExact(value.ptr())) return false;
        double real = PyFloat_AsDouble(PyTuple_GET_ITEM(expected.ptr(), 1));
        if (PyErr_Occurred()) throw nb::python_error();
        double imag = PyFloat_AsDouble(PyTuple_GET_ITEM(expected.ptr(), 2));
        if (PyErr_Occurred()) throw nb::python_error();
        return PyComplex_RealAsDouble(value.ptr()) == real && PyComplex_ImagAsDouble(value.ptr()) == imag;
    }
    if (length != 2) throw nb::type_error("Grid profile atom arity changed");
    PyObject* sample = PyTuple_GET_ITEM(expected.ptr(), 1);
    if (kind == "CODE") return lf_grid_profile_code(value, nb::handle(sample));
    if (kind == "TUPLE") {
        if (!PyTuple_CheckExact(value.ptr()) || !PyTuple_CheckExact(sample) ||
            PyTuple_GET_SIZE(value.ptr()) != PyTuple_GET_SIZE(sample)) return false;
        for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(sample); ++i)
            if (!lf_grid_profile_atom(nb::handle(PyTuple_GET_ITEM(value.ptr(), i)),
                                      nb::handle(PyTuple_GET_ITEM(sample, i)))) return false;
        return true;
    }
    if (kind == "BYTES") {
        if (!PyBytes_CheckExact(value.ptr())) return false;
        nb::object hex = lh_check(PyObject_CallMethod(value.ptr(), "hex", nullptr));
        int equal = PyObject_RichCompareBool(hex.ptr(), sample, Py_EQ);
        if (equal < 0) throw nb::python_error();
        return equal != 0;
    }
    if ((kind == "BOOL" && !PyBool_Check(value.ptr())) ||
        (kind == "INT" && !PyLong_CheckExact(value.ptr())) ||
        (kind == "FLOAT" && !PyFloat_CheckExact(value.ptr())) ||
        (kind == "STR" && !PyUnicode_CheckExact(value.ptr()))) return false;
    if (kind != "BOOL" && kind != "INT" && kind != "FLOAT" && kind != "STR")
        throw nb::type_error("Grid profile tag is unsupported");
    int equal = PyObject_RichCompareBool(value.ptr(), sample, Py_EQ);
    if (equal < 0) throw nb::python_error();
    return equal != 0;
}
bool lf_grid_profile_code(nb::handle code, nb::handle expected) {
    if (!PyCode_Check(code.ptr())) return false;
    if (!PyTuple_CheckExact(expected.ptr()) || PyTuple_GET_SIZE(expected.ptr()) != 18)
        throw nb::type_error("Grid complete compiler profile changed");
    const char* fields[] = {"co_argcount", "co_posonlyargcount", "co_kwonlyargcount",
        "co_nlocals", "co_stacksize", "co_flags", "co_code", "co_consts", "co_names",
        "co_varnames", "co_freevars", "co_cellvars", "co_name", "co_qualname",
        "co_firstlineno", "co_exceptiontable", "co_linetable", "co_filename"};
    for (int i = 0; i < 18; ++i) {
        nb::object actual = lh_check(PyObject_GetAttrString(code.ptr(), fields[i]));
        if (!lf_grid_profile_atom(actual, nb::handle(PyTuple_GET_ITEM(expected.ptr(), i)))) return false;
    }
    return true;
}

// Actual source-named generator functions are created before GET_ITER. Their
// own frame supplies builtins and closure cells when iteration first starts.
#define LFE(value) [&]() -> nb::object { return (value); }
PyObject* lfe_frame(PyObject*, PyObject* args) {
    try {
        if (PyTuple_GET_SIZE(args)!=3 || !PyLong_CheckExact(PyTuple_GET_ITEM(args,0)) ||
            PyLong_AsLong(PyTuple_GET_ITEM(args,0))!=-2)
            throw nb::type_error("expression parameter transport changed");
        PyFrameObject* frame=PyEval_GetFrame();
        if (!frame) throw nb::type_error("actual expression frame is required");
        nb::object code=lh_check(reinterpret_cast<PyObject*>(PyFrame_GetCode(frame)));
        auto* active=reinterpret_cast<PyCodeObject*>(code.ptr());
        if (PyTuple_GET_SIZE(active->co_consts)<1) throw nb::type_error("expression metadata is missing");
        PyObject* payload=PyTuple_GET_ITEM(active->co_consts,PyTuple_GET_SIZE(active->co_consts)-1);
        if (!PyTuple_CheckExact(payload) || PyTuple_GET_SIZE(payload)!=4 ||
            !PyUnicode_CheckExact(PyTuple_GET_ITEM(payload,0)) ||
            PyUnicode_CompareWithASCIIString(PyTuple_GET_ITEM(payload,0),"frontend-expression-v0")!=0)
            throw nb::type_error("prepared expression metadata is required");
        long kind=PyLong_AsLong(PyTuple_GET_ITEM(payload,1));
        if (kind<0 || kind>6) throw nb::type_error("invalid native expression kind");
        nb::object cells;
        PyObject* supplier=PyTuple_GET_ITEM(args,2);
        const int cell_counts[]={0,3,1,1,1,0,0};
        if (cell_counts[kind]) {
            if (!PyFunction_Check(supplier)) throw nb::type_error("actual expression closure supplier is required");
            PyObject* captured=PyFunction_GetClosure(supplier);
            if (!captured || PyTuple_GET_SIZE(captured)!=cell_counts[kind])
                throw nb::type_error("actual expression closure cells changed");
            nb::object names=lh_check(PyObject_GetAttrString(PyFunction_GetCode(supplier),"co_freevars"));
            int same=PyObject_RichCompareBool(names.ptr(),PyTuple_GET_ITEM(payload,3),Py_EQ);
            if (same<0) throw nb::python_error();
            if (!same) throw nb::type_error("expression closure names changed");
            for (Py_ssize_t i=0;i<PyTuple_GET_SIZE(captured);++i)
                if (!PyCell_Check(PyTuple_GET_ITEM(captured,i))) throw nb::type_error("genuine expression cell is required");
            if (kind==1)
                cells=nb::make_tuple(nb::handle(PyTuple_GET_ITEM(captured,1)),nb::handle(PyTuple_GET_ITEM(captured,0)),nb::handle(PyTuple_GET_ITEM(captured,2)));
            else cells=nb::borrow<nb::object>(captured);
        } else {
            if (supplier!=Py_None) throw nb::type_error("closure-free expression transport changed");
            cells=lh_check(PyTuple_New(0));
        }
        LFExpressionState state;
        state.kind=static_cast<int>(kind);
        state.globals=lh_check(PyFrame_GetGlobals(frame));
        state.builtins=lh_check(PyFrame_GetBuiltins(frame));
        state.metadata=nb::borrow<nb::object>(PyTuple_GET_ITEM(payload,2));
        state.iterator=nb::borrow<nb::object>(PyTuple_GET_ITEM(args,1));
        state.cells=std::move(cells);
        return nb::cast(std::move(state)).release().ptr();
    } catch (...) { lf_adapter_error(); return nullptr; }
}
PyMethodDef lfe_frame_definition={"frontend_expression_frame",lfe_frame,METH_VARARGS,nullptr};

nb::object lfe_prepare(const LFSource& source, int kind, nb::object cells, nb::object transport) {
    if (kind<0 || kind>6 || !PyTuple_CheckExact(cells.ptr())) throw nb::type_error("expression declaration changed");
    const int counts[]={0,3,1,1,1,0,0};
    if (PyTuple_GET_SIZE(cells.ptr())!=counts[kind]) throw nb::type_error("expression cell count changed");
    if (!PyTuple_CheckExact(transport.ptr())) {
        // Explicit legacy probe callback only. New full-frame preparation uses
        // the seven-code tuple below; this path is not canonical admission.
        if (!PyCallable_Check(transport.ptr())) throw nb::type_error("expression code table or explicit probe transport required");
        return nb::make_tuple(nb::str("legacy-expression-probe"),nb::int_(kind),
            source.globals,source.builtins,source.metadata,cells,transport);
    }
    if (PyTuple_GET_SIZE(transport.ptr())!=7) throw nb::type_error("seven prepared expression codes are required");
    PyObject* code=PyTuple_GET_ITEM(transport.ptr(),kind);
    if (!PyCode_Check(code)) throw nb::type_error("prepared expression code is required");
    nb::object closure=kind==1 ?
        nb::make_tuple(nb::handle(PyTuple_GET_ITEM(cells.ptr(),1)),nb::handle(PyTuple_GET_ITEM(cells.ptr(),0)),nb::handle(PyTuple_GET_ITEM(cells.ptr(),2))) : cells;
    if (PyTuple_GET_SIZE(closure.ptr())!=PyCode_GetNumFree(reinterpret_cast<PyCodeObject*>(code)))
        throw nb::type_error("expression closure layout changed");
    nb::object function=lh_check(PyFunction_New(code,source.globals.ptr()));
    if (PyTuple_GET_SIZE(closure.ptr()) && PyFunction_SetClosure(function.ptr(),closure.ptr())<0)
        throw nb::python_error();
    return function;
}
nb::object lfe_start(nb::object function, nb::object iterator) {
    if (PyFunction_Check(function.ptr())) return lh_call(std::move(function),{LFE(iterator)});
    if (!PyTuple_CheckExact(function.ptr()) || PyTuple_GET_SIZE(function.ptr())!=7)
        throw nb::type_error("prepared expression function is required");
    auto F=[&](Py_ssize_t i){return nb::borrow<nb::object>(PyTuple_GET_ITEM(function.ptr(),i));};
    LFExpressionState state;
    state.kind=static_cast<int>(PyLong_AsLong(F(1).ptr()));
    state.globals=F(2); state.builtins=F(3); state.metadata=F(4); state.cells=F(5);
    state.iterator=std::move(iterator);
    nb::object object=nb::cast(std::move(state)), protocol=F(6);
    function.reset();
    return lh_call(std::move(protocol),{LFE(object)});
}

nb::object lfe_prepare_codes_impl(nb::tuple parents, nb::tuple source_profiles,
                             nb::tuple shells, nb::tuple shell_profiles, bool code_parents) {
    if (parents.size()!=4 || source_profiles.size()!=50 || shells.size()!=7 || shell_profiles.size()!=7)
        throw nb::type_error("complete frontend expression compiler tables are required");
    const int parent_slot[]={0,0,1,2,3,3,3}, parent_entry[]={10,10,11,29,3,3,3}, ordinal[]={0,1,0,0,0,1,2};
    nb::object codes=lh_check(PyTuple_New(7));
    nb::object module=nb::str("naparixx._native");
    nb::object callback=lh_check(PyCFunction_NewEx(&lfe_frame_definition,nullptr,module.ptr()));
    for (int kind=0;kind<7;++kind) {
        PyObject* parent=parents[parent_slot[kind]].ptr();
        if (code_parents ? !PyCode_Check(parent) : !PyFunction_Check(parent))
            throw nb::type_error("genuine expression parent declaration is required");
        auto* parent_code=reinterpret_cast<PyCodeObject*>(code_parents?parent:PyFunction_GetCode(parent));
        if (!lf_grid_profile_code(nb::handle(reinterpret_cast<PyObject*>(parent_code)),source_profiles[parent_entry[kind]]))
            throw nb::type_error("expression parent compiler profile does not match");
        PyCodeObject* original=nullptr;
        int found=0;
        for (Py_ssize_t i=0;i<PyTuple_GET_SIZE(parent_code->co_consts);++i) {
            PyObject* value=PyTuple_GET_ITEM(parent_code->co_consts,i);
            if (PyCode_Check(value) && PyUnicode_CompareWithASCIIString(reinterpret_cast<PyCodeObject*>(value)->co_name,"<genexpr>")==0) {
                if (found++==ordinal[kind]) {original=reinterpret_cast<PyCodeObject*>(value);break;}
            }
        }
        if (!original) throw nb::type_error("actual source expression declaration is missing");
        nb::object shell=nb::borrow<nb::object>(shells[kind]);
        if (!lf_grid_profile_code(shell,shell_profiles[kind])) throw nb::type_error("expression shell compiler profile does not match");
        auto* raw=reinterpret_cast<PyCodeObject*>(shell.ptr());
        nb::object freevars=lh_check(PyObject_GetAttrString(reinterpret_cast<PyObject*>(original),"co_freevars"));
        nb::object shell_freevars=lh_check(PyObject_GetAttrString(shell.ptr(),"co_freevars"));
        int same=PyObject_RichCompareBool(freevars.ptr(),shell_freevars.ptr(),Py_EQ);
        if (same<0) throw nb::python_error();
        if (!same || raw->co_argcount!=1 || original->co_argcount!=1 || !(raw->co_flags&CO_GENERATOR))
            throw nb::type_error("expression argument/cell ABI changed");
        nb::object constants=lh_check(PyTuple_New(PyTuple_GET_SIZE(raw->co_consts)+1));
        bool marker=false;
        for (Py_ssize_t i=0;i<PyTuple_GET_SIZE(raw->co_consts);++i) {
            PyObject* value=PyTuple_GET_ITEM(raw->co_consts,i);
            if (value==Py_Ellipsis) {value=callback.ptr();marker=true;}
            else if (PyLong_CheckExact(value)) {
                long token=PyLong_AsLong(value);
                if (token==0) value=PyExc_BaseException;
                else if (token==1) value=PyExc_StopIteration;
            }
            PyTuple_SET_ITEM(constants.ptr(),i,Py_NewRef(value));
        }
        if (!marker) throw nb::type_error("expression native entry marker is missing");
        nb::object payload=nb::make_tuple(nb::str("frontend-expression-v0"),nb::int_(kind),le_metadata(original),freevars);
        PyTuple_SET_ITEM(constants.ptr(),PyTuple_GET_SIZE(raw->co_consts),payload.release().ptr());
        nb::object source_vars=lh_check(PyObject_GetAttrString(reinterpret_cast<PyObject*>(original),"co_varnames"));
        nb::object shell_vars=lh_check(PyObject_GetAttrString(shell.ptr(),"co_varnames"));
        nb::object varnames=lh_check(PyTuple_New(PyTuple_GET_SIZE(shell_vars.ptr())));
        for (Py_ssize_t i=0;i<PyTuple_GET_SIZE(shell_vars.ptr());++i)
            PyTuple_SET_ITEM(varnames.ptr(),i,Py_NewRef(PyTuple_GET_ITEM(i==0?source_vars.ptr():shell_vars.ptr(),i==0?0:i)));
        nb::dict kwargs;
        kwargs["co_consts"]=constants; kwargs["co_varnames"]=varnames;
        kwargs["co_filename"]=nb::borrow<nb::object>(original->co_filename);
        kwargs["co_name"]=nb::borrow<nb::object>(original->co_name);
        kwargs["co_qualname"]=nb::borrow<nb::object>(original->co_qualname);
        kwargs["co_firstlineno"]=nb::int_(original->co_firstlineno);
        kwargs["co_flags"]=nb::int_(original->co_flags);
        nb::object replace=lh_check(PyObject_GetAttrString(shell.ptr(),"replace"));
        nb::object args=lh_check(PyTuple_New(0));
        nb::object prepared=lh_check(PyObject_Call(replace.ptr(),args.ptr(),kwargs.ptr()));
        PyTuple_SET_ITEM(codes.ptr(),kind,prepared.release().ptr());
    }
    return codes;
}
#undef LFE

nb::object lfe_prepare_codes(nb::tuple parents,nb::tuple source_profiles,nb::tuple shells,nb::tuple shell_profiles) {
    return lfe_prepare_codes_impl(parents,source_profiles,shells,shell_profiles,false);
}
// Unified experimental dispatcher for all50 source-selected bodies. Profiles
// reject incompatible templates; canonical declaration provenance is separate.
#define LFF(value) [&]() -> nb::object { return (value); }
nb::object lfu_supplier_cell(nb::object supplier) {
    if (!PyFunction_Check(supplier.ptr())) throw nb::type_error("genuine closure supplier is required");
    PyObject* cells = PyFunction_GetClosure(supplier.ptr());
    if (!cells || PyTuple_GET_SIZE(cells) != 1 || !PyCell_Check(PyTuple_GET_ITEM(cells, 0)))
        throw nb::type_error("one actual source closure cell is required");
    return nb::borrow<nb::object>(PyTuple_GET_ITEM(cells, 0));
}
nb::object lfu_factory_cell(const LFSource& source, nb::object cell, nb::object code) {
    if (!PyCode_Check(code.ptr())) throw nb::type_error("prepared adapter call code is required");
    nb::object wraps = source.global("wraps");
    PyObject* raw = PyCell_Get(cell.ptr());
    if (!raw) {
        if (PyErr_Occurred()) throw nb::python_error();
        PyErr_SetString(PyExc_UnboundLocalError,
            "cannot access local variable 'original' where it is not associated with a value");
        throw nb::python_error();
    }
    nb::object original = nb::steal<nb::object>(raw);
    nb::object decorator = lh_call(std::move(wraps), {LFF(original)});
    original.reset();
    nb::object function = lh_check(PyFunction_New(code.ptr(), source.globals.ptr()));
    nb::object closure = nb::make_tuple(cell);
    if (PyFunction_SetClosure(function.ptr(), closure.ptr()) < 0) throw nb::python_error();
    return lh_call(std::move(decorator), {LFF(function)});
}

PyObject* lfu_frame(PyObject*, PyObject* args) {
    try {
        Py_ssize_t count = PyTuple_GET_SIZE(args);
        if (count < 1 || !PyLong_CheckExact(PyTuple_GET_ITEM(args, 0)))
            throw nb::type_error("frontend entry is required");
        long entry = PyLong_AsLong(PyTuple_GET_ITEM(args, 0));
        if (entry == -1 && PyErr_Occurred()) throw nb::python_error();
        auto A = [&](Py_ssize_t index) { return nb::borrow<nb::object>(PyTuple_GET_ITEM(args, index + 1)); };
        PyFrameObject* frame = PyEval_GetFrame();
        if (!frame) throw nb::type_error("actual frontend source frame is required");
        nb::object code = lh_check(reinterpret_cast<PyObject*>(PyFrame_GetCode(frame)));
        auto* active = reinterpret_cast<PyCodeObject*>(code.ptr());
        PyObject* constants = active->co_consts;
        if (PyTuple_GET_SIZE(constants)<1) throw nb::type_error("frontend payload is missing");
        PyObject* raw_payload = PyTuple_GET_ITEM(constants, PyTuple_GET_SIZE(constants) - 1);
        if (!PyTuple_CheckExact(raw_payload) || PyTuple_GET_SIZE(raw_payload) != 5 ||
            !PyUnicode_CheckExact(PyTuple_GET_ITEM(raw_payload, 0)) ||
            PyUnicode_CompareWithASCIIString(PyTuple_GET_ITEM(raw_payload, 0), "frontend-probe-v0") != 0)
            throw nb::type_error("reviewed frontend payload is required");
        if (entry == -1) {
            if (count != 2 || !nb::isinstance<LFDrawState>(A(0)))
                throw nb::type_error("actual draw state is required");
            nb::object exception = nb::steal<nb::object>(PyErr_GetHandledException());
            if (!exception.is_valid()) throw nb::type_error("actual transport exception is required");
            nb::object injected = nb::make_tuple(exception);
            return lf_draw_throw(*nb::inst_ptr<LFDrawState>(A(0).ptr()), nb::borrow<nb::args>(injected)).release().ptr();
        }
        const int sizes[] = {2,3,4,4,4,4,4,3,4,2,4,1,4,1,1,7,1,1,1,2,1,1,2,2,1,1,2,2,2,3,1,3,1,3,2,1,4,1,4,1,1,1,2,1,2,2,2,2,1,1};
        if (entry < 0 || entry >= 50 || count != sizes[entry] + 1)
            throw nb::type_error("frontend parameter transport changed");
        long declared = PyLong_AsLong(PyTuple_GET_ITEM(raw_payload, 1));
        if (declared == -1 && PyErr_Occurred()) throw nb::python_error();
        if (declared != entry) throw nb::type_error("frontend entry does not match its source payload");
        LFSource source{lh_check(PyFrame_GetGlobals(frame)), lh_check(PyFrame_GetBuiltins(frame)),
                        nb::borrow<nb::object>(PyTuple_GET_ITEM(raw_payload, 2))};
        lh_metadata_values(source.metadata, 0); lh_metadata_values(source.metadata, 1);
        nb::object transport_code = nb::borrow<nb::object>(PyTuple_GET_ITEM(raw_payload, 3));
        nb::object child = nb::borrow<nb::object>(PyTuple_GET_ITEM(raw_payload, 4));
        if (!PyCode_Check(transport_code.ptr()) && !PyTuple_CheckExact(transport_code.ptr()))
            throw nb::type_error("frontend protocol code or expression table is required");
        nb::object transport = nb::none();
        if (entry==3 || entry==10 || entry==11 || entry==29) {
            if (PyTuple_CheckExact(transport_code.ptr())) transport=transport_code;
            else transport=lh_check(PyFunction_New(transport_code.ptr(), source.globals.ptr()));
        }
        nb::object import_locals=nb::none();
        if (entry==3 || entry==9) {
            PyObject* locals=frame->f_frame->f_locals;
            if (locals) import_locals=nb::borrow<nb::object>(locals);
        }
        nb::object result;
        switch (entry) {
        case 0: result=lf_validate_label(source,A(0),A(1)); break;
        case 1: result=lf_validate_non_painted(source,A(0),A(1),A(2)); break;
        case 2: result=lf_fill(source,A(0),A(1),A(2),A(3)); break;
        case 3: result=lf_flood_bbox(source,A(0),A(1),A(2),A(3),transport,import_locals); break;
        case 4: result=lf_draw(source,A(0),A(1),A(2),A(3)); break;
        case 5: result=lf_paint(source,A(0),A(1),A(2),A(3)); break;
        case 6: result=lf_brush_bbox(source,A(0),A(1),A(2),A(3)); break;
        case 7: result=lf_paint_polygon(source,A(0),A(1),A(2)); break;
        case 8: result=lf_polygon_bbox(source,A(0),A(1),A(2),A(3)); break;
        case 9: result=lf_create_polygon_mask(source,A(0),A(1),import_locals); break;
        case 10: result=lf_create_brush_mask(source,A(0),A(1),A(2),A(3),transport); break;
        case 11: result=lf_compute_mask_bbox(source,A(0),transport); break;
        case 12: result=lf_build_slice_key(source,A(0),A(1),A(2),A(3)); break;
        case 13: result=lf_shape_and_dims(source,A(0)); break;
        case 14: result=lf_dims_to_paint(source,A(0)); break;
        case 15: result=lf_wrong_label_init(source,A(0),A(1),A(2),A(3),A(4),A(5),lfu_supplier_cell(A(6))); break;
        case 16: result=lf_property_get(source,A(0),0); break;
        case 17: result=lf_property_get(source,A(0),1); break;
        case 18: result=lf_property_get(source,A(0),2); break;
        case 19: result=lf_brush_size_set(source,A(0),A(1)); break;
        case 20: result=lf_calculate_cursor_size(source,A(0)); break;
        case 21: result=lf_property_get(source,A(0),3); break;
        case 22: result=lf_selected_label_set(source,A(0),A(1)); break;
        case 23: result=lf_get_color(source,A(0),A(1)); break;
        case 24: result=lf_property_get(source,A(0),4); break;
        case 25: result=lf_property_get(source,A(0),5); break;
        case 26: result=lf_draw_state(source,A(0),A(1)); break;
        case 27: result=lf_pick(source,A(0),A(1)); break;
        case 28: result=lf_brush_mouse_init(source,A(0),A(1)); break;
        case 29: result=lf_brush_mouse_call(source,A(0),A(1),A(2),transport); break;
        case 30: result=lf_brush_modifiers_change(source,A(0)); break;
        case 31: result=lf_interpolate(source,A(0),A(1),A(2)); break;
        case 32: result=lf_get_dtype(source,A(0)); break;
        case 33: result=lf_first_nonzero(source,A(0),A(1),A(2)); break;
        case 34: result=lf_mouse_coordinate(source,A(0),A(1)); break;
        case 35: result=lfu_factory_cell(source,lfu_supplier_cell(A(0)),child); break;
        case 36: result=lf_interpolation_adapter_call(source,A(0),A(1),A(2),lfu_supplier_cell(A(3))); break;
        case 37: result=lfu_factory_cell(source,lfu_supplier_cell(A(0)),child); break;
        case 38: result=lf_ray_adapter_call(source,A(0),A(1),A(2),lfu_supplier_cell(A(3))); break;
        case 39: result=lf_plain_real(source,A(0)); break;
        case 40: result=lf_property_get(source,A(0),7); break;
        case 41: result=lf_property_get(source,A(0),8); break;
        case 42: result=lf_cursor_set(source,A(0),A(1),false); break;
        case 43: result=lf_property_get(source,A(0),9); break;
        case 44: result=lf_cursor_set(source,A(0),A(1),true); break;
        case 45: result=lf_mouse_press(source,A(0),A(1)); break;
        case 46: result=lf_mouse_move(source,A(0),A(1)); break;
        case 47: result=lf_mouse_release(source,A(0),A(1)); break;
        case 48: result=lf_property_get(source,A(0),6); break;
        case 49: result=lf_property_get(source,A(0),10); break;
        }
        return result.release().ptr();
    } catch (...) { lf_adapter_error(); return nullptr; }
}
PyMethodDef lfu_frame_definition = {"frontend_frame", lfu_frame, METH_VARARGS, nullptr};

nb::object lfu_protocol_code(nb::object code) {
    auto* raw = reinterpret_cast<PyCodeObject*>(code.ptr());
    nb::object constants = lh_check(PyTuple_New(PyTuple_GET_SIZE(raw->co_consts)));
    for (Py_ssize_t i=0;i<PyTuple_GET_SIZE(raw->co_consts);++i) {
        PyObject* value=PyTuple_GET_ITEM(raw->co_consts,i);
        if (PyLong_CheckExact(value)) {
            long marker=PyLong_AsLong(value);
            if (marker==0) value=PyExc_BaseException;
            else if (marker==1) value=PyExc_StopIteration;
        }
        PyTuple_SET_ITEM(constants.ptr(),i,Py_NewRef(value));
    }
    nb::dict kwargs; kwargs["co_consts"]=constants;
    nb::object args=lh_check(PyTuple_New(0));
    nb::object replace=lh_check(PyObject_GetAttrString(code.ptr(),"replace"));
    return lh_check(PyObject_Call(replace.ptr(),args.ptr(),kwargs.ptr()));
}

nb::object lfu_prepare_code(PyCodeObject* original, int entry, nb::tuple shells,
                            nb::tuple source_profiles, nb::tuple shell_profiles, nb::object protocol) {
    if (entry<0 || entry>=50 || shells.size()!=50 || source_profiles.size()!=50 || shell_profiles.size()!=50)
        throw nb::type_error("complete fifty-function frontend profile tables are required");
    if (!lf_grid_profile_code(nb::handle(reinterpret_cast<PyObject*>(original)),source_profiles[entry]))
        throw nb::type_error("original frontend compiler profile does not match");
    nb::object code=nb::borrow<nb::object>(shells[entry]);
    if (!lf_grid_profile_code(code,shell_profiles[entry]))
        throw nb::type_error("frontend shell compiler profile does not match");
    auto* raw=reinterpret_cast<PyCodeObject*>(code.ptr());
    if (raw->co_argcount!=original->co_argcount || raw->co_posonlyargcount!=original->co_posonlyargcount ||
        raw->co_kwonlyargcount!=original->co_kwonlyargcount || PyCode_GetNumFree(raw)!=PyCode_GetNumFree(original))
        throw nb::type_error("frontend Python binding/closure ABI changed");
    nb::object child=nb::none();
    if (entry==35 || entry==37) {
        PyCodeObject* selected=nullptr;
        for (Py_ssize_t i=0;i<PyTuple_GET_SIZE(original->co_consts);++i) {
            PyObject* value=PyTuple_GET_ITEM(original->co_consts,i);
            if (PyCode_Check(value) && PyUnicode_CompareWithASCIIString(reinterpret_cast<PyCodeObject*>(value)->co_name,"call")==0)
                selected=reinterpret_cast<PyCodeObject*>(value);
        }
        if (!selected) throw nb::type_error("actual adapter child declaration is required");
        child=lfu_prepare_code(selected,entry==35?36:38,shells,source_profiles,shell_profiles,protocol);
    }
    nb::object constants=lh_check(PyTuple_New(PyTuple_GET_SIZE(raw->co_consts)+1));
    nb::object module=nb::str("naparixx._native");
    nb::object callback=lh_check(PyCFunction_NewEx(&lfu_frame_definition,nullptr,module.ptr()));
    bool replaced=false;
    for (Py_ssize_t i=0;i<PyTuple_GET_SIZE(raw->co_consts);++i) {
        PyObject* value=PyTuple_GET_ITEM(raw->co_consts,i);
        if (value==Py_Ellipsis) { value=callback.ptr(); replaced=true; }
        else if (entry==26 && PyLong_CheckExact(value)) {
            long marker=PyLong_AsLong(value);
            if (marker==0) value=PyExc_BaseException;
            else if (marker==1) value=PyExc_StopIteration;
        }
        PyTuple_SET_ITEM(constants.ptr(),i,Py_NewRef(value));
    }
    if (!replaced) throw nb::type_error("frontend native entry marker is missing");
    nb::object metadata=le_metadata(original);
    nb::object payload=nb::make_tuple(nb::str("frontend-probe-v0"),nb::int_(entry),metadata,protocol,child);
    PyTuple_SET_ITEM(constants.ptr(),PyTuple_GET_SIZE(raw->co_consts),payload.release().ptr());
    nb::dict kwargs; kwargs["co_consts"]=constants;
    kwargs["co_filename"]=nb::borrow<nb::object>(original->co_filename);
    kwargs["co_name"]=nb::borrow<nb::object>(original->co_name);
    kwargs["co_qualname"]=nb::borrow<nb::object>(original->co_qualname);
    kwargs["co_firstlineno"]=nb::int_(original->co_firstlineno);
    kwargs["co_flags"]=nb::int_(original->co_flags);
    nb::object args=lh_check(PyTuple_New(0));
    nb::object replace=lh_check(PyObject_GetAttrString(code.ptr(),"replace"));
    return lh_check(PyObject_Call(replace.ptr(),args.ptr(),kwargs.ptr()));
}
#undef LFF

} // namespace nx

namespace nx {
// Read-only canonical binding snapshot. This does not mutate or admit code.
// Actual defining-execution provenance and atomic publication are separate gates.
struct LFCanonicalBindings {
    std::vector<nb::object> anchors;
    std::vector<nb::object> functions;
    std::vector<size_t> code_anchors;
    void keep(nb::handle value) { anchors.push_back(nb::borrow<nb::object>(value)); }
};
nb::object lfc_dict_value(PyObject* dictionary, const char* name) {
    if (!dictionary || !PyDict_CheckExact(dictionary)) throw nb::type_error("exact defining dictionary is required");
    // Admission must not call equality/hash overrides on foreign dictionary
    // keys during the final callback-free transaction validation.
    PyObject *key,*value,*found=nullptr;
    Py_ssize_t position=0;
    while (PyDict_Next(dictionary,&position,&key,&value)) {
        if (!PyUnicode_Check(key) || PyUnicode_CompareWithASCIIString(key,name)!=0) continue;
        if (!PyUnicode_CheckExact(key) || found)
            throw nb::type_error("canonical dictionary key is not a unique exact string");
        found=value;
    }
    if (!found) throw nb::type_error("required canonical binding is missing");
    return nb::borrow<nb::object>(found);
}
nb::object lfc_module(LFCanonicalBindings& state,const char* name) {
    nb::object module=lfc_dict_value(PyImport_GetModuleDict(),name);
    if (!PyModule_CheckExact(module.ptr())) throw nb::type_error("exact registered source module is required");
    nb::object dictionary=nb::borrow<nb::object>(PyModule_GetDict(module.ptr()));
    nb::object actual=lfc_dict_value(dictionary.ptr(),"__name__");
    if (!PyUnicode_CheckExact(actual.ptr()) || PyUnicode_CompareWithASCIIString(actual.ptr(),name)!=0)
        throw nb::type_error("registered module name changed");
    state.keep(module); state.keep(dictionary);
    return dictionary;
}
nb::object lfc_class(LFCanonicalBindings& state,nb::handle dictionary,const char* name,const char* module) {
    nb::object cls=lfc_dict_value(dictionary.ptr(),name);
    if (!PyType_Check(cls.ptr())) throw nb::type_error("actual defining class is required");
    auto* type=reinterpret_cast<PyTypeObject*>(cls.ptr());
    nb::object actual_module=lfc_dict_value(type->tp_dict,"__module__");
    if (!(type->tp_flags&Py_TPFLAGS_HEAPTYPE)) throw nb::type_error("actual source heap class is required");
    nb::object actual_name=nb::borrow<nb::object>(reinterpret_cast<PyHeapTypeObject*>(type)->ht_qualname);
    if (!PyUnicode_CheckExact(actual_module.ptr()) || !PyUnicode_CheckExact(actual_name.ptr()) ||
        PyUnicode_CompareWithASCIIString(actual_module.ptr(),module)!=0 ||
        PyUnicode_CompareWithASCIIString(actual_name.ptr(),name)!=0)
        throw nb::type_error("defining class identity metadata changed");
    state.keep(cls); state.keep(nb::handle(type->tp_dict));
    return cls;
}
nb::object lfc_member(LFCanonicalBindings& state,nb::handle cls,const char* name,int descriptor=0) {
    nb::object value=lfc_dict_value(reinterpret_cast<PyTypeObject*>(cls.ptr())->tp_dict,name);
    state.keep(value);
    if (descriptor==1) {
        if (Py_TYPE(value.ptr())!=&PyStaticMethod_Type) throw nb::type_error("exact source staticmethod is required");
        value=lh_check(PyObject_GetAttrString(value.ptr(),"__func__"));
    } else if (descriptor==2 || descriptor==3) {
        if (Py_TYPE(value.ptr())!=&PyProperty_Type) throw nb::type_error("exact source property is required");
        value=lh_check(PyObject_GetAttrString(value.ptr(),descriptor==2?"fget":"fset"));
    }
    return value;
}
nb::object lfc_cell(LFCanonicalBindings& state,nb::handle function,const char* name) {
    if (!PyFunction_Check(function.ptr())) throw nb::type_error("genuine source function is required");
    PyObject* code=PyFunction_GetCode(function.ptr());
    nb::object names=lh_check(PyObject_GetAttrString(code,"co_freevars"));
    PyObject* closure=PyFunction_GetClosure(function.ptr());
    if (!closure || !PyTuple_CheckExact(closure) || PyTuple_GET_SIZE(closure)!=PyTuple_GET_SIZE(names.ptr()))
        throw nb::type_error("actual closure tuple is required");
    state.keep(nb::handle(closure));
    for (Py_ssize_t i=0;i<PyTuple_GET_SIZE(names.ptr());++i) {
        if (PyUnicode_CompareWithASCIIString(PyTuple_GET_ITEM(names.ptr(),i),name)!=0) continue;
        PyObject* cell=PyTuple_GET_ITEM(closure,i);
        if (!PyCell_Check(cell)) throw nb::type_error("genuine source cell is required");
        nb::object value=lh_check(PyCell_Get(cell));
        state.keep(nb::handle(cell)); state.keep(value);
        return value;
    }
    throw nb::type_error("required source closure name is missing");
}
void lfc_function(LFCanonicalBindings& state,nb::object function,nb::handle globals) {
    if (!PyFunction_Check(function.ptr()) || PyFunction_GetGlobals(function.ptr())!=globals.ptr())
        throw nb::type_error("function must use its actual defining module dictionary");
    state.functions.push_back(function); state.keep(function);
    state.code_anchors.push_back(state.anchors.size());
    state.keep(nb::handle(PyFunction_GetCode(function.ptr())));
    // Keep identities only. Mutable public metadata remains actual Python data.
    auto* actual=reinterpret_cast<PyFunctionObject*>(function.ptr());
    for (PyObject* value:{actual->func_defaults,actual->func_kwdefaults,actual->func_closure,actual->func_builtins})
        state.keep(nb::handle(value?value:Py_None));
}
void lfc_wrapped(LFCanonicalBindings& state,nb::handle function,nb::handle expected) {
    nb::object dictionary=nb::borrow<nb::object>(reinterpret_cast<PyFunctionObject*>(function.ptr())->func_dict);
    nb::object wrapped=lfc_dict_value(dictionary.ptr(),"__wrapped__");
    if (wrapped.ptr()!=expected.ptr()) throw nb::type_error("decorator wrapped alias and original cell disagree");
    state.keep(wrapped);
}
LFCanonicalBindings lfc_collect(nb::handle wrapper_profile) {
    LFCanonicalBindings state;
    nb::object labels=lfc_module(state,"napari.layers.labels.labels");
    nb::object mouse=lfc_module(state,"napari.layers.labels._labels_mouse_bindings");
    nb::object utils=lfc_module(state,"napari.layers.labels._labels_utils");
    nb::object adapters=lfc_module(state,"naparixx._adapters");
    nb::object base=lfc_module(state,"napari.layers.base.base");
    nb::object interactions=lfc_module(state,"napari.utils.interactions");
    nb::object backend=lfc_module(state,"naparixx._backend");
    nb::object label_class=lfc_class(state,labels,"Labels","napari.layers.labels.labels");
    nb::object error_class=lfc_class(state,labels,"WrongSelectedLabelError","napari.layers.labels.labels");
    nb::object brush_class=lfc_class(state,mouse,"BrushSizeOnMouseMove","napari.layers.labels._labels_mouse_bindings");
    nb::object layer_class=lfc_class(state,base,"Layer","napari.layers.base.base");
    auto member=[&](nb::handle cls,nb::handle globals,const char* name,int descriptor=0) {
        lfc_function(state,lfc_member(state,cls,name,descriptor),globals);
    };
    const char* label_methods[]={"_validate_label_in_range","_validate_non_painted_coord","fill","_get_flood_mask_and_bbox",
        "_draw","paint","_get_brush_mask_and_bbox","paint_polygon","_get_polygon_mask_and_bbox",
        "_create_polygon_mask","_create_brush_mask","_compute_mask_bbox","_build_slice_key","_get_shape_and_dims_to_paint","_get_dims_to_paint"};
    for (int i=0;i<15;++i) member(label_class,labels,label_methods[i],i>=9&&i<=12?1:0);
    member(error_class,labels,"__init__");
    if (lfc_cell(state,state.functions[15],"__class__").ptr()!=error_class.ptr())
        throw nb::type_error("error initializer must capture its actual defining class");
    member(label_class,labels,"contiguous",2); member(label_class,labels,"n_edit_dimensions",2);
    member(label_class,labels,"brush_size",2); member(label_class,labels,"brush_size",3);
    member(label_class,labels,"_calculate_cursor_size"); member(label_class,labels,"selected_label",2);
    member(label_class,labels,"selected_label",3); member(label_class,labels,"get_color");
    member(label_class,labels,"show_selected_label",2); member(label_class,labels,"preserve_labels",2);
    lfc_function(state,lfc_dict_value(mouse.ptr(),"draw"),mouse);
    lfc_function(state,lfc_dict_value(mouse.ptr(),"pick"),mouse);
    member(brush_class,mouse,"__init__"); member(brush_class,mouse,"__call__"); member(brush_class,mouse,"_on_modifiers_change");
    nb::object generic[2],adapter[2];
    for (int i=0;i<2;++i) {
        nb::object wrapper=lfc_dict_value(utils.ptr(),i==0?"interpolate_coordinates":"first_nonzero_coordinate");
        if (!PyFunction_Check(wrapper.ptr()) || PyFunction_GetGlobals(wrapper.ptr())!=backend.ptr() ||
            !lf_grid_profile_code(nb::handle(PyFunction_GetCode(wrapper.ptr())),wrapper_profile))
            throw nb::type_error("canonical dispatch wrapper compiler profile/namespace changed");
        state.keep(wrapper); state.keep(nb::handle(PyFunction_GetCode(wrapper.ptr())));
        generic[i]=lfc_cell(state,wrapper,"original"); adapter[i]=lfc_cell(state,wrapper,"adapter");
        lfc_wrapped(state,wrapper,generic[i]);
        if (lfc_cell(state,adapter[i],"original").ptr()!=generic[i].ptr())
            throw nb::type_error("actual adapter and dispatch original cells disagree");
        lfc_wrapped(state,adapter[i],generic[i]);
    }
    lfc_function(state,generic[0],utils); lfc_function(state,lfc_dict_value(utils.ptr(),"get_dtype"),utils);
    lfc_function(state,generic[1],utils); lfc_function(state,lfc_dict_value(utils.ptr(),"mouse_event_to_labels_coordinate"),utils);
    lfc_function(state,lfc_dict_value(adapters.ptr(),"interpolation_adapter"),adapters);
    lfc_function(state,adapter[0],adapters);
    lfc_function(state,lfc_dict_value(adapters.ptr(),"ray_adapter"),adapters);
    lfc_function(state,adapter[1],adapters);
    lfc_function(state,lfc_dict_value(adapters.ptr(),"_plain_real"),adapters);
    member(layer_class,base,"ndim",2); member(layer_class,base,"cursor",2); member(layer_class,base,"cursor",3);
    member(layer_class,base,"cursor_size",2); member(layer_class,base,"cursor_size",3);
    for (const char* name:{"mouse_press_callbacks","mouse_move_callbacks","mouse_release_callbacks"})
        lfc_function(state,lfc_dict_value(interactions.ptr(),name),interactions);
    member(label_class,labels,"colormap",2); member(layer_class,base,"scale",2);
    if (state.functions.size()!=50) throw nb::type_error("complete canonical frontend table is required");
    for (size_t i=0;i<50;++i) for (size_t j=0;j<i;++j)
        if (state.functions[i].ptr()==state.functions[j].ptr()) throw nb::type_error("distinct canonical source functions are required");
    return state;
}
void lfp_validate_snapshot(nb::handle owners,nb::handle wrapper_profile);
struct LFCanonicalSnapshot;
nb::object lfb_publish_frontend(LFCanonicalSnapshot& value);
struct LFCanonicalSnapshot {
    LFCanonicalBindings bindings;
    nb::object wrapper_profile;
    nb::object producer_scopes;
    std::vector<nb::object> prepared;
    bool published=false;
    void validate() const {
        if (producer_scopes.is_valid()) lfp_validate_snapshot(producer_scopes,wrapper_profile);
        LFCanonicalBindings current=lfc_collect(wrapper_profile);
        if (current.anchors.size()!=bindings.anchors.size()) throw nb::type_error("canonical binding snapshot changed");
        for (size_t i=0;i<current.anchors.size();++i)
            if (current.anchors[i].ptr()!=bindings.anchors[i].ptr()) throw nb::type_error("canonical binding snapshot changed");
    }
};
int lfc_traverse(PyObject* self,visitproc visit,void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) {
        const auto& state=*nb::inst_ptr<LFCanonicalSnapshot>(self);
        for (const auto& value:state.bindings.anchors) Py_VISIT(value.ptr());
        for (const auto& value:state.bindings.functions) Py_VISIT(value.ptr());
        for (const auto& value:state.prepared) Py_VISIT(value.ptr());
        Py_VISIT(state.wrapper_profile.ptr());
        Py_VISIT(state.producer_scopes.ptr());
    }
    return 0;
}
int lfc_clear(PyObject* self) {
    if (nb::inst_ready(self)) {
        auto& state=*nb::inst_ptr<LFCanonicalSnapshot>(self);
        state.bindings.anchors.clear();state.bindings.functions.clear();state.prepared.clear();state.wrapper_profile.reset();
        state.producer_scopes.reset();
    }
    return 0;
}
PyType_Slot lfc_slots[]={{Py_tp_traverse,reinterpret_cast<void*>(lfc_traverse)},
    {Py_tp_clear,reinterpret_cast<void*>(lfc_clear)},{0,nullptr}};
nb::object lfc_tuple(const std::vector<nb::object>& values) {
    nb::object result=lh_check(PyTuple_New(values.size()));
    for (size_t i=0;i<values.size();++i) PyTuple_SET_ITEM(result.ptr(),i,Py_NewRef(values[i].ptr()));
    return result;
}
nb::object lfc_prepare(nb::tuple shells,nb::tuple source_profiles,nb::tuple shell_profiles,
                       nb::tuple expression_shells,nb::tuple expression_profiles,nb::tuple wrapper_profile) {
    LFCanonicalSnapshot result;
    result.wrapper_profile=wrapper_profile;
    result.bindings=lfc_collect(wrapper_profile);
    nb::object parents=nb::make_tuple(result.bindings.functions[10],result.bindings.functions[11],
                                    result.bindings.functions[29],result.bindings.functions[3]);
    nb::object expressions=lfe_prepare_codes(nb::cast<nb::tuple>(parents),source_profiles,expression_shells,expression_profiles);
    for (int i=0;i<50;++i) result.prepared.push_back(lfu_prepare_code(
        reinterpret_cast<PyCodeObject*>(PyFunction_GetCode(result.bindings.functions[i].ptr())),
        i,shells,source_profiles,shell_profiles,expressions));
    result.validate();
    return nb::cast(std::move(result));
}
void lfc_exports(nb::module_& m) {
    nb::class_<LFCanonicalSnapshot>(m,"CanonicalSnapshot",nb::is_weak_referenceable(),nb::type_slots(lfc_slots))
        .def("validate",&LFCanonicalSnapshot::validate)
        .def("publish_experiment",&lfb_publish_frontend)
        .def_prop_ro("published",[](const LFCanonicalSnapshot& value){return value.published;})
        .def_prop_ro("functions",[](const LFCanonicalSnapshot& value){value.validate();return lfc_tuple(value.bindings.functions);})
        .def_prop_ro("prepared_codes",[](const LFCanonicalSnapshot& value){value.validate();return lfc_tuple(value.prepared);});
    m.def("prepare_canonical_snapshot_probe",&lfc_prepare);
}

}
#include "producer_capture.inc"
#include "batch_publication.inc"
NB_MODULE(_frontend_policy_probe, m) {
    namespace nb = nanobind;
    nx::lfc_exports(m);
    nx::lfp_exports(m);
    nx::lfb_exports(m);
    m.def("prepare_expression_codes_probe", &nx::lfe_prepare_codes);
    m.def("prepare_unified_expression_probe", [](nb::object function, int entry, nb::tuple shells,
        nb::tuple source_profiles, nb::tuple shell_profiles, nb::tuple expression_codes) {
        if (!PyFunction_Check(function.ptr()) || expression_codes.size()!=7)
            throw nb::type_error("genuine function and prepared expression table are required");
        return nx::lfu_prepare_code(reinterpret_cast<PyCodeObject*>(PyFunction_GetCode(function.ptr())), entry,
            shells,source_profiles,shell_profiles,expression_codes);
    });
    m.def("prepare_unified_probe", [](nb::object function, int entry, nb::tuple shells,
        nb::tuple source_profiles, nb::tuple shell_profiles, nb::object protocol, nb::object protocol_profile) {
        if (!PyFunction_Check(function.ptr()) || !nx::lf_grid_profile_code(protocol, protocol_profile))
            throw nb::type_error("genuine function and exact protocol compiler profile are required");
        return nx::lfu_prepare_code(reinterpret_cast<PyCodeObject*>(PyFunction_GetCode(function.ptr())), entry,
            shells, source_profiles, shell_profiles, nx::lfu_protocol_code(protocol));
    });
    m.def("mask_policy", [](int entry, nb::object function, nb::tuple args,
                             nb::object transport, nb::object import_locals) {
        nx::LFSource source{function.attr("__globals__"), function.attr("__builtins__"),
            nx::le_metadata(reinterpret_cast<PyCodeObject*>(PyFunction_GetCode(function.ptr())))};
        auto A = [&](size_t i) { return nb::borrow<nb::object>(args[i]); };
        if (entry == 0) return nx::lf_create_polygon_mask(source, A(0), A(1), import_locals);
        if (entry == 1) return nx::lf_flood_bbox(source, A(0), A(1), A(2), A(3), transport, import_locals);
        throw nb::type_error("invalid mask parent policy");
    }, nb::arg("entry"), nb::arg("function"), nb::arg("args"),
       nb::arg("transport"), nb::arg("import_locals").none());
    m.def("import_member", [](nb::object imported, nb::object name) {
        return nx::lfi_import_from(std::move(imported), std::move(name));
    });
    m.def("make_adapter_probe", [](nb::object factory, nb::object original, nb::object shell_factory) {
        auto child = [](nb::object function) -> nb::object {
            auto* code = reinterpret_cast<PyCodeObject*>(PyFunction_GetCode(function.ptr()));
            for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(code->co_consts); ++i) {
                PyObject* candidate = PyTuple_GET_ITEM(code->co_consts, i);
                if (PyCode_Check(candidate) && PyUnicode_CompareWithASCIIString(
                    reinterpret_cast<PyCodeObject*>(candidate)->co_name, "call") == 0)
                    return nb::borrow<nb::object>(candidate);
            }
            throw nb::type_error("actual adapter call template is required");
        };
        nb::object original_code = child(factory), shell_code = child(shell_factory);
        nx::LFSource source{factory.attr("__globals__"), factory.attr("__builtins__"),
            nx::le_metadata(reinterpret_cast<PyCodeObject*>(PyFunction_GetCode(factory.ptr())))};
        return nx::lf_adapter_factory(source, std::move(original), shell_code,
            reinterpret_cast<PyCodeObject*>(original_code.ptr()));
    });
    m.def("prepare_generic_probe", [](nb::object function, nb::object shell) {
        return nx::lf_adapter_shell_code(nb::handle(PyFunction_GetCode(shell.ptr())),
            reinterpret_cast<PyCodeObject*>(PyFunction_GetCode(function.ptr())), true);
    });
    m.def("error_init", [](nb::object function, nb::tuple args, nb::object class_cell) {
        nx::LFSource source{function.attr("__globals__"), function.attr("__builtins__"),
            nx::le_metadata(reinterpret_cast<PyCodeObject*>(function.attr("__code__").ptr()))};
        auto A = [&](size_t i) { return nb::borrow<nb::object>(args[i]); };
        return nx::lf_wrong_label_init(source, A(0), A(1), A(2), A(3), A(4), A(5), class_cell);
    });
    nx::lfx_exports(m);
    m.def("run_expression_policy", [](int entry, nb::object function, nb::tuple args, nb::object transport) {
        nx::LFSource source{function.attr("__globals__"), function.attr("__builtins__"),
            nx::le_metadata(reinterpret_cast<PyCodeObject*>(function.attr("__code__").ptr()))};
        auto A = [&](size_t i) { return nb::borrow<nb::object>(args[i]); };
        switch (entry) {
        case 0: return nx::lf_create_brush_mask(source, A(0), A(1), A(2), A(3), transport);
        case 1: return nx::lf_compute_mask_bbox(source, A(0), transport);
        case 2: return nx::lf_brush_mouse_call(source, A(0), A(1), A(2), transport);
        default: throw nb::type_error("invalid expression parent policy");
        }
    });
    m.def("expression_state", [](int kind, nb::object function, nb::object iterator, nb::object cells) {
        nx::LFExpressionState o;
        o.kind = kind; o.globals = function.attr("__globals__");
        o.builtins = function.attr("__builtins__");
        o.metadata = nx::le_metadata(reinterpret_cast<PyCodeObject*>(function.attr("__code__").ptr()));
        o.iterator = std::move(iterator); o.cells = std::move(cells);
        return nb::cast(std::move(o));
    });
    m.def("prepare_draw_probe", &nx::lf_prepare_draw_probe);
    nx::lf_draw_exports(m);
    m.def("draw_state", [](nb::object function, nb::object layer, nb::object event) {
        nx::LFSource source{function.attr("__globals__"), function.attr("__builtins__"),
            nx::le_metadata(reinterpret_cast<PyCodeObject*>(function.attr("__code__").ptr()))};
        return nx::lf_draw_state(std::move(source), std::move(layer), std::move(event));
    });
    m.def("run", [](int entry, nb::object function, nb::tuple args) -> nb::object {
        nx::LFSource source{function.attr("__globals__"), function.attr("__builtins__"),
            nx::le_metadata(reinterpret_cast<PyCodeObject*>(function.attr("__code__").ptr()))};
        auto A = [&](size_t i) { return nb::borrow<nb::object>(args[i]); };
        switch (entry) {
        case 0: return nx::lf_get_dtype(source, A(0));
        case 1: return nx::lf_interpolate(source, A(0), A(1), A(2));
        case 2: return nx::lf_first_nonzero(source, A(0), A(1), A(2));
        case 3: return nx::lf_mouse_coordinate(source, A(0), A(1));
        case 4: return nx::lf_pick(source, A(0), A(1));
        case 5: return nx::lf_build_slice_key(source, A(0), A(1), A(2), A(3));
        case 6: return nx::lf_validate_label(source, A(0), A(1));
        case 7: return nx::lf_get_color(source, A(0), A(1));
        case 8: return nx::lf_dims_to_paint(source, A(0));
        case 9: return nx::lf_shape_and_dims(source, A(0));
        case 10: return nx::lf_calculate_cursor_size(source, A(0));
        case 11: return nx::lf_selected_label_set(source, A(0), A(1));
        case 12: return nx::lf_brush_size_set(source, A(0), A(1));
        case 13: return nx::lf_cursor_set(source, A(0), A(1), false);
        case 14: return nx::lf_cursor_set(source, A(0), A(1), true);
        case 15: return nx::lf_validate_non_painted(source, A(0), A(1), A(2));
        case 16: return nx::lf_brush_mouse_init(source, A(0), A(1));
        case 17: return nx::lf_brush_modifiers_change(source, A(0));
        case 18: return nx::lf_plain_real(source, A(0));
        case 19: return nx::lf_mouse_press(source, A(0), A(1));
        case 20: return nx::lf_mouse_move(source, A(0), A(1));
        case 21: return nx::lf_mouse_release(source, A(0), A(1));
        default: throw nb::type_error("unknown isolated probe entry");
        }
    });
}
