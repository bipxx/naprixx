// Explicit PEP 669 pilot. CPython keeps the function vectorcall and caller
// specialization caches; local start/return events bracket the native driver.
#if PY_VERSION_HEX >= 0x030C0000
struct LFGridMonitoredFrame {
    nb::object frame;
    std::unique_ptr<LFGridLineDriver> scope;
};
struct LFGridCodeMonitor {
    PyObject* capsule=nullptr; // borrowed; module and C callbacks own it
    int tool=-1;
    uint64_t session=0;
    nb::object monitoring;
    std::array<nb::object,3> callbacks;
    std::map<PyObject*,PyObject*> codes; // code -> owned weak reference
    std::map<PyFrameObject*,LFGridMonitoredFrame> frames;
    std::map<std::pair<PyThreadState*,uint64_t>,PyObject*> threads; // borrowed tokens
    nb::object thread_key;
};
LFGridCodeMonitor* lfgm_owner=nullptr;
struct LFGridMonitorThread {
    LFGridCodeMonitor* state;
    std::pair<PyThreadState*,uint64_t> identity;
    nb::object owner;
};
void lfgm_track_thread(LFGridCodeMonitor* state) {
    auto* thread=PyThreadState_Get();auto identity=std::make_pair(thread,PyThreadState_GetID(thread));
    if(state->threads.find(identity)!=state->threads.end())return;
    auto* dictionary=PyThreadState_GetDict();
    if(!dictionary) {PyErr_NoMemory();throw nb::python_error();}
    if(PyDict_GetItemWithError(dictionary,state->thread_key.ptr()))
        throw nb::value_error("code monitor thread token was replaced");
    if(PyErr_Occurred())throw nb::python_error();
    auto token=std::make_unique<LFGridMonitorThread>(LFGridMonitorThread{
        state,identity,nb::borrow<nb::object>(state->capsule)});
    nb::capsule capsule(token.get(),[](void* value) noexcept {
        auto* token=static_cast<LFGridMonitorThread*>(value);
        token->state->threads.erase(token->identity);
        delete token;
    });
    token.release();
    state->threads.emplace(identity,capsule.ptr());
    if(PyDict_SetItem(dictionary,state->thread_key.ptr(),capsule.ptr())<0)throw nb::python_error();
}
void lfgm_untrack_thread(LFGridCodeMonitor* state) {
    auto* thread=PyThreadState_Get();auto identity=std::make_pair(thread,PyThreadState_GetID(thread));
    for(const auto& item:state->frames)
        if(item.second.scope->thread==thread && item.second.scope->thread_id==identity.second)return;
    auto found=state->threads.find(identity);if(found==state->threads.end())return;
    auto* dictionary=PyThreadState_GetDict();
    if(!dictionary) {PyErr_NoMemory();throw nb::python_error();}
    if(PyDict_GetItemWithError(dictionary,state->thread_key.ptr())!=found->second)
        throw nb::value_error("code monitor thread token was replaced");
    if(PyDict_DelItem(dictionary,state->thread_key.ptr())<0)throw nb::python_error();
}
void lfgm_retire_finished() noexcept {
    auto* state=lfgm_owner;if(!state)return;
    // Frame-local finalizers can stop monitoring and remove the module owner.
    // Keep the registry alive until every retirement loop has finished.
    nb::object owner=nb::borrow<nb::object>(state->capsule);
    auto* thread=PyThreadState_Get();auto id=PyThreadState_GetID(thread);
    while(lfgt_driver) {
        auto found=state->frames.begin();
        for(;found!=state->frames.end();++found)
            if(found->second.scope.get()==lfgt_driver)break;
        if(found==state->frames.end())break;
        auto* scope=found->second.scope.get();
        if(scope->thread!=thread || scope->thread_id!=id ||
           found->first->f_frame->owner!=FRAME_OWNED_BY_FRAME_OBJECT)break;
        // Only a detached, completed synchronous frame on its original live
        // thread is safe to retire. Live/suspended/foreign-thread scopes stay.
        auto retired=state->frames.extract(found);
    }
    // A terminated worker has no live TLS chain to restore. Detached frames
    // may then be released on this interpreter's next native boundary.
    // Extract before decref: releasing frame locals can reenter this registry.
    for(;;) {
        auto found=state->frames.begin();
        for(;found!=state->frames.end();++found) {
            auto* scope=found->second.scope.get();
            if(found->first->f_frame->owner==FRAME_OWNED_BY_FRAME_OBJECT &&
               state->threads.find(std::make_pair(scope->thread,scope->thread_id))==state->threads.end())break;
        }
        if(found==state->frames.end())break;
        auto retired=state->frames.extract(found);
    }
}
constexpr int lfgm_events[3]={1<<PY_MONITORING_EVENT_PY_START,
    1<<PY_MONITORING_EVENT_PY_RETURN,1<<PY_MONITORING_EVENT_PY_UNWIND};
