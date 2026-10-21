#include "arrays.hpp"
#include "collection_protocol.hpp"
#include <nanobind/stl/string.h>
#include <array>
#include <initializer_list>
#include <utility>
#include <vector>

namespace nx {
namespace {

nb::object sg_checked(PyObject* value) {
    if (!value) throw nb::python_error();
    return nb::steal<nb::object>(value);
}
nb::object sg_attr(nb::object value, const char* name) { return sg_checked(PyObject_GetAttrString(value.ptr(), name)); }
nb::object sg_item(nb::object value, nb::object key) { return sg_checked(PyObject_GetItem(value.ptr(), key.ptr())); }
nb::object sg_call0(nb::object fn) { return sg_checked(PyObject_CallNoArgs(fn.ptr())); }
nb::object sg_call1(nb::object fn, nb::object a) { return sg_checked(PyObject_CallOneArg(fn.ptr(), a.ptr())); }
nb::object sg_call2(nb::object fn, nb::object a, nb::object b) { return sg_checked(PyObject_CallFunctionObjArgs(fn.ptr(), a.ptr(), b.ptr(), nullptr)); }
nb::object sg_call3(nb::object fn, nb::object a, nb::object b, nb::object c) { return sg_checked(PyObject_CallFunctionObjArgs(fn.ptr(), a.ptr(), b.ptr(), c.ptr(), nullptr)); }
nb::object sg_kw(nb::object fn, nb::tuple args, nb::dict kwargs) { return sg_checked(PyObject_Call(fn.ptr(), args.ptr(), kwargs.ptr())); }
nb::object sg_add(nb::object a, nb::object b) { return sg_checked(PyNumber_Add(a.ptr(), b.ptr())); }
nb::object sg_sub(nb::object a, nb::object b) { return sg_checked(PyNumber_Subtract(a.ptr(), b.ptr())); }
nb::object sg_mul(nb::object a, nb::object b) { return sg_checked(PyNumber_Multiply(a.ptr(), b.ptr())); }
nb::object sg_div(nb::object a, nb::object b) { return sg_checked(PyNumber_TrueDivide(a.ptr(), b.ptr())); }
nb::object sg_matmul(nb::object a, nb::object b) { return sg_checked(PyNumber_MatrixMultiply(a.ptr(), b.ptr())); }
nb::object sg_neg(nb::object a) { return sg_checked(PyNumber_Negative(a.ptr())); }
nb::object sg_compare(nb::object a, nb::object b, int op) { return sg_checked(PyObject_RichCompare(a.ptr(), b.ptr(), op)); }
bool sg_truth(nb::object a) { int result = PyObject_IsTrue(a.ptr()); if (result < 0) throw nb::python_error(); return result != 0; }
bool sg_test(nb::object a, nb::object b, int op) { return sg_truth(sg_compare(std::move(a), std::move(b), op)); }
bool sg_contains(nb::object a, nb::object b) { int result = PySequence_Contains(a.ptr(), b.ptr()); if (result < 0) throw nb::python_error(); return result != 0; }
nb::object sg_slice() { return sg_checked(PySlice_New(Py_None, Py_None, Py_None)); }
nb::object sg_slice_to(nb::object stop) { return sg_checked(PySlice_New(Py_None, stop.ptr(), Py_None)); }
nb::object sg_slice_from(nb::object start) { return sg_checked(PySlice_New(start.ptr(), Py_None, Py_None)); }
nb::object sg_col(nb::object a, int column) { return sg_item(std::move(a), nb::make_tuple(sg_slice(), nb::int_(column))); }
nb::object sg_vertex(nb::object a, int vertex) { return sg_item(std::move(a), nb::make_tuple(sg_slice(), nb::int_(vertex), sg_slice())); }
nb::object sg_vertex_col(nb::object a, int vertex, int column) { return sg_item(std::move(a), nb::make_tuple(sg_slice(), nb::int_(vertex), nb::int_(column))); }
nb::object sg_optional(const nb::object& a) { return a.is_valid() ? a : nb::none(); }
nb::object sg_next(nb::handle iterator) { PyObject* result = PyIter_Next(iterator.ptr()); if (!result && PyErr_Occurred()) throw nb::python_error(); return nb::steal<nb::object>(result); }

struct StatusGeometryNamespace {
    nb::dict globals;
    nb::object builtins;
    explicit StatusGeometryNamespace(nb::dict ns) : globals(std::move(ns)), builtins(nb::borrow<nb::object>(PyEval_GetBuiltins())) {}
    nb::object get(const char* name) const {
        nb::object key = nb::str(name);
        PyObject* value = PyDict_GetItemWithError(globals.ptr(), key.ptr());
        if (value) return nb::borrow<nb::object>(value);
        if (PyErr_Occurred()) throw nb::python_error();
        value = PyObject_GetItem(builtins.ptr(), key.ptr());
        if (value) return nb::steal<nb::object>(value);
        if (!PyErr_ExceptionMatches(PyExc_KeyError)) throw nb::python_error();
        PyErr_Clear();
        nb::object message = sg_checked(PyUnicode_FromFormat("name '%s' is not defined", name));
        nb::object exception = sg_call1(nb::borrow<nb::object>(PyExc_NameError), std::move(message));
        if (PyObject_SetAttrString(exception.ptr(), "name", key.ptr()) < 0) throw nb::python_error();
        PyErr_SetObject(PyExc_NameError, exception.ptr());
        throw nb::python_error();
    }
    nb::object np(const char* name) const { return sg_attr(get("np"), name); }
    nb::object norm() const { return sg_attr(sg_attr(get("np"), "linalg"), "norm"); }
};

// This is a finite assigned-local owner, never application/model authority.
// The layout gives each complete operation its original local cleanup order.
#define SG_FIELDS(X) \
 X(points) X(plane_point) X(plane_normal) X(point_vector) X(signed_distance_to_plane) X(projected_points) \
 X(vec_1) X(vec_2) X(cross_prod) X(dot_prod) X(s) X(kmat) X(rotation_matrix) X(rotated_points) X(clamped_point) \
 X(axis) X(face_coordinate) X(plane_axis) X(t) X(x_min) X(x_max) X(y_min) X(y_max) X(z_min) X(z_max) X(face_coords) \
 X(AB) X(AC) X(BC) X(s_AB) X(s_AC) X(s_BC) X(inside) X(line_position) X(line_direction) X(plane_position) \
 X(line_plane_direction) X(line_plane_on_plane_normal) X(line_direction_on_plane_normal) X(scale_factor) \
 X(repeated_line_position) X(repeated_line_direction) X(edge_1) X(edge_2) X(triangle_normals) X(intersection_points) \
 X(triangle_vertices) X(in_triangles) X(vertices_plane) X(_) X(rotated_vertices) X(quadrilateral_2D) X(click_pos_2D) \
 X(vertices) X(rotated_vertices_2d) X(rotated_triangles_2d) X(line_pos_2D) X(front_face_normal) X(back_face_normal) \
 X(bbox_face_coords) X(k) X(v) X(front_face_coordinate) X(intersection_point) X(n_intersected_triangles) \
 X(intersected_triangles) X(start_to_intersection) X(distances) X(closest_triangle_index) X(intersected_triangle_indices) \
 X(closest_intersected_triangle_index) X(intersection) X(end_position_view) X(ray_direction) X(start_position) X(end_position) \
 X(vertex_a) X(vertex_b) X(vertex_c) X(v0) X(v1) X(v2) X(d00) X(d01) X(d11) X(d20) X(d21) X(denominator) X(w) X(u) \
 X(luminance) X(all_dims) X(not_in_dims_displayed) X(order) X(offset) X(order_arr) X(n_display_world) X(n_display_layer) X(dims_displayed) \
 X(array) X(point) X(normal)
enum class SGField {
#define SG_ENUM(name) name,
    SG_FIELDS(SG_ENUM)
#undef SG_ENUM
    count
};
struct ViewerStatusGeometryLocals {
#define SG_DECLARE(name) nb::object name;
    SG_FIELDS(SG_DECLARE)
#undef SG_DECLARE
    std::vector<SGField> release_order;
    ViewerStatusGeometryLocals() = default;
    ViewerStatusGeometryLocals(ViewerStatusGeometryLocals&&) = default;
    ~ViewerStatusGeometryLocals() { clear(); }
    void layout(std::initializer_list<SGField> names) { release_order.assign(names); }
    void clear() {
        std::array<nb::object, static_cast<size_t>(SGField::count)> detached = {
#define SG_MOVE(name) std::move(name),
            SG_FIELDS(SG_MOVE)
#undef SG_MOVE
        };
        std::vector<SGField> order_ = std::move(release_order);
        release_order.clear();
        for (SGField name : order_) detached[static_cast<size_t>(name)].reset();
        for (nb::object& value : detached) value.reset();
    }
};
int sg_owner_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) {
        const auto& owner = *nb::inst_ptr<ViewerStatusGeometryLocals>(self);
#define SG_VISIT(name) Py_VISIT(owner.name.ptr());
        SG_FIELDS(SG_VISIT)
#undef SG_VISIT
    }
    return 0;
}
int sg_owner_clear(PyObject* self) { if (nb::inst_ready(self)) nb::inst_ptr<ViewerStatusGeometryLocals>(self)->clear(); return 0; }
PyType_Slot sg_owner_slots[] = {{Py_tp_traverse, reinterpret_cast<void*>(sg_owner_traverse)}, {Py_tp_clear, reinterpret_cast<void*>(sg_owner_clear)}, {0, nullptr}};
#define SG_LAYOUT(owner, ...) owner.layout({__VA_ARGS__})
#define SF(name) SGField::name

