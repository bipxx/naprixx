#include "arrays.hpp"
#include "collection_protocol.hpp"
#include <array>
#include <cstddef>
#include <cstring>
#include <string>
#include <structmember.h>
#include <utility>

namespace nx {
namespace {

nb::object sd_checked(PyObject* value) {
    if (!value) throw nb::python_error();
    return nb::steal<nb::object>(value);
}
nb::object sd_attr(nb::object value, const char* name) {
    return sd_checked(PyObject_GetAttrString(value.ptr(), name));
}
nb::object sd_call0(nb::object callback) { return sd_checked(PyObject_CallNoArgs(callback.ptr())); }
nb::object sd_call1(nb::object callback, nb::object arg) {
    return sd_checked(PyObject_CallOneArg(callback.ptr(), arg.ptr()));
}
nb::object sd_call2(nb::object callback, nb::object a, nb::object b) {
    return sd_checked(PyObject_CallFunctionObjArgs(callback.ptr(), a.ptr(), b.ptr(), nullptr));
}
nb::object sd_call3(nb::object callback, nb::object a, nb::object b, nb::object c) {
    return sd_checked(PyObject_CallFunctionObjArgs(callback.ptr(), a.ptr(), b.ptr(), c.ptr(), nullptr));
}
nb::object sd_kw(nb::object callback, nb::tuple args, nb::dict kwargs) {
    return sd_checked(PyObject_Call(callback.ptr(), args.ptr(), kwargs.ptr()));
}
nb::object sd_item(nb::object value, nb::object key) {
    return sd_checked(PyObject_GetItem(value.ptr(), key.ptr()));
}
void sd_set_item(nb::object value, nb::object key, nb::object replacement) {
    if (PyObject_SetItem(value.ptr(), key.ptr(), replacement.ptr()) < 0) throw nb::python_error();
}
bool sd_truth(nb::object value) {
    const int result = PyObject_IsTrue(value.ptr());
    if (result < 0) throw nb::python_error();
    return result != 0;
}
nb::object sd_compare(nb::object a, nb::object b, int op) {
    return sd_checked(PyObject_RichCompare(a.ptr(), b.ptr(), op));
}
nb::object sd_add(nb::object a, nb::object b) { return sd_checked(PyNumber_Add(a.ptr(), b.ptr())); }
nb::object sd_subtract(nb::object a, nb::object b) { return sd_checked(PyNumber_Subtract(a.ptr(), b.ptr())); }
nb::object sd_multiply(nb::object a, nb::object b) { return sd_checked(PyNumber_Multiply(a.ptr(), b.ptr())); }
nb::object sd_negative(nb::object a) { return sd_checked(PyNumber_Negative(a.ptr())); }
nb::object sd_slice(nb::object start, nb::object stop) {
    return sd_checked(PySlice_New(start.ptr(), stop.ptr(), Py_None));
}
void sd_replace(nb::object& destination, nb::object value) {
    nb::object previous = std::move(destination);
    destination = std::move(value);
    previous.reset();
}
nb::object sd_optional(const nb::object& value) { return value.is_valid() ? value : nb::none(); }
nb::object sd_cell_optional(const nb::object& cell) {
    if (!cell.is_valid()) return nb::none();
    PyObject* value = PyCell_GET(cell.ptr());
    return value ? nb::borrow<nb::object>(value) : nb::none();
}
struct SliceNamespace {
    nb::dict globals;
    nb::object builtins;
    explicit SliceNamespace(nb::dict value)
        : globals(std::move(value)), builtins(nb::borrow<nb::object>(PyEval_GetBuiltins())) {}
    SliceNamespace(nb::dict value, nb::object builtin_values)
        : globals(std::move(value)), builtins(std::move(builtin_values)) {}
    nb::object get(const char* name) const {
        nb::object key = nb::str(name);
        PyObject* value = PyDict_GetItemWithError(globals.ptr(), key.ptr());
        if (value) return nb::borrow<nb::object>(value);
        if (PyErr_Occurred()) throw nb::python_error();
        value = PyObject_GetItem(builtins.ptr(), key.ptr());
        if (value) return nb::steal<nb::object>(value);
        if (!PyErr_ExceptionMatches(PyExc_KeyError)) throw nb::python_error();
        PyErr_Clear();
        nb::object message = sd_checked(PyUnicode_FromFormat("name '%s' is not defined", name));
        nb::object error = sd_call1(nb::borrow<nb::object>(PyExc_NameError), std::move(message));
        if (PyObject_SetAttrString(error.ptr(), "name", key.ptr()) < 0) throw nb::python_error();
        PyErr_SetObject(PyExc_NameError, error.ptr());
        throw nb::python_error();
    }
    nb::object numpy(const char* name) const { return sd_attr(get("np"), name); }
};

// The ordinary descriptor machinery and all saved vars()/__dict__ aliases use
// this one physical dictionary, available at allocation. No field mirror,
// constructor interception or nanobind instance header is present.
struct SliceNamespaceStorage { PyObject_HEAD PyObject* dictionary; PyObject* weakrefs; };
int sd_namespace_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(reinterpret_cast<SliceNamespaceStorage*>(self)->dictionary);
    Py_VISIT(Py_TYPE(self));
    return 0;
}
int sd_namespace_clear(PyObject* self) {
    Py_CLEAR(reinterpret_cast<SliceNamespaceStorage*>(self)->dictionary);
    return 0;
}
int sd_namespace_setdict(PyObject* self, PyObject* value, void*) {
    if (value && !PyDict_Check(value)) {
        PyErr_Format(PyExc_TypeError, "__dict__ must be set to a dictionary, not a '%.200s'", Py_TYPE(value)->tp_name);
        return -1;
    }
    auto* owner = reinterpret_cast<SliceNamespaceStorage*>(self);
    Py_XSETREF(owner->dictionary, Py_XNewRef(value));
    return 0;
}
PyObject* sd_namespace_weakref(PyObject* self, void*) {
    auto* owner = reinterpret_cast<SliceNamespaceStorage*>(self);
    return Py_NewRef(owner->weakrefs ? owner->weakrefs : Py_None);
}
PyMemberDef sd_namespace_members[] = {
    {const_cast<char*>("__dictoffset__"), T_PYSSIZET, offsetof(SliceNamespaceStorage, dictionary), READONLY, nullptr},
    {const_cast<char*>("__weaklistoffset__"), T_PYSSIZET, offsetof(SliceNamespaceStorage, weakrefs), READONLY, nullptr},
    {nullptr, 0, 0, 0, nullptr},
};
PyGetSetDef sd_namespace_getsets[] = {
    {const_cast<char*>("__dict__"), PyObject_GenericGetDict, sd_namespace_setdict, nullptr, nullptr},
    {const_cast<char*>("__weakref__"), sd_namespace_weakref, nullptr, nullptr, nullptr},
    {nullptr, nullptr, nullptr, nullptr, nullptr},
};
PyType_Slot sd_namespace_slots[] = {
    {Py_tp_members, sd_namespace_members}, {Py_tp_getset, sd_namespace_getsets},
    {Py_tp_traverse, reinterpret_cast<void*>(sd_namespace_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(sd_namespace_clear)}, {0, nullptr},
};

struct MakeFullLocals {
    nb::object point, margin_left, margin_right, ndim, val, val_ndim, prepend;
    MakeFullLocals() = default;
    MakeFullLocals(MakeFullLocals&&) = default;
    ~MakeFullLocals() { clear(); }
    void clear() {
        std::array<nb::object, 7> old = {std::move(point), std::move(margin_left), std::move(margin_right),
            std::move(ndim), std::move(val), std::move(val_ndim), std::move(prepend)};
        for (auto& edge : old) edge.reset();
    }
};
struct ResizeLocals {
    nb::object old_ndim, world_slice, order;
    ResizeLocals() = default;
    ResizeLocals(ResizeLocals&&) = default;
    ~ResizeLocals() { clear(); }
    void clear() {
        std::array<nb::object, 3> old = {std::move(old_ndim), std::move(world_slice), std::move(order)};
        for (auto& edge : old) edge.reset();
    }
};
struct OrthogonalLocals {
    nb::object world_to_data, non_displayed_subspace, d, mapped_nd_subspace, displayed_mapped_subspace;
    OrthogonalLocals() = default;
    OrthogonalLocals(OrthogonalLocals&&) = default;
    ~OrthogonalLocals() { clear(); }
    void clear() {
        std::array<nb::object, 5> old = {std::move(world_to_data), std::move(non_displayed_subspace),
            std::move(d), std::move(mapped_nd_subspace), std::move(displayed_mapped_subspace)};
        for (auto& edge : old) edge.reset();
    }
};
struct DataSliceLocals {
    nb::object slice_world_to_data, world_slice_not_disp, data_slice, full_data_slice, i, ax;
    DataSliceLocals() = default;
    DataSliceLocals(DataSliceLocals&&) = default;
    ~DataSliceLocals() { clear(); }
    void clear() {
        std::array<nb::object, 6> old = {std::move(slice_world_to_data), std::move(world_slice_not_disp),
            std::move(data_slice), std::move(full_data_slice), std::move(i), std::move(ax)};
        for (auto& edge : old) edge.reset();
    }
};
int sd_make_full_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) {
        const auto& s = *nb::inst_ptr<MakeFullLocals>(self);
        Py_VISIT(s.point.ptr()); Py_VISIT(s.margin_left.ptr()); Py_VISIT(s.margin_right.ptr());
        Py_VISIT(s.ndim.ptr()); Py_VISIT(s.val.ptr()); Py_VISIT(s.val_ndim.ptr()); Py_VISIT(s.prepend.ptr());
    }
    return 0;
}
int sd_resize_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) {
        const auto& s = *nb::inst_ptr<ResizeLocals>(self);
        Py_VISIT(s.old_ndim.ptr()); Py_VISIT(s.world_slice.ptr()); Py_VISIT(s.order.ptr());
    }
    return 0;
}
int sd_orthogonal_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) {
        const auto& s = *nb::inst_ptr<OrthogonalLocals>(self);
        Py_VISIT(s.world_to_data.ptr()); Py_VISIT(s.non_displayed_subspace.ptr()); Py_VISIT(s.d.ptr());
        Py_VISIT(s.mapped_nd_subspace.ptr()); Py_VISIT(s.displayed_mapped_subspace.ptr());
    }
    return 0;
}
int sd_data_slice_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) {
        const auto& s = *nb::inst_ptr<DataSliceLocals>(self);
        Py_VISIT(s.slice_world_to_data.ptr()); Py_VISIT(s.world_slice_not_disp.ptr()); Py_VISIT(s.data_slice.ptr());
        Py_VISIT(s.full_data_slice.ptr()); Py_VISIT(s.i.ptr()); Py_VISIT(s.ax.ptr());
    }
    return 0;
}
template <typename State> int sd_clear(PyObject* self) {
    if (nb::inst_ready(self)) nb::inst_ptr<State>(self)->clear();
    return 0;
}
PyType_Slot sd_make_full_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(sd_make_full_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(sd_clear<MakeFullLocals>)}, {0, nullptr},
};
PyType_Slot sd_resize_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(sd_resize_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(sd_clear<ResizeLocals>)}, {0, nullptr},
};
PyType_Slot sd_orthogonal_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(sd_orthogonal_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(sd_clear<OrthogonalLocals>)}, {0, nullptr},
};
PyType_Slot sd_data_slice_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(sd_data_slice_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(sd_clear<DataSliceLocals>)}, {0, nullptr},
};

