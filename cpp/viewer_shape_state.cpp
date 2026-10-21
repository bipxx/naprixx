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
nb::object ss_check(PyObject* p) { if (!p) throw nb::python_error(); return nb::steal<nb::object>(p); }
nb::object ss_attr(nb::object a, const char* n) { return ss_check(PyObject_GetAttrString(a.ptr(), n)); }
nb::object ss_item(nb::object a, nb::object k) { return ss_check(PyObject_GetItem(a.ptr(), k.ptr())); }
void ss_store(nb::object a, const char* n, nb::object v) { if (PyObject_SetAttrString(a.ptr(), n, v.ptr()) < 0) throw nb::python_error(); }
void ss_set(nb::object a, nb::object k, nb::object v) { if (PyObject_SetItem(a.ptr(), k.ptr(), v.ptr()) < 0) throw nb::python_error(); }
void ss_assign(nb::object& a, nb::object v) { nb::object old = std::move(a); a = std::move(v); old.reset(); }
bool ss_truth(nb::object a) { int r = PyObject_IsTrue(a.ptr()); if (r < 0) throw nb::python_error(); return r != 0; }
nb::object ss_iter(nb::object a) { return ss_check(PyObject_GetIter(a.ptr())); }
nb::object ss_next(nb::handle a) { PyObject* p = PyIter_Next(a.ptr()); if (!p && PyErr_Occurred()) throw nb::python_error(); return nb::steal<nb::object>(p); }
using SSExpr = std::function<nb::object()>;
using SSKw = std::pair<const char*, SSExpr>;
nb::object ss_call(nb::object fn, std::initializer_list<SSExpr> positional = {}, std::initializer_list<SSKw> keywords = {}) {
    nb::tuple args = nb::steal<nb::tuple>(PyTuple_New(static_cast<Py_ssize_t>(positional.size())));
    if (!args.is_valid()) throw nb::python_error(); Py_ssize_t i = 0;
    for (const auto& f : positional) { nb::object v = f(); PyTuple_SET_ITEM(args.ptr(), i++, v.release().ptr()); }
    nb::dict kwargs; for (const auto& kv : keywords) kwargs[kv.first] = kv.second();
    return ss_check(PyObject_Call(fn.ptr(), args.ptr(), keywords.size() ? kwargs.ptr() : nullptr));
}
nb::tuple ss_tuple(std::initializer_list<SSExpr> values) {
    nb::tuple out = nb::steal<nb::tuple>(PyTuple_New(static_cast<Py_ssize_t>(values.size())));
    if (!out.is_valid()) throw nb::python_error(); Py_ssize_t i = 0;
    for (const auto& f : values) { nb::object v = f(); PyTuple_SET_ITEM(out.ptr(), i++, v.release().ptr()); } return out;
}
nb::list ss_list(std::initializer_list<SSExpr> values) { nb::list out; for (const auto& f : values) out.append(f()); return out; }
nb::object ss_binary(SSExpr a, SSExpr b, binaryfunc fn) { nb::object left = a(); nb::object right = b(); return ss_check(fn(left.ptr(), right.ptr())); }
nb::object ss_subscript(SSExpr a, SSExpr b) { nb::object left = a(); nb::object key = b(); return ss_item(std::move(left),std::move(key)); }
void ss_publish(SSExpr value, SSExpr target, const char* name) { nb::object v = value(); nb::object t = target(); ss_store(std::move(t),name,std::move(v)); }
void ss_publish_item(SSExpr value, SSExpr target, SSExpr key) { nb::object v = value(); nb::object t = target(); nb::object k = key(); ss_set(std::move(t),std::move(k),std::move(v)); }
nb::object ss_compare(SSExpr a, SSExpr b, int op) { nb::object left = a(); nb::object right = b(); return ss_check(PyObject_RichCompare(left.ptr(), right.ptr(), op)); }
nb::object ss_slice(SSExpr start, SSExpr stop, SSExpr step) { nb::object a = start(); nb::object b = stop(); nb::object c = step(); return ss_check(PySlice_New(a.ptr(), b.ptr(), c.ptr())); }
#define SX(expr) [&]() -> nb::object { return (expr); }
#define SA(a,b) ss_binary(SX(a),SX(b),PyNumber_Add)
#define SB(a,b) ss_binary(SX(a),SX(b),PyNumber_Subtract)
#define SM(a,b) ss_binary(SX(a),SX(b),PyNumber_Multiply)
#define SI(a,b) ss_binary(SX(a),SX(b),PyNumber_InPlaceAdd)
#define SC(a,b,op) ss_compare(SX(a),SX(b),op)
#define SK(a,b) ss_subscript(SX(a),SX(b))
#define STORE(a,n,v) ss_publish(SX(v),SX(a),n)
#define SET(a,k,v) ss_publish_item(SX(v),SX(a),SX(k))
#define ST(...) ss_tuple({__VA_ARGS__})
#define SL(...) ss_list({__VA_ARGS__})
#define SS(a,b) ss_slice(SX(a),SX(b),SX(nb::none()))
[[noreturn]] void ss_raise(nb::object value) {
    if (PyExceptionClass_Check(value.ptr())) { nb::object cls = std::move(value); value = ss_call(cls);
        if (!PyExceptionInstance_Check(value.ptr())) { PyErr_Format(PyExc_TypeError, "calling %R should have returned an instance of BaseException, not %R", cls.ptr(), reinterpret_cast<PyObject*>(Py_TYPE(value.ptr()))); throw nb::python_error(); }
    }
    if (PyExceptionInstance_Check(value.ptr())) PyErr_SetObject(reinterpret_cast<PyObject*>(Py_TYPE(value.ptr())), value.ptr());
    else PyErr_SetString(PyExc_TypeError, "exceptions must derive from BaseException"); throw nb::python_error();
}
struct ShapeNamespace {
    nb::dict values; nb::object builtins;
    explicit ShapeNamespace(nb::dict v) : values(std::move(v)), builtins(nb::borrow<nb::object>(PyEval_GetBuiltins())) {}
    ShapeNamespace(nb::dict v, nb::object b) : values(std::move(v)), builtins(std::move(b)) {}
    nb::object get(const char* name) const {
        nb::object key = nb::str(name); PyObject* p = PyDict_GetItemWithError(values.ptr(), key.ptr());
        if (p) return nb::borrow<nb::object>(p); if (PyErr_Occurred()) throw nb::python_error();
        p = PyObject_GetItem(builtins.ptr(), key.ptr()); if (p) return nb::steal<nb::object>(p);
        if (!PyErr_ExceptionMatches(PyExc_KeyError)) throw nb::python_error(); PyErr_Clear();
        nb::object text = ss_check(PyUnicode_FromFormat("name '%s' is not defined", name));
        nb::object error = ss_call(nb::borrow<nb::object>(PyExc_NameError), {SX(text)}); ss_store(error, "name", key); ss_raise(error);
    }
    nb::object np(const char* name) const { return ss_attr(get("np"), name); }
};
#define SN(fn,...) ss_call(ns.get(fn),{__VA_ARGS__})
#define NP(fn,...) ss_call(ns.np(fn),{__VA_ARGS__})
#define NK(fn,args,...) ss_call(ns.np(fn),args,{__VA_ARGS__})
#define KW(name,expr) SSKw{name,SX(expr)}
#define CALL(obj,name,...) ss_call(ss_attr((obj),name),{__VA_ARGS__})

#define SHAPE_FIELDS(X) \
 X(shapes) X(face_colors) X(edge_colors) X(v) X(n_vertices) X(n_indices) X(n_mesh_vertices) X(n_face_tri) X(n_edge_tri) X(shape) X(sizes) X(n_shapes) X(dim) \
 X(z_index) X(vertices_index) X(mesh_triangles_index) X(mesh_vertices_index) X(vertices) X(mesh_vertices) X(mesh_vertices_centers) X(mesh_vertices_offsets) X(total_triangles) X(mesh_triangles) X(mesh_triangles_colors) X(_n_indices) \
 X(start_mesh_index) X(start_triangle_index) X(start_vertices_index) X(arrays) X(vertices_offset) X(mesh_vertices_offset) X(triangles_offset) X(i) X(vertices_) X(face_vertices) X(n_face_vertices) X(face_vertices_slice) X(face_triangles) X(n_face_triangles) X(face_triangles_slice) \
 X(edge_vertices) X(edge_offsets) X(n_edge_vertices) X(curr_vertices) X(edge_vertices_slice) X(edge_triangles) X(n_edge_triangles) X(edge_triangles_slice) X(face_color) X(edge_color) X(meth) X(self) X(data) X(ndisplay) X(shape_index) X(start) X(end) X(slices) X(count) X(res) X(begin) X(r) \
 X(shape_indexes) X(ranges) X(end_shape) X(slicess) X(s) X(slice_key) X(displayed_indices) X(shift_idx) X(elem_num) X(disp_indices) X(z_order) X(triangle_ranges) X(vertices_range) X(z_refresh) X(m) X(triangles) X(color_array) X(idx) X(counts) X(triangles_z_order) X(z) X(args) X(kwargs)
enum class ShapeField {
#define SS_ENUM(n) n,
 SHAPE_FIELDS(SS_ENUM)
#undef SS_ENUM
 count_fields
};
struct ShapeStateLocals {
#define SS_DECLARE(n) nb::object n;
 SHAPE_FIELDS(SS_DECLARE)
#undef SS_DECLARE
    std::vector<ShapeField> order;
    ShapeStateLocals() = default; ShapeStateLocals(ShapeStateLocals&&) = default;
    ~ShapeStateLocals() { clear(); }
    void clear() {
        std::array<nb::object, static_cast<size_t>(ShapeField::count_fields)> old = {
#define SS_MOVE(n) std::move(n),
 SHAPE_FIELDS(SS_MOVE)
#undef SS_MOVE
        }; auto fields = std::move(order); order.clear(); for (ShapeField f : fields) old[static_cast<size_t>(f)].reset(); for (auto& v : old) v.reset();
    }
};
ShapeStateLocals shape_state_locals(nb::tuple names) {
    ShapeStateLocals o;
    for (nb::handle value : names) { std::string s = nb::cast<std::string>(value); bool found = false;
#define SS_MATCH(n) if (s == #n) { o.order.push_back(ShapeField::n); found = true; }
 SHAPE_FIELDS(SS_MATCH)
#undef SS_MATCH
        if (!found) { PyErr_Format(PyExc_ValueError, "unknown original shape local %s", s.c_str()); throw nb::python_error(); }
    } return o;
}
ShapeStateLocals shape_state_transfer_locals(nb::tuple names,nb::dict bindings) {
    ShapeStateLocals o=shape_state_locals(std::move(names));
    for (auto item : bindings) { std::string name=nb::cast<std::string>(item.first);bool found=false;
#define SS_TRANSFER(n) if(name==#n){ss_assign(o.n,nb::borrow<nb::object>(item.second));found=true;}
        SHAPE_FIELDS(SS_TRANSFER)
#undef SS_TRANSFER
        if(!found){PyErr_Format(PyExc_ValueError,"unknown original shape local %s",name.c_str());throw nb::python_error();}
    }return o;
}
int ss_locals_traverse(PyObject* self, visitproc visit, void* arg) { Py_VISIT(Py_TYPE(self)); if (nb::inst_ready(self)) { auto& o = *nb::inst_ptr<ShapeStateLocals>(self);
#define SS_VISIT(n) Py_VISIT(o.n.ptr());
 SHAPE_FIELDS(SS_VISIT)
#undef SS_VISIT
 } return 0; }
