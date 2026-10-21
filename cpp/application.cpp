#include "application.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>

namespace nx::app {
namespace {
constexpr double pi = 3.14159265358979323846;

template <typename T>
void ensure_length(std::vector<T>& values, Index size, const T& pad) {
    if (size < 0) throw std::invalid_argument("dimensionality must be nonnegative");
    const auto length = static_cast<size_t>(size);
    if (values.size() < length)
        values.insert(values.begin(), length - values.size(), pad);
    else if (length && values.size() > length)
        values.erase(values.begin(), values.end() - static_cast<std::ptrdiff_t>(length));
    // Python's [-0:] preserves the tuple, including the original zero-dim case.
}

Index integer(double value, bool round = false) {
    if (std::isnan(value)) throw std::invalid_argument("cannot convert NaN to integer");
    if (!std::isfinite(value) || value < -9223372036854775808.0 ||
        value >= 9223372036854775808.0)
        throw std::overflow_error("dimension step is outside the supported integer range");
    if (!round) return static_cast<Index>(value);
    const double lower = std::floor(value), fraction = value - lower;
    return static_cast<Index>(fraction < 0.5 ? lower : fraction > 0.5 ? lower + 1 :
                              std::fmod(lower, 2) == 0 ? lower : lower + 1);
}

std::string order_string(const std::vector<Index>& order) {
    std::ostringstream out;
    out << '(';
    for (size_t i = 0; i < order.size(); ++i) {
        if (i) out << ", ";
        out << order[i];
    }
    if (order.size() == 1) out << ',';
    out << ')';
    return out.str();
}

void same_length(size_t first, size_t second) {
    if (first != second)
        throw std::invalid_argument("axis and value sequences must have equal length");
}

double dot(const Vec3& a, const Vec3& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0]};
}
Vec3 normalized(Vec3 a) {
    const double norm = std::sqrt(dot(a, a));
    if (!std::isfinite(norm) || norm == 0)
        throw std::invalid_argument("camera directions must be finite, nonzero and nonparallel");
    for (auto& x : a) x /= norm;
    return a;
}
} // namespace

void DimsState::validate_ranges(const std::vector<Range>& ranges) {
    for (size_t i = 0; i < ranges.size(); ++i) {
        const auto& r = ranges[i];
        if (r[0] > r[1])
            throw std::invalid_argument("start and stop must be strictly increasing for axis " +
                                        std::to_string(i));
        if (r[2] <= 0)
            throw std::invalid_argument("step must be strictly positive for axis " +
                                        std::to_string(i));
    }
}

std::vector<Index> reorder_after_reduction(const std::vector<Index>& order) {
    auto sorted = order;
    std::sort(sorted.begin(), sorted.end());
    std::vector<Index> result;
    result.reserve(order.size());
    for (auto value : order)
        result.push_back(std::lower_bound(sorted.begin(), sorted.end(), value) - sorted.begin());
    return result;
}

void DimsState::normalize() {
    normalize_primary();
    validate_order();
    normalize_labels();
    normalize_last_used();
}

void DimsState::normalize_primary() {
    if (ndisplay != 2 && ndisplay != 3)
        throw std::invalid_argument("ndisplay must be 2 or 3");
    normalize_ranges();
    normalize_point();
    normalize_margins(true);
    normalize_margins(false);
    normalize_order();
}

void DimsState::normalize_ranges() {
    validate_ranges(ranges);
    ensure_length(ranges, ndim, Range{0, 2, 1});
}

void DimsState::normalize_point() {
    ensure_length(point, ndim, 0.0);
    for (size_t i = 0; i < std::min(point.size(), ranges.size()); ++i) {
        if (!std::isnan(point[i])) {
            if (std::isnan(ranges[i][0]) || std::isnan(ranges[i][1]))
                point[i] = std::numeric_limits<double>::quiet_NaN();
            else point[i] = std::clamp(point[i], ranges[i][0], ranges[i][1]);
        }
    }
}

void DimsState::normalize_margins(bool left) {
    ensure_length(left ? margin_left : margin_right, ndim, 0.0);
}

