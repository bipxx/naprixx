#include "arrays.hpp"
#include "collection_protocol.hpp"
#include <array>
#include <nanobind/stl/vector.h>
#include <nanobind/stl/string.h>

namespace nx {

template <size_t D>
void affine_rows(const double* coords, int64_t row_stride, int64_t col_stride,
                 size_t rows, const double* matrix, const double* translate,
                 double* out) {
    for (size_t r = 0; r < rows; ++r) {
        double input[D];
        const int64_t row_offset = static_cast<int64_t>(r) * row_stride;
        for (size_t k = 0; k < D; ++k)
            input[k] = read(coords, row_offset + static_cast<int64_t>(k) * col_stride);
        for (size_t c = 0; c < D; ++c) {
            double value = 0;
            for (size_t k = 0; k < D; ++k)
                value += input[k] * matrix[c * D + k];
            out[r * D + c] = value + translate[c];
        }
    }
}

Output<double> affine(Input<double> coords, Input<double> matrix,
                      Input<double> translate) {
    const size_t dims = coordinate_dims(coords);
    if (matrix.ndim() != 2 || matrix.shape(0) != matrix.shape(1))
        throw std::invalid_argument("linear matrix must be square");
    const size_t md = matrix.shape(0);
    require_vector(translate, md, "translation must match the linear matrix");
    if (dims < md)
        throw std::invalid_argument("coordinate dimensions are smaller than the transform");
    const size_t rows = coords.ndim() == 1 ? 1 : coords.shape(0);
    auto out = allocate<double>(coords.ndim() == 1
        ? std::vector<size_t>{dims} : std::vector<size_t>{rows, dims});
    const size_t leading = dims - md;
    std::vector<double> padded(checked_product({dims, dims}), 0);
    std::vector<double> shift(dims, 0);
    for (size_t c = 0; c < leading; ++c) padded[c * dims + c] = 1;
    for (size_t c = 0; c < md; ++c) {
        shift[leading + c] = at(translate, c);
        for (size_t k = 0; k < md; ++k)
            padded[(leading + c) * dims + leading + k] = at(matrix, c, k);
    }
    const int64_t row_stride = coords.ndim() == 1 ? 0 : coords.stride(0);
    const int64_t col_stride = coords.stride(coords.ndim() - 1);
    {
        nb::gil_scoped_release release;
        auto* input = coords.data();
        auto* output = out.data();
        switch (dims) {
        case 2: affine_rows<2>(input, row_stride, col_stride, rows, padded.data(), shift.data(), output); break;
        case 3: affine_rows<3>(input, row_stride, col_stride, rows, padded.data(), shift.data(), output); break;
        case 4: affine_rows<4>(input, row_stride, col_stride, rows, padded.data(), shift.data(), output); break;
        case 5: affine_rows<5>(input, row_stride, col_stride, rows, padded.data(), shift.data(), output); break;
        default:
        for (size_t r = 0; r < rows; ++r) {
            for (size_t c = 0; c < dims; ++c) {
                double value = 0;
                for (size_t k = 0; k < dims; ++k) {
                    value += read(input, static_cast<int64_t>(r) * row_stride
                                         + static_cast<int64_t>(k) * col_stride)
                             * padded[c * dims + k];
                }
                output[r * dims + c] = value + shift[c];
            }
        }
        }
    }
    return out;
}

template <typename T>
Output<T> scale_translate(Input<T> coords, Input<T> scale, Input<T> translate) {
    const size_t dims = coordinate_dims(coords);
    if (scale.ndim() != 1 || translate.ndim() != 1 ||
        scale.shape(0) != translate.shape(0) || dims < scale.shape(0))
        throw std::invalid_argument("scale and translation must match coordinate dimensions");
    const size_t leading = dims - scale.shape(0);
    const size_t rows = coords.ndim() == 1 ? 1 : coords.shape(0);
    auto out = allocate<T>(coords.ndim() == 1
        ? std::vector<size_t>{dims} : std::vector<size_t>{rows, dims});
    {
        nb::gil_scoped_release release;
        for (size_t r = 0; r < rows; ++r)
            for (size_t c = 0; c < dims; ++c) {
                T value = coord(coords, r, c);
                if (c >= leading) {
                    value = value * at(scale, c - leading);
                    value = value + at(translate, c - leading);
                }
                out.data()[r * dims + c] = value;
            }
    }
    return out;
}

namespace {
nb::object tn_checked(PyObject* value) { if (!value) throw nb::python_error(); return nb::steal<nb::object>(value); }
nb::object tn_attr(nb::object value, const char* name) { return tn_checked(PyObject_GetAttrString(value.ptr(), name)); }
nb::object tn_item(nb::object value, nb::object key) { return tn_checked(PyObject_GetItem(value.ptr(), key.ptr())); }
nb::object tn_call1(nb::object fn, nb::object value) { return tn_checked(PyObject_CallOneArg(fn.ptr(), value.ptr())); }
nb::object tn_call2(nb::object fn, nb::object a, nb::object b) { return tn_checked(PyObject_CallFunctionObjArgs(fn.ptr(), a.ptr(), b.ptr(), nullptr)); }
nb::object tn_call3(nb::object fn, nb::object a, nb::object b, nb::object c) { return tn_checked(PyObject_CallFunctionObjArgs(fn.ptr(), a.ptr(), b.ptr(), c.ptr(), nullptr)); }
nb::object tn_kw(nb::object fn, nb::tuple args, nb::dict kw) { return tn_checked(PyObject_Call(fn.ptr(), args.ptr(), kw.ptr())); }
bool tn_truth(nb::object value) { int r = PyObject_IsTrue(value.ptr()); if (r < 0) throw nb::python_error(); return r != 0; }
nb::object tn_compare(nb::object a, nb::object b, int op) { return tn_checked(PyObject_RichCompare(a.ptr(), b.ptr(), op)); }
void tn_replace(nb::object& target, nb::object value) { nb::object old = std::move(target); target = std::move(value); old.reset(); }
nb::object tn_optional(const nb::object& value) { return value.is_valid() ? value : nb::none(); }
nb::object tn_slice() { return tn_checked(PySlice_New(Py_None, Py_None, Py_None)); }
struct NumericalNamespace {
    nb::dict globals;
    nb::object builtins;
    explicit NumericalNamespace(nb::dict ns) : globals(std::move(ns)), builtins(nb::borrow<nb::object>(PyEval_GetBuiltins())) {}
    nb::object get(const char* name) const {
        nb::object key = nb::str(name);
        PyObject* value = PyDict_GetItemWithError(globals.ptr(), key.ptr());
        if (value) return nb::borrow<nb::object>(value);
        if (PyErr_Occurred()) throw nb::python_error();
        value = PyObject_GetItem(builtins.ptr(), key.ptr());
        if (value) return nb::steal<nb::object>(value);
        if (!PyErr_ExceptionMatches(PyExc_KeyError)) throw nb::python_error();
        PyErr_Clear();
        nb::object message = tn_checked(PyUnicode_FromFormat("name '%s' is not defined", name));
        nb::object error = tn_call1(nb::borrow<nb::object>(PyExc_NameError), std::move(message));
        if (PyObject_SetAttrString(error.ptr(), "name", key.ptr()) < 0) throw nb::python_error();
        PyErr_SetObject(PyExc_NameError, error.ptr()); throw nb::python_error();
    }
    nb::object numpy(const char* name) const { return tn_attr(get("np"), name); }
};
struct TransformCallLocals {
    enum class Layout { Empty, Scale, Affine } layout = Layout::Empty;
    nb::object coords, append_first_axis, coords_ndim, scale, translate, padded_linear_matrix, out;
    TransformCallLocals() = default;
    TransformCallLocals(TransformCallLocals&&) = default;
    ~TransformCallLocals() { clear(); }
    void clear() {
        std::array<nb::object, 7> old = {std::move(coords), std::move(append_first_axis), std::move(coords_ndim),
            std::move(scale), std::move(translate), std::move(padded_linear_matrix), std::move(out)};
        const auto source = layout; layout = Layout::Empty;
        if (source == Layout::Affine) for (size_t i : {0u, 1u, 2u, 5u, 4u, 6u}) old[i].reset();
        for (auto& value : old) value.reset();
    }
};
struct TransformAdapterLocals {
    nb::object array, matrix, translate, matrix_dtype, dtype;
    TransformAdapterLocals() = default;
    TransformAdapterLocals(TransformAdapterLocals&&) = default;
    ~TransformAdapterLocals() { clear(); }
    void clear() {
        std::array<nb::object, 5> old = {std::move(array), std::move(matrix), std::move(translate), std::move(matrix_dtype), std::move(dtype)};
        for (auto& value : old) value.reset();
    }
};
int tn_call_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self)); if (nb::inst_ready(self)) {
        const auto& s = *nb::inst_ptr<TransformCallLocals>(self);
        Py_VISIT(s.coords.ptr()); Py_VISIT(s.append_first_axis.ptr()); Py_VISIT(s.coords_ndim.ptr());
        Py_VISIT(s.scale.ptr()); Py_VISIT(s.translate.ptr()); Py_VISIT(s.padded_linear_matrix.ptr()); Py_VISIT(s.out.ptr());
    } return 0;
}
int tn_call_clear(PyObject* self) { if (nb::inst_ready(self)) nb::inst_ptr<TransformCallLocals>(self)->clear(); return 0; }
int tn_adapter_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self)); if (nb::inst_ready(self)) {
        const auto& s = *nb::inst_ptr<TransformAdapterLocals>(self);
        Py_VISIT(s.array.ptr()); Py_VISIT(s.matrix.ptr()); Py_VISIT(s.translate.ptr()); Py_VISIT(s.matrix_dtype.ptr()); Py_VISIT(s.dtype.ptr());
    } return 0;
}
int tn_adapter_clear(PyObject* self) { if (nb::inst_ready(self)) nb::inst_ptr<TransformAdapterLocals>(self)->clear(); return 0; }
PyType_Slot tn_call_slots[] = {{Py_tp_traverse, reinterpret_cast<void*>(tn_call_traverse)}, {Py_tp_clear, reinterpret_cast<void*>(tn_call_clear)}, {0, nullptr}};
PyType_Slot tn_adapter_slots[] = {{Py_tp_traverse, reinterpret_cast<void*>(tn_adapter_traverse)}, {Py_tp_clear, reinterpret_cast<void*>(tn_adapter_clear)}, {0, nullptr}};

