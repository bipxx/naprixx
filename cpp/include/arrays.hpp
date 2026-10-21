#pragma once

#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

namespace nx {
namespace nb = nanobind;

template <typename T>
using Input = nb::ndarray<nb::numpy, const T, nb::device::cpu>;
template <typename T>
using Output = nb::ndarray<nb::numpy, T, nb::device::cpu>;

inline size_t checked_product(const std::vector<size_t>& shape) {
    size_t result = 1;
    for (auto value : shape) {
        if (value && result > std::numeric_limits<size_t>::max() / value)
            throw std::overflow_error("array shape exceeds addressable memory");
        result *= value;
    }
    return result;
}

template <typename T>
Output<T> allocate(const std::vector<size_t>& shape) {
    const size_t count = checked_product(shape);
    if (count > std::numeric_limits<size_t>::max() / sizeof(T))
        throw std::overflow_error("array size exceeds addressable memory");
    auto data = std::make_unique<T[]>(std::max<size_t>(count, 1));
    nb::capsule owner(data.get(), [](void* ptr) noexcept {
        delete[] static_cast<T*>(ptr);
    });
    auto* ptr = data.release();
    return Output<T>(ptr, shape.size(), shape.data(), owner);
}

template <typename T>
Output<T> from_vector(const std::vector<T>& data, const std::vector<size_t>& shape) {
    auto out = allocate<T>(shape);
    std::copy(data.begin(), data.end(), out.data());
    return out;
}

template <typename T>
T read(const T* base, int64_t offset) {
    T value;
    std::memcpy(&value, reinterpret_cast<const char*>(base) + offset * static_cast<int64_t>(sizeof(T)), sizeof(T));
    return value;
}

template <typename T>
T at(const Input<T>& a, size_t i) {
    return read(a.data(), static_cast<int64_t>(i) * a.stride(0));
}

template <typename T>
T at(const Input<T>& a, size_t i, size_t j) {
    return read(a.data(), static_cast<int64_t>(i) * a.stride(0)
                         + static_cast<int64_t>(j) * a.stride(1));
}

// Python/NumPy use round-to-even. Do not depend on the caller's FPU mode.
inline double round_even(double value) {
    const double base = std::floor(value);
    const double fraction = value - base;
    if (fraction < 0.5) return base;
    if (fraction > 0.5) return base + 1;
    return std::fmod(base, 2) == 0 ? base : base + 1;
}

template <typename T>
size_t coordinate_dims(const Input<T>& a) {
    if (a.ndim() != 1 && a.ndim() != 2)
        throw std::invalid_argument("coordinates must have shape (D,) or (N, D)");
    return a.shape(a.ndim() - 1);
}

template <typename T>
T coord(const Input<T>& a, size_t row, size_t col) {
    return a.ndim() == 1 ? at(a, col) : at(a, row, col);
}

template <typename T>
void require_vector(const Input<T>& a, size_t length, const char* message) {
    if (a.ndim() != 1 || a.shape(0) != length)
        throw std::invalid_argument(message);
}

void bind_transforms(nb::module_& m);
void bind_points(nb::module_& m);
void bind_labels(nb::module_& m);
void bind_labels_display(nb::module_& m);
void bind_labels_response_display(nb::module_& m);
void bind_viewer_close(nb::module_& m);
void bind_viewer_status_workers(nb::module_& m);
void bind_viewer_status_providers(nb::module_& m);
void bind_viewer_status_geometry(nb::module_& m);
void bind_viewer_status_format(nb::module_& m);
void bind_viewer_shape_state(nb::module_& m);
void bind_viewer_shape_model(nb::module_& m);
void bind_viewer_dims_reconciliation(nb::module_& m);
void bind_viewer_reset_fit(nb::module_& m);
void bind_viewer_layer_lifecycle(nb::module_& m);
void bind_settings_grid_publication(nb::module_& m);
void bind_theme_color_publication(nb::module_& m);
void bind_theme_registration_icons(nb::module_& m);
void bind_theme_state(nb::module_& m);
void bind_shape_geometry(nb::module_& m);
void bind_shape_publication(nb::module_& m);
void bind_shape_acceleration(nb::module_& m);
void bind_viewer_animation_workers(nb::module_& m);
void bind_label_colormap(nb::module_& m);
void bind_colormap_acceleration(nb::module_& m);
void bind_colormap_inner_loops(nb::module_& m);
void bind_direct_colormap_cache(nb::module_& m);
void bind_label_colormap_map(nb::module_& m);
void bind_vispy_labels(nb::module_& m);
void bind_vispy_scalar_field(nb::module_& m);
void bind_vispy_texture_services(nb::module_& m);
void bind_vispy_base_layer(nb::module_& m);
void bind_vispy_image_visuals(nb::module_& m);
void bind_vispy_image_layer(nb::module_& m);
void bind_layer_publication(nb::module_& m);
void bind_layer_thumbnails(nb::module_& m);
void bind_intensity_validation(nb::module_& m);
void bind_layer_construction(nb::module_& m);
void bind_image_construction(nb::module_& m);
void bind_layer_first_refresh(nb::module_& m);
void bind_scalar_first_refresh(nb::module_& m);
void bind_dask_indexing(nb::module_& m);
void bind_layer_data_geometry(nb::module_& m);
void bind_scalar_shape_extent(nb::module_& m);
void bind_slice_dataclass_state(nb::module_& m);
void bind_scalar_slice_executor(nb::module_& m);
void bind_multiscale_data(nb::module_& m);
void bind_scalar_viewport_policy(nb::module_& m);
void bind_scalar_data_controller(nb::module_& m);
nb::object scalar_executor_request_compat(nb::object self, nb::dict namespace_);
nb::object scalar_executor_single_compat(nb::object self, nb::dict namespace_);
nb::object scalar_executor_multi_compat(nb::object self, nb::dict namespace_);
nb::object scalar_executor_order_compat(nb::object self, nb::dict namespace_);
nb::object scalar_executor_project_compat(
    nb::object self, nb::object data, nb::object data_slice, nb::dict namespace_);
nb::object scalar_executor_image_project_compat(
    nb::object data, nb::object axis, nb::object mode, nb::dict namespace_);
nb::object slice_dataclass_data_slice_compat(
    nb::object self, nb::object world_to_data,
    nb::dict namespace_, nb::object warning_message);
void bind_geometry(nb::module_& m);
void bind_range(nb::module_& m);
void bind_application(nb::module_& m);
void bind_dims_model(nb::module_& m);
void bind_layers(nb::module_& m);
void bind_slicing(nb::module_& m);
void bind_layer_slice_scheduler(nb::module_& m);
void bind_orchestration(nb::module_& m);
void bind_data(nb::module_& m);
void bind_models(nb::module_& m);
void bind_notifications(nb::module_& m);
} // namespace nx
