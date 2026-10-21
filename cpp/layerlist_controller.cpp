#include "layerlist_controller.hpp"
#include "collection_protocol.hpp"

namespace nx {
namespace nb = nanobind;
namespace {
using namespace collections;
nb::object g(const char* name) { return global("napari.components.layerlist", name); }
nb::object parent(nb::handle cls, nb::handle self) { return g("super")(cls, self); }
nb::object iterate(nb::handle value) { return checked(PyObject_GetIter(value.ptr())); }
nb::object singleton(nb::handle values) {
    nb::object source = iterate(values), first = next_value(source);
    if (!first.is_valid()) error(PyExc_ValueError, nb::str("not enough values to unpack (expected 1, got 0)"));
    if (next_value(source).is_valid()) error(PyExc_ValueError, nb::str("too many values to unpack (expected 1)"));
    return first;
}
nb::object imported(const char* module, const char* name) {
    return nb::module_::import_(module).attr(name);
}
nb::list attributes(nb::handle values, const char* name) {
    nb::list result;
    nb::object source = iterate(values);
    while (nb::object value = next_value(source)) result.append(value.attr(name));
    return result;
}
nb::list annotated(nb::handle self) {
    nb::list result;
    nb::object source = iterate(self);
    while (nb::object layer = next_value(source))
        if (!truth(layer.attr("_has_default_axis_labels")())) result.append(layer.attr("axis_labels"));
    return result;
}
nb::object empty_set() { return checked(PySet_New(nullptr)); }
void add_set(nb::handle values, nb::handle value) {
    if (PySet_Add(values.ptr(), value.ptr()) < 0) throw nb::python_error();
}
void selection_changed(nb::handle self) {
    nb::object emit = self.attr("selection").attr("events").attr("changed");
    emit(nb::arg("added") = nb::dict(), nb::arg("removed") = nb::dict());
}
void connect(nb::handle self, const char* event, const char* callback, bool disconnect = false) {
    nb::object method = self.attr("events").attr(event).attr(disconnect ? "disconnect" : "connect");
    method(self.attr(callback));
}

// Protocol adapters own Python references in native fields and expose them to
// GC explicitly. No borrowed iterator/model address depends on mutable attrs.
struct ValueIterator : IteratorState {
    nb::object source;
    unsigned kind = 0;
    void clear() { exhausted = true; source = nb::object(); }
};
int values_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(nb::handle(self))) Py_VISIT(nb::inst_ptr<ValueIterator>(nb::handle(self))->source.ptr());
    return 0;
}
int values_clear(PyObject* self) {
    if (nb::inst_ready(nb::handle(self))) nb::inst_ptr<ValueIterator>(nb::handle(self))->clear();
    return 0;
}
PyType_Slot values_slots[] = {{Py_tp_traverse, reinterpret_cast<void*>(values_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(values_clear)}, {0, nullptr}};
nb::object values_next(nb::handle self, nb::handle sent) {
    auto& state = *nb::inst_ptr<ValueIterator>(self);
    if (state.exhausted) throw nb::stop_iteration();
    Running active(state);
    if (!state.started && !sent.is_none()) throw nb::type_error("can't send non-None value to a just-started generator");
    state.started = true;
    try {
        nb::object value = next_value(state.source);
        if (value.is_valid()) {
            if (state.kind == 0) return value.attr("ndim");
            if (state.kind == 2) {
                nb::object repr = g("repr"); return repr(value.attr("name"));
            }
            nb::object factory = g("RangeTuple");
            nb::object args = checked(PySequence_Tuple(value.ptr()));
            return checked(PyObject_CallObject(factory.ptr(), args.ptr()));
        }
    } catch (const nb::python_error& failure) {
        state.clear();
        if (failure.matches(PyExc_StopIteration)) raise_exception(failure.value(), true);
        throw;
    }
    state.clear();
    throw nb::stop_iteration();
}
nb::object values_throw(nb::handle self, nb::args args) {
    auto& state = *nb::inst_ptr<ValueIterator>(self);
    Running active(state);
    nb::object exception = normalize_throw(args);
    const bool exhausted = state.exhausted;
    state.clear();
    raise_exception(exception, !exhausted);
}
void values_close(nb::handle self) {
    auto& state = *nb::inst_ptr<ValueIterator>(self);
    if (state.exhausted) return;
    Running active(state);
    state.clear();
}
nb::object values(nb::handle source, unsigned kind) {
    ValueIterator state;
    state.source = iterate(source); state.kind = kind;
    nb::object native = nb::cast(std::move(state));
    return g("_layerlist_yield_from")(native);
}

struct BatchContext : IteratorState {
    nb::object model;
    void clear() { exhausted = true; model = nb::object(); }
};
int batch_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(nb::handle(self))) Py_VISIT(nb::inst_ptr<BatchContext>(nb::handle(self))->model.ptr());
    return 0;
}
int batch_clear(PyObject* self) {
    if (nb::inst_ready(nb::handle(self))) nb::inst_ptr<BatchContext>(nb::handle(self))->clear();
    return 0;
}
PyType_Slot batch_slots[] = {{Py_tp_traverse, reinterpret_cast<void*>(batch_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(batch_clear)}, {0, nullptr}};
void batch_enter(nb::handle self) {
    auto& state = *nb::inst_ptr<BatchContext>(self);
    Running active(state);
    if (state.started || state.exhausted) throw nb::value_error("batch context is already entered or closed");
    state.started = true;
    nb::object model = state.model;
    try { model.attr("events").attr("begin_batch")(); }
    catch (const nb::python_error& failure) {
        state.clear();
        HandledException handled(failure.value());
        try { model.attr("events").attr("end_batch")(); }
        catch (const nb::python_error& cleanup) {
            if (cleanup.value().ptr() != failure.value().ptr())
                PyException_SetContext(cleanup.value().ptr(), failure.value().inc_ref().ptr());
            throw;
        }
        throw;
    }
}
void batch_exit(nb::handle self, nb::handle, nb::handle exception, nb::handle) {
    auto& state = *nb::inst_ptr<BatchContext>(self);
    Running active(state);
    if (state.exhausted) return;
    nb::object model = state.model;
    state.clear();
    if (!exception.is_none()) {
        HandledException handled(exception);
        model.attr("events").attr("end_batch")();
    } else model.attr("events").attr("end_batch")();
}
nb::object batch_context(nb::handle model) {
    BatchContext state; state.model = nb::borrow<nb::object>(model);
    return nb::cast(std::move(state));
}

void initialize(nb::handle self, nb::handle data, nb::handle cls) {
    set_attr(self, "_units", nb::none());
    nb::object init = parent(cls, self).attr("__init__");
    nb::object layer = g("Layer");
    nb::dict lookup; lookup[g("str")] = g("get_name");
    init(nb::arg("data") = data, nb::arg("basetype") = layer, nb::arg("lookup") = lookup);
    nb::object add = self.attr("events").attr("add");
    nb::object begin = g("Event"), end = g("Event"), renamed = g("Event"), units = g("Event");
    add(nb::arg("begin_batch") = begin, nb::arg("end_batch") = end,
        nb::arg("renamed") = renamed, nb::arg("units") = units);
    connect(self, "inserted", "_on_layer_inserted");
    connect(self, "removed", "_on_layer_removed");
    self.attr("_create_contexts")();
}
void contexts(nb::handle self) {
    nb::object create = imported("napari._app_model.context", "create_context");
    nb::module_ module = nb::module_::import_("napari._app_model.context._layerlist_context");
    nb::object keys = module.attr("LayerListContextKeys"), selected = module.attr("LayerListSelectionContextKeys");
    set_attr(self, "_ctx", create(self));
    if (!self.attr("_ctx").is_none()) {
        set_attr(self, "_ctx_keys", keys(self.attr("_ctx")));
        nb::object inserted = self.attr("events").attr("inserted").attr("connect");
        inserted(self.attr("_ctx_keys").attr("update"));
        nb::object removed = self.attr("events").attr("removed").attr("connect");
        removed(self.attr("_ctx_keys").attr("update"));
        set_attr(self, "_selection_ctx_keys", selected(self.attr("_ctx")));
        nb::object changed = self.attr("selection").attr("events").attr("changed").attr("connect");
        changed(self.attr("_selection_ctx_keys").attr("update"));
    }
}
void process_delete(nb::handle self, nb::handle layer, nb::handle cls) {
    parent(cls, self).attr("_process_delete_item")(layer);
    for (const char* event : {"extent", "_extent_augmented"}) {
        nb::object disconnect = layer.attr("events").attr(event).attr("disconnect");
        disconnect(self.attr("_clean_cache"));
    }
    nb::object disconnect = layer.attr("events").attr("locked").attr("disconnect");
    disconnect(self.attr("_refresh_selection_ctx_keys"));
    if (truth(g("hasattr")(layer.attr("events"), nb::str("locked_data_level")))) {
        disconnect = layer.attr("events").attr("locked_data_level").attr("disconnect");
        disconnect(self.attr("_refresh_selection_ctx_keys"));
    }
    nb::object unlink = self.attr("unlink_layers"); nb::list layers; layers.append(layer); unlink(layers);
    self.attr("_clean_cache")();
}
void clean_cache(nb::handle self) {
    for (const char* name : {"extent", "_extent_world", "_extent_world_augmented", "_step_size"})
        self.attr("__dict__").attr("pop")(nb::str(name), nb::none());
}
void setitem(nb::handle self, nb::handle key, nb::object value, nb::handle cls) {
    nb::object old = item(self.attr("_list"), key);
    if (truth(g("isinstance")(key, g("slice")))) value = self.attr("_ensure_unique")(value, old);
    else if (truth(g("isinstance")(key, g("int"))))
        value = singleton(self.attr("_ensure_unique")(nb::make_tuple(value), nb::make_tuple(old)));
    parent(cls, self).attr("__setitem__")(key, value);
}
void insert(nb::handle self, nb::handle index, nb::handle value, nb::handle cls) {
    nb::object unique = self.attr("_ensure_unique")(nb::make_tuple(value));
    nb::object checked_layer = self.attr("_type_check")(singleton(unique));
    nb::object coerce = self.attr("_coerce_name");
    set_attr(checked_layer, "name", coerce(checked_layer.attr("name")));
    self.attr("_clean_cache")();
    for (const char* event : {"extent", "_extent_augmented"}) {
        nb::object subscribe = checked_layer.attr("events").attr(event).attr("connect");
        subscribe(self.attr("_clean_cache"));
    }
    nb::object subscribe = checked_layer.attr("events").attr("data").attr("connect");
    subscribe(self.attr("_trigger_check_ndim_and_maybe_clean_units"));
    subscribe = checked_layer.attr("events").attr("locked").attr("connect");
    subscribe(self.attr("_refresh_selection_ctx_keys"));
    if (truth(g("hasattr")(checked_layer.attr("events"), nb::str("locked_data_level")))) {
        subscribe = checked_layer.attr("events").attr("locked_data_level").attr("connect");
        subscribe(self.attr("_refresh_selection_ctx_keys"));
    }
    parent(cls, self).attr("insert")(index, checked_layer);
    nb::object check = self.attr("_check_ndim_and_maybe_clean_units"); check(checked_layer.attr("ndim"));
}
void remove_selected(nb::handle self, nb::handle cls) {
    if (!truth(self.attr("selection"))) return;
    nb::object deletable = empty_set(), source = iterate(self.attr("selection"));
    while (nb::object layer = next_value(source)) if (!truth(layer.attr("locked"))) add_set(deletable, layer);
    nb::object locked = subtract(self.attr("selection"), deletable);
    if (truth(locked)) {
        nb::object notify = imported("napari.utils.notifications", "show_info");
        nb::object join = nb::str(", ").attr("join"), sort = g("sorted");
        nb::object key = nb::cpp_function([](nb::handle layer) -> nb::object { return layer.attr("name"); }, nb::arg("x").none());
        nb::object ordered = sort(locked, nb::arg("key") = key);
        nb::object joined = join(values(ordered, 2));
        notify(message({nb::str("Layer(s) "), formatted(joined), nb::str(" are locked and cannot be deleted.")}));
    }
    if (!truth(deletable)) return;
    self.attr("unlink_layers")(deletable);
    nb::object all = g("set")(self), remaining = checked(PyNumber_And(locked.ptr(), all.ptr()));
    self.attr("selection").attr("intersection_update")(deletable);
    with_context(self.attr("batched_update")(), [&] { parent(cls, self).attr("remove_selected")(); });
    if (truth(remaining)) {
        self.attr("selection").attr("clear")();
        self.attr("selection").attr("update")(remaining);
    }
}
void toggle_visible(nb::handle self) {
    nb::object source = iterate(self.attr("selection"));
    while (nb::object layer = next_value(source)) set_attr(layer, "visible", nb::bool_(!truth(layer.attr("visible"))));
}
nb::object world(nb::handle self, nb::handle extents, nb::handle augmented, nb::handle units) {
    nb::object minimum, maximum;
    if (compare(g("len")(self), nb::int_(0))) {
        nb::object zeros = g("np").attr("zeros"); minimum = zeros(self.attr("ndim"));
        nb::object full = g("np").attr("full"); maximum = full(self.attr("ndim"), nb::float_(511.0));
        if (truth(augmented)) {
            minimum = inplace_subtract(minimum, nb::float_(0.5));
            maximum = inplace_add(maximum, nb::float_(0.5));
        }
    } else {
        nb::list extrema;
        nb::object source = iterate(extents);
        while (nb::object extent = next_value(source)) {
            if (units.is_none()) extrema.append(extent.attr("world"));
            else {
                nb::object convert = self.attr("_convert_scale_between_units");
                nb::object scale = extent.attr("world").attr("T"), from = extent.attr("units");
                extrema.append(convert(scale, from, units).attr("T"));
            }
        }
        nb::list mins, maxs;
        for (nb::handle extent : extrema) mins.append(item(extent, nb::int_(0)));
        for (nb::handle extent : extrema) maxs.append(item(extent, nb::int_(1)));
        nb::tuple bounds = pair(self.attr("_get_min_and_max")(mins, maxs));
        minimum = bounds[0]; maximum = bounds[1];
    }
    nb::object stack = g("np").attr("vstack"); nb::list bounds; bounds.append(minimum); bounds.append(maximum);
    return stack(bounds);
}
nb::object cached_world(nb::handle self, bool augmented) {
    nb::object calculate = self.attr("_get_extent_world");
    nb::list extents = attributes(self, augmented ? "_extent_augmented" : "extent");
    nb::object units = self.attr("units");
    if (augmented) return calculate(extents, nb::arg("augmented") = true, nb::arg("units") = units);
    return calculate(extents, nb::arg("units") = units);
}
nb::object steps(nb::handle self, nb::handle extents, nb::handle units) {
    if (compare(g("len")(extents), nb::int_(0))) {
        nb::object ones = g("np").attr("ones"); return ones(self.attr("ndim"));
    }
    nb::list scales;
    nb::object source = iterate(extents);
    while (nb::object extent = next_value(source)) {
        if (units.is_none()) scales.append(extent.attr("step"));
        else {
            nb::object convert = self.attr("_convert_scale_between_units");
            nb::object scale = extent.attr("step"), from = extent.attr("units");
            scales.append(convert(scale, from, units));
        }
    }
    return self.attr("_step_size_from_scales")(scales);
}
nb::object extent(nb::handle self, nb::handle layers) {
    nb::list extents = attributes(layers, "extent");
    nb::object units = self.attr("_units");
    if (!truth(units)) units = self.attr("_get_units")(extents);
    nb::object factory = g("LayerListExtent");
    nb::object world = self.attr("_get_extent_world")(extents, nb::arg("units") = units);
    nb::object step = self.attr("_get_step_size")(extents, nb::arg("units") = units);
    return factory(nb::arg("data") = nb::none(), nb::arg("world") = world, nb::arg("step") = step, nb::arg("units") = units);
}
nb::object ranges(nb::handle self) {
    nb::object extent = self.attr("extent"), tuple = g("tuple"), zip = g("zip");
    nb::object first = item(extent.attr("world"), nb::int_(0)), second = item(extent.attr("world"), nb::int_(1));
    nb::object step = extent.attr("step");
    nb::object zipped = zip(first, second, step, nb::arg("strict") = false);
    return tuple(values(zipped, true));
}
nb::object get_units(nb::handle self) {
    if (self.attr("extent").attr("units").is_none()) return nb::none();
    nb::object override = self.attr("_units");
    return truth(override) ? override : nb::object(self.attr("extent").attr("units"));
}
void set_units(nb::handle self, nb::object units) {
    if (units.is_none()) {
        set_attr(self, "_units", nb::none()); self.attr("_clean_cache")();
        nb::object emit = self.attr("events").attr("units"); emit(nb::arg("value") = self.attr("units"));
        return;
    }
    if (self.attr("extent").attr("units").is_none())
        error(PyExc_ValueError, nb::str("Cannot set units when layers have inconsistent dimensionality."));
    nb::object count = g("len")(units), ndim = self.attr("ndim");
    if (compare(count, ndim, Py_LT)) error(PyExc_ValueError, nb::str("Number of units must be at least the number of dimensions."));
    units = g("get_units_from_name")(units);
    nb::object enumerate = g("enumerate"), zip = g("zip");
    nb::object reversed = events::slice(units, nb::none(), nb::none(), nb::int_(-1));
    nb::object old = self.attr("units"); old = events::slice(old, nb::none(), nb::none(), nb::int_(-1));
    nb::object source = iterate(enumerate(zip(reversed, old, nb::arg("strict") = false), nb::arg("start") = 1));
    while (nb::object entry = next_value(source)) {
        nb::tuple indexed = pair(entry), unit_pair = pair(indexed[1]);
        nb::object new_unit = unit_pair[0], old_unit = unit_pair[1];
        nb::object new_dimension = new_unit.attr("dimensionality"), old_dimension = old_unit.attr("dimensionality");
        if (compare(new_dimension, old_dimension, Py_NE)) {
            error(PyExc_ValueError, message({nb::str("On axis -"), formatted(indexed[0]),
                nb::str(" units must be consistent across all dimensions.The new dimensionality is "),
                formatted(new_unit.attr("dimensionality")), nb::str(" while the previous dimensionality was "),
                formatted(old_unit.attr("dimensionality")), nb::str(".")}));
        }
    }
    set_attr(self, "_units", units); self.attr("_clean_cache")();
    nb::object emit = self.attr("events").attr("units"); emit(nb::arg("value") = self.attr("units"));
}
void check_units(nb::handle self, nb::handle ndim) {
    if (!self.attr("_units").is_none()) {
        nb::object units = self.attr("_units"), count = g("len")(units);
        if (compare(ndim, count, Py_GT)) {
            g("warnings").attr("warn")(nb::str("New layer has more dimensions than the current units, dropping units override."), nb::arg("stacklevel") = 2);
            set_attr(self, "units", nb::none());
        }
    }
}
nb::object ndim(nb::handle self) {
    nb::object maximum = g("max"); return maximum(values(self, false), nb::arg("default") = 2);
}
nb::object longest(nb::handle self) {
    nb::list labels = annotated(self);
    nb::object reversed = events::slice(labels, nb::none(), nb::none(), nb::int_(-1));
    nb::object maximum = g("max"), key = g("len");
    return maximum(reversed, nb::arg("key") = key, nb::arg("default") = nb::tuple());
}
nb::object axis_labels(nb::handle self) {
    nb::object calculate = g("_native").attr("layer_axis_labels"), dimensions = self.attr("ndim");
    nb::list labels = annotated(self); return calculate(dimensions, labels);
}
void link(nb::handle self, nb::handle method, nb::object layers, nb::handle attributes) {
    nb::object helpers = imported("napari.layers.utils", "_link_layers");
    if (!layers.is_none()) {
        nb::list result; nb::object source = iterate(layers);
        while (nb::object layer = next_value(source)) {
            if (truth(g("isinstance")(layer, g("str")))) result.append(item(self, layer));
            else result.append(layer);
        }
        layers = result;
    } else layers = nb::borrow<nb::object>(self);
    nb::object function = g("getattr")(helpers, method); function(layers, attributes);
    selection_changed(self);
}
nb::object save(nb::handle self, nb::handle path, nb::handle selected, nb::handle plugin, nb::handle writer) {
    nb::object function = imported("napari.plugins.io", "save_layers"), layers;
    if (truth(selected)) {
        nb::list chosen; nb::object source = iterate(self);
        while (nb::object layer = next_value(source)) if (contains(self.attr("selection"), layer)) chosen.append(layer);
        layers = chosen;
    } else layers = g("list")(self);
    nb::str text(truth(selected) ? "No layers selected" : "No layers to save");
    if (!truth(layers)) { g("warnings").attr("warn")(text); return nb::list(); }
    return function(path, layers, nb::arg("plugin") = plugin, nb::arg("_writer") = writer);
}
} // namespace