void tn_prepare_coords(NumericalNamespace& ns, TransformCallLocals& s) {
    tn_replace(s.coords, tn_call1(ns.numpy("asarray"), s.coords));
    tn_replace(s.append_first_axis, tn_compare(tn_attr(s.coords, "ndim"), nb::int_(1), Py_EQ));
    if (tn_truth(s.append_first_axis)) {
        nb::object target = s.coords;
        nb::object key = nb::make_tuple(ns.numpy("newaxis"), tn_slice());
        tn_replace(s.coords, tn_item(std::move(target), std::move(key)));
    }
    tn_replace(s.coords_ndim, tn_item(tn_attr(s.coords, "shape"), nb::int_(1)));
}
nb::object tn_finish(TransformCallLocals& s) {
    tn_replace(s.out, tn_checked(PyNumber_InPlaceAdd(s.out.ptr(), s.translate.ptr())));
    if (tn_truth(s.append_first_axis)) tn_replace(s.out, tn_item(s.out, nb::int_(0)));
    return s.out;
}
nb::object affine_call(nb::object self, nb::object coords, nb::dict namespace_, TransformCallLocals& s) {
    // coords is supplied from the readonly transferred source parameter slot;
    // it is not a second mutable/native array authority.
    s.layout = TransformCallLocals::Layout::Affine; NumericalNamespace ns(std::move(namespace_));
    if (!s.coords.is_valid()) tn_replace(s.coords, std::move(coords));
    coords.reset(); tn_prepare_coords(ns, s);
    nb::object embed = ns.get("embed_in_identity_matrix");
    tn_replace(s.padded_linear_matrix, tn_call2(std::move(embed), tn_attr(self, "_linear_matrix"), s.coords_ndim));
    nb::object converter = ns.get("translate_to_vector");
    nb::object value = tn_attr(self, "_translate"); nb::dict kw; kw["ndim"] = s.coords_ndim;
    tn_replace(s.translate, tn_kw(std::move(converter), nb::make_tuple(std::move(value)), std::move(kw)));
    nb::object left = s.coords; nb::object right = tn_attr(s.padded_linear_matrix, "T");
    nb::object product = tn_checked(PyNumber_MatrixMultiply(left.ptr(), right.ptr()));
    left.reset(); right.reset(); tn_replace(s.out, std::move(product));
    return tn_finish(s);
}
nb::object scale_translate_call(nb::object self, nb::object coords, nb::dict namespace_, TransformCallLocals& s) {
    s.layout = TransformCallLocals::Layout::Scale; NumericalNamespace ns(std::move(namespace_));
    if (!s.coords.is_valid()) tn_replace(s.coords, std::move(coords));
    coords.reset(); tn_prepare_coords(ns, s);
    nb::object len = ns.get("len"); nb::object source_scale = tn_attr(self, "scale");
    nb::object scale_length = tn_call1(std::move(len), std::move(source_scale));
    if (tn_truth(tn_compare(s.coords_ndim, std::move(scale_length), Py_EQ))) {
        tn_replace(s.scale, tn_attr(self, "scale")); tn_replace(s.translate, tn_attr(self, "translate"));
    } else {
        nb::object concatenate = ns.numpy("concatenate"); nb::list padding; padding.append(nb::float_(1.0));
        len = ns.get("len"); source_scale = tn_attr(self, "scale");
        scale_length = tn_call1(std::move(len), std::move(source_scale));
        nb::object count = tn_checked(PyNumber_Subtract(s.coords_ndim.ptr(), scale_length.ptr())); scale_length.reset();
        nb::object repeated = tn_checked(PyNumber_Multiply(padding.ptr(), count.ptr())); padding.reset(); count.reset();
        nb::object value = tn_attr(self, "scale");
        tn_replace(s.scale, tn_call1(std::move(concatenate), nb::make_tuple(std::move(repeated), std::move(value))));
        concatenate = ns.numpy("concatenate"); nb::list shift_padding; shift_padding.append(nb::float_(0.0));
        len = ns.get("len"); value = tn_attr(self, "translate");
        scale_length = tn_call1(std::move(len), std::move(value));
        count = tn_checked(PyNumber_Subtract(s.coords_ndim.ptr(), scale_length.ptr())); scale_length.reset();
        repeated = tn_checked(PyNumber_Multiply(shift_padding.ptr(), count.ptr())); shift_padding.reset(); count.reset();
        value = tn_attr(self, "translate");
        tn_replace(s.translate, tn_call1(std::move(concatenate), nb::make_tuple(std::move(repeated), std::move(value))));
    }
    tn_replace(s.out, tn_checked(PyNumber_Multiply(s.scale.ptr(), s.coords.ptr())));
    return tn_finish(s);
}
bool tn_dtype_member(nb::object value, NumericalNamespace& ns, const char* set_name) {
    nb::object dtype = tn_attr(std::move(value), "dtype"), accepted = ns.get(set_name);
    const int result = PySequence_Contains(accepted.ptr(), dtype.ptr()); if (result < 0) throw nb::python_error(); return result != 0;
}
bool tn_rank_ok(nb::object array) {
    nb::object ndim = tn_attr(std::move(array), "ndim");
    nb::tuple ranks = nb::make_tuple(1, 2);
    const int result = PySequence_Contains(ranks.ptr(), ndim.ptr());
    if (result < 0) throw nb::python_error(); return result != 0;
}
nb::object tn_len_attr(NumericalNamespace& ns, const nb::object& self, const char* name) {
    nb::object len = ns.get("len"); nb::object value = tn_attr(self, name);
    return tn_call1(std::move(len), std::move(value));
}
nb::object transform_adapter_double(nb::object value, nb::dict namespace_) {
    NumericalNamespace ns(std::move(namespace_)); nb::object asarray = ns.numpy("asarray");
    nb::dict kw; kw["dtype"] = ns.numpy("float64");
    return tn_kw(std::move(asarray), nb::make_tuple(std::move(value)), std::move(kw));
}
nb::object tn_original_call(nb::object original_getter, nb::object self, nb::object coords) {
    nb::object original = tn_checked(PyObject_CallNoArgs(original_getter.ptr()));
    return tn_call2(std::move(original), std::move(self), std::move(coords));
}
nb::object affine_adapter_call(nb::object original_getter, nb::object self, nb::object coords,
                               nb::dict namespace_, TransformAdapterLocals& s) {
    NumericalNamespace ns(std::move(namespace_));
    tn_replace(s.array, tn_call1(ns.numpy("asarray"), coords));
    tn_replace(s.matrix, tn_attr(self, "_linear_matrix")); tn_replace(s.translate, tn_attr(self, "_translate"));
    if (!tn_rank_ok(s.array) || !tn_dtype_member(s.array, ns, "_REAL_DTYPES") ||
        !tn_dtype_member(s.matrix, ns, "_REAL_DTYPES") || !tn_dtype_member(s.translate, ns, "_REAL_DTYPES"))
        return tn_original_call(std::move(original_getter), std::move(self), std::move(coords));
    nb::object coordinate_dim = tn_item(tn_attr(s.array, "shape"), nb::int_(-1));
    nb::object matrix_dim = tn_item(tn_attr(s.matrix, "shape"), nb::int_(0));
    if (tn_truth(tn_compare(std::move(coordinate_dim), std::move(matrix_dim), Py_LT)))
        return tn_original_call(std::move(original_getter), std::move(self), std::move(coords));
    coordinate_dim = tn_item(tn_attr(s.array, "shape"), nb::int_(-1));
    matrix_dim = tn_item(tn_attr(s.matrix, "shape"), nb::int_(0));
    if (tn_truth(tn_compare(std::move(coordinate_dim), std::move(matrix_dim), Py_EQ))) tn_replace(s.matrix_dtype, tn_attr(s.matrix, "dtype"));
    else tn_replace(s.matrix_dtype, ns.numpy("float64"));
    nb::object result_type = ns.numpy("result_type");
    nb::object dtype = tn_call2(std::move(result_type), tn_attr(s.array, "dtype"), s.matrix_dtype);
    nb::object expected = ns.numpy("float64");
    if (tn_truth(tn_compare(std::move(dtype), std::move(expected), Py_NE)))
        return tn_original_call(std::move(original_getter), std::move(self), std::move(coords));
    nb::object engine = tn_attr(ns.get("_native"), "affine");
    nb::object array = tn_call1(ns.get("_double"), s.array);
    nb::object matrix = tn_call1(ns.get("_double"), s.matrix);
    nb::object translate = tn_call1(ns.get("_double"), s.translate);
    return tn_call3(std::move(engine), std::move(array), std::move(matrix), std::move(translate));
}
nb::object scale_translate_adapter_call(nb::object original_getter, nb::object self, nb::object coords,
                                        nb::dict namespace_, TransformAdapterLocals& s) {
    NumericalNamespace ns(std::move(namespace_));
    tn_replace(s.array, tn_call1(ns.numpy("asarray"), coords));
    if (!tn_rank_ok(s.array) || !tn_dtype_member(s.array, ns, "_REAL_DTYPES") ||
        !tn_dtype_member(tn_attr(self, "scale"), ns, "_REAL_DTYPES") ||
        !tn_dtype_member(tn_attr(self, "translate"), ns, "_REAL_DTYPES"))
        return tn_original_call(std::move(original_getter), std::move(self), std::move(coords));
    nb::object coord_dim = tn_item(tn_attr(s.array, "shape"), nb::int_(-1));
    nb::object scale_dim = tn_len_attr(ns, self, "scale");
    if (tn_truth(tn_compare(std::move(coord_dim), std::move(scale_dim), Py_LT)))
        return tn_original_call(std::move(original_getter), std::move(self), std::move(coords));
    scale_dim = tn_len_attr(ns, self, "scale");
    nb::object translate_dim = tn_len_attr(ns, self, "translate");
    if (tn_truth(tn_compare(std::move(scale_dim), std::move(translate_dim), Py_NE)))
        return tn_original_call(std::move(original_getter), std::move(self), std::move(coords));
    nb::object fn = ns.numpy("result_type"), first = tn_attr(s.array, "dtype"), second = tn_attr(tn_attr(self, "scale"), "dtype");
    tn_replace(s.dtype, tn_call2(std::move(fn), std::move(first), std::move(second)));
    coord_dim = tn_item(tn_attr(s.array, "shape"), nb::int_(-1)); scale_dim = tn_len_attr(ns, self, "scale");
    if (tn_truth(tn_compare(std::move(coord_dim), std::move(scale_dim), Py_NE))) {
        fn = ns.numpy("result_type"); second = ns.numpy("float64");
        tn_replace(s.dtype, tn_call2(std::move(fn), s.dtype, std::move(second)));
    }
    nb::object accepted = ns.get("_FLOAT_DTYPES");
    const int member = PySequence_Contains(accepted.ptr(), s.dtype.ptr()); if (member < 0) throw nb::python_error(); accepted.reset();
    bool unsupported = member == 0;
    if (!unsupported) {
        fn = ns.numpy("result_type"); second = tn_attr(tn_attr(self, "translate"), "dtype");
        unsupported = tn_truth(tn_compare(tn_call2(std::move(fn), s.dtype, std::move(second)), s.dtype, Py_NE));
    }
    if (unsupported) return tn_original_call(std::move(original_getter), std::move(self), std::move(coords));
    nb::object engine = tn_attr(ns.get("_native"), "scale_translate");
    fn = ns.numpy("asarray"); nb::dict kw; kw["dtype"] = s.dtype;
    nb::object array = tn_kw(std::move(fn), nb::make_tuple(s.array), std::move(kw));
    fn = ns.numpy("asarray"); nb::object scale = tn_attr(self, "scale"); kw = nb::dict(); kw["dtype"] = s.dtype;
    scale = tn_kw(std::move(fn), nb::make_tuple(std::move(scale)), std::move(kw));
    fn = ns.numpy("asarray"); nb::object translate = tn_attr(self, "translate"); kw = nb::dict(); kw["dtype"] = s.dtype;
    translate = tn_kw(std::move(fn), nb::make_tuple(std::move(translate)), std::move(kw));
    return tn_call3(std::move(engine), std::move(array), std::move(scale), std::move(translate));
}
} // namespace