nb::object lfgm_call(LFGridCodeMonitor* state,const char* name,nb::tuple args) {
    nb::object method=lh_check(PyObject_GetAttrString(state->monitoring.ptr(),name));
    return lh_check(PyObject_CallObject(method.ptr(),args.ptr()));
}
LFGridCodeMonitor* lfgm_state(PyObject* capsule) {
    auto* state=static_cast<LFGridCodeMonitor*>(PyCapsule_GetPointer(capsule,nullptr));
    if(!state)throw nb::python_error();return state;
}
PyObject* lfgm_release(PyObject* capsule,PyObject* weak) {
    try {
        auto* state=lfgm_state(capsule);
        for(auto found=state->codes.begin();found!=state->codes.end();++found) {
            if(found->second!=weak)continue;
            Py_INCREF(capsule);state->codes.erase(found);Py_DECREF(weak);Py_DECREF(capsule);break;
        }
        Py_RETURN_NONE;
    } catch(...) {lf_adapter_error();return nullptr;}
}
PyObject* lfgm_start(PyObject* capsule,PyObject* args) {
    try {
        auto* state=lfgm_state(capsule);
        lfgm_retire_finished();
        if(PyTuple_GET_SIZE(args)!=2)throw nb::type_error("monitor start arguments changed");
        PyObject* code=PyTuple_GET_ITEM(args,0);
        if(state->codes.find(code)==state->codes.end())Py_RETURN_NONE;
        auto* frame=PyEval_GetFrame();
        if(!frame)throw nb::type_error("monitor frame missing");
        nb::object actual=lh_check(reinterpret_cast<PyObject*>(PyFrame_GetCode(frame)));
        if(actual.ptr()!=code)throw nb::type_error("monitor frame declaration changed");
        if(state->frames.find(frame)!=state->frames.end())throw nb::value_error("monitor frame already active");
        lfgm_track_thread(state);
        LFGridMonitoredFrame active{nb::borrow<nb::object>(reinterpret_cast<PyObject*>(frame)),
            std::make_unique<LFGridLineDriver>()};
        active.scope->states.emplace(frame,LFGridOpcodeState{});
        state->frames.emplace(frame,std::move(active));
        Py_RETURN_NONE;
    } catch(...) {lf_adapter_error();return nullptr;}
}
PyObject* lfgm_finish(PyObject* capsule,PyObject* args,bool failing) {
    try {
        auto* state=lfgm_state(capsule);
        lfgm_retire_finished();
        if(PyTuple_GET_SIZE(args)!=3)throw nb::type_error("monitor completion arguments changed");
        if(state->codes.find(PyTuple_GET_ITEM(args,0))==state->codes.end())Py_RETURN_NONE;
        auto* frame=PyEval_GetFrame();if(!frame)Py_RETURN_NONE;
        auto found=state->frames.find(frame);if(found==state->frames.end())Py_RETURN_NONE;
        if(lfgt_driver!=found->second.scope.get())throw nb::value_error("monitor scopes completed out of order");
        if(failing) {
            PyObject* error=PyTuple_GET_ITEM(args,2);
            nb::object traceback=nb::steal<nb::object>(PyException_GetTraceback(error));
            for(const auto& item:found->second.scope->states)
                if(traceback.is_valid())lfgt_strip_transport(item.first,item.second,error,traceback.ptr());
        }
        {auto retired=state->frames.extract(found);}
        lfgm_untrack_thread(state);
        Py_RETURN_NONE;
    } catch(...) {lf_adapter_error();return nullptr;}
}
PyObject* lfgm_return(PyObject* capsule,PyObject* args){return lfgm_finish(capsule,args,false);}
PyObject* lfgm_unwind(PyObject* capsule,PyObject* args){return lfgm_finish(capsule,args,true);}
PyMethodDef lfgm_definitions[4]={
    {"_grid_code_start",lfgm_start,METH_VARARGS,nullptr},
    {"_grid_code_return",lfgm_return,METH_VARARGS,nullptr},
    {"_grid_code_unwind",lfgm_unwind,METH_VARARGS,nullptr},
    {"_grid_code_release",lfgm_release,METH_O,nullptr}};