int ss_locals_clear(PyObject* self) { if (nb::inst_ready(self)) nb::inst_ptr<ShapeStateLocals>(self)->clear(); return 0; }
PyType_Slot ss_local_slots[] = {{Py_tp_traverse,reinterpret_cast<void*>(ss_locals_traverse)},{Py_tp_clear,reinterpret_cast<void*>(ss_locals_clear)},{0,nullptr}};
#define TAKE(n) ss_assign(o.n,std::move(n))
#define PUT(n,expr) ss_assign(o.n,(expr))

// Plain instance state has one physical dictionary and weak-reference list.
// Arrays, lists, Mesh references and cached_property entries live only here.
struct ShapeNamespaceObject { PyObject_HEAD PyObject* dictionary; PyObject* weakrefs; };
int ss_namespace_traverse(PyObject* self, visitproc visit, void* arg) { auto* p = reinterpret_cast<ShapeNamespaceObject*>(self); Py_VISIT(p->dictionary); Py_VISIT(Py_TYPE(self)); return 0; }
int ss_namespace_clear(PyObject* self) { Py_CLEAR(reinterpret_cast<ShapeNamespaceObject*>(self)->dictionary); return 0; }
int ss_namespace_setdict(PyObject* self, PyObject* value, void*) { if (value && !PyDict_Check(value)) { PyErr_Format(PyExc_TypeError, "__dict__ must be set to a dictionary, not a '%.200s'", Py_TYPE(value)->tp_name); return -1; } Py_XSETREF(reinterpret_cast<ShapeNamespaceObject*>(self)->dictionary, Py_XNewRef(value)); return 0; }
PyObject* ss_namespace_getweak(PyObject* self, void*) { PyObject* p = reinterpret_cast<ShapeNamespaceObject*>(self)->weakrefs; return Py_NewRef(p ? p : Py_None); }
PyMemberDef ss_members[] = {{const_cast<char*>("__dictoffset__"),T_PYSSIZET,offsetof(ShapeNamespaceObject,dictionary),READONLY,nullptr},{const_cast<char*>("__weaklistoffset__"),T_PYSSIZET,offsetof(ShapeNamespaceObject,weakrefs),READONLY,nullptr},{nullptr,0,0,0,nullptr}};
PyGetSetDef ss_getsets[] = {{const_cast<char*>("__dict__"),PyObject_GenericGetDict,ss_namespace_setdict,nullptr,nullptr},{const_cast<char*>("__weakref__"),ss_namespace_getweak,nullptr,nullptr,nullptr},{nullptr,nullptr,nullptr,nullptr,nullptr}};
PyType_Slot ss_namespace_slots[] = {{Py_tp_members,ss_members},{Py_tp_getset,ss_getsets},{Py_tp_traverse,reinterpret_cast<void*>(ss_namespace_traverse)},{Py_tp_clear,reinterpret_cast<void*>(ss_namespace_clear)},{0,nullptr}};

nb::object shape_mesh_init(nb::object self, nb::object ndisplay, nb::dict globals, ShapeStateLocals& o) {
    TAKE(self); TAKE(ndisplay); ss_store(o.self,"_ndisplay",o.ndisplay); CALL(o.self,"clear"); return nb::none();
}
nb::object shape_mesh_ndisplay(nb::object self, nb::dict globals, ShapeStateLocals& o) { TAKE(self); return ss_attr(o.self,"_ndisplay"); }
nb::object shape_mesh_set_ndisplay(nb::object self, nb::object ndisplay, nb::dict globals, ShapeStateLocals& o) {
    TAKE(self); TAKE(ndisplay); if (ss_truth(SC(ss_attr(o.self,"ndisplay"),o.ndisplay,Py_EQ))) return nb::none();
    ss_store(o.self,"_ndisplay",o.ndisplay); CALL(o.self,"clear"); return nb::none();
}
nb::object shape_mesh_clear(nb::object self, nb::dict globals, nb::tuple literals, ShapeStateLocals& o) {
    TAKE(self); ShapeNamespace ns(std::move(globals));
    ss_store(o.self,"vertices",ss_call(ns.np("empty"),{SX(ST(SX(nb::int_(0)),SX(ss_attr(o.self,"ndisplay"))))},{KW("dtype",ns.get("CoordinateDtype"))}));
    ss_store(o.self,"vertices_centers",ss_call(ns.np("empty"),{SX(ST(SX(nb::int_(0)),SX(ss_attr(o.self,"ndisplay"))))},{KW("dtype",ns.get("CoordinateDtype"))}));
    ss_store(o.self,"vertices_offsets",ss_call(ns.np("empty"),{SX(ST(SX(nb::int_(0)),SX(ss_attr(o.self,"ndisplay"))))},{KW("dtype",ns.get("CoordinateDtype"))}));
    ss_store(o.self,"vertices_index",ss_call(ns.np("zeros"),{SX(nb::int_(1))},{KW("dtype",ns.get("IndexDtype"))}));
    ss_store(o.self,"triangles",ss_call(ns.np("empty"),{SX(SK(literals,nb::int_(0)))},{KW("dtype",ns.get("TriangleDtype"))}));
    ss_store(o.self,"triangles_index",ss_call(ns.np("zeros"),{SX(nb::int_(1))},{KW("dtype",ns.get("IndexDtype"))}));
    ss_store(o.self,"triangles_colors",ss_call(ns.np("empty"),{SX(SK(literals,nb::int_(1)))},{KW("dtype",ns.get("ShapeColorDtype"))}));
    ss_store(o.self,"triangles_z_order",ss_call(ns.np("empty"),{SX(nb::int_(0))},{KW("dtype",ns.get("ZOrderDtype"))}));
    ss_store(o.self,"displayed_triangles",ss_call(ns.np("empty"),{SX(SK(literals,nb::int_(0)))},{KW("dtype",ns.get("TriangleDtype"))}));
    ss_store(o.self,"displayed_triangles_to_shape_index",ss_call(ns.np("zeros"),{SX(nb::int_(1))},{KW("dtype",ns.get("IndexDtype"))}));
    ss_store(o.self,"displayed_triangles_colors",ss_call(ns.np("empty"),{SX(SK(literals,nb::int_(1)))},{KW("dtype",ns.get("ShapeColorDtype"))})); return nb::none();
}
nb::object shape_list_ndisplay(nb::object self, nb::dict globals, ShapeStateLocals& o) { TAKE(self); return ss_attr(o.self,"_ndisplay"); }
nb::object shape_list_slice_key(nb::object self, nb::dict globals, ShapeStateLocals& o) { TAKE(self); return ss_attr(o.self,"_slice_key"); }
nb::object shape_list_clear_cache(nb::object self, nb::dict globals, ShapeStateLocals& o) {
    TAKE(self); CALL(ss_attr(o.self,"__dict__"),"pop",SX(nb::str("_bounding_boxes")),SX(nb::none()));
    CALL(ss_attr(o.self,"__dict__"),"pop",SX(nb::str("_visible_shapes")),SX(nb::none()));
    CALL(ss_attr(o.self,"__dict__"),"pop",SX(nb::str("_visible_shapes_indices")),SX(nb::none())); return nb::none();
}
nb::object shape_list_slice_keys(nb::object self, nb::dict globals, ShapeStateLocals& o) {
    TAKE(self); ShapeNamespace ns(std::move(globals)); nb::object fn = ns.np("array"); nb::object iterator = ss_iter(ss_attr(o.self,"shapes")); nb::object saved=std::move(o.s);nb::list result;
    try { while (nb::object v = ss_next(iterator)) { PUT(s,std::move(v)); result.append(ss_attr(o.s,"slice_key")); } } catch (...) { iterator.reset();result.reset();ss_assign(o.s,std::move(saved));throw; }
    iterator.reset();ss_assign(o.s,std::move(saved));return ss_call(std::move(fn),{SX(result)});
}
nb::object shape_list_visible_indices(nb::object self, nb::dict globals, ShapeStateLocals& o) {
    TAKE(self); ShapeNamespace ns(std::move(globals)); nb::object fn = ns.np("array"); nb::object iterator = ss_iter(ss_attr(o.self,"_visible_shapes"));nb::object saved=std::move(o.s);nb::list result;
    try { while (nb::object v = ss_next(iterator)) { PUT(s,std::move(v)); result.append(SK(o.s,nb::int_(0))); } } catch (...) { iterator.reset();result.reset();ss_assign(o.s,std::move(saved));throw; }
    iterator.reset();ss_assign(o.s,std::move(saved));return ss_call(std::move(fn),{SX(result)});
}

nb::object shape_list_init(nb::object self, nb::object ndisplay, nb::dict globals, nb::tuple color_shape, ShapeStateLocals& o) {
    TAKE(self); TAKE(ndisplay); ShapeNamespace ns(std::move(globals));
    STORE(o.self,"_ndisplay",o.ndisplay); STORE(o.self,"shapes",nb::list());
    STORE(o.self,"_displayed",NP("array",SX(nb::list()))); STORE(o.self,"_slice_key",NP("array",SX(nb::list())));
    STORE(o.self,"displayed_vertices",ss_call(ns.np("array"),{SX(nb::list())},{KW("dtype",ns.get("CoordinateDtype"))}));
    STORE(o.self,"displayed_vertices_to_shape_num",ss_call(ns.np("array"),{SX(nb::list())},{KW("dtype",ns.get("IndexDtype"))}));
    STORE(o.self,"displayed_indices",ss_call(ns.np("array"),{SX(nb::list())},{KW("dtype",ns.get("IndexDtype"))}));
    STORE(o.self,"_vertices",ss_call(ns.np("empty"),{SX(ST(SX(nb::int_(0)),SX(ss_attr(o.self,"ndisplay"))))},{KW("dtype",ns.get("CoordinateDtype"))}));
    STORE(o.self,"_vertices_index",ss_call(ns.np("zeros"),{SX(nb::int_(1))},{KW("dtype",ns.get("IndexDtype"))}));
    STORE(o.self,"_z_index",ss_call(ns.np("empty"),{SX(nb::int_(0))},{KW("dtype",ns.get("IndexDtype"))}));
    STORE(o.self,"_z_order",ss_call(ns.np("empty"),{SX(nb::int_(0))},{KW("dtype",ns.get("IndexDtype"))}));
    STORE(o.self,"_mesh",ss_call(ns.get("Mesh"),{},{KW("ndisplay",ss_attr(o.self,"ndisplay"))}));
    STORE(o.self,"_edge_color",NP("empty",SX(color_shape)));
    STORE(o.self,"_face_color",NP("empty",SX(color_shape)));
    STORE(o.self,"_ShapeList__batched_level",nb::int_(0)); STORE(o.self,"_ShapeList__batch_force_call",nb::bool_(false)); STORE(o.self,"_ShapeList__update_displayed_called",nb::int_(0));
    if (!ss_truth(SN("isinstance",SX(o.data),SX(ns.get("Sequence"))))) PUT(data,SN("list",SX(o.data)));
    CALL(o.self,"add",SX(o.data)); return nb::none();
}

