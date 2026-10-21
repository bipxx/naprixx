#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include "event_protocol.hpp"
#include <array>
#include <string>
#include <utility>
#include <cwchar>
#include <climits>
#ifdef MS_WINDOWS
#include <direct.h>
#else
#include <unistd.h>
#endif
#include <osdefs.h>

// Whole current-fork close policy. Actual Qt/Pydantic wrappers and the existing
// private/model dictionaries remain authoritative; this unit creates no widget,
// alternate namespace, persistent close flag or application-state snapshot.
namespace nx {
namespace nb = nanobind;
namespace {
nb::object vc_check(PyObject* p) { if(!p)throw nb::python_error();return nb::steal<nb::object>(p); }
nb::object vc_attr(nb::object p,const char* n) { return vc_check(PyObject_GetAttrString(p.ptr(),n)); }
nb::str vc_name(const char* n) { return nb::steal<nb::str>(vc_check(PyUnicode_InternFromString(n)).release()); }
nb::object vc_call0(nb::object p) { return vc_check(PyObject_CallNoArgs(p.ptr())); }
nb::object vc_call1(nb::object p,nb::object a) { return vc_check(PyObject_CallOneArg(p.ptr(),a.ptr())); }
nb::object vc_call2(nb::object p,nb::object a,nb::object b) { return vc_check(PyObject_CallFunctionObjArgs(p.ptr(),a.ptr(),b.ptr(),nullptr)); }
nb::object vc_kw(nb::object p,nb::tuple a,nb::dict k) { return vc_check(PyObject_Call(p.ptr(),a.ptr(),k.ptr())); }
nb::object vc_item(nb::object p,nb::object key) { return vc_check(PyObject_GetItem(p.ptr(),key.ptr())); }
void vc_store(nb::object p,const char* n,nb::object value) { if(PyObject_SetAttrString(p.ptr(),n,value.ptr())<0)throw nb::python_error(); }
void vc_set(nb::object p,nb::object key,nb::object value) { if(PyObject_SetItem(p.ptr(),key.ptr(),value.ptr())<0)throw nb::python_error(); }
void vc_assign(nb::object& target,nb::object value) { nb::object old=std::move(target);target=std::move(value);old.reset(); }
bool vc_truth(nb::object p) { int n=PyObject_IsTrue(p.ptr());if(n<0)throw nb::python_error();return n!=0; }
nb::object vc_compare(nb::object a,nb::object b,int op) { return vc_check(PyObject_RichCompare(a.ptr(),b.ptr(),op)); }
nb::object vc_iter(nb::object p) { return vc_check(PyObject_GetIter(p.ptr())); }
nb::object vc_next(nb::handle p) { PyObject* n=PyIter_Next(p.ptr());if(!n && PyErr_Occurred())throw nb::python_error();return nb::steal<nb::object>(n); }
nb::object vc_format(nb::object p) { return vc_check(PyObject_Format(p.ptr(),nullptr)); }
nb::object vc_concat(nb::object a,nb::object b) { return vc_check(PyUnicode_Concat(a.ptr(),b.ptr())); }
nb::object vc_lookup(nb::handle mapping,nb::handle key) {
    if(PyDict_CheckExact(mapping.ptr())) {PyObject* p=PyDict_GetItemWithError(mapping.ptr(),key.ptr());if(p)return nb::borrow<nb::object>(p);if(PyErr_Occurred())throw nb::python_error();return nb::object();}
    PyObject* p=PyObject_GetItem(mapping.ptr(),key.ptr());if(p)return nb::steal<nb::object>(p);
    if(!PyErr_ExceptionMatches(PyExc_KeyError))throw nb::python_error();PyErr_Clear();return nb::object();
}
struct CloseNamespace {
    nb::dict globals;nb::object builtins;
    explicit CloseNamespace(nb::dict ns):globals(std::move(ns)),builtins(nb::borrow<nb::object>(PyEval_GetBuiltins())){}
    nb::object get(const char* n)const {
        nb::object key=vc_name(n),p=vc_lookup(globals,key);if(!p.is_valid())p=vc_lookup(builtins,key);if(p.is_valid())return p;
        nb::object e=vc_call1(nb::borrow<nb::object>(PyExc_NameError),vc_check(PyUnicode_FromFormat("name '%s' is not defined",n)));vc_store(e,"name",key);PyErr_SetObject(PyExc_NameError,e.ptr());throw nb::python_error();
    }
};
nb::object vc_super(nb::object provider,nb::object self,const char* method) { return vc_attr(vc_call1(std::move(provider),std::move(self)),method); }
nb::object vc_optional(const nb::object& p) { return p.is_valid()?p:nb::none(); }
void vc_except_type(nb::handle type) {
    bool valid=PyExceptionClass_Check(type.ptr())!=0;
    if(PyTuple_Check(type.ptr())) {valid=true;for(Py_ssize_t i=0;i<PyTuple_GET_SIZE(type.ptr());++i)if(!PyExceptionClass_Check(PyTuple_GET_ITEM(type.ptr(),i))){valid=false;break;}}
    if(!valid){PyErr_SetString(PyExc_TypeError,"catching classes that do not inherit from BaseException is not allowed");throw nb::python_error();}
}
template<size_t N> std::array<nb::object,N> vc_unpack(nb::object value) {
    PyObject* raw=PyObject_GetIter(value.ptr());
    if(!raw){if(PyErr_ExceptionMatches(PyExc_TypeError)&&!Py_TYPE(value.ptr())->tp_iter&&!PySequence_Check(value.ptr()))PyErr_Format(PyExc_TypeError,"cannot unpack non-iterable %.200s object",Py_TYPE(value.ptr())->tp_name);throw nb::python_error();}
    nb::object iterator=nb::steal<nb::object>(raw);std::array<nb::object,N> result;
    for(size_t i=0;i<N;++i){result[i]=vc_next(iterator);if(!result[i].is_valid()){PyErr_Format(PyExc_ValueError,"not enough values to unpack (expected %zu, got %zu)",N,i);throw nb::python_error();}}
    nb::object extra=vc_next(iterator);if(extra.is_valid()){extra.reset();PyErr_Format(PyExc_ValueError,"too many values to unpack (expected %zu)",N);throw nb::python_error();}return result;
}
nb::set vc_status_set(CloseNamespace& ns) {
    nb::object pending=vc_attr(ns.get("Status"),"PENDING"),busy=vc_attr(ns.get("Status"),"BUSY");nb::set result;
    int error=PySet_Add(result.ptr(),pending.ptr());pending.reset();if(error==0)error=PySet_Add(result.ptr(),busy.ptr());busy.reset();if(error){result.reset();throw nb::python_error();}return result;
}
#define CLOSE_FIELDS(X) \
 X(viewers) X(ret) X(viewer) X(confirm_need_local) X(parent) X(task_status) X(dock) X(_) X(widget) X(theme) \
 X(window_fullscreen) X(window_maximized) X(window_state) X(window_size) X(window_position) X(settings) \
 X(ipy_module) X(shell) X(v) X(monitor) X(item) X(messages) X(provider) X(_ts) X(status) X(description) \
 X(extra_info) X(cancel_btn) X(close_btn) X(icon_label) X(text) X(layout) X(layout2) X(layout3) X(text_label) X(layout4)
struct ViewerCloseEdges {
#define VC_FIELD(n) nb::object n;
    CLOSE_FIELDS(VC_FIELD)
#undef VC_FIELD
};
struct ViewerCloseLocals : ViewerCloseEdges {
    std::string operation;
    ViewerCloseLocals()=default;ViewerCloseLocals(ViewerCloseLocals&&)=default;
    ~ViewerCloseLocals(){clear();}
    void clear();
};
void ViewerCloseLocals::clear() {
    // Detach every source edge before any destructor can reenter the owner.
    ViewerCloseEdges old;
#define VC_DETACH(n) old.n=std::move(n);
    CLOSE_FIELDS(VC_DETACH)
#undef VC_DETACH
    if(operation=="get_settings"){old.window_fullscreen.reset();old.window_maximized.reset();old.window_state.reset();}
    else if(operation=="save_settings"){old.window_state.reset();old.window_size.reset();old.window_position.reset();old.window_maximized.reset();old.window_fullscreen.reset();old.settings.reset();}
    else if(operation=="task_status"){old.messages.reset();old._.reset();old.item.reset();old.provider.reset();old._ts.reset();old.status.reset();old.description.reset();}
    // Remaining layouts have the source declaration order in CLOSE_FIELDS.
#define VC_RELEASE(n) old.n.reset();
    CLOSE_FIELDS(VC_RELEASE)
#undef VC_RELEASE
}
int vc_locals_traverse(PyObject* self,visitproc visit,void* arg) {
    Py_VISIT(Py_TYPE(self));if(nb::inst_ready(self)){const auto& s=*nb::inst_ptr<ViewerCloseLocals>(self);
#define VC_VISIT(n) Py_VISIT(s.n.ptr());
    CLOSE_FIELDS(VC_VISIT)
#undef VC_VISIT
    }return 0;
}
int vc_locals_clear(PyObject* self){if(nb::inst_ready(self))nb::inst_ptr<ViewerCloseLocals>(self)->clear();return 0;}
PyType_Slot vc_locals_slots[]={{Py_tp_traverse,reinterpret_cast<void*>(vc_locals_traverse)},{Py_tp_clear,reinterpret_cast<void*>(vc_locals_clear)},{0,nullptr}};

void close_viewer(nb::object self,nb::dict namespace_) {
    CloseNamespace ns(std::move(namespace_));vc_call0(vc_attr(vc_attr(self,"_layer_slicer"),"shutdown"));
    nb::object fn=ns.get("disconnect_events"),events=vc_attr(vc_attr(self,"dims"),"events");vc_call2(std::move(fn),std::move(events),self);
    vc_call0(vc_attr(vc_attr(self,"layers"),"clear"));vc_call0(vc_attr(vc_attr(self,"window"),"close"));vc_call1(vc_attr(vc_attr(self,"_instances"),"discard"),self);
}
nb::object close_all_viewers(nb::object cls,nb::dict namespace_,ViewerCloseLocals& s) {
    CloseNamespace ns(std::move(namespace_));nb::object fn=ns.get("list");vc_assign(s.viewers,vc_call1(std::move(fn),vc_attr(cls,"_instances")));vc_assign(s.ret,vc_call1(ns.get("len"),s.viewers));
    nb::object iterator=vc_iter(s.viewers);while(nb::object next=vc_next(iterator)){vc_assign(s.viewer,std::move(next));vc_call0(vc_attr(s.viewer,"close"));}return s.ret;
}
nb::object close_main_window(nb::object self,nb::object quit_app,nb::object confirm_need,nb::dict namespace_,nb::object super_provider,ViewerCloseLocals& s) {
    CloseNamespace ns(std::move(namespace_));
    if(!vc_truth(quit_app)&&!vc_truth(vc_attr(vc_attr(vc_attr(self,"_qt_viewer"),"viewer"),"layers")))return vc_call0(vc_super(std::move(super_provider),self,"close"));
    vc_assign(s.confirm_need_local,vc_truth(confirm_need)?vc_item(vc_attr(self,"_is_close_dialog"),quit_app):confirm_need);
    vc_set(vc_attr(self,"_is_close_dialog"),quit_app,nb::bool_(false));
    bool accepted=!vc_truth(s.confirm_need_local);
    if(!accepted)accepted=!vc_truth(vc_attr(vc_attr(vc_call0(ns.get("get_settings")),"application"),"confirm_close_window"));
    if(!accepted){nb::object result=vc_call0(vc_attr(vc_call2(ns.get("ConfirmCloseDialog"),self,quit_app),"exec_"));nb::object expected=vc_attr(vc_attr(ns.get("QDialog"),"DialogCode"),"Accepted");accepted=vc_truth(vc_compare(std::move(result),std::move(expected),Py_EQ));}
    if(accepted){vc_store(self,"_quit_app",quit_app);vc_set(vc_attr(self,"_is_close_dialog"),quit_app,nb::bool_(true));vc_call0(vc_attr(vc_attr(vc_attr(self,"_qt_viewer"),"dims"),"stop"));return vc_call0(vc_super(std::move(super_provider),self,"close"));}
    vc_set(vc_attr(self,"_is_close_dialog"),quit_app,nb::bool_(true));return nb::none();
}
void close_active_window(nb::object self,nb::dict namespace_,nb::object parent_name,ViewerCloseLocals& s) {
    CloseNamespace ns(std::move(namespace_));vc_assign(s.parent,vc_call0(vc_attr(ns.get("QApplication"),"focusWidget")));
    while(!s.parent.is_none()) {
        nb::object fn=ns.get("isinstance"),p=s.parent,type=ns.get("QMainWindow");if(vc_truth(vc_call2(std::move(fn),std::move(p),std::move(type)))){vc_call0(vc_attr(self,"close"));break;}
        fn=ns.get("isinstance");p=s.parent;type=ns.get("QDialog");if(vc_truth(vc_call2(std::move(fn),std::move(p),std::move(type)))){vc_call0(vc_attr(s.parent,"close"));break;}
        try {vc_assign(s.parent,vc_call0(vc_attr(s.parent,"parent")));}
        catch(const nb::python_error& error){events::HandledException handled(error.value());nb::object kind=ns.get("AttributeError");vc_except_type(kind);if(!PyErr_GivenExceptionMatches(error.value().ptr(),kind.ptr()))throw;nb::object getter=ns.get("getattr");vc_assign(s.parent,vc_check(PyObject_CallFunctionObjArgs(getter.ptr(),s.parent.ptr(),parent_name.ptr(),Py_None,nullptr)));}
    }
}
void stop_main_workers(nb::object self,nb::dict) {vc_call0(vc_attr(vc_attr(self,"status_thread"),"close_terminate"));vc_call0(vc_attr(vc_attr(self,"status_thread"),"wait"));vc_call0(vc_attr(vc_attr(vc_attr(self,"_qt_viewer"),"dims"),"stop"));}
void close_main_event(nb::object self,nb::object event,nb::dict namespace_,nb::object join_separator,nb::object draw_pause,ViewerCloseLocals& s) {
    CloseNamespace ns(std::move(namespace_));vc_assign(s.task_status,vc_call0(vc_attr(vc_attr(vc_attr(self,"_window"),"_task_status_manager"),"get_status")));
    bool reject=false;
    if(vc_truth(vc_call0(vc_attr(event,"spontaneous")))&&vc_truth(s.task_status)) {
        nb::object factory=ns.get("ConfirmCloseDialog"),join=vc_attr(join_separator,"join");nb::object extra=vc_call1(std::move(join),s.task_status);nb::dict kwargs;kwargs["close_app"]=nb::bool_(false);kwargs["extra_info"]=std::move(extra);kwargs["display_checkbox"]=nb::bool_(false);
        nb::object result=vc_call0(vc_attr(vc_kw(std::move(factory),nb::make_tuple(self),std::move(kwargs)),"exec_"));nb::object expected=vc_attr(ns.get("QDialog"),"Accepted");reject=vc_truth(vc_compare(std::move(result),std::move(expected),Py_NE));
    }
    if(!reject && vc_truth(vc_call0(vc_attr(event,"spontaneous"))) && vc_truth(vc_attr(vc_attr(vc_call0(ns.get("get_settings")),"application"),"confirm_close_window")) && vc_truth(vc_attr(vc_attr(vc_attr(self,"_qt_viewer"),"viewer"),"layers"))) {
        nb::object factory=ns.get("ConfirmCloseDialog");nb::dict kwargs;kwargs["close_app"]=nb::bool_(false);nb::object result=vc_call0(vc_attr(vc_kw(std::move(factory),nb::make_tuple(self),std::move(kwargs)),"exec_"));nb::object expected=vc_attr(ns.get("QDialog"),"Accepted");reject=vc_truth(vc_compare(std::move(result),std::move(expected),Py_NE));
    }
    if(reject){vc_call0(vc_attr(event,"ignore"));return;}
    if(vc_truth(vc_call0(vc_attr(vc_attr(vc_attr(self,"_window"),"_task_status_manager"),"is_busy"))))vc_call0(vc_attr(vc_attr(vc_attr(self,"_window"),"_task_status_manager"),"cancel_all"));
    vc_call0(vc_attr(self,"_stop_worker_threads"));
    if(vc_truth(vc_attr(self,"_ev"))&&vc_truth(vc_call0(vc_attr(vc_attr(self,"_ev"),"isRunning"))))vc_call0(vc_attr(vc_attr(self,"_ev"),"quit"));
    nb::object find=vc_attr(self,"findChildren"),kind=ns.get("QtViewerDockWidget");nb::object iterator=vc_iter(vc_call1(std::move(find),std::move(kind)));
    while(nb::object next=vc_next(iterator)){vc_assign(s.dock,std::move(next));nb::object instance=ns.get("isinstance"),dock=s.dock,widget=ns.get("QWidget");if(vc_truth(vc_call2(std::move(instance),std::move(dock),std::move(widget)))&&vc_truth(vc_call0(vc_attr(s.dock,"isFloating"))))vc_call1(vc_attr(s.dock,"setFloating"),nb::bool_(false));}
    iterator.reset();vc_call0(vc_attr(self,"_save_current_window_settings"));
    if(vc_truth(vc_call0(vc_attr(self,"isFullScreen")))){vc_call0(vc_attr(self,"showNormal"));iterator=vc_iter(vc_call1(ns.get("range"),nb::int_(5)));while(nb::object next=vc_next(iterator)){vc_assign(s._,std::move(next));vc_call1(vc_attr(ns.get("time"),"sleep"),draw_pause);vc_call0(vc_attr(ns.get("QApplication"),"processEvents"));}iterator.reset();}
    if(vc_truth(vc_attr(self,"_quit_app")))vc_call0(ns.get("quit_app_"));vc_call0(vc_attr(event,"accept"));
}
void theme_connections(nb::object self,nb::object theme,bool connect) {
    const char* method=connect?"connect":"disconnect";
    for(const char* member:{"background","foreground","primary","secondary","highlight","text","warning","current","icon","font_size"}) {nb::object fn=vc_attr(vc_attr(vc_attr(theme,"events"),member),method),callback=vc_attr(self,"_update_theme_no_event");vc_call1(std::move(fn),std::move(callback));}
    nb::object fn=vc_attr(vc_attr(vc_attr(theme,"events"),"canvas"),method),callback=vc_attr(self,"_update_canvas_theme");vc_call1(std::move(fn),std::move(callback));
    if(vc_truth(vc_attr(vc_attr(self,"_qt_viewer"),"_console"))) {
        fn=vc_attr(vc_attr(vc_attr(theme,"events"),"console"),method);callback=vc_attr(vc_attr(vc_attr(self,"_qt_viewer"),"console"),"_update_theme");vc_call1(std::move(fn),std::move(callback));
        fn=vc_attr(vc_attr(vc_attr(theme,"events"),"syntax_style"),method);callback=vc_attr(vc_attr(vc_attr(self,"_qt_viewer"),"console"),"_update_theme");vc_call1(std::move(fn),std::move(callback));
    }
}
void setup_themes(nb::object self,nb::object connect,nb::dict namespace_,ViewerCloseLocals& s) {CloseNamespace ns(std::move(namespace_));nb::object iterator=vc_iter(vc_call0(vc_attr(ns.get("_themes"),"values")));while(nb::object next=vc_next(iterator)){vc_assign(s.theme,std::move(next));vc_call1(vc_attr(self,vc_truth(connect)?"_connect_theme":"_disconnect_theme"),s.theme);}}
void teardown_window(nb::object self,nb::dict namespace_) {CloseNamespace ns(std::move(namespace_));vc_call1(vc_attr(self,"_setup_existing_themes"),nb::bool_(false));nb::object fn=vc_attr(vc_attr(vc_attr(ns.get("_themes"),"events"),"added"),"disconnect"),callback=vc_attr(self,"_add_theme");vc_call1(std::move(fn),std::move(callback));fn=vc_attr(vc_attr(vc_attr(ns.get("_themes"),"events"),"removed"),"disconnect");callback=vc_attr(self,"_remove_theme");vc_call1(std::move(fn),std::move(callback));}
void close_window(nb::object self,nb::dict namespace_,nb::object window_name) {CloseNamespace ns(std::move(namespace_));if(vc_truth(vc_call2(ns.get("hasattr"),self,window_name))){vc_call0(vc_attr(self,"_teardown"));vc_call0(vc_attr(vc_attr(self,"_qt_viewer"),"close"));vc_call0(vc_attr(vc_attr(self,"_qt_window"),"close"));if(PyObject_DelAttrString(self.ptr(),"_qt_window")<0)throw nb::python_error();}}
void update_canvas_theme(nb::object self,nb::dict namespace_) {CloseNamespace ns(std::move(namespace_));nb::object fn=vc_attr(vc_attr(vc_attr(self,"_qt_viewer"),"canvas"),"_set_theme_change"),theme=vc_attr(vc_attr(vc_call0(ns.get("get_settings")),"appearance"),"theme");vc_call1(std::move(fn),std::move(theme));}
void update_theme_no_event(nb::object self,nb::dict) {vc_call0(vc_attr(self,"_update_theme"));}
void theme_added(nb::object self,nb::object event,nb::dict,ViewerCloseLocals& s){vc_assign(s.theme,vc_attr(std::move(event),"value"));vc_call1(vc_attr(self,"_connect_theme"),s.theme);}
void theme_removed(nb::object self,nb::object event,nb::dict,ViewerCloseLocals& s){vc_assign(s.theme,vc_attr(std::move(event),"value"));vc_call1(vc_attr(self,"_disconnect_theme"),s.theme);}
void close_qt_viewer(nb::object self,nb::object event,nb::dict) {if(!vc_attr(self,"_layers").is_none())vc_call0(vc_attr(vc_attr(self,"layers"),"close"));vc_call0(vc_attr(vc_attr(self,"dims"),"stop"));vc_call0(vc_attr(vc_attr(self,"dims"),"close"));vc_call0(vc_attr(vc_attr(self,"canvas"),"delete"));if(!vc_attr(self,"_console").is_none())vc_call0(vc_attr(vc_attr(self,"_console"),"close"));vc_call0(vc_attr(vc_attr(self,"dockConsole"),"deleteLater"));if(!event.is_none())vc_call0(vc_attr(event,"accept"));}
void stop_dims(nb::object self,nb::dict){vc_call0(vc_attr(vc_attr(self,"_animation_thread"),"_stop"));vc_call0(vc_attr(vc_attr(self,"_animation_thread"),"wait"));}
void close_dims(nb::object self,nb::object event,nb::dict namespace_,ViewerCloseLocals& s) {
    CloseNamespace ns(std::move(namespace_));vc_call0(vc_attr(self,"stop"));
    if(!vc_attr(self,"dims").is_none()) {
        nb::object fn=ns.get("disconnect_events"),events=vc_attr(vc_attr(self,"dims"),"events");vc_call2(std::move(fn),std::move(events),self);
        fn=ns.get("disconnect_events");events=vc_attr(vc_attr(self,"dims"),"events");nb::object thread=vc_attr(self,"_animation_thread");vc_call2(std::move(fn),std::move(events),std::move(thread));
        nb::object iterator=vc_iter(vc_attr(self,"slider_widgets"));while(nb::object next=vc_next(iterator)){
            vc_assign(s.widget,std::move(next));fn=ns.get("disconnect_events");events=vc_attr(vc_attr(self,"dims"),"events");vc_call2(std::move(fn),std::move(events),s.widget);
            fn=vc_attr(vc_attr(vc_attr(s.widget,"curslice_label"),"editingFinished"),"disconnect");nb::object callback=vc_attr(s.widget,"_set_slice_from_label");vc_call1(std::move(fn),std::move(callback));
            fn=vc_attr(vc_attr(vc_attr(s.widget,"axis_label"),"editingFinished"),"disconnect");callback=vc_attr(s.widget,"_clear_label_focus");vc_call1(std::move(fn),std::move(callback));
            vc_store(s.widget,"dims",nb::none());vc_store(s.widget,"qt_dims",nb::none());vc_call0(vc_attr(s.widget,"deleteLater"));
        }
        iterator.reset();vc_call0(vc_attr(vc_attr(self,"slider_widgets"),"clear"));vc_call0(vc_attr(vc_attr(self,"_displayed_sliders"),"clear"));vc_store(self,"dims",nb::none());
    }
    vc_call0(vc_attr(self,"deleteLater"));vc_call0(vc_attr(event,"accept"));
}
void stop_animation(nb::object self,nb::dict){vc_call0(vc_attr(vc_attr(self,"_waiter"),"set"));}
void close_status(nb::object self,nb::dict){vc_store(self,"_allow_start",nb::bool_(false));vc_call0(vc_attr(self,"terminate"));}
void terminate_status(nb::object self,nb::dict){vc_store(self,"_terminate",nb::bool_(true));vc_call0(vc_attr(vc_attr(self,"_need_status_update"),"set"));}
void start_status(nb::object self,nb::object priority,nb::dict,nb::object super_provider){if(!vc_truth(vc_attr(self,"_allow_start")))return;vc_store(self,"_terminate",nb::bool_(false));vc_call1(vc_super(std::move(super_provider),self,"start"),std::move(priority));}
void disconnect_canvas(nb::object self,nb::dict namespace_) {
    CloseNamespace ns(std::move(namespace_));
    for(const char* path:{"","canvas","canvas.overlays","canvas.overlay_tiling","scene.overlays","scene.camera","scene.camera","layers","cursor"}) {
        nb::object fn=ns.get("disconnect_events"),target=vc_attr(self,"viewer");std::string p(path);size_t begin=0;
        while(begin<p.size()){size_t dot=p.find('.',begin);std::string member=p.substr(begin,dot==std::string::npos?std::string::npos:dot-begin);target=vc_attr(std::move(target),member.c_str());if(dot==std::string::npos)break;begin=dot+1;}
        vc_call2(std::move(fn),vc_attr(std::move(target),"events"),self);
    }
    nb::object fn=ns.get("disconnect_events"),events=vc_attr(vc_attr(self,"_scene_canvas"),"events");vc_call2(std::move(fn),std::move(events),self);
}
void delete_canvas(nb::object self,nb::dict){vc_call0(vc_attr(vc_attr(self,"native"),"deleteLater"));}

// Reuse the already reviewed native interpreter primitive; no Python eval/body fallback.
// Complete CPython IMPORT_FROM primitive for the source-verified 3.12,
// 3.13 and 3.14 profiles. These are interpreter protocol/error semantics,
// not napari state. Primary ceval.c/moduleobject.c/errors.c sources:
// https://raw.githubusercontent.com/python/cpython/v3.12.8/Python/ceval.c
// https://raw.githubusercontent.com/python/cpython/v3.13.14/Python/ceval.c
// https://raw.githubusercontent.com/python/cpython/v3.14.4/Python/ceval.c
// Compiled/installed 3.13/3.14 application parity is a separate open gate.
nb::object vc_optional_attr(nb::handle object, const char* name) {
    PyObject* result = PyObject_GetAttrString(object.ptr(), name);
    if (result) return nb::steal<nb::object>(result);
    if (!PyErr_ExceptionMatches(PyExc_AttributeError)) throw nb::python_error();
    PyErr_Clear();
    return nb::object();
}
[[noreturn]] void vc_raise_import(nb::object message, nb::handle name,
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
    nb::object error = vc_kw(nb::borrow<nb::object>(PyExc_ImportError), std::move(args), std::move(kwargs));
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
wchar_t* vc_wgetcwd(wchar_t* buffer, size_t capacity) {
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
nb::object vc_spec_file_origin(nb::handle spec) {
    nb::object located = vc_optional_attr(spec, "has_location");
    if (!located.is_valid()) return nb::object();
    bool has_location = vc_truth(std::move(located));
    if (!has_location) return nb::object();
    nb::object origin = vc_optional_attr(spec, "origin");
    if (origin.is_valid() && !PyUnicode_Check(origin.ptr())) origin.reset();
    return origin;
}
bool vc_possibly_shadowing(nb::handle origin) {
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
        if (!vc_wgetcwd(cwd, MAXPATHLEN)) return false;
        path0 = cwd;
    }
    return std::wcscmp(path0, root) == 0;
}
nb::object vc_import_from(nb::handle module, nb::handle requested_name) {
    PyObject* result = PyObject_GetAttr(module.ptr(), requested_name.ptr());
    if (result) return nb::steal<nb::object>(result);
    if (!PyErr_ExceptionMatches(PyExc_AttributeError)) throw nb::python_error();
    PyErr_Clear();
    nb::object mod_name = vc_optional_attr(module, "__name__");
    if (mod_name.is_valid() && !PyUnicode_Check(mod_name.ptr())) mod_name.reset();
    if (mod_name.is_valid()) {
        nb::object fullname = vc_check(PyUnicode_FromFormat("%U.%U", mod_name.ptr(), requested_name.ptr()));
        result = PyImport_GetModule(fullname.ptr());
        fullname.reset();
        if (result) return nb::steal<nb::object>(result);
        if (PyErr_Occurred()) throw nb::python_error();
    }
    PyObject* raw_mod_name = mod_name.ptr();
    nb::object name_or_unknown = mod_name.is_valid() ? std::move(mod_name) : nb::str("<unknown module name>");
    nb::object origin, spec, message;
    try {
        spec = vc_optional_attr(module, "__spec__");
        if (!spec.is_valid()) {
            message = vc_check(PyUnicode_FromFormat(
                "cannot import name %R from %R (unknown location)", requested_name.ptr(), name_or_unknown.ptr()));
        } else {
            origin = vc_spec_file_origin(spec);
            bool shadowing = vc_possibly_shadowing(origin);
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
                message = vc_check(PyUnicode_FromFormat(
                    "cannot import name %R from %R (consider renaming %R since it has the same name as the standard library module named %R and prevents importing that standard library module)",
                    requested_name.ptr(), name_or_unknown.ptr(), origin.ptr(), name_or_unknown.ptr()));
            } else {
                nb::object initializing = vc_optional_attr(spec, "_initializing");
                bool is_initializing = initializing.is_valid() && vc_truth(std::move(initializing));
                if (is_initializing && shadowing) {
                    message = vc_check(PyUnicode_FromFormat(
                        "cannot import name %R from %R (consider renaming %R if it has the same name as a library you intended to import)",
                        requested_name.ptr(), name_or_unknown.ptr(), origin.ptr()));
                } else if (is_initializing && origin.is_valid()) {
                    message = vc_check(PyUnicode_FromFormat(
                        "cannot import name %R from partially initialized module %R (most likely due to a circular import) (%S)",
                        requested_name.ptr(), name_or_unknown.ptr(), origin.ptr()));
                } else if (is_initializing) {
                    message = vc_check(PyUnicode_FromFormat(
                        "cannot import name %R from partially initialized module %R (most likely due to a circular import)",
                        requested_name.ptr(), name_or_unknown.ptr()));
                } else if (origin.is_valid()) {
                    message = vc_check(PyUnicode_FromFormat("cannot import name %R from %R (%S)",
                        requested_name.ptr(), name_or_unknown.ptr(), origin.ptr()));
                } else {
                    message = vc_check(PyUnicode_FromFormat("cannot import name %R from %R (unknown location)",
                        requested_name.ptr(), name_or_unknown.ptr()));
                }
            }
        }
        vc_raise_import(std::move(message), nb::handle(raw_mod_name), origin, requested_name);
    } catch (...) {
        // Actual import_from error cleanup order, including callback failures.
        message.reset(); origin.reset(); spec.reset(); name_or_unknown.reset();
        throw;
    }
}
#else
bool vc_spec_initializing(nb::handle spec) {
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
nb::object vc_import_from(nb::handle module, nb::handle requested_name) {
    PyObject* result = PyObject_GetAttr(module.ptr(), requested_name.ptr());
    if (result) return nb::steal<nb::object>(result);
    if (!PyErr_ExceptionMatches(PyExc_AttributeError)) throw nb::python_error();
    PyErr_Clear();
    nb::object pkgname = nb::steal<nb::object>(PyObject_GetAttrString(module.ptr(), "__name__"));
    if (pkgname.is_valid() && !PyUnicode_Check(pkgname.ptr())) pkgname.reset();
    if (pkgname.is_valid()) {
        nb::object fullname = vc_check(PyUnicode_FromFormat("%U.%U", pkgname.ptr(), requested_name.ptr()));
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
        bool initializing = vc_spec_initializing(spec);
        spec.reset();
        const char* format = initializing
            ? "cannot import name %R from partially initialized module %R (most likely due to a circular import) (%S)"
            : "cannot import name %R from %R (%S)";
        message = nb::steal<nb::object>(PyUnicode_FromFormat(format, requested_name.ptr(), name_or_unknown.ptr(), pkgpath.ptr()));
        error_path = pkgpath;
    }
    try { vc_raise_import(std::move(message), nb::handle(raw_pkgname), error_path, requested_name); }
    catch (...) {
        // Actual 3.12 error cleanup order.
        message.reset(); name_or_unknown.reset(); pkgpath.reset();
        throw;
    }
}
#endif


nb::object get_window_settings(nb::object self,nb::dict namespace_,ViewerCloseLocals& s) {
    CloseNamespace ns(std::move(namespace_));vc_assign(s.window_fullscreen,vc_call0(vc_attr(self,"isFullScreen")));
    vc_assign(s.window_maximized,vc_truth(s.window_fullscreen)?vc_attr(self,"_maximized_flag"):vc_call0(vc_attr(self,"isMaximized")));
    nb::object converter=ns.get("qbytearray_to_str");vc_assign(s.window_state,vc_call1(std::move(converter),vc_call0(vc_attr(self,"saveState"))));
    nb::object size=vc_attr(self,"_window_size");if(!vc_truth(size)){size.reset();nb::object width=vc_call0(vc_attr(self,"width")),height=vc_call0(vc_attr(self,"height"));size=nb::make_tuple(std::move(width),std::move(height));}
    nb::object position=vc_attr(self,"_window_pos");if(!vc_truth(position)){position.reset();nb::object x=vc_call0(vc_attr(self,"x")),y=vc_call0(vc_attr(self,"y"));position=nb::make_tuple(std::move(x),std::move(y));}
    return nb::make_tuple(s.window_state,std::move(size),std::move(position),s.window_maximized,s.window_fullscreen);
}
void save_window_settings(nb::object self,nb::dict namespace_,ViewerCloseLocals& s) {
    CloseNamespace ns(std::move(namespace_));auto values=vc_unpack<5>(vc_call0(vc_attr(self,"_get_window_settings")));
    vc_assign(s.window_state,std::move(values[0]));vc_assign(s.window_size,std::move(values[1]));vc_assign(s.window_position,std::move(values[2]));vc_assign(s.window_maximized,std::move(values[3]));vc_assign(s.window_fullscreen,std::move(values[4]));
    vc_assign(s.settings,vc_call0(ns.get("get_settings")));
    if(vc_truth(vc_attr(vc_attr(s.settings,"application"),"save_window_geometry"))){vc_store(vc_attr(s.settings,"application"),"window_maximized",s.window_maximized);vc_store(vc_attr(s.settings,"application"),"window_fullscreen",s.window_fullscreen);vc_store(vc_attr(s.settings,"application"),"window_position",s.window_position);vc_store(vc_attr(s.settings,"application"),"window_size",s.window_size);nb::object hidden=vc_call0(vc_attr(vc_call0(vc_attr(self,"statusBar")),"isHidden"));nb::object visible=nb::bool_(!vc_truth(std::move(hidden)));vc_store(vc_attr(s.settings,"application"),"window_statusbar",std::move(visible));}
    if(vc_truth(vc_attr(vc_attr(s.settings,"application"),"save_window_state")))vc_store(vc_attr(s.settings,"application"),"window_state",s.window_state);
}
nb::object bytearray_to_str(nb::object value,nb::dict namespace_) {CloseNamespace ns(std::move(namespace_));nb::object flag=ns.get("QBYTE_FLAG"),data=vc_call0(vc_attr(vc_call0(vc_attr(value,"toBase64")),"data"));nb::object decoded=vc_call0(vc_attr(std::move(data),"decode"));return vc_check(PyNumber_Add(flag.ptr(),decoded.ptr()));}
nb::object task_timestamp(nb::object,nb::dict namespace_) {CloseNamespace ns(std::move(namespace_));return vc_call0(vc_attr(vc_call0(vc_attr(vc_attr(ns.get("datetime"),"datetime"),"now")),"isoformat"));}
void task_update(nb::object self,nb::object status,nb::object description,nb::dict) {nb::object append=vc_attr(vc_attr(self,"_timestamp"),"append"),stamp=vc_call0(vc_attr(self,"_timestap"));vc_call1(std::move(append),std::move(stamp));vc_call1(vc_attr(vc_attr(self,"_status"),"append"),std::move(status));vc_call1(vc_attr(vc_attr(self,"_description"),"append"),std::move(description));}
nb::object task_cancel(nb::object self,nb::dict namespace_,nb::object empty_description) {CloseNamespace ns(std::move(namespace_));nb::object update=vc_attr(self,"update"),status=vc_attr(ns.get("Status"),"CANCELLED");vc_call2(std::move(update),std::move(status),std::move(empty_description));if(!vc_attr(self,"_cancel_callback").is_none())return vc_call0(vc_attr(self,"_cancel_callback"));return nb::bool_(false);}
nb::object task_state(nb::object self,nb::dict) {nb::object provider=vc_attr(self,"_provider"),stamp=vc_item(vc_attr(self,"_timestamp"),nb::int_(-1)),status=vc_item(vc_attr(self,"_status"),nb::int_(-1)),description=vc_item(vc_attr(self,"_description"),nb::int_(-1));return nb::make_tuple(std::move(provider),std::move(stamp),std::move(status),std::move(description));}
nb::object tasks_busy(nb::object self,nb::dict namespace_,ViewerCloseLocals& s) {CloseNamespace ns(std::move(namespace_));nb::object iterator=vc_iter(vc_call0(vc_attr(vc_attr(self,"_tasks"),"items")));while(nb::object next=vc_next(iterator)){auto pair=vc_unpack<2>(std::move(next));vc_assign(s._,std::move(pair[0]));vc_assign(s.item,std::move(pair[1]));nb::object state=vc_item(vc_call0(vc_attr(s.item,"state")),nb::int_(2)),valid=vc_status_set(ns);int found=PySequence_Contains(valid.ptr(),state.ptr());if(found<0)throw nb::python_error();state.reset();valid.reset();if(found)return nb::bool_(true);}return nb::bool_(false);}
template<size_t N> nb::object vc_build_string(std::array<nb::object,N> pieces);
nb::object tasks_status(nb::object self,nb::dict namespace_,ViewerCloseLocals& s) {
    CloseNamespace ns(std::move(namespace_));vc_assign(s.messages,nb::list());nb::object iterator=vc_iter(vc_call0(vc_attr(vc_attr(self,"_tasks"),"items")));
    while(nb::object next=vc_next(iterator)){auto pair=vc_unpack<2>(std::move(next));vc_assign(s._,std::move(pair[0]));vc_assign(s.item,std::move(pair[1]));auto state=vc_unpack<4>(vc_call0(vc_attr(s.item,"state")));vc_assign(s.provider,std::move(state[0]));vc_assign(s._ts,std::move(state[1]));vc_assign(s.status,std::move(state[2]));vc_assign(s.description,std::move(state[3]));nb::object valid=vc_status_set(ns);int found=PySequence_Contains(valid.ptr(),s.status.ptr());if(found<0)throw nb::python_error();valid.reset();if(found){nb::object append=vc_attr(s.messages,"append"),provider=vc_format(s.provider);nb::object description=vc_format(s.description);nb::object text=vc_build_string<3>({std::move(provider),nb::str(": "),std::move(description)});vc_call1(std::move(append),std::move(text));}}
    return s.messages;
}
void tasks_cancel(nb::object self,nb::dict,ViewerCloseLocals& s){nb::object iterator=vc_iter(vc_call0(vc_attr(vc_attr(self,"_tasks"),"items")));while(nb::object next=vc_next(iterator)){auto pair=vc_unpack<2>(std::move(next));vc_assign(s._,std::move(pair[0]));vc_assign(s.item,std::move(pair[1]));vc_call0(vc_attr(s.item,"cancel"));}}
nb::object ipython_eventloop(nb::dict namespace_,nb::object module_name,nb::object qt_name,ViewerCloseLocals& s) {
    CloseNamespace ns(std::move(namespace_));vc_assign(s.ipy_module,vc_call1(vc_attr(vc_attr(ns.get("sys"),"modules"),"get"),std::move(module_name)));if(!vc_truth(s.ipy_module))return nb::bool_(false);vc_assign(s.shell,vc_call0(vc_attr(s.ipy_module,"get_ipython")));if(!vc_truth(s.shell))return nb::bool_(false);return vc_compare(vc_attr(s.shell,"active_eventloop"),std::move(qt_name),Py_EQ);
}
void quit_application(nb::dict namespace_,nb::object app_name,nb::object import_name,nb::object import_fromlist,ViewerCloseLocals& s) {
    CloseNamespace ns(std::move(namespace_));nb::object list=ns.get("list"),instances=vc_attr(ns.get("Viewer"),"_instances");nb::object iterator=vc_iter(vc_call1(std::move(list),std::move(instances)));while(nb::object next=vc_next(iterator)){vc_assign(s.v,std::move(next));vc_call0(vc_attr(s.v,"close"));}iterator.reset();vc_call0(vc_attr(ns.get("QApplication"),"closeAllWindows"));
    nb::object name=vc_call0(vc_attr(ns.get("QApplication"),"applicationName"));bool ours=vc_truth(vc_compare(std::move(name),std::move(app_name),Py_EQ));
    if(ours&&!vc_truth(vc_call0(ns.get("_ipython_has_eventloop"))))vc_call0(vc_attr(ns.get("QApplication"),"quit"));else{nb::object setter=vc_attr(ns.get("QApplication"),"setWindowIcon"),icon=vc_call0(ns.get("QIcon"));vc_call1(std::move(setter),std::move(icon));}
    if(!vc_attr(ns.get("perf"),"perf_config").is_none())vc_call0(vc_attr(vc_attr(ns.get("perf"),"timers"),"stop_trace_file"));
    if(vc_truth(vc_attr(ns.get("config"),"monitor"))) {
        nb::object importer=vc_lookup(ns.builtins,vc_name("__import__"));if(!importer.is_valid()){PyErr_SetString(PyExc_ImportError,"__import__ not found");throw nb::python_error();}
        PyObject* raw=PyEval_GetLocals();if(!raw&&PyErr_Occurred())throw nb::python_error();nb::object locals=nb::borrow<nb::object>(raw?raw:Py_None);
        nb::object module=vc_check(PyObject_CallFunctionObjArgs(importer.ptr(),import_name.ptr(),ns.globals.ptr(),locals.ptr(),import_fromlist.ptr(),nb::int_(0).ptr(),nullptr));
        vc_assign(s.monitor,vc_import_from(module,vc_item(import_fromlist,nb::int_(0))));module.reset();vc_call0(vc_attr(s.monitor,"stop"));
    }
}

nb::object vc_literal(nb::handle values,Py_ssize_t index) { return nb::borrow<nb::object>(PyTuple_GET_ITEM(values.ptr(),index)); }
template<size_t N> nb::object vc_build_string(std::array<nb::object,N> pieces) {
    nb::tuple parts=nb::steal<nb::tuple>(vc_check(PyTuple_New(N)).release());
    for(size_t i=0;i<N;++i)PyTuple_SET_ITEM(parts.ptr(),i,pieces[i].inc_ref().ptr());
    PyObject* raw=PyUnicode_Join(nb::str("").ptr(),parts.ptr());parts.reset();
    // CPython 3.11-3.14 BUILD_STRING releases pieces in reverse stack order, even on error.
    for(size_t i=N;i>0;)pieces[--i].reset();return vc_check(raw);
}
nb::object vc_shortcut(CloseNamespace& ns,nb::object literal) {nb::object key=vc_call1(ns.get("QKeySequence"),std::move(literal)),fn=vc_attr(std::move(key),"toString");nb::object mode=vc_attr(vc_attr(ns.get("QKeySequence"),"SequenceFormat"),"NativeText");return vc_call1(std::move(fn),std::move(mode));}
void init_confirm_dialog(nb::object self,nb::object parent,nb::object close_app,nb::object display_checkbox,nb::dict namespace_,nb::object super_provider,nb::tuple literals,ViewerCloseLocals& s) {
    CloseNamespace ns(std::move(namespace_));vc_call1(vc_super(std::move(super_provider),self,"__init__"),std::move(parent));
    if(vc_truth(s.extra_info)){nb::object formatted=vc_format(s.extra_info);vc_assign(s.extra_info,vc_build_string<2>({vc_literal(literals,0),std::move(formatted)}));}else vc_assign(s.extra_info,vc_literal(literals,1));
    vc_assign(s.cancel_btn,vc_call1(ns.get("QPushButton"),vc_literal(literals,2)));vc_assign(s.close_btn,vc_call1(ns.get("QPushButton"),vc_literal(literals,3)));vc_call1(vc_attr(s.close_btn,"setObjectName"),vc_literal(literals,4));vc_assign(s.icon_label,vc_call0(ns.get("QWidget")));
    vc_store(self,"do_not_ask",vc_call1(ns.get("QCheckBox"),vc_literal(literals,5)));vc_call1(vc_attr(vc_attr(self,"do_not_ask"),"setVisible"),display_checkbox);vc_store(self,"_display_checkbox",display_checkbox);
    if(vc_truth(close_app)) {
        vc_call1(vc_attr(self,"setWindowTitle"),vc_literal(literals,6));nb::object key=vc_format(vc_shortcut(ns,vc_literal(literals,8))),extra=vc_format(s.extra_info);vc_assign(s.text,vc_build_string<4>({vc_literal(literals,7),std::move(key),vc_literal(literals,9),std::move(extra)}));
        vc_call1(vc_attr(s.close_btn,"setObjectName"),vc_literal(literals,10));nb::object setter=vc_attr(s.close_btn,"setShortcut"),shortcut=vc_call1(ns.get("QKeySequence"),vc_literal(literals,8));vc_call1(std::move(setter),std::move(shortcut));vc_call1(vc_attr(s.icon_label,"setObjectName"),vc_literal(literals,11));
    } else {
        vc_call1(vc_attr(self,"setWindowTitle"),vc_literal(literals,12));nb::object key=vc_format(vc_shortcut(ns,vc_literal(literals,14))),extra=vc_format(s.extra_info);vc_assign(s.text,vc_build_string<4>({vc_literal(literals,13),std::move(key),vc_literal(literals,15),std::move(extra)}));
        vc_call1(vc_attr(s.close_btn,"setObjectName"),vc_literal(literals,4));nb::object setter=vc_attr(s.close_btn,"setShortcut"),shortcut=vc_call1(ns.get("QKeySequence"),vc_literal(literals,14));vc_call1(std::move(setter),std::move(shortcut));vc_call1(vc_attr(s.icon_label,"setObjectName"),vc_literal(literals,16));
    }
    nb::object connect=vc_attr(vc_attr(s.cancel_btn,"clicked"),"connect"),callback=vc_attr(self,"reject");vc_call1(std::move(connect),std::move(callback));connect=vc_attr(vc_attr(s.close_btn,"clicked"),"connect");callback=vc_attr(self,"accept");vc_call1(std::move(connect),std::move(callback));
    vc_assign(s.layout,vc_call0(ns.get("QVBoxLayout")));vc_assign(s.layout2,vc_call0(ns.get("QHBoxLayout")));vc_call1(vc_attr(s.layout2,"addWidget"),s.icon_label);vc_assign(s.layout3,vc_call0(ns.get("QVBoxLayout")));vc_assign(s.text_label,vc_call1(ns.get("QLabel"),s.text));
    nb::object setter=vc_attr(s.text_label,"setTextInteractionFlags"),mouse=vc_attr(vc_attr(ns.get("Qt"),"TextInteractionFlag"),"TextSelectableByMouse"),keyboard=vc_attr(vc_attr(ns.get("Qt"),"TextInteractionFlag"),"TextSelectableByKeyboard");nb::object flags=vc_check(PyNumber_Or(mouse.ptr(),keyboard.ptr()));mouse.reset();keyboard.reset();vc_call1(std::move(setter),std::move(flags));
    vc_call1(vc_attr(s.layout3,"addWidget"),s.text_label);nb::object add=vc_attr(s.layout3,"addWidget"),check=vc_attr(self,"do_not_ask");vc_call1(std::move(add),std::move(check));vc_call1(vc_attr(s.layout2,"addLayout"),s.layout3);vc_assign(s.layout4,vc_call0(ns.get("QHBoxLayout")));vc_call1(vc_attr(s.layout4,"addStretch"),nb::int_(1));vc_call1(vc_attr(s.layout4,"addWidget"),s.cancel_btn);vc_call1(vc_attr(s.layout4,"addWidget"),s.close_btn);vc_call1(vc_attr(s.layout,"addLayout"),s.layout2);vc_call1(vc_attr(s.layout,"addLayout"),s.layout4);vc_call1(vc_attr(self,"setLayout"),s.layout);vc_store(self,"close_btn",s.close_btn);vc_store(self,"cancel_btn",s.cancel_btn);
}
void accept_confirm_dialog(nb::object self,nb::dict namespace_,nb::object super_provider) {CloseNamespace ns(std::move(namespace_));if(vc_truth(vc_attr(self,"_display_checkbox"))&&vc_truth(vc_call0(vc_attr(vc_attr(self,"do_not_ask"),"isChecked"))))vc_store(vc_attr(vc_call0(ns.get("get_settings")),"application"),"confirm_close_window",nb::bool_(false));vc_call0(vc_super(std::move(super_provider),self,"accept"));}
} // namespace

void bind_viewer_close(nb::module_& m) {
    auto owner=nb::class_<ViewerCloseLocals>(m,"ViewerCloseLocals",nb::type_slots(vc_locals_slots),nb::is_weak_referenceable());
#define VC_PROPERTY(n) owner.def_prop_ro(#n,[](const ViewerCloseLocals& s){return vc_optional(s.n);});
    CLOSE_FIELDS(VC_PROPERTY)
#undef VC_PROPERTY
    m.def("viewer_close_locals",[](nb::object operation,nb::object extra_info){ViewerCloseLocals s;s.operation=nb::cast<std::string>(operation);if(s.operation=="confirm_init")s.extra_info=std::move(extra_info);return s;},nb::arg("operation"),nb::arg("extra_info").none());
    m.def("viewer_close_viewer",&close_viewer,nb::arg("self").none(),nb::arg("namespace"));
    m.def("viewer_close_all",&close_all_viewers,nb::arg("cls").none(),nb::arg("namespace"),nb::arg("locals_owner"));
    m.def("viewer_close_main_window",&close_main_window,nb::arg("self").none(),nb::arg("quit_app").none(),nb::arg("confirm_need").none(),nb::arg("namespace"),nb::arg("super_provider").none(),nb::arg("locals_owner"));
    m.def("viewer_close_active_window",&close_active_window,nb::arg("self").none(),nb::arg("namespace"),nb::arg("parent_name").none(),nb::arg("locals_owner"));
    m.def("viewer_close_main_event",&close_main_event,nb::arg("self").none(),nb::arg("event").none(),nb::arg("namespace"),nb::arg("join_separator").none(),nb::arg("draw_pause").none(),nb::arg("locals_owner"));
    m.def("viewer_close_stop_main_workers",&stop_main_workers,nb::arg("self").none(),nb::arg("namespace"));
    m.def("viewer_close_setup_themes",&setup_themes,nb::arg("self").none(),nb::arg("connect").none(),nb::arg("namespace"),nb::arg("locals_owner"));
    m.def("viewer_close_connect_theme",[](nb::object self,nb::object theme,nb::dict){theme_connections(std::move(self),std::move(theme),true);},nb::arg("self").none(),nb::arg("theme").none(),nb::arg("namespace"));
    m.def("viewer_close_disconnect_theme",[](nb::object self,nb::object theme,nb::dict){theme_connections(std::move(self),std::move(theme),false);},nb::arg("self").none(),nb::arg("theme").none(),nb::arg("namespace"));
    m.def("viewer_close_update_canvas_theme",&update_canvas_theme,nb::arg("self").none(),nb::arg("namespace"));
    m.def("viewer_close_update_theme_no_event",&update_theme_no_event,nb::arg("self").none(),nb::arg("namespace"));
    m.def("viewer_close_add_theme",&theme_added,nb::arg("self").none(),nb::arg("event").none(),nb::arg("namespace"),nb::arg("locals_owner"));
    m.def("viewer_close_remove_theme",&theme_removed,nb::arg("self").none(),nb::arg("event").none(),nb::arg("namespace"),nb::arg("locals_owner"));
    m.def("viewer_close_teardown",&teardown_window,nb::arg("self").none(),nb::arg("namespace"));
    m.def("viewer_close_window",&close_window,nb::arg("self").none(),nb::arg("namespace"),nb::arg("window_name").none());
    m.def("viewer_close_get_window_settings",&get_window_settings,nb::arg("self").none(),nb::arg("namespace"),nb::arg("locals_owner"));
    m.def("viewer_close_save_window_settings",&save_window_settings,nb::arg("self").none(),nb::arg("namespace"),nb::arg("locals_owner"));
    m.def("viewer_close_qbytearray_to_str",&bytearray_to_str,nb::arg("qbyte").none(),nb::arg("namespace"));
    m.def("viewer_close_qt_viewer",&close_qt_viewer,nb::arg("self").none(),nb::arg("event").none(),nb::arg("namespace"));
    m.def("viewer_close_stop_dims",&stop_dims,nb::arg("self").none(),nb::arg("namespace"));
    m.def("viewer_close_dims",&close_dims,nb::arg("self").none(),nb::arg("event").none(),nb::arg("namespace"),nb::arg("locals_owner"));
    m.def("viewer_close_stop_animation",&stop_animation,nb::arg("self").none(),nb::arg("namespace"));
    m.def("viewer_close_status",&close_status,nb::arg("self").none(),nb::arg("namespace"));
    m.def("viewer_close_terminate_status",&terminate_status,nb::arg("self").none(),nb::arg("namespace"));
    m.def("viewer_close_start_status",&start_status,nb::arg("self").none(),nb::arg("priority").none(),nb::arg("namespace"),nb::arg("super_provider").none());
    m.def("viewer_close_disconnect_canvas",&disconnect_canvas,nb::arg("self").none(),nb::arg("namespace"));
    m.def("viewer_close_delete_canvas",&delete_canvas,nb::arg("self").none(),nb::arg("namespace"));
    m.def("viewer_close_task_timestamp",&task_timestamp,nb::arg("self").none(),nb::arg("namespace"));
    m.def("viewer_close_task_update",&task_update,nb::arg("self").none(),nb::arg("status").none(),nb::arg("description").none(),nb::arg("namespace"));
    m.def("viewer_close_task_cancel",&task_cancel,nb::arg("self").none(),nb::arg("namespace"),nb::arg("empty_description").none());
    m.def("viewer_close_task_state",&task_state,nb::arg("self").none(),nb::arg("namespace"));
    m.def("viewer_close_tasks_busy",&tasks_busy,nb::arg("self").none(),nb::arg("namespace"),nb::arg("locals_owner"));
    m.def("viewer_close_tasks_status",&tasks_status,nb::arg("self").none(),nb::arg("namespace"),nb::arg("locals_owner"));
    m.def("viewer_close_tasks_cancel",&tasks_cancel,nb::arg("self").none(),nb::arg("namespace"),nb::arg("locals_owner"));
    m.def("viewer_close_ipython_eventloop",&ipython_eventloop,nb::arg("namespace"),nb::arg("module_name").none(),nb::arg("qt_name").none(),nb::arg("locals_owner"));
    m.def("viewer_close_quit_app",&quit_application,nb::arg("namespace"),nb::arg("app_name").none(),nb::arg("import_name").none(),nb::arg("import_fromlist").none(),nb::arg("locals_owner"));
    m.def("viewer_close_confirm_init",&init_confirm_dialog,nb::arg("self").none(),nb::arg("parent").none(),nb::arg("close_app").none(),nb::arg("display_checkbox").none(),nb::arg("namespace"),nb::arg("super_provider").none(),nb::arg("literals"),nb::arg("locals_owner"));
    m.def("viewer_close_confirm_accept",&accept_confirm_dialog,nb::arg("self").none(),nb::arg("namespace"),nb::arg("super_provider").none());
}
#undef CLOSE_FIELDS
} // namespace nx
