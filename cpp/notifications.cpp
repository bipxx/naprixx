#include "arrays.hpp"
#include "event_protocol.hpp"
#include "collection_protocol.hpp"
#include <nanobind/stl/string.h>
#include <array>
#include <cstddef>
#include <structmember.h>
#include <utility>
#include <vector>

namespace nx {
namespace {
nb::object nf_check(PyObject* value) { if (!value) throw nb::python_error(); return nb::steal<nb::object>(value); }
nb::object nf_attr(nb::object value, const char* name) { return nf_check(PyObject_GetAttrString(value.ptr(), name)); }
nb::object nf_item(nb::object value, nb::object key) { return nf_check(PyObject_GetItem(value.ptr(), key.ptr())); }
nb::object nf_call0(nb::object fn) { return nf_check(PyObject_CallNoArgs(fn.ptr())); }
nb::object nf_call1(nb::object fn, nb::object value) { return nf_check(PyObject_CallOneArg(fn.ptr(), value.ptr())); }
nb::object nf_call2(nb::object fn, nb::object a, nb::object b) { return nf_check(PyObject_CallFunctionObjArgs(fn.ptr(), a.ptr(), b.ptr(), nullptr)); }
nb::object nf_call3(nb::object fn, nb::object a, nb::object b, nb::object c) { return nf_check(PyObject_CallFunctionObjArgs(fn.ptr(), a.ptr(), b.ptr(), c.ptr(), nullptr)); }
nb::object nf_kw(nb::object fn, nb::tuple args, nb::dict kwargs) { return nf_check(PyObject_Call(fn.ptr(), args.ptr(), kwargs.ptr())); }
nb::tuple nf_star(nb::handle fn,nb::object value){
    PyObject* result=PySequence_Tuple(value.ptr());if(result)return nb::steal<nb::tuple>(result);
    if(PyErr_ExceptionMatches(PyExc_TypeError)&&Py_TYPE(value.ptr())->tp_iter==nullptr&&!PySequence_Check(value.ptr())){
        PyErr_Clear();nb::object name=nf_check(_PyObject_FunctionStr(fn.ptr()));PyErr_Format(PyExc_TypeError,"%U argument after * must be an iterable, not %.200s",name.ptr(),Py_TYPE(value.ptr())->tp_name);
    }throw nb::python_error();
}
void nf_merge_kwargs(nb::handle fn,nb::dict& target,nb::dict source){
    if(_PyDict_MergeEx(target.ptr(),source.ptr(),2)>=0)return;
    nb::python_error error;if(!error.matches(PyExc_KeyError))throw error;
    nb::object key=nf_item(nf_attr(nb::borrow<nb::object>(error.value()),"args"),nb::int_(0));nb::object name=nf_check(_PyObject_FunctionStr(fn.ptr()));PyErr_Format(PyExc_TypeError,"%U got multiple values for keyword argument '%S'",name.ptr(),key.ptr());throw nb::python_error();
}
void nf_store(nb::object target, const char* name, nb::object value) { if (PyObject_SetAttrString(target.ptr(), name, value.ptr()) < 0) throw nb::python_error(); }
void nf_assign(nb::object& target, nb::object value) { nb::object old=std::move(target);target=std::move(value);old.reset(); }
nb::object nf_optional(const nb::object& value) { return value.is_valid()?value:nb::none(); }
bool nf_truth(nb::object value) { int n=PyObject_IsTrue(value.ptr());if(n<0)throw nb::python_error();return n!=0; }
bool nf_contains(nb::object container, nb::object key) { int n=PySequence_Contains(container.ptr(),key.ptr());if(n<0)throw nb::python_error();return n!=0; }
nb::object nf_compare(nb::object a,nb::object b,int op) { return nf_check(PyObject_RichCompare(a.ptr(),b.ptr(),op)); }
nb::object nf_format(nb::object value) { return nf_check(PyObject_Format(value.ptr(),nullptr)); }
nb::object nf_add(nb::object a,nb::object b) { return nf_check(PyNumber_Add(a.ptr(),b.ptr())); }
nb::object nf_sub(nb::object a,nb::object b) { return nf_check(PyNumber_Subtract(a.ptr(),b.ptr())); }
nb::object nf_iter(nb::object value) { return nf_check(PyObject_GetIter(value.ptr())); }
nb::object nf_next(nb::handle value) { PyObject* result=PyIter_Next(value.ptr());if(!result&&PyErr_Occurred())throw nb::python_error();return nb::steal<nb::object>(result); }
[[noreturn]] void nf_raise(nb::object value) {
    if(PyExceptionClass_Check(value.ptr())) {
        nb::object klass=std::move(value);value=nf_call0(klass);
        if(!PyExceptionInstance_Check(value.ptr())){PyErr_Format(PyExc_TypeError,"calling %R should have returned an instance of BaseException, not %R",klass.ptr(),reinterpret_cast<PyObject*>(Py_TYPE(value.ptr())));throw nb::python_error();}
    }
    if(!PyExceptionInstance_Check(value.ptr()))PyErr_SetString(PyExc_TypeError,"exceptions must derive from BaseException");
    else PyErr_SetObject(reinterpret_cast<PyObject*>(Py_TYPE(value.ptr())),value.ptr());
    throw nb::python_error();
}
void nf_except_type(nb::handle value) { bool valid=PyExceptionClass_Check(value.ptr());if(PyTuple_Check(value.ptr())){valid=true;for(Py_ssize_t i=0;i<PyTuple_GET_SIZE(value.ptr());++i)if(!PyExceptionClass_Check(PyTuple_GET_ITEM(value.ptr(),i))){valid=false;break;}}if(!valid){PyErr_SetString(PyExc_TypeError,"catching classes that do not inherit from BaseException is not allowed");throw nb::python_error();} }
nb::object nf_string(std::vector<nb::object> pieces) {
    nb::list list;for(auto& p:pieces)list.append(p);
    PyObject* result=PyUnicode_Join(nb::str("").ptr(),list.ptr());list.reset();
    for(size_t i=pieces.size();i>0;--i)pieces[i-1].reset();return nf_check(result);
}
struct NotificationGlobals {
    nb::dict values;nb::object builtins;
    explicit NotificationGlobals(nb::dict v):values(std::move(v)),builtins(nb::borrow<nb::object>(PyEval_GetBuiltins())){}
    NotificationGlobals(nb::dict v,nb::object b):values(std::move(v)),builtins(std::move(b)){}
    nb::object builtin(const char* name) const {
        nb::object key=nb::str(name);PyObject* value=PyObject_GetItem(builtins.ptr(),key.ptr());
        if(value)return nb::steal<nb::object>(value);if(!PyErr_ExceptionMatches(PyExc_KeyError))throw nb::python_error();PyErr_Clear();
        if(std::string(name)=="__import__"){PyErr_SetString(PyExc_ImportError,"__import__ not found");throw nb::python_error();}
        nb::object text=nf_check(PyUnicode_FromFormat("name '%s' is not defined",name));nb::object error=nf_call1(nb::borrow<nb::object>(PyExc_NameError),std::move(text));nf_store(error,"name",key);nf_raise(std::move(error));
    }
    nb::object get(const char* name) const { PyObject* value=PyDict_GetItemString(values.ptr(),name);if(value)return nb::borrow<nb::object>(value);if(PyErr_Occurred())throw nb::python_error();return builtin(name); }
    nb::object import_module(nb::object name,nb::object fromlist) const {
        nb::object importer=builtin("__import__");PyObject* raw=PyEval_GetLocals();if(!raw&&PyErr_Occurred())throw nb::python_error();nb::object locals=nb::borrow<nb::object>(raw?raw:Py_None);nb::object zero=nb::int_(0);
        return nf_check(PyObject_CallFunctionObjArgs(importer.ptr(),name.ptr(),values.ptr(),locals.ptr(),fromlist.ptr(),zero.ptr(),nullptr));
    }
};
template<class F> nb::object nf_unary(const NotificationGlobals& ns,const char* name,F&& value) { nb::object fn=ns.get(name);nb::object arg=value();return nf_call1(std::move(fn),std::move(arg)); }

// Original assigned locals only. Instance state is solely the live dictionary.
struct NotificationLocals {
    nb::object msg,actions,get_tb_formatter,fmt,exc_info,category,get_settings,member;
    NotificationLocals()=default;NotificationLocals(NotificationLocals&&)=default;~NotificationLocals(){clear();}
    void clear(){std::array<nb::object,8> old={std::move(msg),std::move(actions),std::move(get_tb_formatter),std::move(fmt),std::move(exc_info),std::move(category),std::move(get_settings),std::move(member)};for(auto& x:old)x.reset();}
};
struct TracebackLocals {
    nb::object IPython,traceback,format_exc_info,vbtb,ansi_string,html,tb_text;
    TracebackLocals()=default;TracebackLocals(TracebackLocals&&)=default;~TracebackLocals(){clear();}
    void clear(){std::array<nb::object,7> old={std::move(IPython),std::move(format_exc_info),std::move(traceback),std::move(vbtb),std::move(ansi_string),std::move(html),std::move(tb_text)};for(auto& x:old)x.reset();}
};
#define NF_VISIT_NOTIFICATION(s) Py_VISIT(s.msg.ptr());Py_VISIT(s.actions.ptr());Py_VISIT(s.get_tb_formatter.ptr());Py_VISIT(s.fmt.ptr());Py_VISIT(s.exc_info.ptr());Py_VISIT(s.category.ptr());Py_VISIT(s.get_settings.ptr());Py_VISIT(s.member.ptr())
#define NF_VISIT_TRACEBACK(s) Py_VISIT(s.IPython.ptr());Py_VISIT(s.traceback.ptr());Py_VISIT(s.format_exc_info.ptr());Py_VISIT(s.vbtb.ptr());Py_VISIT(s.ansi_string.ptr());Py_VISIT(s.html.ptr());Py_VISIT(s.tb_text.ptr())
int nf_locals_traverse(PyObject* self,visitproc visit,void* arg){Py_VISIT(Py_TYPE(self));if(nb::inst_ready(self)){auto& s=*nb::inst_ptr<NotificationLocals>(self);NF_VISIT_NOTIFICATION(s);}return 0;}
int nf_locals_clear(PyObject* self){if(nb::inst_ready(self))nb::inst_ptr<NotificationLocals>(self)->clear();return 0;}
int nf_tb_traverse(PyObject* self,visitproc visit,void* arg){Py_VISIT(Py_TYPE(self));if(nb::inst_ready(self)){auto& s=*nb::inst_ptr<TracebackLocals>(self);NF_VISIT_TRACEBACK(s);}return 0;}
int nf_tb_clear(PyObject* self){if(nb::inst_ready(self))nb::inst_ptr<TracebackLocals>(self)->clear();return 0;}
PyType_Slot nf_local_slots[]={{Py_tp_traverse,reinterpret_cast<void*>(nf_locals_traverse)},{Py_tp_clear,reinterpret_cast<void*>(nf_locals_clear)},{0,nullptr}};
PyType_Slot nf_tb_slots[]={{Py_tp_traverse,reinterpret_cast<void*>(nf_tb_traverse)},{Py_tp_clear,reinterpret_cast<void*>(nf_tb_clear)},{0,nullptr}};

struct NotificationNamespace {PyObject_HEAD PyObject* dictionary;PyObject* weakrefs;};
int nf_namespace_traverse(PyObject* self,visitproc visit,void* arg){auto* s=reinterpret_cast<NotificationNamespace*>(self);Py_VISIT(s->dictionary);Py_VISIT(Py_TYPE(self));return 0;}
int nf_namespace_clear(PyObject* self){Py_CLEAR(reinterpret_cast<NotificationNamespace*>(self)->dictionary);return 0;}
int nf_set_dict(PyObject* self,PyObject* value,void*){if(value&&!PyDict_Check(value)){PyErr_Format(PyExc_TypeError,"__dict__ must be set to a dictionary, not a '%.200s'",Py_TYPE(value)->tp_name);return -1;}Py_XSETREF(reinterpret_cast<NotificationNamespace*>(self)->dictionary,Py_XNewRef(value));return 0;}
PyObject* nf_get_weak(PyObject* self,void*){PyObject* value=reinterpret_cast<NotificationNamespace*>(self)->weakrefs;return Py_NewRef(value?value:Py_None);}
PyMemberDef nf_members[]={{const_cast<char*>("__dictoffset__"),T_PYSSIZET,offsetof(NotificationNamespace,dictionary),READONLY,nullptr},{const_cast<char*>("__weaklistoffset__"),T_PYSSIZET,offsetof(NotificationNamespace,weakrefs),READONLY,nullptr},{nullptr,0,0,0,nullptr}};
PyGetSetDef nf_getsets[]={{const_cast<char*>("__dict__"),PyObject_GenericGetDict,nf_set_dict,nullptr,nullptr},{const_cast<char*>("__weakref__"),nf_get_weak,nullptr,nullptr,nullptr},{nullptr,nullptr,nullptr,nullptr,nullptr}};
PyType_Slot nf_namespace_slots[]={{Py_tp_members,nf_members},{Py_tp_getset,nf_getsets},{Py_tp_traverse,reinterpret_cast<void*>(nf_namespace_traverse)},{Py_tp_clear,reinterpret_cast<void*>(nf_namespace_clear)},{0,nullptr}};

// IMPORT_FROM's ordinary successful and circular-import paths; metadata error
// diagnostics follow the accepted CPython3.12 primitive (wider versions open).
nb::object nf_import_from(nb::handle module,const char* name){
    PyObject* value=PyObject_GetAttrString(module.ptr(),name);if(value)return nb::steal<nb::object>(value);if(!PyErr_ExceptionMatches(PyExc_AttributeError))throw nb::python_error();PyErr_Clear();
    nb::object modname=nb::steal<nb::object>(PyObject_GetAttrString(module.ptr(),"__name__"));
    if(!modname.is_valid()||!PyUnicode_Check(modname.ptr())){PyErr_Clear();PyErr_Format(PyExc_ImportError,"cannot import name '%s' from '<unknown module name>' (unknown location)",name);throw nb::python_error();}
    nb::object fullname=nf_check(PyUnicode_FromFormat("%U.%s",modname.ptr(),name));value=PyImport_GetModule(fullname.ptr());if(value)return nb::steal<nb::object>(value);if(PyErr_Occurred())throw nb::python_error();
    nb::object file=nb::steal<nb::object>(PyModule_GetFilenameObject(module.ptr()));if(!file.is_valid()){PyErr_Clear();file=nb::none();}
    nb::object text=file.is_none()?nf_check(PyUnicode_FromFormat("cannot import name '%s' from '%U' (unknown location)",name,modname.ptr())):nf_check(PyUnicode_FromFormat("cannot import name '%s' from '%U' (%U)",name,modname.ptr(),file.ptr()));PyErr_SetImportError(text.ptr(),modname.ptr(),file.ptr());throw nb::python_error();
}

template<class F> void nf_with(nb::object manager,F&& body){
    nb::object enter=collections::context_method(manager,"__enter__",false),exit=collections::context_method(manager,"__exit__",true);manager.reset();nf_call0(std::move(enter));
    try{body();}
    catch(const nb::python_error& error){
        bool suppressed=false;
        try{{events::HandledException handled(error.value());nb::object tb=error.traceback();if(!tb.is_valid())tb=nb::none();suppressed=nf_truth(nf_call3(exit,nb::borrow<nb::object>(error.type()),nb::borrow<nb::object>(error.value()),std::move(tb)));}}
        catch(...){exit.reset();throw;}
        exit.reset();if(!suppressed)throw;return;
    }
    nf_call3(std::move(exit),nb::none(),nb::none(),nb::none());
}

nb::object severity_icon(nb::object self,nb::tuple icons){
    nb::dict values;const char* names[]={"ERROR","WARNING","INFO","DEBUG","NONE"};std::array<nb::object,5> keys;
    for(size_t i=0;i<5;++i)keys[i]=nf_attr(self,names[i]);for(size_t i=0;i<5;++i){values[keys[i]]=icons[i];keys[i].reset();}
    return nf_item(std::move(values),self);
}
nb::object severity_compare(nb::object self,nb::object other,nb::dict globals,int op){
    NotificationGlobals ns(std::move(globals));nb::object mapping=ns.get("name2num");nb::object key=nf_unary(ns,"str",[&]{return self;});nb::object left=nf_item(std::move(mapping),std::move(key));
    mapping=ns.get("name2num");key=nf_unary(ns,"str",[&]{return other;});nb::object right=nf_item(std::move(mapping),std::move(key));return nf_compare(std::move(left),std::move(right),op);
}
nb::object severity_equal(nb::object self,nb::object other,nb::dict globals){NotificationGlobals ns(std::move(globals));nb::object a=nf_unary(ns,"str",[&]{return self;});nb::object b=nf_unary(ns,"str",[&]{return other;});return nf_compare(std::move(a),std::move(b),Py_EQ);}
nb::object severity_hash(nb::object self,nb::dict globals){NotificationGlobals ns(std::move(globals));return nf_unary(ns,"hash",[&]{return nf_attr(self,"value");});}
void notification_init(nb::object self,nb::object message,nb::object severity,nb::object actions,nb::dict kwargs,nb::dict globals,nb::object super_provider){
    NotificationGlobals ns(std::move(globals));nb::object factory=ns.get("NotificationSeverity");nf_store(self,"severity",nf_call1(std::move(factory),severity));
    nb::object base=nf_call1(std::move(super_provider),self);nb::object text=nf_unary(ns,"str",[&]{return nf_attr(self,"severity");});text=nf_call0(nf_attr(std::move(text),"lower"));
    nb::dict expanded;expanded["type_name"]=text;text.reset();
    nf_merge_kwargs(base,expanded,std::move(kwargs));nf_kw(std::move(base),nb::tuple(),std::move(expanded));
    nf_store(self,"_message",std::move(message));nf_store(self,"actions",std::move(actions));nb::object now=nf_attr(ns.get("datetime"),"now");nf_store(self,"date",nf_call0(std::move(now)));
}
nb::object notification_message(nb::object self){return nf_attr(self,"_message");}
void notification_set_message(nb::object self,nb::object value){nf_store(std::move(self),"_message",std::move(value));}
nb::object notification_from_exception(nb::object exc,nb::dict kwargs,nb::dict globals){NotificationGlobals ns(std::move(globals));nb::object factory=ns.get("ErrorNotification");nb::tuple arguments=nb::make_tuple(exc);nb::dict expanded=nb::steal<nb::dict>(nf_check(PyDict_Copy(kwargs.ptr())).release().ptr());return nf_kw(std::move(factory),std::move(arguments),std::move(expanded));}
nb::object notification_from_warning(nb::object warning,nb::dict kwargs,nb::dict globals){NotificationGlobals ns(std::move(globals));nb::object factory=ns.get("WarningNotification");nb::tuple arguments=nb::make_tuple(warning);nb::dict expanded=nb::steal<nb::dict>(nf_check(PyDict_Copy(kwargs.ptr())).release().ptr());return nf_kw(std::move(factory),std::move(arguments),std::move(expanded));}
nb::object notification_str(nb::object self,nb::dict globals,nb::object separator){NotificationGlobals ns(std::move(globals));nb::object severity=nf_unary(ns,"str",[&]{return nf_attr(self,"severity");});severity=nf_call0(nf_attr(std::move(severity),"upper"));nb::object first=nf_format(std::move(severity));nb::object second=nf_format(nf_attr(self,"message"));return nf_string({std::move(first),std::move(separator),std::move(second)});}
void notification_exception_init(nb::object self,nb::object exception,nb::dict globals,nb::object super_provider,nb::tuple attribute_literals,NotificationLocals& s,bool warning,nb::object filename,nb::object lineno){
    NotificationGlobals ns(std::move(globals));nb::object get=ns.get("getattr");nb::object name=nb::borrow<nb::object>(attribute_literals[0]);nb::object fallback=nf_unary(ns,"str",[&]{return exception;});nf_assign(s.msg,nf_call3(std::move(get),exception,std::move(name),std::move(fallback)));
    get=ns.get("getattr");nf_assign(s.actions,nf_call3(std::move(get),exception,nb::borrow<nb::object>(attribute_literals[1]),nb::borrow<nb::object>(attribute_literals[2])));
    nb::object base=nf_call1(std::move(super_provider),self);nb::object severity=nf_attr(ns.get("NotificationSeverity"),warning?"WARNING":"ERROR");nf_call3(std::move(base),s.msg,std::move(severity),s.actions);
    nf_store(self,warning?"warning":"exception",std::move(exception));if(warning){nf_store(self,"filename",std::move(filename));nf_store(self,"lineno",std::move(lineno));}
}
nb::object notification_error_format(nb::object self,nb::dict globals,nb::tuple literals,NotificationLocals& s,int mode){
    NotificationGlobals ns(std::move(globals));{nb::object imported=ns.import_module(nb::borrow<nb::object>(literals[0]),nb::borrow<nb::object>(literals[1]));nf_assign(s.get_tb_formatter,nf_import_from(imported,"get_tb_formatter"));}
    nf_assign(s.fmt,nf_call0(s.get_tb_formatter));nb::object first=nf_attr(nf_attr(self,"exception"),"__class__");nb::object second=nf_attr(self,"exception");nb::object third=nf_attr(nf_attr(self,"exception"),"__traceback__");nf_assign(s.exc_info,nb::make_tuple(std::move(first),std::move(second),std::move(third)));
    nb::dict kwargs;kwargs["as_html"]=nb::bool_(mode==0);if(mode==1)kwargs["color"]=literals[2];return nf_kw(s.fmt,nb::make_tuple(s.exc_info),std::move(kwargs));
}
nb::object notification_warning_str(nb::object self,nb::dict globals,nb::tuple literals,NotificationLocals& s){
    NotificationGlobals ns(std::move(globals));nb::object type=nf_unary(ns,"type",[&]{return nf_attr(self,"warning");});nf_assign(s.category,nf_attr(std::move(type),"__name__"));
    nb::object filename=nf_format(nf_attr(self,"filename"));nb::object lineno=nf_format(nf_attr(self,"lineno"));nb::object category=nf_format(s.category);nb::object warning=nf_format(nf_attr(self,"warning"));
    return nf_string({std::move(filename),nb::borrow<nb::object>(literals[0]),std::move(lineno),nb::borrow<nb::object>(literals[1]),std::move(category),nb::borrow<nb::object>(literals[1]),std::move(warning),nb::borrow<nb::object>(literals[2])});
}
void notification_manager_init(nb::object self,nb::dict globals,nb::tuple literals){
    NotificationGlobals ns(std::move(globals));nf_store(self,"records",nb::list());nb::object env=nf_attr(ns.get("os"),"getenv");nb::object value=nf_call1(std::move(env),nb::borrow<nb::object>(literals[0]));nf_store(self,"exit_on_error",nb::bool_(nf_contains(nb::borrow<nb::object>(literals[1]),std::move(value))));
    env=nf_attr(ns.get("os"),"getenv");value=nf_call1(std::move(env),nb::borrow<nb::object>(literals[2]));nf_store(self,"catch_error",nb::bool_(!nf_contains(nb::borrow<nb::object>(literals[3]),std::move(value))));
    nb::object factory=ns.get("EventEmitter");nb::dict kwargs;kwargs["source"]=self;kwargs["event_class"]=ns.get("Notification");value=nf_kw(std::move(factory),nb::tuple(),std::move(kwargs));nf_store(self,"notification_ready",value);nf_store(self,"changed",std::move(value));
    nf_store(self,"_originals_except_hooks",nb::list());nf_store(self,"_original_showwarnings_hooks",nb::list());nf_store(self,"_originals_thread_except_hooks",nb::list());value=nf_call0(ns.get("set"));nf_store(self,"_seen_warnings",std::move(value));
}
nb::object notification_manager_enter(nb::object self){nf_call0(nf_attr(self,"install_hooks"));return self;}
void notification_manager_exit(nb::object self){nf_call0(nf_attr(self,"restore_hooks"));}
void notification_install_hooks(nb::object self,nb::dict globals){
    NotificationGlobals ns(std::move(globals));nb::object append=nf_attr(nf_attr(self,"_originals_thread_except_hooks"),"append");nb::object value=nf_attr(ns.get("threading"),"excepthook");nf_call1(std::move(append),std::move(value));value=nf_attr(self,"receive_thread_error");nf_store(ns.get("threading"),"excepthook",std::move(value));
    append=nf_attr(nf_attr(self,"_originals_except_hooks"),"append");value=nf_attr(ns.get("sys"),"excepthook");nf_call1(std::move(append),std::move(value));append=nf_attr(nf_attr(self,"_original_showwarnings_hooks"),"append");value=nf_attr(ns.get("warnings"),"showwarning");nf_call1(std::move(append),std::move(value));
    value=nf_attr(self,"receive_error");nf_store(ns.get("sys"),"excepthook",std::move(value));value=nf_attr(self,"receive_warning");nf_store(ns.get("warnings"),"showwarning",std::move(value));
}
void notification_restore_hooks(nb::object self,nb::dict globals){NotificationGlobals ns(std::move(globals));nb::object value=nf_call0(nf_attr(nf_attr(self,"_originals_thread_except_hooks"),"pop"));nf_store(ns.get("threading"),"excepthook",std::move(value));value=nf_call0(nf_attr(nf_attr(self,"_originals_except_hooks"),"pop"));nf_store(ns.get("sys"),"excepthook",std::move(value));value=nf_call0(nf_attr(nf_attr(self,"_original_showwarnings_hooks"),"pop"));nf_store(ns.get("warnings"),"showwarning",std::move(value));}
void notification_dispatch(nb::object self,nb::object value){nf_call1(nf_attr(nf_attr(self,"records"),"append"),value);nf_call1(nf_attr(self,"notification_ready"),std::move(value));}
void notification_thread_error(nb::object self,nb::object args){nb::object fn=nf_attr(self,"receive_error");nb::tuple sequence=nf_star(fn,std::move(args));nf_kw(std::move(fn),std::move(sequence),nb::dict());}
void notification_receive_error(nb::object self,nb::object exctype,nb::object value,nb::object traceback,nb::dict globals,nb::tuple literals){
    NotificationGlobals ns(std::move(globals));nb::object isinstance=ns.get("isinstance"),kind=ns.get("KeyboardInterrupt");if(nf_truth(nf_call2(std::move(isinstance),value,std::move(kind))))nf_call1(nf_attr(ns.get("sys"),"exit"),nb::borrow<nb::object>(literals[0]));
    if(nf_truth(nf_attr(self,"exit_on_error"))){nf_call3(nf_attr(ns.get("sys"),"__excepthook__"),exctype,value,traceback);nf_call1(nf_attr(ns.get("sys"),"exit"),nb::borrow<nb::object>(literals[1]));}
    if(!nf_truth(nf_attr(self,"catch_error"))){nf_call3(nf_attr(ns.get("sys"),"__excepthook__"),exctype,value,traceback);return;}
    nb::object dispatch=nf_attr(self,"dispatch"),factory=nf_attr(ns.get("Notification"),"from_exception");nf_call1(std::move(dispatch),nf_call1(std::move(factory),std::move(value)));
}
void notification_receive_warning(nb::object self,nb::object message,nb::object category,nb::object filename,nb::object lineno,nb::dict globals,NotificationLocals& s){
    NotificationGlobals ns(std::move(globals));nb::object instance=ns.get("isinstance"),kind=ns.get("str");bool text=nf_truth(nf_call2(std::move(instance),message,std::move(kind)));nf_assign(s.msg,text?message:nf_item(nf_attr(message,"args"),nb::int_(0)));
    nb::object key=nb::make_tuple(s.msg,category,filename,lineno);nb::object seen=nf_attr(self,"_seen_warnings");if(nf_contains(std::move(seen),std::move(key)))return;
    nb::object add=nf_attr(nf_attr(self,"_seen_warnings"),"add");key=nb::make_tuple(s.msg,category,filename,lineno);nf_call1(std::move(add),std::move(key));
    nb::object dispatch=nf_attr(self,"dispatch"),factory=nf_attr(ns.get("Notification"),"from_warning");nb::dict kwargs;kwargs["filename"]=filename;kwargs["lineno"]=lineno;nb::object notification=nf_kw(std::move(factory),nb::make_tuple(message),std::move(kwargs));nf_call1(std::move(dispatch),std::move(notification));
}
void notification_receive_info(nb::object self,nb::object message,nb::dict globals,nb::object info){NotificationGlobals ns(std::move(globals));nb::object dispatch=nf_attr(self,"dispatch"),factory=ns.get("Notification");nb::object value=nf_call2(std::move(factory),std::move(message),std::move(info));nf_call1(std::move(dispatch),std::move(value));}
void notification_show(nb::object message,nb::dict globals,const char* severity){NotificationGlobals ns(std::move(globals));nb::object dispatch=nf_attr(ns.get("notification_manager"),"dispatch"),factory=ns.get("Notification");nb::dict kwargs;kwargs["severity"]=nf_attr(ns.get("NotificationSeverity"),severity);nf_call1(std::move(dispatch),nf_kw(std::move(factory),nb::make_tuple(message),std::move(kwargs)));}
void notification_console(nb::object value,nb::dict globals,nb::tuple literals,NotificationLocals& s){
    NotificationGlobals ns(std::move(globals));try{
        {nb::object imported=ns.import_module(nb::borrow<nb::object>(literals[0]),nb::borrow<nb::object>(literals[1]));nf_assign(s.get_settings,nf_import_from(imported,"get_settings"));}
        nb::object left=nf_attr(value,"severity");nb::object settings=nf_call0(s.get_settings);nb::object right=nf_attr(nf_attr(std::move(settings),"application"),"console_notification_level");if(nf_truth(nf_compare(std::move(left),std::move(right),Py_LT)))return;nf_call1(ns.get("print"),value);
    }catch(const nb::python_error& error){events::HandledException handled(error.value());nb::object kind=ns.get("Exception");nf_except_type(kind);if(!PyErr_GivenExceptionMatches(error.value().ptr(),kind.ptr()))throw;nb::object logger=nf_attr(ns.get("logging"),"getLogger");logger=nf_call1(std::move(logger),nb::borrow<nb::object>(literals[2]));nf_call1(nf_attr(std::move(logger),"exception"),nb::borrow<nb::object>(literals[3]));throw;}
}

nb::object string_enum_generate(nb::object name){return nf_call0(nf_attr(std::move(name),"lower"));}
nb::object string_enum_str(nb::object self){return nf_attr(std::move(self),"value");}
[[noreturn]] void string_enum_value_error(const NotificationGlobals& ns,nb::object cls,nb::object value,nb::tuple literals){nb::object kind=ns.get("ValueError");nb::object first=nf_format(cls),second=nf_format(cls);nb::object type=nf_attr(ns.get("builtins"),"type");type=nf_call1(std::move(type),value);nb::object third=nf_format(std::move(type));nb::object text=nf_string({std::move(first),nb::borrow<nb::object>(literals[0]),std::move(second),nb::borrow<nb::object>(literals[1]),std::move(third)});nf_raise(nf_call1(std::move(kind),std::move(text)));}
nb::object string_enum_missing(nb::object cls,nb::object value,nb::dict globals,nb::tuple literals,NotificationLocals& s){
    NotificationGlobals ns(std::move(globals));nb::object instance=ns.get("isinstance"),kind=ns.get("StringEnum");if(nf_truth(nf_call2(std::move(instance),value,std::move(kind))))string_enum_value_error(ns,cls,value,literals);
    instance=ns.get("isinstance");kind=ns.get("str");if(nf_truth(nf_call2(std::move(instance),value,std::move(kind)))){nb::object iterator=nf_iter(cls);while(nb::object member=nf_next(iterator)){nf_assign(s.member,std::move(member));nb::object left=nf_attr(s.member,"value"),right=nf_call0(nf_attr(value,"lower"));if(nf_truth(nf_compare(std::move(left),std::move(right),Py_EQ)))return s.member;}return nb::none();}string_enum_value_error(ns,std::move(cls),std::move(value),literals);
}
nb::object string_enum_equal(nb::object self,nb::object other,nb::dict globals){NotificationGlobals ns(std::move(globals));nb::object first=nf_unary(ns,"type",[&]{return self;}),second=nf_unary(ns,"type",[&]{return other;});bool same=first.is(second);first.reset();second.reset();if(same)return nb::bool_(self.is(other));nb::object instance=ns.get("isinstance"),kind=ns.get("StringEnum");if(nf_truth(nf_call2(std::move(instance),other,std::move(kind))))return nb::bool_(false);instance=ns.get("isinstance");kind=ns.get("str");if(nf_truth(nf_call2(std::move(instance),other,std::move(kind)))){first=nf_unary(ns,"str",[&]{return self;});return nf_compare(std::move(first),other,Py_EQ);}return nb::bool_(false);}
nb::object string_enum_not_equal(nb::object self,nb::object other){return nb::bool_(!nf_truth(nf_call1(nf_attr(self,"__eq__"),std::move(other))));}
nb::object string_enum_hash(nb::object self,nb::dict globals){NotificationGlobals ns(std::move(globals));nb::object hash=ns.get("hash"),value=nf_unary(ns,"str",[&]{return self;});return nf_call1(std::move(hash),std::move(value));}
nb::object string_enum_keys(nb::object cls,nb::dict globals){NotificationGlobals ns(std::move(globals));nb::object list=ns.get("list"),map=ns.get("map"),str=ns.get("str");nb::object mapped=nf_call2(std::move(map),std::move(str),cls);return nf_call1(std::move(list),std::move(mapped));}

void nf_formatter_metadata(nb::object fn){nf_store(fn,"__name__",nb::str("format_exc_info"));nf_store(std::move(fn),"__qualname__",nb::str("get_tb_formatter.<locals>.format_exc_info"));}
nb::object traceback_factory(nb::dict globals,nb::object ipython_factory,nb::object fallback_factory,nb::tuple literals,TracebackLocals& s){
    NotificationGlobals ns(std::move(globals));try{
        nf_assign(s.IPython,ns.import_module(nb::borrow<nb::object>(literals[0]),nb::none()));nf_assign(s.IPython,ns.import_module(nb::borrow<nb::object>(literals[1]),nb::none()));
        nf_assign(s.format_exc_info,nf_call1(std::move(ipython_factory),s.IPython));nf_formatter_metadata(s.format_exc_info);
    }catch(const nb::python_error& error){events::HandledException handled(error.value());nb::object kind=ns.get("ModuleNotFoundError");nf_except_type(kind);if(!PyErr_GivenExceptionMatches(error.value().ptr(),kind.ptr()))throw;
        nf_assign(s.traceback,ns.import_module(nb::borrow<nb::object>(literals[2]),nb::none()));nf_assign(s.format_exc_info,nf_call1(std::move(fallback_factory),s.traceback));nf_formatter_metadata(s.format_exc_info);
    }return s.format_exc_info;
}
nb::object nf_local(const nb::object& value,const char* name){if(value.is_valid())return value;PyErr_Format(PyExc_UnboundLocalError,"cannot access local variable '%s' where it is not associated with a value",name);throw nb::python_error();}
nb::object nf_printoptions(const NotificationGlobals& ns){nb::object fn=nf_attr(ns.get("np"),"printoptions");nb::dict kw;kw["precision"]=nb::int_(5);kw["threshold"]=nb::int_(10);kw["edgeitems"]=nb::int_(2);return nf_kw(std::move(fn),nb::tuple(),std::move(kw));}
nb::object traceback_ipython(nb::object info,nb::object as_html,nb::object color,nb::dict globals,nb::object ipython_getter,nb::tuple literals,TracebackLocals& s){
    NotificationGlobals ns(std::move(globals));nf_with(nf_printoptions(ns),[&]{
        nb::object version=nf_attr(nf_call0(ipython_getter),"version_info");bool modern=nf_truth(nf_compare(std::move(version),nb::borrow<nb::object>(literals[0]),Py_GE));
        nb::object factory=nf_attr(nf_attr(nf_attr(nf_call0(ipython_getter),"core"),"ultratb"),"VerboseTB");nb::dict kw;
        if(modern)kw["theme_name"]=nf_call0(nf_attr(color,"lower"));else kw["color_scheme"]=color;nf_assign(s.vbtb,nf_kw(std::move(factory),nb::tuple(),std::move(kw)));
        if(nf_truth(as_html)){
            nb::object fn=nf_attr(s.vbtb,"text");nb::tuple args=nf_star(fn,info);nb::object text=nf_kw(std::move(fn),std::move(args),nb::dict());nf_assign(s.ansi_string,nf_call2(nf_attr(std::move(text),"replace"),nb::borrow<nb::object>(literals[1]),nb::borrow<nb::object>(literals[2])));
            nb::object join=nf_attr(nb::borrow<nb::object>(literals[3]),"join");nb::object ansi=nf_call1(ns.get("ansi2html"),s.ansi_string);nf_assign(s.html,nf_call1(std::move(join),std::move(ansi)));
            nf_assign(s.html,nf_call2(nf_attr(s.html,"replace"),nb::borrow<nb::object>(literals[4]),nb::borrow<nb::object>(literals[5])));
            nb::object value=nf_add(nb::borrow<nb::object>(literals[6]),s.html);value=nf_add(std::move(value),nb::borrow<nb::object>(literals[7]));nf_assign(s.html,std::move(value));nf_assign(s.tb_text,s.html);
        }else{nb::object fn=nf_attr(s.vbtb,"text");nb::tuple args=nf_star(fn,info);nf_assign(s.tb_text,nf_kw(std::move(fn),std::move(args),nb::dict()));}
    });return nf_local(s.tb_text,"tb_text");
}
nb::object traceback_fallback(nb::object info,nb::object as_html,nb::object color,nb::dict globals,nb::object traceback_getter,nb::tuple literals,TracebackLocals& s){
    (void)color;NotificationGlobals ns(std::move(globals));nf_with(nf_printoptions(ns),[&]{
        nb::object join=nf_attr(nb::borrow<nb::object>(literals[0]),"join");nb::object fn=nf_attr(nf_call0(traceback_getter),"format_exception");nb::tuple args=nf_star(fn,info);nb::object pieces=nf_kw(std::move(fn),std::move(args),nb::dict());nf_assign(s.tb_text,nf_call1(std::move(join),std::move(pieces)));
        if(nf_truth(as_html)){nb::object result=nf_add(nb::borrow<nb::object>(literals[1]),s.tb_text);result=nf_add(std::move(result),nb::borrow<nb::object>(literals[2]));nf_assign(s.tb_text,std::move(result));}
    });return nf_local(s.tb_text,"tb_text");
}

// Source generator state and its original suspended iterator stack. Namespace
// and builtins are the source-frame edges, not a controller snapshot/cache.
struct AnsiHTMLIterator {
    nb::object ansi_string,styles,previous_end,in_span,ansi_codes,ansi_finder,match,params,command,i,v,style;
    nb::object match_iterator,params_iterator,globals,builtins,literals;
    int stage=0;bool started=false,closed=false,running=false;
    AnsiHTMLIterator()=default;AnsiHTMLIterator(AnsiHTMLIterator&&)=default;~AnsiHTMLIterator(){clear();}
    void clear(){closed=true;std::array<nb::object,17> old={std::move(ansi_string),std::move(styles),std::move(previous_end),std::move(in_span),std::move(ansi_codes),std::move(ansi_finder),std::move(match),std::move(params),std::move(command),std::move(i),std::move(v),std::move(style),std::move(match_iterator),std::move(params_iterator),std::move(globals),std::move(builtins),std::move(literals)};for(auto& x:old)x.reset();}
};
int ansi_traverse(PyObject* self,visitproc visit,void* arg){Py_VISIT(Py_TYPE(self));if(nb::inst_ready(self)){auto& s=*nb::inst_ptr<AnsiHTMLIterator>(self);
    Py_VISIT(s.ansi_string.ptr());Py_VISIT(s.styles.ptr());Py_VISIT(s.previous_end.ptr());Py_VISIT(s.in_span.ptr());Py_VISIT(s.ansi_codes.ptr());Py_VISIT(s.ansi_finder.ptr());Py_VISIT(s.match.ptr());Py_VISIT(s.params.ptr());Py_VISIT(s.command.ptr());Py_VISIT(s.i.ptr());Py_VISIT(s.v.ptr());Py_VISIT(s.style.ptr());Py_VISIT(s.match_iterator.ptr());Py_VISIT(s.params_iterator.ptr());Py_VISIT(s.globals.ptr());Py_VISIT(s.builtins.ptr());Py_VISIT(s.literals.ptr());}return 0;}
int ansi_clear(PyObject* self){if(nb::inst_ready(self))nb::inst_ptr<AnsiHTMLIterator>(self)->clear();return 0;}
PyType_Slot ansi_slots[]={{Py_tp_traverse,reinterpret_cast<void*>(ansi_traverse)},{Py_tp_clear,reinterpret_cast<void*>(ansi_clear)},{0,nullptr}};
nb::object ansi_literal(AnsiHTMLIterator& s,size_t index){return nb::borrow<nb::object>(nb::borrow<nb::tuple>(s.literals)[index]);}
nb::object ansi_slice(nb::object text,nb::object start,nb::object stop){nb::object key=nf_check(PySlice_New(start.ptr(),stop.ptr(),Py_None));return nf_item(std::move(text),std::move(key));}
void ansi_pair(nb::object supplied,nb::object& first,nb::object& second){nb::tuple pair=events::pair(supplied);supplied.reset();nb::object a=nb::borrow<nb::object>(pair[0]),b=nb::borrow<nb::object>(pair[1]);pair.reset();nf_assign(first,std::move(a));nf_assign(second,std::move(b));}
void ansi_convert_params(AnsiHTMLIterator& s,const NotificationGlobals& ns){
    try{
        nb::object split=nf_attr(s.params,"split");nb::object iterable=nf_call1(std::move(split),ansi_literal(s,2));nb::object iterator=nf_iter(std::move(iterable));nb::list converted;
        while(nb::object value=nf_next(iterator)){nb::object fn=ns.get("int");nb::object result=nf_call1(std::move(fn),std::move(value));converted.append(result);}
        iterator.reset();nf_assign(s.params,std::move(converted));
    }catch(const nb::python_error& error){events::HandledException handled(error.value());nb::object kind=ns.get("ValueError");nf_except_type(kind);if(!PyErr_GivenExceptionMatches(error.value().ptr(),kind.ptr()))throw;nb::list zero;zero.append(nb::int_(0));nf_assign(s.params,std::move(zero));}
}
void ansi_make_style(AnsiHTMLIterator& s){
    nb::object iterator=nf_iter(s.ansi_codes);nb::list style;nb::object outer_k;
    // s.v is the genuine prior enumerate binding. The nested inlined
    // comprehensions save it and restore it; they never discard that lease.
    try{
        while(nb::object supplied=nf_next(iterator)){
            nf_assign(outer_k,std::move(supplied));
            if(!nf_contains(s.styles,outer_k))continue;
            nb::object join=nf_attr(ansi_literal(s,3),"join");nb::object entry=nf_item(s.styles,outer_k);nb::object iterable=nf_call0(nf_attr(std::move(entry),"items"));nb::object inner=nf_iter(std::move(iterable));nb::list fragments;nb::object k,v;
            try{
                while(nb::object supplied=nf_next(inner)){
                    nb::tuple pair=events::pair(supplied);supplied.reset();nb::object new_k=nb::borrow<nb::object>(pair[0]),new_v=nb::borrow<nb::object>(pair[1]);pair.reset();
                    nf_assign(k,std::move(new_k));nf_assign(v,std::move(new_v));
                    nb::object first=nf_format(k),second=nf_format(v);fragments.append(nf_string({std::move(first),ansi_literal(s,4),std::move(second)}));
                }
            }catch(...){inner.reset();fragments.reset();v.reset();k.reset();throw;}
            // END_FOR drops .0; source then restores v and the saved outer k
            // before calling join. Separate native bindings keep outer_k live.
            inner.reset();v.reset();k.reset();nb::object text=nf_call1(std::move(join),std::move(fragments));text=nf_call0(nf_attr(std::move(text),"strip"));style.append(text);
        }
    }catch(...){iterator.reset();style.reset();/* prior s.v remains restored */outer_k.reset();throw;}
    iterator.reset();nf_assign(s.style,std::move(style));outer_k.reset();
}
nb::object ansi_step(AnsiHTMLIterator& s){
    NotificationGlobals ns(nb::borrow<nb::dict>(s.globals),s.builtins);
    for(;;){
        if(s.stage==0){nf_assign(s.previous_end,nb::int_(0));nf_assign(s.in_span,nb::bool_(false));nf_assign(s.ansi_codes,nb::list());nb::object compile=nf_attr(ns.get("re"),"compile");nf_assign(s.ansi_finder,nf_call1(std::move(compile),ansi_literal(s,0)));nb::object find=nf_attr(s.ansi_finder,"finditer");nf_assign(s.match_iterator,nf_iter(nf_call1(std::move(find),s.ansi_string)));s.stage=1;}
        if(s.stage==1){nb::object match=nf_next(s.match_iterator);if(!match.is_valid()){s.match_iterator.reset();s.stage=7;continue;}nf_assign(s.match,std::move(match));nb::object stop=nf_call0(nf_attr(s.match,"start"));nb::object result=ansi_slice(s.ansi_string,s.previous_end,std::move(stop));s.stage=2;return result;}
        if(s.stage==2){nf_assign(s.previous_end,nf_call0(nf_attr(s.match,"end")));nb::object groups=nf_call0(nf_attr(s.match,"groups"));ansi_pair(std::move(groups),s.params,s.command);if(!nf_contains(ansi_literal(s,1),s.command)){s.stage=1;continue;}ansi_convert_params(s,ns);nb::object enumerate=ns.get("enumerate");nf_assign(s.params_iterator,nf_iter(nf_call1(std::move(enumerate),s.params)));s.stage=3;}
        if(s.stage==3){nb::object pair=nf_next(s.params_iterator);if(!pair.is_valid()){s.params_iterator.reset();s.stage=5;continue;}ansi_pair(std::move(pair),s.i,s.v);if(nf_truth(nf_compare(s.v,nb::int_(0),Py_EQ))){nb::object start=nf_add(s.i,nb::int_(1));nf_assign(s.params,ansi_slice(s.params,std::move(start),nb::none()));if(nf_truth(s.in_span)){nf_assign(s.in_span,nb::bool_(false));s.stage=4;return ansi_literal(s,5);}nf_assign(s.ansi_codes,nb::list());if(!nf_truth(s.params))continue;}continue;}
        if(s.stage==4){nf_assign(s.ansi_codes,nb::list());if(!nf_truth(s.params)){s.stage=3;continue;}s.stage=3;continue;}
        if(s.stage==5){nb::object extend=nf_attr(s.ansi_codes,"extend");nf_call1(std::move(extend),s.params);if(nf_truth(s.in_span)){s.stage=6;return ansi_literal(s,5);}s.stage=9;}
        if(s.stage==6){nf_assign(s.in_span,nb::bool_(false));s.stage=9;}
        if(s.stage==9){if(!nf_truth(s.ansi_codes)){s.stage=1;continue;}ansi_make_style(s);nb::object format=nf_attr(ansi_literal(s,6),"format");nb::object joined=nf_call1(nf_attr(ansi_literal(s,3),"join"),s.style);nb::object result=nf_call1(std::move(format),std::move(joined));s.stage=10;return result;}
        if(s.stage==10){nf_assign(s.in_span,nb::bool_(true));s.stage=1;continue;}
        if(s.stage==7){nb::object result=ansi_slice(s.ansi_string,s.previous_end,nb::none());s.stage=8;return result;}
        if(s.stage==8){if(nf_truth(s.in_span)){s.stage=11;return ansi_literal(s,5);}return nb::object();}
        if(s.stage==11)return nb::object();
    }
}
struct AnsiRunning {AnsiHTMLIterator& s;explicit AnsiRunning(AnsiHTMLIterator& v):s(v){if(s.running)throw nb::value_error("generator already executing");s.running=true;}~AnsiRunning(){s.running=false;}};
nb::object ansi_send(nb::handle self,nb::object value){auto& s=*nb::inst_ptr<AnsiHTMLIterator>(self);AnsiRunning running(s);if(s.closed)throw nb::stop_iteration();if(!s.started&&!value.is_none())throw nb::type_error("can't send non-None value to a just-started generator");s.started=true;
    try{nb::object result=ansi_step(s);if(result.is_valid())return result;s.clear();throw nb::stop_iteration();}
    catch(const nb::python_error& error){s.closed=true;s.params_iterator.reset();s.match_iterator.reset();if(error.matches(PyExc_StopIteration))collections::raise_exception(error.value(),true);throw;}
}
void ansi_close(nb::handle self){auto& s=*nb::inst_ptr<AnsiHTMLIterator>(self);AnsiRunning running(s);if(!s.closed)s.clear();}
void ansi_throw(nb::handle self,nb::args args){nb::object error=collections::normalize_throw(args);auto& s=*nb::inst_ptr<AnsiHTMLIterator>(self);AnsiRunning running(s);bool convert=s.started&&!s.closed;s.closed=true;s.params_iterator.reset();s.match_iterator.reset();collections::raise_exception(std::move(error),convert);}
nb::object traceback_ansi_iterator(nb::object ansi_string,nb::object styles,nb::dict globals,nb::tuple literals){AnsiHTMLIterator s;s.ansi_string=std::move(ansi_string);s.styles=std::move(styles);s.globals=std::move(globals);s.builtins=nb::borrow<nb::object>(PyEval_GetBuiltins());s.literals=std::move(literals);return nb::cast(std::move(s));}

} // namespace
void bind_notifications(nb::module_& m){
    PyType_Spec spec={"naparixx._native.NotificationNamespaceBase",static_cast<int>(sizeof(NotificationNamespace)),0,Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE|Py_TPFLAGS_HAVE_GC,nf_namespace_slots};nb::object base=nf_check(PyType_FromSpec(&spec));m.attr("NotificationNamespaceBase")=base;
    nb::class_<NotificationLocals>(m,"NotificationLocals",nb::type_slots(nf_local_slots))
        .def_prop_ro("msg",[](const NotificationLocals& s){return nf_optional(s.msg);}).def_prop_ro("actions",[](const NotificationLocals& s){return nf_optional(s.actions);})
        .def_prop_ro("get_tb_formatter",[](const NotificationLocals& s){return nf_optional(s.get_tb_formatter);}).def_prop_ro("fmt",[](const NotificationLocals& s){return nf_optional(s.fmt);})
        .def_prop_ro("exc_info",[](const NotificationLocals& s){return nf_optional(s.exc_info);}).def_prop_ro("category",[](const NotificationLocals& s){return nf_optional(s.category);})
        .def_prop_ro("get_settings",[](const NotificationLocals& s){return nf_optional(s.get_settings);}).def_prop_ro("member",[](const NotificationLocals& s){return nf_optional(s.member);});
    nb::class_<TracebackLocals>(m,"TracebackLocals",nb::type_slots(nf_tb_slots))
        .def_prop_ro("IPython",[](const TracebackLocals& s){return nf_optional(s.IPython);}).def_prop_ro("traceback",[](const TracebackLocals& s){return nf_optional(s.traceback);})
        .def_prop_ro("format_exc_info",[](const TracebackLocals& s){return nf_optional(s.format_exc_info);}).def_prop_ro("vbtb",[](const TracebackLocals& s){return nf_optional(s.vbtb);})
        .def_prop_ro("ansi_string",[](const TracebackLocals& s){return nf_optional(s.ansi_string);}).def_prop_ro("html",[](const TracebackLocals& s){return nf_optional(s.html);}).def_prop_ro("tb_text",[](const TracebackLocals& s){return nf_optional(s.tb_text);});
    nb::class_<AnsiHTMLIterator>(m,"AnsiHTMLIterator",nb::is_weak_referenceable(),nb::type_slots(ansi_slots))
        .def("__iter__",[](nb::handle self){return nb::borrow<nb::object>(self);}).def("__next__",[](nb::handle self){return ansi_send(self,nb::none());})
        .def("send",&ansi_send,nb::arg("value").none()).def("throw",&ansi_throw).def("close",&ansi_close)
        .def_prop_ro("ansi_string",[](const AnsiHTMLIterator& s){return nf_optional(s.ansi_string);}).def_prop_ro("styles",[](const AnsiHTMLIterator& s){return nf_optional(s.styles);})
        .def_prop_ro("previous_end",[](const AnsiHTMLIterator& s){return nf_optional(s.previous_end);}).def_prop_ro("in_span",[](const AnsiHTMLIterator& s){return nf_optional(s.in_span);})
        .def_prop_ro("ansi_codes",[](const AnsiHTMLIterator& s){return nf_optional(s.ansi_codes);}).def_prop_ro("ansi_finder",[](const AnsiHTMLIterator& s){return nf_optional(s.ansi_finder);})
        .def_prop_ro("match",[](const AnsiHTMLIterator& s){return nf_optional(s.match);}).def_prop_ro("params",[](const AnsiHTMLIterator& s){return nf_optional(s.params);})
        .def_prop_ro("command",[](const AnsiHTMLIterator& s){return nf_optional(s.command);}).def_prop_ro("i",[](const AnsiHTMLIterator& s){return nf_optional(s.i);})
        .def_prop_ro("v",[](const AnsiHTMLIterator& s){return nf_optional(s.v);}).def_prop_ro("style",[](const AnsiHTMLIterator& s){return nf_optional(s.style);});
    m.def("notification_locals",[]{return NotificationLocals();});m.def("traceback_locals",[]{return TracebackLocals();});
    m.def("notification_severity_icon",&severity_icon,nb::arg("self").none(),nb::arg("icons"));
    m.def("notification_severity_lt",[](nb::object self,nb::object other,nb::dict ns){return severity_compare(std::move(self),std::move(other),std::move(ns),Py_LT);},nb::arg("self").none(),nb::arg("other").none(),nb::arg("namespace"));
    m.def("notification_severity_le",[](nb::object self,nb::object other,nb::dict ns){return severity_compare(std::move(self),std::move(other),std::move(ns),Py_LE);},nb::arg("self").none(),nb::arg("other").none(),nb::arg("namespace"));
    m.def("notification_severity_gt",[](nb::object self,nb::object other,nb::dict ns){return severity_compare(std::move(self),std::move(other),std::move(ns),Py_GT);},nb::arg("self").none(),nb::arg("other").none(),nb::arg("namespace"));
    m.def("notification_severity_ge",[](nb::object self,nb::object other,nb::dict ns){return severity_compare(std::move(self),std::move(other),std::move(ns),Py_GE);},nb::arg("self").none(),nb::arg("other").none(),nb::arg("namespace"));
    m.def("notification_severity_eq",&severity_equal,nb::arg("self").none(),nb::arg("other").none(),nb::arg("namespace"));
    m.def("notification_severity_hash",&severity_hash,nb::arg("self").none(),nb::arg("namespace"));
    m.def("notification_init",&notification_init,nb::arg("self").none(),nb::arg("message").none(),nb::arg("severity").none(),nb::arg("actions").none(),nb::arg("kwargs"),nb::arg("namespace"),nb::arg("super_provider").none());
    m.def("notification_message",&notification_message,nb::arg("self").none());m.def("notification_set_message",&notification_set_message,nb::arg("self").none(),nb::arg("value").none());
    m.def("notification_from_exception",&notification_from_exception,nb::arg("exc").none(),nb::arg("kwargs"),nb::arg("namespace"));m.def("notification_from_warning",&notification_from_warning,nb::arg("warning").none(),nb::arg("kwargs"),nb::arg("namespace"));
    m.def("notification_str",&notification_str,nb::arg("self").none(),nb::arg("namespace"),nb::arg("separator").none());
    m.def("notification_error_init",[](nb::object self,nb::object exception,nb::dict ns,nb::object super_provider,nb::tuple attribute_literals,NotificationLocals& s){notification_exception_init(std::move(self),std::move(exception),std::move(ns),std::move(super_provider),std::move(attribute_literals),s,false,nb::none(),nb::none());},nb::arg("self").none(),nb::arg("exception").none(),nb::arg("namespace"),nb::arg("super_provider").none(),nb::arg("attribute_literals"),nb::arg("locals_owner"));
    m.def("notification_error_html",[](nb::object self,nb::dict ns,nb::tuple literals,NotificationLocals& s){return notification_error_format(std::move(self),std::move(ns),std::move(literals),s,0);},nb::arg("self").none(),nb::arg("namespace"),nb::arg("literals"),nb::arg("locals_owner"));
    m.def("notification_error_text",[](nb::object self,nb::dict ns,nb::tuple literals,NotificationLocals& s){return notification_error_format(std::move(self),std::move(ns),std::move(literals),s,1);},nb::arg("self").none(),nb::arg("namespace"),nb::arg("literals"),nb::arg("locals_owner"));
    m.def("notification_error_str",[](nb::object self,nb::dict ns,nb::tuple literals,NotificationLocals& s){return notification_error_format(std::move(self),std::move(ns),std::move(literals),s,2);},nb::arg("self").none(),nb::arg("namespace"),nb::arg("literals"),nb::arg("locals_owner"));
    m.def("notification_warning_init",[](nb::object self,nb::object warning,nb::object filename,nb::object lineno,nb::dict ns,nb::object super_provider,nb::tuple attribute_literals,NotificationLocals& s){notification_exception_init(std::move(self),std::move(warning),std::move(ns),std::move(super_provider),std::move(attribute_literals),s,true,std::move(filename),std::move(lineno));},nb::arg("self").none(),nb::arg("warning").none(),nb::arg("filename").none(),nb::arg("lineno").none(),nb::arg("namespace"),nb::arg("super_provider").none(),nb::arg("attribute_literals"),nb::arg("locals_owner"));
    m.def("notification_warning_str",&notification_warning_str,nb::arg("self").none(),nb::arg("namespace"),nb::arg("literals"),nb::arg("locals_owner"));
    m.def("notification_manager_init",&notification_manager_init,nb::arg("self").none(),nb::arg("namespace"),nb::arg("literals"));m.def("notification_manager_enter",&notification_manager_enter,nb::arg("self").none());m.def("notification_manager_exit",&notification_manager_exit,nb::arg("self").none());
    m.def("notification_install_hooks",&notification_install_hooks,nb::arg("self").none(),nb::arg("namespace"));m.def("notification_restore_hooks",&notification_restore_hooks,nb::arg("self").none(),nb::arg("namespace"));m.def("notification_dispatch",&notification_dispatch,nb::arg("self").none(),nb::arg("notification").none());
    m.def("notification_thread_error",&notification_thread_error,nb::arg("self").none(),nb::arg("args").none());m.def("notification_receive_error",&notification_receive_error,nb::arg("self").none(),nb::arg("exctype").none(),nb::arg("value").none(),nb::arg("traceback").none(),nb::arg("namespace"),nb::arg("literals"));
    m.def("notification_receive_warning",&notification_receive_warning,nb::arg("self").none(),nb::arg("message").none(),nb::arg("category").none(),nb::arg("filename").none(),nb::arg("lineno").none(),nb::arg("namespace"),nb::arg("locals_owner"));m.def("notification_receive_info",&notification_receive_info,nb::arg("self").none(),nb::arg("message").none(),nb::arg("namespace"),nb::arg("info_literal").none());
    m.def("notification_show_debug",[](nb::object message,nb::dict ns){notification_show(std::move(message),std::move(ns),"DEBUG");},nb::arg("message").none(),nb::arg("namespace"));m.def("notification_show_info",[](nb::object message,nb::dict ns){notification_show(std::move(message),std::move(ns),"INFO");},nb::arg("message").none(),nb::arg("namespace"));m.def("notification_show_warning",[](nb::object message,nb::dict ns){notification_show(std::move(message),std::move(ns),"WARNING");},nb::arg("message").none(),nb::arg("namespace"));m.def("notification_show_error",[](nb::object message,nb::dict ns){notification_show(std::move(message),std::move(ns),"ERROR");},nb::arg("message").none(),nb::arg("namespace"));
    m.def("notification_console",&notification_console,nb::arg("notification").none(),nb::arg("namespace"),nb::arg("literals"),nb::arg("locals_owner"));
    m.def("string_enum_generate",&string_enum_generate,nb::arg("name").none());m.def("string_enum_str",&string_enum_str,nb::arg("self").none());m.def("string_enum_missing",&string_enum_missing,nb::arg("cls").none(),nb::arg("value").none(),nb::arg("namespace"),nb::arg("literals"),nb::arg("locals_owner"));m.def("string_enum_eq",&string_enum_equal,nb::arg("self").none(),nb::arg("other").none(),nb::arg("namespace"));m.def("string_enum_ne",&string_enum_not_equal,nb::arg("self").none(),nb::arg("other").none());m.def("string_enum_hash",&string_enum_hash,nb::arg("self").none(),nb::arg("namespace"));m.def("string_enum_keys",&string_enum_keys,nb::arg("cls").none(),nb::arg("namespace"));
    m.def("traceback_factory",&traceback_factory,nb::arg("namespace"),nb::arg("ipython_factory").none(),nb::arg("fallback_factory").none(),nb::arg("literals"),nb::arg("locals_owner"));m.def("traceback_ipython",&traceback_ipython,nb::arg("info").none(),nb::arg("as_html").none(),nb::arg("color").none(),nb::arg("namespace"),nb::arg("ipython_getter").none(),nb::arg("literals"),nb::arg("locals_owner"));m.def("traceback_fallback",&traceback_fallback,nb::arg("info").none(),nb::arg("as_html").none(),nb::arg("color").none(),nb::arg("namespace"),nb::arg("traceback_getter").none(),nb::arg("literals"),nb::arg("locals_owner"));m.def("traceback_ansi_iterator",&traceback_ansi_iterator,nb::arg("ansi_string").none(),nb::arg("styles").none(),nb::arg("namespace"),nb::arg("literals"));
}
} // namespace nx