// Each public helper retains its original assigned locals; only the common
// primitive range construction is shared, without caching any model data.
nb::object shape_list_vertices_slice(nb::object self, nb::object shape_index, nb::dict globals, ShapeStateLocals& o) {
    TAKE(self); TAKE(shape_index); ShapeNamespace ns(std::move(globals));
    PUT(start,SK(ss_attr(o.self,"_vertices_index"),o.shape_index)); PUT(shape,SK(ss_attr(o.self,"shapes"),o.shape_index));
    return SN("slice",SX(o.start),SX(SA(o.start,SK(ss_attr(ss_attr(o.shape,"data_displayed"),"shape"),nb::int_(0)))));
}
nb::object shape_list_vertices_slice_available(nb::object self, nb::object shape_index, nb::dict globals, ShapeStateLocals& o) {
    TAKE(self); TAKE(shape_index); ShapeNamespace ns(std::move(globals)); PUT(start,SK(ss_attr(o.self,"_vertices_index"),o.shape_index));
    PUT(end,SK(ss_attr(o.self,"_vertices_index"),SA(o.shape_index,nb::int_(1)))); return SN("slice",SX(o.start),SX(o.end));
}
nb::object shape_list_mesh_vertices_slice(nb::object self, nb::object shape_index, nb::dict globals, ShapeStateLocals& o) {
    TAKE(self); TAKE(shape_index); ShapeNamespace ns(std::move(globals)); PUT(start,SK(ss_attr(ss_attr(o.self,"_mesh"),"vertices_index"),o.shape_index));
    PUT(shape,SK(ss_attr(o.self,"shapes"),o.shape_index)); return SN("slice",SX(o.start),SX(SA(o.start,ss_attr(o.shape,"vertices_count"))));
}
nb::object shape_list_mesh_vertices_slice_available(nb::object self, nb::object shape_index, nb::dict globals, ShapeStateLocals& o) {
    TAKE(self); TAKE(shape_index); ShapeNamespace ns(std::move(globals)); PUT(start,SK(ss_attr(ss_attr(o.self,"_mesh"),"vertices_index"),o.shape_index));
    PUT(end,SK(ss_attr(ss_attr(o.self,"_mesh"),"vertices_index"),SA(o.shape_index,nb::int_(1)))); return SN("slice",SX(o.start),SX(o.end));
}
nb::object shape_list_mesh_triangles_slice(nb::object self, nb::object shape_index, nb::dict globals, ShapeStateLocals& o) {
    TAKE(self); TAKE(shape_index); ShapeNamespace ns(std::move(globals)); PUT(start,SK(ss_attr(ss_attr(o.self,"_mesh"),"triangles_index"),o.shape_index));
    PUT(shape,SK(ss_attr(o.self,"shapes"),o.shape_index)); return SN("slice",SX(o.start),SX(SA(o.start,ss_attr(o.shape,"triangles_count"))));
}
nb::object shape_list_mesh_triangles_slice_available(nb::object self, nb::object shape_index, nb::dict globals, ShapeStateLocals& o) {
    TAKE(self); TAKE(shape_index); ShapeNamespace ns(std::move(globals)); PUT(start,SK(ss_attr(ss_attr(o.self,"_mesh"),"triangles_index"),o.shape_index));
    PUT(end,SK(ss_attr(ss_attr(o.self,"_mesh"),"triangles_index"),SA(o.shape_index,nb::int_(1)))); return SN("slice",SX(o.start),SX(o.end));
}
nb::object shape_list_mesh_vertices_face_slice(nb::object self, nb::object shape_index, nb::dict globals, ShapeStateLocals& o) {
    TAKE(self); TAKE(shape_index); ShapeNamespace ns(std::move(globals)); PUT(shape,SK(ss_attr(o.self,"shapes"),o.shape_index));
    PUT(start,SK(ss_attr(ss_attr(o.self,"_mesh"),"vertices_index"),o.shape_index)); return SN("slice",SX(o.start),SX(SA(o.start,ss_attr(o.shape,"face_vertices_count"))));
}
nb::object shape_list_mesh_triangles_face_slice(nb::object self, nb::object shape_index, nb::dict globals, ShapeStateLocals& o) {
    TAKE(self); TAKE(shape_index); ShapeNamespace ns(std::move(globals)); PUT(shape,SK(ss_attr(o.self,"shapes"),o.shape_index));
    PUT(start,SK(ss_attr(ss_attr(o.self,"_mesh"),"triangles_index"),o.shape_index)); return SN("slice",SX(o.start),SX(SA(o.start,ss_attr(o.shape,"face_triangles_count"))));
}
nb::object shape_list_mesh_vertices_edge_slice(nb::object self, nb::object shape_index, nb::dict globals, ShapeStateLocals& o) {
    TAKE(self); TAKE(shape_index); ShapeNamespace ns(std::move(globals)); PUT(shape,SK(ss_attr(o.self,"shapes"),o.shape_index));
    PUT(start,SA(SK(ss_attr(ss_attr(o.self,"_mesh"),"vertices_index"),o.shape_index),ss_attr(o.shape,"face_vertices_count")));
    return SN("slice",SX(o.start),SX(SA(o.start,ss_attr(o.shape,"edge_vertices_count"))));
}
nb::object shape_list_mesh_triangles_edge_slice(nb::object self, nb::object shape_index, nb::dict globals, ShapeStateLocals& o) {
    TAKE(self); TAKE(shape_index); ShapeNamespace ns(std::move(globals)); PUT(shape,SK(ss_attr(o.self,"shapes"),o.shape_index));
    PUT(start,SA(SK(ss_attr(ss_attr(o.self,"_mesh"),"triangles_index"),o.shape_index),ss_attr(o.shape,"face_triangles_count")));
    return SN("slice",SX(o.start),SX(SA(o.start,ss_attr(o.shape,"edge_triangles_count"))));
}
nb::object shape_list_mesh_triangles_range_seq(nb::object self, nb::object shape_indexes, nb::dict globals, ShapeStateLocals& o) {
    TAKE(self); TAKE(shape_indexes); ShapeNamespace ns(std::move(globals));
    if (ss_truth(SC(SB(SK(o.shape_indexes,nb::int_(-1)),SK(o.shape_indexes,nb::int_(0))),SB(SN("len",SX(o.shape_indexes)),nb::int_(1)),Py_EQ))) {
        PUT(start,SK(ss_attr(ss_attr(o.self,"_mesh"),"triangles_index"),SK(o.shape_indexes,nb::int_(0))));
        PUT(end,SK(ss_attr(ss_attr(o.self,"_mesh"),"triangles_index"),SK(o.shape_indexes,nb::int_(-1))));
        PUT(end_shape,SK(ss_attr(o.self,"shapes"),SK(o.shape_indexes,nb::int_(-1)))); return SN("slice",SX(o.start),SX(SA(o.end,ss_attr(o.end_shape,"triangles_count"))));
    }
    nb::object iterator = ss_iter(o.shape_indexes);nb::object saved=std::move(o.i);nb::list list;
    try { while (nb::object value = ss_next(iterator)) { PUT(i,std::move(value)); list.append(CALL(o.self,"_mesh_triangles_slice",SX(o.i))); } } catch (...) { iterator.reset();list.reset();ss_assign(o.i,std::move(saved));throw; }
    iterator.reset();PUT(ranges,std::move(list));ss_assign(o.i,std::move(saved));return CALL(o.self,"_slicess_to_array",SX(o.ranges));
}
nb::object shape_list_vertices_range_seq(nb::object self, nb::object shape_indexes, nb::dict globals, ShapeStateLocals& o) {
    TAKE(self); TAKE(shape_indexes); ShapeNamespace ns(std::move(globals));
    if (ss_truth(SC(SB(SK(o.shape_indexes,nb::int_(-1)),SK(o.shape_indexes,nb::int_(0))),SB(SN("len",SX(o.shape_indexes)),nb::int_(1)),Py_EQ))) {
        PUT(start,SK(ss_attr(o.self,"_vertices_index"),SK(o.shape_indexes,nb::int_(0))));
        PUT(end,SK(ss_attr(o.self,"_vertices_index"),SK(o.shape_indexes,nb::int_(-1))));
        PUT(end_shape,SK(ss_attr(o.self,"shapes"),SK(o.shape_indexes,nb::int_(-1)))); return SN("slice",SX(o.start),SX(SA(o.end,SK(ss_attr(ss_attr(o.end_shape,"data_displayed"),"shape"),nb::int_(0)))));
    }
    nb::object iterator = ss_iter(o.shape_indexes);nb::object saved=std::move(o.i);nb::list list;
    try { while (nb::object value = ss_next(iterator)) { PUT(i,std::move(value)); list.append(CALL(o.self,"_vertices_slice",SX(o.i))); } } catch (...) { iterator.reset();list.reset();ss_assign(o.i,std::move(saved));throw; }
    iterator.reset();PUT(slicess,std::move(list));ss_assign(o.i,std::move(saved));return CALL(o.self,"_slicess_to_array",SX(o.slicess));
}

