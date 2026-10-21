#include "arrays.hpp"
#include "iterator_frame.hpp"
#include <utility>

namespace nx {
namespace {

nb::object accel_checked(PyObject* value) {
    if (!value) throw nb::python_error();
    return nb::steal<nb::object>(value);
}

nb::object accel_attr(nb::object value, const char* name) {
    return accel_checked(PyObject_GetAttrString(value.ptr(), name));
}

nb::object accel_item(nb::object value, nb::object key) {
    return accel_checked(PyObject_GetItem(value.ptr(), key.ptr()));
}

nb::object accel_iter(nb::object value) {
    return accel_checked(PyObject_GetIter(value.ptr()));
}

nb::object accel_call0(nb::object callable) {
    return accel_checked(PyObject_CallNoArgs(callable.ptr()));
}

nb::object accel_call1(nb::object callable, nb::object value) {
    return accel_checked(PyObject_CallOneArg(callable.ptr(), value.ptr()));
}

nb::object accel_call2(nb::object callable, nb::object first, nb::object second) {
    return accel_checked(PyObject_CallFunctionObjArgs(
        callable.ptr(), first.ptr(), second.ptr(), nullptr));
}

nb::object accel_call3(nb::object callable, nb::object first,
                       nb::object second, nb::object third) {
    return accel_checked(PyObject_CallFunctionObjArgs(
        callable.ptr(), first.ptr(), second.ptr(), third.ptr(), nullptr));
}

nb::object accel_call4(nb::object callable, nb::object first,
                       nb::object second, nb::object third, nb::object fourth) {
    return accel_checked(PyObject_CallFunctionObjArgs(
        callable.ptr(), first.ptr(), second.ptr(), third.ptr(), fourth.ptr(), nullptr));
}

nb::object accel_call_kw(nb::object callable, nb::tuple arguments,
                        const char* keyword, nb::object value) {
    nb::dict keywords;
    keywords[nb::str(keyword)] = value;
    return accel_checked(PyObject_Call(callable.ptr(), arguments.ptr(), keywords.ptr()));
}

bool accel_truth(nb::handle value) {
    const int result = PyObject_IsTrue(value.ptr());
    if (result < 0) throw nb::python_error();
    return result != 0;
}

bool accel_not_none(nb::object value) { return !value.is_none(); }

nb::object accel_compare(nb::object first, nb::object second, int operation) {
    return accel_checked(PyObject_RichCompare(first.ptr(), second.ptr(), operation));
}

bool accel_test(nb::object first, nb::object second, int operation) {
    return accel_truth(accel_compare(std::move(first), std::move(second), operation));
}

nb::object accel_binary(PyObject* (*operation)(PyObject*, PyObject*),
                        nb::object first, nb::object second) {
    return accel_checked(operation(first.ptr(), second.ptr()));
}

struct AccelNamespace {
    nb::dict globals;
    nb::object builtins;

    explicit AccelNamespace(nb::dict namespace_)
        : globals(std::move(namespace_)),
          builtins(nb::borrow<nb::object>(PyEval_GetBuiltins())) {}

    nb::object get(const char* name) const {
        nb::object key = nb::str(name);
        PyObject* value = PyDict_GetItemWithError(globals.ptr(), key.ptr());
        if (value) return nb::borrow<nb::object>(value);
        if (PyErr_Occurred()) throw nb::python_error();
        value = PyObject_GetItem(builtins.ptr(), key.ptr());
        if (value) return nb::steal<nb::object>(value);
        if (!PyErr_ExceptionMatches(PyExc_KeyError)) throw nb::python_error();
        PyErr_Clear();
        nb::object text = accel_checked(PyUnicode_FromFormat("name '%s' is not defined", name));
        nb::object error = accel_checked(PyObject_CallOneArg(PyExc_NameError, text.ptr()));
        if (PyObject_SetAttrString(error.ptr(), "name", key.ptr()) < 0) throw nb::python_error();
        PyErr_SetObject(PyExc_NameError, error.ptr());
        throw nb::python_error();
    }

    nb::object numpy(const char* name) const { return accel_attr(get("np"), name); }

