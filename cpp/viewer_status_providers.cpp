#include "arrays.hpp"
#include "collection_protocol.hpp"
#include "event_protocol.hpp"
#include <nanobind/stl/string.h>
#include <array>
#include <initializer_list>
#include <utility>
#include <vector>

namespace nx {
namespace {

nb::object sp_checked(PyObject* value) { if (!value) throw nb::python_error(); return nb::steal<nb::object>(value); }
nb::object sp_attr(nb::object value, const char* name) { return sp_checked(PyObject_GetAttrString(value.ptr(), name)); }
nb::object sp_item(nb::object value, nb::object key) { return sp_checked(PyObject_GetItem(value.ptr(), key.ptr())); }
void sp_set(nb::handle value, nb::object key, nb::object result) { if (PyObject_SetItem(value.ptr(), key.ptr(), result.ptr()) < 0) throw nb::python_error(); }
void sp_store(nb::handle value, const char* name, nb::object result) { if (PyObject_SetAttrString(value.ptr(), name, result.ptr()) < 0) throw nb::python_error(); }
nb::object sp_call0(nb::object fn) { return sp_checked(PyObject_CallNoArgs(fn.ptr())); }
nb::object sp_call1(nb::object fn, nb::object a) { return sp_checked(PyObject_CallOneArg(fn.ptr(), a.ptr())); }
nb::object sp_call2(nb::object fn, nb::object a, nb::object b) { return sp_checked(PyObject_CallFunctionObjArgs(fn.ptr(), a.ptr(), b.ptr(), nullptr)); }
nb::object sp_call3(nb::object fn, nb::object a, nb::object b, nb::object c) { return sp_checked(PyObject_CallFunctionObjArgs(fn.ptr(), a.ptr(), b.ptr(), c.ptr(), nullptr)); }
nb::object sp_kw(nb::object fn, nb::tuple args, nb::dict kwargs) { return sp_checked(PyObject_Call(fn.ptr(), args.ptr(), kwargs.ptr())); }
nb::object sp_add(nb::object a, nb::object b) { return sp_checked(PyNumber_Add(a.ptr(), b.ptr())); }
nb::object sp_sub(nb::object a, nb::object b) { return sp_checked(PyNumber_Subtract(a.ptr(), b.ptr())); }
nb::object sp_mul(nb::object a, nb::object b) { return sp_checked(PyNumber_Multiply(a.ptr(), b.ptr())); }
nb::object sp_div(nb::object a, nb::object b) { return sp_checked(PyNumber_TrueDivide(a.ptr(), b.ptr())); }
nb::object sp_neg(nb::object a) { return sp_checked(PyNumber_Negative(a.ptr())); }
nb::object sp_compare(nb::object a, nb::object b, int op) { return sp_checked(PyObject_RichCompare(a.ptr(), b.ptr(), op)); }
bool sp_truth(nb::object value) { int result = PyObject_IsTrue(value.ptr()); if (result < 0) throw nb::python_error(); return result != 0; }
bool sp_test(nb::object a, nb::object b, int op) { return sp_truth(sp_compare(std::move(a), std::move(b), op)); }
bool sp_contains(nb::object a, nb::object b) { int result = PySequence_Contains(a.ptr(), b.ptr()); if (result < 0) throw nb::python_error(); return result != 0; }
nb::object sp_format(nb::object a) { return sp_checked(PyObject_Format(a.ptr(), nullptr)); }
nb::object sp_slice() { return sp_checked(PySlice_New(Py_None, Py_None, Py_None)); }
nb::object sp_slice_to(nb::object a) { return sp_checked(PySlice_New(Py_None, a.ptr(), Py_None)); }
nb::object sp_slice_from(nb::object a) { return sp_checked(PySlice_New(a.ptr(), Py_None, Py_None)); }
nb::object sp_next(nb::handle iterator) { PyObject* value = PyIter_Next(iterator.ptr()); if (!value && PyErr_Occurred()) throw nb::python_error(); return nb::steal<nb::object>(value); }
nb::object sp_optional(const nb::object& value) { return value.is_valid() ? value : nb::none(); }
nb::object sp_cell(nb::object getter) {
    if (!PyFunction_Check(getter.ptr())) { PyErr_SetString(PyExc_TypeError, "cell getter must be a genuine function"); throw nb::python_error(); }
    PyObject* closure = PyFunction_GetClosure(getter.ptr());
    if (!closure || PyTuple_GET_SIZE(closure) != 1 || !PyCell_Check(PyTuple_GET_ITEM(closure, 0))) { PyErr_SetString(PyExc_TypeError, "cell getter must capture one actual cell"); throw nb::python_error(); }
    return nb::borrow<nb::object>(PyTuple_GET_ITEM(closure, 0));
}
nb::object sp_read_cell(const nb::object& cell, const char* name) {
    PyObject* value = PyCell_GET(cell.ptr());
    if (value) return nb::borrow<nb::object>(value);
    nb::object message = sp_checked(PyUnicode_FromFormat("cannot access free variable '%s' where it is not associated with a value in enclosing scope", name));
    nb::object exception = sp_call1(nb::borrow<nb::object>(PyExc_NameError), std::move(message));
    if (PyObject_SetAttrString(exception.ptr(), "name", nb::str(name).ptr()) < 0) throw nb::python_error();
    PyErr_SetObject(PyExc_NameError, exception.ptr());
    throw nb::python_error();
}
struct StatusProviderNamespace {
    nb::dict globals;
    nb::object builtins;
    explicit StatusProviderNamespace(nb::dict ns) : globals(std::move(ns)), builtins(nb::borrow<nb::object>(PyEval_GetBuiltins())) {}
    StatusProviderNamespace(nb::dict ns, nb::object builtins_) : globals(std::move(ns)), builtins(std::move(builtins_)) {}
    nb::object get(const char* name) const {
        nb::object key = nb::str(name); PyObject* result = PyDict_GetItemWithError(globals.ptr(), key.ptr());
        if (result) return nb::borrow<nb::object>(result); if (PyErr_Occurred()) throw nb::python_error();
        result = PyObject_GetItem(builtins.ptr(), key.ptr()); if (result) return nb::steal<nb::object>(result);
        if (!PyErr_ExceptionMatches(PyExc_KeyError)) throw nb::python_error(); PyErr_Clear();
        nb::object message = sp_checked(PyUnicode_FromFormat("name '%s' is not defined", name));
        nb::object exception = sp_call1(nb::borrow<nb::object>(PyExc_NameError), std::move(message));
        if (PyObject_SetAttrString(exception.ptr(), "name", key.ptr()) < 0) throw nb::python_error();
        PyErr_SetObject(PyExc_NameError, exception.ptr()); throw nb::python_error();
    }
    nb::object np(const char* name) const { return sp_attr(get("np"), name); }
    nb::object norm() const { return sp_attr(sp_attr(get("np"), "linalg"), "norm"); }
};
nb::object sp_len(nb::object a, const StatusProviderNamespace& ns) { nb::object fn = ns.get("len"); return sp_call1(std::move(fn), std::move(a)); }
nb::object sp_copy(nb::object a) { nb::object fn = sp_attr(std::move(a), "copy"); return sp_call0(std::move(fn)); }
nb::object sp_axis(nb::object fn, nb::object a, int axis) { nb::dict kwargs; kwargs["axis"] = nb::int_(axis); return sp_kw(std::move(fn), nb::make_tuple(std::move(a)), std::move(kwargs)); }
nb::object sp_sample(nb::object fn, nb::object position, nb::object view_direction, nb::object dims_displayed, nb::object world) {
    nb::tuple args = nb::make_tuple(std::move(position)); nb::dict kwargs;
    kwargs["view_direction"] = std::move(view_direction); kwargs["dims_displayed"] = std::move(dims_displayed); kwargs["world"] = std::move(world);
    return sp_kw(std::move(fn), std::move(args), std::move(kwargs));
}
nb::object sp_raycast(nb::object fn, nb::object start, nb::object end, nb::object dims) {
    nb::dict kwargs; kwargs["start_point"] = std::move(start); kwargs["end_point"] = std::move(end); kwargs["dims_displayed"] = std::move(dims);
    return sp_kw(std::move(fn), nb::tuple(), std::move(kwargs));
}
struct SPPair { nb::object first, second; };
SPPair sp_pair(nb::object value) {
    PyObject* raw_iterator = PyObject_GetIter(value.ptr());
    if (!raw_iterator) {
        nb::python_error error;
        if (Py_TYPE(value.ptr())->tp_iter == nullptr && !PySequence_Check(value.ptr()) && error.matches(PyExc_TypeError)) {
            PyErr_Format(PyExc_TypeError, "cannot unpack non-iterable %.200s object", Py_TYPE(value.ptr())->tp_name);
            throw nb::python_error();
        }
        throw error;
    }
    nb::object iterator = nb::steal<nb::object>(raw_iterator); nb::object first = sp_next(iterator);
    if (!first.is_valid()) { PyErr_SetString(PyExc_ValueError, "not enough values to unpack (expected 2, got 0)"); throw nb::python_error(); }
    nb::object second = sp_next(iterator);
    if (!second.is_valid()) { PyErr_SetString(PyExc_ValueError, "not enough values to unpack (expected 2, got 1)"); throw nb::python_error(); }
    nb::object extra = sp_next(iterator);
    if (extra.is_valid()) { PyErr_SetString(PyExc_ValueError, "too many values to unpack (expected 2)"); throw nb::python_error(); }
    iterator.reset(); value.reset(); return {std::move(first), std::move(second)};
}
template<size_t N> nb::object sp_build_string(std::array<nb::object, N> pieces) {
    nb::tuple tuple = nb::steal<nb::tuple>(PyTuple_New(static_cast<Py_ssize_t>(N)));
    if (!tuple.is_valid()) throw nb::python_error();
    for (size_t i = 0; i < N; ++i) PyTuple_SET_ITEM(tuple.ptr(), static_cast<Py_ssize_t>(i), pieces[i].inc_ref().ptr());
    PyObject* joined = PyUnicode_Join(nb::str("").ptr(), tuple.ptr());
    tuple.reset(); for (size_t i = N; i-- > 0;) pieces[i].reset(); return sp_checked(joined);
}

#define SP_FIELDS(X) \
 X(coord2val) X(coord_str) X(status_str) X(tooltip_text) X(selection) X(active) X(status) X(layer) X(separator) X(emphasis) X(values) X(key) X(status_strs) \
 X(position) X(view_direction) X(dims_displayed) X(ndim_world) X(value) X(start_point) X(end_point) X(coords) X(simplified) X(position_nd) X(position_ndisplay) \
 X(p1) X(p0) X(normalized_vector) X(vector_data_nd) X(vector_data_ndisplay) X(bounding_box) X(view_dir) X(click_pos_data) X(front_face_normal) X(back_face_normal) \
 X(start_point_displayed_dimensions) X(end_point_displayed_dimensions) X(components) X(status_dict) X(precision) X(coords_str) X(value_str) X(coord) X(raw) X(shape) X(offset) \
 X(im_slice) X(slice_shape) X(level0_shape) X(ds) X(sample_ray) X(length_sample_vector) X(n_points) X(sample_points) X(clamped) \
 X(luminance) X(hits) X(attenuated) X(step_size) X(sumval) X(scale) X(non_bg) X(properties) X(label_value) X(idx) X(view_data) X(displayed_position) X(scale_ratio) X(sizes) \
 X(distances) X(in_slice_matches) X(indices) X(plane_point) X(plane_normal) X(projected_points) X(projection_distances) X(rotated_points) X(rotation_matrix) X(rotated_click_point) \
 X(candidate_point_distances) X(closest_index) X(data) X(selected_index) X(box) X(matches) X(selected_vertex_mask) X(vertices) X(index) X(vals) X(shape_in_list) \
 X(_) X(start_position) X(ray_direction) X(intersection) X(intersection_point) X(view_indices) X(slice_key) X(bounding_boxes) X(in_bbox) X(inside_indices) X(z_index) X(pos) \
 X(triangles) X(inside) X(intersection_points) X(start_to_intersection) X(closest_shape_index) X(intersected_triangles) X(mesh_triangles) X(intersection_index) \
 X(triangle_vertex_indices) X(triangle_vertices) X(barycentric_coordinates) X(vertex_values) X(intersection_value) X(val) X(_d) X(pruned)
enum class SPField {
#define SP_ENUM(name) name,
 SP_FIELDS(SP_ENUM)
#undef SP_ENUM
 count
};
struct ViewerStatusProviderLocals {
#define SP_DECLARE(name) nb::object name;
    SP_FIELDS(SP_DECLARE)
#undef SP_DECLARE
    std::vector<SPField> release_order;
    bool inside_indices_is_cell = false;
    ViewerStatusProviderLocals() = default;
    ViewerStatusProviderLocals(ViewerStatusProviderLocals&&) = default;
    ~ViewerStatusProviderLocals() { clear(); }
    void layout(std::initializer_list<SPField> order_) { release_order.assign(order_); }
    void clear() {
        std::array<nb::object, static_cast<size_t>(SPField::count)> detached = {
#define SP_MOVE(name) std::move(name),
            SP_FIELDS(SP_MOVE)
#undef SP_MOVE
        };
        std::vector<SPField> order_ = std::move(release_order); release_order.clear(); inside_indices_is_cell = false;
        for (SPField field : order_) detached[static_cast<size_t>(field)].reset();
        for (nb::object& value_ : detached) value_.reset();
    }
};
int sp_owner_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self)); if (nb::inst_ready(self)) { const auto& o = *nb::inst_ptr<ViewerStatusProviderLocals>(self);
#define SP_VISIT(name) Py_VISIT(o.name.ptr());
        SP_FIELDS(SP_VISIT)
#undef SP_VISIT
    } return 0;
}
int sp_owner_clear(PyObject* self) { if (nb::inst_ready(self)) nb::inst_ptr<ViewerStatusProviderLocals>(self)->clear(); return 0; }
PyType_Slot sp_owner_slots[] = {{Py_tp_traverse, reinterpret_cast<void*>(sp_owner_traverse)}, {Py_tp_clear, reinterpret_cast<void*>(sp_owner_clear)}, {0, nullptr}};
#define PF(name) SPField::name
#define SP_LAYOUT(owner, ...) owner.layout({__VA_ARGS__})

