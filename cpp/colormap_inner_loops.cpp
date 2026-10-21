#include "arrays.hpp"
#include <utility>

namespace nx {
namespace {

nb::object inner_checked(PyObject* value) {
    if (!value) throw nb::python_error();
    return nb::steal<nb::object>(value);
}

nb::object inner_attr(nb::object value, const char* name) {
    return inner_checked(PyObject_GetAttrString(value.ptr(), name));
}

nb::object inner_item(nb::object value, nb::object key) {
    return inner_checked(PyObject_GetItem(value.ptr(), key.ptr()));
}

nb::object inner_call1(nb::object callable, nb::object value) {
    return inner_checked(PyObject_CallOneArg(callable.ptr(), value.ptr()));
}

nb::object inner_iter(nb::object value) {
    return inner_checked(PyObject_GetIter(value.ptr()));
}

nb::object inner_next(nb::handle iterator) {
    PyObject* value = PyIter_Next(iterator.ptr());
    if (!value && PyErr_Occurred()) throw nb::python_error();
    return nb::steal<nb::object>(value);
}

nb::object inner_compare(nb::object left, nb::object right, int operation) {
    return inner_checked(PyObject_RichCompare(left.ptr(), right.ptr(), operation));
}

bool inner_truth(nb::object value) {
    const int result = PyObject_IsTrue(value.ptr());
    if (result < 0) throw nb::python_error();
    return result != 0;
}

nb::object inner_subtract(nb::object left, nb::object right) {
    return inner_checked(PyNumber_Subtract(left.ptr(), right.ptr()));
}

nb::object inner_remainder(nb::object left, nb::object right) {
    return inner_checked(PyNumber_Remainder(left.ptr(), right.ptr()));
}

nb::object inner_add(nb::object left, nb::object right) {
    return inner_checked(PyNumber_Add(left.ptr(), right.ptr()));
}

void inner_set_item(nb::object target, nb::object index, nb::object value) {
    if (PyObject_SetItem(target.ptr(), index.ptr(), value.ptr()) < 0)
        throw nb::python_error();
}

struct InnerNamespace {
    nb::dict globals;
    nb::object builtins;

    explicit InnerNamespace(nb::dict globals_)
        : globals(std::move(globals_)),
          builtins(nb::borrow<nb::object>(PyEval_GetBuiltins())) {}

    nb::object get(const char* name) const {
        nb::object key = nb::str(name);
        PyObject* found = PyDict_GetItemWithError(globals.ptr(), key.ptr());
        if (found) return nb::borrow<nb::object>(found);
        if (PyErr_Occurred()) throw nb::python_error();
        found = PyObject_GetItem(builtins.ptr(), key.ptr());
        if (found) return nb::steal<nb::object>(found);
        if (!PyErr_ExceptionMatches(PyExc_KeyError)) throw nb::python_error();
        PyErr_Clear();
        nb::object message = inner_checked(
            PyUnicode_FromFormat("name '%s' is not defined", name));
        nb::object error = inner_checked(
            PyObject_CallOneArg(PyExc_NameError, message.ptr()));
        if (PyObject_SetAttrString(error.ptr(), "name", key.ptr()) < 0)
            throw nb::python_error();
        PyErr_SetObject(PyExc_NameError, error.ptr());
        throw nb::python_error();
    }
};

// These are the two assigned loop locals, not an array or mapping cache. A
// facade keeps one owner local so a saved Python traceback retains the same
// last assignments after a native provider operation raises. No iterator or
// namespace is retained after the native invocation.
struct ColormapInnerLoopLocals {
    nb::object i;
    nb::object val;

    void assign_i(nb::object next) {
        // STORE_FAST publishes the new reference before releasing the old one.
        nb::object previous = std::move(i);
        i = std::move(next);
    }

    void assign_val(nb::object next) {
        nb::object previous = std::move(val);
        val = std::move(next);
    }

    void gc_clear() noexcept {
        // Detach both edges before finalizers can reenter a diagnostic view.
        nb::object previous_i = std::move(i);
        nb::object previous_val = std::move(val);
    }

    nb::object assigned_i() const {
        if (!i.is_valid())
            throw nb::attribute_error("loop local 'i' is unassigned");
        return i;
    }