void bind_layerlist_controller(nb::module_& m) {
    nb::class_<ValueIterator>(m, "LayerListValueIterator", nb::type_slots(values_slots), nb::is_weak_referenceable())
        .def("__iter__", [](nb::object self) { return self; })
        .def("__next__", [](nb::handle self) { return values_next(self, nb::none()); })
        .def("send", &values_next, nb::arg("value").none())
        .def("throw", &values_throw).def("close", &values_close);
    nb::class_<BatchContext>(m, "LayerListBatchContext", nb::type_slots(batch_slots), nb::is_weak_referenceable())
        .def("__enter__", &batch_enter)
        .def("__exit__", &batch_exit, nb::arg("type").none(), nb::arg("value").none(), nb::arg("traceback").none());
    m.def("layerlist_get_name", [](nb::handle layer) -> nb::object { return layer.attr("name"); }, nb::arg("layer").none());
    m.def("layerlist_init", &initialize, nb::arg("self").none(), nb::arg("data").none(), nb::arg("cls"));
    m.def("layerlist_inserted", [](nb::handle self, nb::handle event) {
        nb::object layer = event.attr("value"), subscribe = layer.attr("events").attr("name").attr("connect");
        subscribe(self.attr("_on_layer_renamed"));
    }, nb::arg("self").none(), nb::arg("event").none());
    m.def("layerlist_removed", [](nb::handle self, nb::handle event) {
        nb::object layer = event.attr("value"), disconnect = layer.attr("events").attr("name").attr("disconnect");
        disconnect(self.attr("_on_layer_renamed"));
    }, nb::arg("self").none(), nb::arg("event").none());
    m.def("layerlist_renamed", [](nb::handle self, nb::handle event) {
        nb::object layer = event.attr("source"), index = self.attr("index")(layer);
        self.attr("events").attr("renamed")(nb::arg("index") = index);
    }, nb::arg("self").none(), nb::arg("event").none());
    m.def("layerlist_contexts", &contexts, nb::arg("self").none());
    m.def("layerlist_delete", &process_delete, nb::arg("self").none(), nb::arg("layer").none(), nb::arg("cls"));
    m.def("layerlist_refresh_selection", [](nb::handle self, nb::handle) { selection_changed(self); }, nb::arg("self").none(), nb::arg("event").none());
    m.def("layerlist_clean_cache", &clean_cache, nb::arg("self").none());
    m.def("layerlist_newlike", [](nb::handle, nb::handle data) -> nb::object { return g("LayerList")(data); }, nb::arg("self").none(), nb::arg("data").none());
    m.def("layerlist_update_name", [](nb::handle self, nb::handle event) {
        nb::object layer = event.attr("source"), coerce = self.attr("_coerce_name"), name = layer.attr("name");
        set_attr(layer, "name", coerce(name, layer));
    }, nb::arg("self").none(), nb::arg("event").none());
    m.def("layerlist_getitem", [](nb::handle self, nb::handle key, nb::handle cls) -> nb::object { return parent(cls, self).attr("__getitem__")(key); }, nb::arg("self").none(), nb::arg("key").none(), nb::arg("cls"));
    m.def("layerlist_setitem", &setitem, nb::arg("self").none(), nb::arg("key").none(), nb::arg("value").none(), nb::arg("cls"));
    m.def("layerlist_insert", &insert, nb::arg("self").none(), nb::arg("index").none(), nb::arg("value").none(), nb::arg("cls"));
    m.def("layerlist_remove_selected", &remove_selected, nb::arg("self").none(), nb::arg("cls"));
    m.def("layerlist_toggle_visible", &toggle_visible, nb::arg("self").none());
    m.def("layerlist_cached_world", &cached_world, nb::arg("self").none(), nb::arg("augmented"));
    m.def("layerlist_extent_world", &world, nb::arg("self").none(), nb::arg("extents").none(), nb::arg("augmented").none(), nb::arg("units").none());
    m.def("layerlist_cached_step", [](nb::handle self) -> nb::object {
        nb::object function = self.attr("_get_step_size"); return function(attributes(self, "extent"));
    }, nb::arg("self").none());
    m.def("layerlist_step", &steps, nb::arg("self").none(), nb::arg("extents").none(), nb::arg("units").none());
    m.def("layerlist_get_extent", &extent, nb::arg("self").none(), nb::arg("layers").none());
    m.def("layerlist_cached_extent", [](nb::handle self) -> nb::object {
        nb::object function = self.attr("get_extent"), layers = g("list")(self); return function(layers);
    }, nb::arg("self").none());
    m.def("layerlist_ranges", &ranges, nb::arg("self").none());
    m.def("layerlist_units", &get_units, nb::arg("self").none());
    m.def("layerlist_set_units", &set_units, nb::arg("self").none(), nb::arg("units").none());
    m.def("layerlist_trigger_units", [](nb::handle self, nb::handle event) {
        nb::object function = self.attr("_check_ndim_and_maybe_clean_units"); function(event.attr("source").attr("ndim"));
    }, nb::arg("self").none(), nb::arg("event").none());
    m.def("layerlist_check_units", &check_units, nb::arg("self").none(), nb::arg("ndim").none());
    m.def("layerlist_ndim", &ndim, nb::arg("self").none());
    m.def("layerlist_longest_labels", &longest, nb::arg("self").none());
    m.def("layerlist_axis_labels", &axis_labels, nb::arg("self").none());
    m.def("layerlist_link", &link, nb::arg("self").none(), nb::arg("method").none(), nb::arg("layers").none(), nb::arg("attributes").none());
    m.def("layerlist_link_layers", [](nb::handle self, nb::handle layers, nb::handle attributes) -> nb::object {
        return self.attr("_link_layers")(nb::str("link_layers"), layers, attributes);
    }, nb::arg("self").none(), nb::arg("layers").none(), nb::arg("attributes").none());
    m.def("layerlist_unlink_layers", [](nb::handle self, nb::handle layers, nb::handle attributes) -> nb::object {
        return self.attr("_link_layers")(nb::str("unlink_layers"), layers, attributes);
    }, nb::arg("self").none(), nb::arg("layers").none(), nb::arg("attributes").none());
    m.def("layerlist_save", &save, nb::arg("self").none(), nb::arg("path").none(), nb::arg("selected").none(), nb::arg("plugin").none(), nb::arg("writer").none());
    m.def("layerlist_clear", [](nb::handle self, nb::handle cls) {
        with_context(self.attr("batched_update")(), [&] { parent(cls, self).attr("clear")(); });
    }, nb::arg("self").none(), nb::arg("cls"));
    m.def("layerlist_batch_context", &batch_context, nb::arg("self").none());
}
} // namespace nx