void sp_validate_except(nb::handle klass) {
    if (PyTuple_Check(klass.ptr())) {
        for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(klass.ptr()); ++i) {
            if (!PyExceptionClass_Check(PyTuple_GET_ITEM(klass.ptr(), i))) { PyErr_SetString(PyExc_TypeError, "catching classes that do not inherit from BaseException is not allowed"); throw nb::python_error(); }
        }
    } else if (!PyExceptionClass_Check(klass.ptr())) { PyErr_SetString(PyExc_TypeError, "catching classes that do not inherit from BaseException is not allowed"); throw nb::python_error(); }
}
nb::python_error sp_add_caught_frame(nb::python_error& error) {
    error.restore();
    if (PyTraceBack_Here(PyEval_GetFrame()) < 0) throw nb::python_error();
    return nb::python_error();
}
bool sp_matches(nb::python_error& error, const char* name, const StatusProviderNamespace& ns) {
    nb::object klass = ns.get(name); sp_validate_except(klass); return PyErr_GivenExceptionMatches(error.type().ptr(), klass.ptr()) != 0;
}

enum class StatusGeneratorKind { UnitEquality, CoordinateBounds, ShapeInside };
struct ViewerStatusGeneratorIterator {
    StatusGeneratorKind kind;
    nb::object iterator, current, second, globals, builtins, self_cell, coord_cell, indices_cell;
    bool started = false, running = false, closed = false;
    ViewerStatusGeneratorIterator(StatusGeneratorKind kind_, nb::object source, nb::dict globals_, nb::object builtins_)
        : kind(kind_), iterator(std::move(source)), globals(std::move(globals_)), builtins(std::move(builtins_)) {}
    ViewerStatusGeneratorIterator(ViewerStatusGeneratorIterator&&) = default;
    ~ViewerStatusGeneratorIterator() { clear(); }
    void clear() {
        std::array<nb::object, 8> old = {std::move(iterator), std::move(current), std::move(second), std::move(globals), std::move(builtins), std::move(self_cell), std::move(coord_cell), std::move(indices_cell)};
        closed = true; running = false; for (nb::object& value : old) value.reset();
    }
    nb::object next() {
        if (closed) { PyErr_SetNone(PyExc_StopIteration); throw nb::python_error(); }
        if (running) { PyErr_SetString(PyExc_ValueError, "generator already executing"); throw nb::python_error(); }
        running = true; started = true;
        try {
            StatusProviderNamespace ns(nb::borrow<nb::dict>(globals), builtins);
            for (;;) {
                nb::object next = sp_next(iterator);
                if (!next.is_valid()) { clear(); PyErr_SetNone(PyExc_StopIteration); throw nb::python_error(); }
                if (kind == StatusGeneratorKind::CoordinateBounds) {
                    SPPair pair = sp_pair(std::move(next)); current = std::move(pair.first); second = std::move(pair.second);
                    nb::object result = sp_compare(nb::int_(0), current, Py_LE);
                    if (sp_truth(result)) result = sp_compare(current, second, Py_LT);
                    running = false; return result;
                }
                current = std::move(next);
                if (kind == StatusGeneratorKind::UnitEquality) {
                    nb::object fn = sp_attr(ns.get("pint"), "get_application_registry");
                    nb::object pixel = sp_attr(sp_call0(std::move(fn)), "pixel");
                    nb::object result = sp_compare(current, std::move(pixel), Py_EQ); running = false; return result;
                }
                nb::object any = ns.np("any"); nb::object triangles = ns.get("inside_triangles");
                nb::object visible = sp_attr(sp_read_cell(self_cell, "self"), "_visible_shapes");
                nb::object index = sp_item(sp_read_cell(indices_cell, "inside_indices"), current);
                nb::object shape = sp_item(sp_item(std::move(visible), std::move(index)), nb::int_(1));
                nb::object all_triangles = sp_attr(std::move(shape), "_all_triangles"); nb::object vertices = sp_call0(std::move(all_triangles));
                nb::object relative = sp_sub(std::move(vertices), sp_read_cell(coord_cell, "coord"));
                nb::object result = sp_call1(std::move(triangles), std::move(relative));
                if (sp_truth(sp_call1(std::move(any), std::move(result)))) {
                    nb::object selected = sp_item(sp_read_cell(indices_cell, "inside_indices"), current); running = false; return selected;
                }
            }
        } catch (const nb::python_error& error) {
            running = false; if (closed) throw; closed = true;
            collections::raise_exception(error.value(), true);
        }
    }
    nb::object send(nb::object value) {
        if (closed) { PyErr_SetNone(PyExc_StopIteration); throw nb::python_error(); }
        if (!started && !value.is_none()) { PyErr_SetString(PyExc_TypeError, "can't send non-None value to a just-started generator"); throw nb::python_error(); }
        return next();
    }
    void close() { if (running) { PyErr_SetString(PyExc_ValueError, "generator already executing"); throw nb::python_error(); } if (!closed) clear(); }
    nb::object throw_(nb::args args) { nb::object error = collections::normalize_throw(args); if (running) { PyErr_SetString(PyExc_ValueError, "generator already executing"); throw nb::python_error(); } const bool exhausted = closed; closed = true; started = true; collections::raise_exception(error, !exhausted); }
};
int sp_generator_traverse(PyObject* self, visitproc visit, void* arg) { Py_VISIT(Py_TYPE(self)); if (nb::inst_ready(self)) { const auto& o = *nb::inst_ptr<ViewerStatusGeneratorIterator>(self); Py_VISIT(o.iterator.ptr()); Py_VISIT(o.current.ptr()); Py_VISIT(o.second.ptr()); Py_VISIT(o.globals.ptr()); Py_VISIT(o.builtins.ptr()); Py_VISIT(o.self_cell.ptr()); Py_VISIT(o.coord_cell.ptr()); Py_VISIT(o.indices_cell.ptr()); } return 0; }
int sp_generator_clear(PyObject* self) { if (nb::inst_ready(self)) nb::inst_ptr<ViewerStatusGeneratorIterator>(self)->clear(); return 0; }
PyType_Slot sp_generator_slots[] = {{Py_tp_traverse, reinterpret_cast<void*>(sp_generator_traverse)}, {Py_tp_clear, reinterpret_cast<void*>(sp_generator_clear)}, {0, nullptr}};
nb::object sp_generator(nb::object factory, ViewerStatusGeneratorIterator iterator, const char* qualname) {
    nb::object native = nb::cast(std::move(iterator)); nb::object generator = sp_call1(std::move(factory), std::move(native));
    if (PyObject_SetAttrString(generator.ptr(), "__name__", nb::str("<genexpr>").ptr()) < 0) throw nb::python_error();
    if (PyObject_SetAttrString(generator.ptr(), "__qualname__", nb::str(qualname).ptr()) < 0) throw nb::python_error(); return generator;
}

nb::object viewer_status_calc_cursor(nb::object self, nb::dict namespace_, nb::tuple literals, ViewerStatusProviderLocals& o) {
    SP_LAYOUT(o, PF(coord2val), PF(coord_str), PF(status_str), PF(tooltip_text), PF(selection), PF(active), PF(status), PF(layer), PF(separator), PF(emphasis), PF(values), PF(key), PF(status_strs));
    StatusProviderNamespace ns(std::move(namespace_));
    if (!sp_truth(sp_attr(self, "mouse_over_canvas"))) return nb::none();
    o.coord2val = nb::dict(); o.coord_str = nb::borrow<nb::object>(literals[0]); o.status_str = nb::borrow<nb::object>(literals[0]); o.tooltip_text = nb::borrow<nb::object>(literals[0]);
    o.selection = sp_attr(sp_attr(self, "layers"), "selection"); o.active = sp_attr(o.selection, "active");
    const auto sample = [&](nb::object callback, bool as_array) {
        nb::object position;
        if (as_array) { nb::object asarray = ns.np("asarray"); nb::object cursor = sp_attr(sp_attr(self, "cursor"), "position"); position = sp_call1(std::move(asarray), std::move(cursor)); }
        else position = sp_attr(sp_attr(self, "cursor"), "position");
        nb::object direction = sp_attr(sp_attr(self, "cursor"), "_view_direction");
        nb::object list = ns.get("list"); nb::object displayed = sp_attr(sp_attr(self, "dims"), "displayed"); nb::object dims = sp_call1(std::move(list), std::move(displayed));
        return sp_sample(std::move(callback), std::move(position), std::move(direction), std::move(dims), nb::bool_(true));
    };
    if (sp_truth(sp_attr(sp_attr(self, "tooltip"), "visible")) && !o.active.is_none() && sp_truth(sp_attr(sp_attr(o.active, "_slicing_state"), "_loaded"))) {
        nb::object fn = sp_attr(o.active, "_get_tooltip_text"); o.tooltip_text = sample(std::move(fn), true);
    }
    if (!o.active.is_none() && sp_truth(sp_attr(sp_attr(o.active, "_slicing_state"), "_loaded")) && sp_test(sp_len(o.selection, ns), nb::int_(2), Py_LT)) {
        nb::object fn = sp_attr(o.active, "get_status"); o.status = sample(std::move(fn), false); return nb::make_tuple(o.status, o.tooltip_text);
    }
    nb::object reversed_key = sp_checked(PySlice_New(Py_None, Py_None, nb::int_(-1).ptr()));
    nb::object reversed = sp_item(sp_attr(self, "layers"), std::move(reversed_key)); nb::object iterator = sp_checked(PyObject_GetIter(reversed.ptr())); reversed.reset();
    for (;;) {
        nb::object layer = sp_next(iterator); if (!layer.is_valid()) break; o.layer = std::move(layer);
        if (!sp_truth(sp_attr(o.layer, "visible")) || sp_test(sp_attr(o.layer, "opacity"), nb::int_(0), Py_EQ) || !sp_truth(sp_attr(sp_attr(o.layer, "_slicing_state"), "_loaded")) || (!sp_contains(o.selection, o.layer) && !sp_truth(sp_attr(sp_attr(sp_attr(self, "canvas"), "grid"), "enabled")))) continue;
        nb::object fn = sp_attr(o.layer, "get_status"); o.status = sample(std::move(fn), false);
        o.separator = nb::borrow<nb::object>(literals[1]); o.emphasis = o.layer.ptr() == o.active.ptr() ? o.separator : nb::borrow<nb::object>(literals[0]);
        nb::object coords = sp_format(sp_item(o.status, nb::str("coords"))); o.coord_str = sp_build_string<2>({std::move(coords), nb::borrow<nb::object>(literals[2])});
        if (sp_test(sp_item(o.status, nb::str("value")), nb::borrow<nb::object>(literals[0]), Py_NE)) {
            if (!sp_contains(o.coord2val, o.coord_str)) sp_set(o.coord2val, o.coord_str, nb::list());
            fn = sp_attr(sp_item(o.coord2val, o.coord_str), "append");
            nb::object name = sp_format(sp_attr(o.layer, "name")); nb::object value = sp_format(sp_item(o.status, nb::str("value"))); nb::object emphasis = sp_format(o.emphasis);
            nb::object formatted = sp_build_string<4>({std::move(name), nb::borrow<nb::object>(literals[3]), std::move(value), std::move(emphasis)});
            sp_call1(std::move(fn), std::move(formatted));
        }
    }
    iterator.reset();
    if (sp_truth(o.coord2val)) {
        if (!sp_truth(sp_attr(sp_attr(sp_attr(self, "canvas"), "grid"), "enabled"))) {
            nb::object list = ns.get("list"); nb::object chain = sp_attr(ns.get("itertools"), "chain"); nb::object values = sp_attr(o.coord2val, "values"); nb::object iterable = sp_call0(std::move(values));
            nb::object args = sp_checked(PySequence_Tuple(iterable.ptr())); iterable.reset(); nb::object chained = sp_checked(PyObject_CallObject(chain.ptr(), args.ptr())); args.reset(); chain.reset();
            o.values = sp_call1(std::move(list), std::move(chained)); nb::object next = ns.get("next"); nb::object iter = ns.get("iter"); nb::object current = sp_call1(std::move(iter), o.coord2val); o.key = sp_call1(std::move(next), std::move(current));
            nb::dict collapsed; collapsed[o.key] = o.values; o.coord2val = std::move(collapsed);
        }
        nb::list strings; nb::object comp_key, comp_values; nb::object items = sp_call0(sp_attr(o.coord2val, "items")); iterator = sp_checked(PyObject_GetIter(items.ptr())); items.reset();
        try {
            for (;;) {
                nb::object item = sp_next(iterator); if (!item.is_valid()) break; SPPair pair = sp_pair(std::move(item));
                comp_key = std::move(pair.first); comp_values = std::move(pair.second); nb::object left = comp_key; nb::object join = sp_attr(o.separator, "join"); nb::object joined = sp_call1(std::move(join), comp_values); strings.append(sp_add(std::move(left), std::move(joined)));
            }
        } catch (...) { iterator.reset(); strings.reset(); comp_values.reset(); comp_key.reset(); throw; }
        iterator.reset(); comp_values.reset(); comp_key.reset(); o.status_strs = std::move(strings); nb::object join = sp_attr(o.separator, "join"); o.status_str = sp_call1(std::move(join), o.status_strs);
    } else if (sp_truth(o.coord_str) && !sp_truth(sp_attr(sp_attr(sp_attr(self, "canvas"), "grid"), "enabled"))) o.status_str = sp_add(o.coord_str, nb::borrow<nb::object>(literals[4]));
    else if (sp_truth(sp_attr(sp_attr(sp_attr(self, "canvas"), "grid"), "enabled"))) o.status_str = nb::borrow<nb::object>(literals[4]);
    else o.status_str = nb::borrow<nb::object>(literals[5]);
    return nb::make_tuple(o.status_str, o.tooltip_text);
}
void viewer_status_update_cursor(nb::object self, ViewerStatusProviderLocals& o) {
    SP_LAYOUT(o, PF(status), PF(active)); o.status = sp_call0(sp_attr(self, "_calc_status_from_cursor"));
    if (!o.status.is_none()) { SPPair pair = sp_pair(o.status); sp_store(self, "status", std::move(pair.first)); nb::object tooltip = sp_attr(self, "tooltip"); sp_store(tooltip, "text", std::move(pair.second)); }
    o.active = sp_attr(sp_attr(sp_attr(self, "layers"), "selection"), "active"); if (!o.active.is_none()) sp_store(self, "help", sp_attr(o.active, "help"));
}

