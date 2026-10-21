// Experimental GIL-only publication protocol for CPython3.11-3.13.
// All audit/watcher notifications precede all code writes. A notification can
// therefore be followed by an abort if another callback changes a binding.
// This batch protocol deliberately is not a sequence of ordinary setters.
#include <mutex>
#include <unordered_set>
#ifdef Py_GIL_DISABLED
#error "Frontend batch publication requires the GIL"
#endif
namespace nx {
std::mutex lfb_mutex;
std::unordered_set<PyInterpreterState*> lfb_active;
struct LFBTransaction {
    PyInterpreterState* interpreter=PyInterpreterState_Get();
    LFBTransaction() {
        std::lock_guard<std::mutex> lock(lfb_mutex);
        if (!lfb_active.insert(interpreter).second)
            throw std::runtime_error("frontend publication transaction is already active");
    }
    ~LFBTransaction() {std::lock_guard<std::mutex> lock(lfb_mutex);lfb_active.erase(interpreter);}
};
struct LFBGCPause {
    bool enabled=PyGC_IsEnabled()!=0;
    LFBGCPause(){PyGC_Disable();}
    ~LFBGCPause(){if(enabled) PyGC_Enable();}
};
struct LFBChange {
    nb::object function,old_code,new_code,globals,builtins,defaults,kwdefaults,closure;
    bool same() const {
        auto* f=reinterpret_cast<PyFunctionObject*>(function.ptr());
        auto nullable=[](PyObject* p){return p?p:Py_None;};
        return f->func_code==old_code.ptr() && f->func_globals==globals.ptr() &&
            nullable(f->func_builtins)==builtins.ptr() && nullable(f->func_defaults)==defaults.ptr() &&
            nullable(f->func_kwdefaults)==kwdefaults.ptr() && nullable(f->func_closure)==closure.ptr();
    }
};
LFBChange lfb_change(nb::object function,nb::object code) {
    if (!PyFunction_Check(function.ptr()) || !PyCode_Check(code.ptr()))
        throw nb::type_error("genuine function and prepared CodeType are required");
    auto* f=reinterpret_cast<PyFunctionObject*>(function.ptr());
    auto* before=reinterpret_cast<PyCodeObject*>(f->func_code);
    auto* after=reinterpret_cast<PyCodeObject*>(code.ptr());
    constexpr int category=CO_GENERATOR|CO_COROUTINE|CO_ASYNC_GENERATOR|CO_VARARGS|CO_VARKEYWORDS;
    if ((before->co_flags&category)!=(after->co_flags&category) || before->co_argcount!=after->co_argcount ||
        before->co_posonlyargcount!=after->co_posonlyargcount || before->co_kwonlyargcount!=after->co_kwonlyargcount ||
        PyCode_GetNumFree(after)!=(f->func_closure?PyTuple_GET_SIZE(f->func_closure):0))
        throw nb::type_error("prepared function binding/category ABI changed");
    LFBChange result;
    result.function=function;result.old_code=nb::borrow<nb::object>(f->func_code);result.new_code=code;
    auto hold=[](PyObject* value){return nb::borrow<nb::object>(value?value:Py_None);};
    result.globals=hold(f->func_globals);result.builtins=hold(f->func_builtins);
    result.defaults=hold(f->func_defaults);result.kwdefaults=hold(f->func_kwdefaults);result.closure=hold(f->func_closure);
    return result;
}
void lfb_notify(PyFunctionObject* function,PyObject* code) {
#if PY_VERSION_HEX >= 0x030C0000
    PyInterpreterState* interpreter=PyInterpreterState_Get();
    unsigned bits=interpreter->active_func_watchers;
    for (int index=0;bits;++index,bits>>=1) {
        if (!(bits&1)) continue;
        auto callback=interpreter->func_watchers[index];
        if (!callback) throw std::runtime_error("function watcher registration changed during notification");
        if (callback(PyFunction_EVENT_MODIFY_CODE,function,code)>=0) continue;
#if PY_VERSION_HEX >= 0x030D0000
        PyErr_FormatUnraisable("Exception ignored in %s watcher callback for function %U at %p",
            "PyFunction_EVENT_MODIFY_CODE",function->func_qualname,function);
#else
        nb::object representation=nb::steal<nb::object>(PyUnicode_FromFormat("<function %U at %p>",function->func_qualname,function));
        nb::object context;
        if (representation.is_valid()) context=nb::steal<nb::object>(PyUnicode_FromFormat(
            "%s watcher callback for %U","PyFunction_EVENT_MODIFY_CODE",representation.ptr()));
        if (!context.is_valid()) {PyErr_Clear();context=nb::none();}
        PyErr_WriteUnraisable(context.ptr());
#endif
    }
#else
    (void)function;(void)code;
#endif
}
template<class Validate,class Committed>
void lfb_execute(std::vector<LFBChange>& changes,Validate&& validate,Committed&& committed) {
    LFBTransaction transaction;
    auto check=[&](){
        for (const auto& change:changes) if (!change.same()) throw nb::type_error("publication inputs changed during callbacks");
        validate();
    };
    check();
    for (size_t i=0;i<changes.size();++i) {
        for (size_t j=0;j<i;++j) if (changes[i].function.ptr()==changes[j].function.ptr())
            throw nb::type_error("duplicate publication function");
        if (PySys_Audit("object.__setattr__","OsO",changes[i].function.ptr(),"__code__",changes[i].new_code.ptr())<0)
            throw nb::python_error();
        check();
    }
    for (const auto& change:changes) {
        lfb_notify(reinterpret_cast<PyFunctionObject*>(change.function.ptr()),change.new_code.ptr());
        check();
    }
    // Final validation cannot run Python callbacks. After it succeeds, the
    // writes/metadata commit cannot allocate, release the GIL or drop last refs.
    LFBGCPause pause;
    check();
    for (auto& change:changes) {
        auto* function=reinterpret_cast<PyFunctionObject*>(change.function.ptr());
#if PY_VERSION_HEX >= 0x030D0000
        RARE_EVENT_INTERP_INC(transaction.interpreter,func_modification);
        _PyFunction_SetVersion(function,0);
#else
        function->func_version=0;
#endif
        PyObject* old=function->func_code;
        function->func_code=change.new_code.inc_ref().ptr();
        Py_DECREF(old); // change.old_code pins this object through every write.
    }
    committed();
}
nb::object lfb_publish_frontend(LFCanonicalSnapshot& snapshot) {
    if (snapshot.published) throw std::runtime_error("frontend plan is already published");
    if (!snapshot.producer_scopes.is_valid() || snapshot.prepared.size()!=50 || snapshot.bindings.code_anchors.size()!=50)
        throw nb::type_error("producer-qualified complete frontend preparation is required");
    std::vector<LFBChange> changes;
    changes.reserve(50);
    auto anchors=snapshot.bindings.anchors;
    for (size_t i=0;i<50;++i) {
        changes.push_back(lfb_change(snapshot.bindings.functions[i],snapshot.prepared[i]));
        anchors[snapshot.bindings.code_anchors[i]]=snapshot.prepared[i];
    }
    nb::object retired_producers;
    lfb_execute(changes,[&](){snapshot.validate();},[&]() noexcept {
        snapshot.bindings.anchors.swap(anchors);
        retired_producers=std::move(snapshot.producer_scopes);
        snapshot.published=true;
    });
    return nb::none();
}
nb::object lfb_probe(nb::tuple functions,nb::tuple codes) {
    if (functions.size()!=codes.size() || !functions.size()) throw nb::type_error("complete nonempty publication tables required");
    std::vector<LFBChange> changes;
    for (size_t i=0;i<functions.size();++i)
        changes.push_back(lfb_change(nb::borrow<nb::object>(functions[i]),nb::borrow<nb::object>(codes[i])));
    lfb_execute(changes,[](){},[]() noexcept {});
    return nb::none();
}
#if PY_VERSION_HEX >= 0x030C0000
int lfb_test_watcher(PyFunction_WatchEvent event,PyFunctionObject* function,PyObject* value) {
    if (event!=PyFunction_EVENT_MODIFY_CODE) return 0;
    PyObject* module=PyDict_GetItemString(PyImport_GetModuleDict(),"_frontend_policy_probe");
    PyObject* callback=module?PyDict_GetItemString(PyModule_GetDict(module),"_test_watcher_callback"):nullptr;
    if (!callback) return 0;
    PyObject* result=PyObject_CallFunctionObjArgs(callback,reinterpret_cast<PyObject*>(function),value,nullptr);
    if (!result) return -1;
    Py_DECREF(result);return 0;
}
#endif
void lfb_exports(nb::module_& m) {
    m.def("publish_code_batch_probe",&lfb_probe);
    m.def("function_version_probe",[](nb::object function){
        if (!PyFunction_Check(function.ptr())) throw nb::type_error("genuine function is required");
        return reinterpret_cast<PyFunctionObject*>(function.ptr())->func_version;
    });
#if PY_VERSION_HEX >= 0x030D0000
    m.def("version_cache_probe",[](uint32_t version){
        auto& slot=PyInterpreterState_Get()->func_state.func_version_cache[version%FUNC_VERSION_CACHE_SIZE];
        return nb::make_tuple(nb::handle(slot.func?reinterpret_cast<PyObject*>(slot.func):Py_None),
                              nb::handle(slot.code?slot.code:Py_None));
    });
#endif
#if PY_VERSION_HEX >= 0x030C0000
    m.def("start_watcher_probe",[](nb::object callback){
        nb::object m=lfc_dict_value(PyImport_GetModuleDict(),"_frontend_policy_probe");
        if (!PyCallable_Check(callback.ptr()) || PyObject_HasAttrString(m.ptr(),"_test_watcher_id"))
            throw nb::type_error("one callable watcher probe is allowed");
        m.attr("_test_watcher_callback")=callback;
        int id=PyFunction_AddWatcher(&lfb_test_watcher);
        if (id<0) {PyObject_DelAttrString(m.ptr(),"_test_watcher_callback");throw nb::python_error();}
        m.attr("_test_watcher_id")=nb::int_(id);
    });
    m.def("stop_watcher_probe",[](){
        nb::object m=lfc_dict_value(PyImport_GetModuleDict(),"_frontend_policy_probe");
        nb::object id=lh_check(PyObject_GetAttrString(m.ptr(),"_test_watcher_id"));
        if (PyFunction_ClearWatcher(nb::cast<int>(id))<0) throw nb::python_error();
        if (PyObject_DelAttrString(m.ptr(),"_test_watcher_id")<0 || PyObject_DelAttrString(m.ptr(),"_test_watcher_callback")<0)
            throw nb::python_error();
    });
#endif
}
}
