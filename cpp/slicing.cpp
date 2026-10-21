#include "slicing.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace nx::app {
namespace {
double rounded(double value) {
    if (!std::isfinite(value)) return value;
    const double base = std::floor(value), fraction = value - base;
    if (fraction < 0.5) return base;
    if (fraction > 0.5) return base + 1;
    return std::fmod(base, 2) == 0 ? base : base + 1;
}
Index integer(double value) {
    if (std::isnan(value)) throw std::invalid_argument("cannot convert float NaN to integer");
    if (!std::isfinite(value) || value >= -static_cast<double>(std::numeric_limits<Index>::min()) ||
        value < static_cast<double>(std::numeric_limits<Index>::min()))
        throw std::overflow_error("slice index exceeds the native 64-bit range");
    return static_cast<Index>(value);
}
bool close(double a, double b) {
    return a == b || (std::isfinite(a) && std::isfinite(b) && std::abs(a - b) <= 1e-8 + 1e-5 * std::abs(b));
}
void validate(const Matrix& slice, size_t ndim) {
    if (slice.rows != 3 || slice.cols != ndim)
        throw std::invalid_argument("thick slice must have shape (3, ndim)");
}
size_t axis(Index value, size_t ndim) {
    if (value < 0 || static_cast<size_t>(value) >= ndim)
        throw std::out_of_range("slice axis is out of bounds");
    return static_cast<size_t>(value);
}
} // namespace

Selections point_selections(const std::vector<double>& point) {
    Selections result(point.size());
    for (size_t i = 0; i < point.size(); ++i) if (!std::isnan(point[i]))
        result[i] = {AxisSelection::Kind::index, integer(rounded(point[i])), 0};
    return result;
}
Selections thick_selections(const Matrix& slice, const std::vector<Index>& displayed) {
    validate(slice, slice.cols);
    std::vector<bool> visible(slice.cols);
    for (Index value : displayed) visible[axis(value, slice.cols)] = true;
    Selections result(slice.cols);
    for (size_t i = 0; i < slice.cols; ++i) {
        if (visible[i]) continue;
        const double high_value = slice(0, i) + slice(2, i);
        const Index low = std::max(integer(rounded(slice(0, i) - slice(1, i))), Index{0});
        Index high = std::max(integer(rounded(high_value)), Index{0});
        if (close(static_cast<double>(high), high_value) || low == high) {
            if (high == std::numeric_limits<Index>::max()) throw std::overflow_error("slice stop exceeds the native 64-bit range");
            ++high;
        }
        result[i] = {AxisSelection::Kind::range, low, high};
    }
    return result;
}
bool slice_out_of_bounds(const Matrix& slice, const std::vector<Index>& hidden,
                         const std::vector<double>& shape, bool project) {
    validate(slice, shape.size());
    for (Index value : hidden) {
        const size_t i = axis(value, shape.size());
        const double low = rounded(slice(0, i) - (project ? slice(1, i) : 0));
        const double high = rounded(slice(0, i) + (project ? slice(2, i) : 0));
        if (high < 0 || low > shape[i] - 1) return true;
    }
    return false;
}
Matrix slice_at_level(const Matrix& slice, const std::vector<double>& factors,
                       const std::vector<double>& shape) {
    validate(slice, factors.size());
    if (shape.size() != factors.size()) throw std::invalid_argument("level shape must match downsampling factors");
    Matrix result = slice;
    for (size_t i = 0; i < slice.cols; ++i) {
        for (size_t row = 0; row < 3; ++row) result(row, i) /= factors[i];
        if (!std::isnan(result(0, i))) result(0, i) = std::min(std::max(result(0, i), 0.0), shape[i] - 1);
    }
    return result;
}
std::vector<Index> image_axis_order(const std::vector<Index>& displayed, bool rgb) {
    auto order = reorder_after_reduction(displayed);
    if (rgb) {
        if (order.empty()) throw std::invalid_argument("max() arg is an empty sequence");
        order.push_back(*std::max_element(order.begin(), order.end()) + 1);
    }
    return order;
}
TilePlan image_tile_plan(const SlicePlan& slice, size_t data_ndim,
                         const Matrix& corners, const std::vector<double>& factors) {
    const size_t ndim = slice.order.size();
    if (corners.rows != 2 || corners.cols != ndim || factors.size() != ndim)
        throw std::invalid_argument("tile corners and factors must match slice dimensions");
    TilePlan result{std::vector<double>(ndim, 1), std::vector<double>(ndim), Selections(data_ndim)};
    for (Index value : slice.axes(true)) {
        const size_t i = axis(value, ndim);
        result.scale[i] = factors[i];
        if (slice.ndisplay == 2) {
            if (i >= data_ndim) throw std::out_of_range("displayed dimension exceeds data dimensions");
            const Index stop = integer(corners(1, i));
            if (stop == std::numeric_limits<Index>::max()) throw std::overflow_error("tile stop exceeds the native 64-bit range");
            result.selections[i] = {AxisSelection::Kind::range, integer(corners(0, i)), stop + 1};
        }
    }
    if (slice.ndisplay == 2)
        for (size_t i = 0; i < ndim; ++i) result.translate[i] = corners(0, i) * result.scale[i];
    return result;
}
VisibleVectors slice_vectors(const Matrix& origins, const Matrix& slice,
                              const std::vector<Index>& hidden, bool project, bool fade) {
    validate(slice, origins.cols);
    std::vector<double> low(hidden.size()), high(hidden.size());
    for (size_t j = 0; j < hidden.size(); ++j) {
        const size_t i = axis(hidden[j], origins.cols);
        low[j] = slice(0, i) - (project ? slice(1, i) : 0);
        high[j] = slice(0, i) + (project ? slice(2, i) : 0);
        if (close(high[j], low[j])) { low[j] -= 0.5; high[j] += 0.5; }
    }
    VisibleVectors result;
    for (size_t row = 0; row < origins.rows; ++row) {
        bool visible = true;
        double alpha = 1;
        for (size_t j = 0; j < hidden.size(); ++j) {
            const size_t i = static_cast<size_t>(hidden[j]);
            const double value = origins(row, i);
            if (!(value >= low[j] && value <= high[j])) { visible = false; break; }
            if (fade) {
                const double distance = value - slice(0, i);
                const double end = (distance < 0 ? low[j] : high[j]) - slice(0, i);
                alpha *= 1 - distance / end;
            }
        }
        if (visible) { result.indices.push_back(static_cast<Index>(row)); result.alphas.push_back(alpha); }
    }
    return result;
}
std::vector<Index> reindex_faces(const std::vector<Index>& faces, size_t width,
    size_t vertex_count, const std::vector<Index>& selected) {
    if (!width || faces.size() % width) throw std::invalid_argument("faces must form a rectangular array");
    std::vector<Index> mapping(vertex_count, -1);
    for (size_t i = 0; i < selected.size(); ++i) mapping.at(axis(selected[i], vertex_count)) = static_cast<Index>(i);
    std::vector<Index> result;
    for (size_t i = 0; i < faces.size(); i += width) {
        bool valid = true;
        for (size_t j = 0; j < width; ++j) {
            const Index index = DimsState::axis_in_bounds(faces[i + j], static_cast<Index>(vertex_count));
            if (mapping.at(static_cast<size_t>(index)) < 0) valid = false;
        }
        if (valid) for (size_t j = 0; j < width; ++j) {
            const Index index = DimsState::axis_in_bounds(faces[i + j], static_cast<Index>(vertex_count));
            result.push_back(mapping.at(static_cast<size_t>(index)));
        }
    }
    return result;
}
} // namespace nx::app