// General UNPACK_SEQUENCE primitive, including source noniterable diagnostics.
std::vector<nb::object> ss_unpack(nb::object value, size_t count) {
    PyObject* raw = PyObject_GetIter(value.ptr());
    if (!raw) { nb::python_error error; if (error.matches(PyExc_TypeError) && Py_TYPE(value.ptr())->tp_iter == nullptr && !PySequence_Check(value.ptr())) { PyErr_Format(PyExc_TypeError,"cannot unpack non-iterable %.200s object",Py_TYPE(value.ptr())->tp_name); throw nb::python_error(); } throw error; }
    nb::object iterator = nb::steal<nb::object>(raw); std::vector<nb::object> values;
    for (size_t i=0;i<count;++i) { nb::object item=ss_next(iterator); if (!item.is_valid()) { PyErr_Format(PyExc_ValueError,"not enough values to unpack (expected %zu, got %zu)",count,i); throw nb::python_error(); } values.push_back(std::move(item)); }
    nb::object extra=ss_next(iterator); if (extra.is_valid()) { PyErr_Format(PyExc_ValueError,"too many values to unpack (expected %zu)",count); throw nb::python_error(); }
    iterator.reset(); value.reset(); return values;
}
nb::object shape_ensure_color_arrays(nb::object shapes, nb::dict globals, ShapeStateLocals& o) {
    TAKE(shapes); ShapeNamespace ns(std::move(globals));
    if (o.face_colors.is_none()) PUT(face_colors,NP("tile",SX(ss_call(ns.np("array"),{SX(SL(SX(nb::int_(1)),SX(nb::int_(1)),SX(nb::int_(1)),SX(nb::int_(1))))},{KW("dtype",ss_attr(ns.get("np"),"float32"))})),SX(ST(SX(SN("len",SX(o.shapes))),SX(nb::int_(1))))));
    else PUT(face_colors,ss_call(ns.np("asarray"),{SX(o.face_colors)},{KW("dtype",ss_attr(ns.get("np"),"float32"))}));
    if (o.edge_colors.is_none()) { PUT(v,nb::float_(7.0/15.0)); PUT(edge_colors,NP("tile",SX(ss_call(ns.np("array"),{SX(SL(SX(o.v),SX(o.v),SX(o.v),SX(nb::int_(1))))},{KW("dtype",ss_attr(ns.get("np"),"float32"))})),SX(ST(SX(SN("len",SX(o.shapes))),SX(nb::int_(1)))))); }
    else PUT(edge_colors,NP("asarray",SX(o.edge_colors),SX(ss_attr(ns.get("np"),"float32"))));
    nb::object a = SN("len",SX(o.face_colors)); nb::object b = SN("len",SX(o.edge_colors)); nb::object result = SC(a,b,Py_EQ); a.reset();
    if (ss_truth(result)) { result.reset(); nb::object c = SN("len",SX(o.shapes)); result = SC(b,c,Py_EQ); } b.reset();
    if (!ss_truth(result)) ss_raise(SN("ValueError",SX(nb::str("shapes, face_colors, and edge_colors must be the same length"))));
    return ST(SX(o.face_colors),SX(o.edge_colors));
}
nb::object shape_calculate_array_sizes(nb::object shapes, nb::dict globals, ShapeStateLocals& o) {
    TAKE(shapes); ShapeNamespace ns(std::move(globals)); PUT(n_vertices,nb::int_(0)); PUT(n_indices,nb::int_(0)); PUT(n_mesh_vertices,nb::int_(0)); PUT(n_face_tri,nb::int_(0)); PUT(n_edge_tri,nb::int_(0));
    nb::object iterator = ss_iter(o.shapes);
    while (nb::object value = ss_next(iterator)) {
        PUT(shape,std::move(value)); PUT(n_vertices,SI(o.n_vertices,SN("len",SX(ss_attr(o.shape,"data_displayed")))));
        PUT(n_indices,SI(o.n_indices,SN("len",SX(ss_attr(o.shape,"data")))));
        PUT(n_mesh_vertices,SI(o.n_mesh_vertices,SA(SN("len",SX(ss_attr(o.shape,"_face_vertices"))),SN("len",SX(ss_attr(o.shape,"_edge_vertices"))))));
        PUT(n_face_tri,SI(o.n_face_tri,SN("len",SX(ss_attr(o.shape,"_face_triangles")))));
        PUT(n_edge_tri,SI(o.n_edge_tri,SN("len",SX(ss_attr(o.shape,"_edge_triangles")))));
    } iterator.reset(); return ST(SX(o.n_vertices),SX(o.n_indices),SX(o.n_mesh_vertices),SX(o.n_face_tri),SX(o.n_edge_tri));
}
nb::object shape_preallocate_arrays(nb::object shapes, nb::object sizes, nb::dict globals, ShapeStateLocals& o) {
    TAKE(shapes); TAKE(sizes); ShapeNamespace ns(std::move(globals)); PUT(n_shapes,SN("len",SX(o.shapes)));
    auto values = ss_unpack(o.sizes,5); PUT(n_vertices,std::move(values[0])); PUT(_n_indices,std::move(values[1])); PUT(n_mesh_vertices,std::move(values[2])); PUT(n_face_tri,std::move(values[3])); PUT(n_edge_tri,std::move(values[4]));
    PUT(dim,SK(ss_attr(ss_attr(SK(o.shapes,nb::int_(0)),"_face_vertices"),"shape"),nb::int_(1)));
    PUT(z_index,ss_call(ns.np("empty"),{SX(o.n_shapes)},{KW("dtype",ss_attr(ns.get("np"),"int32"))}));
    PUT(vertices_index,ss_call(ns.np("empty"),{SX(o.n_shapes)},{KW("dtype",ns.get("IndexDtype"))}));
    PUT(mesh_triangles_index,ss_call(ns.np("empty"),{SX(o.n_shapes)},{KW("dtype",ns.get("IndexDtype"))}));
    PUT(mesh_vertices_index,ss_call(ns.np("empty"),{SX(o.n_shapes)},{KW("dtype",ns.get("IndexDtype"))}));
    PUT(vertices,ss_call(ns.np("empty"),{SX(ST(SX(o.n_vertices),SX(o.dim)))},{KW("dtype",ns.get("CoordinateDtype"))}));
    PUT(mesh_vertices,ss_call(ns.np("empty"),{SX(ST(SX(o.n_mesh_vertices),SX(o.dim)))},{KW("dtype",ns.get("CoordinateDtype"))}));
    PUT(mesh_vertices_centers,ss_call(ns.np("empty"),{SX(ST(SX(o.n_mesh_vertices),SX(o.dim)))},{KW("dtype",ns.get("CoordinateDtype"))}));
    PUT(mesh_vertices_offsets,ss_call(ns.np("empty"),{SX(ST(SX(o.n_mesh_vertices),SX(o.dim)))},{KW("dtype",ns.get("CoordinateDtype"))}));
    PUT(total_triangles,SA(o.n_face_tri,o.n_edge_tri));
    PUT(mesh_triangles,ss_call(ns.np("empty"),{SX(ST(SX(o.total_triangles),SX(nb::int_(3))))},{KW("dtype",ns.get("TriangleDtype"))}));
    PUT(mesh_triangles_colors,ss_call(ns.np("empty"),{SX(ST(SX(o.total_triangles),SX(nb::int_(4))))},{KW("dtype",ns.get("ShapeColorDtype"))}));
    nb::dict result; result["z_index"]=o.z_index; result["vertices"]=o.vertices; result["mesh_vertices"]=o.mesh_vertices; result["mesh_vertices_centers"]=o.mesh_vertices_centers; result["mesh_vertices_offsets"]=o.mesh_vertices_offsets;
    result["mesh_triangles"]=o.mesh_triangles; result["mesh_triangles_colors"]=o.mesh_triangles_colors; result["vertices_index"]=o.vertices_index; result["mesh_triangles_index"]=o.mesh_triangles_index; result["mesh_vertices_index"]=o.mesh_vertices_index; return result;
}
nb::object shape_fill_arrays(nb::object start_mesh_index, nb::object start_triangle_index, nb::object start_vertices_index, nb::object shapes, nb::object face_colors, nb::object edge_colors, nb::object arrays, nb::dict globals, ShapeStateLocals& o) {
    TAKE(start_mesh_index); TAKE(start_triangle_index); TAKE(start_vertices_index); TAKE(shapes); TAKE(face_colors); TAKE(edge_colors); TAKE(arrays); ShapeNamespace ns(std::move(globals));
    PUT(z_index,SK(o.arrays,nb::str("z_index"))); PUT(vertices,SK(o.arrays,nb::str("vertices")));
    PUT(mesh_vertices,SK(o.arrays,nb::str("mesh_vertices"))); PUT(mesh_vertices_centers,SK(o.arrays,nb::str("mesh_vertices_centers"))); PUT(mesh_vertices_offsets,SK(o.arrays,nb::str("mesh_vertices_offsets")));
    PUT(mesh_triangles,SK(o.arrays,nb::str("mesh_triangles"))); PUT(mesh_triangles_colors,SK(o.arrays,nb::str("mesh_triangles_colors")));
    PUT(vertices_index,SK(o.arrays,nb::str("vertices_index"))); PUT(mesh_triangles_index,SK(o.arrays,nb::str("mesh_triangles_index"))); PUT(mesh_vertices_index,SK(o.arrays,nb::str("mesh_vertices_index")));
    PUT(vertices_offset,nb::int_(0)); PUT(mesh_vertices_offset,nb::int_(0)); PUT(triangles_offset,nb::int_(0));
    nb::object iterator=ss_iter(SN("enumerate",SX(ss_call(ns.get("zip"),{SX(o.shapes),SX(o.face_colors),SX(o.edge_colors)},{KW("strict",nb::bool_(true))}))));
    while (nb::object value=ss_next(iterator)) {
        auto outer=ss_unpack(std::move(value),2); PUT(i,std::move(outer[0])); auto inner=ss_unpack(std::move(outer[1]),3); PUT(shape,std::move(inner[0])); PUT(face_color,std::move(inner[1])); PUT(edge_color,std::move(inner[2]));
        SET(o.z_index,o.i,ss_attr(o.shape,"z_index")); PUT(vertices_,ss_attr(o.shape,"data_displayed")); PUT(n_vertices,SN("len",SX(o.vertices_)));
        SET(o.vertices_index,o.i,SA(SA(o.start_vertices_index,o.vertices_offset),o.n_vertices));
        SET(o.mesh_triangles_index,o.i,SA(SA(o.start_triangle_index,o.triangles_offset),ss_attr(o.shape,"triangles_count")));
        SET(o.mesh_vertices_index,o.i,SA(SA(o.start_mesh_index,o.mesh_vertices_offset),ss_attr(o.shape,"vertices_count")));
        SET(o.vertices,SS(o.vertices_offset,SA(o.vertices_offset,o.n_vertices)),o.vertices_); PUT(vertices_offset,SI(o.vertices_offset,o.n_vertices));
        PUT(face_vertices,ss_attr(o.shape,"_face_vertices")); PUT(n_face_vertices,SN("len",SX(o.face_vertices)));
        PUT(face_vertices_slice,SN("slice",SX(o.mesh_vertices_offset),SX(SA(o.mesh_vertices_offset,o.n_face_vertices))));
        SET(o.mesh_vertices,o.face_vertices_slice,o.face_vertices); SET(o.mesh_vertices_centers,o.face_vertices_slice,o.face_vertices); SET(o.mesh_vertices_offsets,o.face_vertices_slice,nb::int_(0));
        PUT(face_triangles,SA(SA(ss_attr(o.shape,"_face_triangles"),o.start_mesh_index),o.mesh_vertices_offset)); PUT(n_face_triangles,SN("len",SX(o.face_triangles)));
        PUT(face_triangles_slice,SN("slice",SX(o.triangles_offset),SX(SA(o.triangles_offset,o.n_face_triangles))));
        SET(o.mesh_triangles,o.face_triangles_slice,o.face_triangles); SET(o.mesh_triangles_colors,o.face_triangles_slice,o.face_color);
        PUT(mesh_vertices_offset,SI(o.mesh_vertices_offset,o.n_face_vertices)); PUT(triangles_offset,SI(o.triangles_offset,o.n_face_triangles));
        PUT(edge_vertices,ss_attr(o.shape,"_edge_vertices")); PUT(edge_offsets,ss_attr(o.shape,"_edge_offsets")); PUT(n_edge_vertices,SN("len",SX(o.edge_vertices)));
        PUT(curr_vertices,SA(o.edge_vertices,SM(ss_attr(o.shape,"edge_width"),o.edge_offsets)));
        PUT(edge_vertices_slice,SN("slice",SX(o.mesh_vertices_offset),SX(SA(o.mesh_vertices_offset,o.n_edge_vertices))));
        SET(o.mesh_vertices,o.edge_vertices_slice,o.curr_vertices); SET(o.mesh_vertices_centers,o.edge_vertices_slice,o.edge_vertices); SET(o.mesh_vertices_offsets,o.edge_vertices_slice,o.edge_offsets);
        PUT(edge_triangles,SA(SA(ss_attr(o.shape,"_edge_triangles"),o.start_mesh_index),o.mesh_vertices_offset)); PUT(n_edge_triangles,SN("len",SX(o.edge_triangles)));
        PUT(edge_triangles_slice,SN("slice",SX(o.triangles_offset),SX(SA(o.triangles_offset,o.n_edge_triangles))));
        SET(o.mesh_triangles,o.edge_triangles_slice,o.edge_triangles); SET(o.mesh_triangles_colors,o.edge_triangles_slice,o.edge_color);
        PUT(mesh_vertices_offset,SI(o.mesh_vertices_offset,o.n_edge_vertices)); PUT(triangles_offset,SI(o.triangles_offset,o.n_edge_triangles));
    } iterator.reset(); return nb::none();
}

