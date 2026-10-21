// Only interoperability configuration is retained: the prior C call pointer,
// an owning weak reference and an active-call count. No owning function/code
// or application reference is stored.
#if PY_VERSION_HEX >= 0x030C0000
struct LFGridFunctionEntry {vectorcallfunc previous;PyObject* weak;size_t active=0;};
struct LFGridFunctionOwner {
    std::map<PyObject*,LFGridFunctionEntry> functions;
    PyObject* capsule=nullptr; // borrowed; module and weak callbacks own it
};
LFGridFunctionOwner* lfgv_owner=nullptr;
PyObject* lfgv_release(PyObject* capsule,PyObject* weak) {
    auto* owner=static_cast<LFGridFunctionOwner*>(PyCapsule_GetPointer(capsule,nullptr));
    if(!owner)return nullptr;
    for(auto found=owner->functions.begin();found!=owner->functions.end();++found) {
        if(found->second.weak!=weak)continue;
        Py_INCREF(capsule);owner->functions.erase(found);Py_DECREF(weak);Py_DECREF(capsule);break;
    }
    Py_RETURN_NONE;
}
PyMethodDef lfgv_release_definition={"_grid_function_scope_release",lfgv_release,METH_O,nullptr};
PyObject* lfgv_call(PyObject* function,PyObject* const* args,size_t count,PyObject* names) {
    try {
        if(!lfgv_owner)throw nb::type_error("function scope owner missing");
        auto found=lfgv_owner->functions.find(function);
        if(found==lfgv_owner->functions.end())throw nb::type_error("function scope declaration missing");
        auto* code=reinterpret_cast<PyCodeObject*>(PyFunction_GET_CODE(function));
        if(!lfgt_points(code).is_valid() || !lfgr_routes(code).is_valid())
            throw nb::type_error("function scope requires the prepared completion generation");
        struct Active {
            LFGridFunctionEntry& entry;
            Active(LFGridFunctionEntry& value):entry(value){++entry.active;}
            ~Active(){--entry.active;}
        } active(found->second);
        LFGridLineDriver scope;
        PyObject* result=found->second.previous(function,args,count,names);
        if(!result && PyErr_Occurred()) {
            nb::python_error error;
            nb::object traceback=nb::steal<nb::object>(PyException_GetTraceback(error.value().ptr()));
            for(const auto& item:scope.states)
                if(traceback.is_valid())lfgt_strip_transport(item.first,item.second,error.value().ptr(),traceback.ptr());
            error.restore();
        }
        return result;
    } catch(...) {lf_adapter_error();return nullptr;}
}
void lfgv_initialize(nb::module_& module) {
    if(lfgv_owner)throw nb::value_error("function scope module owner already exists");
    auto owner=std::make_unique<LFGridFunctionOwner>();
    nb::capsule capsule(owner.get(),[](void* value) noexcept {
        auto* state=static_cast<LFGridFunctionOwner*>(value);
        // Each registered weak callback owns this capsule. Final release is
        // possible only after all functions have died or been unregistered.
        if(lfgv_owner==state)lfgv_owner=nullptr;
        delete state;
    });
    auto* state=owner.release();state->capsule=capsule.ptr();
    module.attr("_grid_function_scope_owner")=capsule;lfgv_owner=state;
}
#else
void lfgv_initialize(nb::module_&) {}
#endif

void lfgv_set(nb::handle object,bool enabled) {
    if(!PyFunction_Check(object.ptr()))throw nb::type_error("genuine function required");
#if PY_VERSION_HEX >= 0x030C0000
    if(!lfgv_owner)throw nb::type_error("function scope owner missing");
    auto* function=reinterpret_cast<PyFunctionObject*>(object.ptr());
    auto found=lfgv_owner->functions.find(object.ptr());
    if(enabled) {
        auto* code=reinterpret_cast<PyCodeObject*>(function->func_code);
        if(!lfgt_points(code).is_valid() || !lfgr_routes(code).is_valid())
            throw nb::type_error("prepared completion function required");
        if(found!=lfgv_owner->functions.end() || !function->vectorcall || function->vectorcall==lfgv_call)
            throw nb::value_error("function call hook is already registered or invalid");
        nb::object code_owner=nb::borrow<nb::object>(function->func_code);
        auto previous=function->vectorcall;
        nb::object callback=lh_check(PyCFunction_NewEx(&lfgv_release_definition,lfgv_owner->capsule,nullptr));
        nb::object weak=lh_check(PyWeakref_NewRef(object.ptr(),callback.ptr()));
        if(function->func_code!=code_owner.ptr() || function->vectorcall!=previous ||
           lfgv_owner->functions.find(object.ptr())!=lfgv_owner->functions.end())
            throw nb::value_error("function changed during scope registration");
        auto inserted=lfgv_owner->functions.emplace(object.ptr(),LFGridFunctionEntry{previous,weak.ptr()});
        if(!inserted.second)throw nb::value_error("function scope registration raced");
        weak.release();
        PyFunction_SetVectorcall(function,lfgv_call);
    } else {
        if(found==lfgv_owner->functions.end() || function->vectorcall!=lfgv_call)
            throw nb::value_error("function scope is not the current call hook");
        if(found->second.active)throw nb::value_error("function scope is active");
        auto previous=found->second.previous;PyObject* weak=found->second.weak;
        PyFunction_SetVectorcall(function,previous);
        lfgv_owner->functions.erase(found);Py_DECREF(weak);
    }
#else
    PyErr_SetString(PyExc_NotImplementedError,"per-function scope requires CPython 3.12 or later");
    throw nb::python_error();
#endif
}

size_t lfgv_count() {
#if PY_VERSION_HEX >= 0x030C0000
    return lfgv_owner?lfgv_owner->functions.size():0;
#else
    return 0;
#endif
}