struct SGPair { nb::object first, second; };
SGPair sg_pair(nb::object value) {
    PyObject* raw_iterator = PyObject_GetIter(value.ptr());
    if (!raw_iterator) {
        nb::python_error error;
        if (Py_TYPE(value.ptr())->tp_iter == nullptr && !PySequence_Check(value.ptr()) && error.matches(PyExc_TypeError)) {
            PyErr_Format(PyExc_TypeError, "cannot unpack non-iterable %.200s object", Py_TYPE(value.ptr())->tp_name);
            throw nb::python_error();
        }
        throw error;
    }
    nb::object iterator = nb::steal<nb::object>(raw_iterator);
    nb::object first = sg_next(iterator);
    if (!first.is_valid()) { PyErr_SetString(PyExc_ValueError, "not enough values to unpack (expected 2, got 0)"); throw nb::python_error(); }
    nb::object second = sg_next(iterator);
    if (!second.is_valid()) { PyErr_SetString(PyExc_ValueError, "not enough values to unpack (expected 2, got 1)"); throw nb::python_error(); }
    nb::object extra = sg_next(iterator);
    if (extra.is_valid()) { PyErr_SetString(PyExc_ValueError, "too many values to unpack (expected 2)"); throw nb::python_error(); }
    iterator.reset(); value.reset();
    return {std::move(first), std::move(second)};
}
nb::object sg_axis_call(nb::object callback, nb::object value, int axis) {
    nb::tuple arguments = nb::make_tuple(std::move(value));
    nb::dict keywords; keywords["axis"] = nb::int_(axis);
    return sg_kw(std::move(callback), std::move(arguments), std::move(keywords));
}
nb::object sg_norm(nb::object value, const StatusGeometryNamespace& ns) { nb::object fn = ns.norm(); return sg_call1(std::move(fn), std::move(value)); }
nb::object sg_dot(nb::object a, nb::object b, const StatusGeometryNamespace& ns) { nb::object fn = ns.np("dot"); return sg_call2(std::move(fn), std::move(a), std::move(b)); }

nb::object viewer_geometry_project_points(nb::dict namespace_, ViewerStatusGeometryLocals& o) {
    SG_LAYOUT(o, SF(points), SF(plane_point), SF(plane_normal), SF(point_vector), SF(signed_distance_to_plane), SF(projected_points));
    StatusGeometryNamespace ns(std::move(namespace_));
    nb::object fn = ns.np("atleast_2d"); o.points = sg_call1(std::move(fn), o.points);
    fn = ns.np("asarray"); o.plane_point = sg_call1(std::move(fn), o.plane_point);
    fn = ns.np("tile"); nb::object shape = sg_attr(o.points, "shape"); nb::object rows = sg_item(std::move(shape), nb::int_(0));
    nb::object repeat = nb::make_tuple(std::move(rows), nb::int_(1));
    o.plane_normal = sg_call2(std::move(fn), o.plane_normal, std::move(repeat));
    o.point_vector = sg_sub(o.points, o.plane_point);
    fn = ns.np("multiply"); nb::object product = sg_call2(std::move(fn), o.point_vector, o.plane_normal);
    fn = sg_attr(std::move(product), "sum"); nb::dict keywords; keywords["axis"] = nb::int_(1);
    o.signed_distance_to_plane = sg_kw(std::move(fn), nb::tuple(), std::move(keywords));
    nb::object left = o.points;
    nb::object distance = o.signed_distance_to_plane;
    nb::object key = nb::make_tuple(sg_slice(), ns.np("newaxis"));
    nb::object expanded = sg_item(std::move(distance), std::move(key));
    nb::object projected = sg_mul(std::move(expanded), o.plane_normal);
    o.projected_points = sg_sub(std::move(left), std::move(projected));
    return nb::make_tuple(o.projected_points, o.signed_distance_to_plane);
}

nb::object viewer_geometry_rotation_3d(nb::dict namespace_, ViewerStatusGeometryLocals& o) {
    SG_LAYOUT(o, SF(vec_1), SF(vec_2), SF(cross_prod), SF(dot_prod), SF(s), SF(kmat), SF(rotation_matrix));
    StatusGeometryNamespace ns(std::move(namespace_));
    nb::object left = o.vec_1; nb::object normalized = sg_div(std::move(left), sg_norm(o.vec_1, ns));
    nb::object fn = sg_attr(std::move(normalized), "reshape"); o.vec_1 = sg_call1(std::move(fn), nb::int_(3));
    left = o.vec_2; normalized = sg_div(std::move(left), sg_norm(o.vec_2, ns));
    fn = sg_attr(std::move(normalized), "reshape"); o.vec_2 = sg_call1(std::move(fn), nb::int_(3));
    fn = ns.np("cross"); o.cross_prod = sg_call2(std::move(fn), o.vec_1, o.vec_2);
    fn = ns.np("dot"); o.dot_prod = sg_call2(std::move(fn), o.vec_1, o.vec_2);
    fn = ns.get("any");
    if (sg_truth(sg_call1(std::move(fn), o.cross_prod))) {
        o.s = sg_norm(o.cross_prod, ns);
        fn = ns.np("array");
        nb::list row0; row0.append(nb::int_(0)); row0.append(sg_neg(sg_item(o.cross_prod, nb::int_(2)))); row0.append(sg_item(o.cross_prod, nb::int_(1)));
        nb::list row1; row1.append(sg_item(o.cross_prod, nb::int_(2))); row1.append(nb::int_(0)); row1.append(sg_neg(sg_item(o.cross_prod, nb::int_(0))));
        nb::list row2; row2.append(sg_neg(sg_item(o.cross_prod, nb::int_(1)))); row2.append(sg_item(o.cross_prod, nb::int_(0))); row2.append(nb::int_(0));
        nb::list rows; rows.append(std::move(row0)); rows.append(std::move(row1)); rows.append(std::move(row2));
        o.kmat = sg_call1(std::move(fn), std::move(rows));
        fn = ns.np("eye"); nb::object eye = sg_call1(std::move(fn), nb::int_(3));
        nb::object first = sg_add(std::move(eye), o.kmat);
        fn = sg_attr(o.kmat, "dot"); nb::object squared = sg_call1(std::move(fn), o.kmat);
        nb::object numerator = sg_sub(nb::int_(1), o.dot_prod);
        nb::object denominator = sg_checked(PyNumber_Power(o.s.ptr(), nb::int_(2).ptr(), Py_None));
        nb::object factor = sg_div(std::move(numerator), std::move(denominator));
        nb::object correction = sg_mul(std::move(squared), std::move(factor));
        o.rotation_matrix = sg_add(std::move(first), std::move(correction));
    } else {
        fn = ns.np("allclose");
        if (sg_truth(sg_call2(std::move(fn), o.dot_prod, nb::int_(1)))) {
            fn = ns.np("eye"); o.rotation_matrix = sg_call1(std::move(fn), nb::int_(3));
        } else {
            fn = ns.np("diag"); nb::list diagonal; diagonal.append(nb::int_(-1)); diagonal.append(nb::int_(-1)); diagonal.append(nb::int_(1));
            o.rotation_matrix = sg_call1(std::move(fn), std::move(diagonal));
        }
    }
    return o.rotation_matrix;
}

