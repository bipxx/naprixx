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
} // namespace nx