nb::object viewer_layer_get_value(nb::object self, nb::object world, nb::dict namespace_, nb::object pan_zoom, ViewerStatusProviderLocals& o) {
    SP_LAYOUT(o, PF(position), PF(view_direction), PF(dims_displayed), PF(ndim_world), PF(value), PF(start_point), PF(end_point)); StatusProviderNamespace ns(std::move(namespace_));
    nb::object fn = ns.np("asarray"); o.position = sp_call1(std::move(fn), o.position);
    if (sp_truth(sp_attr(self, "visible"))) {
        if (sp_truth(world)) {
            o.ndim_world = sp_len(o.position, ns);
            if (!o.dims_displayed.is_none()) {
                fn = ns.get("dims_displayed_world_to_layer"); nb::dict kwargs; kwargs["ndim_world"] = o.ndim_world; kwargs["ndim_layer"] = sp_attr(self, "ndim"); o.dims_displayed = sp_kw(std::move(fn), nb::make_tuple(o.dims_displayed), std::move(kwargs));
            }
            fn = sp_attr(self, "world_to_data"); o.position = sp_call1(std::move(fn), o.position);
        }
        if (!o.dims_displayed.is_none() && !o.view_direction.is_none()) {
            if (sp_test(sp_len(o.dims_displayed, ns), nb::int_(2), Py_EQ) || sp_test(sp_attr(self, "ndim"), nb::int_(2), Py_EQ)) {
                fn = sp_attr(self, "_get_value"); nb::object tuple = ns.get("tuple"); nb::dict kwargs; kwargs["position"] = sp_call1(std::move(tuple), o.position); o.value = sp_kw(std::move(fn), nb::tuple(), std::move(kwargs));
            } else {
                fn = sp_attr(self, "_world_to_data_ray"); o.view_direction = sp_call1(std::move(fn), o.view_direction);
                fn = sp_attr(self, "get_ray_intersections"); nb::dict kwargs; kwargs["position"] = o.position; kwargs["view_direction"] = o.view_direction; kwargs["dims_displayed"] = o.dims_displayed; kwargs["world"] = nb::bool_(false);
                SPPair pair = sp_pair(sp_kw(std::move(fn), nb::tuple(), std::move(kwargs))); o.start_point = std::move(pair.first); o.end_point = std::move(pair.second);
                fn = sp_attr(self, "_get_value_3d"); o.value = sp_raycast(std::move(fn), o.start_point, o.end_point, o.dims_displayed);
            }
        } else { fn = sp_attr(self, "_get_value"); o.value = sp_call1(std::move(fn), o.position); }
    } else o.value = nb::none();
    if (sp_test(sp_attr(self, "mode"), std::move(pan_zoom), Py_NE)) sp_store(self, "_value", o.value); return o.value;
}
nb::object viewer_layer_value_3d_none(nb::object self, nb::object start_point, nb::object end_point, nb::object dims_displayed) { return nb::none(); }
nb::object viewer_layer_world_to_data(nb::object self, nb::dict namespace_, ViewerStatusProviderLocals& o) {
    SP_LAYOUT(o, PF(position), PF(coords), PF(simplified)); StatusProviderNamespace ns(std::move(namespace_)); nb::object fn = ns.np("asarray"); o.position = sp_call1(std::move(fn), o.position);
    nb::object n = sp_len(o.position, ns); nb::object ndim = sp_attr(self, "ndim");
    if (sp_test(std::move(n), std::move(ndim), Py_GE)) {
        fn = ns.get("list"); nb::object negative = sp_neg(sp_attr(self, "ndim")); nb::object selected = sp_item(o.position, sp_slice_from(std::move(negative))); o.coords = sp_call1(std::move(fn), std::move(selected));
    } else {
        nb::list zero; zero.append(nb::int_(0)); nb::object ndim = sp_attr(self, "ndim"); nb::object n = sp_len(o.position, ns); nb::object missing = sp_sub(std::move(ndim), std::move(n)); nb::object prefix = sp_mul(std::move(zero), std::move(missing));
        fn = ns.get("list"); nb::object coordinates = sp_call1(std::move(fn), o.position); o.coords = sp_add(std::move(prefix), std::move(coordinates));
    }
    nb::object transforms = sp_attr(self, "_transforms"); o.simplified = sp_attr(sp_item(std::move(transforms), sp_slice_from(nb::int_(1))), "simplified");
    fn = sp_attr(o.simplified, "inverse"); return sp_call1(std::move(fn), o.coords);
}
nb::object viewer_layer_world_displayed(nb::object self, nb::object position, nb::object dims_displayed, ViewerStatusProviderLocals& o) {
    SP_LAYOUT(o, PF(position_nd), PF(position_ndisplay)); o.position_nd = sp_call1(sp_attr(std::move(self), "world_to_data"), std::move(position)); o.position_ndisplay = sp_item(o.position_nd, std::move(dims_displayed)); return o.position_ndisplay;
}
nb::object viewer_layer_world_ray(nb::object self, nb::object vector, nb::dict namespace_, ViewerStatusProviderLocals& o) {
    SP_LAYOUT(o, PF(p1), PF(p0), PF(normalized_vector)); StatusProviderNamespace ns(std::move(namespace_));
    nb::object array = ns.np("asarray"); nb::object fn = sp_attr(self, "world_to_data"); nb::object transformed = sp_call1(std::move(fn), vector); o.p1 = sp_call1(std::move(array), std::move(transformed));
    array = ns.np("asarray"); fn = sp_attr(std::move(self), "world_to_data"); nb::object zero = ns.np("zeros_like"); nb::object zeros = sp_call1(std::move(zero), std::move(vector)); transformed = sp_call1(std::move(fn), std::move(zeros)); o.p0 = sp_call1(std::move(array), std::move(transformed));
    nb::object numerator = sp_sub(o.p1, o.p0); fn = ns.norm(); nb::object delta = sp_sub(o.p1, o.p0); nb::object denominator = sp_call1(std::move(fn), std::move(delta)); o.normalized_vector = sp_div(std::move(numerator), std::move(denominator)); return o.normalized_vector;
}
nb::object viewer_layer_world_displayed_ray(nb::object self, nb::object vector_world, nb::object dims_displayed, nb::dict namespace_, ViewerStatusProviderLocals& o) {
    SP_LAYOUT(o, PF(vector_data_nd), PF(vector_data_ndisplay)); StatusProviderNamespace ns(std::move(namespace_));
    nb::object fn = sp_attr(std::move(self), "_world_to_data_ray"); o.vector_data_nd = sp_call1(std::move(fn), std::move(vector_world)); o.vector_data_ndisplay = sp_item(o.vector_data_nd, std::move(dims_displayed));
    fn = ns.norm(); nb::object norm = sp_call1(std::move(fn), o.vector_data_ndisplay); o.vector_data_ndisplay = sp_checked(PyNumber_InPlaceTrueDivide(o.vector_data_ndisplay.ptr(), norm.ptr())); return o.vector_data_ndisplay;
}
nb::object viewer_layer_display_bbox(nb::object self, nb::object dims_displayed) { nb::object extent = sp_attr(std::move(self), "_extent_data"); nb::object selected = sp_item(std::move(extent), nb::make_tuple(sp_slice(), std::move(dims_displayed))); return sp_attr(std::move(selected), "T"); }
nb::object viewer_layer_ray_intersections(nb::object self, nb::object dims_displayed, nb::object world, nb::dict namespace_, ViewerStatusProviderLocals& o) {
    SP_LAYOUT(o, PF(position), PF(view_direction), PF(bounding_box), PF(start_point), PF(end_point)); StatusProviderNamespace ns(std::move(namespace_));
    nb::object fn = ns.np("asarray"); o.position = sp_call1(std::move(fn), o.position); fn = ns.np("asarray"); o.view_direction = sp_call1(std::move(fn), o.view_direction);
    if (sp_test(sp_len(dims_displayed, ns), nb::int_(3), Py_NE)) return nb::make_tuple(nb::none(), nb::none());
    fn = sp_attr(self, "_display_bounding_box"); o.bounding_box = sp_call1(std::move(fn), dims_displayed);
    nb::object key = nb::make_tuple(sp_slice(), nb::int_(1)); nb::object column = sp_item(o.bounding_box, key); nb::object incremented = sp_checked(PyNumber_InPlaceAdd(column.ptr(), nb::int_(1).ptr())); column.reset(); sp_set(o.bounding_box, std::move(key), std::move(incremented));
    fn = sp_attr(std::move(self), "_get_ray_intersections"); nb::dict kwargs; kwargs["position"] = o.position; kwargs["view_direction"] = o.view_direction; kwargs["dims_displayed"] = std::move(dims_displayed); kwargs["world"] = std::move(world); kwargs["bounding_box"] = o.bounding_box;
    SPPair pair = sp_pair(sp_kw(std::move(fn), nb::tuple(), std::move(kwargs))); o.start_point = std::move(pair.first); o.end_point = std::move(pair.second); return nb::make_tuple(o.start_point, o.end_point);
}
nb::object viewer_layer_offset_position(nb::object self, nb::object position, nb::dict namespace_) { StatusProviderNamespace ns(std::move(namespace_)); nb::object fn = ns.np("asarray"); return sp_call1(std::move(fn), std::move(position)); }
nb::object viewer_layer_ray_intersections_internal(nb::object self, nb::object view_direction, nb::object dims_displayed, nb::object bounding_box, nb::object world,
                                                 nb::dict namespace_, ViewerStatusProviderLocals& o) {
    SP_LAYOUT(o, PF(position), PF(view_dir), PF(click_pos_data), PF(front_face_normal), PF(back_face_normal), PF(start_point_displayed_dimensions), PF(end_point_displayed_dimensions), PF(start_point), PF(end_point)); StatusProviderNamespace ns(std::move(namespace_));
    if (world.ptr() == Py_True) { nb::object fn = sp_attr(self, "_world_to_displayed_data_ray"); o.view_dir = sp_call2(std::move(fn), std::move(view_direction), dims_displayed); fn = sp_attr(self, "_world_to_displayed_data"); o.click_pos_data = sp_call2(std::move(fn), o.position, dims_displayed); }
    else { nb::object fn = sp_attr(std::move(self), "_get_offset_data_position"); o.position = sp_call1(std::move(fn), o.position); o.view_dir = sp_item(std::move(view_direction), dims_displayed); o.click_pos_data = sp_item(o.position, dims_displayed); }
    nb::object fn = ns.get("find_front_back_face"); SPPair pair = sp_pair(sp_call3(std::move(fn), o.click_pos_data, bounding_box, o.view_dir)); o.front_face_normal = std::move(pair.first); o.back_face_normal = std::move(pair.second);
    if (o.front_face_normal.is_none() || o.back_face_normal.is_none()) return nb::make_tuple(nb::none(), nb::none());
    const auto intersection = [&](const nb::object& normal) { nb::object callback = ns.get("intersect_line_with_axis_aligned_bounding_box_3d"); nb::tuple args = nb::make_tuple(o.click_pos_data, o.view_dir, bounding_box, normal); return sp_checked(PyObject_CallObject(callback.ptr(), args.ptr())); };
    o.start_point_displayed_dimensions = intersection(o.front_face_normal); o.end_point_displayed_dimensions = intersection(o.back_face_normal);
    o.start_point = sp_copy(o.position); sp_set(o.start_point, dims_displayed, o.start_point_displayed_dimensions);
    o.end_point = sp_copy(o.position); sp_set(o.end_point, std::move(dims_displayed), o.end_point_displayed_dimensions); return nb::make_tuple(o.start_point, o.end_point);
}

nb::object viewer_layer_source_info(nb::object self, nb::dict namespace_, nb::tuple literals, ViewerStatusProviderLocals& o) {
    SP_LAYOUT(o, PF(components)); StatusProviderNamespace ns(std::move(namespace_)); o.components = nb::dict();
    const auto metadata = [&](size_t type) { sp_set(o.components, nb::str("layer_name"), sp_attr(self, "name")); sp_set(o.components, nb::str("layer_base"), sp_attr(self, "name")); sp_set(o.components, nb::str("source_type"), nb::borrow<nb::object>(literals[type])); };
    if (sp_truth(sp_attr(sp_attr(self, "source"), "reader_plugin"))) {
        sp_set(o.components, nb::str("layer_name"), sp_attr(self, "name")); nb::object basename = sp_attr(sp_attr(ns.get("os"), "path"), "basename"); nb::object path = sp_attr(sp_attr(self, "source"), "path"); if (!sp_truth(path)) path = nb::borrow<nb::object>(literals[3]);
        sp_set(o.components, nb::str("layer_base"), sp_call1(std::move(basename), std::move(path))); sp_set(o.components, nb::str("source_type"), nb::borrow<nb::object>(literals[0]));
        try { nb::object fn = sp_attr(ns.get("pm"), "get_manifest"); nb::object plugin = sp_attr(sp_attr(self, "source"), "reader_plugin"); nb::object manifest = sp_call1(std::move(fn), std::move(plugin)); sp_set(o.components, nb::str("plugin"), sp_attr(std::move(manifest), "display_name")); }
        catch (nb::python_error& error) { nb::python_error traced = sp_add_caught_frame(error); { events::HandledException handled(traced.value()); if (!sp_matches(traced, "KeyError", ns)) throw std::move(traced); sp_set(o.components, nb::str("plugin"), sp_attr(sp_attr(self, "source"), "reader_plugin")); } }
        return o.components;
    }
    if (sp_truth(sp_attr(sp_attr(self, "source"), "sample"))) {
        metadata(1);
        try { nb::object fn = sp_attr(ns.get("pm"), "get_manifest"); nb::object sample = sp_attr(sp_attr(self, "source"), "sample"); nb::object plugin = sp_item(std::move(sample), nb::int_(0)); nb::object manifest = sp_call1(std::move(fn), std::move(plugin)); sp_set(o.components, nb::str("plugin"), sp_attr(std::move(manifest), "display_name")); }
        catch (nb::python_error& error) { nb::python_error traced = sp_add_caught_frame(error); { events::HandledException handled(traced.value()); if (!sp_matches(traced, "KeyError", ns)) throw std::move(traced); sp_set(o.components, nb::str("plugin"), sp_item(sp_attr(sp_attr(self, "source"), "sample"), nb::int_(0))); } }
        return o.components;
    }
    if (sp_truth(sp_attr(sp_attr(self, "source"), "widget"))) { metadata(2); nb::object widget = sp_attr(sp_attr(self, "source"), "widget"); sp_set(o.components, nb::str("plugin"), sp_attr(sp_attr(std::move(widget), "_function"), "__name__")); return o.components; }
    metadata(3); sp_set(o.components, nb::str("plugin"), nb::borrow<nb::object>(literals[3])); return o.components;
}

