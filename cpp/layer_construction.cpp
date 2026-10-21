#include "arrays.hpp"
#include <array>
#include <string>
#include <utility>

namespace nx {
namespace {

nb::object lc_checked(PyObject* value) {
    if (!value) throw nb::python_error();
    return nb::steal<nb::object>(value);
}
nb::object lc_attr(nb::object value, const char* name) {
    return lc_checked(PyObject_GetAttrString(value.ptr(), name));
}
void lc_store(nb::handle self, const char* name, nb::object value) {
    if (PyObject_SetAttrString(self.ptr(), name, value.ptr()) < 0)
        throw nb::python_error();
}
nb::object lc_call0(nb::object callback) {
    return lc_checked(PyObject_CallNoArgs(callback.ptr()));
}
nb::object lc_call1(nb::object callback, nb::object value) {
    return lc_checked(PyObject_CallOneArg(callback.ptr(), value.ptr()));
}
nb::object lc_call2(nb::object callback, nb::object first, nb::object second) {
    return lc_checked(PyObject_CallFunctionObjArgs(callback.ptr(), first.ptr(), second.ptr(), nullptr));
}
nb::object lc_call_kw(nb::object callback, nb::tuple arguments, nb::dict keywords) {
    return lc_checked(PyObject_Call(callback.ptr(), arguments.ptr(), keywords.ptr()));
}
bool lc_truth(nb::object value) {
    int result = PyObject_IsTrue(value.ptr());
    if (result < 0) throw nb::python_error();
    return result != 0;
}
nb::object lc_optional(const nb::object& value) {
    return value.is_valid() ? value : nb::none();
}
nb::object lc_literal(const char* value) {
    return lc_checked(PyUnicode_InternFromString(value));
}
[[noreturn]] void lc_raise(nb::object exception) {
    if (PyExceptionClass_Check(exception.ptr())) exception = lc_call0(std::move(exception));
    if (PyExceptionInstance_Check(exception.ptr()))
        PyErr_SetObject(reinterpret_cast<PyObject*>(Py_TYPE(exception.ptr())), exception.ptr());
    else
        PyErr_SetString(PyExc_TypeError, "exceptions must derive from BaseException");
    throw nb::python_error();
}

// Only per-call source bindings live in these carriers. Controller/model state
// remains in the real object's sole namespace; no provider/state mirror exists.
struct LayerPostInitLocals {
    nb::object sig, params, obj;
    LayerPostInitLocals() = default;
    LayerPostInitLocals(LayerPostInitLocals&&) = default;
    ~LayerPostInitLocals() { clear(); }
    void clear() {
        std::array<nb::object, 3> old = {
            std::move(sig), std::move(params), std::move(obj),
        };
        for (nb::object& value : old) value.reset();
    }
};
struct LayerConstructionLocals {
    nb::object name, scale, translate, current_source;
    nb::object BoundingBoxOverlay, SelectionBoxOverlay, TransformBoxOverlay, LayerNameOverlay;
    LayerConstructionLocals() = default;
    LayerConstructionLocals(LayerConstructionLocals&&) = default;
    ~LayerConstructionLocals() { clear(); }
    void clear() {
        std::array<nb::object, 8> old = {
            std::move(name), std::move(scale), std::move(translate), std::move(current_source),
            std::move(BoundingBoxOverlay), std::move(SelectionBoxOverlay),
            std::move(TransformBoxOverlay), std::move(LayerNameOverlay),
        };
        for (nb::object& value : old) value.reset();
    }
};
int lc_postinit_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) {
        const auto& state = *nb::inst_ptr<LayerPostInitLocals>(self);
        Py_VISIT(state.sig.ptr()); Py_VISIT(state.params.ptr()); Py_VISIT(state.obj.ptr());
    }
    return 0;
}
int lc_postinit_clear(PyObject* self) {
    if (nb::inst_ready(self)) nb::inst_ptr<LayerPostInitLocals>(self)->clear();
    return 0;
}
int lc_construction_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) {
        const auto& state = *nb::inst_ptr<LayerConstructionLocals>(self);
        Py_VISIT(state.name.ptr()); Py_VISIT(state.scale.ptr()); Py_VISIT(state.translate.ptr());
        Py_VISIT(state.current_source.ptr()); Py_VISIT(state.BoundingBoxOverlay.ptr());
        Py_VISIT(state.SelectionBoxOverlay.ptr()); Py_VISIT(state.TransformBoxOverlay.ptr());
        Py_VISIT(state.LayerNameOverlay.ptr());
    }
    return 0;
}
int lc_construction_clear(PyObject* self) {
    if (nb::inst_ready(self)) nb::inst_ptr<LayerConstructionLocals>(self)->clear();
    return 0;
}
PyType_Slot lc_postinit_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(lc_postinit_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(lc_postinit_clear)}, {0, nullptr},
};
PyType_Slot lc_construction_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(lc_construction_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(lc_construction_clear)}, {0, nullptr},
};

