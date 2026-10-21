#include "application.hpp"
#include "arrays.hpp"
#include "dims_dictionary.hpp"
#include "component_state.hpp"
#include <nanobind/stl/array.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/pair.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>

namespace nx {
namespace {
using namespace app;

template <typename Sequence>
nb::tuple tuple_of(const Sequence& sequence) {
    auto result = nb::steal<nb::tuple>(PyTuple_New(static_cast<Py_ssize_t>(sequence.size())));
    if (!result.is_valid()) throw nb::python_error();
    for (size_t i = 0; i < sequence.size(); ++i) {
        auto item = nb::cast(sequence[i]);
        PyTuple_SET_ITEM(result.ptr(), static_cast<Py_ssize_t>(i), item.release().ptr());
    }
    return result;
}

nb::tuple bool_tuple(const std::vector<bool>& sequence) {
    nb::list result;
    for (bool value : sequence) result.append(nb::bool_(value));
    return nb::tuple(result);
}

nb::tuple ranges_tuple(const std::vector<Range>& ranges) {
    nb::list result;
    for (const auto& r : ranges) result.append(tuple_of(r));
    return nb::tuple(result);
}

nb::tuple sanitize_dims_input(nb::handle axis, nb::handle value, bool sequence_value, Index ndim) {
    auto integral = nb::module_::import_("numbers").attr("Integral");
    const int scalar_axis = PyObject_IsInstance(axis.ptr(), integral.ptr());
    if (scalar_axis < 0) throw nb::python_error();
    nb::list axes, values;
    if (scalar_axis) {
        const auto sequence = nb::module_::import_("collections.abc").attr("Sequence");
        const int is_sequence = PyObject_IsInstance(value.ptr(), sequence.ptr());
        if (is_sequence < 0) throw nb::python_error();
        if (is_sequence && !nb::isinstance<nb::str>(value) && !sequence_value)
            throw nb::value_error("cannot set multiple values to a single axis");
        axes.append(axis);
        values.append(value);
    } else {
        axes = nb::list(axis);
        values = nb::list(value);
    }
    if (nb::len(axes) != nb::len(values))
        throw nb::value_error("axis and value sequences must have equal length");
    for (auto a : axes) DimsState::axis_in_bounds(nb::cast<Index>(a), ndim);
    return nb::make_tuple(axes, values);
}

nb::object ensure_len(nb::handle values, Index length, nb::handle pad) {
    const auto count = static_cast<Index>(nb::len(values));
    if (count < length) {
        nb::list padding;
        for (Index index = count; index < length; ++index) padding.append(pad);
        nb::tuple prefix(padding);
        nb::object result = nb::steal<nb::object>(PyNumber_Add(prefix.ptr(), values.ptr()));
        if (!result.is_valid()) throw nb::python_error();
        return result;
    }
    if (count > length) {
        nb::object slice = nb::module_::import_("builtins").attr("slice")(-length, nb::none());
        return values[slice];
    }
    return nb::borrow<nb::object>(values);
}

std::vector<double> size_from_extent(const Input<double>& extent) {
    if (extent.ndim() != 2 || extent.shape(0) != 2)
        throw nb::value_error("extent must have shape (2, D)");
    std::vector<double> result(extent.shape(1));
    for (size_t i = 0; i < result.size(); ++i) result[i] = at(extent, 1, i) - at(extent, 0, i);
    return result;
}
} // namespace

void bind_application(nb::module_& m) {
    using namespace app;
    m.def("ensure_len", &ensure_len, nb::arg("values").none(), nb::arg("length"), nb::arg("pad").none());
    m.def("ensure_axis_in_bounds", &DimsState::axis_in_bounds, nb::arg("axis"), nb::arg("ndim"));
    m.def("sanitize_dims_input", &sanitize_dims_input, nb::arg("axis").none(), nb::arg("value").none(),
          nb::arg("value_is_sequence"), nb::arg("ndim"));
    m.def("validate_dims_ranges", &DimsState::validate_ranges);
    m.def("reorder_after_dim_reduction", [](const std::vector<Index>& order) {
        return tuple_of(reorder_after_reduction(order));
    });
    m.def("dims_nsteps", [](const std::vector<Range>& ranges) {
        DimsState state;
        state.ranges = ranges;
        return tuple_of(state.nsteps());
    });
    m.def("split_thickness", [](const std::vector<double>& values, bool steps) {
        std::vector<double> result;
        for (double value : values) result.push_back(steps ? std::floor(value / 2) : value / 2);
        return tuple_of(result);
    }, nb::arg("values"), nb::arg("steps") = false);
    m.def("make_dims_state", [](Index ndim, Index ndisplay, const std::vector<Range>& ranges,
            const std::vector<double>& point, const std::vector<double>& left,
            const std::vector<double>& right, const std::vector<Index>& order,
            const std::vector<std::string>& labels, const std::vector<bool>& rollable, Index last_used,
            bool normalize) {
        DimsState state{ndim, ndisplay, ranges, point, left, right, order, labels, rollable, last_used};
        if (normalize) state.normalize();
        return state;
    }, nb::arg("ndim"), nb::arg("ndisplay"), nb::arg("ranges"), nb::arg("point"),
       nb::arg("margin_left"), nb::arg("margin_right"), nb::arg("order"),
       nb::arg("axis_labels"), nb::arg("rollable"), nb::arg("last_used"), nb::arg("normalize") = true);
    // GC must see the instance -> type edge in cycles involving Python
    // facades and Qt widgets. Weak-referenceable instances install nanobind's
    // traversal slots and also let lifecycle checks observe their release.
    nb::class_<DimsState>(m, "DimsState", nb::is_weak_referenceable())
        .def_prop_ro("ndim", [](const DimsState& s) { sync_dims_dictionary_state(s); return s.ndim; })
        .def_prop_ro("ndisplay", [](const DimsState& s) { sync_dims_dictionary_state(s); return s.ndisplay; })
        .def_prop_ro("ranges", [](const DimsState& s) { sync_dims_dictionary_state(s); return ranges_tuple(s.ranges); })
        .def_prop_ro("point", [](const DimsState& s) { sync_dims_dictionary_state(s); return tuple_of(s.point); })
        .def_prop_ro("margin_left", [](const DimsState& s) { sync_dims_dictionary_state(s); return tuple_of(s.margin_left); })
        .def_prop_ro("margin_right", [](const DimsState& s) { sync_dims_dictionary_state(s); return tuple_of(s.margin_right); })
        .def_prop_ro("order", [](const DimsState& s) { sync_dims_dictionary_state(s); return tuple_of(s.order); })
        .def_prop_ro("axis_labels", [](const DimsState& s) { sync_dims_dictionary_state(s); return tuple_of(s.axis_labels); })
        .def_prop_ro("rollable", [](const DimsState& s) { sync_dims_dictionary_state(s); return bool_tuple(s.rollable); })
        .def_prop_ro("last_used", [](const DimsState& s) { sync_dims_dictionary_state(s); return s.last_used; })
        .def_prop_ro("nsteps", [](const DimsState& s) { sync_dims_dictionary_state(s); return tuple_of(s.nsteps()); })
        .def_prop_ro("current_step", [](const DimsState& s) { sync_dims_dictionary_state(s); return tuple_of(s.current_step()); })
        .def_prop_ro("margin_left_step", [](const DimsState& s) { sync_dims_dictionary_state(s); return tuple_of(s.margin_steps(true)); })
        .def_prop_ro("margin_right_step", [](const DimsState& s) { sync_dims_dictionary_state(s); return tuple_of(s.margin_steps(false)); })
        .def_prop_ro("thickness", [](const DimsState& s) { sync_dims_dictionary_state(s); return tuple_of(s.thickness()); })
        .def_prop_ro("thickness_step", [](const DimsState& s) { sync_dims_dictionary_state(s); return tuple_of(s.thickness_steps()); })
        .def_prop_ro("displayed", [](const DimsState& s) { sync_dims_dictionary_state(s); return tuple_of(s.displayed()); })
        .def_prop_ro("not_displayed", [](const DimsState& s) { sync_dims_dictionary_state(s); return tuple_of(s.displayed(false)); })
        .def_prop_ro("displayed_order", [](const DimsState& s) { sync_dims_dictionary_state(s); return tuple_of(s.displayed_order()); })
        .def("point_from_steps", [](const DimsState& s, const std::vector<double>& v) { sync_dims_dictionary_state(s); return tuple_of(s.point_from_steps(v)); })
        .def("margin_from_steps", [](const DimsState& s, const std::vector<double>& v) { sync_dims_dictionary_state(s); return tuple_of(s.margin_from_steps(v)); })
        .def("ranges_from_nsteps", [](const DimsState& s, const std::vector<double>& v) { sync_dims_dictionary_state(s);
            try {
                return ranges_tuple(s.ranges_from_nsteps(v));
            } catch (const std::domain_error& error) {
                PyErr_SetString(PyExc_ZeroDivisionError, error.what());
                throw nb::python_error();
            }
        })
        .def("point_on_axes", [](const DimsState& s, const std::vector<Index>& axes, const std::vector<double>& v, bool steps) { sync_dims_dictionary_state(s);
            return tuple_of(s.point_on_axes(axes, v, steps));
        }, nb::arg("axes"), nb::arg("values"), nb::arg("steps") = false)
        .def("ranges_on_axes", [](const DimsState& s, const std::vector<Index>& axes, const std::vector<Range>& v) { sync_dims_dictionary_state(s);
            return ranges_tuple(s.ranges_on_axes(axes, v));
        })
        .def("labels_on_axes", [](const DimsState& s, const std::vector<Index>& axes, const std::vector<std::string>& v) { sync_dims_dictionary_state(s);
            return tuple_of(s.labels_on_axes(axes, v));
        })
        .def("transposed_order", [](const DimsState& s) { sync_dims_dictionary_state(s); return tuple_of(s.transposed_order()); })
        .def("rolled_order", [](const DimsState& s) { sync_dims_dictionary_state(s); return tuple_of(s.rolled_order()); })
        .def("focused_axis", [](const DimsState& s, int direction) { sync_dims_dictionary_state(s); return s.focused_axis(direction); })
        .def("center_steps", [](const DimsState& s) { sync_dims_dictionary_state(s); return tuple_of(s.center_steps()); })
        .def("incremented_point", [](const DimsState& s, Index axis, int direction) { sync_dims_dictionary_state(s);
            const auto normalized_axis = DimsState::axis_in_bounds(axis, s.ndim);
            const double step = static_cast<double>(s.current_step().at(normalized_axis)) + direction;
            return tuple_of(s.point_on_axes({axis}, {step}, true));
        })
        .def("__copy__", [](const DimsState& s) { sync_dims_dictionary_state(s); return DimsState(s); })
        .def("__deepcopy__", [](const DimsState& s, nb::dict) { sync_dims_dictionary_state(s); return DimsState(s); })
        .def("__reduce__", [](const DimsState& s) { sync_dims_dictionary_state(s);
            return nb::make_tuple(nb::module_::import_("naparixx._restore").attr("dims"),
                nb::make_tuple(s.ndim, s.ndisplay, ranges_tuple(s.ranges), tuple_of(s.point),
                    tuple_of(s.margin_left), tuple_of(s.margin_right), tuple_of(s.order),
                    tuple_of(s.axis_labels), bool_tuple(s.rollable), s.last_used, false));
        });

    using SavedCamera = std::array<double, 7>;
    m.def("restore_camera_state", [](Vec3 center, double zoom, Vec3 angles, double perspective,
            bool pan, bool mouse_zoom, std::array<std::string, 3> orientation, bool synced,
            std::optional<SavedCamera> cache2d, std::optional<SavedCamera> cache3d) {
        CameraState state;
        state.center = center; state.zoom = zoom; state.angles = angles;
        state.perspective = perspective; state.mouse_pan = pan; state.mouse_zoom = mouse_zoom;
        state.orientation = std::move(orientation); state.synced = synced;
        auto restore = [](const std::optional<SavedCamera>& cache) -> std::optional<CameraSnapshot> {
            if (!cache) return std::nullopt;
            return CameraSnapshot{{(*cache)[0], (*cache)[1], (*cache)[2]},
                                  {(*cache)[3], (*cache)[4], (*cache)[5]}, (*cache)[6]};
        };
        state.cached_2d = restore(cache2d); state.cached_3d = restore(cache3d);
        return state;
    });

    m.def("camera_property_dependencies", [] {
        nb::dict result;
        result["view_direction"] = nb::cast(std::vector<std::string>{"angles", "orientation"});
        result["up_direction"] = nb::cast(std::vector<std::string>{"angles", "orientation"});
        result["orientation2d"] = nb::cast(std::vector<std::string>{"orientation"});
        // The pinned dependency scanner sees orientation inside the handedness
        // comprehension only when CPython inlines comprehensions (3.12+).
        // Preserve that interpreter-specific event contract with native bodies.
#if PY_VERSION_HEX >= 0x030C0000
        result["handedness"] = nb::cast(std::vector<std::string>{"orientation"});
#else
        result["handedness"] = nb::list();
#endif
        return result;
    });

    nb::class_<CameraSnapshot>(m, "CameraSnapshot")
        .def_prop_ro("center", [](const CameraSnapshot& s) { return tuple_of(s.center); })
        .def_prop_ro("angles", [](const CameraSnapshot& s) { return tuple_of(s.angles); })
        .def_ro("zoom", &CameraSnapshot::zoom);
    nb::class_<CameraTransition>(m, "CameraTransition")
        .def_prop_ro("center", [](const CameraTransition& s) -> nb::object {
            if (s.center) return tuple_of(*s.center);
            return nb::none();
        })
        .def_prop_ro("angles", [](const CameraTransition& s) -> nb::object {
            if (s.angles) return tuple_of(*s.angles);
            return nb::none();
        })
        .def_ro("zoom", &CameraTransition::zoom)
        .def_ro("point_axis", &CameraTransition::point_axis)
        .def_ro("point_value", &CameraTransition::point_value)
        .def_ro("fit", &CameraTransition::fit);
    nb::class_<CameraState>(m, "CameraState", nb::is_weak_referenceable(), nb::dynamic_attr())
        .def(nb::init<>())
        .def("update", [](CameraState& s, Vec3 center, double zoom, Vec3 angles,
                double perspective, bool pan, bool mouse_zoom,
                std::array<std::string, 3> orientation, bool synced) {
            s.center = center; s.zoom = zoom; s.angles = angles; s.perspective = perspective;
            s.mouse_pan = pan; s.mouse_zoom = mouse_zoom; s.orientation = std::move(orientation); s.synced = synced;
            publish_component_state(s);
        })
        .def_prop_ro("center", [](const CameraState& s) { sync_component_state(s, CameraCenter); return tuple_of(s.center); })
        .def_prop_ro("angles", [](const CameraState& s) { sync_component_state(s, CameraAngles); return tuple_of(s.angles); })
        .def_prop_ro("zoom", [](const CameraState& s) { sync_component_state(s, CameraZoom); return s.zoom; })
        .def_prop_ro("perspective", [](const CameraState& s) { sync_component_state(s, CameraPerspective); return s.perspective; })
        .def_prop_ro("synced", [](const CameraState& s) { sync_component_state(s, CameraSynced); return s.synced; })
        .def_prop_ro("mouse_pan", [](const CameraState& s) { sync_component_state(s, CameraPan); return s.mouse_pan; })
        .def_prop_ro("mouse_zoom", [](const CameraState& s) { sync_component_state(s, CameraMouseZoom); return s.mouse_zoom; })
        .def_prop_ro("orientation", [](const CameraState& s) { sync_component_state(s, CameraOrientation); return tuple_of(s.orientation); })
        .def("directions", [](const CameraState& s) {
            sync_component_state(s, CameraAngles | CameraOrientation);
            const auto result = s.directions();
            return nb::make_tuple(tuple_of(result.first), tuple_of(result.second));
        })
        .def("angles_for_directions", [](const CameraState& s, Vec3 view, Vec3 up) {
            sync_component_state(s, CameraOrientation);
            return tuple_of(s.angles_for_directions(view, up));
        })
        .def("embed_direction", [](const CameraState& s, Index ndim, const std::vector<Index>& axes, bool up) -> nb::object {
            if (axes.size() != 3) return nb::none();
            sync_component_state(s, CameraAngles | CameraOrientation);
            const auto result = s.embed_direction(ndim, axes, up);
            return nb::cast(from_vector(result, {result.size()}));
        }, nb::arg("ndim"), nb::arg("axes"), nb::arg("up") = false)
        .def_prop_ro("left_handed", [](const CameraState& s) { sync_component_state(s, CameraOrientation); return s.left_handed(); })
        .def("cache", [](CameraState& s, Index mode) { sync_component_state(s, CameraCenter | CameraZoom | CameraAngles); s.cache(mode); })
        .def("pop_cache", &CameraState::pop_cache)
        .def("switch_mode", [](CameraState& s, Index mode, const DimsState& dims) {
            sync_dims_dictionary_state(dims); sync_component_state(s, CameraCenter | CameraZoom | CameraAngles | CameraSynced);
            auto result = s.switch_mode(mode, dims); publish_component_state(s); return result;
        })
        .def("__copy__", [](const CameraState& s) { sync_component_state(s); return CameraState(s); })
        .def("__deepcopy__", [](const CameraState& s, nb::dict) { sync_component_state(s); return CameraState(s); })
        .def("__reduce__", [](const CameraState& s) {
            sync_component_state(s);
            auto save = [](const std::optional<CameraSnapshot>& value) -> nb::object {
                if (!value) return nb::none();
                return nb::make_tuple(value->center[0], value->center[1], value->center[2],
                                     value->angles[0], value->angles[1], value->angles[2], value->zoom);
            };
            return nb::make_tuple(nb::module_::import_("naparixx._restore").attr("camera"),
                nb::make_tuple(tuple_of(s.center), s.zoom, tuple_of(s.angles), s.perspective,
                    s.mouse_pan, s.mouse_zoom, tuple_of(s.orientation), s.synced,
                    save(s.cached_2d), save(s.cached_3d)));
        });

    nb::class_<GridLayout>(m, "GridLayout")
        .def_prop_ro("shape", [](const GridLayout& s) { return tuple_of(s.shape); })
        .def("position", [](const GridLayout& s, Index index) { return tuple_of(s.position(index)); })
        .def("contents", [](const GridLayout& s, Cell cell) { return tuple_of(s.contents(cell)); })
        .def("viewboxes", [](const GridLayout& s) {
            nb::list result;
            for (const auto& entry : s.viewboxes())
                result.append(nb::make_tuple(tuple_of(entry.first), tuple_of(entry.second)));
            return result;
        })
        .def("groups", [](const GridLayout& s) {
            nb::dict groups;
            for (size_t i = 0; i < s.group_of.size(); ++i) groups[nb::int_(i)] = nb::int_(s.group_of[i]);
            return nb::make_tuple(groups, nb::cast(s.occupied));
        });
    m.def("make_grid_state", [](nb::handle stride, nb::handle shape, bool enabled, double spacing) {
        GridState state;
        set_grid_stride(state, stride); set_grid_shape(state, shape);
        state.enabled = enabled; state.spacing = spacing;
        return state;
    });
    nb::class_<GridState>(m, "GridState", nb::is_weak_referenceable(), nb::dynamic_attr())
        .def(nb::init<>())
        .def_prop_ro("stride", [](const GridState& s) { sync_component_state(s, GridStride); return grid_stride_value(s); })
        .def_prop_ro("shape", [](const GridState& s) { sync_component_state(s, GridShape); return grid_shape_value(s); })
        .def_prop_ro("enabled", [](const GridState& s) { sync_component_state(s, GridEnabled); return s.enabled; })
        .def_prop_ro("spacing", [](const GridState& s) { sync_component_state(s, GridSpacing); return s.spacing; })
        .def("update", [](GridState& s, nb::handle stride, nb::handle shape, bool enabled, double spacing) {
            auto next = s;
            set_grid_stride(next, stride); set_grid_shape(next, shape);
            next.enabled = enabled; next.spacing = spacing;
            s = std::move(next);
            publish_component_state(s);
        })
        .def("layout", [](const GridState& s, const std::vector<bool>& visible) {
            sync_component_state(s, GridEnabled);
            if (s.enabled && !visible.empty()) sync_component_state(s, GridStride | GridShape);
            return s.layout(visible);
        })
        .def("effective_indices", &GridState::effective_indices)
        .def("canvas_spacing", [](const GridState& s, Cell canvas, const GridLayout& layout, bool safe) {
            sync_component_state(s, GridSpacing); return s.canvas_spacing(canvas, layout, safe);
        },
             nb::arg("canvas"), nb::arg("layout"), nb::arg("safe") = true)
        .def("viewbox_size", [](const GridState& s, Cell canvas, const GridLayout& layout) {
            sync_component_state(s, GridEnabled | GridSpacing);
            return tuple_of(s.viewbox_size(canvas, layout));
        })
        .def("__copy__", [](const GridState& s) { sync_component_state(s); return GridState(s); })
        .def("__deepcopy__", [](const GridState& s, nb::dict) { sync_component_state(s); return GridState(s); })
        .def("__reduce__", [](const GridState& s) {
            sync_component_state(s);
            return nb::make_tuple(nb::module_::import_("naparixx._restore").attr("grid"),
                                 nb::make_tuple(grid_stride_value(s), grid_shape_value(s), s.enabled, s.spacing));
        });

    nb::class_<ViewFit>(m, "ViewFit")
        .def_prop_ro("center", [](const ViewFit& s) { return tuple_of(s.center); })
        .def_ro("zoom", &ViewFit::zoom);
    m.def("view_center", [](const std::vector<double>& corner, const std::vector<double>& size, Index ndisplay) {
        return tuple_of(view_center(corner, size, ndisplay));
    });
    m.def("view_scale_factor", &scale_factor);
    m.def("view_zoom_2d", &zoom_2d);
    m.def("view_zoom_3d", [](Input<double> extent, Vec3 view, Vec3 up,
                             std::array<double, 2> canvas, double scale) {
        return zoom_3d(size_from_extent(extent), view, up, canvas, scale);
    });
    m.def("view_bounding_box", [](Input<double> extent, Vec3 view, Vec3 up) {
        const auto result = bounding_box(size_from_extent(extent), view, up);
        return from_vector(std::vector<double>(result.begin(), result.end()), {2});
    });
    m.def("fit_view", &fit_view);
}
} // namespace nx