void DimsState::normalize_order() {
    if (static_cast<Index>(order.size()) < ndim) {
        const Index offset = ndim - static_cast<Index>(order.size());
        for (auto& value : order) value += offset;
        std::vector<Index> prefix(static_cast<size_t>(offset));
        std::iota(prefix.begin(), prefix.end(), Index{0});
        order.insert(order.begin(), prefix.begin(), prefix.end());
    } else if (static_cast<Index>(order.size()) > ndim) {
        if (ndim > 0) order.erase(order.begin(), order.end() - ndim);
        order = reorder_after_reduction(order);
    }
}

void DimsState::validate_order() const {
    std::set<Index> actual(order.begin(), order.end());
    if (static_cast<Index>(actual.size()) != ndim ||
        (!actual.empty() && (*actual.begin() != 0 || *actual.rbegin() != ndim - 1)))
        throw std::invalid_argument("Invalid ordering " + order_string(order) + " for " +
                                    std::to_string(ndim) + " dimensions");
}

void DimsState::normalize_labels() {
    if (static_cast<Index>(axis_labels.size()) < ndim) {
        std::vector<std::string> prefix;
        for (Index i = -ndim; i < -static_cast<Index>(axis_labels.size()); ++i)
            prefix.push_back(std::to_string(i));
        axis_labels.insert(axis_labels.begin(), prefix.begin(), prefix.end());
    } else if (ndim && static_cast<Index>(axis_labels.size()) > ndim)
        axis_labels.erase(axis_labels.begin(), axis_labels.end() - ndim);
    ensure_length(rollable, ndim, true);
}

void DimsState::normalize_last_used() {
    const auto steps = nsteps();
    std::vector<Index> sliders;
    for (auto axis : displayed(false))
        if (axis >= 0 && static_cast<size_t>(axis) < steps.size() && steps[axis] > 1)
            sliders.push_back(axis);
    if (!sliders.empty() && std::find(sliders.begin(), sliders.end(), last_used) == sliders.end())
        last_used = sliders[0];
}

Index DimsState::axis_in_bounds(Index axis, Index ndim) {
    if (axis < -ndim || axis >= ndim || ndim <= 0)
        throw std::invalid_argument("Axis " + std::to_string(axis) +
            " not defined for dimensionality " + std::to_string(ndim) +
            ". Must be in [" + std::to_string(-ndim) + ", " + std::to_string(ndim) + ").");
    return axis < 0 ? axis + ndim : axis;
}