enum class ShapeGeneratorKind { SliceSizes, ShapeValidation };
struct ShapeStateGenerator {
    ShapeGeneratorKind kind; nb::object iterator, current, globals, builtins;
    bool started=false, running=false, closed=false;
    ShapeStateGenerator(ShapeGeneratorKind k,nb::object i,nb::dict g,nb::object b):kind(k),iterator(std::move(i)),globals(std::move(g)),builtins(std::move(b)){}
    ShapeStateGenerator(ShapeStateGenerator&&)=default; ~ShapeStateGenerator(){clear();}
    void clear(){auto i=std::move(iterator);auto c=std::move(current);auto g=std::move(globals);auto b=std::move(builtins);closed=true;running=false;i.reset();c.reset();g.reset();b.reset();}
    nb::object next(){
        if(closed){PyErr_SetNone(PyExc_StopIteration);throw nb::python_error();} if(running){PyErr_SetString(PyExc_ValueError,"generator already executing");throw nb::python_error();} running=true;started=true;
        try {nb::object value=ss_next(iterator);if(!value.is_valid()){clear();PyErr_SetNone(PyExc_StopIteration);throw nb::python_error();}ss_assign(current,std::move(value));ShapeNamespace ns(nb::borrow<nb::dict>(globals),builtins);
            nb::object result=kind==ShapeGeneratorKind::SliceSizes?SB(ss_attr(current,"stop"),ss_attr(current,"start")):SN("issubclass",SX(SN("type",SX(current))),SX(ns.get("Shape")));running=false;return result;
        }catch(const nb::python_error& e){bool ended=closed;closed=true;running=false;if(!ended&&e.matches(PyExc_StopIteration))collections::raise_exception(e.value(),true);throw;}
    }
    nb::object send(nb::object value){if(!started&&!value.is_none()){PyErr_SetString(PyExc_TypeError,"can't send non-None value to a just-started generator");throw nb::python_error();}return next();}
    void close(){if(running){PyErr_SetString(PyExc_ValueError,"generator already executing");throw nb::python_error();}if(!closed)clear();}
    nb::object throw_into(nb::args args){if(running){PyErr_SetString(PyExc_ValueError,"generator already executing");throw nb::python_error();}nb::object e=collections::normalize_throw(args);bool convert=!closed;closed=true;running=false;collections::raise_exception(e,convert);}
};
int ss_generator_traverse(PyObject* self,visitproc visit,void* arg){Py_VISIT(Py_TYPE(self));if(nb::inst_ready(self)){auto& s=*nb::inst_ptr<ShapeStateGenerator>(self);Py_VISIT(s.iterator.ptr());Py_VISIT(s.current.ptr());Py_VISIT(s.globals.ptr());Py_VISIT(s.builtins.ptr());}return 0;}
int ss_generator_clear(PyObject* self){if(nb::inst_ready(self))nb::inst_ptr<ShapeStateGenerator>(self)->clear();return 0;}
PyType_Slot ss_generator_slots[]={{Py_tp_traverse,reinterpret_cast<void*>(ss_generator_traverse)},{Py_tp_clear,reinterpret_cast<void*>(ss_generator_clear)},{0,nullptr}};
nb::object ss_generator(nb::object factory,ShapeStateGenerator state,const char* qualname){
    nb::object iterator=nb::cast(std::move(state));nb::object generator=ss_call(std::move(factory),{SX(iterator)});
    ss_store(generator,"__name__",nb::str("<genexpr>"));ss_store(generator,"__qualname__",nb::str(qualname));return generator;
}
nb::object shape_list_slicess_to_array(nb::object slices,nb::dict globals,nb::object generator_factory,ShapeStateLocals& o){
    TAKE(slices);ShapeNamespace ns(std::move(globals));nb::object sum=ns.get("sum");nb::object iterator=ss_iter(o.slices);
    nb::object generator=ss_generator(std::move(generator_factory),ShapeStateGenerator(ShapeGeneratorKind::SliceSizes,std::move(iterator),ns.values,ns.builtins),"ShapeList._slicess_to_array.<locals>.<genexpr>");
    nb::object count_result=ss_call(std::move(sum),{SX(generator)});generator.reset();PUT(count,std::move(count_result));PUT(res,ss_call(ns.np("empty"),{SX(o.count)},{KW("dtype",ss_attr(ns.get("np"),"int64"))}));PUT(begin,nb::int_(0));
    iterator=ss_iter(o.slices);while(nb::object value=ss_next(iterator)){PUT(r,std::move(value));PUT(end,SA(o.begin,SB(ss_attr(o.r,"stop"),ss_attr(o.r,"start"))));
        SET(o.res,SS(o.begin,o.end),NP("arange",SX(ss_attr(o.r,"start")),SX(ss_attr(o.r,"stop"))));PUT(begin,o.end);
    }iterator.reset();return o.res;
}

struct ShapeBatchIterator {
    nb::object self, globals, builtins; bool assertions=true, started=false, closed=false, running=false;
    ShapeBatchIterator(nb::object s,nb::dict g,nb::object b,bool a):self(std::move(s)),globals(std::move(g)),builtins(std::move(b)),assertions(a){}
    ShapeBatchIterator(ShapeBatchIterator&&)=default;~ShapeBatchIterator(){clear();}
    void clear(){auto s=std::move(self);auto g=std::move(globals);auto b=std::move(builtins);closed=true;running=false;s.reset();g.reset();b.reset();}
    void finish(){
        if(ss_truth(SC(ss_attr(self,"_ShapeList__batched_level"),nb::int_(1),Py_EQ))&&ss_truth(ss_attr(self,"_ShapeList__update_displayed_called"))){
            STORE(self,"_ShapeList__batch_force_call",nb::bool_(true));CALL(self,"_update_displayed");STORE(self,"_ShapeList__batch_force_call",nb::bool_(false));STORE(self,"_ShapeList__update_displayed_called",nb::int_(0));
        }
        STORE(self,"_ShapeList__batched_level",ss_binary(SX(ss_attr(self,"_ShapeList__batched_level")),SX(nb::int_(1)),PyNumber_InPlaceSubtract));
    }
    nb::object next(){
        if(closed){PyErr_SetNone(PyExc_StopIteration);throw nb::python_error();}if(running){PyErr_SetString(PyExc_ValueError,"generator already executing");throw nb::python_error();}running=true;
        try{if(!started){if(assertions&&!ss_truth(SC(ss_attr(self,"_ShapeList__batched_level"),nb::int_(0),Py_GE))){PyErr_SetNone(PyExc_AssertionError);throw nb::python_error();}
                STORE(self,"_ShapeList__batched_level",SI(ss_attr(self,"_ShapeList__batched_level"),nb::int_(1)));started=true;running=false;return nb::none();}
            finish();if(assertions&&!ss_truth(SC(ss_attr(self,"_ShapeList__batched_level"),nb::int_(0),Py_GE))){PyErr_SetNone(PyExc_AssertionError);throw nb::python_error();}clear();PyErr_SetNone(PyExc_StopIteration);throw nb::python_error();
        }catch(const nb::python_error& e){bool ended=closed;clear();if(!ended&&e.matches(PyExc_StopIteration))collections::raise_exception(e.value(),true);throw;}
    }
    nb::object send(nb::object value){if(!started&&!value.is_none()){PyErr_SetString(PyExc_TypeError,"can't send non-None value to a just-started generator");throw nb::python_error();}return next();}
    void close(){if(closed)return;if(running){PyErr_SetString(PyExc_ValueError,"generator already executing");throw nb::python_error();}running=true;
        try{if(started){nb::object e=ss_call(nb::borrow<nb::object>(PyExc_GeneratorExit));collections::HandledException handled(e);finish();}clear();}catch(const nb::python_error& e){clear();if(e.matches(PyExc_StopIteration))collections::raise_exception(e.value(),true);throw;}
    }
    nb::object throw_into(nb::args args){if(running){PyErr_SetString(PyExc_ValueError,"generator already executing");throw nb::python_error();}nb::object e=collections::normalize_throw(args);bool convert=!closed;running=true;
        try{if(started&&!closed){collections::HandledException handled(e);try{finish();}catch(const nb::python_error& cleanup){if(cleanup.value().ptr()!=e.ptr())PyException_SetContext(cleanup.value().ptr(),e.inc_ref().ptr());throw;}}clear();collections::raise_exception(e,convert);
        }catch(...){clear();throw;}
    }
};
int ss_batch_traverse(PyObject* self,visitproc visit,void* arg){Py_VISIT(Py_TYPE(self));if(nb::inst_ready(self)){auto& s=*nb::inst_ptr<ShapeBatchIterator>(self);Py_VISIT(s.self.ptr());Py_VISIT(s.globals.ptr());Py_VISIT(s.builtins.ptr());}return 0;}
int ss_batch_clear(PyObject* self){if(nb::inst_ready(self))nb::inst_ptr<ShapeBatchIterator>(self)->clear();return 0;}
PyType_Slot ss_batch_slots[]={{Py_tp_traverse,reinterpret_cast<void*>(ss_batch_traverse)},{Py_tp_clear,reinterpret_cast<void*>(ss_batch_clear)},{0,nullptr}};
nb::object shape_list_batched_updates(nb::object self,nb::dict globals,bool assertions,ShapeStateLocals& o){
    TAKE(self);ShapeNamespace ns(std::move(globals));return nb::cast(ShapeBatchIterator(o.self,ns.values,ns.builtins,assertions));
}
nb::object ss_cell(nb::object getter){
    if(!PyFunction_Check(getter.ptr())){PyErr_SetString(PyExc_TypeError,"cell getter must be a genuine function");throw nb::python_error();}PyObject* closure=PyFunction_GetClosure(getter.ptr());
    if(!closure||PyTuple_GET_SIZE(closure)!=1||!PyCell_Check(PyTuple_GET_ITEM(closure,0))){PyErr_SetString(PyExc_TypeError,"cell getter must capture one actual cell");throw nb::python_error();}return nb::borrow<nb::object>(PyTuple_GET_ITEM(closure,0));
}
nb::object ss_read_cell(nb::object cell,const char* name){PyObject* value=PyCell_GET(cell.ptr());if(value)return nb::borrow<nb::object>(value);
    nb::object text=ss_check(PyUnicode_FromFormat("cannot access free variable '%s' where it is not associated with a value in enclosing scope",name));nb::object e=ss_call(nb::borrow<nb::object>(PyExc_NameError),{SX(text)});ss_store(e,"name",nb::str(name));ss_raise(e);
}
// This owned WITH boundary consumes the manager and enter temporaries. Saved
// exit alone remains live through suppression truth and handled-state restore.
template<class Function> void ss_with_context(nb::object manager,Function body){
    nb::object enter=collections::context_method(manager,"__enter__",false);
    nb::object exit=collections::context_method(manager,"__exit__",true);
    manager.reset();nb::object entered=ss_call(std::move(enter));entered.reset();
    try{body();}
    catch(const nb::python_error& original){
        collections::HandledException handled(original.value());nb::object traceback=original.traceback();if(!traceback.is_valid())traceback=nb::none();
        try{if(!ss_truth(ss_call(exit,{SX(nb::borrow<nb::object>(original.type())),SX(nb::borrow<nb::object>(original.value())),SX(traceback)})))throw;}
        catch(const nb::python_error& cleanup){if(cleanup.value().ptr()!=original.value().ptr())PyException_SetContext(cleanup.value().ptr(),original.value().inc_ref().ptr());throw;}
        return;
    }ss_call(std::move(exit),{SX(nb::none()),SX(nb::none()),SX(nb::none())});
}
nb::object shape_batch_wrapped(nb::object self,nb::tuple args,nb::dict kwargs,nb::object method_getter,nb::dict globals,ShapeStateLocals& o){
    TAKE(self);PUT(args,std::move(args));PUT(kwargs,std::move(kwargs));nb::object cell=ss_cell(std::move(method_getter));nb::object result=nb::none();
    ss_with_context(CALL(o.self,"batched_updates"),[&]{nb::object method=ss_read_cell(cell,"meth");nb::list positional;positional.append(o.self);
        nb::object iterator=ss_iter(o.args);while(nb::object value=ss_next(iterator))positional.append(std::move(value));iterator.reset();nb::object expanded=ss_check(PySequence_Tuple(positional.ptr()));positional.reset();
        nb::object copied=ss_check(PyDict_Copy(o.kwargs.ptr()));result=ss_check(PyObject_Call(method.ptr(),expanded.ptr(),copied.ptr()));
    });return result;
}
nb::object shape_list_set_ndisplay(nb::object self,nb::object ndisplay,nb::dict globals,ShapeStateLocals& o){
    TAKE(self);TAKE(ndisplay);if(ss_truth(SC(ss_attr(o.self,"ndisplay"),o.ndisplay,Py_EQ)))return nb::none();
    STORE(o.self,"_ndisplay",o.ndisplay);STORE(ss_attr(o.self,"_mesh"),"ndisplay",ss_attr(o.self,"ndisplay"));
    PUT(shapes,ss_attr(o.self,"shapes"));PUT(face_color,ss_attr(o.self,"_face_color"));PUT(edge_color,ss_attr(o.self,"_edge_color"));
    ss_with_context(CALL(o.self,"batched_updates"),[&]{nb::object iterator=ss_iter(o.shapes);while(nb::object value=ss_next(iterator)){PUT(shape,std::move(value));STORE(o.shape,"ndisplay",ss_attr(o.self,"ndisplay"));}iterator.reset();
        CALL(o.self,"remove_all");ss_call(ss_attr(o.self,"_add_multiple_shapes"),{SX(o.shapes)},{KW("face_colors",o.face_color),KW("edge_colors",o.edge_color)});
    });return nb::none();
}
nb::object shape_list_set_slice_key(nb::object self,nb::dict globals,ShapeStateLocals& o){
    TAKE(self);ShapeNamespace ns(std::move(globals));PUT(slice_key,SN("list",SX(o.slice_key)));
    if(!ss_truth(NP("array_equal",SX(ss_attr(o.self,"_slice_key")),SX(o.slice_key)))){STORE(o.self,"_slice_key",o.slice_key);CALL(o.self,"_clear_cache");CALL(o.self,"_update_displayed");}return nb::none();
}

