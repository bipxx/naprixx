#include "arrays.hpp"
#include <nanobind/stl/vector.h>

namespace nx {
namespace {
bool compare(nb::handle a, nb::handle b, int op) {
    const int result = PyObject_RichCompareBool(a.ptr(), b.ptr(), op);
    if (result < 0) throw nb::python_error();
    return result != 0;
}
nb::tuple guess_multiscale(nb::handle data) {
    nb::object multiscale = nb::module_::import_("napari.layers._multiscale_data").attr("MultiScaleData");
    const int is_multiscale = PyObject_IsInstance(data.ptr(), multiscale.ptr());
    if (is_multiscale < 0) throw nb::python_error();
    if (is_multiscale) return nb::make_tuple(true, data);
    if (nb::hasattr(data, "ndim") && compare(data.attr("ndim"), nb::int_(1), Py_GT)) return nb::make_tuple(false, data);
    if ((PyList_Check(data.ptr()) || PyTuple_Check(data.ptr())) && nb::len(data) == 1)
        return nb::make_tuple(false, data[nb::int_(0)]);
    nb::list sizes;
    for (auto item : nb::iter(data)) sizes.append(item.attr("size"));
    if (sizes.size() <= 1) return nb::make_tuple(false, data);
    bool decreasing = true, same = true;
    for (size_t i = 0; i < sizes.size(); ++i) {
        same = same && compare(sizes[i], sizes[0], Py_EQ);
        if (i) decreasing = decreasing && compare(sizes[i - 1], sizes[i], Py_GT);
    }
    if (same || !decreasing) {
        if (same) PyErr_Format(PyExc_ValueError,
            "Input data should be an array-like object, or a sequence of arrays of decreasing size. Got arrays of single size: %S", sizes[0].ptr());
        else PyErr_Format(PyExc_ValueError,
            "Input data should be an array-like object, or a sequence of arrays of decreasing size. Got arrays in incorrect order, sizes: %S", sizes.ptr());
        throw nb::python_error();
    }
    return nb::make_tuple(true, multiscale(data));
}
} // namespace
void bind_data(nb::module_& m) {
    m.def("guess_rgb", [](const std::vector<int64_t>& shape, int64_t min_side) {
        if (shape.empty()) throw nb::index_error("tuple index out of range");
        const size_t n = shape.size();
        return n > 2 && (shape.back() == 3 || shape.back() == 4) && shape[n - 2] > min_side && shape[n - 3] > min_side;
    });
    m.def("guess_multiscale", &guess_multiscale, nb::arg("data").none());
    m.def("guess_labels", [](nb::handle data) {
        if (nb::hasattr(data, "dtype")) {
            nb::object dtype = data.attr("dtype");
            auto numpy = nb::module_::import_("numpy");
            for (const char* type : {"int32", "uint32", "int64", "uint64"})
                if (compare(dtype, numpy.attr(type), Py_EQ)) return "labels";
        }
        return "image";
    }, nb::arg("data").none());
}
} // namespace nx
