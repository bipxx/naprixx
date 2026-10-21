// Opt-in PEP 523 experiment: keep source tracing around a genuine frame's
// complete evaluation, including its thin transport entry and completion.
// This hook is interpreter-wide and is never installed by module import.
struct LFGridEvalScopeState {
    _PyFrameEvalFunction previous;
    size_t calls=0;
};
std::map<PyInterpreterState*,LFGridEvalScopeState> lfge_scopes;

PyObject* lfge_eval(PyThreadState* thread,_PyInterpreterFrame* frame,int throwing) {
    auto entry=lfge_scopes.find(thread->interp);
    if(entry==lfge_scopes.end())return _PyEval_EvalFrameDefault(thread,frame,throwing);
    auto evaluator=entry->second.previous;
    if(!evaluator)evaluator=_PyEval_EvalFrameDefault;
    struct Active {
        LFGridEvalScopeState& state;
        Active(LFGridEvalScopeState& value):state(value){++state.calls;}
        ~Active(){--state.calls;}
    } active(entry->second);
    try {
#if PY_VERSION_HEX >= 0x030D0000
        auto* code=_PyFrame_GetCode(frame);
#else
        auto* code=frame->f_code;
#endif
        if(!lfgt_points(code).is_valid())return evaluator(thread,frame,throwing);
        LFGridLineDriver scope;
        PyObject* result=evaluator(thread,frame,throwing);
        if(!result && PyErr_Occurred()) {
            // Match the explicit adapter boundary when a callback disables
            // tracing before the VM can report the transport's exception.
            nb::python_error error;
            nb::object traceback=nb::steal<nb::object>(PyException_GetTraceback(error.value().ptr()));
            for(const auto& item:scope.states)
                if(traceback.is_valid())lfgt_strip_transport(item.first,item.second,error.value().ptr(),traceback.ptr());
            error.restore();
        }
        return result;
    } catch(...) {lf_adapter_error();return nullptr;}
}

void lfge_set(bool enabled) {
    auto* interpreter=PyThreadState_Get()->interp;
    auto found=lfge_scopes.find(interpreter);
    auto current=_PyInterpreterState_GetEvalFrameFunc(interpreter);
    if(enabled) {
        if(found!=lfge_scopes.end())throw nb::value_error("grid evaluator scope already enabled");
        if(current==lfge_eval)throw nb::value_error("grid evaluator scope state missing");
        lfge_scopes.emplace(interpreter,LFGridEvalScopeState{current});
        _PyInterpreterState_SetEvalFrameFunc(interpreter,lfge_eval);
    } else {
        if(found==lfge_scopes.end())throw nb::value_error("grid evaluator scope is not enabled");
        // The binding itself is evaluated by native code. An active Python
        // evaluator using this state must return before it can be removed.
        if(found->second.calls)throw nb::value_error("grid evaluator scope is active");
        if(current!=lfge_eval)throw nb::value_error("another evaluator replaced the grid scope");
        auto previous=found->second.previous;
        _PyInterpreterState_SetEvalFrameFunc(interpreter,previous);
        lfge_scopes.erase(found);
    }
}