void lfgm_initialize(nb::module_& module) {
    if(lfgm_owner)throw nb::value_error("code monitor owner already exists");
    auto owner=std::make_unique<LFGridCodeMonitor>();
    owner->thread_key=nb::str("_naparixx_grid_monitor_thread_probe");
    nb::capsule capsule(owner.get(),[](void* value) noexcept {
        auto* state=static_cast<LFGridCodeMonitor*>(value);
        if(lfgm_owner==state) {lfgm_owner=nullptr;lfgt_retire_finished=nullptr;}
        delete state;
    });
    auto* state=owner.release();state->capsule=capsule.ptr();
    module.attr("_grid_code_monitor_owner")=capsule;lfgm_owner=state;lfgt_retire_finished=lfgm_retire_finished;
}
#else
void lfgm_initialize(nb::module_&) {}
#endif
void lfgm_begin(int tool) {
#if PY_VERSION_HEX >= 0x030C0000
    auto* state=lfgm_owner;if(!state)throw nb::value_error("code monitor owner missing");
    nb::object owner=nb::borrow<nb::object>(state->capsule);
    auto session=state->session;
    lfgm_retire_finished();
    if(state->session!=session)throw nb::value_error("code monitor session changed during cleanup");
    if(state->tool>=0)throw nb::value_error("code monitor already started");
    nb::object sys=lh_check(PyImport_ImportModule("sys"));
    state->monitoring=lh_check(PyObject_GetAttrString(sys.ptr(),"monitoring"));
    try {
        if(!lfgm_call(state,"get_tool",nb::make_tuple(tool)).is_none() ||
           nb::cast<int>(lfgm_call(state,"get_events",nb::make_tuple(tool)))!=0)
            throw nb::value_error("an unused monitoring tool with no global events is required");
        lfgm_call(state,"use_tool_id",nb::make_tuple(tool,"naparixx.grid_scope_probe"));
    } catch(...) {state->monitoring.reset();throw;}
    state->tool=tool;++state->session;
    try {
        for(int i=0;i<3;++i) {
            state->callbacks[i]=lh_check(PyCFunction_NewEx(&lfgm_definitions[i],owner.ptr(),nullptr));
            nb::object old=lfgm_call(state,"register_callback",nb::make_tuple(tool,lfgm_events[i],state->callbacks[i]));
            if(!old.is_none()) {
                lfgm_call(state,"register_callback",nb::make_tuple(tool,lfgm_events[i],old));
                state->callbacks[i].reset();
                throw nb::value_error("unused monitoring tool has a retained callback");
            }
        }
        lfgm_call(state,"set_events",nb::make_tuple(tool,lfgm_events[2]));
    } catch(...) {
        for(int i=0;i<3;++i)if(state->callbacks[i].is_valid()) {
            lfgm_call(state,"register_callback",nb::make_tuple(tool,lfgm_events[i],nb::none()));
            state->callbacks[i].reset();
        }
        lfgm_call(state,"free_tool_id",nb::make_tuple(tool));state->tool=-1;++state->session;state->monitoring.reset();throw;
    }
#else
    PyErr_SetString(PyExc_NotImplementedError,"code monitoring requires CPython 3.12 or later");throw nb::python_error();
#endif
}
void lfgm_set(nb::handle code,bool enabled) {
#if PY_VERSION_HEX >= 0x030C0000
    auto* state=lfgm_owner;
    if(!state || state->tool<0)throw nb::value_error("code monitor is not started");
    nb::object owner=nb::borrow<nb::object>(state->capsule);
    auto session=state->session;
    lfgm_retire_finished();
    if(state->tool<0)throw nb::value_error("code monitor is not started");
    if(state->session!=session)throw nb::value_error("code monitor session changed during cleanup");
    if(!PyCode_Check(code.ptr()))throw nb::type_error("prepared code required");
    auto found=state->codes.find(code.ptr());
    if(enabled) {
        auto* actual=reinterpret_cast<PyCodeObject*>(code.ptr());
        if(!lfgt_points(actual).is_valid() || !lfgr_routes(actual).is_valid() ||
           (actual->co_flags&(CO_GENERATOR|CO_COROUTINE|CO_ASYNC_GENERATOR)))
            throw nb::type_error("prepared synchronous completion code required");
        if(found!=state->codes.end())throw nb::value_error("code monitor already registered");
        nb::object callback=lh_check(PyCFunction_NewEx(&lfgm_definitions[3],owner.ptr(),nullptr));
        nb::object weak=lh_check(PyWeakref_NewRef(code.ptr(),callback.ptr()));
        auto inserted=state->codes.emplace(code.ptr(),weak.ptr());
        if(!inserted.second)throw nb::value_error("code monitor registration raced");
        weak.release();
        try {lfgm_call(state,"set_local_events",nb::make_tuple(state->tool,code,lfgm_events[0]|lfgm_events[1]));}
        catch(...) {PyObject* reference=inserted.first->second;state->codes.erase(inserted.first);Py_DECREF(reference);throw;}
    } else {
        if(found==state->codes.end())throw nb::value_error("code monitor is not registered");
        for(const auto& item:state->frames) {
            nb::object actual=lh_check(reinterpret_cast<PyObject*>(PyFrame_GetCode(item.first)));
            if(actual.ptr()==code.ptr())throw nb::value_error("code monitor is active");
        }
        lfgm_call(state,"set_local_events",nb::make_tuple(state->tool,code,0));
        PyObject* weak=found->second;state->codes.erase(found);Py_DECREF(weak);
    }
#else
    PyErr_SetString(PyExc_NotImplementedError,"code monitoring requires CPython 3.12 or later");throw nb::python_error();
#endif
}
void lfgm_end() {
#if PY_VERSION_HEX >= 0x030C0000
    auto* state=lfgm_owner;if(!state || state->tool<0)throw nb::value_error("code monitor is not started");
    nb::object owner=nb::borrow<nb::object>(state->capsule);
    auto session=state->session;
    lfgm_retire_finished();
    if(state->tool<0)throw nb::value_error("code monitor is not started");
    if(state->session!=session)throw nb::value_error("code monitor session changed during cleanup");
    if(!state->codes.empty() || !state->frames.empty())throw nb::value_error("code monitor still has registrations or frames");
    lfgm_untrack_thread(state);
    for(int i=0;i<3;++i) {
        nb::object old=lfgm_call(state,"register_callback",nb::make_tuple(state->tool,lfgm_events[i],state->callbacks[i]));
        if(old.ptr()!=state->callbacks[i].ptr()) {
            lfgm_call(state,"register_callback",nb::make_tuple(state->tool,lfgm_events[i],old));
            throw nb::value_error("another callback replaced the code monitor");
        }
    }
    lfgm_call(state,"set_events",nb::make_tuple(state->tool,0));
    for(int i=0;i<3;++i)lfgm_call(state,"register_callback",nb::make_tuple(state->tool,lfgm_events[i],nb::none()));
    lfgm_call(state,"free_tool_id",nb::make_tuple(state->tool));state->tool=-1;++state->session;
    for(auto& callback:state->callbacks)callback.reset();state->monitoring.reset();
#else
    PyErr_SetString(PyExc_NotImplementedError,"code monitoring requires CPython 3.12 or later");throw nb::python_error();
#endif
}
nb::tuple lfgm_counts() {
#if PY_VERSION_HEX >= 0x030C0000
    if(lfgm_owner)return nb::make_tuple(lfgm_owner->tool,lfgm_owner->codes.size(),lfgm_owner->frames.size());
#endif
    return nb::make_tuple(-1,0,0);
}
