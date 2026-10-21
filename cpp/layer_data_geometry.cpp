#include "arrays.hpp"
#include <array>
#include <utility>

namespace nx {
namespace {

nb::object lg_checked(PyObject* value) {
    if (!value) throw nb::python_error();
    return nb::steal<nb::object>(value);
}
nb::object lg_attr(nb::object value, const char* name) {
    return lg_checked(PyObject_GetAttrString(value.ptr(), name));
}
nb::object lg_item(nb::object value, nb::object index) {
    return lg_checked(PyObject_GetItem(value.ptr(), index.ptr()));
}
nb::object lg_call0(nb::object callback) {
    return lg_checked(PyObject_CallNoArgs(callback.ptr()));
}
nb::object lg_call1(nb::object callback, nb::object value) {
    return lg_checked(PyObject_CallOneArg(callback.ptr(), value.ptr()));
}
nb::object lg_call2(nb::object callback, nb::object first, nb::object second) {
    return lg_checked(PyObject_CallFunctionObjArgs(callback.ptr(), first.ptr(), second.ptr(), nullptr));
}
nb::object lg_kw(nb::object callback, nb::tuple args, nb::dict kwargs) {
    return lg_checked(PyObject_Call(callback.ptr(), args.ptr(), kwargs.ptr()));
}
bool lg_truth(nb::object value) {
    int result = PyObject_IsTrue(value.ptr());
    if (result < 0) throw nb::python_error();
    return result != 0;
}
nb::object lg_compare(nb::object first, nb::object second, int operation) {
    return lg_checked(PyObject_RichCompare(first.ptr(), second.ptr(), operation));
}
void lg_replace(nb::object& target, nb::object value) {
    nb::object old = std::move(target);
    target = std::move(value);
    old.reset();
}
nb::object lg_optional(const nb::object& value) {
    return value.is_valid() ? value : nb::none();
}
struct GeometryNamespace {
    nb::dict globals;
    nb::object builtins;
    explicit GeometryNamespace(nb::dict value)
        : globals(std::move(value)), builtins(nb::borrow<nb::object>(PyEval_GetBuiltins())) {}
    nb::object get(const char* name) const {
        nb::object key = nb::str(name);
        PyObject* result = PyDict_GetItemWithError(globals.ptr(), key.ptr());
        if (result) return nb::borrow<nb::object>(result);
        if (PyErr_Occurred()) throw nb::python_error();
        result = PyObject_GetItem(builtins.ptr(), key.ptr());
        if (result) return nb::steal<nb::object>(result);
        if (!PyErr_ExceptionMatches(PyExc_KeyError)) throw nb::python_error();
        PyErr_Clear();
        nb::object message = lg_checked(PyUnicode_FromFormat("name '%s' is not defined", name));
        nb::object error = lg_call1(nb::borrow<nb::object>(PyExc_NameError), std::move(message));
        if (PyObject_SetAttrString(error.ptr(), "name", key.ptr()) < 0) throw nb::python_error();
        PyErr_SetObject(PyExc_NameError, error.ptr());
        throw nb::python_error();
    }
    nb::object numpy(const char* name) const { return lg_attr(get("np"), name); }
};

// These are source-assigned locals only. The actual Layer namespace, transform
// objects and cached_property descriptors remain the sole state/cache authority.
struct LayerGeometryLocals {
    nb::object mode, res, extent_data, data_to_world, extent_world;
    nb::object D, full_data_extent, full_world_extent, world_extent;
    LayerGeometryLocals() = default;
    LayerGeometryLocals(LayerGeometryLocals&&) = default;
    ~LayerGeometryLocals() { clear(); }
    void clear() {
        std::array<nb::object, 9> previous = {
            std::move(mode), std::move(res), std::move(extent_data),
            std::move(data_to_world), std::move(extent_world), std::move(D),
            std::move(full_data_extent), std::move(full_world_extent), std::move(world_extent),
        };
        for (auto& value : previous) value.reset();
    }
};
int lg_locals_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) {
        const auto& s = *nb::inst_ptr<LayerGeometryLocals>(self);
        Py_VISIT(s.mode.ptr()); Py_VISIT(s.res.ptr()); Py_VISIT(s.extent_data.ptr());
        Py_VISIT(s.data_to_world.ptr()); Py_VISIT(s.extent_world.ptr()); Py_VISIT(s.D.ptr());
        Py_VISIT(s.full_data_extent.ptr()); Py_VISIT(s.full_world_extent.ptr()); Py_VISIT(s.world_extent.ptr());
    }
    return 0;
}
int lg_locals_clear(PyObject* self) {
    if (nb::inst_ready(self)) nb::inst_ptr<LayerGeometryLocals>(self)->clear();
    return 0;
}
PyType_Slot lg_locals_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(lg_locals_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(lg_locals_clear)}, {0, nullptr},
};