nb::object viewer_geometry_rotate_points(nb::object points, nb::object current_plane_normal, nb::object new_plane_normal,
                                       nb::dict namespace_, ViewerStatusGeometryLocals& o) {
    SG_LAYOUT(o, SF(rotation_matrix), SF(rotated_points)); StatusGeometryNamespace ns(std::move(namespace_));
    nb::object fn = ns.get("rotation_matrix_from_vectors_3d");
    o.rotation_matrix = sg_call2(std::move(fn), std::move(current_plane_normal), std::move(new_plane_normal));
    nb::object matrix = sg_attr(o.rotation_matrix, "T"); o.rotated_points = sg_matmul(std::move(points), std::move(matrix));
    return nb::make_tuple(o.rotated_points, o.rotation_matrix);
}
nb::object viewer_geometry_clamp_point(nb::object point, nb::object bounding_box, nb::dict namespace_, ViewerStatusGeometryLocals& o) {
    SG_LAYOUT(o, SF(clamped_point)); StatusGeometryNamespace ns(std::move(namespace_));
    nb::object fn = ns.np("clip"); nb::object low = sg_col(bounding_box, 0); nb::object high = sg_sub(sg_col(std::move(bounding_box), 1), nb::int_(1));
    o.clamped_point = sg_call3(std::move(fn), std::move(point), std::move(low), std::move(high)); return o.clamped_point;
}
nb::object viewer_geometry_face_coordinate(nb::object bounding_box, nb::object face_normal, nb::dict namespace_, ViewerStatusGeometryLocals& o) {
    SG_LAYOUT(o, SF(axis), SF(face_coordinate)); StatusGeometryNamespace ns(std::move(namespace_));
    nb::object fn = ns.np("argwhere"); o.axis = sg_call1(std::move(fn), face_normal);
    const bool positive = sg_test(sg_item(std::move(face_normal), o.axis), nb::int_(0), Py_GT);
    o.face_coordinate = sg_item(std::move(bounding_box), nb::make_tuple(o.axis, nb::int_(positive ? 1 : 0))); return o.face_coordinate;
}
nb::object viewer_geometry_axis_plane(nb::object plane_intercept, nb::object plane_normal, nb::object line_start, nb::object line_direction,
                                    nb::dict namespace_, ViewerStatusGeometryLocals& o) {
    SG_LAYOUT(o, SF(plane_axis), SF(t)); StatusGeometryNamespace ns(std::move(namespace_));
    nb::object squeeze = ns.np("squeeze"); nb::object argwhere = ns.np("argwhere");
    o.plane_axis = sg_call1(std::move(squeeze), sg_call1(std::move(argwhere), std::move(plane_normal)));
    nb::object numerator = sg_sub(std::move(plane_intercept), sg_item(line_start, o.plane_axis));
    nb::object denominator = sg_item(line_direction, o.plane_axis); o.t = sg_div(std::move(numerator), std::move(denominator));
    nb::object offset = sg_mul(o.t, std::move(line_direction)); return sg_add(std::move(line_start), std::move(offset));
}

nb::object viewer_geometry_bbox_faces(nb::object bounding_box, nb::dict namespace_, nb::tuple face_names, ViewerStatusGeometryLocals& o) {
    SG_LAYOUT(o, SF(x_min), SF(x_max), SF(y_min), SF(y_max), SF(z_min), SF(z_max), SF(face_coords)); StatusGeometryNamespace ns(std::move(namespace_));
    SGPair pair = sg_pair(sg_item(bounding_box, nb::make_tuple(nb::int_(-1), sg_slice()))); o.x_min = std::move(pair.first); o.x_max = std::move(pair.second);
    pair = sg_pair(sg_item(bounding_box, nb::make_tuple(nb::int_(-2), sg_slice()))); o.y_min = std::move(pair.first); o.y_max = std::move(pair.second);
    pair = sg_pair(sg_item(std::move(bounding_box), nb::make_tuple(nb::int_(-3), sg_slice()))); o.z_min = std::move(pair.first); o.z_max = std::move(pair.second);
    // The dict is assigned only after all six value calls succeed.  Earlier
    // keys/value arrays remain expression temporaries if a later call fails.
    const std::array<std::array<int, 3>, 24> vertices = {{{0,0,1},{0,1,1},{1,1,1},{1,0,1}, {0,0,0},{0,1,0},{1,1,0},{1,0,0},
        {0,1,0},{0,1,1},{1,1,1},{1,1,0}, {0,0,0},{0,0,1},{1,0,1},{1,0,0}, {1,0,0},{1,0,1},{1,1,1},{1,1,0}, {0,0,0},{0,0,1},{0,1,1},{0,1,0}}};
    std::array<nb::object, 6> keys, values;
    for (size_t face = 0; face < 6; ++face) {
        keys[face] = nb::borrow<nb::object>(face_names[face]);
        nb::object fn = ns.np("array"); nb::list rows;
        for (size_t row = 0; row < 4; ++row) {
            const auto& indices = vertices[face * 4 + row]; nb::list coordinate;
            coordinate.append(indices[0] ? o.z_max : o.z_min); coordinate.append(indices[1] ? o.y_max : o.y_min); coordinate.append(indices[2] ? o.x_max : o.x_min);
            rows.append(std::move(coordinate));
        }
        values[face] = sg_call1(std::move(fn), std::move(rows));
    }
    nb::dict faces;
    for (size_t face = 0; face < 6; ++face) {
        if (PyDict_SetItem(faces.ptr(), keys[face].ptr(), values[face].ptr()) < 0) throw nb::python_error();
    }
    for (size_t face = 6; face-- > 0;) { values[face].reset(); keys[face].reset(); }
    o.face_coords = std::move(faces); return o.face_coords;
}

nb::object viewer_geometry_inside_triangles(nb::object triangles, nb::dict namespace_, ViewerStatusGeometryLocals& o) {
    SG_LAYOUT(o, SF(AB), SF(AC), SF(BC), SF(s_AB), SF(s_AC), SF(s_BC), SF(inside)); StatusGeometryNamespace ns(std::move(namespace_));
    nb::object first = sg_vertex(triangles, 1); nb::object second = sg_vertex(triangles, 0); o.AB = sg_sub(std::move(first), std::move(second));
    first = sg_vertex(triangles, 2); second = sg_vertex(triangles, 0); o.AC = sg_sub(std::move(first), std::move(second));
    first = sg_vertex(triangles, 2); second = sg_vertex(triangles, 1); o.BC = sg_sub(std::move(first), std::move(second));
    const auto oriented = [&](const nb::object& edge, int vertex) {
        nb::object a = sg_neg(sg_col(edge, 0)); nb::object b = sg_vertex_col(triangles, vertex, 1);
        nb::object ab = sg_mul(std::move(a), std::move(b));
        a = sg_col(edge, 1); b = sg_vertex_col(triangles, vertex, 0);
        nb::object cd = sg_mul(std::move(a), std::move(b));
        return sg_compare(sg_add(std::move(ab), std::move(cd)), nb::int_(0), Py_GE);
    };
    o.s_AB = oriented(o.AB, 0); o.s_AC = oriented(o.AC, 0); o.s_BC = oriented(o.BC, 1);
    nb::object all = ns.np("all"); nb::object array = ns.np("array"); nb::list signs;
    signs.append(sg_compare(o.s_AB, o.s_AC, Py_NE)); signs.append(sg_compare(o.s_AB, o.s_BC, Py_EQ));
    nb::object masks = sg_call1(std::move(array), std::move(signs)); o.inside = sg_axis_call(std::move(all), std::move(masks), 0);
    return o.inside;
}

