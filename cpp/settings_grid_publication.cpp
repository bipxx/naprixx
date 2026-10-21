#include "arrays.hpp"
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
nb::object sg_check(PyObject* value){if(!value)throw nb::python_error();return nb::steal<nb::object>(value);}
nb::object sg_attr(nb::object value,const char* name){return sg_check(PyObject_GetAttrString(value.ptr(),name));}
void sg_assign(nb::object& target,nb::object value){nb::object previous=std::move(target);target=std::move(value);previous.reset();}
using SGExpr=std::function<nb::object()>;
nb::object sg_call(nb::object function,std::initializer_list<SGExpr> positional={},std::initializer_list<SGExpr> keywords={},nb::object keyword_names={}){
    std::vector<nb::object> values;values.reserve(positional.size()+keywords.size());for(const auto& expression:positional)values.push_back(expression());for(const auto& expression:keywords)values.push_back(expression());
    if(keywords.size()&&(!keyword_names.is_valid()||!PyTuple_CheckExact(keyword_names.ptr())||PyTuple_GET_SIZE(keyword_names.ptr())!=static_cast<Py_ssize_t>(keywords.size())))throw nb::type_error("source keyword-name metadata must be the matching tuple");
    std::vector<PyObject*> operands;operands.reserve(values.size());for(const auto& value:values)operands.push_back(value.ptr());
    PyObject* result=PyObject_Vectorcall(function.ptr(),operands.data(),positional.size(),keyword_names.ptr());function.reset();for(auto& value:values)value.reset();keyword_names.reset();return sg_check(result);
}
nb::object sg_expanded_call(nb::object function,nb::handle kwargs){
    // Genuine source CALL_FUNCTION_EX: resolve callable first, then build a
    // fresh exact keyword expansion. Caller kwargs remains the same object.
    nb::object arguments=sg_check(PyTuple_New(0));
    nb::object expanded=sg_check(PyDict_Copy(kwargs.ptr()));
    PyObject* result=PyObject_Call(function.ptr(),arguments.ptr(),expanded.ptr());
    function.reset();arguments.reset();expanded.reset();return sg_check(result);
}
void sg_publish(SGExpr value,SGExpr receiver,const char* name){nb::object result=value(),target=receiver();int status=PyObject_SetAttrString(target.ptr(),name,result.ptr());result.reset();target.reset();if(status<0)throw nb::python_error();}
nb::tuple sg_tuple(std::initializer_list<SGExpr> expressions){
    std::vector<nb::object> values;values.reserve(expressions.size());for(const auto& expression:expressions)values.push_back(expression());
    nb::tuple result=nb::steal<nb::tuple>(PyTuple_New(static_cast<Py_ssize_t>(values.size())));if(!result.is_valid())throw nb::python_error();
    for(size_t index=0;index<values.size();++index)PyTuple_SET_ITEM(result.ptr(),static_cast<Py_ssize_t>(index),values[index].release().ptr());return result;
}
nb::object sg_import_frame_locals(){
#if PY_VERSION_HEX >= 0x030C0000 && PY_VERSION_HEX < 0x030D0000
    PyFrameObject* frame=PyEval_GetFrame();if(!frame){if(PyErr_Occurred())throw nb::python_error();return nb::none();}
    PyObject* locals=frame->f_frame->f_locals;return nb::borrow<nb::object>(locals?locals:Py_None);
#else
    PyErr_SetString(PyExc_RuntimeError,"Settings/grid IMPORT_NAME existing-locals handoff requires its reviewed CPython 3.12 profile; other required interpreter profiles remain open");throw nb::python_error();
#endif
}
struct SettingsGridGlobals{
    nb::dict values;nb::object builtins;
    explicit SettingsGridGlobals(nb::dict source):values(std::move(source)),builtins(nb::borrow<nb::object>(PyEval_GetBuiltins())){}
    nb::object builtin(const char* name)const{nb::object key=nb::str(name);PyObject* value=PyObject_GetItem(builtins.ptr(),key.ptr());if(value)return nb::steal<nb::object>(value);if(!PyErr_ExceptionMatches(PyExc_KeyError))throw nb::python_error();PyErr_Clear();if(std::string(name)=="__import__"){PyErr_SetString(PyExc_ImportError,"__import__ not found");throw nb::python_error();}nb::object message=sg_check(PyUnicode_FromFormat("name '%s' is not defined",name));nb::object error=sg_call(nb::borrow<nb::object>(PyExc_NameError),{[&]()->nb::object{return message;}});if(PyObject_SetAttrString(error.ptr(),"name",key.ptr())<0)throw nb::python_error();PyErr_SetObject(PyExc_NameError,error.ptr());throw nb::python_error();}
    nb::object get(const char* name)const{PyObject* value=PyDict_GetItemString(values.ptr(),name);if(value)return nb::borrow<nb::object>(value);if(PyErr_Occurred())throw nb::python_error();return builtin(name);}
    void store(const char* name,nb::object value){int status=PyDict_SetItemString(values.ptr(),name,value.ptr());value.reset();if(status<0)throw nb::python_error();}
    nb::object import_module(nb::object name,nb::object fromlist,nb::object level)const{
        PyObject* raw_name=name.release().ptr();PyUnicode_InternInPlace(&raw_name);if(PyErr_Occurred()){Py_XDECREF(raw_name);throw nb::python_error();}name=nb::steal<nb::object>(raw_name);
        nb::object importer=builtin("__import__"),locals=sg_import_frame_locals();
        return sg_call(std::move(importer),{[&]()->nb::object{return name;},[&]()->nb::object{return values;},[&]()->nb::object{return locals;},[&]()->nb::object{return fromlist;},[&]()->nb::object{return level;}});
    }
};
// Reuse the accepted handwritten CPython3.12 IMPORT_FROM protocol. In
// particular, an AttributeError must use the genuine sys.modules fallback
// and source ImportError metadata, rather than becoming plain getattr.
bool sg_spec_initializing(nb::handle spec){
    if(spec.is_valid()){nb::object value=nb::steal<nb::object>(PyObject_GetAttrString(spec.ptr(),"_initializing"));if(!value.is_valid()&&PyErr_ExceptionMatches(PyExc_AttributeError)){PyErr_Clear();return false;}if(value.is_valid()){int initializing=PyObject_IsTrue(value.ptr());value.reset();if(initializing>=0)return initializing!=0;}}
    PyErr_Clear();return false;
}
[[noreturn]] void sg_raise_import(nb::object message,nb::handle name,nb::handle path,nb::handle requested){
    if(!message.is_valid()){PyErr_SetString(PyExc_TypeError,"expected a message argument");throw nb::python_error();}
    nb::dict kwargs;kwargs["name"]=name.is_valid()?nb::borrow<nb::object>(name):nb::none();kwargs["path"]=path.is_valid()?nb::borrow<nb::object>(path):nb::none();
#if PY_VERSION_HEX >= 0x030C0000
    kwargs["name_from"]=requested;
#endif
    nb::object arguments=sg_check(PyTuple_New(1));PyTuple_SET_ITEM(arguments.ptr(),0,message.inc_ref().ptr());
    nb::object error=sg_check(PyObject_Call(PyExc_ImportError,arguments.ptr(),kwargs.ptr()));
    PyErr_SetObject(reinterpret_cast<PyObject*>(Py_TYPE(error.ptr())),error.ptr());throw nb::python_error();
}
nb::object sg_import_from(nb::handle module,nb::handle requested){
#if PY_VERSION_HEX >= 0x030C0000 && PY_VERSION_HEX < 0x030D0000
    PyObject* result=PyObject_GetAttr(module.ptr(),requested.ptr());if(result)return nb::steal<nb::object>(result);if(!PyErr_ExceptionMatches(PyExc_AttributeError))throw nb::python_error();PyErr_Clear();
    nb::object pkgname=nb::steal<nb::object>(PyObject_GetAttrString(module.ptr(),"__name__"));if(pkgname.is_valid()&&!PyUnicode_Check(pkgname.ptr()))pkgname.reset();
    if(pkgname.is_valid()){nb::object fullname=sg_check(PyUnicode_FromFormat("%U.%U",pkgname.ptr(),requested.ptr()));result=PyImport_GetModule(fullname.ptr());fullname.reset();if(result)return nb::steal<nb::object>(result);if(PyErr_Occurred())throw nb::python_error();}
    nb::object pkgpath=nb::steal<nb::object>(PyModule_GetFilenameObject(module.ptr()));PyObject* raw_pkgname=pkgname.ptr();
    nb::object name_or_unknown=pkgname.is_valid()?std::move(pkgname):nb::str("<unknown module name>");nb::object message;nb::handle error_path;
    if(!pkgpath.is_valid()||!PyUnicode_Check(pkgpath.ptr())){PyErr_Clear();message=nb::steal<nb::object>(PyUnicode_FromFormat("cannot import name %R from %R (unknown location)",requested.ptr(),name_or_unknown.ptr()));}
    else{nb::object spec=nb::steal<nb::object>(PyObject_GetAttrString(module.ptr(),"__spec__"));bool initializing=sg_spec_initializing(spec);spec.reset();const char* format=initializing?"cannot import name %R from partially initialized module %R (most likely due to a circular import) (%S)":"cannot import name %R from %R (%S)";message=nb::steal<nb::object>(PyUnicode_FromFormat(format,requested.ptr(),name_or_unknown.ptr(),pkgpath.ptr()));error_path=pkgpath;}
    try{sg_raise_import(std::move(message),nb::handle(raw_pkgname),error_path,requested);}catch(...){message.reset();name_or_unknown.reset();pkgpath.reset();throw;}
#else
    PyErr_SetString(PyExc_RuntimeError,"Settings/grid IMPORT_FROM requires its reviewed CPython 3.12 profile; other required interpreter profiles remain open");throw nb::python_error();
#endif
}
struct SettingsGridPublicationLocals{
    nb::object settings,change_brush_size_on_mouse_move_modifiers,status_messages;
    SettingsGridPublicationLocals()=default;SettingsGridPublicationLocals(SettingsGridPublicationLocals&&)=default;
    void clear(){std::array<nb::object,3> values={std::move(settings),std::move(change_brush_size_on_mouse_move_modifiers),std::move(status_messages)};for(auto& value:values)value.reset();}
    ~SettingsGridPublicationLocals(){clear();}
};
int sg_owner_traverse(PyObject* self,visitproc visit,void* arg){Py_VISIT(Py_TYPE(self));if(nb::inst_ready(self)){auto& o=*nb::inst_ptr<SettingsGridPublicationLocals>(self);Py_VISIT(o.settings.ptr());Py_VISIT(o.change_brush_size_on_mouse_move_modifiers.ptr());Py_VISIT(o.status_messages.ptr());}return 0;}
int sg_owner_clear(PyObject* self){if(nb::inst_ready(self))nb::inst_ptr<SettingsGridPublicationLocals>(self)->clear();return 0;}
PyType_Slot sg_owner_slots[]={{Py_tp_traverse,reinterpret_cast<void*>(sg_owner_traverse)},{Py_tp_clear,reinterpret_cast<void*>(sg_owner_clear)},{0,nullptr}};
#define GX(value) [&]() -> nb::object { return (value); }
void settings_grid_canvas_init(nb::object self,nb::dict kwargs,nb::dict namespace_,nb::object super_provider,SettingsGridPublicationLocals& o,nb::tuple overlay_keyword_names){
    SettingsGridGlobals ns(std::move(namespace_));
    sg_expanded_call(sg_attr(sg_call(std::move(super_provider)),"__init__"),kwargs);
    sg_call(sg_attr(sg_attr(self,"events"),"add"),{}, {GX(ns.get("Event"))},overlay_keyword_names);
    sg_publish(GX(nb::none()),GX(self),"_viewer_theme");sg_call(sg_attr(self,"_update_viewer_grid"));
    sg_assign(o.settings,sg_call(ns.get("get_settings")));
    sg_call(sg_attr(sg_attr(sg_attr(sg_attr(o.settings,"application"),"events"),"grid_stride"),"connect"),{GX(sg_attr(self,"_update_viewer_grid"))});
    sg_call(sg_attr(sg_attr(sg_attr(sg_attr(o.settings,"application"),"events"),"grid_width"),"connect"),{GX(sg_attr(self,"_update_viewer_grid"))});
    sg_call(sg_attr(sg_attr(sg_attr(sg_attr(o.settings,"application"),"events"),"grid_height"),"connect"),{GX(sg_attr(self,"_update_viewer_grid"))});
    sg_call(sg_attr(sg_attr(sg_attr(sg_attr(o.settings,"application"),"events"),"grid_spacing"),"connect"),{GX(sg_attr(self,"_update_viewer_grid"))});
    sg_call(sg_attr(sg_attr(sg_attr(sg_attr(o.settings,"appearance"),"events"),"theme"),"connect"),{GX(sg_attr(sg_attr(self,"events"),"background_color"))});
}
void settings_grid_canvas_update(nb::object self,nb::dict namespace_,SettingsGridPublicationLocals& o){
    SettingsGridGlobals ns(std::move(namespace_));sg_assign(o.settings,sg_call(ns.get("get_settings")));
    sg_publish(GX(sg_attr(sg_attr(o.settings,"application"),"grid_stride")),GX(sg_attr(self,"grid")),"stride");
    sg_publish(GX(sg_tuple({GX(sg_attr(sg_attr(o.settings,"application"),"grid_height")),GX(sg_attr(sg_attr(o.settings,"application"),"grid_width"))})),GX(sg_attr(self,"grid")),"shape");
    sg_publish(GX(sg_attr(sg_attr(o.settings,"application"),"grid_spacing")),GX(sg_attr(self,"grid")),"spacing");
}
void settings_grid_napari_connect(nb::object self){sg_call(sg_attr(sg_attr(self,"application"),"_connect_events"));sg_call(sg_attr(sg_attr(self,"experimental"),"_connect_events"));}
void settings_grid_application_connect(nb::object self,nb::dict namespace_,nb::tuple value_keyword_names){
    SettingsGridGlobals ns(std::move(namespace_));sg_call(sg_attr(sg_attr(sg_attr(self,"events"),"brush_size_on_mouse_move_modifiers"),"connect"),{GX(ns.get("brush_size_on_mouse_move_modifiers_callback"))});
    sg_call(sg_attr(sg_attr(self,"events"),"brush_size_on_mouse_move_modifiers"),{}, {GX(sg_attr(self,"brush_size_on_mouse_move_modifiers"))},value_keyword_names);
    sg_call(sg_attr(sg_attr(sg_attr(self,"events"),"float_display_precision"),"connect"),{GX(ns.get("float_display_precision_callback"))});
    sg_call(sg_attr(sg_attr(self,"events"),"float_display_precision"),{}, {GX(sg_attr(self,"float_display_precision"))},value_keyword_names);
}
void settings_grid_brush_callback(nb::object event,nb::dict namespace_,SettingsGridPublicationLocals& o,nb::tuple constants){
    SettingsGridGlobals ns(std::move(namespace_));nb::object module=ns.import_module(nb::borrow<nb::object>(constants[0]),nb::borrow<nb::object>(constants[1]),nb::borrow<nb::object>(constants[2]));
    nb::object requested=nb::borrow<nb::object>(PyTuple_GET_ITEM(constants[1].ptr(),0));sg_assign(o.change_brush_size_on_mouse_move_modifiers,sg_import_from(module,requested));module.reset();requested.reset();
    sg_call(o.change_brush_size_on_mouse_move_modifiers,{GX(sg_call(sg_attr(sg_attr(event,"value"),"split"),{GX(nb::borrow<nb::object>(constants[3]))}))});
}
void settings_grid_precision_callback(nb::object event,nb::dict namespace_,SettingsGridPublicationLocals& o,nb::tuple constants){
    SettingsGridGlobals ns(std::move(namespace_));nb::object module=ns.import_module(nb::borrow<nb::object>(constants[0]),nb::borrow<nb::object>(constants[1]),nb::borrow<nb::object>(constants[2]));
    nb::object requested=nb::borrow<nb::object>(PyTuple_GET_ITEM(constants[1].ptr(),0));sg_assign(o.status_messages,sg_import_from(module,requested));module.reset();requested.reset();
    sg_publish(GX(sg_attr(event,"value")),GX(o.status_messages),"DEFAULT_PRECISION");
}
void settings_grid_labels_brush_modifiers(nb::object value,nb::dict namespace_){SettingsGridGlobals ns(std::move(namespace_));ns.store("BRUSH_SIZE_ON_MOUSE_MOVE_MODIFIERS_PARTS",std::move(value));}
#undef GX
} // namespace
void bind_settings_grid_publication(nb::module_& module){
    auto owner=nb::class_<SettingsGridPublicationLocals>(module,"SettingsGridPublicationLocals",nb::is_weak_referenceable(),nb::type_slots(sg_owner_slots));
#define SG_FIELD(name) owner.def_prop_ro(#name,[](const SettingsGridPublicationLocals& value){return value.name.is_valid()?value.name:nb::none();});
    SG_FIELD(settings) SG_FIELD(change_brush_size_on_mouse_move_modifiers) SG_FIELD(status_messages)
#undef SG_FIELD
    module.def("settings_grid_publication_locals",[]{return SettingsGridPublicationLocals();});
    module.def("settings_grid_canvas_init",&settings_grid_canvas_init,nb::arg("self").none(),nb::arg("kwargs"),nb::arg("namespace"),nb::arg("super_provider").none(),nb::arg("locals_owner"),nb::arg("overlay_keyword_names"));
    module.def("settings_grid_canvas_update",&settings_grid_canvas_update,nb::arg("self").none(),nb::arg("namespace"),nb::arg("locals_owner"));
    module.def("settings_grid_napari_connect",&settings_grid_napari_connect,nb::arg("self").none());
    module.def("settings_grid_application_connect",&settings_grid_application_connect,nb::arg("self").none(),nb::arg("namespace"),nb::arg("value_keyword_names"));
    module.def("settings_grid_brush_callback",&settings_grid_brush_callback,nb::arg("event").none(),nb::arg("namespace"),nb::arg("locals_owner"),nb::arg("constants"));
    module.def("settings_grid_precision_callback",&settings_grid_precision_callback,nb::arg("event").none(),nb::arg("namespace"),nb::arg("locals_owner"),nb::arg("constants"));
    module.def("settings_grid_labels_brush_modifiers",&settings_grid_labels_brush_modifiers,nb::arg("value").none(),nb::arg("namespace"));
}
} // namespace nx