nb::object sd_getter_cell(nb::handle getter) {
    if (!PyFunction_Check(getter.ptr())) throw nb::type_error("cell getter must be a Python function");
    PyObject* closure = PyFunction_GetClosure(getter.ptr());
    if (!closure || PyTuple_GET_SIZE(closure) != 1 || !PyCell_Check(PyTuple_GET_ITEM(closure, 0)))
        throw nb::type_error("cell getter must retain one genuine cell");
    return nb::borrow<nb::object>(PyTuple_GET_ITEM(closure, 0));
}
void sd_cell_store(nb::handle cell, nb::object value) {
    if (PyCell_Set(cell.ptr(), value.ptr()) < 0) throw nb::python_error();
}

// Roles are identified from semantic values in the original generated function,
// never from private stdlib free-variable spellings. Only its cells survive.
void slice_dataclass_install_init(nb::object cls, nb::object original_init, nb::object signature_glue) {
    if (!PyFunction_Check(original_init.ptr()) || !PyFunction_Check(signature_glue.ptr()))
        throw nb::type_error("constructor handoff requires Python functions");
    PyObject* old_closure = PyFunction_GetClosure(original_init.ptr());
    PyObject* glue_closure = PyFunction_GetClosure(signature_glue.ptr());
    nb::object code = sd_attr(signature_glue, "__code__");
    nb::object names = sd_attr(std::move(code), "co_freevars");
    const Py_ssize_t count = PyTuple_Size(names.ptr());
    if (count < 0) throw nb::python_error();
    nb::tuple rebased = nb::steal<nb::tuple>(PyTuple_New(count));
    if (!rebased.is_valid()) throw nb::python_error();
    nb::object sentinel, factory;
    if (count == 3) {
        PyObject* defaults = PyFunction_GetDefaults(original_init.ptr());
        if (!defaults || !PyTuple_Check(defaults) || PyTuple_GET_SIZE(defaults) < 1)
            throw nb::type_error("request constructor has no factory sentinel");
        sentinel = nb::borrow<nb::object>(PyTuple_GET_ITEM(defaults, PyTuple_GET_SIZE(defaults) - 1));
        nb::object field = sd_item(sd_attr(cls, "__dataclass_fields__"), nb::str("id"));
        factory = sd_attr(std::move(field), "default_factory");
    }
    for (Py_ssize_t i = 0; i < count; ++i) {
        const char* name = PyUnicode_AsUTF8(PyTuple_GET_ITEM(names.ptr(), i));
        if (!name) throw nb::python_error();
        PyObject* wanted = nullptr;
        if (std::strcmp(name, "_object_capture") == 0) wanted = reinterpret_cast<PyObject*>(&PyBaseObject_Type);
        else if (std::strcmp(name, "_sentinel_capture") == 0) wanted = sentinel.ptr();
        else if (std::strcmp(name, "_id_factory_capture") == 0) wanted = factory.ptr();
        else throw nb::type_error("unexpected constructor glue capture");
        PyObject* cell = nullptr;
        if (old_closure) {
            for (Py_ssize_t j = 0; j < PyTuple_GET_SIZE(old_closure); ++j) {
                PyObject* candidate = PyTuple_GET_ITEM(old_closure, j);
                if (PyCell_Check(candidate) && PyCell_GET(candidate) == wanted) { cell = candidate; break; }
            }
        }
        if (!cell || !glue_closure) throw nb::type_error("original constructor semantic cell is missing");
        Py_INCREF(cell);
        PyTuple_SET_ITEM(rebased.ptr(), i, cell);
    }
    if (PyFunction_SetClosure(signature_glue.ptr(), rebased.ptr()) < 0) throw nb::python_error();
    PyObject* defaults = PyFunction_GetDefaults(original_init.ptr());
    if (PyFunction_SetDefaults(signature_glue.ptr(), defaults ? defaults : Py_None) < 0) throw nb::python_error();
    PyObject* kwdefaults = PyFunction_GetKwDefaults(original_init.ptr());
    if (PyFunction_SetKwDefaults(signature_glue.ptr(), kwdefaults ? kwdefaults : Py_None) < 0) throw nb::python_error();
    for (const char* name : {"__name__", "__qualname__", "__doc__", "__module__", "__annotations__"}) {
        nb::object value = sd_attr(original_init, name);
        if (PyObject_SetAttrString(signature_glue.ptr(), name, value.ptr()) < 0) throw nb::python_error();
    }
#if PY_VERSION_HEX >= 0x030C0000
    nb::object type_params = sd_attr(original_init, "__type_params__");
    if (PyObject_SetAttrString(signature_glue.ptr(), "__type_params__", type_params.ptr()) < 0) throw nb::python_error();
#endif
    if (PyObject_SetAttrString(cls.ptr(), "__init__", signature_glue.ptr()) < 0) throw nb::python_error();
}

