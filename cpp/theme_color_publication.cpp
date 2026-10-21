#include "arrays.hpp"
#if PY_VERSION_HEX >= 0x030C0000 && PY_VERSION_HEX < 0x030D0000
#include <internal/pycore_frame.h>
#endif
#include <structmember.h>
#include <array>
#include <functional>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace nx {
namespace {
nb::object tc_check(PyObject* value){if(!value)throw nb::python_error();return nb::steal<nb::object>(value);}
nb::object tc_attr(nb::object value,const char* name){return tc_check(PyObject_GetAttrString(value.ptr(),name));}
void tc_assign(nb::object& target,nb::object value){nb::object previous=std::move(target);target=std::move(value);previous.reset();}
using TCExpr=std::function<nb::object()>;
nb::object tc_call(nb::object function,std::initializer_list<TCExpr> positional={},std::initializer_list<TCExpr> keywords={},nb::object keyword_names={}){
    std::vector<nb::object> values;values.reserve(positional.size()+keywords.size());for(const auto& expression:positional)values.push_back(expression());for(const auto& expression:keywords)values.push_back(expression());
    if(keywords.size()&&(!keyword_names.is_valid()||!PyTuple_CheckExact(keyword_names.ptr())||PyTuple_GET_SIZE(keyword_names.ptr())!=static_cast<Py_ssize_t>(keywords.size())))throw nb::type_error("source keyword-name metadata must be the matching tuple");
    std::vector<PyObject*> operands;operands.reserve(values.size());for(const auto& value:values)operands.push_back(value.ptr());
    PyObject* result=PyObject_Vectorcall(function.ptr(),operands.data(),positional.size(),keyword_names.ptr());function.reset();for(auto& value:values)value.reset();keyword_names.reset();return tc_check(result);
}
nb::object tc_expanded_call(nb::object function,nb::handle kwargs){
    // Genuine source CALL_FUNCTION_EX: resolve callable first, then build a
    // fresh exact keyword expansion. Caller kwargs remains the same object.
    nb::object arguments=tc_check(PyTuple_New(0));
    nb::object expanded=tc_check(PyDict_Copy(kwargs.ptr()));
    PyObject* result=PyObject_Call(function.ptr(),arguments.ptr(),expanded.ptr());
    function.reset();arguments.reset();expanded.reset();return tc_check(result);
}
void tc_publish(TCExpr value,TCExpr receiver,const char* name){nb::object result=value(),target=receiver();int status=PyObject_SetAttrString(target.ptr(),name,result.ptr());result.reset();target.reset();if(status<0)throw nb::python_error();}
nb::tuple tc_tuple(std::initializer_list<TCExpr> expressions){
    std::vector<nb::object> values;values.reserve(expressions.size());for(const auto& expression:expressions)values.push_back(expression());
    nb::tuple result=nb::steal<nb::tuple>(PyTuple_New(static_cast<Py_ssize_t>(values.size())));if(!result.is_valid())throw nb::python_error();
    for(size_t index=0;index<values.size();++index)PyTuple_SET_ITEM(result.ptr(),static_cast<Py_ssize_t>(index),values[index].release().ptr());return result;
}
nb::object tc_import_frame_locals(){
#if PY_VERSION_HEX >= 0x030C0000 && PY_VERSION_HEX < 0x030D0000
    PyFrameObject* frame=PyEval_GetFrame();if(!frame){if(PyErr_Occurred())throw nb::python_error();return nb::none();}
    PyObject* locals=frame->f_frame->f_locals;return nb::borrow<nb::object>(locals?locals:Py_None);
#else
    PyErr_SetString(PyExc_RuntimeError,"Theme/color IMPORT_NAME existing-locals handoff requires its reviewed CPython 3.12 profile; other required interpreter profiles remain open");throw nb::python_error();
#endif
}
struct ThemeColorGlobals{
    nb::dict values;nb::object builtins;
    explicit ThemeColorGlobals(nb::dict source):values(std::move(source)),builtins(nb::borrow<nb::object>(PyEval_GetBuiltins())){}
    nb::object builtin(const char* name)const{nb::object key=nb::str(name);PyObject* value=PyObject_GetItem(builtins.ptr(),key.ptr());if(value)return nb::steal<nb::object>(value);if(!PyErr_ExceptionMatches(PyExc_KeyError))throw nb::python_error();PyErr_Clear();if(std::string(name)=="__import__"){PyErr_SetString(PyExc_ImportError,"__import__ not found");throw nb::python_error();}nb::object message=tc_check(PyUnicode_FromFormat("name '%s' is not defined",name));nb::object error=tc_call(nb::borrow<nb::object>(PyExc_NameError),{[&]()->nb::object{return message;}});if(PyObject_SetAttrString(error.ptr(),"name",key.ptr())<0)throw nb::python_error();PyErr_SetObject(PyExc_NameError,error.ptr());throw nb::python_error();}
    nb::object get(const char* name)const{PyObject* value=PyDict_GetItemString(values.ptr(),name);if(value)return nb::borrow<nb::object>(value);if(PyErr_Occurred())throw nb::python_error();return builtin(name);}
    void store(const char* name,nb::object value){int status=PyDict_SetItemString(values.ptr(),name,value.ptr());value.reset();if(status<0)throw nb::python_error();}
    nb::object import_module(nb::object name,nb::object fromlist,nb::object level)const{
        PyObject* raw_name=name.release().ptr();PyUnicode_InternInPlace(&raw_name);if(PyErr_Occurred()){Py_XDECREF(raw_name);throw nb::python_error();}name=nb::steal<nb::object>(raw_name);
        nb::object importer=builtin("__import__"),locals=tc_import_frame_locals();
        return tc_call(std::move(importer),{[&]()->nb::object{return name;},[&]()->nb::object{return values;},[&]()->nb::object{return locals;},[&]()->nb::object{return fromlist;},[&]()->nb::object{return level;}});
    }
};
// Reuse the accepted handwritten CPython3.12 IMPORT_FROM protocol. In
// particular, an AttributeError must use the genuine sys.modules fallback
// and source ImportError metadata, rather than becoming plain getattr.
bool tc_spec_initializing(nb::handle spec){
    if(spec.is_valid()){nb::object value=nb::steal<nb::object>(PyObject_GetAttrString(spec.ptr(),"_initializing"));if(!value.is_valid()&&PyErr_ExceptionMatches(PyExc_AttributeError)){PyErr_Clear();return false;}if(value.is_valid()){int initializing=PyObject_IsTrue(value.ptr());value.reset();if(initializing>=0)return initializing!=0;}}
    PyErr_Clear();return false;
}
[[noreturn]] void tc_raise_import(nb::object message,nb::handle name,nb::handle path,nb::handle requested){
    if(!message.is_valid()){PyErr_SetString(PyExc_TypeError,"expected a message argument");throw nb::python_error();}
    nb::dict kwargs;kwargs["name"]=name.is_valid()?nb::borrow<nb::object>(name):nb::none();kwargs["path"]=path.is_valid()?nb::borrow<nb::object>(path):nb::none();
#if PY_VERSION_HEX >= 0x030C0000
    kwargs["name_from"]=requested;
#endif
    nb::object arguments=tc_check(PyTuple_New(1));PyTuple_SET_ITEM(arguments.ptr(),0,message.inc_ref().ptr());
    nb::object error=tc_check(PyObject_Call(PyExc_ImportError,arguments.ptr(),kwargs.ptr()));
    PyErr_SetObject(reinterpret_cast<PyObject*>(Py_TYPE(error.ptr())),error.ptr());throw nb::python_error();
}
nb::object tc_import_from(nb::handle module,nb::handle requested){
#if PY_VERSION_HEX >= 0x030C0000 && PY_VERSION_HEX < 0x030D0000
    PyObject* result=PyObject_GetAttr(module.ptr(),requested.ptr());if(result)return nb::steal<nb::object>(result);if(!PyErr_ExceptionMatches(PyExc_AttributeError))throw nb::python_error();PyErr_Clear();
    nb::object pkgname=nb::steal<nb::object>(PyObject_GetAttrString(module.ptr(),"__name__"));if(pkgname.is_valid()&&!PyUnicode_Check(pkgname.ptr()))pkgname.reset();
    if(pkgname.is_valid()){nb::object fullname=tc_check(PyUnicode_FromFormat("%U.%U",pkgname.ptr(),requested.ptr()));result=PyImport_GetModule(fullname.ptr());fullname.reset();if(result)return nb::steal<nb::object>(result);if(PyErr_Occurred())throw nb::python_error();}
    nb::object pkgpath=nb::steal<nb::object>(PyModule_GetFilenameObject(module.ptr()));PyObject* raw_pkgname=pkgname.ptr();
    nb::object name_or_unknown=pkgname.is_valid()?std::move(pkgname):nb::str("<unknown module name>");nb::object message;nb::handle error_path;
    if(!pkgpath.is_valid()||!PyUnicode_Check(pkgpath.ptr())){PyErr_Clear();message=nb::steal<nb::object>(PyUnicode_FromFormat("cannot import name %R from %R (unknown location)",requested.ptr(),name_or_unknown.ptr()));}
    else{nb::object spec=nb::steal<nb::object>(PyObject_GetAttrString(module.ptr(),"__spec__"));bool initializing=tc_spec_initializing(spec);spec.reset();const char* format=initializing?"cannot import name %R from partially initialized module %R (most likely due to a circular import) (%S)":"cannot import name %R from %R (%S)";message=nb::steal<nb::object>(PyUnicode_FromFormat(format,requested.ptr(),name_or_unknown.ptr(),pkgpath.ptr()));error_path=pkgpath;}
    try{tc_raise_import(std::move(message),nb::handle(raw_pkgname),error_path,requested);}catch(...){message.reset();name_or_unknown.reset();pkgpath.reset();throw;}
#else
    PyErr_SetString(PyExc_RuntimeError,"Theme/color IMPORT_FROM requires its reviewed CPython 3.12 profile; other required interpreter profiles remain open");throw nb::python_error();
#endif
}

bool tc_truth(nb::object value){int result=PyObject_IsTrue(value.ptr());value.reset();if(result<0)throw nb::python_error();return result!=0;}
nb::object tc_compare(TCExpr left,TCExpr right,int operation){nb::object a=left(),b=right();PyObject* result=PyObject_RichCompare(a.ptr(),b.ptr(),operation);a.reset();b.reset();return tc_check(result);}
nb::object tc_binary(TCExpr left,TCExpr right,PyObject*(*operation)(PyObject*,PyObject*)){nb::object a=left(),b=right();PyObject* result=operation(a.ptr(),b.ptr());a.reset();b.reset();return tc_check(result);}
nb::object tc_item(nb::object value,nb::object index){PyObject* result=PyObject_GetItem(value.ptr(),index.ptr());value.reset();index.reset();return tc_check(result);}
nb::object tc_index(TCExpr value,TCExpr index){nb::object target=value(),key=index();return tc_item(std::move(target),std::move(key));}
void tc_set_item(TCExpr value,TCExpr receiver,TCExpr index){nb::object result=value(),target=receiver(),key=index();int status=PyObject_SetItem(target.ptr(),key.ptr(),result.ptr());result.reset();target.reset();key.reset();if(status<0)throw nb::python_error();}
nb::object tc_slice(nb::object first,nb::object last){PyObject* result=PySlice_New(first.ptr(),last.ptr(),Py_None);first.reset();last.reset();return tc_check(result);}
nb::object tc_iter(nb::object value){PyObject* result=PyObject_GetIter(value.ptr());value.reset();return tc_check(result);}
nb::object tc_next(nb::handle value){PyObject* result=PyIter_Next(value.ptr());if(!result&&PyErr_Occurred())throw nb::python_error();return nb::steal<nb::object>(result);}
nb::object tc_list(std::initializer_list<TCExpr> expressions){std::vector<nb::object> values;values.reserve(expressions.size());for(const auto& expression:expressions)values.push_back(expression());nb::object result=tc_check(PyList_New(static_cast<Py_ssize_t>(values.size())));for(size_t index=0;index<values.size();++index)PyList_SET_ITEM(result.ptr(),static_cast<Py_ssize_t>(index),values[index].release().ptr());return result;}
std::array<nb::object,2> tc_unpack_pair(nb::object value){PyObject* raw=PyObject_GetIter(value.ptr());if(!raw){nb::python_error error;if(error.matches(PyExc_TypeError)&&Py_TYPE(value.ptr())->tp_iter==nullptr&&!PySequence_Check(value.ptr())){PyErr_Format(PyExc_TypeError,"cannot unpack non-iterable %.200s object",Py_TYPE(value.ptr())->tp_name);throw nb::python_error();}throw error;}nb::object iterator=nb::steal<nb::object>(raw);std::array<nb::object,2> result;for(size_t i=0;i<2;++i){result[i]=tc_next(iterator);if(!result[i].is_valid()){PyErr_Format(PyExc_ValueError,"not enough values to unpack (expected 2, got %zu)",i);throw nb::python_error();}}nb::object extra=tc_next(iterator);if(extra.is_valid()){PyErr_SetString(PyExc_ValueError,"too many values to unpack (expected 2)");throw nb::python_error();}iterator.reset();value.reset();return result;}
nb::object tc_format(nb::object value){PyObject* result=PyObject_Format(value.ptr(),nullptr);value.reset();return tc_check(result);}
nb::object tc_join(std::initializer_list<TCExpr> pieces){nb::tuple parts=tc_tuple(pieces);nb::object separator=nb::str("");PyObject* result=PyUnicode_Join(separator.ptr(),parts.ptr());parts.reset();separator.reset();return tc_check(result);}
[[noreturn]]void tc_raise(nb::object value){if(PyExceptionClass_Check(value.ptr())){nb::object cls=std::move(value);value=tc_call(cls);if(!PyExceptionInstance_Check(value.ptr())){PyErr_Format(PyExc_TypeError,"calling %R should have returned an instance of BaseException, not %R",cls.ptr(),reinterpret_cast<PyObject*>(Py_TYPE(value.ptr())));throw nb::python_error();}}if(!PyExceptionInstance_Check(value.ptr())){PyErr_SetString(PyExc_TypeError,"exceptions must derive from BaseException");throw nb::python_error();}PyErr_SetObject(reinterpret_cast<PyObject*>(Py_TYPE(value.ptr())),value.ptr());throw nb::python_error();}

[[noreturn]]void tc_raise_cause(nb::object value,nb::handle cause){
    // Source RAISE_VARARGS2 first normalizes the error class/instance, then
    // attaches its genuine already-caught exception cause.
    if(PyExceptionClass_Check(value.ptr())){nb::object cls=std::move(value);value=tc_call(cls);if(!PyExceptionInstance_Check(value.ptr())){PyErr_Format(PyExc_TypeError,"calling %R should have returned an instance of BaseException, not %R",cls.ptr(),reinterpret_cast<PyObject*>(Py_TYPE(value.ptr())));throw nb::python_error();}}
    if(!PyExceptionInstance_Check(value.ptr())){PyErr_SetString(PyExc_TypeError,"exceptions must derive from BaseException");throw nb::python_error();}
    PyException_SetCause(value.ptr(),cause.inc_ref().ptr());tc_raise(std::move(value));
}
struct TCHandledException{PyObject* previous;explicit TCHandledException(nb::handle value):previous(PyErr_GetHandledException()){PyErr_SetHandledException(value.ptr());}~TCHandledException(){PyErr_SetHandledException(previous);Py_XDECREF(previous);}};
struct ThemeColorLocals{
    nb::object self,cls,value,color,colors,theme_id,theme,changed,QT6,Qt,QGuiApplication,style_hints,scheme;
    nb::object colortype,typ,handler,colorarray,num_str,dtype,color_array,kind,idx,c,e,out_of_bounds_idx,out_of_bounds,norm,iterator;
    ThemeColorLocals()=default;ThemeColorLocals(ThemeColorLocals&&)=default;
    void clear(){std::array<nb::object,28> values={std::move(self),std::move(cls),std::move(value),std::move(color),std::move(colors),std::move(theme_id),std::move(theme),std::move(changed),std::move(QT6),std::move(Qt),std::move(QGuiApplication),std::move(style_hints),std::move(scheme),std::move(colortype),std::move(typ),std::move(handler),std::move(colorarray),std::move(num_str),std::move(dtype),std::move(color_array),std::move(kind),std::move(idx),std::move(c),std::move(e),std::move(out_of_bounds_idx),std::move(out_of_bounds),std::move(norm),std::move(iterator)};for(auto& value:values)value.reset();}
    ~ThemeColorLocals(){clear();}
};
int tc_owner_traverse(PyObject* self,visitproc visit,void* arg){Py_VISIT(Py_TYPE(self));if(nb::inst_ready(self)){auto& o=*nb::inst_ptr<ThemeColorLocals>(self);
#define TC_VISIT(name) Py_VISIT(o.name.ptr());
TC_VISIT(self) TC_VISIT(cls) TC_VISIT(value) TC_VISIT(color) TC_VISIT(colors) TC_VISIT(theme_id) TC_VISIT(theme) TC_VISIT(changed) TC_VISIT(QT6) TC_VISIT(Qt) TC_VISIT(QGuiApplication) TC_VISIT(style_hints) TC_VISIT(scheme) TC_VISIT(colortype) TC_VISIT(typ) TC_VISIT(handler) TC_VISIT(colorarray) TC_VISIT(num_str) TC_VISIT(dtype) TC_VISIT(color_array) TC_VISIT(kind) TC_VISIT(idx) TC_VISIT(c) TC_VISIT(e) TC_VISIT(out_of_bounds_idx) TC_VISIT(out_of_bounds) TC_VISIT(norm) TC_VISIT(iterator)
#undef TC_VISIT
}return 0;}
int tc_owner_clear(PyObject* self){if(nb::inst_ready(self))nb::inst_ptr<ThemeColorLocals>(self)->clear();return 0;}
PyType_Slot tc_owner_slots[]={{Py_tp_traverse,reinterpret_cast<void*>(tc_owner_traverse)},{Py_tp_clear,reinterpret_cast<void*>(tc_owner_clear)},{0,nullptr}};
#define TX(value) [&]() -> nb::object { return (value); }
nb::object tc_literal(nb::handle metadata,Py_ssize_t index){return nb::borrow<nb::object>(PyTuple_GET_ITEM(metadata.ptr(),index));}
// Preserve the older actual list-extend primitive, and use the public
// Python 3.13 API with the same None/new-reference or error result protocol.
PyObject* theme_list_extend(PyListObject* list, PyObject* iterable) {
#if PY_VERSION_HEX >= 0x030D0000
    if (PyList_Extend(reinterpret_cast<PyObject*>(list),iterable)<0) return nullptr;
    return Py_NewRef(Py_None);
#else
    return _PyList_Extend(list,iterable);
#endif
}

struct TCPolicy{
    ThemeColorGlobals ns;ThemeColorLocals& o;nb::tuple metadata;
    TCPolicy(nb::dict source,ThemeColorLocals& owner,nb::tuple constants):ns(std::move(source)),o(owner),metadata(std::move(constants)){}
    nb::object K(Py_ssize_t index)const{return tc_literal(metadata,index);}
    nb::object np(const char* name){return tc_attr(ns.get("np"),name);}
    nb::object dims(nb::object value,Py_ssize_t index){return tc_item(tc_attr(std::move(value),"shape"),nb::int_(index));}
    void warn(nb::object message){tc_call(tc_attr(ns.get("warnings"),"warn"),{TX(message)});}
    nb::object white(TCExpr rows,Py_ssize_t dtype_index,Py_ssize_t keyword_index){return tc_call(np("ones"),{TX(tc_tuple({rows,TX(nb::int_(4))}))},{TX(np("float32"))},K(keyword_index));}
    nb::object import_member(Py_ssize_t name_index,const char* field){nb::object declaration=K(name_index);nb::object module=ns.import_module(tc_item(declaration,nb::int_(0)),tc_item(declaration,nb::int_(1)),tc_item(declaration,nb::int_(2)));nb::object requested=tc_item(tc_item(declaration,nb::int_(1)),nb::int_(0));nb::object value=tc_import_from(module,requested);module.reset();requested.reset();return value;}
    nb::object canvas_background(){
        if(!tc_attr(o.self,"background_color_override").is_none())return tc_call(tc_attr(tc_attr(o.self,"background_color_override"),"copy"));
        tc_assign(o.theme,tc_attr(o.self,"_viewer_theme").is_none()?tc_attr(tc_attr(tc_call(ns.get("get_settings")),"appearance"),"theme"):tc_attr(o.self,"_viewer_theme"));
        return tc_call(ns.get("ColorValue"),{TX(tc_binary(TX(tc_call(np("array"),{TX(tc_call(tc_attr(tc_attr(tc_call(ns.get("get_theme"),{TX(o.theme)}),"canvas"),"as_rgb_tuple")))})),TX(K(0)),PyNumber_TrueDivide))});
    }
    nb::object canvas_update(){tc_assign(o.changed,tc_compare(TX(tc_attr(o.theme,"value")),TX(tc_attr(o.self,"_viewer_theme")),Py_NE));tc_publish(TX(tc_attr(o.theme,"value")),TX(o.self),"_viewer_theme");if(tc_truth(o.changed)&&tc_attr(o.self,"background_color_override").is_none())tc_call(tc_attr(tc_attr(o.self,"events"),"background_color"));return nb::none();}
    nb::object vispy_background(){tc_publish(TX(tc_attr(tc_attr(tc_attr(o.self,"viewer"),"canvas"),"background_color")),TX(tc_attr(o.self,"_scene_canvas")),"bgcolor");return nb::none();}
    nb::object system_theme(){
        try{tc_assign(o.QT6,import_member(0,"QT6"));tc_assign(o.Qt,import_member(1,"Qt"));tc_assign(o.QGuiApplication,import_member(2,"QGuiApplication"));}
        catch(nb::python_error& failure){nb::tuple types=tc_tuple({TX(ns.get("ImportError")),TX(ns.get("RuntimeError"))});if(!failure.matches(types))throw;TCHandledException handled(failure.value());return K(3);}
        if(!tc_truth(o.QT6)){tc_call(ns.get("warn"),{TX(K(4))},{TX(K(5))},K(6));return K(3);}
        tc_assign(o.style_hints,tc_call(tc_attr(o.QGuiApplication,"styleHints")));if(o.style_hints.is_none())return K(3);
        tc_assign(o.scheme,tc_call(tc_attr(o.style_hints,"colorScheme")));
        if(tc_truth(tc_compare(TX(o.scheme),TX(tc_attr(tc_attr(o.Qt,"ColorScheme"),"Dark")),Py_EQ)))return K(3);
        if(tc_truth(tc_compare(TX(o.scheme),TX(tc_attr(tc_attr(o.Qt,"ColorScheme"),"Light")),Py_EQ)))return K(7);
        return K(3);
    }
    nb::object get_theme(){
        if(tc_truth(tc_compare(TX(o.theme_id),TX(K(0)),Py_EQ)))tc_assign(o.theme_id,tc_call(ns.get("get_system_theme")));
        nb::object registry=ns.get("_themes");int contains=PySequence_Contains(registry.ptr(),o.theme_id.ptr());registry.reset();if(contains<0)throw nb::python_error();
        if(!contains)tc_raise(tc_call(ns.get("ValueError"),{TX(tc_join({TX(K(1)),TX(tc_format(o.theme_id)),TX(K(2)),TX(tc_format(tc_call(ns.get("available_themes"))))}))}));
        tc_assign(o.theme,tc_call(tc_attr(tc_item(ns.get("_themes"),o.theme_id),"model_copy")));return o.theme;
    }
    nb::object available(){nb::object result=tc_check(PyList_New(0)),iterable=ns.get("_themes");nb::object extended=tc_check(theme_list_extend(reinterpret_cast<PyListObject*>(result.ptr()),iterable.ptr()));iterable.reset();extended.reset();nb::object system=K(0);int status=PyList_Append(result.ptr(),system.ptr());system.reset();if(status<0)throw nb::python_error();return result;}
    nb::object color_new(){return tc_call(tc_attr(o.cls,"validate"),{TX(o.value)});}
    nb::object color_validate(){return tc_call(tc_attr(tc_item(tc_call(ns.get("transform_color"),{TX(o.value)}),K(0)),"view"),{TX(o.cls)});}
    nb::object transform(){
        tc_assign(o.colortype,tc_call(ns.get("type"),{TX(o.colors)}));nb::object iterator=tc_iter(tc_call(tc_attr(ns.get("_color_switch"),"items")));
        while(nb::object pair=tc_next(iterator)){auto parts=tc_unpack_pair(std::move(pair));tc_assign(o.typ,std::move(parts[0]));tc_assign(o.handler,std::move(parts[1]));if(tc_truth(tc_call(ns.get("issubclass"),{TX(o.colortype),TX(o.typ)}))){nb::object result=tc_call(o.handler,{TX(o.colors)});iterator.reset();return result;}}
        iterator.reset();tc_raise(tc_call(ns.get("ValueError"),{TX(tc_join({TX(K(0)),TX(tc_format(o.colortype)),TX(K(1))}))}));
    }
    nb::object string(){
        if(tc_truth(tc_compare(TX(tc_call(ns.get("len"),{TX(o.color)})),TX(K(0)),Py_EQ))){warn(K(1));return tc_call(np("zeros"),{TX(K(2))},{TX(np("float32"))},K(3));}
        tc_assign(o.colorarray,tc_call(tc_attr(tc_call(np("atleast_2d"),{TX(tc_call(ns.get("_string_to_rgb"),{TX(o.color)}))}),"astype"),{TX(np("float32"))}));
        if(tc_truth(tc_compare(TX(dims(o.colorarray,1)),TX(K(4)),Py_EQ)))tc_assign(o.colorarray,tc_call(np("column_stack"),{TX(tc_list({TX(o.colorarray),TX(tc_call(np("float32"),{TX(K(5))}))}))}));
        return o.colorarray;
    }
    nb::object list_like(){
        try{
            nb::object length=ns.get("len");
            nb::object cursor_object=nb::cast(ThemeColorLocals());auto& cursor=*nb::inst_ptr<ThemeColorLocals>(cursor_object.ptr());cursor.iterator=tc_iter(o.colors);
            nb::object result;
            try{result=tc_check(PyList_New(0));while(nb::object c=tc_next(cursor.iterator)){tc_assign(cursor.c,std::move(c));if(tc_truth(tc_call(ns.get("isinstance"),{TX(cursor.c),TX(ns.get("str"))}))){int status=PyList_Append(result.ptr(),cursor.c.ptr());if(status<0)throw nb::python_error();}}}catch(...){cursor.iterator.reset();result.reset();cursor.c.reset();cursor_object.reset();throw;}
            cursor.iterator.reset();cursor.c.reset();cursor_object.reset();nb::object count=tc_call(std::move(length),{TX(result)});result.reset();tc_assign(o.num_str,std::move(count));
            bool mixed=tc_truth(tc_compare(TX(K(0)),TX(o.num_str),Py_LT))&&tc_truth(tc_compare(TX(o.num_str),TX(tc_call(ns.get("len"),{TX(o.colors)})),Py_LT));tc_assign(o.dtype,mixed?K(1):nb::none());
            tc_assign(o.color_array,tc_call(np("atleast_2d"),{TX(tc_call(np("asarray"),{TX(o.colors)},{TX(o.dtype)},K(2)))}));
        }catch(nb::python_error& failure){if(!failure.matches(ns.get("ValueError")))throw;TCHandledException handled(failure.value());warn(K(3));return white(TX(tc_call(ns.get("max"),{TX(tc_call(ns.get("len"),{TX(o.colors)})),TX(K(4))})),5,5);}
        nb::object kind=tc_attr(tc_attr(o.color_array,"dtype"),"kind");int found=PySequence_Contains(K(6).ptr(),kind.ptr());kind.reset();if(found<0)throw nb::python_error();if(found)return tc_call(ns.get("_handle_array"),{TX(o.color_array)});
        kind=tc_attr(tc_attr(o.color_array,"dtype"),"kind");found=PySequence_Contains(K(7).ptr(),kind.ptr());kind.reset();if(found<0)throw nb::python_error();if(found)return tc_call(ns.get("_handle_str_list_like"),{TX(tc_call(tc_attr(o.color_array,"ravel")))});return nb::none();
    }
    nb::object generator(){return tc_call(ns.get("_handle_list_like"),{TX(tc_call(ns.get("list"),{TX(o.colors)}))});}
    nb::object array(){
        tc_assign(o.kind,tc_attr(tc_attr(o.colors,"dtype"),"kind"));
        if(tc_truth(tc_compare(TX(o.kind),TX(K(0)),Py_EQ))){warn(K(1));return white(TX(tc_call(ns.get("max"),{TX(tc_call(ns.get("len"),{TX(o.colors)})),TX(K(2))})),3,3);}
        if(tc_truth(tc_compare(TX(o.kind),TX(K(4)),Py_EQ))){if(tc_truth(tc_compare(TX(tc_attr(o.colors,"ndim")),TX(K(2)),Py_EQ)))return tc_call(ns.get("_handle_str_list_like"),{TX(o.colors)});warn(K(5));return white(TX(tc_call(ns.get("len"),{TX(o.colors)})),3,3);}
        if(tc_truth(tc_compare(TX(dims(o.colors,-1)),TX(K(6)),Py_EQ))){warn(K(7));return tc_call(np("ones"),{TX(K(8))},{TX(np("float32"))},K(3));}
        tc_assign(o.colors,tc_call(np("atleast_2d"),{TX(o.colors)}));
        if(tc_truth(tc_compare(TX(tc_attr(o.colors,"ndim")),TX(K(9)),Py_GT)))tc_raise(tc_call(ns.get("ValueError"),{TX(tc_join({TX(K(10)),TX(tc_format(tc_attr(o.colors,"ndim"))),TX(K(11))}))}));
        if(tc_truth(tc_compare(TX(dims(o.colors,0)),TX(K(2)),Py_EQ))){nb::object width=dims(o.colors,1);int found=PySequence_Contains(K(12).ptr(),width.ptr());width.reset();if(found<0)throw nb::python_error();if(!found)tc_raise(tc_call(ns.get("ValueError"),{TX(tc_join({TX(K(13)),TX(tc_format(o.colors)),TX(K(14))}))}));}
        nb::object width=dims(o.colors,1);bool valid=tc_truth(tc_compare(TX(K(15)),TX(width),Py_LE))&&tc_truth(tc_compare(TX(std::move(width)),TX(K(16)),Py_LE));width.reset();
        if(!valid){warn(tc_join({TX(K(17)),TX(tc_format(dims(o.colors,1))),TX(K(18))}));return white(TX(tc_call(ns.get("len"),{TX(o.colors)})),3,3);}
        int found=PySequence_Contains(K(19).ptr(),o.kind.ptr());if(found<0)throw nb::python_error();if(found)return tc_call(ns.get("_convert_array_to_correct_format"),{TX(o.colors)});
        tc_raise(tc_call(ns.get("ValueError"),{TX(tc_join({TX(K(20)),TX(tc_format(tc_attr(o.colors,"dtype"))),TX(K(21))}))}));
    }
    nb::object convert(){
        if(tc_truth(tc_compare(TX(dims(o.colors,1)),TX(K(0)),Py_EQ)))tc_assign(o.colors,tc_call(np("column_stack"),{TX(tc_list({TX(o.colors),TX(tc_call(np("ones"),{TX(tc_call(ns.get("len"),{TX(o.colors)}))},{TX(np("float32"))},K(1)))}))}));
        if(tc_truth(tc_compare(TX(tc_call(tc_attr(o.colors,"min"))),TX(K(2)),Py_LT)))tc_raise(tc_call(ns.get("ValueError"),{TX(K(3))}));
        if(tc_truth(tc_compare(TX(tc_call(tc_attr(o.colors,"max"))),TX(K(4)),Py_GT))){warn(K(5));tc_assign(o.colors,tc_call(ns.get("_normalize_color_array"),{TX(o.colors)}));}
        return tc_call(np("atleast_2d"),{TX(tc_call(np("asarray"),{TX(o.colors)},{TX(np("float32"))},K(1)))});
    }
    nb::object string_list(){
        tc_assign(o.color_array,tc_call(np("empty"),{TX(tc_tuple({TX(tc_call(ns.get("len"),{TX(o.colors)})),TX(K(0))}))},{TX(np("float32"))},K(1)));
        nb::object iterator=tc_iter(tc_call(ns.get("enumerate"),{TX(o.colors)}));
        while(nb::object pair=tc_next(iterator)){auto parts=tc_unpack_pair(std::move(pair));tc_assign(o.idx,std::move(parts[0]));tc_assign(o.c,std::move(parts[1]));
            try{tc_set_item(TX(tc_call(tc_index(TX(ns.get("_color_switch")),TX(tc_call(ns.get("type"),{TX(o.c)}))),{TX(o.c)})),TX(o.color_array),TX(tc_tuple({TX(o.idx),TX(tc_slice(nb::none(),nb::none()))})));}
            catch(nb::python_error& failure){nb::tuple types=tc_tuple({TX(ns.get("ValueError")),TX(ns.get("TypeError")),TX(ns.get("KeyError"))});if(!failure.matches(types))throw;TCHandledException handled(failure.value());tc_assign(o.e,nb::borrow<nb::object>(failure.value()));try{nb::object error=tc_call(ns.get("ValueError"),{TX(tc_join({TX(K(2)),TX(tc_format(o.c)),TX(K(3)),TX(tc_format(o.idx)),TX(K(4))}))});tc_raise_cause(std::move(error),o.e);}catch(...){o.e.reset();throw;}}
        }iterator.reset();return o.color_array;
    }
    nb::object none(){return tc_call(np("zeros"),{TX(K(0))},{TX(np("float32"))},K(1));}
    nb::object normalize(){
        tc_assign(o.colors,tc_call(tc_attr(o.colors,"astype"),{TX(np("float32"))},{TX(K(0))},K(1)));
        tc_assign(o.out_of_bounds_idx,tc_call(np("unique"),{TX(tc_item(tc_call(np("where"),{TX(tc_binary(TX(tc_compare(TX(o.colors),TX(K(2)),Py_GT)),TX(tc_compare(TX(o.colors),TX(K(3)),Py_LT)),PyNumber_Or))}),K(3)))}));
        tc_assign(o.out_of_bounds,tc_item(o.colors,o.out_of_bounds_idx));
        tc_assign(o.norm,tc_call(tc_attr(tc_attr(ns.get("np"),"linalg"),"norm"),{TX(o.out_of_bounds),TX(np("inf"))},{TX(K(2))},K(4)));
        tc_assign(o.out_of_bounds,tc_binary(TX(o.out_of_bounds),TX(tc_item(o.norm,tc_tuple({TX(tc_slice(nb::none(),nb::none())),TX(np("newaxis"))}))),PyNumber_TrueDivide));
        tc_set_item(TX(o.out_of_bounds),TX(o.colors),TX(o.out_of_bounds_idx));return tc_call(tc_attr(o.colors,"astype"),{TX(np("float32"))});
    }
};
ThemeColorLocals theme_color_locals(int entry,nb::object a,nb::object b){ThemeColorLocals owner;switch(entry){case 0:case 2:owner.self=std::move(a);break;case 1:owner.self=std::move(a);owner.theme=std::move(b);break;case 4:owner.theme_id=std::move(a);break;case 6:case 7:owner.cls=std::move(a);owner.value=std::move(b);break;case 9:case 15:owner.color=std::move(a);break;case 8:case 10:case 11:case 12:case 13:case 14:case 16:owner.colors=std::move(a);break;case 3:case 5:break;default:throw nb::value_error("unknown theme/color source entry");}return owner;}
nb::object theme_color_run(int entry,nb::dict namespace_,ThemeColorLocals& owner,nb::tuple metadata){TCPolicy policy(std::move(namespace_),owner,std::move(metadata));switch(entry){case 0:return policy.canvas_background();case 1:return policy.canvas_update();case 2:return policy.vispy_background();case 3:return policy.system_theme();case 4:return policy.get_theme();case 5:return policy.available();case 6:return policy.color_new();case 7:return policy.color_validate();case 8:return policy.transform();case 9:return policy.string();case 10:return policy.list_like();case 11:return policy.generator();case 12:return policy.array();case 13:return policy.convert();case 14:return policy.string_list();case 15:return policy.none();case 16:return policy.normalize();default:throw nb::value_error("unknown theme/color source entry");}}
// Six owned Theme module declarations. Only actual re.compile construction is
// retained; the literal, lookup, call and immediate publication order is native.
nb::object theme_regex_load_name(nb::handle locals, nb::handle globals,
                                nb::handle builtins, nb::handle name) {
    // LOAD_NAME checks locals, then globals, even when both are the same dict.
    PyObject* value=PyDict_GetItemWithError(locals.ptr(),name.ptr());
    if (value) return nb::borrow<nb::object>(value);
    if (PyErr_Occurred()) throw nb::python_error();
    value=PyDict_GetItemWithError(globals.ptr(),name.ptr());
    if (value) return nb::borrow<nb::object>(value);
    if (PyErr_Occurred()) throw nb::python_error();
    if (PyDict_CheckExact(builtins.ptr())) {
        value=PyDict_GetItemWithError(builtins.ptr(),name.ptr());
        if (value) return nb::borrow<nb::object>(value);
        if (PyErr_Occurred()) throw nb::python_error();
    } else {
        value=PyObject_GetItem(builtins.ptr(),name.ptr());
        if (value) return nb::steal<nb::object>(value);
        if (!PyErr_ExceptionMatches(PyExc_KeyError)) throw nb::python_error();
        PyErr_Clear();
    }
    nb::object message=tc_check(PyUnicode_FromFormat("name '%U' is not defined",name.ptr()));
    nb::object error=tc_check(PyObject_CallFunctionObjArgs(PyExc_NameError,message.ptr(),nullptr));
    if (PyObject_SetAttrString(error.ptr(),"name",name.ptr())<0) throw nb::python_error();
    PyErr_SetObject(PyExc_NameError,error.ptr()); throw nb::python_error();
}
void theme_regex_initialize() {
    PyFrameObject* caller=PyEval_GetFrame();
    if (!caller) {
        if (PyErr_Occurred()) throw nb::python_error();
        throw nb::type_error("Theme regex initialization requires its actual module frame");
    }
    nb::object code=tc_check(reinterpret_cast<PyObject*>(PyFrame_GetCode(caller)));
    const auto* source_code=reinterpret_cast<PyCodeObject*>(code.ptr());
    nb::object locals=tc_check(PyFrame_GetLocals(caller));
    nb::object globals=tc_check(PyFrame_GetGlobals(caller));
    nb::object builtins=tc_check(PyFrame_GetBuiltins(caller));
    if (!PyDict_CheckExact(locals.ptr()) || locals.ptr()!=globals.ptr() ||
        !PyUnicode_CheckExact(source_code->co_name) ||
        PyUnicode_CompareWithASCIIString(source_code->co_name,"<module>")!=0)
        throw nb::type_error("Theme regex initialization requires its actual module namespace");
    // Original frame builtin identity, with live contents. No __builtins__
    // binding reread, namespace copy, precompiled batch or additional cache.
    struct Declaration { const char* target; const char* pattern; };
    constexpr Declaration declarations[]={
        {"increase_pattern",R"NX({{\s?increase\((\w+),?\s?([-\d]+)?\)\s?}})NX"},
        {"decrease_pattern",R"NX({{\s?decrease\((\w+),?\s?([-\d]+)?\)\s?}})NX"},
        {"gradient_pattern",R"NX(([vh])gradient\((.+)\))NX"},
        {"darken_pattern",R"NX({{\s?darken\((\w+),?\s?([-\d]+)?\)\s?}})NX"},
        {"lighten_pattern",R"NX({{\s?lighten\((\w+),?\s?([-\d]+)?\)\s?}})NX"},
        {"opacity_pattern",R"NX({{\s?opacity\((\w+),?\s?([-\d]+)?\)\s?}})NX"},
    };
    nb::object re_name=tc_check(PyUnicode_InternFromString("re"));
    nb::object compile_name=tc_check(PyUnicode_InternFromString("compile"));
    for (const auto& declaration:declarations) {
        nb::object provider=theme_regex_load_name(locals,globals,builtins,re_name);
        PyObject* raw_compiler=PyObject_GetAttr(provider.ptr(),compile_name.ptr());
        provider.reset();
        nb::object compiler=tc_check(raw_compiler);
        nb::object result=tc_call(std::move(compiler),{
            [&]()->nb::object{return tc_check(PyUnicode_FromString(declaration.pattern));}
        });
        // STORE_NAME completes and drops its temporary result before the next
        // LOAD_NAME. Foreign return objects and old-value finalizers are real.
        nb::object target=tc_check(PyUnicode_InternFromString(declaration.target));
        const int status=PyDict_SetItem(locals.ptr(),target.ptr(),result.ptr());
        result.reset(); target.reset();
        if (status<0) throw nb::python_error();
    }
}

// Handwritten Theme template/font/color/gradient application bodies. Public
// Python frames bind parameters and retain source metadata; they run no policy.
// Color-provider parsing/storage policies remain required same-parent OPEN.
struct ThemeTemplateBinding {
    nb::object callback_factory, native_frame;
    ThemeTemplateBinding()=default;
    ThemeTemplateBinding(ThemeTemplateBinding&&)=default;
    void clear(){auto a=std::move(callback_factory),b=std::move(native_frame);a.reset();b.reset();}
    ~ThemeTemplateBinding(){clear();}
};
struct ThemeTemplateCallback {
    int entry=-1;
    nb::object source_globals,source_builtins,theme_cell,literals;
    ThemeTemplateCallback()=default;
    ThemeTemplateCallback(ThemeTemplateCallback&&)=default;
    void clear(){auto a=std::move(source_globals),b=std::move(source_builtins),c=std::move(theme_cell),d=std::move(literals);a.reset();b.reset();c.reset();d.reset();}
    ~ThemeTemplateCallback(){clear();}
};
#define TT_FIELDS(F) \
F(source_globals) F(source_builtins) F(source_code) F(binding) F(literals) \
F(pt) F(font_size) F(color) F(percentage) F(theme_type) F(value) F(stops) \
F(horizontal) F(css) F(theme_cell) F(matchobj) F(_pt) F(r) F(g) F(b) F(lum) \
F(target) F(scale) F(sat) F(gray) F(v) F(red) F(green) F(blue) F(grad) \
F(_stops) F(k) F(to_add) F(to_subtract) F(iterator) F(n) F(stop) F(i) F(rgb) \
F(_increase_match) F(_decrease_match) F(darken_match) F(lighten_match) \
F(opacity_match) F(gradient_match)
struct ThemeTemplateLocals {
    int entry=-1;
#define TT_DECLARE(name) nb::object name;
    TT_FIELDS(TT_DECLARE)
#undef TT_DECLARE
    ThemeTemplateLocals()=default;
    ThemeTemplateLocals(ThemeTemplateLocals&&)=default;
    void clear(){
#define TT_MOVE(name) std::move(name),
        std::array<nb::object,45> values={TT_FIELDS(TT_MOVE)};
#undef TT_MOVE
        for(auto& value:values)value.reset();
    }
    ~ThemeTemplateLocals(){clear();}
};
int tt_binding_traverse(PyObject* self,visitproc visit,void* arg){Py_VISIT(Py_TYPE(self));if(nb::inst_ready(self)){auto& o=*nb::inst_ptr<ThemeTemplateBinding>(self);Py_VISIT(o.callback_factory.ptr());Py_VISIT(o.native_frame.ptr());}return 0;}
int tt_binding_clear(PyObject* self){if(nb::inst_ready(self))nb::inst_ptr<ThemeTemplateBinding>(self)->clear();return 0;}
PyType_Slot tt_binding_slots[]={{Py_tp_traverse,reinterpret_cast<void*>(tt_binding_traverse)},{Py_tp_clear,reinterpret_cast<void*>(tt_binding_clear)},{0,nullptr}};
int tt_callback_traverse(PyObject* self,visitproc visit,void* arg){Py_VISIT(Py_TYPE(self));if(nb::inst_ready(self)){auto& o=*nb::inst_ptr<ThemeTemplateCallback>(self);Py_VISIT(o.source_globals.ptr());Py_VISIT(o.source_builtins.ptr());Py_VISIT(o.theme_cell.ptr());Py_VISIT(o.literals.ptr());}return 0;}
int tt_callback_clear(PyObject* self){if(nb::inst_ready(self))nb::inst_ptr<ThemeTemplateCallback>(self)->clear();return 0;}
PyType_Slot tt_callback_slots[]={{Py_tp_traverse,reinterpret_cast<void*>(tt_callback_traverse)},{Py_tp_clear,reinterpret_cast<void*>(tt_callback_clear)},{0,nullptr}};
int tt_owner_traverse(PyObject* self,visitproc visit,void* arg){Py_VISIT(Py_TYPE(self));if(nb::inst_ready(self)){auto& o=*nb::inst_ptr<ThemeTemplateLocals>(self);
#define TT_VISIT(name) Py_VISIT(o.name.ptr());
    TT_FIELDS(TT_VISIT)
#undef TT_VISIT
}return 0;}
int tt_owner_clear(PyObject* self){if(nb::inst_ready(self))nb::inst_ptr<ThemeTemplateLocals>(self)->clear();return 0;}
PyType_Slot tt_owner_slots[]={{Py_tp_traverse,reinterpret_cast<void*>(tt_owner_traverse)},{Py_tp_clear,reinterpret_cast<void*>(tt_owner_clear)},{0,nullptr}};

nb::object tt_global(ThemeTemplateLocals& o,const char* name){
    // The actual function/frame builtin table, never the current module's
    // __builtins__ binding. Its contents and actual globals stay live.
    PyObject* raw=PyDict_GetItemString(o.source_globals.ptr(),name);
    if(raw)return nb::borrow<nb::object>(raw);
    if(PyErr_Occurred())throw nb::python_error();
    if(PyDict_CheckExact(o.source_builtins.ptr())){
        raw=PyDict_GetItemString(o.source_builtins.ptr(),name);
        if(raw)return nb::borrow<nb::object>(raw);
        if(PyErr_Occurred())throw nb::python_error();
    }else{
        nb::object key=nb::str(name);raw=PyObject_GetItem(o.source_builtins.ptr(),key.ptr());
        if(raw)return nb::steal<nb::object>(raw);
        if(!PyErr_ExceptionMatches(PyExc_KeyError))throw nb::python_error();
        PyErr_Clear();
    }
    nb::object key=nb::str(name),message=tc_check(PyUnicode_FromFormat("name '%s' is not defined",name));
    nb::object error=tc_check(PyObject_CallFunctionObjArgs(PyExc_NameError,message.ptr(),nullptr));
    if(PyObject_SetAttrString(error.ptr(),"name",key.ptr())<0)throw nb::python_error();
    PyErr_SetObject(PyExc_NameError,error.ptr());throw nb::python_error();
}
std::array<nb::object,3> tt_unpack_three(nb::object value){
    PyObject* raw=PyObject_GetIter(value.ptr());
    if(!raw){nb::python_error error;if(error.matches(PyExc_TypeError)&&Py_TYPE(value.ptr())->tp_iter==nullptr&&!PySequence_Check(value.ptr())){PyErr_Format(PyExc_TypeError,"cannot unpack non-iterable %.200s object",Py_TYPE(value.ptr())->tp_name);throw nb::python_error();}throw error;}
    nb::object iterator=nb::steal<nb::object>(raw);std::array<nb::object,3> result;
    for(size_t index=0;index<3;++index){result[index]=tc_next(iterator);if(!result[index].is_valid()){PyErr_Format(PyExc_ValueError,"not enough values to unpack (expected 3, got %zu)",index);throw nb::python_error();}}
    nb::object extra=tc_next(iterator);if(extra.is_valid()){PyErr_SetString(PyExc_ValueError,"too many values to unpack (expected 3)");throw nb::python_error();}
    iterator.reset();value.reset();return result;
}
nb::object tt_theme_value(const ThemeTemplateLocals& o){
    if(!o.theme_cell.is_valid()||!PyCell_Check(o.theme_cell.ptr()))throw nb::type_error("Theme template source cell was cleared");
    PyObject* value=PyCell_GET(o.theme_cell.ptr());if(value)return nb::borrow<nb::object>(value);
    if(o.entry==9){PyErr_SetString(PyExc_UnboundLocalError,"cannot access local variable 'theme' where it is not associated with a value");throw nb::python_error();}
    nb::object error=tc_check(PyObject_CallFunction(PyExc_NameError,"s","cannot access free variable 'theme' where it is not associated with a value in enclosing scope"));
    nb::object name=nb::str("theme");if(PyObject_SetAttrString(error.ptr(),"name",name.ptr())<0)throw nb::python_error();
    PyErr_SetObject(PyExc_NameError,error.ptr());throw nb::python_error();
}

struct TTPolicy {
    ThemeTemplateLocals& o;
    explicit TTPolicy(ThemeTemplateLocals& owner):o(owner){}
    nb::object K(Py_ssize_t index){return tc_literal(o.literals,index);}
    nb::object G(const char* name){return tt_global(o,name);}
    nb::object theme(){return tt_theme_value(o);}
    nb::object platform(){
        if(tc_truth(tc_compare(TX(tc_attr(G("sys"),"platform")),TX(K(0)),Py_EQ)))
            return tc_binary(TX(tc_binary(TX(tc_call(G("float"),{TX(o.pt)})),TX(K(1)),PyNumber_Multiply)),TX(K(2)),PyNumber_TrueDivide);
        return tc_call(G("float"),{TX(o.pt)});
    }
    nb::object font(bool increase){
        tc_assign(o._pt,tc_call(G("_platform_aware_font_size_adjustment"),{TX(o.pt)}));
        return tc_join({
            TX(tc_format(tc_binary(
                TX(tc_call(G("int"),{TX(tc_item(o.font_size,tc_slice(nb::none(),K(0))))})),
                TX(o._pt),increase?PyNumber_Add:PyNumber_Subtract))),
            TX(K(1))});
    }
    nb::object parse(){
        if(tc_truth(tc_call(G("isinstance"),{TX(o.color),TX(G("str"))}))){
            if(tc_truth(tc_call(tc_attr(o.color,"startswith"),{TX(K(0))})))
                return tc_call(G("literal_eval"),{TX(tc_call(tc_attr(tc_call(tc_attr(o.color,"lstrip"),{TX(K(0))}),"rstrip"),{TX(K(1))}))});
            return tc_item(tc_call(tc_attr(tc_call(G("Color"),{TX(o.color)}),"as_rgb_tuple")),tc_slice(nb::none(),K(2)));
        }
        return tc_item(tc_call(tc_attr(o.color,"as_rgb_tuple")),tc_slice(nb::none(),K(2)));
    }
    nb::object clamp(TCExpr value){return tc_call(G("max"),{TX(K(4)),TX(tc_call(G("min"),{TX(K(8)),TX(tc_call(G("int"),{value}))}))});}
    nb::object luminance(){
        auto parts=tt_unpack_three(o.color);tc_assign(o.r,std::move(parts[0]));tc_assign(o.g,std::move(parts[1]));tc_assign(o.b,std::move(parts[2]));
        tc_assign(o.percentage,tc_binary(TX(o.percentage),TX(K(0)),PyNumber_InPlaceTrueDivide));
        tc_assign(o.lum,tc_binary(TX(tc_binary(TX(tc_binary(TX(K(1)),TX(o.r),PyNumber_Multiply)),TX(tc_binary(TX(K(2)),TX(o.g),PyNumber_Multiply)),PyNumber_Add)),TX(tc_binary(TX(K(3)),TX(o.b),PyNumber_Multiply)),PyNumber_Add));
        if(tc_truth(tc_compare(TX(o.percentage),TX(K(4)),Py_GE)))
            tc_assign(o.target,tc_binary(TX(o.lum),TX(tc_binary(TX(tc_binary(TX(K(5)),TX(o.lum),PyNumber_Subtract)),TX(o.percentage),PyNumber_Multiply)),PyNumber_Add));
        else tc_assign(o.target,tc_binary(TX(o.lum),TX(tc_binary(TX(K(6)),TX(o.percentage),PyNumber_Add)),PyNumber_Multiply));
        tc_assign(o.target,tc_call(G("max"),{TX(K(7)),TX(tc_call(G("min"),{TX(K(5)),TX(o.target)}))}));
        if(tc_truth(tc_compare(TX(o.lum),TX(K(6)),Py_LT))){tc_assign(o.v,clamp(TX(o.target)));return tc_tuple({TX(o.v),TX(o.v),TX(o.v)});}
        tc_assign(o.scale,tc_binary(TX(o.target),TX(o.lum),PyNumber_TrueDivide));
        tc_assign(o.r,tc_binary(TX(o.r),TX(o.scale),PyNumber_InPlaceMultiply));
        tc_assign(o.g,tc_binary(TX(o.g),TX(o.scale),PyNumber_InPlaceMultiply));
        tc_assign(o.b,tc_binary(TX(o.b),TX(o.scale),PyNumber_InPlaceMultiply));
        if(tc_truth(tc_compare(TX(o.scale),TX(K(6)),Py_GT)))
            tc_assign(o.sat,tc_binary(TX(K(6)),TX(tc_binary(TX(K(6)),TX(tc_binary(TX(tc_binary(TX(o.scale),TX(K(6)),PyNumber_Subtract)),TX(K(9)),PyNumber_Multiply)),PyNumber_Add)),PyNumber_TrueDivide));
        else tc_assign(o.sat,tc_binary(TX(K(6)),TX(tc_binary(TX(tc_binary(TX(K(6)),TX(o.scale),PyNumber_Subtract)),TX(K(9)),PyNumber_Multiply)),PyNumber_Add));
        tc_assign(o.gray,tc_binary(TX(tc_binary(TX(tc_binary(TX(o.r),TX(o.g),PyNumber_Add)),TX(o.b),PyNumber_Add)),TX(K(10)),PyNumber_TrueDivide));
        tc_assign(o.r,tc_binary(TX(o.gray),TX(tc_binary(TX(tc_binary(TX(o.r),TX(o.gray),PyNumber_Subtract)),TX(o.sat),PyNumber_Multiply)),PyNumber_Add));
        tc_assign(o.g,tc_binary(TX(o.gray),TX(tc_binary(TX(tc_binary(TX(o.g),TX(o.gray),PyNumber_Subtract)),TX(o.sat),PyNumber_Multiply)),PyNumber_Add));
        tc_assign(o.b,tc_binary(TX(o.gray),TX(tc_binary(TX(tc_binary(TX(o.b),TX(o.gray),PyNumber_Subtract)),TX(o.sat),PyNumber_Multiply)),PyNumber_Add));
        return tc_tuple({TX(clamp(TX(o.r))),TX(clamp(TX(o.g))),TX(clamp(TX(o.b)))});
    }
    nb::object shade(){
        tc_assign(o.rgb,tc_call(G("_parse_color_as_rgb"),{TX(o.color)}));
        tc_assign(o.percentage,tc_binary(TX(o.percentage),TX(tc_truth(tc_compare(TX(o.theme_type),TX(K(0)),Py_EQ))?K(1):K(2)),PyNumber_InPlaceMultiply));
        auto parts=tt_unpack_three(tc_call(G("_shift_luminance"),{TX(o.rgb),TX(o.percentage)}));
        tc_assign(o.r,std::move(parts[0]));tc_assign(o.g,std::move(parts[1]));tc_assign(o.b,std::move(parts[2]));
        return tc_join({TX(K(3)),TX(tc_format(o.r)),TX(K(4)),TX(tc_format(o.g)),TX(K(4)),TX(tc_format(o.b)),TX(K(5))});
    }
    nb::object alpha(){
        auto parts=tt_unpack_three(tc_call(G("_parse_color_as_rgb"),{TX(o.color)}));
        tc_assign(o.red,std::move(parts[0]));tc_assign(o.green,std::move(parts[1]));tc_assign(o.blue,std::move(parts[2]));
        return tc_join({TX(K(0)),TX(tc_format(o.red)),TX(K(1)),TX(tc_format(o.green)),TX(K(1)),TX(tc_format(o.blue)),TX(K(1)),TX(tc_format(tc_call(G("max"),{TX(tc_call(G("min"),{TX(tc_call(G("int"),{TX(o.value)})),TX(K(3))})),TX(K(4))}))),TX(K(2))});
    }
    nb::object gradient(){
        tc_assign(o.grad,tc_truth(o.horizontal)?K(0):K(1));
        nb::object iterable=tc_call(G("enumerate"),{TX(o.stops)});
        nb::object iterator=tc_iter(std::move(iterable)),items=tc_check(PyList_New(0));
        try{
            while(nb::object item=tc_next(iterator)){
                auto parts=tc_unpack_pair(std::move(item));tc_assign(o.n,std::move(parts[0]));tc_assign(o.stop,std::move(parts[1]));
                nb::object result=tc_join({TX(K(2)),TX(tc_format(o.n)),TX(K(3)),TX(tc_format(o.stop))});
                int status=PyList_Append(items.ptr(),result.ptr());result.reset();if(status<0)throw nb::python_error();
            }
        }catch(...){
#if PY_VERSION_HEX < 0x030C0000
            items.reset();tc_assign(o.iterator,std::move(iterator));
#else
            iterator.reset();items.reset();o.stop.reset();o.n.reset();
#endif
            throw;}
        iterator.reset();
#if PY_VERSION_HEX < 0x030C0000
        o.n.reset();o.stop.reset();tc_assign(o._stops,std::move(items));
#else
        tc_assign(o._stops,std::move(items));o.n.reset();o.stop.reset();
#endif
        tc_assign(o.grad,tc_binary(TX(o.grad),TX(tc_binary(TX(tc_call(tc_attr(K(4),"join"),{TX(o._stops)})),TX(K(5)),PyNumber_Add)),PyNumber_InPlaceAdd));
        return o.grad;
    }
    nb::object callback(){
        if(o.entry==15){
            tc_assign(o.horizontal,tc_compare(TX(tc_item(tc_call(tc_attr(o.matchobj,"groups")),nb::int_(1))),TX(K(1)),Py_EQ));
            nb::object iterator=tc_iter(tc_call(tc_attr(tc_item(tc_call(tc_attr(o.matchobj,"groups")),nb::int_(1)),"split"),{TX(K(2))}));
            nb::object items=tc_check(PyList_New(0));
            try{
                while(nb::object item=tc_next(iterator)){tc_assign(o.i,std::move(item));nb::object result=tc_call(tc_attr(o.i,"strip"));int status=PyList_Append(items.ptr(),result.ptr());result.reset();if(status<0)throw nb::python_error();}
            }catch(...){
#if PY_VERSION_HEX < 0x030C0000
                items.reset();tc_assign(o.iterator,std::move(iterator));
#else
                iterator.reset();items.reset();o.i.reset();
#endif
                throw;}
            iterator.reset();
#if PY_VERSION_HEX < 0x030C0000
            o.i.reset();tc_assign(o.stops,std::move(items));
#else
            tc_assign(o.stops,std::move(items));o.i.reset();
#endif
            return tc_call(G("gradient"),{TX(o.stops),TX(o.horizontal)});
        }
        auto parts=tc_unpack_pair(tc_call(tc_attr(o.matchobj,"groups")));
        if(o.entry==10){tc_assign(o.font_size,std::move(parts[0]));tc_assign(o.to_add,std::move(parts[1]));return tc_call(G("increase"),{TX(tc_item(theme(),o.font_size)),TX(o.to_add)});}
        if(o.entry==11){tc_assign(o.font_size,std::move(parts[0]));tc_assign(o.to_subtract,std::move(parts[1]));return tc_call(G("decrease"),{TX(tc_item(theme(),o.font_size)),TX(o.to_subtract)});}
        tc_assign(o.color,std::move(parts[0]));
        if(o.entry==14){tc_assign(o.value,std::move(parts[1]));return tc_call(G("opacity"),{TX(tc_item(theme(),o.color)),TX(tc_call(G("int"),{TX(o.value)}))});}
        tc_assign(o.percentage,std::move(parts[1]));
        return tc_call(G(o.entry==12?"darken":"lighten"),{TX(tc_item(theme(),o.color)),TX(tc_call(G("float"),{TX(o.percentage)}))},{TX(tc_item(theme(),K(0)))},tc_tuple({TX(tc_check(PyUnicode_InternFromString("theme_type")))}));
    }
    nb::object create_callback(int entry,const char* name){
        auto* binding=nb::inst_ptr<ThemeTemplateBinding>(o.binding.ptr());
        ThemeTemplateCallback context;context.entry=entry;context.source_globals=o.source_globals;
        context.literals=o.literals;if(entry!=15)context.theme_cell=o.theme_cell;
        nb::object capture=nb::cast(std::move(context));
        auto* source_code=reinterpret_cast<PyCodeObject*>(o.source_code.ptr());
        const int callback_ends[]={508,512,518,524,528,533};
        const int source_end=callback_ends[entry-10];
        nb::object declaration=tc_call(binding->callback_factory,{TX(capture)});
        if(!PyFunction_Check(declaration.ptr())||PyFunction_GetGlobals(declaration.ptr())!=o.source_globals.ptr())throw nb::type_error("Theme template callback declaration metadata changed");
        nb::object code=nb::borrow<nb::object>(PyFunction_GetCode(declaration.ptr()));
        auto* shell=reinterpret_cast<PyCodeObject*>(code.ptr());
        nb::object bytes=tc_check(PyCode_GetCode(shell));
        const int last_line=PyCode_Addr2Line(shell,static_cast<int>(PyBytes_GET_SIZE(bytes.ptr())-2));
        if(last_line<shell->co_firstlineno)throw nb::type_error("Theme template callback source metadata is missing");
        nb::object constants=tc_attr(code,"co_consts"),replacements=tc_check(PyTuple_New(PyTuple_GET_SIZE(constants.ptr())));bool patched=false;
        for(Py_ssize_t item=0;item<PyTuple_GET_SIZE(constants.ptr());++item){PyObject* value=PyTuple_GET_ITEM(constants.ptr(),item);if(value==Py_Ellipsis){value=binding->native_frame.ptr();patched=true;}PyTuple_SET_ITEM(replacements.ptr(),item,Py_NewRef(value));}
        if(!patched)throw nb::type_error("Theme template callback declaration marker is missing");
        nb::object qualname=tc_check(PyUnicode_FromFormat("template.<locals>.%s",name));
        nb::object keywords=tc_tuple({TX(nb::str("co_consts")),TX(nb::str("co_filename")),TX(nb::str("co_firstlineno")),TX(nb::str("co_name")),TX(nb::str("co_qualname"))});
        nb::object replacement=tc_call(tc_attr(code,"replace"),{},
            {TX(replacements),TX(nb::borrow<nb::object>(source_code->co_filename)),TX(nb::int_(shell->co_firstlineno+source_end-last_line)),TX(nb::str(name)),TX(qualname)},keywords);
        // Construct while the actual template frame is current: its builtin
        // table is the fallback only if current source globals omit the key.
        nb::object result=tc_check(PyFunction_NewWithQualName(replacement.ptr(),o.source_globals.ptr(),qualname.ptr()));
        PyObject* declared_closure=PyFunction_GetClosure(declaration.ptr());
        nb::object freevars=tc_attr(replacement,"co_freevars");
        if(!declared_closure||PyTuple_GET_SIZE(declared_closure)!=PyTuple_GET_SIZE(freevars.ptr()))throw nb::type_error("Theme template callback closure metadata changed");
        nb::object closure=tc_check(PyTuple_New(PyTuple_GET_SIZE(declared_closure)));bool shared=false;
        for(Py_ssize_t item=0;item<PyTuple_GET_SIZE(declared_closure);++item){PyObject* cell=PyTuple_GET_ITEM(declared_closure,item);if(PyUnicode_CompareWithASCIIString(PyTuple_GET_ITEM(freevars.ptr(),item),"theme")==0){if(entry==15||!o.theme_cell.is_valid())throw nb::type_error("Theme template callback cell scope changed");cell=o.theme_cell.ptr();shared=true;}PyTuple_SET_ITEM(closure.ptr(),item,Py_NewRef(cell));}
        if(shared!=(entry!=15))throw nb::type_error("Theme template callback shared-cell metadata changed");
        if(PyFunction_SetClosure(result.ptr(),closure.ptr())<0)throw nb::python_error();
        nb::inst_ptr<ThemeTemplateCallback>(capture.ptr())->source_builtins=tc_attr(result,"__builtins__");
        return result;
    }
    nb::object expand(){
        tc_assign(o._increase_match,create_callback(10,"_increase_match"));
        tc_assign(o._decrease_match,create_callback(11,"_decrease_match"));
        tc_assign(o.darken_match,create_callback(12,"darken_match"));
        tc_assign(o.lighten_match,create_callback(13,"lighten_match"));
        tc_assign(o.opacity_match,create_callback(14,"opacity_match"));
        tc_assign(o.gradient_match,create_callback(15,"gradient_match"));
        tc_assign(o.iterator,tc_iter(tc_call(tc_attr(theme(),"items"))));
        while(nb::object item=tc_next(o.iterator)){
            auto parts=tc_unpack_pair(std::move(item));tc_assign(o.k,std::move(parts[0]));tc_assign(o.v,std::move(parts[1]));
            tc_assign(o.css,tc_call(tc_attr(G("increase_pattern"),"sub"),{TX(o._increase_match),TX(o.css)}));
            tc_assign(o.css,tc_call(tc_attr(G("decrease_pattern"),"sub"),{TX(o._decrease_match),TX(o.css)}));
            tc_assign(o.css,tc_call(tc_attr(G("gradient_pattern"),"sub"),{TX(o.gradient_match),TX(o.css)}));
            tc_assign(o.css,tc_call(tc_attr(G("darken_pattern"),"sub"),{TX(o.darken_match),TX(o.css)}));
            tc_assign(o.css,tc_call(tc_attr(G("lighten_pattern"),"sub"),{TX(o.lighten_match),TX(o.css)}));
            tc_assign(o.css,tc_call(tc_attr(G("opacity_pattern"),"sub"),{TX(o.opacity_match),TX(o.css)}));
            if(tc_truth(tc_call(G("isinstance"),{TX(o.v),TX(G("Color"))})))tc_assign(o.v,tc_call(tc_attr(o.v,"as_rgb")));
            tc_assign(o.css,tc_call(tc_attr(o.css,"replace"),{TX(tc_join({TX(K(3)),TX(tc_format(o.k)),TX(K(4))})),TX(o.v)}));
        }
        o.iterator.reset();return o.css;
    }
    nb::object run(){switch(o.entry){case 0:return platform();case 1:return font(false);case 2:return font(true);case 3:return parse();case 4:return luminance();case 5:case 6:return shade();case 7:return alpha();case 8:return gradient();case 9:return expand();case 10:case 11:case 12:case 13:case 14:case 15:return callback();default:throw nb::value_error("unknown Theme template source entry");}}
};

PyObject* tt_frame(PyObject*,PyObject* arguments){
    try{
        const Py_ssize_t size=PyTuple_GET_SIZE(arguments);
        if(size==1){nb::handle value(PyTuple_GET_ITEM(arguments,0));if(!nb::isinstance<ThemeTemplateLocals>(value)||!nb::inst_ready(value.ptr()))throw nb::type_error("Theme template source owner is required");auto& owner=*nb::inst_ptr<ThemeTemplateLocals>(value.ptr());if(!owner.source_globals.is_valid()||!owner.source_builtins.is_valid()||!owner.literals.is_valid())throw nb::type_error("Theme template source metadata was cleared");return TTPolicy(owner).run().release().ptr();}
        if(size<2)throw nb::type_error("Theme template declaration arguments are required");
        ThemeTemplateLocals owner;
        nb::handle first(PyTuple_GET_ITEM(arguments,0));
        if(nb::isinstance<ThemeTemplateCallback>(first)){
            if(!nb::inst_ready(first.ptr()))throw nb::type_error("Theme template callback metadata was cleared");
            auto& callback=*nb::inst_ptr<ThemeTemplateCallback>(first.ptr());owner.entry=callback.entry;
            if(size!=2)throw nb::type_error("Theme template callback declaration arguments changed");
            if(!callback.source_globals.is_valid()||!callback.source_builtins.is_valid()||!callback.literals.is_valid())throw nb::type_error("Theme template callback namespaces were cleared");
            owner.source_globals=callback.source_globals;owner.source_builtins=callback.source_builtins;owner.literals=callback.literals;
            if(owner.entry!=15){if(!callback.theme_cell.is_valid()||!PyCell_Check(callback.theme_cell.ptr()))throw nb::type_error("Theme template callback shared cell was cleared");owner.theme_cell=callback.theme_cell;}
            owner.matchobj=nb::borrow<nb::object>(PyTuple_GET_ITEM(arguments,size-1));
        }else{
            if(!PyLong_CheckExact(first.ptr()))throw nb::type_error("Theme template source entry must be an integer");
            long entry=PyLong_AsLong(first.ptr());if(entry==-1&&PyErr_Occurred())throw nb::python_error();owner.entry=static_cast<int>(entry);
            if(owner.entry<0||owner.entry>9)throw nb::value_error("unknown Theme template source entry");
            nb::handle literals(PyTuple_GET_ITEM(arguments,1));if(!PyTuple_CheckExact(literals.ptr()))throw nb::type_error("Theme template literal declaration metadata changed");owner.literals=nb::borrow<nb::object>(literals);
            const int literal_counts[]={3,2,2,3,11,6,6,5,6,5};if(PyTuple_GET_SIZE(literals.ptr())!=literal_counts[owner.entry])throw nb::type_error("Theme template literal declaration scope changed");
            for(Py_ssize_t item=0;item<PyTuple_GET_SIZE(literals.ptr());++item){PyObject* value=PyTuple_GET_ITEM(literals.ptr(),item);if(!PyUnicode_CheckExact(value)&&!PyLong_CheckExact(value)&&!PyFloat_CheckExact(value))throw nb::type_error("Theme template literals must be immutable source scalars");}
            const int counts[]={1,2,2,1,2,3,3,2,2,2};if(size!=counts[owner.entry]+2)throw nb::type_error("Theme template source parameters changed");
            PyFrameObject* frame=PyEval_GetFrame();if(!frame)throw nb::type_error("Theme template source frame is required");
            owner.source_globals=tc_check(PyFrame_GetGlobals(frame));owner.source_builtins=tc_check(PyFrame_GetBuiltins(frame));
            if(!PyDict_CheckExact(owner.source_globals.ptr()))throw nb::type_error("Theme template source globals must be the actual dict");
            auto parameter=[&](Py_ssize_t index){return nb::borrow<nb::object>(PyTuple_GET_ITEM(arguments,index+2));};
            switch(owner.entry){case 0:owner.pt=parameter(0);break;case 1:case 2:owner.font_size=parameter(0);owner.pt=parameter(1);break;case 3:owner.color=parameter(0);break;case 4:owner.color=parameter(0);owner.percentage=parameter(1);break;case 5:case 6:owner.color=parameter(0);owner.percentage=parameter(1);owner.theme_type=parameter(2);break;case 7:owner.color=parameter(0);owner.value=parameter(1);break;case 8:owner.stops=parameter(0);owner.horizontal=parameter(1);break;case 9:owner.css=parameter(0);{nb::object actual_theme=parameter(1);owner.theme_cell=tc_check(PyCell_New(actual_theme.ptr()));}owner.source_code=tc_check(reinterpret_cast<PyObject*>(PyFrame_GetCode(frame)));{PyObject* raw=PyDict_GetItemString(owner.source_globals.ptr(),"_theme_template_binding");if(!raw||!nb::isinstance<ThemeTemplateBinding>(nb::handle(raw))||!nb::inst_ready(raw))throw nb::type_error("Theme template declaration binding is missing or changed");owner.binding=nb::borrow<nb::object>(raw);}break;}
        }
        return nb::cast(std::move(owner)).release().ptr();
    }catch(nb::python_error& error){error.restore();}
    catch(const nb::builtin_exception& error){PyErr_SetString(error.type()==nb::exception_type::value_error?PyExc_ValueError:PyExc_TypeError,error.what());}
    catch(const std::bad_alloc&){PyErr_NoMemory();}
    catch(const std::exception& error){PyErr_SetString(PyExc_RuntimeError,error.what());}
    catch(...){PyErr_SetString(PyExc_SystemError,"untranslated Theme template source exception");}
    return nullptr;
}
PyMethodDef tt_frame_definition={"_theme_template_source_frame",tt_frame,METH_VARARGS,nullptr};
ThemeTemplateBinding theme_template_bind(nb::tuple functions,nb::object callback_factory){
    if(PyTuple_GET_SIZE(functions.ptr())!=10||!PyFunction_Check(callback_factory.ptr()))throw nb::type_error("Theme template declaration factory is required");
    ThemeTemplateBinding binding;binding.callback_factory=std::move(callback_factory);
    nb::object module_name=nb::str("naparixx._native");binding.native_frame=tc_check(PyCFunction_NewEx(&tt_frame_definition,nullptr,module_name.ptr()));
    for(Py_ssize_t index=0;index<10;++index){
        PyObject* function=PyTuple_GET_ITEM(functions.ptr(),index);if(!PyFunction_Check(function))throw nb::type_error("Theme template source declaration must be a Python function");
        nb::object code=nb::borrow<nb::object>(PyFunction_GetCode(function)),constants=tc_attr(code,"co_consts");
        if(!PyTuple_CheckExact(constants.ptr()))throw nb::type_error("Theme template source constants changed");
        nb::object replacements=tc_check(PyTuple_New(PyTuple_GET_SIZE(constants.ptr())));bool patched=false,already=false;
        for(Py_ssize_t item=0;item<PyTuple_GET_SIZE(constants.ptr());++item){PyObject* value=PyTuple_GET_ITEM(constants.ptr(),item);if(value==Py_Ellipsis){value=binding.native_frame.ptr();patched=true;}else if(PyCFunction_Check(value)&&PyCFunction_GET_FUNCTION(value)==reinterpret_cast<PyCFunction>(tt_frame)&&PyCFunction_GET_SELF(value)==nullptr)already=true;PyTuple_SET_ITEM(replacements.ptr(),item,Py_NewRef(value));}
        if(!patched){if(already)continue;throw nb::type_error("Theme template declaration native marker is missing");}
        auto* shell=reinterpret_cast<PyCodeObject*>(code.ptr());nb::object bytes=tc_check(PyCode_GetCode(shell));
        const int last_line=PyCode_Addr2Line(shell,static_cast<int>(PyBytes_GET_SIZE(bytes.ptr())-2));
        const int source_ends[]={391,397,403,411,462,473,484,489,502,545};
        if(last_line<shell->co_firstlineno)throw nb::type_error("Theme template source declaration line metadata is missing");
        nb::object names=tc_tuple({TX(nb::str("co_consts")),TX(nb::str("co_firstlineno"))});
        nb::object replacement=tc_call(tc_attr(code,"replace"),{},{TX(replacements),TX(nb::int_(shell->co_firstlineno+source_ends[index]-last_line))},names);
        if(PyObject_SetAttrString(function,"__code__",replacement.ptr())<0)throw nb::python_error();
    }
    return binding;
}

// Native policies for the genuine extra-types Color construction/RGB frontier.
// The original classes and their real slots are the sole representation.
#define CP_FIELDS(F) \
F(source_globals) F(source_builtins) F(source_code) F(binding) \
F(self) F(value) F(r) F(g) F(b) F(alpha) F(item) F(max_val) F(color) \
F(h) F(h_units) F(sat) F(light) F(h_value) F(s_value) F(l_value) \
F(value_lower) F(m) F(rgb) F(a) F(c) F(hue) F(l) F(s) F(m1) F(m2)
struct ColorPolicyLocals {
    int entry=-1;
#define CP_DECLARE(name) nb::object name;
    CP_FIELDS(CP_DECLARE)
#undef CP_DECLARE
    ColorPolicyLocals()=default;
    ColorPolicyLocals(ColorPolicyLocals&&)=default;
    void clear(){
#define CP_CLEAR(name) name.reset();
        CP_FIELDS(CP_CLEAR)
#undef CP_CLEAR
    }
    ~ColorPolicyLocals(){clear();}
};
struct ColorPolicyBinding {
    nb::object frame, shells, generator_shell, profiles, class_owner, schema_owner;
    ColorPolicyBinding()=default;
    ColorPolicyBinding(ColorPolicyBinding&&)=default;
    void clear(){frame.reset();shells.reset();generator_shell.reset();profiles.reset();class_owner.reset();schema_owner.reset();}
    ~ColorPolicyBinding(){clear();}
};
struct ColorPolicyGenerator {
    int entry=-1;
    bool exhausted=false;
    nb::object source_globals,source_builtins,iterator,v;
    ColorPolicyGenerator()=default;
    ColorPolicyGenerator(ColorPolicyGenerator&&)=default;
    void clear(){source_globals.reset();source_builtins.reset();iterator.reset();v.reset();}
    ~ColorPolicyGenerator(){clear();}
};
int cp_owner_traverse(PyObject* self,visitproc visit,void* arg){Py_VISIT(Py_TYPE(self));if(nb::inst_ready(self)){auto& o=*nb::inst_ptr<ColorPolicyLocals>(self);
#define CP_VISIT(name) Py_VISIT(o.name.ptr());
    CP_FIELDS(CP_VISIT)
#undef CP_VISIT
}return 0;}
int cp_owner_clear(PyObject* self){if(nb::inst_ready(self))nb::inst_ptr<ColorPolicyLocals>(self)->clear();return 0;}
PyType_Slot cp_owner_slots[]={{Py_tp_traverse,reinterpret_cast<void*>(cp_owner_traverse)},{Py_tp_clear,reinterpret_cast<void*>(cp_owner_clear)},{0,nullptr}};
int cp_binding_traverse(PyObject* self,visitproc visit,void* arg){Py_VISIT(Py_TYPE(self));if(nb::inst_ready(self)){auto& o=*nb::inst_ptr<ColorPolicyBinding>(self);Py_VISIT(o.frame.ptr());Py_VISIT(o.shells.ptr());Py_VISIT(o.generator_shell.ptr());Py_VISIT(o.profiles.ptr());Py_VISIT(o.class_owner.ptr());Py_VISIT(o.schema_owner.ptr());}return 0;}
int cp_binding_clear(PyObject* self){if(nb::inst_ready(self))nb::inst_ptr<ColorPolicyBinding>(self)->clear();return 0;}
PyType_Slot cp_binding_slots[]={{Py_tp_traverse,reinterpret_cast<void*>(cp_binding_traverse)},{Py_tp_clear,reinterpret_cast<void*>(cp_binding_clear)},{0,nullptr}};
int cp_generator_traverse(PyObject* self,visitproc visit,void* arg){Py_VISIT(Py_TYPE(self));if(nb::inst_ready(self)){auto& o=*nb::inst_ptr<ColorPolicyGenerator>(self);Py_VISIT(o.source_globals.ptr());Py_VISIT(o.source_builtins.ptr());Py_VISIT(o.iterator.ptr());Py_VISIT(o.v.ptr());}return 0;}
int cp_generator_clear(PyObject* self){if(nb::inst_ready(self))nb::inst_ptr<ColorPolicyGenerator>(self)->clear();return 0;}
PyType_Slot cp_generator_slots[]={{Py_tp_traverse,reinterpret_cast<void*>(cp_generator_traverse)},{Py_tp_clear,reinterpret_cast<void*>(cp_generator_clear)},{0,nullptr}};

nb::object cp_global(nb::handle globals,nb::handle builtins,const char* name){
    PyObject* raw=PyDict_GetItemString(globals.ptr(),name);if(raw)return nb::borrow<nb::object>(raw);if(PyErr_Occurred())throw nb::python_error();
    if(PyDict_CheckExact(builtins.ptr())){raw=PyDict_GetItemString(builtins.ptr(),name);if(raw)return nb::borrow<nb::object>(raw);if(PyErr_Occurred())throw nb::python_error();}
    else{nb::object key=nb::str(name);raw=PyObject_GetItem(builtins.ptr(),key.ptr());if(raw)return nb::steal<nb::object>(raw);if(!PyErr_ExceptionMatches(PyExc_KeyError))throw nb::python_error();PyErr_Clear();}
    nb::object key=nb::str(name),message=tc_check(PyUnicode_FromFormat("name '%s' is not defined",name));nb::object error=tc_call(nb::borrow<nb::object>(PyExc_NameError),{TX(message)});
    if(PyObject_SetAttrString(error.ptr(),"name",key.ptr())<0)throw nb::python_error();tc_raise(std::move(error));
}
bool cp_except_match(nb::python_error& error,nb::object types){
    if(PyTuple_Check(types.ptr())){
        for(Py_ssize_t i=0;i<PyTuple_GET_SIZE(types.ptr());++i)if(!PyExceptionClass_Check(PyTuple_GET_ITEM(types.ptr(),i))){PyErr_SetString(PyExc_TypeError,"catching classes that do not inherit from BaseException is not allowed");throw nb::python_error();}
    }else if(!PyExceptionClass_Check(types.ptr())){PyErr_SetString(PyExc_TypeError,"catching classes that do not inherit from BaseException is not allowed");throw nb::python_error();}
    return error.matches(types);
}
nb::object cp_generator_next(ColorPolicyGenerator& o){
    if(o.exhausted){PyErr_SetNone(PyExc_StopIteration);throw nb::python_error();}
    auto G=[&](const char* name){return cp_global(o.source_globals,o.source_builtins,name);};
    try{
        nb::object next=tc_next(o.iterator);
        if(!next.is_valid()){o.exhausted=true;o.iterator.reset();o.v.reset();PyErr_SetNone(PyExc_StopIteration);throw nb::python_error();}
        tc_assign(o.v,std::move(next));
        if(o.entry==0)return tc_call(G("float_to_255"),{TX(o.v)});
        if(o.entry==1||o.entry==2)return tc_call(G("parse_color_value"),{TX(o.v)});
        if(o.entry==3)return tc_call(G("int"),{TX(tc_binary(TX(o.v),TX(nb::int_(2)),PyNumber_Multiply)),TX(nb::int_(16))});
        if(o.entry==4)return tc_call(G("int"),{TX(o.v),TX(nb::int_(16))});
        throw nb::value_error("unknown Color generator source entry");
    }catch(nb::python_error& error){
        if(o.exhausted)throw;
        o.exhausted=true;
        if(error.matches(PyExc_StopIteration)){
            TCHandledException handled(error.value());nb::object failure=tc_call(nb::borrow<nb::object>(PyExc_RuntimeError),{TX(nb::str("generator raised StopIteration"))});
            PyException_SetContext(failure.ptr(),error.value().inc_ref().ptr());PyException_SetCause(failure.ptr(),error.value().inc_ref().ptr());tc_raise(std::move(failure));
        }
        throw;
    }
}
// A freshly created genuine generator declaration captures its own current
// globals-derived builtin table. Existing outer functions retain theirs.
nb::object cp_generator(ColorPolicyLocals& o,int entry,nb::object iterable){
    nb::object iterator=tc_iter(std::move(iterable));
    auto& binding=*nb::inst_ptr<ColorPolicyBinding>(o.binding.ptr());
    nb::object shell=tc_attr(binding.generator_shell,"__code__"),constants=tc_attr(shell,"co_consts");
    nb::object replacements=tc_check(PyTuple_New(PyTuple_GET_SIZE(constants.ptr())));
    for(Py_ssize_t i=0;i<PyTuple_GET_SIZE(constants.ptr());++i)PyTuple_SET_ITEM(replacements.ptr(),i,Py_NewRef(PyTuple_GET_ITEM(constants.ptr(),i)));
    const int lines[]={170,251,254,289,296};
    nb::object names=tc_tuple({TX(nb::str("co_consts")),TX(nb::str("co_name")),TX(nb::str("co_qualname")),TX(nb::str("co_filename")),TX(nb::str("co_firstlineno"))});
    nb::object code=tc_call(tc_attr(shell,"replace"),{},{TX(replacements),TX(nb::str("<genexpr>")),TX(nb::str("<genexpr>")),TX(tc_attr(o.source_code,"co_filename")),TX(nb::int_(lines[entry]-1))},names);
    nb::object function=tc_check(PyFunction_NewWithQualName(code.ptr(),o.source_globals.ptr(),nb::str("<genexpr>").ptr()));
    ColorPolicyGenerator owner;owner.entry=entry;owner.source_globals=o.source_globals;owner.source_builtins=tc_attr(function,"__builtins__");owner.iterator=std::move(iterator);
    nb::object context=nb::cast(std::move(owner));return tc_call(std::move(function),{TX(context)});
}
nb::object cp_expanded_call(nb::object function,TCExpr expansion){
    nb::object iterable=expansion(),args=tc_check(PySequence_Tuple(iterable.ptr()));iterable.reset();PyObject* result=PyObject_CallObject(function.ptr(),args.ptr());function.reset();args.reset();return tc_check(result);
}
std::pair<nb::object,nb::object> cp_unpack_star_last(nb::object value){
    nb::object iterator=tc_iter(std::move(value)),items=tc_check(PySequence_List(iterator.ptr()));iterator.reset();
    const Py_ssize_t size=PyList_GET_SIZE(items.ptr());if(size<1){PyErr_Format(PyExc_ValueError,"not enough values to unpack (expected at least 1, got %zd)",size);throw nb::python_error();}
    nb::object last=nb::borrow<nb::object>(PyList_GET_ITEM(items.ptr(),size-1));if(PySequence_DelItem(items.ptr(),size-1)<0)throw nb::python_error();return {std::move(items),std::move(last)};
}
struct CPPolicy {
    ColorPolicyLocals& o;
    explicit CPPolicy(ColorPolicyLocals& owner):o(owner){}
    nb::object G(const char* name){return cp_global(o.source_globals,o.source_builtins,name);}
    nb::object rgba(){return tc_attr(o.self,"_rgba");}
    nb::object custom(const char* message){return tc_call(G("PydanticCustomError"),{TX(nb::str("color_error")),TX(nb::str(message))});}
    void channels(nb::object value){auto values=tt_unpack_three(std::move(value));tc_assign(o.r,std::move(values[0]));tc_assign(o.g,std::move(values[1]));tc_assign(o.b,std::move(values[2]));}
    bool is_instance(TCExpr value,TCExpr type){return tc_truth(tc_call(G("isinstance"),{std::move(value),std::move(type)}));}
    nb::object color_init(){
        if(is_instance(TX(o.value),TX(tc_tuple({TX(G("tuple")),TX(G("list"))}))))tc_publish(TX(tc_call(G("parse_tuple"),{TX(o.value)})),TX(o.self),"_rgba");
        else if(is_instance(TX(o.value),TX(G("str"))))tc_publish(TX(tc_call(G("parse_str"),{TX(o.value)})),TX(o.self),"_rgba");
        else if(is_instance(TX(o.value),TX(G("Color")))){tc_publish(TX(tc_attr(o.value,"_rgba")),TX(o.self),"_rgba");tc_assign(o.value,tc_attr(o.value,"_original"));}
        else tc_raise(custom("value is not a valid color: value must be a tuple, list or string"));
        tc_publish(TX(o.value),TX(o.self),"_original");return nb::none();
    }
    nb::object rgba_init(){
        tc_publish(TX(o.r),TX(o.self),"r");tc_publish(TX(o.g),TX(o.self),"g");tc_publish(TX(o.b),TX(o.self),"b");tc_publish(TX(o.alpha),TX(o.self),"alpha");
        tc_publish(TX(tc_tuple({TX(o.r),TX(o.g),TX(o.b),TX(o.alpha)})),TX(o.self),"_tuple");return nb::none();
    }
    nb::object alpha_float(){return tc_attr(rgba(),"alpha").is_none()?nb::object(nb::int_(1)):tc_attr(rgba(),"alpha");}
    nb::object rgb(){
        if(tc_attr(rgba(),"alpha").is_none())return tc_join({TX(nb::str("rgb(")),TX(tc_format(tc_call(G("float_to_255"),{TX(tc_attr(rgba(),"r"))}))),TX(nb::str(", ")),TX(tc_format(tc_call(G("float_to_255"),{TX(tc_attr(rgba(),"g"))}))),TX(nb::str(", ")),TX(tc_format(tc_call(G("float_to_255"),{TX(tc_attr(rgba(),"b"))}))),TX(nb::str(")"))});
        return tc_join({TX(nb::str("rgba(")),TX(tc_format(tc_call(G("float_to_255"),{TX(tc_attr(rgba(),"r"))}))),TX(nb::str(", ")),TX(tc_format(tc_call(G("float_to_255"),{TX(tc_attr(rgba(),"g"))}))),TX(nb::str(", ")),TX(tc_format(tc_call(G("float_to_255"),{TX(tc_attr(rgba(),"b"))}))),TX(nb::str(", ")),TX(tc_format(tc_call(G("round"),{TX(tc_call(tc_attr(o.self,"_alpha_float"))),TX(nb::int_(2))}))),TX(nb::str(")"))});
    }
    nb::object rgb_tuple(){
        channels(cp_generator(o,0,tc_item(rgba(),tc_slice(nb::none(),nb::int_(3)))));
        bool omit=o.alpha.is_none()&&tc_attr(rgba(),"alpha").is_none();if(!omit&&!o.alpha.is_none())omit=!tc_truth(o.alpha);
        if(omit)return tc_tuple({TX(o.r),TX(o.g),TX(o.b)});return tc_tuple({TX(o.r),TX(o.g),TX(o.b),TX(tc_call(tc_attr(o.self,"_alpha_float")))});
    }
    nb::object parse_tuple(){
        if(tc_truth(tc_compare(TX(tc_call(G("len"),{TX(o.value)})),TX(nb::int_(3)),Py_EQ))){channels(cp_generator(o,1,o.value));return tc_call(G("RGBA"),{TX(o.r),TX(o.g),TX(o.b),TX(nb::none())});}
        if(tc_truth(tc_compare(TX(tc_call(G("len"),{TX(o.value)})),TX(nb::int_(4)),Py_EQ))){channels(cp_generator(o,2,tc_item(o.value,tc_slice(nb::none(),nb::int_(3)))));return tc_call(G("RGBA"),{TX(o.r),TX(o.g),TX(o.b),TX(tc_call(G("parse_float_alpha"),{TX(tc_item(o.value,nb::int_(3)))}))});}
        tc_raise(custom("value is not a valid color: tuples must have length 3 or 4"));
    }
    nb::object fullmatch(const char* name){return tc_call(tc_attr(G("re"),"fullmatch"),{TX(G(name)),TX(o.value_lower)});}
    nb::object parse_str(){
        tc_assign(o.value_lower,tc_call(tc_attr(o.value,"lower")));
        {nb::object table=G("COLORS_BY_NAME");int contains=PySequence_Contains(table.ptr(),o.value_lower.ptr());table.reset();if(contains<0)throw nb::python_error();if(contains){channels(tc_item(G("COLORS_BY_NAME"),o.value_lower));return tc_call(G("ints_to_rgba"),{TX(o.r),TX(o.g),TX(o.b),TX(nb::none())});}}
        tc_assign(o.m,fullmatch("r_hex_short"));
        if(tc_truth(o.m)){auto values=cp_unpack_star_last(tc_call(tc_attr(o.m,"groups")));tc_assign(o.rgb,std::move(values.first));tc_assign(o.a,std::move(values.second));channels(cp_generator(o,3,o.rgb));tc_assign(o.alpha,tc_truth(o.a)?tc_binary(TX(tc_call(G("int"),{TX(tc_binary(TX(o.a),TX(nb::int_(2)),PyNumber_Multiply)),TX(nb::int_(16))})),TX(nb::int_(255)),PyNumber_TrueDivide):nb::none());return tc_call(G("ints_to_rgba"),{TX(o.r),TX(o.g),TX(o.b),TX(o.alpha)});}
        tc_assign(o.m,fullmatch("r_hex_long"));
        if(tc_truth(o.m)){auto values=cp_unpack_star_last(tc_call(tc_attr(o.m,"groups")));tc_assign(o.rgb,std::move(values.first));tc_assign(o.a,std::move(values.second));channels(cp_generator(o,4,o.rgb));tc_assign(o.alpha,tc_truth(o.a)?tc_binary(TX(tc_call(G("int"),{TX(o.a),TX(nb::int_(16))})),TX(nb::int_(255)),PyNumber_TrueDivide):nb::none());return tc_call(G("ints_to_rgba"),{TX(o.r),TX(o.g),TX(o.b),TX(o.alpha)});}
        {nb::object match=fullmatch("r_rgb");if(!tc_truth(match)){match.reset();match=fullmatch("r_rgb_v4_style");}tc_assign(o.m,std::move(match));}
        if(tc_truth(o.m))return cp_expanded_call(G("ints_to_rgba"),TX(tc_call(tc_attr(o.m,"groups"))));
        {nb::object match=fullmatch("r_hsl");if(!tc_truth(match)){match.reset();match=fullmatch("r_hsl_v4_style");}tc_assign(o.m,std::move(match));}
        if(tc_truth(o.m))return cp_expanded_call(G("parse_hsl"),TX(tc_call(tc_attr(o.m,"groups"))));
        if(tc_truth(tc_compare(TX(o.value_lower),TX(nb::str("transparent")),Py_EQ)))return tc_call(G("RGBA"),{TX(nb::int_(0)),TX(nb::int_(0)),TX(nb::int_(0)),TX(nb::int_(0))});
        tc_raise(custom("value is not a valid color: string not recognised as a valid color"));
    }
    nb::object ints_to_rgba(){return tc_call(G("RGBA"),{TX(tc_call(G("parse_color_value"),{TX(o.r)})),TX(tc_call(G("parse_color_value"),{TX(o.g)})),TX(tc_call(G("parse_color_value"),{TX(o.b)})),TX(tc_call(G("parse_float_alpha"),{TX(o.alpha)}))});}
    nb::object parse_color_value(){
        try{tc_assign(o.color,tc_call(G("float"),{TX(o.value)}));}
        catch(nb::python_error& error){TCHandledException handled(error.value());nb::object types=tc_tuple({TX(G("ValueError")),TX(G("TypeError"))});if(!cp_except_match(error,std::move(types)))throw;tc_raise_cause(custom("value is not a valid color: color values must be a valid number"),error.value());}
        if(tc_truth(tc_compare(TX(nb::int_(0)),TX(o.color),Py_LE))&&tc_truth(tc_compare(TX(o.color),TX(o.max_val),Py_LE)))return tc_binary(TX(o.color),TX(o.max_val),PyNumber_TrueDivide);
        tc_raise(tc_call(G("PydanticCustomError"),{TX(nb::str("color_error")),TX(nb::str("value is not a valid color: color values must be in the range 0 to {max_val}")),[&]()->nb::object{nb::dict context;context["max_val"]=o.max_val;return context;}}));
    }
    nb::object parse_float_alpha(){
        if(o.value.is_none())return nb::none();
        try{
            if(is_instance(TX(o.value),TX(G("str")))&&tc_truth(tc_call(tc_attr(o.value,"endswith"),{TX(nb::str("%"))})))tc_assign(o.alpha,tc_binary(TX(tc_call(G("float"),{TX(tc_item(o.value,tc_slice(nb::none(),nb::int_(-1))))})),TX(nb::int_(100)),PyNumber_TrueDivide));
            else tc_assign(o.alpha,tc_call(G("float"),{TX(o.value)}));
        }catch(nb::python_error& error){TCHandledException handled(error.value());nb::object type=G("ValueError");if(!cp_except_match(error,std::move(type)))throw;tc_raise_cause(custom("value is not a valid color: alpha values must be a valid float"),error.value());}
        if(tc_truth(tc_call(tc_attr(G("math"),"isclose"),{TX(o.alpha),TX(nb::int_(1))})))return nb::none();
        if(tc_truth(tc_compare(TX(nb::int_(0)),TX(o.alpha),Py_LE))&&tc_truth(tc_compare(TX(o.alpha),TX(nb::int_(1)),Py_LE)))return o.alpha;
        tc_raise(custom("value is not a valid color: alpha values must be in the range 0 to 1"));
    }
    nb::object parse_hsl(){
        nb::object s=tc_call(G("parse_color_value"),{TX(o.sat),TX(nb::int_(100))}),l=tc_call(G("parse_color_value"),{TX(o.light),TX(nb::int_(100))});tc_assign(o.s_value,std::move(s));tc_assign(o.l_value,std::move(l));
        tc_assign(o.h_value,tc_call(G("float"),{TX(o.h)}));
        nb::object items=tc_tuple({TX(nb::none()),TX(nb::str("deg"))}),units=tc_check(PyFrozenSet_New(items.ptr()));items.reset();int contains=PySet_Contains(units.ptr(),o.h_units.ptr());units.reset();if(contains<0)throw nb::python_error();
        if(contains)tc_assign(o.h_value,tc_binary(TX(tc_binary(TX(o.h_value),TX(nb::int_(360)),PyNumber_Remainder)),TX(nb::int_(360)),PyNumber_TrueDivide));
        else if(tc_truth(tc_compare(TX(o.h_units),TX(nb::str("rad")),Py_EQ)))tc_assign(o.h_value,tc_binary(TX(tc_binary(TX(o.h_value),TX(G("rads")),PyNumber_Remainder)),TX(G("rads")),PyNumber_TrueDivide));
        else tc_assign(o.h_value,tc_binary(TX(o.h_value),TX(nb::int_(1)),PyNumber_InPlaceRemainder));
        channels(tc_call(G("hls_to_rgb"),{TX(o.h_value),TX(o.l_value),TX(o.s_value)}));return tc_call(G("RGBA"),{TX(o.r),TX(o.g),TX(o.b),TX(tc_call(G("parse_float_alpha"),{TX(o.alpha)}))});
    }
    nb::object hls(){
        if(tc_truth(tc_compare(TX(o.s),TX(nb::float_(0.0)),Py_EQ)))return tc_tuple({TX(o.l),TX(o.l),TX(o.l)});
        if(tc_truth(tc_compare(TX(o.l),TX(nb::float_(0.5)),Py_LE)))tc_assign(o.m2,tc_binary(TX(o.l),TX(tc_binary(TX(nb::float_(1.0)),TX(o.s),PyNumber_Add)),PyNumber_Multiply));
        else tc_assign(o.m2,tc_binary(TX(tc_binary(TX(o.l),TX(o.s),PyNumber_Add)),TX(tc_binary(TX(o.l),TX(o.s),PyNumber_Multiply)),PyNumber_Subtract));
        tc_assign(o.m1,tc_binary(TX(tc_binary(TX(nb::float_(2.0)),TX(o.l),PyNumber_Multiply)),TX(o.m2),PyNumber_Subtract));
        return tc_tuple({TX(tc_call(G("_v"),{TX(o.m1),TX(o.m2),TX(tc_binary(TX(o.h),TX(G("ONE_THIRD")),PyNumber_Add))})),TX(tc_call(G("_v"),{TX(o.m1),TX(o.m2),TX(o.h)})),TX(tc_call(G("_v"),{TX(o.m1),TX(o.m2),TX(tc_binary(TX(o.h),TX(G("ONE_THIRD")),PyNumber_Subtract))}))});
    }
    nb::object hls_v(){
        tc_assign(o.hue,tc_binary(TX(o.hue),TX(nb::float_(1.0)),PyNumber_Remainder));
        if(tc_truth(tc_compare(TX(o.hue),TX(G("ONE_SIXTH")),Py_LT)))return tc_binary(TX(o.m1),TX(tc_binary(TX(tc_binary(TX(tc_binary(TX(o.m2),TX(o.m1),PyNumber_Subtract)),TX(o.hue),PyNumber_Multiply)),TX(nb::float_(6.0)),PyNumber_Multiply)),PyNumber_Add);
        if(tc_truth(tc_compare(TX(o.hue),TX(nb::float_(0.5)),Py_LT)))return o.m2;
        if(tc_truth(tc_compare(TX(o.hue),TX(G("TWO_THIRD")),Py_LT)))return tc_binary(TX(o.m1),TX(tc_binary(TX(tc_binary(TX(tc_binary(TX(o.m2),TX(o.m1),PyNumber_Subtract)),TX(tc_binary(TX(G("TWO_THIRD")),TX(o.hue),PyNumber_Subtract)),PyNumber_Multiply)),TX(nb::float_(6.0)),PyNumber_Multiply)),PyNumber_Add);
        return o.m1;
    }
    nb::object run(){switch(o.entry){case 0:return color_init();case 1:return rgb();case 2:return rgb_tuple();case 3:return alpha_float();case 4:return rgba_init();case 5:return tc_item(tc_attr(o.self,"_tuple"),o.item);case 6:return parse_tuple();case 7:return parse_str();case 8:return tc_call(G("round"),{TX(tc_binary(TX(o.c),TX(nb::int_(255)),PyNumber_Multiply))});case 9:return parse_color_value();case 10:return parse_float_alpha();case 11:return ints_to_rgba();case 12:return parse_hsl();case 13:return tc_attr(o.self,"_original");case 14:return hls();case 15:return hls_v();default:throw nb::value_error("unknown Color source policy entry");}}
};

PyObject* cp_frame(PyObject*,PyObject* arguments){
    try{
        const Py_ssize_t size=PyTuple_GET_SIZE(arguments);
        if(size==1){nb::handle first(PyTuple_GET_ITEM(arguments,0));if(nb::isinstance<ColorPolicyLocals>(first)){if(!nb::inst_ready(first.ptr()))throw nb::type_error("Color policy owner was cleared");return CPPolicy(*nb::inst_ptr<ColorPolicyLocals>(first.ptr())).run().release().ptr();}if(nb::isinstance<ColorPolicyGenerator>(first))return Py_NewRef(first.ptr());throw nb::type_error("Color source owner is required");}
        if(size<2||!PyLong_CheckExact(PyTuple_GET_ITEM(arguments,0)))throw nb::type_error("Color source entry and parameters are required");
        long entry=PyLong_AsLong(PyTuple_GET_ITEM(arguments,0));if(entry==-1&&PyErr_Occurred())throw nb::python_error();if(entry<0||entry>15)throw nb::value_error("unknown Color source entry");
        const int counts[]={2,1,2,1,5,2,1,1,1,2,1,4,5,1,3,3};if(size!=counts[entry]+1)throw nb::type_error("Color source parameter declaration changed");
        PyFrameObject* frame=PyEval_GetFrame();if(!frame)throw nb::type_error("Color source frame is required");ColorPolicyLocals owner;owner.entry=static_cast<int>(entry);owner.source_globals=tc_check(PyFrame_GetGlobals(frame));owner.source_builtins=tc_check(PyFrame_GetBuiltins(frame));owner.source_code=tc_check(reinterpret_cast<PyObject*>(PyFrame_GetCode(frame)));
        PyObject* binding=PyDict_GetItemString(owner.source_globals.ptr(),"__naparixx_color_policy_binding__");if(!binding||!nb::isinstance<ColorPolicyBinding>(nb::handle(binding))||!nb::inst_ready(binding))throw nb::type_error("Color declaration binding is missing or changed");owner.binding=nb::borrow<nb::object>(binding);
        auto parameter=[&](Py_ssize_t index){return nb::borrow<nb::object>(PyTuple_GET_ITEM(arguments,index+1));};
        switch(entry){case 0:owner.self=parameter(0);owner.value=parameter(1);break;case 1:case 3:case 13:owner.self=parameter(0);break;case 2:owner.self=parameter(0);owner.alpha=parameter(1);break;case 4:owner.self=parameter(0);owner.r=parameter(1);owner.g=parameter(2);owner.b=parameter(3);owner.alpha=parameter(4);break;case 5:owner.self=parameter(0);owner.item=parameter(1);break;case 6:case 7:case 10:owner.value=parameter(0);break;case 8:owner.c=parameter(0);break;case 9:owner.value=parameter(0);owner.max_val=parameter(1);break;case 11:owner.r=parameter(0);owner.g=parameter(1);owner.b=parameter(2);owner.alpha=parameter(3);break;case 12:owner.h=parameter(0);owner.h_units=parameter(1);owner.sat=parameter(2);owner.light=parameter(3);owner.alpha=parameter(4);break;case 14:owner.h=parameter(0);owner.l=parameter(1);owner.s=parameter(2);break;case 15:owner.m1=parameter(0);owner.m2=parameter(1);owner.hue=parameter(2);break;}
        return nb::cast(std::move(owner)).release().ptr();
    }catch(nb::python_error& error){error.restore();}catch(const nb::builtin_exception& error){PyErr_SetString(error.type()==nb::exception_type::value_error?PyExc_ValueError:PyExc_TypeError,error.what());}catch(const std::bad_alloc&){PyErr_NoMemory();}catch(const std::exception& error){PyErr_SetString(PyExc_RuntimeError,error.what());}catch(...){PyErr_SetString(PyExc_SystemError,"untranslated Color source exception");}return nullptr;
}
PyMethodDef cp_frame_definition={"_color_policy_source_frame",cp_frame,METH_VARARGS,nullptr};

// Bodyless whole-module compiler profiles: immutable tuples only, no code
// objects or original application bodies retained for production execution.
bool cp_profile_code(nb::handle value,nb::handle expected);
bool cp_profile_atom(nb::handle value,nb::handle expected){
    if(!PyTuple_CheckExact(expected.ptr())||PyTuple_GET_SIZE(expected.ptr())<1)throw nb::type_error("Color compiler atom metadata changed");
    const char* tag=PyUnicode_AsUTF8(PyTuple_GET_ITEM(expected.ptr(),0));if(!tag)throw nb::python_error();
    if(std::string(tag)=="NONE")return value.ptr()==Py_None;
    if(std::string(tag)=="CODE")return cp_profile_code(value,nb::handle(PyTuple_GET_ITEM(expected.ptr(),1)));
    if(std::string(tag)=="TUPLE"){
        if(!PyTuple_CheckExact(value.ptr()))return false;PyObject* seq=PyTuple_GET_ITEM(expected.ptr(),1);if(!PyTuple_CheckExact(seq)||PyTuple_GET_SIZE(value.ptr())!=PyTuple_GET_SIZE(seq))return false;
        for(Py_ssize_t i=0;i<PyTuple_GET_SIZE(seq);++i)if(!cp_profile_atom(nb::handle(PyTuple_GET_ITEM(value.ptr(),i)),nb::handle(PyTuple_GET_ITEM(seq,i))))return false;return true;
    }
    if(std::string(tag)=="FROZENSET"){
        if(!PyFrozenSet_CheckExact(value.ptr()))return false;PyObject* seq=PyTuple_GET_ITEM(expected.ptr(),1);if(PySet_GET_SIZE(value.ptr())!=PyTuple_GET_SIZE(seq))return false;
        nb::object iterator=tc_iter(nb::borrow<nb::object>(value));while(nb::object item=tc_next(iterator)){bool found=false;for(Py_ssize_t i=0;i<PyTuple_GET_SIZE(seq);++i)if(cp_profile_atom(item,nb::handle(PyTuple_GET_ITEM(seq,i)))){found=true;break;}if(!found)return false;}return true;
    }
    PyObject* sample=PyTuple_GET_ITEM(expected.ptr(),1);
    if(std::string(tag)=="BOOL"&&!PyBool_Check(value.ptr()))return false;
    if(std::string(tag)=="INT"&&!PyLong_CheckExact(value.ptr()))return false;
    if(std::string(tag)=="FLOAT"&&!PyFloat_CheckExact(value.ptr()))return false;
    if(std::string(tag)=="STR"&&!PyUnicode_CheckExact(value.ptr()))return false;
    if(std::string(tag)=="BYTES"){
        if(!PyBytes_CheckExact(value.ptr()))return false;nb::object actual=tc_call(tc_attr(nb::borrow<nb::object>(value),"hex"));int eq=PyObject_RichCompareBool(actual.ptr(),sample,Py_EQ);if(eq<0)throw nb::python_error();return eq!=0;
    }
    int eq=PyObject_RichCompareBool(value.ptr(),sample,Py_EQ);if(eq<0)throw nb::python_error();return eq!=0;
}
bool cp_profile_code(nb::handle value,nb::handle expected){
    if(!PyCode_Check(value.ptr()))return false;if(!PyTuple_CheckExact(expected.ptr())||PyTuple_GET_SIZE(expected.ptr())!=16)throw nb::type_error("Color whole-module compiler profile changed");
    const char* fields[]={"co_argcount","co_posonlyargcount","co_kwonlyargcount","co_nlocals","co_stacksize","co_flags","co_code","co_consts","co_names","co_varnames","co_freevars","co_cellvars","co_name","co_qualname","co_firstlineno","co_exceptiontable"};
    for(Py_ssize_t i=0;i<16;++i){nb::object actual=tc_attr(nb::borrow<nb::object>(value),fields[i]);if(!cp_profile_atom(actual,nb::handle(PyTuple_GET_ITEM(expected.ptr(),i))))return false;}return true;
}
bool cp_profile_function(nb::handle function,nb::handle row){
    if(!PyFunction_Check(function.ptr()))return false;
    // Admission authenticates the reviewed code/control/signature. Mutable
    // function metadata is preserved on the same function, never pinned.
    return cp_profile_code(nb::handle(PyFunction_GetCode(function.ptr())),row);
}
bool cp_native_code(nb::handle function){
    if(!PyFunction_Check(function.ptr()))return false;auto* code=reinterpret_cast<PyCodeObject*>(PyFunction_GetCode(function.ptr()));
    for(Py_ssize_t i=0;i<PyTuple_GET_SIZE(code->co_consts);++i){PyObject* value=PyTuple_GET_ITEM(code->co_consts,i);if(PyCFunction_Check(value)&&PyCFunction_GET_FUNCTION(value)==reinterpret_cast<PyCFunction>(cp_frame)&&PyCFunction_GET_SELF(value)==nullptr)return true;}return false;
}
nb::object cp_shell_code(ColorPolicyBinding& binding,Py_ssize_t entry,nb::handle function){
    nb::object shell=tc_attr(nb::borrow<nb::object>(nb::handle(PyTuple_GET_ITEM(binding.shells.ptr(),entry))),"__code__"),constants=tc_attr(shell,"co_consts"),original=nb::borrow<nb::object>(PyFunction_GetCode(function.ptr()));
    nb::object replacements=tc_check(PyTuple_New(PyTuple_GET_SIZE(constants.ptr())));bool patched=false;
    for(Py_ssize_t i=0;i<PyTuple_GET_SIZE(constants.ptr());++i){PyObject* value=PyTuple_GET_ITEM(constants.ptr(),i);if(value==Py_Ellipsis){value=binding.frame.ptr();patched=true;}PyTuple_SET_ITEM(replacements.ptr(),i,Py_NewRef(value));}
    if(!patched)throw nb::type_error("Color declaration metadata marker is missing");
    nb::object names=tc_tuple({TX(nb::str("co_consts")),TX(nb::str("co_name")),TX(nb::str("co_qualname")),TX(nb::str("co_filename")),TX(nb::str("co_firstlineno")),TX(nb::str("co_flags"))});
    return tc_call(tc_attr(shell,"replace"),{},{TX(replacements),TX(tc_attr(original,"co_name")),TX(tc_attr(original,"co_qualname")),TX(tc_attr(original,"co_filename")),TX(tc_attr(original,"co_firstlineno")),TX(tc_attr(original,"co_flags"))},names);
}
nb::object cp_profile_rows(ColorPolicyBinding& binding){
    for(Py_ssize_t i=0;i<PyTuple_GET_SIZE(binding.profiles.ptr());++i){PyObject* row=PyTuple_GET_ITEM(binding.profiles.ptr(),i);long minor=PyLong_AsLong(PyTuple_GET_ITEM(row,0));if(minor==-1&&PyErr_Occurred())throw nb::python_error();if(minor==PY_MINOR_VERSION)return nb::borrow<nb::object>(PyTuple_GET_ITEM(row,1));}
    throw nb::value_error("Color native admission has no reviewed compiler profile for this interpreter");
}
void cc_admit(nb::object binding_object,nb::object module);
void cp_admit(nb::object binding_object,nb::object module,bool kernel){
    auto& binding=*nb::inst_ptr<ColorPolicyBinding>(binding_object.ptr());nb::object rows=cp_profile_rows(binding);nb::object globals=tc_check(PyModule_GetDict(module.ptr())?Py_NewRef(PyModule_GetDict(module.ptr())):nullptr);
    const char* paths[]={"Color.__init__","Color.as_rgb","Color.as_rgb_tuple","Color._alpha_float","RGBA.__init__","RGBA.__getitem__","parse_tuple","parse_str","float_to_255","parse_color_value","parse_float_alpha","ints_to_rgba","parse_hsl","Color.original","hls_to_rgb","_v"};
    std::vector<std::pair<nb::object,nb::object>> changes;
    for(Py_ssize_t i=kernel?14:0;i<(kernel?16:14);++i){
        std::string path=paths[i];const auto dot=path.find('.');PyObject* raw=nullptr;
        if(dot==std::string::npos)raw=PyDict_GetItemString(globals.ptr(),path.c_str());
        else{PyObject* cls=PyDict_GetItemString(globals.ptr(),path.substr(0,dot).c_str());if(cls&&PyType_Check(cls)){PyObject* dictionary=reinterpret_cast<PyTypeObject*>(cls)->tp_dict;raw=PyDict_GetItemString(dictionary,path.substr(dot+1).c_str());}}
        if(!raw){if(PyErr_Occurred())throw nb::python_error();continue;}
        nb::object function=nb::borrow<nb::object>(raw);if(cp_native_code(function))continue;
        if(!cp_profile_function(function,nb::handle(PyTuple_GET_ITEM(rows.ptr(),i))))continue;
        if(PyFunction_GetGlobals(function.ptr())!=globals.ptr())continue;
        changes.emplace_back(function,cp_shell_code(binding,i,function));
    }
    if(PyDict_SetItemString(globals.ptr(),"__naparixx_color_policy_binding__",binding_object.ptr())<0)throw nb::python_error();
    for(auto& change:changes)if(PyObject_SetAttrString(change.first.ptr(),"__code__",change.second.ptr())<0)throw nb::python_error();
    if(binding.class_owner.is_valid())cc_admit(binding.class_owner,module);
}

struct ColorPolicyAdmission {
    nb::object binding,bootstrap,search;
    ColorPolicyAdmission()=default;
    ColorPolicyAdmission(ColorPolicyAdmission&&)=default;
    void clear(){binding.reset();bootstrap.reset();search.reset();}
    ~ColorPolicyAdmission(){clear();}
};
struct ColorPolicyLoader {
    nb::object delegate,admission,spec;
    ColorPolicyLoader()=default;
    ColorPolicyLoader(ColorPolicyLoader&&)=default;
    void clear(){delegate.reset();admission.reset();spec.reset();}
    ~ColorPolicyLoader(){clear();}
};
struct ColorPolicyExec {
    bool legacy=false;
    nb::object callback,admission,wrapper;
    ColorPolicyExec()=default;
    ColorPolicyExec(ColorPolicyExec&&)=default;
    void clear(){callback.reset();admission.reset();wrapper.reset();}
    ~ColorPolicyExec(){clear();}
};
int cp_admission_traverse(PyObject* self,visitproc visit,void* arg){Py_VISIT(Py_TYPE(self));if(nb::inst_ready(self)){auto& o=*nb::inst_ptr<ColorPolicyAdmission>(self);Py_VISIT(o.binding.ptr());Py_VISIT(o.bootstrap.ptr());Py_VISIT(o.search.ptr());}return 0;}
int cp_admission_clear(PyObject* self){if(nb::inst_ready(self))nb::inst_ptr<ColorPolicyAdmission>(self)->clear();return 0;}
PyType_Slot cp_admission_slots[]={{Py_tp_traverse,reinterpret_cast<void*>(cp_admission_traverse)},{Py_tp_clear,reinterpret_cast<void*>(cp_admission_clear)},{0,nullptr}};
int cp_loader_traverse(PyObject* self,visitproc visit,void* arg){Py_VISIT(Py_TYPE(self));if(nb::inst_ready(self)){auto& o=*nb::inst_ptr<ColorPolicyLoader>(self);Py_VISIT(o.delegate.ptr());Py_VISIT(o.admission.ptr());Py_VISIT(o.spec.ptr());}return 0;}
int cp_loader_clear(PyObject* self){if(nb::inst_ready(self))nb::inst_ptr<ColorPolicyLoader>(self)->clear();return 0;}
PyType_Slot cp_loader_slots[]={{Py_tp_traverse,reinterpret_cast<void*>(cp_loader_traverse)},{Py_tp_clear,reinterpret_cast<void*>(cp_loader_clear)},{0,nullptr}};
int cp_exec_traverse(PyObject* self,visitproc visit,void* arg){Py_VISIT(Py_TYPE(self));if(nb::inst_ready(self)){auto& o=*nb::inst_ptr<ColorPolicyExec>(self);Py_VISIT(o.callback.ptr());Py_VISIT(o.admission.ptr());Py_VISIT(o.wrapper.ptr());}return 0;}
int cp_exec_clear(PyObject* self){if(nb::inst_ready(self))nb::inst_ptr<ColorPolicyExec>(self)->clear();return 0;}
PyType_Slot cp_exec_slots[]={{Py_tp_traverse,reinterpret_cast<void*>(cp_exec_traverse)},{Py_tp_clear,reinterpret_cast<void*>(cp_exec_clear)},{0,nullptr}};
// In-place Color/RGBA allocation and the remaining reviewed public policies.
// Physical member slots on the genuine provider types are the only values.
#define CC_FIELDS(F) \
F(source_globals) F(source_builtins) F(source_code) F(source_literals) F(binding) F(self) F(cls) \
F(core_schema) F(source) F(handler) F(input_value) F(info) F(fallback) F(format) \
F(alpha) F(other) F(object) F(join_str) F(fmt) F(kwargs) F(r) F(g) F(b) F(h) \
F(l) F(s) F(li) F(a) F(field_schema) F(rgb) F(values) F(hex_cell) F(c) F(v) \
F(attrs_names) F(attrs) F(name) F(field_repr) F(maxc) F(minc) F(sumc) F(rangec) \
F(rc) F(gc) F(bc)
struct ColorClassLocals {
    int entry=-1;
#define CC_DECLARE(name) nb::object name;
    CC_FIELDS(CC_DECLARE)
#undef CC_DECLARE
    ColorClassLocals()=default;ColorClassLocals(ColorClassLocals&&)=default;
    void clear(){
#define CC_CLEAR(name) name.reset();
        CC_FIELDS(CC_CLEAR)
#undef CC_CLEAR
    }
    ~ColorClassLocals(){clear();}
};
struct ColorClassBinding {
    nb::object frame,shells,generator_shell,profiles;
    ColorClassBinding()=default;ColorClassBinding(ColorClassBinding&&)=default;
    void clear(){frame.reset();shells.reset();generator_shell.reset();profiles.reset();}
    ~ColorClassBinding(){clear();}
};
PyObject* cc_allocate(PyTypeObject* type,Py_ssize_t nitems);
struct ColorClassAllocation {
    nb::object class_ref;
    allocfunc delegate=nullptr;
    size_t allocations=0;
    ColorClassAllocation()=default;ColorClassAllocation(ColorClassAllocation&&)=default;
    void clear(){if(class_ref.is_valid()){PyObject* type=PyWeakref_GetObject(class_ref.ptr());if(type&&type!=Py_None&&PyType_Check(type)&&reinterpret_cast<PyTypeObject*>(type)->tp_alloc==cc_allocate&&delegate)reinterpret_cast<PyTypeObject*>(type)->tp_alloc=delegate;}class_ref.reset();delegate=nullptr;}
    ~ColorClassAllocation(){clear();}
};
struct ColorClassGenerator {
    int entry=-1,state=0;
    bool exhausted=false;
    nb::object source_globals,source_builtins,source_literals,iterator,self,fmt,kwargs,cell,c,v,a,name,field_repr;
    ColorClassGenerator()=default;ColorClassGenerator(ColorClassGenerator&&)=default;
    void clear(){source_globals.reset();source_builtins.reset();source_literals.reset();iterator.reset();self.reset();fmt.reset();kwargs.reset();cell.reset();c.reset();v.reset();a.reset();name.reset();field_repr.reset();}
    ~ColorClassGenerator(){clear();}
};
int cc_owner_traverse(PyObject* self,visitproc visit,void* arg){Py_VISIT(Py_TYPE(self));if(nb::inst_ready(self)){auto& o=*nb::inst_ptr<ColorClassLocals>(self);
#define CC_VISIT(name) Py_VISIT(o.name.ptr());
CC_FIELDS(CC_VISIT)
#undef CC_VISIT
}return 0;}
int cc_owner_clear(PyObject* self){if(nb::inst_ready(self))nb::inst_ptr<ColorClassLocals>(self)->clear();return 0;}
int cc_binding_traverse(PyObject* self,visitproc visit,void* arg){Py_VISIT(Py_TYPE(self));if(nb::inst_ready(self)){auto& o=*nb::inst_ptr<ColorClassBinding>(self);Py_VISIT(o.frame.ptr());Py_VISIT(o.shells.ptr());Py_VISIT(o.generator_shell.ptr());Py_VISIT(o.profiles.ptr());}return 0;}
int cc_binding_clear(PyObject* self){if(nb::inst_ready(self))nb::inst_ptr<ColorClassBinding>(self)->clear();return 0;}
int cc_allocation_traverse(PyObject* self,visitproc visit,void* arg){Py_VISIT(Py_TYPE(self));if(nb::inst_ready(self))Py_VISIT(nb::inst_ptr<ColorClassAllocation>(self)->class_ref.ptr());return 0;}
int cc_allocation_clear(PyObject* self){if(nb::inst_ready(self))nb::inst_ptr<ColorClassAllocation>(self)->clear();return 0;}
int cc_generator_traverse(PyObject* self,visitproc visit,void* arg){Py_VISIT(Py_TYPE(self));if(nb::inst_ready(self)){auto& o=*nb::inst_ptr<ColorClassGenerator>(self);
#define CC_GEN_VISIT(name) Py_VISIT(o.name.ptr());
CC_GEN_VISIT(source_globals) CC_GEN_VISIT(source_builtins) CC_GEN_VISIT(source_literals) CC_GEN_VISIT(iterator) CC_GEN_VISIT(self) CC_GEN_VISIT(fmt) CC_GEN_VISIT(kwargs) CC_GEN_VISIT(cell) CC_GEN_VISIT(c) CC_GEN_VISIT(v) CC_GEN_VISIT(a) CC_GEN_VISIT(name) CC_GEN_VISIT(field_repr)
#undef CC_GEN_VISIT
}return 0;}
int cc_generator_clear(PyObject* self){if(nb::inst_ready(self))nb::inst_ptr<ColorClassGenerator>(self)->clear();return 0;}
PyType_Slot cc_owner_slots[]={{Py_tp_traverse,reinterpret_cast<void*>(cc_owner_traverse)},{Py_tp_clear,reinterpret_cast<void*>(cc_owner_clear)},{0,nullptr}};
PyType_Slot cc_binding_slots[]={{Py_tp_traverse,reinterpret_cast<void*>(cc_binding_traverse)},{Py_tp_clear,reinterpret_cast<void*>(cc_binding_clear)},{0,nullptr}};
PyType_Slot cc_allocation_slots[]={{Py_tp_traverse,reinterpret_cast<void*>(cc_allocation_traverse)},{Py_tp_clear,reinterpret_cast<void*>(cc_allocation_clear)},{0,nullptr}};
PyType_Slot cc_generator_slots[]={{Py_tp_traverse,reinterpret_cast<void*>(cc_generator_traverse)},{Py_tp_clear,reinterpret_cast<void*>(cc_generator_clear)},{0,nullptr}};

// Immutable declaration metadata retains the genuine literal objects. It does
// not retain any original executable CodeType or source function/body.
nb::object cc_literal_metadata(PyCodeObject* code){
    PyObject* constants=code->co_consts;nb::object values=tc_check(PyTuple_New(PyTuple_GET_SIZE(constants))),children=tc_check(PyTuple_New(PyTuple_GET_SIZE(constants)));
    for(Py_ssize_t i=0;i<PyTuple_GET_SIZE(constants);++i){PyObject* value=PyTuple_GET_ITEM(constants,i);if(PyCode_Check(value)){auto* child=reinterpret_cast<PyCodeObject*>(value);PyTuple_SET_ITEM(values.ptr(),i,Py_NewRef(Py_None));nb::object metadata=cc_literal_metadata(child);nb::object record=tc_tuple({TX(nb::borrow<nb::object>(child->co_name)),TX(nb::int_(child->co_firstlineno)),TX(metadata)});PyTuple_SET_ITEM(children.ptr(),i,record.release().ptr());}else{PyTuple_SET_ITEM(values.ptr(),i,Py_NewRef(value));PyTuple_SET_ITEM(children.ptr(),i,Py_NewRef(Py_None));}}
    return tc_tuple({TX(values),TX(children)});
}
PyObject* cc_literal_values(nb::handle metadata){if(!PyTuple_CheckExact(metadata.ptr())||PyTuple_GET_SIZE(metadata.ptr())!=2)throw nb::type_error("Color source literal declaration metadata changed");PyObject* values=PyTuple_GET_ITEM(metadata.ptr(),0);if(!PyTuple_CheckExact(values))throw nb::type_error("Color source literals must be the captured tuple");return values;}
nb::object cc_child_literals(nb::handle metadata,int line){PyObject* children=PyTuple_GET_ITEM(metadata.ptr(),1);if(!PyTuple_CheckExact(children))throw nb::type_error("Color nested literal metadata changed");for(Py_ssize_t i=0;i<PyTuple_GET_SIZE(children);++i){PyObject* row=PyTuple_GET_ITEM(children,i);if(row!=Py_None&&PyTuple_CheckExact(row)&&PyTuple_GET_SIZE(row)==3&&PyUnicode_CompareWithASCIIString(PyTuple_GET_ITEM(row,0),"<genexpr>")==0&&PyLong_AsLong(PyTuple_GET_ITEM(row,1))==line)return nb::borrow<nb::object>(PyTuple_GET_ITEM(row,2));}if(PyErr_Occurred())throw nb::python_error();throw nb::type_error("Color genuine nested literal metadata is missing");}
nb::object cc_string(nb::handle metadata,const char* text){PyObject* values=cc_literal_values(metadata);for(Py_ssize_t i=0;i<PyTuple_GET_SIZE(values);++i){PyObject* v=PyTuple_GET_ITEM(values,i);if(PyUnicode_CheckExact(v)&&PyUnicode_CompareWithASCIIString(v,text)==0)return nb::borrow<nb::object>(v);}throw nb::type_error("Color genuine string constant is missing");}
nb::object cc_integer(nb::handle metadata,long value){PyObject* values=cc_literal_values(metadata);for(Py_ssize_t i=0;i<PyTuple_GET_SIZE(values);++i){PyObject* v=PyTuple_GET_ITEM(values,i);if(PyLong_CheckExact(v)&&PyLong_AsLong(v)==value)return nb::borrow<nb::object>(v);}if(PyErr_Occurred())throw nb::python_error();throw nb::type_error("Color genuine integer constant is missing");}
nb::object cc_floating(nb::handle metadata,double value){PyObject* values=cc_literal_values(metadata);for(Py_ssize_t i=0;i<PyTuple_GET_SIZE(values);++i){PyObject* v=PyTuple_GET_ITEM(values,i);if(PyFloat_CheckExact(v)&&PyFloat_AS_DOUBLE(v)==value)return nb::borrow<nb::object>(v);}throw nb::type_error("Color genuine float constant is missing");}
nb::object cc_keywords(nb::handle metadata,std::initializer_list<const char*> names){PyObject* values=cc_literal_values(metadata);for(Py_ssize_t i=0;i<PyTuple_GET_SIZE(values);++i){PyObject* v=PyTuple_GET_ITEM(values,i);if(!PyTuple_CheckExact(v)||PyTuple_GET_SIZE(v)!=static_cast<Py_ssize_t>(names.size()))continue;size_t j=0;bool match=true;for(const char* name:names){PyObject* word=PyTuple_GET_ITEM(v,j++);if(!PyUnicode_CheckExact(word)||PyUnicode_CompareWithASCIIString(word,name)!=0){match=false;break;}}if(match)return nb::borrow<nb::object>(v);}throw nb::type_error("Color genuine keyword tuple is missing");}
nb::object cc_formatted(nb::object value,nb::object format){PyObject* result=PyObject_Format(value.ptr(),format.ptr());value.reset();format.reset();return tc_check(result);}
nb::object cc_repr(nb::object value){PyObject* result=PyObject_Repr(value.ptr());value.reset();return tc_check(result);}
std::array<nb::object,4> cc_unpack_four(nb::object value){PyObject* raw=PyObject_GetIter(value.ptr());if(!raw){nb::python_error error;if(error.matches(PyExc_TypeError)&&Py_TYPE(value.ptr())->tp_iter==nullptr&&!PySequence_Check(value.ptr())){PyErr_Format(PyExc_TypeError,"cannot unpack non-iterable %.200s object",Py_TYPE(value.ptr())->tp_name);throw nb::python_error();}throw error;}nb::object iterator=nb::steal<nb::object>(raw);std::array<nb::object,4> result;for(size_t i=0;i<4;++i){result[i]=tc_next(iterator);if(!result[i].is_valid()){PyErr_Format(PyExc_ValueError,"not enough values to unpack (expected 4, got %zu)",i);throw nb::python_error();}}nb::object extra=tc_next(iterator);if(extra.is_valid()){PyErr_SetString(PyExc_ValueError,"too many values to unpack (expected 4)");throw nb::python_error();}iterator.reset();value.reset();return result;}
bool cc_contains(nb::object table,nb::object value){int result=PySequence_Contains(table.ptr(),value.ptr());table.reset();value.reset();if(result<0)throw nb::python_error();return result!=0;}
nb::object cc_cell(nb::handle cell){PyObject* value=PyCell_Get(cell.ptr());if(!value){if(PyErr_Occurred())throw nb::python_error();PyErr_SetString(PyExc_NameError,"cannot access free variable 'as_hex' where it is not associated with a value in enclosing scope");throw nb::python_error();}return nb::steal<nb::object>(value);}
void cc_store_cell(nb::handle cell,nb::object value){int result=PyCell_Set(cell.ptr(),value.ptr());value.reset();if(result<0)throw nb::python_error();}

nb::object cc_generator_next(ColorClassGenerator& o){
    if(o.exhausted)throw nb::stop_iteration();
    auto G=[&](const char* name){return cp_global(o.source_globals,o.source_builtins,name);};
    auto L=[&](const char* text){return cc_string(o.source_literals,text);};
    auto I=[&](long value){return cc_integer(o.source_literals,value);};
    try{
        if(o.entry==5){
            for(;;){switch(o.state){
            case 0:o.state=1;return tc_binary(TX(tc_call(tc_attr(o.self,"__repr_name__"))),TX(L("(")),PyNumber_Add);
            case 1:o.state=2;return I(1);
            case 2:o.iterator=tc_iter(tc_call(tc_attr(o.self,"__repr_args__")));o.state=3;continue;
            case 3:{nb::object item=tc_next(o.iterator);if(!item.is_valid()){o.state=7;continue;}auto pair=tc_unpack_pair(std::move(item));tc_assign(o.name,std::move(pair[0]));tc_assign(o.field_repr,std::move(pair[1]));o.state=4;if(!o.name.is_none())return tc_binary(TX(o.name),TX(L("=")),PyNumber_Add);continue;}
            case 4:o.state=5;return tc_call(o.fmt,{TX(o.field_repr)});
            case 5:o.state=6;return L(",");
            case 6:o.state=3;return I(0);
            case 7:o.state=8;return I(-1);
            case 8:o.state=9;return L(")");
            default:o.exhausted=true;o.clear();throw nb::stop_iteration();
            }}
        }
        if(o.entry==6){
            if(!o.iterator.is_valid())o.iterator=tc_iter(tc_call(tc_attr(o.self,"__repr_args__")));
            nb::object item=tc_next(o.iterator);if(!item.is_valid()){o.exhausted=true;o.clear();throw nb::stop_iteration();}
            auto pair=tc_unpack_pair(std::move(item));tc_assign(o.name,std::move(pair[0]));tc_assign(o.field_repr,std::move(pair[1]));
            return o.name.is_none()?o.field_repr:nb::object(tc_tuple({TX(o.name),TX(o.field_repr)}));
        }
        nb::object next=tc_next(o.iterator);if(!next.is_valid()){o.exhausted=true;o.clear();throw nb::stop_iteration();}
        if(o.entry==0){tc_assign(o.v,std::move(next));return cc_formatted(o.v,L("02x"));}
        if(o.entry==1){tc_assign(o.c,std::move(next));return nb::bool_(cc_contains(G("repeat_colors"),o.c));}
        if(o.entry==2){tc_assign(o.c,std::move(next));return tc_item(cc_cell(o.cell),o.c);}
        if(o.entry==3){tc_assign(o.c,std::move(next));return tc_tuple({TX(o.c),TX(tc_call(G("getattr"),{TX(o.self),TX(o.c)}))});}
        if(o.entry==4){auto pair=tc_unpack_pair(std::move(next));tc_assign(o.a,std::move(pair[0]));tc_assign(o.v,std::move(pair[1]));if(o.a.is_none())return tc_call(G("repr"),{TX(o.v)});return tc_join({TX(tc_format(o.a)),TX(L("=")),TX(cc_repr(o.v))});}
        throw nb::value_error("unknown Color class generator source entry");
    }catch(nb::python_error& error){o.exhausted=true;if(error.matches(PyExc_StopIteration)){TCHandledException handled(error.value());nb::object failure=tc_call(nb::borrow<nb::object>(PyExc_RuntimeError),{TX(nb::str("generator raised StopIteration"))});PyException_SetContext(failure.ptr(),error.value().inc_ref().ptr());PyException_SetCause(failure.ptr(),error.value().inc_ref().ptr());tc_raise(std::move(failure));}throw;}
}

nb::object cc_generator(ColorClassLocals& o,int entry,nb::object iterable={}){
    auto& b=*nb::inst_ptr<ColorClassBinding>(o.binding.ptr());nb::object original=o.source_code;
    nb::object code=tc_attr(b.generator_shell,"__code__"),constants=tc_attr(code,"co_consts");
    nb::object replacements=tc_check(PyTuple_New(PyTuple_GET_SIZE(constants.ptr())));
    for(Py_ssize_t i=0;i<PyTuple_GET_SIZE(constants.ptr());++i){PyObject* value=PyTuple_GET_ITEM(constants.ptr(),i);PyTuple_SET_ITEM(replacements.ptr(),i,Py_NewRef(value==Py_Ellipsis?b.frame.ptr():value));}
    nb::dict kwargs;kwargs["co_consts"]=replacements;kwargs["co_filename"]=tc_attr(original,"co_filename");
    const int lines[]={141,142,143,47,60,62,75};
    kwargs["co_firstlineno"]=nb::int_(lines[entry]);kwargs["co_name"]=nb::str(entry<5?"<genexpr>":entry==5?"__pretty__":"__rich_repr__");
    kwargs["co_qualname"]=tc_attr(original,"co_qualname");nb::object arguments=tc_check(PyTuple_New(0));
    code=tc_check(PyObject_Call(tc_attr(code,"replace").ptr(),arguments.ptr(),kwargs.ptr()));
    nb::object function=tc_check(PyFunction_New(code.ptr(),o.source_globals.ptr()));
    ColorClassGenerator generator;generator.entry=entry;generator.source_globals=o.source_globals;generator.source_literals=entry<5?cc_child_literals(o.source_literals,lines[entry]):o.source_literals;
    generator.source_builtins=entry>=5?o.source_builtins:tc_attr(function,"__builtins__");
    if(entry<5)generator.iterator=tc_iter(std::move(iterable));
    if(entry==2)generator.cell=o.hex_cell;
    if(entry>=3)generator.self=o.self;
    if(entry==5){generator.fmt=o.fmt;generator.kwargs=o.kwargs;}
    nb::object owner=nb::cast(std::move(generator));return tc_call(std::move(function),{TX(owner)});
}

struct CCPolicy {
    ColorClassLocals& o;explicit CCPolicy(ColorClassLocals& owner):o(owner){}
    nb::object G(const char* name){return cp_global(o.source_globals,o.source_builtins,name);}
    nb::object L(const char* text){return cc_string(o.source_literals,text);}
    nb::object I(long value){return cc_integer(o.source_literals,value);}
    nb::object F(double value){return cc_floating(o.source_literals,value);}
    nb::object KW(std::initializer_list<const char*> names){return cc_keywords(o.source_literals,names);}
    nb::object rgba(){return tc_attr(o.self,"_rgba");}
    nb::object K(const char* name,nb::object value){nb::object names=KW({name});return tc_call(tc_attr(o.self,"as_named"),{},{TX(value)},std::move(names));}
    void hsl_channels(nb::object value,bool alpha){
        if(alpha){auto first=cc_unpack_four(std::move(value));tc_assign(o.h,std::move(first[0]));tc_assign(o.s,std::move(first[1]));tc_assign(o.li,std::move(first[2]));tc_assign(o.a,std::move(first[3]));}
        else{auto first=tt_unpack_three(std::move(value));tc_assign(o.h,std::move(first[0]));tc_assign(o.s,std::move(first[1]));tc_assign(o.li,std::move(first[2]));}
    }
    nb::object named(){
        if(!tc_attr(rgba(),"alpha").is_none())return tc_call(tc_attr(o.self,"as_hex"));
        tc_assign(o.rgb,tc_call(G("cast"),{TX(L("tuple[int, int, int]")),TX(tc_call(tc_attr(o.self,"as_rgb_tuple")))}));
        if(cc_contains(G("COLORS_BY_VALUE"),o.rgb))return tc_item(G("COLORS_BY_VALUE"),o.rgb);
        if(tc_truth(o.fallback))return tc_call(tc_attr(o.self,"as_hex"));
        tc_raise(tc_call(G("ValueError"),{TX(L("no named color found, use fallback=True, as_hex() or as_rgb()"))}));
    }
    nb::object hex(){
        nb::object iterator=tc_iter(tc_item(rgba(),tc_slice(nb::none(),I(3)))),items=tc_check(PyList_New(0));
        try{for(;;){nb::object next=tc_next(iterator);if(!next.is_valid())break;tc_assign(o.c,std::move(next));nb::object value=tc_call(G("float_to_255"),{TX(o.c)});if(PyList_Append(items.ptr(),value.ptr())<0)throw nb::python_error();}}
        catch(...){iterator.reset();items.reset();o.c.reset();throw;}
        iterator.reset();tc_assign(o.values,std::move(items));o.c.reset();
        if(!tc_attr(rgba(),"alpha").is_none())tc_call(tc_attr(o.values,"append"),{TX(tc_call(G("float_to_255"),{TX(tc_attr(rgba(),"alpha"))}))}).reset();
        cc_store_cell(o.hex_cell,tc_call(tc_attr(L(""),"join"),{TX(cc_generator(o,0,o.values))}));
        if(tc_truth(tc_compare(TX(o.format),TX(L("short")),Py_EQ))&&tc_truth(tc_call(G("all"),{TX(cc_generator(o,1,o.values))}))){
            nb::object join=tc_attr(L(""),"join");
            nb::object range=tc_call(G("range"),{TX(I(0)),TX(tc_call(G("len"),{TX(cc_cell(o.hex_cell))})),TX(I(2))});
            cc_store_cell(o.hex_cell,tc_call(std::move(join),{TX(cc_generator(o,2,std::move(range)))}));
        }
        return tc_join({TX(L("#")),TX(tc_format(cc_cell(o.hex_cell)))});
    }
    nb::object hsl(){
        bool alpha=!tc_attr(rgba(),"alpha").is_none();
        hsl_channels(tc_call(tc_attr(o.self,"as_hsl_tuple"),{},{TX(nb::bool_(alpha))},KW({"alpha"})),alpha);
        if(!alpha)return tc_join({TX(L("hsl(")),TX(cc_formatted(tc_binary(TX(o.h),TX(I(360)),PyNumber_Multiply),L("0.0f"))),TX(L(", ")),TX(cc_formatted(o.s,L("0.0%"))),TX(L(", ")),TX(cc_formatted(o.li,L("0.0%"))),TX(L(")"))});
        return tc_join({TX(L("hsl(")),TX(cc_formatted(tc_binary(TX(o.h),TX(I(360)),PyNumber_Multiply),L("0.0f"))),TX(L(", ")),TX(cc_formatted(o.s,L("0.0%"))),TX(L(", ")),TX(cc_formatted(o.li,L("0.0%"))),TX(L(", ")),TX(tc_format(tc_call(G("round"),{TX(o.a),TX(I(2))}))),TX(L(")"))});
    }
    nb::object hsl_tuple(){
        auto parts=tt_unpack_three(tc_call(G("rgb_to_hls"),{TX(tc_attr(rgba(),"r")),TX(tc_attr(rgba(),"g")),TX(tc_attr(rgba(),"b"))}));
        tc_assign(o.h,std::move(parts[0]));tc_assign(o.l,std::move(parts[1]));tc_assign(o.s,std::move(parts[2]));
        if(o.alpha.is_none()){if(tc_attr(rgba(),"alpha").is_none())return tc_tuple({TX(o.h),TX(o.s),TX(o.l)});return tc_tuple({TX(o.h),TX(o.s),TX(o.l),TX(tc_call(tc_attr(o.self,"_alpha_float")))});}
        if(tc_truth(o.alpha))return tc_tuple({TX(o.h),TX(o.s),TX(o.l),TX(tc_call(tc_attr(o.self,"_alpha_float")))});
        return tc_tuple({TX(o.h),TX(o.s),TX(o.l)});
    }
    nb::object repr_args(){
        tc_assign(o.attrs_names,tc_call(G("cast"),{TX(tc_item(G("Collection"),G("str"))),TX(tc_attr(o.self,"__slots__"))}));
        if(!tc_truth(o.attrs_names)&&tc_truth(tc_call(G("hasattr"),{TX(o.self),TX(L("__dict__"))})))tc_assign(o.attrs_names,tc_call(tc_attr(tc_attr(o.self,"__dict__"),"keys")));
        tc_assign(o.attrs,cc_generator(o,3,o.attrs_names));
        nb::object iterator=tc_iter(o.attrs),items=tc_check(PyList_New(0));
        try{for(;;){nb::object next=tc_next(iterator);if(!next.is_valid())break;auto pair=tc_unpack_pair(std::move(next));tc_assign(o.a,std::move(pair[0]));tc_assign(o.v,std::move(pair[1]));if(!o.v.is_none()){nb::object value=tc_tuple({TX(o.a),TX(o.v.ptr()!=o.self.ptr()?o.v:tc_call(tc_attr(o.self,"__repr_recursion__"),{TX(o.v)}))});if(PyList_Append(items.ptr(),value.ptr())<0)throw nb::python_error();}}}
        catch(...){iterator.reset();items.reset();o.v.reset();o.a.reset();throw;}
        iterator.reset();nb::object result=std::move(items);o.a.reset();o.v.reset();return result;
    }
    nb::object rgb_hls(){
        tc_assign(o.maxc,tc_call(G("max"),{TX(o.r),TX(o.g),TX(o.b)}));tc_assign(o.minc,tc_call(G("min"),{TX(o.r),TX(o.g),TX(o.b)}));
#if PY_VERSION_HEX >= 0x030C0000
        tc_assign(o.sumc,tc_binary(TX(o.maxc),TX(o.minc),PyNumber_Add));tc_assign(o.rangec,tc_binary(TX(o.maxc),TX(o.minc),PyNumber_Subtract));tc_assign(o.l,tc_binary(TX(o.sumc),TX(F(2.0)),PyNumber_TrueDivide));
#else
        tc_assign(o.l,tc_binary(TX(tc_binary(TX(o.minc),TX(o.maxc),PyNumber_Add)),TX(F(2.0)),PyNumber_TrueDivide));
#endif
        if(tc_truth(tc_compare(TX(o.minc),TX(o.maxc),Py_EQ)))return tc_tuple({TX(F(0.0)),TX(o.l),TX(F(0.0))});
#if PY_VERSION_HEX >= 0x030C0000
        tc_assign(o.s,tc_truth(tc_compare(TX(o.l),TX(F(0.5)),Py_LE))?tc_binary(TX(o.rangec),TX(o.sumc),PyNumber_TrueDivide):tc_binary(TX(o.rangec),TX(tc_binary(TX(tc_binary(TX(F(2.0)),TX(o.maxc),PyNumber_Subtract)),TX(o.minc),PyNumber_Subtract)),PyNumber_TrueDivide));
        tc_assign(o.rc,tc_binary(TX(tc_binary(TX(o.maxc),TX(o.r),PyNumber_Subtract)),TX(o.rangec),PyNumber_TrueDivide));tc_assign(o.gc,tc_binary(TX(tc_binary(TX(o.maxc),TX(o.g),PyNumber_Subtract)),TX(o.rangec),PyNumber_TrueDivide));tc_assign(o.bc,tc_binary(TX(tc_binary(TX(o.maxc),TX(o.b),PyNumber_Subtract)),TX(o.rangec),PyNumber_TrueDivide));
#else
        tc_assign(o.s,tc_truth(tc_compare(TX(o.l),TX(F(0.5)),Py_LE))?tc_binary(TX(tc_binary(TX(o.maxc),TX(o.minc),PyNumber_Subtract)),TX(tc_binary(TX(o.maxc),TX(o.minc),PyNumber_Add)),PyNumber_TrueDivide):tc_binary(TX(tc_binary(TX(o.maxc),TX(o.minc),PyNumber_Subtract)),TX(tc_binary(TX(tc_binary(TX(F(2.0)),TX(o.maxc),PyNumber_Subtract)),TX(o.minc),PyNumber_Subtract)),PyNumber_TrueDivide));
        tc_assign(o.rc,tc_binary(TX(tc_binary(TX(o.maxc),TX(o.r),PyNumber_Subtract)),TX(tc_binary(TX(o.maxc),TX(o.minc),PyNumber_Subtract)),PyNumber_TrueDivide));tc_assign(o.gc,tc_binary(TX(tc_binary(TX(o.maxc),TX(o.g),PyNumber_Subtract)),TX(tc_binary(TX(o.maxc),TX(o.minc),PyNumber_Subtract)),PyNumber_TrueDivide));tc_assign(o.bc,tc_binary(TX(tc_binary(TX(o.maxc),TX(o.b),PyNumber_Subtract)),TX(tc_binary(TX(o.maxc),TX(o.minc),PyNumber_Subtract)),PyNumber_TrueDivide));
#endif
        if(tc_truth(tc_compare(TX(o.r),TX(o.maxc),Py_EQ)))tc_assign(o.h,tc_binary(TX(o.bc),TX(o.gc),PyNumber_Subtract));
        else if(tc_truth(tc_compare(TX(o.g),TX(o.maxc),Py_EQ)))tc_assign(o.h,tc_binary(TX(tc_binary(TX(F(2.0)),TX(o.rc),PyNumber_Add)),TX(o.bc),PyNumber_Subtract));
        else tc_assign(o.h,tc_binary(TX(tc_binary(TX(F(4.0)),TX(o.gc),PyNumber_Add)),TX(o.rc),PyNumber_Subtract));
        tc_assign(o.h,tc_binary(TX(tc_binary(TX(o.h),TX(F(6.0)),PyNumber_TrueDivide)),TX(F(1.0)),PyNumber_Remainder));return tc_tuple({TX(o.h),TX(o.l),TX(o.s)});
    }
    nb::object run(){switch(o.entry){
    case 0:tc_assign(o.field_schema,tc_check(PyDict_New()));tc_call(tc_attr(o.field_schema,"update"),{},{TX(L("string")),TX(L("color"))},KW({"type","format"})).reset();return o.field_schema;
    case 1:return named();case 2:return hex();case 3:return hsl();case 4:return hsl_tuple();
    case 5:return tc_call(tc_attr(G("core_schema"),"with_info_plain_validator_function"),{TX(tc_attr(o.cls,"_validate"))},{TX(tc_call(tc_attr(G("core_schema"),"to_string_ser_schema")))},KW({"serialization"}));
    case 6:return tc_call(o.cls,{TX(o.input_value)});case 7:return K("fallback",nb::bool_(true));
    case 8:return tc_binary(TX(tc_list({TX(tc_tuple({TX(nb::none()),TX(K("fallback",nb::bool_(true)))}))})),TX(tc_list({TX(tc_tuple({TX(L("rgb")),TX(tc_call(tc_attr(o.self,"as_rgb_tuple")))}))})),PyNumber_Add);
    case 9:{nb::object test=tc_call(G("isinstance"),{TX(o.other),TX(G("Color"))});if(!tc_truth(test))return test;test.reset();return tc_compare(TX(tc_call(tc_attr(o.self,"as_rgb_tuple"))),TX(tc_call(tc_attr(o.other,"as_rgb_tuple"))),Py_EQ);}
    case 10:return tc_call(G("hash"),{TX(tc_call(tc_attr(o.self,"as_rgb_tuple")))});
    case 11:return repr_args();case 12:return tc_attr(tc_attr(o.self,"__class__"),"__name__");
    case 13:return tc_join({TX(L("<Recursion on ")),TX(tc_format(tc_attr(tc_call(G("type"),{TX(o.object)}),"__name__"))),TX(L(" with id=")),TX(tc_format(tc_call(G("id"),{TX(o.object)}))),TX(L(">"))});
    case 14:return tc_call(tc_attr(o.join_str,"join"),{TX(cc_generator(o,4,tc_call(tc_attr(o.self,"__repr_args__"))))});
    case 15:return cc_generator(o,5);case 16:return cc_generator(o,6);
    case 17:return tc_call(tc_attr(o.self,"__repr_str__"),{TX(L(" "))});
    case 18:return tc_join({TX(tc_format(tc_call(tc_attr(o.self,"__repr_name__")))),TX(L("(")),TX(tc_format(tc_call(tc_attr(o.self,"__repr_str__"),{TX(L(", "))}))),TX(L(")"))});
    case 19:return tc_call(G("str"),{TX(o.self)});case 20:return rgb_hls();
    default:throw nb::value_error("unknown Color class source policy entry");}}
};

PyObject* cc_frame(PyObject*,PyObject* arguments){
    try{
        Py_ssize_t size=PyTuple_GET_SIZE(arguments);
        if(size==1){nb::handle value(PyTuple_GET_ITEM(arguments,0));if(nb::isinstance<ColorClassLocals>(value)&&nb::inst_ready(value.ptr()))return CCPolicy(*nb::inst_ptr<ColorClassLocals>(value.ptr())).run().release().ptr();if(nb::isinstance<ColorClassGenerator>(value)&&nb::inst_ready(value.ptr()))return Py_NewRef(value.ptr());throw nb::type_error("Color class source owner is required");}
        if(size<2||!PyLong_CheckExact(PyTuple_GET_ITEM(arguments,0)))throw nb::type_error("Color class source entry and actual parameters are required");
        long entry=PyLong_AsLong(PyTuple_GET_ITEM(arguments,0));if(entry==-1&&PyErr_Occurred())throw nb::python_error();if(entry<0||entry>20)throw nb::value_error("unknown Color class source entry");
        const int counts[]={3,2,2,1,2,3,3,1,1,2,1,1,1,2,2,3,1,1,1,1,3};if(size!=counts[entry]+1)throw nb::type_error("Color class source parameter declaration changed");
        PyFrameObject* frame=PyEval_GetFrame();if(!frame)throw nb::type_error("Color class source frame is required");
        ColorClassLocals o;o.entry=static_cast<int>(entry);o.source_globals=tc_check(PyFrame_GetGlobals(frame));o.source_builtins=tc_check(PyFrame_GetBuiltins(frame));o.source_code=tc_check(reinterpret_cast<PyObject*>(PyFrame_GetCode(frame)));nb::object literals=tc_attr(o.source_code,"co_consts");o.source_literals=nb::borrow<nb::object>(PyTuple_GET_ITEM(literals.ptr(),PyTuple_GET_SIZE(literals.ptr())-1));cc_literal_values(o.source_literals);
        PyObject* binding=PyDict_GetItemString(o.source_globals.ptr(),"__naparixx_color_class_binding__");if(!binding||!nb::isinstance<ColorClassBinding>(nb::handle(binding))||!nb::inst_ready(binding))throw nb::type_error("Color class declaration binding is missing or changed");o.binding=nb::borrow<nb::object>(binding);
        auto p=[&](Py_ssize_t i){return nb::borrow<nb::object>(PyTuple_GET_ITEM(arguments,i+1));};
        switch(entry){
        case 0:o.cls=p(0);o.core_schema=p(1);o.handler=p(2);break;
        case 1:o.self=p(0);o.fallback=p(1);break;
        case 2:o.self=p(0);o.format=p(1);o.hex_cell=tc_check(PyCell_New(nullptr));break;
        case 4:o.self=p(0);o.alpha=p(1);break;
        case 5:o.cls=p(0);o.source=p(1);o.handler=p(2);break;
        case 6:o.cls=p(0);o.input_value=p(1);o.info=p(2);break;
        case 9:o.self=p(0);o.other=p(1);break;
        case 13:o.self=p(0);o.object=p(1);break;
        case 14:o.self=p(0);o.join_str=p(1);break;
        case 15:o.self=p(0);o.fmt=p(1);o.kwargs=p(2);break;
        case 20:o.r=p(0);o.g=p(1);o.b=p(2);break;
        default:o.self=p(0);break;
        }
        return nb::cast(std::move(o)).release().ptr();
    }catch(nb::python_error& error){error.restore();return nullptr;}catch(const std::exception& error){PyErr_SetString(PyExc_RuntimeError,error.what());return nullptr;}
}
PyMethodDef cc_frame_definition={"_color_class_source_frame",cc_frame,METH_VARARGS,nullptr};
nb::object cc_rows(ColorClassBinding& b){for(Py_ssize_t i=0;i<PyTuple_GET_SIZE(b.profiles.ptr());++i){PyObject* row=PyTuple_GET_ITEM(b.profiles.ptr(),i);long minor=PyLong_AsLong(PyTuple_GET_ITEM(row,0));if(minor==-1&&PyErr_Occurred())throw nb::python_error();if(minor==PY_MINOR_VERSION){nb::object rows=nb::borrow<nb::object>(PyTuple_GET_ITEM(row,1));if(!PyTuple_CheckExact(rows.ptr())||PyTuple_GET_SIZE(rows.ptr())!=21)throw nb::type_error("Color class whole-module declaration metadata changed");return rows;}}throw nb::value_error("Color class native admission has no reviewed interpreter profile");}
bool cc_native_code(nb::handle function){if(!PyFunction_Check(function.ptr()))return false;auto* code=reinterpret_cast<PyCodeObject*>(PyFunction_GetCode(function.ptr()));for(Py_ssize_t i=0;i<PyTuple_GET_SIZE(code->co_consts);++i){PyObject* value=PyTuple_GET_ITEM(code->co_consts,i);if(PyCFunction_Check(value)&&PyCFunction_GET_FUNCTION(value)==reinterpret_cast<PyCFunction>(cc_frame)&&PyCFunction_GET_SELF(value)==nullptr)return true;}return false;}
nb::object cc_shell_code(ColorClassBinding& b,Py_ssize_t entry,nb::handle function){
    nb::object shell=tc_attr(nb::borrow<nb::object>(PyTuple_GET_ITEM(b.shells.ptr(),entry)),"__code__"),constants=tc_attr(shell,"co_consts"),original=nb::borrow<nb::object>(PyFunction_GetCode(function.ptr()));
    nb::object replacements=tc_check(PyTuple_New(PyTuple_GET_SIZE(constants.ptr())+1));bool marker=false;
    for(Py_ssize_t i=0;i<PyTuple_GET_SIZE(constants.ptr());++i){PyObject* value=PyTuple_GET_ITEM(constants.ptr(),i);if(value==Py_Ellipsis){value=b.frame.ptr();marker=true;}PyTuple_SET_ITEM(replacements.ptr(),i,Py_NewRef(value));}
    nb::object source_literals=cc_literal_metadata(reinterpret_cast<PyCodeObject*>(original.ptr()));PyTuple_SET_ITEM(replacements.ptr(),PyTuple_GET_SIZE(constants.ptr()),source_literals.release().ptr());
    if(!marker)throw nb::type_error("Color class declaration transport marker is missing");
    return tc_call(tc_attr(shell,"replace"),{},{TX(replacements),TX(tc_attr(original,"co_name")),TX(tc_attr(original,"co_qualname")),TX(tc_attr(original,"co_filename")),TX(tc_attr(original,"co_firstlineno")),TX(tc_attr(original,"co_flags"))},tc_tuple({TX(nb::str("co_consts")),TX(nb::str("co_name")),TX(nb::str("co_qualname")),TX(nb::str("co_filename")),TX(nb::str("co_firstlineno")),TX(nb::str("co_flags"))}));
}

constexpr const char* cc_journal_key="__naparixx_color_class_owner__";
PyObject* cc_allocate(PyTypeObject* type,Py_ssize_t nitems){
    PyObject* value=type->tp_dict?PyDict_GetItemString(type->tp_dict,cc_journal_key):nullptr;
    if(!value||!nb::isinstance<ColorClassAllocation>(nb::handle(value))||!nb::inst_ready(value)){PyErr_SetString(PyExc_TypeError,"Color class allocation metadata is missing or changed");return nullptr;}
    auto& owner=*nb::inst_ptr<ColorClassAllocation>(value);allocfunc allocator=owner.delegate;
    if(!allocator||allocator==cc_allocate||!owner.class_ref.is_valid()||PyWeakref_GetObject(owner.class_ref.ptr())!=reinterpret_cast<PyObject*>(type)){PyErr_SetString(PyExc_TypeError,"Color class allocator metadata was cleared or replaced");return nullptr;}
    Py_INCREF(value);PyObject* result=allocator(type,nitems);if(result)++owner.allocations;Py_DECREF(value);return result;
}
nb::object color_class_allocation_info(nb::object cls){if(!PyType_Check(cls.ptr()))throw nb::type_error("Color allocation inspection requires an actual type");auto* type=reinterpret_cast<PyTypeObject*>(cls.ptr());PyObject* value=type->tp_dict?PyDict_GetItemString(type->tp_dict,cc_journal_key):nullptr;if(value&&nb::isinstance<ColorClassAllocation>(nb::handle(value))&&nb::inst_ready(value))return nb::borrow<nb::object>(value);return nb::none();}
bool cc_allocation_active(const ColorClassAllocation& o){if(!o.class_ref.is_valid())return false;PyObject* value=PyWeakref_GetObject(o.class_ref.ptr());return value&&value!=Py_None&&PyType_Check(value)&&reinterpret_cast<PyTypeObject*>(value)->tp_alloc==cc_allocate&&o.delegate;}
bool cc_layout(nb::handle cls,bool rgba){
    if(!PyType_Check(cls.ptr())||Py_TYPE(cls.ptr())!=&PyType_Type)return false;
    auto* type=reinterpret_cast<PyTypeObject*>(cls.ptr());if(!(type->tp_flags&Py_TPFLAGS_HEAPTYPE)||type->tp_itemsize||type->tp_dictoffset||type->tp_weaklistoffset)return false;
    const char* names_rgba[]={"r","g","b","alpha","_tuple"};const char* names_color[]={"_original","_rgba"};
    const char** names=rgba?names_rgba:names_color;size_t count=rgba?5:2;
    if(!type->tp_base||type->tp_basicsize!=type->tp_base->tp_basicsize+static_cast<Py_ssize_t>(count*sizeof(PyObject*)))return false;
    if(rgba&&type->tp_base!=&PyBaseObject_Type)return false;
    for(size_t i=0;i<count;++i){PyObject* raw=PyDict_GetItemString(type->tp_dict,names[i]);if(!raw||Py_TYPE(raw)!=&PyMemberDescr_Type)return false;auto* member=reinterpret_cast<PyMemberDescrObject*>(raw);if(member->d_common.d_type!=type||member->d_member->type!=T_OBJECT_EX||member->d_member->flags||member->d_member->offset<type->tp_base->tp_basicsize||member->d_member->offset+static_cast<Py_ssize_t>(sizeof(PyObject*))>type->tp_basicsize)return false;}
    return true;
}
nb::object cc_prepare_allocator(nb::handle cls){
    auto* type=reinterpret_cast<PyTypeObject*>(cls.ptr());PyObject* existing=PyDict_GetItemString(type->tp_dict,cc_journal_key);
    if(existing&&nb::isinstance<ColorClassAllocation>(nb::handle(existing))&&nb::inst_ready(existing)&&cc_allocation_active(*nb::inst_ptr<ColorClassAllocation>(existing)))return nb::none();
    if(type->tp_alloc!=PyType_GenericAlloc)return nb::none();
    ColorClassAllocation owner;owner.class_ref=tc_check(PyWeakref_NewRef(cls.ptr(),nullptr));owner.delegate=type->tp_alloc;return nb::cast(std::move(owner));
}
bool cc_target(nb::handle name){return PyUnicode_Check(name.ptr())&&PyUnicode_CompareWithASCIIString(name.ptr(),"pydantic._internal._repr")==0;}
void cc_admit(nb::object binding_object,nb::object module){
    if(!binding_object.is_valid())return;
    auto& binding=*nb::inst_ptr<ColorClassBinding>(binding_object.ptr());nb::object rows=cc_rows(binding),globals=tc_check(Py_NewRef(PyModule_GetDict(module.ptr())));
    nb::object module_name=tc_attr(module,"__name__");const char* module_text=PyUnicode_AsUTF8(module_name.ptr());if(!module_text)throw nb::python_error();
    int first=0,last=11;if(std::string(module_text)=="pydantic._internal._repr"){first=11;last=20;}else if(std::string(module_text)=="colorsys"){first=20;last=21;}else if(std::string(module_text)!="pydantic_extra_types.color")return;
    const char* paths[]={"Color.__get_pydantic_json_schema__","Color.as_named","Color.as_hex","Color.as_hsl","Color.as_hsl_tuple","Color.__get_pydantic_core_schema__","Color._validate","Color.__str__","Color.__repr_args__","Color.__eq__","Color.__hash__","Representation.__repr_args__","Representation.__repr_name__","Representation.__repr_recursion__","Representation.__repr_str__","Representation.__pretty__","Representation.__rich_repr__","Representation.__str__","Representation.__repr__","PlainRepr.__repr__","rgb_to_hls"};
    std::vector<std::pair<nb::object,nb::object>> changes;
    for(int i=first;i<last;++i){std::string path=paths[i];auto dot=path.find('.');PyObject* raw=nullptr;
        if(dot==std::string::npos)raw=PyDict_GetItemString(globals.ptr(),path.c_str());else{PyObject* cls=PyDict_GetItemString(globals.ptr(),path.substr(0,dot).c_str());if(cls&&PyType_Check(cls))raw=PyDict_GetItemString(reinterpret_cast<PyTypeObject*>(cls)->tp_dict,path.substr(dot+1).c_str());}
        if(!raw)continue;nb::object function=nb::borrow<nb::object>(raw);
        if(Py_TYPE(raw)==&PyClassMethod_Type)function=tc_attr(std::move(function),"__func__");
        if(cc_native_code(function))continue;
        if(!cp_profile_function(function,nb::handle(PyTuple_GET_ITEM(rows.ptr(),i)))||PyFunction_GetGlobals(function.ptr())!=globals.ptr())continue;
        changes.emplace_back(function,cc_shell_code(binding,i,function));
    }
    std::vector<std::pair<nb::object,nb::object>> allocations;
    if(first==0){for(const char* name:{"Color","RGBA"}){PyObject* cls=PyDict_GetItemString(globals.ptr(),name);bool rgba=std::string(name)=="RGBA";if(!cls||!cc_layout(nb::handle(cls),rgba))continue;
        PyObject* init=PyDict_GetItemString(reinterpret_cast<PyTypeObject*>(cls)->tp_dict,"__init__");if(!init||!cp_native_code(nb::handle(init))||PyFunction_GetGlobals(init)!=globals.ptr())continue;
        nb::object journal=cc_prepare_allocator(nb::handle(cls));if(!journal.is_none())allocations.emplace_back(nb::borrow<nb::object>(cls),std::move(journal));
    }}
    if(PyDict_SetItemString(globals.ptr(),"__naparixx_color_class_binding__",binding_object.ptr())<0)throw nb::python_error();
    for(auto& change:changes)if(PyObject_SetAttrString(change.first.ptr(),"__code__",change.second.ptr())<0)throw nb::python_error();
    try{for(auto& item:allocations){auto* type=reinterpret_cast<PyTypeObject*>(item.first.ptr());if(PyDict_SetItemString(type->tp_dict,cc_journal_key,item.second.ptr())<0)throw nb::python_error();type->tp_alloc=cc_allocate;PyType_Modified(type);}}
    catch(...){for(auto& item:allocations){auto* type=reinterpret_cast<PyTypeObject*>(item.first.ptr());nb::inst_ptr<ColorClassAllocation>(item.second.ptr())->clear();PyObject* current=PyDict_GetItemString(type->tp_dict,cc_journal_key);if(current==item.second.ptr()&&PyDict_DelItemString(type->tp_dict,cc_journal_key)<0)PyErr_Clear();}throw;}
}
nb::object color_class_bind(nb::object admission,nb::tuple shells,nb::object generator_shell,nb::tuple profiles){
    if(!nb::isinstance<ColorPolicyAdmission>(admission)||!nb::inst_ready(admission.ptr())||PyTuple_GET_SIZE(shells.ptr())!=21||!PyFunction_Check(generator_shell.ptr()))throw nb::type_error("Color class declaration metadata and admission are required");
    for(Py_ssize_t i=0;i<21;++i)if(!PyFunction_Check(PyTuple_GET_ITEM(shells.ptr(),i)))throw nb::type_error("Color class metadata shell must be a function");
    auto& policy=*nb::inst_ptr<ColorPolicyAdmission>(admission.ptr());auto& parent=*nb::inst_ptr<ColorPolicyBinding>(policy.binding.ptr());
    if(parent.class_owner.is_valid())return parent.class_owner;
    ColorClassBinding binding;binding.shells=std::move(shells);binding.generator_shell=std::move(generator_shell);binding.profiles=std::move(profiles);nb::object module_name=nb::str("naparixx._native");binding.frame=tc_check(PyCFunction_NewEx(&cc_frame_definition,nullptr,module_name.ptr()));cc_rows(binding);
    parent.class_owner=nb::cast(std::move(binding));return parent.class_owner;
}


// The three reviewed schema factory bodies and their kwargs comprehension.
// Every output is the actual source object; this owner adds no schema mirror.
#define SF_FIELDS(X) X(source_globals) X(source_builtins) X(source_code) X(source_literals) X(binding) X(when_used) X(function) X(field_name) X(ref) X(json_schema_input_schema) X(metadata) X(serialization) X(kwargs) X(s) X(iterator) X(k) X(v)
struct SchemaFactoryLocals {
    int entry=-1;
#define SF_DECLARE(name) nb::object name;
    SF_FIELDS(SF_DECLARE)
#undef SF_DECLARE
    SchemaFactoryLocals()=default;SchemaFactoryLocals(SchemaFactoryLocals&&)=default;
    void clear(){
#define SF_CLEAR(name) name.reset();
        SF_FIELDS(SF_CLEAR)
#undef SF_CLEAR
    }
    ~SchemaFactoryLocals(){clear();}
};
struct SchemaFactoryBinding {
    nb::object frame,shells,profiles;
    SchemaFactoryBinding()=default;SchemaFactoryBinding(SchemaFactoryBinding&&)=default;
    void clear(){frame.reset();shells.reset();profiles.reset();}
    ~SchemaFactoryBinding(){clear();}
};
int sf_owner_traverse(PyObject* self,visitproc visit,void* arg){Py_VISIT(Py_TYPE(self));if(nb::inst_ready(self)){auto& o=*nb::inst_ptr<SchemaFactoryLocals>(self);
#define SF_VISIT(name) Py_VISIT(o.name.ptr());
    SF_FIELDS(SF_VISIT)
#undef SF_VISIT
}return 0;}
int sf_owner_clear(PyObject* self){if(nb::inst_ready(self))nb::inst_ptr<SchemaFactoryLocals>(self)->clear();return 0;}
int sf_binding_traverse(PyObject* self,visitproc visit,void* arg){Py_VISIT(Py_TYPE(self));if(nb::inst_ready(self)){auto& o=*nb::inst_ptr<SchemaFactoryBinding>(self);Py_VISIT(o.frame.ptr());Py_VISIT(o.shells.ptr());Py_VISIT(o.profiles.ptr());}return 0;}
int sf_binding_clear(PyObject* self){if(nb::inst_ready(self))nb::inst_ptr<SchemaFactoryBinding>(self)->clear();return 0;}
PyType_Slot sf_owner_slots[]={{Py_tp_traverse,reinterpret_cast<void*>(sf_owner_traverse)},{Py_tp_clear,reinterpret_cast<void*>(sf_owner_clear)},{0,nullptr}};
PyType_Slot sf_binding_slots[]={{Py_tp_traverse,reinterpret_cast<void*>(sf_binding_traverse)},{Py_tp_clear,reinterpret_cast<void*>(sf_binding_clear)},{0,nullptr}};

nb::object sf_run(SchemaFactoryLocals& o){
    auto G=[&](const char* name){return cp_global(o.source_globals,o.source_builtins,name);};
    auto L=[&](const char* value){return cc_string(o.source_literals,value);};
    auto KW=[&](std::initializer_list<const char*> names){return cc_keywords(o.source_literals,names);};
    switch(o.entry){
    case 0:{
        o.s=tc_call(G("dict"),{},{TX(L("to-string"))},KW({"type"}));
        nb::object comparison=tc_check(PyObject_RichCompare(o.when_used.ptr(),L("json-unless-none").ptr(),Py_NE));
        bool include=tc_truth(comparison);comparison.reset();
        if(include){nb::object key=L("when_used");if(PyObject_SetItem(o.s.ptr(),key.ptr(),o.when_used.ptr())<0)throw nb::python_error();}
        return o.s;
    }
    case 1:{
        if(!o.field_name.is_none()){
            nb::object emitted=tc_call(tc_attr(G("warnings"),"warn"),{
                TX(L("The `field_name` argument on `with_info_plain_validator_function` is deprecated, it will be passed to the function through `ValidationState` instead.")),
                TX(G("DeprecationWarning"))
            },{TX(cc_integer(o.source_literals,2))},KW({"stacklevel"}));
            emitted.reset();
        }
        // Source resolves the outer callable before evaluating inner kwargs.
        nb::object outer=G("_dict_not_none");
        return tc_call(std::move(outer),{}, {
            TX(L("function-plain")),
            [&](){return tc_call(G("_dict_not_none"),{}, {TX(L("with-info")),TX(o.function),TX(o.field_name)},KW({"type","function","field_name"}));},
            TX(o.ref),TX(o.json_schema_input_schema),TX(o.metadata),TX(o.serialization)
        },KW({"type","function","ref","json_schema_input_schema","metadata","serialization"}));
    }
    case 2:{
        if(!PyDict_CheckExact(o.kwargs.ptr()))throw nb::type_error("schema source kwargs must be the actual declaration dictionary");
        // GET_ITER precedes BUILD_MAP in the genuine full-module compiler.
        o.iterator=tc_iter(tc_call(tc_attr(o.kwargs,"items")));
        try{
            o.s=tc_check(PyDict_New());
            while(nb::object item=tc_next(o.iterator)){
                auto pair=tc_unpack_pair(std::move(item));
                tc_assign(o.k,std::move(pair[0]));tc_assign(o.v,std::move(pair[1]));
                if(!o.v.is_none()&&PyDict_SetItem(o.s.ptr(),o.k.ptr(),o.v.ptr())<0)throw nb::python_error();
            }
            o.iterator.reset();o.v.reset();o.k.reset();
            return o.s;
        }catch(...){
#if PY_VERSION_HEX >= 0x030C0000
            o.iterator.reset();o.s.reset();o.v.reset();o.k.reset();
#else
            // CP311's saved comprehension frame can retain its iterator/locals.
            // The outer source carrier retains the same live objects; exact
            // separate private-frame parity remains a required open boundary.
            o.s.reset();
#endif
            throw;
        }
    }
    default:throw nb::value_error("unknown schema source entry");
    }
}
PyObject* sf_frame(PyObject*,PyObject* arguments){
    try{
        Py_ssize_t size=PyTuple_GET_SIZE(arguments);
        if(size==1){nb::handle owner(PyTuple_GET_ITEM(arguments,0));if(!nb::isinstance<SchemaFactoryLocals>(owner)||!nb::inst_ready(owner.ptr()))throw nb::type_error("schema source parameter owner is required");return sf_run(*nb::inst_ptr<SchemaFactoryLocals>(owner.ptr())).release().ptr();}
        if(size<2||!PyLong_CheckExact(PyTuple_GET_ITEM(arguments,0)))throw nb::type_error("schema source entry and parameters are required");
        long entry=PyLong_AsLong(PyTuple_GET_ITEM(arguments,0));if(entry==-1&&PyErr_Occurred())throw nb::python_error();
        const int counts[]={1,6,1};if(entry<0||entry>2||size!=counts[entry]+1)throw nb::type_error("schema source parameter declaration changed");
        PyFrameObject* frame=PyEval_GetFrame();if(!frame)throw nb::type_error("schema source frame is required");
        SchemaFactoryLocals o;o.entry=static_cast<int>(entry);o.source_globals=tc_check(PyFrame_GetGlobals(frame));o.source_builtins=tc_check(PyFrame_GetBuiltins(frame));o.source_code=tc_check(reinterpret_cast<PyObject*>(PyFrame_GetCode(frame)));
        nb::object constants=tc_attr(o.source_code,"co_consts");o.source_literals=nb::borrow<nb::object>(PyTuple_GET_ITEM(constants.ptr(),PyTuple_GET_SIZE(constants.ptr())-1));cc_literal_values(o.source_literals);
        PyObject* binding=PyDict_GetItemString(o.source_globals.ptr(),"__naparixx_schema_factory_binding__");if(!binding||!nb::isinstance<SchemaFactoryBinding>(nb::handle(binding))||!nb::inst_ready(binding))throw nb::type_error("schema source declaration binding is missing or changed");o.binding=nb::borrow<nb::object>(binding);
        auto p=[&](Py_ssize_t index){return nb::borrow<nb::object>(PyTuple_GET_ITEM(arguments,index+1));};
        if(entry==0)o.when_used=p(0);else if(entry==1){o.function=p(0);o.field_name=p(1);o.ref=p(2);o.json_schema_input_schema=p(3);o.metadata=p(4);o.serialization=p(5);}else o.kwargs=p(0);
        return nb::cast(std::move(o)).release().ptr();
    }catch(nb::python_error& error){error.restore();return nullptr;}catch(const std::exception& error){PyErr_SetString(PyExc_RuntimeError,error.what());return nullptr;}
}
PyMethodDef sf_frame_definition={"_schema_factory_source_frame",sf_frame,METH_VARARGS,nullptr};
nb::object sf_rows(SchemaFactoryBinding& b){
    for(Py_ssize_t i=0;i<PyTuple_GET_SIZE(b.profiles.ptr());++i){PyObject* row=PyTuple_GET_ITEM(b.profiles.ptr(),i);long minor=PyLong_AsLong(PyTuple_GET_ITEM(row,0));if(minor==-1&&PyErr_Occurred())throw nb::python_error();if(minor==PY_MINOR_VERSION){nb::object rows=nb::borrow<nb::object>(PyTuple_GET_ITEM(row,1));if(!PyTuple_CheckExact(rows.ptr())||PyTuple_GET_SIZE(rows.ptr())!=3)throw nb::type_error("schema whole-module declaration profiles changed");return rows;}}
    throw nb::value_error("schema native admission has no reviewed interpreter profile");
}
bool sf_native_code(nb::handle function){
    if(!PyFunction_Check(function.ptr()))return false;auto* code=reinterpret_cast<PyCodeObject*>(PyFunction_GetCode(function.ptr()));
    for(Py_ssize_t i=0;i<PyTuple_GET_SIZE(code->co_consts);++i){PyObject* value=PyTuple_GET_ITEM(code->co_consts,i);if(PyCFunction_Check(value)&&PyCFunction_GET_FUNCTION(value)==reinterpret_cast<PyCFunction>(sf_frame)&&PyCFunction_GET_SELF(value)==nullptr)return true;}return false;
}
nb::object sf_shell_code(SchemaFactoryBinding& b,Py_ssize_t entry,nb::handle function){
    nb::object shell=tc_attr(nb::borrow<nb::object>(PyTuple_GET_ITEM(b.shells.ptr(),entry)),"__code__"),constants=tc_attr(shell,"co_consts"),original=nb::borrow<nb::object>(PyFunction_GetCode(function.ptr()));
    nb::object replacements=tc_check(PyTuple_New(PyTuple_GET_SIZE(constants.ptr())+1));bool marker=false;
    for(Py_ssize_t i=0;i<PyTuple_GET_SIZE(constants.ptr());++i){PyObject* value=PyTuple_GET_ITEM(constants.ptr(),i);if(value==Py_Ellipsis){value=b.frame.ptr();marker=true;}PyTuple_SET_ITEM(replacements.ptr(),i,Py_NewRef(value));}
    nb::object literals=cc_literal_metadata(reinterpret_cast<PyCodeObject*>(original.ptr()));PyTuple_SET_ITEM(replacements.ptr(),PyTuple_GET_SIZE(constants.ptr()),literals.release().ptr());
    if(!marker)throw nb::type_error("schema declaration transport marker is missing");
    return tc_call(tc_attr(shell,"replace"),{}, {TX(replacements),TX(tc_attr(original,"co_name")),TX(tc_attr(original,"co_qualname")),TX(tc_attr(original,"co_filename")),TX(tc_attr(original,"co_firstlineno")),TX(tc_attr(original,"co_flags"))},tc_tuple({TX(nb::str("co_consts")),TX(nb::str("co_name")),TX(nb::str("co_qualname")),TX(nb::str("co_filename")),TX(nb::str("co_firstlineno")),TX(nb::str("co_flags"))}));
}
bool sf_target(nb::handle name){return PyUnicode_Check(name.ptr())&&PyUnicode_CompareWithASCIIString(name.ptr(),"pydantic_core.core_schema")==0;}
void sf_admit(nb::object binding_object,nb::object module){
    auto& binding=*nb::inst_ptr<SchemaFactoryBinding>(binding_object.ptr());nb::object rows=sf_rows(binding),globals=tc_check(Py_NewRef(PyModule_GetDict(module.ptr())));
    const char* names[]={"to_string_ser_schema","with_info_plain_validator_function","_dict_not_none"};std::vector<std::pair<nb::object,nb::object>> changes;
    for(Py_ssize_t i=0;i<3;++i){PyObject* raw=PyDict_GetItemString(globals.ptr(),names[i]);if(!raw){if(PyErr_Occurred())throw nb::python_error();continue;}nb::object function=nb::borrow<nb::object>(raw);if(sf_native_code(function))continue;if(!cp_profile_function(function,nb::handle(PyTuple_GET_ITEM(rows.ptr(),i)))||PyFunction_GetGlobals(function.ptr())!=globals.ptr())continue;changes.emplace_back(function,sf_shell_code(binding,i,function));}
    if(PyDict_SetItemString(globals.ptr(),"__naparixx_schema_factory_binding__",binding_object.ptr())<0)throw nb::python_error();
    for(auto& change:changes)if(PyObject_SetAttrString(change.first.ptr(),"__code__",change.second.ptr())<0)throw nb::python_error();
}
nb::object schema_factory_bind(nb::object admission,nb::tuple shells,nb::tuple profiles){
    if(!nb::isinstance<ColorPolicyAdmission>(admission)||!nb::inst_ready(admission.ptr())||PyTuple_GET_SIZE(shells.ptr())!=3)throw nb::type_error("schema declaration metadata and existing admission are required");
    for(Py_ssize_t i=0;i<3;++i)if(!PyFunction_Check(PyTuple_GET_ITEM(shells.ptr(),i)))throw nb::type_error("schema declaration shell must be a function");
    auto& policy=*nb::inst_ptr<ColorPolicyAdmission>(admission.ptr());auto& parent=*nb::inst_ptr<ColorPolicyBinding>(policy.binding.ptr());if(parent.schema_owner.is_valid())return parent.schema_owner;
    SchemaFactoryBinding binding;binding.shells=std::move(shells);binding.profiles=std::move(profiles);nb::object module_name=nb::str("naparixx._native");binding.frame=tc_check(PyCFunction_NewEx(&sf_frame_definition,nullptr,module_name.ptr()));sf_rows(binding);parent.schema_owner=nb::cast(std::move(binding));return parent.schema_owner;
}


bool cp_target(nb::handle name,bool& kernel){if(!PyUnicode_Check(name.ptr()))return false;int eq=PyUnicode_CompareWithASCIIString(name.ptr(),"pydantic_extra_types.color");if(eq==-1&&PyErr_Occurred())throw nb::python_error();if(eq==0){kernel=false;return true;}eq=PyUnicode_CompareWithASCIIString(name.ptr(),"colorsys");if(eq==-1&&PyErr_Occurred())throw nb::python_error();if(eq==0){kernel=true;return true;}return false;}
void cp_admit_result(nb::object admission,nb::object module){
    if(!PyModule_Check(module.ptr()))throw nb::type_error("Color provider loader did not publish an actual module");bool kernel=false;nb::object name=tc_attr(module,"__name__");if(cp_target(name,kernel))cp_admit(nb::inst_ptr<ColorPolicyAdmission>(admission.ptr())->binding,std::move(module),kernel);else if(cc_target(name)){auto& p=*nb::inst_ptr<ColorPolicyAdmission>(admission.ptr());auto& b=*nb::inst_ptr<ColorPolicyBinding>(p.binding.ptr());if(b.class_owner.is_valid())cc_admit(b.class_owner,std::move(module));}else if(sf_target(name)){auto& p=*nb::inst_ptr<ColorPolicyAdmission>(admission.ptr());auto& b=*nb::inst_ptr<ColorPolicyBinding>(p.binding.ptr());if(b.schema_owner.is_valid())sf_admit(b.schema_owner,std::move(module));}
}
nb::object cp_loader_attribute(nb::object self,nb::str name){
    auto& o=*nb::inst_ptr<ColorPolicyLoader>(self.ptr());
    nb::object callback=tc_check(PyObject_GetAttr(o.delegate.ptr(),name.ptr()));const char* key=PyUnicode_AsUTF8(name.ptr());if(!key)throw nb::python_error();
    if(std::string(key)!="exec_module"&&std::string(key)!="load_module")return callback;
    ColorPolicyExec call;call.legacy=std::string(key)=="load_module";call.callback=std::move(callback);call.admission=o.admission;call.wrapper=std::move(self);return nb::cast(std::move(call));
}
void cp_restore_loader_fields(nb::object wrapper,nb::handle module){
    auto& loader=*nb::inst_ptr<ColorPolicyLoader>(wrapper.ptr());
    nb::object current=nb::steal<nb::object>(PyObject_GetAttrString(loader.spec.ptr(),"loader"));
    if(!current.is_valid()){if(!PyErr_ExceptionMatches(PyExc_AttributeError))throw nb::python_error();PyErr_Clear();}
    if(current.is_valid()&&current.ptr()==wrapper.ptr()){
        if(PyObject_SetAttrString(loader.spec.ptr(),"loader",loader.delegate.ptr())<0)throw nb::python_error();
    }
    current.reset();
    if(module.is_valid()&&PyModule_Check(module.ptr())){
        PyObject* raw=PyDict_GetItemString(PyModule_GetDict(module.ptr()),"__loader__");
        if(raw==wrapper.ptr()&&PyObject_SetAttrString(module.ptr(),"__loader__",loader.delegate.ptr())<0)throw nb::python_error();
        if(PyErr_Occurred())throw nb::python_error();
    }
}
nb::object cp_loader_execute(ColorPolicyExec& o,nb::object argument){
    // Restore only fields still installed by this bridge BEFORE the genuine
    // delegate observes them. Its later mutations/deletions remain untouched.
    cp_restore_loader_fields(o.wrapper,o.legacy?nb::handle():nb::handle(argument));
    nb::object result=tc_call(o.callback,{TX(argument)});cp_admit_result(o.admission,o.legacy?result:argument);return result;
}
nb::object cp_find_spec(nb::object self,nb::object name,nb::object path,nb::object target){
    auto& o=*nb::inst_ptr<ColorPolicyAdmission>(self.ptr());nb::object spec=tc_call(o.search,{TX(name),TX(path),TX(target)});if(spec.is_none())return spec;
    bool kernel=false;if(!cp_target(name,kernel)&&!cc_target(name)&&!sf_target(name))return spec;nb::object delegate=tc_attr(spec,"loader");if(delegate.is_none())return spec;
    if(nb::isinstance<ColorPolicyLoader>(delegate))return spec;ColorPolicyLoader loader;loader.delegate=std::move(delegate);loader.admission=std::move(self);loader.spec=spec;nb::object wrapper=nb::cast(std::move(loader));if(PyObject_SetAttrString(spec.ptr(),"loader",wrapper.ptr())<0)throw nb::python_error();return spec;
}
void cp_activate(nb::object self){
    auto& o=*nb::inst_ptr<ColorPolicyAdmission>(self.ptr());if(!o.binding.is_valid())throw nb::type_error("Color admission metadata was cleared");cp_profile_rows(*nb::inst_ptr<ColorPolicyBinding>(o.binding.ptr()));
    if(!o.bootstrap.is_valid())o.bootstrap=tc_check(PyImport_ImportModule("importlib._bootstrap"));nb::object current=tc_attr(o.bootstrap,"_find_spec");
    nb::object bridge=tc_attr(self,"find_spec");if(current.ptr()!=bridge.ptr()){
        nb::object owner=nb::steal<nb::object>(PyObject_GetAttrString(current.ptr(),"__self__"));if(!owner.is_valid()){if(!PyErr_ExceptionMatches(PyExc_AttributeError))throw nb::python_error();PyErr_Clear();}
        if(!owner.is_valid()||owner.ptr()!=self.ptr()){o.search=std::move(current);if(PyObject_SetAttrString(o.bootstrap.ptr(),"_find_spec",bridge.ptr())<0)throw nb::python_error();}
    }
    PyObject* modules=PyImport_GetModuleDict();for(const char* name:{"colorsys","pydantic_extra_types.color","pydantic._internal._repr","pydantic_core.core_schema"}){PyObject* module=PyDict_GetItemString(modules,name);if(module&&module!=Py_None)cp_admit_result(self,nb::borrow<nb::object>(module));else if(PyErr_Occurred())throw nb::python_error();}
}
nb::object color_policy_bind(nb::tuple shells,nb::object generator_shell,nb::tuple profiles){
    if(PyTuple_GET_SIZE(shells.ptr())!=16||!PyFunction_Check(generator_shell.ptr()))throw nb::type_error("Color declaration shell metadata is required");
    for(Py_ssize_t i=0;i<16;++i)if(!PyFunction_Check(PyTuple_GET_ITEM(shells.ptr(),i)))throw nb::type_error("Color declaration shell must be a function");
    ColorPolicyBinding binding;binding.shells=std::move(shells);binding.generator_shell=std::move(generator_shell);binding.profiles=std::move(profiles);nb::object module_name=nb::str("naparixx._native");binding.frame=tc_check(PyCFunction_NewEx(&cp_frame_definition,nullptr,module_name.ptr()));
    ColorPolicyAdmission admission;admission.binding=nb::cast(std::move(binding));return nb::cast(std::move(admission));
}

#undef TX
} // namespace
void bind_theme_color_publication(nb::module_& module){
    auto owner=nb::class_<ThemeColorLocals>(module,"ThemeColorLocals",nb::is_weak_referenceable(),nb::type_slots(tc_owner_slots));
#define TC_FIELD(name) owner.def_prop_ro(#name,[](const ThemeColorLocals& value){return value.name.is_valid()?value.name:nb::none();});
TC_FIELD(self) TC_FIELD(cls) TC_FIELD(value) TC_FIELD(color) TC_FIELD(colors) TC_FIELD(theme_id) TC_FIELD(theme) TC_FIELD(changed) TC_FIELD(QT6) TC_FIELD(Qt) TC_FIELD(QGuiApplication) TC_FIELD(style_hints) TC_FIELD(scheme) TC_FIELD(colortype) TC_FIELD(typ) TC_FIELD(handler) TC_FIELD(colorarray) TC_FIELD(num_str) TC_FIELD(dtype) TC_FIELD(color_array) TC_FIELD(kind) TC_FIELD(idx) TC_FIELD(c) TC_FIELD(e) TC_FIELD(out_of_bounds_idx) TC_FIELD(out_of_bounds) TC_FIELD(norm) TC_FIELD(iterator)
#undef TC_FIELD
    module.def("theme_color_locals",&theme_color_locals,nb::arg("entry"),nb::arg("first").none()=nb::none(),nb::arg("second").none()=nb::none());
    module.def("theme_color_run",&theme_color_run,nb::arg("entry"),nb::arg("namespace"),nb::arg("locals_owner"),nb::arg("metadata"));
    module.def("theme_regex_initialize",&theme_regex_initialize);
    auto template_binding=nb::class_<ThemeTemplateBinding>(module,"ThemeTemplateBinding",nb::is_weak_referenceable(),nb::type_slots(tt_binding_slots));
    auto template_callback=nb::class_<ThemeTemplateCallback>(module,"ThemeTemplateCallback",nb::is_weak_referenceable(),nb::type_slots(tt_callback_slots));
    template_callback.def_prop_ro("entry",[](const ThemeTemplateCallback& value){return value.entry;});
#define TT_CALLBACK_FIELD(name) template_callback.def_prop_ro(#name,[](const ThemeTemplateCallback& value){return value.name.is_valid()?value.name:nb::none();});
    TT_CALLBACK_FIELD(source_globals) TT_CALLBACK_FIELD(source_builtins) TT_CALLBACK_FIELD(theme_cell)
#undef TT_CALLBACK_FIELD
    template_callback.def_prop_ro("theme",[](const ThemeTemplateCallback& value){PyObject* current=value.theme_cell.is_valid()?PyCell_GET(value.theme_cell.ptr()):nullptr;return nb::borrow<nb::object>(current?current:Py_None);});
    auto template_owner=nb::class_<ThemeTemplateLocals>(module,"ThemeTemplateLocals",nb::is_weak_referenceable(),nb::type_slots(tt_owner_slots));
    template_owner.def_prop_ro("entry",[](const ThemeTemplateLocals& value){return value.entry;});
#define TT_PUBLIC_FIELD(name) template_owner.def_prop_ro(#name,[](const ThemeTemplateLocals& value){return value.name.is_valid()?value.name:nb::none();});
    TT_FIELDS(TT_PUBLIC_FIELD)
#undef TT_PUBLIC_FIELD
    template_owner.def_prop_ro("theme",[](const ThemeTemplateLocals& value){PyObject* current=value.theme_cell.is_valid()?PyCell_GET(value.theme_cell.ptr()):nullptr;return nb::borrow<nb::object>(current?current:Py_None);});
#undef TT_FIELDS
    module.def("theme_template_bind",&theme_template_bind,nb::arg("functions"),nb::arg("callback_metadata_factory"));


    auto color_owner=nb::class_<ColorPolicyLocals>(module,"ColorPolicyLocals",nb::is_weak_referenceable(),nb::type_slots(cp_owner_slots));
    color_owner.def_prop_ro("entry",[](const ColorPolicyLocals& value){return value.entry;});
#define CP_PUBLIC_FIELD(name) color_owner.def_prop_ro(#name,[](const ColorPolicyLocals& value){return value.name.is_valid()?value.name:nb::none();});
    CP_FIELDS(CP_PUBLIC_FIELD)
#undef CP_PUBLIC_FIELD
#undef CP_FIELDS
    nb::class_<ColorPolicyBinding>(module,"ColorPolicyBinding",nb::is_weak_referenceable(),nb::type_slots(cp_binding_slots));
    auto color_generator=nb::class_<ColorPolicyGenerator>(module,"ColorPolicyGenerator",nb::is_weak_referenceable(),nb::type_slots(cp_generator_slots));
    color_generator.def("__iter__",[](nb::object self){return self;});
    color_generator.def("__next__",&cp_generator_next);
    color_generator.def("send",[](ColorPolicyGenerator& value,nb::object){return cp_generator_next(value);},nb::arg("value"));
    color_generator.def("close",[](ColorPolicyGenerator& value){value.exhausted=true;value.clear();});
    color_generator.def_prop_ro("entry",[](const ColorPolicyGenerator& value){return value.entry;});
#define CP_GENERATOR_FIELD(name) color_generator.def_prop_ro(#name,[](const ColorPolicyGenerator& value){return value.name.is_valid()?value.name:nb::none();});
    CP_GENERATOR_FIELD(source_globals) CP_GENERATOR_FIELD(source_builtins) CP_GENERATOR_FIELD(iterator) CP_GENERATOR_FIELD(v)
#undef CP_GENERATOR_FIELD
    auto color_admission=nb::class_<ColorPolicyAdmission>(module,"ColorPolicyAdmission",nb::is_weak_referenceable(),nb::type_slots(cp_admission_slots));
    color_admission.def("activate",[](nb::object self){cp_activate(std::move(self));});
    color_admission.def("find_spec",&cp_find_spec,nb::arg("name"),nb::arg("path").none(),nb::arg("target").none()=nb::none());
    auto color_loader=nb::class_<ColorPolicyLoader>(module,"ColorPolicyLoader",nb::is_weak_referenceable(),nb::type_slots(cp_loader_slots));
    color_loader.def("__getattr__",&cp_loader_attribute,nb::arg("name"));
    color_loader.def_prop_ro("delegate",[](const ColorPolicyLoader& value){return value.delegate;});
    auto color_exec=nb::class_<ColorPolicyExec>(module,"ColorPolicyExec",nb::is_weak_referenceable(),nb::type_slots(cp_exec_slots));
    color_exec.def("__call__",&cp_loader_execute,nb::arg("argument"));
    module.def("color_policy_bind",&color_policy_bind,nb::arg("shells"),nb::arg("generator_shell"),nb::arg("profiles"));
    auto class_locals=nb::class_<ColorClassLocals>(module,"ColorClassLocals",nb::is_weak_referenceable(),nb::type_slots(cc_owner_slots));
    class_locals.def_prop_ro("entry",[](const ColorClassLocals& o){return o.entry;});
#define CC_PUBLIC(name) class_locals.def_prop_ro(#name,[](const ColorClassLocals& o){return o.name.is_valid()?o.name:nb::none();});
    CC_FIELDS(CC_PUBLIC)
#undef CC_PUBLIC
#undef CC_FIELDS
    class_locals.def_prop_ro("_Color__input_value",[](const ColorClassLocals& o){return o.input_value.is_valid()?o.input_value:nb::none();});
    class_locals.def_prop_ro("_",[](const ColorClassLocals& o){return o.info.is_valid()?o.info:nb::none();});
    class_locals.def_prop_ro("as_hex",[](const ColorClassLocals& o){PyObject* value=o.hex_cell.is_valid()?PyCell_GET(o.hex_cell.ptr()):nullptr;return value?nb::borrow<nb::object>(value):nb::none();});
    nb::class_<ColorClassBinding>(module,"ColorClassBinding",nb::is_weak_referenceable(),nb::type_slots(cc_binding_slots));
    auto allocation=nb::class_<ColorClassAllocation>(module,"ColorClassAllocation",nb::is_weak_referenceable(),nb::type_slots(cc_allocation_slots));
    allocation.def_prop_ro("class_ref",[](const ColorClassAllocation& o){return o.class_ref.is_valid()?o.class_ref:nb::none();});
    allocation.def_prop_ro("allocations",[](const ColorClassAllocation& o){return o.allocations;});
    allocation.def_prop_ro("active",&cc_allocation_active);
    auto class_generator=nb::class_<ColorClassGenerator>(module,"ColorClassGenerator",nb::is_weak_referenceable(),nb::type_slots(cc_generator_slots));
    class_generator.def("__iter__",[](nb::object self){return self;});
    class_generator.def("__next__",&cc_generator_next);
    class_generator.def("send",[](ColorClassGenerator& o,nb::object){return cc_generator_next(o);},nb::arg("value").none());
    class_generator.def("close",[](ColorClassGenerator& o){o.exhausted=true;o.clear();});
    class_generator.def_prop_ro("entry",[](const ColorClassGenerator& o){return o.entry;});
    module.def("color_class_bind",&color_class_bind,nb::arg("admission"),nb::arg("shells"),nb::arg("generator_shell"),nb::arg("profiles"));
    module.def("color_class_allocation_info",&color_class_allocation_info,nb::arg("cls"));
    auto schema_owner=nb::class_<SchemaFactoryLocals>(module,"SchemaFactoryLocals",nb::is_weak_referenceable(),nb::type_slots(sf_owner_slots));
    schema_owner.def_prop_ro("entry",[](const SchemaFactoryLocals& o){return o.entry;});
#define SF_PUBLIC(name) schema_owner.def_prop_ro(#name,[](const SchemaFactoryLocals& o){return o.name.is_valid()?o.name:nb::none();});
    SF_FIELDS(SF_PUBLIC)
#undef SF_PUBLIC
#undef SF_FIELDS
    nb::class_<SchemaFactoryBinding>(module,"SchemaFactoryBinding",nb::is_weak_referenceable(),nb::type_slots(sf_binding_slots));
    module.def("schema_factory_bind",&schema_factory_bind,nb::arg("admission"),nb::arg("shells"),nb::arg("profiles"));


}
} // namespace nx