nb::object viewer_layer_integer_coords(nb::object self, nb::dict namespace_, nb::object generator_factory) {
    StatusProviderNamespace ns(namespace_); nb::object bool_ = ns.get("bool"); nb::object all = ns.np("all"); nb::object array = ns.np("asarray"); nb::object scale = sp_attr(self, "scale");
    nb::object actual = sp_call1(std::move(array), std::move(scale)); nb::object result = sp_call1(std::move(all), sp_compare(std::move(actual), nb::int_(1), Py_EQ));
    if (sp_truth(result)) { all = ns.np("all"); array = ns.np("asarray"); nb::object translate = sp_attr(self, "translate"); actual = sp_call1(std::move(array), std::move(translate)); result = sp_call1(std::move(all), sp_compare(std::move(actual), nb::int_(0), Py_EQ));
        if (sp_truth(result)) { all = ns.get("all"); nb::object units = sp_attr(std::move(self), "units"); nb::object iterator = sp_checked(PyObject_GetIter(units.ptr())); units.reset(); nb::object generator = sp_generator(std::move(generator_factory), ViewerStatusGeneratorIterator(StatusGeneratorKind::UnitEquality, std::move(iterator), namespace_, ns.builtins), "Layer._use_integer_coords_in_status.<locals>.<genexpr>"); result = sp_call1(std::move(all), std::move(generator)); } }
    return sp_call1(std::move(bool_), std::move(result));
}

nb::object viewer_layer_status(nb::object self, nb::object view_direction, nb::object dims_displayed, nb::object world,
                               nb::dict namespace_, nb::tuple literals, ViewerStatusProviderLocals& o) {
    SP_LAYOUT(o, PF(position), PF(value), PF(status_dict), PF(precision), PF(coords_str), PF(value_str)); StatusProviderNamespace ns(std::move(namespace_));
    o.status_dict = sp_copy(sp_call0(sp_attr(self, "_get_source_info")));
    if (!o.position.is_none()) {
        nb::object int_ = ns.get("int"); nb::object max = ns.get("max"); nb::object npmax = ns.np("max"); nb::object ceil = ns.np("ceil"); nb::object log10 = ns.np("log10"); nb::object abs = ns.np("abs"); nb::object scale = sp_attr(self, "scale");
        nb::object absolute = sp_call1(std::move(abs), std::move(scale)); nb::object logarithm = sp_call1(std::move(log10), std::move(absolute)); nb::object negative = sp_neg(std::move(logarithm)); nb::object rounded = sp_call1(std::move(ceil), std::move(negative)); nb::object npmaximum = sp_call1(std::move(npmax), std::move(rounded)); nb::object maximum = sp_call2(std::move(max), std::move(npmaximum), nb::int_(-2)); nb::object shifted = sp_add(std::move(maximum), nb::int_(2)); o.precision = sp_call1(std::move(int_), std::move(shifted));
        nb::object asarray = ns.np("asarray"); o.position = sp_call1(std::move(asarray), o.position);
        nb::object fn = sp_attr(self, "get_value"); o.value = sp_sample(std::move(fn), o.position, std::move(view_direction), std::move(dims_displayed), std::move(world));
        if (sp_truth(sp_call0(sp_attr(self, "_use_integer_coords_in_status")))) { nb::object round = ns.np("round"); nb::object rounded = sp_call1(std::move(round), o.position); fn = sp_attr(std::move(rounded), "astype"); nb::object int_ = ns.get("int"); o.position = sp_call1(std::move(fn), std::move(int_)); }
        fn = ns.get("generate_layer_status_strings"); nb::object target = o.position; nb::object negative_ndim = sp_neg(sp_attr(std::move(self), "ndim")); nb::object position = sp_item(std::move(target), sp_slice_from(std::move(negative_ndim))); nb::dict kwargs; kwargs["precision"] = o.precision;
        SPPair pair = sp_pair(sp_kw(std::move(fn), nb::make_tuple(std::move(position), o.value), std::move(kwargs))); o.coords_str = std::move(pair.first); o.value_str = std::move(pair.second);
    } else { o.coords_str = nb::borrow<nb::object>(literals[0]); o.value_str = nb::borrow<nb::object>(literals[0]); }
    nb::object join = sp_attr(nb::borrow<nb::object>(literals[1]), "join"); nb::object text = sp_call1(std::move(join), nb::make_tuple(o.coords_str, o.value_str)); sp_set(o.status_dict, nb::str("coordinates"), std::move(text)); sp_set(o.status_dict, nb::str("coords"), o.coords_str); sp_set(o.status_dict, nb::str("value"), o.value_str); return o.status_dict;
}
nb::object viewer_layer_tooltip_empty(nb::object self, nb::object position, nb::object view_direction, nb::object dims_displayed, nb::object world, nb::object empty) { return empty; }