std::vector<Index> DimsState::nsteps() const {
    std::vector<Index> result;
    for (const auto& r : ranges) {
        const auto n = integer((r[1] - r[0]) / (r[2] != 0 ? r[2] : 1));
        if (n == std::numeric_limits<Index>::max())
            throw std::overflow_error("dimension step is outside the supported integer range");
        result.push_back(n + 1);
    }
    return result;
}
std::vector<Index> DimsState::current_step() const {
    std::vector<Index> result;
    for (size_t i = 0; i < std::min(point.size(), ranges.size()); ++i)
        result.push_back(integer((point[i] - ranges[i][0]) /
                                (ranges[i][2] != 0 ? ranges[i][2] : 1), true));
    return result;
}
std::vector<Index> DimsState::margin_steps(bool left) const {
    const auto& values = left ? margin_left : margin_right;
    std::vector<Index> result;
    for (size_t i = 0; i < std::min(values.size(), ranges.size()); ++i)
        result.push_back(integer(values[i] / (ranges[i][2] != 0 ? ranges[i][2] : 1), true));
    return result;
}
std::vector<double> DimsState::thickness() const {
    std::vector<double> result;
    for (size_t i = 0; i < std::min(margin_left.size(), margin_right.size()); ++i)
        result.push_back(margin_left[i] + margin_right[i]);
    return result;
}
std::vector<Index> DimsState::thickness_steps() const {
    auto left = margin_steps(true), right = margin_steps(false);
    for (size_t i = 0; i < std::min(left.size(), right.size()); ++i) {
        if ((right[i] > 0 && left[i] > std::numeric_limits<Index>::max() - right[i]) ||
            (right[i] < 0 && left[i] < std::numeric_limits<Index>::min() - right[i]))
            throw std::overflow_error("slice thickness is outside the supported integer range");
        left[i] += right[i];
    }
    left.resize(std::min(left.size(), right.size()));
    return left;
}
std::vector<Index> DimsState::displayed(bool visible) const {
    const auto count = std::min(order.size(), static_cast<size_t>(ndisplay));
    const auto split = order.end() - static_cast<std::ptrdiff_t>(count);
    return visible ? std::vector<Index>(split, order.end()) : std::vector<Index>(order.begin(), split);
}
std::vector<Index> DimsState::displayed_order() const {
    const auto axes = displayed();
    std::vector<Index> result(axes.size());
    std::iota(result.begin(), result.end(), Index{0});
    std::stable_sort(result.begin(), result.end(), [&](auto a, auto b) { return axes[a] < axes[b]; });
    return result;
}
std::vector<double> DimsState::point_from_steps(const std::vector<double>& steps) const {
    std::vector<double> result;
    for (size_t i = 0; i < std::min(steps.size(), ranges.size()); ++i)
        result.push_back(ranges[i][0] + steps[i] * (ranges[i][2] != 0 ? ranges[i][2] : 1));
    return result;
}
std::vector<double> DimsState::margin_from_steps(const std::vector<double>& steps) const {
    std::vector<double> result;
    for (size_t i = 0; i < std::min(steps.size(), ranges.size()); ++i)
        result.push_back(steps[i] * (ranges[i][2] != 0 ? ranges[i][2] : 1));
    return result;
}
std::vector<Range> DimsState::ranges_from_nsteps(const std::vector<double>& steps) const {
    std::vector<Range> result;
    for (size_t i = 0; i < std::min(steps.size(), ranges.size()); ++i) {
        if (steps[i] == 1) throw std::domain_error("division by zero");
        result.push_back({ranges[i][0], ranges[i][1], (ranges[i][1] - ranges[i][0]) / (steps[i] - 1)});
    }
    return result;
}
std::vector<double> DimsState::point_on_axes(const std::vector<Index>& axes,
                                           const std::vector<double>& values, bool steps) const {
    same_length(axes.size(), values.size());
    auto result = point;
    for (size_t i = 0; i < axes.size(); ++i) {
        const auto axis = axis_in_bounds(axes[i], ndim);
        result.at(axis) = steps ? ranges.at(axis)[0] + values[i] * ranges.at(axis)[2] : values[i];
    }
    return result;
}
std::vector<Range> DimsState::ranges_on_axes(const std::vector<Index>& axes,
                                          const std::vector<Range>& values) const {
    same_length(axes.size(), values.size());
    auto result = ranges;
    for (size_t i = 0; i < axes.size(); ++i) result.at(axis_in_bounds(axes[i], ndim)) = values[i];
    return result;
}
std::vector<std::string> DimsState::labels_on_axes(const std::vector<Index>& axes,
                                                const std::vector<std::string>& values) const {
    same_length(axes.size(), values.size());
    auto result = axis_labels;
    for (size_t i = 0; i < axes.size(); ++i) result.at(axis_in_bounds(axes[i], ndim)) = values[i];
    return result;
}
std::vector<Index> DimsState::transposed_order() const {
    auto result = order;
    if (result.size() < 2) throw std::out_of_range("list index out of range");
    std::swap(result[result.size() - 1], result[result.size() - 2]);
    return result;
}
std::vector<Index> DimsState::rolled_order() const {
    auto result = order;
    const auto steps = nsteps();
    std::vector<size_t> positions;
    for (size_t i = 0; i < result.size(); ++i)
        if (rollable.at(result[i]) && steps.at(result[i]) > 1) positions.push_back(i);
    if (!positions.empty()) {
        auto carry = result[positions.back()];
        for (auto i : positions) std::swap(carry, result[i]);
    }
    return result;
}
Index DimsState::focused_axis(int direction) const {
    const auto steps = nsteps();
    std::vector<Index> sliders;
    for (auto axis : displayed(false)) if (steps.at(axis) > 1) sliders.push_back(axis);
    if (sliders.empty()) return last_used;
    const auto current = std::find(sliders.begin(), sliders.end(), last_used);
    if (current == sliders.end()) throw std::invalid_argument("last_used is not a visible slider");
    const auto count = static_cast<Index>(sliders.size());
    return sliders[(current - sliders.begin() + direction + count) % count];
}
std::vector<Index> DimsState::center_steps() const {
    auto result = nsteps();
    for (auto& x : result) x = (x - 1) / 2;
    return result;
}

