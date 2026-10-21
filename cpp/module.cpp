#include "arrays.hpp"
#include "dims_dictionary.hpp"
#include "dims_flags_bindings.hpp"
#include "event_bindings.hpp"
#include "transform_bindings.hpp"
#include "collection_bindings.hpp"
#include "model_lifecycle.hpp"
#include "component_state.hpp"
#include "grid_controller.hpp"
#include "layerlist_controller.hpp"
#include "qt_lifecycle.hpp"
#include "qt_focus.hpp"
#include "qt_focus_connection.hpp"
#include "vispy_controller_state.hpp"

NB_MODULE(_native, m) {
    m.doc() = "C++ application models and numerical kernels for napari.";
    m.attr("napari_version") = "0.9.2";
    m.attr("__version__") = "0.3.0";
    nx::bind_transform_state_owner(m);
    nx::bind_transforms(m);
    nx::bind_transform_family_policy(m);
    nx::bind_transform_objects(m);
    nx::bind_transform_family_backend(m);
    nx::bind_collections(m);
    nx::bind_nested_collections(m);
    nx::bind_selection(m);
    nx::bind_dictionaries(m);
    nx::bind_points(m);
    nx::bind_labels(m);
    nx::bind_labels_display(m);
    nx::bind_labels_response_display(m);
    nx::bind_viewer_close(m);
    nx::bind_viewer_status_workers(m);
    nx::bind_viewer_status_providers(m);
    nx::bind_viewer_status_geometry(m);
    nx::bind_viewer_status_format(m);
    nx::bind_viewer_shape_state(m);
    nx::bind_viewer_shape_model(m);
    nx::bind_viewer_dims_reconciliation(m);
    nx::bind_viewer_reset_fit(m);
    nx::bind_viewer_layer_lifecycle(m);
    nx::bind_settings_grid_publication(m);
    nx::bind_theme_color_publication(m);
    nx::bind_theme_registration_icons(m);
    nx::bind_theme_state(m);
    nx::bind_shape_geometry(m);
    nx::bind_shape_publication(m);
    nx::bind_shape_acceleration(m);
    nx::bind_viewer_animation_workers(m);
    nx::bind_label_colormap(m);
    nx::bind_colormap_acceleration(m);
    nx::bind_colormap_inner_loops(m);
    nx::bind_direct_colormap_cache(m);
    nx::bind_label_colormap_map(m);
    nx::bind_vispy_labels(m);
    nx::bind_vispy_scalar_field(m);
    nx::bind_vispy_texture_services(m);
    nx::bind_vispy_controller_state(m);
    nx::bind_vispy_base_layer(m);
    nx::bind_vispy_image_visuals(m);
    nx::bind_vispy_image_layer(m);
    nx::bind_layer_publication(m);
    nx::bind_layer_thumbnails(m);
    nx::bind_intensity_validation(m);
    nx::bind_layer_construction(m);
    nx::bind_image_construction(m);
    nx::bind_layer_first_refresh(m);
    nx::bind_scalar_first_refresh(m);
    nx::bind_dask_indexing(m);
    nx::bind_layer_data_geometry(m);
    nx::bind_scalar_shape_extent(m);
    nx::bind_slice_dataclass_state(m);
    nx::bind_scalar_slice_executor(m);
    nx::bind_multiscale_data(m);
    nx::bind_scalar_viewport_policy(m);
    nx::bind_scalar_data_controller(m);
    nx::bind_geometry(m);
    nx::bind_range(m);
    nx::bind_application(m);
    nx::bind_component_state(m);
    nx::bind_grid_controller(m);
    nx::bind_layerlist_controller(m);
    nx::bind_qt_lifecycle(m);
    nx::bind_qt_focus(m);
    nx::bind_qt_focus_connection(m);
    nx::bind_dims_dictionary(m);
    nx::bind_dims_flags(m);
    nx::bind_dims_model(m);
    nx::bind_layers(m);
    nx::bind_slicing(m);
    nx::bind_layer_slice_scheduler(m);
    nx::bind_orchestration(m);
    nx::bind_data(m);
    nx::bind_models(m);
    nx::bind_model_lifecycle(m);
    nx::bind_event_objects(m);
    nx::bind_event_subscriptions(m);
    nx::bind_event_delivery(m);
    nx::bind_event_groups(m);
    nx::bind_event_tracing(m);
    nx::bind_notifications(m);
}
