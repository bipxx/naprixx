#include "arrays.hpp"
#include "layer_core.hpp"
#include <nanobind/stl/vector.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/string.h>

namespace nx {
namespace {
using namespace app;
nb::object numpy() { return nb::module_::import_("numpy"); }
nb::object checked(PyObject* value) {
    if (!value) throw nb::python_error();
    return nb::steal<nb::object>(value);
}
bool truth(nb::handle value) {
    const int result = PyObject_IsTrue(value.ptr());
    if (result < 0) throw nb::python_error();
    return result != 0;
}
nb::tuple as_tuple(nb::handle value) { return nb::cast<nb::tuple>(checked(PySequence_Tuple(value.ptr()))); }
nb::tuple tuple_slice(nb::handle value, Index start) {
    auto key = nb::steal<nb::object>(PySlice_New(nb::int_(start).ptr(), Py_None, Py_None));
    if (!key.is_valid()) throw nb::python_error();
    return as_tuple(checked(PyObject_GetItem(value.ptr(), key.ptr())));
}
template <typename T> nb::tuple tuple_of(const std::vector<T>& values) {
    nb::list result;
    for (const auto& value : values) result.append(nb::cast(value));
    return as_tuple(result);
}
Input<double> as_double(nb::handle value) {
    return nb::cast<Input<double>>(numpy().attr("asarray")(value, nb::arg("dtype") = "float64"));
}
Matrix matrix(nb::handle value) {
    const auto array = as_double(value);
    if (array.ndim() != 2) throw std::invalid_argument("transform matrix must have two dimensions");
    Matrix result(array.shape(0), array.shape(1));
    for (size_t i = 0; i < result.rows; ++i)
        for (size_t j = 0; j < result.cols; ++j) result(i, j) = at(array, i, j);
    return result;
}
Output<double> array(const Matrix& value) { return from_vector(value.values, {value.rows, value.cols}); }
size_t dimensions(Index ndim) {
    if (ndim < 0) throw std::invalid_argument("negative dimensions are not allowed");
    return static_cast<size_t>(ndim);
}
Matrix make_rotation(nb::handle value) {
    const auto input = as_double(value);
    if (input.ndim() == 0) return rotation_2d(*input.data());
    if (input.ndim() == 1 && input.shape(0) == 3)
        return rotation_3d(at(input, 0), at(input, 1), at(input, 2));
    return matrix(value);
}
[[noreturn]] void invalid_shear(nb::handle value) {
    auto detail = checked(PyObject_Format(value.ptr(), nb::str("").ptr()));
    auto text = checked(PyUnicode_FromFormat("Only upper triangular or lower triangular matrices are accepted for shear, got %U. For other matrices, set the affine_matrix or linear_matrix directly.", detail.ptr()));
    PyErr_SetObject(PyExc_ValueError, text.ptr());
    throw nb::python_error();
}
Matrix make_shear(nb::handle value) {
    auto input = as_double(value);
    if (input.ndim() == 0)
        throw std::invalid_argument("Scalars are not valid values for shear. Shear must be an upper triangular vector or square matrix with ones along the main diagonal.");
    if (input.ndim() == 1) {
        std::vector<double> values(input.shape(0));
        for (size_t i = 0; i < values.size(); ++i) values[i] = at(input, i);
        return expand_shear(values);
    }
    auto result = matrix(value);
    if (!triangular(result, true) && !triangular(result, false))
        invalid_shear(value);
    return result;
}
nb::tuple make_slice_fields(nb::handle point, nb::handle left, nb::handle right, nb::handle ndim) {
    Index count = 0;
    bool found = false;
    for (auto value : {point, left, right}) if (!value.is_none()) {
        count = static_cast<Index>(nb::len(value)); found = true; break;
    }
    if (!found && ndim.is_none())
        throw std::invalid_argument("ndim must be provided if no other value is given");
    const Index size = ndim.is_none() ? count : nb::cast<Index>(ndim);
    if (!found) count = size;
    const Index prepend = std::max(Index{0}, size - count);
    nb::list result;
    for (auto value : {point, left, right}) {
        nb::list entries;
        for (Index i = 0; i < prepend; ++i) entries.append(0);
        if (value.is_none()) for (Index i = 0; i < size; ++i) entries.append(0);
        else for (auto entry : nb::iter(value)) entries.append(entry);
        result.append(tuple_slice(entries, -size));
    }
    return as_tuple(result);
}
nb::object converted_scale(nb::handle scale, nb::handle from, nb::handle to) {
    auto target = tuple_slice(to, -static_cast<Index>(nb::len(from)));
    int same = PyObject_RichCompareBool(from.ptr(), target.ptr(), Py_EQ);
    if (same < 0) {
        if (!PyErr_ExceptionMatches(PyExc_ValueError)) throw nb::python_error();
        PyErr_Clear(); same = 0;
    }
    if (same) return numpy().attr("array")(scale);
    auto values = as_tuple(scale), units = as_tuple(from);
    nb::list result;
    const size_t size = std::min({values.size(), units.size(), target.size()});
    for (size_t i = 0; i < size; ++i) {
        auto quantity = checked(PyNumber_Multiply(values[i].ptr(), units[i].ptr()));
        result.append(quantity.attr("to")(target[i]).attr("magnitude"));
    }
    return numpy().attr("array")(result);
}
bool unit_less(nb::handle a, nb::handle b, nb::handle registry) {
    nb::object x = registry.attr("get_base_units")(a)[nb::int_(0)];
    nb::object y = registry.attr("get_base_units")(b)[nb::int_(0)];
    const int result = PyObject_RichCompareBool(x.ptr(), y.ptr(), Py_LT);
    if (result < 0) throw nb::python_error();
    return result != 0;
}
void update_units(nb::handle units, nb::dict selected, nb::handle registry) {
    for (auto unit : nb::iter(units)) {
        auto key = unit.attr("dimensionality");
        if (!selected.contains(key) || !unit_less(selected[key], unit, registry)) selected[key] = unit;
    }
}
nb::object common_units(nb::handle extents) {
    auto registry = nb::module_::import_("pint").attr("get_application_registry")();
    nb::dict selected;
    nb::tuple axes;
    for (auto extent : nb::iter(extents)) {
        auto units = as_tuple(extent.attr("units"));
        update_units(units, selected, registry);
        for (size_t i = 1; i <= std::min(units.size(), axes.size()); ++i) {
            auto a = units[units.size() - i].attr("dimensionality");
            auto b = axes[axes.size() - i].attr("dimensionality");
            const int same = PyObject_RichCompareBool(a.ptr(), b.ptr(), Py_EQ);
            if (same < 0) throw nb::python_error();
            if (!same) return nb::none();
        }
        if (units.size() > axes.size()) axes = units;
    }
    if (!axes.size()) return nb::none();
    nb::list result;
    for (auto unit : axes) result.append(selected[unit.attr("dimensionality")]);
    return as_tuple(result);
}
nb::object join(nb::handle a, nb::handle b) { return checked(PyUnicode_Concat(a.ptr(), b.ptr())); }
nb::object increment_name(nb::str name, nb::str sep, nb::handle start) {
    const Py_ssize_t size = PyUnicode_GetLength(name.ptr());
    Py_ssize_t begin = size, end = size;
    if (size && PyUnicode_ReadChar(name.ptr(), size - 1) == ']') {
        Py_ssize_t pos = size - 2;
        while (pos >= 0 && Py_UNICODE_ISDECIMAL(PyUnicode_ReadChar(name.ptr(), pos))) --pos;
        if (pos >= 0 && PyUnicode_ReadChar(name.ptr(), pos) == '[' &&
            (pos == 0 || Py_UNICODE_ISSPACE(PyUnicode_ReadChar(name.ptr(), pos - 1)))) {
            begin = pos + 1; end = size - 1;
        }
    }
    nb::object replacement;
    if (begin < end) {
        auto digits = checked(PyUnicode_Substring(name.ptr(), begin, end));
        auto number = checked(PyLong_FromUnicodeObject(digits.ptr(), 10));
        auto increased = checked(PyNumber_Add(number.ptr(), nb::int_(1).ptr()));
        replacement = checked(PyObject_Str(increased.ptr()));
    } else {
        replacement = join(join(join(sep, nb::str("[")), checked(PyObject_Str(start.ptr()))), nb::str("]"));
    }
    return join(join(checked(PyUnicode_Substring(name.ptr(), 0, begin)), replacement),
                checked(PyUnicode_Substring(name.ptr(), end, size)));
}
nb::object unit_from_name(nb::handle unit) {
    auto pint = nb::module_::import_("pint");
    const int is_unit = PyObject_IsInstance(unit.ptr(), pint.attr("Unit").ptr());
    if (is_unit < 0) throw nb::python_error();
    if (is_unit) return nb::borrow<nb::object>(unit);
    auto registry = pint.attr("get_application_registry")();
    if (unit.is_none()) return registry.attr("pixel");
    if (PyUnicode_Check(unit.ptr())) {
        try { return registry.attr("parse_expression")(unit).attr("units"); }
        catch (nb::python_error& error) {
            if (!error.matches(pint.attr("errors").attr("UndefinedUnitError"))) throw;
        }
    }
    PyErr_Format(PyExc_ValueError, "Could not find unit %S", unit.ptr());
    throw nb::python_error();
}
nb::object units_from_name(nb::handle units) {
    if (units.is_none() || PyUnicode_Check(units.ptr())) return unit_from_name(units);
    nb::object sequence = nb::module_::import_("collections.abc").attr("Sequence");
    const int is_sequence = PyObject_IsInstance(units.ptr(), sequence.ptr());
    if (is_sequence < 0) throw nb::python_error();
    if (is_sequence) {
        nb::list result;
        for (auto unit : nb::iter(units)) result.append(unit_from_name(unit));
        return as_tuple(result);
    }
    PyErr_Format(PyExc_ValueError, "Could not find unit %S", units.ptr());
    throw nb::python_error();
}
nb::object apply_units(nb::handle transform, nb::handle world_units) {
    if (world_units.is_none()) return nb::borrow<nb::object>(transform);
    nb::object layer_units = transform.attr("units");
    const size_t count = nb::len(layer_units);
    if (nb::len(world_units) < count) return nb::borrow<nb::object>(transform);
    auto target = tuple_slice(world_units, -static_cast<Index>(count));
    if (target.size() != count) throw std::invalid_argument("zip() argument 2 is shorter than argument 1");
    auto registry = nb::module_::import_("pint").attr("get_application_registry")();
    auto scaling = Matrix::identity(count);
    for (size_t i = 0; i < count; ++i) {
        nb::object a = registry.attr("get_base_units")(target[i])[nb::int_(0)];
        nb::object b = registry.attr("get_base_units")(layer_units[nb::int_(i)])[nb::int_(0)];
        scaling(i, i) = nb::cast<double>(a) / nb::cast<double>(b);
    }
    return nb::module_::import_("napari.utils.transforms").attr("Affine")(
        nb::arg("ndim") = transform.attr("ndim"),
        nb::arg("linear_matrix") = array(multiply(matrix(transform.attr("linear_matrix")), scaling)),
        nb::arg("translate") = transform.attr("translate"));
}
bool complex_array(nb::handle value) {
    return nb::cast<std::string>(numpy().attr("asarray")(value).attr("dtype").attr("kind")) == "c";
}
nb::object embedded_array(nb::handle value, size_t ndim) {
    auto input = numpy().attr("asarray")(value);
    const auto shape = nb::cast<std::vector<size_t>>(input.attr("shape"));
    if (shape.size() != 2 || shape[0] != shape[1]) throw std::invalid_argument("matrix must be square");
    if (shape[0] == ndim) return nb::borrow<nb::object>(value);
    auto result = numpy().attr("eye")(ndim);
    auto key = checked(PySlice_New(nb::int_(-static_cast<Index>(shape[0])).ptr(), Py_None, Py_None));
    result[nb::make_tuple(key, key)] = input;
    return result;
}
bool triangular_array(nb::handle value, bool upper) {
    if (!complex_array(value)) return triangular(matrix(value), upper);
    return truth(numpy().attr("allclose")(value, numpy().attr(upper ? "triu" : "tril")(value)));
}
nb::object rotation_array(nb::handle value) {
    auto input = numpy().attr("array")(value);
    const int ndim = nb::cast<int>(input.attr("ndim"));
    if (ndim == 2 || (ndim == 1 && nb::len(input) != 3) ||
        (ndim == 0 && !truth(numpy().attr("isscalar")(value)))) return input;
    if (complex_array(input)) numpy().attr("deg2rad")(input); // preserves the unsupported-angle exception
    return nb::cast(array(make_rotation(value)));
}
nb::object shear_array(nb::handle value) {
    auto input = numpy().attr("array")(value);
    if (nb::cast<int>(input.attr("ndim")) == 2) {
        if (!triangular_array(input, true) && !triangular_array(input, false))
            invalid_shear(value);
        return input;
    }
    return nb::cast(array(make_shear(value)));
}
nb::object composed_array(nb::handle rotate, nb::handle scale, nb::handle shear) {
    if (complex_array(rotate) || complex_array(scale) || complex_array(shear)) {
        auto r = rotation_array(rotate), s = numpy().attr("diag")(scale), h = shear_array(shear);
        const size_t n = std::max({nb::cast<size_t>(r.attr("shape")[nb::int_(0)]),
            nb::cast<size_t>(s.attr("shape")[nb::int_(0)]), nb::cast<size_t>(h.attr("shape")[nb::int_(0)])});
        return numpy().attr("matmul")(numpy().attr("matmul")(embedded_array(r, n), embedded_array(h, n)), embedded_array(s, n));
    }
    auto rotation = make_rotation(rotate), skew = make_shear(shear);
    const auto factors = nb::cast<std::vector<double>>(scale);
    const size_t n = std::max({rotation.rows, factors.size(), skew.rows});
    auto scaling = Matrix::identity(factors.size());
    for (size_t i = 0; i < factors.size(); ++i) scaling(i, i) = factors[i];
    return nb::cast(array(multiply(multiply(embed(rotation, n), embed(skew, n)), embed(scaling, n))));
}
nb::tuple decompose_array(nb::handle value, bool upper) {
    auto input = numpy().attr("asarray")(value);
    const std::string dtype = nb::cast<std::string>(input.attr("dtype").attr("name"));
    if (dtype == "float32" || complex_array(input)) {
        auto scipy = nb::module_::import_("scipy.linalg");
        nb::object rotation, tri;
        if (upper) {
            auto qr = scipy.attr("qr")(input);
            rotation = qr[nb::int_(0)]; tri = qr[nb::int_(1)];
        } else {
            auto rq = scipy.attr("rq")(input.attr("T"));
            rotation = rq[nb::int_(1)].attr("T"); tri = rq[nb::int_(0)].attr("T");
        }
        auto signed_scale = numpy().attr("diag")(tri).attr("copy")();
        auto scale = numpy().attr("abs")(signed_scale);
        auto normalize = numpy().attr("divide")(scale, signed_scale);
        tri.attr("__imul__")(normalize.attr("reshape")(nb::make_tuple(-1, 1)));
        rotation.attr("__imul__")(normalize);
        nb::object skew = numpy().attr("matmul")(tri, numpy().attr("linalg").attr("inv")(numpy().attr("diag")(scale)));
        if (upper) {
            const size_t n = nb::cast<size_t>(input.attr("shape")[nb::int_(0)]);
            auto mask = numpy().attr("triu")(numpy().attr("ones")(nb::make_tuple(n, n)), 1).attr("astype")("bool");
            skew = skew[mask];
        }
        return nb::make_tuple(rotation, scale, skew);
    }
    const auto result = decompose(matrix(value), upper);
    nb::object shear;
    if (upper) {
        std::vector<double> elements;
        for (size_t i = 0; i < result.shear.rows; ++i)
            for (size_t j = i + 1; j < result.shear.cols; ++j) elements.push_back(result.shear(i, j));
        shear = nb::cast(from_vector(elements, {elements.size()}));
    } else shear = nb::cast(array(result.shear));
    return nb::make_tuple(array(result.rotation), from_vector(result.scale, {result.scale.size()}), shear);
}
nb::object coerce_layer_name(nb::handle layers, nb::handle name, nb::handle excluded) {
    auto existing = checked(PySet_New(nullptr));
    for (auto layer : nb::iter(layers)) if (layer.ptr() != excluded.ptr()) {
        nb::object label = layer.attr("name");
        if (PySet_Add(existing.ptr(), label.ptr()) < 0) throw nb::python_error();
    }
    nb::object result = nb::borrow<nb::object>(name);
    for (size_t i = 0; i < nb::len(layers); ++i) {
        const int present = PySet_Contains(existing.ptr(), result.ptr());
        if (present < 0) throw nb::python_error();
        if (present) result = increment_name(nb::cast<nb::str>(result), nb::str(" "), nb::int_(1));
    }
    return result;
}
nb::tuple unique_layers(nb::handle layers, nb::handle values, nb::handle allow) {
    auto existing = checked(PySet_New(layers.ptr()));
    auto blocked = existing.attr("difference")(allow);
    nb::object iterable = nb::module_::import_("collections.abc").attr("Iterable");
    const int is_iterable = PyObject_IsInstance(values.ptr(), iterable.ptr());
    if (is_iterable < 0) throw nb::python_error();
    auto result = is_iterable ? as_tuple(values) : nb::make_tuple(values);
    for (auto value : result) {
        const int duplicate = PySet_Contains(blocked.ptr(), value.ptr());
        if (duplicate < 0) throw nb::python_error();
        if (duplicate) {
            PyErr_Format(PyExc_ValueError, "Layer '%S' is already present in layer list", value.ptr());
            throw nb::python_error();
        }
    }
    return result;
}
} // namespace

void bind_layers(nb::module_& m) {
    m.def("transform_vector", [](std::optional<std::vector<double>> value, Index ndim, double pad) {
        auto result = trailing_vector(value, dimensions(ndim), pad);
        return from_vector(result, {result.size()});
    });
    m.def("transform_rotation_2d", [](double angle) { return array(rotation_2d(angle)); });
    m.def("transform_rotation_3d", [](double a, double b, double c) { return array(rotation_3d(a, b, c)); });
    m.def("transform_cos_sin", [](nb::handle angle) {
        auto radians = numpy().attr("deg2rad")(angle);
        return nb::make_tuple(numpy().attr("cos")(radians), numpy().attr("sin")(radians));
    });
    m.def("transform_make_rotation", &rotation_array, nb::arg("value").none());
    m.def("transform_rotation", [](nb::handle value, Index ndim) {
        return array(value.is_none() ? Matrix::identity(dimensions(ndim)) : embed(matrix(rotation_array(value)), dimensions(ndim)));
    }, nb::arg("value").none(), nb::arg("ndim"));
    m.def("transform_make_shear", &shear_array);
    m.def("transform_shear", [](nb::handle value, Index ndim) {
        return array(value.is_none() ? Matrix::identity(dimensions(ndim)) : embed(matrix(shear_array(value)), dimensions(ndim)));
    }, nb::arg("value").none(), nb::arg("ndim"));
    m.def("transform_expand_shear", [](const std::vector<double>& values) { return array(expand_shear(values)); });
    m.def("transform_embed", [](nb::handle value, Index ndim) -> nb::object {
        if (complex_array(value)) return embedded_array(value, dimensions(ndim));
        auto input = matrix(value);
        auto result = embed(input, dimensions(ndim));
        if (input.rows == static_cast<size_t>(ndim)) return nb::borrow<nb::object>(value);
        return nb::cast(array(result));
    });
    m.def("transform_compose", &composed_array);
    m.def("transform_infer", [](nb::handle scale, nb::handle translate, nb::handle rotate, nb::handle shear) {
        size_t ndim = 0;
        for (auto value : {scale, translate}) if (!value.is_none()) ndim = std::max(ndim, nb::len(value));
        if (!rotate.is_none()) ndim = std::max(ndim, nb::cast<size_t>(rotation_array(rotate).attr("shape")[nb::int_(0)]));
        if (!shear.is_none()) ndim = std::max(ndim, nb::cast<size_t>(shear_array(shear).attr("shape")[nb::int_(0)]));
        return ndim;
    }, nb::arg("scale").none(), nb::arg("translate").none(), nb::arg("rotate").none(), nb::arg("shear").none());
    m.def("transform_triangular", &triangular_array);
    m.def("transform_diagonal", [](nb::handle value, double tol) {
        if (!complex_array(value)) return diagonal(matrix(value), tol);
        auto input = numpy().attr("asarray")(value);
        auto shape = nb::cast<std::vector<size_t>>(input.attr("shape"));
        if (shape.size() != 2 || shape[0] != shape[1]) throw std::invalid_argument("matrix must be square");
        auto mask = numpy().attr("logical_not")(numpy().attr("eye")(shape[0], nb::arg("dtype") = "bool"));
        nb::object off_diagonal = input[mask];
        if (tol == 0) return nb::cast<size_t>(numpy().attr("count_nonzero")(off_diagonal)) == 0;
        return truth(numpy().attr("less_equal")(numpy().attr("max")(numpy().attr("abs")(off_diagonal)), tol));
    });
    m.def("transform_multiply", [](nb::handle a, nb::handle b) -> nb::object {
        if (complex_array(a) || complex_array(b)) return numpy().attr("matmul")(a, b);
        return nb::cast(array(multiply(matrix(a), matrix(b))));
    });
    m.def("transform_inverse", [](nb::handle value) -> nb::object {
        if (complex_array(value)) return numpy().attr("linalg").attr("inv")(value);
        try { return nb::cast(array(inverse(matrix(value)))); }
        catch (const std::domain_error& error) {
            auto type = numpy().attr("linalg").attr("LinAlgError");
            PyErr_SetString(type.ptr(), error.what()); throw nb::python_error();
        }
    });
    m.def("transform_decompose", [](nb::handle value, bool upper) {
        try {
            return decompose_array(value, upper);
        } catch (const std::domain_error& error) {
            auto type = numpy().attr("linalg").attr("LinAlgError");
            PyErr_SetString(type.ptr(), error.what()); throw nb::python_error();
        }
    });
    m.def("transform_shear_angle", [](double angle, Index ndim, const std::vector<Index>& axes) {
        auto result = Matrix::identity(dimensions(ndim));
        if (axes.size() != 2) throw std::invalid_argument("shear requires a pair of axes");
        const Index r = DimsState::axis_in_bounds(axes[0], ndim), c = DimsState::axis_in_bounds(axes[1], ndim);
        result(static_cast<size_t>(r), static_cast<size_t>(c)) = std::tan((90 - angle) * 3.14159265358979323846 / 180);
        return array(result);
    });
    m.def("make_slice_fields", &make_slice_fields, nb::arg("point").none(), nb::arg("left").none(), nb::arg("right").none(), nb::arg("ndim").none());
    m.def("copy_slice_fields", [](nb::handle source, nb::handle p, nb::handle l, nb::handle r, nb::handle n) {
        return make_slice_fields(truth(p) ? p : source.attr("point"), truth(l) ? l : source.attr("margin_left"),
                                 truth(r) ? r : source.attr("margin_right"), truth(n) ? n : source.attr("ndim"));
    }, nb::arg("source"), nb::arg("point").none(), nb::arg("left").none(), nb::arg("right").none(), nb::arg("ndim").none());
    m.def("select_slice_fields", [](nb::handle source, nb::handle key) {
        nb::list values;
        for (auto field : {"point", "margin_left", "margin_right"}) {
            auto a = numpy().attr("array")(source.attr(field));
            values.append(as_tuple(checked(PyObject_GetItem(a.ptr(), key.ptr()))));
        }
        return as_tuple(values);
    });
    nb::class_<SlicePlan>(m, "SlicePlan", nb::is_weak_referenceable())
        .def(nb::init<Index, std::vector<Index>>())
        .def_ro("ndisplay", &SlicePlan::ndisplay)
        .def_prop_ro("order", [](const SlicePlan& s) { return tuple_of(s.order); })
        .def_prop_ro("displayed", [](const SlicePlan& s) { return s.axes(true); })
        .def_prop_ro("not_displayed", [](const SlicePlan& s) { return s.axes(false); })
        .def("resized", &SlicePlan::resized)
        .def("orthogonal", [](const SlicePlan& s, nb::handle transform) { return s.orthogonal(matrix(transform)); })
        .def("data_slice", [](const SlicePlan& s, nb::handle transform, const std::vector<double>& translate, nb::handle world) {
            return array(s.data_slice(matrix(transform), translate, matrix(world)));
        })
        .def("__copy__", [](const SlicePlan& s) { return s; })
        .def("__deepcopy__", [](const SlicePlan& s, nb::dict) { return s; })
        .def("__reduce__", [](const SlicePlan& s) {
            return nb::make_tuple(nb::module_::import_("naparixx._restore").attr("slice_plan"), nb::make_tuple(s.ndisplay, tuple_of(s.order)));
        });
    nb::class_<LayerLoadState>(m, "LayerLoadState", nb::is_weak_referenceable())
        .def(nb::init<>())
        .def_rw("loaded", &LayerLoadState::loaded)
        .def_rw("request_id", &LayerLoadState::request_id)
        .def("set_loaded", &LayerLoadState::set_loaded)
        .def("begin", &LayerLoadState::begin)
        .def("finish", &LayerLoadState::finish)
        .def("__copy__", [](const LayerLoadState& s) { return s; })
        .def("__deepcopy__", [](const LayerLoadState& s, nb::dict) { return s; })
        .def("__reduce__", [](const LayerLoadState& s) {
            return nb::make_tuple(nb::module_::import_("naparixx._restore").attr("layer_load"), nb::make_tuple(s.loaded, s.request_id));
        });
    m.def("world_to_layer_axes", [](const std::vector<Index>& axes, Index world, Index layer) {
        const auto result = world_to_layer_axes(axes, world, layer);
        return from_vector(result, {result.size()});
    });
    m.def("layer_extent_bounds", [](const std::vector<std::vector<double>>& lo, const std::vector<std::vector<double>>& hi) {
        const auto value = aggregate_extent(lo, hi);
        std::vector<double> low(value.values.begin(), value.values.begin() + value.cols);
        std::vector<double> high(value.values.begin() + value.cols, value.values.end());
        return nb::make_tuple(from_vector(low, {low.size()}), from_vector(high, {high.size()}));
    });
    m.def("layer_step_sizes", [](const std::vector<std::vector<double>>& values) {
        const auto result = aggregate_steps(values);
        return from_vector(result, {result.size()});
    });
    m.def("layer_axis_labels", [](Index ndim, const std::vector<std::vector<std::string>>& annotations) {
        return tuple_of(layer_axis_labels(ndim, annotations));
    });
    m.def("convert_layer_scale", &converted_scale);
    m.def("common_layer_units", &common_units);
    m.def("unit_less", &unit_less);
    m.def("update_unit_selection", &update_units);
    m.def("increment_layer_name", &increment_name);
    m.def("unit_from_name", &unit_from_name, nb::arg("unit").none());
    m.def("units_from_name", &units_from_name, nb::arg("units").none());
    m.def("apply_slice_units", &apply_units, nb::arg("transform"), nb::arg("world_units").none());
    m.def("coerce_layer_name", &coerce_layer_name, nb::arg("layers"), nb::arg("name"), nb::arg("excluded").none());
    m.def("unique_layers", &unique_layers);
}
} // namespace nx
