#include "arrays.hpp"
#include "collection_protocol.hpp"
#include "event_protocol.hpp"
#include <nanobind/stl/string.h>
#include <array>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <structmember.h>
#include <utility>
#include <vector>

namespace nx {
namespace {
nb::object gg_check(PyObject* p) { if (!p) throw nb::python_error(); return nb::steal<nb::object>(p); }
nb::object gg_attr(nb::object a, const char* n) { return gg_check(PyObject_GetAttrString(a.ptr(), n)); }
nb::object gg_item(nb::object a, nb::object k) { return gg_check(PyObject_GetItem(a.ptr(), k.ptr())); }
void gg_store(nb::object a, const char* n, nb::object v) { if (PyObject_SetAttrString(a.ptr(), n, v.ptr()) < 0) throw nb::python_error(); }
void gg_set(nb::object a, nb::object k, nb::object v) { if (PyObject_SetItem(a.ptr(), k.ptr(), v.ptr()) < 0) throw nb::python_error(); }
void gg_assign(nb::object& a, nb::object v) { nb::object old = std::move(a); a = std::move(v); old.reset(); }
bool gg_truth(nb::object a) { int r = PyObject_IsTrue(a.ptr()); if (r < 0) throw nb::python_error(); return r != 0; }
nb::object gg_iter(nb::object a) { return gg_check(PyObject_GetIter(a.ptr())); }
nb::object gg_next(nb::handle a) { PyObject* p = PyIter_Next(a.ptr()); if (!p && PyErr_Occurred()) throw nb::python_error(); return nb::steal<nb::object>(p); }
using GGExpr = std::function<nb::object()>;
using GGKw = std::pair<const char*, GGExpr>;
nb::object gg_call(nb::object fn, std::initializer_list<GGExpr> positional = {}, std::initializer_list<GGKw> keywords = {}) {
    nb::tuple args = nb::steal<nb::tuple>(PyTuple_New(static_cast<Py_ssize_t>(positional.size())));
    if (!args.is_valid()) throw nb::python_error(); Py_ssize_t i = 0;
    for (const auto& f : positional) { nb::object v = f(); PyTuple_SET_ITEM(args.ptr(), i++, v.release().ptr()); }
    nb::dict kwargs; for (const auto& kv : keywords) kwargs[kv.first] = kv.second();
    return gg_check(PyObject_Call(fn.ptr(), args.ptr(), keywords.size() ? kwargs.ptr() : nullptr));
}
nb::tuple gg_tuple(std::initializer_list<GGExpr> values) {
    nb::tuple out = nb::steal<nb::tuple>(PyTuple_New(static_cast<Py_ssize_t>(values.size())));
    if (!out.is_valid()) throw nb::python_error(); Py_ssize_t i = 0;
    for (const auto& f : values) { nb::object v = f(); PyTuple_SET_ITEM(out.ptr(), i++, v.release().ptr()); } return out;
}
nb::list gg_list(std::initializer_list<GGExpr> values) { nb::list out; for (const auto& f : values) out.append(f()); return out; }
nb::object gg_binary(GGExpr a, GGExpr b, binaryfunc fn) { nb::object left = a(); nb::object right = b(); return gg_check(fn(left.ptr(), right.ptr())); }
nb::object gg_subscript(GGExpr a, GGExpr b) { nb::object left = a(); nb::object key = b(); return gg_item(std::move(left),std::move(key)); }
void gg_publish(GGExpr value, GGExpr target, const char* name) { nb::object v = value(); nb::object t = target(); gg_store(std::move(t),name,std::move(v)); }
void gg_publish_item(GGExpr value, GGExpr target, GGExpr key) { nb::object v = value(); nb::object t = target(); nb::object k = key(); gg_set(std::move(t),std::move(k),std::move(v)); }
nb::object gg_compare(GGExpr a, GGExpr b, int op) { nb::object left = a(); nb::object right = b(); return gg_check(PyObject_RichCompare(left.ptr(), right.ptr(), op)); }
nb::object gg_slice(GGExpr start, GGExpr stop, GGExpr step) { nb::object a = start(); nb::object b = stop(); nb::object c = step(); return gg_check(PySlice_New(a.ptr(), b.ptr(), c.ptr())); }
#define SX(expr) [&]() -> nb::object { return (expr); }
#define SA(a,b) gg_binary(SX(a),SX(b),PyNumber_Add)
#define SB(a,b) gg_binary(SX(a),SX(b),PyNumber_Subtract)
#define SM(a,b) gg_binary(SX(a),SX(b),PyNumber_Multiply)
#define SI(a,b) gg_binary(SX(a),SX(b),PyNumber_InPlaceAdd)
#define SC(a,b,op) gg_compare(SX(a),SX(b),op)
#define SK(a,b) gg_subscript(SX(a),SX(b))
#define STORE(a,n,v) gg_publish(SX(v),SX(a),n)
#define SET(a,k,v) gg_publish_item(SX(v),SX(a),SX(k))
#define ST(...) gg_tuple({__VA_ARGS__})
#define SL(...) gg_list({__VA_ARGS__})
#define SS(a,b) gg_slice(SX(a),SX(b),SX(nb::none()))
[[noreturn]] void gg_raise(nb::object value) {
    if (PyExceptionClass_Check(value.ptr())) { nb::object cls = std::move(value); value = gg_call(cls);
        if (!PyExceptionInstance_Check(value.ptr())) { PyErr_Format(PyExc_TypeError, "calling %R should have returned an instance of BaseException, not %R", cls.ptr(), reinterpret_cast<PyObject*>(Py_TYPE(value.ptr()))); throw nb::python_error(); }
    }
    if (PyExceptionInstance_Check(value.ptr())) PyErr_SetObject(reinterpret_cast<PyObject*>(Py_TYPE(value.ptr())), value.ptr());
    else PyErr_SetString(PyExc_TypeError, "exceptions must derive from BaseException"); throw nb::python_error();
}
struct GeometryNamespace {
    nb::dict values; nb::object builtins;
    explicit GeometryNamespace(nb::dict v) : values(std::move(v)), builtins(nb::borrow<nb::object>(PyEval_GetBuiltins())) {}
    GeometryNamespace(nb::dict v, nb::object b) : values(std::move(v)), builtins(std::move(b)) {}
    nb::object get(const char* name) const {
        nb::object key = nb::str(name); PyObject* p = PyDict_GetItemWithError(values.ptr(), key.ptr());
        if (p) return nb::borrow<nb::object>(p); if (PyErr_Occurred()) throw nb::python_error();
        p = PyObject_GetItem(builtins.ptr(), key.ptr()); if (p) return nb::steal<nb::object>(p);
        if (!PyErr_ExceptionMatches(PyExc_KeyError)) throw nb::python_error(); PyErr_Clear();
        nb::object text = gg_check(PyUnicode_FromFormat("name '%s' is not defined", name));
        nb::object error = gg_call(nb::borrow<nb::object>(PyExc_NameError), {SX(text)}); gg_store(error, "name", key); gg_raise(error);
    }
    nb::object np(const char* name) const { return gg_attr(get("np"), name); }
};
#define SN(fn,...) gg_call(ns.get(fn),{__VA_ARGS__})
#define NP(fn,...) gg_call(ns.np(fn),{__VA_ARGS__})
#define NK(fn,args,...) gg_call(ns.np(fn),args,{__VA_ARGS__})
#define KW(name,expr) GGKw{name,SX(expr)}
#define CALL(obj,name,...) gg_call(gg_attr((obj),name),{__VA_ARGS__})


#define GEOMETRY_FIELDS(X) \
 X(points) X(ndim) X(axis_idx) X(values) X(poly) X(vertices) X(triangles) X(boxes) X(AB) X(AM) X(BC) X(BM) X(ABAM) X(ABAB) X(BCBM) X(BCBC) X(c1) X(c2) X(c3) X(c4) X(inside) X(corners) X(vertices_inside) X(edge_intersects) X(intersects) X(box) X(i) X(below_top) X(above_bottom) X(all_intersects) X(p1) X(q1) X(j) X(p2) X(q2) X(p1_arr) X(q1_arr) X(o1) X(o2) X(o3) X(o4) X(p) X(q) X(r) X(point) X(lines) X(lines_vectors) X(point_vectors) X(end_point_vectors) X(norm_lines) X(reject) X(unit_lines) X(line_dist) X(line_loc) X(index) X(location) X(data) X(min_val) X(max_val) X(tl) X(tr) X(br) X(bl) X(center) X(radii) X(num_segments) X(adjusted) X(ax1) X(ax2) X(transform) X(v2d) X(theta) X(centers) X(in_poly) X(axis) X(value) X(new_vertices) X(polygon_vertices) X(triangulate_face_) X(data2d) X(face_tri) X(edge_tri) X(raw_vertices) X(edges) X(face_tri_) X(polygon_list) X(centers_list) X(offset_list) X(triangles_list) X(offset_idx) X(polygon) X(offset) X(tri) X(e) X(path) X(text_path) X(res) X(raw_triangles) X(closed) X(idx) X(clean_path) X(offsets) X(tube_points) X(_tangents) X(normals) X(binormals) X(segments) X(grid) X(grid_off) X(pos) X(normal) X(binormal) X(v) X(cx) X(cy) X(indices) X(ip) X(jp) X(index_a) X(index_b) X(index_c) X(index_d) X(mask_shape) X(mask) X(duplicates) X(iis) X(jjs) X(v1) X(v2) X(ii) X(jj) X(shape) X(x) X(y) X(n_verts) X(cond_1) X(cond_2) X(cond_3) X(d) X(tolerance) X(cond_4) X(cond_5) X(shape_type) X(datum) X(current_type) X(default) X(first_type) X(n_shapes) X(min_vertices) X(valid_vertices) X(line_start) X(line_end) X(t) X(epsilon) X(max_distance_index) X(max_distance) X(l1) X(l2) X(target_dir) X(backend) X(binary_file) X(text_file)
enum class GeometryField {
#define GG_ENUM(n) f_##n,
 GEOMETRY_FIELDS(GG_ENUM)
#undef GG_ENUM
 count_fields
};
constexpr size_t GeometryFieldCount=static_cast<size_t>(GeometryField::count_fields);
const char* geometry_field_names[]={
#define GG_NAME(n) #n,
 GEOMETRY_FIELDS(GG_NAME)
#undef GG_NAME
};
struct GeometryLocals {
    std::array<nb::object,GeometryFieldCount> fields;std::array<bool,GeometryFieldCount> cells{};std::vector<GeometryField> order;
    nb::object propagated_context_exception,propagated_context_traceback;
    GeometryLocals()=default;GeometryLocals(GeometryLocals&&)=default;~GeometryLocals(){clear();}
    nb::object read(GeometryField f) const {size_t i=static_cast<size_t>(f);if(!fields[i].is_valid()){PyErr_Format(PyExc_UnboundLocalError,"cannot access local variable '%s' where it is not associated with a value",geometry_field_names[i]);throw nb::python_error();}
        if(!cells[i])return fields[i];PyObject* value=PyCell_GET(fields[i].ptr());if(!value){PyErr_Format(PyExc_UnboundLocalError,"cannot access local variable '%s' where it is not associated with a value",geometry_field_names[i]);throw nb::python_error();}return nb::borrow<nb::object>(value);
    }
    void assign(GeometryField f,nb::object value){size_t i=static_cast<size_t>(f);if(cells[i]){if(PyCell_Set(fields[i].ptr(),value.ptr())<0)throw nb::python_error();}else gg_assign(fields[i],std::move(value));}
    void clear(){auto old=std::move(fields);auto error=std::move(propagated_context_exception);auto traceback=std::move(propagated_context_traceback);auto layout=std::move(order);order.clear();error.reset();traceback.reset();for(auto f:layout)old[static_cast<size_t>(f)].reset();for(auto& value:old)value.reset();}
};
GeometryField gg_field(nb::handle key){std::string name=nb::cast<std::string>(key);for(size_t i=0;i<GeometryFieldCount;++i)if(name==geometry_field_names[i])return static_cast<GeometryField>(i);PyErr_Format(PyExc_ValueError,"unknown original shape geometry local %s",name.c_str());throw nb::python_error();}
GeometryLocals shape_geometry_locals(nb::tuple layout,nb::dict bindings,nb::dict cells){GeometryLocals o;for(auto key:layout)o.order.push_back(gg_field(key));for(auto item:bindings)o.assign(gg_field(item.first),nb::borrow<nb::object>(item.second));for(auto item:cells){if(!PyCell_Check(item.second.ptr())){PyErr_SetString(PyExc_TypeError,"source closure metadata must be a genuine cell");throw nb::python_error();}size_t i=static_cast<size_t>(gg_field(item.first));o.fields[i]=nb::borrow<nb::object>(item.second);o.cells[i]=true;}return o;}
int gg_owner_traverse(PyObject* self,visitproc visit,void* arg){Py_VISIT(Py_TYPE(self));if(nb::inst_ready(self)){auto& o=*nb::inst_ptr<GeometryLocals>(self);for(auto& value:o.fields)Py_VISIT(value.ptr());Py_VISIT(o.propagated_context_exception.ptr());Py_VISIT(o.propagated_context_traceback.ptr());}return 0;}
int gg_owner_clear(PyObject* self){if(nb::inst_ready(self))nb::inst_ptr<GeometryLocals>(self)->clear();return 0;}
PyType_Slot gg_owner_slots[]={{Py_tp_traverse,reinterpret_cast<void*>(gg_owner_traverse)},{Py_tp_clear,reinterpret_cast<void*>(gg_owner_clear)},{0,nullptr}};
#define G(n) o.read(GeometryField::f_##n)
#define PUT(n,value) o.assign(GeometryField::f_##n,(value))
#define GEOMETRY_DEF(name) nb::object shape_geometry_##name(nb::dict globals,nb::tuple literals,GeometryLocals& o)
#define GEOMETRY_GEN_DEF(name) nb::object shape_geometry_##name(nb::dict globals,nb::tuple literals,nb::object generator_factory,GeometryLocals& o)
#define GG_BEGIN GeometryNamespace ns(std::move(globals))
nb::object gg_literal(const nb::tuple& literals,Py_ssize_t i){PyObject* value=PyTuple_GetItem(literals.ptr(),i);if(!value)throw nb::python_error();return nb::borrow<nb::object>(value);}
#define MC(i) gg_literal(literals,i)
#define SD(a,b) gg_binary(SX(a),SX(b),PyNumber_TrueDivide)
#define SAND(a,b) gg_binary(SX(a),SX(b),PyNumber_And)
#define SOR(a,b) gg_binary(SX(a),SX(b),PyNumber_Or)
#define SREM(a,b) gg_binary(SX(a),SX(b),PyNumber_Remainder)
#define SINPLACEOR(a,b) gg_binary(SX(a),SX(b),PyNumber_InPlaceOr)
nb::object gg_negative(nb::object a){return gg_check(PyNumber_Negative(a.ptr()));}
nb::object gg_invert(nb::object a){return gg_check(PyNumber_Invert(a.ptr()));}
bool gg_in(GGExpr value,GGExpr collection){nb::object v=value();nb::object c=collection();int result=PySequence_Contains(c.ptr(),v.ptr());if(result<0)throw nb::python_error();return result!=0;}
std::vector<nb::object> gg_unpack(nb::object value,size_t count){PyObject* raw=PyObject_GetIter(value.ptr());if(!raw){if(PyErr_ExceptionMatches(PyExc_TypeError)&&!Py_TYPE(value.ptr())->tp_iter&&!PySequence_Check(value.ptr()))PyErr_Format(PyExc_TypeError,"cannot unpack non-iterable %.200s object",Py_TYPE(value.ptr())->tp_name);throw nb::python_error();}nb::object iterator=nb::steal<nb::object>(raw);std::vector<nb::object> out;for(size_t i=0;i<count;++i){nb::object item=gg_next(iterator);if(!item.is_valid()){PyErr_Format(PyExc_ValueError,"not enough values to unpack (expected %zu, got %zu)",count,i);throw nb::python_error();}out.push_back(std::move(item));}nb::object extra=gg_next(iterator);if(extra.is_valid()){extra.reset();PyErr_Format(PyExc_ValueError,"too many values to unpack (expected %zu)",count);throw nb::python_error();}iterator.reset();value.reset();return out;}
nb::object gg_format(nb::object value){return gg_check(PyObject_Format(value.ptr(),nullptr));}
nb::object gg_string(std::initializer_list<GGExpr> expressions){nb::list parts;for(auto& value:expressions)parts.append(value());return gg_check(PyUnicode_Join(nb::str("").ptr(),parts.ptr()));}
void gg_except_type(nb::object type){bool valid=PyExceptionClass_Check(type.ptr());if(PyTuple_Check(type.ptr())){valid=true;for(Py_ssize_t i=0;i<PyTuple_GET_SIZE(type.ptr());++i)if(!PyExceptionClass_Check(PyTuple_GET_ITEM(type.ptr(),i))){valid=false;break;}}if(!valid){PyErr_SetString(PyExc_TypeError,"catching classes that do not inherit from BaseException is not allowed");throw nb::python_error();}}
[[noreturn]]void gg_raise_cause(nb::object error,nb::object cause){if(PyExceptionClass_Check(error.ptr())){nb::object cls=std::move(error);error=gg_call(cls);if(!PyExceptionInstance_Check(error.ptr())){PyErr_Format(PyExc_TypeError,"calling %R should have returned an instance of BaseException, not %R",cls.ptr(),reinterpret_cast<PyObject*>(Py_TYPE(error.ptr())));throw nb::python_error();}}if(!PyExceptionInstance_Check(error.ptr())){PyErr_SetString(PyExc_TypeError,"exceptions must derive from BaseException");throw nb::python_error();}PyException_SetCause(error.ptr(),cause.inc_ref().ptr());gg_raise(std::move(error));}

GEOMETRY_DEF(find_planar_axis){GG_BEGIN;PUT(ndim,SK(gg_attr(G(points),"shape"),MC(1)));if(gg_truth(SC(G(ndim),MC(2),Py_EQ)))return ST(SX(G(points)),SX(MC(3)),SX(MC(3)));
    nb::object iterator=gg_iter(SN("range",SX(G(ndim))));while(nb::object v=gg_next(iterator)){PUT(axis_idx,std::move(v));PUT(values,NP("unique",SX(SK(G(points),ST(SX(SS(MC(3),MC(3))),SX(G(axis_idx)))))));if(gg_truth(SC(SN("len",SX(G(values))),MC(1),Py_EQ)))return ST(SX(gg_call(ns.np("delete"),{SX(G(points)),SX(G(axis_idx))},{KW("axis",MC(1))})),SX(G(axis_idx)),SX(SK(G(values),MC(5))));}iterator.reset();return ST(SX(gg_call(ns.np("empty"),{SX(MC(6))},{KW("dtype",gg_attr(G(points),"dtype"))})),SX(MC(3)),SX(MC(3)));}
GEOMETRY_DEF(fan_triangulation){GG_BEGIN;PUT(vertices,NP("copy",SX(G(poly))));PUT(triangles,gg_call(ns.np("zeros"),{SX(ST(SX(SB(SN("len",SX(G(poly))),MC(1))),SX(MC(2))))},{KW("dtype",gg_attr(ns.get("np"),"uint32"))}));SET(G(triangles),ST(SX(SS(MC(5),MC(5))),SX(MC(4))),NP("arange",SX(MC(4)),SX(SB(SN("len",SX(G(poly))),MC(4)))));SET(G(triangles),ST(SX(SS(MC(5),MC(5))),SX(MC(1))),NP("arange",SX(MC(1)),SX(SN("len",SX(G(poly))))));return ST(SX(G(vertices)),SX(G(triangles)));}
GEOMETRY_DEF(inside_boxes){GG_BEGIN;
    PUT(AB,SB(SK(G(boxes),ST(SX(SS(MC(1),MC(1))),SX(MC(2)))),SK(G(boxes),ST(SX(SS(MC(1),MC(1))),SX(MC(3))))));PUT(AM,SK(G(boxes),ST(SX(SS(MC(1),MC(1))),SX(MC(2)))));PUT(BC,SB(SK(G(boxes),ST(SX(SS(MC(1),MC(1))),SX(MC(3)))),SK(G(boxes),ST(SX(SS(MC(1),MC(1))),SX(MC(4))))));PUT(BM,SK(G(boxes),ST(SX(SS(MC(1),MC(1))),SX(MC(3)))));
    PUT(ABAM,CALL(NP("multiply",SX(G(AB)),SX(G(AM))),"sum",SX(MC(5))));PUT(ABAB,CALL(NP("multiply",SX(G(AB)),SX(G(AB))),"sum",SX(MC(5))));PUT(BCBM,CALL(NP("multiply",SX(G(BC)),SX(G(BM))),"sum",SX(MC(5))));PUT(BCBC,CALL(NP("multiply",SX(G(BC)),SX(G(BC))),"sum",SX(MC(5))));PUT(c1,SC(G(ABAM),MC(2),Py_GE));PUT(c2,SC(G(ABAM),G(ABAB),Py_LE));PUT(c3,SC(G(BCBM),MC(2),Py_GE));PUT(c4,SC(G(BCBM),G(BCBC),Py_LE));PUT(inside,gg_call(ns.np("all"),{SX(NP("array",SX(SL(SX(G(c1)),SX(G(c2)),SX(G(c3)),SX(G(c4))))))},{KW("axis",MC(2))}));return G(inside);}
GEOMETRY_DEF(triangles_intersect_box){GG_BEGIN;PUT(vertices_inside,SN("triangle_vertices_inside_box",SX(G(triangles)),SX(G(corners))));PUT(edge_intersects,SN("triangle_edges_intersect_box",SX(G(triangles)),SX(G(corners))));PUT(intersects,NP("logical_or",SX(G(vertices_inside)),SX(G(edge_intersects))));return G(intersects);}
GEOMETRY_DEF(triangle_vertices_inside_box){GG_BEGIN;PUT(box,SK(SN("create_box",SX(G(corners))),SL(SX(MC(1)),SX(MC(2)))));PUT(vertices_inside,gg_call(ns.np("empty"),{SX(SK(gg_attr(G(triangles),"shape"),SS(MC(3),MC(4))))},{KW("dtype",ns.get("bool"))}));
    nb::object iterator=gg_iter(SN("range",SX(MC(6))));while(nb::object value=gg_next(iterator)){PUT(i,std::move(value));PUT(below_top,gg_call(ns.np("all"),{SX(SC(SK(G(box),MC(7)),SK(G(triangles),ST(SX(SS(MC(3),MC(3))),SX(G(i)),SX(SS(MC(3),MC(3))))),Py_GE))},{KW("axis",MC(7))}));PUT(above_bottom,gg_call(ns.np("all"),{SX(SC(SK(G(triangles),ST(SX(SS(MC(3),MC(3))),SX(G(i)),SX(SS(MC(3),MC(3))))),SK(G(box),MC(1)),Py_GE))},{KW("axis",MC(7))}));SET(G(vertices_inside),ST(SX(SS(MC(3),MC(3))),SX(G(i))),NP("logical_and",SX(G(below_top)),SX(G(above_bottom))));}iterator.reset();PUT(inside,gg_call(ns.np("any"),{SX(G(vertices_inside))},{KW("axis",MC(7))}));return G(inside);}
GEOMETRY_DEF(triangle_edges_intersect_box){GG_BEGIN;PUT(box,SK(SN("create_box",SX(G(corners))),SL(SX(SK(MC(1),nb::int_(0))),SX(SK(MC(1),nb::int_(1))),SX(SK(MC(1),nb::int_(2))),SX(SK(MC(1),nb::int_(3))))));PUT(all_intersects,gg_call(ns.np("zeros"),{SX(SN("len",SX(G(triangles))))},{KW("dtype",ns.get("bool"))}));
    nb::object outer=gg_iter(SN("range",SX(MC(3))));while(nb::object value=gg_next(outer)){PUT(i,std::move(value));PUT(p1,SK(G(triangles),ST(SX(SS(MC(4),MC(4))),SX(G(i)),SX(SS(MC(4),MC(4))))));PUT(q1,SK(G(triangles),ST(SX(SS(MC(4),MC(4))),SX(SREM(SA(G(i),MC(5)),MC(3))),SX(SS(MC(4),MC(4))))));nb::object inner=gg_iter(SN("range",SX(MC(6))));while(nb::object item=gg_next(inner)){PUT(j,std::move(item));PUT(p2,SK(G(box),G(j)));PUT(q2,SK(G(box),SREM(SA(G(j),MC(5)),MC(6))));PUT(intersects,SN("vectorized_lines_intersect",SX(G(p1)),SX(G(q1)),SX(G(p2)),SX(G(q2))));PUT(all_intersects,SINPLACEOR(G(all_intersects),G(intersects)));}inner.reset();}outer.reset();return G(all_intersects);}
GEOMETRY_DEF(lines_intersect){GG_BEGIN;PUT(p1_arr,NP("array",SX(SL(SX(G(p1))))));PUT(q1_arr,NP("array",SX(SL(SX(G(q1))))));return SK(SN("vectorized_lines_intersect",SX(G(p1_arr)),SX(G(q1_arr)),SX(G(p2)),SX(G(q2))),MC(1));}
GEOMETRY_DEF(vectorized_lines_intersect){GG_BEGIN;PUT(o1,CALL(ns.get("_triangulate_py"),"vectorized_orientation",SX(G(p1)),SX(G(q1)),SX(G(p2))));PUT(o2,CALL(ns.get("_triangulate_py"),"vectorized_orientation",SX(G(p1)),SX(G(q1)),SX(G(q2))));PUT(o3,CALL(ns.get("_triangulate_py"),"vectorized_orientation",SX(G(p2)),SX(G(q2)),SX(G(p1))));PUT(o4,CALL(ns.get("_triangulate_py"),"vectorized_orientation",SX(G(p2)),SX(G(q2)),SX(G(q1))));PUT(intersects,SAND(SC(G(o1),G(o2),Py_NE),SC(G(o3),G(o4),Py_NE)));
    PUT(intersects,SINPLACEOR(G(intersects),SAND(SC(G(o1),MC(1),Py_EQ),SN("on_segment",SX(G(p1)),SX(G(p2)),SX(G(q1))))));PUT(intersects,SINPLACEOR(G(intersects),SAND(SC(G(o2),MC(1),Py_EQ),SN("on_segment",SX(G(p1)),SX(G(q2)),SX(G(q1))))));PUT(intersects,SINPLACEOR(G(intersects),SAND(SC(G(o3),MC(1),Py_EQ),SN("on_segment",SX(G(p2)),SX(G(p1)),SX(G(q2))))));PUT(intersects,SINPLACEOR(G(intersects),SAND(SC(G(o4),MC(1),Py_EQ),SN("on_segment",SX(G(p2)),SX(G(q1)),SX(G(q2))))));return G(intersects);}
GEOMETRY_DEF(on_segment){GG_BEGIN;return SAND(SAND(SAND(SC(SK(G(q),MC(1)),NP("maximum",SX(SK(G(p),MC(1))),SX(SK(G(r),MC(1)))),Py_LE),SC(SK(G(q),MC(1)),NP("minimum",SX(SK(G(p),MC(1))),SX(SK(G(r),MC(1)))),Py_GE)),SC(SK(G(q),MC(2)),NP("maximum",SX(SK(G(p),MC(2))),SX(SK(G(r),MC(2)))),Py_LE)),SC(SK(G(q),MC(2)),NP("minimum",SX(SK(G(p),MC(2))),SX(SK(G(r),MC(2)))),Py_GE));}
GEOMETRY_DEF(point_to_lines){GG_BEGIN;PUT(lines_vectors,SB(SK(G(lines),ST(SX(SS(MC(1),MC(1))),SX(MC(2)))),SK(G(lines),ST(SX(SS(MC(1),MC(1))),SX(MC(3))))));PUT(point_vectors,SB(G(point),SK(G(lines),ST(SX(SS(MC(1),MC(1))),SX(MC(3))))));PUT(end_point_vectors,SB(G(point),SK(G(lines),ST(SX(SS(MC(1),MC(1))),SX(MC(2))))));
    PUT(norm_lines,gg_call(gg_attr(gg_attr(ns.get("np"),"linalg"),"norm"),{SX(G(lines_vectors))},{KW("axis",MC(2)),KW("keepdims",MC(4))}));PUT(reject,CALL(SC(G(norm_lines),MC(3),Py_EQ),"squeeze"));SET(G(norm_lines),G(reject),MC(2));PUT(unit_lines,SD(G(lines_vectors),G(norm_lines)));
    PUT(line_dist,SN("abs",SX(SB(SM(SK(G(unit_lines),MC(6)),SK(G(point_vectors),MC(7))),SM(SK(G(unit_lines),MC(7)),SK(G(point_vectors),MC(6)))))));PUT(line_loc,SD(gg_call(gg_attr(SM(G(unit_lines),G(point_vectors)),"sum"),{},{KW("axis",MC(2))}),CALL(G(norm_lines),"squeeze")));
    SET(G(line_dist),SC(G(line_loc),MC(3),Py_LT),gg_call(gg_attr(gg_attr(ns.get("np"),"linalg"),"norm"),{SX(SK(G(point_vectors),SC(G(line_loc),MC(3),Py_LT)))},{KW("axis",MC(2))}));SET(G(line_dist),SC(G(line_loc),MC(2),Py_GT),gg_call(gg_attr(gg_attr(ns.get("np"),"linalg"),"norm"),{SX(SK(G(end_point_vectors),SC(G(line_loc),MC(2),Py_GT)))},{KW("axis",MC(2))}));SET(G(line_dist),G(reject),gg_call(gg_attr(gg_attr(ns.get("np"),"linalg"),"norm"),{SX(SK(G(point_vectors),G(reject)))},{KW("axis",MC(2))}));SET(G(line_loc),G(reject),MC(9));PUT(index,NP("argmin",SX(G(line_dist))));PUT(location,SK(G(line_loc),G(index)));return ST(SX(G(index)),SX(G(location)));}
GEOMETRY_DEF(create_box){GG_BEGIN;PUT(min_val,SL(SX(gg_call(gg_attr(SK(G(data),ST(SX(SS(MC(1),MC(1))),SX(MC(2)))),"min"),{},{KW("axis",MC(2))})),SX(gg_call(gg_attr(SK(G(data),ST(SX(SS(MC(1),MC(1))),SX(MC(4)))),"min"),{},{KW("axis",MC(2))}))));PUT(max_val,SL(SX(gg_call(gg_attr(SK(G(data),ST(SX(SS(MC(1),MC(1))),SX(MC(2)))),"max"),{},{KW("axis",MC(2))})),SX(gg_call(gg_attr(SK(G(data),ST(SX(SS(MC(1),MC(1))),SX(MC(4)))),"max"),{},{KW("axis",MC(2))}))));
    PUT(tl,NP("array",SX(SL(SX(SK(G(min_val),MC(2))),SX(SK(G(min_val),MC(4)))))));PUT(tr,NP("array",SX(SL(SX(SK(G(max_val),MC(2))),SX(SK(G(min_val),MC(4)))))));PUT(br,NP("array",SX(SL(SX(SK(G(max_val),MC(2))),SX(SK(G(max_val),MC(4)))))));PUT(bl,NP("array",SX(SL(SX(SK(G(min_val),MC(2))),SX(SK(G(max_val),MC(4)))))));
    PUT(box,NP("array",SX(SL(SX(G(tl)),SX(SD(SA(G(tl),G(tr)),MC(5))),SX(G(tr)),SX(SD(SA(G(tr),G(br)),MC(5))),SX(G(br)),SX(SD(SA(G(br),G(bl)),MC(5))),SX(G(bl)),SX(SD(SA(G(bl),G(tl)),MC(5))),SX(SD(SA(SA(SA(G(tl),G(tr)),G(br)),G(bl)),MC(6)))))));return G(box);}
GEOMETRY_DEF(rectangle_to_box){GG_BEGIN;if(gg_truth(SC(SK(gg_attr(G(data),"shape"),MC(1)),MC(2),Py_NE)))gg_raise(SN("ValueError",SX(MC(3))));PUT(box,NP("array",SX(SL(SX(SK(G(data),MC(1))),SX(SD(SA(SK(G(data),MC(1)),SK(G(data),MC(4))),MC(5))),SX(SK(G(data),MC(4))),SX(SD(SA(SK(G(data),MC(4)),SK(G(data),MC(5))),MC(5))),SX(SK(G(data),MC(5))),SX(SD(SA(SK(G(data),MC(5)),SK(G(data),MC(6))),MC(5))),SX(SK(G(data),MC(6))),SX(SD(SA(SK(G(data),MC(6)),SK(G(data),MC(1))),MC(5))),SX(gg_call(gg_attr(G(data),"mean"),{},{KW("axis",MC(1))}))))));return G(box);}
GEOMETRY_DEF(find_corners){GG_BEGIN;PUT(min_val,gg_call(gg_attr(G(data),"min"),{},{KW("axis",MC(1))}));PUT(max_val,gg_call(gg_attr(G(data),"max"),{},{KW("axis",MC(1))}));PUT(tl,NP("array",SX(SL(SX(SK(G(min_val),MC(1))),SX(SK(G(min_val),MC(3)))))));PUT(tr,NP("array",SX(SL(SX(SK(G(max_val),MC(1))),SX(SK(G(min_val),MC(3)))))));PUT(br,NP("array",SX(SL(SX(SK(G(max_val),MC(1))),SX(SK(G(max_val),MC(3)))))));PUT(bl,NP("array",SX(SL(SX(SK(G(min_val),MC(1))),SX(SK(G(max_val),MC(3)))))));PUT(corners,NP("array",SX(SL(SX(G(tl)),SX(G(tr)),SX(G(br)),SX(G(bl))))));return G(corners);}
GEOMETRY_DEF(center_radii_to_corners){GG_BEGIN;PUT(data,NP("array",SX(SL(SX(SA(G(center),G(radii))),SX(SB(G(center),G(radii)))))));PUT(corners,SN("find_corners",SX(G(data))));return G(corners);}
nb::object shape_geometry_triangulate_ellipse(nb::dict globals,nb::tuple literals,bool assertions,GeometryLocals& o){GG_BEGIN;if(gg_truth(SC(SK(gg_attr(G(corners),"shape"),MC(1)),MC(2),Py_NE)))gg_raise(SN("ValueError",SX(MC(3))));if(assertions&&!gg_in(SX(gg_attr(G(corners),"shape")),SX(MC(4))))gg_raise(nb::borrow<nb::object>(PyExc_AssertionError));PUT(center,gg_call(gg_attr(G(corners),"mean"),{},{KW("axis",MC(1))}));PUT(adjusted,SB(G(corners),G(center)));PUT(ax1,SD(SB(SK(G(adjusted),MC(6)),SK(G(adjusted),MC(1))),MC(7)));PUT(ax2,SD(SB(SK(G(adjusted),MC(7)),SK(G(adjusted),MC(6))),MC(7)));PUT(transform,NP("stack",SX(ST(SX(G(ax1)),SX(G(ax2))))));
    if(gg_truth(SC(gg_attr(G(corners),"shape"),MC(8),Py_EQ))){if(assertions&&!gg_truth(SC(gg_attr(G(transform),"shape"),MC(9),Py_EQ)))gg_raise(nb::borrow<nb::object>(PyExc_AssertionError));}else if(assertions&&!gg_truth(SC(gg_attr(G(transform),"shape"),MC(10),Py_EQ)))gg_raise(nb::borrow<nb::object>(PyExc_AssertionError));
    PUT(v2d,gg_call(ns.np("zeros"),{SX(ST(SX(SA(G(num_segments),MC(6))),SX(MC(7))))},{KW("dtype",gg_attr(ns.get("np"),"float32"))}));PUT(theta,NP("linspace",SX(MC(1)),SX(NP("deg2rad",SX(MC(12)))),SX(G(num_segments))));SET(G(v2d),ST(SX(SS(MC(6),MC(13))),SX(MC(1))),NP("cos",SX(G(theta))));SET(G(v2d),ST(SX(SS(MC(6),MC(13))),SX(MC(6))),NP("sin",SX(G(theta))));PUT(vertices,NP("matmul",SX(G(v2d)),SX(G(transform))));PUT(vertices,SA(G(vertices),G(center)));
    PUT(triangles,SM(gg_attr(SA(NP("arange",SX(G(num_segments))),NP("array",SX(SL(SX(SL(SX(MC(1)))),SX(SL(SX(MC(6)))),SX(SL(SX(MC(7)))))))),"T"),NP("array",SX(SL(SX(SK(MC(14),nb::int_(0))),SX(SK(MC(14),nb::int_(1))),SX(SK(MC(14),nb::int_(2))))))));SET(G(triangles),MC(15),MC(6));return ST(SX(G(vertices)),SX(G(triangles)));}
GEOMETRY_DEF(cull_triangles_not_in_poly){GG_BEGIN;PUT(centers,gg_call(ns.np("mean"),{SX(SK(G(vertices),G(triangles)))},{KW("axis",MC(1))}));PUT(in_poly,CALL(ns.get("measure"),"points_in_poly",SX(G(centers)),SX(G(poly))));return SK(G(triangles),G(in_poly));}
GEOMETRY_DEF(fix_vertices_if_needed){GG_BEGIN;if(G(axis).is_none()||G(value).is_none())return G(vertices);PUT(new_vertices,gg_call(ns.np("insert"),{SX(G(vertices)),SX(G(axis)),SX(G(value))},{KW("axis",MC(1))}));return G(new_vertices);}
GEOMETRY_DEF(triangulate_face_and_edges){GG_BEGIN;auto plane=gg_unpack(SN("find_planar_axis",SX(G(polygon_vertices))),3);PUT(data2d,std::move(plane[0]));PUT(axis,std::move(plane[1]));PUT(value,std::move(plane[2]));
    if(!gg_truth(SN("len",SX(G(data2d))))||gg_truth(SN("is_collinear",SX(G(data2d))))){PUT(face_tri,ST(SX(gg_call(ns.np("empty"),{SX(ST(SX(MC(1)),SX(SK(gg_attr(G(polygon_vertices),"shape"),MC(2)))))},{KW("dtype",gg_attr(ns.get("np"),"float32"))})),SX(gg_call(ns.np("empty"),{SX(MC(4))},{KW("dtype",gg_attr(ns.get("np"),"int32"))}))));PUT(edge_tri,gg_call(ns.get("triangulate_edge"),{SX(G(polygon_vertices))},{KW("closed",MC(5))}));return ST(SX(G(face_tri)),SX(G(edge_tri)));}
    if(gg_truth(CALL(ns.get("_triangulate_dispatch"),"is_convex",SX(G(data2d))))){auto pair=gg_unpack(SN("_fan_triangulation",SX(G(data2d))),2);PUT(vertices,std::move(pair[0]));PUT(triangles,std::move(pair[1]));PUT(face_tri,ST(SX(gg_call(ns.get("_fix_vertices_if_needed"),{SX(G(vertices))},{KW("axis",G(axis)),KW("value",G(value))})),SX(G(triangles))));PUT(edge_tri,gg_call(ns.get("triangulate_edge"),{SX(G(polygon_vertices))},{KW("closed",MC(5))}));return ST(SX(G(face_tri)),SX(G(edge_tri)));}
    auto normalized=gg_unpack(gg_call(gg_attr(ns.get("_triangulate_dispatch"),"normalize_vertices_and_edges"),{SX(G(data2d))},{KW("close",MC(5))}),2);PUT(raw_vertices,std::move(normalized[0]));PUT(edges,std::move(normalized[1]));auto face=gg_unpack(gg_call(G(triangulate_face_),{SX(CALL(G(raw_vertices),"copy")),SX(CALL(G(edges),"copy")),SX(G(polygon_vertices))}),2);PUT(vertices,std::move(face[0]));PUT(triangles,std::move(face[1]));PUT(face_tri_,ST(SX(gg_call(ns.get("_fix_vertices_if_needed"),{SX(G(vertices))},{KW("axis",G(axis)),KW("value",G(value))})),SX(G(triangles))));
    if(gg_truth(SC(SN("len",SX(G(edges))),SN("len",SX(G(polygon_vertices))),Py_EQ)))PUT(edge_tri,gg_call(ns.get("triangulate_edge"),{SX(G(polygon_vertices))},{KW("closed",MC(5))}));else PUT(edge_tri,SN("reconstruct_and_triangulate_edge",SX(G(raw_vertices)),SX(G(edges))));return ST(SX(G(face_tri_)),SX(G(edge_tri)));}
GEOMETRY_DEF(reconstruct_and_triangulate_edge){GG_BEGIN;PUT(polygon_list,CALL(ns.get("_triangulate_dispatch"),"reconstruct_polygons_from_edges",SX(G(vertices)),SX(G(edges))));PUT(centers_list,nb::list());PUT(offset_list,nb::list());PUT(triangles_list,nb::list());PUT(offset_idx,MC(1));nb::object iterator=gg_iter(G(polygon_list));while(nb::object value=gg_next(iterator)){PUT(polygon,std::move(value));auto parts=gg_unpack(gg_call(ns.get("triangulate_edge"),{SX(G(polygon))},{KW("closed",MC(2))}),3);PUT(centers,std::move(parts[0]));PUT(offset,std::move(parts[1]));PUT(triangles,std::move(parts[2]));CALL(G(centers_list),"append",SX(G(centers)));CALL(G(offset_list),"append",SX(G(offset)));CALL(G(triangles_list),"append",SX(SA(G(triangles),G(offset_idx))));PUT(offset_idx,SI(G(offset_idx),SN("len",SX(G(centers)))));}iterator.reset();return ST(SX(NP("concatenate",SX(G(centers_list)))),SX(NP("concatenate",SX(G(offset_list)))),SX(NP("concatenate",SX(G(triangles_list)))));}
GEOMETRY_DEF(triangulate_face){GG_BEGIN;if(gg_truth(CALL(ns.get("_triangulate_dispatch"),"is_convex",SX(G(polygon_vertices)))))return SN("_fan_triangulation",SX(G(polygon_vertices)));auto parts=gg_unpack(gg_call(gg_attr(ns.get("_triangulate_dispatch"),"normalize_vertices_and_edges"),{SX(G(polygon_vertices))},{KW("close",MC(1))}),2);PUT(raw_vertices,std::move(parts[0]));PUT(edges,std::move(parts[1]));return gg_call(G(triangulate_face_),{SX(G(raw_vertices)),SX(G(edges)),SX(G(polygon_vertices))});}
GEOMETRY_DEF(triangulate_face_vispy){GG_BEGIN;try{PUT(tri,SN("Triangulation",SX(G(raw_vertices)),SX(G(edges))));CALL(G(tri),"triangulate");nb::object vertices=gg_attr(G(tri),"pts");nb::object triangles=gg_attr(G(tri),"tris");PUT(vertices,std::move(vertices));PUT(triangles,std::move(triangles));}
    catch(nb::python_error& raised){raised.restore();if(PyTraceBack_Here(PyEval_GetFrame())<0)throw nb::python_error();nb::python_error original;collections::HandledException handled(original.value());nb::object klass=ns.get("Exception");gg_except_type(klass);int matched=PyErr_GivenExceptionMatches(original.type().ptr(),klass.ptr());klass.reset();if(!matched)throw original;PUT(e,nb::borrow<nb::object>(original.value()));try{auto paths=gg_unpack(gg_call(ns.get("_save_failed_triangulation"),{SX(G(raw_vertices))},{KW("backend",MC(1))}),2);PUT(path,std::move(paths[0]));PUT(text_path,std::move(paths[1]));gg_raise_cause(SN("RuntimeError",SX(gg_string({SX(MC(3)),SX(gg_format(G(path))),SX(MC(4)),SX(gg_format(G(text_path)))}))),G(e));}catch(...){o.fields[static_cast<size_t>(GeometryField::f_e)].reset();throw;}}
    PUT(triangles,CALL(G(triangles),"astype",SX(gg_attr(ns.get("np"),"uint32"))));return ST(SX(G(vertices)),SX(G(triangles)));}
GEOMETRY_DEF(triangulate_face_triangle){GG_BEGIN;PUT(res,gg_call(ns.get("triangulate"),{SX([&]{nb::dict args;args[SK(MC(1),nb::int_(0))]=G(raw_vertices);args[SK(MC(1),nb::int_(1))]=G(edges);return nb::object(std::move(args));}())},{KW("opts",MC(2))}));PUT(vertices,SK(G(res),MC(4)));PUT(raw_triangles,SK(G(res),MC(5)));PUT(triangles,SN("_cull_triangles_not_in_poly",SX(G(vertices)),SX(G(raw_triangles)),SX(G(polygon_vertices))));PUT(triangles,CALL(G(triangles),"astype",SX(gg_attr(ns.get("np"),"uint32"))));return ST(SX(G(vertices)),SX(G(triangles)));}
GEOMETRY_DEF(triangulate_edge){GG_BEGIN;PUT(path,NP("asanyarray",SX(G(path))));if(gg_truth(SC(SN("len",SX(G(path))),MC(1),Py_GT))){PUT(idx,NP("concatenate",SX(SL(SX(SL(SX(MC(2)))),SX(gg_invert(gg_call(ns.np("all"),{SX(SC(SK(G(path),SS(MC(3),MC(4))),SK(G(path),SS(MC(4),MC(5))),Py_EQ))},{KW("axis",MC(5))})))))));PUT(clean_path,CALL(SK(G(path),G(idx)),"copy"));if(gg_truth(SC(SK(gg_attr(G(clean_path),"shape"),MC(7)),MC(3),Py_EQ)))PUT(clean_path,gg_call(ns.np("concatenate"),{SX(ST(SX(G(clean_path)),SX(G(clean_path))))},{KW("axis",MC(7))}));}else PUT(clean_path,G(path));
    if(gg_truth(SC(SK(gg_attr(G(clean_path),"shape"),MC(5)),MC(1),Py_EQ))){auto parts=gg_unpack(gg_call(gg_attr(ns.get("_triangulate_dispatch"),"generate_2D_edge_meshes"),{SX(gg_call(ns.np("asarray"),{SX(G(clean_path))},{KW("dtype",gg_attr(ns.get("np"),"float32"))}))},{KW("closed",G(closed))}),3);PUT(centers,std::move(parts[0]));PUT(offsets,std::move(parts[1]));PUT(triangles,std::move(parts[2]));}else{auto parts=gg_unpack(gg_call(ns.get("generate_tube_meshes"),{SX(G(clean_path))},{KW("closed",G(closed))}),3);PUT(centers,std::move(parts[0]));PUT(offsets,std::move(parts[1]));PUT(triangles,std::move(parts[2]));}return ST(SX(G(centers)),SX(G(offsets)),SX(G(triangles)));}
GEOMETRY_DEF(generate_tube_meshes){GG_BEGIN;PUT(points,CALL(NP("array",SX(G(path))),"astype",SX(ns.get("float"))));if(gg_truth(G(closed))&&!gg_truth(NP("array_equal",SX(SK(G(points),MC(1))),SX(SK(G(points),MC(2))))))PUT(points,gg_call(ns.np("concatenate"),{SX(SL(SX(G(points)),SX(SL(SX(SK(G(points),MC(1)))))))},{KW("axis",MC(1))}));auto frames=gg_unpack(SN("_frenet_frames",SX(G(points)),SX(G(closed))),3);PUT(_tangents,std::move(frames[0]));PUT(normals,std::move(frames[1]));PUT(binormals,std::move(frames[2]));PUT(segments,SB(SN("len",SX(G(points))),MC(4)));PUT(grid,NP("zeros",SX(ST(SX(SN("len",SX(G(points)))),SX(G(tube_points)),SX(MC(5))))));PUT(grid_off,NP("zeros",SX(ST(SX(SN("len",SX(G(points)))),SX(G(tube_points)),SX(MC(5))))));
    nb::object iterator=gg_iter(SN("range",SX(SN("len",SX(G(points))))));while(nb::object value=gg_next(iterator)){PUT(i,std::move(value));PUT(pos,SK(G(points),G(i)));PUT(normal,SK(G(normals),G(i)));PUT(binormal,SK(G(binormals),G(i)));PUT(v,SM(SM(SD(gg_call(ns.np("arange"),{SX(G(tube_points))},{KW("dtype",ns.get("float"))}),G(tube_points)),MC(7)),gg_attr(ns.get("np"),"pi")));PUT(cx,SM(MC(8),NP("cos",SX(G(v)))));PUT(cy,NP("sin",SX(G(v))));SET(G(grid),G(i),G(pos));SET(G(grid_off),G(i),SA(SM(SK(G(cx),ST(SX(SS(MC(9),MC(9))),SX(gg_attr(ns.get("np"),"newaxis")))),G(normal)),SM(SK(G(cy),ST(SX(SS(MC(9),MC(9))),SX(gg_attr(ns.get("np"),"newaxis")))),G(binormal))));}iterator.reset();PUT(indices,nb::list());
    iterator=gg_iter(CALL(ns.get("itertools"),"product",SX(SN("range",SX(G(segments)))),SX(SN("range",SX(G(tube_points))))));while(nb::object value=gg_next(iterator)){auto pair=gg_unpack(std::move(value),2);PUT(i,std::move(pair[0]));PUT(j,std::move(pair[1]));if(gg_truth(G(closed)))PUT(ip,SREM(SA(G(i),MC(4)),G(segments)));else PUT(ip,SA(G(i),MC(4)));PUT(jp,SREM(SA(G(j),MC(4)),G(tube_points)));PUT(index_a,SA(SM(G(i),G(tube_points)),G(j)));PUT(index_b,SA(SM(G(ip),G(tube_points)),G(j)));PUT(index_c,SA(SM(G(ip),G(tube_points)),G(jp)));PUT(index_d,SA(SM(G(i),G(tube_points)),G(jp)));CALL(G(indices),"extend",SX(ST(SX(SL(SX(G(index_a)),SX(G(index_b)),SX(G(index_d)))),SX(SL(SX(G(index_b)),SX(G(index_c)),SX(G(index_d)))))));}iterator.reset();PUT(triangles,gg_call(ns.np("array"),{SX(G(indices))},{KW("dtype",gg_attr(ns.get("np"),"uint32"))}));PUT(centers,CALL(G(grid),"reshape",SX(SM(SK(gg_attr(G(grid),"shape"),MC(1)),SK(gg_attr(G(grid),"shape"),MC(4)))),SX(MC(5))));PUT(offsets,CALL(G(grid_off),"reshape",SX(SM(SK(gg_attr(G(grid_off),"shape"),MC(1)),SK(gg_attr(G(grid_off),"shape"),MC(4)))),SX(MC(5))));return ST(SX(G(centers)),SX(G(offsets)),SX(G(triangles)));}
void gg_star_extend(nb::handle list,nb::handle value,Py_ssize_t start){if(PyList_SetSlice(list.ptr(),start,PY_SSIZE_T_MAX,value.ptr())<0){if(PyErr_ExceptionMatches(PyExc_TypeError)&&!Py_TYPE(value.ptr())->tp_iter&&!PySequence_Check(value.ptr()))PyErr_Format(PyExc_TypeError,"Value after * must be an iterable, not %.200s",Py_TYPE(value.ptr())->tp_name);throw nb::python_error();}}
GEOMETRY_DEF(path_to_mask){GG_BEGIN;PUT(mask_shape,gg_call(ns.np("asarray"),{SX(G(mask_shape))},{KW("dtype",ns.get("int"))}));PUT(mask,gg_call(ns.np("zeros"),{SX(G(mask_shape))},{KW("dtype",ns.get("bool"))}));PUT(vertices,CALL(NP("round",SX(NP("clip",SX(G(vertices)),SX(MC(2)),SX(SB(G(mask_shape),MC(3)))))),"astype",SX(ns.get("int"))));PUT(duplicates,gg_call(ns.np("all"),{SX(SC(gg_call(ns.np("diff"),{SX(G(vertices))},{KW("axis",MC(2))}),MC(2),Py_EQ))},{KW("axis",MC(5))}));PUT(duplicates,NP("concatenate",SX(ST(SX(SL(SX(MC(6)))),SX(G(duplicates))))));PUT(vertices,SK(G(vertices),gg_invert(G(duplicates))));PUT(iis,nb::list());PUT(jjs,nb::list());
    nb::object iterator=gg_iter(CALL(ns.get("itertools"),"pairwise",SX(G(vertices))));while(nb::object value=gg_next(iterator)){auto pair=gg_unpack(std::move(value),2);PUT(v1,std::move(pair[0]));PUT(v2,std::move(pair[1]));nb::object callback=ns.get("line");nb::list args;nb::object first=G(v1);gg_star_extend(args,first,0);first.reset();nb::object second=G(v2);gg_star_extend(args,second,PY_SSIZE_T_MAX);second.reset();nb::object tuple=gg_check(PyList_AsTuple(args.ptr()));args.reset();nb::object supplied=gg_check(PyObject_CallObject(callback.ptr(),tuple.ptr()));tuple.reset();callback.reset();auto coords=gg_unpack(std::move(supplied),2);PUT(ii,std::move(coords[0]));PUT(jj,std::move(coords[1]));CALL(G(iis),"extend",SX(CALL(G(ii),"tolist")));CALL(G(jjs),"extend",SX(CALL(G(jj),"tolist")));}iterator.reset();SET(G(mask),ST(SX(G(iis)),SX(G(jjs))),MC(3));return G(mask);}
GEOMETRY_DEF(poly_to_mask){GG_BEGIN;return SN("polygon2mask",SX(G(mask_shape)),SX(G(vertices)));}
nb::object gg_grid_points(const GeometryNamespace& ns,const nb::tuple& literals,GeometryLocals& o){nb::object outer=gg_iter(SN("range",SX(SK(G(shape),MC(1)))));nb::object saved_x=std::move(o.fields[static_cast<size_t>(GeometryField::f_x)]);nb::object saved_y=std::move(o.fields[static_cast<size_t>(GeometryField::f_y)]);nb::list points;
    try{while(nb::object x=gg_next(outer)){PUT(x,std::move(x));nb::object inner=gg_iter(SN("range",SX(SK(G(shape),MC(2)))));while(nb::object y=gg_next(inner)){PUT(y,std::move(y));points.append(ST(SX(G(x)),SX(G(y))));}inner.reset();}}catch(...){outer.reset();points.reset();PUT(y,std::move(saved_y));PUT(x,std::move(saved_x));throw;}outer.reset();PUT(y,std::move(saved_y));PUT(x,std::move(saved_x));return points;}
GEOMETRY_DEF(grid_points_in_poly){GG_BEGIN;PUT(points,gg_call(ns.np("array"),{SX(gg_grid_points(ns,literals,o))},{KW("dtype",ns.get("int"))}));PUT(inside,SN("points_in_poly",SX(G(points)),SX(G(vertices))));PUT(mask,CALL(G(inside),"reshape",SX(G(shape))));return G(mask);}
GEOMETRY_DEF(points_in_poly){GG_BEGIN;PUT(n_verts,SN("len",SX(G(vertices))));PUT(inside,gg_call(ns.np("zeros"),{SX(SN("len",SX(G(points))))},{KW("dtype",ns.get("bool"))}));PUT(j,SB(G(n_verts),MC(2)));nb::object iterator=gg_iter(SN("range",SX(G(n_verts))));while(nb::object value=gg_next(iterator)){PUT(i,std::move(value));
    PUT(cond_1,NP("logical_and",SX(SC(SK(G(vertices),ST(SX(G(i)),SX(MC(2)))),SK(G(points),ST(SX(SS(MC(3),MC(3))),SX(MC(2)))),Py_LE)),SX(SC(SK(G(points),ST(SX(SS(MC(3),MC(3))),SX(MC(2)))),SK(G(vertices),ST(SX(G(j)),SX(MC(2)))),Py_LT))));PUT(cond_2,NP("logical_and",SX(SC(SK(G(vertices),ST(SX(G(j)),SX(MC(2)))),SK(G(points),ST(SX(SS(MC(3),MC(3))),SX(MC(2)))),Py_LE)),SX(SC(SK(G(points),ST(SX(SS(MC(3),MC(3))),SX(MC(2)))),SK(G(vertices),ST(SX(G(i)),SX(MC(2)))),Py_LT))));PUT(cond_3,NP("logical_or",SX(G(cond_1)),SX(G(cond_2))));PUT(d,SB(SK(G(vertices),G(j)),SK(G(vertices),G(i))));PUT(tolerance,MC(4));PUT(d,NP("where",SX(SC(SN("abs",SX(G(d))),G(tolerance),Py_LT)),SX(MC(5)),SX(G(d))));
    if(gg_truth(SC(SK(G(d),MC(2)),MC(5),Py_EQ)))PUT(cond_4,SC(SM(SK(G(d),MC(5)),SB(SK(G(points),ST(SX(SS(MC(3),MC(3))),SX(MC(2)))),SK(G(vertices),ST(SX(G(i)),SX(MC(2)))))),MC(5),Py_GT));else PUT(cond_4,SC(SK(G(points),ST(SX(SS(MC(3),MC(3))),SX(MC(5)))),SA(SD(SM(SK(G(d),MC(5)),SB(SK(G(points),ST(SX(SS(MC(3),MC(3))),SX(MC(2)))),SK(G(vertices),ST(SX(G(i)),SX(MC(2)))))),SK(G(d),MC(2))),SK(G(vertices),ST(SX(G(i)),SX(MC(5))))),Py_LT));PUT(cond_5,NP("logical_and",SX(G(cond_3)),SX(G(cond_4))));SET(G(inside),G(cond_5),SB(MC(2),SK(G(inside),G(cond_5))));PUT(j,G(i));}iterator.reset();return G(inside);}

enum class GeometryGeneratorKind{Collinear,TupleValidation,DefaultType};
nb::object gg_free_cell(nb::handle cell,const char* name){PyObject* value=PyCell_GET(cell.ptr());if(value)return nb::borrow<nb::object>(value);
    nb::object text=gg_check(PyUnicode_FromFormat("cannot access free variable '%s' where it is not associated with a value in enclosing scope",name));nb::object error=gg_call(nb::borrow<nb::object>(PyExc_NameError),{SX(text)});gg_store(error,"name",gg_check(PyUnicode_InternFromString(name)));gg_raise(std::move(error));
}
struct GeometryGenerator {
    GeometryGeneratorKind kind;nb::object iterator,current,globals,builtins,cell;bool started=false,running=false,closed=false;
    GeometryGenerator(GeometryGeneratorKind k,nb::object i,nb::dict g,nb::object b,nb::object c):kind(k),iterator(std::move(i)),globals(std::move(g)),builtins(std::move(b)),cell(std::move(c)){}
    GeometryGenerator(GeometryGenerator&&)=default;~GeometryGenerator(){clear();}
    void clear(){auto i=std::move(iterator);auto c=std::move(current);auto g=std::move(globals);auto b=std::move(builtins);auto captured=std::move(cell);closed=true;running=false;i.reset();c.reset();g.reset();b.reset();captured.reset();}
    nb::object next(){if(closed){PyErr_SetNone(PyExc_StopIteration);throw nb::python_error();}if(running){PyErr_SetString(PyExc_ValueError,"generator already executing");throw nb::python_error();}running=true;started=true;try{nb::object value=gg_next(iterator);if(!value.is_valid()){clear();PyErr_SetNone(PyExc_StopIteration);throw nb::python_error();}gg_assign(current,std::move(value));GeometryNamespace ns(nb::borrow<nb::dict>(globals),builtins);nb::object result;
        if(kind==GeometryGeneratorKind::Collinear)result=SC(CALL(ns.get("_triangulate_py"),"orientation",SX(SK(gg_free_cell(cell,"points"),nb::int_(0))),SX(SK(gg_free_cell(cell,"points"),nb::int_(1))),SX(current)),nb::int_(0),Py_EQ);
        else if(kind==GeometryGeneratorKind::TupleValidation)result=SN("isinstance",SX(current),SX(ns.get("tuple")));else result=SC(current,gg_free_cell(cell,"first_type"),Py_EQ);running=false;return result;
    }catch(const nb::python_error& error){bool ended=closed;closed=true;running=false;if(!ended&&error.matches(PyExc_StopIteration))collections::raise_exception(error.value(),true);throw;}}
    nb::object send(nb::object value){if(!started&&!value.is_none()){PyErr_SetString(PyExc_TypeError,"can't send non-None value to a just-started generator");throw nb::python_error();}return next();}
    void close(){if(running){PyErr_SetString(PyExc_ValueError,"generator already executing");throw nb::python_error();}if(!closed)clear();}
    nb::object throw_into(nb::args args){if(running){PyErr_SetString(PyExc_ValueError,"generator already executing");throw nb::python_error();}nb::object error=collections::normalize_throw(args);bool convert=!closed;closed=true;running=false;collections::raise_exception(error,convert);}
};
int gg_generator_traverse(PyObject* self,visitproc visit,void* arg){Py_VISIT(Py_TYPE(self));if(nb::inst_ready(self)){auto& s=*nb::inst_ptr<GeometryGenerator>(self);Py_VISIT(s.iterator.ptr());Py_VISIT(s.current.ptr());Py_VISIT(s.globals.ptr());Py_VISIT(s.builtins.ptr());Py_VISIT(s.cell.ptr());}return 0;}
int gg_generator_clear(PyObject* self){if(nb::inst_ready(self))nb::inst_ptr<GeometryGenerator>(self)->clear();return 0;}
PyType_Slot gg_generator_slots[]={{Py_tp_traverse,reinterpret_cast<void*>(gg_generator_traverse)},{Py_tp_clear,reinterpret_cast<void*>(gg_generator_clear)},{0,nullptr}};
nb::object gg_generator(nb::object factory,GeometryGenerator state,const char* qualname){nb::object iterator=nb::cast(std::move(state));nb::object generator=gg_call(std::move(factory),{SX(iterator)});gg_store(generator,"__name__",nb::str("<genexpr>"));gg_store(generator,"__qualname__",nb::str(qualname));return generator;}
GEOMETRY_GEN_DEF(is_collinear){GG_BEGIN;if(gg_truth(SC(SN("len",SX(G(points))),MC(1),Py_LT)))return MC(2);nb::object all=ns.get("all");nb::object iterator=gg_iter(SK(G(points),SS(MC(4),MC(5))));nb::object generator=gg_generator(std::move(generator_factory),GeometryGenerator(GeometryGeneratorKind::Collinear,std::move(iterator),ns.values,ns.builtins,o.fields[static_cast<size_t>(GeometryField::f_points)]),"is_collinear.<locals>.<genexpr>");return gg_call(std::move(all),{SX(generator)});}
GEOMETRY_GEN_DEF(extract_shape_type){GG_BEGIN;if(gg_truth(SN("isinstance",SX(G(data)),SX(ns.get("tuple"))))){PUT(shape_type,SK(G(data),MC(1)));PUT(data,SK(G(data),MC(2)));}else if(gg_truth(SC(SN("len",SX(G(data))),MC(2),Py_NE))){nb::object all=ns.get("all");nb::object iterator=gg_iter(G(data));nb::object generator=gg_generator(std::move(generator_factory),GeometryGenerator(GeometryGeneratorKind::TupleValidation,std::move(iterator),ns.values,ns.builtins,nb::object()),"extract_shape_type.<locals>.<genexpr>");nb::object valid=gg_call(std::move(all),{SX(generator)});generator.reset();if(gg_truth(std::move(valid))){
    for(int pass=0;pass<2;++pass){iterator=gg_iter(G(data));nb::object saved=std::move(o.fields[static_cast<size_t>(GeometryField::f_datum)]);nb::list result;try{while(nb::object datum=gg_next(iterator)){PUT(datum,std::move(datum));result.append(SK(G(datum),MC(pass?2:1)));}}catch(...){iterator.reset();result.reset();PUT(datum,std::move(saved));throw;}iterator.reset();if(pass)PUT(data,std::move(result));else PUT(shape_type,std::move(result));PUT(datum,std::move(saved));}
    }}return ST(SX(G(data)),SX(G(shape_type)));}
GEOMETRY_GEN_DEF(get_default_shape_type){GG_BEGIN;PUT(default,MC(1));if(!gg_truth(G(current_type)))return G(default);PUT(first_type,SK(G(current_type),MC(2)));nb::object all=ns.get("all");nb::object iterator=gg_iter(G(current_type));nb::object generator=gg_generator(std::move(generator_factory),GeometryGenerator(GeometryGeneratorKind::DefaultType,std::move(iterator),ns.values,ns.builtins,o.fields[static_cast<size_t>(GeometryField::f_first_type)]),"get_default_shape_type.<locals>.<genexpr>");nb::object result=gg_call(std::move(all),{SX(generator)});generator.reset();if(gg_truth(std::move(result)))return G(first_type);return G(default);}
GEOMETRY_DEF(get_shape_ndim){GG_BEGIN;if(gg_truth(SC(gg_attr(gg_call(ns.np("array"),{SX(G(data))},{KW("dtype",ns.get("object"))}),"ndim"),MC(2),Py_EQ)))PUT(ndim,SK(gg_attr(NP("array",SX(G(data))),"shape"),MC(3)));else if(gg_truth(SC(gg_attr(NP("array",SX(SK(G(data),MC(4)))),"ndim"),MC(5),Py_EQ)))PUT(ndim,SK(gg_attr(NP("array",SX(G(data))),"shape"),MC(5)));else PUT(ndim,SK(gg_attr(NP("array",SX(SK(G(data),MC(4)))),"shape"),MC(5)));return G(ndim);}
GEOMETRY_DEF(number_of_shapes){GG_BEGIN;if(gg_truth(SC(SN("len",SX(G(data))),MC(1),Py_EQ)))PUT(n_shapes,MC(1));else if(gg_truth(SC(gg_attr(NP("array",SX(SK(G(data),MC(1)))),"ndim"),MC(2),Py_EQ)))PUT(n_shapes,MC(2));else PUT(n_shapes,SN("len",SX(G(data))));return G(n_shapes);}
GEOMETRY_DEF(validate_num_vertices){GG_BEGIN;PUT(n_shapes,SN("number_of_shapes",SX(G(data))));if(gg_truth(SC(G(n_shapes),MC(1),Py_EQ))&&gg_truth(SC(SN("len",SX(gg_attr(NP("array",SX(G(data))),"shape"))),MC(2),Py_EQ)))PUT(data,SL(SX(G(data))));nb::object iterator=gg_iter(G(data));while(nb::object value=gg_next(iterator)){PUT(shape,std::move(value));if((gg_truth(G(valid_vertices))&&!gg_in(SX(SN("len",SX(G(shape)))),SX(G(valid_vertices))))||(gg_truth(G(min_vertices))&&gg_truth(SC(SN("len",SX(G(shape))),G(min_vertices),Py_LT))))gg_raise(SN("ValueError",SX(gg_string({SX(gg_format(G(shape_type))),SX(MC(3)),SX(gg_format(G(shape))),SX(MC(4)),SX(gg_format(SN("len",SX(G(shape))))),SX(MC(5))}))));}iterator.reset();return nb::none();}
GEOMETRY_DEF(perpendicular_distance){GG_BEGIN;if(gg_truth(NP("array_equal",SX(G(line_start)),SX(G(line_end)))))return SN("float",SX(CALL(gg_attr(ns.get("np"),"linalg"),"norm",SX(SB(G(point),G(line_start))))));PUT(t,SD(NP("dot",SX(SB(G(point),G(line_end))),SX(SB(G(line_start),G(line_end)))),NP("dot",SX(SB(G(line_start),G(line_end))),SX(SB(G(line_start),G(line_end))))));return SN("float",SX(CALL(gg_attr(ns.get("np"),"linalg"),"norm",SX(SB(SA(SM(G(t),SB(G(line_start),G(line_end))),G(line_end)),G(point))))));}
GEOMETRY_DEF(rdp){GG_BEGIN;PUT(max_distance_index,MC(1));PUT(max_distance,MC(2));nb::object iterator=gg_iter(SN("range",SX(MC(3)),SX(SK(gg_attr(G(vertices),"shape"),MC(4)))));while(nb::object value=gg_next(iterator)){PUT(i,std::move(value));PUT(d,SN("perpendicular_distance",SX(SK(G(vertices),G(i))),SX(SK(G(vertices),MC(4))),SX(SK(G(vertices),MC(1)))));if(gg_truth(SC(G(d),G(max_distance),Py_GT))){PUT(max_distance_index,G(i));PUT(max_distance,G(d));}}iterator.reset();if(gg_truth(SC(G(epsilon),MC(4),Py_NE))){if(gg_truth(SC(G(max_distance),G(epsilon),Py_GT))&&gg_truth(G(epsilon))){PUT(l1,SN("rdp",SX(SK(G(vertices),SS(MC(5),SA(G(max_distance_index),MC(3))))),SX(G(epsilon))));PUT(l2,SN("rdp",SX(SK(G(vertices),SS(G(max_distance_index),MC(5)))),SX(G(epsilon))));return NP("vstack",SX(ST(SX(SK(G(l1),SS(MC(5),MC(1)))),SX(G(l2)))));}return NP("vstack",SX(ST(SX(SK(G(vertices),MC(4))),SX(SK(G(vertices),MC(1))))));}return G(vertices);}
template<class Function>void gg_with_bound_context(nb::object manager,GeometryLocals& o,GeometryField field,Function body){nb::object enter=collections::context_method(manager,"__enter__",false);nb::object exit=collections::context_method(manager,"__exit__",true);manager.reset();nb::object entered=gg_call(std::move(enter));o.assign(field,std::move(entered));
    try{body();}catch(nb::python_error& original){
        original.restore();if(PyTraceBack_Here(PyEval_GetFrame())<0)throw nb::python_error();nb::python_error traced;bool suppressed;
        {collections::HandledException handled(traced.value());nb::object traceback=traced.traceback();if(!traceback.is_valid())traceback=nb::none();
            try{suppressed=gg_truth(gg_call(exit,{SX(nb::borrow<nb::object>(traced.type())),SX(nb::borrow<nb::object>(traced.value())),SX(traceback)}));}
            catch(const nb::python_error& cleanup){if(cleanup.value().ptr()!=traced.value().ptr())PyException_SetContext(cleanup.value().ptr(),traced.value().inc_ref().ptr());throw;}
        }
        exit.reset();if(!suppressed){o.propagated_context_exception=nb::borrow<nb::object>(traced.value());nb::object traceback=traced.traceback();o.propagated_context_traceback=traceback.is_valid()?std::move(traceback):nb::object(nb::none());throw traced;}return;
    }gg_call(std::move(exit),{SX(nb::none()),SX(nb::none()),SX(nb::none())});
}
// The genuine facade catches native propagation, restores this actual post-exit
// traceback and bare-reraises. No duplicate facade frame is added; any traceback
// retained or replaced by the source exit callback stays valid and authoritative.
nb::object shape_geometry_restore_context_traceback(GeometryLocals& o){
    if(!o.propagated_context_exception.is_valid())return nb::none();nb::object error=std::move(o.propagated_context_exception);nb::object traceback=std::move(o.propagated_context_traceback);nb::object handled=nb::steal<nb::object>(PyErr_GetHandledException());
    if(handled.is_valid()&&handled.ptr()==error.ptr()&&PyException_SetTraceback(handled.ptr(),traceback.ptr())<0)throw nb::python_error();return nb::none();
}
GEOMETRY_DEF(save_failed_triangulation){GG_BEGIN;gg_with_bound_context(gg_call(gg_attr(ns.get("tempfile"),"NamedTemporaryFile"),{},{KW("delete",MC(1)),KW("suffix",MC(2)),KW("prefix",gg_string({SX(MC(3)),SX(gg_format(G(backend))),SX(MC(4))})),KW("dir",G(target_dir))}),o,GeometryField::f_binary_file,[&]{gg_call(ns.np("savez"),{SX(G(binary_file))},{KW("data",G(data))});});
    gg_with_bound_context(gg_call(gg_attr(ns.get("tempfile"),"NamedTemporaryFile"),{},{KW("delete",MC(1)),KW("suffix",MC(8)),KW("prefix",gg_string({SX(MC(3)),SX(gg_format(G(backend))),SX(MC(4))})),KW("mode",MC(9)),KW("dir",G(target_dir))}),o,GeometryField::f_text_file,[&]{NP("savetxt",SX(G(text_file)),SX(G(data)));});return ST(SX(gg_attr(G(binary_file),"name")),SX(gg_attr(G(text_file),"name")));}
} // anonymous namespace

void bind_shape_geometry(nb::module_& m){
    auto locals=nb::class_<GeometryLocals>(m,"ShapeGeometryLocals",nb::is_weak_referenceable(),nb::type_slots(gg_owner_slots));
#define GG_PROP(n) locals.def_prop_ro(#n,[](const GeometryLocals& o){size_t i=static_cast<size_t>(GeometryField::f_##n);if(!o.fields[i].is_valid())return nb::object(nb::none());if(o.cells[i]){PyObject* value=PyCell_GET(o.fields[i].ptr());return nb::borrow<nb::object>(value?value:Py_None);}return o.fields[i];});
    GEOMETRY_FIELDS(GG_PROP)
#undef GG_PROP
    nb::class_<GeometryGenerator>(m,"ShapeGeometryGeneratorIterator",nb::is_weak_referenceable(),nb::type_slots(gg_generator_slots)).def("__iter__",[](nb::handle self){return nb::borrow<nb::object>(self);}).def("__next__",&GeometryGenerator::next).def("send",&GeometryGenerator::send,nb::arg("value").none()).def("throw",&GeometryGenerator::throw_into).def("close",&GeometryGenerator::close);
    m.def("shape_geometry_locals",&shape_geometry_locals,nb::arg("layout"),nb::arg("bindings"),nb::arg("cells"));
#define GG_BIND(n) m.def("shape_geometry_" #n,&shape_geometry_##n,nb::arg("namespace"),nb::arg("literals"),nb::arg("owner"));
#define GG_BIND_GEN(n) m.def("shape_geometry_" #n,&shape_geometry_##n,nb::arg("namespace"),nb::arg("literals"),nb::arg("generator_factory").none(),nb::arg("owner"));
    GG_BIND(find_planar_axis)
    GG_BIND(fan_triangulation)
    GG_BIND(inside_boxes)
    GG_BIND(triangles_intersect_box)
    GG_BIND(triangle_vertices_inside_box)
    GG_BIND(triangle_edges_intersect_box)
    GG_BIND(lines_intersect)
    GG_BIND(vectorized_lines_intersect)
    GG_BIND(on_segment)
    GG_BIND_GEN(is_collinear)
    GG_BIND(point_to_lines)
    GG_BIND(create_box)
    GG_BIND(rectangle_to_box)
    GG_BIND(find_corners)
    GG_BIND(center_radii_to_corners)
    m.def("shape_geometry_triangulate_ellipse",&shape_geometry_triangulate_ellipse,nb::arg("namespace"),nb::arg("literals"),nb::arg("assertions"),nb::arg("owner"));
    GG_BIND(cull_triangles_not_in_poly)
    GG_BIND(fix_vertices_if_needed)
    GG_BIND(triangulate_face_and_edges)
    GG_BIND(reconstruct_and_triangulate_edge)
    GG_BIND(triangulate_face)
    GG_BIND(triangulate_face_vispy)
    GG_BIND(triangulate_face_triangle)
    GG_BIND(triangulate_edge)
    GG_BIND(generate_tube_meshes)
    GG_BIND(path_to_mask)
    GG_BIND(poly_to_mask)
    GG_BIND(grid_points_in_poly)
    GG_BIND(points_in_poly)
    GG_BIND_GEN(extract_shape_type)
    GG_BIND_GEN(get_default_shape_type)
    GG_BIND(get_shape_ndim)
    GG_BIND(number_of_shapes)
    GG_BIND(validate_num_vertices)
    GG_BIND(perpendicular_distance)
    GG_BIND(rdp)
    GG_BIND(save_failed_triangulation)
    m.def("shape_geometry_restore_context_traceback",&shape_geometry_restore_context_traceback,nb::arg("owner"));
#undef GG_BIND
#undef GG_BIND_GEN
}
} // namespace nx