nb::object shape_list_update_triangle_map(nb::object self,nb::object displayed_indices,nb::dict globals,ShapeStateLocals& o){
    TAKE(self);TAKE(displayed_indices);ShapeNamespace ns(std::move(globals));
    if(ss_truth(SC(SK(ss_attr(ss_attr(ss_attr(o.self,"_mesh"),"displayed_triangles_to_shape_index"),"shape"),nb::int_(0)),SK(ss_attr(ss_attr(ss_attr(o.self,"_mesh"),"displayed_triangles"),"shape"),nb::int_(0)),Py_NE))){
        STORE(ss_attr(o.self,"_mesh"),"displayed_triangles_to_shape_index",ss_call(ns.np("full"),{SX(SK(ss_attr(ss_attr(ss_attr(o.self,"_mesh"),"displayed_triangles"),"shape"),nb::int_(0))),SX(nb::int_(-1))},{KW("dtype",ns.get("IndexDtype"))}));
    }
    PUT(shift_idx,nb::int_(0));nb::object iterator=ss_iter(o.displayed_indices);
    while(nb::object value=ss_next(iterator)){PUT(i,std::move(value));PUT(begin,SK(ss_attr(ss_attr(o.self,"_mesh"),"triangles_index"),o.i));PUT(end,SK(ss_attr(ss_attr(o.self,"_mesh"),"triangles_index"),SA(o.i,nb::int_(1))));PUT(elem_num,SB(o.end,o.begin));
        SET(ss_attr(ss_attr(o.self,"_mesh"),"displayed_triangles_to_shape_index"),SS(o.shift_idx,SA(o.shift_idx,o.elem_num)),o.i);PUT(shift_idx,SI(o.shift_idx,o.elem_num));
    }iterator.reset();return nb::none();
}
nb::object shape_list_update_vertex_map(nb::object self,nb::object displayed_indices,nb::dict globals,ShapeStateLocals& o){
    TAKE(self);TAKE(displayed_indices);ShapeNamespace ns(std::move(globals));
    if(ss_truth(SC(SK(ss_attr(ss_attr(o.self,"displayed_vertices_to_shape_num"),"shape"),nb::int_(0)),SK(ss_attr(ss_attr(o.self,"displayed_vertices"),"shape"),nb::int_(0)),Py_NE))){
        STORE(o.self,"displayed_vertices_to_shape_num",ss_call(ns.np("full"),{SX(SK(ss_attr(ss_attr(o.self,"displayed_vertices"),"shape"),nb::int_(0))),SX(nb::int_(-1))},{KW("dtype",ns.get("IndexDtype"))}));
    }
    PUT(shift_idx,nb::int_(0));nb::object iterator=ss_iter(o.displayed_indices);
    while(nb::object value=ss_next(iterator)){PUT(i,std::move(value));PUT(begin,SK(ss_attr(o.self,"_vertices_index"),o.i));PUT(end,SK(ss_attr(o.self,"_vertices_index"),SA(o.i,nb::int_(1))));PUT(elem_num,SB(o.end,o.begin));
        SET(ss_attr(o.self,"displayed_vertices_to_shape_num"),SS(o.shift_idx,SA(o.shift_idx,o.elem_num)),o.i);PUT(shift_idx,SI(o.shift_idx,o.elem_num));
    }iterator.reset();return nb::none();
}
nb::object shape_list_update_displayed(nb::object self,nb::dict globals,bool assertions,ShapeStateLocals& o){
    TAKE(self);ShapeNamespace ns(std::move(globals));
    if(assertions&&!ss_truth(SC(ss_attr(o.self,"_ShapeList__batched_level"),nb::int_(1),Py_GE))){PyErr_SetObject(PyExc_AssertionError,nb::str("call _update_displayed from within self.batched_updates context manager").ptr());throw nb::python_error();}
    if(!ss_truth(ss_attr(o.self,"_ShapeList__batch_force_call"))){STORE(o.self,"_ShapeList__update_displayed_called",SI(ss_attr(o.self,"_ShapeList__update_displayed_called"),nb::int_(1)));return nb::none();}
    PUT(slice_key,NP("array",SX(SL(SX(ss_attr(o.self,"slice_key")),SX(ss_attr(o.self,"slice_key"))))));
    if(ss_truth(SC(SN("len",SX(ss_attr(o.self,"shapes"))),nb::int_(0),Py_GT))){STORE(o.self,"_displayed",ss_call(ns.np("all"),{SX(SC(NP("abs",SX(SB(ss_attr(o.self,"slice_keys"),o.slice_key))),nb::float_(0.5),Py_LT))},{KW("axis",ST(SX(nb::int_(1)),SX(nb::int_(2))))}));}
    else STORE(o.self,"_displayed",NP("array",SX(nb::list())));
    PUT(disp_indices,SK(NP("nonzero",SX(ss_attr(o.self,"_displayed"))),nb::int_(0)));PUT(z_order,ss_attr(ss_attr(o.self,"_mesh"),"triangles_z_order"));
    if(ss_truth(SC(ss_attr(o.disp_indices,"size"),nb::int_(0),Py_EQ))){
        PUT(triangle_ranges,ss_call(ns.np("array"),{SX(nb::list())},{KW("dtype",ss_attr(ns.get("np"),"int64"))}));
        PUT(vertices_range,ss_call(ns.np("array"),{SX(nb::list())},{KW("dtype",ss_attr(ns.get("np"),"int64"))}));
    }else{PUT(triangle_ranges,CALL(o.self,"_mesh_triangles_range_seq",SX(o.disp_indices)));PUT(vertices_range,CALL(o.self,"_vertices_range_seq",SX(o.disp_indices)));}
    STORE(ss_attr(o.self,"_mesh"),"displayed_triangles",SK(ss_attr(ss_attr(o.self,"_mesh"),"triangles"),SK(o.z_order,o.triangle_ranges)));
    CALL(o.self,"_update_displayed_triangles_to_shape_index",SX(o.disp_indices));
    STORE(ss_attr(o.self,"_mesh"),"displayed_triangles_colors",SK(ss_attr(ss_attr(o.self,"_mesh"),"triangles_colors"),SK(o.z_order,o.triangle_ranges)));
    STORE(o.self,"displayed_vertices",SK(ss_attr(o.self,"_vertices"),o.vertices_range));CALL(o.self,"_update_displayed_vertices_to_shape_num",SX(o.disp_indices));
    STORE(o.self,"displayed_index",SK(ss_attr(o.self,"_vertices_index"),o.disp_indices));return nb::none();
}
nb::object shape_list_add(nb::object self,nb::object shape,nb::object face_color,nb::object edge_color,nb::object shape_index,nb::object z_refresh,nb::dict globals,ShapeStateLocals& o){
    TAKE(self);TAKE(shape);TAKE(face_color);TAKE(edge_color);TAKE(shape_index);TAKE(z_refresh);ShapeNamespace ns(std::move(globals));
    if(ss_truth(SN("issubclass",SX(SN("type",SX(o.shape))),SX(ns.get("Shape"))))){ss_call(ss_attr(o.self,"_add_single_shape"),{},{KW("shape",o.shape),KW("face_color",o.face_color),KW("edge_color",o.edge_color),KW("shape_index",o.shape_index),KW("z_refresh",o.z_refresh)});}
    else if(ss_truth(SN("isinstance",SX(o.shape),SX(ns.get("Iterable"))))){
        if(!o.shape_index.is_none())ss_raise(SN("ValueError",SX(nb::str("shape_index must be None when adding multiple shapes"))));
        ss_call(ss_attr(o.self,"_add_multiple_shapes"),{},{KW("shapes",o.shape),KW("face_colors",o.face_color),KW("edge_colors",o.edge_color),KW("z_refresh",o.z_refresh)});
    }else ss_raise(SN("TypeError",SX(nb::str("Cannot add single nor multiple shape"))));return nb::none();
}
nb::object shape_list_add_single(nb::object self,nb::object shape,nb::object face_color,nb::object edge_color,nb::object shape_index,nb::dict globals,ShapeStateLocals& o){
    TAKE(self);TAKE(shape);TAKE(face_color);TAKE(edge_color);TAKE(shape_index);ShapeNamespace ns(std::move(globals));
    if(!ss_truth(SN("issubclass",SX(SN("type",SX(o.shape))),SX(ns.get("Shape")))))ss_raise(SN("TypeError",SX(nb::str("shape must be subclass of Shape"))));
    if(o.shape_index.is_none()){
        CALL(ss_attr(o.self,"shapes"),"append",SX(o.shape));STORE(o.self,"_z_index",NP("append",SX(ss_attr(o.self,"_z_index")),SX(ss_attr(o.shape,"z_index"))));
        if(o.face_color.is_none())PUT(face_color,NP("array",SX(SL(SX(nb::int_(1)),SX(nb::int_(1)),SX(nb::int_(1)),SX(nb::int_(1))))));
        STORE(o.self,"_face_color",NP("vstack",SX(SL(SX(ss_attr(o.self,"_face_color")),SX(o.face_color)))));
        if(o.edge_color.is_none())PUT(edge_color,NP("array",SX(SL(SX(nb::int_(0)),SX(nb::int_(0)),SX(nb::int_(0)),SX(nb::int_(1))))));
        STORE(o.self,"_edge_color",NP("vstack",SX(SL(SX(ss_attr(o.self,"_edge_color")),SX(o.edge_color)))));
    }else{
        PUT(z_refresh,nb::bool_(false));SET(ss_attr(o.self,"shapes"),o.shape_index,o.shape);SET(ss_attr(o.self,"_z_index"),o.shape_index,ss_attr(o.shape,"z_index"));
        if(o.face_color.is_none())PUT(face_color,SK(ss_attr(o.self,"_face_color"),o.shape_index));else SET(ss_attr(o.self,"_face_color"),ST(SX(o.shape_index),SX(SS(nb::none(),nb::none()))),o.face_color);
        if(o.edge_color.is_none())PUT(edge_color,SK(ss_attr(o.self,"_edge_color"),o.shape_index));else SET(ss_attr(o.self,"_edge_color"),ST(SX(o.shape_index),SX(SS(nb::none(),nb::none()))),o.edge_color);
    }
    PUT(vertices_,ss_attr(o.shape,"data_displayed"));
    STORE(o.self,"_vertices_index",ss_call(ns.np("append"),{SX(ss_attr(o.self,"_vertices_index")),SX(SL(SX(SA(SN("len",SX(ss_attr(o.self,"_vertices"))),SN("len",SX(o.vertices_))))))},{KW("axis",nb::int_(0))}));
    STORE(o.self,"_vertices",ss_call(ns.np("append"),{SX(ss_attr(o.self,"_vertices")),SX(o.vertices_)},{KW("axis",nb::int_(0))}));
    PUT(m,SN("len",SX(ss_attr(ss_attr(o.self,"_mesh"),"vertices"))));PUT(vertices,ss_attr(o.shape,"_face_vertices"));
    STORE(ss_attr(o.self,"_mesh"),"vertices_index",ss_call(ns.np("append"),{SX(ss_attr(ss_attr(o.self,"_mesh"),"vertices_index")),SX(SL(SX(SA(SN("len",SX(ss_attr(ss_attr(o.self,"_mesh"),"vertices"))),ss_attr(o.shape,"vertices_count")))))},{KW("axis",nb::int_(0))}));
    STORE(ss_attr(o.self,"_mesh"),"vertices",ss_call(ns.np("append"),{SX(ss_attr(ss_attr(o.self,"_mesh"),"vertices")),SX(o.vertices)},{KW("axis",nb::int_(0))}));
    PUT(vertices,ss_attr(o.shape,"_face_vertices"));STORE(ss_attr(o.self,"_mesh"),"vertices_centers",ss_call(ns.np("append"),{SX(ss_attr(ss_attr(o.self,"_mesh"),"vertices_centers")),SX(o.vertices)},{KW("axis",nb::int_(0))}));
    PUT(vertices,NP("zeros",SX(ss_attr(ss_attr(o.shape,"_face_vertices"),"shape"))));STORE(ss_attr(o.self,"_mesh"),"vertices_offsets",ss_call(ns.np("append"),{SX(ss_attr(ss_attr(o.self,"_mesh"),"vertices_offsets")),SX(o.vertices)},{KW("axis",nb::int_(0))}));
    PUT(triangles,SA(ss_attr(o.shape,"_face_triangles"),o.m));
    STORE(ss_attr(o.self,"_mesh"),"triangles_index",ss_call(ns.np("append"),{SX(ss_attr(ss_attr(o.self,"_mesh"),"triangles_index")),SX(SL(SX(SA(SN("len",SX(ss_attr(ss_attr(o.self,"_mesh"),"triangles"))),ss_attr(o.shape,"triangles_count")))))},{KW("axis",nb::int_(0))}));
    STORE(ss_attr(o.self,"_mesh"),"triangles",ss_call(ns.np("append"),{SX(ss_attr(ss_attr(o.self,"_mesh"),"triangles")),SX(o.triangles)},{KW("axis",nb::int_(0))}));
    PUT(color_array,ss_call(ns.np("repeat"),{SX(SL(SX(o.face_color))),SX(SN("len",SX(o.triangles)))},{KW("axis",nb::int_(0))}));
    STORE(ss_attr(o.self,"_mesh"),"triangles_colors",ss_call(ns.np("append"),{SX(ss_attr(ss_attr(o.self,"_mesh"),"triangles_colors")),SX(o.color_array)},{KW("axis",nb::int_(0))}));
    PUT(m,SN("len",SX(ss_attr(ss_attr(o.self,"_mesh"),"vertices"))));PUT(vertices,SA(ss_attr(o.shape,"_edge_vertices"),SM(ss_attr(o.shape,"edge_width"),ss_attr(o.shape,"_edge_offsets"))));
    STORE(ss_attr(o.self,"_mesh"),"vertices",ss_call(ns.np("append"),{SX(ss_attr(ss_attr(o.self,"_mesh"),"vertices")),SX(o.vertices)},{KW("axis",nb::int_(0))}));
    PUT(vertices,ss_attr(o.shape,"_edge_vertices"));STORE(ss_attr(o.self,"_mesh"),"vertices_centers",ss_call(ns.np("append"),{SX(ss_attr(ss_attr(o.self,"_mesh"),"vertices_centers")),SX(o.vertices)},{KW("axis",nb::int_(0))}));
    PUT(vertices,ss_attr(o.shape,"_edge_offsets"));STORE(ss_attr(o.self,"_mesh"),"vertices_offsets",ss_call(ns.np("append"),{SX(ss_attr(ss_attr(o.self,"_mesh"),"vertices_offsets")),SX(o.vertices)},{KW("axis",nb::int_(0))}));
    PUT(triangles,SA(ss_attr(o.shape,"_edge_triangles"),o.m));STORE(ss_attr(o.self,"_mesh"),"triangles",ss_call(ns.np("append"),{SX(ss_attr(ss_attr(o.self,"_mesh"),"triangles")),SX(o.triangles)},{KW("axis",nb::int_(0))}));
    PUT(color_array,ss_call(ns.np("repeat"),{SX(SL(SX(o.edge_color))),SX(SN("len",SX(o.triangles)))},{KW("axis",nb::int_(0))}));STORE(ss_attr(o.self,"_mesh"),"triangles_colors",ss_call(ns.np("append"),{SX(ss_attr(ss_attr(o.self,"_mesh"),"triangles_colors")),SX(o.color_array)},{KW("axis",nb::int_(0))}));
    if(ss_truth(o.z_refresh))CALL(o.self,"_update_z_order");CALL(o.self,"_clear_cache");return nb::none();
}