nb::object viewer_scalar_get_value(nb::object self, nb::object position, nb::dict namespace_, nb::object generator_factory, ViewerStatusProviderLocals& o) {
    SP_LAYOUT(o, PF(coord), PF(raw), PF(shape), PF(offset), PF(value)); StatusProviderNamespace ns(namespace_);
    if (sp_truth(sp_attr(self, "multiscale"))) { nb::object transform = sp_item(sp_attr(self, "_transforms"), nb::str("tile2data")); nb::object fn = sp_attr(std::move(transform), "inverse"); o.coord = sp_call1(std::move(fn), std::move(position)); }
    else o.coord = std::move(position);
    nb::object round = ns.np("round"); nb::object rounded = sp_call1(std::move(round), o.coord); nb::object astype = sp_attr(std::move(rounded), "astype"); nb::object int_ = ns.get("int"); o.coord = sp_call1(std::move(astype), std::move(int_));
    o.raw = sp_attr(sp_attr(sp_attr(self, "_slice"), "image"), "raw");
    nb::object ndim = sp_attr(self, "ndim"); nb::object len = ns.get("len"); nb::object data_shape = sp_attr(sp_attr(self, "_data"), "shape"); nb::object data_ndim = sp_call1(std::move(len), std::move(data_shape));
    if (sp_test(std::move(ndim), std::move(data_ndim), Py_NE)) o.shape = sp_item(sp_attr(o.raw, "shape"), sp_slice_to(nb::int_(-1))); else o.shape = sp_attr(o.raw, "shape");
    ndim = sp_attr(self, "ndim"); nb::object coord_ndim = sp_len(o.coord, ns);
    if (sp_test(std::move(ndim), std::move(coord_ndim), Py_LT)) {
        nb::object coord_ndim = sp_len(o.coord, ns); nb::object shape_ndim = sp_len(o.shape, ns); o.offset = sp_sub(std::move(coord_ndim), std::move(shape_ndim));
        nb::object target = o.coord; nb::object displayed = sp_attr(sp_attr(self, "_slice_input"), "displayed"); nb::object iterator = sp_checked(PyObject_GetIter(displayed.ptr())); displayed.reset(); nb::list indices; nb::object d;
        try { for (;;) { nb::object next = sp_next(iterator); if (!next.is_valid()) break; d = std::move(next); indices.append(sp_add(d, o.offset)); } }
        catch (...) { iterator.reset(); indices.reset(); d.reset(); throw; }
        iterator.reset(); d.reset(); o.coord = sp_item(std::move(target), std::move(indices));
    } else o.coord = sp_item(o.coord, sp_attr(sp_attr(self, "_slice_input"), "displayed"));
    nb::object all = ns.get("all"); nb::object zip = ns.get("zip"); nb::dict kwargs; kwargs["strict"] = nb::bool_(false);
    nb::object zipped = sp_kw(std::move(zip), nb::make_tuple(o.coord, o.shape), std::move(kwargs)); nb::object iterator = sp_checked(PyObject_GetIter(zipped.ptr())); zipped.reset();
    nb::object generator = sp_generator(std::move(generator_factory), ViewerStatusGeneratorIterator(StatusGeneratorKind::CoordinateBounds, std::move(iterator), namespace_, ns.builtins), "ScalarFieldBase._get_value.<locals>.<genexpr>");
    if (sp_truth(sp_call1(std::move(all), std::move(generator)))) { nb::object target = o.raw; nb::object tuple = ns.get("tuple"); nb::object key = sp_call1(std::move(tuple), o.coord); o.value = sp_item(std::move(target), std::move(key)); }
    else o.value = nb::none();
    if (sp_truth(sp_attr(self, "multiscale"))) { nb::object level = sp_attr(std::move(self), "data_level"); o.value = nb::make_tuple(std::move(level), o.value); }
    return o.value;
}
nb::object viewer_scalar_value_ray(nb::object self, nb::object dims_displayed, nb::dict namespace_, ViewerStatusProviderLocals& o) {
    SP_LAYOUT(o, PF(start_point), PF(end_point), PF(im_slice), PF(slice_shape), PF(level0_shape), PF(ds), PF(sample_ray), PF(length_sample_vector), PF(n_points), PF(sample_points), PF(bounding_box), PF(clamped), PF(values)); StatusProviderNamespace ns(std::move(namespace_));
    if (o.start_point.is_none() || o.end_point.is_none()) return nb::none();
    if (!sp_test(sp_len(dims_displayed, ns), nb::int_(3), Py_EQ)) return nb::none();
    o.im_slice = sp_attr(sp_attr(sp_attr(self, "_slice"), "image"), "raw");
    nb::object fn = ns.np("array"); nb::object shape = sp_attr(o.im_slice, "shape"); nb::object array = sp_call1(std::move(fn), std::move(shape)); nb::object n = sp_len(dims_displayed, ns); o.slice_shape = sp_item(std::move(array), sp_slice_to(std::move(n)));
    fn = ns.np("array"); nb::object level = sp_item(sp_attr(self, "level_shapes"), nb::int_(0)); o.level0_shape = sp_call1(std::move(fn), std::move(level));
    nb::object level_shape = sp_item(o.level0_shape, dims_displayed); o.ds = sp_div(std::move(level_shape), o.slice_shape);
    nb::object selected = sp_item(o.start_point, dims_displayed); o.start_point = sp_div(std::move(selected), o.ds); selected = sp_item(o.end_point, dims_displayed); o.end_point = sp_div(std::move(selected), o.ds);
    fn = ns.get("cast"); nb::object type = ns.np("ndarray"); o.start_point = sp_call2(std::move(fn), std::move(type), o.start_point);
    fn = ns.get("cast"); type = ns.np("ndarray"); o.end_point = sp_call2(std::move(fn), std::move(type), o.end_point);
    o.sample_ray = sp_sub(o.end_point, o.start_point); fn = ns.norm(); o.length_sample_vector = sp_call1(std::move(fn), o.sample_ray);
    fn = ns.get("int"); nb::object count = sp_mul(nb::int_(2), o.length_sample_vector); o.n_points = sp_call1(std::move(fn), std::move(count));
    fn = ns.np("linspace"); nb::dict kwargs; kwargs["endpoint"] = nb::bool_(true); o.sample_points = sp_kw(std::move(fn), nb::make_tuple(o.start_point, o.end_point, o.n_points), std::move(kwargs));
    fn = ns.np("zeros"); n = sp_len(std::move(dims_displayed), ns); o.bounding_box = sp_call1(std::move(fn), nb::make_tuple(std::move(n), nb::int_(2)));
    sp_set(o.bounding_box, nb::make_tuple(sp_slice(), nb::int_(1)), o.slice_shape);
    fn = ns.get("clamp_point_to_bounding_box"); nb::object clamped = sp_call2(std::move(fn), o.sample_points, o.bounding_box); fn = sp_attr(std::move(clamped), "astype"); nb::object int_ = ns.get("int"); o.clamped = sp_call1(std::move(fn), std::move(int_));
    nb::object target = o.im_slice; nb::object tuple = ns.get("tuple"); nb::object transposed = sp_attr(o.clamped, "T"); nb::object key = sp_call1(std::move(tuple), std::move(transposed)); o.values = sp_item(std::move(target), std::move(key));
    fn = sp_attr(std::move(self), "_calculate_value_from_ray"); return sp_call1(std::move(fn), o.values);
}
nb::object viewer_scalar_value_3d(nb::object self, nb::object start_point, nb::object end_point, nb::object dims_displayed, ViewerStatusProviderLocals& o) {
    SP_LAYOUT(o, PF(value)); nb::object fn = sp_attr(self, "_get_value_ray"); o.value = sp_raycast(std::move(fn), std::move(start_point), std::move(end_point), std::move(dims_displayed));
    if (sp_truth(sp_attr(self, "multiscale")) && !o.value.is_none()) return nb::make_tuple(sp_attr(std::move(self), "data_level"), o.value); return o.value;
}
nb::object viewer_scalar_offset(nb::object self, nb::object position, nb::object half) { return sp_add(std::move(position), std::move(half)); }
[[noreturn]] void sp_raise(nb::object exception) {
    if (PyExceptionClass_Check(exception.ptr())) {
        nb::object klass = std::move(exception); exception = sp_call0(klass);
        if (!PyExceptionInstance_Check(exception.ptr())) { PyErr_Format(PyExc_TypeError, "calling %R should have returned an instance of BaseException, not %R", klass.ptr(), reinterpret_cast<PyObject*>(Py_TYPE(exception.ptr()))); throw nb::python_error(); }
    } else if (!PyExceptionInstance_Check(exception.ptr())) { PyErr_SetString(PyExc_TypeError, "exceptions must derive from BaseException"); throw nb::python_error(); }
    PyErr_SetObject(reinterpret_cast<PyObject*>(Py_TYPE(exception.ptr())), exception.ptr()); throw nb::python_error();
}
nb::object viewer_image_value_ray(nb::object self, nb::object values, nb::dict namespace_, nb::tuple literals, ViewerStatusProviderLocals& o) {
    SP_LAYOUT(o, PF(luminance), PF(hits), PF(attenuated), PF(step_size), PF(sumval), PF(scale)); StatusProviderNamespace ns(std::move(namespace_));
    if (!sp_truth(sp_attr(values, "size"))) return nb::none();
    const auto rendering = [&](const char* name) { nb::object actual = sp_attr(self, "rendering"); nb::object expected = sp_attr(ns.get("ImageRendering"), name); return sp_test(std::move(actual), std::move(expected), Py_EQ); };
    if (rendering("TRANSLUCENT")) return sp_item(std::move(values), nb::int_(0));
    nb::object all = ns.np("all"); nb::object isnan = ns.np("isnan"); nb::object nan = sp_call1(std::move(isnan), values); if (sp_truth(sp_call1(std::move(all), std::move(nan)))) return nb::none();
    if (sp_truth(sp_attr(self, "rgb"))) { nb::object fn = ns.get("rgb_to_luminance"); o.luminance = sp_call1(std::move(fn), values); } else o.luminance = values;
    if (rendering("ISO")) { nb::object threshold = sp_attr(self, "iso_threshold"); o.hits = sp_compare(o.luminance, std::move(threshold), Py_GE); nb::object any = ns.np("any"); if (!sp_truth(sp_call1(std::move(any), o.hits))) return nb::none(); nb::object target = values; nb::object max = ns.np("nanargmax"); nb::object index = sp_call1(std::move(max), o.hits); return sp_item(std::move(target), std::move(index)); }
    if (rendering("AVERAGE")) { nb::object fn = ns.np("nanmean"); return sp_axis(std::move(fn), std::move(values), 0); }
    if (rendering("ADDITIVE")) { nb::object fn = ns.np("nansum"); return sp_axis(std::move(fn), std::move(values), 0); }
    if (rendering("MIP")) { nb::object target = values; nb::object fn = ns.np("nanargmax"); nb::object index = sp_call1(std::move(fn), o.luminance); return sp_item(std::move(target), std::move(index)); }
    if (rendering("MINIP")) { nb::object target = values; nb::object fn = ns.np("nanargmin"); nb::object index = sp_call1(std::move(fn), o.luminance); return sp_item(std::move(target), std::move(index)); }
    if (rendering("ATTENUATED_MIP")) {
        nb::object low = sp_item(sp_attr(self, "contrast_limits"), nb::int_(0)); nb::object numerator = sp_sub(o.luminance, std::move(low)); nb::object high = sp_item(sp_attr(self, "contrast_limits"), nb::int_(1)); o.attenuated = sp_div(std::move(numerator), std::move(high));
        o.step_size = nb::borrow<nb::object>(literals[0]); nb::object first = o.step_size; nb::object cumsum = ns.np("cumsum"); nb::object clip = ns.np("clip"); nb::object clipped = sp_call3(std::move(clip), o.attenuated, nb::int_(0), nb::int_(1)); nb::object accumulated = sp_call1(std::move(cumsum), std::move(clipped)); nb::object scaled = sp_mul(std::move(first), std::move(accumulated)); nb::object n = sp_len(o.attenuated, ns); o.sumval = sp_mul(std::move(scaled), std::move(n));
        nb::object exp = ns.np("exp"); nb::object negative = sp_neg(sp_attr(self, "attenuation")); nb::object shifted = sp_sub(o.sumval, nb::int_(1)); nb::object exponent = sp_mul(std::move(negative), std::move(shifted)); o.scale = sp_call1(std::move(exp), std::move(exponent));
        nb::object target = values; nb::object fn = ns.np("nanargmin"); nb::object product = sp_mul(o.attenuated, o.scale); nb::object index = sp_call1(std::move(fn), std::move(product)); return sp_item(std::move(target), std::move(index));
    }
    nb::object error = ns.get("RuntimeError"); nb::object message = sp_build_string<2>({nb::borrow<nb::object>(literals[1]), sp_format(sp_attr(std::move(self), "rendering"))}); sp_raise(sp_call1(std::move(error), std::move(message)));
}
nb::object viewer_labels_value_ray(nb::object self, nb::object values, nb::dict namespace_, ViewerStatusProviderLocals& o) {
    SP_LAYOUT(o, PF(non_bg)); StatusProviderNamespace ns(std::move(namespace_)); nb::object background = sp_attr(sp_attr(std::move(self), "colormap"), "background_value"); o.non_bg = sp_compare(values, std::move(background), Py_NE);
    nb::object any = ns.np("any"); if (!sp_truth(sp_call1(std::move(any), o.non_bg))) return nb::none(); nb::object target = values; nb::object argmax = ns.np("argmax"); nb::object ravel = ns.np("ravel"); nb::object flat = sp_call1(std::move(ravel), o.non_bg); nb::object index = sp_call1(std::move(argmax), std::move(flat)); return sp_item(std::move(target), std::move(index));
}
nb::object sp_feature_strings(nb::object self, nb::object index, const StatusProviderNamespace& ns, nb::tuple literals) {
    nb::object items = sp_call0(sp_attr(sp_attr(std::move(self), "features"), "items")); nb::object iterator = sp_checked(PyObject_GetIter(items.ptr())); items.reset(); nb::list result; nb::object k, v;
    try {
        for (;;) {
            nb::object item = sp_next(iterator); if (!item.is_valid()) break; SPPair pair = sp_pair(std::move(item)); k = std::move(pair.first); v = std::move(pair.second);
            if (!sp_test(k, nb::borrow<nb::object>(literals[0]), Py_NE)) continue;
            if (!sp_test(sp_len(v, ns), index, Py_GT)) continue;
            if (sp_item(v, index).is_none()) continue;
            nb::object isinstance = ns.get("isinstance"); nb::object value = sp_item(v, index); nb::object float_ = ns.get("float");
            if (sp_truth(sp_call2(std::move(isinstance), std::move(value), std::move(float_)))) { nb::object isnan = ns.np("isnan"); value = sp_item(v, index); if (sp_truth(sp_call1(std::move(isnan), std::move(value)))) continue; }
            nb::object key = sp_format(k); nb::object formatter = ns.get("format_feature_value"); value = sp_item(v, index); nb::object text = sp_format(sp_call1(std::move(formatter), std::move(value)));
            result.append(sp_build_string<3>({std::move(key), nb::borrow<nb::object>(literals[1]), std::move(text)}));
        }
    } catch (...) { iterator.reset(); result.reset(); v.reset(); k.reset(); throw; }
    iterator.reset(); v.reset(); k.reset(); return result;
}
nb::object viewer_labels_properties(nb::object self, nb::object position, nb::object view_direction, nb::object dims_displayed, nb::object world,
                                   nb::dict namespace_, nb::tuple literals, ViewerStatusProviderLocals& o) {
    SP_LAYOUT(o, PF(value), PF(label_value), PF(idx)); StatusProviderNamespace ns(std::move(namespace_));
    nb::object len = ns.get("len"); nb::object label_index = sp_attr(self, "_label_index"); nb::object count = sp_call1(std::move(len), std::move(label_index));
    if (sp_test(std::move(count), nb::int_(0), Py_EQ) || sp_test(sp_item(sp_attr(sp_attr(self, "features"), "shape"), nb::int_(1)), nb::int_(0), Py_EQ)) return nb::list();
    nb::object fn = sp_attr(self, "get_value"); o.value = sp_sample(std::move(fn), std::move(position), std::move(view_direction), std::move(dims_displayed), std::move(world)); if (o.value.is_none()) return nb::list();
    nb::object cast = sp_attr(ns.get("typing"), "cast"); nb::object int_ = ns.get("int"); nb::object label = sp_truth(sp_attr(self, "multiscale")) ? sp_item(o.value, nb::int_(1)) : o.value; o.label_value = sp_call2(std::move(cast), std::move(int_), std::move(label));
    if (!sp_contains(sp_attr(self, "_label_index"), o.label_value)) { nb::list missing; missing.append(literals[2]); return missing; }
    o.idx = sp_item(sp_attr(self, "_label_index"), o.label_value); return sp_feature_strings(std::move(self), o.idx, ns, std::move(literals));
}
void sp_append_properties(ViewerStatusProviderLocals& o, nb::tuple literals) {
    for (const char* key : {"coordinates", "value"}) {
        nb::object target = o.status; nb::object key_ = nb::str(key); nb::object original = sp_item(target, key_);
        nb::object prefix = nb::borrow<nb::object>(literals[0]); nb::object join = sp_attr(nb::borrow<nb::object>(literals[1]), "join"); nb::object joined = sp_call1(std::move(join), o.properties); nb::object addition = sp_add(std::move(prefix), std::move(joined));
        nb::object updated = sp_checked(PyNumber_InPlaceAdd(original.ptr(), addition.ptr())); original.reset(); addition.reset(); sp_set(std::move(target), std::move(key_), std::move(updated));
    }
}
nb::object viewer_labels_status(nb::object self, nb::object position, nb::object view_direction, nb::object dims_displayed, nb::object world,
                               nb::dict namespace_, nb::object super_provider, nb::tuple literals, ViewerStatusProviderLocals& o) {
    SP_LAYOUT(o, PF(status), PF(properties)); StatusProviderNamespace ns(std::move(namespace_));
    nb::object fn = sp_call1(std::move(super_provider), self); o.status = sp_sample(std::move(fn), position, view_direction, dims_displayed, world);
    fn = sp_attr(std::move(self), "_get_properties"); nb::object array = ns.np("asarray"); nb::object direction = sp_call1(std::move(array), std::move(view_direction)); o.properties = sp_sample(std::move(fn), std::move(position), std::move(direction), std::move(dims_displayed), std::move(world));
    if (sp_truth(o.properties)) sp_append_properties(o, std::move(literals)); return o.status;
}
nb::object viewer_labels_tooltip(nb::object self, nb::object position, nb::object view_direction, nb::object dims_displayed, nb::object world,
                                nb::tuple literals, ViewerStatusProviderLocals& o) {
    SP_LAYOUT(o, PF(value), PF(properties)); nb::object fn = sp_attr(self, "get_value"); o.value = sp_sample(std::move(fn), position, view_direction, dims_displayed, world); if (o.value.is_none()) return nb::borrow<nb::object>(literals[0]);
    fn = sp_attr(std::move(self), "_get_properties"); o.properties = sp_sample(std::move(fn), std::move(position), std::move(view_direction), std::move(dims_displayed), std::move(world)); if (!sp_truth(o.properties)) return sp_format(o.value);
    nb::object value = sp_format(o.value); nb::object prefix = sp_build_string<2>({std::move(value), nb::borrow<nb::object>(literals[1])}); nb::object join = sp_attr(nb::borrow<nb::object>(literals[1]), "join"); nb::object text = sp_call1(std::move(join), o.properties); return sp_add(std::move(prefix), std::move(text));
}