nb::object viewer_geometry_multiple_planes(nb::dict namespace_, ViewerStatusGeometryLocals& o) {
    SG_LAYOUT(o, SF(line_position), SF(line_direction), SF(plane_position), SF(plane_normal), SF(line_plane_direction), SF(line_plane_on_plane_normal), SF(line_direction_on_plane_normal), SF(scale_factor), SF(repeated_line_position), SF(repeated_line_direction));
    StatusGeometryNamespace ns(std::move(namespace_));
    const auto cast_array = [&](const nb::object& value) {
        nb::object fn = ns.np("asarray"); nb::dict kwargs; kwargs["dtype"] = ns.get("float");
        return sg_kw(std::move(fn), nb::make_tuple(value), std::move(kwargs));
    };
    o.line_position = cast_array(o.line_position); o.line_direction = cast_array(o.line_direction);
    nb::object fn = ns.np("atleast_2d"); nb::object array = sg_call1(std::move(fn), o.plane_position); fn = sg_attr(std::move(array), "astype"); o.plane_position = sg_call1(std::move(fn), ns.get("float"));
    fn = ns.np("atleast_2d"); array = sg_call1(std::move(fn), o.plane_normal); fn = sg_attr(std::move(array), "astype"); o.plane_normal = sg_call1(std::move(fn), ns.get("float"));
    o.line_plane_direction = sg_sub(o.plane_position, o.line_position);
    fn = ns.np("sum"); nb::object product = sg_mul(o.line_plane_direction, o.plane_normal); o.line_plane_on_plane_normal = sg_axis_call(std::move(fn), std::move(product), 1);
    fn = ns.np("sum"); product = sg_mul(o.line_direction, o.plane_normal); o.line_direction_on_plane_normal = sg_axis_call(std::move(fn), std::move(product), 1);
    o.scale_factor = sg_div(o.line_plane_on_plane_normal, o.line_direction_on_plane_normal);
    const auto repeat = [&](const nb::object& value) {
        nb::object callback = ns.np("repeat"); nb::object key = nb::make_tuple(ns.np("newaxis"), sg_slice());
        nb::object expanded = sg_item(value, std::move(key)); nb::object length = ns.get("len"); nb::object n = sg_call1(std::move(length), o.scale_factor);
        nb::tuple args = nb::make_tuple(std::move(expanded), std::move(n)); nb::dict kwargs; kwargs["axis"] = nb::int_(0);
        return sg_kw(std::move(callback), std::move(args), std::move(kwargs));
    };
    o.repeated_line_position = repeat(o.line_position); o.repeated_line_direction = repeat(o.line_direction);
    nb::object left = o.repeated_line_position; fn = ns.np("expand_dims"); nb::object expanded = sg_axis_call(std::move(fn), o.scale_factor, 1);
    nb::object offset = sg_mul(std::move(expanded), o.repeated_line_direction); return sg_add(std::move(left), std::move(offset));
}

nb::object viewer_geometry_triangle_intersections(nb::object line_point, nb::object line_direction, nb::object triangles, nb::dict namespace_, ViewerStatusGeometryLocals& o) {
    SG_LAYOUT(o, SF(edge_1), SF(edge_2), SF(triangle_normals), SF(intersection_points)); StatusGeometryNamespace ns(std::move(namespace_));
    nb::object left = sg_vertex(triangles, 1); nb::object right = sg_vertex(triangles, 0); o.edge_1 = sg_sub(std::move(left), std::move(right));
    left = sg_vertex(triangles, 2); right = sg_vertex(triangles, 0); o.edge_2 = sg_sub(std::move(left), std::move(right));
    nb::object fn = ns.np("cross"); o.triangle_normals = sg_call2(std::move(fn), o.edge_1, o.edge_2);
    left = o.triangle_normals; nb::object expand = ns.np("expand_dims"); fn = ns.norm();
    nb::object norms = sg_axis_call(std::move(fn), o.triangle_normals, 1); right = sg_call2(std::move(expand), std::move(norms), nb::int_(1));
    o.triangle_normals = sg_div(std::move(left), std::move(right));
    fn = ns.get("intersect_line_with_multiple_planes_3d"); nb::dict kwargs;
    kwargs["line_position"] = std::move(line_point); kwargs["line_direction"] = std::move(line_direction);
    kwargs["plane_position"] = sg_vertex(std::move(triangles), 0); kwargs["plane_normal"] = o.triangle_normals;
    o.intersection_points = sg_kw(std::move(fn), nb::tuple(), std::move(kwargs)); return o.intersection_points;
}

nb::object viewer_geometry_point_quadrilateral(nb::object point, nb::object quadrilateral, nb::dict namespace_, ViewerStatusGeometryLocals& o) {
    SG_LAYOUT(o, SF(triangle_vertices), SF(in_triangles)); StatusGeometryNamespace ns(std::move(namespace_));
    nb::object fn = ns.np("stack"); nb::list a; a.append(nb::int_(0)); a.append(nb::int_(1)); a.append(nb::int_(2));
    nb::object first = sg_item(quadrilateral, std::move(a)); nb::list b; b.append(nb::int_(0)); b.append(nb::int_(2)); b.append(nb::int_(3));
    nb::object second = sg_item(std::move(quadrilateral), std::move(b));
    o.triangle_vertices = sg_call1(std::move(fn), nb::make_tuple(std::move(first), std::move(second)));
    fn = ns.get("inside_triangles"); nb::object relative = sg_sub(o.triangle_vertices, std::move(point)); o.in_triangles = sg_call1(std::move(fn), std::move(relative));
    fn = sg_attr(o.in_triangles, "sum"); return sg_compare(sg_call0(std::move(fn)), nb::int_(1), Py_GE);
}

nb::object sg_z_normal(const StatusGeometryNamespace& ns) { nb::object fn = ns.np("array"); nb::list normal; normal.append(nb::int_(0)); normal.append(nb::int_(0)); normal.append(nb::int_(1)); return sg_call1(std::move(fn), std::move(normal)); }
nb::object viewer_geometry_line_quadrilateral(nb::object line_point, nb::object line_direction, nb::object quadrilateral, nb::dict namespace_, ViewerStatusGeometryLocals& o) {
    SG_LAYOUT(o, SF(vertices_plane), SF(_), SF(rotated_vertices), SF(rotation_matrix), SF(quadrilateral_2D), SF(click_pos_2D)); StatusGeometryNamespace ns(std::move(namespace_));
    nb::object fn = ns.get("project_points_onto_plane"); nb::dict kwargs; kwargs["points"] = std::move(quadrilateral); kwargs["plane_point"] = line_point; kwargs["plane_normal"] = line_direction;
    SGPair pair = sg_pair(sg_kw(std::move(fn), nb::tuple(), std::move(kwargs))); o.vertices_plane = std::move(pair.first); o._ = std::move(pair.second);
    fn = ns.get("rotate_points"); kwargs = nb::dict(); kwargs["points"] = o.vertices_plane; kwargs["current_plane_normal"] = std::move(line_direction); kwargs["new_plane_normal"] = sg_z_normal(ns);
    pair = sg_pair(sg_kw(std::move(fn), nb::tuple(), std::move(kwargs))); o.rotated_vertices = std::move(pair.first); o.rotation_matrix = std::move(pair.second);
    o.quadrilateral_2D = sg_item(o.rotated_vertices, nb::make_tuple(sg_slice(), sg_slice_to(nb::int_(2))));
    fn = sg_attr(o.rotation_matrix, "dot"); nb::object clicked = sg_call1(std::move(fn), std::move(line_point)); o.click_pos_2D = sg_item(std::move(clicked), sg_slice_to(nb::int_(2)));
    fn = ns.get("point_in_quadrilateral_2d"); return sg_call2(std::move(fn), o.click_pos_2D, o.quadrilateral_2D);
}