struct ConstructionNamespace {
    nb::dict globals;
    nb::object builtins;
    explicit ConstructionNamespace(nb::dict namespace_)
        : globals(std::move(namespace_)), builtins(nb::borrow<nb::object>(PyEval_GetBuiltins())) {}
    nb::object builtin(const char* name) const {
        nb::object key = nb::str(name);
        PyObject* found = PyObject_GetItem(builtins.ptr(), key.ptr());
        if (found) return nb::steal<nb::object>(found);
        if (!PyErr_ExceptionMatches(PyExc_KeyError)) throw nb::python_error();
        PyErr_Clear();
        if (std::string(name) == "__import__") {
            PyErr_SetString(PyExc_ImportError, "__import__ not found");
            throw nb::python_error();
        }
        nb::object message = lc_checked(PyUnicode_FromFormat("name '%s' is not defined", name));
        nb::object exception = lc_call1(nb::borrow<nb::object>(PyExc_NameError), std::move(message));
        if (PyObject_SetAttrString(exception.ptr(), "name", key.ptr()) < 0) throw nb::python_error();
        lc_raise(std::move(exception));
    }
    nb::object get(const char* name) const {
        nb::object key = nb::str(name);
        PyObject* found = PyDict_GetItemWithError(globals.ptr(), key.ptr());
        if (found) return nb::borrow<nb::object>(found);
        if (PyErr_Occurred()) throw nb::python_error();
        return builtin(name);
    }
    nb::object numpy(const char* name) const { return lc_attr(get("np"), name); }
    nb::object import_module(const char* name, nb::tuple fromlist) const {
        nb::object importer = builtin("__import__");
        PyObject* frame_locals = PyEval_GetLocals();
        if (!frame_locals) {
            if (PyErr_Occurred()) throw nb::python_error();
            frame_locals = Py_None;
        }
        nb::object module_name = nb::str(name);
        nb::object locals = nb::borrow<nb::object>(frame_locals);
        nb::object level = nb::int_(0);
        return lc_checked(PyObject_CallFunctionObjArgs(
            importer.ptr(), module_name.ptr(), globals.ptr(), locals.ptr(),
            fromlist.ptr(), level.ptr(), nullptr));
    }
};