void sd_store_field(nb::object self, nb::object getter, const char* name, nb::object value) {
    nb::object setter = sd_attr(sd_call0(std::move(getter)), "__setattr__");
    nb::object field = sd_checked(PyUnicode_InternFromString(name));
    sd_call3(std::move(setter), std::move(self), std::move(field), std::move(value));
}
void slice_dataclass_thick_init(nb::object self, nb::object point, nb::object margin_left,
    nb::object margin_right, nb::object saved_object_getter) {
    sd_store_field(self, saved_object_getter, "point", point);
    sd_store_field(self, saved_object_getter, "margin_left", margin_left);
    sd_store_field(self, saved_object_getter, "margin_right", margin_right);
}
void slice_dataclass_input_init(nb::object self, nb::object ndisplay, nb::object world_slice,
    nb::object order, nb::object saved_object_getter) {
    sd_store_field(self, saved_object_getter, "ndisplay", ndisplay);
    sd_store_field(self, saved_object_getter, "world_slice", world_slice);
    sd_store_field(self, saved_object_getter, "order", order);
}
void slice_dataclass_view_init(nb::object self, nb::object raw, nb::object view, nb::object saved_object_getter) {
    sd_store_field(self, saved_object_getter, "raw", raw);
    sd_store_field(self, saved_object_getter, "view", view);
}
void slice_dataclass_response_init(nb::object self, nb::object image, nb::object thumbnail,
    nb::object tile_to_data, nb::object slice_input, nb::object request_id, nb::object empty,
    nb::object saved_object_getter) {
    sd_store_field(self, saved_object_getter, "image", image);
    sd_store_field(self, saved_object_getter, "thumbnail", thumbnail);
    sd_store_field(self, saved_object_getter, "tile_to_data", tile_to_data);
    sd_store_field(self, saved_object_getter, "slice_input", slice_input);
    sd_store_field(self, saved_object_getter, "request_id", request_id);
    sd_store_field(self, saved_object_getter, "empty", empty);
}
void slice_dataclass_request_init(nb::object self, nb::object slice_input, nb::object data_at_data_level,
    nb::object data_at_thumbnail_level, nb::object dtype, nb::object dask_indexer, nb::object data_slice,
    nb::object projection_mode, nb::object multiscale, nb::object corner_pixels, nb::object rgb,
    nb::object data_level, nb::object thumbnail_level, nb::object level_shapes, nb::object downsample_factors,
    nb::object id, nb::object saved_object_getter, nb::object sentinel_getter, nb::object id_factory_getter) {
    sd_store_field(self, saved_object_getter, "slice_input", slice_input);
    sd_store_field(self, saved_object_getter, "data_at_data_level", data_at_data_level);
    sd_store_field(self, saved_object_getter, "data_at_thumbnail_level", data_at_thumbnail_level);
    sd_store_field(self, saved_object_getter, "dtype", dtype);
    sd_store_field(self, saved_object_getter, "dask_indexer", dask_indexer);
    sd_store_field(self, saved_object_getter, "data_slice", data_slice);
    sd_store_field(self, saved_object_getter, "projection_mode", projection_mode);
    sd_store_field(self, saved_object_getter, "multiscale", multiscale);
    sd_store_field(self, saved_object_getter, "corner_pixels", corner_pixels);
    sd_store_field(self, saved_object_getter, "rgb", rgb);
    sd_store_field(self, saved_object_getter, "data_level", data_level);
    sd_store_field(self, saved_object_getter, "thumbnail_level", thumbnail_level);
    sd_store_field(self, saved_object_getter, "level_shapes", level_shapes);
    sd_store_field(self, saved_object_getter, "downsample_factors", downsample_factors);
    nb::object setter = sd_attr(sd_call0(saved_object_getter), "__setattr__");
    nb::object field = sd_checked(PyUnicode_InternFromString("id"));
    nb::object sentinel = sd_call0(sentinel_getter);
    const bool use_factory = id.ptr() == sentinel.ptr();
    sentinel.reset();
    nb::object value = use_factory ? sd_call0(sd_call0(id_factory_getter)) : id;
    sd_call3(std::move(setter), self, std::move(field), std::move(value));
}

