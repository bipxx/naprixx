#include "arrays.hpp"
#include "collection_protocol.hpp"
#include "event_protocol.hpp"
#if PY_VERSION_HEX >= 0x030C0000 && PY_VERSION_HEX < 0x030D0000
// CPython 3.12 IMPORT_NAME forwards the already-existing interpreter-frame
// f_locals reference. Reading it must not call the materializing locals API.
#include <internal/pycore_frame.h>
#endif
#include <nanobind/stl/string.h>
#include <array>
#include <climits>
#include <cstddef>
#include <cwchar>
#include <functional>
#include <initializer_list>
#ifdef MS_WINDOWS
#include <direct.h>
#else
#include <unistd.h>
#endif
#include <osdefs.h>
#include <structmember.h>
#include <utility>
#include <vector>

namespace nx {
namespace {
nb::object md_check(PyObject* p) { if (!p) throw nb::python_error(); return nb::steal<nb::object>(p); }
nb::object md_attr(nb::object a, const char* n) { return md_check(PyObject_GetAttrString(a.ptr(), n)); }
nb::object md_item(nb::object a, nb::object k) { return md_check(PyObject_GetItem(a.ptr(), k.ptr())); }
void md_store(nb::object a, const char* n, nb::object v) { if (PyObject_SetAttrString(a.ptr(), n, v.ptr()) < 0) throw nb::python_error(); }
void md_set(nb::object a, nb::object k, nb::object v) { if (PyObject_SetItem(a.ptr(), k.ptr(), v.ptr()) < 0) throw nb::python_error(); }
void md_assign(nb::object& a, nb::object v) { nb::object old = std::move(a); a = std::move(v); old.reset(); }
bool md_truth(nb::object a) { int r = PyObject_IsTrue(a.ptr()); if (r < 0) throw nb::python_error(); return r != 0; }
nb::object md_iter(nb::object a) { return md_check(PyObject_GetIter(a.ptr())); }
nb::object md_next(nb::handle a) { PyObject* p = PyIter_Next(a.ptr()); if (!p && PyErr_Occurred()) throw nb::python_error(); return nb::steal<nb::object>(p); }
using MDExpr = std::function<nb::object()>;
using MDKw = std::pair<const char*, MDExpr>;
nb::object md_call(nb::object fn, std::initializer_list<MDExpr> positional = {}, std::initializer_list<MDKw> keywords = {}) {
    nb::tuple args = nb::steal<nb::tuple>(PyTuple_New(static_cast<Py_ssize_t>(positional.size())));
    if (!args.is_valid()) throw nb::python_error(); Py_ssize_t i = 0;
    for (const auto& f : positional) { nb::object v = f(); PyTuple_SET_ITEM(args.ptr(), i++, v.release().ptr()); }
    nb::dict kwargs; for (const auto& kv : keywords) kwargs[kv.first] = kv.second();
    return md_check(PyObject_Call(fn.ptr(), args.ptr(), keywords.size() ? kwargs.ptr() : nullptr));
}
nb::tuple md_tuple(std::initializer_list<MDExpr> values) {
    nb::tuple out = nb::steal<nb::tuple>(PyTuple_New(static_cast<Py_ssize_t>(values.size())));
    if (!out.is_valid()) throw nb::python_error(); Py_ssize_t i = 0;
    for (const auto& f : values) { nb::object v = f(); PyTuple_SET_ITEM(out.ptr(), i++, v.release().ptr()); } return out;
}
nb::list md_list(std::initializer_list<MDExpr> values) { nb::list out; for (const auto& f : values) out.append(f()); return out; }
nb::object md_binary(MDExpr a, MDExpr b, binaryfunc fn) { nb::object left = a(); nb::object right = b(); return md_check(fn(left.ptr(), right.ptr())); }
nb::object md_subscript(MDExpr a, MDExpr b) { nb::object left = a(); nb::object key = b(); return md_item(std::move(left),std::move(key)); }
void md_publish(MDExpr value, MDExpr target, const char* name) { nb::object v = value(); nb::object t = target(); md_store(std::move(t),name,std::move(v)); }
void md_publish_item(MDExpr value, MDExpr target, MDExpr key) { nb::object v = value(); nb::object t = target(); nb::object k = key(); md_set(std::move(t),std::move(k),std::move(v)); }
nb::object md_compare(MDExpr a, MDExpr b, int op) { nb::object left = a(); nb::object right = b(); return md_check(PyObject_RichCompare(left.ptr(), right.ptr(), op)); }
nb::object md_slice(MDExpr start, MDExpr stop, MDExpr step) { nb::object a = start(); nb::object b = stop(); nb::object c = step(); return md_check(PySlice_New(a.ptr(), b.ptr(), c.ptr())); }
#define SX(expr) [&]() -> nb::object { return (expr); }
#define SA(a,b) md_binary(SX(a),SX(b),PyNumber_Add)
#define SB(a,b) md_binary(SX(a),SX(b),PyNumber_Subtract)
#define SM(a,b) md_binary(SX(a),SX(b),PyNumber_Multiply)
#define SI(a,b) md_binary(SX(a),SX(b),PyNumber_InPlaceAdd)
#define SC(a,b,op) md_compare(SX(a),SX(b),op)
#define SK(a,b) md_subscript(SX(a),SX(b))
#define STORE(a,n,v) md_publish(SX(v),SX(a),n)
#define SET(a,k,v) md_publish_item(SX(v),SX(a),SX(k))
#define ST(...) md_tuple({__VA_ARGS__})
#define SL(...) md_list({__VA_ARGS__})
#define SS(a,b) md_slice(SX(a),SX(b),SX(nb::none()))
[[noreturn]] void md_raise(nb::object value) {
    if (PyExceptionClass_Check(value.ptr())) { nb::object cls = std::move(value); value = md_call(cls);
        if (!PyExceptionInstance_Check(value.ptr())) { PyErr_Format(PyExc_TypeError, "calling %R should have returned an instance of BaseException, not %R", cls.ptr(), reinterpret_cast<PyObject*>(Py_TYPE(value.ptr()))); throw nb::python_error(); }
    }
    if (PyExceptionInstance_Check(value.ptr())) PyErr_SetObject(reinterpret_cast<PyObject*>(Py_TYPE(value.ptr())), value.ptr());
    else PyErr_SetString(PyExc_TypeError, "exceptions must derive from BaseException"); throw nb::python_error();
}
nb::object md_import_frame_locals(){
#if PY_VERSION_HEX >= 0x030C0000 && PY_VERSION_HEX < 0x030D0000
    PyFrameObject* frame=PyEval_GetFrame();if(!frame){if(PyErr_Occurred())throw nb::python_error();return nb::none();}
    PyObject* locals=frame->f_frame->f_locals;return nb::borrow<nb::object>(locals?locals:Py_None);
#else
    PyErr_SetString(PyExc_RuntimeError,"Shape model IMPORT_NAME frame-locals handoff requires its reviewed CPython 3.12 profile; other interpreter profiles remain open");throw nb::python_error();
#endif
}
struct ShapeModelGlobals {
    nb::dict values; nb::object builtins;
    explicit ShapeModelGlobals(nb::dict v) : values(std::move(v)), builtins(nb::borrow<nb::object>(PyEval_GetBuiltins())) {}
    ShapeModelGlobals(nb::dict v, nb::object b) : values(std::move(v)), builtins(std::move(b)) {}
    nb::object get(const char* name) const {
        nb::object key = nb::str(name); PyObject* p = PyDict_GetItemWithError(values.ptr(), key.ptr());
        if (p) return nb::borrow<nb::object>(p); if (PyErr_Occurred()) throw nb::python_error();
        p = PyObject_GetItem(builtins.ptr(), key.ptr()); if (p) return nb::steal<nb::object>(p);
        if (!PyErr_ExceptionMatches(PyExc_KeyError)) throw nb::python_error(); PyErr_Clear();
        nb::object text = md_check(PyUnicode_FromFormat("name '%s' is not defined", name));
        nb::object error = md_call(nb::borrow<nb::object>(PyExc_NameError), {SX(text)}); md_store(error, "name", key); md_raise(error);
    }
    nb::object np(const char* name) const { return md_attr(get("np"), name); }
    nb::object import_module(nb::object name,nb::object fromlist,nb::object level) const {
        nb::object key=nb::str("__import__");PyObject* raw=PyObject_GetItem(builtins.ptr(),key.ptr());
        if(!raw){if(!PyErr_ExceptionMatches(PyExc_KeyError))throw nb::python_error();PyErr_Clear();PyErr_SetString(PyExc_ImportError,"__import__ not found");throw nb::python_error();}
        nb::object importer=nb::steal<nb::object>(raw);nb::object locals=md_import_frame_locals();
        return md_check(PyObject_CallFunctionObjArgs(importer.ptr(),name.ptr(),values.ptr(),locals.ptr(),fromlist.ptr(),level.ptr(),nullptr));
    }
};
#define SN(fn,...) md_call(ns.get(fn),{__VA_ARGS__})
#define NP(fn,...) md_call(ns.np(fn),{__VA_ARGS__})
#define NK(fn,args,...) md_call(ns.np(fn),args,{__VA_ARGS__})
#define KW(name,expr) MDKw{name,SX(expr)}
#define CALL(obj,name,...) md_call(md_attr((obj),name),{__VA_ARGS__})

#define MODEL_FIELDS(X) \
 X(self) X(data) X(edge_width) X(opacity) X(z_index) X(dims_order) X(ndisplay) X(vertices) X(triangles) X(transform) X(points) X(centers) X(offsets) X(interpolation_order) X(data_displayed) X(shape_type) X(cls) X(args) X(kwargs) X(closed) X(face) X(edge) X(face_triangles) X(face_vertices) X(edge_triangles) X(e) X(path) X(text_path) X(f_vertices) X(f_triangles) X(ndim) X(data2d) X(axis) X(value) X(shift) X(scale) X(center) X(angle) X(theta) X(mask_shape) X(zoom_factor) X(offset) X(embedded) X(shape_plane) X(d) X(mask_p) X(mask) X(slice_key) X(i) X(displayed_order) X(filled) X(name) X(interpolation_sampling) X(splev) X(splprep) X(data_spline) X(tck) X(_) X(u) X(bbox)
enum class ModelField {
#define MD_ENUM(n) n,
 MODEL_FIELDS(MD_ENUM)
#undef MD_ENUM
 count_fields
};
struct ShapeModelLocals {
#define MD_DECLARE(n) nb::object n;
 MODEL_FIELDS(MD_DECLARE)
#undef MD_DECLARE
    std::vector<ModelField> order;
    ShapeModelLocals()=default;ShapeModelLocals(ShapeModelLocals&&)=default;~ShapeModelLocals(){clear();}
    void clear(){std::array<nb::object,static_cast<size_t>(ModelField::count_fields)> old={
#define MD_MOVE(n) std::move(n),
 MODEL_FIELDS(MD_MOVE)
#undef MD_MOVE
    };auto fields=std::move(order);order.clear();for(ModelField f:fields)old[static_cast<size_t>(f)].reset();for(auto& value:old)value.reset();}
};
ShapeModelLocals shape_model_locals(nb::tuple layout,nb::dict bindings){
    ShapeModelLocals o;for(nb::handle item:layout){std::string name=nb::cast<std::string>(item);bool found=false;
#define MD_LAYOUT(n) if(name==#n){o.order.push_back(ModelField::n);found=true;}
        MODEL_FIELDS(MD_LAYOUT)
#undef MD_LAYOUT
        if(!found){PyErr_Format(PyExc_ValueError,"unknown original shape model local %s",name.c_str());throw nb::python_error();}
    }
    for(auto item:bindings){std::string name=nb::cast<std::string>(item.first);bool found=false;
#define MD_BIND(n) if(name==#n){md_assign(o.n,nb::borrow<nb::object>(item.second));found=true;}
        MODEL_FIELDS(MD_BIND)
#undef MD_BIND
        if(!found){PyErr_Format(PyExc_ValueError,"unknown original shape model parameter %s",name.c_str());throw nb::python_error();}
    }return o;
}
int md_owner_traverse(PyObject* self,visitproc visit,void* arg){Py_VISIT(Py_TYPE(self));if(nb::inst_ready(self)){auto& o=*nb::inst_ptr<ShapeModelLocals>(self);
#define MD_VISIT(n) Py_VISIT(o.n.ptr());
 MODEL_FIELDS(MD_VISIT)
#undef MD_VISIT
    }return 0;}
int md_owner_clear(PyObject* self){if(nb::inst_ready(self))nb::inst_ptr<ShapeModelLocals>(self)->clear();return 0;}
PyType_Slot md_owner_slots[]={{Py_tp_traverse,reinterpret_cast<void*>(md_owner_traverse)},{Py_tp_clear,reinterpret_cast<void*>(md_owner_clear)},{0,nullptr}};
struct ModelNamespaceObject {PyObject_HEAD PyObject* dictionary;PyObject* weakrefs;};
int md_namespace_traverse(PyObject* self,visitproc visit,void* arg){auto* p=reinterpret_cast<ModelNamespaceObject*>(self);Py_VISIT(p->dictionary);Py_VISIT(Py_TYPE(self));return 0;}
int md_namespace_clear(PyObject* self){Py_CLEAR(reinterpret_cast<ModelNamespaceObject*>(self)->dictionary);return 0;}
int md_namespace_setdict(PyObject* self,PyObject* value,void*){if(value&&!PyDict_Check(value)){PyErr_Format(PyExc_TypeError,"__dict__ must be set to a dictionary, not a '%.200s'",Py_TYPE(value)->tp_name);return -1;}Py_XSETREF(reinterpret_cast<ModelNamespaceObject*>(self)->dictionary,Py_XNewRef(value));return 0;}
PyObject* md_namespace_getweak(PyObject* self,void*){PyObject* value=reinterpret_cast<ModelNamespaceObject*>(self)->weakrefs;return Py_NewRef(value?value:Py_None);}
PyMemberDef md_members[]={{const_cast<char*>("__dictoffset__"),T_PYSSIZET,offsetof(ModelNamespaceObject,dictionary),READONLY,nullptr},{const_cast<char*>("__weaklistoffset__"),T_PYSSIZET,offsetof(ModelNamespaceObject,weakrefs),READONLY,nullptr},{nullptr,0,0,0,nullptr}};
PyGetSetDef md_getsets[]={{const_cast<char*>("__dict__"),PyObject_GenericGetDict,md_namespace_setdict,nullptr,nullptr},{const_cast<char*>("__weakref__"),md_namespace_getweak,nullptr,nullptr,nullptr},{nullptr,nullptr,nullptr,nullptr,nullptr}};
PyType_Slot md_namespace_slots[]={{Py_tp_members,md_members},{Py_tp_getset,md_getsets},{Py_tp_traverse,reinterpret_cast<void*>(md_namespace_traverse)},{Py_tp_clear,reinterpret_cast<void*>(md_namespace_clear)},{0,nullptr}};
#define PUT(n,expr) md_assign(o.n,(expr))
#define MD_DEF(n) nb::object n(nb::dict globals,nb::tuple literals,ShapeModelLocals& o)
#define MD_SUPER_DEF(n) nb::object n(nb::dict globals,nb::tuple literals,nb::object super_provider,ShapeModelLocals& o)
#define MD_BEGIN ShapeModelGlobals ns(std::move(globals))
nb::object md_literal(const nb::tuple& values,Py_ssize_t index){PyObject* value=PyTuple_GetItem(values.ptr(),index);if(!value)throw nb::python_error();return nb::borrow<nb::object>(value);}
#define MC(i) md_literal(literals,i)
nb::object md_negative(nb::object v){return md_check(PyNumber_Negative(v.ptr()));}
#define MN(v) md_negative(v)
#define MM(a,b) md_binary(SX(a),SX(b),PyNumber_MatrixMultiply)
bool md_in(MDExpr item,MDExpr container){nb::object value=item();nb::object collection=container();int found=PySequence_Contains(collection.ptr(),value.ptr());if(found<0)throw nb::python_error();return found!=0;}
nb::object md_set(std::initializer_list<MDExpr> values){auto expression=values.begin();nb::object first=(*expression++)();nb::object second=(*expression)();nb::object set=md_check(PySet_New(nullptr));
    int error=PySet_Add(set.ptr(),first.ptr());first.reset();if(error==0)error=PySet_Add(set.ptr(),second.ptr());second.reset();if(error){set.reset();throw nb::python_error();}return set;
}

MD_DEF(shape_model_init){MD_BEGIN;
    nb::object dims=o.dims_order;if(!md_truth(dims))dims=SN("list",SX(SN("range",SX(MC(1)))));
    STORE(o.self,"_dims_order",std::move(dims));STORE(o.self,"_ndisplay",o.ndisplay);
    STORE(o.self,"_face_vertices",md_call(ns.np("empty"),{SX(ST(SX(MC(2)),SX(md_attr(o.self,"ndisplay"))))},{KW("dtype",md_attr(ns.get("np"),"float32"))}));
    STORE(o.self,"_face_triangles",md_call(ns.np("empty"),{SX(MC(4))},{KW("dtype",md_attr(ns.get("np"),"uint32"))}));
    STORE(o.self,"_edge_vertices",md_call(ns.np("empty"),{SX(ST(SX(MC(2)),SX(md_attr(o.self,"ndisplay"))))},{KW("dtype",md_attr(ns.get("np"),"float32"))}));
    STORE(o.self,"_edge_offsets",md_call(ns.np("empty"),{SX(ST(SX(MC(2)),SX(md_attr(o.self,"ndisplay"))))},{KW("dtype",md_attr(ns.get("np"),"float32"))}));
    STORE(o.self,"_edge_triangles",md_call(ns.np("empty"),{SX(MC(4))},{KW("dtype",md_attr(ns.get("np"),"uint32"))}));
    STORE(o.self,"_box",md_call(ns.np("empty"),{SX(MC(5))},{KW("dtype",md_attr(ns.get("np"),"float32"))}));
    STORE(o.self,"_closed",MC(6));STORE(o.self,"_filled",MC(7));STORE(o.self,"_use_face_vertices",MC(6));STORE(o.self,"edge_width",o.edge_width);STORE(o.self,"z_index",o.z_index);STORE(o.self,"name",MC(8));
    STORE(o.self,"_bounding_box",NP("empty",SX(ST(SX(MC(2)),SX(md_attr(o.self,"ndisplay"))))));return nb::none();
}
MD_SUPER_DEF(shape_model_new){MD_BEGIN;
    if(md_in(SX(ns.get("TRIANGULATION_BACKEND")),SX(md_set({SX(md_attr(ns.get("TriangulationBackend"),"bermuda")),SX(md_attr(ns.get("TriangulationBackend"),"fastest_available"))})))&&!ns.get("bermuda").is_none()){
        STORE(o.cls,"_set_meshes",md_attr(o.cls,"_set_meshes_compiled_bermuda"));STORE(o.cls,"_triangulate_edge",md_attr(o.cls,"_triangulate_edge_bermuda"));
    }else if(md_in(SX(ns.get("TRIANGULATION_BACKEND")),SX(md_set({SX(md_attr(ns.get("TriangulationBackend"),"partsegcore")),SX(md_attr(ns.get("TriangulationBackend"),"fastest_available"))})))&&!ns.get("partsegcore_triangulate").is_none()){
        STORE(o.cls,"_set_meshes",md_attr(o.cls,"_set_meshes_compiled_partseg"));STORE(o.cls,"_triangulate_edge",md_attr(o.cls,"_triangulate_edge_partseg"));
    }else if(md_in(SX(ns.get("TRIANGULATION_BACKEND")),SX(md_set({SX(md_attr(ns.get("TriangulationBackend"),"triangle")),SX(md_attr(ns.get("TriangulationBackend"),"fastest_available"))})))&&md_in(SX(MC(1)),SX(md_attr(ns.get("sys"),"modules")))){
        STORE(o.cls,"_set_meshes",md_attr(o.cls,"_set_meshes_triangle"));
    }else STORE(o.cls,"_set_meshes",md_attr(o.cls,"_set_meshes_py"));
    return CALL(md_call(std::move(super_provider)),"__new__",SX(o.cls));
}
MD_SUPER_DEF(shape_rectangle_init){MD_BEGIN;md_call(md_attr(md_call(std::move(super_provider)),"__init__"),{},{KW("edge_width",o.edge_width),KW("z_index",o.z_index),KW("dims_order",o.dims_order),KW("ndisplay",o.ndisplay)});STORE(o.self,"_closed",MC(2));STORE(o.self,"data",o.data);STORE(o.self,"name",MC(3));return nb::none();}
MD_SUPER_DEF(shape_ellipse_init){MD_BEGIN;md_call(md_attr(md_call(std::move(super_provider)),"__init__"),{},{KW("edge_width",o.edge_width),KW("z_index",o.z_index),KW("dims_order",o.dims_order),KW("ndisplay",o.ndisplay)});STORE(o.self,"_closed",MC(2));STORE(o.self,"_use_face_vertices",MC(2));STORE(o.self,"data",o.data);STORE(o.self,"name",MC(3));return nb::none();}
MD_SUPER_DEF(shape_line_init){MD_BEGIN;md_call(md_attr(md_call(std::move(super_provider)),"__init__"),{},{KW("edge_width",o.edge_width),KW("z_index",o.z_index),KW("dims_order",o.dims_order),KW("ndisplay",o.ndisplay)});STORE(o.self,"_filled",MC(2));STORE(o.self,"data",o.data);STORE(o.self,"name",MC(3));return nb::none();}
MD_SUPER_DEF(shape_polygon_base_init){MD_BEGIN;md_call(md_attr(md_call(std::move(super_provider)),"__init__"),{},{KW("edge_width",o.edge_width),KW("z_index",o.z_index),KW("dims_order",o.dims_order),KW("ndisplay",o.ndisplay)});STORE(o.self,"_filled",o.filled);STORE(o.self,"_closed",o.closed);STORE(o.self,"name",o.name);STORE(o.self,"interpolation_order",o.interpolation_order);STORE(o.self,"interpolation_sampling",o.interpolation_sampling);STORE(o.self,"data",o.data);return nb::none();}
MD_SUPER_DEF(shape_path_init){MD_BEGIN;md_call(md_attr(md_call(std::move(super_provider)),"__init__"),{SX(o.data)},{KW("edge_width",o.edge_width),KW("z_index",o.z_index),KW("dims_order",o.dims_order),KW("ndisplay",o.ndisplay),KW("filled",MC(1)),KW("closed",MC(1)),KW("name",MC(2)),KW("interpolation_order",o.interpolation_order)});return nb::none();}
MD_SUPER_DEF(shape_polygon_init){MD_BEGIN;md_call(md_attr(md_call(std::move(super_provider)),"__init__"),{},{KW("data",o.data),KW("edge_width",o.edge_width),KW("z_index",o.z_index),KW("dims_order",o.dims_order),KW("ndisplay",o.ndisplay),KW("closed",MC(1)),KW("filled",MC(1)),KW("name",MC(2)),KW("interpolation_order",o.interpolation_order)});return nb::none();}
MD_DEF(shape_model_abstract_data){MD_BEGIN;md_raise(ns.get("NotImplementedError"));}
MD_DEF(shape_model_abstract_set_data){MD_BEGIN;md_raise(ns.get("NotImplementedError"));}
MD_DEF(shape_model_abstract_update){MD_BEGIN;md_raise(ns.get("NotImplementedError"));}
MD_DEF(shape_model_ndisplay){return md_attr(o.self,"_ndisplay");}
MD_DEF(shape_model_set_ndisplay){if(md_truth(SC(md_attr(o.self,"ndisplay"),o.ndisplay,Py_EQ)))return nb::none();STORE(o.self,"_ndisplay",o.ndisplay);CALL(o.self,"_update_displayed_data");return nb::none();}
MD_DEF(shape_model_dims_order){return md_attr(o.self,"_dims_order");}
MD_DEF(shape_model_set_dims_order){if(md_truth(SC(md_attr(o.self,"dims_order"),o.dims_order,Py_EQ)))return nb::none();STORE(o.self,"_dims_order",o.dims_order);CALL(o.self,"_update_displayed_data");return nb::none();}
MD_DEF(shape_model_dims_displayed){return SK(md_attr(o.self,"dims_order"),SS(MN(md_attr(o.self,"ndisplay")),nb::none()));}
MD_DEF(shape_model_dims_not_displayed){return SK(md_attr(o.self,"dims_order"),SS(nb::none(),MN(md_attr(o.self,"ndisplay"))));}
MD_DEF(shape_model_data_displayed){return SK(md_attr(o.self,"data"),ST(SX(SS(nb::none(),nb::none())),SX(md_attr(o.self,"dims_displayed"))));}
MD_DEF(shape_model_bounding_box){return SA(SK(md_attr(o.self,"_bounding_box"),ST(SX(SS(nb::none(),nb::none())),SX(md_attr(o.self,"dims_displayed")))),SL(SX(SL(SX(SM(MC(2),md_attr(o.self,"edge_width"))))),SX(SL(SX(SM(MC(3),md_attr(o.self,"edge_width")))))));}
MD_DEF(shape_model_edge_width){return md_attr(o.self,"_edge_width");}
MD_DEF(shape_model_set_edge_width){STORE(o.self,"_edge_width",o.edge_width);return nb::none();}
MD_DEF(shape_model_z_index){return md_attr(o.self,"_z_index");}
MD_DEF(shape_model_set_z_index){STORE(o.self,"_z_index",o.z_index);return nb::none();}
MD_DEF(shape_model_vertices_count){return SA(SK(md_attr(md_attr(o.self,"_edge_vertices"),"shape"),MC(1)),SK(md_attr(md_attr(o.self,"_face_vertices"),"shape"),MC(1)));}
MD_DEF(shape_model_triangles_count){return SA(SK(md_attr(md_attr(o.self,"_face_triangles"),"shape"),MC(1)),SK(md_attr(md_attr(o.self,"_edge_triangles"),"shape"),MC(1)));}
MD_DEF(shape_model_face_triangles_count){return SK(md_attr(md_attr(o.self,"_face_triangles"),"shape"),MC(1));}
MD_DEF(shape_model_face_vertices_count){return SK(md_attr(md_attr(o.self,"_face_vertices"),"shape"),MC(1));}
MD_DEF(shape_model_edge_triangles_count){return SK(md_attr(md_attr(o.self,"_edge_triangles"),"shape"),MC(1));}
MD_DEF(shape_model_edge_vertices_count){return SK(md_attr(md_attr(o.self,"_edge_vertices"),"shape"),MC(1));}
MD_DEF(shape_rectangle_data){return md_attr(o.self,"_data");}
MD_DEF(shape_ellipse_data){return md_attr(o.self,"_data");}
MD_DEF(shape_line_data){return md_attr(o.self,"_data");}
MD_DEF(shape_polygon_base_data){return md_attr(o.self,"_data");}

std::vector<nb::object> md_unpack(nb::object value,size_t count){
    PyObject* raw=PyObject_GetIter(value.ptr());if(!raw){nb::python_error error;if(error.matches(PyExc_TypeError)&&Py_TYPE(value.ptr())->tp_iter==nullptr&&!PySequence_Check(value.ptr())){PyErr_Format(PyExc_TypeError,"cannot unpack non-iterable %.200s object",Py_TYPE(value.ptr())->tp_name);throw nb::python_error();}throw error;}
    nb::object iterator=nb::steal<nb::object>(raw);std::vector<nb::object> result;
    for(size_t i=0;i<count;++i){nb::object item=md_next(iterator);if(!item.is_valid()){PyErr_Format(PyExc_ValueError,"not enough values to unpack (expected %zu, got %zu)",count,i);throw nb::python_error();}result.push_back(std::move(item));}
    nb::object extra=md_next(iterator);if(extra.is_valid()){PyErr_Format(PyExc_ValueError,"too many values to unpack (expected %zu)",count);throw nb::python_error();}iterator.reset();value.reset();return result;
}
nb::object md_format(nb::object value){return md_check(PyObject_Format(value.ptr(),nullptr));}
nb::object md_string(std::initializer_list<MDExpr> expressions){nb::list values;for(const auto& expression:expressions)values.append(expression());return md_check(PyUnicode_Join(nb::str("").ptr(),values.ptr()));}
void md_except_type(nb::object type){bool valid=PyExceptionClass_Check(type.ptr());if(PyTuple_Check(type.ptr())){valid=true;for(Py_ssize_t i=0;i<PyTuple_GET_SIZE(type.ptr());++i)if(!PyExceptionClass_Check(PyTuple_GET_ITEM(type.ptr(),i))){valid=false;break;}}if(!valid){PyErr_SetString(PyExc_TypeError,"catching classes that do not inherit from BaseException is not allowed");throw nb::python_error();}}
[[noreturn]]void md_raise_cause(nb::object value,nb::object cause){
    if(PyExceptionClass_Check(value.ptr())){nb::object cls=std::move(value);value=md_call(cls);if(!PyExceptionInstance_Check(value.ptr())){PyErr_Format(PyExc_TypeError,"calling %R should have returned an instance of BaseException, not %R",cls.ptr(),reinterpret_cast<PyObject*>(Py_TYPE(value.ptr())));throw nb::python_error();}}
    if(!PyExceptionInstance_Check(value.ptr())){PyErr_SetString(PyExc_TypeError,"exceptions must derive from BaseException");throw nb::python_error();}
    PyException_SetCause(value.ptr(),cause.inc_ref().ptr());md_raise(std::move(value));
}
MD_DEF(shape_model_empty_edge){MD_BEGIN;
    STORE(o.self,"_edge_vertices",md_call(ns.np("empty"),{SX(ST(SX(MC(1)),SX(md_attr(o.self,"ndisplay"))))},{KW("dtype",md_attr(ns.get("np"),"float32"))}));
    STORE(o.self,"_edge_offsets",md_call(ns.np("empty"),{SX(ST(SX(MC(1)),SX(md_attr(o.self,"ndisplay"))))},{KW("dtype",md_attr(ns.get("np"),"float32"))}));
    STORE(o.self,"_edge_triangles",md_call(ns.np("empty"),{SX(MC(3))},{KW("dtype",md_attr(ns.get("np"),"uint32"))}));return nb::none();
}
MD_DEF(shape_model_empty_face){MD_BEGIN;
    STORE(o.self,"_face_vertices",md_call(ns.np("empty"),{SX(ST(SX(MC(1)),SX(md_attr(o.self,"ndisplay"))))},{KW("dtype",md_attr(ns.get("np"),"float32"))}));
    STORE(o.self,"_face_triangles",md_call(ns.np("empty"),{SX(MC(3))},{KW("dtype",md_attr(ns.get("np"),"uint32"))}));return nb::none();
}
MD_DEF(shape_model_meshes_3d){MD_BEGIN;
    if(md_truth(o.face)){auto parts=md_unpack(CALL(ns.get("bermuda"),"triangulate_polygons_face_3d",SX(SL(SX(o.data)))),2);PUT(face_triangles,std::move(parts[0]));PUT(face_vertices,std::move(parts[1]));STORE(o.self,"_face_vertices",o.face_vertices);STORE(o.self,"_face_triangles",o.face_triangles);}
    else CALL(o.self,"_set_empty_face");
    if(md_truth(o.edge)){auto parts=md_unpack(md_call(ns.get("triangulate_edge"),{SX(o.data)},{KW("closed",o.closed)}),3);PUT(centers,std::move(parts[0]));PUT(offsets,std::move(parts[1]));PUT(edge_triangles,std::move(parts[2]));STORE(o.self,"_edge_vertices",o.centers);STORE(o.self,"_edge_offsets",o.offsets);STORE(o.self,"_edge_triangles",o.edge_triangles);}
    else CALL(o.self,"_set_empty_edge");return nb::none();
}
MD_DEF(shape_model_meshes_bermuda){MD_BEGIN;
    if(md_truth(SC(SK(md_attr(o.data,"shape"),MC(1)),MC(2),Py_EQ))){md_call(md_attr(o.self,"_set_meshes_compiled_3d"),{SX(o.data)},{KW("closed",o.closed),KW("face",o.face),KW("edge",o.edge)});return nb::none();}
    if(md_truth(o.edge)&&md_truth(o.face)){
        try{auto pair=md_unpack(CALL(ns.get("bermuda"),"triangulate_polygons_with_edge",SX(SL(SX(o.data)))),2);auto faces=md_unpack(std::move(pair[0]),2);PUT(triangles,std::move(faces[0]));PUT(vertices,std::move(faces[1]));auto edges=md_unpack(std::move(pair[1]),3);PUT(centers,std::move(edges[0]));PUT(offsets,std::move(edges[1]));PUT(edge_triangles,std::move(edges[2]));}
        catch(nb::python_error& raised){raised.restore();if(PyTraceBack_Here(PyEval_GetFrame())<0)throw nb::python_error();nb::python_error original;collections::HandledException handled(original.value());nb::object klass=ns.get("BaseException");md_except_type(klass);int matched=PyErr_GivenExceptionMatches(original.type().ptr(),klass.ptr());klass.reset();if(!matched)throw original;PUT(e,nb::borrow<nb::object>(original.value()));
            try{auto paths=md_unpack(md_call(ns.get("_save_failed_triangulation"),{SX(o.data)},{KW("backend",MC(5))}),2);PUT(path,std::move(paths[0]));PUT(text_path,std::move(paths[1]));nb::object error=SN("RuntimeError",SX(md_string({SX(MC(7)),SX(md_format(o.path)),SX(MC(8)),SX(md_format(o.text_path))})));md_raise_cause(std::move(error),o.e);}catch(...){o.e.reset();throw;}
        }
        STORE(o.self,"_edge_vertices",o.centers);STORE(o.self,"_edge_offsets",o.offsets);STORE(o.self,"_edge_triangles",o.edge_triangles);STORE(o.self,"_face_vertices",o.vertices);STORE(o.self,"_face_triangles",o.triangles);return nb::none();
    }
    if(md_truth(o.edge)){auto parts=md_unpack(md_call(md_attr(ns.get("bermuda"),"triangulate_path_edge"),{SX(o.data)},{KW("closed",o.closed)}),3);STORE(o.self,"_edge_vertices",std::move(parts[0]));STORE(o.self,"_edge_offsets",std::move(parts[1]));STORE(o.self,"_edge_triangles",std::move(parts[2]));}else CALL(o.self,"_set_empty_edge");
    if(md_truth(o.face)){auto parts=md_unpack(CALL(ns.get("bermuda"),"triangulate_polygons_face",SX(SL(SX(o.data)))),2);STORE(o.self,"_face_triangles",std::move(parts[0]));STORE(o.self,"_face_vertices",std::move(parts[1]));}else CALL(o.self,"_set_empty_face");return nb::none();
}
MD_DEF(shape_model_meshes_partseg){MD_BEGIN;
    if(md_truth(SC(SK(md_attr(o.data,"shape"),MC(1)),MC(2),Py_EQ))){md_call(md_attr(o.self,"_set_meshes_py"),{SX(o.data)},{KW("closed",o.closed),KW("face",o.face),KW("edge",o.edge)});return nb::none();}
    if(md_truth(o.edge)&&md_truth(o.face)){
        try{auto pair=md_unpack(md_call(md_attr(ns.get("partsegcore_triangulate"),"triangulate_polygon_with_edge_numpy_li"),{SX(SL(SX(o.data)))},{KW("split_edges",MC(5))}),2);auto faces=md_unpack(std::move(pair[0]),2);PUT(triangles,std::move(faces[0]));PUT(vertices,std::move(faces[1]));auto edges=md_unpack(std::move(pair[1]),3);PUT(centers,std::move(edges[0]));PUT(offsets,std::move(edges[1]));PUT(edge_triangles,std::move(edges[2]));}
        catch(nb::python_error& raised){raised.restore();if(PyTraceBack_Here(PyEval_GetFrame())<0)throw nb::python_error();nb::python_error original;collections::HandledException handled(original.value());nb::object klass=ns.get("BaseException");md_except_type(klass);int matched=PyErr_GivenExceptionMatches(original.type().ptr(),klass.ptr());klass.reset();if(!matched)throw original;PUT(e,nb::borrow<nb::object>(original.value()));
            try{auto paths=md_unpack(md_call(ns.get("_save_failed_triangulation"),{SX(o.data)},{KW("backend",MC(7))}),2);PUT(path,std::move(paths[0]));PUT(text_path,std::move(paths[1]));nb::object error=SN("RuntimeError",SX(md_string({SX(MC(9)),SX(md_format(o.path)),SX(MC(10)),SX(md_format(o.text_path))})));md_raise_cause(std::move(error),o.e);}catch(...){o.e.reset();throw;}
        }
        STORE(o.self,"_edge_vertices",o.centers);STORE(o.self,"_edge_offsets",o.offsets);STORE(o.self,"_edge_triangles",o.edge_triangles);STORE(o.self,"_face_vertices",o.vertices);STORE(o.self,"_face_triangles",o.triangles);return nb::none();
    }
    if(md_truth(o.edge)){auto parts=md_unpack(md_call(md_attr(ns.get("partsegcore_triangulate"),"triangulate_path_edge_numpy"),{SX(o.data)},{KW("closed",o.closed)}),3);STORE(o.self,"_edge_vertices",std::move(parts[0]));STORE(o.self,"_edge_offsets",std::move(parts[1]));STORE(o.self,"_edge_triangles",std::move(parts[2]));}else CALL(o.self,"_set_empty_edge");
    if(md_truth(o.face)){auto parts=md_unpack(CALL(ns.get("partsegcore_triangulate"),"triangulate_polygon_numpy_li",SX(SL(SX(o.data)))),2);STORE(o.self,"_face_triangles",std::move(parts[0]));STORE(o.self,"_face_vertices",std::move(parts[1]));}else CALL(o.self,"_set_empty_face");return nb::none();
}
MD_DEF(shape_model_meshes_triangle){MD_BEGIN;
    PUT(data,md_call(ns.get("remove_path_duplicates"),{SX(o.data)},{KW("closed",o.closed)}));
    if(md_truth(o.edge)&&md_truth(o.face)){auto pair=md_unpack(SN("triangulate_face_and_edges",SX(o.data),SX(ns.get("triangulate_face_triangle"))),2);auto faces=md_unpack(std::move(pair[0]),2);PUT(f_vertices,std::move(faces[0]));PUT(f_triangles,std::move(faces[1]));auto edges=md_unpack(std::move(pair[1]),3);PUT(centers,std::move(edges[0]));PUT(offsets,std::move(edges[1]));PUT(triangles,std::move(edges[2]));STORE(o.self,"_edge_vertices",o.centers);STORE(o.self,"_edge_offsets",o.offsets);STORE(o.self,"_edge_triangles",o.triangles);STORE(o.self,"_face_vertices",o.f_vertices);STORE(o.self,"_face_triangles",o.f_triangles);return nb::none();}
    if(md_truth(o.edge)){auto parts=md_unpack(md_call(ns.get("triangulate_edge"),{SX(o.data)},{KW("closed",o.closed)}),3);PUT(centers,std::move(parts[0]));PUT(offsets,std::move(parts[1]));PUT(triangles,std::move(parts[2]));STORE(o.self,"_edge_vertices",o.centers);STORE(o.self,"_edge_offsets",o.offsets);STORE(o.self,"_edge_triangles",o.triangles);}else CALL(o.self,"_set_empty_edge");
    PUT(ndim,SK(md_attr(o.data,"shape"),MC(3)));auto planar=md_unpack(SN("find_planar_axis",SX(o.data)),3);PUT(data2d,std::move(planar[0]));PUT(axis,std::move(planar[1]));PUT(value,std::move(planar[2]));CALL(o.self,"_set_empty_face");
    if(md_truth(o.face)&&!md_truth(SN("is_collinear",SX(o.data2d)))){auto parts=md_unpack(SN("triangulate_face",SX(o.data2d),SX(ns.get("triangulate_face_triangle"))),2);PUT(vertices,std::move(parts[0]));PUT(triangles,std::move(parts[1]));if(md_truth(SC(o.ndim,MC(4),Py_EQ))&&!o.axis.is_none()&&!o.value.is_none())PUT(vertices,md_call(ns.np("insert"),{SX(o.vertices),SX(o.axis),SX(o.value)},{KW("axis",MC(3))}));if(md_truth(SC(SN("len",SX(o.triangles)),MC(6),Py_GT))){STORE(o.self,"_face_vertices",o.vertices);STORE(o.self,"_face_triangles",o.triangles);}}
    return nb::none();
}
MD_DEF(shape_model_meshes_py){MD_BEGIN;
    PUT(data,md_call(ns.get("remove_path_duplicates"),{SX(o.data)},{KW("closed",o.closed)}));
    if(md_truth(o.edge)&&md_truth(o.face)){auto pair=md_unpack(SN("triangulate_face_and_edges",SX(o.data),SX(ns.get("triangulate_face_vispy"))),2);auto faces=md_unpack(std::move(pair[0]),2);PUT(f_vertices,std::move(faces[0]));PUT(f_triangles,std::move(faces[1]));auto edges=md_unpack(std::move(pair[1]),3);PUT(centers,std::move(edges[0]));PUT(offsets,std::move(edges[1]));PUT(triangles,std::move(edges[2]));STORE(o.self,"_edge_vertices",o.centers);STORE(o.self,"_edge_offsets",o.offsets);STORE(o.self,"_edge_triangles",o.triangles);STORE(o.self,"_face_vertices",o.f_vertices);STORE(o.self,"_face_triangles",o.f_triangles);return nb::none();}
    if(md_truth(o.edge)){auto parts=md_unpack(md_call(ns.get("triangulate_edge"),{SX(o.data)},{KW("closed",o.closed)}),3);PUT(centers,std::move(parts[0]));PUT(offsets,std::move(parts[1]));PUT(triangles,std::move(parts[2]));STORE(o.self,"_edge_vertices",o.centers);STORE(o.self,"_edge_offsets",o.offsets);STORE(o.self,"_edge_triangles",o.triangles);}else CALL(o.self,"_set_empty_edge");
    PUT(ndim,SK(md_attr(o.data,"shape"),MC(3)));auto planar=md_unpack(SN("find_planar_axis",SX(o.data)),3);PUT(data2d,std::move(planar[0]));PUT(axis,std::move(planar[1]));PUT(value,std::move(planar[2]));CALL(o.self,"_set_empty_face");
    if(md_truth(o.face)&&!md_truth(SN("is_collinear",SX(o.data2d)))){auto parts=md_unpack(SN("triangulate_face",SX(o.data2d),SX(ns.get("triangulate_face_vispy"))),2);PUT(vertices,std::move(parts[0]));PUT(triangles,std::move(parts[1]));if(md_truth(SC(o.ndim,MC(4),Py_EQ))&&!o.axis.is_none()&&!o.value.is_none())PUT(vertices,md_call(ns.np("insert"),{SX(o.vertices),SX(o.axis),SX(o.value)},{KW("axis",MC(3))}));if(md_truth(SC(SN("len",SX(o.triangles)),MC(6),Py_GT))){STORE(o.self,"_face_vertices",o.vertices);STORE(o.self,"_face_triangles",o.triangles);}}
    return nb::none();
}
MD_DEF(shape_model_triangulate_edge){MD_BEGIN;return md_call(ns.get("triangulate_edge"),{SX(o.data)},{KW("closed",o.closed)});}
MD_DEF(shape_model_triangulate_partseg){MD_BEGIN;return md_call(md_attr(ns.get("partsegcore_triangulate"),"triangulate_path_edge_numpy"),{SX(o.data)},{KW("closed",o.closed)});}
MD_DEF(shape_model_triangulate_bermuda){MD_BEGIN;return md_call(md_attr(ns.get("bermuda"),"triangulate_path_edge"),{SX(o.data)},{KW("closed",o.closed)});}

enum class ModelDataKind{Rectangle,Ellipse,Line,Polygon};
nb::object md_data_set(const ShapeModelGlobals& ns,const nb::tuple& literals,ShapeModelLocals& o,ModelDataKind kind){
    PUT(data,CALL(NP("array",SX(o.data)),"astype",SX(md_attr(ns.get("np"),"float32"))));
    if(md_truth(SC(SN("len",SX(md_attr(o.self,"dims_order"))),SK(md_attr(o.data,"shape"),MC(1)),Py_NE)))STORE(o.self,"_dims_order",SN("list",SX(SN("range",SX(SK(md_attr(o.data,"shape"),MC(1)))))));
    if(kind==ModelDataKind::Rectangle||kind==ModelDataKind::Ellipse){
        if(md_truth(SC(SN("len",SX(o.data)),MC(2),Py_EQ))&&md_truth(SC(SK(md_attr(o.data,"shape"),MC(1)),MC(2),Py_EQ))){
            if(kind==ModelDataKind::Rectangle)PUT(data,SN("find_corners",SX(o.data)));
            else PUT(data,SN("center_radii_to_corners",SX(SK(o.data,MC(3))),SX(SK(o.data,MC(1)))));
        }
    }
    bool invalid=md_truth(SC(SN("len",SX(o.data)),kind==ModelDataKind::Line||kind==ModelDataKind::Polygon?MC(2):MC(kind==ModelDataKind::Rectangle?3:4),kind==ModelDataKind::Polygon?Py_LT:Py_NE));
    if(invalid){int prefix=kind==ModelDataKind::Rectangle?4:kind==ModelDataKind::Ellipse?5:3;md_raise(SN("ValueError",SX(md_string({SX(MC(prefix)),SX(md_format(SN("len",SX(o.data)))),SX(MC(prefix+1))}))));}
    STORE(o.self,"_data",o.data);const char* reducer=kind==ModelDataKind::Ellipse?"round":"array";
    STORE(o.self,"_bounding_box",NP(reducer,SX(SL(SX(md_call(ns.np("min"),{SX(o.data)},{KW("axis",nb::int_(0))})),SX(md_call(ns.np("max"),{SX(o.data)},{KW("axis",nb::int_(0))}))))));
    CALL(o.self,"_update_displayed_data");return nb::none();
}
MD_DEF(shape_rectangle_set_data){MD_BEGIN;return md_data_set(ns,literals,o,ModelDataKind::Rectangle);}
MD_DEF(shape_ellipse_set_data){MD_BEGIN;return md_data_set(ns,literals,o,ModelDataKind::Ellipse);}
MD_DEF(shape_line_set_data){MD_BEGIN;return md_data_set(ns,literals,o,ModelDataKind::Line);}
MD_DEF(shape_polygon_base_set_data){MD_BEGIN;return md_data_set(ns,literals,o,ModelDataKind::Polygon);}
MD_DEF(shape_rectangle_update){MD_BEGIN;CALL(o.self,"_clean_cache");PUT(data_displayed,md_attr(o.self,"data_displayed"));
    md_call(md_attr(o.self,"_set_meshes"),{SX(o.data_displayed)},{KW("face",MC(1))});STORE(o.self,"_face_vertices",o.data_displayed);
    STORE(o.self,"_face_triangles",NP("array",SX(SL(SX(SL(SX(SK(MC(3),nb::int_(0))),SX(SK(MC(3),nb::int_(1))),SX(SK(MC(3),nb::int_(2))))),SX(SL(SX(SK(MC(4),nb::int_(0))),SX(SK(MC(4),nb::int_(1))),SX(SK(MC(4),nb::int_(2)))))))));
    STORE(o.self,"_box",SN("rectangle_to_box",SX(o.data_displayed)));STORE(o.self,"slice_key",CALL(SK(md_attr(o.self,"_bounding_box"),ST(SX(SS(nb::none(),nb::none())),SX(md_attr(o.self,"dims_not_displayed")))),"astype",SX(MC(6))));return nb::none();
}
MD_DEF(shape_ellipse_update){MD_BEGIN;CALL(o.self,"_clean_cache");auto parts=md_unpack(SN("triangulate_ellipse",SX(md_attr(o.self,"data_displayed"))),2);PUT(vertices,std::move(parts[0]));PUT(triangles,std::move(parts[1]));
    md_call(md_attr(o.self,"_set_meshes"),{SX(SK(o.vertices,SS(MC(1),MC(2))))},{KW("face",MC(3))});STORE(o.self,"_face_vertices",o.vertices);STORE(o.self,"_face_triangles",o.triangles);STORE(o.self,"_box",SN("rectangle_to_box",SX(md_attr(o.self,"data_displayed"))));STORE(o.self,"slice_key",CALL(SK(md_attr(o.self,"_bounding_box"),ST(SX(SS(nb::none(),nb::none())),SX(md_attr(o.self,"dims_not_displayed")))),"astype",SX(MC(6))));return nb::none();
}
MD_DEF(shape_line_update){MD_BEGIN;CALL(o.self,"_clean_cache");md_call(md_attr(o.self,"_set_meshes"),{SX(md_attr(o.self,"data_displayed"))},{KW("face",MC(1)),KW("closed",MC(1))});STORE(o.self,"_box",SN("create_box",SX(md_attr(o.self,"data_displayed"))));STORE(o.self,"slice_key",CALL(NP("round",SX(SK(md_attr(o.self,"_bounding_box"),ST(SX(SS(nb::none(),nb::none())),SX(md_attr(o.self,"dims_not_displayed")))))),"astype",SX(MC(4))));return nb::none();}
nb::object md_import_kw(nb::object callback,nb::tuple args,nb::dict kwargs){return md_check(PyObject_Call(callback.ptr(),args.ptr(),kwargs.ptr()));}
nb::object md_import_name(nb::object value){PyObject* name=value.release().ptr();PyUnicode_InternInPlace(&name);if(PyErr_Occurred()){Py_XDECREF(name);throw nb::python_error();}return nb::steal<nb::object>(name);}
// Complete CPython IMPORT_FROM primitive for the source-verified 3.12,
// 3.13 and 3.14 profiles. These are interpreter protocol/error semantics,
// not napari state. Primary ceval.c/moduleobject.c/errors.c sources:
// https://raw.githubusercontent.com/python/cpython/v3.12.8/Python/ceval.c
// https://raw.githubusercontent.com/python/cpython/v3.13.14/Python/ceval.c
// https://raw.githubusercontent.com/python/cpython/v3.14.4/Python/ceval.c
// Compiled/installed 3.13/3.14 application parity is a separate open gate.
nb::object md_optional_attr(nb::handle object, const char* name) {
    PyObject* result = PyObject_GetAttrString(object.ptr(), name);
    if (result) return nb::steal<nb::object>(result);
    if (!PyErr_ExceptionMatches(PyExc_AttributeError)) throw nb::python_error();
    PyErr_Clear();
    return nb::object();
}
[[noreturn]] void md_raise_import(nb::object message, nb::handle name,
    nb::handle path, nb::handle requested_name) {
    if (!message.is_valid()) {
        PyErr_SetString(PyExc_TypeError, "expected a message argument");
        throw nb::python_error();
    }
    nb::dict kwargs;
    kwargs["name"] = name.is_valid() ? nb::borrow<nb::object>(name) : nb::none();
    kwargs["path"] = path.is_valid() ? nb::borrow<nb::object>(path) : nb::none();
#if PY_VERSION_HEX >= 0x030C0000
    kwargs["name_from"] = requested_name;
#endif
    nb::tuple args = nb::make_tuple(message);
    nb::object error = md_import_kw(nb::borrow<nb::object>(PyExc_ImportError), std::move(args), std::move(kwargs));
    PyErr_SetObject(reinterpret_cast<PyObject*>(Py_TYPE(error.ptr())), error.ptr());
    throw nb::python_error();
}
#if PY_VERSION_HEX >= 0x030D0000
// These exported CPython protocol primitives read the actual interpreter
// configuration/sys dictionary/CWD. No sys.path approximation or snapshot.
extern "C" PyAPI_FUNC(const PyConfig*) _Py_GetConfig(void);
extern "C" PyAPI_FUNC(int) _PySys_GetOptionalAttrString(const char*, PyObject**);
// Public/CRT equivalent of the identical CPython 3.13.14/3.14.4
// fileutils.c CWD primitive. A private non-exported helper is not linked.
wchar_t* md_wgetcwd(wchar_t* buffer, size_t capacity) {
#ifdef MS_WINDOWS
    int bounded = static_cast<int>(capacity < INT_MAX ? capacity : INT_MAX);
    return _wgetcwd(buffer, bounded);
#else
    char filename[MAXPATHLEN];
    if (!getcwd(filename, sizeof(filename))) return nullptr;
    size_t length = 0;
    wchar_t* wide = Py_DecodeLocale(filename, &length);
    if (!wide) return nullptr;
    if (capacity <= length) { PyMem_RawFree(wide); return nullptr; }
    std::wcsncpy(buffer, wide, capacity);
    PyMem_RawFree(wide);
    return buffer;
#endif
}
nb::object md_spec_file_origin(nb::handle spec) {
    nb::object located = md_optional_attr(spec, "has_location");
    if (!located.is_valid()) return nb::object();
    bool has_location = md_truth(std::move(located));
    if (!has_location) return nb::object();
    nb::object origin = md_optional_attr(spec, "origin");
    if (origin.is_valid() && !PyUnicode_Check(origin.ptr())) origin.reset();
    return origin;
}
bool md_possibly_shadowing(nb::handle origin) {
    if (!origin.is_valid()) return false;
    const PyConfig* config = _Py_GetConfig();
    if (config->safe_path) return false;
    wchar_t root[MAXPATHLEN + 1];
    Py_ssize_t size = PyUnicode_AsWideChar(origin.ptr(), root, MAXPATHLEN);
    if (size < 0) throw nb::python_error();
    root[size] = L'\0';
    wchar_t* separator = std::wcsrchr(root, SEP);
    if (!separator) return false;
    if (std::wcscmp(separator + 1, L"__init__.py") == 0) {
        *separator = L'\0';
        separator = std::wcsrchr(root, SEP);
        if (!separator) return false;
    }
    *separator = L'\0';
    const wchar_t* path0 = config->sys_path_0;
    if (!path0) return false;
    wchar_t cwd[MAXPATHLEN];
    if (!path0[0]) {
        if (!md_wgetcwd(cwd, MAXPATHLEN)) return false;
        path0 = cwd;
    }
    return std::wcscmp(path0, root) == 0;
}
nb::object md_import_from(nb::handle module, nb::handle requested_name) {
    PyObject* result = PyObject_GetAttr(module.ptr(), requested_name.ptr());
    if (result) return nb::steal<nb::object>(result);
    if (!PyErr_ExceptionMatches(PyExc_AttributeError)) throw nb::python_error();
    PyErr_Clear();
    nb::object mod_name = md_optional_attr(module, "__name__");
    if (mod_name.is_valid() && !PyUnicode_Check(mod_name.ptr())) mod_name.reset();
    if (mod_name.is_valid()) {
        nb::object fullname = md_check(PyUnicode_FromFormat("%U.%U", mod_name.ptr(), requested_name.ptr()));
        result = PyImport_GetModule(fullname.ptr());
        fullname.reset();
        if (result) return nb::steal<nb::object>(result);
        if (PyErr_Occurred()) throw nb::python_error();
    }
    PyObject* raw_mod_name = mod_name.ptr();
    nb::object name_or_unknown = mod_name.is_valid() ? std::move(mod_name) : nb::str("<unknown module name>");
    nb::object origin, spec, message;
    try {
        spec = md_optional_attr(module, "__spec__");
        if (!spec.is_valid()) {
            message = md_check(PyUnicode_FromFormat(
                "cannot import name %R from %R (unknown location)", requested_name.ptr(), name_or_unknown.ptr()));
        } else {
            origin = md_spec_file_origin(spec);
            bool shadowing = md_possibly_shadowing(origin);
            bool shadowing_stdlib = false;
            if (shadowing) {
                PyObject* raw_stdlib = nullptr;
                if (_PySys_GetOptionalAttrString("stdlib_module_names", &raw_stdlib) < 0) throw nb::python_error();
                nb::object stdlib = nb::steal<nb::object>(raw_stdlib);
                if (stdlib.is_valid() && PyAnySet_Check(stdlib.ptr())) {
                    int contains = PySet_Contains(stdlib.ptr(), name_or_unknown.ptr());
                    if (contains < 0) throw nb::python_error();
                    shadowing_stdlib = contains != 0;
                }
            }
            if (!origin.is_valid() && PyModule_Check(module.ptr())) {
                origin = nb::steal<nb::object>(PyModule_GetFilenameObject(module.ptr()));
                if (!origin.is_valid()) {
                    if (!PyErr_ExceptionMatches(PyExc_SystemError)) throw nb::python_error();
                    PyErr_Clear();
                }
            }
            if (shadowing_stdlib) {
                message = md_check(PyUnicode_FromFormat(
                    "cannot import name %R from %R (consider renaming %R since it has the same name as the standard library module named %R and prevents importing that standard library module)",
                    requested_name.ptr(), name_or_unknown.ptr(), origin.ptr(), name_or_unknown.ptr()));
            } else {
                nb::object initializing = md_optional_attr(spec, "_initializing");
                bool is_initializing = initializing.is_valid() && md_truth(std::move(initializing));
                if (is_initializing && shadowing) {
                    message = md_check(PyUnicode_FromFormat(
                        "cannot import name %R from %R (consider renaming %R if it has the same name as a library you intended to import)",
                        requested_name.ptr(), name_or_unknown.ptr(), origin.ptr()));
                } else if (is_initializing && origin.is_valid()) {
                    message = md_check(PyUnicode_FromFormat(
                        "cannot import name %R from partially initialized module %R (most likely due to a circular import) (%S)",
                        requested_name.ptr(), name_or_unknown.ptr(), origin.ptr()));
                } else if (is_initializing) {
                    message = md_check(PyUnicode_FromFormat(
                        "cannot import name %R from partially initialized module %R (most likely due to a circular import)",
                        requested_name.ptr(), name_or_unknown.ptr()));
                } else if (origin.is_valid()) {
                    message = md_check(PyUnicode_FromFormat("cannot import name %R from %R (%S)",
                        requested_name.ptr(), name_or_unknown.ptr(), origin.ptr()));
                } else {
                    message = md_check(PyUnicode_FromFormat("cannot import name %R from %R (unknown location)",
                        requested_name.ptr(), name_or_unknown.ptr()));
                }
            }
        }
        md_raise_import(std::move(message), nb::handle(raw_mod_name), origin, requested_name);
    } catch (...) {
        // Actual import_from error cleanup order, including callback failures.
        message.reset(); origin.reset(); spec.reset(); name_or_unknown.reset();
        throw;
    }
}
#else
bool md_spec_initializing(nb::handle spec) {
    if (spec.is_valid()) {
        nb::object value = nb::steal<nb::object>(PyObject_GetAttrString(spec.ptr(), "_initializing"));
        if (!value.is_valid() && PyErr_ExceptionMatches(PyExc_AttributeError)) {
            PyErr_Clear();
            return false;
        }
        if (value.is_valid()) {
            int initializing = PyObject_IsTrue(value.ptr());
            value.reset();
            if (initializing >= 0) return initializing != 0;
        }
    }
    PyErr_Clear();
    return false;
}
nb::object md_import_from(nb::handle module, nb::handle requested_name) {
    PyObject* result = PyObject_GetAttr(module.ptr(), requested_name.ptr());
    if (result) return nb::steal<nb::object>(result);
    if (!PyErr_ExceptionMatches(PyExc_AttributeError)) throw nb::python_error();
    PyErr_Clear();
    nb::object pkgname = nb::steal<nb::object>(PyObject_GetAttrString(module.ptr(), "__name__"));
    if (pkgname.is_valid() && !PyUnicode_Check(pkgname.ptr())) pkgname.reset();
    if (pkgname.is_valid()) {
        nb::object fullname = md_check(PyUnicode_FromFormat("%U.%U", pkgname.ptr(), requested_name.ptr()));
        result = PyImport_GetModule(fullname.ptr());
        fullname.reset();
        if (result) return nb::steal<nb::object>(result);
        if (PyErr_Occurred()) throw nb::python_error();
    }
    nb::object pkgpath = nb::steal<nb::object>(PyModule_GetFilenameObject(module.ptr()));
    PyObject* raw_pkgname = pkgname.ptr();
    nb::object name_or_unknown = pkgname.is_valid() ? std::move(pkgname) : nb::str("<unknown module name>");
    nb::object message;
    nb::handle error_path;
    if (!pkgpath.is_valid() || !PyUnicode_Check(pkgpath.ptr())) {
        PyErr_Clear();
        message = nb::steal<nb::object>(PyUnicode_FromFormat(
            "cannot import name %R from %R (unknown location)", requested_name.ptr(), name_or_unknown.ptr()));
    } else {
        nb::object spec = nb::steal<nb::object>(PyObject_GetAttrString(module.ptr(), "__spec__"));
        bool initializing = md_spec_initializing(spec);
        spec.reset();
        const char* format = initializing
            ? "cannot import name %R from partially initialized module %R (most likely due to a circular import) (%S)"
            : "cannot import name %R from %R (%S)";
        message = nb::steal<nb::object>(PyUnicode_FromFormat(format, requested_name.ptr(), name_or_unknown.ptr(), pkgpath.ptr()));
        error_path = pkgpath;
    }
    try { md_raise_import(std::move(message), nb::handle(raw_pkgname), error_path, requested_name); }
    catch (...) {
        // Actual 3.12 error cleanup order.
        message.reset(); name_or_unknown.reset(); pkgpath.reset();
        throw;
    }
}
#endif
MD_DEF(shape_polygon_base_update){MD_BEGIN;CALL(o.self,"_clean_cache");PUT(data,md_attr(o.self,"data_displayed"));
    if(md_truth(SC(md_attr(o.self,"interpolation_order"),MC(1),Py_GT))){
        nb::object module=ns.import_module(md_import_name(MC(11)),MC(3),MC(2));PUT(splev,md_import_from(module,SK(MC(3),nb::int_(0))));PUT(splprep,md_import_from(module,SK(MC(3),nb::int_(1))));module.reset();
        PUT(data_spline,md_call(ns.get("remove_path_duplicates"),{SX(o.data)},{KW("closed",MC(4))}));
        if(md_truth(SC(SN("len",SX(o.data_spline)),md_attr(o.self,"interpolation_order"),Py_GT))){
            PUT(data,CALL(o.data_spline,"copy"));if(md_truth(md_attr(o.self,"_closed")))PUT(data,md_call(ns.np("append"),{SX(o.data),SX(SK(o.data,SS(nb::none(),MC(1))))},{KW("axis",MC(2))}));
            nb::object supplied=md_call(o.splprep,{SX(md_attr(o.data,"T"))},{KW("s",MC(2)),KW("k",md_attr(o.self,"interpolation_order")),KW("per",md_attr(o.self,"_closed"))});PyObject* raw_iterator=PyObject_GetIter(supplied.ptr());
            if(!raw_iterator){if(PyErr_ExceptionMatches(PyExc_TypeError)&&!Py_TYPE(supplied.ptr())->tp_iter&&!PySequence_Check(supplied.ptr()))PyErr_Format(PyExc_TypeError,"cannot unpack non-iterable %.200s object",Py_TYPE(supplied.ptr())->tp_name);throw nb::python_error();}nb::object iterator=nb::steal<nb::object>(raw_iterator);nb::object head=md_next(iterator);
            if(!head.is_valid()){PyErr_SetString(PyExc_ValueError,"not enough values to unpack (expected at least 1, got 0)");throw nb::python_error();}nb::object tail=md_check(PySequence_List(iterator.ptr()));iterator.reset();supplied.reset();PUT(tck,std::move(head));PUT(_,std::move(tail));
            PUT(u,NP("linspace",SX(MC(2)),SX(MC(1)),SX(SM(md_attr(o.self,"interpolation_sampling"),SN("len",SX(o.data))))));
            PUT(data,CALL(SK(md_call(ns.np("stack"),{SX(md_call(o.splev,{SX(o.u),SX(o.tck)}))},{KW("axis",MC(1))}),SS(nb::none(),MC(9))),"astype",SX(md_attr(ns.get("np"),"float32"))));
        }
    }
    md_call(md_attr(o.self,"_set_meshes"),{SX(o.data)},{KW("face",md_attr(o.self,"_filled")),KW("closed",md_attr(o.self,"_closed"))});PUT(bbox,SK(md_attr(o.self,"_bounding_box"),ST(SX(SS(nb::none(),nb::none())),SX(md_attr(o.self,"dims_displayed")))));STORE(o.self,"_box",SN("create_box_from_bounding",SX(o.bbox)));STORE(o.self,"slice_key",CALL(NP("rint",SX(SK(md_attr(o.self,"_bounding_box"),ST(SX(SS(nb::none(),nb::none())),SX(md_attr(o.self,"dims_not_displayed")))))),"astype",SX(ns.get("int"))));return nb::none();
}
MD_DEF(shape_model_transform){MD_BEGIN;STORE(o.self,"_box",MM(md_attr(o.self,"_box"),md_attr(o.transform,"T")));
    SET(md_attr(o.self,"_data"),ST(SX(SS(nb::none(),nb::none())),SX(md_attr(o.self,"dims_displayed"))),MM(SK(md_attr(o.self,"_data"),ST(SX(SS(nb::none(),nb::none())),SX(md_attr(o.self,"dims_displayed")))),md_attr(o.transform,"T")));
    STORE(o.self,"_face_vertices",MM(md_attr(o.self,"_face_vertices"),md_attr(o.transform,"T")));CALL(md_attr(o.self,"__dict__"),"pop",SX(MC(2)),SX(MC(1)));PUT(points,md_attr(o.self,"data_displayed"));PUT(points,md_call(ns.get("remove_path_duplicates"),{SX(o.points)},{KW("closed",md_attr(o.self,"_closed"))}));
    auto parts=md_unpack(md_call(md_attr(o.self,"_triangulate_edge"),{SX(o.points)},{KW("closed",md_attr(o.self,"_closed"))}),3);PUT(centers,std::move(parts[0]));PUT(offsets,std::move(parts[1]));PUT(triangles,std::move(parts[2]));STORE(o.self,"_edge_vertices",o.centers);STORE(o.self,"_edge_offsets",o.offsets);STORE(o.self,"_edge_triangles",o.triangles);
    STORE(o.self,"_bounding_box",NP("array",SX(SL(SX(md_call(ns.np("min"),{SX(md_attr(o.self,"_data"))},{KW("axis",MC(4))})),SX(md_call(ns.np("max"),{SX(md_attr(o.self,"_data"))},{KW("axis",MC(4))}))))));CALL(o.self,"_clean_cache");return nb::none();
}
MD_DEF(shape_ellipse_transform){MD_BEGIN;STORE(o.self,"_box",MM(md_attr(o.self,"_box"),md_attr(o.transform,"T")));
    SET(md_attr(o.self,"_data"),ST(SX(SS(nb::none(),nb::none())),SX(md_attr(o.self,"dims_displayed"))),MM(SK(md_attr(o.self,"_data"),ST(SX(SS(nb::none(),nb::none())),SX(md_attr(o.self,"dims_displayed")))),md_attr(o.transform,"T")));
    STORE(o.self,"_face_vertices",MM(md_attr(o.self,"_face_vertices"),md_attr(o.transform,"T")));PUT(points,SK(md_attr(o.self,"_face_vertices"),SS(MC(2),MC(3))));auto parts=md_unpack(md_call(ns.get("triangulate_edge"),{SX(o.points)},{KW("closed",md_attr(o.self,"_closed"))}),3);PUT(centers,std::move(parts[0]));PUT(offsets,std::move(parts[1]));PUT(triangles,std::move(parts[2]));STORE(o.self,"_edge_vertices",o.centers);STORE(o.self,"_edge_offsets",o.offsets);STORE(o.self,"_edge_triangles",o.triangles);
    STORE(o.self,"_bounding_box",NP("array",SX(SL(SX(md_call(ns.np("min"),{SX(md_attr(o.self,"_data"))},{KW("axis",MC(5))})),SX(md_call(ns.np("max"),{SX(md_attr(o.self,"_data"))},{KW("axis",MC(5))}))))));CALL(o.self,"_clean_cache");return nb::none();
}
MD_DEF(shape_model_shift){MD_BEGIN;PUT(shift,NP("array",SX(o.shift)));STORE(o.self,"_face_vertices",SA(md_attr(o.self,"_face_vertices"),o.shift));STORE(o.self,"_edge_vertices",SA(md_attr(o.self,"_edge_vertices"),o.shift));STORE(o.self,"_box",SA(md_attr(o.self,"_box"),o.shift));
    SET(md_attr(o.self,"_data"),ST(SX(SS(nb::none(),nb::none())),SX(md_attr(o.self,"dims_displayed"))),SA(md_attr(o.self,"data_displayed"),o.shift));
    SET(md_attr(o.self,"_bounding_box"),ST(SX(SS(nb::none(),nb::none())),SX(md_attr(o.self,"dims_displayed"))),SA(SK(md_attr(o.self,"_bounding_box"),ST(SX(SS(nb::none(),nb::none())),SX(md_attr(o.self,"dims_displayed")))),o.shift));CALL(o.self,"_clean_cache");return nb::none();
}
MD_DEF(shape_model_scale){MD_BEGIN;
    if(md_truth(SN("isinstance",SX(o.scale),SX(md_binary(SX(ns.get("list")),SX(md_attr(ns.get("np"),"ndarray")),PyNumber_Or))))){PUT(transform,NP("array",SX(SL(SX(SL(SX(SK(o.scale,MC(1))),SX(MC(1)))),SX(SL(SX(MC(1)),SX(SK(o.scale,MC(2)))))))));}
    else PUT(transform,NP("array",SX(SL(SX(SL(SX(o.scale),SX(MC(1)))),SX(SL(SX(MC(1)),SX(o.scale)))))));
    if(o.center.is_none())CALL(o.self,"transform",SX(o.transform));else{PUT(center,NP("array",SX(o.center)));CALL(o.self,"shift",SX(MN(o.center)));CALL(o.self,"transform",SX(o.transform));CALL(o.self,"shift",SX(o.center));}return nb::none();
}
MD_DEF(shape_model_rotate){MD_BEGIN;PUT(theta,NP("radians",SX(o.angle)));PUT(transform,NP("array",SX(SL(SX(SL(SX(NP("cos",SX(o.theta))),SX(NP("sin",SX(o.theta))))),SX(SL(SX(MN(NP("sin",SX(o.theta)))),SX(NP("cos",SX(o.theta)))))))));
    if(o.center.is_none())CALL(o.self,"transform",SX(o.transform));else{PUT(center,NP("array",SX(o.center)));CALL(o.self,"shift",SX(MN(o.center)));CALL(o.self,"transform",SX(o.transform));CALL(o.self,"shift",SX(o.center));}return nb::none();
}
MD_DEF(shape_model_flip){MD_BEGIN;if(md_truth(SC(o.axis,MC(1),Py_EQ)))PUT(transform,NP("array",SX(SL(SX(SL(SX(MC(2)),SX(MC(1)))),SX(SL(SX(MC(1)),SX(MC(3))))))));
    else if(md_truth(SC(o.axis,MC(2),Py_EQ)))PUT(transform,NP("array",SX(SL(SX(SL(SX(MC(3)),SX(MC(1)))),SX(SL(SX(MC(1)),SX(MC(2))))))));
    else md_raise(SN("ValueError",SX(MC(4))));
    if(o.center.is_none())CALL(o.self,"transform",SX(o.transform));else{CALL(o.self,"shift",SX(MN(o.center)));CALL(o.self,"transform",SX(o.transform));CALL(o.self,"shift",SX(MN(o.center)));}return nb::none();
}

MD_DEF(shape_model_to_mask){MD_BEGIN;
    if(o.mask_shape.is_none())PUT(mask_shape,CALL(NP("round",SX(md_call(md_attr(md_attr(o.self,"data_displayed"),"max"),{},{KW("axis",MC(2))}))),"astype",SX(MC(4))));
    if(md_truth(SC(SN("len",SX(o.mask_shape)),MC(5),Py_EQ))){PUT(embedded,MC(6));PUT(shape_plane,o.mask_shape);}
    else if(md_truth(SC(SN("len",SX(o.mask_shape)),SK(md_attr(md_attr(o.self,"data"),"shape"),MC(7)),Py_EQ))){PUT(embedded,MC(8));
        nb::object iterator=md_iter(md_attr(o.self,"dims_displayed"));nb::object saved=std::move(o.d);nb::list plane;
        try{while(nb::object value=md_next(iterator)){PUT(d,std::move(value));plane.append(SK(o.mask_shape,o.d));}}catch(...){iterator.reset();plane.reset();md_assign(o.d,std::move(saved));throw;}
        iterator.reset();PUT(shape_plane,std::move(plane));md_assign(o.d,std::move(saved));
    }else md_raise(SN("ValueError",SX(md_string({SX(MC(9)),SX(md_format(SK(md_attr(md_attr(o.self,"data"),"shape"),MC(7)))),SX(MC(10)),SX(md_format(SN("len",SX(o.mask_shape)))),SX(MC(11))}))));
    if(md_truth(md_attr(o.self,"_use_face_vertices")))PUT(data,md_attr(o.self,"_face_vertices"));else PUT(data,md_attr(o.self,"data_displayed"));
    PUT(data,SK(o.data,ST(SX(SS(nb::none(),nb::none())),SX(SS(MN(SN("len",SX(o.shape_plane))),nb::none())))));
    if(md_truth(md_attr(o.self,"_filled")))PUT(mask_p,SN("poly_to_mask",SX(o.shape_plane),SX(SM(SB(o.data,o.offset),o.zoom_factor))));
    else PUT(mask_p,SN("path_to_mask",SX(o.shape_plane),SX(SM(SB(o.data,o.offset),o.zoom_factor))));
    if(md_truth(o.embedded)){
        PUT(mask,md_call(ns.np("zeros"),{SX(o.mask_shape)},{KW("dtype",ns.get("bool"))}));PUT(slice_key,SM(SL(SX(MC(2))),SN("len",SX(o.mask_shape))));
        nb::object iterator=md_iter(SN("range",SX(SN("len",SX(o.mask_shape)))));
        while(nb::object value=md_next(iterator)){PUT(i,std::move(value));
            if(md_in(SX(o.i),SX(md_attr(o.self,"dims_displayed"))))SET(o.slice_key,o.i,SN("slice",SX(MC(1))));
            else if(!md_attr(o.self,"slice_key").is_none())SET(o.slice_key,o.i,SN("slice",SX(SK(md_attr(o.self,"slice_key"),ST(SX(MC(2)),SX(o.i)))),SX(SA(SK(md_attr(o.self,"slice_key"),ST(SX(MC(7)),SX(o.i))),MC(7)))));
            else md_raise(SN("RuntimeError",SX(MC(13))));
        }iterator.reset();PUT(displayed_order,SN("argsort",SX(md_attr(o.self,"dims_displayed"))));SET(o.mask,SN("tuple",SX(o.slice_key)),CALL(o.mask_p,"transpose",SX(o.displayed_order)));
    }else PUT(mask,o.mask_p);return o.mask;
}
MD_DEF(shape_model_clean_cache){
    if(md_in(SX(MC(1)),SX(md_attr(o.self,"__dict__")))){nb::object dictionary=md_attr(o.self,"__dict__");nb::object key=MC(1);if(PyObject_DelItem(dictionary.ptr(),key.ptr())<0)throw nb::python_error();}
    if(md_in(SX(MC(2)),SX(md_attr(o.self,"__dict__")))){nb::object dictionary=md_attr(o.self,"__dict__");nb::object key=MC(2);if(PyObject_DelItem(dictionary.ptr(),key.ptr())<0)throw nb::python_error();}return nb::none();
}
} // anonymous namespace

void bind_viewer_shape_model(nb::module_& m){
    PyType_Spec spec={"naparixx._native.ShapeModelNamespaceBase",static_cast<int>(sizeof(ModelNamespaceObject)),0,Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE|Py_TPFLAGS_HAVE_GC,md_namespace_slots};
    m.attr("ShapeModelNamespaceBase")=md_check(PyType_FromSpec(&spec));
    auto locals=nb::class_<ShapeModelLocals>(m,"ShapeModelLocals",nb::is_weak_referenceable(),nb::type_slots(md_owner_slots));
#define MD_PROP(n) locals.def_prop_ro(#n,[](const ShapeModelLocals& o){return o.n.is_valid()?o.n:nb::none();});
    MODEL_FIELDS(MD_PROP)
#undef MD_PROP
    m.def("shape_model_locals",&shape_model_locals,nb::arg("layout"),nb::arg("bindings"));
#define MD_BIND_FN(n) m.def(#n,&n,nb::arg("namespace"),nb::arg("literals"),nb::arg("owner"));
#define MD_BIND_SUPER(n) m.def(#n,&n,nb::arg("namespace"),nb::arg("literals"),nb::arg("super_provider").none(),nb::arg("owner"));
    MD_BIND_FN(shape_model_init)
    MD_BIND_SUPER(shape_model_new)
    MD_BIND_SUPER(shape_rectangle_init)
    MD_BIND_SUPER(shape_ellipse_init)
    MD_BIND_SUPER(shape_line_init)
    MD_BIND_SUPER(shape_polygon_base_init)
    MD_BIND_SUPER(shape_path_init)
    MD_BIND_SUPER(shape_polygon_init)
    MD_BIND_FN(shape_model_abstract_data)
    MD_BIND_FN(shape_model_abstract_set_data)
    MD_BIND_FN(shape_model_abstract_update)
    MD_BIND_FN(shape_model_ndisplay)
    MD_BIND_FN(shape_model_set_ndisplay)
    MD_BIND_FN(shape_model_dims_order)
    MD_BIND_FN(shape_model_set_dims_order)
    MD_BIND_FN(shape_model_dims_displayed)
    MD_BIND_FN(shape_model_dims_not_displayed)
    MD_BIND_FN(shape_model_data_displayed)
    MD_BIND_FN(shape_model_bounding_box)
    MD_BIND_FN(shape_model_edge_width)
    MD_BIND_FN(shape_model_set_edge_width)
    MD_BIND_FN(shape_model_z_index)
    MD_BIND_FN(shape_model_set_z_index)
    MD_BIND_FN(shape_model_vertices_count)
    MD_BIND_FN(shape_model_triangles_count)
    MD_BIND_FN(shape_model_face_triangles_count)
    MD_BIND_FN(shape_model_face_vertices_count)
    MD_BIND_FN(shape_model_edge_triangles_count)
    MD_BIND_FN(shape_model_edge_vertices_count)
    MD_BIND_FN(shape_rectangle_data)
    MD_BIND_FN(shape_ellipse_data)
    MD_BIND_FN(shape_line_data)
    MD_BIND_FN(shape_polygon_base_data)
    MD_BIND_FN(shape_model_empty_edge)
    MD_BIND_FN(shape_model_empty_face)
    MD_BIND_FN(shape_model_meshes_3d)
    MD_BIND_FN(shape_model_meshes_bermuda)
    MD_BIND_FN(shape_model_meshes_partseg)
    MD_BIND_FN(shape_model_meshes_triangle)
    MD_BIND_FN(shape_model_meshes_py)
    MD_BIND_FN(shape_model_triangulate_edge)
    MD_BIND_FN(shape_model_triangulate_partseg)
    MD_BIND_FN(shape_model_triangulate_bermuda)
    MD_BIND_FN(shape_rectangle_set_data)
    MD_BIND_FN(shape_ellipse_set_data)
    MD_BIND_FN(shape_line_set_data)
    MD_BIND_FN(shape_polygon_base_set_data)
    MD_BIND_FN(shape_rectangle_update)
    MD_BIND_FN(shape_ellipse_update)
    MD_BIND_FN(shape_line_update)
    MD_BIND_FN(shape_polygon_base_update)
    MD_BIND_FN(shape_model_transform)
    MD_BIND_FN(shape_ellipse_transform)
    MD_BIND_FN(shape_model_shift)
    MD_BIND_FN(shape_model_scale)
    MD_BIND_FN(shape_model_rotate)
    MD_BIND_FN(shape_model_flip)
    MD_BIND_FN(shape_model_to_mask)
    MD_BIND_FN(shape_model_clean_cache)
#undef MD_BIND_FN
#undef MD_BIND_SUPER
}
} // namespace nx