// IMPORT_FROM first resolves the actual returned module/object. As CPython does,
// a missing attribute may be a partially initialized submodule in sys.modules.
nb::object lc_import_from(nb::handle module, const char* name) {
    PyObject* result = PyObject_GetAttrString(module.ptr(), name);
    if (result) return nb::steal<nb::object>(result);
    if (!PyErr_ExceptionMatches(PyExc_AttributeError)) throw nb::python_error();
    PyErr_Clear();
    nb::object module_name = nb::steal<nb::object>(PyObject_GetAttrString(module.ptr(), "__name__"));
    if (!module_name.is_valid() || !PyUnicode_Check(module_name.ptr())) {
        PyErr_Clear();
        PyErr_Format(PyExc_ImportError, "cannot import name '%s' from '<unknown module name>' (unknown location)", name);
        throw nb::python_error();
    }
    nb::object full_name = lc_checked(PyUnicode_FromFormat("%U.%s", module_name.ptr(), name));
    result = PyImport_GetModule(full_name.ptr());
    if (result) return nb::steal<nb::object>(result);
    if (PyErr_Occurred()) throw nb::python_error();
    nb::object filename = nb::steal<nb::object>(PyObject_GetAttrString(module.ptr(), "__file__"));
    if (!filename.is_valid() || !PyUnicode_Check(filename.ptr())) {
        PyErr_Clear();
        filename = nb::none();
    }
    nb::object message;
    if (filename.is_none())
        message = lc_checked(PyUnicode_FromFormat("cannot import name '%s' from '%U' (unknown location)", name, module_name.ptr()));
    else
        message = lc_checked(PyUnicode_FromFormat("cannot import name '%s' from '%U' (%U)", name, module_name.ptr(), filename.ptr()));
    PyErr_SetImportError(message.ptr(), module_name.ptr(), filename.ptr());
    throw nb::python_error();
}

nb::object lc_super_expanded(nb::object self, nb::tuple args, nb::dict kwargs, nb::object provider) {
    nb::object callback = lc_call1(std::move(provider), std::move(self));
    // CALL_FUNCTION_EX makes its kwargs copy after the callable is captured.
    nb::dict expanded = nb::steal<nb::dict>(PyDict_Copy(kwargs.ptr()));
    if (!expanded.is_valid()) throw nb::python_error();
    return lc_call_kw(std::move(callback), std::move(args), std::move(expanded));
}

void layer_postinit_initialize(nb::object self, nb::tuple args, nb::dict kwargs,
                              nb::object super_provider, nb::dict globals,
                              LayerPostInitLocals& owner) {
    lc_super_expanded(self, std::move(args), std::move(kwargs), std::move(super_provider));
    ConstructionNamespace ns(std::move(globals));
    {
        nb::object signature = lc_attr(ns.get("inspect"), "signature");
        nb::object initializer = lc_attr(self, "__init__");
        owner.sig = lc_call1(std::move(signature), std::move(initializer));
    }
    {
        nb::object tuple = ns.get("tuple");
        nb::object values = lc_attr(lc_attr(owner.sig, "parameters"), "values");
        nb::object parameters = lc_call0(std::move(values));
        owner.params = lc_call1(std::move(tuple), std::move(parameters));
    }
    {
        nb::object replace = lc_attr(owner.sig, "replace");
        nb::object one = nb::int_(1);
        nb::object slice = lc_checked(PySlice_New(one.ptr(), Py_None, Py_None));
        nb::object parameters = lc_checked(PyObject_GetItem(owner.params.ptr(), slice.ptr()));
        slice.reset();
        nb::dict keywords;
        keywords["parameters"] = std::move(parameters);
        lc_store(self, "__signature__", lc_call_kw(std::move(replace), nb::tuple(), std::move(keywords)));
    }
}
nb::object layer_postinit_construct(nb::object self, nb::tuple args, nb::dict kwargs,
                                   nb::object super_provider, nb::dict /* globals */,
                                   LayerPostInitLocals& owner) {
    owner.obj = lc_super_expanded(std::move(self), std::move(args), std::move(kwargs), std::move(super_provider));
    lc_call0(lc_attr(owner.obj, "_post_init"));
    return owner.obj;
}