nb::object slice_dataclass_thick_ndim(nb::object self, nb::dict namespace_) {
    SliceNamespace ns(std::move(namespace_));
    nb::object callback = ns.get("len");
    nb::object point = sd_attr(self, "point");
    return sd_call1(std::move(callback), std::move(point));
}
nb::object slice_dataclass_thick_make_full(nb::object cls, nb::dict namespace_, MakeFullLocals& s,
    nb::object zero_tuple) {
    SliceNamespace ns(std::move(namespace_));
    {
        nb::tuple values = nb::make_tuple(s.point, s.margin_left, s.margin_right);
        nb::object iterator = sd_checked(PyObject_GetIter(values.ptr()));
        bool found = false;
        while (true) {
            PyObject* next = PyIter_Next(iterator.ptr());
            if (!next) { if (PyErr_Occurred()) throw nb::python_error(); break; }
            sd_replace(s.val, nb::steal<nb::object>(next));
            if (!s.val.is_none()) {
                sd_replace(s.val_ndim, sd_call1(ns.get("len"), s.val));
                found = true;
                break;
            }
        }
        if (!found) {
            if (s.ndim.is_none()) {
                nb::object error = sd_call1(ns.get("ValueError"), nb::str("ndim must be provided if no other value is given"));
                collections::raise_exception(error, false);
            }
            sd_replace(s.val_ndim, s.ndim);
        }
    }
    sd_replace(s.ndim, s.ndim.is_none() ? s.val_ndim : s.ndim);
    sd_replace(s.point, s.point.is_none() ? sd_multiply(zero_tuple, s.ndim) : sd_call1(ns.get("tuple"), s.point));
    sd_replace(s.margin_left, s.margin_left.is_none() ? sd_multiply(zero_tuple, s.ndim) : sd_call1(ns.get("tuple"), s.margin_left));
    sd_replace(s.margin_right, s.margin_right.is_none() ? sd_multiply(zero_tuple, s.ndim) : sd_call1(ns.get("tuple"), s.margin_right));
    nb::object maximum = ns.get("max");
    nb::object difference = sd_subtract(s.ndim, s.val_ndim);
    sd_replace(s.prepend, sd_call2(std::move(maximum), std::move(difference), nb::int_(0)));
    sd_replace(s.point, sd_add(sd_multiply(zero_tuple, s.prepend), s.point));
    sd_replace(s.margin_left, sd_add(sd_multiply(zero_tuple, s.prepend), s.margin_left));
    sd_replace(s.margin_right, sd_add(sd_multiply(zero_tuple, s.prepend), s.margin_right));
    nb::dict kwargs;
    kwargs["point"] = sd_item(s.point, sd_slice(sd_negative(s.ndim), nb::none()));
    kwargs["margin_left"] = sd_item(s.margin_left, sd_slice(sd_negative(s.ndim), nb::none()));
    kwargs["margin_right"] = sd_item(s.margin_right, sd_slice(sd_negative(s.ndim), nb::none()));
    return sd_kw(cls, nb::tuple(), std::move(kwargs));
}
nb::object slice_dataclass_thick_from_dims(nb::object cls, nb::object dims) {
    nb::object callback = sd_attr(cls, "make_full");
    nb::object point = sd_attr(dims, "point");
    nb::object left = sd_attr(dims, "margin_left");
    nb::object right = sd_attr(dims, "margin_right");
    return sd_call3(std::move(callback), std::move(point), std::move(left), std::move(right));
}
nb::object slice_dataclass_thick_copy_with(nb::object self, nb::object point, nb::object margin_left,
    nb::object margin_right, nb::object ndim) {
    nb::object callback = sd_attr(self, "make_full");
    nb::dict kwargs;
    kwargs["point"] = sd_truth(point) ? point : sd_attr(self, "point");
    kwargs["margin_left"] = sd_truth(margin_left) ? margin_left : sd_attr(self, "margin_left");
    kwargs["margin_right"] = sd_truth(margin_right) ? margin_right : sd_attr(self, "margin_right");
    kwargs["ndim"] = sd_truth(ndim) ? ndim : sd_attr(self, "ndim");
    return sd_kw(std::move(callback), nb::tuple(), std::move(kwargs));
}
nb::object sd_array_field(SliceNamespace& ns, nb::object self, const char* name, nb::object key) {
    nb::object consumer = ns.get("tuple");
    nb::object array = ns.numpy("array");
    nb::object value = sd_attr(self, name);
    nb::object selected = sd_item(sd_call1(std::move(array), std::move(value)), std::move(key));
    return sd_call1(std::move(consumer), std::move(selected));
}
nb::object slice_dataclass_thick_getitem(nb::object self, nb::object key, nb::dict namespace_) {
    SliceNamespace ns(std::move(namespace_));
    nb::object constructor = ns.get("_ThickNDSlice");
    nb::dict kwargs;
    kwargs["point"] = sd_array_field(ns, self, "point", key);
    kwargs["margin_left"] = sd_array_field(ns, self, "margin_left", key);
    kwargs["margin_right"] = sd_array_field(ns, self, "margin_right", key);
    return sd_kw(std::move(constructor), nb::tuple(), std::move(kwargs));
}
nb::object slice_dataclass_thick_iter(nb::object self, nb::dict namespace_) {
    SliceNamespace ns(std::move(namespace_));
    nb::object callback = ns.get("zip");
    nb::object point = sd_attr(self, "point");
    nb::object left = sd_attr(self, "margin_left");
    nb::object right = sd_attr(self, "margin_right");
    nb::tuple args = nb::make_tuple(point, left, right);
    point.reset(); left.reset(); right.reset();
    nb::dict kwargs; kwargs["strict"] = nb::bool_(false);
    return sd_kw(std::move(callback), std::move(args), std::move(kwargs));
}
nb::object slice_dataclass_input_ndim(nb::object self, nb::dict namespace_) {
    SliceNamespace ns(std::move(namespace_));
    nb::object callback = ns.get("len");
    nb::object order = sd_attr(self, "order");
    return sd_call1(std::move(callback), std::move(order));
}
nb::object slice_dataclass_input_displayed(nb::object self, nb::dict namespace_) {
    SliceNamespace ns(std::move(namespace_));
    nb::object callback = ns.get("list");
    nb::object order = sd_attr(self, "order");
    nb::object start = sd_negative(sd_attr(self, "ndisplay"));
    return sd_call1(std::move(callback), sd_item(std::move(order), sd_slice(std::move(start), nb::none())));
}
nb::object slice_dataclass_input_not_displayed(nb::object self, nb::dict namespace_) {
    SliceNamespace ns(std::move(namespace_));
    nb::object callback = ns.get("list");
    nb::object order = sd_attr(self, "order");
    nb::object stop = sd_negative(sd_attr(self, "ndisplay"));
    return sd_call1(std::move(callback), sd_item(std::move(order), sd_slice(nb::none(), std::move(stop))));
}

