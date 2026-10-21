#pragma once

#include "application.hpp"
#include "arrays.hpp"

namespace nx {
enum CameraFields : unsigned {
    CameraCenter = 1u, CameraZoom = 2u, CameraAngles = 4u,
    CameraPerspective = 8u, CameraPan = 16u, CameraMouseZoom = 32u,
    CameraOrientation = 64u, CameraSynced = 128u, CameraAll = 255u
};
enum GridFields : unsigned { GridStride = 1u, GridShape = 2u, GridEnabled = 4u, GridSpacing = 8u, GridAll = 15u };

// Adapter references are native record fields with explicit GC traversal.
// The independent core contains only C++ values. These entries require the GIL.
void attach_component_state(nb::handle model);
void refresh_component_model(nb::handle model);
void sync_component_state(const app::CameraState& state, unsigned required = 0);
void sync_component_state(const app::GridState& state, unsigned required = 0);
void publish_component_state(app::CameraState& state);
void publish_component_state(app::GridState& state);
void set_grid_stride(app::GridState& state, nb::handle value);
void set_grid_shape(app::GridState& state, nb::handle value);
nb::object grid_stride_value(const app::GridState& state);
nb::tuple grid_shape_value(const app::GridState& state);

class ComponentSyncGuard {
    nb::object owner_;
    const void* address_ = nullptr;
public:
    ComponentSyncGuard(nb::handle model, nb::handle state);
    ~ComponentSyncGuard();
    ComponentSyncGuard(const ComponentSyncGuard&) = delete;
    ComponentSyncGuard& operator=(const ComponentSyncGuard&) = delete;
};
void install_camera_namespace(nb::handle type);
void install_grid_namespace(nb::handle type);
void bind_component_state(nb::module_& module);
}
