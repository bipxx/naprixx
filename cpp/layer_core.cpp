#include "layer_core.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace nx::app {
namespace {
constexpr double radians = 3.14159265358979323846 / 180.0;
void square(const Matrix& value) {
    if (value.rows != value.cols) throw std::invalid_argument("matrix must be square");
}
double finite_default(double value, double fallback) {
    if (std::isnan(value)) return fallback;
    if (std::isinf(value)) return std::copysign(std::numeric_limits<double>::max(), value);
    return value;
}
size_t aligned_size(const std::vector<std::vector<double>>& values) {
    size_t size = 0;
    for (const auto& value : values) size = std::max(size, value.size());
    return size;
}
Matrix reversed(const Matrix& value) {
    Matrix result(value.rows, value.cols);
    for (size_t i = 0; i < value.rows; ++i)
        for (size_t j = 0; j < value.cols; ++j)
            result(i, j) = value(value.rows - 1 - i, value.cols - 1 - j);
    return result;
}
} // namespace

Matrix::Matrix(size_t r, size_t c) : rows(r), cols(c) {
    if (c && r > std::numeric_limits<size_t>::max() / c)
        throw std::overflow_error("matrix exceeds addressable memory");
    values.resize(r * c);
}
Matrix Matrix::identity(size_t size) {
    Matrix value(size, size);
    for (size_t i = 0; i < size; ++i) value(i, i) = 1;
    return value;
}
Matrix Matrix::transpose() const {
    Matrix value(cols, rows);
    for (size_t i = 0; i < rows; ++i)
        for (size_t j = 0; j < cols; ++j) value(j, i) = (*this)(i, j);
    return value;
}
Matrix multiply(const Matrix& left, const Matrix& right) {
    if (left.cols != right.rows) throw std::invalid_argument("matrix dimensions do not align");
    Matrix value(left.rows, right.cols);
    for (size_t i = 0; i < left.rows; ++i)
        for (size_t k = 0; k < left.cols; ++k)
            for (size_t j = 0; j < right.cols; ++j)
                value(i, j) += left(i, k) * right(k, j);
    return value;
}
Matrix inverse(const Matrix& matrix) {
    square(matrix);
    Matrix left = matrix, result = Matrix::identity(matrix.rows);
    for (size_t col = 0; col < matrix.cols; ++col) {
        size_t pivot = col;
        for (size_t row = col + 1; row < matrix.rows; ++row)
            if (std::abs(left(row, col)) > std::abs(left(pivot, col))) pivot = row;
        if (left(pivot, col) == 0) throw std::domain_error("Singular matrix");
        for (size_t j = 0; j < matrix.cols; ++j) {
            std::swap(left(col, j), left(pivot, j));
            std::swap(result(col, j), result(pivot, j));
        }
        const double divisor = left(col, col);
        for (size_t j = 0; j < matrix.cols; ++j) {
            left(col, j) /= divisor;
            result(col, j) /= divisor;
        }
        for (size_t row = 0; row < matrix.rows; ++row) {
            if (row == col) continue;
            const double factor = left(row, col);
            for (size_t j = 0; j < matrix.cols; ++j) {
                left(row, j) -= factor * left(col, j);
                result(row, j) -= factor * result(col, j);
            }
        }
    }
    return result;
}
Matrix embed(const Matrix& matrix, size_t ndim) {
    square(matrix);
    if (matrix.rows == ndim) return matrix;
    if (matrix.rows > ndim || matrix.rows == 0)
        throw std::invalid_argument("transform cannot be embedded in the requested dimensions");
    auto result = Matrix::identity(ndim);
    const auto offset = ndim - matrix.rows;
    for (size_t i = 0; i < matrix.rows; ++i)
        for (size_t j = 0; j < matrix.cols; ++j) result(i + offset, j + offset) = matrix(i, j);
    return result;
}
Matrix rotation_2d(double angle) {
    Matrix result(2, 2);
    const double c = std::cos(angle * radians), s = std::sin(angle * radians);
    result.values = {c, -s, s, c};
    return result;
}
Matrix rotation_3d(double alpha, double beta, double gamma) {
    auto a = Matrix::identity(3), b = a, c = a;
    const double ca = std::cos(alpha * radians), sa = std::sin(alpha * radians);
    const double cb = std::cos(beta * radians), sb = std::sin(beta * radians);
    const double cc = std::cos(gamma * radians), sc = std::sin(gamma * radians);
    a.values = {ca, -sa, 0, sa, ca, 0, 0, 0, 1};
    b.values = {cb, 0, sb, 0, 1, 0, -sb, 0, cb};
    c.values = {1, 0, 0, 0, cc, -sc, 0, sc, cc};
    return multiply(multiply(a, b), c);
}
Matrix expand_shear(const std::vector<double>& values) {
    const double size = (1 + std::sqrt(8.0 * values.size() + 1)) / 2;
    if (std::floor(size) != size)
        throw std::invalid_argument(std::to_string(values.size()) + " is a strange number of shear elements");
    auto result = Matrix::identity(static_cast<size_t>(size));
    size_t k = 0;
    for (size_t i = 0; i < result.rows; ++i)
        for (size_t j = i + 1; j < result.cols; ++j) result(i, j) = values[k++];
    return result;
}
bool triangular(const Matrix& matrix, bool upper) {
    for (size_t i = 0; i < matrix.rows; ++i)
        for (size_t j = 0; j < matrix.cols; ++j) {
            const double value = matrix(i, j);
            if (std::isnan(value)) return false;
            if ((upper ? i > j : i < j) && !(std::abs(value) <= 1e-8)) return false;
        }
    return true;
}
bool diagonal(const Matrix& matrix, double tolerance) {
    square(matrix);
    if (matrix.rows <= 1 && tolerance != 0)
        throw std::invalid_argument("zero-size array to reduction operation maximum which has no identity");
    bool has_nan = false;
    double maximum = 0;
    for (size_t i = 0; i < matrix.rows; ++i)
        for (size_t j = 0; j < matrix.cols; ++j) {
            if (i == j) continue;
            if (tolerance == 0 && matrix(i, j) != 0) return false;
            has_nan = has_nan || std::isnan(matrix(i, j));
            maximum = std::max(maximum, std::abs(matrix(i, j)));
        }
    return tolerance == 0 || (!has_nan && maximum <= tolerance);
}
Decomposition decompose(const Matrix& matrix, bool upper) {
    square(matrix);
    for (double value : matrix.values)
        if (!std::isfinite(value)) throw std::invalid_argument("array must not contain infs or NaNs");
    const size_t n = matrix.rows;
    Matrix r = upper ? matrix : reversed(matrix), q = Matrix::identity(n);
    // Householder QR; reversing both axes gives the corresponding lower factor.
    for (size_t k = 0; k + 1 < n; ++k) {
        std::vector<double> v(n - k);
        double norm = 0;
        for (size_t i = k; i < n; ++i) norm = std::hypot(norm, r(i, k));
        if (norm == 0) continue;
        const double alpha = -std::copysign(norm, r(k, k));
        for (size_t i = k; i < n; ++i) v[i - k] = r(i, k);
        v[0] -= alpha;
        double length = 0;
        for (double value : v) length = std::hypot(length, value);
        for (double& value : v) value /= length;
        for (size_t j = k; j < n; ++j) {
            double dot = 0;
            for (size_t i = k; i < n; ++i) dot += v[i - k] * r(i, j);
            for (size_t i = k; i < n; ++i) r(i, j) -= 2 * v[i - k] * dot;
        }
        for (size_t i = 0; i < n; ++i) {
            double dot = 0;
            for (size_t j = k; j < n; ++j) dot += q(i, j) * v[j - k];
            for (size_t j = k; j < n; ++j) q(i, j) -= 2 * dot * v[j - k];
        }
        for (size_t i = k + 1; i < n; ++i) r(i, k) = 0;
    }
    if (!upper) { r = reversed(r); q = reversed(q); }
    std::vector<double> scale(n);
    for (size_t i = 0; i < n; ++i) {
        scale[i] = std::abs(r(i, i));
        if (scale[i] == 0) throw std::domain_error("Singular matrix");
        const double sign = std::copysign(1.0, r(i, i));
        for (size_t j = 0; j < n; ++j) { r(i, j) *= sign; q(j, i) *= sign; }
    }
    for (size_t i = 0; i < n; ++i)
        for (size_t j = 0; j < n; ++j) r(i, j) /= scale[j];
    return {q, scale, r};
}
std::vector<double> trailing_vector(const std::optional<std::vector<double>>& values,
                                    size_t ndim, double pad) {
    std::vector<double> result(ndim, pad);
    if (!values) return result;
    if ((values->empty() && ndim) || values->size() > ndim)
        throw std::invalid_argument("could not broadcast vector into requested dimensions");
    std::copy(values->begin(), values->end(), result.end() - values->size());
    return result;
}
std::vector<Index> SlicePlan::axes(bool displayed) const {
    const Index n = static_cast<Index>(order.size());
    const Index split = ndisplay == 0 ? 0 : ndisplay > 0 ? std::max(Index{0}, n - ndisplay) :
        (ndisplay <= -n ? n : -ndisplay);
    const auto mid = order.begin() + split;
    return displayed ? std::vector<Index>(mid, order.end()) : std::vector<Index>(order.begin(), mid);
}
SlicePlan SlicePlan::resized(Index ndim) const {
    if (ndim < 0) throw std::invalid_argument("ndim must be nonnegative");
    auto result = *this;
    const Index old = static_cast<Index>(order.size());
    if (old > ndim) {
        if (ndim) result.order.erase(result.order.begin(), result.order.end() - ndim);
        result.order = reorder_after_reduction(result.order);
    } else if (old < ndim) {
        result.order.clear();
        for (Index i = 0; i < ndim - old; ++i) result.order.push_back(i);
        for (Index value : order) result.order.push_back(value + ndim - old);
    }
    return result;
}
bool SlicePlan::orthogonal(const Matrix& transform) const {
    square(transform);
    const size_t n = order.size();
    const auto matrix = embed(transform, n);
    const auto hidden = axes(false);
    for (Index row : axes(true)) {
        double sum = 0;
        for (Index col : hidden) sum += matrix(static_cast<size_t>(row), static_cast<size_t>(col));
        if (!(std::abs(sum) < 1e-8)) return false;
    }
    return true;
}
Matrix SlicePlan::data_slice(const Matrix& transform, const std::vector<double>& translate,
                             const Matrix& world) const {
    square(transform);
    if (world.rows != 3 || translate.size() != transform.rows)
        throw std::invalid_argument("slice coordinates and transform dimensions do not match");
    Matrix result(3, order.size());
    std::fill(result.values.begin(), result.values.end(), std::numeric_limits<double>::quiet_NaN());
    const auto hidden = axes(false);
    const bool is_diagonal = diagonal(transform, 1e-8);
    for (size_t row = 0; row < 3; ++row)
        for (Index axis : hidden) {
            double value = 0;
            for (Index other : hidden)
                value += (is_diagonal && axis != other ? 0 : transform(static_cast<size_t>(axis), static_cast<size_t>(other))) *
                         world(row, static_cast<size_t>(other));
            value += translate.at(static_cast<size_t>(axis));
            if (row != 0) value -= translate.at(static_cast<size_t>(axis));
            result(row, static_cast<size_t>(axis)) = finite_default(value, 0);
        }
    return result;
}
std::vector<Index> world_to_layer_axes(const std::vector<Index>& world, Index world_ndim,
                                      Index layer_ndim) {
    const Index offset = world_ndim - layer_ndim;
    std::vector<Index> result;
    if (offset < 0) for (Index i = 0; i < -offset; ++i) result.push_back(i);
    for (Index axis : world) if (offset <= 0 || axis >= offset) result.push_back(axis - offset);
    return result;
}
bool LayerLoadState::set_loaded(bool value) {
    const bool changed = loaded != value;
    loaded = value;
    return changed;
}
bool LayerLoadState::begin(Index id) { request_id = id; return set_loaded(false); }
bool LayerLoadState::finish(Index id) { return id == request_id && set_loaded(true); }
Matrix aggregate_extent(const std::vector<std::vector<double>>& minima,
                         const std::vector<std::vector<double>>& maxima) {
    const size_t n = std::max(aligned_size(minima), aligned_size(maxima));
    if (!n) throw std::invalid_argument("zero-size array to reduction operation fmin which has no identity");
    Matrix result(2, n);
    for (size_t axis = 0; axis < n; ++axis) {
        double lo = std::numeric_limits<double>::quiet_NaN(), hi = lo;
        for (const auto& values : minima) if (axis >= n - values.size()) {
            const double value = values[axis - (n - values.size())];
            if (!std::isnan(value) && (std::isnan(lo) || value < lo)) lo = value;
        }
        for (const auto& values : maxima) if (axis >= n - values.size()) {
            const double value = values[axis - (n - values.size())];
            if (!std::isnan(value) && (std::isnan(hi) || value > hi)) hi = value;
        }
        result(0, axis) = finite_default(lo, -0.5);
        result(1, axis) = finite_default(hi, 511.5);
    }
    return result;
}
std::vector<double> aggregate_steps(const std::vector<std::vector<double>>& scales) {
    const size_t n = aligned_size(scales);
    if (!n) throw std::invalid_argument("zero-size array to reduction operation fmin which has no identity");
    std::vector<double> result(n, std::numeric_limits<double>::quiet_NaN());
    for (const auto& scale : scales)
        for (size_t i = 0; i < scale.size(); ++i) {
            double& current = result[n - scale.size() + i];
            if (!std::isnan(scale[i]) && (std::isnan(current) || scale[i] < current)) current = scale[i];
        }
    return result;
}
std::vector<std::string> layer_axis_labels(Index ndim,
    const std::vector<std::vector<std::string>>& annotations) {
    if (ndim < 0) throw std::invalid_argument("ndim must be nonnegative");
    std::vector<std::string> result;
    for (Index i = -ndim; i < 0; ++i) result.push_back(std::to_string(i));
    const std::vector<std::string>* selected = nullptr;
    for (const auto& labels : annotations)
        if (!selected || labels.size() >= selected->size()) selected = &labels;
    if (selected) {
        if (selected->size() > result.size()) throw std::invalid_argument("too many axis labels");
        std::copy(selected->begin(), selected->end(), result.end() - selected->size());
    }
    return result;
}
} // namespace nx::app