enum class SliceIteratorKind { Resize, Mapped, Compare };
struct SliceIterator {
    SliceIteratorKind kind = SliceIteratorKind::Resize;
    bool started = false, running = false, closed = false;
    nb::object iterator, current, getter, other_getter, namespace_, builtins, threshold;
    SliceIterator() = default;
    SliceIterator(SliceIterator&&) = default;
    ~SliceIterator() { clear(); }
    void clear() {
        closed = true;
        std::array<nb::object, 7> old = {std::move(iterator), std::move(current), std::move(getter),
            std::move(other_getter), std::move(namespace_), std::move(builtins), std::move(threshold)};
        for (auto& edge : old) edge.reset();
    }
};
struct SliceRunning {
    SliceIterator& s;
    explicit SliceRunning(SliceIterator& state) : s(state) {
        if (s.running) throw nb::value_error("generator already executing");
        s.running = true;
    }
    ~SliceRunning() { s.running = false; }
};
nb::object sd_iterator_next(nb::handle self) {
    auto& s = *nb::inst_ptr<SliceIterator>(self);
    SliceRunning active(s);
    if (s.closed) throw nb::stop_iteration();
    s.started = true;
    try {
        PyObject* next = PyIter_Next(s.iterator.ptr());
        if (!next) {
            if (PyErr_Occurred()) throw nb::python_error();
            s.clear();
            throw nb::stop_iteration();
        }
        sd_replace(s.current, nb::steal<nb::object>(next));
        if (s.kind == SliceIteratorKind::Resize) {
            nb::object sum = sd_add(s.current, sd_call0(s.getter));
            return sd_subtract(std::move(sum), sd_call0(s.other_getter));
        }
        if (s.kind == SliceIteratorKind::Mapped) return sd_item(sd_call0(s.getter), s.current);
        SliceNamespace ns(nb::borrow<nb::dict>(s.namespace_), s.builtins);
        nb::object absolute = ns.get("abs");
        return sd_compare(sd_call1(std::move(absolute), s.current), s.threshold, Py_LT);
    } catch (const nb::python_error& error) {
        events::HandledException handled(error.value());
        s.closed = true;
        if (error.matches(PyExc_StopIteration)) collections::raise_exception(error.value(), true);
        throw;
    } catch (...) { s.clear(); throw; }
}
void sd_iterator_close(nb::handle self) {
    auto& s = *nb::inst_ptr<SliceIterator>(self);
    SliceRunning active(s);
    if (s.closed) return;
    if (!s.started) { s.clear(); return; }
    nb::object exit = sd_call0(nb::borrow<nb::object>(PyExc_GeneratorExit));
    events::HandledException handled(exit);
    s.clear();
}
void sd_iterator_throw(nb::handle self, nb::args args) {
    nb::object exception = collections::normalize_throw(args);
    auto& s = *nb::inst_ptr<SliceIterator>(self);
    SliceRunning active(s);
    bool open = !s.closed;
    s.closed = true;
    events::HandledException handled(exception);
    collections::raise_exception(exception, open);
}
int sd_iterator_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) {
        const auto& s = *nb::inst_ptr<SliceIterator>(self);
        Py_VISIT(s.iterator.ptr()); Py_VISIT(s.current.ptr()); Py_VISIT(s.getter.ptr());
        Py_VISIT(s.other_getter.ptr()); Py_VISIT(s.namespace_.ptr()); Py_VISIT(s.builtins.ptr()); Py_VISIT(s.threshold.ptr());
    }
    return 0;
}
void sd_iterator_finalize(PyObject* self) noexcept {
    if (!nb::inst_ready(self) || nb::inst_ptr<SliceIterator>(self)->closed) return;
    PyObject *type = nullptr, *value = nullptr, *trace = nullptr;
    PyErr_Fetch(&type, &value, &trace);
    try { sd_iterator_close(self); }
    catch (nb::python_error& error) { error.restore(); PyErr_WriteUnraisable(self); }
    catch (const std::exception& error) { PyErr_SetString(PyExc_RuntimeError, error.what()); PyErr_WriteUnraisable(self); }
    catch (...) { PyErr_SetString(PyExc_RuntimeError, "failed to close slice iterator"); PyErr_WriteUnraisable(self); }
    PyErr_Restore(type, value, trace);
}
PyType_Slot sd_iterator_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(sd_iterator_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(sd_clear<SliceIterator>)},
    {Py_tp_finalize, reinterpret_cast<void*>(sd_iterator_finalize)}, {0, nullptr},
};
destructor sd_iterator_base_dealloc = nullptr;
void sd_iterator_dealloc(PyObject* self) noexcept {
    PyObject_GC_UnTrack(self);
    PyObject_ClearWeakRefs(self);
    PyObject_GC_Track(self);
    if (PyObject_CallFinalizerFromDealloc(self) < 0) return;
    sd_iterator_base_dealloc(self);
}
nb::object sd_generator(SliceIteratorKind kind, nb::object iterator, nb::object getter, nb::object other_getter,
    SliceNamespace& ns, nb::object threshold, nb::object factory, const char* qualname) {
    SliceIterator state;
    state.kind = kind; state.iterator = std::move(iterator); state.getter = std::move(getter);
    state.other_getter = std::move(other_getter); state.namespace_ = ns.globals;
    state.builtins = ns.builtins; state.threshold = std::move(threshold);
    nb::object delegate = nb::cast(std::move(state), nb::rv_policy::move);
    nb::object generator = sd_call1(std::move(factory), std::move(delegate));
    nb::object name = nb::str("<genexpr>"), qualified = nb::str(qualname);
    if (PyObject_SetAttrString(generator.ptr(), "__name__", name.ptr()) < 0 ||
        PyObject_SetAttrString(generator.ptr(), "__qualname__", qualified.ptr()) < 0) throw nb::python_error();
    return generator;
}