nb::object viewer_geometry_line_triangles(nb::object line_point, nb::object line_direction, nb::object triangles, nb::dict namespace_, ViewerStatusGeometryLocals& o) {
    SG_LAYOUT(o, SF(vertices), SF(vertices_plane), SF(_), SF(rotation_matrix), SF(rotated_vertices), SF(rotated_vertices_2d), SF(rotated_triangles_2d), SF(line_pos_2D)); StatusGeometryNamespace ns(std::move(namespace_));
    nb::object fn = sg_attr(triangles, "reshape"); nb::object shape = sg_attr(std::move(triangles), "shape"); nb::object last = sg_item(std::move(shape), nb::int_(2));
    o.vertices = sg_call1(std::move(fn), nb::make_tuple(nb::int_(-1), std::move(last)));
    fn = ns.get("project_points_onto_plane"); nb::dict kwargs; kwargs["points"] = o.vertices; kwargs["plane_point"] = line_point; kwargs["plane_normal"] = line_direction;
    SGPair pair = sg_pair(sg_kw(std::move(fn), nb::tuple(), std::move(kwargs))); o.vertices_plane = std::move(pair.first); o._ = std::move(pair.second);
    fn = ns.get("rotation_matrix_from_vectors_3d"); nb::object normal = sg_z_normal(ns); o.rotation_matrix = sg_call2(std::move(fn), std::move(line_direction), std::move(normal));
    nb::object matrix = sg_attr(o.rotation_matrix, "T"); o.rotated_vertices = sg_matmul(o.vertices_plane, std::move(matrix));
    o.rotated_vertices_2d = sg_item(o.rotated_vertices, nb::make_tuple(sg_slice(), sg_slice_to(nb::int_(2))));
    fn = sg_attr(o.rotated_vertices_2d, "reshape"); o.rotated_triangles_2d = sg_call3(std::move(fn), nb::int_(-1), nb::int_(3), nb::int_(2));
    fn = sg_attr(o.rotation_matrix, "dot"); nb::object projected = sg_call1(std::move(fn), std::move(line_point)); o.line_pos_2D = sg_item(std::move(projected), sg_slice_to(nb::int_(2)));
    fn = ns.get("inside_triangles"); nb::object relative = sg_sub(o.rotated_triangles_2d, o.line_pos_2D); return sg_call1(std::move(fn), std::move(relative));
}

nb::object viewer_geometry_front_back_face(nb::object click_pos, nb::object bounding_box, nb::object view_dir, nb::dict namespace_, ViewerStatusGeometryLocals& o) {
    SG_LAYOUT(o, SF(front_face_normal), SF(back_face_normal), SF(bbox_face_coords), SF(k), SF(v), SF(dot_prod)); StatusGeometryNamespace ns(std::move(namespace_));
    o.front_face_normal = nb::none(); o.back_face_normal = nb::none();
    nb::object fn = ns.get("bounding_box_to_face_vertices"); o.bbox_face_coords = sg_call1(std::move(fn), std::move(bounding_box));
    fn = sg_attr(ns.get("FACE_NORMALS"), "items"); nb::object items = sg_call0(std::move(fn)); nb::object iterator = sg_checked(PyObject_GetIter(items.ptr())); items.reset();
    for (;;) {
        nb::object item = sg_next(iterator); if (!item.is_valid()) break;
        SGPair pair = sg_pair(std::move(item)); o.k = std::move(pair.first); o.v = std::move(pair.second);
        fn = ns.np("dot"); o.dot_prod = sg_call2(std::move(fn), view_dir, o.v);
        fn = ns.get("line_in_quadrilateral_3d"); nb::object face = sg_item(o.bbox_face_coords, o.k);
        if (sg_truth(sg_call3(std::move(fn), click_pos, view_dir, std::move(face)))) {
            if (sg_test(o.dot_prod, nb::int_(0), Py_LT)) o.front_face_normal = o.v; else o.back_face_normal = o.v;
        }
        if (!o.front_face_normal.is_none() && !o.back_face_normal.is_none()) break;
    }
    iterator.reset(); return nb::make_tuple(o.front_face_normal, o.back_face_normal);
}

nb::object viewer_geometry_bbox_intersection(nb::object line_point, nb::object line_direction, nb::object bounding_box, nb::object face_normal,
                                           nb::dict namespace_, ViewerStatusGeometryLocals& o) {
    SG_LAYOUT(o, SF(front_face_coordinate), SF(intersection_point)); StatusGeometryNamespace ns(std::move(namespace_));
    nb::object fn = ns.get("face_coordinate_from_bounding_box"); o.front_face_coordinate = sg_call2(std::move(fn), std::move(bounding_box), face_normal);
    nb::object squeeze = ns.np("squeeze"); fn = ns.get("intersect_line_with_axis_aligned_plane"); nb::object negative = sg_neg(std::move(line_direction));
    nb::tuple args = nb::make_tuple(o.front_face_coordinate, std::move(face_normal), std::move(line_point), std::move(negative));
    nb::object intersection = sg_checked(PyObject_CallObject(fn.ptr(), args.ptr())); args.reset(); fn.reset();
    o.intersection_point = sg_call1(std::move(squeeze), std::move(intersection)); return o.intersection_point;
}

nb::object viewer_geometry_nearest_triangle(nb::object ray_position, nb::object ray_direction, nb::object triangles, nb::dict namespace_, ViewerStatusGeometryLocals& o) {
    SG_LAYOUT(o, SF(inside), SF(n_intersected_triangles), SF(intersected_triangles), SF(intersection_points), SF(start_to_intersection), SF(distances), SF(closest_triangle_index), SF(intersected_triangle_indices), SF(closest_intersected_triangle_index), SF(intersection)); StatusGeometryNamespace ns(std::move(namespace_));
    nb::object fn = ns.get("line_in_triangles_3d"); nb::dict kwargs; kwargs["line_point"] = ray_position; kwargs["line_direction"] = ray_direction; kwargs["triangles"] = triangles;
    o.inside = sg_kw(std::move(fn), nb::tuple(), std::move(kwargs));
    fn = ns.np("sum"); o.n_intersected_triangles = sg_call1(std::move(fn), o.inside);
    if (sg_test(o.n_intersected_triangles, nb::int_(0), Py_EQ)) return nb::make_tuple(nb::none(), nb::none());
    o.intersected_triangles = sg_item(std::move(triangles), o.inside);
    fn = ns.get("intersect_line_with_triangles"); kwargs = nb::dict(); kwargs["line_point"] = ray_position; kwargs["line_direction"] = std::move(ray_direction); kwargs["triangles"] = o.intersected_triangles;
    o.intersection_points = sg_kw(std::move(fn), nb::tuple(), std::move(kwargs));
    o.start_to_intersection = sg_sub(o.intersection_points, std::move(ray_position));
    fn = ns.norm(); o.distances = sg_axis_call(std::move(fn), o.start_to_intersection, 1);
    fn = ns.np("argmin"); o.closest_triangle_index = sg_call1(std::move(fn), o.distances);
    fn = ns.np("argwhere"); o.intersected_triangle_indices = sg_call1(std::move(fn), o.inside);
    nb::object selected = sg_item(o.intersected_triangle_indices, o.closest_triangle_index); o.closest_intersected_triangle_index = sg_item(std::move(selected), nb::int_(0));
    o.intersection = sg_item(o.intersection_points, o.closest_triangle_index); return nb::make_tuple(o.closest_intersected_triangle_index, o.intersection);
}

nb::object viewer_geometry_displayed_plane(nb::object start_point, nb::object end_point, nb::object dims_displayed, nb::dict namespace_, ViewerStatusGeometryLocals& o) {
    SG_LAYOUT(o, SF(plane_point), SF(end_position_view), SF(ray_direction), SF(plane_normal)); StatusGeometryNamespace ns(std::move(namespace_));
    o.plane_point = sg_item(std::move(start_point), dims_displayed); o.end_position_view = sg_item(std::move(end_point), std::move(dims_displayed));
    o.ray_direction = sg_sub(o.end_position_view, o.plane_point); nb::object left = o.ray_direction;
    o.plane_normal = sg_div(std::move(left), sg_norm(o.ray_direction, ns)); return nb::make_tuple(o.plane_point, o.plane_normal);
}
nb::object viewer_geometry_displayed_ray(nb::object start_point, nb::object end_point, nb::object dims_displayed, nb::dict namespace_, nb::object backward_step, ViewerStatusGeometryLocals& o) {
    SG_LAYOUT(o, SF(start_position), SF(end_position), SF(ray_direction)); StatusGeometryNamespace ns(std::move(namespace_));
    o.start_position = sg_item(std::move(start_point), dims_displayed); o.end_position = sg_item(std::move(end_point), std::move(dims_displayed));
    o.ray_direction = sg_sub(o.end_position, o.start_position); nb::object left = o.ray_direction;
    o.ray_direction = sg_div(std::move(left), sg_norm(o.ray_direction, ns)); left = o.start_position;
    nb::object offset = sg_mul(std::move(backward_step), o.ray_direction); o.start_position = sg_sub(std::move(left), std::move(offset)); return nb::make_tuple(o.start_position, o.ray_direction);
}

