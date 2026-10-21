#include "arrays.hpp"
#include "reductions.hpp"
#include <type_traits>
#include <utility>

namespace nx {

template <typename T>
nb::tuple calc_data_range(Input<T> data) {
    if constexpr (std::is_same_v<T, uint8_t>)
        return nb::make_tuple(0, 255);
    const size_t count = data.size();
    if (!count)
        throw std::invalid_argument("zero-size array has no minimum or maximum");
    T minimum{}, maximum{};
    bool found = false;
    bool contiguous = true;
    int64_t expected = 1;
    for (size_t c = data.ndim(); c-- > 0;) {
        if (data.shape(c) > 1 && data.stride(c) != expected) contiguous = false;
        expected *= static_cast<int64_t>(data.shape(c));
    }
    {
        nb::gil_scoped_release release;
        auto consume = [&](T value) {
            if constexpr (std::is_floating_point_v<T>)
                if (!std::isfinite(value)) return;
            if (!found) { minimum = maximum = value; found = true; }
            else { minimum = std::min(minimum, value); maximum = std::max(maximum, value); }
        };
        if (contiguous && contiguous_finite_range(data.data(), count, minimum, maximum)) {
            found = true;
        } else if (contiguous) {
            for (size_t i = 0; i < count; ++i)
                consume(read(data.data(), static_cast<int64_t>(i)));
        } else {
            std::vector<size_t> indices(data.ndim(), 0);
            int64_t offset = 0;
            for (size_t i = 0; i < count; ++i) {
                consume(read(data.data(), offset));
                for (size_t c = data.ndim(); c-- > 0;) {
                    offset += data.stride(c);
                    if (++indices[c] < data.shape(c)) break;
                    offset -= static_cast<int64_t>(data.shape(c)) * data.stride(c);
                    indices[c] = 0;
                }
            }
        }
    }
    if (!found) return nb::make_tuple(0.0, 1.0);
    // Test equality in the original dtype before converting large int64/uint64
    // extrema to Python floats, as napari does.
    if (minimum == maximum) {
        minimum = std::min(minimum, T(0));
        maximum = std::max(maximum, T(1));
    }
    return nb::make_tuple(static_cast<double>(minimum), static_cast<double>(maximum));
}

template <typename T>
void bind_type(nb::module_& m) {
    m.def("calc_data_range", &calc_data_range<T>, nb::arg("data").noconvert());
}

namespace {

nb::object range_checked(PyObject* value) {
    if (!value) throw nb::python_error();
    return nb::steal<nb::object>(value);
}

bool range_truth(nb::handle value) {
    int result = PyObject_IsTrue(value.ptr());
    if (result < 0) throw nb::python_error();
    return result != 0;
}

nb::object range_lookup(nb::handle mapping, nb::handle key) {
    if (PyDict_CheckExact(mapping.ptr())) {
        PyObject* value = PyDict_GetItemWithError(mapping.ptr(), key.ptr());
        if (value) return nb::borrow<nb::object>(value);
        if (PyErr_Occurred()) throw nb::python_error();
        return {};
    }
    PyObject* value = PyObject_GetItem(mapping.ptr(), key.ptr());
    if (value) return nb::steal<nb::object>(value);
    if (!PyErr_ExceptionMatches(PyExc_KeyError)) throw nb::python_error();
    PyErr_Clear();
    return {};
}

struct RangeNamespace {
    nb::dict globals;
    nb::object builtins;

    explicit RangeNamespace(nb::dict namespace_)
        : globals(std::move(namespace_)),
          builtins(nb::borrow<nb::object>(PyEval_GetBuiltins())) {}

    RangeNamespace(nb::dict namespace_, nb::object builtins_)
        : globals(std::move(namespace_)), builtins(std::move(builtins_)) {}

    nb::object get(const char* name) const {
        nb::object key = nb::str(name);
        nb::object value = range_lookup(globals, key);
        if (value.ptr()) return value;
        value = range_lookup(builtins, key);
        if (value.ptr()) return value;
        nb::object message = range_checked(PyUnicode_FromFormat("name '%s' is not defined", name));
        nb::object error = range_checked(PyObject_CallOneArg(PyExc_NameError, message.ptr()));
        if (PyObject_SetAttrString(error.ptr(), "name", key.ptr()) < 0) throw nb::python_error();
        PyErr_SetObject(PyExc_NameError, error.ptr());
        throw nb::python_error();
    }