nb::object sp_displayed_position(nb::object self, nb::object position) {
    nb::object displayed = sp_attr(sp_attr(std::move(self), "_slice_input"), "displayed"); nb::object iterator = sp_checked(PyObject_GetIter(displayed.ptr())); displayed.reset(); nb::list result; nb::object i;
    try { for (;;) { nb::object next = sp_next(iterator); if (!next.is_valid()) break; i = std::move(next); result.append(sp_item(position, i)); } }
    catch (...) { iterator.reset(); result.reset(); i.reset(); throw; } iterator.reset(); i.reset(); return result;
}
void sp_point_sizes(nb::object self, const StatusProviderNamespace& ns, ViewerStatusProviderLocals& o) {
    nb::object abs = ns.np("abs"); nb::object scale = sp_attr(self, "scale"); nb::object displayed = sp_attr(sp_attr(self, "_slice_input"), "displayed"); nb::object first = sp_item(std::move(scale), std::move(displayed)); nb::object last = sp_item(sp_attr(self, "scale"), nb::int_(-1)); nb::object ratio = sp_div(std::move(first), std::move(last)); o.scale_ratio = sp_call1(std::move(abs), std::move(ratio));
    nb::object expand = ns.np("expand_dims"); nb::object size = sp_attr(std::move(self), "_view_size"); nb::object expanded = sp_axis(std::move(expand), std::move(size), 1); nb::object adjusted = sp_div(std::move(expanded), o.scale_ratio); o.sizes = sp_div(std::move(adjusted), nb::int_(2));
}
void sp_point_indices(nb::object data, nb::object position, const StatusProviderNamespace& ns, ViewerStatusProviderLocals& o) {
    nb::object abs = ns.get("abs"); nb::object delta = sp_sub(std::move(data), std::move(position)); o.distances = sp_call1(std::move(abs), std::move(delta));
    nb::object all = ns.np("all"); nb::object within = sp_compare(o.distances, o.sizes, Py_LE); o.in_slice_matches = sp_axis(std::move(all), std::move(within), 1);
    nb::object where = ns.np("where"); nb::object result = sp_call1(std::move(where), o.in_slice_matches); o.indices = sp_item(std::move(result), nb::int_(0));
}
nb::object viewer_points_value(nb::object self, nb::object position, nb::dict namespace_, ViewerStatusProviderLocals& o) {
    SP_LAYOUT(o, PF(view_data), PF(selection), PF(displayed_position), PF(scale_ratio), PF(sizes), PF(distances), PF(in_slice_matches), PF(indices)); StatusProviderNamespace ns(std::move(namespace_));
    o.view_data = sp_attr(self, "_view_data"); o.selection = nb::none();
    if (sp_test(sp_len(o.view_data, ns), nb::int_(0), Py_GT)) {
        o.displayed_position = sp_displayed_position(self, std::move(position)); sp_point_sizes(self, ns, o); sp_point_indices(o.view_data, o.displayed_position, ns, o);
        if (sp_test(sp_len(o.indices, ns), nb::int_(0), Py_GT)) { nb::object target = sp_attr(std::move(self), "_view_indices"); nb::object index = sp_item(o.indices, nb::int_(-1)); o.selection = sp_item(std::move(target), std::move(index)); }
    } return o.selection;
}
nb::object viewer_points_value_3d(nb::object self, nb::object start_point, nb::object end_point, nb::object dims_displayed, nb::dict namespace_, ViewerStatusProviderLocals& o) {
    SP_LAYOUT(o, PF(plane_point), PF(plane_normal), PF(projected_points), PF(projection_distances), PF(rotated_points), PF(rotation_matrix), PF(rotated_click_point), PF(scale_ratio), PF(sizes), PF(distances), PF(in_slice_matches), PF(indices), PF(candidate_point_distances), PF(closest_index), PF(selection)); StatusProviderNamespace ns(std::move(namespace_));
    if (start_point.is_none() || end_point.is_none()) return nb::none();
    nb::object fn = ns.get("displayed_plane_from_nd_line_segment"); SPPair pair = sp_pair(sp_call3(std::move(fn), std::move(start_point), std::move(end_point), std::move(dims_displayed))); o.plane_point = std::move(pair.first); o.plane_normal = std::move(pair.second);
    fn = ns.get("project_points_onto_plane"); nb::dict kwargs; kwargs["points"] = sp_attr(self, "_view_data"); kwargs["plane_point"] = o.plane_point; kwargs["plane_normal"] = o.plane_normal;
    pair = sp_pair(sp_kw(std::move(fn), nb::tuple(), std::move(kwargs))); o.projected_points = std::move(pair.first); o.projection_distances = std::move(pair.second);
    fn = ns.get("rotate_points"); kwargs = nb::dict(); kwargs["points"] = o.projected_points; kwargs["current_plane_normal"] = o.plane_normal; nb::list z; z.append(nb::int_(0)); z.append(nb::int_(0)); z.append(nb::int_(1)); kwargs["new_plane_normal"] = std::move(z);
    pair = sp_pair(sp_kw(std::move(fn), nb::tuple(), std::move(kwargs))); o.rotated_points = std::move(pair.first); o.rotation_matrix = std::move(pair.second);
    fn = ns.np("dot"); o.rotated_click_point = sp_call2(std::move(fn), o.rotation_matrix, o.plane_point); sp_point_sizes(self, ns, o); sp_point_indices(o.rotated_points, o.rotated_click_point, ns, o);
    if (sp_test(sp_len(o.indices, ns), nb::int_(0), Py_GT)) { o.candidate_point_distances = sp_item(o.projection_distances, o.indices); nb::object target = o.indices; fn = ns.np("argmin"); nb::object closest = sp_call1(std::move(fn), o.candidate_point_distances); o.closest_index = sp_item(std::move(target), std::move(closest)); o.selection = sp_item(sp_attr(std::move(self), "_view_indices"), o.closest_index); }
    else o.selection = nb::none(); return o.selection;
}
nb::object viewer_points_status(nb::object self, nb::object position, nb::object view_direction, nb::object dims_displayed, nb::object world,
                               nb::object super_provider, nb::tuple literals, ViewerStatusProviderLocals& o) {
    SP_LAYOUT(o, PF(status), PF(properties)); nb::object fn = sp_call1(std::move(super_provider), self); o.status = sp_sample(std::move(fn), position, view_direction, dims_displayed, world);
    fn = sp_attr(std::move(self), "_get_properties"); o.properties = sp_sample(std::move(fn), std::move(position), std::move(view_direction), std::move(dims_displayed), std::move(world));
    if (sp_truth(o.properties)) sp_append_properties(o, std::move(literals)); return o.status;
}
nb::object viewer_points_tooltip(nb::object self, nb::object position, nb::object view_direction, nb::object dims_displayed, nb::object world, nb::object newline) {
    nb::object join = sp_attr(std::move(newline), "join"); nb::object properties = sp_attr(std::move(self), "_get_properties"); nb::object result = sp_sample(std::move(properties), std::move(position), std::move(view_direction), std::move(dims_displayed), std::move(world)); return sp_call1(std::move(join), std::move(result));
}
nb::object viewer_points_properties(nb::object self, nb::object position, nb::object view_direction, nb::object dims_displayed, nb::object world,
                                   nb::dict namespace_, nb::tuple literals, ViewerStatusProviderLocals& o) {
    SP_LAYOUT(o, PF(value)); StatusProviderNamespace ns(std::move(namespace_));
    if (sp_test(sp_item(sp_attr(sp_attr(self, "features"), "shape"), nb::int_(1)), nb::int_(0), Py_EQ)) return nb::list();
    nb::object fn = sp_attr(self, "get_value"); o.value = sp_sample(std::move(fn), std::move(position), std::move(view_direction), std::move(dims_displayed), std::move(world));
    if (o.value.is_none()) return nb::list(); nb::object first = o.value; nb::object count = sp_item(sp_attr(sp_attr(self, "data"), "shape"), nb::int_(0)); if (sp_test(std::move(first), std::move(count), Py_GT)) return nb::list();
    return sp_feature_strings(std::move(self), o.value, ns, std::move(literals));
}
nb::object viewer_points_view_data(nb::object self, nb::dict namespace_, ViewerStatusProviderLocals& o) {
    SP_LAYOUT(o, PF(data)); StatusProviderNamespace ns(std::move(namespace_));
    nb::object len = ns.get("len"); nb::object view_indices = sp_attr(self, "_view_indices"); nb::object count = sp_call1(std::move(len), std::move(view_indices));
    if (sp_test(std::move(count), nb::int_(0), Py_GT)) {
        nb::object data = sp_attr(self, "data"); nb::object ix = ns.np("ix_"); nb::object indices = sp_attr(self, "_view_indices"); nb::object displayed = sp_attr(sp_attr(self, "_slice_input"), "displayed"); nb::object key = sp_call2(std::move(ix), std::move(indices), std::move(displayed)); o.data = sp_item(std::move(data), std::move(key));
    } else { nb::object zeros = ns.np("zeros"); nb::object ndim = sp_attr(sp_attr(std::move(self), "_slice_input"), "ndisplay"); o.data = sp_call1(std::move(zeros), nb::make_tuple(nb::int_(0), std::move(ndim))); }
    return o.data;
}

nb::object viewer_shapes_value(nb::object self, nb::object position, nb::dict namespace_, ViewerStatusProviderLocals& o) {
    SP_LAYOUT(o, PF(coord), PF(value), PF(selected_index), PF(box), PF(distances), PF(matches), PF(selected_vertex_mask), PF(vertices), PF(index), PF(shape), PF(vals), PF(idx), PF(shape_in_list)); StatusProviderNamespace ns(std::move(namespace_));
    if (sp_test(sp_attr(sp_attr(self, "_slice_input"), "ndisplay"), nb::int_(3), Py_EQ)) return nb::make_tuple(nb::none(), nb::none()); if (sp_truth(sp_attr(self, "_is_moving"))) return sp_attr(std::move(self), "_moving_value");
    o.coord = sp_displayed_position(self, std::move(position)); o.value = nb::none(); o.selected_index = sp_attr(self, "_selected_data_in_view");
    if (sp_test(sp_len(o.selected_index, ns), nb::int_(0), Py_GT)) {
        nb::object scale = sp_attr(self, "scale"); nb::object displayed = sp_attr(sp_attr(self, "_slice_input"), "displayed"); sp_item(std::move(scale), std::move(displayed));
        nb::object mode = sp_attr(self, "_mode"); nb::object select = sp_attr(ns.get("Mode"), "SELECT");
        if (sp_test(std::move(mode), std::move(select), Py_EQ) && !sp_attr(self, "_selected_box").is_none()) {
            nb::object box = sp_attr(self, "_selected_box"); nb::object handle = sp_attr(ns.get("Box"), "WITH_HANDLE"); o.box = sp_item(std::move(box), std::move(handle));
            nb::object abs = ns.get("abs"); nb::object delta = sp_sub(o.box, o.coord); o.distances = sp_call1(std::move(abs), std::move(delta));
            nb::object all = ns.np("all"); nb::object limit = sp_attr(self, "_normalized_vertex_radius"); nb::object within = sp_compare(o.distances, std::move(limit), Py_LE); nb::object matches = sp_axis(std::move(all), std::move(within), 1); o.matches = sp_call0(sp_attr(std::move(matches), "nonzero"));
            nb::object len = ns.get("len"); nb::object first_matches = sp_item(o.matches, nb::int_(0)); nb::object count = sp_call1(std::move(len), std::move(first_matches));
            if (sp_test(std::move(count), nb::int_(0), Py_GT)) { nb::object shape = sp_item(o.selected_index, nb::int_(0)); nb::object match = sp_item(sp_item(o.matches, nb::int_(0)), nb::int_(-1)); o.value = nb::make_tuple(std::move(shape), std::move(match)); }
        } else {
            mode = sp_attr(self, "_mode"); nb::list modes; modes.append(sp_attr(ns.get("Mode"), "DIRECT")); modes.append(sp_attr(ns.get("Mode"), "VERTEX_INSERT")); modes.append(sp_attr(ns.get("Mode"), "VERTEX_REMOVE"));
            if (sp_contains(std::move(modes), std::move(mode))) {
                nb::object isin = ns.np("isin"); nb::object shape_indices = sp_attr(sp_attr(self, "_data_view"), "displayed_vertices_to_shape_num"); o.selected_vertex_mask = sp_call2(std::move(isin), std::move(shape_indices), o.selected_index);
                o.vertices = sp_item(sp_attr(sp_attr(self, "_data_view"), "displayed_vertices"), o.selected_vertex_mask); nb::object abs = ns.get("abs"); nb::object delta = sp_sub(o.vertices, o.coord); o.distances = sp_call1(std::move(abs), std::move(delta));
                nb::object all = ns.np("all"); nb::object radius = sp_attr(self, "_normalized_vertex_radius"); nb::object within = sp_compare(o.distances, std::move(radius), Py_LE); nb::object matches = sp_axis(std::move(all), std::move(within), 1); o.matches = sp_item(sp_call0(sp_attr(std::move(matches), "nonzero")), nb::int_(0));
                if (sp_test(sp_len(o.matches, ns), nb::int_(0), Py_GT)) {
                    nb::object nonzero = sp_call0(sp_attr(o.selected_vertex_mask, "nonzero")); nb::object target = sp_item(std::move(nonzero), nb::int_(0)); nb::object last = sp_item(o.matches, nb::int_(-1)); o.index = sp_item(std::move(target), std::move(last));
                    o.shape = sp_item(sp_attr(sp_attr(self, "_data_view"), "displayed_vertices_to_shape_num"), o.index);
                    nb::object unique = ns.np("unique"); shape_indices = sp_attr(sp_attr(self, "_data_view"), "displayed_vertices_to_shape_num"); nb::dict kwargs; kwargs["return_index"] = nb::bool_(true); SPPair pair = sp_pair(sp_kw(std::move(unique), nb::make_tuple(std::move(shape_indices)), std::move(kwargs))); o.vals = std::move(pair.first); o.idx = std::move(pair.second);
                    nb::object list = ns.get("list"); nb::object values = sp_call1(std::move(list), o.vals); nb::object index = sp_attr(std::move(values), "index"); o.shape_in_list = sp_call1(std::move(index), o.shape);
                    nb::object offset = sp_item(o.idx, o.shape_in_list); nb::object vertex = sp_sub(o.index, std::move(offset)); o.value = nb::make_tuple(o.shape, std::move(vertex));
                }
            }
        }
    }
    if (o.value.is_none()) { nb::object inside = sp_attr(sp_attr(std::move(self), "_data_view"), "inside"); o.shape = sp_call1(std::move(inside), o.coord); o.value = nb::make_tuple(o.shape, nb::none()); } return o.value;
}
nb::object viewer_shapes_value_3d(nb::object self, nb::object start_point, nb::object end_point, nb::object dims_displayed, ViewerStatusProviderLocals& o) {
    SP_LAYOUT(o, PF(value), PF(_)); nb::object fn = sp_attr(std::move(self), "_get_index_and_intersection"); SPPair pair = sp_pair(sp_raycast(std::move(fn), std::move(start_point), std::move(end_point), std::move(dims_displayed))); o.value = std::move(pair.first); o._ = std::move(pair.second); return nb::make_tuple(o.value, nb::none());
}
nb::object viewer_shapes_index_intersection(nb::object self, nb::object start_point, nb::object end_point, nb::object dims_displayed, nb::dict namespace_, ViewerStatusProviderLocals& o) {
    SP_LAYOUT(o, PF(start_position), PF(ray_direction), PF(value), PF(intersection), PF(intersection_point)); StatusProviderNamespace ns(std::move(namespace_));
    if (sp_test(sp_len(dims_displayed, ns), nb::int_(3), Py_NE)) return nb::make_tuple(nb::none(), nb::none()); if (start_point.is_none() || end_point.is_none()) return nb::make_tuple(nb::none(), nb::none());
    nb::object fn = ns.get("nd_line_segment_to_displayed_data_ray"); SPPair pair = sp_pair(sp_raycast(std::move(fn), start_point, std::move(end_point), dims_displayed)); o.start_position = std::move(pair.first); o.ray_direction = std::move(pair.second);
    fn = sp_attr(sp_attr(std::move(self), "_data_view"), "_inside_3d"); pair = sp_pair(sp_call2(std::move(fn), o.start_position, o.ray_direction)); o.value = std::move(pair.first); o.intersection = std::move(pair.second);
    o.intersection_point = sp_copy(std::move(start_point)); sp_set(o.intersection_point, std::move(dims_displayed), o.intersection); return nb::make_tuple(o.value, o.intersection_point);
}
nb::object viewer_shapes_selected_view(nb::object self, nb::dict namespace_, ViewerStatusProviderLocals& o) {
    SP_LAYOUT(o, PF(view_indices)); StatusProviderNamespace ns(std::move(namespace_)); nb::object set = ns.get("set"); nb::object list = sp_call0(sp_attr(sp_attr(self, "_view_indices"), "tolist")); o.view_indices = sp_call1(std::move(set), std::move(list));
    nb::object selected = sp_attr(std::move(self), "selected_data"); nb::object iterator = sp_checked(PyObject_GetIter(selected.ptr())); selected.reset(); nb::list result; nb::object i;
    try { for (;;) { nb::object next = sp_next(iterator); if (!next.is_valid()) break; i = std::move(next); if (sp_contains(o.view_indices, i)) result.append(i); } }
    catch (...) { iterator.reset(); result.reset(); i.reset(); throw; } iterator.reset(); i.reset(); return result;
}
nb::object viewer_shapes_normalized_scale(nb::object self, nb::dict namespace_) { StatusProviderNamespace ns(std::move(namespace_)); nb::object scale = sp_attr(self, "scale_factor"); nb::object abs = ns.get("abs"); nb::object last = sp_item(sp_attr(std::move(self), "scale"), nb::int_(-1)); nb::object denominator = sp_call1(std::move(abs), std::move(last)); return sp_div(std::move(scale), std::move(denominator)); }
nb::object viewer_shapes_vertex_radius(nb::object self) { nb::object size = sp_attr(self, "_vertex_size"); nb::object scale = sp_attr(std::move(self), "_normalized_scale_factor"); nb::object product = sp_mul(std::move(size), std::move(scale)); return sp_div(std::move(product), nb::int_(2)); }

