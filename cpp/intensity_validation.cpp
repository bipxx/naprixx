#include "arrays.hpp"
#include "collection_protocol.hpp"

#include <array>
#include <initializer_list>
#include <utility>

namespace nx {
namespace {

nb::object ivd_checked(PyObject* value) {
    if (!value) throw nb::python_error();
    return nb::steal<nb::object>(value);
}
nb::object ivd_attr(nb::handle object, const char* name) {
    return ivd_checked(PyObject_GetAttrString(object.ptr(), name));
}
nb::object ivd_item(nb::handle object, nb::handle key) {
    return ivd_checked(PyObject_GetItem(object.ptr(), key.ptr()));
}
nb::object ivd_call0(nb::object callable) {
    return ivd_checked(PyObject_CallNoArgs(callable.ptr()));
}
nb::object ivd_call1(nb::object callable, nb::object value) {
    return ivd_checked(PyObject_CallOneArg(callable.ptr(), value.ptr()));
}
nb::object ivd_call2(nb::object callable, nb::object first, nb::object second) {
    return ivd_checked(PyObject_CallFunctionObjArgs(
        callable.ptr(), first.ptr(), second.ptr(), nullptr));
}
nb::object ivd_compare(nb::handle first, nb::handle second, int operation) {
    return ivd_checked(PyObject_RichCompare(first.ptr(), second.ptr(), operation));
}
bool ivd_truth(nb::object value) {
    const int result = PyObject_IsTrue(value.ptr());
    if (result < 0) throw nb::python_error();
    return result != 0;
}
void ivd_store(nb::handle self, const char* name, nb::object value) {
    if (PyObject_SetAttrString(self.ptr(), name, value.ptr()) < 0)
        throw nb::python_error();
}
void ivd_set_item(nb::handle self, nb::handle key, nb::object value) {
    if (PyObject_SetItem(self.ptr(), key.ptr(), value.ptr()) < 0)
        throw nb::python_error();
}
void ivd_replace(nb::object& field, nb::object value) {
    nb::object previous = std::move(field);
    field = std::move(value);
    previous.reset();
}
nb::object ivd_optional(const nb::object& value) {
    return value.is_valid() ? value : nb::none();
}
nb::object ivd_format(nb::handle value) {
    return ivd_checked(PyObject_Format(value.ptr(), nb::str("").ptr()));
}
nb::object ivd_message(std::initializer_list<nb::object> parts) {
    nb::list values;
    for (const auto& part : parts)
        if (PyList_Append(values.ptr(), part.ptr()) < 0) throw nb::python_error();
    return ivd_checked(PyUnicode_Join(nb::str("").ptr(), values.ptr()));
}
[[noreturn]] void ivd_raise(nb::object exception) {
    if (PyExceptionInstance_Check(exception.ptr()))
        PyErr_SetObject(reinterpret_cast<PyObject*>(Py_TYPE(exception.ptr())), exception.ptr());
    else if (PyExceptionClass_Check(exception.ptr())) PyErr_SetNone(exception.ptr());
    else PyErr_SetString(PyExc_TypeError, "exceptions must derive from BaseException");
    throw nb::python_error();
}

struct IntensityValidationNamespace {
    nb::dict globals;
    nb::object builtins;
    explicit IntensityValidationNamespace(nb::dict namespace_)
        : globals(std::move(namespace_)),
          builtins(nb::borrow<nb::object>(PyEval_GetBuiltins())) {}
    nb::object get(const char* name) const {
        nb::str key(name);
        PyObject* found = PyDict_GetItemWithError(globals.ptr(), key.ptr());
        if (found) return nb::borrow<nb::object>(found);
        if (PyErr_Occurred()) throw nb::python_error();
        found = PyObject_GetItem(builtins.ptr(), key.ptr());
        if (found) return nb::steal<nb::object>(found);
        if (!PyErr_ExceptionMatches(PyExc_KeyError)) throw nb::python_error();
        PyErr_Clear();
        nb::object text = ivd_checked(PyUnicode_FromFormat("name '%s' is not defined", name));
        nb::object exception = ivd_call1(nb::borrow<nb::object>(PyExc_NameError), std::move(text));
        if (PyObject_SetAttrString(exception.ptr(), "name", key.ptr()) < 0)
            throw nb::python_error();
        ivd_raise(std::move(exception));
    }
};

// The genuine nested Python function is only signature/doc/closure glue. Its
// n and dtype cells remain the sole captures and are supplied on every call.
// The native factory returns that exact function, without a second callable
// wrapper, copied captures, application continuation or original-body fallback.
nb::object validator_n_seq_bind(nb::object function) { return function; }

struct ValidatorSequenceLocals {
    nb::object item;
    ValidatorSequenceLocals() = default;
    ValidatorSequenceLocals(ValidatorSequenceLocals&&) = default;
    ~ValidatorSequenceLocals() { clear(); }
    void clear() { nb::object previous = std::move(item); previous.reset(); }
};
struct ValidatorPairwiseLocals {
    nb::object a, b;
    ValidatorPairwiseLocals() = default;
    ValidatorPairwiseLocals(ValidatorPairwiseLocals&&) = default;
    ~ValidatorPairwiseLocals() { clear(); }
    void clear() {
        nb::object previous_a = std::move(a), previous_b = std::move(b);
        previous_a.reset(); previous_b.reset();
    }
};
struct IntensityContrastLocals {
    enum class Layout { Empty, Limits, Range };
    Layout layout = Layout::Empty;
    nb::object newrange, value, current_range, i, clipped_limits;
    IntensityContrastLocals() = default;
    IntensityContrastLocals(IntensityContrastLocals&&) = default;
    ~IntensityContrastLocals() { clear(); }
    void clear() {
        std::array<nb::object, 5> previous = {
            std::move(newrange), std::move(value), std::move(current_range),
            std::move(i), std::move(clipped_limits),
        };
        const Layout old_layout = layout;
        layout = Layout::Empty;
        if (old_layout == Layout::Range) {
            // value is the original reassignable parameter; the facade transfers
            // it before the complete operation and deletes its old binding.
            previous[1].reset(); previous[2].reset();
            previous[3].reset(); previous[4].reset();
        } else if (old_layout == Layout::Limits) previous[0].reset();
        for (auto& reference : previous) reference.reset();
    }
};

int ivd_sequence_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self))
        Py_VISIT(nb::inst_ptr<ValidatorSequenceLocals>(self)->item.ptr());
    return 0;
}
int ivd_sequence_clear(PyObject* self) {
    if (nb::inst_ready(self)) nb::inst_ptr<ValidatorSequenceLocals>(self)->clear();
    return 0;
}
int ivd_pairwise_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) {
        const auto& state = *nb::inst_ptr<ValidatorPairwiseLocals>(self);
        Py_VISIT(state.a.ptr()); Py_VISIT(state.b.ptr());
    }
    return 0;
}
int ivd_pairwise_clear(PyObject* self) {
    if (nb::inst_ready(self)) nb::inst_ptr<ValidatorPairwiseLocals>(self)->clear();
    return 0;
}
int ivd_contrast_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) {
        const auto& state = *nb::inst_ptr<IntensityContrastLocals>(self);
        Py_VISIT(state.newrange.ptr()); Py_VISIT(state.value.ptr());
        Py_VISIT(state.current_range.ptr()); Py_VISIT(state.i.ptr());
        Py_VISIT(state.clipped_limits.ptr());
    }
    return 0;
}
int ivd_contrast_clear(PyObject* self) {
    if (nb::inst_ready(self)) nb::inst_ptr<IntensityContrastLocals>(self)->clear();
    return 0;
}
PyType_Slot ivd_sequence_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(ivd_sequence_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(ivd_sequence_clear)}, {0, nullptr},
};
PyType_Slot ivd_pairwise_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(ivd_pairwise_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(ivd_pairwise_clear)}, {0, nullptr},
};
PyType_Slot ivd_contrast_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(ivd_contrast_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(ivd_contrast_clear)}, {0, nullptr},
};

