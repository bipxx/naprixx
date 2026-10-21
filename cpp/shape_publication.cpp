#include "arrays.hpp"
#include "collection_protocol.hpp"
#include <nanobind/stl/string.h>
#if PY_VERSION_HEX >= 0x030C0000 && PY_VERSION_HEX < 0x030D0000
#include <internal/pycore_frame.h>
#endif
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
#include <utility>
#include <vector>

namespace nx {
namespace {
nb::object pu_check(PyObject* value) { if (!value) throw nb::python_error(); return nb::steal<nb::object>(value); }
nb::object pu_attr(nb::object value, const char* name) { return pu_check(PyObject_GetAttrString(value.ptr(), name)); }
nb::object pu_item(nb::object value, nb::object key) { return pu_check(PyObject_GetItem(value.ptr(), key.ptr())); }
void pu_assign(nb::object& target, nb::object value) { nb::object old=std::move(target); target=std::move(value); old.reset(); }
bool pu_truth(nb::object value) { int truth=PyObject_IsTrue(value.ptr()); if(truth<0)throw nb::python_error(); return truth!=0; }
nb::object pu_iter(nb::object value) { return pu_check(PyObject_GetIter(value.ptr())); }
nb::object pu_next(nb::handle iterator) { PyObject* value=PyIter_Next(iterator.ptr()); if(!value&&PyErr_Occurred())throw nb::python_error(); return nb::steal<nb::object>(value); }
using PUExpr=std::function<nb::object()>;
using PUKw=std::pair<nb::object,PUExpr>;
nb::object pu_call(nb::object fn,std::initializer_list<PUExpr> args={},std::initializer_list<PUKw> keywords={}) {
    nb::tuple positional=nb::steal<nb::tuple>(PyTuple_New(static_cast<Py_ssize_t>(args.size())));
    if(!positional.is_valid())throw nb::python_error(); Py_ssize_t index=0;
    for(const auto& expression:args){nb::object value=expression();PyTuple_SET_ITEM(positional.ptr(),index++,value.release().ptr());}
    nb::dict kwargs;for(const auto& keyword:keywords){nb::object value=keyword.second();if(PyDict_SetItem(kwargs.ptr(),keyword.first.ptr(),value.ptr())<0)throw nb::python_error();}
    return pu_check(PyObject_Call(fn.ptr(),positional.ptr(),keywords.size()?kwargs.ptr():nullptr));
}
nb::list pu_list(std::initializer_list<PUExpr> values){nb::list result;for(const auto& expression:values)result.append(expression());return result;}
nb::object pu_compare(PUExpr left,PUExpr right,int operation){nb::object a=left();nb::object b=right();return pu_check(PyObject_RichCompare(a.ptr(),b.ptr(),operation));}
nb::object pu_literal(const nb::tuple& literals,Py_ssize_t index){PyObject* value=PyTuple_GetItem(literals.ptr(),index);if(!value)throw nb::python_error();return nb::borrow<nb::object>(value);}
nb::object pu_bool(bool value){return nb::borrow<nb::object>(value?Py_True:Py_False);}
nb::object pu_format(nb::object value){return pu_check(PyObject_Format(value.ptr(),nullptr));}
template<size_t N> nb::object pu_string(std::array<nb::object,N> values){
    nb::tuple tuple=nb::steal<nb::tuple>(PyTuple_New(N));if(!tuple.is_valid())throw nb::python_error();
    for(size_t i=0;i<N;++i)PyTuple_SET_ITEM(tuple.ptr(),i,values[i].inc_ref().ptr());
    PyObject* result=PyUnicode_Join(nb::str("").ptr(),tuple.ptr());tuple.reset();for(size_t i=N;i>0;)values[--i].reset();return pu_check(result);
}
[[noreturn]] void pu_raise(nb::object value){
    if(PyExceptionClass_Check(value.ptr())){nb::object klass=std::move(value);value=pu_call(klass);
        if(!PyExceptionInstance_Check(value.ptr())){PyErr_Format(PyExc_TypeError,"calling %R should have returned an instance of BaseException, not %R",klass.ptr(),reinterpret_cast<PyObject*>(Py_TYPE(value.ptr())));throw nb::python_error();}}
    if(PyExceptionInstance_Check(value.ptr()))PyErr_SetObject(reinterpret_cast<PyObject*>(Py_TYPE(value.ptr())),value.ptr());
    else PyErr_SetString(PyExc_TypeError,"exceptions must derive from BaseException");throw nb::python_error();
}
nb::object pu_frame_locals(){
#if PY_VERSION_HEX >= 0x030C0000 && PY_VERSION_HEX < 0x030D0000
    PyFrameObject* frame=PyEval_GetFrame();if(!frame){if(PyErr_Occurred())throw nb::python_error();return nb::none();}
    PyObject* locals=frame->f_frame->f_locals;return nb::borrow<nb::object>(locals?locals:Py_None);
#else
    PyErr_SetString(PyExc_RuntimeError,"Shape publication IMPORT_NAME existing-locals handoff requires its reviewed CPython 3.12 profile; other required profiles remain open");throw nb::python_error();
#endif
}
struct PublicationGlobals {
    nb::dict values;nb::object builtins;
    explicit PublicationGlobals(nb::dict value):values(std::move(value)),builtins(nb::borrow<nb::object>(PyEval_GetBuiltins())){}
    nb::object get(const char* name)const{
        nb::object key=nb::str(name);PyObject* found=PyDict_GetItemWithError(values.ptr(),key.ptr());
        if(found)return nb::borrow<nb::object>(found);if(PyErr_Occurred())throw nb::python_error();
        found=PyObject_GetItem(builtins.ptr(),key.ptr());if(found)return nb::steal<nb::object>(found);
        if(!PyErr_ExceptionMatches(PyExc_KeyError))throw nb::python_error();PyErr_Clear();
        nb::object exception=pu_call(nb::borrow<nb::object>(PyExc_NameError),{[&](){return pu_check(PyUnicode_FromFormat("name '%s' is not defined",name));}});
        if(PyObject_SetAttrString(exception.ptr(),"name",key.ptr())<0)throw nb::python_error();pu_raise(std::move(exception));
    }
    void publish(const char* name,nb::object value)const{if(PyDict_SetItemString(values.ptr(),name,value.ptr())<0)throw nb::python_error();}
    nb::object np(const char* name)const{return pu_attr(get("np"),name);}
    nb::object import_module(nb::object name,nb::object fromlist,nb::object level)const{
        PyObject* raw=PyObject_GetItem(builtins.ptr(),nb::str("__import__").ptr());
        if(!raw){if(!PyErr_ExceptionMatches(PyExc_KeyError))throw nb::python_error();PyErr_Clear();PyErr_SetString(PyExc_ImportError,"__import__ not found");throw nb::python_error();}
        nb::object importer=nb::steal<nb::object>(raw);nb::object locals=pu_frame_locals();
        return pu_check(PyObject_CallFunctionObjArgs(importer.ptr(),name.ptr(),values.ptr(),locals.ptr(),fromlist.ptr(),level.ptr(),nullptr));
    }
};
nb::object pu_import_name(nb::object name){PyObject* raw=name.release().ptr();PyUnicode_InternInPlace(&raw);if(PyErr_Occurred()){Py_XDECREF(raw);throw nb::python_error();}return nb::steal<nb::object>(raw);}
nb::object pu_import_kw(nb::object callback,nb::tuple args,nb::dict kwargs){return pu_check(PyObject_Call(callback.ptr(),args.ptr(),kwargs.ptr()));}
nb::object pu_optional_attr(nb::handle object, const char* name) {
    PyObject* result = PyObject_GetAttrString(object.ptr(), name);
    if (result) return nb::steal<nb::object>(result);
    if (!PyErr_ExceptionMatches(PyExc_AttributeError)) throw nb::python_error();
    PyErr_Clear();
    return nb::object();
}
[[noreturn]] void pu_raise_import(nb::object message, nb::handle name,
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
    nb::object error = pu_import_kw(nb::borrow<nb::object>(PyExc_ImportError), std::move(args), std::move(kwargs));
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
wchar_t* pu_wgetcwd(wchar_t* buffer, size_t capacity) {
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
nb::object pu_spec_file_origin(nb::handle spec) {
    nb::object located = pu_optional_attr(spec, "has_location");
    if (!located.is_valid()) return nb::object();
    bool has_location = pu_truth(std::move(located));
    if (!has_location) return nb::object();
    nb::object origin = pu_optional_attr(spec, "origin");
    if (origin.is_valid() && !PyUnicode_Check(origin.ptr())) origin.reset();
    return origin;
}
bool pu_possibly_shadowing(nb::handle origin) {
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
        if (!pu_wgetcwd(cwd, MAXPATHLEN)) return false;
        path0 = cwd;
    }
    return std::wcscmp(path0, root) == 0;
}
nb::object pu_import_from(nb::handle module, nb::handle requested_name) {
    PyObject* result = PyObject_GetAttr(module.ptr(), requested_name.ptr());
    if (result) return nb::steal<nb::object>(result);
    if (!PyErr_ExceptionMatches(PyExc_AttributeError)) throw nb::python_error();
    PyErr_Clear();
    nb::object mod_name = pu_optional_attr(module, "__name__");
    if (mod_name.is_valid() && !PyUnicode_Check(mod_name.ptr())) mod_name.reset();
    if (mod_name.is_valid()) {
        nb::object fullname = pu_check(PyUnicode_FromFormat("%U.%U", mod_name.ptr(), requested_name.ptr()));
        result = PyImport_GetModule(fullname.ptr());
        fullname.reset();
        if (result) return nb::steal<nb::object>(result);
        if (PyErr_Occurred()) throw nb::python_error();
    }
    PyObject* raw_mod_name = mod_name.ptr();
    nb::object name_or_unknown = mod_name.is_valid() ? std::move(mod_name) : nb::str("<unknown module name>");
    nb::object origin, spec, message;
    try {
        spec = pu_optional_attr(module, "__spec__");
        if (!spec.is_valid()) {
            message = pu_check(PyUnicode_FromFormat(
                "cannot import name %R from %R (unknown location)", requested_name.ptr(), name_or_unknown.ptr()));
        } else {
            origin = pu_spec_file_origin(spec);
            bool shadowing = pu_possibly_shadowing(origin);
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
                message = pu_check(PyUnicode_FromFormat(
                    "cannot import name %R from %R (consider renaming %R since it has the same name as the standard library module named %R and prevents importing that standard library module)",
                    requested_name.ptr(), name_or_unknown.ptr(), origin.ptr(), name_or_unknown.ptr()));
            } else {
                nb::object initializing = pu_optional_attr(spec, "_initializing");
                bool is_initializing = initializing.is_valid() && pu_truth(std::move(initializing));
                if (is_initializing && shadowing) {
                    message = pu_check(PyUnicode_FromFormat(
                        "cannot import name %R from %R (consider renaming %R if it has the same name as a library you intended to import)",
                        requested_name.ptr(), name_or_unknown.ptr(), origin.ptr()));
                } else if (is_initializing && origin.is_valid()) {
                    message = pu_check(PyUnicode_FromFormat(
                        "cannot import name %R from partially initialized module %R (most likely due to a circular import) (%S)",
                        requested_name.ptr(), name_or_unknown.ptr(), origin.ptr()));
                } else if (is_initializing) {
                    message = pu_check(PyUnicode_FromFormat(
                        "cannot import name %R from partially initialized module %R (most likely due to a circular import)",
                        requested_name.ptr(), name_or_unknown.ptr()));
                } else if (origin.is_valid()) {
                    message = pu_check(PyUnicode_FromFormat("cannot import name %R from %R (%S)",
                        requested_name.ptr(), name_or_unknown.ptr(), origin.ptr()));
                } else {
                    message = pu_check(PyUnicode_FromFormat("cannot import name %R from %R (unknown location)",
                        requested_name.ptr(), name_or_unknown.ptr()));
                }
            }
        }
        pu_raise_import(std::move(message), nb::handle(raw_mod_name), origin, requested_name);
    } catch (...) {
        // Actual import_from error cleanup order, including callback failures.
        message.reset(); origin.reset(); spec.reset(); name_or_unknown.reset();
        throw;
    }
}
#else
bool pu_spec_initializing(nb::handle spec) {
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
nb::object pu_import_from(nb::handle module, nb::handle requested_name) {
    PyObject* result = PyObject_GetAttr(module.ptr(), requested_name.ptr());
    if (result) return nb::steal<nb::object>(result);
    if (!PyErr_ExceptionMatches(PyExc_AttributeError)) throw nb::python_error();
    PyErr_Clear();
    nb::object pkgname = nb::steal<nb::object>(PyObject_GetAttrString(module.ptr(), "__name__"));
    if (pkgname.is_valid() && !PyUnicode_Check(pkgname.ptr())) pkgname.reset();
    if (pkgname.is_valid()) {
        nb::object fullname = pu_check(PyUnicode_FromFormat("%U.%U", pkgname.ptr(), requested_name.ptr()));
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
        bool initializing = pu_spec_initializing(spec);
        spec.reset();
        const char* format = initializing
            ? "cannot import name %R from partially initialized module %R (most likely due to a circular import) (%S)"
            : "cannot import name %R from %R (%S)";
        message = nb::steal<nb::object>(PyUnicode_FromFormat(format, requested_name.ptr(), name_or_unknown.ptr(), pkgpath.ptr()));
        error_path = pkgpath;
    }
    try { pu_raise_import(std::move(message), nb::handle(raw_pkgname), error_path, requested_name); }
    catch (...) {
        // Actual 3.12 error cleanup order.
        message.reset(); name_or_unknown.reset(); pkgpath.reset();
        throw;
    }
}
#endif


#define PUBLICATION_FIELDS(X) \
 X(value) X(val) X(name) X(order) X(data) X(data2) X(v) X(e) X(values) X(self) X(cls) X(backend) \
 X(_set_numba) X(_set_warmup) X(shape) X(bermuda_loaded) X(partsegcore_loaded) X(any_compiled_loaded) X(need_numba_warmup) X(prev) X(event) X(experimental)
enum class PublicationField{
#define PU_ENUM(n) n,
 PUBLICATION_FIELDS(PU_ENUM)
#undef PU_ENUM
 count_fields};
struct ShapePublicationLocals{
#define PU_MEMBER(n) nb::object n;
 PUBLICATION_FIELDS(PU_MEMBER)
#undef PU_MEMBER
    std::vector<PublicationField> order_of_locals;
    ShapePublicationLocals()=default;ShapePublicationLocals(ShapePublicationLocals&&)=default;
    ~ShapePublicationLocals(){clear();}
    void clear(){std::array<nb::object,static_cast<size_t>(PublicationField::count_fields)> old={
#define PU_MOVE(n) std::move(n),
 PUBLICATION_FIELDS(PU_MOVE)
#undef PU_MOVE
    };auto order=std::move(order_of_locals);order_of_locals.clear();for(auto name:order)old[static_cast<size_t>(name)].reset();for(auto& item:old)item.reset();}
};
ShapePublicationLocals shape_publication_locals(nb::tuple layout,nb::dict bindings){
    ShapePublicationLocals owner;
    for(nb::handle item:layout){std::string name=nb::cast<std::string>(item);bool found=false;
#define PU_LAYOUT(n) if(name==#n){owner.order_of_locals.push_back(PublicationField::n);found=true;}
 PUBLICATION_FIELDS(PU_LAYOUT)
#undef PU_LAYOUT
        if(!found){PyErr_Format(PyExc_ValueError,"unknown original shape publication local %s",name.c_str());throw nb::python_error();}}
    for(auto binding:bindings){std::string name=nb::cast<std::string>(binding.first);bool found=false;
#define PU_BIND(n) if(name==#n){pu_assign(owner.n,nb::borrow<nb::object>(binding.second));found=true;}
 PUBLICATION_FIELDS(PU_BIND)
#undef PU_BIND
        if(!found){PyErr_Format(PyExc_ValueError,"unknown original shape publication parameter %s",name.c_str());throw nb::python_error();}}
    return owner;
}
nb::dict shape_publication_globals(){
    PyObject* globals=PyEval_GetGlobals();if(!globals){PyErr_SetString(PyExc_RuntimeError,"shape publication binding requires an executing Python frame");throw nb::python_error();}
    return nb::borrow<nb::dict>(globals);
}
int pu_owner_traverse(PyObject* self,visitproc visit,void* arg){Py_VISIT(Py_TYPE(self));if(nb::inst_ready(self)){auto& owner=*nb::inst_ptr<ShapePublicationLocals>(self);
#define PU_VISIT(n) Py_VISIT(owner.n.ptr());
 PUBLICATION_FIELDS(PU_VISIT)
#undef PU_VISIT
    }return 0;}
int pu_owner_clear(PyObject* self){if(nb::inst_ready(self))nb::inst_ptr<ShapePublicationLocals>(self)->clear();return 0;}
PyType_Slot pu_owner_slots[]={{Py_tp_traverse,reinterpret_cast<void*>(pu_owner_traverse)},{Py_tp_clear,reinterpret_cast<void*>(pu_owner_clear)},{0,nullptr}};
#define PX(expr) [&]()->nb::object{return (expr);}
#define PC(i) pu_literal(literals,i)
#define PK(i,j) pu_item(PC(i),nb::int_(j))
#define PUT(n,expr) pu_assign(o.n,(expr))
#define PU_DEF(n) nb::object n(nb::dict globals,nb::tuple literals,ShapePublicationLocals& o)
#define PU_BEGIN PublicationGlobals ns(std::move(globals))

void pu_validate_except(nb::handle klass){
    if(PyTuple_Check(klass.ptr())){for(Py_ssize_t i=0;i<PyTuple_GET_SIZE(klass.ptr());++i)if(!PyExceptionClass_Check(PyTuple_GET_ITEM(klass.ptr(),i))){PyErr_SetString(PyExc_TypeError,"catching classes that do not inherit from BaseException is not allowed");throw nb::python_error();}}
    else if(!PyExceptionClass_Check(klass.ptr())){PyErr_SetString(PyExc_TypeError,"catching classes that do not inherit from BaseException is not allowed");throw nb::python_error();}
}
nb::object shape_publication_initialize(nb::dict globals,nb::tuple literals){
    PublicationGlobals ns(std::move(globals));
    try{nb::object module=ns.import_module(pu_import_name(PC(0)),PC(1),PC(2));nb::object imported=pu_import_from(module,PK(1,0));ns.publish("_accelerated_triangulate_numba",std::move(imported));module.reset();}
    catch(nb::python_error& caught){caught.restore();if(PyTraceBack_Here(PyEval_GetFrame())<0)throw nb::python_error();nb::python_error original;collections::HandledException handled(original.value());
        nb::object klass=ns.get("ImportError");pu_validate_except(klass);bool matched=PyErr_GivenExceptionMatches(original.type().ptr(),klass.ptr())!=0;klass.reset();if(!matched)throw original;ns.publish("_accelerated_triangulate_numba",nb::none());}
    ns.publish("USE_NUMBA_FOR_EDGE_TRIANGULATION",pu_bool(!ns.get("_accelerated_triangulate_numba").is_none()));
    ns.publish("RUN_WARMUP",pu_bool(!ns.get("_accelerated_triangulate_numba").is_none()));
    ns.publish("CACHE_WARMUP",PC(3));ns.publish("UNIVERSAL_CACHE_WARMUP",PC(3));
    ns.publish("normalize_vertices_and_edges",pu_attr(ns.get("_accelerated_triangulate_python"),"normalize_vertices_and_edges_py"));
    ns.publish("ALWAYS_NUMBA",PC(4));ns.publish("SWAPPABLE_NUMBA",PC(5));
    if(!ns.get("_accelerated_triangulate_numba").is_none()){
        ns.publish("remove_path_duplicates",pu_attr(ns.get("_accelerated_triangulate_numba"),"remove_path_duplicates"));
        ns.publish("create_box_from_bounding",pu_attr(ns.get("_accelerated_triangulate_numba"),"create_box_from_bounding"));
        ns.publish("generate_2D_edge_meshes",pu_attr(ns.get("_accelerated_triangulate_numba"),"generate_2D_edge_meshes"));
        ns.publish("is_convex",pu_attr(ns.get("_accelerated_triangulate_numba"),"is_convex"));
        ns.publish("normalize_vertices_and_edges",pu_attr(ns.get("_accelerated_triangulate_numba"),"normalize_vertices_and_edges"));
        ns.publish("reconstruct_polygons_from_edges",pu_attr(ns.get("_accelerated_triangulate_numba"),"reconstruct_polygons_from_edges"));
    }else{
        ns.publish("remove_path_duplicates",pu_attr(ns.get("_accelerated_triangulate_python"),"remove_path_duplicates_py"));
        ns.publish("create_box_from_bounding",pu_attr(ns.get("_accelerated_triangulate_python"),"create_box_from_bounding_py"));
        ns.publish("generate_2D_edge_meshes",pu_attr(ns.get("_accelerated_triangulate_python"),"generate_2D_edge_meshes_py"));
        ns.publish("is_convex",pu_attr(ns.get("_accelerated_triangulate_python"),"is_convex_py"));
        ns.publish("reconstruct_polygons_from_edges",pu_attr(ns.get("_accelerated_triangulate_python"),"reconstruct_polygons_from_edges_py"));}
    return nb::none();
}
PU_DEF(shape_publication_set_numba){PU_BEGIN;
    nb::object selected=o.value;if(pu_truth(selected))selected=pu_bool(!ns.get("_accelerated_triangulate_numba").is_none());PUT(val,std::move(selected));
    bool enabled=pu_truth(o.val);nb::object iterator=pu_iter(ns.get("SWAPPABLE_NUMBA"));
    while(true){nb::object name=pu_next(iterator);if(!name.is_valid())break;PUT(name,std::move(name));nb::object value;
        if(enabled)value=pu_call(ns.get("getattr"),{PX(ns.get("_accelerated_triangulate_numba")),PX(o.name)});
        else value=pu_call(ns.get("getattr"),{PX(ns.get("_accelerated_triangulate_python")),PX(pu_string<2>({pu_format(o.name),PC(2)}))});
        nb::object mapping=pu_call(ns.get("globals"));if(PyObject_SetItem(mapping.ptr(),o.name.ptr(),value.ptr())<0)throw nb::python_error();}
    iterator.reset();ns.publish("USE_NUMBA_FOR_EDGE_TRIANGULATION",o.val);return nb::none();
}
PU_DEF(shape_publication_set_warmup){PU_BEGIN;ns.publish("RUN_WARMUP",o.value);return nb::none();}
nb::object pu_warmup_array(const PublicationGlobals& ns,const nb::tuple& literals,ShapePublicationLocals& o,bool second){
    nb::object fn=ns.np("array");nb::list data;
    if(second)data=pu_list({PX(pu_list({PX(PC(5)),PX(PC(5))})),PX(pu_list({PX(PC(7)),PX(PC(8))}))});
    else data=pu_list({PX(pu_list({PX(PC(4)),PX(PC(4))})),PX(pu_list({PX(PC(5)),PX(PC(5))})),PX(pu_list({PX(PC(4)),PX(PC(5))})),PX(pu_list({PX(PC(5)),PX(PC(4))}))});
    return pu_call(std::move(fn),{PX(data)},{{PK(6,0),PX(pu_attr(ns.get("np"),"float32"))},{PK(6,1),PX(o.order)}});
}
PU_DEF(shape_publication_warmup_universal){PU_BEGIN;
    if(ns.get("_accelerated_triangulate_numba").is_none())return nb::none();if(pu_truth(ns.get("UNIVERSAL_CACHE_WARMUP")))return nb::none();
    ns.publish("UNIVERSAL_CACHE_WARMUP",PC(2));nb::object iterator=pu_iter(PC(3));
    while(true){nb::object order=pu_next(iterator);if(!order.is_valid())break;PUT(order,std::move(order));PUT(data,pu_warmup_array(ns,literals,o,false));PUT(data2,pu_warmup_array(ns,literals,o,true));
        pu_call(pu_attr(ns.get("_accelerated_triangulate_numba"),"remove_path_duplicates"),{PX(o.data),PX(PC(9))});
        pu_call(pu_attr(ns.get("_accelerated_triangulate_numba"),"remove_path_duplicates"),{PX(o.data),PX(PC(2))});
        pu_call(pu_attr(ns.get("_accelerated_triangulate_numba"),"create_box_from_bounding"),{PX(o.data2)});}
    return nb::none();
}
std::array<nb::object,2> pu_pair(nb::object value){
    PyObject* raw=PyObject_GetIter(value.ptr());if(!raw){nb::python_error original;if(Py_TYPE(value.ptr())->tp_iter==nullptr&&!PySequence_Check(value.ptr())&&original.matches(PyExc_TypeError)){PyErr_Format(PyExc_TypeError,"cannot unpack non-iterable %.200s object",Py_TYPE(value.ptr())->tp_name);throw nb::python_error();}throw original;}
    nb::object iterator=nb::steal<nb::object>(raw);nb::object first=pu_next(iterator);if(!first.is_valid()){PyErr_SetString(PyExc_ValueError,"not enough values to unpack (expected 2, got 0)");throw nb::python_error();}
    nb::object second=pu_next(iterator);if(!second.is_valid()){PyErr_SetString(PyExc_ValueError,"not enough values to unpack (expected 2, got 1)");throw nb::python_error();}
    nb::object extra=pu_next(iterator);if(extra.is_valid()){PyErr_SetString(PyExc_ValueError,"too many values to unpack (expected 2)");throw nb::python_error();}
    iterator.reset();value.reset();return {std::move(first),std::move(second)};
}
PU_DEF(shape_publication_warmup_cache){PU_BEGIN;
    if(ns.get("_accelerated_triangulate_numba").is_none())return nb::none();if(!pu_truth(ns.get("RUN_WARMUP"))){pu_call(ns.get("warmup_universal_numba"));return nb::none();}
    if(pu_truth(ns.get("CACHE_WARMUP")))return nb::none();ns.publish("CACHE_WARMUP",PC(1));pu_call(ns.get("warmup_universal_numba"));nb::object iterator=pu_iter(PC(2));
    while(true){nb::object order=pu_next(iterator);if(!order.is_valid())break;PUT(order,std::move(order));
        nb::object fn=ns.np("array");nb::list data=pu_list({PX(pu_list({PX(PC(3)),PX(PC(3))})),PX(pu_list({PX(PC(4)),PX(PC(4))})),PX(pu_list({PX(PC(3)),PX(PC(4))})),PX(pu_list({PX(PC(4)),PX(PC(3))}))});
        nb::object array=pu_call(std::move(fn),{PX(data)},{{PK(5,0),PX(pu_attr(ns.get("np"),"float32"))},{PK(5,1),PX(o.order)}});data.reset();PUT(data,std::move(array));
        if(!pu_truth(ns.get("USE_NUMBA_FOR_EDGE_TRIANGULATION"))){
            pu_call(pu_attr(ns.get("_accelerated_triangulate_numba"),"generate_2D_edge_meshes"),{PX(o.data),PX(PC(1))});
            pu_call(pu_attr(ns.get("_accelerated_triangulate_numba"),"generate_2D_edge_meshes"),{PX(o.data),PX(PC(6))});
            pu_call(pu_attr(ns.get("_accelerated_triangulate_numba"),"remove_path_duplicates"),{PX(o.data),PX(PC(6))});
            pu_call(pu_attr(ns.get("_accelerated_triangulate_numba"),"is_convex"),{PX(o.data)});
            auto parts=pu_pair(pu_call(pu_attr(ns.get("_accelerated_triangulate_numba"),"normalize_vertices_and_edges"),{PX(o.data)}));PUT(v,std::move(parts[0]));PUT(e,std::move(parts[1]));
            pu_call(pu_attr(ns.get("_accelerated_triangulate_numba"),"reconstruct_polygons_from_edges"),{PX(o.v),PX(o.e)});}}
    return nb::none();
}
PU_DEF(shape_publication_argsort){PU_BEGIN;
    nb::object sorted=ns.get("sorted");nb::object range=pu_call(ns.get("range"),{PX(pu_call(ns.get("len"),{PX(o.values)}))});
    return pu_call(std::move(sorted),{PX(range)},{{PK(1,0),PX(pu_attr(o.values,"__getitem__"))}});
}
PU_DEF(shape_publication_backend_string){PU_BEGIN;return pu_call(ns.get("str"),{PX(pu_attr(o.self,"value"))});}
PU_DEF(shape_publication_backend_repr){PU_BEGIN;return pu_attr(o.self,"name");}
PU_DEF(shape_publication_backend_missing){PU_BEGIN;
    if(pu_truth(pu_call(ns.get("isinstance"),{PX(o.value),PX(ns.get("str"))}))){nb::object cls=o.cls;nb::object key=pu_call(pu_attr(pu_call(pu_attr(o.value,"replace"),{PX(PC(1)),PX(PC(2))}),"lower"));return pu_item(std::move(cls),std::move(key));}
    nb::object factory=ns.get("ValueError");nb::object formatted=pu_format(o.value);pu_raise(pu_call(std::move(factory),{PX(pu_string<3>({PC(3),std::move(formatted),PC(4)}))}));
}
PU_DEF(shape_publication_get_backend){PU_BEGIN;
    nb::object module=ns.import_module(pu_import_name(PC(3)),PC(2),PC(1));PUT(shape,pu_import_from(module,PK(2,0)));module.reset();return pu_attr(o.shape,"TRIANGULATION_BACKEND");
}
nb::object pu_backend_term(const PublicationGlobals& ns,ShapePublicationLocals& o,const char* member,const nb::object& flag){
    nb::object comparison=pu_compare(PX(o.backend),PX(pu_attr(ns.get("TriangulationBackend"),member)),Py_EQ);
    if(!pu_truth(comparison))return comparison;comparison.reset();return pu_bool(!pu_truth(flag));
}
PU_DEF(shape_publication_set_backend){PU_BEGIN;
    nb::object module=ns.import_module(pu_import_name(PC(6)),PC(2),PC(1));PUT(_set_numba,pu_import_from(module,PK(2,0)));PUT(_set_warmup,pu_import_from(module,PK(2,1)));module.reset();
    module=ns.import_module(pu_import_name(PC(7)),PC(3),PC(1));PUT(shape,pu_import_from(module,PK(3,0)));module.reset();
    {nb::object item=PC(4);nb::object container=pu_attr(ns.get("sys"),"modules");int found=PySequence_Contains(container.ptr(),item.ptr());if(found<0)throw nb::python_error();PUT(bermuda_loaded,pu_bool(found!=0));}
    {nb::object item=PC(5);nb::object container=pu_attr(ns.get("sys"),"modules");int found=PySequence_Contains(container.ptr(),item.ptr());if(found<0)throw nb::python_error();PUT(partsegcore_loaded,pu_bool(found!=0));}
    nb::object any=o.bermuda_loaded;if(!pu_truth(any))any=o.partsegcore_loaded;PUT(any_compiled_loaded,std::move(any));
    nb::object need=pu_compare(PX(o.backend),PX(pu_attr(ns.get("TriangulationBackend"),"numba")),Py_EQ);
    if(!pu_truth(need)){need.reset();need=pu_backend_term(ns,o,"fastest_available",o.any_compiled_loaded);
        if(!pu_truth(need)){need.reset();need=pu_backend_term(ns,o,"bermuda",o.bermuda_loaded);
            if(!pu_truth(need)){need.reset();need=pu_backend_term(ns,o,"partsegcore",o.partsegcore_loaded);}}}
    PUT(need_numba_warmup,std::move(need));pu_call(o._set_numba,{PX(pu_compare(PX(o.backend),PX(pu_attr(ns.get("TriangulationBackend"),"pure_python")),Py_NE))});
    pu_call(o._set_warmup,{PX(o.need_numba_warmup)});PUT(prev,pu_attr(o.shape,"TRIANGULATION_BACKEND"));
    if(PyObject_SetAttrString(o.shape.ptr(),"TRIANGULATION_BACKEND",o.backend.ptr())<0)throw nb::python_error();return o.prev;
}
PU_DEF(shape_publication_connect_events){PU_BEGIN;
    pu_call(pu_attr(pu_attr(pu_attr(o.self,"events"),"triangulation_backend"),"connect"),{PX(ns.get("_update_triangulation_backend"))});
    pu_call(pu_attr(pu_attr(o.self,"events"),"triangulation_backend"),{},{{PK(1,0),PX(pu_attr(o.self,"triangulation_backend"))}});
    pu_call(pu_attr(pu_attr(pu_attr(o.self,"events"),"colormap_backend"),"connect"),{PX(ns.get("_update_colormap_backend"))});
    pu_call(pu_attr(pu_attr(o.self,"events"),"colormap_backend"),{},{{PK(1,0),PX(pu_attr(o.self,"colormap_backend"))}});return nb::none();
}
PU_DEF(shape_publication_update_backend){PU_BEGIN;PUT(experimental,pu_attr(o.event,"source"));pu_call(ns.get("set_triangulation_backend"),{PX(pu_attr(o.experimental,"triangulation_backend"))});return nb::none();}
} // namespace
void bind_shape_publication(nb::module_& m){
    nb::class_<ShapePublicationLocals> owner(m,"ShapePublicationLocals",nb::is_weak_referenceable(),nb::type_slots(pu_owner_slots));
#define PU_PROP(n) owner.def_prop_ro(#n,[](const ShapePublicationLocals& o){return o.n.is_valid()?o.n:nb::none();});
 PUBLICATION_FIELDS(PU_PROP)
#undef PU_PROP
    m.def("shape_publication_locals",&shape_publication_locals,nb::arg("layout"),nb::arg("bindings"));
    m.def("shape_publication_globals",&shape_publication_globals);
    m.def("shape_publication_initialize",&shape_publication_initialize,nb::arg("namespace"),nb::arg("literals"));
#define PU_BIND(n) m.def(#n,&n,nb::arg("namespace"),nb::arg("literals"),nb::arg("owner"));
    PU_BIND(shape_publication_set_numba)
    PU_BIND(shape_publication_set_warmup)
    PU_BIND(shape_publication_warmup_universal)
    PU_BIND(shape_publication_warmup_cache)
    PU_BIND(shape_publication_argsort)
    PU_BIND(shape_publication_backend_string)
    PU_BIND(shape_publication_backend_repr)
    PU_BIND(shape_publication_backend_missing)
    PU_BIND(shape_publication_get_backend)
    PU_BIND(shape_publication_set_backend)
    PU_BIND(shape_publication_connect_events)
    PU_BIND(shape_publication_update_backend)
#undef PU_BIND
}
} // namespace nx