nb::object viewer_shape_list_visible(nb::object self, nb::dict namespace_, ViewerStatusProviderLocals& o) {
    SP_LAYOUT(o, PF(slice_key)); StatusProviderNamespace ns(std::move(namespace_)); o.slice_key = sp_attr(self, "slice_key");
    if (sp_truth(sp_len(o.slice_key, ns))) {
        nb::object enumerate = ns.get("enumerate"); nb::object shapes = sp_attr(std::move(self), "shapes"); nb::object enumerated = sp_call1(std::move(enumerate), std::move(shapes)); nb::object iterator = sp_checked(PyObject_GetIter(enumerated.ptr())); enumerated.reset(); nb::list result; nb::object i, s;
        try { for (;;) { nb::object item = sp_next(iterator); if (!item.is_valid()) break; SPPair pair = sp_pair(std::move(item)); i = std::move(pair.first); s = std::move(pair.second);
            nb::object all = ns.np("all"); nb::object low = sp_item(sp_attr(s, "slice_key"), nb::int_(0)); nb::object within = sp_compare(std::move(low), o.slice_key, Py_LE); if (!sp_truth(sp_call1(std::move(all), std::move(within)))) continue;
            all = ns.np("all"); nb::object left = o.slice_key; nb::object high = sp_item(sp_attr(s, "slice_key"), nb::int_(1)); within = sp_compare(std::move(left), std::move(high), Py_LE); if (sp_truth(sp_call1(std::move(all), std::move(within)))) result.append(nb::make_tuple(i, s));
        } } catch (...) { iterator.reset(); result.reset(); s.reset(); i.reset(); throw; } iterator.reset(); s.reset(); i.reset(); return result;
    }
    nb::object list = ns.get("list"); nb::object enumerate = ns.get("enumerate"); nb::object shapes = sp_attr(std::move(self), "shapes"); nb::object enumerated = sp_call1(std::move(enumerate), std::move(shapes)); return sp_call1(std::move(list), std::move(enumerated));
}
nb::object viewer_shape_list_boxes(nb::object self, nb::dict namespace_, ViewerStatusProviderLocals& o) {
    SP_LAYOUT(o, PF(data)); StatusProviderNamespace ns(std::move(namespace_)); nb::object array = ns.np("array"); nb::object visible = sp_attr(self, "_visible_shapes"); nb::object iterator = sp_checked(PyObject_GetIter(visible.ptr())); visible.reset(); nb::list boxes; nb::object s_;
    try { for (;;) { nb::object next = sp_next(iterator); if (!next.is_valid()) break; s_ = std::move(next); boxes.append(sp_attr(sp_item(s_, nb::int_(1)), "bounding_box")); } }
    catch (...) { iterator.reset(); boxes.reset(); s_.reset(); throw; } iterator.reset(); s_.reset(); o.data = sp_call1(std::move(array), std::move(boxes));
    if (sp_test(sp_attr(o.data, "size"), nb::int_(0), Py_EQ)) { nb::object empty = ns.np("empty"); nb::object ndisplay = sp_attr(self, "ndisplay"); nb::object first = sp_call1(std::move(empty), nb::make_tuple(nb::int_(0), std::move(ndisplay))); empty = ns.np("empty"); ndisplay = sp_attr(std::move(self), "ndisplay"); nb::object second = sp_call1(std::move(empty), nb::make_tuple(nb::int_(0), std::move(ndisplay))); return nb::make_tuple(std::move(first), std::move(second)); }
    nb::object first = sp_item(o.data, nb::make_tuple(sp_slice(), nb::int_(0))); nb::object second = sp_item(o.data, nb::make_tuple(sp_slice(), nb::int_(1))); return nb::make_tuple(std::move(first), std::move(second));
}
nb::object viewer_shape_list_inside(nb::object self_getter, nb::object coord_getter, nb::object indices_getter, nb::dict namespace_, nb::object generator_factory, ViewerStatusProviderLocals& o) {
    SP_LAYOUT(o, PF(bounding_boxes), PF(in_bbox), PF(inside_indices), PF(z_index), PF(pos)); StatusProviderNamespace ns(namespace_);
    nb::object self_cell = sp_cell(std::move(self_getter)); nb::object coord_cell = sp_cell(std::move(coord_getter)); o.inside_indices = sp_cell(std::move(indices_getter)); o.inside_indices_is_cell = true;
    const auto self = [&]() { return sp_read_cell(self_cell, "self"); }; const auto coord = [&]() { return sp_read_cell(coord_cell, "coord"); }; const auto indices = [&]() { return sp_read_cell(o.inside_indices, "inside_indices"); };
    if (!sp_truth(sp_attr(self(), "shapes"))) return nb::none(); o.bounding_boxes = sp_attr(self(), "_bounding_boxes");
    nb::object all = ns.np("all"); nb::object low = sp_item(o.bounding_boxes, nb::int_(0)); nb::object actual = coord(); nb::object lower = sp_compare(std::move(low), std::move(actual), Py_LE);
    nb::object high = sp_item(o.bounding_boxes, nb::int_(1)); actual = coord(); nb::object upper = sp_compare(std::move(high), std::move(actual), Py_GE); nb::object mask = sp_mul(std::move(lower), std::move(upper)); o.in_bbox = sp_axis(std::move(all), std::move(mask), 1);
    nb::object flat = ns.np("flatnonzero"); nb::object index_values = sp_call1(std::move(flat), o.in_bbox); if (PyCell_Set(o.inside_indices.ptr(), index_values.ptr()) < 0) throw nb::python_error(); index_values.reset();
    if (sp_test(sp_attr(indices(), "size"), nb::int_(0), Py_EQ)) return nb::none();
    try {
        nb::object values = indices(); nb::object iterator = sp_checked(PyObject_GetIter(values.ptr())); values.reset(); nb::list z_index; nb::object i;
        try { for (;;) { nb::object next = sp_next(iterator); if (!next.is_valid()) break; i = std::move(next); nb::object visible = sp_attr(self(), "_visible_shapes"); nb::object entry = sp_item(std::move(visible), i); nb::object shape = sp_item(std::move(entry), nb::int_(1)); z_index.append(sp_attr(std::move(shape), "z_index")); } }
        catch (...) { iterator.reset(); z_index.reset(); i.reset(); throw; } iterator.reset(); i.reset(); o.z_index = std::move(z_index);
        nb::object argsort = ns.np("argsort"); o.pos = sp_call1(std::move(argsort), o.z_index);
        nb::object visible = sp_attr(self(), "_visible_shapes"); nb::object next = ns.get("next"); nb::object reversed = sp_item(o.pos, sp_checked(PySlice_New(Py_None, Py_None, nb::int_(-1).ptr()))); iterator = sp_checked(PyObject_GetIter(reversed.ptr())); reversed.reset();
        ViewerStatusGeneratorIterator native(StatusGeneratorKind::ShapeInside, std::move(iterator), namespace_, ns.builtins); native.self_cell = self_cell; native.coord_cell = coord_cell; native.indices_cell = o.inside_indices;
        nb::object generator = sp_generator(std::move(generator_factory), std::move(native), "ShapeList.inside.<locals>.<genexpr>"); nb::object chosen = sp_call1(std::move(next), std::move(generator)); nb::object entry = sp_item(std::move(visible), std::move(chosen)); return sp_item(std::move(entry), nb::int_(0));
    } catch (nb::python_error& error) { nb::python_error traced = sp_add_caught_frame(error); { events::HandledException handled(traced.value()); if (!sp_matches(traced, "StopIteration", ns)) throw std::move(traced); } return nb::none(); }
}
nb::object viewer_shape_list_inside_3d(nb::object self, nb::object ray_position, nb::object ray_direction, nb::dict namespace_, ViewerStatusProviderLocals& o) {
    SP_LAYOUT(o, PF(triangles), PF(inside), PF(intersection_points), PF(start_to_intersection), PF(distances), PF(closest_shape_index), PF(shape), PF(intersection)); StatusProviderNamespace ns(std::move(namespace_));
    nb::object vertices = sp_attr(sp_attr(self, "_mesh"), "vertices"); nb::object displayed = sp_attr(sp_attr(self, "_mesh"), "displayed_triangles"); o.triangles = sp_item(std::move(vertices), std::move(displayed));
    nb::object fn = ns.get("line_in_triangles_3d"); nb::dict kwargs; kwargs["line_point"] = ray_position; kwargs["line_direction"] = ray_direction; kwargs["triangles"] = o.triangles; o.inside = sp_kw(std::move(fn), nb::tuple(), std::move(kwargs));
    nb::object any = ns.np("any"); if (!sp_truth(sp_call1(std::move(any), o.inside))) return nb::make_tuple(nb::none(), nb::none());
    fn = sp_attr(self, "_triangle_intersection"); kwargs = nb::dict(); kwargs["triangle_indices"] = o.inside; kwargs["ray_position"] = ray_position; kwargs["ray_direction"] = std::move(ray_direction); o.intersection_points = sp_kw(std::move(fn), nb::tuple(), std::move(kwargs));
    o.start_to_intersection = sp_sub(o.intersection_points, std::move(ray_position)); fn = ns.norm(); o.distances = sp_axis(std::move(fn), o.start_to_intersection, 1); fn = ns.np("argmin"); o.closest_shape_index = sp_call1(std::move(fn), o.distances);
    nb::object shape_indices = sp_attr(sp_attr(std::move(self), "_mesh"), "displayed_triangles_to_shape_index"); nb::object selected = sp_item(std::move(shape_indices), o.inside); o.shape = sp_item(std::move(selected), o.closest_shape_index); o.intersection = sp_item(o.intersection_points, o.closest_shape_index); return nb::make_tuple(o.shape, o.intersection);
}
nb::object viewer_shape_list_triangle_intersection(nb::object self, nb::object triangle_indices, nb::object ray_position, nb::object ray_direction, nb::dict namespace_, ViewerStatusProviderLocals& o) {
    SP_LAYOUT(o, PF(triangles), PF(intersected_triangles), PF(intersection_points)); StatusProviderNamespace ns(std::move(namespace_)); nb::object vertices = sp_attr(sp_attr(self, "_mesh"), "vertices"); nb::object displayed = sp_attr(sp_attr(std::move(self), "_mesh"), "displayed_triangles"); o.triangles = sp_item(std::move(vertices), std::move(displayed)); o.intersected_triangles = sp_item(o.triangles, std::move(triangle_indices));
    nb::object fn = ns.get("intersect_line_with_triangles"); nb::dict kwargs; kwargs["line_point"] = std::move(ray_position); kwargs["line_direction"] = std::move(ray_direction); kwargs["triangles"] = o.intersected_triangles; o.intersection_points = sp_kw(std::move(fn), nb::tuple(), std::move(kwargs)); return o.intersection_points;
}
nb::object viewer_shape_all_triangles(nb::object self, nb::dict namespace_) {
    StatusProviderNamespace ns(std::move(namespace_)); nb::object stack = ns.np("vstack"); nb::object vertices = sp_attr(self, "_face_vertices"); nb::object triangles = sp_attr(self, "_face_triangles"); nb::object face = sp_item(std::move(vertices), std::move(triangles));
    vertices = sp_attr(self, "_edge_vertices"); nb::object width = sp_attr(self, "edge_width"); nb::object offsets = sp_attr(self, "_edge_offsets"); nb::object offset = sp_mul(std::move(width), std::move(offsets)); nb::object coordinates = sp_add(std::move(vertices), std::move(offset)); triangles = sp_attr(std::move(self), "_edge_triangles"); nb::object edge = sp_item(std::move(coordinates), std::move(triangles)); nb::list values; values.append(std::move(face)); values.append(std::move(edge)); return sp_call1(std::move(stack), std::move(values));
}

nb::object viewer_surface_value_none(nb::object self, nb::object position) { return nb::none(); }
nb::object viewer_surface_value_3d(nb::object self, nb::object start_point, nb::object end_point, nb::object dims_displayed, nb::dict namespace_, ViewerStatusProviderLocals& o) {
    SP_LAYOUT(o, PF(start_position), PF(ray_direction), PF(mesh_triangles), PF(intersection_index), PF(intersection), PF(intersection_point), PF(triangle_vertex_indices), PF(triangle_vertices), PF(barycentric_coordinates), PF(vertex_values), PF(intersection_value)); StatusProviderNamespace ns(std::move(namespace_));
    if (sp_test(sp_len(dims_displayed, ns), nb::int_(3), Py_NE)) return nb::make_tuple(nb::none(), nb::none()); if (start_point.is_none() || end_point.is_none()) return nb::make_tuple(nb::none(), nb::none());
    nb::object fn = ns.get("nd_line_segment_to_displayed_data_ray"); SPPair pair = sp_pair(sp_raycast(std::move(fn), start_point, std::move(end_point), dims_displayed)); o.start_position = std::move(pair.first); o.ray_direction = std::move(pair.second);
    nb::object vertices = sp_attr(self, "_view_vertices"); nb::object faces = sp_attr(self, "_view_faces"); o.mesh_triangles = sp_item(std::move(vertices), std::move(faces));
    fn = ns.get("find_nearest_triangle_intersection"); nb::dict kwargs; kwargs["ray_position"] = o.start_position; kwargs["ray_direction"] = o.ray_direction; kwargs["triangles"] = o.mesh_triangles; pair = sp_pair(sp_kw(std::move(fn), nb::tuple(), std::move(kwargs))); o.intersection_index = std::move(pair.first); o.intersection = std::move(pair.second);
    if (o.intersection_index.is_none() || o.intersection.is_none() || sp_attr(self, "_view_vertex_values").is_none()) return nb::make_tuple(nb::none(), nb::none());
    o.intersection_point = sp_copy(std::move(start_point)); sp_set(o.intersection_point, std::move(dims_displayed), o.intersection);
    o.triangle_vertex_indices = sp_item(sp_attr(self, "_view_faces"), o.intersection_index); o.triangle_vertices = sp_item(sp_attr(self, "_view_vertices"), o.triangle_vertex_indices);
    fn = ns.get("calculate_barycentric_coordinates"); o.barycentric_coordinates = sp_call2(std::move(fn), o.intersection, o.triangle_vertices); o.vertex_values = sp_item(sp_attr(std::move(self), "_view_vertex_values"), o.triangle_vertex_indices);
    nb::object product = sp_mul(o.barycentric_coordinates, o.vertex_values); fn = sp_attr(std::move(product), "sum"); o.intersection_value = sp_call0(std::move(fn)); return nb::make_tuple(o.intersection_value, o.intersection_index);
}
nb::object viewer_vectors_value_none(nb::object self, nb::object position) { return nb::none(); }
nb::object viewer_tracks_value(nb::object self, nb::object position, nb::dict namespace_, ViewerStatusProviderLocals& o) {
    SP_LAYOUT(o, PF(val)); StatusProviderNamespace ns(std::move(namespace_)); nb::object fn = sp_attr(sp_attr(std::move(self), "_manager"), "get_value"); nb::object array = ns.np("array"); nb::object coords = sp_call1(std::move(array), std::move(position)); o.val = sp_call1(std::move(fn), std::move(coords)); if (o.val.is_none()) return nb::none(); nb::object int_ = ns.get("int"); return sp_call1(std::move(int_), o.val);
}
nb::object viewer_track_manager_value(nb::object self, nb::object coords, nb::dict namespace_, ViewerStatusProviderLocals& o) {
    SP_LAYOUT(o, PF(_d), PF(idx), PF(pruned)); StatusProviderNamespace ns(std::move(namespace_)); if (sp_attr(self, "_kdtree").is_none()) return nb::none();
    nb::object fn = sp_attr(sp_attr(self, "_kdtree"), "query"); nb::dict kwargs; kwargs["k"] = nb::int_(10); SPPair pair = sp_pair(sp_kw(std::move(fn), nb::make_tuple(coords), std::move(kwargs))); o._d = std::move(pair.first); o.idx = std::move(pair.second);
    nb::object iterator = sp_checked(PyObject_GetIter(o.idx.ptr())); nb::list filtered; nb::object i;
    try { for (;;) { nb::object next = sp_next(iterator); if (!next.is_valid()) break; i = std::move(next); nb::object first = sp_compare(nb::int_(0), i, Py_LE); if (!sp_truth(std::move(first))) continue; nb::object left = i; nb::object shape = sp_item(sp_attr(sp_attr(self, "_points"), "shape"), nb::int_(0)); if (sp_test(std::move(left), std::move(shape), Py_LT)) filtered.append(i); } }
    catch (...) { iterator.reset(); filtered.reset(); i.reset(); throw; } iterator.reset(); i.reset(); o.idx = std::move(filtered);
    iterator = sp_checked(PyObject_GetIter(o.idx.ptr())); nb::list pruned;
    try { for (;;) { nb::object next = sp_next(iterator); if (!next.is_valid()) break; i = std::move(next); nb::object point = sp_item(sp_attr(self, "_points"), nb::make_tuple(i, nb::int_(0))); nb::object t = sp_item(coords, nb::int_(0)); if (sp_test(std::move(point), std::move(t), Py_EQ)) pruned.append(i); } }
    catch (...) { iterator.reset(); pruned.reset(); i.reset(); throw; } iterator.reset(); i.reset(); o.pruned = std::move(pruned);
    if (sp_truth(o.pruned) && !sp_attr(self, "_points_id").is_none()) { nb::object ids = sp_attr(std::move(self), "_points_id"); nb::object i = sp_item(o.pruned, nb::int_(0)); return sp_item(std::move(ids), std::move(i)); } return nb::none();
}

} // namespace