void validator_sequence_validate(nb::object obj, nb::object n, nb::object dtype,
                                  nb::dict globals, ValidatorSequenceLocals& owner) {
    IntensityValidationNamespace ns(std::move(globals));
    nb::object callable = ns.get("isinstance");
    nb::object generator = ns.get("Generator");
    if (ivd_truth(ivd_call2(std::move(callable), obj, std::move(generator)))) return;

    callable = ns.get("isinstance");
    nb::object collection = ns.get("Collection");
    bool indexable = ivd_truth(ivd_call2(std::move(callable), obj, std::move(collection)));
    if (indexable) {
        callable = ns.get("hasattr");
        indexable = ivd_truth(ivd_call2(std::move(callable), obj, nb::str("__getitem__")));
    }
    if (!indexable) {
        callable = ns.get("TypeError");
        nb::object object_text = ivd_format(obj);
        nb::object length_text = ivd_format(n);
        nb::object text = ivd_message({nb::str("object '"), std::move(object_text),
            nb::str("' is not an indexable collection (list, tuple, or np.array), of length "),
            std::move(length_text)});
        ivd_raise(ivd_call1(std::move(callable), std::move(text)));
    }

    nb::object length = ivd_call1(ns.get("len"), obj);
    nb::object different = ivd_compare(length, n, Py_NE);
    length.reset();
    if (ivd_truth(std::move(different))) {
        callable = ns.get("ValueError");
        nb::object expected_text = ivd_format(n);
        nb::object actual = ivd_call1(ns.get("len"), obj);
        nb::object actual_text = ivd_format(actual);
        actual.reset();
        nb::object text = ivd_message({nb::str("object must have length "),
            std::move(expected_text), nb::str(", got "), std::move(actual_text)});
        ivd_raise(ivd_call1(std::move(callable), std::move(text)));
    }

    if (!dtype.is_none()) {
        nb::object iterator = ivd_checked(PyObject_GetIter(obj.ptr()));
        for (;;) {
            PyObject* next = PyIter_Next(iterator.ptr());
            if (!next) { if (PyErr_Occurred()) throw nb::python_error(); break; }
            ivd_replace(owner.item, nb::steal<nb::object>(next));
            if (!ivd_truth(ivd_call2(ns.get("isinstance"), owner.item, dtype))) {
                callable = ns.get("TypeError");
                nb::object dtype_text = ivd_format(dtype);
                nb::object item_text = ivd_format(owner.item);
                nb::object type = ivd_call1(ns.get("type"), owner.item);
                nb::object type_text = ivd_format(type);
                type.reset();
                nb::object text = ivd_message({
                    nb::str("Every item in the sequence must be of type "), std::move(dtype_text),
                    nb::str(", but "), std::move(item_text), nb::str(" is of type "),
                    std::move(type_text)});
                ivd_raise(ivd_call1(std::move(callable), std::move(text)));
            }
        }
    }
}