std::pair<Vec3, Vec3> CameraState::directions() const {
    const double x = -angles[0] * pi / 180, y = -angles[1] * pi / 180,
                 z = -angles[2] * pi / 180;
    const double cx = std::cos(x), sx = std::sin(x), cy = std::cos(y),
                 sy = std::sin(y), cz = std::cos(z), sz = std::sin(z);
    const double depth = orientation[0] == "towards" ? -1 : 1;
    const double vertical = orientation[1] == "down" ? -1 : 1;
    return {{depth * cy * cz, depth * (cx * sz + sx * sy * cz),
             depth * (sx * sz - cx * sy * cz)},
            {vertical * -cy * sz, vertical * (cx * cz - sx * sy * sz),
             vertical * (sx * cz + cx * sy * sz)}};
}
Vec3 CameraState::angles_for_directions(const Vec3& view_raw, const Vec3& up_raw) const {
    auto view = normalized(view_raw);
    const auto parallel = dot(up_raw, view);
    Vec3 up;
    for (size_t i = 0; i < 3; ++i) up[i] = up_raw[i] - parallel * view[i];
    up = normalized(up);
    const auto side = cross(view, up);
    const double depth = orientation[0] == "towards" ? -1 : 1,
                 vertical = orientation[1] == "down" ? -1 : 1;
    double rotation[3][3];
    for (size_t i = 0; i < 3; ++i) {
        rotation[i][0] = depth * view[i];
        rotation[i][1] = vertical * up[i];
        rotation[i][2] = depth * vertical * side[i];
    }
    const double y = std::asin(std::clamp(rotation[0][2], -1.0, 1.0));
    double x, z;
    if (std::abs(std::cos(y)) <= 1e-7) {
        x = std::atan2(rotation[2][1], rotation[1][1]);
        z = 0;
    } else {
        x = std::atan2(-rotation[1][2], rotation[2][2]);
        z = std::atan2(-rotation[0][1], rotation[0][0]);
    }
    return {-x * 180 / pi, -y * 180 / pi, -z * 180 / pi};
}
std::vector<double> CameraState::embed_direction(Index ndim, const std::vector<Index>& axes,
                                                bool up) const {
    if (ndim < 0) throw std::invalid_argument("negative dimensions are not allowed");
    if (axes.size() != 3) throw std::invalid_argument("embedding requires three displayed axes");
    std::vector<double> result(static_cast<size_t>(ndim), 0);
    const auto vectors = directions();
    const auto& vector = up ? vectors.second : vectors.first;
    for (size_t i = 0; i < 3; ++i) result.at(DimsState::axis_in_bounds(axes[i], ndim)) = vector[i];
    return result;
}
bool CameraState::left_handed() const {
    return ((orientation[0] != "towards") + (orientation[1] != "down") +
            (orientation[2] != "right")) % 2 != 0;
}
void CameraState::cache(Index mode) {
    (mode == 2 ? cached_2d : cached_3d) = CameraSnapshot{center, angles, zoom};
}
std::optional<CameraSnapshot> CameraState::pop_cache(Index mode) {
    auto& slot = mode == 2 ? cached_2d : cached_3d;
    auto result = slot;
    slot.reset();
    return result;
}
CameraTransition CameraState::switch_mode(Index mode, const DimsState& dims) {
    CameraTransition result;
    if (synced) {
        auto new_center = center;
        if (dims.order.size() >= 3) {
            const auto axis = dims.order[dims.order.size() - 3];
            if (mode == 3) new_center[0] = dims.point.at(axis);
            else {
                result.point_axis = axis;
                result.point_value = new_center[0];
                new_center[0] = 0;
            }
        } else if (mode == 2) new_center[0] = 0;
        result.center = new_center;
    } else if (auto state = pop_cache(mode)) {
        result.center = state->center;
        result.angles = state->angles;
        result.zoom = state->zoom;
    } else result.fit = true;
    return result;
}

