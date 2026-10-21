#pragma once

// Application state and decisions: this library has no Python/Qt dependencies.
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace nx::app {
using Index = std::int64_t;
using Range = std::array<double, 3>;
using Vec3 = std::array<double, 3>;
using Cell = std::array<Index, 2>;

enum class DimsField : unsigned {
    Ndim, Ndisplay, Order, AxisLabels, Rollable, Ranges,
    MarginLeft, MarginRight, Point, LastUsed
};

enum class DimsFlag : unsigned { PlayReady, ScrollProgress, Validating };

// Primitive application flags live here. Non-primitive plugin values remain
// at the interpreter boundary; this core records their selection only.
struct DimsFlagValue {
    enum class Kind { Boolean, Integer, Real, Provider };
    Kind kind = Kind::Provider;
    bool present = true, boolean = false;
    Index integer = 0;
    double real = 0;

    static DimsFlagValue from_bool(bool value) {
        DimsFlagValue result; result.kind = Kind::Boolean; result.boolean = value;
        return result;
    }
    static DimsFlagValue from_integer(Index value) {
        DimsFlagValue result; result.kind = Kind::Integer; result.integer = value;
        return result;
    }
    static DimsFlagValue from_real(double value) {
        DimsFlagValue result; result.kind = Kind::Real; result.real = value;
        return result;
    }
    std::optional<bool> truth() const noexcept {
        if (!present || kind == Kind::Provider) return std::nullopt;
        if (kind == Kind::Boolean) return boolean;
        if (kind == Kind::Integer) return integer != 0;
        return real != 0;
    }
};

struct DimsFlags {
    std::array<DimsFlagValue, 3> values{
        DimsFlagValue::from_bool(true), DimsFlagValue::from_integer(0),
        DimsFlagValue::from_bool(false)
    };
    DimsFlagValue& value(DimsFlag flag) noexcept { return values[static_cast<unsigned>(flag)]; }
    const DimsFlagValue& value(DimsFlag flag) const noexcept { return values[static_cast<unsigned>(flag)]; }
};

struct DimsState {
    Index ndim = 2;
    Index ndisplay = 2;
    std::vector<Range> ranges;
    std::vector<double> point, margin_left, margin_right;
    std::vector<Index> order;
    std::vector<std::string> axis_labels;
    std::vector<bool> rollable;
    Index last_used = 0;
    // Schema-bypassing Python inputs are provider values at the binding boundary.
    // Only their field-selection bits live here; this core owns no Python refs.
    std::uint16_t external_fields = 0;
    std::uint16_t missing_fields = 0;

    bool present(DimsField field) const noexcept {
        return (missing_fields & (std::uint16_t{1} << static_cast<unsigned>(field))) == 0;
    }

    void present(DimsField field, bool value) noexcept {
        const auto bit = static_cast<std::uint16_t>(std::uint16_t{1} << static_cast<unsigned>(field));
        if (value) missing_fields &= static_cast<std::uint16_t>(~bit);
        else missing_fields |= bit;
    }

    bool external(DimsField field) const noexcept {
        return (external_fields & (std::uint16_t{1} << static_cast<unsigned>(field))) != 0;
    }
    void external(DimsField field, bool value) noexcept {
        const auto bit = static_cast<std::uint16_t>(std::uint16_t{1} << static_cast<unsigned>(field));
        if (value) external_fields |= bit;
        else external_fields &= static_cast<std::uint16_t>(~bit);
    }

    void normalize();
    void normalize_primary();
    void normalize_ranges();
    void normalize_point();
    void normalize_margins(bool left);
    void normalize_order();
    void validate_order() const;
    void normalize_labels();
    void normalize_last_used();
    static void validate_ranges(const std::vector<Range>& ranges);
    static Index axis_in_bounds(Index axis, Index ndim);
    std::vector<Index> nsteps() const;
    std::vector<Index> current_step() const;
    std::vector<Index> margin_steps(bool left) const;
    std::vector<double> thickness() const;
    std::vector<Index> thickness_steps() const;
    std::vector<Index> displayed(bool visible = true) const;
    std::vector<Index> displayed_order() const;
    std::vector<double> point_from_steps(const std::vector<double>& steps) const;
    std::vector<double> margin_from_steps(const std::vector<double>& steps) const;
    std::vector<Range> ranges_from_nsteps(const std::vector<double>& steps) const;
    std::vector<double> point_on_axes(const std::vector<Index>& axes,
                                      const std::vector<double>& values,
                                      bool steps = false) const;
    std::vector<Range> ranges_on_axes(const std::vector<Index>& axes,
                                     const std::vector<Range>& values) const;
    std::vector<std::string> labels_on_axes(const std::vector<Index>& axes,
                                          const std::vector<std::string>& values) const;
    std::vector<Index> transposed_order() const;
    std::vector<Index> rolled_order() const;
    Index focused_axis(int direction) const;
    std::vector<Index> center_steps() const;
};

std::vector<Index> reorder_after_reduction(const std::vector<Index>& order);

struct CameraSnapshot {
    Vec3 center{}, angles{};
    double zoom = 1.0;
};

struct CameraTransition {
    std::optional<Vec3> center, angles;
    std::optional<double> zoom;
    std::optional<Index> point_axis;
    double point_value = 0;
    bool fit = false;
};

struct CameraState {
    Vec3 center{}, angles{};
    double zoom = 1.0, perspective = 0.0;
    bool mouse_pan = true, mouse_zoom = true, synced = true;
    std::array<std::string, 3> orientation{"towards", "down", "right"};
    std::optional<CameraSnapshot> cached_2d, cached_3d;

    std::pair<Vec3, Vec3> directions() const;
    Vec3 angles_for_directions(const Vec3& view, const Vec3& up) const;
    std::vector<double> embed_direction(Index ndim, const std::vector<Index>& axes,
                                       bool up) const;
    bool left_handed() const;
    void cache(Index mode);
    std::optional<CameraSnapshot> pop_cache(Index mode);
    CameraTransition switch_mode(Index mode, const DimsState& dims);
};

struct GridLayout {
    bool enabled;
    std::vector<bool> visible;
    Cell shape;
    std::vector<Index> group_of, occupied;
    std::vector<Cell> positions;
    Cell position(Index index) const;
    std::vector<Index> contents(Cell position) const;
    std::vector<std::pair<Cell, std::vector<Index>>> viewboxes() const;
};

struct GridState {
    Index stride = 1;
    Cell shape{-1, -1};
    bool enabled = false;
    double spacing = 0;
    // Canonical hexadecimal values outside Index's range. Empty means the ordinary
    // field is authoritative. No interpreter references live in the core.
    std::string wide_stride;
    std::array<std::string, 2> wide_shape;
    GridLayout layout(const std::vector<bool>& visible) const;
    std::vector<Index> effective_indices(const std::vector<bool>& visible) const;
    Index canvas_spacing(Cell canvas, const GridLayout& layout, bool safe = true) const;
    std::array<double, 2> viewbox_size(Cell canvas, const GridLayout& layout) const;
};

struct ViewFit {
    std::vector<double> center;
    double zoom;
};
std::vector<double> view_center(const std::vector<double>& corner,
                                const std::vector<double>& size, Index ndisplay);
double scale_factor(double margin);
std::array<double, 2> bounding_box(const std::vector<double>& size,
                                  const Vec3& view, const Vec3& up);
double zoom_2d(const std::vector<double>& size, std::array<double, 2> canvas,
               double scale);
double zoom_3d(const std::vector<double>& size, const Vec3& view, const Vec3& up,
               std::array<double, 2> canvas, double scale);
ViewFit fit_view(const std::vector<double>& corner, const std::vector<double>& size,
                 Index ndisplay, const Vec3& view, const Vec3& up,
                 std::array<double, 2> canvas, std::array<double, 2> whole_canvas,
                 double margin);
} // namespace nx::app