nb::object slice_dataclass_input_with_ndim(nb::object self, nb::dict namespace_, nb::object ndim_getter,
    nb::object old_ndim_getter, nb::object generator_factory, ResizeLocals& s) {
    SliceNamespace ns(std::move(namespace_));
    sd_replace(s.old_ndim, sd_getter_cell(old_ndim_getter));
    sd_cell_store(s.old_ndim, sd_attr(self, "ndim"));
    nb::object copy = sd_attr(sd_attr(self, "world_slice"), "copy_with");
    nb::dict copy_args; copy_args["ndim"] = sd_call0(ndim_getter);
    sd_replace(s.world_slice, sd_kw(std::move(copy), nb::tuple(), std::move(copy_args)));
    nb::object old_ndim = sd_call0(old_ndim_getter);
    nb::object ndim = sd_call0(ndim_getter);
    const bool smaller = sd_truth(sd_compare(std::move(old_ndim), std::move(ndim), Py_GT));
    if (smaller) {
        nb::object reorder = ns.get("reorder_after_dim_reduction");
        nb::object order = sd_attr(self, "order");
        nb::object start = sd_negative(sd_call0(ndim_getter));
        sd_replace(s.order, sd_call1(std::move(reorder), sd_item(std::move(order), sd_slice(std::move(start), nb::none()))));
    } else {
        old_ndim = sd_call0(old_ndim_getter);
        ndim = sd_call0(ndim_getter);
        const bool larger = sd_truth(sd_compare(std::move(old_ndim), std::move(ndim), Py_LT));
        if (larger) {
            nb::object first_tuple = ns.get("tuple");
            nb::object range = ns.get("range");
            ndim = sd_call0(ndim_getter);
            old_ndim = sd_call0(old_ndim_getter);
            nb::object first = sd_call1(std::move(first_tuple), sd_call1(std::move(range), sd_subtract(std::move(ndim), std::move(old_ndim))));
            nb::object second_tuple = ns.get("tuple");
            nb::object iterator = sd_checked(PyObject_GetIter(sd_attr(self, "order").ptr()));
            nb::object generator = sd_generator(SliceIteratorKind::Resize, std::move(iterator), ndim_getter,
                old_ndim_getter, ns, nb::object(), generator_factory, "_SliceInput.with_ndim.<locals>.<genexpr>");
            nb::object second = sd_call1(std::move(second_tuple), std::move(generator));
            sd_replace(s.order, sd_add(std::move(first), std::move(second)));
        } else sd_replace(s.order, sd_attr(self, "order"));
    }
    nb::object constructor = ns.get("_SliceInput");
    nb::dict kwargs;
    kwargs["ndisplay"] = sd_attr(self, "ndisplay"); kwargs["world_slice"] = s.world_slice; kwargs["order"] = s.order;
    return sd_kw(std::move(constructor), nb::tuple(), std::move(kwargs));
}
nb::object slice_dataclass_input_is_orthogonal(nb::object self, nb::dict namespace_, nb::object mapped_getter,
    nb::object generator_factory, OrthogonalLocals& s, nb::object threshold) {
    SliceNamespace ns(std::move(namespace_));
    sd_replace(s.mapped_nd_subspace, sd_getter_cell(mapped_getter));
    nb::object zeros = ns.numpy("zeros");
    nb::object ndim = sd_attr(self, "ndim");
    sd_replace(s.non_displayed_subspace, sd_call1(std::move(zeros), std::move(ndim)));
    {
        nb::object iterator = sd_checked(PyObject_GetIter(sd_attr(self, "not_displayed").ptr()));
        while (true) {
            PyObject* next = PyIter_Next(iterator.ptr());
            if (!next) { if (PyErr_Occurred()) throw nb::python_error(); break; }
            sd_replace(s.d, nb::steal<nb::object>(next));
            sd_set_item(s.non_displayed_subspace, s.d, nb::int_(1));
        }
    }
    nb::object affine = ns.get("Affine");
    nb::dict kwargs;
    kwargs["ndim"] = sd_attr(self, "ndim");
    kwargs["linear_matrix"] = sd_attr(s.world_to_data, "linear_matrix");
    kwargs["translate"] = nb::none();
    sd_replace(s.world_to_data, sd_kw(std::move(affine), nb::tuple(), std::move(kwargs)));
    sd_cell_store(s.mapped_nd_subspace, sd_call1(s.world_to_data, s.non_displayed_subspace));
    nb::object iterator = sd_checked(PyObject_GetIter(sd_attr(self, "displayed").ptr()));
    sd_replace(s.displayed_mapped_subspace, sd_generator(SliceIteratorKind::Mapped, std::move(iterator),
        mapped_getter, nb::object(), ns, nb::object(), generator_factory, "_SliceInput.is_orthogonal.<locals>.<genexpr>"));
    nb::object all = ns.get("all");
    nb::object comparison_iterator = sd_checked(PyObject_GetIter(s.displayed_mapped_subspace.ptr()));
    nb::object comparisons = sd_generator(SliceIteratorKind::Compare, std::move(comparison_iterator),
        nb::object(), nb::object(), ns, std::move(threshold), generator_factory, "_SliceInput.is_orthogonal.<locals>.<genexpr>");
    return sd_call1(std::move(all), std::move(comparisons));
}
nb::object sd_data_slice(nb::object self, nb::object world_to_data, nb::dict namespace_,
    nb::object warning_message, DataSliceLocals& s) {
    SliceNamespace ns(std::move(namespace_));
    if (!sd_truth(sd_call1(sd_attr(self, "is_orthogonal"), world_to_data))) {
        nb::object warn = sd_attr(ns.get("warnings"), "warn");
        nb::dict kwargs; kwargs["category"] = ns.get("UserWarning");
        sd_kw(std::move(warn), nb::make_tuple(warning_message), std::move(kwargs));
    }
    nb::object set_slice = sd_attr(world_to_data, "set_slice");
    nb::object not_displayed = sd_attr(self, "not_displayed");
    sd_replace(s.slice_world_to_data, sd_call1(std::move(set_slice), std::move(not_displayed)));
    nb::object world_slice = sd_attr(self, "world_slice");
    nb::object indices = sd_attr(self, "not_displayed");
    sd_replace(s.world_slice_not_disp, sd_call0(sd_attr(sd_item(std::move(world_slice), std::move(indices)), "as_array")));
    sd_replace(s.data_slice, sd_call1(s.slice_world_to_data, s.world_slice_not_disp));
    {
        nb::object target = s.data_slice;
        nb::object key = sd_slice(nb::int_(1), nb::none());
        nb::object left = sd_item(target, key);
        nb::object right = sd_attr(s.slice_world_to_data, "translate");
        nb::object value = sd_checked(PyNumber_InPlaceSubtract(left.ptr(), right.ptr()));
        left.reset(); right.reset();
        sd_set_item(std::move(target), std::move(key), std::move(value));
    }
    nb::object full = ns.numpy("full");
    nb::object ndim = sd_attr(self, "ndim");
    nb::tuple shape = nb::make_tuple(nb::int_(3), ndim); ndim.reset();
    nb::object nan = ns.numpy("nan");
    sd_replace(s.full_data_slice, sd_call2(std::move(full), std::move(shape), std::move(nan)));
    {
        nb::object enumerate = ns.get("enumerate");
        nb::object axes = sd_attr(self, "not_displayed");
        nb::object iterator = sd_checked(PyObject_GetIter(sd_call1(std::move(enumerate), std::move(axes)).ptr()));
        while (true) {
            nb::object item = nb::steal<nb::object>(PyIter_Next(iterator.ptr()));
            if (!item.is_valid()) { if (PyErr_Occurred()) throw nb::python_error(); break; }
            nb::tuple pair = events::pair(item); item.reset();
            sd_replace(s.i, nb::borrow<nb::object>(pair[0]));
            sd_replace(s.ax, nb::borrow<nb::object>(pair[1])); pair.reset();
            nb::object convert = ns.numpy("nan_to_num");
            nb::object selected = sd_item(s.data_slice, nb::make_tuple(sd_slice(nb::none(), nb::none()), s.i));
            nb::dict kwargs; kwargs["nan"] = nb::int_(0);
            nb::object value = sd_kw(std::move(convert), nb::make_tuple(selected), std::move(kwargs)); selected.reset();
            nb::object target = s.full_data_slice;
            nb::object key = nb::make_tuple(sd_slice(nb::none(), nb::none()), s.ax);
            sd_set_item(std::move(target), std::move(key), std::move(value));
        }
    }
    nb::object constructor = sd_attr(ns.get("_ThickNDSlice"), "from_array");
    return sd_call1(std::move(constructor), s.full_data_slice);
}
nb::object slice_dataclass_next_request_id(nb::dict namespace_) {
    SliceNamespace ns(std::move(namespace_));
    nb::object next = ns.get("next");
    nb::object ids = ns.get("_request_ids");
    return sd_call1(std::move(next), std::move(ids));
}

} // namespace