nb::object shape_list_extend_meshes(nb::object self,nb::object face_colors,nb::object edge_colors,nb::object arrays,nb::dict globals,ShapeStateLocals& o){
    TAKE(self);TAKE(face_colors);TAKE(edge_colors);TAKE(arrays);ShapeNamespace ns(std::move(globals));
    PUT(z_index,SK(o.arrays,nb::str("z_index")));PUT(vertices,SK(o.arrays,nb::str("vertices")));PUT(mesh_vertices,SK(o.arrays,nb::str("mesh_vertices")));PUT(mesh_vertices_centers,SK(o.arrays,nb::str("mesh_vertices_centers")));PUT(mesh_vertices_offsets,SK(o.arrays,nb::str("mesh_vertices_offsets")));
    PUT(mesh_triangles,SK(o.arrays,nb::str("mesh_triangles")));PUT(mesh_triangles_colors,SK(o.arrays,nb::str("mesh_triangles_colors")));PUT(vertices_index,SK(o.arrays,nb::str("vertices_index")));PUT(mesh_triangles_index,SK(o.arrays,nb::str("mesh_triangles_index")));PUT(mesh_vertices_index,SK(o.arrays,nb::str("mesh_vertices_index")));
    STORE(o.self,"_z_index",ss_call(ns.np("append"),{SX(ss_attr(o.self,"_z_index")),SX(o.z_index)},{KW("axis",nb::int_(0))}));
    STORE(o.self,"_face_color",NP("vstack",SX(ST(SX(ss_attr(o.self,"_face_color")),SX(o.face_colors)))));
    STORE(o.self,"_edge_color",NP("vstack",SX(ST(SX(ss_attr(o.self,"_edge_color")),SX(o.edge_colors)))));
    STORE(o.self,"_vertices",NP("vstack",SX(ST(SX(ss_attr(o.self,"_vertices")),SX(o.vertices)))));
    STORE(o.self,"_vertices_index",ss_call(ns.np("append"),{SX(ss_attr(o.self,"_vertices_index")),SX(o.vertices_index)},{KW("axis",nb::int_(0))}));
    STORE(ss_attr(o.self,"_mesh"),"vertices",NP("vstack",SX(ST(SX(ss_attr(ss_attr(o.self,"_mesh"),"vertices")),SX(o.mesh_vertices)))));
    STORE(ss_attr(o.self,"_mesh"),"vertices_centers",NP("vstack",SX(ST(SX(ss_attr(ss_attr(o.self,"_mesh"),"vertices_centers")),SX(o.mesh_vertices_centers)))));
    STORE(ss_attr(o.self,"_mesh"),"vertices_offsets",NP("vstack",SX(ST(SX(ss_attr(ss_attr(o.self,"_mesh"),"vertices_offsets")),SX(o.mesh_vertices_offsets)))));
    STORE(ss_attr(o.self,"_mesh"),"vertices_index",ss_call(ns.np("append"),{SX(ss_attr(ss_attr(o.self,"_mesh"),"vertices_index")),SX(o.mesh_vertices_index)},{KW("axis",nb::int_(0))}));
    STORE(ss_attr(o.self,"_mesh"),"triangles",NP("vstack",SX(ST(SX(ss_attr(ss_attr(o.self,"_mesh"),"triangles")),SX(o.mesh_triangles)))));
    STORE(ss_attr(o.self,"_mesh"),"triangles_index",ss_call(ns.np("append"),{SX(ss_attr(ss_attr(o.self,"_mesh"),"triangles_index")),SX(o.mesh_triangles_index)},{KW("axis",nb::int_(0))}));
    STORE(ss_attr(o.self,"_mesh"),"triangles_colors",NP("vstack",SX(ST(SX(ss_attr(ss_attr(o.self,"_mesh"),"triangles_colors")),SX(o.mesh_triangles_colors)))));return nb::none();
}
nb::object shape_list_add_multiple(nb::object self,nb::object shapes,nb::object z_refresh,nb::dict globals,nb::object generator_factory,ShapeStateLocals& o){
    TAKE(self);TAKE(shapes);TAKE(z_refresh);ShapeNamespace ns(std::move(globals));if(ss_truth(SC(SN("len",SX(o.shapes)),nb::int_(0),Py_EQ)))return nb::none();
    nb::object all=ns.get("all");nb::object iterator=ss_iter(o.shapes);nb::object generator=ss_generator(std::move(generator_factory),ShapeStateGenerator(ShapeGeneratorKind::ShapeValidation,std::move(iterator),ns.values,ns.builtins),"ShapeList._add_multiple_shapes.<locals>.<genexpr>");
    nb::object valid=ss_call(std::move(all),{SX(generator)});generator.reset();if(!ss_truth(std::move(valid)))ss_raise(SN("ValueError",SX(nb::str("all shapes must be subclass of Shape"))));
    auto colors=ss_unpack(SN("_ensure_color_arrays",SX(o.shapes),SX(o.face_colors),SX(o.edge_colors)),2);PUT(face_colors,std::move(colors[0]));PUT(edge_colors,std::move(colors[1]));
    PUT(sizes,SN("_calculate_array_sizes",SX(o.shapes)));PUT(arrays,SN("_preallocate_arrays",SX(o.shapes),SX(o.sizes)));
    SN("_fill_arrays",SX(SN("len",SX(ss_attr(ss_attr(o.self,"_mesh"),"vertices")))),SX(SN("len",SX(ss_attr(ss_attr(o.self,"_mesh"),"triangles")))),SX(SN("len",SX(ss_attr(o.self,"_vertices")))),SX(o.shapes),SX(o.face_colors),SX(o.edge_colors),SX(o.arrays));
    CALL(o.self,"_extend_meshes",SX(o.face_colors),SX(o.edge_colors),SX(o.arrays));CALL(ss_attr(o.self,"shapes"),"extend",SX(o.shapes));if(ss_truth(o.z_refresh))CALL(o.self,"_update_z_order");CALL(o.self,"_clear_cache");return nb::none();
}
nb::object shape_list_remove_all(nb::object self,nb::dict globals,nb::tuple color_shape,ShapeStateLocals& o){
    TAKE(self);ShapeNamespace ns(std::move(globals));STORE(o.self,"shapes",nb::list());STORE(o.self,"_vertices",NP("empty",SX(ST(SX(nb::int_(0)),SX(ss_attr(o.self,"ndisplay"))))));
    STORE(o.self,"_vertices_index",ss_call(ns.np("zeros"),{SX(nb::int_(1))},{KW("dtype",ns.get("IndexDtype"))}));STORE(o.self,"_z_index",ss_call(ns.np("empty"),{SX(nb::int_(0))},{KW("dtype",ns.get("IndexDtype"))}));STORE(o.self,"_z_order",ss_call(ns.np("empty"),{SX(nb::int_(0))},{KW("dtype",ns.get("ZOrderDtype"))}));
    STORE(o.self,"_edge_color",ss_call(ns.np("empty"),{SX(color_shape)},{KW("dtype",ns.get("ShapeColorDtype"))}));STORE(o.self,"_face_color",ss_call(ns.np("empty"),{SX(color_shape)},{KW("dtype",ns.get("ShapeColorDtype"))}));CALL(ss_attr(o.self,"_mesh"),"clear");CALL(o.self,"_update_displayed");return nb::none();
}
nb::object shape_list_update_z_order(nb::object self,nb::dict globals,ShapeStateLocals& o){
    TAKE(self);ShapeNamespace ns(std::move(globals));STORE(o.self,"_z_order",ss_call(ns.np("argsort"),{SX(ss_attr(o.self,"_z_index"))},{KW("kind",nb::str("stable"))}));
    if(ss_truth(SC(SN("len",SX(ss_attr(o.self,"_z_order"))),nb::int_(0),Py_EQ))){STORE(ss_attr(o.self,"_mesh"),"triangles_z_order",ss_call(ns.np("empty"),{SX(nb::int_(0))},{KW("dtype",ns.get("ZOrderDtype"))}));}
    else{PUT(idx,ss_attr(ss_attr(o.self,"_mesh"),"triangles_index"));PUT(counts,ss_call(ns.np("empty"),{SX(ss_attr(o.idx,"shape"))},{KW("dtype",ss_attr(o.idx,"dtype"))}));
        SET(o.counts,SS(nb::none(),nb::int_(-1)),SB(SK(o.idx,SS(nb::int_(1),nb::none())),SK(o.idx,SS(nb::none(),nb::int_(-1)))));
        SET(o.counts,nb::int_(-1),SB(SN("len",SX(ss_attr(ss_attr(o.self,"_mesh"),"triangles"))),SK(o.idx,nb::int_(-1))));
        nb::object iterator=ss_iter(ss_attr(o.self,"_z_order"));nb::object saved=std::move(o.z);nb::list list;
        try{while(nb::object value=ss_next(iterator)){PUT(z,std::move(value));list.append(NP("arange",SX(SK(o.idx,o.z)),SX(SA(SK(o.idx,o.z),SK(o.counts,o.z)))));}}catch(...){iterator.reset();list.reset();ss_assign(o.z,std::move(saved));throw;}
        iterator.reset();PUT(triangles_z_order,std::move(list));ss_assign(o.z,std::move(saved));STORE(ss_attr(o.self,"_mesh"),"triangles_z_order",NP("concatenate",SX(o.triangles_z_order)));
    }CALL(o.self,"_update_displayed");return nb::none();
}
} // anonymous namespace