nb::object layer_geometry_projection_get(nb::object self) {
    return lg_attr(std::move(self), "_projection_mode");
}
void layer_geometry_projection_set(nb::object self, nb::dict namespace_, LayerGeometryLocals& owner) {
    GeometryNamespace ns(std::move(namespace_));
    nb::object normalizer = lg_attr(self, "_projectionclass");
    nb::object text = lg_call1(ns.get("str"), owner.mode);
    lg_replace(owner.mode, lg_call1(std::move(normalizer), std::move(text)));
    if (lg_truth(lg_compare(lg_attr(self, "_projection_mode"), owner.mode, Py_NE))) {
        if (PyObject_SetAttrString(self.ptr(), "_projection_mode", owner.mode.ptr()) < 0)
            throw nb::python_error();
        lg_call0(lg_attr(lg_attr(self, "events"), "projection_mode"));
        nb::object callback = lg_attr(self, "refresh");
        nb::dict kwargs;
        kwargs["extent"] = nb::bool_(false);
        lg_kw(std::move(callback), nb::tuple(), std::move(kwargs));
    }
}
nb::object layer_geometry_units(nb::object self) {
    nb::object transforms = lg_attr(std::move(self), "_transforms");
    nb::object name = lg_checked(PyUnicode_InternFromString("data2physical"));
    return lg_attr(lg_item(std::move(transforms), std::move(name)), "units");
}
nb::object lg_world(nb::object self, nb::dict namespace_, const char* name) {
    GeometryNamespace ns(std::move(namespace_));
    nb::object callback = ns.get("get_extent_world");
    nb::object data = lg_attr(self, name);
    nb::object transform = lg_attr(self, "_data_to_world");
    return lg_call2(std::move(callback), std::move(data), std::move(transform));
}
nb::object layer_geometry_extent_world(nb::object self, nb::dict namespace_) {
    return lg_world(std::move(self), std::move(namespace_), "_extent_data");
}
nb::object layer_geometry_extent_world_augmented(nb::object self, nb::dict namespace_) {
    return lg_world(std::move(self), std::move(namespace_), "_extent_data_augmented");
}
nb::object lg_extent(nb::object self, nb::dict namespace_, LayerGeometryLocals& owner, const char* name) {
    GeometryNamespace ns(std::move(namespace_));
    lg_replace(owner.extent_data, lg_attr(self, name));
    lg_replace(owner.data_to_world, lg_attr(self, "_data_to_world"));
    lg_replace(owner.extent_world, lg_call2(ns.get("get_extent_world"), owner.extent_data, owner.data_to_world));
    nb::object constructor = ns.get("Extent");
    nb::object absolute = ns.get("abs");
    nb::object scale = lg_attr(owner.data_to_world, "scale");
    nb::object step = lg_call1(std::move(absolute), std::move(scale));
    nb::object units = lg_attr(self, "units");
    nb::dict kwargs;
    kwargs["data"] = owner.extent_data;
    kwargs["world"] = owner.extent_world;
    kwargs["step"] = step;
    step.reset();
    kwargs["units"] = units;
    units.reset();
    return lg_kw(std::move(constructor), nb::tuple(), std::move(kwargs));
}
nb::object layer_geometry_extent(nb::object self, nb::dict namespace_, LayerGeometryLocals& owner) {
    return lg_extent(std::move(self), std::move(namespace_), owner, "_extent_data");
}
nb::object layer_geometry_extent_augmented(nb::object self, nb::dict namespace_, LayerGeometryLocals& owner) {
    return lg_extent(std::move(self), std::move(namespace_), owner, "_extent_data_augmented");
}
nb::object layer_geometry_data_to_world(nb::object self, LayerGeometryLocals& owner) {
    nb::object transforms = lg_attr(self, "_transforms");
    nb::object key = lg_checked(PySlice_New(nb::int_(1).ptr(), nb::int_(3).ptr(), Py_None));
    lg_replace(owner.res, lg_attr(lg_item(std::move(transforms), std::move(key)), "simplified"));
    nb::object units = lg_attr(self, "units");
    if (PyObject_SetAttrString(owner.res.ptr(), "units", units.ptr()) < 0) throw nb::python_error();
    return owner.res;
}
nb::object lg_axis(nb::object callback, nb::object value) {
    nb::tuple args = nb::make_tuple(value);
    value.reset();
    nb::dict kwargs;
    kwargs["axis"] = nb::int_(0);
    return lg_kw(std::move(callback), std::move(args), std::move(kwargs));
}
nb::object layer_geometry_get_extent_world(nb::object data_extent, nb::object data_to_world,
                                         nb::object centered, nb::dict namespace_,
                                         LayerGeometryLocals& owner, nb::object warning_message) {
    GeometryNamespace ns(std::move(namespace_));
    if (!centered.is_none()) {
        nb::object warn = lg_attr(ns.get("warnings"), "warn");
        nb::tuple args = nb::make_tuple(warning_message);
        nb::dict kwargs;
        kwargs["stacklevel"] = nb::int_(2);
        lg_kw(std::move(warn), std::move(args), std::move(kwargs));
    }
    lg_replace(owner.D, lg_item(lg_attr(data_extent, "shape"), nb::int_(1)));
    nb::object array = ns.numpy("array");
    nb::object meshgrid = ns.numpy("meshgrid");
    nb::object transpose = lg_attr(data_extent, "T");
    nb::object args = PyTuple_CheckExact(transpose.ptr()) ? std::move(transpose)
        : lg_checked(PySequence_Tuple(transpose.ptr()));
    transpose.reset();
    nb::object mesh = lg_checked(PyObject_CallObject(meshgrid.ptr(), args.ptr()));
    meshgrid.reset();
    args.reset();
    nb::object grid = lg_call1(std::move(array), std::move(mesh));
    nb::object reshape = lg_attr(lg_attr(std::move(grid), "T"), "reshape");
    lg_replace(owner.full_data_extent, lg_call2(std::move(reshape), nb::int_(-1), owner.D));
    lg_replace(owner.full_world_extent, lg_call1(data_to_world, owner.full_data_extent));
    array = ns.numpy("array");
    nb::object minimum = lg_axis(ns.numpy("min"), owner.full_world_extent);
    nb::object maximum = lg_axis(ns.numpy("max"), owner.full_world_extent);
    nb::list bounds;
    if (PyList_Append(bounds.ptr(), minimum.ptr()) < 0) throw nb::python_error();
    minimum.reset();
    if (PyList_Append(bounds.ptr(), maximum.ptr()) < 0) throw nb::python_error();
    maximum.reset();
    lg_replace(owner.world_extent, lg_call1(std::move(array), std::move(bounds)));
    return owner.world_extent;
}

} // namespace

