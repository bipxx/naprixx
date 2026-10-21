#include "arrays.hpp"

namespace nx {

Output<double> segment_normal(Input<double> a, Input<double> b, Input<double> p) {
    const size_t dims = coordinate_dims(a);
    const size_t rows = a.ndim() == 1 ? 1 : a.shape(0);
    if (coordinate_dims(b) != dims || a.ndim() != b.ndim() ||
        (a.ndim() == 2 && a.shape(0) != b.shape(0)) || (dims != 2 && dims != 3))
        throw std::invalid_argument("segment endpoints must have matching 2D or 3D shapes");
    require_vector(p, 3, "reference vector must have shape (3,)");
    auto out = allocate<double>(a.ndim() == 1
        ? std::vector<size_t>{dims} : std::vector<size_t>{rows, dims});
    {
        nb::gil_scoped_release release;
        for (size_t r = 0; r < rows; ++r) {
            double d[3]{};
            for (size_t c = 0; c < dims; ++c)
                d[c] = coord(b, r, c) - coord(a, r, c);
            double n[3]{};
            if (dims == 2) { n[0] = d[1]; n[1] = -d[0]; }
            else {
                n[0] = d[1] * at(p, 2) - d[2] * at(p, 1);
                n[1] = d[2] * at(p, 0) - d[0] * at(p, 2);
                n[2] = d[0] * at(p, 1) - d[1] * at(p, 0);
            }
            double norm_sq = 0;
            for (size_t c = 0; c < dims; ++c) norm_sq += n[c] * n[c];
            double norm = std::sqrt(norm_sq);
            if (norm == 0) norm = 1;
            for (size_t c = 0; c < dims; ++c)
                out.data()[r * dims + c] = n[c] / norm;
        }
    }
    return out;
}

nb::tuple project_points_onto_plane(Input<double> points, Input<double> point, Input<double> normal) {
    if (coordinate_dims(points) != 3)
        throw std::invalid_argument("points must have shape (3,) or (N, 3)");
    require_vector(point, 3, "plane point must have shape (3,)");
    require_vector(normal, 3, "plane normal must have shape (3,)");
    const size_t rows = points.ndim() == 1 ? 1 : points.shape(0);
    auto projected = allocate<double>({rows, 3});
    auto distances = allocate<double>({rows});
    {
        nb::gil_scoped_release release;
        for (size_t r = 0; r < rows; ++r) {
            double distance = 0;
            for (size_t c = 0; c < 3; ++c)
                distance += (coord(points, r, c) - at(point, c)) * at(normal, c);
            distances.data()[r] = distance;
            for (size_t c = 0; c < 3; ++c)
                projected.data()[r * 3 + c] = coord(points, r, c) - distance * at(normal, c);
        }
    }
    return nb::make_tuple(projected, distances);
}

void bind_geometry(nb::module_& m) {
    m.def("segment_normal", &segment_normal, nb::arg("a").noconvert(),
          nb::arg("b").noconvert(), nb::arg("p").noconvert());
    m.def("project_points_onto_plane", &project_points_onto_plane, nb::arg("points").noconvert(),
          nb::arg("plane_point").noconvert(), nb::arg("plane_normal").noconvert());
}
} // namespace nx
