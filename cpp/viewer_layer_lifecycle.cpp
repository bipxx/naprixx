#include "arrays.hpp"
#if PY_VERSION_HEX >= 0x030C0000 && PY_VERSION_HEX < 0x030D0000
// Reuse the accepted CPython3.12 IMPORT_NAME existing-locals profile.
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
nb::object ll_check(PyObject* value) { if (!value) throw nb::python_error(); return nb::steal<nb::object>(value); }
nb::object ll_attr(nb::object receiver, const char* name) { return ll_check(PyObject_GetAttrString(receiver.ptr(), name)); }
nb::object ll_item(nb::object receiver, nb::object key) { return ll_check(PyObject_GetItem(receiver.ptr(), key.ptr())); }
void ll_assign(nb::object& target, nb::object value) { nb::object previous=std::move(target);target=std::move(value);previous.reset(); }
bool ll_truth(nb::object value) { int result=PyObject_IsTrue(value.ptr());value.reset();if(result<0)throw nb::python_error();return result!=0; }
using LLExpr=std::function<nb::object()>;
nb::object ll_call(nb::object function, std::initializer_list<LLExpr> positional={},
                   std::initializer_list<LLExpr> keywords={}, nb::object keyword_names={}) {
    std::vector<nb::object> values;values.reserve(positional.size()+keywords.size());
    for(const auto& expression:positional)values.push_back(expression());
    for(const auto& expression:keywords)values.push_back(expression());
    if(keywords.size()&&(!keyword_names.is_valid()||!PyTuple_CheckExact(keyword_names.ptr())||
       PyTuple_GET_SIZE(keyword_names.ptr())!=static_cast<Py_ssize_t>(keywords.size())))
        throw nb::type_error("source keyword-name metadata must be the matching tuple");
    std::vector<PyObject*> operands;operands.reserve(values.size());for(const auto& value:values)operands.push_back(value.ptr());
    PyObject* result=PyObject_Vectorcall(function.ptr(),operands.data(),positional.size(),keyword_names.ptr());
    function.reset();for(auto& value:values)value.reset();keyword_names.reset();return ll_check(result);
}
void ll_publish(LLExpr value,LLExpr receiver,const char* name) {
    nb::object result=value(),target=receiver();int status=PyObject_SetAttrString(target.ptr(),name,result.ptr());
    result.reset();target.reset();if(status<0)throw nb::python_error();
}
bool ll_is(LLExpr left,LLExpr right) { nb::object a=left(),b=right();bool result=a.ptr()==b.ptr();a.reset();b.reset();return result; }
nb::object ll_compare(LLExpr left,LLExpr right,int operation) {
    nb::object a=left(),b=right();PyObject* result=PyObject_RichCompare(a.ptr(),b.ptr(),operation);a.reset();b.reset();return ll_check(result);
}
nb::object ll_format(nb::object value) { return ll_check(PyObject_Format(value.ptr(),nullptr)); }
nb::object ll_iter(nb::object value) { return ll_check(PyObject_GetIter(value.ptr())); }
nb::object ll_next(nb::handle iterator) { PyObject* value=PyIter_Next(iterator.ptr());if(!value&&PyErr_Occurred())throw nb::python_error();return nb::steal<nb::object>(value); }
nb::list ll_list(std::initializer_list<LLExpr> expressions={}) {
    std::vector<nb::object> values;values.reserve(expressions.size());for(const auto& expression:expressions)values.push_back(expression());
    nb::list result=nb::steal<nb::list>(PyList_New(static_cast<Py_ssize_t>(values.size())));if(!result.is_valid())throw nb::python_error();
    Py_ssize_t index=0;for(auto& value:values)PyList_SET_ITEM(result.ptr(),index++,value.release().ptr());return result;
}
nb::dict ll_map(std::initializer_list<std::pair<LLExpr,LLExpr>> expressions) {
    std::vector<nb::object> values;values.reserve(expressions.size()*2);
    auto release=[&](){for(size_t index=values.size();index>0;)values[--index].reset();};
    try{for(const auto& expression:expressions){values.push_back(expression.first());values.push_back(expression.second());}}
    catch(...){release();throw;}
    PyObject* raw=PyDict_New();if(!raw){release();throw nb::python_error();}
    nb::dict result=nb::steal<nb::dict>(raw);
    for(size_t index=0;index<values.size();index+=2)
        if(PyDict_SetItem(result.ptr(),values[index].ptr(),values[index+1].ptr())<0){
            // Match BUILD_MAP: partial dict first, then operands in reverse.
            // Preserve the original Python error while its operands unwind.
            result.reset();release();throw nb::python_error();
        }
    release();return result;
}
nb::object ll_string(std::initializer_list<LLExpr> expressions) {
    std::vector<nb::object> pieces;pieces.reserve(expressions.size());for(const auto& expression:expressions)pieces.push_back(expression());
    nb::tuple sequence=nb::steal<nb::tuple>(PyTuple_New(static_cast<Py_ssize_t>(pieces.size())));if(!sequence.is_valid())throw nb::python_error();
    for(size_t index=0;index<pieces.size();++index)PyTuple_SET_ITEM(sequence.ptr(),static_cast<Py_ssize_t>(index),pieces[index].inc_ref().ptr());
    PyObject* result=PyUnicode_Join(nb::str("").ptr(),sequence.ptr());sequence.reset();for(size_t index=pieces.size();index>0;--index)pieces[index-1].reset();return ll_check(result);
}
std::array<nb::object,2> ll_unpack_pair(nb::object value) {
    PyObject* raw=PyObject_GetIter(value.ptr());if(!raw){nb::python_error error;if(error.matches(PyExc_TypeError)&&Py_TYPE(value.ptr())->tp_iter==nullptr&&!PySequence_Check(value.ptr())){PyErr_Format(PyExc_TypeError,"cannot unpack non-iterable %.200s object",Py_TYPE(value.ptr())->tp_name);throw nb::python_error();}throw error;}
    nb::object iterator=nb::steal<nb::object>(raw);std::array<nb::object,2> result;
    for(size_t index=0;index<2;++index){result[index]=ll_next(iterator);if(!result[index].is_valid()){PyErr_Format(PyExc_ValueError,"not enough values to unpack (expected 2, got %zu)",index);throw nb::python_error();}}
    nb::object extra=ll_next(iterator);if(extra.is_valid()){PyErr_SetString(PyExc_ValueError,"too many values to unpack (expected 2)");throw nb::python_error();}
    iterator.reset();value.reset();return result;
}
[[noreturn]] void ll_raise(nb::object value) {
    if(PyExceptionClass_Check(value.ptr())){nb::object klass=std::move(value);value=ll_call(klass);if(!PyExceptionInstance_Check(value.ptr())){PyErr_Format(PyExc_TypeError,"calling %R should have returned an instance of BaseException, not %R",klass.ptr(),reinterpret_cast<PyObject*>(Py_TYPE(value.ptr())));throw nb::python_error();}}
    if(!PyExceptionInstance_Check(value.ptr()))PyErr_SetString(PyExc_TypeError,"exceptions must derive from BaseException");
    else PyErr_SetObject(reinterpret_cast<PyObject*>(Py_TYPE(value.ptr())),value.ptr());throw nb::python_error();
}
nb::object ll_import_frame_locals(){
#if PY_VERSION_HEX >= 0x030C0000 && PY_VERSION_HEX < 0x030D0000
    PyFrameObject* frame=PyEval_GetFrame();if(!frame){if(PyErr_Occurred())throw nb::python_error();return nb::none();}
    PyObject* locals=frame->f_frame->f_locals;return nb::borrow<nb::object>(locals?locals:Py_None);
#else
    PyErr_SetString(PyExc_RuntimeError,"Viewer lifecycle IMPORT_NAME existing-locals handoff requires its reviewed CPython 3.12 profile; other required interpreter profiles remain open");throw nb::python_error();
#endif
}
struct LifecycleGlobals {
    nb::dict values;nb::object builtins;
    explicit LifecycleGlobals(nb::dict source):values(std::move(source)),builtins(nb::borrow<nb::object>(PyEval_GetBuiltins())) {}
    nb::object builtin(const char* name)const {
        nb::object key=nb::str(name);PyObject* value=PyObject_GetItem(builtins.ptr(),key.ptr());if(value)return nb::steal<nb::object>(value);
        if(!PyErr_ExceptionMatches(PyExc_KeyError))throw nb::python_error();PyErr_Clear();
        if(std::string(name)=="__import__"){PyErr_SetString(PyExc_ImportError,"__import__ not found");throw nb::python_error();}
        nb::object message=ll_check(PyUnicode_FromFormat("name '%s' is not defined",name));nb::object error=ll_call(nb::borrow<nb::object>(PyExc_NameError),{[&]()->nb::object{return message;}});
        if(PyObject_SetAttrString(error.ptr(),"name",key.ptr())<0)throw nb::python_error();ll_raise(std::move(error));
    }
    nb::object get(const char* name)const { PyObject* value=PyDict_GetItemString(values.ptr(),name);if(value)return nb::borrow<nb::object>(value);if(PyErr_Occurred())throw nb::python_error();return builtin(name); }
    void store(const char* name,nb::object value){int status=PyDict_SetItemString(values.ptr(),name,value.ptr());value.reset();if(status<0)throw nb::python_error();}
    nb::object import_module(nb::object name)const {
        nb::object importer=builtin("__import__");nb::object locals=ll_import_frame_locals();
        return ll_call(std::move(importer),{[&]()->nb::object{return name;},[&]()->nb::object{return values;},[&]()->nb::object{return locals;},[]()->nb::object{return nb::none();},[]()->nb::object{return nb::int_(0);}});
    }
};
// Actual assigned source locals only. Singleton/model state stays exclusively
// in the executing Python module dictionary and actual model receivers.
struct ViewerLayerLifecycleLocals {
    nb::object layer,layer_to_func_and_mode,help_li,shortcuts,fun,mode_,action_name,desc,active,path,inspect,curframe,calframe;
    ViewerLayerLifecycleLocals()=default;
    ViewerLayerLifecycleLocals(ViewerLayerLifecycleLocals&&)=default;
    void clear(){std::array<nb::object,13> values={std::move(layer),std::move(layer_to_func_and_mode),std::move(help_li),std::move(shortcuts),std::move(fun),std::move(mode_),std::move(action_name),std::move(desc),std::move(active),std::move(path),std::move(inspect),std::move(curframe),std::move(calframe)};for(auto& value:values)value.reset();}
    ~ViewerLayerLifecycleLocals(){clear();}
};
int ll_owner_traverse(PyObject* self,visitproc visit,void* arg){Py_VISIT(Py_TYPE(self));if(nb::inst_ready(self)){auto& o=*nb::inst_ptr<ViewerLayerLifecycleLocals>(self);for(nb::object* value:{&o.layer,&o.layer_to_func_and_mode,&o.help_li,&o.shortcuts,&o.fun,&o.mode_,&o.action_name,&o.desc,&o.active,&o.path,&o.inspect,&o.curframe,&o.calframe})Py_VISIT(value->ptr());}return 0;}
int ll_owner_clear(PyObject* self){if(nb::inst_ready(self))nb::inst_ptr<ViewerLayerLifecycleLocals>(self)->clear();return 0;}
PyType_Slot ll_owner_slots[]={{Py_tp_traverse,reinterpret_cast<void*>(ll_owner_traverse)},{Py_tp_clear,reinterpret_cast<void*>(ll_owner_clear)},{0,nullptr}};
#define LX(value) [&]() -> nb::object { return (value); }
void viewer_lifecycle_update_mouse_pan(nb::object self,nb::object event){if(ll_is(LX(ll_attr(event,"source")),LX(ll_attr(ll_attr(ll_attr(self,"layers"),"selection"),"active"))))ll_publish(LX(ll_attr(event,"mouse_pan")),LX(ll_attr(ll_attr(self,"scene"),"camera")),"mouse_pan");}
void viewer_lifecycle_update_mouse_zoom(nb::object self,nb::object event){if(ll_is(LX(ll_attr(event,"source")),LX(ll_attr(ll_attr(ll_attr(self,"layers"),"selection"),"active"))))ll_publish(LX(ll_attr(event,"mouse_zoom")),LX(ll_attr(ll_attr(self,"scene"),"camera")),"mouse_zoom");}
void viewer_lifecycle_update_cursor(nb::object self,nb::object event){ll_publish(LX(ll_attr(event,"cursor")),LX(ll_attr(self,"cursor")),"style");}
void viewer_lifecycle_update_cursor_size(nb::object self,nb::object event){ll_publish(LX(ll_attr(event,"cursor_size")),LX(ll_attr(self,"cursor")),"size");}
void viewer_lifecycle_on_add_layer(nb::object self,nb::object event,nb::dict namespace_,ViewerLayerLifecycleLocals& o,nb::tuple layers_keyword_names,nb::tuple constants){
    LifecycleGlobals ns(std::move(namespace_));ll_assign(o.layer,ll_attr(event,"value"));
    ll_call(ll_attr(ll_attr(ll_attr(o.layer,"events"),"mouse_pan"),"connect"),{LX(ll_attr(self,"_update_mouse_pan"))});
    ll_call(ll_attr(ll_attr(ll_attr(o.layer,"events"),"mouse_zoom"),"connect"),{LX(ll_attr(self,"_update_mouse_zoom"))});
    ll_call(ll_attr(ll_attr(ll_attr(o.layer,"events"),"cursor"),"connect"),{LX(ll_attr(self,"_update_cursor"))});
    ll_call(ll_attr(ll_attr(ll_attr(o.layer,"events"),"cursor_size"),"connect"),{LX(ll_attr(self,"_update_cursor_size"))});
    ll_call(ll_attr(ll_attr(ll_attr(o.layer,"events"),"data"),"connect"),{LX(ll_attr(self,"_on_layers_change"))});
    ll_call(ll_attr(ll_attr(ll_attr(o.layer,"events"),"scale"),"connect"),{LX(ll_attr(self,"_on_layers_change"))});
    ll_call(ll_attr(ll_attr(ll_attr(o.layer,"events"),"units"),"connect"),{LX(ll_attr(self,"_on_layers_change"))});
    ll_call(ll_attr(ll_attr(ll_attr(o.layer,"events"),"translate"),"connect"),{LX(ll_attr(self,"_on_layers_change"))});
    ll_call(ll_attr(ll_attr(ll_attr(o.layer,"events"),"rotate"),"connect"),{LX(ll_attr(self,"_on_layers_change"))});
    ll_call(ll_attr(ll_attr(ll_attr(o.layer,"events"),"shear"),"connect"),{LX(ll_attr(self,"_on_layers_change"))});
    ll_call(ll_attr(ll_attr(ll_attr(o.layer,"events"),"affine"),"connect"),{LX(ll_attr(self,"_on_layers_change"))});
    ll_call(ll_attr(ll_attr(ll_attr(o.layer,"events"),"axis_labels"),"connect"),{LX(ll_attr(self,"_on_layers_change"))});
    ll_call(ll_attr(ll_attr(ll_attr(o.layer,"events"),"name"),"connect"),{LX(ll_attr(ll_attr(self,"layers"),"_update_name"))});
    ll_call(ll_attr(ll_attr(ll_attr(o.layer,"events"),"reload"),"connect"),{LX(ll_attr(self,"_on_layer_reload"))});
    if(ll_truth(ll_call(ns.get("hasattr"),{LX(ll_attr(o.layer,"events")),LX(nb::borrow<nb::object>(constants[0]))})))ll_call(ll_attr(ll_attr(ll_attr(o.layer,"events"),"mode"),"connect"),{LX(ll_attr(self,"_on_layer_mode_change"))});
    ll_call(ll_attr(self,"_layer_help_from_mode"),{LX(o.layer)});ll_call(ll_attr(self,"_on_layers_change"));
    ll_call(ll_attr(self,"_update_layers"),{}, {LX(ll_list({LX(o.layer)}))},layers_keyword_names);
    if(ll_truth(ll_compare(LX(ll_call(ns.get("len"),{LX(ll_attr(self,"layers"))})),LX(nb::int_(1)),Py_EQ))){ll_call(ll_attr(self,"reset_view"));ll_call(ll_attr(ll_attr(self,"dims"),"_go_to_center_step"));}
}
void viewer_lifecycle_layer_help_from_mode(nb::object layer,nb::dict namespace_,ViewerLayerLifecycleLocals& o,nb::tuple constants){
    LifecycleGlobals ns(std::move(namespace_));
    ll_assign(o.layer_to_func_and_mode,ll_map({
        {LX(ns.get("Points")),LX(ns.get("points_fun_to_mode"))},{LX(ns.get("Labels")),LX(ns.get("labels_fun_to_mode"))},
        {LX(ns.get("Shapes")),LX(ns.get("shapes_fun_to_mode"))},{LX(ns.get("Vectors")),LX(ns.get("vectors_fun_to_mode"))},
        {LX(ns.get("Image")),LX(ns.get("image_fun_to_mode"))},{LX(ns.get("Surface")),LX(ns.get("surface_fun_to_mode"))},
        {LX(ns.get("Tracks")),LX(ns.get("tracks_fun_to_mode"))}}));
    ll_assign(o.help_li,ll_list());ll_assign(o.shortcuts,ll_attr(ll_attr(ll_call(ns.get("get_settings")),"shortcuts"),"shortcuts"));
    nb::object iterator=ll_iter(ll_call(ll_attr(o.layer_to_func_and_mode,"get"),{LX(ll_attr(layer,"__class__")),LX(ll_list())}));
    while(nb::object pair=ll_next(iterator)){auto values=ll_unpack_pair(std::move(pair));ll_assign(o.fun,std::move(values[0]));ll_assign(o.mode_,std::move(values[1]));
        if(ll_truth(ll_compare(LX(o.mode_),LX(ll_attr(layer,"mode")),Py_EQ)))continue;
        ll_assign(o.action_name,ll_string({LX(nb::borrow<nb::object>(constants[0])),LX(ll_format(ll_attr(o.fun,"__name__")))}));
        ll_assign(o.desc,ll_call(ll_attr(ll_attr(ll_item(ll_attr(ns.get("action_manager"),"_actions"),o.action_name),"description"),"lower")));
        if(!ll_truth(ll_call(ll_attr(o.shortcuts,"get"),{LX(o.action_name),LX(ll_list())})))continue;
        ll_call(ll_attr(o.help_li,"append"),{LX(ll_string({LX(nb::borrow<nb::object>(constants[1])),LX(ll_format(ll_item(ll_item(o.shortcuts,o.action_name),nb::int_(0)))),LX(nb::borrow<nb::object>(constants[2])),LX(ll_format(o.desc))}))});
    }
    iterator.reset();ll_publish(LX(ll_call(ll_attr(nb::borrow<nb::object>(constants[3]),"join"),{LX(o.help_li)})),LX(layer),"help");
}
void viewer_lifecycle_on_layer_mode_change(nb::object self,nb::object event,ViewerLayerLifecycleLocals& o){ll_call(ll_attr(self,"_layer_help_from_mode"),{LX(ll_attr(event,"source"))});ll_assign(o.active,ll_attr(ll_attr(ll_attr(self,"layers"),"selection"),"active"));if(!o.active.is_none())ll_publish(LX(ll_attr(o.active,"help")),LX(self),"help");}
void viewer_lifecycle_on_remove_layer(nb::object self,nb::object event,nb::dict namespace_,ViewerLayerLifecycleLocals& o){LifecycleGlobals ns(std::move(namespace_));ll_assign(o.layer,ll_attr(event,"value"));ll_call(ns.get("disconnect_events"),{LX(ll_attr(o.layer,"events")),LX(self)});ll_call(ns.get("disconnect_events"),{LX(ll_attr(o.layer,"events")),LX(ll_attr(self,"layers"))});ll_call(ll_attr(self,"_on_layers_change"));}
nb::object settings_lifecycle_get_settings(nb::dict namespace_,ViewerLayerLifecycleLocals& o,nb::tuple keyword_names){
    LifecycleGlobals ns(std::move(namespace_));
    if(ns.get("_SETTINGS").is_none()){
        if(!ll_is(LX(o.path),LX(ns.get("_NOT_SET"))))ll_assign(o.path,!o.path.is_none()?ll_call(ll_attr(ll_call(ns.get("Path"),{LX(o.path)}),"resolve")):nb::none());
        ns.store("_SETTINGS",ll_call(ns.get("NapariSettings"),{}, {LX(o.path)},keyword_names));
        ll_call(ll_attr(ns.get("_SETTINGS"),"_connect_events"));
    }else if(!ll_is(LX(o.path),LX(ns.get("_NOT_SET"))))ll_call(ns.get("_raise_path_set_twice"));
    return ns.get("_SETTINGS");
}
[[noreturn]] void settings_lifecycle_raise_path_set_twice(nb::dict namespace_,ViewerLayerLifecycleLocals& o,nb::tuple constants){
    LifecycleGlobals ns(std::move(namespace_));ll_assign(o.inspect,ns.import_module(nb::borrow<nb::object>(constants[0])));
    ll_assign(o.curframe,ll_call(ll_attr(o.inspect,"currentframe")));
    ll_assign(o.calframe,ll_call(ll_attr(o.inspect,"getouterframes"),{LX(o.curframe),LX(nb::int_(2))}));
    ll_raise(ll_call(ns.get("RuntimeError"),{LX(ll_string({LX(nb::borrow<nb::object>(constants[1])),LX(ll_format(ll_item(ll_item(o.calframe,nb::int_(2)),nb::int_(3))))}))}));
}
#undef LX
} // namespace
void bind_viewer_layer_lifecycle(nb::module_& module){
    auto owner=nb::class_<ViewerLayerLifecycleLocals>(module,"ViewerLayerLifecycleLocals",nb::is_weak_referenceable(),nb::type_slots(ll_owner_slots));
#define LL_FIELD(name) owner.def_prop_ro(#name,[](const ViewerLayerLifecycleLocals& value){return value.name.is_valid()?value.name:nb::none();});
    LL_FIELD(layer) LL_FIELD(layer_to_func_and_mode) LL_FIELD(help_li) LL_FIELD(shortcuts) LL_FIELD(fun) LL_FIELD(mode_) LL_FIELD(action_name) LL_FIELD(desc) LL_FIELD(active) LL_FIELD(path) LL_FIELD(inspect) LL_FIELD(curframe) LL_FIELD(calframe)
#undef LL_FIELD
    module.def("viewer_layer_lifecycle_locals",[](int kind,nb::object path){ViewerLayerLifecycleLocals owner;if(kind==3)owner.path=std::move(path);return owner;},nb::arg("kind")=0,nb::arg("path").none()=nb::none());
    module.def("viewer_lifecycle_on_add_layer",&viewer_lifecycle_on_add_layer,nb::arg("self").none(),nb::arg("event").none(),nb::arg("namespace"),nb::arg("locals_owner"),nb::arg("layers_keyword_names"),nb::arg("constants"));
    module.def("viewer_lifecycle_on_remove_layer",&viewer_lifecycle_on_remove_layer,nb::arg("self").none(),nb::arg("event").none(),nb::arg("namespace"),nb::arg("locals_owner"));
    module.def("viewer_lifecycle_update_mouse_pan",&viewer_lifecycle_update_mouse_pan,nb::arg("self").none(),nb::arg("event").none());
    module.def("viewer_lifecycle_update_mouse_zoom",&viewer_lifecycle_update_mouse_zoom,nb::arg("self").none(),nb::arg("event").none());
    module.def("viewer_lifecycle_update_cursor",&viewer_lifecycle_update_cursor,nb::arg("self").none(),nb::arg("event").none());
    module.def("viewer_lifecycle_update_cursor_size",&viewer_lifecycle_update_cursor_size,nb::arg("self").none(),nb::arg("event").none());
    module.def("viewer_lifecycle_layer_help_from_mode",&viewer_lifecycle_layer_help_from_mode,nb::arg("layer").none(),nb::arg("namespace"),nb::arg("locals_owner"),nb::arg("constants"));
    module.def("viewer_lifecycle_on_layer_mode_change",&viewer_lifecycle_on_layer_mode_change,nb::arg("self").none(),nb::arg("event").none(),nb::arg("locals_owner"));
    module.def("settings_lifecycle_get_settings",&settings_lifecycle_get_settings,nb::arg("namespace"),nb::arg("locals_owner"),nb::arg("keyword_names"));
    module.def("settings_lifecycle_raise_path_set_twice",&settings_lifecycle_raise_path_set_twice,nb::arg("namespace"),nb::arg("locals_owner"),nb::arg("constants"));
}
} // namespace nx