    void set(const char* name, nb::object value) const {
        if (PyDict_SetItemString(globals.ptr(), name, value.ptr()) < 0)
            throw nb::python_error();
    }
};

nb::object accel_minimum_dtype(nb::object count, nb::dict globals) {
    AccelNamespace namespace_(std::move(globals));
    nb::object callable = namespace_.numpy("iinfo");
    nb::object type = namespace_.numpy("uint8");
    nb::object maximum = accel_attr(accel_call1(std::move(callable), std::move(type)), "max");
    if (accel_test(count, std::move(maximum), Py_LE)) {
        callable = namespace_.numpy("dtype");
        type = namespace_.numpy("uint8");
        return accel_call1(std::move(callable), std::move(type));
    }
    callable = namespace_.numpy("iinfo");
    type = namespace_.numpy("uint16");
    maximum = accel_attr(accel_call1(std::move(callable), std::move(type)), "max");
    if (accel_test(std::move(count), std::move(maximum), Py_LE)) {
        callable = namespace_.numpy("dtype");
        type = namespace_.numpy("uint16");
        return accel_call1(std::move(callable), std::move(type));
    }
    callable = namespace_.numpy("dtype");
    type = namespace_.numpy("float32");
    return accel_call1(std::move(callable), std::move(type));
}

nb::object accel_modulo_numpy_result(nb::object values, nb::object divisor,
                                    nb::object dtype, nb::dict globals) {
    AccelNamespace namespace_(std::move(globals));
    nb::object callable = namespace_.numpy("iinfo");
    nb::object maximum = accel_attr(accel_call1(std::move(callable), dtype), "max");
    if (accel_test(divisor, std::move(maximum), Py_GT))
        return accel_call1(accel_attr(std::move(values), "astype"), std::move(dtype));
    nb::object transformed = accel_binary(PyNumber_Subtract, std::move(values), nb::int_(1));
    transformed = accel_binary(PyNumber_Remainder, std::move(transformed), std::move(divisor));
    transformed = accel_binary(PyNumber_Add, std::move(transformed), nb::int_(1));
    return accel_call1(accel_attr(std::move(transformed), "astype"), std::move(dtype));
}

nb::object accel_modulo_zero_result(nb::object values, nb::object result,
                                   nb::object to_zero) {
    nb::object index = accel_compare(std::move(values), std::move(to_zero), Py_EQ);
    if (PyObject_SetItem(result.ptr(), index.ptr(), nb::int_(0).ptr()) < 0)
        throw nb::python_error();
    return result;
}

nb::object accel_modulo_loop_result(nb::object values, nb::object dtype,
                                   nb::dict globals) {
    AccelNamespace namespace_(std::move(globals));
    nb::object callable = namespace_.numpy("empty_like");
    return accel_call_kw(std::move(callable), nb::make_tuple(std::move(values)),
                         "dtype", std::move(dtype));
}

nb::object accel_modulo_loop_apply(nb::object values, nb::object divisor,
                                  nb::object to_zero, nb::object result,
                                  nb::dict globals) {
    AccelNamespace namespace_(std::move(globals));
    nb::object callable = namespace_.get("_zero_preserving_modulo_inner_loop");
    return accel_call_kw(std::move(callable),
        nb::make_tuple(std::move(values), std::move(divisor), std::move(to_zero)),
        "out", std::move(result));
}

bool accel_numpy_clip_version(nb::dict globals) {
    AccelNamespace namespace_(std::move(globals));
    nb::object left = accel_call1(namespace_.get("parse"), nb::str("2.0"));
    nb::object callable = namespace_.get("parse");
    nb::object current = accel_call1(namespace_.get("version"), nb::str("numpy"));
    current = accel_call1(std::move(callable), std::move(current));
    if (!accel_test(std::move(left), current, Py_LE)) return false;
    nb::object upper = accel_call1(namespace_.get("parse"), nb::str("2.1"));
    return accel_test(std::move(current), std::move(upper), Py_LT);
}

nb::object accel_clip_info(nb::object data, nb::dict globals) {
    AccelNamespace namespace_(std::move(globals));
    nb::object callable = namespace_.numpy("iinfo");
    nb::object dtype = accel_attr(std::move(data), "dtype");
    return accel_call1(std::move(callable), std::move(dtype));
}

nb::object accel_clip_min(nb::object minimum, nb::object info, nb::dict globals) {
    AccelNamespace namespace_(std::move(globals));
    nb::object callable = namespace_.get("max");
    nb::object bound = accel_attr(std::move(info), "min");
    return accel_call2(std::move(callable), std::move(minimum), std::move(bound));
}

nb::object accel_clip_max(nb::object maximum, nb::object info, nb::dict globals) {
    AccelNamespace namespace_(std::move(globals));
    nb::object callable = namespace_.get("min");
    nb::object bound = accel_attr(std::move(info), "max");
    return accel_call2(std::move(callable), std::move(maximum), std::move(bound));
}

nb::object accel_clip(nb::object data, nb::object minimum, nb::object maximum,
                      nb::dict globals) {
    AccelNamespace namespace_(std::move(globals));
    nb::object callable = namespace_.numpy("clip");
    return accel_call3(std::move(callable), std::move(data),
                       std::move(minimum), std::move(maximum));
}

bool accel_direct_selection(nb::object colormap) {
    return accel_truth(accel_attr(std::move(colormap), "use_selection"));
}

nb::object accel_direct_selected(nb::object data, nb::object colormap,
                                 nb::dict globals) {
    AccelNamespace namespace_(std::move(globals));
    nb::object selection = accel_attr(std::move(colormap), "selection");
    nb::object comparison = accel_compare(std::move(data), std::move(selection), Py_EQ);
    nb::object callable = accel_attr(std::move(comparison), "astype");
    nb::object dtype = namespace_.numpy("uint8");
    return accel_call1(std::move(callable), std::move(dtype));
}

nb::object accel_direct_mapper(nb::object colormap) {
    return accel_attr(std::move(colormap), "_array_map");
}

// This iterator is the handwritten native body of the source's lazy negative-
// key generator expression. A replaced `any` sees the lazy iterator, and its
// suspended references are visible to Python's cyclic collector.
struct AccelNegativeKeys : collections::IteratorFrame {
    bool started = false;
    bool running = false;
    bool closed = false;