struct IntensityValidationPair { nb::object first, second; };
IntensityValidationPair ivd_unpack_pair(nb::object value) {
    nb::object iterator = nb::steal<nb::object>(PyObject_GetIter(value.ptr()));
    if (!iterator.is_valid()) {
        if (PyErr_ExceptionMatches(PyExc_TypeError) && !Py_TYPE(value.ptr())->tp_iter
            && !PySequence_Check(value.ptr()))
            PyErr_Format(PyExc_TypeError, "cannot unpack non-iterable %.200s object",
                         Py_TYPE(value.ptr())->tp_name);
        throw nb::python_error();
    }
    value.reset();
    nb::object first = nb::steal<nb::object>(PyIter_Next(iterator.ptr()));
    if (!first.is_valid()) {
        if (PyErr_Occurred()) throw nb::python_error();
        throw nb::value_error("not enough values to unpack (expected 2, got 0)");
    }
    nb::object second = nb::steal<nb::object>(PyIter_Next(iterator.ptr()));
    if (!second.is_valid()) {
        if (PyErr_Occurred()) throw nb::python_error();
        throw nb::value_error("not enough values to unpack (expected 2, got 1)");
    }
    nb::object extra = nb::steal<nb::object>(PyIter_Next(iterator.ptr()));
    if (extra.is_valid()) throw nb::value_error("too many values to unpack (expected 2)");
    if (PyErr_Occurred()) throw nb::python_error();
    return {std::move(first), std::move(second)};
}

