#include "arrays.hpp"
#include <array>
#include <utility>

namespace nx {
namespace {

nb::object sfr_checked(PyObject* value) {
    if (!value) throw nb::python_error();
    return nb::steal<nb::object>(value);
}
nb::object sfr_attr(nb::object value, const char* name) {
    return sfr_checked(PyObject_GetAttrString(value.ptr(), name));
}
nb::object sfr_item(nb::object value, nb::object key) {
    return sfr_checked(PyObject_GetItem(value.ptr(), key.ptr()));
}
nb::object sfr_call0(nb::object callback) {
    return sfr_checked(PyObject_CallNoArgs(callback.ptr()));
}
nb::object sfr_call1(nb::object callback, nb::object value) {
    return sfr_checked(PyObject_CallOneArg(callback.ptr(), value.ptr()));
}
nb::object sfr_call2(nb::object callback, nb::object first, nb::object second) {
    return sfr_checked(PyObject_CallFunctionObjArgs(callback.ptr(), first.ptr(), second.ptr(), nullptr));
}
nb::object sfr_call_kw(nb::object callback, nb::tuple arguments, nb::dict keywords) {
    return sfr_checked(PyObject_Call(callback.ptr(), arguments.ptr(), keywords.ptr()));
}
void sfr_store(nb::object self, const char* name, nb::object value) {
    if (PyObject_SetAttrString(self.ptr(), name, value.ptr()) < 0) throw nb::python_error();
}
void sfr_set(nb::object target, nb::object key, nb::object value) {
    if (PyObject_SetItem(target.ptr(), key.ptr(), value.ptr()) < 0) throw nb::python_error();
}
bool sfr_truth(nb::object value) {
    const int result = PyObject_IsTrue(value.ptr());
    if (result < 0) throw nb::python_error();
    return result != 0;
}
nb::object sfr_compare(nb::object first, nb::object second, int operation) {
    return sfr_checked(PyObject_RichCompare(first.ptr(), second.ptr(), operation));
}
nb::object sfr_subtract(nb::object first, nb::object second) {
    return sfr_checked(PyNumber_Subtract(first.ptr(), second.ptr()));
}
nb::object sfr_multiply(nb::object first, nb::object second) {
    return sfr_checked(PyNumber_Multiply(first.ptr(), second.ptr()));
}
nb::object sfr_add(nb::object first, nb::object second) {
    return sfr_checked(PyNumber_Add(first.ptr(), second.ptr()));
}
void sfr_replace(nb::object& target, nb::object value) {
    nb::object previous = std::move(target);
    target = std::move(value);
    previous.reset();
}
nb::object sfr_optional(const nb::object& value) { return value.is_valid() ? value : nb::none(); }
[[noreturn]] void sfr_raise(nb::object error) {
    if (PyExceptionInstance_Check(error.ptr()))
        PyErr_SetObject(reinterpret_cast<PyObject*>(Py_TYPE(error.ptr())), error.ptr());
    else if (PyExceptionClass_Check(error.ptr())) PyErr_SetNone(error.ptr());
    else PyErr_SetString(PyExc_TypeError, "exceptions must derive from BaseException");
    throw nb::python_error();
}
nb::object sfr_layer(nb::object self) { return sfr_attr(std::move(self), "layer"); }
void sfr_layer_store(nb::object self, const char* name, nb::object value) {
    sfr_store(sfr_layer(std::move(self)), name, std::move(value));
}

struct ScalarFirstRefreshNamespace {
    nb::dict globals;
    nb::object builtins;
    explicit ScalarFirstRefreshNamespace(nb::dict namespace_)
        : globals(std::move(namespace_)), builtins(nb::borrow<nb::object>(PyEval_GetBuiltins())) {}
    nb::object get(const char* name) const {
        nb::object key = nb::str(name);
        PyObject* found = PyDict_GetItemWithError(globals.ptr(), key.ptr());
        if (found) return nb::borrow<nb::object>(found);
        if (PyErr_Occurred()) throw nb::python_error();
        found = PyObject_GetItem(builtins.ptr(), key.ptr());
        if (found) return nb::steal<nb::object>(found);
        if (!PyErr_ExceptionMatches(PyExc_KeyError)) throw nb::python_error();
        PyErr_Clear();
        nb::object message = sfr_checked(PyUnicode_FromFormat("name '%s' is not defined", name));
        nb::object error = sfr_call1(nb::borrow<nb::object>(PyExc_NameError), std::move(message));
        if (PyObject_SetAttrString(error.ptr(), "name", key.ptr()) < 0) throw nb::python_error();
        sfr_raise(std::move(error));
    }
    nb::object numpy(const char* name) const { return sfr_attr(get("np"), name); }
};

// Exactly the original assigned locals of two separate invocation bodies.
// Neither carrier mirrors persistent layer/request/model state or a namespace.
struct ScalarFirstRefreshLocals {
    enum class Layout { Empty, SetView, MakeRequest };
    Layout layout = Layout::Empty;
    nb::object displayed, level, shape, corners, request, response;
    nb::object data, locked, data_level, thumbnail_level, data_at_thumbnail_level, data_at_data_level;
    ScalarFirstRefreshLocals() = default;
    ScalarFirstRefreshLocals(ScalarFirstRefreshLocals&&) = default;
    ~ScalarFirstRefreshLocals() { clear(); }
    void clear() {
        std::array<nb::object, 12> old = {
            std::move(displayed), std::move(level), std::move(shape), std::move(corners),
            std::move(request), std::move(response), std::move(data), std::move(locked),
            std::move(data_level), std::move(thumbnail_level),
            std::move(data_at_thumbnail_level), std::move(data_at_data_level),
        };
        const Layout previous = layout;
        layout = Layout::Empty;
        if (previous == Layout::SetView)
            for (size_t index = 0; index < 6; ++index) old[index].reset();
        else if (previous == Layout::MakeRequest)
            for (size_t index = 6; index < 12; ++index) old[index].reset();
        for (nb::object& value : old) value.reset();
    }
};
struct ScalarEmptyResponseLocals {
    nb::object request_id, shape, data, image, ndim, tile_to_data;
    ScalarEmptyResponseLocals() = default;
    ScalarEmptyResponseLocals(ScalarEmptyResponseLocals&&) = default;
    ~ScalarEmptyResponseLocals() { clear(); }
    void clear() {
        std::array<nb::object, 6> old = {
            std::move(request_id), std::move(shape), std::move(data),
            std::move(image), std::move(ndim), std::move(tile_to_data),
        };
        for (nb::object& value : old) value.reset();
    }
};

int sfr_locals_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) {
        const auto& s = *nb::inst_ptr<ScalarFirstRefreshLocals>(self);
        Py_VISIT(s.displayed.ptr()); Py_VISIT(s.level.ptr()); Py_VISIT(s.shape.ptr());
        Py_VISIT(s.corners.ptr()); Py_VISIT(s.request.ptr()); Py_VISIT(s.response.ptr());
        Py_VISIT(s.data.ptr()); Py_VISIT(s.locked.ptr()); Py_VISIT(s.data_level.ptr());
        Py_VISIT(s.thumbnail_level.ptr()); Py_VISIT(s.data_at_thumbnail_level.ptr());
        Py_VISIT(s.data_at_data_level.ptr());
    }
    return 0;
}
int sfr_locals_clear(PyObject* self) {
    if (nb::inst_ready(self)) nb::inst_ptr<ScalarFirstRefreshLocals>(self)->clear();
    return 0;
}
PyType_Slot sfr_locals_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(sfr_locals_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(sfr_locals_clear)}, {0, nullptr},
};
int sfr_empty_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) {
        const auto& s = *nb::inst_ptr<ScalarEmptyResponseLocals>(self);
        Py_VISIT(s.request_id.ptr()); Py_VISIT(s.shape.ptr()); Py_VISIT(s.data.ptr());
        Py_VISIT(s.image.ptr()); Py_VISIT(s.ndim.ptr()); Py_VISIT(s.tile_to_data.ptr());
    }
    return 0;
}
int sfr_empty_clear(PyObject* self) {
    if (nb::inst_ready(self)) nb::inst_ptr<ScalarEmptyResponseLocals>(self)->clear();
    return 0;
}
PyType_Slot sfr_empty_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(sfr_empty_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(sfr_empty_clear)}, {0, nullptr},
};

