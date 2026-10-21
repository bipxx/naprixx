// Handwritten CPython 3.12 observer-position state. This schedules no policy
// execution and retains neither an original code body nor owning Python refs.
// Cache/version rules are adapted from CPython v3.12.8 specialize.c,
// ceval_macros.h and generated_cases.c.h; see licenses/Python-PSF.txt.
#if PY_VERSION_HEX >= 0x030C0000 && PY_VERSION_HEX < 0x030D0000
#include <opcode.h>
struct LFGridAdaptiveSite {
    uint16_t counter=17;
    uint32_t type_version=0,function_version=0;
    int kind=0; // 0 generic, 1 Python __getattribute__, 2 Python __getitem__, 3 property.
    int end=0,line=0;
};
struct LFGridAdaptiveCode {
    std::map<long,LFGridAdaptiveSite> sites;
    bool instrumented=false;
    getattrofunc direct_getattribute=nullptr;
};
void lfga_free(void* value) {delete static_cast<LFGridAdaptiveCode*>(value);}
Py_ssize_t lfga_index() {
    constexpr auto key="naparixx._frontend_policy_probe.grid_adaptive_position_index";
    PyObject* dictionary=PyInterpreterState_GetDict(PyThreadState_Get()->interp);
    if(!dictionary)throw nb::python_error();
    PyObject* found=PyDict_GetItemString(dictionary,key);
    if(found) {
        if(!PyLong_CheckExact(found))throw nb::type_error("adaptive code-extra index changed");
        Py_ssize_t result=PyLong_AsSsize_t(found);if(PyErr_Occurred())throw nb::python_error();return result;
    }
    Py_ssize_t result=PyUnstable_Eval_RequestCodeExtraIndex(lfga_free);
    if(result<0)throw nb::python_error();
    nb::object number=nb::int_(result);
    if(PyDict_SetItemString(dictionary,key,number.ptr())<0)throw nb::python_error();
    return result;
}
void lfga_prepare(PyCodeObject* source,PyCodeObject* target,nb::handle rows) {
    auto* state=new LFGridAdaptiveCode;
    state->instrumented=source->_co_monitoring &&
        source->_co_monitoring->active_monitors.tools[PY_MONITORING_EVENT_INSTRUCTION];
    try {
        for(Py_ssize_t i=0;i<PyTuple_GET_SIZE(rows.ptr());++i) {
            PyObject* row=PyTuple_GET_ITEM(rows.ptr(),i),*name=PyTuple_GET_ITEM(row,1);
            if(PyUnicode_CompareWithASCIIString(name,"LOAD_ATTR") && PyUnicode_CompareWithASCIIString(name,"BINARY_SUBSCR"))continue;
            long offset=PyLong_AsLong(PyTuple_GET_ITEM(row,0));
            if(i+1>=PyTuple_GET_SIZE(rows.ptr()) || offset+2>=_PyCode_NBYTES(source))
                throw nb::type_error("adaptive source cache position is missing");
            LFGridAdaptiveSite site;
            _Py_CODEUNIT* instruction=_PyCode_CODE(source)+offset/2;
            site.counter=instruction[1].cache;
            if(instruction->op.code==LOAD_ATTR_GETATTRIBUTE_OVERRIDDEN || instruction->op.code==LOAD_ATTR_PROPERTY) {
                if(offset+2*(1+INLINE_CACHE_ENTRIES_LOAD_ATTR)>_PyCode_NBYTES(source))
                    throw nb::type_error("captured attribute cache is truncated");
                auto* cache=reinterpret_cast<_PyLoadMethodCache*>(instruction+1);
                site.kind=instruction->op.code==LOAD_ATTR_PROPERTY?3:1;
                site.type_version=read_u32(cache->type_version);
                site.function_version=read_u32(cache->keys_version);
            } else if(instruction->op.code==BINARY_SUBSCR_GETITEM)site.kind=2;
            site.end=static_cast<int>(PyLong_AsLong(PyTuple_GET_ITEM(PyTuple_GET_ITEM(rows.ptr(),i+1),0)))-2;
            site.line=static_cast<int>(PyLong_AsLong(PyTuple_GET_ITEM(row,4)));
            state->sites.emplace(offset,site);
        }
        if(PyUnstable_Code_SetExtra(reinterpret_cast<PyObject*>(target),lfga_index(),state)<0)throw nb::python_error();
    } catch(...) {delete state;throw;}
}
bool lfga_simple(PyObject* object,int count) {
    if(!object || !PyFunction_Check(object))return false;
    auto* code=reinterpret_cast<PyCodeObject*>(PyFunction_GET_CODE(object));
    return (code->co_flags&CO_OPTIMIZED) && !(code->co_flags&(CO_VARARGS|CO_VARKEYWORDS)) &&
        !code->co_kwonlyargcount && code->co_argcount==count;
}
uint32_t lfga_function_version(PyObject* object) {
    auto* function=reinterpret_cast<PyFunctionObject*>(object);
    if(function->func_version)return function->func_version;
    auto& next=PyThreadState_Get()->interp->func_state.next_version;
    if(!next)return 0;
    function->func_version=next++;return function->func_version;
}
PyObject* lfga_lookup(PyTypeObject* type,const char* name) {
    nb::object key=lh_check(PyUnicode_InternFromString(name));
    return _PyType_Lookup(type,key.ptr());
}
LFGridAdaptiveCode* lfga_sync(const LFGridFastLocals& locals) {
    if(!lfgo_context(locals).state)return nullptr;
    void* raw=nullptr;
    if(PyUnstable_Code_GetExtra(reinterpret_cast<PyObject*>(locals.code),lfgt_driver->adaptive_extra_index,&raw)<0)throw nb::python_error();
    if(!raw)return nullptr;
    auto& state=*static_cast<LFGridAdaptiveCode*>(raw);
    // Instruction instrumentation is code-wide. Once legacy opcode tracing
    // has been requested, a line-only tracer can keep it active even when
    // this frame requests no opcode callbacks. Removing that instrumentation
    // resets the source adaptive counters to their first-use delay.
    bool instrumented=locals.code->_co_monitoring &&
        locals.code->_co_monitoring->active_monitors.tools[PY_MONITORING_EVENT_INSTRUCTION];
    if(instrumented) {
        if(!state.instrumented)for(auto& pair:state.sites)pair.second.kind=0;
        state.instrumented=true;return &state;
    }
    if(state.instrumented)for(auto& pair:state.sites) {
        pair.second.kind=0;pair.second.counter=17;
    }
    state.instrumented=false;
    return &state;
}
void lfga_position(const LFGridFastLocals& locals,nb::handle owner,bool item,const char* attribute=nullptr) {
    auto context=lfgo_context(locals);if(!context.state)return;
    auto* observed=lfga_sync(locals);if(!observed || observed->instrumented)return;
    auto& state=*observed;
    PyObject* row=PyTuple_GET_ITEM(context.rows.ptr(),context.state->cursor-1);
    long offset=PyLong_AsLong(PyTuple_GET_ITEM(row,0));
    auto found=state.sites.find(offset);if(found==state.sites.end())return;
    auto& site=found->second;PyTypeObject* type=Py_TYPE(owner.ptr());
    nb::object property_function;
    int candidate_kind=item?2:1;
    auto candidate=[&]()->PyObject* {
        if(item) {
            if(!(type->tp_flags&Py_TPFLAGS_HEAPTYPE))return nullptr;
            PyObject* function=lfga_lookup(type,"__getitem__");
            return lfga_simple(function,2)?function:nullptr;
        }
        if(lfga_lookup(type,"__getattr__"))return nullptr;
        if(attribute && type->tp_getattro==PyObject_GenericGetAttr) {
            PyObject* descriptor=lfga_lookup(type,attribute);
            if(descriptor && Py_IS_TYPE(descriptor,&PyProperty_Type)) {
                property_function=lh_check(PyObject_GetAttrString(descriptor,"fget"));
                candidate_kind=3;
                return lfga_simple(property_function.ptr(),1)?property_function.ptr():nullptr;
            }
        }
        if(!state.direct_getattribute || type->tp_getattro!=state.direct_getattribute)return nullptr;
        PyObject* function=lfga_lookup(type,"__getattribute__");
        return lfga_simple(function,2)?function:nullptr;
    };
    if(site.kind) {
        bool hit=false;
        if(item && (type->tp_flags&Py_TPFLAGS_HEAPTYPE)) {
            auto& cached=reinterpret_cast<PyHeapTypeObject*>(type)->_spec_cache;
            hit=cached.getitem && PyFunction_Check(cached.getitem) &&
                reinterpret_cast<PyFunctionObject*>(cached.getitem)->func_version==cached.getitem_version;
        } else if(!item && type->tp_version_tag==site.type_version) {
            // A captured inline cache proves the direct slot only while both
            // the receiver type and getter function versions still match.
            if(site.kind==1 && !state.direct_getattribute && !lfga_lookup(type,"__getattr__")) {
                PyObject* function=lfga_lookup(type,"__getattribute__");
                if(lfga_simple(function,2) && reinterpret_cast<PyFunctionObject*>(function)->func_version==site.function_version)
                    state.direct_getattribute=type->tp_getattro;
            }
            PyObject* function=candidate();
            hit=function && candidate_kind==site.kind && reinterpret_cast<PyFunctionObject*>(function)->func_version==site.function_version;
        }
        if(hit && !PyThreadState_Get()->interp->eval_frame) {lfgt_set_position(locals,site.end,site.line);return;}
    }
    if(site.counter>>4) {site.counter-=16;return;}
    PyObject* function=candidate();
    uint32_t version=function?lfga_function_version(function):0;
    if(version && type->tp_version_tag && !PyThreadState_Get()->interp->eval_frame) {
        site.kind=candidate_kind;site.function_version=version;site.type_version=type->tp_version_tag;
        site.counter=52<<4;
        if(item) {
            auto& cached=reinterpret_cast<PyHeapTypeObject*>(type)->_spec_cache;
            cached.getitem=function;cached.getitem_version=version;
        }
        lfgt_set_position(locals,site.end,site.line);return;
    }
    unsigned backoff=std::min<unsigned>((site.counter&15)+1,12);
    // A failed specialization retries the generic instruction immediately;
    // that same dispatch consumes one deferred execution from the new backoff.
    site.kind=0;site.counter=static_cast<uint16_t>(((((1u<<backoff)-1)<<4)|backoff)-16);
}
uint32_t lfga_attr_version(const LFGridFastLocals& locals,nb::handle owner) {
    if(!lfgo_context(locals).state)return 0;
    PyTypeObject* type=Py_TYPE(owner.ptr());
    if(lfga_lookup(type,"__getattr__") || !lfga_simple(lfga_lookup(type,"__getattribute__"),2))return 0;
    return type->tp_version_tag;
}
void lfga_after_attr(const LFGridFastLocals& locals,nb::handle owner,PyTypeObject* before,uint32_t version) {
    if(!version || Py_TYPE(owner.ptr())!=before || before->tp_version_tag!=version)return;
    auto context=lfgo_context(locals);if(!context.state)return;
    void* raw=nullptr;
    if(PyUnstable_Code_GetExtra(reinterpret_cast<PyObject*>(locals.code),lfgt_driver->adaptive_extra_index,&raw)<0)throw nb::python_error();
    if(!raw)return;
    auto& state=*static_cast<LFGridAdaptiveCode*>(raw);
    // An unchanged type with a Python getter and no __getattr__ uses the
    // direct getter slot after a successful call. A callback replacing the
    // method invalidates the version and must not re-calibrate this pointer.
    if(!state.direct_getattribute)state.direct_getattribute=before->tp_getattro;
}
#else
Py_ssize_t lfga_index() {return -1;}
void lfga_prepare(PyCodeObject*,PyCodeObject*,nb::handle) {}
void lfga_sync(const LFGridFastLocals&) {}
void lfga_position(const LFGridFastLocals&,nb::handle,bool,const char* =nullptr) {}
uint32_t lfga_attr_version(const LFGridFastLocals&,nb::handle) {return 0;}
void lfga_after_attr(const LFGridFastLocals&,nb::handle,PyTypeObject*,uint32_t) {}
#endif