void bind_transforms(nb::module_& m) {
    m.def("affine", &affine, nb::arg("coords").noconvert(), nb::arg("matrix").noconvert(), nb::arg("translate").noconvert());
    m.def("scale_translate", &scale_translate<float>, nb::arg("coords").noconvert(), nb::arg("scale").noconvert(), nb::arg("translate").noconvert());
    m.def("scale_translate", &scale_translate<double>, nb::arg("coords").noconvert(), nb::arg("scale").noconvert(), nb::arg("translate").noconvert());
    nb::class_<TransformCallLocals>(m, "TransformCallLocals", nb::is_weak_referenceable(), nb::type_slots(tn_call_slots))
        .def_prop_ro("coords", [](const TransformCallLocals& s) { return tn_optional(s.coords); })
        .def_prop_ro("append_first_axis", [](const TransformCallLocals& s) { return tn_optional(s.append_first_axis); })
        .def_prop_ro("coords_ndim", [](const TransformCallLocals& s) { return tn_optional(s.coords_ndim); })
        .def_prop_ro("scale", [](const TransformCallLocals& s) { return tn_optional(s.scale); })
        .def_prop_ro("translate", [](const TransformCallLocals& s) { return tn_optional(s.translate); })
        .def_prop_ro("padded_linear_matrix", [](const TransformCallLocals& s) { return tn_optional(s.padded_linear_matrix); })
        .def_prop_ro("out", [](const TransformCallLocals& s) { return tn_optional(s.out); });
    nb::class_<TransformAdapterLocals>(m, "TransformAdapterLocals", nb::is_weak_referenceable(), nb::type_slots(tn_adapter_slots))
        .def_prop_ro("array", [](const TransformAdapterLocals& s) { return tn_optional(s.array); })
        .def_prop_ro("matrix", [](const TransformAdapterLocals& s) { return tn_optional(s.matrix); })
        .def_prop_ro("translate", [](const TransformAdapterLocals& s) { return tn_optional(s.translate); })
        .def_prop_ro("matrix_dtype", [](const TransformAdapterLocals& s) { return tn_optional(s.matrix_dtype); })
        .def_prop_ro("dtype", [](const TransformAdapterLocals& s) { return tn_optional(s.dtype); });
    m.def("transform_call_locals", [](nb::object coords) { TransformCallLocals s; s.coords = std::move(coords); return s; }, nb::arg("coords").none());
    m.def("transform_adapter_locals", [] { return TransformAdapterLocals(); });
    m.def("affine_call", &affine_call, nb::arg("self").none(), nb::arg("coords").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    m.def("scale_translate_call", &scale_translate_call, nb::arg("self").none(), nb::arg("coords").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    m.def("affine_adapter_call", &affine_adapter_call, nb::arg("original_getter").none(), nb::arg("self").none(), nb::arg("coords").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    m.def("scale_translate_adapter_call", &scale_translate_adapter_call, nb::arg("original_getter").none(), nb::arg("self").none(), nb::arg("coords").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    m.def("transform_adapter_double", &transform_adapter_double, nb::arg("value").none(), nb::arg("namespace"));
}
} // namespace nx
