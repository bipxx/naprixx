#include "grid_controller.hpp"
#include "collection_protocol.hpp"
#include "iterator_frame.hpp"
#include "component_state.hpp"

namespace nx {
namespace nb = nanobind;
namespace {
using namespace collections;
constexpr const char* module_name = "napari.components.grid";
nb::object g(const char* name) { return global(module_name, name); }
nb::object multiply(nb::handle a, nb::handle b) { return checked(PyNumber_Multiply(a.ptr(), b.ptr())); }
nb::object divide(nb::handle a, nb::handle b) { return checked(PyNumber_TrueDivide(a.ptr(), b.ptr())); }
nb::object floor_divide(nb::handle a, nb::handle b) { return checked(PyNumber_FloorDivide(a.ptr(), b.ptr())); }
nb::object remainder(nb::handle a, nb::handle b) { return checked(PyNumber_Remainder(a.ptr(), b.ptr())); }
nb::object iterator(nb::handle value) { return checked(PyObject_GetIter(value.ptr())); }

nb::list effective(nb::handle self, nb::handle layers) {
    refresh_component_model(self);
    nb::object enumerate = g("enumerate");
    nb::object source = iterator(enumerate(layers));
    nb::list result;
    while (nb::object entry = next_value(source)) {
        nb::tuple unpacked = pair(entry);
        if (truth(unpacked[1].attr("visible"))) result.append(unpacked[0]);
    }
    return result;
}

nb::tuple groups(nb::handle self, nb::handle layers) {
    refresh_component_model(self);
    nb::object absolute = g("abs"), stride = absolute(self.attr("stride"));
    nb::object count = g("len")(layers);
    const bool positive = compare(self.attr("stride"), nb::int_(0), Py_GT);
    nb::object source = iterator(g("range")(count));
    nb::dict group_of;
    while (nb::object index = next_value(source)) {
        nb::object adjusted = positive ? index : subtract(subtract(count, nb::int_(1)), index);
        set_item(group_of, index, floor_divide(adjusted, stride));
    }
    nb::object visible = self.attr("_effective_indices")(layers);
    nb::object sorted = g("sorted");
    nb::object occupied = checked(PySet_New(nullptr));
    source = iterator(visible);
    while (nb::object index = next_value(source)) {
        nb::object group = item(group_of, index);
        if (PySet_Add(occupied.ptr(), group.ptr()) < 0) throw nb::python_error();
    }
    return nb::make_tuple(group_of, sorted(occupied));
}

nb::tuple actual_shape(nb::handle self, nb::handle layers) {
    refresh_component_model(self);
    if (!truth(self.attr("enabled")) || !truth(layers)
        || !truth(self.attr("_effective_indices")(layers))) return nb::make_tuple(1, 1);
    nb::tuple dimensions = pair(self.attr("shape"));
    nb::object rows = dimensions[0], columns = dimensions[1];
    nb::tuple grouped = pair(self.attr("_viewbox_groups")(layers));
    nb::object cells = g("len")(grouped[1]);
    if (compare(rows, nb::int_(-1)) && compare(columns, nb::int_(-1))) {
        nb::object ceiling = g("np").attr("ceil"), root = g("np").attr("sqrt");
        nb::object rounded = ceiling(root(cells)), convert = rounded.attr("astype");
        columns = convert(g("int"));
        // The original captures np.ceil before evaluating the division.
        ceiling = g("np").attr("ceil");
        rounded = ceiling(divide(cells, columns));
        convert = rounded.attr("astype");
        rows = convert(g("int"));
    } else if (compare(rows, nb::int_(-1))) {
        nb::object ceiling = g("np").attr("ceil");
        nb::object rounded = ceiling(divide(cells, columns)), convert = rounded.attr("astype");
        rows = convert(g("int"));
    } else if (compare(columns, nb::int_(-1))) {
        nb::object ceiling = g("np").attr("ceil");
        nb::object rounded = ceiling(divide(cells, rows)), convert = rounded.attr("astype");
        columns = convert(g("int"));
    }
    rows = g("max")(nb::int_(1), rows);
    columns = g("max")(nb::int_(1), columns);
    nb::object first = g("int")(rows), second = g("int")(columns);
    return nb::make_tuple(first, second);
}

nb::tuple position(nb::handle self, nb::handle index, nb::handle layers) {
    refresh_component_model(self);
    if (!truth(self.attr("enabled")) || !truth(layers)) return nb::make_tuple(0, 0);
    if (compare(index, nb::int_(0), Py_LT) || compare(index, g("len")(layers), Py_GE)) {
        nb::object text_index = formatted(index), count = g("len")(layers);
        error(PyExc_ValueError, message({nb::str("Index "), text_index,
            nb::str(" is out of bounds for number of layers "), formatted(count), nb::str(".")}));
    }
    nb::object visible = self.attr("_effective_indices")(layers);
    if (!contains(visible, index)) return nb::make_tuple(-1, -1);
    nb::tuple shape = pair(self.attr("actual_shape")(layers));
    nb::tuple grouped = pair(self.attr("_viewbox_groups")(layers));
    nb::object find = grouped[1].attr("index"), group = item(grouped[0], index);
    nb::object adjusted = find(group);
    adjusted = remainder(adjusted, multiply(shape[0], shape[1]));
    nb::object row = floor_divide(adjusted, shape[1]), column = remainder(adjusted, shape[1]);
    nb::object first = g("int")(row), second = g("int")(column);
    return nb::make_tuple(first, second);
}

nb::object spacing_raw(nb::handle self, nb::handle canvas, nb::handle layers) {
    refresh_component_model(self);
    nb::tuple shape = pair(self.attr("actual_shape")(layers)), size = pair(canvas);
    nb::object spacing = self.attr("spacing");
    if (compare(spacing, nb::int_(1), Py_GE)) return g("int")(spacing);
    nb::object width = divide(size[0], shape[1]), height = divide(size[1], shape[0]);
    nb::object mean = g("np").attr("mean")(nb::make_tuple(width, height));
    nb::object convert = g("int");
    return convert(multiply(spacing, mean));
}
nb::object spacing_safe(nb::handle self, nb::handle canvas, nb::handle layers) {
    refresh_component_model(self);
    nb::tuple shape = pair(self.attr("actual_shape")(layers)), size = pair(canvas);
    nb::object numerator = subtract(size[0], multiply(shape[1], nb::int_(20)));
    nb::object maximum = g("max"), denominator = maximum(nb::int_(1), subtract(shape[1], nb::int_(1)));
    nb::object horizontal = divide(numerator, denominator);
    numerator = subtract(size[1], multiply(shape[0], nb::int_(20)));
    maximum = g("max");
    denominator = maximum(nb::int_(1), subtract(shape[0], nb::int_(1)));
    nb::object vertical = divide(numerator, denominator);
    nb::object safe_max = g("min")(horizontal, vertical);
    maximum = g("max");
    nb::object safe = maximum(nb::int_(0), g("int")(safe_max));
    nb::object minimum = g("min"), raw = self.attr("_compute_canvas_spacing_raw")(canvas, layers);
    return minimum(raw, safe);
}

struct GridIterator : IteratorState, IteratorFrame {
    enum Kind { Viewboxes, Contents, Visibility } kind = Viewboxes;
    void gc_clear() {
        exhausted = true;
        release_frame();
    }
};
nb::object advance(nb::handle self, GridIterator& state) {
    nb::dict values = frame(self);
    if (!state.started) {
        state.started = true;
        if (state.kind == GridIterator::Viewboxes) {
            nb::object indices = g("np").attr("ndindex");
            nb::object shape = values["model"].attr("actual_shape")(values["layers"]);
            values["iterator"] = iterator(indices(shape));
        }
    }
    nb::object source = values["iterator"];
    while (nb::object current = next_value(source)) {
        if (state.kind == GridIterator::Visibility) return current.attr("visible");
        if (state.kind == GridIterator::Contents) {
            nb::object result = values["model"].attr("position")(current, values["layers"]);
            if (compare(result, values["position"])) return current;
        } else {
            nb::tuple coordinates = pair(current);
            values["coordinates"] = coordinates;
            nb::tuple location = nb::make_tuple(coordinates[0], coordinates[1]);
            nb::object contents = values["model"].attr("contents_at")(
                nb::make_tuple(coordinates[0], coordinates[1]), values["layers"]);
            return nb::make_tuple(location, contents);
        }
    }
    return {};
}
nb::object make_iterator(GridIterator::Kind kind, nb::dict values) {
    GridIterator state;
    state.kind = kind;
    nb::object result = nb::cast(state);
    nb::inst_ptr<GridIterator>(result)->frame_owner = values;
    return result;
}
nb::object contents(nb::handle self, nb::handle location, nb::handle layers) {
    refresh_component_model(self);
    nb::object tuple = g("tuple"), range = g("range"), count = g("len")(layers);
    nb::dict values;
    values["model"] = self; values["layers"] = layers; values["position"] = location;
    values["iterator"] = iterator(range(count));
    return tuple(make_iterator(GridIterator::Contents, values));
}
nb::object layout(nb::handle self, nb::handle layers) {
    nb::object call = self.attr("_native_state").attr("layout"), tuple = g("tuple");
    nb::dict values;
    values["iterator"] = iterator(layers);
    return call(tuple(make_iterator(GridIterator::Visibility, values)));
}
} // namespace

void bind_grid_controller(nb::module_& m) {
    nb::class_<GridIterator>(m, "GridIterator", nb::type_slots(iterator_frame_slots<GridIterator>()), nb::is_weak_referenceable())
        .def_prop_ro("frame", [](const GridIterator& state) { return state.frame(); })
        .def("__iter__", [](nb::object self) -> nb::object { return self; })
        .def("__next__", [](nb::handle self) { return resume<GridIterator>(self, nb::none(), advance); })
        .def("send", [](nb::handle self, nb::object sent) { return resume<GridIterator>(self, sent, advance); }, nb::arg("value").none())
        .def("close", &close<GridIterator>)
        .def("throw", &throw_into<GridIterator>);
    m.def("grid_actual_shape", &actual_shape, nb::arg("self").none(), nb::arg("layers").none());
    m.def("grid_position", &position, nb::arg("self").none(), nb::arg("index").none(), nb::arg("layers").none());
    m.def("grid_contents", &contents, nb::arg("self").none(), nb::arg("position").none(), nb::arg("layers").none());
    m.def("grid_viewboxes", [](nb::handle self, nb::handle layers) {
        nb::dict values; values["model"] = self; values["layers"] = layers;
        return make_iterator(GridIterator::Viewboxes, values);
    }, nb::arg("self").none(), nb::arg("layers").none());
    m.def("grid_spacing", &spacing_safe, nb::arg("self").none(), nb::arg("canvas_size").none(), nb::arg("layers").none());
    m.def("grid_spacing_raw", &spacing_raw, nb::arg("self").none(), nb::arg("canvas_size").none(), nb::arg("layers").none());
    m.def("grid_effective_indices", &effective, nb::arg("self").none(), nb::arg("layers").none());
    m.def("grid_groups", &groups, nb::arg("self").none(), nb::arg("layers").none());
    m.def("grid_layout", &layout, nb::arg("self").none(), nb::arg("layers").none());
}
} // namespace nx