nb::object validator_pairwise(nb::object iterable, nb::dict globals,
                              ValidatorPairwiseLocals& owner) {
    IntensityValidationNamespace ns(std::move(globals));
    IntensityValidationPair pair = ivd_unpack_pair(ivd_call1(ns.get("tee"), iterable));
    ivd_replace(owner.a, std::move(pair.first));
    ivd_replace(owner.b, std::move(pair.second));
    ivd_call2(ns.get("next"), owner.b, nb::none());
    nb::object zip = ns.get("zip");
    nb::tuple args = nb::make_tuple(owner.a, owner.b);
    nb::dict kwargs;
    kwargs["strict"] = nb::bool_(false);
    return ivd_checked(PyObject_Call(zip.ptr(), args.ptr(), kwargs.ptr()));
}

// This is the comparison genexpr, not the public _pairwise zip. Its five
// references correspond to the original suspended iterator, assigned a/b and
// function namespace/builtins. No consumer, input values or state snapshot is
// rooted here. PyGeneratorType/gi_frame/gi_code identity is a named M0 gap.
struct ValidatorIncreasingIterator {
    bool started = false, running = false, closed = false;
    nb::object iterator, a, b, namespace_, builtins;
    ValidatorIncreasingIterator() = default;
    ValidatorIncreasingIterator(ValidatorIncreasingIterator&&) = default;
    ~ValidatorIncreasingIterator() { clear(); }
    void clear() {
        closed = true;
        std::array<nb::object, 5> previous = {
            std::move(iterator), std::move(a), std::move(b),
            std::move(namespace_), std::move(builtins),
        };
        for (auto& reference : previous) reference.reset();
    }
};
struct IncreasingRunning {
    ValidatorIncreasingIterator& state;
    explicit IncreasingRunning(ValidatorIncreasingIterator& state_) : state(state_) {
        if (state.running) throw nb::value_error("generator already executing");
        state.running = true;
    }
    ~IncreasingRunning() { state.running = false; }
};
nb::object ivd_increasing_next(nb::handle self) {
    auto& state = *nb::inst_ptr<ValidatorIncreasingIterator>(self);
    IncreasingRunning running(state);
    if (state.closed) throw nb::stop_iteration();
    state.started = true;
    try {
        PyObject* next = PyIter_Next(state.iterator.ptr());
        if (!next) {
            if (PyErr_Occurred()) throw nb::python_error();
            state.clear();
            throw nb::stop_iteration();
        }
        IntensityValidationPair pair = ivd_unpack_pair(nb::steal<nb::object>(next));
        ivd_replace(state.a, std::move(pair.first));
        ivd_replace(state.b, std::move(pair.second));
        return ivd_compare(state.a, state.b, Py_GE);
    } catch (const nb::python_error& error) {
        events::HandledException active(error.value());
        state.clear();
        if (error.matches(PyExc_StopIteration))
            collections::raise_exception(error.value(), true);
        throw;
    } catch (...) { state.clear(); throw; }
}
void ivd_increasing_close(nb::handle self) {
    auto& state = *nb::inst_ptr<ValidatorIncreasingIterator>(self);
    IncreasingRunning running(state);
    if (state.closed) return;
    if (!state.started) { state.clear(); return; }
    nb::object exit = ivd_call0(nb::borrow<nb::object>(PyExc_GeneratorExit));
    events::HandledException active(exit);
    state.clear();
}
void ivd_increasing_throw(nb::handle self, nb::args args) {
    nb::object exception = collections::normalize_throw(args);
    auto& state = *nb::inst_ptr<ValidatorIncreasingIterator>(self);
    IncreasingRunning running(state);
    const bool open = !state.closed;
    events::HandledException active(exception);
    state.clear();
    collections::raise_exception(exception, open);
}
int ivd_increasing_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) {
        const auto& state = *nb::inst_ptr<ValidatorIncreasingIterator>(self);
        Py_VISIT(state.iterator.ptr()); Py_VISIT(state.a.ptr()); Py_VISIT(state.b.ptr());
        Py_VISIT(state.namespace_.ptr()); Py_VISIT(state.builtins.ptr());
    }
    return 0;
}
int ivd_increasing_clear(PyObject* self) {
    if (nb::inst_ready(self)) nb::inst_ptr<ValidatorIncreasingIterator>(self)->clear();
    return 0;
}
void ivd_increasing_finalize(PyObject* self) noexcept {
    if (!nb::inst_ready(self) || nb::inst_ptr<ValidatorIncreasingIterator>(self)->closed) return;
    PyObject *type = nullptr, *value = nullptr, *traceback = nullptr;
    PyErr_Fetch(&type, &value, &traceback);
    try { ivd_increasing_close(self); }
    catch (nb::python_error& error) { error.restore(); PyErr_WriteUnraisable(self); }
    catch (const std::exception& error) {
        PyErr_SetString(PyExc_RuntimeError, error.what()); PyErr_WriteUnraisable(self);
    } catch (...) {
        PyErr_SetString(PyExc_RuntimeError, "failed to close increasing iterator");
        PyErr_WriteUnraisable(self);
    }
    PyErr_Restore(type, value, traceback);
}
PyType_Slot ivd_increasing_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(ivd_increasing_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(ivd_increasing_clear)},
    {Py_tp_finalize, reinterpret_cast<void*>(ivd_increasing_finalize)}, {0, nullptr},
};
destructor ivd_increasing_base_dealloc = nullptr;
void ivd_increasing_dealloc(PyObject* self) noexcept {
    // Match the established generator lifecycle: weakrefs die before close
    // releases payloads, and finalizer resurrection is honored by CPython.
    PyObject_GC_UnTrack(self);
    PyObject_ClearWeakRefs(self);
    PyObject_GC_Track(self);
    if (PyObject_CallFinalizerFromDealloc(self) < 0) return;
    ivd_increasing_base_dealloc(self);
}