void bind_viewer_status_providers(nb::module_& module) {
    nb::class_<ViewerStatusProviderLocals>(module, "ViewerStatusProviderLocals", nb::is_weak_referenceable(), nb::type_slots(sp_owner_slots))
#define SP_PROPERTY(name) .def_prop_ro(#name, [](const ViewerStatusProviderLocals& o) { \
            if constexpr (SPField::name == SPField::inside_indices) { \
                if (o.inside_indices_is_cell && o.inside_indices.is_valid()) { \
                    PyObject* value = PyCell_GET(o.inside_indices.ptr()); \
                    return value ? nb::borrow<nb::object>(value) : nb::object(nb::none()); \
                } \
            } \
            return sp_optional(o.name); \
        })
        SP_FIELDS(SP_PROPERTY)
#undef SP_PROPERTY
        ;
    nb::class_<ViewerStatusGeneratorIterator>(module, "ViewerStatusGeneratorIterator", nb::is_weak_referenceable(), nb::type_slots(sp_generator_slots))
        .def("__iter__", [](nb::object self) { return self; })
        .def("__next__", &ViewerStatusGeneratorIterator::next)
        .def("send", &ViewerStatusGeneratorIterator::send, nb::arg("value").none())
        .def("throw", &ViewerStatusGeneratorIterator::throw_)
        .def("close", &ViewerStatusGeneratorIterator::close);
    module.def("viewer_status_provider_locals", [] { return ViewerStatusProviderLocals{}; });
    module.def("viewer_status_position_locals", [](nb::object position) { ViewerStatusProviderLocals o; o.position = std::move(position); SP_LAYOUT(o, PF(position)); return o; }, nb::arg("position").none());
    module.def("viewer_status_value_locals", [](nb::object position, nb::object view_direction, nb::object dims_displayed) { ViewerStatusProviderLocals o; o.position = std::move(position); o.view_direction = std::move(view_direction); o.dims_displayed = std::move(dims_displayed); SP_LAYOUT(o, PF(position), PF(view_direction), PF(dims_displayed)); return o; }, nb::arg("position").none(), nb::arg("view_direction").none(), nb::arg("dims_displayed").none());
    module.def("viewer_status_ray_locals", [](nb::object position, nb::object view_direction) { ViewerStatusProviderLocals o; o.position = std::move(position); o.view_direction = std::move(view_direction); SP_LAYOUT(o, PF(position), PF(view_direction)); return o; }, nb::arg("position").none(), nb::arg("view_direction").none());
    module.def("viewer_status_status_locals", [](nb::object position, nb::object value) { ViewerStatusProviderLocals o; o.position = std::move(position); o.value = std::move(value); SP_LAYOUT(o, PF(position), PF(value)); return o; }, nb::arg("position").none(), nb::arg("value").none());
    module.def("viewer_status_sample_locals", [](nb::object start_point, nb::object end_point) { ViewerStatusProviderLocals o; o.start_point = std::move(start_point); o.end_point = std::move(end_point); SP_LAYOUT(o, PF(start_point), PF(end_point)); return o; }, nb::arg("start_point").none(), nb::arg("end_point").none());
    module.def("viewer_status_calc_cursor", &viewer_status_calc_cursor, nb::arg("self").none(), nb::arg("namespace"), nb::arg("literals"), nb::arg("locals_owner"));
    module.def("viewer_status_update_cursor", &viewer_status_update_cursor, nb::arg("self").none(), nb::arg("locals_owner"));
    module.def("viewer_layer_get_value", &viewer_layer_get_value, nb::arg("self").none(), nb::arg("world").none(), nb::arg("namespace"), nb::arg("pan_zoom").none(), nb::arg("locals_owner"));
    module.def("viewer_layer_value_3d_none", &viewer_layer_value_3d_none, nb::arg("self").none(), nb::arg("start_point").none(), nb::arg("end_point").none(), nb::arg("dims_displayed").none());
    module.def("viewer_layer_world_to_data", &viewer_layer_world_to_data, nb::arg("self").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("viewer_layer_world_displayed", &viewer_layer_world_displayed, nb::arg("self").none(), nb::arg("position").none(), nb::arg("dims_displayed").none(), nb::arg("locals_owner"));
    module.def("viewer_layer_world_ray", &viewer_layer_world_ray, nb::arg("self").none(), nb::arg("vector").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("viewer_layer_world_displayed_ray", &viewer_layer_world_displayed_ray, nb::arg("self").none(), nb::arg("vector_world").none(), nb::arg("dims_displayed").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("viewer_layer_display_bbox", &viewer_layer_display_bbox, nb::arg("self").none(), nb::arg("dims_displayed").none());
    module.def("viewer_layer_ray_intersections", &viewer_layer_ray_intersections, nb::arg("self").none(), nb::arg("dims_displayed").none(), nb::arg("world").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("viewer_layer_offset_position", &viewer_layer_offset_position, nb::arg("self").none(), nb::arg("position").none(), nb::arg("namespace"));
    module.def("viewer_layer_ray_intersections_internal", &viewer_layer_ray_intersections_internal, nb::arg("self").none(), nb::arg("view_direction").none(), nb::arg("dims_displayed").none(), nb::arg("bounding_box").none(), nb::arg("world").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("viewer_layer_source_info", &viewer_layer_source_info, nb::arg("self").none(), nb::arg("namespace"), nb::arg("literals"), nb::arg("locals_owner"));
    module.def("viewer_layer_integer_coords", &viewer_layer_integer_coords, nb::arg("self").none(), nb::arg("namespace"), nb::arg("generator_factory").none());
    module.def("viewer_layer_status", &viewer_layer_status, nb::arg("self").none(), nb::arg("view_direction").none(), nb::arg("dims_displayed").none(), nb::arg("world").none(), nb::arg("namespace"), nb::arg("literals"), nb::arg("locals_owner"));
    module.def("viewer_layer_tooltip_empty", &viewer_layer_tooltip_empty, nb::arg("self").none(), nb::arg("position").none(), nb::arg("view_direction").none(), nb::arg("dims_displayed").none(), nb::arg("world").none(), nb::arg("empty").none());
    module.def("viewer_scalar_get_value", &viewer_scalar_get_value, nb::arg("self").none(), nb::arg("position").none(), nb::arg("namespace"), nb::arg("generator_factory").none(), nb::arg("locals_owner"));
    module.def("viewer_scalar_value_ray", &viewer_scalar_value_ray, nb::arg("self").none(), nb::arg("dims_displayed").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("viewer_scalar_value_3d", &viewer_scalar_value_3d, nb::arg("self").none(), nb::arg("start_point").none(), nb::arg("end_point").none(), nb::arg("dims_displayed").none(), nb::arg("locals_owner"));
    module.def("viewer_scalar_offset", &viewer_scalar_offset, nb::arg("self").none(), nb::arg("position").none(), nb::arg("half").none());
    module.def("viewer_image_value_ray", &viewer_image_value_ray, nb::arg("self").none(), nb::arg("values").none(), nb::arg("namespace"), nb::arg("literals"), nb::arg("locals_owner"));
    module.def("viewer_labels_value_ray", &viewer_labels_value_ray, nb::arg("self").none(), nb::arg("values").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("viewer_labels_properties", &viewer_labels_properties, nb::arg("self").none(), nb::arg("position").none(), nb::arg("view_direction").none(), nb::arg("dims_displayed").none(), nb::arg("world").none(), nb::arg("namespace"), nb::arg("literals"), nb::arg("locals_owner"));
    module.def("viewer_labels_status", &viewer_labels_status, nb::arg("self").none(), nb::arg("position").none(), nb::arg("view_direction").none(), nb::arg("dims_displayed").none(), nb::arg("world").none(), nb::arg("namespace"), nb::arg("super_provider").none(), nb::arg("literals"), nb::arg("locals_owner"));
    module.def("viewer_labels_tooltip", &viewer_labels_tooltip, nb::arg("self").none(), nb::arg("position").none(), nb::arg("view_direction").none(), nb::arg("dims_displayed").none(), nb::arg("world").none(), nb::arg("literals"), nb::arg("locals_owner"));
    module.def("viewer_points_value", &viewer_points_value, nb::arg("self").none(), nb::arg("position").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("viewer_points_value_3d", &viewer_points_value_3d, nb::arg("self").none(), nb::arg("start_point").none(), nb::arg("end_point").none(), nb::arg("dims_displayed").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("viewer_points_status", &viewer_points_status, nb::arg("self").none(), nb::arg("position").none(), nb::arg("view_direction").none(), nb::arg("dims_displayed").none(), nb::arg("world").none(), nb::arg("super_provider").none(), nb::arg("literals"), nb::arg("locals_owner"));
    module.def("viewer_points_tooltip", &viewer_points_tooltip, nb::arg("self").none(), nb::arg("position").none(), nb::arg("view_direction").none(), nb::arg("dims_displayed").none(), nb::arg("world").none(), nb::arg("newline").none());
    module.def("viewer_points_properties", &viewer_points_properties, nb::arg("self").none(), nb::arg("position").none(), nb::arg("view_direction").none(), nb::arg("dims_displayed").none(), nb::arg("world").none(), nb::arg("namespace"), nb::arg("literals"), nb::arg("locals_owner"));
    module.def("viewer_points_view_data", &viewer_points_view_data, nb::arg("self").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("viewer_shapes_value", &viewer_shapes_value, nb::arg("self").none(), nb::arg("position").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("viewer_shapes_value_3d", &viewer_shapes_value_3d, nb::arg("self").none(), nb::arg("start_point").none(), nb::arg("end_point").none(), nb::arg("dims_displayed").none(), nb::arg("locals_owner"));
    module.def("viewer_shapes_index_intersection", &viewer_shapes_index_intersection, nb::arg("self").none(), nb::arg("start_point").none(), nb::arg("end_point").none(), nb::arg("dims_displayed").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("viewer_shapes_selected_view", &viewer_shapes_selected_view, nb::arg("self").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("viewer_shapes_normalized_scale", &viewer_shapes_normalized_scale, nb::arg("self").none(), nb::arg("namespace"));
    module.def("viewer_shapes_vertex_radius", &viewer_shapes_vertex_radius, nb::arg("self").none());
    module.def("viewer_shape_list_visible", &viewer_shape_list_visible, nb::arg("self").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("viewer_shape_list_boxes", &viewer_shape_list_boxes, nb::arg("self").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("viewer_shape_list_inside", &viewer_shape_list_inside, nb::arg("self_getter").none(), nb::arg("coord_getter").none(), nb::arg("indices_getter").none(), nb::arg("namespace"), nb::arg("generator_factory").none(), nb::arg("locals_owner"));
    module.def("viewer_shape_list_inside_3d", &viewer_shape_list_inside_3d, nb::arg("self").none(), nb::arg("ray_position").none(), nb::arg("ray_direction").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("viewer_shape_list_triangle_intersection", &viewer_shape_list_triangle_intersection, nb::arg("self").none(), nb::arg("triangle_indices").none(), nb::arg("ray_position").none(), nb::arg("ray_direction").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("viewer_shape_all_triangles", &viewer_shape_all_triangles, nb::arg("self").none(), nb::arg("namespace"));
    module.def("viewer_surface_value_none", &viewer_surface_value_none, nb::arg("self").none(), nb::arg("position").none());
    module.def("viewer_surface_value_3d", &viewer_surface_value_3d, nb::arg("self").none(), nb::arg("start_point").none(), nb::arg("end_point").none(), nb::arg("dims_displayed").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("viewer_vectors_value_none", &viewer_vectors_value_none, nb::arg("self").none(), nb::arg("position").none());
    module.def("viewer_tracks_value", &viewer_tracks_value, nb::arg("self").none(), nb::arg("position").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("viewer_track_manager_value", &viewer_track_manager_value, nb::arg("self").none(), nb::arg("coords").none(), nb::arg("namespace"), nb::arg("locals_owner"));
}

#undef SP_LAYOUT
#undef PF
#undef SP_FIELDS
} // namespace nx