    void gc_clear() {
        closed = true;
        release_frame();
    }
};

struct AccelGeneratorRunning {
    AccelNegativeKeys& state;
    explicit AccelGeneratorRunning(AccelNegativeKeys& state_) : state(state_) {
        if (state.running) throw nb::value_error("generator already executing");
        state.running = true;
    }
    ~AccelGeneratorRunning() { state.running = false; }
};

struct AccelHandledException {
    PyObject* previous = PyErr_GetHandledException();
    explicit AccelHandledException(nb::handle exception) {
        Py_INCREF(exception.ptr());
        PyErr_SetExcInfo(nullptr, exception.ptr(), nullptr);
    }
    ~AccelHandledException() { PyErr_SetExcInfo(nullptr, previous, nullptr); }
};

[[noreturn]] void accel_generator_raise(nb::object exception, bool convert_stop_iteration) {
    if (convert_stop_iteration
        && PyErr_GivenExceptionMatches(exception.ptr(), PyExc_StopIteration)) {
        nb::object converted = accel_checked(PyObject_CallFunction(
            PyExc_RuntimeError, "s", "generator raised StopIteration"));
        PyException_SetCause(converted.ptr(), exception.inc_ref().ptr());
        PyException_SetContext(converted.ptr(), exception.inc_ref().ptr());
        PyErr_SetObject(PyExc_RuntimeError, converted.ptr());
    } else {
        PyErr_SetObject(reinterpret_cast<PyObject*>(Py_TYPE(exception.ptr())), exception.ptr());
    }
    throw nb::python_error();
}

nb::object accel_negative_next(nb::handle self) {
    auto& state = *nb::inst_ptr<AccelNegativeKeys>(self);
    AccelGeneratorRunning guard(state);
    state.started = true;
    if (state.closed) throw nb::stop_iteration();
    try {
        nb::dict frame = state.frame();
        nb::object iterator = frame[nb::str(".0")];
        for (;;) {
            PyObject* item = PyIter_Next(iterator.ptr());
            if (!item) {
                if (PyErr_Occurred()) throw nb::python_error();
                state.gc_clear();
                throw nb::stop_iteration();
            }
            nb::object value = nb::steal<nb::object>(item);
            frame[nb::str("x")] = value;
            if (!value.is_none())
                return accel_compare(std::move(value), nb::int_(0), Py_LT);
        }
    } catch (const nb::python_error& error) {
        AccelHandledException handled(error.value());
        state.gc_clear();
        if (!error.matches(PyExc_StopIteration)) throw;
        accel_generator_raise(nb::borrow<nb::object>(error.value()), true);
    } catch (...) {
        state.gc_clear();
        throw;
    }
}

void accel_negative_close(nb::handle self) {
    auto& state = *nb::inst_ptr<AccelNegativeKeys>(self);
    AccelGeneratorRunning guard(state);
    state.gc_clear();
}

nb::object accel_generator_exception(nb::args args) {
    if (args.size() < 1 || args.size() > 3) {
        PyErr_Format(PyExc_TypeError, "throw expected %s %d argument%s, got %zd",
            args.size() < 1 ? "at least" : "at most", args.size() < 1 ? 1 : 3,
            args.size() < 1 ? "" : "s", args.size());
        throw nb::python_error();
    }
    nb::object supplied = args[0];
    nb::object value = args.size() > 1 ? nb::borrow<nb::object>(args[1]) : nb::none();
    nb::object traceback = args.size() > 2 ? nb::borrow<nb::object>(args[2]) : nb::none();
    if (!traceback.is_none() && !PyTraceBack_Check(traceback.ptr()))
        throw nb::type_error("throw() third argument must be a traceback object");
    nb::object exception;
    if (PyExceptionInstance_Check(supplied.ptr())) {
        if (!value.is_none()) throw nb::type_error("instance exception may not have a separate value");
        exception = std::move(supplied);
    } else if (PyExceptionClass_Check(supplied.ptr())) {
        PyErr_SetObject(supplied.ptr(), value.ptr());
        PyObject *type = nullptr, *normalized = nullptr, *trace = nullptr;
        PyErr_Fetch(&type, &normalized, &trace);
        PyErr_NormalizeException(&type, &normalized, &trace);
        Py_XDECREF(type);
        Py_XDECREF(trace);
        exception = nb::steal<nb::object>(normalized);
        if (PyErr_Occurred()) throw nb::python_error();
        if (!exception.is_valid()) throw nb::python_error();
    } else {
        PyErr_Format(PyExc_TypeError,
            "exceptions must be classes or instances deriving from BaseException, not %s",
            Py_TYPE(supplied.ptr())->tp_name);
        throw nb::python_error();
    }
    if (!traceback.is_none() && PyException_SetTraceback(exception.ptr(), traceback.ptr()) < 0)
        throw nb::python_error();
    return exception;
}

void accel_negative_throw(nb::handle self, nb::args args) {
    nb::object exception = accel_generator_exception(args);
    auto& state = *nb::inst_ptr<AccelNegativeKeys>(self);
    AccelGeneratorRunning guard(state);
    const bool was_open = !state.closed;
    AccelHandledException handled(exception);
    state.gc_clear();
    accel_generator_raise(std::move(exception), was_open);
}

bool accel_direct_has_negative(nb::object colormap, nb::dict globals) {
    AccelNamespace namespace_(std::move(globals));
    nb::object callable = namespace_.get("any");
    nb::object iterator = accel_iter(accel_attr(std::move(colormap), "color_dict"));
    AccelNegativeKeys state;
    state.frame()[nb::str(".0")] = iterator;
    iterator.reset();
    nb::object generator = nb::cast(std::move(state), nb::rv_policy::move);
    return accel_truth(accel_call1(std::move(callable), std::move(generator)));
}

nb::object accel_direct_half_shape(nb::object mapper) {
    nb::object shape = accel_attr(std::move(mapper), "shape");
    nb::object first = accel_item(std::move(shape), nb::int_(0));
    nb::object half = accel_binary(PyNumber_FloorDivide, std::move(first), nb::int_(2));
    return accel_binary(PyNumber_Subtract, std::move(half), nb::int_(1));
}

nb::object accel_direct_clip_negative(nb::object data, nb::object half_shape,
                                      nb::dict globals) {
    AccelNamespace namespace_(std::move(globals));
    nb::object callable = namespace_.get("clip");
    nb::object negative = accel_checked(PyNumber_Negative(half_shape.ptr()));
    return accel_call3(std::move(callable), std::move(data),
                       std::move(negative), std::move(half_shape));
}

nb::object accel_direct_clip_positive(nb::object data, nb::object mapper,
                                      nb::dict globals) {
    AccelNamespace namespace_(std::move(globals));
    nb::object callable = namespace_.get("clip");
    nb::object shape = accel_attr(std::move(mapper), "shape");
    nb::object first = accel_item(std::move(shape), nb::int_(0));
    nb::object maximum = accel_binary(PyNumber_Subtract, std::move(first), nb::int_(1));
    return accel_call3(std::move(callable), std::move(data), nb::int_(0), std::move(maximum));
}

nb::object accel_direct_index(nb::object mapper, nb::object data) {
    return accel_item(std::move(mapper), std::move(data));
}

nb::object accel_direct_typed_mapping(nb::object data, nb::object colormap) {
    nb::object callable = accel_attr(std::move(colormap), "_get_typed_dict_mapping");
    nb::object dtype = accel_attr(std::move(data), "dtype");
    return accel_call1(std::move(callable), std::move(dtype));
}

nb::object accel_direct_target_dtype(nb::object colormap, nb::dict globals) {
    AccelNamespace namespace_(std::move(globals));
    nb::object callable = namespace_.get("minimum_dtype_for_labels");
    nb::object count = accel_attr(std::move(colormap), "_num_unique_colors");
    count = accel_binary(PyNumber_Add, std::move(count), nb::int_(2));
    return accel_call1(std::move(callable), std::move(count));
}

nb::object accel_direct_result_array(nb::object data, nb::object target_dtype,
                                     nb::dict globals) {
    AccelNamespace namespace_(std::move(globals));
    nb::object callable = namespace_.numpy("full_like");
    nb::object unknown = namespace_.get("MAPPING_OF_UNKNOWN_VALUE");
    return accel_call_kw(std::move(callable),
        nb::make_tuple(std::move(data), std::move(unknown)), "dtype", std::move(target_dtype));
}

nb::object accel_direct_loop_apply(nb::object data, nb::object mapping,
                                   nb::object result_array, nb::dict globals) {
    AccelNamespace namespace_(std::move(globals));
    nb::object callable = namespace_.get("_labels_raw_to_texture_direct_inner_loop");
    return accel_call3(std::move(callable), std::move(data),
                       std::move(mapping), std::move(result_array));
}

nb::object accel_partseg_modulo_out(nb::object values, nb::object dtype,
                                   nb::dict globals) {
    AccelNamespace namespace_(std::move(globals));
    nb::object callable = namespace_.numpy("empty");
    nb::object size = accel_attr(std::move(values), "size");
    return accel_call_kw(std::move(callable), nb::make_tuple(std::move(size)),
                         "dtype", std::move(dtype));
}

nb::object accel_partseg_modulo_apply(nb::object values, nb::object divisor,
                                     nb::object to_zero, nb::object out,
                                     nb::dict globals) {
    AccelNamespace namespace_(std::move(globals));
    nb::object callable = accel_attr(namespace_.get("partsegcore_mapping"),
                                     "zero_preserving_modulo_parallel");
    nb::object reshape = accel_attr(values, "reshape");
    nb::object flat = accel_call1(std::move(reshape), nb::int_(-1));
    // The source discards this kernel return before loading out.reshape or the
    // provider's shape. A local whose lifetime extends to this return is wrong.
    accel_call4(std::move(callable), std::move(flat), std::move(divisor),
                 std::move(to_zero), out);
    reshape = accel_attr(std::move(out), "reshape");
    nb::object shape = accel_attr(std::move(values), "shape");
    return accel_call1(std::move(reshape), std::move(shape));
}

nb::object accel_partseg_selection_dict(nb::object colormap) {
    nb::dict result;
    result[nb::none()] = nb::int_(0);
    nb::object selection = accel_attr(std::move(colormap), "selection");
    if (PyDict_SetItem(result.ptr(), selection.ptr(), nb::int_(1).ptr()) < 0)
        throw nb::python_error();
    return result;
}

struct AccelPair {
    nb::object first;
    nb::object second;
};

AccelPair accel_unpack_pair(nb::object item) {
    nb::object iterator = accel_iter(std::move(item));
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

nb::object accel_partseg_mapping_dict(nb::object colormap, nb::object info) {
    nb::object mapping = accel_attr(std::move(colormap), "_label_mapping_and_color_dict");
    mapping = accel_item(std::move(mapping), nb::int_(0));
    nb::object iterator = accel_iter(accel_call0(accel_attr(std::move(mapping), "items")));
    nb::dict result;
    // Comprehension locals survive the request for the next item. Rebinding
    // them after unpacking releases the previous iteration's values in order.
    nb::object key;
    nb::object value;
    for (;;) {
        PyObject* item = PyIter_Next(iterator.ptr());
        if (!item) {
            if (PyErr_Occurred()) throw nb::python_error();
            break;
        }
        AccelPair pair = accel_unpack_pair(nb::steal<nb::object>(item));
        key = std::move(pair.first);
        value = std::move(pair.second);
        bool include = key.is_none();
        if (!include) {
            nb::object minimum = accel_attr(info, "min");
            include = accel_test(std::move(minimum), key, Py_LE);
            if (include) {
                nb::object maximum = accel_attr(info, "max");
                include = accel_test(key, std::move(maximum), Py_LE);
            }
        }
        if (include && PyDict_SetItem(result.ptr(), key.ptr(), value.ptr()) < 0)
            throw nb::python_error();
    }
    return result;
}

nb::object accel_partseg_direct_out(nb::object data, nb::object target_dtype,
                                   nb::dict globals) {
    return accel_partseg_modulo_out(std::move(data), std::move(target_dtype), std::move(globals));
}

nb::object accel_partseg_direct_apply(nb::object data, nb::object mapping,
                                     nb::object out, nb::dict globals) {
    AccelNamespace namespace_(std::move(globals));
    nb::object callable = accel_attr(namespace_.get("partsegcore_mapping"), "map_array_parallel");
    nb::object reshape = accel_attr(data, "reshape");
    nb::object flat = accel_call1(std::move(reshape), nb::int_(-1));
    nb::object unknown = namespace_.get("MAPPING_OF_UNKNOWN_VALUE");
    accel_call4(std::move(callable), std::move(flat), std::move(mapping), std::move(unknown), out);
    reshape = accel_attr(std::move(out), "reshape");
    nb::object shape = accel_attr(std::move(data), "shape");
    return accel_call1(std::move(reshape), std::move(shape));
}

bool accel_backend_member(nb::object backend, const AccelNamespace& namespace_,
                           const char* alternate) {
    nb::object fastest = accel_attr(namespace_.get("ColormapBackend"), "fastest_available");
    nb::object other = accel_attr(namespace_.get("ColormapBackend"), alternate);
    nb::object choices = accel_checked(PySet_New(nullptr));
    if (PySet_Add(choices.ptr(), fastest.ptr()) < 0) throw nb::python_error();
    if (PySet_Add(choices.ptr(), other.ptr()) < 0) throw nb::python_error();
    fastest.reset();
    other.reset();
    const int result = PySet_Contains(choices.ptr(), backend.ptr());
    if (result < 0) throw nb::python_error();
    return result != 0;
}

void accel_set_backend(nb::object backend, nb::dict globals) {
    AccelNamespace namespace_(std::move(globals));
    namespace_.set("COLORMAP_BACKEND", backend);
    if (accel_not_none(namespace_.get("partsegcore_mapping"))
        && accel_backend_member(backend, namespace_, "partsegcore")) {
        namespace_.set("labels_raw_to_texture_direct", namespace_.get("labels_raw_to_texture_direct_partsegcore"));
        namespace_.set("zero_preserving_modulo", namespace_.get("zero_preserving_modulo_partsegcore"));
        namespace_.set("prange", namespace_.get("range"));
    } else if (accel_not_none(namespace_.get("numba"))
        && accel_backend_member(backend, namespace_, "numba")) {
        namespace_.set("zero_preserving_modulo", namespace_.get("_zero_preserving_modulo_loop"));
        namespace_.set("labels_raw_to_texture_direct", namespace_.get("_labels_raw_to_texture_direct_loop"));
        namespace_.set("prange", accel_attr(namespace_.get("numba"), "prange"));
    } else {
        namespace_.set("zero_preserving_modulo", namespace_.get("zero_preserving_modulo_numpy"));
        namespace_.set("labels_raw_to_texture_direct", namespace_.get("_labels_raw_to_texture_direct_numpy"));
        namespace_.set("prange", namespace_.get("range"));
    }
}

void accel_initial_prange(nb::dict globals) {
    AccelNamespace namespace_(std::move(globals));
    namespace_.set("prange", namespace_.get("range"));
}

void accel_initial_backend(nb::dict globals) {
    AccelNamespace namespace_(std::move(globals));
    namespace_.set("COLORMAP_BACKEND", accel_attr(namespace_.get("ColormapBackend"), "fastest_available"));
}

void accel_initial_aliases(nb::dict globals) {
    AccelNamespace namespace_(std::move(globals));
    namespace_.set("zero_preserving_modulo", namespace_.get("zero_preserving_modulo_numpy"));
    namespace_.set("labels_raw_to_texture_direct", namespace_.get("_labels_raw_to_texture_direct_numpy"));
}

void accel_initial_select_backend(nb::dict globals) {
    AccelNamespace namespace_(std::move(globals));
    nb::object callable = namespace_.get("set_colormap_backend");
    nb::object backend = namespace_.get("COLORMAP_BACKEND");
    accel_call1(std::move(callable), std::move(backend));
}

} // namespace

void bind_colormap_acceleration(nb::module_& module) {
    nb::class_<AccelNegativeKeys>(module, "ColormapNegativeKeysIterator",
        nb::is_weak_referenceable(),
        nb::type_slots(collections::iterator_frame_slots<AccelNegativeKeys>()))
        .def("__iter__", [](nb::handle self) { return nb::borrow<nb::object>(self); })
        .def("__next__", &accel_negative_next)
        .def("send", [](nb::handle self, nb::object value) {
            auto& state = *nb::inst_ptr<AccelNegativeKeys>(self);
            if (state.closed) throw nb::stop_iteration();
            if (!state.started && !value.is_none())
                throw nb::type_error("can't send non-None value to a just-started generator");
            return accel_negative_next(self);
        }, nb::arg("value").none())
        .def("close", &accel_negative_close)
        .def("throw", &accel_negative_throw);

    module.def("colormap_minimum_dtype", &accel_minimum_dtype,
        nb::arg("num_colors").none(), nb::arg("namespace"));
    module.def("colormap_modulo_numpy_result", &accel_modulo_numpy_result,
        nb::arg("values").none(), nb::arg("n").none(), nb::arg("dtype").none(), nb::arg("namespace"));
    module.def("colormap_modulo_zero_result", &accel_modulo_zero_result,
        nb::arg("values").none(), nb::arg("res").none(), nb::arg("to_zero").none());
    module.def("colormap_modulo_loop_result", &accel_modulo_loop_result,
        nb::arg("values").none(), nb::arg("dtype").none(), nb::arg("namespace"));
    module.def("colormap_modulo_loop_apply", &accel_modulo_loop_apply,
        nb::arg("values").none(), nb::arg("n").none(), nb::arg("to_zero").none(), nb::arg("result").none(), nb::arg("namespace"));
    module.def("colormap_numpy_clip_version", &accel_numpy_clip_version, nb::arg("namespace"));
    module.def("colormap_clip_info", &accel_clip_info, nb::arg("data").none(), nb::arg("namespace"));
    module.def("colormap_clip_min", &accel_clip_min, nb::arg("min_val").none(), nb::arg("dtype_info").none(), nb::arg("namespace"));
    module.def("colormap_clip_max", &accel_clip_max, nb::arg("max_val").none(), nb::arg("dtype_info").none(), nb::arg("namespace"));
    module.def("colormap_clip", &accel_clip, nb::arg("data").none(), nb::arg("min_val").none(), nb::arg("max_val").none(), nb::arg("namespace"));
    module.def("colormap_direct_selection", &accel_direct_selection, nb::arg("direct_colormap").none());
    module.def("colormap_direct_selected", &accel_direct_selected, nb::arg("data").none(), nb::arg("direct_colormap").none(), nb::arg("namespace"));
    module.def("colormap_direct_mapper", &accel_direct_mapper, nb::arg("direct_colormap").none());
    module.def("colormap_direct_has_negative", &accel_direct_has_negative, nb::arg("direct_colormap").none(), nb::arg("namespace"));
    module.def("colormap_direct_half_shape", &accel_direct_half_shape, nb::arg("mapper").none());
    module.def("colormap_direct_clip_negative", &accel_direct_clip_negative, nb::arg("data").none(), nb::arg("half_shape").none(), nb::arg("namespace"));
    module.def("colormap_direct_clip_positive", &accel_direct_clip_positive, nb::arg("data").none(), nb::arg("mapper").none(), nb::arg("namespace"));
    module.def("colormap_direct_index", &accel_direct_index, nb::arg("mapper").none(), nb::arg("data").none());
    module.def("colormap_direct_typed_mapping", &accel_direct_typed_mapping, nb::arg("data").none(), nb::arg("direct_colormap").none());
    module.def("colormap_direct_target_dtype", &accel_direct_target_dtype, nb::arg("direct_colormap").none(), nb::arg("namespace"));
    module.def("colormap_direct_result_array", &accel_direct_result_array, nb::arg("data").none(), nb::arg("target_dtype").none(), nb::arg("namespace"));
    module.def("colormap_direct_loop_apply", &accel_direct_loop_apply, nb::arg("data").none(), nb::arg("dkt").none(), nb::arg("result_array").none(), nb::arg("namespace"));
    module.def("colormap_partseg_modulo_out", &accel_partseg_modulo_out, nb::arg("values").none(), nb::arg("dtype").none(), nb::arg("namespace"));
    module.def("colormap_partseg_modulo_apply", &accel_partseg_modulo_apply, nb::arg("values").none(), nb::arg("n").none(), nb::arg("to_zero").none(), nb::arg("out").none(), nb::arg("namespace"));
    module.def("colormap_partseg_selection_dict", &accel_partseg_selection_dict, nb::arg("direct_colormap").none());
    module.def("colormap_partseg_mapping_dict", &accel_partseg_mapping_dict, nb::arg("direct_colormap").none(), nb::arg("iinfo").none());
    module.def("colormap_partseg_direct_out", &accel_partseg_direct_out, nb::arg("data").none(), nb::arg("target_dtype").none(), nb::arg("namespace"));
    module.def("colormap_partseg_direct_apply", &accel_partseg_direct_apply, nb::arg("data").none(), nb::arg("dkt").none(), nb::arg("out").none(), nb::arg("namespace"));
    module.def("colormap_set_backend", &accel_set_backend, nb::arg("backend").none(), nb::arg("namespace"));
    module.def("colormap_initial_prange", &accel_initial_prange, nb::arg("namespace"));
    module.def("colormap_initial_backend", &accel_initial_backend, nb::arg("namespace"));
    module.def("colormap_initial_aliases", &accel_initial_aliases, nb::arg("namespace"));
    module.def("colormap_initial_select_backend", &accel_initial_select_backend, nb::arg("namespace"));
}

} // namespace nx
