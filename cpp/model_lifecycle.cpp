#include "model_lifecycle.hpp"
#include "collection_protocol.hpp"
#include "component_state.hpp"
#include "model_transaction.hpp"

namespace nx {
namespace nb = nanobind;
namespace {
using namespace collections;

constexpr const char* camera_module = "napari.components.camera";
constexpr const char* grid_module = "napari.components.grid";
constexpr const char* model_module = "napari.utils.events.evented_model";
constexpr const char* misc_module = "napari.utils.misc";

nb::object copy_state(nb::handle self, nb::handle parent, const char* module,
                      const char* attribute) {
    nb::object result = parent.attr("__copy__")();
    nb::object copy = global(module, "copy");
    nb::object state = self.attr(attribute);
    set_attr(result, attribute, copy(state));
    if (module != model_module) attach_component_state(result);
    return result;
}

nb::object copy_model(nb::handle parent, nb::handle update, nb::handle deep) {
    nb::object result = parent.attr("model_copy")(nb::arg("update") = update,
                                                 nb::arg("deep") = deep);
    result.attr("_sync_native_state")();
    return result;
}

nb::object camera_sync(nb::handle self) {
    nb::object state = self.attr("_native_state"), update = state.attr("update");
    ComponentSyncGuard guard(self, state);
    nb::object center = self.attr("center"), zoom = self.attr("zoom"), angles = self.attr("angles");
    nb::object perspective = self.attr("perspective"), pan = self.attr("mouse_pan"), mouse_zoom = self.attr("mouse_zoom");
    nb::object tuple = global(camera_module, "tuple"), map = global(camera_module, "map");
    nb::object string = global(camera_module, "str"), orientation = self.attr("orientation");
    nb::object directions = tuple(map(string, orientation));
    nb::object synced = self.attr("synced");
    update(center, zoom, angles, perspective, pan, mouse_zoom, directions, synced);
    return nb::borrow<nb::object>(self);
}

nb::object ensure_tuple(nb::handle value, nb::handle n, nb::handle fill,
                         nb::handle before, nb::handle debug) {
    if (truth(debug) && !truth(checked(PyObject_RichCompare(n.ptr(), nb::int_(0).ptr(), Py_GT))))
        error(PyExc_AssertionError, nb::str("n must be greater than 0"));
    nb::object tuple = global(misc_module, "tuple"), values = tuple(value);
    if (truth(before)) {
        nb::tuple item = nb::make_tuple(fill);
        nb::object length = global(misc_module, "len")(values);
        nb::object count = subtract(n, length);
        nb::object padding = checked(PyNumber_Multiply(item.ptr(), count.ptr()));
        nb::object negative = checked(PyNumber_Negative(n.ptr()));
        nb::object selected = events::slice(values, negative, nb::none());
        return add(padding, selected);
    }
    nb::object selected = events::slice(values, nb::none(), n);
    nb::tuple item = nb::make_tuple(fill);
    nb::object length = global(misc_module, "len")(values);
    nb::object count = subtract(n, length);
    nb::object padding = checked(PyNumber_Multiply(item.ptr(), count.ptr()));
    return add(selected, padding);
}
} // namespace

void bind_model_lifecycle(nb::module_& m) {
    m.def("model_shallow_copy", [](nb::handle self, nb::handle parent) {
        return copy_state(self, parent, model_module, "_change_state");
    }, nb::arg("self").none(), nb::arg("parent"));
    m.def("model_deep_copy", [](nb::handle, nb::handle parent, nb::handle memo) {
        nb::object result = parent.attr("__deepcopy__")(memo);
        set_attr(result, "_primary_view", nb::none());
        return result;
    }, nb::arg("self").none(), nb::arg("parent"), nb::arg("memo").none());
    m.def("model_semaphore_get", [](nb::handle self) -> nb::object {
        if (has_model_transaction_owner(self)) return self.attr("_delay_check_semaphore");
        return self.attr("_change_state").attr("depth");
    }, nb::arg("self").none());
    m.def("model_semaphore_set", [](nb::handle self, nb::handle value) {
        if (has_model_transaction_owner(self)) {
            set_attr(self, "_delay_check_semaphore", value);
            return;
        }
        set_attr(self.attr("_change_state"), "depth", value);
    }, nb::arg("self").none(), nb::arg("value").none());
    m.def("model_delayer_initialize", [](nb::handle self, nb::handle target) {
        set_attr(self, "_target", target);
    }, nb::arg("self").none(), nb::arg("target").none());

    m.def("camera_sync", &camera_sync, nb::arg("self").none());
    m.def("camera_shallow_copy", [](nb::handle self, nb::handle parent) {
        return copy_state(self, parent, camera_module, "_native_state");
    }, nb::arg("self").none(), nb::arg("parent"));
    m.def("camera_copy_model", [](nb::handle, nb::handle parent, nb::handle update, nb::handle deep) {
        return copy_model(parent, update, deep);
    }, nb::arg("self").none(), nb::arg("parent"), nb::arg("update").none(), nb::arg("deep").none());
    m.def("camera_pop_cache", [](nb::handle self, nb::handle mode) -> nb::object {
        nb::object state = self.attr("_native_state").attr("pop_cache")(mode);
        if (state.is_none()) return nb::none();
        nb::object factory = global(camera_module, "_CameraState");
        nb::object center = state.attr("center"), zoom = state.attr("zoom"), angles = state.attr("angles");
        return factory(center, zoom, angles);
    }, nb::arg("self").none(), nb::arg("mode").none());
    m.def("camera_set_direction", [](nb::handle self, nb::handle view, nb::handle up) {
        nb::object result = self.attr("_native_state").attr("angles_for_directions")(view, up);
        set_attr(self, "angles", result);
    }, nb::arg("self").none(), nb::arg("view").none(), nb::arg("up").none());
    m.def("camera_set_orientation2d", [](nb::handle self, nb::handle value) {
        nb::object existing = item(self.attr("orientation"), nb::int_(0));
        nb::object vertical = global(camera_module, "VerticalAxisOrientation");
        nb::object first = vertical(item(value, nb::int_(0)));
        nb::object horizontal = global(camera_module, "HorizontalAxisOrientation");
        nb::object second = horizontal(item(value, nb::int_(1)));
        set_attr(self, "orientation", nb::make_tuple(existing, first, second));
    }, nb::arg("self").none(), nb::arg("value").none());

    m.def("grid_sync", [](nb::handle self) -> nb::object {
        nb::object state = self.attr("_native_state"), update = state.attr("update");
        ComponentSyncGuard guard(self, state);
        nb::object stride = self.attr("stride"), shape = self.attr("shape"), enabled = self.attr("enabled"), spacing = self.attr("spacing");
        update(stride, shape, enabled, spacing);
        return nb::borrow<nb::object>(self);
    }, nb::arg("self").none());
    m.def("grid_shallow_copy", [](nb::handle self, nb::handle parent) {
        return copy_state(self, parent, grid_module, "_native_state");
    }, nb::arg("self").none(), nb::arg("parent"));
    m.def("grid_copy_model", [](nb::handle, nb::handle parent, nb::handle update, nb::handle deep) {
        return copy_model(parent, update, deep);
    }, nb::arg("self").none(), nb::arg("parent"), nb::arg("update").none(), nb::arg("deep").none());
    m.def("misc_ensure_n_tuple", &ensure_tuple, nb::arg("value").none(), nb::arg("n").none(), nb::arg("fill").none(), nb::arg("before").none(), nb::arg("debug").none());
}
} // namespace nx
