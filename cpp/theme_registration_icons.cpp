#include "arrays.hpp"
#include "collection_protocol.hpp"
#if PY_VERSION_HEX >= 0x030C0000 && PY_VERSION_HEX < 0x030D0000
#include <internal/pycore_frame.h>
#endif
#include <array>
#include <functional>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace nx {
namespace {
nb::object tr_check(PyObject* value){if(!value)throw nb::python_error();return nb::steal<nb::object>(value);}
nb::object tr_attr(nb::object value,const char* name){return tr_check(PyObject_GetAttrString(value.ptr(),name));}
void tr_assign(nb::object& target,nb::object value){nb::object previous=std::move(target);target=std::move(value);previous.reset();}
using TRExpr=std::function<nb::object()>;
nb::object tr_call(nb::object function,std::initializer_list<TRExpr> positional={},std::initializer_list<TRExpr> keywords={},nb::object keyword_names={}){
    std::vector<nb::object> values;values.reserve(positional.size()+keywords.size());for(const auto& expression:positional)values.push_back(expression());for(const auto& expression:keywords)values.push_back(expression());
    if(keywords.size()&&(!keyword_names.is_valid()||!PyTuple_CheckExact(keyword_names.ptr())||PyTuple_GET_SIZE(keyword_names.ptr())!=static_cast<Py_ssize_t>(keywords.size())))throw nb::type_error("source keyword-name metadata must be the matching tuple");
    std::vector<PyObject*> operands;operands.reserve(values.size());for(const auto& value:values)operands.push_back(value.ptr());
    PyObject* result=PyObject_Vectorcall(function.ptr(),operands.data(),positional.size(),keyword_names.ptr());function.reset();for(auto& value:values)value.reset();keyword_names.reset();return tr_check(result);
}
nb::object tr_expanded_call(nb::object function,nb::handle kwargs){
    // Genuine source CALL_FUNCTION_EX: resolve callable first, then build a
    // fresh exact keyword expansion. Caller kwargs remains the same object.
    nb::object arguments=tr_check(PyTuple_New(0));
    nb::object expanded=tr_check(PyDict_Copy(kwargs.ptr()));
    PyObject* result=PyObject_Call(function.ptr(),arguments.ptr(),expanded.ptr());
    function.reset();arguments.reset();expanded.reset();return tr_check(result);
}
void tr_publish(TRExpr value,TRExpr receiver,const char* name){nb::object result=value(),target=receiver();int status=PyObject_SetAttrString(target.ptr(),name,result.ptr());result.reset();target.reset();if(status<0)throw nb::python_error();}
nb::tuple tr_tuple(std::initializer_list<TRExpr> expressions){
    std::vector<nb::object> values;values.reserve(expressions.size());for(const auto& expression:expressions)values.push_back(expression());
    nb::tuple result=nb::steal<nb::tuple>(PyTuple_New(static_cast<Py_ssize_t>(values.size())));if(!result.is_valid())throw nb::python_error();
    for(size_t index=0;index<values.size();++index)PyTuple_SET_ITEM(result.ptr(),static_cast<Py_ssize_t>(index),values[index].release().ptr());return result;
}
nb::object tr_import_frame_locals(){
#if PY_VERSION_HEX >= 0x030C0000 && PY_VERSION_HEX < 0x030D0000
    PyFrameObject* frame=PyEval_GetFrame();if(!frame){if(PyErr_Occurred())throw nb::python_error();return nb::none();}
    PyObject* locals=frame->f_frame->f_locals;return nb::borrow<nb::object>(locals?locals:Py_None);
#else
    PyErr_SetString(PyExc_RuntimeError,"Theme registration/icons IMPORT_NAME existing-locals handoff requires its reviewed CPython 3.12 profile; other required interpreter profiles remain open");throw nb::python_error();
#endif
}
struct ThemeRegistrationGlobals{
    nb::dict values;nb::object builtins;
    explicit ThemeRegistrationGlobals(nb::dict source):values(std::move(source)),builtins(nb::borrow<nb::object>(PyEval_GetBuiltins())){}
    nb::object builtin(const char* name)const{nb::object key=nb::str(name);PyObject* value=PyObject_GetItem(builtins.ptr(),key.ptr());if(value)return nb::steal<nb::object>(value);if(!PyErr_ExceptionMatches(PyExc_KeyError))throw nb::python_error();PyErr_Clear();if(std::string(name)=="__import__"){PyErr_SetString(PyExc_ImportError,"__import__ not found");throw nb::python_error();}nb::object message=tr_check(PyUnicode_FromFormat("name '%s' is not defined",name));nb::object error=tr_call(nb::borrow<nb::object>(PyExc_NameError),{[&]()->nb::object{return message;}});if(PyObject_SetAttrString(error.ptr(),"name",key.ptr())<0)throw nb::python_error();PyErr_SetObject(PyExc_NameError,error.ptr());throw nb::python_error();}
    nb::object get(const char* name)const{PyObject* value=PyDict_GetItemString(values.ptr(),name);if(value)return nb::borrow<nb::object>(value);if(PyErr_Occurred())throw nb::python_error();return builtin(name);}
    void store(const char* name,nb::object value){int status=PyDict_SetItemString(values.ptr(),name,value.ptr());value.reset();if(status<0)throw nb::python_error();}
    nb::object import_module(nb::object name,nb::object fromlist,nb::object level)const{
        PyObject* raw_name=name.release().ptr();PyUnicode_InternInPlace(&raw_name);if(PyErr_Occurred()){Py_XDECREF(raw_name);throw nb::python_error();}name=nb::steal<nb::object>(raw_name);
        nb::object importer=builtin("__import__"),locals=tr_import_frame_locals();
        return tr_call(std::move(importer),{[&]()->nb::object{return name;},[&]()->nb::object{return values;},[&]()->nb::object{return locals;},[&]()->nb::object{return fromlist;},[&]()->nb::object{return level;}});
    }
};
// Reuse the accepted handwritten CPython3.12 IMPORT_FROM protocol. In
// particular, an AttributeError must use the genuine sys.modules fallback
// and source ImportError metadata, rather than becoming plain getattr.
bool tr_spec_initializing(nb::handle spec){
    if(spec.is_valid()){nb::object value=nb::steal<nb::object>(PyObject_GetAttrString(spec.ptr(),"_initializing"));if(!value.is_valid()&&PyErr_ExceptionMatches(PyExc_AttributeError)){PyErr_Clear();return false;}if(value.is_valid()){int initializing=PyObject_IsTrue(value.ptr());value.reset();if(initializing>=0)return initializing!=0;}}
    PyErr_Clear();return false;
}
[[noreturn]] void tr_raise_import(nb::object message,nb::handle name,nb::handle path,nb::handle requested){
    if(!message.is_valid()){PyErr_SetString(PyExc_TypeError,"expected a message argument");throw nb::python_error();}
    nb::dict kwargs;kwargs["name"]=name.is_valid()?nb::borrow<nb::object>(name):nb::none();kwargs["path"]=path.is_valid()?nb::borrow<nb::object>(path):nb::none();
#if PY_VERSION_HEX >= 0x030C0000
    kwargs["name_from"]=requested;
#endif
    nb::object arguments=tr_check(PyTuple_New(1));PyTuple_SET_ITEM(arguments.ptr(),0,message.inc_ref().ptr());
    nb::object error=tr_check(PyObject_Call(PyExc_ImportError,arguments.ptr(),kwargs.ptr()));
    PyErr_SetObject(reinterpret_cast<PyObject*>(Py_TYPE(error.ptr())),error.ptr());throw nb::python_error();
}
nb::object tr_import_from(nb::handle module,nb::handle requested){
#if PY_VERSION_HEX >= 0x030C0000 && PY_VERSION_HEX < 0x030D0000
    PyObject* result=PyObject_GetAttr(module.ptr(),requested.ptr());if(result)return nb::steal<nb::object>(result);if(!PyErr_ExceptionMatches(PyExc_AttributeError))throw nb::python_error();PyErr_Clear();
    nb::object pkgname=nb::steal<nb::object>(PyObject_GetAttrString(module.ptr(),"__name__"));if(pkgname.is_valid()&&!PyUnicode_Check(pkgname.ptr()))pkgname.reset();
    if(pkgname.is_valid()){nb::object fullname=tr_check(PyUnicode_FromFormat("%U.%U",pkgname.ptr(),requested.ptr()));result=PyImport_GetModule(fullname.ptr());fullname.reset();if(result)return nb::steal<nb::object>(result);if(PyErr_Occurred())throw nb::python_error();}
    nb::object pkgpath=nb::steal<nb::object>(PyModule_GetFilenameObject(module.ptr()));PyObject* raw_pkgname=pkgname.ptr();
    nb::object name_or_unknown=pkgname.is_valid()?std::move(pkgname):nb::str("<unknown module name>");nb::object message;nb::handle error_path;
    if(!pkgpath.is_valid()||!PyUnicode_Check(pkgpath.ptr())){PyErr_Clear();message=nb::steal<nb::object>(PyUnicode_FromFormat("cannot import name %R from %R (unknown location)",requested.ptr(),name_or_unknown.ptr()));}
    else{nb::object spec=nb::steal<nb::object>(PyObject_GetAttrString(module.ptr(),"__spec__"));bool initializing=tr_spec_initializing(spec);spec.reset();const char* format=initializing?"cannot import name %R from partially initialized module %R (most likely due to a circular import) (%S)":"cannot import name %R from %R (%S)";message=nb::steal<nb::object>(PyUnicode_FromFormat(format,requested.ptr(),name_or_unknown.ptr(),pkgpath.ptr()));error_path=pkgpath;}
    try{tr_raise_import(std::move(message),nb::handle(raw_pkgname),error_path,requested);}catch(...){message.reset();name_or_unknown.reset();pkgpath.reset();throw;}
#else
    PyErr_SetString(PyExc_RuntimeError,"Theme registration/icons IMPORT_FROM requires its reviewed CPython 3.12 profile; other required interpreter profiles remain open");throw nb::python_error();
#endif
}

bool tr_truth(nb::object value){int result=PyObject_IsTrue(value.ptr());value.reset();if(result<0)throw nb::python_error();return result!=0;}
nb::object tr_compare(TRExpr left,TRExpr right,int operation){nb::object a=left(),b=right();PyObject* result=PyObject_RichCompare(a.ptr(),b.ptr(),operation);a.reset();b.reset();return tr_check(result);}
nb::object tr_binary(TRExpr left,TRExpr right,PyObject*(*operation)(PyObject*,PyObject*)){nb::object a=left(),b=right();PyObject* result=operation(a.ptr(),b.ptr());a.reset();b.reset();return tr_check(result);}
nb::object tr_item(nb::object value,nb::object index){PyObject* result=PyObject_GetItem(value.ptr(),index.ptr());value.reset();index.reset();return tr_check(result);}
nb::object tr_index(TRExpr value,TRExpr index){nb::object target=value(),key=index();return tr_item(std::move(target),std::move(key));}
void tr_set_item(TRExpr value,TRExpr receiver,TRExpr index){nb::object result=value(),target=receiver(),key=index();int status=PyObject_SetItem(target.ptr(),key.ptr(),result.ptr());result.reset();target.reset();key.reset();if(status<0)throw nb::python_error();}
nb::object tr_slice(nb::object first,nb::object last){PyObject* result=PySlice_New(first.ptr(),last.ptr(),Py_None);first.reset();last.reset();return tr_check(result);}
nb::object tr_iter(nb::object value){PyObject* result=PyObject_GetIter(value.ptr());value.reset();return tr_check(result);}
nb::object tr_next(nb::handle value){PyObject* result=PyIter_Next(value.ptr());if(!result&&PyErr_Occurred())throw nb::python_error();return nb::steal<nb::object>(result);}
nb::object tr_list(std::initializer_list<TRExpr> expressions){std::vector<nb::object> values;values.reserve(expressions.size());for(const auto& expression:expressions)values.push_back(expression());nb::object result=tr_check(PyList_New(static_cast<Py_ssize_t>(values.size())));for(size_t index=0;index<values.size();++index)PyList_SET_ITEM(result.ptr(),static_cast<Py_ssize_t>(index),values[index].release().ptr());return result;}
std::array<nb::object,2> tr_unpack_pair(nb::object value){PyObject* raw=PyObject_GetIter(value.ptr());if(!raw){nb::python_error error;if(error.matches(PyExc_TypeError)&&Py_TYPE(value.ptr())->tp_iter==nullptr&&!PySequence_Check(value.ptr())){PyErr_Format(PyExc_TypeError,"cannot unpack non-iterable %.200s object",Py_TYPE(value.ptr())->tp_name);throw nb::python_error();}throw error;}nb::object iterator=nb::steal<nb::object>(raw);std::array<nb::object,2> result;for(size_t i=0;i<2;++i){result[i]=tr_next(iterator);if(!result[i].is_valid()){PyErr_Format(PyExc_ValueError,"not enough values to unpack (expected 2, got %zu)",i);throw nb::python_error();}}nb::object extra=tr_next(iterator);if(extra.is_valid()){PyErr_SetString(PyExc_ValueError,"too many values to unpack (expected 2)");throw nb::python_error();}iterator.reset();value.reset();return result;}
nb::object tr_format(nb::object value){PyObject* result=PyObject_Format(value.ptr(),nullptr);value.reset();return tr_check(result);}
nb::object tr_join(std::initializer_list<TRExpr> pieces){nb::tuple parts=tr_tuple(pieces);nb::object separator=nb::str("");PyObject* result=PyUnicode_Join(separator.ptr(),parts.ptr());parts.reset();separator.reset();return tr_check(result);}
[[noreturn]]void tr_raise(nb::object value){if(PyExceptionClass_Check(value.ptr())){nb::object cls=std::move(value);value=tr_call(cls);if(!PyExceptionInstance_Check(value.ptr())){PyErr_Format(PyExc_TypeError,"calling %R should have returned an instance of BaseException, not %R",cls.ptr(),reinterpret_cast<PyObject*>(Py_TYPE(value.ptr())));throw nb::python_error();}}if(!PyExceptionInstance_Check(value.ptr())){PyErr_SetString(PyExc_TypeError,"exceptions must derive from BaseException");throw nb::python_error();}PyErr_SetObject(reinterpret_cast<PyObject*>(Py_TYPE(value.ptr())),value.ptr());throw nb::python_error();}

[[noreturn]]void tr_raise_cause(nb::object value,nb::handle cause){
    // Source RAISE_VARARGS2 first normalizes the error class/instance, then
    // attaches its genuine already-caught exception cause.
    if(PyExceptionClass_Check(value.ptr())){nb::object cls=std::move(value);value=tr_call(cls);if(!PyExceptionInstance_Check(value.ptr())){PyErr_Format(PyExc_TypeError,"calling %R should have returned an instance of BaseException, not %R",cls.ptr(),reinterpret_cast<PyObject*>(Py_TYPE(value.ptr())));throw nb::python_error();}}
    if(!PyExceptionInstance_Check(value.ptr())){PyErr_SetString(PyExc_TypeError,"exceptions must derive from BaseException");throw nb::python_error();}
    PyException_SetCause(value.ptr(),cause.inc_ref().ptr());tr_raise(std::move(value));
}
struct TRHandledException{PyObject* previous;explicit TRHandledException(nb::handle value):previous(PyErr_GetHandledException()){PyErr_SetHandledException(value.ptr());}~TRHandledException(){PyErr_SetHandledException(previous);Py_XDECREF(previous);}};
struct ThemeRegistrationLocals{
    int entry=-1; // immutable source-layout selector, never model state
    nb::object cls,value,STYLE_MAP,valid_styles,self,th,k,v,theme_id,theme,source,plugin_name_file,plugin_name,get_settings,settings,themes,npe2,manifest,theme_dict,theme_info,theme_colors,path,path_or_xml,color,opacity,xml,svg_paths,colors,opacities,theme_override,ALIAS_T,op,clrkey,svg_stem,get_theme,theme_key,op_key,alias,dest,svgs,svg,theme_name,out,f,x,iterator;
    ThemeRegistrationLocals()=default;ThemeRegistrationLocals(ThemeRegistrationLocals&&)=default;
    void clear(){std::array<nb::object,46> values={std::move(cls),std::move(value),std::move(STYLE_MAP),std::move(valid_styles),std::move(self),std::move(th),std::move(k),std::move(v),std::move(theme_id),std::move(theme),std::move(source),std::move(plugin_name_file),std::move(plugin_name),std::move(get_settings),std::move(settings),std::move(themes),std::move(npe2),std::move(manifest),std::move(theme_dict),std::move(theme_info),std::move(theme_colors),std::move(path),std::move(path_or_xml),std::move(color),std::move(opacity),std::move(xml),std::move(svg_paths),std::move(colors),std::move(opacities),std::move(theme_override),std::move(ALIAS_T),std::move(op),std::move(clrkey),std::move(svg_stem),std::move(get_theme),std::move(theme_key),std::move(op_key),std::move(alias),std::move(dest),std::move(svgs),std::move(svg),std::move(theme_name),std::move(out),std::move(f),std::move(x),std::move(iterator)};switch(entry){case 0:for(unsigned index:{0u,1u,2u,3u})values[index].reset();break;case 1:for(unsigned index:{0u,1u})values[index].reset();break;case 2:for(unsigned index:{4u,5u,6u,7u})values[index].reset();break;case 3:for(unsigned index:{8u,9u,10u})values[index].reset();break;case 4:for(unsigned index:{8u})values[index].reset();break;case 5:for(unsigned index:{8u,11u,12u})values[index].reset();break;case 6:for(unsigned index:{13u,14u})values[index].reset();break;case 7:for(unsigned index:{15u,16u,17u,9u,18u,19u,20u})values[index].reset();break;case 8:for(unsigned index:{21u})values[index].reset();break;case 9:for(unsigned index:{22u,23u,24u,25u})values[index].reset();break;case 10:for(unsigned index:{26u,27u,28u,29u,30u,23u,21u,31u,32u,33u,34u,35u,36u,37u})values[index].reset();break;case 11:for(unsigned index:{38u,26u,27u,28u,29u,39u,37u,40u})values[index].reset();break;case 12:for(unsigned index:{41u})values[index].reset();break;case 13:for(unsigned index:{41u,10u,42u,43u})values[index].reset();break;case 14:for(unsigned index:{4u})values[index].reset();break;default:break;}for(auto& value:values)value.reset();}
    ~ThemeRegistrationLocals(){clear();}
};
int tr_owner_traverse(PyObject* self,visitproc visit,void* arg){Py_VISIT(Py_TYPE(self));if(nb::inst_ready(self)){auto& o=*nb::inst_ptr<ThemeRegistrationLocals>(self);Py_VISIT(o.cls.ptr());Py_VISIT(o.value.ptr());Py_VISIT(o.STYLE_MAP.ptr());Py_VISIT(o.valid_styles.ptr());Py_VISIT(o.self.ptr());Py_VISIT(o.th.ptr());Py_VISIT(o.k.ptr());Py_VISIT(o.v.ptr());Py_VISIT(o.theme_id.ptr());Py_VISIT(o.theme.ptr());Py_VISIT(o.source.ptr());Py_VISIT(o.plugin_name_file.ptr());Py_VISIT(o.plugin_name.ptr());Py_VISIT(o.get_settings.ptr());Py_VISIT(o.settings.ptr());Py_VISIT(o.themes.ptr());Py_VISIT(o.npe2.ptr());Py_VISIT(o.manifest.ptr());Py_VISIT(o.theme_dict.ptr());Py_VISIT(o.theme_info.ptr());Py_VISIT(o.theme_colors.ptr());Py_VISIT(o.path.ptr());Py_VISIT(o.path_or_xml.ptr());Py_VISIT(o.color.ptr());Py_VISIT(o.opacity.ptr());Py_VISIT(o.xml.ptr());Py_VISIT(o.svg_paths.ptr());Py_VISIT(o.colors.ptr());Py_VISIT(o.opacities.ptr());Py_VISIT(o.theme_override.ptr());Py_VISIT(o.ALIAS_T.ptr());Py_VISIT(o.op.ptr());Py_VISIT(o.clrkey.ptr());Py_VISIT(o.svg_stem.ptr());Py_VISIT(o.get_theme.ptr());Py_VISIT(o.theme_key.ptr());Py_VISIT(o.op_key.ptr());Py_VISIT(o.alias.ptr());Py_VISIT(o.dest.ptr());Py_VISIT(o.svgs.ptr());Py_VISIT(o.svg.ptr());Py_VISIT(o.theme_name.ptr());Py_VISIT(o.out.ptr());Py_VISIT(o.f.ptr());Py_VISIT(o.x.ptr());Py_VISIT(o.iterator.ptr());}return 0;}
int tr_owner_clear(PyObject* self){if(nb::inst_ready(self))nb::inst_ptr<ThemeRegistrationLocals>(self)->clear();return 0;}
PyType_Slot tr_owner_slots[]={{Py_tp_traverse,reinterpret_cast<void*>(tr_owner_traverse)},{Py_tp_clear,reinterpret_cast<void*>(tr_owner_clear)},{0,nullptr}};

#define RX(value) [&]() -> nb::object { return (value); }
nb::object tr_literal(nb::handle metadata,Py_ssize_t index){return nb::borrow<nb::object>(PyTuple_GET_ITEM(metadata.ptr(),index));}
bool tr_contains(TRExpr container,TRExpr key){nb::object mapping=container(),index=key();int found=PySequence_Contains(mapping.ptr(),index.ptr());index.reset();mapping.reset();if(found<0)throw nb::python_error();return found!=0;}
nb::object tr_repr(nb::object value){PyObject* result=PyObject_Repr(value.ptr());value.reset();return tr_check(result);}
bool tr_matches(nb::handle error,nb::object type){bool valid=true;if(PyTuple_Check(type.ptr())){for(Py_ssize_t i=0;i<PyTuple_GET_SIZE(type.ptr());++i)if(!PyExceptionClass_Check(PyTuple_GET_ITEM(type.ptr(),i))){valid=false;break;}}else valid=PyExceptionClass_Check(type.ptr())!=0;if(!valid){PyErr_SetString(PyExc_TypeError,"catching classes that do not inherit from BaseException is not allowed");throw nb::python_error();}return PyErr_GivenExceptionMatches(error.ptr(),type.ptr())!=0;}
std::array<nb::object,3> tr_unpack_three(nb::object value){PyObject* raw=PyObject_GetIter(value.ptr());if(!raw){nb::python_error error;if(error.matches(PyExc_TypeError)&&Py_TYPE(value.ptr())->tp_iter==nullptr&&!PySequence_Check(value.ptr())){PyErr_Format(PyExc_TypeError,"cannot unpack non-iterable %.200s object",Py_TYPE(value.ptr())->tp_name);throw nb::python_error();}throw error;}nb::object iterator=nb::steal<nb::object>(raw);std::array<nb::object,3> parts;for(size_t index=0;index<3;++index){parts[index]=tr_next(iterator);if(!parts[index].is_valid()){PyErr_Format(PyExc_ValueError,"not enough values to unpack (expected 3, got %zu)",index);throw nb::python_error();}}nb::object extra=tr_next(iterator);if(extra.is_valid()){PyErr_SetString(PyExc_ValueError,"too many values to unpack (expected 3)");throw nb::python_error();}iterator.reset();value.reset();return parts;}
void tr_merge_kwargs(nb::handle callable,nb::dict& keywords,nb::handle mapping){
    if(_PyDict_MergeEx(keywords.ptr(),mapping.ptr(),2)>=0)return;
    if(PyErr_ExceptionMatches(PyExc_AttributeError)){PyErr_Clear();nb::object label=tr_check(_PyObject_FunctionStr(callable.ptr()));PyErr_Format(PyExc_TypeError,"%U argument after ** must be a mapping, not %.200s",label.ptr(),Py_TYPE(mapping.ptr())->tp_name);}
    else if(PyErr_ExceptionMatches(PyExc_KeyError)){PyObject* type=nullptr,*value=nullptr,*trace=nullptr;PyErr_Fetch(&type,&value,&trace);PyErr_NormalizeException(&type,&value,&trace);nb::object error_type=nb::steal<nb::object>(type),error=nb::steal<nb::object>(value),traceback=nb::steal<nb::object>(trace);if(!error.is_valid())throw nb::python_error();nb::object key=tr_item(tr_attr(error,"args"),nb::int_(0)),label=tr_check(_PyObject_FunctionStr(callable.ptr()));PyErr_Format(PyExc_TypeError,"%U got multiple values for keyword argument '%S'",label.ptr(),key.ptr());}
    throw nb::python_error();
}
nb::object tr_mapping_call(nb::object function,nb::handle mapping){nb::object args=tr_check(PyTuple_New(0));nb::dict keywords;tr_merge_kwargs(function,keywords,mapping);PyObject* result=PyObject_Call(function.ptr(),args.ptr(),keywords.ptr());function.reset();args.reset();keywords.reset();return tr_check(result);}
void tr_with(nb::object context,const std::function<void(nb::object)>& body){
    nb::object enter=collections::context_method(context,"__enter__",false),exit=collections::context_method(context,"__exit__",true);context.reset();nb::object entered=tr_call(std::move(enter));
    try{body(std::move(entered));}
    catch(const nb::python_error& error){TRHandledException active(error.value());nb::object trace=error.traceback();if(!trace.is_valid())trace=nb::none();try{if(!tr_truth(tr_call(exit,{RX(nb::borrow<nb::object>(error.type())),RX(nb::borrow<nb::object>(error.value())),RX(trace)})))throw;}catch(const nb::python_error& cleanup){if(cleanup.value().ptr()!=error.value().ptr())PyException_SetContext(cleanup.value().ptr(),error.value().inc_ref().ptr());throw;}return;}
    tr_call(std::move(exit),{RX(nb::none()),RX(nb::none()),RX(nb::none())});
}
struct TRPolicy{
    ThemeRegistrationGlobals ns;ThemeRegistrationLocals& o;nb::tuple metadata;nb::object super_provider;
    TRPolicy(nb::dict source,ThemeRegistrationLocals& owner,nb::tuple constants,nb::object provider=nb::none()):ns(std::move(source)),o(owner),metadata(std::move(constants)),super_provider(std::move(provider)){}
    nb::object K(Py_ssize_t index)const{return tr_literal(metadata,index);}
    nb::object import_member(Py_ssize_t index){nb::object declaration=K(index);nb::object module=ns.import_module(tr_item(declaration,nb::int_(0)),tr_item(declaration,nb::int_(1)),tr_item(declaration,nb::int_(2)));nb::object name=tr_item(tr_item(declaration,nb::int_(1)),nb::int_(0));nb::object value=tr_import_from(module,name);module.reset();name.reset();return value;}
    nb::object syntax_style(){tr_assign(o.STYLE_MAP,import_member(0));tr_assign(o.valid_styles,tr_call(tr_attr(K(1),"join"),{RX(o.STYLE_MAP)}));if(tr_truth(K(2))&&!tr_contains(RX(o.STYLE_MAP),RX(o.value)))tr_raise(tr_call(nb::borrow<nb::object>(PyExc_AssertionError),{RX(tr_join({RX(K(3)),RX(tr_format(o.value)),RX(K(4)),RX(tr_format(o.valid_styles))}))}));return o.value;}
    nb::object font_size(){if(tr_truth(K(0))){if(!tr_truth(tr_call(tr_attr(o.value,"endswith"),{RX(K(1))})))tr_raise(tr_call(nb::borrow<nb::object>(PyExc_AssertionError),{RX(K(2))}));nb::object number=tr_call(ns.get("int"),{RX(tr_item(o.value,tr_slice(nb::none(),K(3))))});if(!tr_truth(tr_compare(RX(std::move(number)),RX(K(4)),Py_GT)))tr_raise(tr_call(nb::borrow<nb::object>(PyExc_AssertionError),{RX(K(5))}));}return o.value;}
    nb::object rgb_dict(){
        tr_assign(o.th,tr_call(tr_attr(tr_call(std::move(super_provider)),"model_dump")));
        nb::object cursor_object=nb::cast(ThemeRegistrationLocals());auto& cursor=*nb::inst_ptr<ThemeRegistrationLocals>(cursor_object.ptr());cursor.iterator=tr_iter(tr_call(tr_attr(o.th,"items")));nb::object result;
        try{result=tr_check(PyDict_New());while(nb::object pair=tr_next(cursor.iterator)){auto parts=tr_unpack_pair(std::move(pair));tr_assign(cursor.k,std::move(parts[0]));tr_assign(cursor.v,std::move(parts[1]));nb::object key=cursor.k;nb::object value=!tr_truth(tr_call(ns.get("isinstance"),{RX(cursor.v),RX(ns.get("Color"))}))?cursor.v:tr_call(tr_attr(cursor.v,"as_rgb"));int status=PyDict_SetItem(result.ptr(),key.ptr(),value.ptr());key.reset();value.reset();if(status<0)throw nb::python_error();}}
        catch(...){cursor.iterator.reset();result.reset();cursor.k.reset();cursor.v.reset();cursor_object.reset();throw;}
        cursor.iterator.reset();cursor.k.reset();cursor.v.reset();cursor_object.reset();return result;
    }
    nb::object register_theme(){if(tr_truth(tr_call(ns.get("isinstance"),{RX(o.theme),RX(ns.get("dict"))})))tr_assign(o.theme,tr_mapping_call(ns.get("Theme"),o.theme));if(tr_truth(K(0))&&!tr_truth(tr_call(ns.get("isinstance"),{RX(o.theme),RX(ns.get("Theme"))})))tr_raise(nb::borrow<nb::object>(PyExc_AssertionError));tr_set_item(RX(o.theme),RX(ns.get("_themes")),RX(o.theme_id));tr_call(ns.get("build_theme_svgs"),{RX(o.theme_id),RX(o.source)});return nb::none();}
    nb::object unregister(){tr_call(tr_attr(ns.get("_themes"),"pop"),{RX(o.theme_id),RX(nb::none())});return nb::none();}
    nb::object available(){if(tr_truth(tr_compare(RX(o.theme_id),RX(K(0)),Py_EQ)))return nb::bool_(true);if(!tr_contains(RX(ns.get("_themes")),RX(o.theme_id))&&tr_truth(tr_call(tr_attr(tr_call(ns.get("_theme_path"),{RX(o.theme_id)}),"exists")))){
        tr_assign(o.plugin_name_file,tr_binary(RX(tr_call(ns.get("_theme_path"),{RX(o.theme_id)})),RX(ns.get("PLUGIN_FILE_NAME")),PyNumber_TrueDivide));if(!tr_truth(tr_call(tr_attr(o.plugin_name_file,"exists"))))return nb::bool_(false);tr_assign(o.plugin_name,tr_call(tr_attr(o.plugin_name_file,"read_text")));
        tr_with(tr_call(ns.get("suppress"),{RX(ns.get("ModuleNotFoundError"))}),[&](nb::object entered){entered.reset();tr_call(tr_attr(tr_call(tr_attr(tr_attr(ns.get("npe2"),"PluginManager"),"instance")),"register"),{RX(o.plugin_name)});});
        tr_call(ns.get("_install_npe2_themes"),{RX(ns.get("_themes"))});}return nb::bool_(tr_contains(RX(ns.get("_themes")),RX(o.theme_id)));}
    nb::object rebuild(){tr_assign(o.get_settings,import_member(0));tr_assign(o.settings,tr_call(o.get_settings));tr_call(tr_attr(tr_attr(o.settings,"appearance"),"refresh_themes"));return nb::none();}
    nb::object install(){
        if(o.themes.is_none())tr_assign(o.themes,ns.get("_themes"));tr_assign(o.npe2,ns.import_module(K(0),nb::none(),K(1)));
        nb::object manifests=tr_iter(tr_call(tr_attr(tr_call(tr_attr(tr_attr(o.npe2,"PluginManager"),"instance")),"iter_manifests"),{}, {RX(K(2))},K(3)));
        while(nb::object manifest=tr_next(manifests)){tr_assign(o.manifest,std::move(manifest));nb::object themes=tr_attr(tr_attr(o.manifest,"contributions"),"themes");if(!tr_truth(themes))themes=K(4);nb::object iterator=tr_iter(std::move(themes));while(nb::object theme=tr_next(iterator)){tr_assign(o.theme,std::move(theme));tr_assign(o.theme_dict,tr_call(tr_attr(tr_index(RX(o.themes),RX(tr_attr(o.theme,"type"))),"model_dump")));
            tr_assign(o.theme_info,tr_call(tr_attr(o.theme,"model_dump"),{}, {RX(tr_check(PySet_New(tr_tuple({RX(K(5))}).ptr()))),RX(K(6))},K(7)));
            tr_assign(o.theme_colors,tr_call(tr_attr(tr_attr(o.theme,"colors"),"model_dump"),{}, {RX(K(6))},K(8)));
            tr_call(tr_attr(o.theme_dict,"update"),{RX(o.theme_info)});tr_call(tr_attr(o.theme_dict,"update"),{RX(o.theme_colors)});
            try{tr_call(ns.get("register_theme"),{RX(tr_attr(o.theme,"id")),RX(o.theme_dict),RX(tr_attr(o.manifest,"name"))});}
            catch(nb::python_error& failure){TRHandledException handled(failure.value());if(!tr_matches(failure.value(),ns.get("ValueError")))throw;tr_call(tr_attr(tr_call(tr_attr(ns.get("logging"),"getLogger"),{RX(K(9))}),"exception"),{RX(K(10))});}
        }iterator.reset();}manifests.reset();return nb::none();
    }
    nb::object raw_svg(){return tr_call(tr_attr(tr_call(ns.get("Path"),{RX(o.path)}),"read_text"));}
    nb::object colorized(){tr_assign(o.path_or_xml,tr_call(ns.get("str"),{RX(o.path_or_xml)}));tr_assign(o.xml,tr_contains(RX(o.path_or_xml),RX(K(0)))?o.path_or_xml:tr_call(ns.get("get_raw_svg"),{RX(o.path_or_xml)}));if(!tr_truth(o.color))return o.xml;if(!tr_truth(tr_call(tr_attr(ns.get("svg_elem"),"search"),{RX(o.xml)})))tr_raise(tr_call(ns.get("ValueError"),{RX(tr_join({RX(K(1)),RX(tr_repr(o.path_or_xml))}))}));return tr_call(tr_attr(ns.get("svg_elem"),"sub"),{RX(tr_join({RX(K(2)),RX(tr_format(tr_call(tr_attr(ns.get("svg_style"),"format"),{RX(o.color),RX(o.opacity)})))})),RX(o.xml)});}
    nb::object write(){tr_assign(o.dest,tr_call(ns.get("Path"),{RX(o.dest)}));tr_call(tr_attr(o.dest,"mkdir"),{}, {RX(K(0)),RX(K(0))},K(1));tr_assign(o.svgs,tr_call(ns.get("generate_colorized_svgs"),{}, {RX(o.svg_paths),RX(o.colors),RX(o.opacities),RX(o.theme_override)},K(2)));nb::object iterator=tr_iter(o.svgs);while(nb::object pair=tr_next(iterator)){auto parts=tr_unpack_pair(std::move(pair));tr_assign(o.alias,std::move(parts[0]));tr_assign(o.svg,std::move(parts[1]));tr_call(tr_attr(tr_binary(RX(o.dest),RX(tr_attr(tr_call(ns.get("Path"),{RX(o.alias)}),"name")),PyNumber_TrueDivide),"write_text"),{RX(o.svg)});}iterator.reset();return nb::none();}
    nb::object theme_path(){return tr_binary(RX(tr_binary(RX(tr_call(ns.get("Path"),{RX(tr_call(ns.get("user_cache_dir")))})),RX(K(0)),PyNumber_TrueDivide)),RX(o.theme_name),PyNumber_TrueDivide);}
    nb::object build(){tr_assign(o.out,tr_call(ns.get("_theme_path"),{RX(o.theme_name)}));tr_call(ns.get("write_colorized_svgs"),{RX(o.out)}, {RX(tr_call(tr_attr(ns.get("ICONS"),"values"))),RX(tr_list({RX(tr_tuple({RX(o.theme_name),RX(K(0))}))})),RX(K(1)),RX(overrides())},K(2));tr_with(tr_call(tr_attr(tr_binary(RX(o.out),RX(ns.get("PLUGIN_FILE_NAME")),PyNumber_TrueDivide),"open"),{RX(K(3))}),[&](nb::object entered){tr_assign(o.f,std::move(entered));tr_call(tr_attr(o.f,"write"),{RX(o.source)});});return tr_call(ns.get("str"),{RX(o.out)});}
    nb::object overrides(){std::vector<nb::object> values={K(4),K(4),K(5),K(5),K(6),K(7)};nb::object result=tr_check(PyDict_New());for(size_t i=0;i<values.size();i+=2){if(PyDict_SetItem(result.ptr(),values[i].ptr(),values[i+1].ptr())<0){result.reset();for(auto it=values.rbegin();it!=values.rend();++it)it->reset();throw nb::python_error();}}for(auto it=values.rbegin();it!=values.rend();++it)it->reset();return result;}
    nb::object refresh(){tr_call(tr_attr(tr_index(RX(tr_index(RX(tr_call(tr_attr(o.self,"model_json_schema"))),RX(K(0)))),RX(K(1))),"update"),{}, {RX(tr_call(ns.get("available_themes")))},K(2));return nb::none();}
};
ThemeRegistrationLocals theme_registration_locals(int entry,nb::args args){ThemeRegistrationLocals o;o.entry=entry;auto put=[&](nb::object& field,Py_ssize_t index){tr_assign(field,nb::borrow<nb::object>(args[index]));};switch(entry){case 0:case 1:put(o.cls,0);put(o.value,1);break;case 2:case 14:put(o.self,0);break;case 3:put(o.theme_id,0);put(o.theme,1);put(o.source,2);break;case 4:case 5:put(o.theme_id,0);break;case 7:put(o.themes,0);break;case 8:put(o.path,0);break;case 9:put(o.path_or_xml,0);put(o.color,1);put(o.opacity,2);break;case 10:put(o.svg_paths,0);put(o.colors,1);put(o.opacities,2);put(o.theme_override,3);break;case 11:put(o.dest,0);put(o.svg_paths,1);put(o.colors,2);put(o.opacities,3);put(o.theme_override,4);break;case 12:put(o.theme_name,0);break;case 13:put(o.theme_name,0);put(o.source,1);break;case 6:break;default:throw nb::value_error("unknown theme registration source entry");}return o;}
nb::object theme_registration_run(int entry,nb::dict namespace_,ThemeRegistrationLocals& owner,nb::tuple metadata,nb::object provider){TRPolicy p(std::move(namespace_),owner,std::move(metadata),std::move(provider));switch(entry){case 0:return p.syntax_style();case 1:return p.font_size();case 2:return p.rgb_dict();case 3:return p.register_theme();case 4:return p.unregister();case 5:return p.available();case 6:return p.rebuild();case 7:return p.install();case 8:return p.raw_svg();case 9:return p.colorized();case 11:return p.write();case 12:return p.theme_path();case 13:return p.build();case 14:return p.refresh();default:throw nb::value_error("unknown theme registration source entry");}}
struct ThemeSvgIterator{
    nb::object owner_object,namespace_,metadata,iterator;bool started=false,closed=false,running=false;
    ThemeSvgIterator(nb::object owner,nb::dict ns,nb::tuple constants):owner_object(std::move(owner)),namespace_(std::move(ns)),metadata(std::move(constants)){}
    ThemeSvgIterator(ThemeSvgIterator&&)=default;
    void detach(){nb::object old_iterator=std::move(iterator);old_iterator.reset();nb::object owner=std::move(owner_object),ns=std::move(namespace_),constants=std::move(metadata);owner.reset();ns.reset();constants.reset();}
    void clear_source(){nb::object old_iterator=std::move(iterator);old_iterator.reset();if(owner_object.is_valid()&&nb::inst_ready(owner_object))nb::inst_ptr<ThemeRegistrationLocals>(owner_object)->clear();detach();}
    // A consumed errored delegate does not own the separately retained facade
    // source owner exclusively. Its last reference releases fields only when
    // actual saved traceback/envelope roots release that owner.
    ~ThemeSvgIterator(){detach();}
    nb::object send(nb::object sent){if(running)throw nb::value_error("generator already executing");if(closed)throw nb::stop_iteration();if(!started&&!sent.is_none())throw nb::type_error("can't send non-None value to a just-started generator");sent.reset();running=true;
        try{auto& o=*nb::inst_ptr<ThemeRegistrationLocals>(owner_object);TRPolicy p(nb::borrow<nb::dict>(namespace_),o,nb::borrow<nb::tuple>(metadata));
            if(!started){started=true;if(!tr_truth(o.theme_override))tr_assign(o.theme_override,tr_check(PyDict_New()));tr_assign(o.ALIAS_T,p.K(0));iterator=tr_iter(tr_call(p.ns.get("product"),{RX(o.colors),RX(o.svg_paths),RX(o.opacities)}));}
            nb::object value=tr_next(iterator);if(!value.is_valid()){running=false;closed=true;clear_source();throw nb::stop_iteration();}auto parts=tr_unpack_three(std::move(value));tr_assign(o.color,std::move(parts[0]));tr_assign(o.path,std::move(parts[1]));tr_assign(o.op,std::move(parts[2]));tr_assign(o.clrkey,o.color);tr_assign(o.svg_stem,tr_attr(tr_call(p.ns.get("Path"),{RX(o.path)}),"stem"));
            if(tr_truth(tr_call(p.ns.get("isinstance"),{RX(o.color),RX(p.ns.get("tuple"))}))){tr_assign(o.get_theme,p.import_member(1));auto color_parts=tr_unpack_pair(o.color);tr_assign(o.clrkey,std::move(color_parts[0]));tr_assign(o.theme_key,std::move(color_parts[1]));tr_assign(o.theme_key,tr_call(tr_attr(o.theme_override,"get"),{RX(o.svg_stem),RX(o.theme_key)}));tr_assign(o.color,tr_call(tr_attr(tr_call(p.ns.get("getattr"),{RX(tr_call(o.get_theme,{RX(o.clrkey)})),RX(o.theme_key)}),"as_hex")));}
            if(tr_truth(tr_compare(RX(o.op),RX(p.K(2)),Py_EQ)))tr_assign(o.op_key,p.K(3));else{nb::object scaled=tr_binary(RX(o.op),RX(p.K(4)),PyNumber_Multiply);nb::object formatted=tr_check(PyObject_Format(scaled.ptr(),p.K(5).ptr()));scaled.reset();tr_assign(o.op_key,tr_join({RX(p.K(6)),RX(formatted)}));}
            tr_assign(o.alias,tr_call(tr_attr(o.ALIAS_T,"format"),{}, {RX(o.clrkey),RX(o.svg_stem),RX(o.op_key)},p.K(7)));nb::object result=tr_tuple({RX(o.alias),RX(tr_call(p.ns.get("get_colorized_svg"),{RX(o.path),RX(o.color),RX(o.op)}))});running=false;return result;
        }catch(const nb::python_error& error){running=false;closed=true;iterator.reset();if(error.matches(PyExc_StopIteration))collections::raise_exception(error.value(),true);throw;}catch(...){running=false;throw;}
    }
    void close(){if(running)throw nb::value_error("generator already executing");if(closed)return;closed=true;clear_source();}
    nb::object throw_(nb::args args){if(running)throw nb::value_error("generator already executing");nb::object error=collections::normalize_throw(args);bool was_closed=closed;closed=true;iterator.reset();collections::raise_exception(error,!was_closed);}
};
int tr_iterator_traverse(PyObject* self,visitproc visit,void* arg){Py_VISIT(Py_TYPE(self));if(nb::inst_ready(self)){auto& o=*nb::inst_ptr<ThemeSvgIterator>(self);Py_VISIT(o.owner_object.ptr());Py_VISIT(o.namespace_.ptr());Py_VISIT(o.metadata.ptr());Py_VISIT(o.iterator.ptr());}return 0;}
int tr_iterator_clear(PyObject* self){if(nb::inst_ready(self))nb::inst_ptr<ThemeSvgIterator>(self)->detach();return 0;}
PyType_Slot tr_iterator_slots[]={{Py_tp_traverse,reinterpret_cast<void*>(tr_iterator_traverse)},{Py_tp_clear,reinterpret_cast<void*>(tr_iterator_clear)},{0,nullptr}};
void theme_registration_icons_discover(nb::dict namespace_,nb::tuple metadata){ThemeRegistrationGlobals ns(std::move(namespace_));nb::object cursor_object=nb::cast(ThemeRegistrationLocals());auto& cursor=*nb::inst_ptr<ThemeRegistrationLocals>(cursor_object);cursor.iterator=tr_iter(tr_call(tr_attr(ns.get("ICON_PATH"),"iterdir")));nb::object result;
    try{result=tr_check(PyDict_New());while(nb::object x=tr_next(cursor.iterator)){tr_assign(cursor.x,std::move(x));if(tr_truth(tr_compare(RX(tr_attr(cursor.x,"suffix")),RX(tr_literal(metadata,0)),Py_EQ))){nb::object key=tr_attr(cursor.x,"stem"),value=tr_call(ns.get("str"),{RX(cursor.x)});int status=PyDict_SetItem(result.ptr(),key.ptr(),value.ptr());key.reset();value.reset();if(status<0)throw nb::python_error();}}}
    catch(...){cursor.iterator.reset();result.reset();cursor.x.reset();cursor_object.reset();throw;}
    cursor.iterator.reset();cursor.x.reset();cursor_object.reset();ns.store("ICONS",std::move(result));
}
#undef RX
} // namespace
void bind_theme_registration_icons(nb::module_& m){
    auto owner=nb::class_<ThemeRegistrationLocals>(m,"ThemeRegistrationLocals",nb::is_weak_referenceable(),nb::type_slots(tr_owner_slots));
    owner.def_prop_ro("cls",[](const ThemeRegistrationLocals& o){return o.cls.is_valid()?o.cls:nb::none();});
    owner.def_prop_ro("value",[](const ThemeRegistrationLocals& o){return o.value.is_valid()?o.value:nb::none();});
    owner.def_prop_ro("STYLE_MAP",[](const ThemeRegistrationLocals& o){return o.STYLE_MAP.is_valid()?o.STYLE_MAP:nb::none();});
    owner.def_prop_ro("valid_styles",[](const ThemeRegistrationLocals& o){return o.valid_styles.is_valid()?o.valid_styles:nb::none();});
    owner.def_prop_ro("self",[](const ThemeRegistrationLocals& o){return o.self.is_valid()?o.self:nb::none();});
    owner.def_prop_ro("th",[](const ThemeRegistrationLocals& o){return o.th.is_valid()?o.th:nb::none();});
    owner.def_prop_ro("k",[](const ThemeRegistrationLocals& o){return o.k.is_valid()?o.k:nb::none();});
    owner.def_prop_ro("v",[](const ThemeRegistrationLocals& o){return o.v.is_valid()?o.v:nb::none();});
    owner.def_prop_ro("theme_id",[](const ThemeRegistrationLocals& o){return o.theme_id.is_valid()?o.theme_id:nb::none();});
    owner.def_prop_ro("theme",[](const ThemeRegistrationLocals& o){return o.theme.is_valid()?o.theme:nb::none();});
    owner.def_prop_ro("source",[](const ThemeRegistrationLocals& o){return o.source.is_valid()?o.source:nb::none();});
    owner.def_prop_ro("plugin_name_file",[](const ThemeRegistrationLocals& o){return o.plugin_name_file.is_valid()?o.plugin_name_file:nb::none();});
    owner.def_prop_ro("plugin_name",[](const ThemeRegistrationLocals& o){return o.plugin_name.is_valid()?o.plugin_name:nb::none();});
    owner.def_prop_ro("get_settings",[](const ThemeRegistrationLocals& o){return o.get_settings.is_valid()?o.get_settings:nb::none();});
    owner.def_prop_ro("settings",[](const ThemeRegistrationLocals& o){return o.settings.is_valid()?o.settings:nb::none();});
    owner.def_prop_ro("themes",[](const ThemeRegistrationLocals& o){return o.themes.is_valid()?o.themes:nb::none();});
    owner.def_prop_ro("npe2",[](const ThemeRegistrationLocals& o){return o.npe2.is_valid()?o.npe2:nb::none();});
    owner.def_prop_ro("manifest",[](const ThemeRegistrationLocals& o){return o.manifest.is_valid()?o.manifest:nb::none();});
    owner.def_prop_ro("theme_dict",[](const ThemeRegistrationLocals& o){return o.theme_dict.is_valid()?o.theme_dict:nb::none();});
    owner.def_prop_ro("theme_info",[](const ThemeRegistrationLocals& o){return o.theme_info.is_valid()?o.theme_info:nb::none();});
    owner.def_prop_ro("theme_colors",[](const ThemeRegistrationLocals& o){return o.theme_colors.is_valid()?o.theme_colors:nb::none();});
    owner.def_prop_ro("path",[](const ThemeRegistrationLocals& o){return o.path.is_valid()?o.path:nb::none();});
    owner.def_prop_ro("path_or_xml",[](const ThemeRegistrationLocals& o){return o.path_or_xml.is_valid()?o.path_or_xml:nb::none();});
    owner.def_prop_ro("color",[](const ThemeRegistrationLocals& o){return o.color.is_valid()?o.color:nb::none();});
    owner.def_prop_ro("opacity",[](const ThemeRegistrationLocals& o){return o.opacity.is_valid()?o.opacity:nb::none();});
    owner.def_prop_ro("xml",[](const ThemeRegistrationLocals& o){return o.xml.is_valid()?o.xml:nb::none();});
    owner.def_prop_ro("svg_paths",[](const ThemeRegistrationLocals& o){return o.svg_paths.is_valid()?o.svg_paths:nb::none();});
    owner.def_prop_ro("colors",[](const ThemeRegistrationLocals& o){return o.colors.is_valid()?o.colors:nb::none();});
    owner.def_prop_ro("opacities",[](const ThemeRegistrationLocals& o){return o.opacities.is_valid()?o.opacities:nb::none();});
    owner.def_prop_ro("theme_override",[](const ThemeRegistrationLocals& o){return o.theme_override.is_valid()?o.theme_override:nb::none();});
    owner.def_prop_ro("ALIAS_T",[](const ThemeRegistrationLocals& o){return o.ALIAS_T.is_valid()?o.ALIAS_T:nb::none();});
    owner.def_prop_ro("op",[](const ThemeRegistrationLocals& o){return o.op.is_valid()?o.op:nb::none();});
    owner.def_prop_ro("clrkey",[](const ThemeRegistrationLocals& o){return o.clrkey.is_valid()?o.clrkey:nb::none();});
    owner.def_prop_ro("svg_stem",[](const ThemeRegistrationLocals& o){return o.svg_stem.is_valid()?o.svg_stem:nb::none();});
    owner.def_prop_ro("get_theme",[](const ThemeRegistrationLocals& o){return o.get_theme.is_valid()?o.get_theme:nb::none();});
    owner.def_prop_ro("theme_key",[](const ThemeRegistrationLocals& o){return o.theme_key.is_valid()?o.theme_key:nb::none();});
    owner.def_prop_ro("op_key",[](const ThemeRegistrationLocals& o){return o.op_key.is_valid()?o.op_key:nb::none();});
    owner.def_prop_ro("alias",[](const ThemeRegistrationLocals& o){return o.alias.is_valid()?o.alias:nb::none();});
    owner.def_prop_ro("dest",[](const ThemeRegistrationLocals& o){return o.dest.is_valid()?o.dest:nb::none();});
    owner.def_prop_ro("svgs",[](const ThemeRegistrationLocals& o){return o.svgs.is_valid()?o.svgs:nb::none();});
    owner.def_prop_ro("svg",[](const ThemeRegistrationLocals& o){return o.svg.is_valid()?o.svg:nb::none();});
    owner.def_prop_ro("theme_name",[](const ThemeRegistrationLocals& o){return o.theme_name.is_valid()?o.theme_name:nb::none();});
    owner.def_prop_ro("out",[](const ThemeRegistrationLocals& o){return o.out.is_valid()?o.out:nb::none();});
    owner.def_prop_ro("f",[](const ThemeRegistrationLocals& o){return o.f.is_valid()?o.f:nb::none();});
    owner.def_prop_ro("x",[](const ThemeRegistrationLocals& o){return o.x.is_valid()?o.x:nb::none();});
    owner.def_prop_ro("iterator",[](const ThemeRegistrationLocals& o){return o.iterator.is_valid()?o.iterator:nb::none();});

    nb::class_<ThemeSvgIterator>(m,"ThemeSvgIterator",nb::is_weak_referenceable(),nb::type_slots(tr_iterator_slots)).def("__iter__",[](nb::object self){return self;}).def("__next__",[](ThemeSvgIterator& o){return o.send(nb::none());}).def("send",&ThemeSvgIterator::send,nb::arg("value").none()).def("throw",&ThemeSvgIterator::throw_).def("close",&ThemeSvgIterator::close);
    m.def("theme_registration_locals",&theme_registration_locals,nb::arg("entry"),nb::arg("parameters"));
    m.def("theme_registration_run",&theme_registration_run,nb::arg("entry"),nb::arg("namespace"),nb::arg("locals_owner"),nb::arg("metadata"),nb::arg("super_provider").none()=nb::none());
    m.def("theme_registration_generator",[](nb::dict ns,nb::object owner,nb::tuple constants){return ThemeSvgIterator(std::move(owner),std::move(ns),std::move(constants));},nb::arg("namespace"),nb::arg("locals_owner"),nb::arg("metadata"));
    m.def("theme_registration_icons_discover",&theme_registration_icons_discover,nb::arg("namespace"),nb::arg("metadata"));
}
} // namespace nx
