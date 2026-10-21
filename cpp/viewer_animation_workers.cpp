#include "arrays.hpp"
#include "collection_protocol.hpp"
#include <nanobind/stl/string.h>
#include <array>
#include <utility>

namespace nx {
namespace {
nb::object aw_check(PyObject* value) { if (!value) throw nb::python_error(); return nb::steal<nb::object>(value); }
nb::object aw_attr(nb::object value, const char* name) { return aw_check(PyObject_GetAttrString(value.ptr(), name)); }
nb::object aw_call0(nb::object function) { return aw_check(PyObject_CallNoArgs(function.ptr())); }
nb::object aw_call1(nb::object function, nb::object value) { return aw_check(PyObject_CallOneArg(function.ptr(), value.ptr())); }
nb::object aw_call2(nb::object function, nb::object first, nb::object second) { return aw_check(PyObject_CallFunctionObjArgs(function.ptr(), first.ptr(), second.ptr(), nullptr)); }
nb::object aw_call3(nb::object function, nb::object first, nb::object second, nb::object third) { return aw_check(PyObject_CallFunctionObjArgs(function.ptr(), first.ptr(), second.ptr(), third.ptr(), nullptr)); }
void aw_store(nb::object self, const char* name, nb::object value) {
    if (PyObject_SetAttrString(self.ptr(), name, value.ptr()) < 0) throw nb::python_error();
    self.reset(); value.reset();
}
void aw_assign(nb::object& target, nb::object value) { nb::object previous = std::move(target); target = std::move(value); previous.reset(); }
nb::object aw_optional(const nb::object& value) { return value.is_valid() ? value : nb::none(); }
bool aw_truth(nb::object value) { int result = PyObject_IsTrue(value.ptr()); if (result < 0) throw nb::python_error(); return result != 0; }
nb::object aw_compare(nb::object first, nb::object second, int op) { return aw_check(PyObject_RichCompare(first.ptr(), second.ptr(), op)); }
nb::object aw_item(nb::object self, nb::object index) { return aw_check(PyObject_GetItem(self.ptr(), index.ptr())); }
nb::object aw_add(nb::object a, nb::object b) { return aw_check(PyNumber_Add(a.ptr(), b.ptr())); }
nb::object aw_subtract(nb::object a, nb::object b) { return aw_check(PyNumber_Subtract(a.ptr(), b.ptr())); }
nb::object aw_multiply(nb::object a, nb::object b) { return aw_check(PyNumber_Multiply(a.ptr(), b.ptr())); }
nb::object aw_divide(nb::object a, nb::object b) { return aw_check(PyNumber_TrueDivide(a.ptr(), b.ptr())); }
bool aw_contains(nb::object value, nb::object container) { int result = PySequence_Contains(container.ptr(), value.ptr()); if (result < 0) throw nb::python_error(); return result != 0; }
struct AnimationNamespace {
    nb::dict globals; nb::object builtins;
    explicit AnimationNamespace(nb::dict value) : globals(std::move(value)), builtins(nb::borrow<nb::object>(PyEval_GetBuiltins())) {}
    nb::object get(const char* name) const {
        nb::object key = nb::str(name); PyObject* value = PyDict_GetItemWithError(globals.ptr(), key.ptr());
        if (value) return nb::borrow<nb::object>(value);
        if (PyErr_Occurred()) throw nb::python_error();
        value = PyObject_GetItem(builtins.ptr(), key.ptr()); if (value) return nb::steal<nb::object>(value);
        if (!PyErr_ExceptionMatches(PyExc_KeyError)) throw nb::python_error(); PyErr_Clear();
        nb::object message = aw_check(PyUnicode_FromFormat("name '%s' is not defined", name));
        nb::object error = aw_call1(nb::borrow<nb::object>(PyExc_NameError), std::move(message)); aw_store(error, "name", key);
        PyErr_SetObject(PyExc_NameError, error.ptr()); throw nb::python_error();
    }
};
void aw_except_type(nb::handle value) {
    bool valid = PyExceptionClass_Check(value.ptr());
    if (PyTuple_Check(value.ptr())) { valid = true; for (Py_ssize_t i=0;i<PyTuple_GET_SIZE(value.ptr());++i) if (!PyExceptionClass_Check(PyTuple_GET_ITEM(value.ptr(),i))) { valid=false;break; } }
    if (!valid) { PyErr_SetString(PyExc_TypeError,"catching classes that do not inherit from BaseException is not allowed"); throw nb::python_error(); }
}
void aw_raise(nb::object kind, nb::object message) {
    nb::object error=aw_call1(std::move(kind),std::move(message));
    if (PyExceptionClass_Check(error.ptr())) {
        nb::object value=aw_call0(error);
        if(!PyExceptionInstance_Check(value.ptr())) {
            PyErr_Format(PyExc_TypeError,"calling %R should have returned an instance of BaseException, not %R",error.ptr(),reinterpret_cast<PyObject*>(Py_TYPE(value.ptr())));
            throw nb::python_error();
        }
        error=std::move(value);
    } else if (!PyExceptionInstance_Check(error.ptr())) {PyErr_SetString(PyExc_TypeError,"exceptions must derive from BaseException");throw nb::python_error();}
    PyErr_SetObject(reinterpret_cast<PyObject*>(Py_TYPE(error.ptr())),error.ptr());throw nb::python_error();
}
nb::object aw_super(nb::object provider, nb::object self, const char* method) { return aw_attr(aw_call1(std::move(provider),std::move(self)),method); }
template<typename Body> void aw_with(nb::object context, Body body) {
    nb::object enter=collections::context_method(context,"__enter__",false);
    nb::object exit=collections::context_method(context,"__exit__",true);
    context.reset(); aw_call0(std::move(enter));
    try { body(); }
    catch (const nb::python_error& original) {
        bool suppressed=false;
        {
            events::HandledException handled(original.value());
            nb::object traceback=original.traceback();if(!traceback.is_valid())traceback=nb::none();
            try {
                nb::object result=aw_call3(exit,nb::borrow<nb::object>(original.type()),nb::borrow<nb::object>(original.value()),std::move(traceback));
                suppressed=aw_truth(std::move(result));
            } catch(const nb::python_error& cleanup) {
                if(cleanup.value().ptr()!=original.value().ptr())PyException_SetContext(cleanup.value().ptr(),original.value().inc_ref().ptr());
                throw;
            }
            if (!suppressed) throw;
        }
        // Source POP_EXCEPT precedes saved-exit discard on suppression.
        exit.reset(); return;
    }
    aw_call3(std::move(exit),nb::none(),nb::none(),nb::none());
}

struct ViewerAnimationLocals {
    std::string operation;
    nb::object prev_slider,_modes,loop_mode,before,e,value,prev,frame_range;
    ViewerAnimationLocals()=default;ViewerAnimationLocals(ViewerAnimationLocals&&)=default;
    ~ViewerAnimationLocals(){clear();}
    void clear(){
        std::array<nb::object,8> values={std::move(prev_slider),std::move(_modes),std::move(loop_mode),std::move(before),std::move(e),std::move(value),std::move(prev),std::move(frame_range)};
        if(operation=="dims_play"){values[2].reset();values[1].reset();}
        for(auto& v:values)v.reset();
    }
};
int aw_traverse(PyObject* self,visitproc visit,void* arg){Py_VISIT(Py_TYPE(self));if(nb::inst_ready(self)){auto& s=*nb::inst_ptr<ViewerAnimationLocals>(self);Py_VISIT(s.prev_slider.ptr());Py_VISIT(s._modes.ptr());Py_VISIT(s.loop_mode.ptr());Py_VISIT(s.before.ptr());Py_VISIT(s.e.ptr());Py_VISIT(s.value.ptr());Py_VISIT(s.prev.ptr());Py_VISIT(s.frame_range.ptr());}return 0;}
int aw_clear(PyObject* self){if(nb::inst_ready(self))nb::inst_ptr<ViewerAnimationLocals>(self)->clear();return 0;}
PyType_Slot aw_slots[]={{Py_tp_traverse,reinterpret_cast<void*>(aw_traverse)},{Py_tp_clear,reinterpret_cast<void*>(aw_clear)},{0,nullptr}};

void animation_init(nb::object self,nb::object parent,nb::dict namespace_,nb::object super_provider,nb::object empty_slider_factory,nb::object frame_range,nb::object dims_range){
    AnimationNamespace ns(std::move(namespace_));nb::object init=aw_super(std::move(super_provider),self,"__init__");
    nb::dict kwargs;kwargs["parent"]=parent;aw_check(PyObject_Call(init.ptr(),nb::tuple().ptr(),kwargs.ptr()));init.reset();kwargs.reset();
    aw_store(self,"_interval",nb::int_(1));
    nb::object empty_slider=aw_call0(std::move(empty_slider_factory));
    if(PyFunction_Check(empty_slider.ptr())) {
        aw_store(empty_slider,"__name__",nb::str("<lambda>"));
        aw_store(empty_slider,"__qualname__",nb::str("AnimationThread.__init__.<locals>.<lambda>"));
    }
    aw_store(self,"_slider",std::move(empty_slider));
    nb::object factory=aw_attr(ns.get("threading"),"Event");aw_store(self,"_waiter",aw_call0(std::move(factory)));
    aw_store(self,"frame_range",std::move(frame_range));aw_store(self,"dims_range",std::move(dims_range));
    aw_store(self,"min_point",nb::int_(0));aw_store(self,"max_point",nb::int_(1));aw_store(self,"current",nb::int_(0));aw_store(self,"step",nb::int_(1));
}
void animation_run(nb::object self,nb::dict){aw_call0(aw_attr(self,"work"));}
nb::object animation_slider(nb::object self,nb::dict){return aw_call0(aw_attr(self,"_slider"));}
void animation_set_slider(nb::object self,nb::object slider,nb::dict namespace_,ViewerAnimationLocals& s){
    AnimationNamespace ns(std::move(namespace_));aw_assign(s.prev_slider,aw_attr(self,"slider"));
    nb::object factory=ns.get("ref");aw_store(self,"_slider",aw_call1(std::move(factory),slider));
    nb::object callback=aw_attr(self,"set_fps"),arg=aw_attr(slider,"fps");aw_call1(std::move(callback),std::move(arg));
    callback=aw_attr(self,"set_frame_range");arg=aw_attr(slider,"frame_range");aw_call1(std::move(callback),std::move(arg));
    if(!s.prev_slider.is_none()){
        callback=aw_attr(aw_attr(s.prev_slider,"fps_changed"),"disconnect");arg=aw_attr(self,"set_fps");aw_call1(std::move(callback),std::move(arg));
        callback=aw_attr(aw_attr(s.prev_slider,"range_changed"),"disconnect");arg=aw_attr(self,"set_frame_range");aw_call1(std::move(callback),std::move(arg));
        callback=aw_attr(aw_attr(aw_attr(aw_attr(s.prev_slider,"dims"),"events"),"current_step"),"disconnect");arg=aw_attr(self,"_on_axis_changed");aw_call1(std::move(callback),std::move(arg));
        callback=aw_attr(aw_attr(self,"finished"),"disconnect");arg=aw_attr(aw_attr(s.prev_slider,"play_button"),"_handle_stop");aw_call1(std::move(callback),std::move(arg));
        callback=aw_attr(aw_attr(self,"started"),"disconnect");arg=aw_attr(aw_attr(s.prev_slider,"play_button"),"_handle_start");aw_call1(std::move(callback),std::move(arg));
    }
    callback=aw_attr(aw_attr(slider,"fps_changed"),"connect");arg=aw_attr(self,"set_fps");aw_call1(std::move(callback),std::move(arg));
    callback=aw_attr(aw_attr(slider,"range_changed"),"connect");arg=aw_attr(self,"set_frame_range");aw_call1(std::move(callback),std::move(arg));
    callback=aw_attr(aw_attr(aw_attr(aw_attr(slider,"dims"),"events"),"current_step"),"connect");arg=aw_attr(self,"_on_axis_changed");aw_call1(std::move(callback),std::move(arg));
    callback=aw_attr(aw_attr(self,"finished"),"connect");arg=aw_attr(aw_attr(slider,"play_button"),"_handle_stop");aw_call1(std::move(callback),std::move(arg));
    callback=aw_attr(aw_attr(self,"started"),"connect");arg=aw_attr(aw_attr(slider,"play_button"),"_handle_start");aw_call1(std::move(callback),std::move(arg));
    callback=ns.get("max");nb::object steps=aw_attr(aw_attr(slider,"dims"),"current_step"),axis=aw_attr(slider,"axis");arg=aw_item(std::move(steps),std::move(axis));
    nb::object minimum=aw_attr(self,"min_point");aw_store(self,"current",aw_call2(std::move(callback),std::move(arg),std::move(minimum)));
    callback=ns.get("min");arg=aw_attr(self,"current");nb::object maximum=aw_attr(self,"max_point");aw_store(self,"current",aw_call2(std::move(callback),std::move(arg),std::move(maximum)));
}
nb::object animation_interval(nb::object self,nb::dict){return aw_attr(self,"_interval");}
void animation_set_interval(nb::object self,nb::object value,nb::dict){aw_store(self,"_interval",std::move(value));}
void animation_work(nb::object self,nb::dict namespace_){
    AnimationNamespace ns(std::move(namespace_));nb::object mode=aw_attr(self,"loop_mode"),once=aw_attr(ns.get("LoopMode"),"ONCE");
    if(aw_truth(aw_compare(std::move(mode),std::move(once),Py_EQ))){
        bool forward=aw_truth(aw_compare(aw_attr(self,"step"),nb::int_(0),Py_GT));bool rewind=false;
        if(forward){nb::object current=aw_attr(self,"current"),maximum=aw_attr(self,"max_point");nb::object bound=aw_subtract(std::move(maximum),nb::int_(1));forward=aw_truth(aw_compare(std::move(current),std::move(bound),Py_GE));}
        if(forward){nb::object emit=aw_attr(aw_attr(self,"frame_requested"),"emit"),axis=aw_attr(self,"axis"),minimum=aw_attr(self,"min_point");aw_call2(std::move(emit),std::move(axis),std::move(minimum));}
        else{
            rewind=aw_truth(aw_compare(aw_attr(self,"step"),nb::int_(0),Py_LT));
            if(rewind){nb::object current=aw_attr(self,"current"),minimum=aw_attr(self,"min_point");nb::object bound=aw_add(std::move(minimum),nb::int_(1));rewind=aw_truth(aw_compare(std::move(current),std::move(bound),Py_LE));}
            if(rewind){nb::object emit=aw_attr(aw_attr(self,"frame_requested"),"emit"),axis=aw_attr(self,"axis"),maximum=aw_attr(self,"max_point");aw_call2(std::move(emit),std::move(axis),std::move(maximum));}
        }
    }else aw_call0(aw_attr(self,"advance"));
    aw_call0(aw_attr(aw_attr(self,"_waiter"),"clear"));
    nb::object wait=aw_attr(aw_attr(self,"_waiter"),"wait"),timeout=aw_divide(aw_attr(self,"interval"),nb::int_(1000));aw_call1(std::move(wait),std::move(timeout));
    while(!aw_truth(aw_call0(aw_attr(aw_attr(self,"_waiter"),"is_set")))){
        aw_call0(aw_attr(self,"advance"));wait=aw_attr(aw_attr(self,"_waiter"),"wait");timeout=aw_divide(aw_attr(self,"interval"),nb::int_(1000));aw_call1(std::move(wait),std::move(timeout));
    }
}
nb::object animation_set_fps(nb::object self,nb::object fps,nb::dict namespace_){
    AnimationNamespace ns(std::move(namespace_));if(aw_truth(aw_compare(fps,nb::int_(0),Py_EQ)))return aw_call0(aw_attr(self,"finish"));
    bool positive=aw_truth(aw_compare(fps,nb::int_(0),Py_GT));aw_store(self,"step",nb::int_(positive?1:-1));
    nb::object absolute=ns.get("abs");nb::object denominator=aw_call1(std::move(absolute),fps);aw_store(self,"interval",aw_divide(nb::int_(1000),std::move(denominator)));return nb::none();
}
void animation_set_range(nb::object self,nb::dict namespace_,nb::object zero_range,nb::tuple messages,ViewerAnimationLocals& s){
    AnimationNamespace ns(std::move(namespace_));nb::object steps=aw_attr(aw_attr(self,"dims"),"nsteps"),axis=aw_attr(self,"axis");
    nb::object length=aw_item(std::move(steps),std::move(axis));aw_store(self,"dims_range",nb::make_tuple(nb::int_(0),std::move(length),nb::int_(1)));
    if(s.frame_range.is_none())aw_assign(s.frame_range,zero_range);
    if(aw_truth(aw_compare(s.frame_range,zero_range,Py_NE))){
        nb::object first=aw_item(s.frame_range,nb::int_(0)),second=aw_item(s.frame_range,nb::int_(1));
        if(aw_truth(aw_compare(std::move(first),std::move(second),Py_GE)))aw_raise(ns.get("ValueError"),nb::borrow<nb::object>(messages[0]));
        first=aw_item(s.frame_range,nb::int_(0));second=aw_item(aw_attr(self,"dims_range"),nb::int_(0));
        if(aw_truth(aw_compare(std::move(first),std::move(second),Py_LT)))aw_raise(ns.get("IndexError"),nb::borrow<nb::object>(messages[1]));
        first=aw_item(s.frame_range,nb::int_(1));second=aw_item(aw_attr(self,"dims_range"),nb::int_(2));nb::object product=aw_multiply(std::move(first),std::move(second));
        second=aw_item(aw_attr(self,"dims_range"),nb::int_(1));if(aw_truth(aw_compare(std::move(product),std::move(second),Py_GE)))aw_raise(ns.get("IndexError"),nb::borrow<nb::object>(messages[2]));
    }
    aw_store(self,"frame_range",s.frame_range);
    if(aw_truth(aw_compare(aw_attr(self,"frame_range"),zero_range,Py_NE))){
        nb::object supplied=aw_attr(self,"frame_range");nb::tuple pair=events::pair(supplied);supplied.reset();
        nb::object minimum=nb::borrow<nb::object>(pair[0]),maximum=nb::borrow<nb::object>(pair[1]);pair.reset();
        aw_store(self,"min_point",std::move(minimum));aw_store(self,"max_point",std::move(maximum));
    }else{
        aw_store(self,"min_point",nb::int_(0));nb::object integer=ns.get("int"),floor=aw_attr(ns.get("np"),"floor");
        nb::object first=aw_item(aw_attr(self,"dims_range"),nb::int_(1)),second=aw_item(aw_attr(self,"dims_range"),nb::int_(2));
        nb::object value=aw_call1(std::move(floor),aw_subtract(std::move(first),std::move(second)));aw_store(self,"max_point",aw_call1(std::move(integer),std::move(value)));
    }
    nb::object previous=aw_attr(self,"max_point"),one=nb::int_(1);nb::object result=aw_check(PyNumber_InPlaceAdd(previous.ptr(),one.ptr()));previous.reset();one.reset();aw_store(self,"max_point",std::move(result));
}
nb::object animation_advance(nb::object self,nb::dict namespace_){
    AnimationNamespace ns(std::move(namespace_));nb::object current=aw_attr(self,"current"),step=aw_attr(self,"step"),delta=aw_item(aw_attr(self,"dims_range"),nb::int_(2));
    nb::object product=aw_multiply(std::move(step),std::move(delta));nb::object result=aw_check(PyNumber_InPlaceAdd(current.ptr(),product.ptr()));current.reset();product.reset();aw_store(self,"current",std::move(result));
    current=aw_attr(self,"current");nb::object minimum=aw_attr(self,"min_point");bool below=aw_truth(aw_compare(std::move(current),std::move(minimum),Py_LT));
    if(below){
        nb::object mode=aw_attr(self,"loop_mode"),back=aw_attr(ns.get("LoopMode"),"BACK_AND_FORTH");
        if(aw_truth(aw_compare(std::move(mode),std::move(back),Py_EQ))){
            step=aw_attr(self,"step");nb::object negative=nb::int_(-1);result=aw_check(PyNumber_InPlaceMultiply(step.ptr(),negative.ptr()));step.reset();negative.reset();aw_store(self,"step",std::move(result));
            minimum=aw_attr(self,"min_point");step=aw_attr(self,"step");delta=aw_item(aw_attr(self,"dims_range"),nb::int_(2));product=aw_multiply(std::move(step),std::move(delta));aw_store(self,"current",aw_add(std::move(minimum),std::move(product)));
        }else{
            mode=aw_attr(self,"loop_mode");nb::object loop=aw_attr(ns.get("LoopMode"),"LOOP");
            if(aw_truth(aw_compare(std::move(mode),std::move(loop),Py_EQ))){
                nb::object maximum=aw_attr(self,"max_point");current=aw_attr(self,"current");result=aw_add(std::move(maximum),std::move(current));minimum=aw_attr(self,"min_point");aw_store(self,"current",aw_subtract(std::move(result),std::move(minimum)));
            }else return aw_call0(aw_attr(self,"finish"));
        }
    }else{
        current=aw_attr(self,"current");nb::object maximum=aw_attr(self,"max_point");
        if(aw_truth(aw_compare(std::move(current),std::move(maximum),Py_GE))){
            nb::object mode=aw_attr(self,"loop_mode"),back=aw_attr(ns.get("LoopMode"),"BACK_AND_FORTH");
            if(aw_truth(aw_compare(std::move(mode),std::move(back),Py_EQ))){
                step=aw_attr(self,"step");nb::object negative=nb::int_(-1);result=aw_check(PyNumber_InPlaceMultiply(step.ptr(),negative.ptr()));step.reset();negative.reset();aw_store(self,"step",std::move(result));
                maximum=aw_attr(self,"max_point");step=aw_attr(self,"step");result=aw_multiply(nb::int_(2),std::move(step));delta=aw_item(aw_attr(self,"dims_range"),nb::int_(2));product=aw_multiply(std::move(result),std::move(delta));aw_store(self,"current",aw_add(std::move(maximum),std::move(product)));
            }else{
                mode=aw_attr(self,"loop_mode");nb::object loop=aw_attr(ns.get("LoopMode"),"LOOP");
                if(aw_truth(aw_compare(std::move(mode),std::move(loop),Py_EQ))){
                    minimum=aw_attr(self,"min_point");current=aw_attr(self,"current");result=aw_add(std::move(minimum),std::move(current));maximum=aw_attr(self,"max_point");aw_store(self,"current",aw_subtract(std::move(result),std::move(maximum)));
                }else return aw_call0(aw_attr(self,"finish"));
            }
        }
    }
    nb::object blocker=aw_attr(aw_attr(aw_attr(aw_attr(self,"dims"),"events"),"current_step"),"blocker"),callback=aw_attr(self,"_on_axis_changed");
    nb::object context=aw_call1(std::move(blocker),std::move(callback));
    aw_with(std::move(context),[&]{nb::object emit=aw_attr(aw_attr(self,"frame_requested"),"emit"),axis=aw_attr(self,"axis"),point=aw_attr(self,"current");aw_call2(std::move(emit),std::move(axis),std::move(point));});return nb::none();
}
nb::object animation_get_optional(nb::object self,nb::dict namespace_,nb::object name){AnimationNamespace ns(std::move(namespace_));nb::object get=ns.get("getattr"),slider=aw_attr(self,"slider");return aw_call3(std::move(get),std::move(slider),std::move(name),nb::none());}
void animation_finish(nb::object self,nb::dict){aw_call0(aw_attr(self,"_stop"));}
void animation_axis_changed(nb::object self,nb::dict){
    if(!aw_attr(self,"dims").is_none()){nb::object steps=aw_attr(aw_attr(self,"dims"),"current_step"),axis=aw_attr(self,"axis");aw_store(self,"current",aw_item(std::move(steps),std::move(axis)));}
}
void dims_play(nb::object self,nb::object axis,nb::object fps,nb::object frame_range,nb::dict namespace_,nb::tuple literals,ViewerAnimationLocals& s){
    AnimationNamespace ns(std::move(namespace_));
    if(!s.loop_mode.is_none()){
        aw_assign(s._modes,aw_call0(aw_attr(ns.get("LoopMode"),"keys")));
        if(!aw_contains(s.loop_mode,s._modes)){
            nb::object kind=ns.get("ValueError"),first=aw_check(PyObject_Format(s._modes.ptr(),nullptr));
            nb::object second=aw_check(PyObject_Format(s.loop_mode.ptr(),nullptr));nb::object pieces=nb::make_tuple(literals[0],first,literals[1],second);
            nb::object message=aw_check(PyUnicode_Join(nb::str("").ptr(),pieces.ptr()));pieces.reset();second.reset();first.reset();aw_raise(std::move(kind),std::move(message));
        }
        nb::object factory=ns.get("LoopMode");aw_assign(s.loop_mode,aw_call1(std::move(factory),s.loop_mode));
    }
    nb::object ndim=aw_attr(aw_attr(self,"dims"),"ndim");if(aw_truth(aw_compare(axis,std::move(ndim),Py_GE)))aw_raise(ns.get("IndexError"),nb::borrow<nb::object>(literals[2]));
    if(aw_truth(aw_attr(self,"is_playing"))){
        nb::object actual=aw_attr(aw_attr(self,"_animation_thread"),"axis");
        if(aw_truth(aw_compare(std::move(actual),axis,Py_EQ))){nb::object method=aw_attr(aw_item(aw_attr(self,"slider_widgets"),axis),"_update_play_settings");aw_call3(std::move(method),fps,s.loop_mode,frame_range);return;}
    }
    if(aw_truth(aw_item(aw_attr(self,"_displayed_sliders"),axis))){
        if(aw_truth(aw_call0(aw_attr(aw_attr(self,"_animation_thread"),"isRunning"))))aw_call0(aw_attr(aw_attr(aw_attr(self,"_animation_thread"),"slider"),"play_button").attr("_handle_stop"));
        nb::object method=aw_attr(aw_item(aw_attr(self,"slider_widgets"),axis),"_update_play_settings");aw_call3(std::move(method),fps,s.loop_mode,frame_range);
        method=aw_attr(aw_attr(self,"_animation_thread"),"set_slider");nb::object slider=aw_item(aw_attr(self,"slider_widgets"),axis);aw_call1(std::move(method),std::move(slider));
        method=aw_attr(aw_attr(aw_attr(self,"_animation_thread"),"frame_requested"),"connect");nb::object receiver=aw_attr(self,"_set_frame");aw_call1(std::move(method),std::move(receiver));
        if(!aw_truth(aw_call0(aw_attr(aw_attr(self,"_animation_thread"),"isRunning"))))aw_call0(aw_attr(aw_attr(self,"_animation_thread"),"start"));
        else aw_call0(aw_attr(aw_attr(aw_attr(self,"_animation_thread"),"slider"),"play_button").attr("_handle_start"));
    }else{nb::object warn=aw_attr(ns.get("warnings"),"warn");aw_call1(std::move(warn),nb::borrow<nb::object>(literals[3]));}
}
nb::object dims_is_playing(nb::object self,nb::dict namespace_,nb::tuple deleted_messages,ViewerAnimationLocals& s){
    AnimationNamespace ns(std::move(namespace_));
    try{return nb::bool_(!aw_truth(aw_call0(aw_attr(aw_attr(aw_attr(self,"_animation_thread"),"_waiter"),"is_set"))));}
    catch(const nb::python_error& error){
        {events::HandledException handled(error.value());nb::object kind=ns.get("RuntimeError");aw_except_type(kind);if(!PyErr_GivenExceptionMatches(error.value().ptr(),kind.ptr()))throw;kind.reset();aw_assign(s.e,nb::borrow<nb::object>(error.value()));
         try{nb::object first=aw_item(aw_attr(s.e,"args"),nb::int_(0));bool absent=!aw_contains(nb::borrow<nb::object>(deleted_messages[0]),std::move(first));if(absent){first=aw_item(aw_attr(s.e,"args"),nb::int_(0));absent=!aw_contains(nb::borrow<nb::object>(deleted_messages[1]),std::move(first));}if(absent)throw;}
         catch(...){s.e.reset();throw;}}
        s.e.reset();return nb::bool_(false);
    }
}
void dims_set_frame(nb::object self,nb::object axis,nb::object frame,nb::dict,ViewerAnimationLocals& s){
    if(aw_truth(aw_attr(aw_attr(self,"dims"),"_play_ready"))){
        aw_store(aw_attr(self,"dims"),"_play_ready",nb::bool_(false));aw_assign(s.before,aw_attr(aw_attr(self,"dims"),"point"));
        nb::object set=aw_attr(aw_attr(self,"dims"),"set_current_step");aw_call2(std::move(set),axis,frame);
        if(aw_truth(aw_compare(aw_attr(aw_attr(self,"dims"),"point"),s.before,Py_EQ)))aw_store(aw_attr(self,"dims"),"_play_ready",nb::bool_(true));
    }
}
nb::object slider_fps(nb::object self,nb::dict){return aw_attr(self,"_fps");}
void slider_set_fps(nb::object self,nb::object value,nb::dict namespace_){
    AnimationNamespace ns(std::move(namespace_));if(aw_truth(aw_compare(aw_attr(self,"_fps"),value,Py_EQ)))return;aw_store(self,"_fps",value);
    nb::object set=aw_attr(aw_attr(aw_attr(self,"play_button"),"fpsspin"),"setValue"),absolute=ns.get("abs");nb::object arg=aw_call1(std::move(absolute),value);aw_call1(std::move(set),std::move(arg));
    set=aw_attr(aw_attr(aw_attr(self,"play_button"),"reverse_check"),"setChecked");arg=aw_compare(value,nb::int_(0),Py_LT);aw_call1(std::move(set),std::move(arg));
    nb::object emit=aw_attr(aw_attr(self,"fps_changed"),"emit");aw_call1(std::move(emit),std::move(value));
}
nb::object slider_loop_mode(nb::object self,nb::dict){return aw_attr(self,"_loop_mode");}
void slider_set_loop_mode(nb::object self,nb::dict namespace_,nb::object underscore,nb::object space,ViewerAnimationLocals& s){
    AnimationNamespace ns(std::move(namespace_));nb::object factory=ns.get("LoopMode");aw_assign(s.value,aw_call1(std::move(factory),s.value));
    if(aw_truth(aw_compare(aw_attr(self,"_loop_mode"),s.value,Py_EQ)))return;aw_store(self,"_loop_mode",s.value);
    factory=ns.get("qt_signals_blocked");nb::object combo=aw_attr(aw_attr(self,"play_button"),"mode_combo");nb::object context=aw_call1(std::move(factory),std::move(combo));
    aw_with(std::move(context),[&]{
        nb::object set=aw_attr(aw_attr(aw_attr(self,"play_button"),"mode_combo"),"setCurrentText"),str=ns.get("str");
        nb::object replace=aw_attr(aw_call1(std::move(str),s.value),"replace");nb::object text=aw_call2(std::move(replace),underscore,space);aw_call1(std::move(set),std::move(text));
    });
    nb::object emit=aw_attr(aw_attr(self,"mode_changed"),"emit"),str=ns.get("str");aw_call1(std::move(emit),aw_call1(std::move(str),s.value));
}
nb::object slider_frame_range(nb::object self,nb::dict){nb::object first=aw_attr(self,"_minframe"),second=aw_attr(self,"_maxframe");return nb::make_tuple(std::move(first),std::move(second));}
void slider_set_frame_range(nb::object self,nb::dict namespace_,nb::object zero_range,nb::tuple messages,ViewerAnimationLocals& s){
    AnimationNamespace ns(std::move(namespace_));nb::object instance=ns.get("isinstance"),tuple=ns.get("tuple"),list=ns.get("list");nb::object valid=aw_check(PyNumber_Or(tuple.ptr(),list.ptr()));tuple.reset();list.reset();
    nb::object type=ns.get("type");nb::object none_type=aw_call1(std::move(type),nb::none());nb::object all=aw_check(PyNumber_Or(valid.ptr(),none_type.ptr()));valid.reset();none_type.reset();
    if(!aw_truth(aw_call2(std::move(instance),s.value,std::move(all))))aw_raise(ns.get("TypeError"),nb::borrow<nb::object>(messages[0]));
    if(aw_truth(s.value)){nb::object len=ns.get("len"),length=aw_call1(std::move(len),s.value);if(aw_truth(aw_compare(std::move(length),nb::int_(2),Py_NE)))aw_raise(ns.get("ValueError"),nb::borrow<nb::object>(messages[1]));}
    if(s.value.is_none())aw_assign(s.value,zero_range);
    nb::object first=aw_attr(self,"_minframe"),second=aw_attr(self,"_maxframe");aw_assign(s.prev,nb::make_tuple(std::move(first),std::move(second)));
    nb::tuple pair=events::pair(s.value);first=nb::borrow<nb::object>(pair[0]);second=nb::borrow<nb::object>(pair[1]);pair.reset();aw_store(self,"_minframe",std::move(first));aw_store(self,"_maxframe",std::move(second));
    if(aw_truth(aw_compare(s.prev,s.value,Py_NE))){nb::object emit=aw_attr(aw_attr(self,"range_changed"),"emit"),tuplefn=ns.get("tuple");aw_call1(std::move(emit),aw_call1(std::move(tuplefn),s.value));}
}
void slider_update_settings(nb::object self,nb::object fps,nb::object loop_mode,nb::object frame_range,nb::dict,nb::object zero_range){
    if(!fps.is_none())aw_store(self,"fps",fps);if(!loop_mode.is_none())aw_store(self,"loop_mode",loop_mode);
    if(aw_truth(aw_compare(frame_range,std::move(zero_range),Py_NE)))aw_store(self,"frame_range",std::move(frame_range));
}
void button_style(nb::object self,nb::dict,nb::object property_name,nb::object playing){aw_call2(aw_attr(self,"setProperty"),std::move(property_name),std::move(playing));aw_call1(aw_attr(aw_call0(aw_attr(self,"style")),"unpolish"),self);aw_call1(aw_attr(aw_call0(aw_attr(self,"style")),"polish"),self);}
void enable_play(nb::object self,nb::object,nb::dict){aw_store(aw_attr(aw_attr(self,"viewer"),"dims"),"_play_ready",nb::bool_(true));}

// Native finally policy; the facade remains a genuine contextmanager-decorated
// Python generator and retains this owner local through a saved traceback.
struct ViewerSignalsBlockedIterator {
    nb::object obj,previous,namespace_,builtins;bool started=false,closed=false,running=false;
    ViewerSignalsBlockedIterator()=default;ViewerSignalsBlockedIterator(ViewerSignalsBlockedIterator&&)=default;~ViewerSignalsBlockedIterator(){clear();}
    void clear(){closed=true;std::array<nb::object,4> old={std::move(obj),std::move(previous),std::move(namespace_),std::move(builtins)};for(auto& v:old)v.reset();}
};
struct SignalsRunning{ViewerSignalsBlockedIterator& s;explicit SignalsRunning(ViewerSignalsBlockedIterator& value):s(value){if(s.running){PyErr_SetString(PyExc_ValueError,"generator already executing");throw nb::python_error();}s.running=true;}~SignalsRunning(){s.running=false;}};
void signals_finally(ViewerSignalsBlockedIterator& s){nb::object callback=aw_attr(s.obj,"blockSignals");aw_call1(std::move(callback),s.previous);}
nb::object signals_send(nb::handle self,nb::object sent){
    auto& s=*nb::inst_ptr<ViewerSignalsBlockedIterator>(self);SignalsRunning running(s);if(s.closed)throw nb::stop_iteration();
    if(!s.started&&!sent.is_none()){PyErr_SetString(PyExc_TypeError,"can't send non-None value to a just-started generator");throw nb::python_error();}
    try{if(!s.started){aw_assign(s.previous,aw_call1(aw_attr(s.obj,"blockSignals"),nb::bool_(true)));s.started=true;return nb::none();}signals_finally(s);}
    catch(const nb::python_error& error){s.closed=true;if(error.matches(PyExc_StopIteration))collections::raise_exception(error.value(),true);throw;}
    s.clear();throw nb::stop_iteration();
}
void signals_close(nb::handle self){
    auto& s=*nb::inst_ptr<ViewerSignalsBlockedIterator>(self);SignalsRunning running(s);if(s.closed)return;if(!s.started){s.clear();return;}
    nb::object exit=aw_call0(nb::borrow<nb::object>(PyExc_GeneratorExit));
    {events::HandledException handled(exit);try{signals_finally(s);}catch(const nb::python_error& error){s.closed=true;if(error.value().ptr()!=exit.ptr())PyException_SetContext(error.value().ptr(),exit.inc_ref().ptr());if(error.matches(PyExc_GeneratorExit)){s.clear();return;}if(error.matches(PyExc_StopIteration))collections::raise_exception(error.value(),true);throw;}}
    s.clear();
}
void signals_throw(nb::handle self,nb::args args){
    nb::object error=collections::normalize_throw(args);auto& s=*nb::inst_ptr<ViewerSignalsBlockedIterator>(self);SignalsRunning running(s);bool live=!s.closed;
    {events::HandledException handled(error);try{if(s.started&&!s.closed)signals_finally(s);}catch(const nb::python_error& cleanup){s.closed=true;if(cleanup.value().ptr()!=error.ptr())PyException_SetContext(cleanup.value().ptr(),error.inc_ref().ptr());if(cleanup.matches(PyExc_StopIteration))collections::raise_exception(cleanup.value(),true);throw;}}
    s.closed=true;collections::raise_exception(error,live);
}
int signals_traverse(PyObject* self,visitproc visit,void* arg){Py_VISIT(Py_TYPE(self));if(nb::inst_ready(self)){const auto& s=*nb::inst_ptr<ViewerSignalsBlockedIterator>(self);Py_VISIT(s.obj.ptr());Py_VISIT(s.previous.ptr());Py_VISIT(s.namespace_.ptr());Py_VISIT(s.builtins.ptr());}return 0;}
int signals_clear(PyObject* self){if(nb::inst_ready(self))nb::inst_ptr<ViewerSignalsBlockedIterator>(self)->clear();return 0;}
void signals_finalize(PyObject* self)noexcept{
    if(!nb::inst_ready(self)||nb::inst_ptr<ViewerSignalsBlockedIterator>(self)->closed)return;
    PyObject *type=nullptr,*value=nullptr,*tb=nullptr;PyErr_Fetch(&type,&value,&tb);
    try{signals_close(self);}catch(nb::python_error& error){error.restore();PyErr_WriteUnraisable(self);}catch(const std::exception& error){PyErr_SetString(PyExc_RuntimeError,error.what());PyErr_WriteUnraisable(self);}catch(...){PyErr_SetString(PyExc_RuntimeError,"failed to restore blocked Qt signals");PyErr_WriteUnraisable(self);}PyErr_Restore(type,value,tb);
}
PyType_Slot signals_slots[]={{Py_tp_traverse,reinterpret_cast<void*>(signals_traverse)},{Py_tp_clear,reinterpret_cast<void*>(signals_clear)},{Py_tp_finalize,reinterpret_cast<void*>(signals_finalize)},{0,nullptr}};
destructor signals_base_dealloc=nullptr;
void signals_dealloc(PyObject* self)noexcept{PyObject_GC_UnTrack(self);PyObject_ClearWeakRefs(self);PyObject_GC_Track(self);if(PyObject_CallFinalizerFromDealloc(self)<0)return;signals_base_dealloc(self);}
nb::object signals_iterator(nb::object obj,nb::dict namespace_){ViewerSignalsBlockedIterator s;s.obj=std::move(obj);s.namespace_=std::move(namespace_);s.builtins=nb::borrow<nb::object>(PyEval_GetBuiltins());return nb::cast(std::move(s),nb::rv_policy::move);}
} // namespace

void bind_viewer_animation_workers(nb::module_& m){
    auto owner=nb::class_<ViewerAnimationLocals>(m,"ViewerAnimationLocals",nb::type_slots(aw_slots),nb::is_weak_referenceable());
#define AW_PROP(n) owner.def_prop_ro(#n,[](const ViewerAnimationLocals& s){return aw_optional(s.n);});
    AW_PROP(prev_slider) AW_PROP(_modes) AW_PROP(loop_mode) AW_PROP(before) AW_PROP(e) AW_PROP(value) AW_PROP(prev) AW_PROP(frame_range)
#undef AW_PROP
    auto iterator=nb::class_<ViewerSignalsBlockedIterator>(m,"ViewerSignalsBlockedIterator",nb::type_slots(signals_slots),nb::is_weak_referenceable())
        .def("__iter__",[](nb::handle self){return nb::borrow<nb::object>(self);}).def("__next__",[](nb::handle self){return signals_send(self,nb::none());})
        .def("send",&signals_send,nb::arg("value").none()).def("throw",&signals_throw).def("close",&signals_close)
        .def_prop_ro("obj",[](const ViewerSignalsBlockedIterator& s){return aw_optional(s.obj);}).def_prop_ro("previous",[](const ViewerSignalsBlockedIterator& s){return aw_optional(s.previous);});
    auto* type=reinterpret_cast<PyTypeObject*>(iterator.ptr());signals_base_dealloc=type->tp_dealloc;type->tp_dealloc=&signals_dealloc;
    m.def("viewer_animation_locals",[](nb::object operation,nb::object value){ViewerAnimationLocals s;s.operation=nb::cast<std::string>(operation);if(s.operation=="dims_play")s.loop_mode=std::move(value);else if(s.operation=="animation_range")s.frame_range=std::move(value);else if(s.operation=="slider_loop"||s.operation=="slider_range")s.value=std::move(value);return s;},nb::arg("operation"),nb::arg("value").none());
    m.def("viewer_animation_init",&animation_init,nb::arg("self").none(),nb::arg("parent").none(),nb::arg("namespace"),nb::arg("super_provider").none(),nb::arg("empty_slider_factory").none(),nb::arg("frame_range").none(),nb::arg("dims_range").none());
    m.def("viewer_animation_run",&animation_run,nb::arg("self").none(),nb::arg("namespace"));
    m.def("viewer_animation_slider",&animation_slider,nb::arg("self").none(),nb::arg("namespace"));
    m.def("viewer_animation_set_slider",&animation_set_slider,nb::arg("self").none(),nb::arg("slider").none(),nb::arg("namespace"),nb::arg("locals_owner"));
    m.def("viewer_animation_interval",&animation_interval,nb::arg("self").none(),nb::arg("namespace"));
    m.def("viewer_animation_set_interval",&animation_set_interval,nb::arg("self").none(),nb::arg("value").none(),nb::arg("namespace"));
    m.def("viewer_animation_work",&animation_work,nb::arg("self").none(),nb::arg("namespace"));
    m.def("viewer_animation_set_fps",&animation_set_fps,nb::arg("self").none(),nb::arg("fps").none(),nb::arg("namespace"));
    m.def("viewer_animation_set_frame_range",&animation_set_range,nb::arg("self").none(),nb::arg("namespace"),nb::arg("zero_range").none(),nb::arg("messages"),nb::arg("locals_owner"));
    m.def("viewer_animation_advance",&animation_advance,nb::arg("self").none(),nb::arg("namespace"));
    m.def("viewer_animation_loop_mode",[](nb::object self,nb::dict ns,nb::object name){return animation_get_optional(std::move(self),std::move(ns),std::move(name));},nb::arg("self").none(),nb::arg("namespace"),nb::arg("name").none());
    m.def("viewer_animation_axis",[](nb::object self,nb::dict ns,nb::object name){return animation_get_optional(std::move(self),std::move(ns),std::move(name));},nb::arg("self").none(),nb::arg("namespace"),nb::arg("name").none());
    m.def("viewer_animation_dims",[](nb::object self,nb::dict ns,nb::object name){return animation_get_optional(std::move(self),std::move(ns),std::move(name));},nb::arg("self").none(),nb::arg("namespace"),nb::arg("name").none());
    m.def("viewer_animation_finish",&animation_finish,nb::arg("self").none(),nb::arg("namespace"));
    m.def("viewer_animation_axis_changed",&animation_axis_changed,nb::arg("self").none(),nb::arg("namespace"));
    m.def("viewer_dims_play",&dims_play,nb::arg("self").none(),nb::arg("axis").none(),nb::arg("fps").none(),nb::arg("frame_range").none(),nb::arg("namespace"),nb::arg("literals"),nb::arg("locals_owner"));
    m.def("viewer_dims_is_playing",&dims_is_playing,nb::arg("self").none(),nb::arg("namespace"),nb::arg("deleted_messages"),nb::arg("locals_owner"));
    m.def("viewer_dims_set_frame",&dims_set_frame,nb::arg("self").none(),nb::arg("axis").none(),nb::arg("frame").none(),nb::arg("namespace"),nb::arg("locals_owner"));
    m.def("viewer_slider_fps",&slider_fps,nb::arg("self").none(),nb::arg("namespace"));
    m.def("viewer_slider_set_fps",&slider_set_fps,nb::arg("self").none(),nb::arg("value").none(),nb::arg("namespace"));
    m.def("viewer_slider_loop_mode",&slider_loop_mode,nb::arg("self").none(),nb::arg("namespace"));
    m.def("viewer_slider_set_loop_mode",&slider_set_loop_mode,nb::arg("self").none(),nb::arg("namespace"),nb::arg("underscore").none(),nb::arg("space").none(),nb::arg("locals_owner"));
    m.def("viewer_slider_frame_range",&slider_frame_range,nb::arg("self").none(),nb::arg("namespace"));
    m.def("viewer_slider_set_frame_range",&slider_set_frame_range,nb::arg("self").none(),nb::arg("namespace"),nb::arg("zero_range").none(),nb::arg("messages"),nb::arg("locals_owner"));
    m.def("viewer_slider_update_settings",&slider_update_settings,nb::arg("self").none(),nb::arg("fps").none(),nb::arg("loop_mode").none(),nb::arg("frame_range").none(),nb::arg("namespace"),nb::arg("zero_range").none());
    m.def("viewer_play_button_start",&button_style,nb::arg("self").none(),nb::arg("namespace"),nb::arg("property_name").none(),nb::arg("playing").none());
    m.def("viewer_play_button_stop",&button_style,nb::arg("self").none(),nb::arg("namespace"),nb::arg("property_name").none(),nb::arg("playing").none());
    m.def("viewer_canvas_enable_play",&enable_play,nb::arg("self").none(),nb::arg("args").none(),nb::arg("namespace"));
    m.def("viewer_signals_blocked",&signals_iterator,nb::arg("obj").none(),nb::arg("namespace"));
}
} // namespace nx