    nb::object numpy(const char* name) const {
        nb::object module = get("np");
        return range_checked(PyObject_GetAttrString(module.ptr(), name));
    }
};

// These helpers consume expression temporaries before returning the result.
// Source locals are separately owned by the controller that uses them.
nb::object range_attr(nb::object value, const char* name) {
    return range_checked(PyObject_GetAttrString(value.ptr(), name));
}

nb::object range_item(nb::object value, nb::object key) {
    return range_checked(PyObject_GetItem(value.ptr(), key.ptr()));
}

nb::object range_call1(nb::object callable, nb::object value) {
    return range_checked(PyObject_CallOneArg(callable.ptr(), value.ptr()));
}

nb::object range_call2(nb::object callable, nb::object left, nb::object right) {
    return range_checked(PyObject_CallFunctionObjArgs(callable.ptr(), left.ptr(), right.ptr(), nullptr));
}

nb::object range_call3(nb::object callable, nb::object first, nb::object second, nb::object third) {
    return range_checked(PyObject_CallFunctionObjArgs(callable.ptr(), first.ptr(), second.ptr(), third.ptr(), nullptr));
}

nb::object range_call_tuple(nb::object callable, nb::object arguments) {
    return range_checked(PyObject_Call(callable.ptr(), arguments.ptr(), nullptr));
}

using RangeBinary = PyObject* (*)(PyObject*, PyObject*);

nb::object range_binary(RangeBinary operation, nb::object left, nb::object right) {
    return range_checked(operation(left.ptr(), right.ptr()));
}

nb::object range_negative(nb::object value) {
    return range_checked(PyNumber_Negative(value.ptr()));
}

nb::object range_compare(nb::object left, nb::object right, int operation) {
    return range_checked(PyObject_RichCompare(left.ptr(), right.ptr(), operation));
}

bool range_test(nb::object left, nb::object right, int operation) {
    return range_truth(range_compare(std::move(left), std::move(right), operation));
}

bool range_contains(nb::handle value, const char* needle) {
    nb::object text = nb::str(needle);
    int result = PySequence_Contains(value.ptr(), text.ptr());
    if (result < 0) throw nb::python_error();
    return result != 0;
}

nb::object range_slice(nb::object start, nb::object stop) {
    return range_checked(PySlice_New(start.ptr(), stop.ptr(), nullptr));
}

nb::object range_next(nb::handle iterator) {
    PyObject* value = PyIter_Next(iterator.ptr());
    if (!value && PyErr_Occurred()) throw nb::python_error();
    return nb::steal<nb::object>(value);
}

nb::object range_iter(nb::object value) {
    return range_checked(PyObject_GetIter(value.ptr()));
}

[[noreturn]] void range_formatted_error(const RangeNamespace& namespace_, const char* exception,
                                       const char* prefix, nb::handle value) {
    nb::object factory = namespace_.get(exception);
    nb::object formatted = range_checked(PyObject_Format(value.ptr(), nb::str("").ptr()));
    nb::object message = range_checked(PyUnicode_Concat(nb::str(prefix).ptr(), formatted.ptr()));
    nb::object error = range_call1(std::move(factory), std::move(message));
    if (PyExceptionInstance_Check(error.ptr())) {
        PyErr_SetObject(reinterpret_cast<PyObject*>(Py_TYPE(error.ptr())), error.ptr());
    } else if (PyExceptionClass_Check(error.ptr())) {
        PyErr_SetNone(error.ptr());
    } else {
        PyErr_SetString(PyExc_TypeError, "exceptions must derive from BaseException");
    }
    throw nb::python_error();
}

nb::object layer_nanreduce(nb::object array, const RangeNamespace& namespace_, bool maximum) {
    const char* reduction = maximum ? "max" : "min";
    nb::object value = range_call1(namespace_.numpy(reduction), array);
    if (!range_truth(range_call1(namespace_.numpy("isfinite"), value))) {
        nb::object mask = range_call1(namespace_.numpy("isfinite"), array);
        nb::object masked = range_item(array, std::move(mask));
        if (range_test(range_attr(masked, "size"), nb::int_(0), Py_EQ))
            return nb::int_(maximum ? 1 : 0);
        value = range_call1(namespace_.numpy(reduction), masked);
    }
    return value;
}

nb::object layer_nanmin(nb::object array, nb::dict namespace_) {
    return layer_nanreduce(std::move(array), RangeNamespace(std::move(namespace_)), false);
}

nb::object layer_nanmax(nb::object array, nb::dict namespace_) {
    return layer_nanreduce(std::move(array), RangeNamespace(std::move(namespace_)), true);
}

nb::tuple range_finish(nb::object minimum, nb::object maximum, const RangeNamespace& namespace_) {
    if (range_test(minimum, maximum, Py_EQ)) {
        minimum = range_call2(namespace_.get("min"), minimum, nb::int_(0));
        maximum = range_call2(namespace_.get("max"), maximum, nb::int_(1));
    }
    nb::object first = range_call1(namespace_.get("float"), minimum);
    nb::object second = range_call1(namespace_.get("float"), maximum);
    return nb::make_tuple(first, second);
}

nb::list range_sample(nb::object data, nb::list indices, nb::object central_slice,
                      const RangeNamespace& namespace_) {
    nb::list rows;
    for (const char* reduction : {"_nanmax", "_nanmin"}) {
        nb::list row;
        nb::object iterator = range_iter(indices);
        nb::object index;
        while (nb::object next = range_next(iterator)) {
            index = std::move(next);
            nb::object helper = namespace_.get(reduction);
            nb::object key = index;
            if (central_slice.ptr())
                key = range_binary(PyNumber_Add, index, central_slice);
            nb::object selected = range_item(data, std::move(key));
            row.append(range_call1(std::move(helper), std::move(selected)));
        }
        rows.append(row);
    }
    return rows;
}

// Native loops implement the source comprehensions. A fresh tuple factory is
// resolved before the source iterable, just as in the executing Python body.
nb::object range_shape_tuple(nb::object data, nb::object offset,
                              const RangeNamespace& namespace_, bool middle) {
    nb::object factory = namespace_.get("tuple");
    nb::object shape = range_attr(data, "shape");
    nb::object stop = range_negative(offset);
    nb::object selected = range_item(std::move(shape), range_slice(nb::none(), std::move(stop)));
    nb::object iterator = range_iter(std::move(selected));
    nb::list values;
    nb::object size;
    while (nb::object next = range_next(iterator)) {
        size = std::move(next);
        values.append(range_binary(middle ? PyNumber_FloorDivide : PyNumber_Subtract,
                                   size, nb::int_(middle ? 2 : 1)));
    }
    return range_call1(std::move(factory), std::move(values));
}

nb::list range_shape_centers(nb::object data, nb::object offset, const RangeNamespace& namespace_) {
    nb::object dimensions = range_attr(data, "shape");
    nb::object coordinates = range_item(std::move(dimensions), range_slice(range_negative(offset), nb::none()));
    nb::object iterator = range_iter(std::move(coordinates));
    nb::list centers;
    nb::object size;
    while (nb::object next = range_next(iterator)) {
        size = std::move(next);
        nb::object integer = namespace_.get("int");
        nb::object half = range_binary(PyNumber_FloorDivide, size, nb::int_(2));
        centers.append(range_call1(std::move(integer), std::move(half)));
    }
    return centers;
}

nb::object range_center_windows(nb::object center, const RangeNamespace& namespace_) {
    nb::object tuple_factory = namespace_.get("tuple");
    nb::object first_two = range_item(center, range_slice(nb::none(), nb::int_(2)));
    nb::object iterator = range_iter(std::move(first_two));
    nb::list windows;
    nb::object coordinate;
    while (nb::object next = range_next(iterator)) {
        coordinate = std::move(next);
        nb::object factory = namespace_.get("slice");
        nb::object start = range_binary(PyNumber_Subtract, coordinate, nb::int_(31));
        nb::object stop = range_binary(PyNumber_Add, coordinate, nb::int_(31));
        windows.append(range_call2(std::move(factory), std::move(start), std::move(stop)));
    }
    return range_call1(std::move(tuple_factory), std::move(windows));
}

nb::tuple layer_calc_data_range_impl(nb::object data, const RangeNamespace& namespace_,
                                     nb::object rgb, nb::object dtype) {
    if (!dtype.is_none() && range_test(dtype, namespace_.numpy("uint8"), Py_EQ))
        return nb::make_tuple(0, 255);
    nb::object data_dtype = range_attr(data, "dtype");
    bool uint8 = range_test(std::move(data_dtype), namespace_.numpy("uint8"), Py_EQ);
    if (uint8) return nb::make_tuple(0, 255);

    nb::object instance_test = namespace_.get("isinstance");
    nb::object ndarray = namespace_.numpy("ndarray");
    bool small_array = range_truth(range_call2(std::move(instance_test), data, std::move(ndarray)));
    if (small_array && range_test(range_attr(data, "ndim"), nb::int_(3), Py_LT)) {
        nb::object minimum = range_call1(namespace_.get("_nanmin"), data);
        nb::object maximum = range_call1(namespace_.get("_nanmax"), data);
        return range_finish(std::move(minimum), std::move(maximum), namespace_);
    }

    nb::object center, offset, bottom, middle, top, central_slice;
    nb::list slices, indices;
    nb::object reduced;
    bool line = range_test(range_attr(data, "size"), nb::float_(1e7), Py_GT);
    if (line) {
        line = range_test(range_attr(data, "ndim"), nb::int_(1), Py_EQ);
        if (!line && range_truth(rgb))
            line = range_test(range_attr(data, "ndim"), nb::int_(2), Py_EQ);
    }
    if (line) {
        nb::object integer = namespace_.get("int");
        nb::object size = range_item(range_attr(data, "shape"), nb::int_(0));
        nb::object half = range_binary(PyNumber_FloorDivide, std::move(size), nb::int_(2));
        center = range_call1(std::move(integer), std::move(half));
        slices.append(range_call2(namespace_.get("slice"), nb::int_(0), nb::int_(4096)));
        nb::object slice_factory = namespace_.get("slice");
        nb::object start = range_binary(PyNumber_Subtract, center, nb::int_(2048));
        nb::object stop = range_binary(PyNumber_Add, center, nb::int_(2048));
        slices.append(range_call2(std::move(slice_factory), std::move(start), std::move(stop)));
        slices.append(range_call2(namespace_.get("slice"), nb::int_(-4096), nb::none()));
        reduced = range_sample(data, slices, {}, namespace_);
    } else if (range_test(range_attr(data, "size"), nb::float_(1e7), Py_GT)) {
        nb::object converted = range_call1(namespace_.get("int"), rgb);
        offset = range_binary(PyNumber_Add, nb::int_(2), std::move(converted));
        nb::object ndim = range_attr(data, "ndim");
        nb::object count = range_binary(PyNumber_Subtract, std::move(ndim), offset);
        bottom = range_binary(PyNumber_Multiply, nb::make_tuple(0), std::move(count));
        middle = range_shape_tuple(data, offset, namespace_, true);
        top = range_shape_tuple(data, offset, namespace_, false);
        indices.append(bottom);
        indices.append(middle);
        indices.append(top);

        nb::object product = namespace_.numpy("prod");
        nb::object shape = range_attr(data, "shape");
        nb::object tail = range_negative(offset);
        nb::object plane = range_item(std::move(shape), range_slice(std::move(tail), nb::none()));
        bool patch = range_test(range_call1(std::move(product), std::move(plane)), nb::float_(1e7), Py_GT);
        if (patch) {
            nb::object dimensions = range_attr(data, "shape");
            patch = range_test(range_item(std::move(dimensions), range_negative(offset)), nb::int_(64), Py_GT);
        }
        if (patch) {
            nb::object dimensions = range_attr(data, "shape");
            nb::object coordinate = range_binary(PyNumber_Add, range_negative(offset), nb::int_(1));
            patch = range_test(range_item(std::move(dimensions), std::move(coordinate)), nb::int_(64), Py_GT);
        }
        if (patch) {
            center = range_shape_centers(data, offset, namespace_);
            central_slice = range_center_windows(center, namespace_);
        }
        reduced = range_sample(data, indices, central_slice, namespace_);
        nb::object dask = namespace_.get("dask");
        nb::object compute = range_attr(std::move(dask), "compute");
        nb::object arguments = range_checked(PyList_AsTuple(reduced.ptr()));
        reduced = range_call_tuple(std::move(compute), std::move(arguments));
    } else {
        reduced = data;
    }
    nb::object minimum = range_call1(namespace_.get("_nanmin"), reduced);
    nb::object maximum = range_call1(namespace_.get("_nanmax"), reduced);
    return range_finish(std::move(minimum), std::move(maximum), namespace_);
}

// Required context selects the complete application operation without changing
// the existing one-array numerical overloads or the source namespace authority.
nb::tuple public_calc_data_range(nb::object data, nb::dict namespace_,
                                 nb::object builtins_, nb::object rgb, nb::object dtype) {
    return layer_calc_data_range_impl(std::move(data),
        RangeNamespace(std::move(namespace_), std::move(builtins_)),
        std::move(rgb), std::move(dtype));
}

nb::tuple layer_calc_data_range(nb::object data, nb::dict namespace_, nb::object rgb, nb::object dtype) {
    return layer_calc_data_range_impl(std::move(data), RangeNamespace(std::move(namespace_)),
                                     std::move(rgb), std::move(dtype));
}

nb::tuple range_adapter_call(nb::object original, nb::object data, nb::object rgb, nb::object dtype) {
    nb::object inspect = nb::module_::import_("inspect");
    nb::object function = range_call1(range_attr(std::move(inspect), "unwrap"), original);
    nb::object namespace_object = range_attr(function, "__globals__");
    if (!PyDict_Check(namespace_object.ptr()))
        throw nb::type_error("range adapter function globals must be a dictionary");
    nb::object builtins = range_attr(function, "__builtins__");
    RangeNamespace namespace_(nb::borrow<nb::dict>(namespace_object), std::move(builtins));
    // Original globals need not import the extension. Resolve its canonical
    // public hook afresh while carrying the original function's exact context.
    nb::object native = nb::module_::import_("naparixx._native");
    nb::object calculate = range_attr(std::move(native), "calc_data_range");
    return nb::cast<nb::tuple>(range_checked(PyObject_CallFunctionObjArgs(
        calculate.ptr(), data.ptr(), namespace_.globals.ptr(), namespace_.builtins.ptr(),
        rgb.ptr(), dtype.ptr(), nullptr)));
}

nb::object dtype_normalize_str_by_bit_depth(nb::object dtype_str, nb::object kind, nb::dict namespace_) {
    RangeNamespace context(std::move(namespace_));
    bool digit = false;
    {
        nb::object iterator = range_iter(dtype_str);
        nb::object character;
        while (nb::object next = range_next(iterator)) {
            character = std::move(next);
            nb::object string_type = context.get("str");
            nb::object isdigit = range_attr(std::move(string_type), "isdigit");
            if (range_truth(range_call1(std::move(isdigit), character))) {
                digit = true;
                break;
            }
        }
    }
    if (!digit) return range_call1(context.numpy("dtype"), kind);
    nb::object bit_dict = range_item(context.get("_np_kinds"), kind);
    for (const auto& entry : {std::pair<const char*, int>{"128", 128}, {"8", 8},
                              {"16", 16}, {"32", 32}, {"64", 64}}) {
        if (range_contains(dtype_str, entry.first))
            return range_item(bit_dict, nb::int_(entry.second));
    }
    range_formatted_error(context, "ValueError", "Unrecognized bit depth in dtype: ", dtype_str);
}

nb::object dtype_normalize(nb::object dtype_spec, nb::dict namespace_) {
    RangeNamespace context(std::move(namespace_));
    nb::object isinstance = context.get("isinstance");
    nb::object dtype_type = context.numpy("dtype");
    if (range_truth(range_call2(std::move(isinstance), dtype_spec, std::move(dtype_type))))
        return dtype_spec;
    isinstance = context.get("isinstance");
    nb::object string_type = context.get("str");
    if (range_truth(range_call2(std::move(isinstance), dtype_spec, std::move(string_type))))
        return range_call1(context.numpy("dtype"), dtype_spec);
    nb::object dtype_str = range_call1(context.get("str"), dtype_spec);
    for (const char* kind : {"uint", "int", "float", "complex"}) {
        if (range_contains(dtype_str, kind))
            return range_call2(context.get("_normalize_str_by_bit_depth"), dtype_str, nb::str(kind));
    }
    if (range_contains(dtype_str, "bool")) {
        nb::object factory = context.numpy("dtype");
        nb::object boolean = context.numpy("bool_");
        return range_call1(std::move(factory), std::move(boolean));
    }
    range_formatted_error(context, "ValueError", "Unrecognized dtype: ", dtype_spec);
}

nb::tuple dtype_limits(nb::object dtype_spec, nb::dict namespace_) {
    RangeNamespace context(std::move(namespace_));
    nb::object dtype = range_call1(context.get("normalize_dtype"), dtype_spec);
    nb::object test = context.numpy("issubdtype");
    nb::object integer = context.numpy("integer");
    bool integral = range_truth(range_call2(std::move(test), dtype, std::move(integer)));
    nb::object info;
    if (integral) {
        info = range_call1(context.numpy("iinfo"), dtype);
    } else {
        bool floating = range_truth(dtype);
        if (floating) {
            test = context.numpy("issubdtype");
            nb::object floating_type = context.numpy("floating");
            floating = range_truth(range_call2(std::move(test), dtype, std::move(floating_type)));
        }
        if (!floating)
            range_formatted_error(context, "TypeError", "Unrecognized or non-numeric dtype: ", dtype_spec);
        info = range_call1(context.numpy("finfo"), dtype);
    }
    nb::object factory = context.get("float");
    nb::object first = range_call1(std::move(factory), range_attr(info, "min"));
    factory = context.get("float");
    nb::object second = range_call1(std::move(factory), range_attr(info, "max"));
    return nb::make_tuple(first, second);
}

nb::object coerced_contrast_data(nb::object coerced, nb::object data) {
    if (range_test(range_attr(coerced, "scale"), nb::int_(1), Py_LE)) {
        nb::object scaled = range_binary(PyNumber_Multiply, data, range_attr(coerced, "scale"));
        return range_binary(PyNumber_Add, std::move(scaled), range_attr(coerced, "offset"));
    }
    nb::object offset = range_attr(coerced, "offset");
    nb::object scale = range_attr(coerced, "scale");
    nb::object quotient = range_binary(PyNumber_TrueDivide, std::move(offset), std::move(scale));
    nb::object shifted = range_binary(PyNumber_Add, data, std::move(quotient));
    return range_binary(PyNumber_Multiply, std::move(shifted), range_attr(coerced, "scale"));
}

nb::object colormap_scale_down(nb::object limits, nb::dict namespace_) {
    RangeNamespace context(std::move(namespace_));
    nb::object minimum = context.get("min");
    nb::object numerator = range_binary(PyNumber_Multiply, context.get("_MAX_VISPY_SUPPORTED_VALUE"), nb::int_(2));
    nb::object upper = range_item(limits, nb::int_(1));
    nb::object lower = range_item(limits, nb::int_(0));
    nb::object span = range_binary(PyNumber_Subtract, std::move(upper), std::move(lower));
    nb::object quotient = range_binary(PyNumber_TrueDivide, std::move(numerator), std::move(span));
    nb::object scale = range_call2(std::move(minimum), nb::float_(1.0), std::move(quotient));
    nb::object first = range_binary(PyNumber_Multiply, range_item(limits, nb::int_(0)), scale);
    nb::object second = range_binary(PyNumber_Multiply, range_item(limits, nb::int_(1)), scale);
    nb::object ctrl_lim = nb::make_tuple(std::move(first), std::move(second));
    nb::object maximum = context.get("max");
    nb::object negative_limit = range_negative(context.get("_MAX_VISPY_SUPPORTED_VALUE"));
    nb::object left = range_binary(PyNumber_Subtract, std::move(negative_limit), range_item(ctrl_lim, nb::int_(0)));
    nb::object left_shift = range_call2(std::move(maximum), nb::float_(0.0), std::move(left));
    maximum = context.get("max");
    nb::object right_value = range_item(ctrl_lim, nb::int_(1));
    nb::object right_limit = context.get("_MAX_VISPY_SUPPORTED_VALUE");
    nb::object right = range_binary(PyNumber_Subtract, std::move(right_value), std::move(right_limit));
    nb::object right_shift = range_call2(std::move(maximum), nb::float_(0.0), std::move(right));
    nb::object offset = range_binary(PyNumber_Subtract, left_shift, right_shift);
    first = range_binary(PyNumber_Add, range_item(ctrl_lim, nb::int_(0)), offset);
    second = range_binary(PyNumber_Add, range_item(ctrl_lim, nb::int_(1)), offset);
    ctrl_lim = nb::make_tuple(std::move(first), std::move(second));
    return range_call3(context.get("CoercedContrastLimits"), ctrl_lim, offset, scale);
}

nb::object colormap_scale_up(nb::object limits, nb::dict namespace_) {
    RangeNamespace context(std::move(namespace_));
    nb::object upper = range_item(limits, nb::int_(1));
    nb::object lower = range_item(limits, nb::int_(0));
    nb::object span = range_binary(PyNumber_Subtract, std::move(upper), std::move(lower));
    nb::object scale = range_binary(PyNumber_TrueDivide, nb::int_(1000), std::move(span));
    nb::object negative = range_negative(range_item(limits, nb::int_(0)));
    nb::object shift = range_binary(PyNumber_Multiply, std::move(negative), scale);
    return range_call3(context.get("CoercedContrastLimits"), nb::make_tuple(0, 1000), shift, scale);
}

bool contrast_spacing_small(nb::object distance, nb::object limit, const RangeNamespace& context) {
    nb::object absolute = context.numpy("abs");
    nb::object spacing = context.numpy("spacing");
    nb::object step = range_call1(std::move(spacing), limit);
    nb::object positive = range_call1(std::move(absolute), std::move(step));
    nb::object shades = context.get("_MINIMUM_SHADES_COUNT");
    nb::object threshold = range_binary(PyNumber_Multiply, std::move(positive), std::move(shades));
    return range_test(distance, std::move(threshold), Py_LT);
}

nb::object colormap_coerce_contrast_limits(nb::object limits, nb::dict namespace_) {
    RangeNamespace context(std::move(namespace_));
    nb::object absolute = range_call1(context.numpy("abs"), limits);
    nb::object max_method = range_attr(std::move(absolute), "max");
    nb::object extreme = range_checked(PyObject_CallNoArgs(max_method.ptr()));
    max_method.reset();
    if (range_test(std::move(extreme), context.get("_MAX_VISPY_SUPPORTED_VALUE"), Py_GT))
        return range_call1(context.get("scale_down"), limits);
    nb::object factory = context.numpy("float32");
    nb::object c_min = range_call1(std::move(factory), range_item(limits, nb::int_(0)));
    factory = context.numpy("float32");
    nb::object c_max = range_call1(std::move(factory), range_item(limits, nb::int_(1)));
    nb::object distance = range_binary(PyNumber_Subtract, c_max, c_min);
    if (contrast_spacing_small(distance, c_min, context) || contrast_spacing_small(distance, c_max, context))
        return range_call1(context.get("scale_up"), limits);
    return range_call3(context.get("CoercedContrastLimits"), limits, nb::int_(0), nb::int_(1));
}

} // namespace

void bind_range(nb::module_& m) {
    bind_type<bool>(m);
    bind_type<int8_t>(m); bind_type<uint8_t>(m);
    bind_type<int16_t>(m); bind_type<uint16_t>(m);
    bind_type<int32_t>(m); bind_type<uint32_t>(m);
    bind_type<int64_t>(m); bind_type<uint64_t>(m);
    bind_type<float>(m); bind_type<double>(m);
    m.def("calc_data_range", &public_calc_data_range, nb::arg("data").none(),
          nb::arg("namespace"), nb::arg("builtins").none(),
          nb::arg("rgb").none(), nb::arg("dtype").none());
    m.def("layer_calc_data_range", &layer_calc_data_range, nb::arg("data").none(),
          nb::arg("namespace"), nb::arg("rgb").none() = false, nb::arg("dtype").none() = nb::none());
    m.def("layer_nanmin", &layer_nanmin, nb::arg("array").none(), nb::arg("namespace"));
    m.def("layer_nanmax", &layer_nanmax, nb::arg("array").none(), nb::arg("namespace"));
    m.def("dtype_normalize", &dtype_normalize, nb::arg("dtype_spec").none(), nb::arg("namespace"));
    m.def("dtype_normalize_str_by_bit_depth", &dtype_normalize_str_by_bit_depth,
          nb::arg("dtype_str").none(), nb::arg("kind").none(), nb::arg("namespace"));
    m.def("dtype_limits", &dtype_limits, nb::arg("dtype_spec").none(), nb::arg("namespace"));
    m.def("coerced_contrast_data", &coerced_contrast_data, nb::arg("coerced").none(), nb::arg("data").none());
    m.def("colormap_coerce_contrast_limits", &colormap_coerce_contrast_limits,
          nb::arg("limits").none(), nb::arg("namespace"));
    m.def("colormap_scale_down", &colormap_scale_down, nb::arg("limits").none(), nb::arg("namespace"));
    m.def("colormap_scale_up", &colormap_scale_up, nb::arg("limits").none(), nb::arg("namespace"));
    m.def("range_adapter_call", &range_adapter_call, nb::arg("original").none(), nb::arg("data").none(),
          nb::arg("rgb").none() = false, nb::arg("dtype").none() = nb::none());
}
} // namespace nx