ScalarEmptyResponseLocals scalar_empty_response_locals(nb::object request_id) {
    ScalarEmptyResponseLocals owner;
    owner.request_id = std::move(request_id);
    return owner;
}

void scalar_first_refresh_set_view(nb::object self, nb::dict globals, ScalarFirstRefreshLocals& owner) {
    owner.layout = ScalarFirstRefreshLocals::Layout::SetView;
    ScalarFirstRefreshNamespace ns(std::move(globals));
    if (sfr_truth(sfr_attr(sfr_layer(self), "multiscale"))
        && sfr_truth(sfr_compare(sfr_attr(sfr_attr(self, "_slice_input"), "ndisplay"), nb::int_(3), Py_EQ))
        && sfr_attr(sfr_layer(self), "_locked_data_level").is_none()) {
        {
            nb::object callback = ns.get("list");
            nb::object displayed = sfr_attr(sfr_attr(self, "_slice_input"), "displayed");
            sfr_replace(owner.displayed, sfr_call1(std::move(callback), std::move(displayed)));
        }
        {
            nb::object callback = ns.get("len");
            nb::object shapes = sfr_attr(sfr_layer(self), "level_shapes");
            nb::object count = sfr_call1(std::move(callback), std::move(shapes));
            sfr_replace(owner.level, sfr_subtract(std::move(count), nb::int_(1)));
        }
        {
            nb::object callback = ns.numpy("take");
            nb::object convert = ns.numpy("asarray");
            nb::object shapes = sfr_attr(sfr_layer(self), "level_shapes");
            nb::object level_shape = sfr_item(std::move(shapes), owner.level);
            nb::object array = sfr_call1(std::move(convert), std::move(level_shape));
            sfr_replace(owner.shape, sfr_call2(std::move(callback), std::move(array), owner.displayed));
        }
        {
            nb::object callback = ns.numpy("zeros");
            nb::object ndim = sfr_attr(sfr_layer(self), "ndim");
            nb::tuple shape = nb::make_tuple(nb::int_(2), ndim);
            ndim.reset();
            nb::dict keywords;
            keywords["dtype"] = ns.get("int");
            nb::object corners = sfr_call_kw(std::move(callback), nb::make_tuple(shape), std::move(keywords));
            shape.reset();
            sfr_replace(owner.corners, std::move(corners));
        }
        {
            nb::object value = sfr_subtract(owner.shape, nb::int_(1));
            nb::object target = owner.corners;
            nb::tuple key = nb::make_tuple(nb::int_(1), owner.displayed);
            sfr_set(std::move(target), std::move(key), std::move(value));
        }
        sfr_layer_store(self, "_data_level", owner.level);
        sfr_layer_store(self, "corner_pixels", owner.corners);
    }
    {
        nb::object callback = sfr_attr(self, "_make_slice_request_internal");
        nb::dict keywords;
        keywords["slice_input"] = sfr_attr(self, "_slice_input");
        keywords["data_slice"] = sfr_attr(self, "data_slice");
        keywords["dask_indexer"] = ns.get("nullcontext");
        sfr_replace(owner.request, sfr_call_kw(std::move(callback), nb::tuple(), std::move(keywords)));
    }
    sfr_replace(owner.response, sfr_call0(owner.request));
    sfr_call1(sfr_attr(self, "_update_slice_response"), owner.response);
}

