#include "arrays.hpp"
#include <nanobind/stl/vector.h>
#include <nanobind/stl/string.h>
#include <cmath>

namespace nx {

// Mode values follow the Python adapter: none, all, linear, spherical.
template <typename T>
nb::tuple slice_points(Input<T> data, Input<double> sizes, Input<bool> shown,
                       const std::vector<int64_t>& axes, Input<double> point,
                       Input<double> left, Input<double> right, int mode) {
    if (data.ndim() != 2)
        throw std::invalid_argument("point data must have shape (N, D)");
    const size_t rows = data.shape(0), count = axes.size();
    require_vector(sizes, rows, "sizes must contain one entry per point");
    require_vector(shown, rows, "shown must contain one entry per point");
    require_vector(point, count, "slice point must match slicing axes");
    require_vector(left, count, "left margins must match slicing axes");
    require_vector(right, count, "right margins must match slicing axes");
    if (mode < 0 || mode > 3)
        throw std::invalid_argument("unknown point projection mode");
    std::vector<double> low(count), high(count);
    std::vector<bool> seen(data.shape(1), false);
    for (size_t j = 0; j < count; ++j) {
        if (axes[j] < 0 || static_cast<size_t>(axes[j]) >= data.shape(1) || seen[axes[j]])
            throw std::invalid_argument("slicing axes must be unique and in bounds");
        seen[axes[j]] = true;
        low[j] = at(point, j) - (mode == 0 ? 0 : at(left, j));
        high[j] = at(point, j) + (mode == 0 ? 0 : at(right, j));
        // Match np.isclose(high, low), including equal signed infinities.
        if (high[j] == low[j] ||
            (std::isfinite(high[j]) && std::isfinite(low[j]) &&
             std::abs(high[j] - low[j]) <= 1e-8 + 1e-5 * std::abs(low[j]))) {
            low[j] -= 0.5;
            high[j] += 0.5;
        }
    }
    std::vector<int64_t> indices;
    std::vector<double> output_sizes;
    {
        nb::gil_scoped_release release;
        for (size_t r = 0; r < rows; ++r) {
            if (!at(shown, r))
                continue;
            bool visible = true;
            for (size_t j = 0; j < count; ++j) {
                double value = static_cast<double>(at(data, r, static_cast<size_t>(axes[j])));
                if (!(value >= low[j] && value <= high[j])) {
                    visible = false;
                    break;
                }
            }
            if (!visible)
                continue;
            double size = at(sizes, r);
            if (mode == 2) {
                double scale = 1;
                for (size_t j = 0; j < count; ++j) {
                    const double distance = static_cast<double>(at(data, r, axes[j])) - at(point, j);
                    const double end = (distance < 0 ? low[j] : high[j]) - at(point, j);
                    scale *= 1 - distance / end;
                }
                size *= scale;
            } else if (mode == 3) {
                const double radius = size / 2;
                double portion = 1;
                for (size_t j = 0; j < count; ++j) {
                    const double distance = std::abs(static_cast<double>(at(data, r, axes[j])) - at(point, j));
                    if (!(distance < radius)) {
                        visible = false;
                        break;
                    }
                    portion *= 1 - distance / radius;
                }
                if (!visible)
                    continue;
                const double segment = (1 - portion) * radius;
                size = 2 * std::sqrt(radius * radius - segment * segment);
            }
            indices.push_back(static_cast<int64_t>(r));
            output_sizes.push_back(size);
        }
    }
    return nb::make_tuple(from_vector(indices, {indices.size()}),
                          from_vector(output_sizes, {output_sizes.size()}));
}

std::vector<int64_t> points_in_box(Input<double> corners, Input<double> points,
                                  Input<double> sizes) {
    if (corners.ndim() != 2 || corners.shape(1) != 2 || corners.shape(0) == 0 ||
        points.ndim() != 2 || points.shape(1) != 2)
        throw std::invalid_argument("corners and points must have shape (N, 2)");
    require_vector(sizes, points.shape(0), "sizes must contain one entry per point");
    double low[2]{at(corners, 0, 0), at(corners, 0, 1)};
    double high[2]{low[0], low[1]};
    for (size_t r = 1; r < corners.shape(0); ++r)
        for (size_t c = 0; c < 2; ++c) {
            const double value = at(corners, r, c);
            if (std::isnan(value)) { low[c] = high[c] = value; }
            else { low[c] = std::min(low[c], value); high[c] = std::max(high[c], value); }
        }
    std::vector<int64_t> result;
    {
        nb::gil_scoped_release release;
        for (size_t r = 0; r < points.shape(0); ++r) {
            const double half = at(sizes, r) * 0.5;
            // Preserve upstream's corner inclusion rule, including large points
            // that surround the whole box and have no corner inside it.
            bool inside[2]{};
            for (size_t c = 0; c < 2; ++c)
                for (int sign : {-1, 1}) {
                    const double value = at(points, r, c) + sign * half;
                    inside[c] = inside[c] || (value >= low[c] && value <= high[c]);
                }
            if (inside[0] && inside[1])
                result.push_back(static_cast<int64_t>(r));
        }
    }
    return result;
}

namespace {
nb::object numpy() { return nb::module_::import_("numpy"); }
nb::object all() {
    PyObject* result = PySlice_New(Py_None, Py_None, Py_None);
    if (!result) throw nb::python_error();
    return nb::steal<nb::object>(result);
}
nb::tuple empty_points() {
    return nb::make_tuple(numpy().attr("empty")(0, nb::arg("dtype") = "int"),
                         numpy().attr("empty")(0, nb::arg("dtype") = "float"));
}
bool kind(nb::handle value, const char* choices) {
    if (!nb::hasattr(value, "dtype")) return false;
    const std::string code = nb::cast<std::string>(value.attr("dtype").attr("kind"));
    return code.size() == 1 && std::strchr(choices, code[0]);
}
bool has_truth(nb::handle value) {
    const int result = PyObject_IsTrue(value.ptr());
    if (result < 0) throw nb::python_error();
    return result != 0;
}
nb::tuple point_data(nb::handle self, const std::vector<int64_t>& axes) {
    auto np = numpy();
    auto thick = self.attr("data_slice").attr("__getitem__")(nb::cast(axes)).attr("as_array")();
    nb::object point = thick[nb::int_(0)], left = thick[nb::int_(1)], right = thick[nb::int_(2)];
    nb::object data = self.attr("data"), sizes = self.attr("size"), shown = self.attr("shown");
    const std::string mode = nb::cast<std::string>(self.attr("projection_mode"));
    const int mode_id = mode == "none" ? 0 : mode == "rescale_linear" ? 2 : mode == "rescale_spherical" ? 3 : 1;
    bool unique_axes = true;
    nb::object data_shape = data.attr("shape");
    const size_t ndim = nb::cast<size_t>(data_shape[nb::int_(1)]);
    std::vector<bool> seen(ndim);
    for (auto axis : axes) {
        if (axis < 0 || static_cast<size_t>(axis) >= ndim || seen[static_cast<size_t>(axis)]) { unique_axes = false; break; }
        seen[static_cast<size_t>(axis)] = true;
    }
    const bool spec64 = nb::cast<std::string>(thick.attr("dtype").attr("name")) == "float64";
    const bool spherical64 = mode_id != 3 ||
        (nb::hasattr(sizes, "dtype") && nb::cast<std::string>(sizes.attr("dtype").attr("name")) == "float64");
    if (spec64 && spherical64 && unique_axes && kind(data, "biuf") && kind(sizes, "biuf") && kind(shown, "b")) {
        auto result = slice_points(
            nb::cast<Input<double>>(np.attr("asarray")(data, nb::arg("dtype") = "float64")),
            nb::cast<Input<double>>(np.attr("asarray")(sizes, nb::arg("dtype") = "float64")),
            nb::cast<Input<bool>>(shown), axes, nb::cast<Input<double>>(point),
            nb::cast<Input<double>>(left), nb::cast<Input<double>>(right), mode_id);
        nb::object indices = result[0];
        if ((mode_id == 0 || mode_id == 1) && nb::len(indices)) return nb::make_tuple(indices, sizes[indices]);
        return result;
    }
    // NumPy supplies dtype-specific primitives at the binding boundary.
    // Application decisions and projection orchestration remain in C++.
    nb::object low = mode_id == 0 ? point.attr("copy")() : np.attr("subtract")(point, left);
    nb::object high = mode_id == 0 ? point.attr("copy")() : np.attr("add")(point, right);
    auto thin = np.attr("isclose")(high, low);
    low[thin] = np.attr("subtract")(low[thin], 0.5);
    high[thin] = np.attr("add")(high[thin], 0.5);
    // Advanced assignment casts values; upstream's in-place subtraction must
    // instead reject integral slice specifications whenever a thin slice exists.
    if (kind(low, "biu") && has_truth(np.attr("any")(thin)))
        throw nb::type_error("Cannot cast ufunc 'subtract' output from float to integer with casting rule 'same_kind'");
    nb::object hidden_data = data[nb::make_tuple(all(), nb::cast(axes))];
    auto inside = np.attr("all")(np.attr("bitwise_and")(
        np.attr("greater_equal")(hidden_data, low), np.attr("less_equal")(hidden_data, high)), nb::arg("axis") = 1);
    nb::object visible = np.attr("where")(np.attr("bitwise_and")(inside, shown))[nb::int_(0)];
    if (!nb::len(visible)) return empty_points();
    nb::object size = sizes[visible];
    if (mode_id == 2 || mode_id == 3) {
        auto distance = np.attr("subtract")(hidden_data[visible], point);
        if (mode_id == 2) {
            auto end = np.attr("where")(np.attr("less")(distance, 0), np.attr("subtract")(low, point), np.attr("subtract")(high, point));
            auto scale = np.attr("prod")(np.attr("subtract")(1, np.attr("divide")(distance, end)), nb::arg("axis") = 1);
            size = np.attr("multiply")(size, scale);
        } else {
            auto radius = np.attr("divide")(size, 2);
            nb::object segment = np.attr("abs")(distance);
            auto valid = np.attr("all")(np.attr("less")(segment, np.attr("expand_dims")(radius, 1)), nb::arg("axis") = 1);
            segment = segment[valid];
            radius = radius[valid];
            auto portion = np.attr("prod")(np.attr("subtract")(1, np.attr("divide")(segment, np.attr("expand_dims")(radius, 1))), nb::arg("axis") = 1);
            segment = np.attr("multiply")(np.attr("subtract")(1, portion), radius);
            size = np.attr("multiply")(np.attr("sqrt")(np.attr("subtract")(
                np.attr("square")(radius), np.attr("square")(segment))), 2);
            visible = visible[valid];
        }
    }
    return nb::make_tuple(visible, size);
}
nb::tuple point_request(nb::handle self) {
    nb::object data = self.attr("data");
    if (!nb::len(data)) return empty_points();
    nb::object hidden = self.attr("slice_input").attr("not_displayed");
    if (!nb::len(hidden)) return nb::make_tuple(numpy().attr("arange")(nb::len(data), nb::arg("dtype") = "int"), self.attr("size"));
    return nb::cast<nb::tuple>(self.attr("_get_slice_data")(hidden));
}
} // namespace

void bind_points(nb::module_& m) {
    m.def("slice_points", &slice_points<float>, nb::arg("data").noconvert(), nb::arg("sizes").noconvert(),
          nb::arg("shown").noconvert(), nb::arg("axes"), nb::arg("point").noconvert(),
          nb::arg("left").noconvert(), nb::arg("right").noconvert(), nb::arg("mode"));
    m.def("slice_points", &slice_points<double>, nb::arg("data").noconvert(), nb::arg("sizes").noconvert(),
          nb::arg("shown").noconvert(), nb::arg("axes"), nb::arg("point").noconvert(),
          nb::arg("left").noconvert(), nb::arg("right").noconvert(), nb::arg("mode"));
    m.def("points_in_box", &points_in_box, nb::arg("corners").noconvert(),
          nb::arg("points").noconvert(), nb::arg("sizes").noconvert());
    m.def("point_slice_data", &point_data);
    m.def("point_slice_request", &point_request);
}
} // namespace nx