void layer_construction_initialize(
    nb::object self, nb::object data, nb::object ndim, nb::object affine,
    nb::object axis_labels, nb::object blending, nb::object cache,
    nb::object experimental_clipping_planes, nb::object metadata, nb::object mode,
    nb::object multiscale, nb::object opacity, nb::object projection_mode,
    nb::object rotate, nb::object shear, nb::object units, nb::object visible,
    nb::object locked, nb::object thumbnail_shape, nb::object super_provider, nb::dict globals,
    LayerConstructionLocals& owner) {
    lc_call0(lc_call1(std::move(super_provider), self));
    ConstructionNamespace ns(std::move(globals));
    if (owner.name.is_none() && !data.is_none())
        owner.name = lc_call1(ns.get("magic_name"), data);
    if (!owner.scale.is_none() && !lc_truth(lc_call1(ns.numpy("all"), owner.scale))) {
        nb::object exception = ns.get("ValueError");
        nb::object name_repr = lc_checked(PyObject_Repr(owner.name.ptr()));
        nb::object scale_repr = lc_checked(PyObject_Repr(owner.scale.ptr()));
        nb::tuple parts = nb::make_tuple(
            nb::str("Layer "), std::move(name_repr),
            nb::str(" is invalid because it has scale values of 0. The layer's scale is currently "),
            std::move(scale_repr));
        nb::object message = lc_checked(PyUnicode_Join(nb::str("").ptr(), parts.ptr()));
        parts.reset();
        lc_raise(lc_call1(std::move(exception), std::move(message)));
    }
    {
        nb::object module = ns.import_module("napari.layers._source", nb::make_tuple(nb::str("current_source")));
        owner.current_source = lc_import_from(module, "current_source");
    }
    lc_store(self, "_highlight_visible", nb::bool_(true));
    lc_store(self, "_unique_id", nb::none());
    lc_store(self, "_source", lc_call0(owner.current_source));
    lc_store(self, "dask_optimized_slicing", lc_call2(ns.get("configure_dask"), data, cache));
    {
        nb::object dict = ns.get("dict");
        nb::object argument = metadata;
        if (!lc_truth(argument)) argument = nb::dict();
        lc_store(self, "_metadata", lc_call1(std::move(dict), std::move(argument)));
    }
    lc_store(self, "_opacity", opacity);
    lc_store(self, "_blending", lc_call1(ns.get("Blending"), blending));
    lc_store(self, "_visible", visible);
    lc_store(self, "_visible_mode", nb::none());
    lc_store(self, "_freeze", nb::bool_(false));
    lc_store(self, "_status", lc_literal("Ready"));
    lc_store(self, "_help", nb::str(""));
    lc_store(self, "_cursor", lc_literal("standard"));
    lc_store(self, "_cursor_size", nb::int_(1));
    lc_store(self, "_mouse_pan", nb::bool_(true));
    lc_store(self, "_mouse_zoom", nb::bool_(true));
    lc_store(self, "_value", nb::none());
    lc_store(self, "_scale_factor", nb::int_(1));
    lc_store(self, "multiscale", multiscale);
    lc_store(self, "_experimental_clipping_planes", lc_call0(ns.get("ClippingPlaneList")));
    lc_store(self, "_mode", lc_call1(lc_attr(self, "_modeclass"), lc_literal("pan_zoom")));
    {
        nb::object projection = lc_attr(self, "_projectionclass");
        nb::object string = lc_call1(ns.get("str"), projection_mode);
        lc_store(self, "_projection_mode", lc_call1(std::move(projection), std::move(string)));
    }
    lc_store(self, "_refresh_blocked", nb::bool_(false));
    lc_store(self, "_ndim", ndim);
    if (owner.scale.is_none()) {
        nb::list initial;
        initial.append(nb::int_(1));
        owner.scale = lc_checked(PyNumber_Multiply(initial.ptr(), ndim.ptr()));
    }
    if (owner.translate.is_none()) {
        nb::list initial;
        initial.append(nb::int_(0));
        owner.translate = lc_checked(PyNumber_Multiply(initial.ptr(), ndim.ptr()));
    }
    {
        nb::object coerce = ns.get("coerce_affine");
        nb::dict keywords;
        keywords["ndim"] = ndim;
        keywords["name"] = lc_literal("physical2world");
        lc_store(self, "_initial_affine", lc_call_kw(std::move(coerce), nb::make_tuple(affine), std::move(keywords)));
    }
    {
        nb::object chain = ns.get("TransformChain");
        nb::object affine_callback = ns.get("Affine");
        nb::object ones = lc_call1(ns.numpy("ones"), ndim);
        nb::object zeros = lc_call1(ns.numpy("zeros"), ndim);
        nb::dict tile_keywords;
        tile_keywords["name"] = lc_literal("tile2data");
        nb::object tile = lc_call_kw(std::move(affine_callback),
            nb::make_tuple(std::move(ones), std::move(zeros)), std::move(tile_keywords));
        nb::object composite = ns.get("CompositeAffine");
        nb::dict keywords;
        keywords["axis_labels"] = axis_labels;
        keywords["rotate"] = rotate;
        keywords["shear"] = shear;
        keywords["ndim"] = ndim;
        keywords["name"] = lc_literal("data2physical");
        keywords["units"] = units;
        nb::object physical = lc_call_kw(std::move(composite),
            nb::make_tuple(owner.scale, owner.translate), std::move(keywords));
        nb::object initial = lc_attr(self, "_initial_affine");
        nb::list transforms = nb::steal<nb::list>(PyList_New(3));
        if (!transforms.is_valid()) throw nb::python_error();
        PyList_SET_ITEM(transforms.ptr(), 0, tile.release().ptr());
        PyList_SET_ITEM(transforms.ptr(), 1, physical.release().ptr());
        PyList_SET_ITEM(transforms.ptr(), 2, initial.release().ptr());
        lc_store(self, "_transforms", lc_call1(std::move(chain), std::move(transforms)));
    }
    {
        nb::object zeros = ns.numpy("zeros");
        nb::tuple shape = nb::make_tuple(nb::int_(2), ndim);
        nb::object dtype = ns.get("int");
        nb::dict keywords;
        keywords["dtype"] = std::move(dtype);
        lc_store(self, "corner_pixels", lc_call_kw(std::move(zeros), nb::make_tuple(std::move(shape)), std::move(keywords)));
    }
    lc_store(self, "_editable", nb::bool_(true));
    lc_store(self, "_locked", lc_call1(lc_attr(self, "_coerce_lock"), locked));
    lc_store(self, "_array_like", nb::bool_(false));
    lc_store(self, "_thumbnail_shape", thumbnail_shape);
    {
        nb::object zeros = ns.numpy("zeros");
        nb::object shape = lc_attr(self, "_thumbnail_shape");
        nb::object dtype = ns.numpy("uint8");
        nb::dict keywords;
        keywords["dtype"] = std::move(dtype);
        lc_store(self, "_thumbnail", lc_call_kw(std::move(zeros), nb::make_tuple(std::move(shape)), std::move(keywords)));
    }
    lc_store(self, "_update_properties", nb::bool_(true));
    lc_store(self, "_name", nb::str(""));
    lc_store(self, "experimental_clipping_planes", experimental_clipping_planes);
    {
        nb::object module = ns.import_module("napari.components.overlays.bounding_box",
            nb::make_tuple(nb::str("BoundingBoxOverlay")));
        owner.BoundingBoxOverlay = lc_import_from(module, "BoundingBoxOverlay");
    }
    {
        nb::object module = ns.import_module("napari.components.overlays.interaction_box",
            nb::make_tuple(nb::str("SelectionBoxOverlay"), nb::str("TransformBoxOverlay")));
        owner.SelectionBoxOverlay = lc_import_from(module, "SelectionBoxOverlay");
        owner.TransformBoxOverlay = lc_import_from(module, "TransformBoxOverlay");
    }
    {
        nb::object module = ns.import_module("napari.components.overlays.text",
            nb::make_tuple(nb::str("LayerNameOverlay")));
        owner.LayerNameOverlay = lc_import_from(module, "LayerNameOverlay");
    }
    lc_store(self, "_overlays", lc_call0(ns.get("EventedDict")));
    {
        nb::object group = ns.get("EmitterGroup");
        constexpr const char* event_names[] = {
            "axis_labels", "data", "metadata", "affine", "blending", "cursor", "cursor_size",
            "editable", "locked", "extent", "help", "loaded", "mode", "mouse_pan", "mouse_zoom",
            "name", "opacity", "projection_mode", "refresh", "reload", "rotate", "scale",
            "scale_factor", "set_data", "shear", "status", "thumbnail", "translate", "units",
            "visible", "_extent_augmented", "_overlays",
        };
        std::array<nb::object, 32> event_values;
        for (nb::object& event : event_values) event = ns.get("Event");
        nb::dict keywords;
        keywords["source"] = self;
        for (size_t i = 0; i < event_values.size(); ++i) {
            keywords[event_names[i]] = event_values[i];
            event_values[i].reset();
        }
        lc_store(self, "events", lc_call_kw(std::move(group), nb::tuple(), std::move(keywords)));
    }
    lc_store(self, "name", owner.name);
    lc_store(self, "mode", mode);
    lc_store(self, "projection_mode", projection_mode);
    {
        nb::object update = lc_attr(lc_attr(self, "_overlays"), "update");
        nb::object transform_box = lc_call0(owner.TransformBoxOverlay);
        nb::object selection_box = lc_call0(owner.SelectionBoxOverlay);
        nb::object bounding_box = lc_call0(owner.BoundingBoxOverlay);
        nb::object layer_name = lc_call0(owner.LayerNameOverlay);
        nb::dict overlays;
        overlays["transform_box"] = transform_box;
        overlays["selection_box"] = selection_box;
        overlays["bounding_box"] = bounding_box;
        overlays["layer_name"] = layer_name;
        transform_box.reset(); selection_box.reset(); bounding_box.reset(); layer_name.reset();
        lc_call1(std::move(update), std::move(overlays));
    }
    lc_store(self, "_slicing_state", lc_call2(lc_attr(self, "_get_layer_slicing_state"), data, cache));
}