    nb::object assigned_val() const {
        if (!val.is_valid())
            throw nb::attribute_error("loop local 'val' is unassigned");
        return val;
    }
};

int inner_locals_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(nb::handle(self))) {
        const auto* state = nb::inst_ptr<ColormapInnerLoopLocals>(nb::handle(self));
        Py_VISIT(state->i.ptr());
        Py_VISIT(state->val.ptr());
    }
    return 0;
}

int inner_locals_clear(PyObject* self) {
    if (nb::inst_ready(nb::handle(self)))
        nb::inst_ptr<ColormapInnerLoopLocals>(nb::handle(self))->gc_clear();
    return 0;
}

PyType_Slot inner_locals_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(inner_locals_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(inner_locals_clear)},
    {0, nullptr},
};

uint64_t inner_integer_bits(nb::handle value) {
    nb::object integer = inner_checked(PyNumber_Index(value.ptr()));
    const unsigned long long bits = PyLong_AsUnsignedLongLongMask(integer.ptr());
    if (PyErr_Occurred()) throw nb::python_error();
    return static_cast<uint64_t>(bits);
}

int64_t inner_signed_bits(uint64_t value) {
    int64_t result;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

nb::object inner_compiled_integer_modulo(nb::object value, int64_t divisor) {
    // The original compiled path promotes small integers to machine signed
    // width, including mixed uint64 operands. Unsigned bit operations make
    // the observed subtract/add wrapping explicit without C++ signed UB.
    const int64_t dividend = inner_signed_bits(inner_integer_bits(value) - 1);
    int64_t remainder = 0;
    if (divisor != 0 &&
        !(dividend == std::numeric_limits<int64_t>::min() && divisor == -1)) {
        remainder = dividend % divisor;
        if (remainder != 0 && ((remainder < 0) != (divisor < 0)))
            remainder = inner_signed_bits(static_cast<uint64_t>(remainder) +
                                          static_cast<uint64_t>(divisor));
    }
    const int64_t result = inner_signed_bits(static_cast<uint64_t>(remainder) + 1);
    return inner_checked(PyLong_FromLongLong(result));
}

struct InnerNumericType {
    bool floating = false;
    bool unsigned_integer = false;
    unsigned bits = 64;
    bool boolean = false;
};

InnerNumericType inner_dtype_type(nb::object dtype) {
    nb::object kind = inner_attr(dtype, "kind");
    const char* text = PyUnicode_AsUTF8(kind.ptr());
    if (!text) throw nb::python_error();
    nb::object itemsize = inner_attr(std::move(dtype), "itemsize");
    const unsigned long width = PyLong_AsUnsignedLong(itemsize.ptr());
    if (PyErr_Occurred()) throw nb::python_error();
    if (text[0] == 'f') return {true, false, static_cast<unsigned>(width * 8)};
    if (text[0] == 'b') return {false, true, 1, true};
    if (text[0] == 'u')
        return {false, true, static_cast<unsigned>(width * 8)};
    if (text[0] == 'i') return {false, false, static_cast<unsigned>(width * 8)};
    throw nb::type_error("compiled label arithmetic requires an integer or real dtype");
}

InnerNumericType inner_scalar_type(nb::handle value) {
    if (PyBool_Check(value.ptr())) return {false, true, 1, true};
    if (PyLong_Check(value.ptr())) {
        int overflow = 0;
        (void) PyLong_AsLongLongAndOverflow(value.ptr(), &overflow);
        if (PyErr_Occurred()) throw nb::python_error();
        return {false, overflow > 0, 64};
    }
    if (PyFloat_Check(value.ptr())) return {true, false, 64};
    return inner_dtype_type(inner_attr(nb::borrow<nb::object>(value), "dtype"));
}

double inner_double(nb::handle value) {
    const double result = PyFloat_AsDouble(value.ptr());
    if (PyErr_Occurred()) throw nb::python_error();
    return result;
}

struct InnerCompiledArithmetic {
    InnerNumericType values_type;
    InnerNumericType background_type;
    bool real_arithmetic = false;
    bool real_comparison = false;
    int64_t integer_divisor = 0;
    uint64_t integer_background = 0;
    double real_divisor = 0;
    double real_background = 0;

    InnerCompiledArithmetic(nb::object values, nb::handle n, nb::handle background)
        : values_type(inner_dtype_type(inner_attr(std::move(values), "dtype"))),
          background_type(inner_scalar_type(background)) {
        const InnerNumericType divisor_type = inner_scalar_type(n);
        real_arithmetic = values_type.floating || divisor_type.floating;
        if (real_arithmetic) real_divisor = inner_double(n);
        else integer_divisor = inner_signed_bits(inner_integer_bits(n));

        // The evidenced Numba equality signature promotes a mixed signed and
        // uint64 pair to float64. Preserve its rounding, including adjacent
        // integer labels that consequently compare equal to the background.
        real_comparison = values_type.floating || background_type.floating ||
            (values_type.unsigned_integer != background_type.unsigned_integer &&
             ((values_type.unsigned_integer && values_type.bits == 64) ||
              (background_type.unsigned_integer && background_type.bits == 64)));
        if (real_comparison) real_background = inner_double(background);
        else integer_background = inner_integer_bits(background);
    }

    bool is_background(nb::object value) const {
        if (real_comparison) return inner_double(value) == real_background;
        return inner_integer_bits(value) == integer_background;
    }

    nb::object modulo(nb::object value) const {
        if (!real_arithmetic)
            return inner_compiled_integer_modulo(std::move(value), integer_divisor);
        // Both float32 and float64 input expressions promote to float64 at
        // the subtraction of the integer literal. fmod keeps compiled-mode
        // zero/nonfinite results without invoking NumPy's warning machinery.
        const double dividend = values_type.floating
            ? inner_double(value) - 1.0
            : static_cast<double>(inner_signed_bits(inner_integer_bits(value) - 1));
        double remainder = std::fmod(dividend, real_divisor);
        if (remainder != 0.0) {
            if ((real_divisor < 0.0) != (remainder < 0.0)) remainder += real_divisor;
        } else {
            remainder = real_divisor < 0.0 ? -0.0 : 0.0;
        }
        return inner_checked(PyFloat_FromDouble(remainder + 1.0));
    }
};

Py_ssize_t inner_compiled_size(nb::object values) {
    nb::object size = inner_attr(std::move(values), "size");
    nb::object integer = inner_checked(PyNumber_Index(size.ptr()));
    const Py_ssize_t result = PyLong_AsSsize_t(integer.ptr());
    if (PyErr_Occurred()) throw nb::python_error();
    return result;
}

nb::object inner_compiled_output_value(nb::object value, const InnerNumericType& dtype) {
    if (dtype.boolean) return nb::bool_(inner_truth(std::move(value)));
    if (dtype.floating) {
        double result = inner_double(value);
        if (dtype.bits == 32 && std::isfinite(result)) {
            const double maximum = static_cast<double>(std::numeric_limits<float>::max());
            if (result > maximum) result = std::numeric_limits<double>::infinity();
            else if (result < -maximum) result = -std::numeric_limits<double>::infinity();
            else result = static_cast<double>(static_cast<float>(result));
        }
        return inner_checked(PyFloat_FromDouble(result));
    }

    uint64_t bits;
    if (PyIndex_Check(value.ptr())) {
        bits = inner_integer_bits(value);
    } else {
        const double number = inner_double(value);
        // The pinned compiled x64 path produces INT64_MIN for the probed
        // nonfinite real-to-int64 stores. Guard before every C++ conversion.
        const double signed_limit = std::ldexp(1.0, 63);
        const double unsigned_limit = std::ldexp(1.0, 64);
        if (!std::isfinite(number)) bits = uint64_t{1} << 63;
        else if (number >= -signed_limit && number < signed_limit)
            bits = static_cast<uint64_t>(static_cast<int64_t>(number));
        else if (dtype.unsigned_integer && number >= 0.0 && number < unsigned_limit)
            bits = static_cast<uint64_t>(number);
        else bits = uint64_t{1} << 63;
    }

    if (dtype.bits < 64) {
        const uint64_t mask = (uint64_t{1} << dtype.bits) - 1;
        bits &= mask;
        if (!dtype.unsigned_integer && (bits & (uint64_t{1} << (dtype.bits - 1))))
            bits |= ~mask;
    }
    if (dtype.unsigned_integer)
        return inner_checked(PyLong_FromUnsignedLongLong(bits));
    return inner_checked(PyLong_FromLongLong(inner_signed_bits(bits)));
}

void inner_compiled_set_item(nb::object out, nb::object index, nb::object value,
                             const InnerNumericType& dtype) {
    value = inner_compiled_output_value(std::move(value), dtype);
    inner_set_item(inner_attr(std::move(out), "flat"), std::move(index), std::move(value));
}

nb::object colormap_zero_preserving_modulo_inner(
    nb::object values, nb::object n, nb::object to_zero, nb::object out,
    nb::dict globals, ColormapInnerLoopLocals& locals, bool compiled_arithmetic) {
    if (compiled_arithmetic) {
        InnerCompiledArithmetic arithmetic(values, n, to_zero);
        const InnerNumericType output_type = inner_dtype_type(inner_attr(out, "dtype"));
        const Py_ssize_t size = inner_compiled_size(values);
        // An already-compiled original invocation executes native prange. It
        // never calls a newly rebound Python global prange provider. Keep this
        // mode fixed for retained dispatcher aliases after a module reload.
        for (Py_ssize_t i = 0; i < size; ++i) {
            nb::object index = nb::int_(i);
            if (arithmetic.is_background(inner_item(inner_attr(values, "flat"), index))) {
                inner_compiled_set_item(out, std::move(index), nb::int_(0), output_type);
            } else {
                nb::object value = inner_item(inner_attr(values, "flat"), index);
                value = arithmetic.modulo(std::move(value));
                inner_compiled_set_item(out, std::move(index), std::move(value), output_type);
            }
        }
        return out;
    }
    InnerNamespace namespace_(std::move(globals));
    // LOAD_GLOBAL precedes the provider's .size read. Use its iterator without
    // materializing indices or normalizing the caller's shape/strides/dtype.
    nb::object prange = namespace_.get("prange");
    nb::object size = inner_attr(values, "size");
    nb::object iterator = inner_iter(
        inner_call1(std::move(prange), std::move(size)));
    while (nb::object next = inner_next(iterator)) {
        locals.assign_i(std::move(next));
        nb::handle index = locals.i;
        if (inner_truth(inner_compare(
                inner_item(inner_attr(values, "flat"), nb::borrow<nb::object>(index)),
                to_zero, Py_EQ))) {
            inner_set_item(inner_attr(out, "flat"), nb::borrow<nb::object>(index), nb::int_(0));
        } else {
            // The non-background branch intentionally reads values.flat[i]
            // again. Python's scalar operators are declared primitives here;
            // their promotion/wrapping differs from the compiled integer path.
            nb::object value = inner_item(inner_attr(values, "flat"),
                                         nb::borrow<nb::object>(index));
            value = inner_subtract(std::move(value), nb::int_(1));
            value = inner_remainder(std::move(value), n);
            value = inner_add(std::move(value), nb::int_(1));
            inner_set_item(inner_attr(out, "flat"), nb::borrow<nb::object>(index),
                           std::move(value));
        }
    }
    return out;
}

nb::object colormap_labels_raw_to_texture_direct_inner(
    nb::object data, nb::object dkt, nb::object out,
    nb::dict globals, ColormapInnerLoopLocals& locals, bool compiled_arithmetic) {
    if (compiled_arithmetic) {
        const InnerNumericType output_type = inner_dtype_type(inner_attr(out, "dtype"));
        const Py_ssize_t size = inner_compiled_size(data);
        for (Py_ssize_t i = 0; i < size; ++i) {
            nb::object index = nb::int_(i);
            nb::object val = inner_item(inner_attr(data, "flat"), index);
            const int found = PySequence_Contains(dkt.ptr(), val.ptr());
            val = nb::object();
            if (found < 0) throw nb::python_error();
            if (found) {
                nb::object value = inner_item(dkt, inner_item(inner_attr(data, "flat"), index));
                inner_compiled_set_item(out, std::move(index), std::move(value), output_type);
            }
        }
        return out;
    }
    InnerNamespace namespace_(std::move(globals));
    nb::object prange = namespace_.get("prange");
    nb::object size = inner_attr(data, "size");
    nb::object iterator = inner_iter(
        inner_call1(std::move(prange), std::move(size)));
    while (nb::object next = inner_next(iterator)) {
        locals.assign_i(std::move(next));
        nb::handle index = locals.i;
        // Evaluate getitem first: a failed read retains the prior assigned val.
        nb::object current_val = inner_item(inner_attr(data, "flat"),
                                           nb::borrow<nb::object>(index));
        locals.assign_val(std::move(current_val));
        const int found = PySequence_Contains(dkt.ptr(), locals.val.ptr());
        if (found < 0) throw nb::python_error();
        if (found) {
            nb::object value = inner_item(dkt,
                inner_item(inner_attr(data, "flat"), nb::borrow<nb::object>(index)));
            inner_set_item(inner_attr(out, "flat"), nb::borrow<nb::object>(index),
                           std::move(value));
        }
        // Missing keys leave the original, possibly prefilled output intact.
    }
    return out;
}

// This wrapper replaces only the owned jit-dispatch stage. Its immutable
// execution mode belongs to this instance, so retained aliases survive reloads
// without consulting a new module-global flag. The actual plain facade and
// standard wrapper metadata remain shared Python objects with visible GC edges.
struct InnerDispatcherState {
    nb::object py_func;

    explicit InnerDispatcherState(nb::object function)
        : py_func(std::move(function)) {
        if (!PyFunction_Check(py_func.ptr()))
            throw nb::type_error("inner-loop dispatcher requires a Python facade function");
    }

    nb::dict globals() const {
        if (!py_func.is_valid())
            throw std::runtime_error("inner-loop dispatcher has been cleared");
        return nb::borrow<nb::dict>(PyFunction_GetGlobals(py_func.ptr()));
    }

};

struct ColormapModuloDispatcher : InnerDispatcherState {
    using InnerDispatcherState::InnerDispatcherState;

    nb::object call(nb::object values, nb::object n, nb::object to_zero, nb::object out) const {
        ColormapInnerLoopLocals locals;
        return colormap_zero_preserving_modulo_inner(
            std::move(values), std::move(n), std::move(to_zero), std::move(out),
            globals(), locals, true);
    }
};

struct ColormapDirectDispatcher : InnerDispatcherState {
    using InnerDispatcherState::InnerDispatcherState;

    nb::object call(nb::object data, nb::object dkt, nb::object out) const {
        ColormapInnerLoopLocals locals;
        return colormap_labels_raw_to_texture_direct_inner(
            std::move(data), std::move(dkt), std::move(out), globals(), locals, true);
    }
};

template <typename State>
int inner_dispatcher_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(nb::handle(self))) {
        const auto* state = nb::inst_ptr<State>(nb::handle(self));
        Py_VISIT(state->py_func.ptr());
    }
    // Custom slots override nanobind's dynamic-attribute visitor. Visit the
    // actual existing instance dictionary without allocating one during GC.
    PyObject** dictionary = _PyObject_GetDictPtr(self);
    if (dictionary) Py_VISIT(*dictionary);
    return 0;
}