nb::object viewer_geometry_barycentric(nb::object point, nb::object triangle_vertices, nb::dict namespace_, ViewerStatusGeometryLocals& o) {
    SG_LAYOUT(o, SF(vertex_a), SF(vertex_b), SF(vertex_c), SF(v0), SF(v1), SF(v2), SF(d00), SF(d01), SF(d11), SF(d20), SF(d21), SF(denominator), SF(v), SF(w), SF(u)); StatusGeometryNamespace ns(std::move(namespace_));
    o.vertex_a = sg_item(triangle_vertices, nb::make_tuple(nb::int_(0), sg_slice()));
    o.vertex_b = sg_item(triangle_vertices, nb::make_tuple(nb::int_(1), sg_slice()));
    o.vertex_c = sg_item(std::move(triangle_vertices), nb::make_tuple(nb::int_(2), sg_slice()));
    o.v0 = sg_sub(o.vertex_b, o.vertex_a); o.v1 = sg_sub(o.vertex_c, o.vertex_a); o.v2 = sg_sub(std::move(point), o.vertex_a);
    o.d00 = sg_dot(o.v0, o.v0, ns); o.d01 = sg_dot(o.v0, o.v1, ns); o.d11 = sg_dot(o.v1, o.v1, ns); o.d20 = sg_dot(o.v2, o.v0, ns); o.d21 = sg_dot(o.v2, o.v1, ns);
    nb::object a = sg_mul(o.d00, o.d11); nb::object b = sg_mul(o.d01, o.d01); o.denominator = sg_sub(std::move(a), std::move(b));
    a = sg_mul(o.d11, o.d20); b = sg_mul(o.d01, o.d21); nb::object numerator = sg_sub(std::move(a), std::move(b)); o.v = sg_div(std::move(numerator), o.denominator);
    a = sg_mul(o.d00, o.d21); b = sg_mul(o.d01, o.d20); numerator = sg_sub(std::move(a), std::move(b)); o.w = sg_div(std::move(numerator), o.denominator);
    a = sg_sub(nb::int_(1), o.v); o.u = sg_sub(std::move(a), o.w); nb::object fn = ns.np("array"); nb::list coordinates; coordinates.append(o.u); coordinates.append(o.v); coordinates.append(o.w);
    return sg_call1(std::move(fn), std::move(coordinates));
}
nb::object viewer_geometry_rgb_luminance(nb::object rgb, nb::dict namespace_, nb::tuple coefficients, nb::object error_message, ViewerStatusGeometryLocals& o) {
    SG_LAYOUT(o, SF(luminance)); StatusGeometryNamespace ns(std::move(namespace_));
    const auto coefficient_array = [&]() { nb::object fn = ns.np("array"); nb::list values; for (nb::handle value : coefficients) values.append(value); nb::dict kwargs; kwargs["dtype"] = ns.np("float32"); return sg_kw(std::move(fn), nb::make_tuple(std::move(values)), std::move(kwargs)); };
    if (sg_test(sg_item(sg_attr(rgb, "shape"), nb::int_(-1)), nb::int_(3), Py_EQ)) return sg_matmul(std::move(rgb), coefficient_array());
    if (sg_test(sg_item(sg_attr(rgb, "shape"), nb::int_(-1)), nb::int_(4), Py_EQ)) {
        nb::object color = sg_item(rgb, nb::make_tuple(nb::ellipsis(), sg_slice_to(nb::int_(3)))); nb::object coefficients_ = coefficient_array(); o.luminance = sg_matmul(std::move(color), std::move(coefficients_));
        nb::object alpha = sg_item(std::move(rgb), nb::make_tuple(nb::ellipsis(), nb::int_(3))); return sg_mul(o.luminance, std::move(alpha));
    }
    nb::object type = ns.get("ValueError"); nb::object exception = sg_call1(std::move(type), std::move(error_message));
    if (PyExceptionClass_Check(exception.ptr())) {
        nb::object klass = std::move(exception); exception = sg_call0(klass);
        if (!PyExceptionInstance_Check(exception.ptr())) {
            PyErr_Format(PyExc_TypeError, "calling %R should have returned an instance of BaseException, not %R", klass.ptr(), reinterpret_cast<PyObject*>(Py_TYPE(exception.ptr())));
            throw nb::python_error();
        }
    }
    if (!PyExceptionInstance_Check(exception.ptr())) { PyErr_SetString(PyExc_TypeError, "exceptions must derive from BaseException"); throw nb::python_error(); }
    PyErr_SetObject(reinterpret_cast<PyObject*>(Py_TYPE(exception.ptr())), exception.ptr()); throw nb::python_error();
}

nb::object viewer_geometry_world_dims(nb::object dims_displayed_world, nb::object ndim_world, nb::object ndim_layer,
                                     nb::dict namespace_, ViewerStatusGeometryLocals& o) {
    SG_LAYOUT(o, SF(all_dims), SF(not_in_dims_displayed), SF(order), SF(offset), SF(order_arr), SF(n_display_world), SF(n_display_layer), SF(dims_displayed));
    StatusGeometryNamespace ns(std::move(namespace_));
    nb::object world = ndim_world;
    nb::object length = ns.get("len"); nb::object n = sg_call1(std::move(length), dims_displayed_world);
    if (sg_test(std::move(world), std::move(n), Py_GT)) {
        nb::object list = ns.get("list"); nb::object range = ns.get("range");
        nb::object sequence = sg_call1(std::move(range), ndim_world); o.all_dims = sg_call1(std::move(list), std::move(sequence));
        nb::list not_displayed;
        nb::object iterator = sg_checked(PyObject_GetIter(o.all_dims.ptr()));
        nb::object d;
        try {
            for (;;) {
                nb::object next = sg_next(iterator); if (!next.is_valid()) break; d = std::move(next);
                if (!sg_contains(dims_displayed_world, d)) not_displayed.append(d);
            }
        } catch (...) { iterator.reset(); not_displayed.reset(); d.reset(); throw; }
        iterator.reset(); d.reset(); o.not_in_dims_displayed = std::move(not_displayed);
        o.order = sg_add(o.not_in_dims_displayed, dims_displayed_world);
    } else o.order = dims_displayed_world;
    o.offset = sg_sub(std::move(ndim_world), ndim_layer);
    nb::object array = ns.np("array"); o.order_arr = sg_call1(std::move(array), o.order);
    if (sg_test(o.offset, nb::int_(0), Py_LE)) {
        nb::object list = ns.get("list"); nb::object range = ns.get("range"); nb::object negative = sg_neg(o.offset);
        nb::object sequence = sg_call1(std::move(range), std::move(negative)); nb::object first = sg_call1(std::move(list), std::move(sequence));
        list = ns.get("list"); nb::object adjusted = sg_sub(o.order_arr, o.offset); nb::object second = sg_call1(std::move(list), std::move(adjusted));
        o.order = sg_add(std::move(first), std::move(second));
    } else {
        nb::object list = ns.get("list"); nb::object target = o.order_arr; nb::object mask = sg_compare(o.order_arr, o.offset, Py_GE);
        nb::object selected = sg_item(std::move(target), std::move(mask)); nb::object adjusted = sg_sub(std::move(selected), o.offset);
        o.order = sg_call1(std::move(list), std::move(adjusted));
    }
    length = ns.get("len"); o.n_display_world = sg_call1(std::move(length), std::move(dims_displayed_world));
    if (sg_test(o.n_display_world, ndim_layer, Py_GT)) o.n_display_layer = std::move(ndim_layer); else o.n_display_layer = o.n_display_world;
    nb::object negative = sg_neg(o.n_display_layer); nb::object key = sg_slice_from(std::move(negative)); o.dims_displayed = sg_item(o.order, std::move(key)); return o.dims_displayed;
}

