#pragma once

#include "layer_core.hpp"

namespace nx::app {
struct AxisSelection {
    enum class Kind { all, index, range };
    Kind kind = Kind::all;
    Index start = 0, stop = 0;
};
using Selections = std::vector<AxisSelection>;
Selections point_selections(const std::vector<double>& point);
Selections thick_selections(const Matrix& slice, const std::vector<Index>& displayed);
bool slice_out_of_bounds(const Matrix& slice, const std::vector<Index>& hidden,
                         const std::vector<double>& shape, bool project);
Matrix slice_at_level(const Matrix& slice, const std::vector<double>& factors,
                       const std::vector<double>& shape);
std::vector<Index> image_axis_order(const std::vector<Index>& displayed, bool rgb);
struct TilePlan {
    std::vector<double> scale, translate;
    Selections selections;
};
TilePlan image_tile_plan(const SlicePlan& slice, size_t data_ndim,
                         const Matrix& corners, const std::vector<double>& factors);
struct VisibleVectors {
    std::vector<Index> indices;
    std::vector<double> alphas;
};
VisibleVectors slice_vectors(const Matrix& origins, const Matrix& slice,
                              const std::vector<Index>& hidden, bool project, bool fade);
std::vector<Index> reindex_faces(const std::vector<Index>& faces, size_t width,
    size_t vertex_count, const std::vector<Index>& selected);
} // namespace nx::app