template <typename State>
int inner_dispatcher_clear(PyObject* self) {
    nb::object previous_func;
    if (nb::inst_ready(nb::handle(self)))
        previous_func = std::move(nb::inst_ptr<State>(nb::handle(self))->py_func);
    PyObject** dictionary = _PyObject_GetDictPtr(self);
    PyObject* previous_dictionary = dictionary ? *dictionary : nullptr;
    if (dictionary) *dictionary = nullptr;
    // Both persistent edges are detached before either payload is released.
    Py_XDECREF(previous_dictionary);
    return 0;
}

template <typename State>
PyType_Slot* inner_dispatcher_slots() {
    static PyType_Slot slots[] = {
        {Py_tp_traverse, reinterpret_cast<void*>(inner_dispatcher_traverse<State>)},
        {Py_tp_clear, reinterpret_cast<void*>(inner_dispatcher_clear<State>)},
        {0, nullptr},
    };
    return slots;
}

template <typename State>
void inner_dispatcher_metadata(nb::class_<State>& binding) {
    binding
        .def_prop_ro("py_func", [](const State& state) -> nb::object {
            return state.py_func.is_valid() ? state.py_func : nb::none();
        });
}

void inner_install_metadata(nb::object dispatcher, nb::object py_func) {
    // Standard metadata is a mutable snapshot on the instance, as with
    // functools.update_wrapper. Class-level __qualname__/__module__ keep their
    // required string values. __annotations__ and __wrapped__ preserve aliases.
    for (const char* name : {"__module__", "__name__", "__qualname__",
                             "__doc__", "__annotations__"}) {
        nb::object value = inner_attr(py_func, name);
        if (PyObject_SetAttrString(dispatcher.ptr(), name, value.ptr()) < 0)
            throw nb::python_error();
    }
    PyObject* raw_dictionary = PyObject_GenericGetDict(dispatcher.ptr(), nullptr);
    if (!raw_dictionary) throw nb::python_error();
    nb::dict dictionary = nb::steal<nb::dict>(raw_dictionary);
    nb::object attributes = inner_attr(py_func, "__dict__");
    if (PyDict_Update(dictionary.ptr(), attributes.ptr()) < 0)
        throw nb::python_error();
    if (PyObject_SetAttrString(dispatcher.ptr(), "__wrapped__", py_func.ptr()) < 0)
        throw nb::python_error();
}