// The adapter's actual any(genexpr) remains a genuine Python generator via a
// pure yield-from provider.  This native iterator owns only that generator's
// source .0, current a, executing globals/builtins and protocol state.
struct ViewerProjectionDtypeIterator {
    nb::object iterator, a, globals, builtins;
    bool started = false, running = false, closed = false;
    ViewerProjectionDtypeIterator(nb::object source_iterator, nb::dict namespace_, nb::object builtin_mapping)
        : iterator(std::move(source_iterator)), globals(std::move(namespace_)), builtins(std::move(builtin_mapping)) {}
    ViewerProjectionDtypeIterator(ViewerProjectionDtypeIterator&&) = default;
    ~ViewerProjectionDtypeIterator() { clear(); }
    void clear() {
        std::array<nb::object, 4> detached = {std::move(iterator), std::move(a), std::move(globals), std::move(builtins)};
        closed = true; running = false;
        for (nb::object& value : detached) value.reset();
    }
    nb::object real_types() {
        nb::object key = nb::str("_REAL_DTYPES");
        PyObject* value = PyDict_GetItemWithError(globals.ptr(), key.ptr());
        if (value) return nb::borrow<nb::object>(value);
        if (PyErr_Occurred()) throw nb::python_error();
        value = PyObject_GetItem(builtins.ptr(), key.ptr());
        if (value) return nb::steal<nb::object>(value);
        if (!PyErr_ExceptionMatches(PyExc_KeyError)) throw nb::python_error();
        PyErr_Clear();
        nb::object message = nb::str("name '_REAL_DTYPES' is not defined");
        nb::object exception = sg_call1(nb::borrow<nb::object>(PyExc_NameError), std::move(message));
        if (PyObject_SetAttrString(exception.ptr(), "name", key.ptr()) < 0) throw nb::python_error();
        PyErr_SetObject(PyExc_NameError, exception.ptr()); throw nb::python_error();
    }
    nb::object next() {
        if (closed) { PyErr_SetNone(PyExc_StopIteration); throw nb::python_error(); }
        if (running) { PyErr_SetString(PyExc_ValueError, "generator already executing"); throw nb::python_error(); }
        running = true; started = true;
        try {
            nb::object value = sg_next(iterator);
            if (!value.is_valid()) { clear(); PyErr_SetNone(PyExc_StopIteration); throw nb::python_error(); }
            a = std::move(value);
            nb::object dtype = sg_attr(a, "dtype"); nb::object types = real_types();
            const bool unsupported = !sg_contains(std::move(types), std::move(dtype));
            running = false; return nb::bool_(unsupported);
        } catch (const nb::python_error& error) {
            running = false;
            if (closed) throw;
            closed = true;
            // Failed native frame locals must survive through the real
            // generator bridge traceback; already-closed close() cannot
            // erase this saved-error lease.
            collections::raise_exception(error.value(), true);
        }
    }
    nb::object send(nb::object value) {
        if (closed) { PyErr_SetNone(PyExc_StopIteration); throw nb::python_error(); }
        if (!started && !value.is_none()) { PyErr_SetString(PyExc_TypeError, "can't send non-None value to a just-started generator"); throw nb::python_error(); }
        return next();
    }
    void close() {
        if (running) { PyErr_SetString(PyExc_ValueError, "generator already executing"); throw nb::python_error(); }
        if (!closed) clear();
    }
    nb::object throw_(nb::args arguments) {
        nb::object exception = collections::normalize_throw(arguments);
        if (running) { PyErr_SetString(PyExc_ValueError, "generator already executing"); throw nb::python_error(); }
        const bool exhausted = closed;
        if (!closed) { started = true; closed = true; }
        collections::raise_exception(exception, !exhausted);
    }
};
int sg_iterator_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) {
        const auto& o = *nb::inst_ptr<ViewerProjectionDtypeIterator>(self);
        Py_VISIT(o.iterator.ptr()); Py_VISIT(o.a.ptr()); Py_VISIT(o.globals.ptr()); Py_VISIT(o.builtins.ptr());
    }
    return 0;
}
int sg_iterator_clear(PyObject* self) { if (nb::inst_ready(self)) nb::inst_ptr<ViewerProjectionDtypeIterator>(self)->clear(); return 0; }
PyType_Slot sg_iterator_slots[] = {{Py_tp_traverse, reinterpret_cast<void*>(sg_iterator_traverse)}, {Py_tp_clear, reinterpret_cast<void*>(sg_iterator_clear)}, {0, nullptr}};
nb::object sg_projection_generator(nb::object generator_factory, nb::dict namespace_, nb::object builtins,
                                   nb::object array, nb::object point, nb::object normal) {
    nb::tuple values = nb::make_tuple(std::move(array), std::move(point), std::move(normal));
    nb::object iterator = sg_checked(PyObject_GetIter(values.ptr())); values.reset();
    nb::object native_iterator = nb::cast(ViewerProjectionDtypeIterator(std::move(iterator), std::move(namespace_), std::move(builtins)));
    nb::object generator = sg_call1(std::move(generator_factory), std::move(native_iterator));
    if (PyObject_SetAttrString(generator.ptr(), "__name__", nb::str("<genexpr>").ptr()) < 0) throw nb::python_error();
    if (PyObject_SetAttrString(generator.ptr(), "__qualname__", nb::str("projection_adapter.<locals>.call.<locals>.<genexpr>").ptr()) < 0) throw nb::python_error();
    return generator;
}
nb::object viewer_geometry_projection_adapter_factory(nb::object original, nb::dict namespace_, nb::object call_factory) {
    StatusGeometryNamespace ns(std::move(namespace_));
    nb::object wraps = ns.get("wraps"); nb::object decorator = sg_call1(std::move(wraps), std::move(original));
    return sg_call1(std::move(call_factory), std::move(decorator));
}
nb::object viewer_geometry_projection_adapter_call(nb::object original_getter, nb::object points, nb::object plane_point, nb::object plane_normal,
                                                 nb::dict namespace_, nb::object generator_factory, nb::tuple expected_shape,
                                                 ViewerStatusGeometryLocals& o) {
    SG_LAYOUT(o, SF(array), SF(point), SF(normal)); StatusGeometryNamespace ns(namespace_);
    nb::object map = ns.get("map"); nb::object asarray = ns.np("asarray");
    nb::tuple values = nb::make_tuple(points, plane_point, plane_normal);
    nb::object mapped = sg_call2(std::move(map), std::move(asarray), std::move(values));
    // UNPACK_SEQUENCE 3 validates all elements before any local assignment.
    PyObject* raw_iterator = PyObject_GetIter(mapped.ptr());
    if (!raw_iterator) {
        nb::python_error error;
        if (Py_TYPE(mapped.ptr())->tp_iter == nullptr && !PySequence_Check(mapped.ptr()) && error.matches(PyExc_TypeError)) {
            PyErr_Format(PyExc_TypeError, "cannot unpack non-iterable %.200s object", Py_TYPE(mapped.ptr())->tp_name);
            throw nb::python_error();
        }
        throw error;
    }
    nb::object iterator = nb::steal<nb::object>(raw_iterator);
    nb::object array = sg_next(iterator); if (!array.is_valid()) { PyErr_SetString(PyExc_ValueError, "not enough values to unpack (expected 3, got 0)"); throw nb::python_error(); }
    nb::object point = sg_next(iterator); if (!point.is_valid()) { PyErr_SetString(PyExc_ValueError, "not enough values to unpack (expected 3, got 1)"); throw nb::python_error(); }
    nb::object normal = sg_next(iterator); if (!normal.is_valid()) { PyErr_SetString(PyExc_ValueError, "not enough values to unpack (expected 3, got 2)"); throw nb::python_error(); }
    nb::object extra = sg_next(iterator); if (extra.is_valid()) { PyErr_SetString(PyExc_ValueError, "too many values to unpack (expected 3)"); throw nb::python_error(); }
    iterator.reset(); mapped.reset();
    o.array = std::move(array); o.point = std::move(point); o.normal = std::move(normal);
    nb::object ndim = sg_attr(o.array, "ndim");
    bool unsupported = !sg_contains(nb::make_tuple(nb::int_(1), nb::int_(2)), std::move(ndim));
    if (!unsupported) unsupported = sg_test(sg_item(sg_attr(o.array, "shape"), nb::int_(-1)), nb::int_(3), Py_NE);
    if (!unsupported) unsupported = sg_test(sg_attr(o.point, "shape"), expected_shape, Py_NE);
    if (!unsupported) unsupported = sg_test(sg_attr(o.normal, "shape"), expected_shape, Py_NE);
    if (!unsupported) {
        nb::object any = ns.get("any");
        nb::object generator = sg_projection_generator(std::move(generator_factory), namespace_, ns.builtins, o.array, o.point, o.normal);
        unsupported = sg_truth(sg_call1(std::move(any), std::move(generator)));
    }
    if (!unsupported) {
        nb::object fn = ns.np("result_type"); nb::object a = sg_attr(o.array, "dtype"); nb::object b = sg_attr(o.point, "dtype");
        nb::object result = sg_call2(std::move(fn), std::move(a), std::move(b)); nb::object float64 = ns.np("float64");
        unsupported = sg_test(std::move(result), std::move(float64), Py_NE);
    }
    if (!unsupported) {
        nb::object fn = ns.np("result_type"); nb::object a = sg_attr(o.array, "dtype"); nb::object b = sg_attr(o.point, "dtype"); nb::object c = sg_attr(o.normal, "dtype");
        nb::object result = sg_call3(std::move(fn), std::move(a), std::move(b), std::move(c)); nb::object float64 = ns.np("float64");
        unsupported = sg_test(std::move(result), std::move(float64), Py_NE);
    }
    if (unsupported) {
        nb::object original = sg_call0(std::move(original_getter));
        return sg_call3(std::move(original), std::move(points), std::move(plane_point), std::move(plane_normal));
    }
    nb::object kernel = sg_attr(ns.get("_native"), "project_points_onto_plane");
    nb::object double_ = ns.get("_double"); nb::object a = sg_call1(std::move(double_), o.array);
    double_ = ns.get("_double"); nb::object b = sg_call1(std::move(double_), o.point);
    double_ = ns.get("_double"); nb::object c = sg_call1(std::move(double_), o.normal);
    return sg_call3(std::move(kernel), std::move(a), std::move(b), std::move(c));
}

} // namespace