void validator_increasing(nb::object values, nb::dict globals) {
    IntensityValidationNamespace ns(std::move(globals));
    nb::object consumer = ns.get("any");
    nb::object pairs = ivd_call1(ns.get("_pairwise"), values);
    ValidatorIncreasingIterator state;
    state.iterator = ivd_checked(PyObject_GetIter(pairs.ptr()));
    pairs.reset();
    state.namespace_ = ns.globals;
    state.builtins = ns.builtins;
    nb::object generator = nb::cast(std::move(state), nb::rv_policy::move);
    if (ivd_truth(ivd_call1(std::move(consumer), std::move(generator)))) {
        nb::object constructor = ns.get("ValueError");
        nb::object values_text = ivd_format(values);
        nb::object text = ivd_message({nb::str("Sequence "), std::move(values_text),
            nb::str(" must be monotonically increasing.")});
        ivd_raise(ivd_call1(std::move(constructor), std::move(text)));
    }
}

nb::object intensity_get_contrast_limits(nb::handle self, nb::dict globals) {
    IntensityValidationNamespace ns(std::move(globals));
    nb::object callable = ns.get("list");
    return ivd_call1(std::move(callable), ivd_attr(self, "_contrast_limits"));
}
nb::object intensity_get_contrast_limits_range(nb::handle self, nb::dict globals) {
    IntensityValidationNamespace ns(std::move(globals));
    nb::object callable = ns.get("list");
    return ivd_call1(std::move(callable), ivd_attr(self, "_contrast_limits_range"));
}
void intensity_set_contrast_limits(nb::handle self, nb::object contrast_limits,
                                    nb::dict globals, IntensityContrastLocals& owner) {
    owner.layout = IntensityContrastLocals::Layout::Limits;
    IntensityValidationNamespace ns(std::move(globals));
    ivd_call1(ns.get("validate_2_tuple"), contrast_limits);
    ivd_call1(ns.get("_validate_increasing"), contrast_limits);

    nb::object callable = ns.get("format_float");
    nb::object first = ivd_item(contrast_limits, nb::int_(0));
    nb::object prefix = ivd_call1(std::move(callable), std::move(first));
    prefix = ivd_checked(PyNumber_Add(prefix.ptr(), nb::str(", ").ptr()));
    callable = ns.get("format_float");
    nb::object second = ivd_item(contrast_limits, nb::int_(1));
    nb::object suffix = ivd_call1(std::move(callable), std::move(second));
    nb::object text = ivd_checked(PyNumber_Add(prefix.ptr(), suffix.ptr()));
    prefix.reset(); suffix.reset();
    ivd_store(self, "_contrast_limits_msg", std::move(text));
    ivd_store(self, "_contrast_limits", contrast_limits);

    callable = ns.get("list");
    ivd_replace(owner.newrange, ivd_call1(std::move(callable), ivd_attr(self, "contrast_limits_range")));
    callable = ns.get("min");
    first = ivd_item(owner.newrange, nb::int_(0));
    second = ivd_item(contrast_limits, nb::int_(0));
    nb::object replacement = ivd_call2(std::move(callable), std::move(first), std::move(second));
    ivd_set_item(owner.newrange, nb::int_(0), std::move(replacement));
    callable = ns.get("max");
    first = ivd_item(owner.newrange, nb::int_(1));
    second = ivd_item(contrast_limits, nb::int_(1));
    replacement = ivd_call2(std::move(callable), std::move(first), std::move(second));
    ivd_set_item(owner.newrange, nb::int_(1), std::move(replacement));
    ivd_store(self, "contrast_limits_range", owner.newrange);
    ivd_call0(ivd_attr(self, "_update_thumbnail"));
    ivd_call0(ivd_attr(ivd_attr(self, "events"), "contrast_limits"));
}