void colormap_compile_inner_loops(nb::dict globals) {
    InnerNamespace namespace_(std::move(globals));
    if (namespace_.get("numba").is_none()) return;

    // Publish in the original wrapping order, retaining each exact facade.
    nb::object modulo_facade = namespace_.get("_zero_preserving_modulo_inner_loop");
    nb::object modulo = nb::cast(
        new ColormapModuloDispatcher(modulo_facade),
        nb::rv_policy::take_ownership);
    inner_install_metadata(modulo, std::move(modulo_facade));
    if (PyDict_SetItemString(namespace_.globals.ptr(),
                            "_zero_preserving_modulo_inner_loop", modulo.ptr()) < 0)
        throw nb::python_error();
    nb::object direct_facade = namespace_.get("_labels_raw_to_texture_direct_inner_loop");
    nb::object direct = nb::cast(
        new ColormapDirectDispatcher(direct_facade),
        nb::rv_policy::take_ownership);
    inner_install_metadata(direct, std::move(direct_facade));
    if (PyDict_SetItemString(namespace_.globals.ptr(),
                            "_labels_raw_to_texture_direct_inner_loop", direct.ptr()) < 0)
        throw nb::python_error();
}

} // namespace

void bind_colormap_inner_loops(nb::module_& m) {
    nb::class_<ColormapModuloDispatcher> modulo(
        m, "ColormapModuloDispatcher",
        nb::type_slots(inner_dispatcher_slots<ColormapModuloDispatcher>()),
        nb::dynamic_attr(), nb::is_weak_referenceable());
    modulo.def("__call__", &ColormapModuloDispatcher::call,
               nb::arg("values").none(), nb::arg("n").none(),
               nb::arg("to_zero").none(), nb::arg("out").none());
    inner_dispatcher_metadata(modulo);
    nb::class_<ColormapDirectDispatcher> direct(
        m, "ColormapDirectDispatcher",
        nb::type_slots(inner_dispatcher_slots<ColormapDirectDispatcher>()),
        nb::dynamic_attr(), nb::is_weak_referenceable());
    direct.def("__call__", &ColormapDirectDispatcher::call,
               nb::arg("data").none(), nb::arg("dkt").none(), nb::arg("out").none());
    inner_dispatcher_metadata(direct);

    nb::class_<ColormapInnerLoopLocals>(
        m, "ColormapInnerLoopLocals", nb::type_slots(inner_locals_slots),
        nb::is_weak_referenceable())
        .def(nb::init<>())
        .def_prop_ro("has_i", [](const ColormapInnerLoopLocals& state) {
            return state.i.is_valid();
        })
        .def_prop_ro("has_val", [](const ColormapInnerLoopLocals& state) {
            return state.val.is_valid();
        })
        .def_prop_ro("i", &ColormapInnerLoopLocals::assigned_i)
        .def_prop_ro("val", &ColormapInnerLoopLocals::assigned_val);
    m.def("colormap_inner_loop_locals", [] {
        return new ColormapInnerLoopLocals();
    }, nb::rv_policy::take_ownership);
    m.def("colormap_zero_preserving_modulo_inner",
          &colormap_zero_preserving_modulo_inner,
          nb::arg("values").none(), nb::arg("n").none(),
          nb::arg("to_zero").none(), nb::arg("out").none(),
          nb::arg("namespace"), nb::arg("owner"),
          nb::arg("compiled_arithmetic") = false);
    m.def("colormap_labels_raw_to_texture_direct_inner",
          &colormap_labels_raw_to_texture_direct_inner,
          nb::arg("data").none(), nb::arg("dkt").none(),
          nb::arg("out").none(), nb::arg("namespace"), nb::arg("owner"),
          nb::arg("compiled_arithmetic") = false);
    m.def("colormap_compile_inner_loops", &colormap_compile_inner_loops,
          nb::arg("namespace"));
}

} // namespace nx