nb::object scalar_first_refresh_make_request(
    nb::object self, nb::object slice_input, nb::object data_slice,
    nb::object dask_indexer, nb::dict globals, ScalarFirstRefreshLocals& owner) {
    owner.layout = ScalarFirstRefreshLocals::Layout::MakeRequest;
    ScalarFirstRefreshNamespace ns(std::move(globals));
    sfr_replace(owner.data, sfr_attr(sfr_layer(self), "data"));
    if (sfr_truth(sfr_attr(sfr_layer(self), "multiscale"))) {
        sfr_replace(owner.locked, sfr_attr(sfr_layer(self), "_locked_data_level"));
        if (!owner.locked.is_none()) sfr_replace(owner.data_level, owner.locked);
        else if (sfr_truth(sfr_compare(sfr_attr(slice_input, "ndisplay"), nb::int_(3), Py_EQ))) {
            nb::object count = sfr_call1(ns.get("len"), owner.data);
            sfr_replace(owner.data_level, sfr_subtract(std::move(count), nb::int_(1)));
        } else sfr_replace(owner.data_level, sfr_attr(sfr_layer(self), "data_level"));
    } else sfr_replace(owner.data_level, nb::int_(0));

    sfr_replace(owner.thumbnail_level, sfr_attr(sfr_layer(self), "_thumbnail_level"));
    if (sfr_truth(sfr_attr(sfr_layer(self), "_level_materializer"))) {
        // The source fetches the callable again after testing the prior value.
        nb::object callback = sfr_attr(sfr_layer(self), "_level_materializer");
        sfr_replace(owner.data_at_thumbnail_level, sfr_call1(std::move(callback), owner.thumbnail_level));
    } else if (sfr_truth(sfr_attr(sfr_layer(self), "multiscale")))
        sfr_replace(owner.data_at_thumbnail_level, sfr_item(owner.data, owner.thumbnail_level));
    else sfr_replace(owner.data_at_thumbnail_level, owner.data);

    if (!sfr_truth(sfr_attr(sfr_layer(self), "multiscale")))
        sfr_replace(owner.data_at_data_level, owner.data);
    else if (sfr_truth(sfr_compare(owner.data_level, owner.thumbnail_level, Py_EQ)))
        sfr_replace(owner.data_at_data_level, owner.data_at_thumbnail_level);
    else sfr_replace(owner.data_at_data_level, sfr_item(owner.data, owner.data_level));

    nb::object callback = sfr_attr(self, "_slice_request_class");
    nb::dict keywords;
    keywords["slice_input"] = slice_input;
    keywords["data_at_data_level"] = owner.data_at_data_level;
    keywords["data_at_thumbnail_level"] = owner.data_at_thumbnail_level;
    keywords["dtype"] = sfr_attr(sfr_layer(self), "dtype");
    keywords["dask_indexer"] = dask_indexer;
    keywords["data_slice"] = data_slice;
    keywords["projection_mode"] = sfr_attr(sfr_layer(self), "projection_mode");
    keywords["multiscale"] = sfr_attr(sfr_layer(self), "multiscale");
    keywords["corner_pixels"] = sfr_attr(sfr_layer(self), "corner_pixels");
    {
        nb::object length = ns.get("len");
        nb::object shape = sfr_attr(sfr_attr(sfr_layer(self), "data"), "shape");
        nb::object dimensions = sfr_call1(std::move(length), std::move(shape));
        nb::object ndim = sfr_attr(self, "ndim");
        keywords["rgb"] = sfr_compare(std::move(dimensions), std::move(ndim), Py_NE);
    }
    keywords["data_level"] = owner.data_level;
    keywords["thumbnail_level"] = owner.thumbnail_level;
    keywords["level_shapes"] = sfr_attr(sfr_layer(self), "level_shapes");
    keywords["downsample_factors"] = sfr_attr(sfr_layer(self), "downsample_factors");
    return sfr_call_kw(std::move(callback), nb::tuple(), std::move(keywords));
}