std::vector<Index> GridState::effective_indices(const std::vector<bool>& visible) const {
    std::vector<Index> result;
    for (size_t i = 0; i < visible.size(); ++i) if (visible[i]) result.push_back(static_cast<Index>(i));
    return result;
}
GridLayout GridState::layout(const std::vector<bool>& visible) const {
    if (stride == 0 && wide_stride.empty() && !visible.empty())
        throw std::invalid_argument("grid stride must be nonzero and representable");
    GridLayout result{enabled, visible, {1, 1}, {}, {}, {}};
    const auto width = stride < 0 ? static_cast<std::uint64_t>(-(stride + 1)) + 1
                                 : static_cast<std::uint64_t>(stride);
    const auto count = static_cast<Index>(visible.size());
    std::set<Index> occupied;
    for (Index i = 0; i < count; ++i) {
        const auto group = wide_stride.empty()
            ? static_cast<Index>(static_cast<std::uint64_t>(stride > 0 ? i : count - 1 - i) / width)
            : Index{0};
        result.group_of.push_back(group);
        if (visible[i]) occupied.insert(group);
    }
    result.occupied.assign(occupied.begin(), occupied.end());
    if (enabled && !occupied.empty()) {
        if (!wide_shape[0].empty() || !wide_shape[1].empty())
            throw std::overflow_error("grid core shape exceeds Index range");
        const auto cells = static_cast<Index>(occupied.size());
        auto rows = shape[0], cols = shape[1];
        if (rows == -1 && cols == -1) {
            cols = static_cast<Index>(std::ceil(std::sqrt(static_cast<double>(cells))));
            rows = 1 + (cells - 1) / cols;
        } else if (rows == -1) {
            if (cols <= 0) throw std::invalid_argument("grid columns must be positive or -1");
            rows = 1 + (cells - 1) / cols;
        } else if (cols == -1) {
            if (rows <= 0) throw std::invalid_argument("grid rows must be positive or -1");
            cols = 1 + (cells - 1) / rows;
        }
        result.shape = {std::max(Index{1}, rows), std::max(Index{1}, cols)};
    }
    if (result.shape[0] > std::numeric_limits<Index>::max() / result.shape[1])
        throw std::overflow_error("grid shape is too large");
    for (Index i = 0; i < count; ++i) {
        if (!enabled) result.positions.push_back({0, 0});
        else if (!visible[i]) result.positions.push_back({-1, -1});
        else {
            const auto group = result.group_of[i];
            const Index cell = (std::lower_bound(result.occupied.begin(), result.occupied.end(), group) -
                                result.occupied.begin()) % (result.shape[0] * result.shape[1]);
            result.positions.push_back({cell / result.shape[1], cell % result.shape[1]});
        }
    }
    return result;
}
Cell GridLayout::position(Index index) const {
    if (!enabled || visible.empty()) return {0, 0};
    if (index < 0 || static_cast<size_t>(index) >= positions.size())
        throw std::invalid_argument("Index " + std::to_string(index) +
            " is out of bounds for number of layers " + std::to_string(positions.size()) + ".");
    return positions[index];
}
std::vector<Index> GridLayout::contents(Cell cell) const {
    std::vector<Index> result;
    for (size_t i = 0; i < positions.size(); ++i)
        if (positions[i] == cell) result.push_back(static_cast<Index>(i));
    return result;
}
std::vector<std::pair<Cell, std::vector<Index>>> GridLayout::viewboxes() const {
    std::vector<std::pair<Cell, std::vector<Index>>> result;
    for (Index row = 0; row < shape[0]; ++row)
        for (Index col = 0; col < shape[1]; ++col) result.push_back({{row, col}, contents({row, col})});
    return result;
}
Index GridState::canvas_spacing(Cell canvas, const GridLayout& layout, bool safe) const {
    const auto rows = layout.shape[0], cols = layout.shape[1];
    const double raw = spacing >= 1 ? spacing :
        spacing * (static_cast<double>(canvas[0]) / cols + static_cast<double>(canvas[1]) / rows) / 2;
    const auto pixels = integer(raw);
    if (!safe) return pixels;
    const double max_x = (static_cast<double>(canvas[0]) - static_cast<double>(cols) * 20) /
                         std::max(Index{1}, cols - 1);
    const double max_y = (static_cast<double>(canvas[1]) - static_cast<double>(rows) * 20) /
                         std::max(Index{1}, rows - 1);
    return std::min(pixels, std::max(Index{0}, integer(std::min(max_x, max_y))));
}