void layer_slicing_construction_initialize(nb::object self, nb::object layer,
                                          nb::object data, nb::object cache, nb::dict globals) {
    ConstructionNamespace ns(std::move(globals));
    lc_store(self, "layer", layer);
    lc_store(self, "_load_state", lc_call0(lc_attr(ns.get("_native"), "LayerLoadState")));
    lc_store(self, "dask_optimized_slicing", lc_call2(ns.get("configure_dask"), data, cache));
    {
        nb::object input = ns.get("_SliceInput");
        nb::dict keywords;
        keywords["ndisplay"] = nb::int_(2);
        nb::object make_full = lc_attr(ns.get("_ThickNDSlice"), "make_full");
        nb::object ndim = lc_attr(self, "ndim");
        nb::dict full_keywords;
        full_keywords["ndim"] = std::move(ndim);
        keywords["world_slice"] = lc_call_kw(std::move(make_full), nb::tuple(), std::move(full_keywords));
        nb::object tuple = ns.get("tuple");
        nb::object range = ns.get("range");
        ndim = lc_attr(self, "ndim");
        nb::object order = lc_call1(std::move(range), std::move(ndim));
        keywords["order"] = lc_call1(std::move(tuple), std::move(order));
        lc_store(self, "_slice_input", lc_call_kw(std::move(input), nb::tuple(), std::move(keywords)));
    }
    lc_store(self, "_loaded", nb::bool_(true));
    lc_store(self, "_last_slice_id", nb::int_(-1));
    lc_store(self, "_units", nb::none());
}