nb::tuple ivd_clip_arguments(nb::object first, nb::handle starred) {
    // This is the source call's positional * expansion, not an application
    // iterator policy. Match BUILD_LIST, LIST_EXTEND and LIST_TO_TUPLE rather
    // than collecting the starred argument in a separate tuple first.
    nb::list arguments;
    if (PyList_Append(arguments.ptr(), first.ptr()) < 0) throw nb::python_error();
    first.reset();
#if PY_VERSION_HEX < 0x030D0000
    nb::object extended = nb::steal<nb::object>(
        _PyList_Extend(reinterpret_cast<PyListObject*>(arguments.ptr()), starred.ptr()));
    const bool failed = !extended.is_valid();
#else
    const bool failed = PyList_Extend(arguments.ptr(), starred.ptr()) < 0;
#endif
    if (failed) {
        if (PyErr_ExceptionMatches(PyExc_TypeError) && !Py_TYPE(starred.ptr())->tp_iter
            && !PySequence_Check(starred.ptr()))
            PyErr_Format(PyExc_TypeError, "Value after * must be an iterable, not %.200s",
                         Py_TYPE(starred.ptr())->tp_name);
        throw nb::python_error();
    }
    return nb::steal<nb::tuple>(ivd_checked(PyList_AsTuple(arguments.ptr())).release().ptr());
}
void intensity_set_contrast_limits_range(nb::handle self, nb::dict globals,
                                          IntensityContrastLocals& owner) {
    owner.layout = IntensityContrastLocals::Layout::Range;
    IntensityValidationNamespace ns(std::move(globals));
    ivd_call1(ns.get("validate_2_tuple"), owner.value);
    ivd_call1(ns.get("_validate_increasing"), owner.value);
    nb::object requested = ivd_call1(ns.get("list"), owner.value);
    nb::object existing = ivd_attr(self, "contrast_limits_range");
    nb::object equal = ivd_compare(requested, existing, Py_EQ);
    requested.reset(); existing.reset();
    if (ivd_truth(std::move(equal))) return;

    ivd_replace(owner.current_range, ivd_attr(self, "contrast_limits_range"));
    ivd_replace(owner.value, ivd_call1(ns.get("list"), owner.value));
    nb::object range = ivd_call1(ns.get("range"), nb::int_(2));
    nb::object iterator = ivd_checked(PyObject_GetIter(range.ptr()));
    range.reset();
    for (;;) {
        PyObject* next = PyIter_Next(iterator.ptr());
        if (!next) { if (PyErr_Occurred()) throw nb::python_error(); break; }
        ivd_replace(owner.i, nb::steal<nb::object>(next));
        nb::object checked = ivd_item(owner.value, owner.i);
        const bool preserve = checked.is_none();
        checked.reset();
        nb::object replacement = preserve ? ivd_item(owner.current_range, owner.i)
                                          : ivd_item(owner.value, owner.i);
        ivd_set_item(owner.value, owner.i, std::move(replacement));
    }
    iterator.reset();
    ivd_store(self, "_contrast_limits_range", owner.value);
    ivd_call0(ivd_attr(ivd_attr(self, "events"), "contrast_limits_range"));

    const bool has_limits = ivd_truth(ivd_call2(ns.get("hasattr"),
        nb::borrow<nb::object>(self), nb::str("_contrast_limits")));
    if (!has_limits) return;
    nb::object consumer = ns.get("any");
    if (!ivd_truth(ivd_call1(std::move(consumer), ivd_attr(self, "_contrast_limits")))) return;
    nb::object clip = ivd_attr(ns.get("np"), "clip");
    nb::object limits = ivd_attr(self, "contrast_limits");
    nb::tuple args = ivd_clip_arguments(std::move(limits), owner.value);
    nb::object clipped = ivd_checked(PyObject_Call(clip.ptr(), args.ptr(), nullptr));
    clip.reset(); args.reset();
    ivd_replace(owner.clipped_limits, std::move(clipped));
    nb::object first = ivd_item(owner.clipped_limits, nb::int_(0));
    nb::object second = ivd_item(owner.clipped_limits, nb::int_(1));
    nb::object increasing = ivd_compare(first, second, Py_LT);
    first.reset(); second.reset();
    nb::object result = ivd_truth(std::move(increasing))
        ? ivd_call1(ns.get("tuple"), owner.clipped_limits)
        : ivd_call1(ns.get("tuple"), owner.value);
    ivd_store(self, "contrast_limits", std::move(result));
}

} // namespace