nb::object scalar_first_refresh_empty_response(
    nb::object slice_input, nb::object rgb, nb::object dtype,
    nb::object one_shape, nb::object rgb_shape, nb::dict globals,
    ScalarEmptyResponseLocals& owner) {
    ScalarFirstRefreshNamespace ns(std::move(globals));
    nb::object ndisplay = sfr_attr(slice_input, "ndisplay");
    sfr_replace(owner.shape, sfr_multiply(one_shape, std::move(ndisplay)));
    if (sfr_truth(rgb)) sfr_replace(owner.shape, sfr_add(owner.shape, rgb_shape));
    {
        nb::object callback = ns.numpy("zeros");
        nb::tuple arguments = nb::make_tuple(owner.shape);
        nb::object normalize = ns.get("normalize_dtype");
        nb::object normalized = sfr_call1(std::move(normalize), dtype);
        nb::dict keywords;
        keywords["dtype"] = normalized;
        normalized.reset();
        sfr_replace(owner.data, sfr_call_kw(std::move(callback), std::move(arguments), std::move(keywords)));
    }
    {
        nb::object callback = sfr_attr(ns.get("_ScalarFieldView"), "from_view");
        sfr_replace(owner.image, sfr_call1(std::move(callback), owner.data));
    }
    sfr_replace(owner.ndim, sfr_attr(slice_input, "ndim"));
    {
        nb::object callback = ns.get("Affine");
        nb::dict keywords;
        keywords["name"] = nb::str("tile2data");
        nb::object eye = ns.numpy("eye");
        keywords["linear_matrix"] = sfr_call1(std::move(eye), owner.ndim);
        keywords["ndim"] = owner.ndim;
        sfr_replace(owner.tile_to_data, sfr_call_kw(std::move(callback), nb::tuple(), std::move(keywords)));
    }
    if (owner.request_id.is_none()) sfr_replace(owner.request_id, sfr_call0(ns.get("_next_request_id")));
    // Original make_empty deliberately ignores cls and fetches this global.
    nb::object callback = ns.get("_ScalarFieldSliceResponse");
    nb::dict keywords;
    keywords["image"] = owner.image;
    keywords["thumbnail"] = owner.image;
    keywords["tile_to_data"] = owner.tile_to_data;
    keywords["slice_input"] = slice_input;
    keywords["request_id"] = owner.request_id;
    keywords["empty"] = nb::bool_(true);
    return sfr_call_kw(std::move(callback), nb::tuple(), std::move(keywords));
}