void layer_keymap_construction_initialize(nb::object self, nb::tuple args,
                                         nb::dict kwargs, nb::object super_provider) {
    lc_super_expanded(self, std::move(args), std::move(kwargs), std::move(super_provider));
    lc_store(self, "_keymap", nb::dict());
}
void layer_mousemap_construction_initialize(nb::object self, nb::tuple args,
                                           nb::dict kwargs, nb::object super_provider) {
    lc_super_expanded(self, std::move(args), std::move(kwargs), std::move(super_provider));
    lc_store(self, "mouse_move_callbacks", nb::list());
    lc_store(self, "mouse_drag_callbacks", nb::list());
    lc_store(self, "mouse_double_click_callbacks", nb::list());
    lc_store(self, "mouse_wheel_callbacks", nb::list());
    lc_store(self, "_persisted_mouse_event", nb::dict());
    lc_store(self, "_mouse_drag_gen", nb::dict());
    lc_store(self, "_mouse_wheel_gen", nb::dict());
}

} // namespace

void bind_layer_construction(nb::module_& module) {
    nb::class_<LayerPostInitLocals>(module, "LayerPostInitLocals", nb::is_weak_referenceable(), nb::type_slots(lc_postinit_slots))
        .def_prop_ro("sig", [](const LayerPostInitLocals& s) { return lc_optional(s.sig); })
        .def_prop_ro("params", [](const LayerPostInitLocals& s) { return lc_optional(s.params); })
        .def_prop_ro("obj", [](const LayerPostInitLocals& s) { return lc_optional(s.obj); });
    nb::class_<LayerConstructionLocals>(module, "LayerConstructionLocals", nb::is_weak_referenceable(), nb::type_slots(lc_construction_slots))
        .def_prop_ro("name", [](const LayerConstructionLocals& s) { return lc_optional(s.name); })
        .def_prop_ro("scale", [](const LayerConstructionLocals& s) { return lc_optional(s.scale); })
        .def_prop_ro("translate", [](const LayerConstructionLocals& s) { return lc_optional(s.translate); })
        .def_prop_ro("current_source", [](const LayerConstructionLocals& s) { return lc_optional(s.current_source); })
        .def_prop_ro("BoundingBoxOverlay", [](const LayerConstructionLocals& s) { return lc_optional(s.BoundingBoxOverlay); })
        .def_prop_ro("SelectionBoxOverlay", [](const LayerConstructionLocals& s) { return lc_optional(s.SelectionBoxOverlay); })
        .def_prop_ro("TransformBoxOverlay", [](const LayerConstructionLocals& s) { return lc_optional(s.TransformBoxOverlay); })
        .def_prop_ro("LayerNameOverlay", [](const LayerConstructionLocals& s) { return lc_optional(s.LayerNameOverlay); });
    module.def("layer_postinit_locals", [] { return LayerPostInitLocals{}; });
    module.def("layer_construction_locals", [](nb::object name, nb::object scale, nb::object translate) {
        LayerConstructionLocals state;
        state.name = std::move(name);
        state.scale = std::move(scale);
        state.translate = std::move(translate);
        return state;
    }, nb::arg("name").none(), nb::arg("scale").none(), nb::arg("translate").none());
    module.def("layer_postinit_initialize", &layer_postinit_initialize,
        nb::arg("self").none(), nb::arg("args"), nb::arg("kwargs"), nb::arg("super_provider").none(),
        nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("layer_postinit_construct", &layer_postinit_construct,
        nb::arg("self").none(), nb::arg("args"), nb::arg("kwargs"), nb::arg("super_provider").none(),
        nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("layer_construction_initialize", &layer_construction_initialize,
        nb::arg("self").none(), nb::arg("data").none(), nb::arg("ndim").none(),
        nb::arg("affine").none(), nb::arg("axis_labels").none(), nb::arg("blending").none(),
        nb::arg("cache").none(), nb::arg("experimental_clipping_planes").none(),
        nb::arg("metadata").none(), nb::arg("mode").none(), nb::arg("multiscale").none(),
        nb::arg("opacity").none(), nb::arg("projection_mode").none(), nb::arg("rotate").none(),
        nb::arg("shear").none(), nb::arg("units").none(), nb::arg("visible").none(),
        nb::arg("locked").none(), nb::arg("thumbnail_shape").none(), nb::arg("super_provider").none(),
        nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("layer_slicing_construction_initialize", &layer_slicing_construction_initialize,
        nb::arg("self").none(), nb::arg("layer").none(), nb::arg("data").none(),
        nb::arg("cache").none(), nb::arg("namespace"));
    module.def("layer_keymap_construction_initialize", &layer_keymap_construction_initialize,
        nb::arg("self").none(), nb::arg("args"), nb::arg("kwargs"), nb::arg("super_provider").none());
    module.def("layer_mousemap_construction_initialize", &layer_mousemap_construction_initialize,
        nb::arg("self").none(), nb::arg("args"), nb::arg("kwargs"), nb::arg("super_provider").none());
}

} // namespace nx