void bind_intensity_validation(nb::module_& module) {
    nb::class_<ValidatorSequenceLocals>(module, "ValidatorSequenceLocals",
        nb::is_weak_referenceable(), nb::type_slots(ivd_sequence_slots))
        .def_prop_ro("item", [](const ValidatorSequenceLocals& s) { return ivd_optional(s.item); });
    nb::class_<ValidatorPairwiseLocals>(module, "ValidatorPairwiseLocals",
        nb::is_weak_referenceable(), nb::type_slots(ivd_pairwise_slots))
        .def_prop_ro("a", [](const ValidatorPairwiseLocals& s) { return ivd_optional(s.a); })
        .def_prop_ro("b", [](const ValidatorPairwiseLocals& s) { return ivd_optional(s.b); });
    nb::class_<IntensityContrastLocals>(module, "IntensityContrastLocals",
        nb::is_weak_referenceable(), nb::type_slots(ivd_contrast_slots))
        .def_prop_ro("newrange", [](const IntensityContrastLocals& s) { return ivd_optional(s.newrange); })
        .def_prop_ro("value", [](const IntensityContrastLocals& s) { return ivd_optional(s.value); })
        .def_prop_ro("current_range", [](const IntensityContrastLocals& s) { return ivd_optional(s.current_range); })
        .def_prop_ro("i", [](const IntensityContrastLocals& s) { return ivd_optional(s.i); })
        .def_prop_ro("clipped_limits", [](const IntensityContrastLocals& s) { return ivd_optional(s.clipped_limits); });

    nb::class_<ValidatorIncreasingIterator> iterator(module, "ValidatorIncreasingIterator",
        nb::is_weak_referenceable(), nb::type_slots(ivd_increasing_slots));
    iterator.def("__iter__", [](nb::handle self) { return nb::borrow<nb::object>(self); })
        .def("__next__", &ivd_increasing_next)
        .def("send", [](nb::handle self, nb::object value) {
            const auto& state = *nb::inst_ptr<ValidatorIncreasingIterator>(self);
            if (state.running) throw nb::value_error("generator already executing");
            if (!state.closed && !state.started && !value.is_none())
                throw nb::type_error("can't send non-None value to a just-started generator");
            return ivd_increasing_next(self);
        }, nb::arg("value").none())
        .def("close", &ivd_increasing_close)
        .def("throw", &ivd_increasing_throw)
        .def_prop_ro("iterator", [](const ValidatorIncreasingIterator& s) { return ivd_optional(s.iterator); })
        .def_prop_ro("a", [](const ValidatorIncreasingIterator& s) { return ivd_optional(s.a); })
        .def_prop_ro("b", [](const ValidatorIncreasingIterator& s) { return ivd_optional(s.b); });
    auto* iterator_type = reinterpret_cast<PyTypeObject*>(iterator.ptr());
    ivd_increasing_base_dealloc = iterator_type->tp_dealloc;
    iterator_type->tp_dealloc = ivd_increasing_dealloc;
    PyType_Modified(iterator_type);

    module.def("validator_n_seq_bind", &validator_n_seq_bind, nb::arg("func").none());
    module.def("validator_sequence_locals", [] { return ValidatorSequenceLocals(); });
    module.def("validator_pairwise_locals", [] { return ValidatorPairwiseLocals(); });
    module.def("intensity_contrast_locals", [] { return IntensityContrastLocals(); });
    module.def("intensity_contrast_range_locals", [](nb::object value) {
        IntensityContrastLocals owner;
        owner.layout = IntensityContrastLocals::Layout::Range;
        owner.value = std::move(value);
        return owner;
    }, nb::arg("value").none());
    module.def("validator_sequence_validate", &validator_sequence_validate,
        nb::arg("obj").none(), nb::arg("n").none(), nb::arg("dtype").none(),
        nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("validator_pairwise", &validator_pairwise,
        nb::arg("iterable").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("validator_increasing", &validator_increasing,
        nb::arg("values").none(), nb::arg("namespace"));
    module.def("intensity_get_contrast_limits", &intensity_get_contrast_limits,
        nb::arg("self").none(), nb::arg("namespace"));
    module.def("intensity_get_contrast_limits_range", &intensity_get_contrast_limits_range,
        nb::arg("self").none(), nb::arg("namespace"));
    module.def("intensity_set_contrast_limits", &intensity_set_contrast_limits,
        nb::arg("self").none(), nb::arg("contrast_limits").none(),
        nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("intensity_set_contrast_limits_range", &intensity_set_contrast_limits_range,
        nb::arg("self").none(), nb::arg("namespace"), nb::arg("locals_owner"));
}
} // namespace nx