std::array<double, 2> GridState::viewbox_size(Cell canvas, const GridLayout& layout) const {
    std::array<double, 2> result{static_cast<double>(canvas[0]), static_cast<double>(canvas[1])};
    if (enabled) {
        const double gap = static_cast<double>(canvas_spacing(canvas, layout));
        for (size_t i = 0; i < 2; ++i)
            result[i] = (result[i] - gap * (layout.shape[i] - 1)) / layout.shape[i];
    }
    return result;
}

std::vector<double> view_center(const std::vector<double>& corner,
                                const std::vector<double>& size, Index ndisplay) {
    if (corner.size() != size.size()) throw std::invalid_argument("scene corner and size must match");
    if (ndisplay != 2 && ndisplay != 3) throw std::invalid_argument("ndisplay must be 2 or 3");
    std::vector<double> result(static_cast<size_t>(ndisplay), 0);
    const auto count = std::min(result.size(), size.size());
    for (size_t i = 0; i < count; ++i)
        result[result.size() - count + i] = corner[corner.size() - count + i] + size[size.size() - count + i] / 2;
    return result;
}
double scale_factor(double margin) {
    if (!(margin >= 0 && margin < 1))
        throw std::invalid_argument("margin must be between 0 and 1");
    return 1 - margin;
}
std::array<double, 2> bounding_box(const std::vector<double>& size, const Vec3& view, const Vec3& up) {
    if (size.size() != 2 && size.size() != 3)
        throw std::invalid_argument("bounding box requires two or three scene dimensions");
    const Vec3 extent = size.size() == 2 ? Vec3{1e-10, size[0], size[1]} : Vec3{size[0], size[1], size[2]};
    const auto right = cross(view, up);
    std::array<double, 2> result{};
    for (size_t i = 0; i < 3; ++i) {
        result[0] += std::abs(up[i]) * extent[i];
        result[1] += std::abs(right[i]) * extent[i];
    }
    return result;
}
double zoom_2d(const std::vector<double>& size, std::array<double, 2> canvas, double scale) {
    if (size.empty()) throw std::invalid_argument("scene must have at least one dimension");
    double factors[2];
    for (size_t i = 0; i < 2; ++i) {
        double extent = size.size() == 1 ? size[0] : size[size.size() - 2 + i];
        if (std::abs(extent) <= 1e-8) extent = 1;
        factors[i] = canvas[i] / extent;
    }
    if (std::isnan(factors[0]) || std::isnan(factors[1])) return std::numeric_limits<double>::quiet_NaN();
    return scale * std::min(factors[0], factors[1]);
}
double zoom_3d(const std::vector<double>& size, const Vec3& view, const Vec3& up,
               std::array<double, 2> canvas, double scale) {
    const auto box = bounding_box(size, view, up);
    const double first = canvas[0] / box[0], second = canvas[1] / box[1];
    if (std::isnan(first) || std::isnan(second)) return std::numeric_limits<double>::quiet_NaN();
    return scale * std::min(first, second);
}
ViewFit fit_view(const std::vector<double>& corner, const std::vector<double>& size,
                 Index ndisplay, const Vec3& view, const Vec3& up,
                 std::array<double, 2> canvas, std::array<double, 2> whole_canvas,
                 double margin) {
    auto center = view_center(corner, size, ndisplay);
    const double scale = scale_factor(margin);
    if (size.empty()) throw std::invalid_argument("scene must have at least one dimension");
    const bool zero = std::all_of(size.begin(), size.end(), [](auto x) { return x == 0; });
    const double zoom = zero ? scale * std::min(whole_canvas[0], whole_canvas[1]) :
        ndisplay == 2 ? zoom_2d(size, canvas, scale) : zoom_3d(size, view, up, canvas, scale);
    return {std::move(center), zoom};
}
} // namespace nx::app