void bind_viewer_status_geometry(nb::module_& module) {
    nb::class_<ViewerStatusGeometryLocals>(module, "ViewerStatusGeometryLocals", nb::is_weak_referenceable(), nb::type_slots(sg_owner_slots))
#define SG_PROPERTY(name) .def_prop_ro(#name, [](const ViewerStatusGeometryLocals& o) { return sg_optional(o.name); })
        SG_FIELDS(SG_PROPERTY)
#undef SG_PROPERTY
        ;
    nb::class_<ViewerProjectionDtypeIterator>(module, "ViewerProjectionDtypeIterator", nb::is_weak_referenceable(), nb::type_slots(sg_iterator_slots))
        .def("__iter__", [](nb::object self) { return self; })
        .def("__next__", &ViewerProjectionDtypeIterator::next)
        .def("send", &ViewerProjectionDtypeIterator::send, nb::arg("value").none())
        .def("throw", &ViewerProjectionDtypeIterator::throw_)
        .def("close", &ViewerProjectionDtypeIterator::close);
    module.def("viewer_geometry_locals", [] { return ViewerStatusGeometryLocals{}; });
    module.def("viewer_geometry_projection_locals", [](nb::object points, nb::object plane_point, nb::object plane_normal) {
        ViewerStatusGeometryLocals o; o.points = std::move(points); o.plane_point = std::move(plane_point); o.plane_normal = std::move(plane_normal);
        SG_LAYOUT(o, SF(points), SF(plane_point), SF(plane_normal)); return o;
    }, nb::arg("points").none(), nb::arg("plane_point").none(), nb::arg("plane_normal").none());
    module.def("viewer_geometry_rotation_locals", [](nb::object vec_1, nb::object vec_2) {
        ViewerStatusGeometryLocals o; o.vec_1 = std::move(vec_1); o.vec_2 = std::move(vec_2); SG_LAYOUT(o, SF(vec_1), SF(vec_2)); return o;
    }, nb::arg("vec_1").none(), nb::arg("vec_2").none());
    module.def("viewer_geometry_planes_locals", [](nb::object line_position, nb::object line_direction, nb::object plane_position, nb::object plane_normal) {
        ViewerStatusGeometryLocals o; o.line_position = std::move(line_position); o.line_direction = std::move(line_direction); o.plane_position = std::move(plane_position); o.plane_normal = std::move(plane_normal);
        SG_LAYOUT(o, SF(line_position), SF(line_direction), SF(plane_position), SF(plane_normal)); return o;
    }, nb::arg("line_position").none(), nb::arg("line_direction").none(), nb::arg("plane_position").none(), nb::arg("plane_normal").none());
    module.def("viewer_geometry_project_points", &viewer_geometry_project_points, nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("viewer_geometry_rotation_3d", &viewer_geometry_rotation_3d, nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("viewer_geometry_rotate_points", &viewer_geometry_rotate_points, nb::arg("points").none(), nb::arg("current_plane_normal").none(), nb::arg("new_plane_normal").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("viewer_geometry_clamp_point", &viewer_geometry_clamp_point, nb::arg("point").none(), nb::arg("bounding_box").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("viewer_geometry_face_coordinate", &viewer_geometry_face_coordinate, nb::arg("bounding_box").none(), nb::arg("face_normal").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("viewer_geometry_axis_plane", &viewer_geometry_axis_plane, nb::arg("plane_intercept").none(), nb::arg("plane_normal").none(), nb::arg("line_start").none(), nb::arg("line_direction").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("viewer_geometry_bbox_faces", &viewer_geometry_bbox_faces, nb::arg("bounding_box").none(), nb::arg("namespace"), nb::arg("face_names"), nb::arg("locals_owner"));
    module.def("viewer_geometry_inside_triangles", &viewer_geometry_inside_triangles, nb::arg("triangles").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("viewer_geometry_multiple_planes", &viewer_geometry_multiple_planes, nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("viewer_geometry_triangle_intersections", &viewer_geometry_triangle_intersections, nb::arg("line_point").none(), nb::arg("line_direction").none(), nb::arg("triangles").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("viewer_geometry_point_quadrilateral", &viewer_geometry_point_quadrilateral, nb::arg("point").none(), nb::arg("quadrilateral").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("viewer_geometry_line_quadrilateral", &viewer_geometry_line_quadrilateral, nb::arg("line_point").none(), nb::arg("line_direction").none(), nb::arg("quadrilateral").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("viewer_geometry_line_triangles", &viewer_geometry_line_triangles, nb::arg("line_point").none(), nb::arg("line_direction").none(), nb::arg("triangles").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("viewer_geometry_front_back_face", &viewer_geometry_front_back_face, nb::arg("click_pos").none(), nb::arg("bounding_box").none(), nb::arg("view_dir").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("viewer_geometry_bbox_intersection", &viewer_geometry_bbox_intersection, nb::arg("line_point").none(), nb::arg("line_direction").none(), nb::arg("bounding_box").none(), nb::arg("face_normal").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("viewer_geometry_nearest_triangle", &viewer_geometry_nearest_triangle, nb::arg("ray_position").none(), nb::arg("ray_direction").none(), nb::arg("triangles").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("viewer_geometry_displayed_plane", &viewer_geometry_displayed_plane, nb::arg("start_point").none(), nb::arg("end_point").none(), nb::arg("dims_displayed").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("viewer_geometry_displayed_ray", &viewer_geometry_displayed_ray, nb::arg("start_point").none(), nb::arg("end_point").none(), nb::arg("dims_displayed").none(), nb::arg("namespace"), nb::arg("backward_step").none(), nb::arg("locals_owner"));
    module.def("viewer_geometry_barycentric", &viewer_geometry_barycentric, nb::arg("point").none(), nb::arg("triangle_vertices").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("viewer_geometry_rgb_luminance", &viewer_geometry_rgb_luminance, nb::arg("rgb").none(), nb::arg("namespace"), nb::arg("coefficients"), nb::arg("error_message").none(), nb::arg("locals_owner"));
    module.def("viewer_geometry_world_dims", &viewer_geometry_world_dims, nb::arg("dims_displayed_world").none(), nb::arg("ndim_world").none(), nb::arg("ndim_layer").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("viewer_geometry_projection_adapter_factory", &viewer_geometry_projection_adapter_factory, nb::arg("original").none(), nb::arg("namespace"), nb::arg("call_factory").none());
    module.def("viewer_geometry_projection_adapter_call", &viewer_geometry_projection_adapter_call, nb::arg("original_getter").none(), nb::arg("points").none(), nb::arg("plane_point").none(), nb::arg("plane_normal").none(), nb::arg("namespace"), nb::arg("generator_factory").none(), nb::arg("expected_shape"), nb::arg("locals_owner"));
}

#undef SG_LAYOUT
#undef SF
#undef SG_FIELDS
} // namespace nx