nb::object slice_dataclass_data_slice_compat(nb::object self, nb::object world_to_data,
    nb::dict namespace_, nb::object warning_message) {
    nb::object locals_owner = nb::cast(DataSliceLocals(), nb::rv_policy::move);
    return sd_data_slice(std::move(self), std::move(world_to_data), std::move(namespace_),
        std::move(warning_message), *nb::inst_ptr<DataSliceLocals>(locals_owner));
}

void bind_slice_dataclass_state(nb::module_& module) {
    PyType_Spec namespace_spec = {"naparixx._native.SliceDataclassNamespaceBase",
        static_cast<int>(sizeof(SliceNamespaceStorage)), 0,
        Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HAVE_GC, sd_namespace_slots};
    module.attr("SliceDataclassNamespaceBase") = sd_checked(PyType_FromSpec(&namespace_spec));
    nb::class_<MakeFullLocals>(module, "SliceDataclassMakeFullLocals", nb::is_weak_referenceable(), nb::type_slots(sd_make_full_slots))
        .def_prop_ro("point", [](const MakeFullLocals& s) { return sd_optional(s.point); })
        .def_prop_ro("margin_left", [](const MakeFullLocals& s) { return sd_optional(s.margin_left); })
        .def_prop_ro("margin_right", [](const MakeFullLocals& s) { return sd_optional(s.margin_right); })
        .def_prop_ro("ndim", [](const MakeFullLocals& s) { return sd_optional(s.ndim); })
        .def_prop_ro("val", [](const MakeFullLocals& s) { return sd_optional(s.val); })
        .def_prop_ro("val_ndim", [](const MakeFullLocals& s) { return sd_optional(s.val_ndim); })
        .def_prop_ro("prepend", [](const MakeFullLocals& s) { return sd_optional(s.prepend); });
    nb::class_<ResizeLocals>(module, "SliceDataclassResizeLocals", nb::is_weak_referenceable(), nb::type_slots(sd_resize_slots))
        .def_prop_ro("old_ndim", [](const ResizeLocals& s) { return sd_cell_optional(s.old_ndim); })
        .def_prop_ro("world_slice", [](const ResizeLocals& s) { return sd_optional(s.world_slice); })
        .def_prop_ro("order", [](const ResizeLocals& s) { return sd_optional(s.order); });
    nb::class_<OrthogonalLocals>(module, "SliceDataclassOrthogonalLocals", nb::is_weak_referenceable(), nb::type_slots(sd_orthogonal_slots))
        .def_prop_ro("world_to_data", [](const OrthogonalLocals& s) { return sd_optional(s.world_to_data); })
        .def_prop_ro("non_displayed_subspace", [](const OrthogonalLocals& s) { return sd_optional(s.non_displayed_subspace); })
        .def_prop_ro("d", [](const OrthogonalLocals& s) { return sd_optional(s.d); })
        .def_prop_ro("mapped_nd_subspace", [](const OrthogonalLocals& s) { return sd_cell_optional(s.mapped_nd_subspace); })
        .def_prop_ro("displayed_mapped_subspace", [](const OrthogonalLocals& s) { return sd_optional(s.displayed_mapped_subspace); });
    nb::class_<DataSliceLocals>(module, "SliceDataclassDataSliceLocals", nb::is_weak_referenceable(), nb::type_slots(sd_data_slice_slots))
        .def_prop_ro("slice_world_to_data", [](const DataSliceLocals& s) { return sd_optional(s.slice_world_to_data); })
        .def_prop_ro("world_slice_not_disp", [](const DataSliceLocals& s) { return sd_optional(s.world_slice_not_disp); })
        .def_prop_ro("data_slice", [](const DataSliceLocals& s) { return sd_optional(s.data_slice); })
        .def_prop_ro("full_data_slice", [](const DataSliceLocals& s) { return sd_optional(s.full_data_slice); })
        .def_prop_ro("i", [](const DataSliceLocals& s) { return sd_optional(s.i); })
        .def_prop_ro("ax", [](const DataSliceLocals& s) { return sd_optional(s.ax); });
    nb::class_<SliceIterator> iterator_type(module, "SliceDataclassIterator", nb::is_weak_referenceable(), nb::type_slots(sd_iterator_slots));
    iterator_type.def("__iter__", [](nb::handle self) { return nb::borrow<nb::object>(self); })
        .def("__next__", &sd_iterator_next)
        .def("send", [](nb::handle self, nb::object value) {
            const auto& s = *nb::inst_ptr<SliceIterator>(self);
            if (s.running) throw nb::value_error("generator already executing");
            if (!s.started && !value.is_none()) throw nb::type_error("can't send non-None value to a just-started generator");
            return sd_iterator_next(self);
        }, nb::arg("value").none())
        .def("close", &sd_iterator_close).def("throw", &sd_iterator_throw);
    auto* heap_type = reinterpret_cast<PyTypeObject*>(iterator_type.ptr());
    sd_iterator_base_dealloc = heap_type->tp_dealloc;
    heap_type->tp_dealloc = sd_iterator_dealloc;
    PyType_Modified(heap_type);
    module.def("slice_dataclass_make_full_locals", [](nb::object point, nb::object margin_left, nb::object margin_right, nb::object ndim) {
        MakeFullLocals s; s.point = std::move(point); s.margin_left = std::move(margin_left);
        s.margin_right = std::move(margin_right); s.ndim = std::move(ndim); return s;
    }, nb::arg("point").none(), nb::arg("margin_left").none(), nb::arg("margin_right").none(), nb::arg("ndim").none());
    module.def("slice_dataclass_resize_locals", [] { return ResizeLocals(); });
    module.def("slice_dataclass_orthogonal_locals", [](nb::object world_to_data) {
        OrthogonalLocals s; s.world_to_data = std::move(world_to_data); return s;
    }, nb::arg("world_to_data").none());
    module.def("slice_dataclass_slice_locals", [] { return DataSliceLocals(); });
    module.def("slice_dataclass_install_init", &slice_dataclass_install_init, nb::arg("cls").none(), nb::arg("original_init").none(), nb::arg("signature_glue").none());
    module.def("slice_dataclass_thick_init", &slice_dataclass_thick_init, nb::arg("self").none(), nb::arg("point").none(), nb::arg("margin_left").none(), nb::arg("margin_right").none(), nb::arg("saved_object_getter").none());
    module.def("slice_dataclass_input_init", &slice_dataclass_input_init, nb::arg("self").none(), nb::arg("ndisplay").none(), nb::arg("world_slice").none(), nb::arg("order").none(), nb::arg("saved_object_getter").none());
    module.def("slice_dataclass_view_init", &slice_dataclass_view_init, nb::arg("self").none(), nb::arg("raw").none(), nb::arg("view").none(), nb::arg("saved_object_getter").none());
    module.def("slice_dataclass_response_init", &slice_dataclass_response_init, nb::arg("self").none(), nb::arg("image").none(), nb::arg("thumbnail").none(), nb::arg("tile_to_data").none(), nb::arg("slice_input").none(), nb::arg("request_id").none(), nb::arg("empty").none(), nb::arg("saved_object_getter").none());
    module.def("slice_dataclass_request_init", &slice_dataclass_request_init, nb::arg("self").none(), nb::arg("slice_input").none(), nb::arg("data_at_data_level").none(), nb::arg("data_at_thumbnail_level").none(), nb::arg("dtype").none(), nb::arg("dask_indexer").none(), nb::arg("data_slice").none(), nb::arg("projection_mode").none(), nb::arg("multiscale").none(), nb::arg("corner_pixels").none(), nb::arg("rgb").none(), nb::arg("data_level").none(), nb::arg("thumbnail_level").none(), nb::arg("level_shapes").none(), nb::arg("downsample_factors").none(), nb::arg("id").none(), nb::arg("saved_object_getter").none(), nb::arg("sentinel_getter").none(), nb::arg("id_factory_getter").none());
    module.def("slice_dataclass_thick_ndim", &slice_dataclass_thick_ndim, nb::arg("self").none(), nb::arg("namespace"));
    module.def("slice_dataclass_thick_make_full", &slice_dataclass_thick_make_full, nb::arg("cls").none(), nb::arg("namespace"), nb::arg("locals_owner"), nb::arg("zero_tuple").none());
    module.def("slice_dataclass_thick_from_dims", &slice_dataclass_thick_from_dims, nb::arg("cls").none(), nb::arg("dims").none());
    module.def("slice_dataclass_thick_copy_with", &slice_dataclass_thick_copy_with, nb::arg("self").none(), nb::arg("point").none(), nb::arg("margin_left").none(), nb::arg("margin_right").none(), nb::arg("ndim").none());
    module.def("slice_dataclass_thick_getitem", &slice_dataclass_thick_getitem, nb::arg("self").none(), nb::arg("key").none(), nb::arg("namespace"));
    module.def("slice_dataclass_thick_iter", &slice_dataclass_thick_iter, nb::arg("self").none(), nb::arg("namespace"));
    module.def("slice_dataclass_input_ndim", &slice_dataclass_input_ndim, nb::arg("self").none(), nb::arg("namespace"));
    module.def("slice_dataclass_input_displayed", &slice_dataclass_input_displayed, nb::arg("self").none(), nb::arg("namespace"));
    module.def("slice_dataclass_input_not_displayed", &slice_dataclass_input_not_displayed, nb::arg("self").none(), nb::arg("namespace"));
    module.def("slice_dataclass_input_with_ndim", &slice_dataclass_input_with_ndim, nb::arg("self").none(), nb::arg("namespace"), nb::arg("ndim_getter").none(), nb::arg("old_ndim_getter").none(), nb::arg("generator_factory").none(), nb::arg("locals_owner"));
    module.def("slice_dataclass_input_is_orthogonal", &slice_dataclass_input_is_orthogonal, nb::arg("self").none(), nb::arg("namespace"), nb::arg("mapped_getter").none(), nb::arg("generator_factory").none(), nb::arg("locals_owner"), nb::arg("threshold").none());
    module.def("slice_dataclass_data_slice", &sd_data_slice, nb::arg("self").none(), nb::arg("world_to_data").none(), nb::arg("namespace"), nb::arg("warning_message").none(), nb::arg("locals_owner"));
    module.def("slice_dataclass_next_request_id", &slice_dataclass_next_request_id, nb::arg("namespace"));
}
} // namespace nx