void bind_viewer_shape_state(nb::module_& m){
    PyType_Spec list_spec={"naparixx._native.ShapeListNamespaceBase",static_cast<int>(sizeof(ShapeNamespaceObject)),0,Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE|Py_TPFLAGS_HAVE_GC,ss_namespace_slots};
    m.attr("ShapeListNamespaceBase")=ss_check(PyType_FromSpec(&list_spec));
    PyType_Spec mesh_spec={"naparixx._native.ShapeMeshNamespaceBase",static_cast<int>(sizeof(ShapeNamespaceObject)),0,Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE|Py_TPFLAGS_HAVE_GC,ss_namespace_slots};
    m.attr("ShapeMeshNamespaceBase")=ss_check(PyType_FromSpec(&mesh_spec));
    auto locals=nb::class_<ShapeStateLocals>(m,"ShapeStateLocals",nb::is_weak_referenceable(),nb::type_slots(ss_local_slots));
#define SS_PROP(n) locals.def_prop_ro(#n,[](const ShapeStateLocals& o){return o.n.is_valid()?o.n:nb::none();});
    SHAPE_FIELDS(SS_PROP)
#undef SS_PROP
    nb::class_<ShapeStateGenerator>(m,"ShapeStateGeneratorIterator",nb::is_weak_referenceable(),nb::type_slots(ss_generator_slots))
        .def("__iter__",[](nb::handle self){return nb::borrow<nb::object>(self);}).def("__next__",&ShapeStateGenerator::next).def("send",&ShapeStateGenerator::send,nb::arg("value").none()).def("throw",&ShapeStateGenerator::throw_into).def("close",&ShapeStateGenerator::close);
    nb::class_<ShapeBatchIterator>(m,"ShapeBatchIterator",nb::is_weak_referenceable(),nb::type_slots(ss_batch_slots))
        .def("__iter__",[](nb::handle self){return nb::borrow<nb::object>(self);}).def("__next__",&ShapeBatchIterator::next).def("send",&ShapeBatchIterator::send,nb::arg("value").none()).def("throw",&ShapeBatchIterator::throw_into).def("close",&ShapeBatchIterator::close);
    m.def("shape_state_locals",&shape_state_locals,nb::arg("layout"));
    m.def("shape_state_transfer_locals",&shape_state_transfer_locals,nb::arg("layout"),nb::arg("bindings"));
#define ARG(n) nb::arg(#n).none()
#define GLOBALS nb::arg("namespace")
#define OWNER nb::arg("owner")
    m.def("shape_mesh_init",&shape_mesh_init,ARG(self),ARG(ndisplay),GLOBALS,OWNER);
    m.def("shape_mesh_clear",&shape_mesh_clear,ARG(self),GLOBALS,nb::arg("literals"),OWNER);
    m.def("shape_mesh_ndisplay",&shape_mesh_ndisplay,ARG(self),GLOBALS,OWNER);
    m.def("shape_mesh_set_ndisplay",&shape_mesh_set_ndisplay,ARG(self),ARG(ndisplay),GLOBALS,OWNER);
    m.def("shape_list_init",&shape_list_init,ARG(self),ARG(ndisplay),GLOBALS,nb::arg("color_shape"),OWNER);
    m.def("shape_list_ndisplay",&shape_list_ndisplay,ARG(self),GLOBALS,OWNER);
    m.def("shape_list_set_ndisplay",&shape_list_set_ndisplay,ARG(self),ARG(ndisplay),GLOBALS,OWNER);
    m.def("shape_list_slice_keys",&shape_list_slice_keys,ARG(self),GLOBALS,OWNER);
    m.def("shape_list_slice_key",&shape_list_slice_key,ARG(self),GLOBALS,OWNER);
    m.def("shape_list_set_slice_key",&shape_list_set_slice_key,ARG(self),GLOBALS,OWNER);
    m.def("shape_list_clear_cache",&shape_list_clear_cache,ARG(self),GLOBALS,OWNER);
    m.def("shape_list_visible_indices",&shape_list_visible_indices,ARG(self),GLOBALS,OWNER);
    m.def("shape_list_vertices_slice",&shape_list_vertices_slice,ARG(self),ARG(shape_index),GLOBALS,OWNER);
    m.def("shape_list_vertices_slice_available",&shape_list_vertices_slice_available,ARG(self),ARG(shape_index),GLOBALS,OWNER);
    m.def("shape_list_mesh_vertices_slice",&shape_list_mesh_vertices_slice,ARG(self),ARG(shape_index),GLOBALS,OWNER);
    m.def("shape_list_mesh_vertices_slice_available",&shape_list_mesh_vertices_slice_available,ARG(self),ARG(shape_index),GLOBALS,OWNER);
    m.def("shape_list_mesh_triangles_slice",&shape_list_mesh_triangles_slice,ARG(self),ARG(shape_index),GLOBALS,OWNER);
    m.def("shape_list_mesh_triangles_slice_available",&shape_list_mesh_triangles_slice_available,ARG(self),ARG(shape_index),GLOBALS,OWNER);
    m.def("shape_list_mesh_vertices_face_slice",&shape_list_mesh_vertices_face_slice,ARG(self),ARG(shape_index),GLOBALS,OWNER);
    m.def("shape_list_mesh_triangles_face_slice",&shape_list_mesh_triangles_face_slice,ARG(self),ARG(shape_index),GLOBALS,OWNER);
    m.def("shape_list_mesh_vertices_edge_slice",&shape_list_mesh_vertices_edge_slice,ARG(self),ARG(shape_index),GLOBALS,OWNER);
    m.def("shape_list_mesh_triangles_edge_slice",&shape_list_mesh_triangles_edge_slice,ARG(self),ARG(shape_index),GLOBALS,OWNER);
    m.def("shape_list_mesh_triangles_range_seq",&shape_list_mesh_triangles_range_seq,ARG(self),ARG(shape_indexes),GLOBALS,OWNER);
    m.def("shape_list_vertices_range_seq",&shape_list_vertices_range_seq,ARG(self),ARG(shape_indexes),GLOBALS,OWNER);
    m.def("shape_list_slicess_to_array",&shape_list_slicess_to_array,ARG(slices),GLOBALS,ARG(generator_factory),OWNER);
    m.def("shape_list_batched_updates",&shape_list_batched_updates,ARG(self),GLOBALS,nb::arg("assertions"),OWNER);
    m.def("shape_batch_wrapped",&shape_batch_wrapped,ARG(self),nb::arg("args"),nb::arg("kwargs"),ARG(method_getter),GLOBALS,OWNER);
    m.def("shape_list_update_triangle_map",&shape_list_update_triangle_map,ARG(self),ARG(displayed_indices),GLOBALS,OWNER);
    m.def("shape_list_update_vertex_map",&shape_list_update_vertex_map,ARG(self),ARG(displayed_indices),GLOBALS,OWNER);
    m.def("shape_list_update_displayed",&shape_list_update_displayed,ARG(self),GLOBALS,nb::arg("assertions"),OWNER);
    m.def("shape_list_add",&shape_list_add,ARG(self),ARG(shape),ARG(face_color),ARG(edge_color),ARG(shape_index),ARG(z_refresh),GLOBALS,OWNER);
    m.def("shape_list_add_single",&shape_list_add_single,ARG(self),ARG(shape),ARG(face_color),ARG(edge_color),ARG(shape_index),GLOBALS,OWNER);
    m.def("shape_list_add_multiple",&shape_list_add_multiple,ARG(self),ARG(shapes),ARG(z_refresh),GLOBALS,ARG(generator_factory),OWNER);
    m.def("shape_list_extend_meshes",&shape_list_extend_meshes,ARG(self),ARG(face_colors),ARG(edge_colors),ARG(arrays),GLOBALS,OWNER);
    m.def("shape_list_remove_all",&shape_list_remove_all,ARG(self),GLOBALS,nb::arg("color_shape"),OWNER);
    m.def("shape_list_update_z_order",&shape_list_update_z_order,ARG(self),GLOBALS,OWNER);
    m.def("shape_ensure_color_arrays",&shape_ensure_color_arrays,ARG(shapes),GLOBALS,OWNER);
    m.def("shape_calculate_array_sizes",&shape_calculate_array_sizes,ARG(shapes),GLOBALS,OWNER);
    m.def("shape_preallocate_arrays",&shape_preallocate_arrays,ARG(shapes),ARG(sizes),GLOBALS,OWNER);
    m.def("shape_fill_arrays",&shape_fill_arrays,ARG(start_mesh_index),ARG(start_triangle_index),ARG(start_vertices_index),ARG(shapes),ARG(face_colors),ARG(edge_colors),ARG(arrays),GLOBALS,OWNER);
#undef ARG
#undef GLOBALS
#undef OWNER
}
} // namespace nx