nb::object scalar_first_refresh_view(nb::object cls, nb::object view) {
    nb::dict keywords;
    keywords["raw"] = view;
    keywords["view"] = view;
    return sfr_call_kw(std::move(cls), nb::tuple(), std::move(keywords));
}

} // namespace

void bind_scalar_first_refresh(nb::module_& module) {
    nb::class_<ScalarFirstRefreshLocals>(module, "ScalarFirstRefreshLocals",
        nb::is_weak_referenceable(), nb::type_slots(sfr_locals_slots))
        .def_prop_ro("displayed", [](const ScalarFirstRefreshLocals& s) { return sfr_optional(s.displayed); })
        .def_prop_ro("level", [](const ScalarFirstRefreshLocals& s) { return sfr_optional(s.level); })
        .def_prop_ro("shape", [](const ScalarFirstRefreshLocals& s) { return sfr_optional(s.shape); })
        .def_prop_ro("corners", [](const ScalarFirstRefreshLocals& s) { return sfr_optional(s.corners); })
        .def_prop_ro("request", [](const ScalarFirstRefreshLocals& s) { return sfr_optional(s.request); })
        .def_prop_ro("response", [](const ScalarFirstRefreshLocals& s) { return sfr_optional(s.response); })
        .def_prop_ro("data", [](const ScalarFirstRefreshLocals& s) { return sfr_optional(s.data); })
        .def_prop_ro("locked", [](const ScalarFirstRefreshLocals& s) { return sfr_optional(s.locked); })
        .def_prop_ro("data_level", [](const ScalarFirstRefreshLocals& s) { return sfr_optional(s.data_level); })
        .def_prop_ro("thumbnail_level", [](const ScalarFirstRefreshLocals& s) { return sfr_optional(s.thumbnail_level); })
        .def_prop_ro("data_at_thumbnail_level", [](const ScalarFirstRefreshLocals& s) { return sfr_optional(s.data_at_thumbnail_level); })
        .def_prop_ro("data_at_data_level", [](const ScalarFirstRefreshLocals& s) { return sfr_optional(s.data_at_data_level); });
    nb::class_<ScalarEmptyResponseLocals>(module, "ScalarEmptyResponseLocals",
        nb::is_weak_referenceable(), nb::type_slots(sfr_empty_slots))
        .def_prop_ro("request_id", [](const ScalarEmptyResponseLocals& s) { return sfr_optional(s.request_id); })
        .def_prop_ro("shape", [](const ScalarEmptyResponseLocals& s) { return sfr_optional(s.shape); })
        .def_prop_ro("data", [](const ScalarEmptyResponseLocals& s) { return sfr_optional(s.data); })
        .def_prop_ro("image", [](const ScalarEmptyResponseLocals& s) { return sfr_optional(s.image); })
        .def_prop_ro("ndim", [](const ScalarEmptyResponseLocals& s) { return sfr_optional(s.ndim); })
        .def_prop_ro("tile_to_data", [](const ScalarEmptyResponseLocals& s) { return sfr_optional(s.tile_to_data); });
    module.def("scalar_first_refresh_locals", [] { return ScalarFirstRefreshLocals(); });
    module.def("scalar_empty_response_locals", &scalar_empty_response_locals, nb::arg("request_id").none());
    module.def("scalar_first_refresh_set_view", &scalar_first_refresh_set_view,
        nb::arg("self").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("scalar_first_refresh_make_request", &scalar_first_refresh_make_request,
        nb::arg("self").none(), nb::arg("slice_input").none(), nb::arg("data_slice").none(),
        nb::arg("dask_indexer").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("scalar_first_refresh_empty_response", &scalar_first_refresh_empty_response,
        nb::arg("slice_input").none(), nb::arg("rgb").none(), nb::arg("dtype").none(),
        nb::arg("one_shape").none(), nb::arg("rgb_shape").none(),
        nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("scalar_first_refresh_view", &scalar_first_refresh_view,
        nb::arg("cls").none(), nb::arg("view").none());
}
} // namespace nx