void bind_layer_data_geometry(nb::module_& module) {
    nb::class_<LayerGeometryLocals>(module, "LayerGeometryLocals",
        nb::is_weak_referenceable(), nb::type_slots(lg_locals_slots))
        .def_prop_ro("mode", [](const LayerGeometryLocals& s) { return lg_optional(s.mode); })
        .def_prop_ro("res", [](const LayerGeometryLocals& s) { return lg_optional(s.res); })
        .def_prop_ro("extent_data", [](const LayerGeometryLocals& s) { return lg_optional(s.extent_data); })
        .def_prop_ro("data_to_world", [](const LayerGeometryLocals& s) { return lg_optional(s.data_to_world); })
        .def_prop_ro("extent_world", [](const LayerGeometryLocals& s) { return lg_optional(s.extent_world); })
        .def_prop_ro("D", [](const LayerGeometryLocals& s) { return lg_optional(s.D); })
        .def_prop_ro("full_data_extent", [](const LayerGeometryLocals& s) { return lg_optional(s.full_data_extent); })
        .def_prop_ro("full_world_extent", [](const LayerGeometryLocals& s) { return lg_optional(s.full_world_extent); })
        .def_prop_ro("world_extent", [](const LayerGeometryLocals& s) { return lg_optional(s.world_extent); });
    module.def("layer_geometry_locals", [] { return LayerGeometryLocals(); });
    module.def("layer_geometry_projection_locals", [](nb::object mode) {
        LayerGeometryLocals owner;
        owner.mode = std::move(mode);
        return owner;
    }, nb::arg("mode").none());
    module.def("layer_geometry_projection_get", &layer_geometry_projection_get, nb::arg("self").none());
    module.def("layer_geometry_projection_set", &layer_geometry_projection_set,
        nb::arg("self").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("layer_geometry_units", &layer_geometry_units, nb::arg("self").none());
    module.def("layer_geometry_extent_world", &layer_geometry_extent_world,
        nb::arg("self").none(), nb::arg("namespace"));
    module.def("layer_geometry_extent_world_augmented", &layer_geometry_extent_world_augmented,
        nb::arg("self").none(), nb::arg("namespace"));
    module.def("layer_geometry_extent", &layer_geometry_extent,
        nb::arg("self").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("layer_geometry_extent_augmented", &layer_geometry_extent_augmented,
        nb::arg("self").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("layer_geometry_data_to_world", &layer_geometry_data_to_world,
        nb::arg("self").none(), nb::arg("locals_owner"));
    module.def("layer_geometry_get_extent_world", &layer_geometry_get_extent_world,
        nb::arg("data_extent").none(), nb::arg("data_to_world").none(), nb::arg("centered").none(),
        nb::arg("namespace"), nb::arg("locals_owner"), nb::arg("warning_message").none());
}
} // namespace nx
