#include "application.hpp"
#include "slicing.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace nx::app;
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    try {
        DimsFlags flags;
        require(flags.value(DimsFlag::PlayReady).truth() == true, "playback flag default");
        require(flags.value(DimsFlag::ScrollProgress).kind == DimsFlagValue::Kind::Integer,
                "scroll flag preserves integer default");
        flags.value(DimsFlag::ScrollProgress) = DimsFlagValue::from_real(-0.25);
        auto flags_copy = flags;
        flags.value(DimsFlag::ScrollProgress) = DimsFlagValue::from_integer(0);
        require(flags_copy.value(DimsFlag::ScrollProgress).real == -0.25,
                "independent native flag copy");
        require(flags.value(DimsFlag::ScrollProgress).truth() == false,
                "native integer flag truth");
        flags.value(DimsFlag::Validating) = DimsFlagValue::from_real(std::nan(""));
        require(flags.value(DimsFlag::Validating).truth() == true, "non-finite flag truth");
        flags.value(DimsFlag::Validating).present = false;
        require(!flags.value(DimsFlag::Validating).truth(), "deleted flag has no native truth");
        flags.value(DimsFlag::Validating) = DimsFlagValue{};
        require(!flags.value(DimsFlag::Validating).truth(), "provider stays at adapter boundary");
        DimsState dims;
        dims.ndim = 4;
        dims.ranges = {{-10, 10, 0.5}, {-10, 10, 0.5}};
        dims.point = {99, -99};
        dims.normalize();
        require(dims.point == std::vector<double>({0, 0, 10, -10}), "range clipping and padding");
        require(dims.current_step() == std::vector<Index>({0, 0, 40, 0}), "slider coordinates");
        auto snapshot = dims;
        dims.ndim = 3;
        dims.normalize();
        require(snapshot.ndim == 4 && dims.order == std::vector<Index>({0, 1, 2}), "dimension snapshot/reduction");

        CameraState camera;
        camera.center = {1, 2, 3}; camera.zoom = 2.5; camera.cache(2);
        camera.center = {4, 5, 6}; camera.cache(3);
        auto cloned = camera;
        require(cloned.pop_cache(2)->center == Vec3({1, 2, 3}), "saved 2D camera");
        require(camera.cached_2d.has_value() && !cloned.cached_2d.has_value(), "independent camera copies");
        camera.angles = {12, 30, 57};
        const auto directions = camera.directions();
        camera.angles = camera.angles_for_directions(directions.first, directions.second);
        const auto recovered = camera.directions();
        for (size_t i = 0; i < 3; ++i)
            require(std::abs(recovered.first[i] - directions.first[i]) < 1e-12, "camera orientation roundtrip");

        GridState grid{-2, {-1, -1}, true, 0.2};
        const auto layout = grid.layout({true, false, true, true, false});
        require(layout.shape == Cell({2, 2}), "occupied grid shape");
        require(layout.position(0) == Cell({1, 0}) && layout.position(2) == Cell({0, 1}), "negative grid stride");
        require(layout.position(1) == Cell({-1, -1}), "hidden layer excluded");
        require(grid.canvas_spacing({5, 10}, layout) == 0, "small canvas spacing protection");
        auto wide_grid = grid;
        wide_grid.stride = 1;
        wide_grid.wide_stride = "0x100000000000000000000";
        require(wide_grid.layout({true, false, true}).group_of == std::vector<Index>({0, 0, 0}),
                "wide positive stride groups without integer narrowing");
        wide_grid.stride = -1;
        wide_grid.wide_stride = "-0x100000000000000000000";
        require(wide_grid.layout({true, true, true}).shape == Cell({1, 1}), "wide negative stride");
        wide_grid.wide_stride.clear();
        wide_grid.stride = std::numeric_limits<Index>::min();
        require(wide_grid.layout({true, true, true}).group_of == std::vector<Index>({0, 0, 0}),
                "minimum signed stride uses unsigned magnitude");
        wide_grid.stride = 0;
        require(wide_grid.layout({}).shape == Cell({1, 1}), "empty grid avoids stride division");
        const auto fit = fit_view({-0.5, -0.5}, {10, 20}, 2, {-1, 0, 0}, {0, -1, 0}, {100, 200}, {100, 200}, 0.05);
        require(fit.center == std::vector<double>({4.5, 9.5}) && fit.zoom == 9.5, "view fitting");

        Matrix transform(3, 3);
        transform.values = {2, 3, 1, 0, 4, 2, 0, 0, 5};
        const auto factors = decompose(transform, true);
        Matrix scaling = Matrix::identity(3);
        for (size_t i = 0; i < 3; ++i) scaling(i, i) = factors.scale[i];
        const auto recomposed = multiply(multiply(factors.rotation, factors.shear), scaling);
        const auto restored = multiply(inverse(transform), transform);
        for (size_t i = 0; i < 3; ++i)
            for (size_t j = 0; j < 3; ++j) {
                require(std::abs(recomposed(i, j) - transform(i, j)) < 1e-12, "affine factorization");
                require(std::abs(restored(i, j) - (i == j)) < 1e-12, "affine inverse");
            }
        const SlicePlan slice{2, {2, 0, 1}};
        require(slice.axes(false) == std::vector<Index>({2}), "hidden slice axes");
        require(slice.resized(5).order == std::vector<Index>({0, 1, 4, 2, 3}), "slice expansion");
        Matrix thick(3, 3);
        thick.values = {0, 0, 2.5, 0, 0, 1, 0, 0, 1};
        const auto selection = thick_selections(thick, {0, 1});
        require(selection[2].start == 2 && selection[2].stop == 4, "round-even thick slice");
        require(!slice_out_of_bounds(thick, {2}, {10, 10, 10}, true), "in-bounds thick slice");
        require(slice_out_of_bounds(thick, {2}, {10, 10, 1}, false), "out-of-bounds point");
        LayerLoadState loading;
        require(loading.begin(4) && !loading.finish(3) && loading.finish(4), "stale slice completion rejected");
        const auto bounds = aggregate_extent({{2, 3}, {1, -1, 0}}, {{6, 8}, {4, 10, 2}});
        require(bounds.values == std::vector<double>({1, -1, 0, 4, 10, 8}), "aligned layer extents");
        std::cout << "C++ application contracts passed without Python or Qt.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
