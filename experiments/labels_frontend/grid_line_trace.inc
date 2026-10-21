// Opt-in native source-line transport pilot. No application startup integration.
struct LFGridLineDriver;
thread_local LFGridLineDriver* lfgt_driver = nullptr;
int lfgt_proxy(PyObject*, PyFrameObject*, int, PyObject*);
Py_ssize_t lfga_index();
struct LFGridOpcodeState {
    Py_ssize_t cursor = 1;
    std::vector<std::pair<std::string,std::string>> pending;
    bool trailing_opcode = false;
    int return_point = -1;
    int error_point = -1;
    int attribute_error_offset = -1;
    int attribute_error_line = -1;
    int return_offset = -1;
    int return_line = -1;
    PyObject* reported_error = nullptr;
    PyObject* reported_traceback = nullptr;
    bool escaping_error = false;
};
void lfgt_error_point(const LFGridFastLocals& locals,int point);

nb::object lfgt_points(PyCodeObject* code) {
    PyObject* constants = code->co_consts;
    if (PyTuple_GET_SIZE(constants) == 0) return {};
    PyObject* payload = PyTuple_GET_ITEM(constants, PyTuple_GET_SIZE(constants) - 1);
    if (!PyTuple_CheckExact(payload) || (PyTuple_GET_SIZE(payload) != 4 && PyTuple_GET_SIZE(payload) != 5)) return {};
    PyObject* tag = PyTuple_GET_ITEM(payload, 0);
    if (!PyUnicode_CheckExact(tag) || PyUnicode_CompareWithASCIIString(tag, "naparixx.grid.v2") != 0) return {};
    PyObject* points = PyTuple_GET_ITEM(payload, 3);
    if (!PyTuple_CheckExact(points) || PyTuple_GET_SIZE(points) < 2 || PyTuple_GET_SIZE(points) > 512)
        throw nb::type_error("native line point metadata changed");
    for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(points); ++i) {
        PyObject* point = PyTuple_GET_ITEM(points, i);
        if (!PyTuple_CheckExact(point) || PyTuple_GET_SIZE(point) != 2 ||
            !PyLong_CheckExact(PyTuple_GET_ITEM(point, 0)) || !PyLong_CheckExact(PyTuple_GET_ITEM(point, 1)))
            throw nb::type_error("native line point shape changed");
        long offset = PyLong_AsLong(PyTuple_GET_ITEM(point, 0));
        long line = PyLong_AsLong(PyTuple_GET_ITEM(point, 1));
        if (PyErr_Occurred()) throw nb::python_error();
        if (offset < 0 || offset >= _PyCode_NBYTES(code) || offset % 2 || line < 0)
            throw nb::type_error("native line point range changed");
    }
    return nb::borrow<nb::object>(points);
}

struct LFGridLinePosition {
    PyFrameObject* frame;
    int previous_line;
    _Py_CODEUNIT* previous_instruction;
    _Py_CODEUNIT* projected_instruction;
    LFGridLinePosition(PyFrameObject* value, PyCodeObject* code, int offset, int line)
        : frame(value), previous_line(value->f_lineno) {
#if PY_VERSION_HEX >= 0x030D0000
        previous_instruction = value->f_frame->instr_ptr;
        projected_instruction = _PyCode_CODE(code) + offset / 2;
        value->f_frame->instr_ptr = projected_instruction;
#else
        previous_instruction = value->f_frame->prev_instr;
        projected_instruction = _PyCode_CODE(code) + offset / 2;
        value->f_frame->prev_instr = projected_instruction;
#endif
        value->f_lineno = line;
    }
    ~LFGridLinePosition() {
#if PY_VERSION_HEX >= 0x030D0000
        frame->f_frame->instr_ptr = previous_instruction;
#else
        frame->f_frame->prev_instr = previous_instruction;
#endif
        frame->f_lineno = previous_line;
    }
    bool jumped() const {
#if PY_VERSION_HEX >= 0x030D0000
        return frame->f_frame->instr_ptr != projected_instruction;
#else
        return frame->f_frame->prev_instr != projected_instruction;
#endif
    }
};

void lfgt_set_position(const LFGridFastLocals& locals,int offset,int line) {
#if PY_VERSION_HEX >= 0x030D0000
    locals.frame->f_frame->instr_ptr=_PyCode_CODE(locals.code)+offset/2;
#else
    locals.frame->f_frame->prev_instr=_PyCode_CODE(locals.code)+offset/2;
#endif
    locals.frame->f_lineno=line;
}

struct LFGridLineDriver {
    PyThreadState* thread = PyThreadState_Get();
    Py_tracefunc previous = thread->c_tracefunc;
    nb::object previous_object = nb::borrow<nb::object>(thread->c_traceobj);
    LFGridLineDriver* parent = lfgt_driver;
    Py_tracefunc callback = parent && previous == lfgt_proxy ? parent->callback : previous;
    Py_ssize_t adaptive_extra_index=lfga_index();
    std::map<PyFrameObject*,LFGridOpcodeState> states;
    LFGridLineDriver() {
        lfgt_driver = this;
        if (previous) PyEval_SetTrace(lfgt_proxy, previous_object.ptr());
    }
    ~LFGridLineDriver() {
        // A trace callback may replace or disable tracing. Preserve that choice.
        if (thread->c_tracefunc == lfgt_proxy)
            PyEval_SetTrace(previous, previous_object.ptr());
        lfgt_driver = parent;
    }
};

void lfgt_strip_transport(PyFrameObject* frame,const LFGridOpcodeState& state,PyObject* error,PyObject* object) {
    if(error!=state.reported_error || !PyTraceBack_Check(object)) return;
    auto* traceback=reinterpret_cast<PyTracebackObject*>(object);
    if(traceback->tb_frame==frame && reinterpret_cast<PyObject*>(traceback->tb_next)==state.reported_traceback) {
        if(PyException_SetTraceback(error,state.reported_traceback)<0) throw nb::python_error();
    }
}

void lfgt_callback_error(PyFrameObject* frame,int offset,int line,int event) {
    nb::python_error error;
    nb::object traceback=nb::steal<nb::object>(PyException_GetTraceback(error.value().ptr()));
    if(PyErr_Occurred())throw nb::python_error();
    nb::object previous=traceback.is_valid()?traceback:nb::none();
    nb::object actual=previous;
    // An exception-event callback failure leaves the already-unwinding source
    // frame without adding that frame to the callback error's traceback.
    bool omit_source=event==PyTrace_EXCEPTION;
#if PY_VERSION_HEX < 0x030C0000
    omit_source=omit_source || event==PyTrace_RETURN;
#endif
    if(!omit_source)
        actual=lh_check(PyObject_CallFunctionObjArgs(reinterpret_cast<PyObject*>(&PyTraceBack_Type),
            previous.ptr(),reinterpret_cast<PyObject*>(frame),nb::int_(offset).ptr(),nb::int_(line).ptr(),nullptr));
    if(PyException_SetTraceback(error.value().ptr(),actual.ptr())<0)throw nb::python_error();
    auto found=lfgt_driver->states.find(frame);
    if(found!=lfgt_driver->states.end()) {
        found->second.reported_error=error.value().ptr();found->second.reported_traceback=actual.ptr();
        found->second.escaping_error=true;found->second.error_point=-1;
    }
    error.restore();
}

int lfgt_dispatch_at(PyFrameObject* frame, int what, PyObject* arg,
                     PyCodeObject* code, int offset, int line) {
    LFGridLinePosition position(frame, code, offset, line);
    int result = lfgt_driver->callback(lfgt_driver->previous_object.ptr(), frame, what, arg);
    if(result<0)lfgt_callback_error(frame,offset,line,what);
    if (result == 0 && position.jumped()) {
        PyErr_SetString(PyExc_NotImplementedError, "native line pilot does not implement debugger jumps");
        return -1;
    }
    return result;
}

int lfgt_dispatch(PyFrameObject* frame, int what, PyObject* arg,
                  PyCodeObject* code, nb::handle points, int index) {
    if(index<0 || index>=PyTuple_GET_SIZE(points.ptr())) throw nb::type_error("native source point index changed");
    PyObject* point = PyTuple_GET_ITEM(points.ptr(), index);
    return lfgt_dispatch_at(frame,what,arg,code,
        static_cast<int>(PyLong_AsLong(PyTuple_GET_ITEM(point,0))),
        static_cast<int>(PyLong_AsLong(PyTuple_GET_ITEM(point,1))));
}

int lfgt_proxy(PyObject*, PyFrameObject* frame, int what, PyObject* arg) {
    try {
        if (!lfgt_driver || !lfgt_driver->callback) return 0;
        nb::object code = lh_check(reinterpret_cast<PyObject*>(PyFrame_GetCode(frame)));
        auto* actual = reinterpret_cast<PyCodeObject*>(code.ptr());
        nb::object points = lfgt_points(actual);
        if (!points.is_valid())
            return lfgt_driver->callback(lfgt_driver->previous_object.ptr(), frame, what, arg);
        if (what == PyTrace_CALL) lfgt_driver->states[frame] = LFGridOpcodeState{};
        // Suppress interop-shell events; native operations supply source events.
        if (what == PyTrace_LINE || what == PyTrace_OPCODE) return 0;
        if(what==PyTrace_EXCEPTION) {
            auto found=lfgt_driver->states.find(frame);
            if(found!=lfgt_driver->states.end() && found->second.reported_error && PyTuple_Check(arg) &&
               PyTuple_GET_SIZE(arg)==3 && PyTuple_GET_ITEM(arg,1)==found->second.reported_error) {
                // The interpreter prepends the thin transport frame after the
                // native handler has already supplied this source frame.
                lfgt_strip_transport(frame,found->second,PyTuple_GET_ITEM(arg,1),PyTuple_GET_ITEM(arg,2));
                return 0;
            }
        }
        if (what == PyTrace_RETURN) {
            auto found=lfgt_driver->states.find(frame);
            int point=found==lfgt_driver->states.end()?-1:found->second.return_point;
            int result;
            if(point<0 && found!=lfgt_driver->states.end() && found->second.return_offset>=0)
                result=lfgt_dispatch_at(frame,what,arg,actual,found->second.return_offset,found->second.return_line);
            else {
                if(point<0) point=static_cast<int>(PyTuple_GET_SIZE(points.ptr())-1);
                result=lfgt_dispatch(frame,what,arg,actual,points,point);
            }
            // 3.11 installs the interpreter traceback after the trace callback.
            // Retain escaping-error identity until the invocation boundary.
            if(found==lfgt_driver->states.end() || !found->second.escaping_error)
                lfgt_driver->states.erase(frame);
            return result;
        }
        return lfgt_driver->callback(lfgt_driver->previous_object.ptr(), frame, what, arg);
    } catch (...) { lf_adapter_error(); return -1; }
}

#include "grid_opcode_trace.inc"

void lfgt_line(const LFGridFastLocals& locals, int index) {
    if (!lfgt_driver || PyThreadState_Get()->tracing) return;
    nb::object points = lfgt_points(locals.code);
    if (!points.is_valid()) return;
    if(index<0 || index>=PyTuple_GET_SIZE(points.ptr())) throw nb::type_error("native source line index changed");
    lfgo_align(locals, PyLong_AsLong(PyTuple_GET_ITEM(PyTuple_GET_ITEM(points.ptr(), index), 0)));
    if (!lfgt_driver->callback || PyThreadState_Get()->c_tracefunc!=lfgt_proxy || !locals.frame->f_trace_lines) return;
    PyThreadState* thread = PyThreadState_Get();
    PyThreadState_EnterTracing(thread);
    int status;
    try { status = lfgt_dispatch(locals.frame, PyTrace_LINE, Py_None, locals.code, points, index); }
    catch (...) { PyThreadState_LeaveTracing(thread); throw; }
    PyThreadState_LeaveTracing(thread);
    if (status < 0) throw nb::python_error();
#if PY_VERSION_HEX < 0x030C0000
    // The legacy evaluator has already queued this instruction's opcode
    // callback when a line callback disables global tracing.
    if (!thread->c_tracefunc && locals.frame->f_trace_opcodes) {
        auto found=lfgt_driver->states.find(locals.frame);
        if(found!=lfgt_driver->states.end()) found->second.trailing_opcode=true;
    }
#endif
}

void lfgt_return(const LFGridFastLocals& locals,int index) {
    if(!lfgt_driver) return;
    auto found=lfgt_driver->states.find(locals.frame);
    if(found!=lfgt_driver->states.end()) found->second.return_point=index;
}
void lfgt_error_point(const LFGridFastLocals& locals,int point) {
    if(!lfgt_driver) return;
    auto found=lfgt_driver->states.find(locals.frame);
    if(found!=lfgt_driver->states.end()) found->second.error_point=point;
}
#include "grid_trace_exception.inc"
#include "grid_trace_next.inc"
struct LFGridGetterLines {
    const LFGridFastLocals& locals;
    void line(int source_line) const {lfgt_line(locals,source_line-146);}
    void point(int index) const {lfgt_line(locals,index);}
    nb::object call(int point,nb::object function,std::initializer_list<LHExpr> args={},
                    std::initializer_list<LHExpr> kw={},nb::object names={}) const {
        return LFGridObservedOps{locals}.call(std::move(function),args,kw,std::move(names),point);
    }
    void exception(nb::python_error& error,bool escaping=false) const {lfgt_exception(locals,error,escaping);}
    void enter_handler(nb::handle pending) const {
        lfgo_exception_entry(locals,"PUSH_EXC_INFO");
        try {lfgo_op(locals,"handled");}
        catch(nb::python_error&) {
            // The compiler's exceptional cleanup consumes the pending error
            // as its saved handled state when tracing fails before PUSH.
            PyErr_SetHandledException(pending.ptr());
            throw;
        }
    }
    void unwind(nb::handle previous) const {
        lfgo_exception_entry(locals,"COPY",true);lfgo_op(locals,"unhandled");
        PyErr_SetHandledException(previous.ptr());lfgo_op(locals,"reraise");
    }
    nb::object handler_return(nb::object result,int point,nb::handle previous) const {
        lfgo_op(locals,"unhandled");PyErr_SetHandledException(previous.ptr());
        return returned(std::move(result),point);
    }
    void error_point(int point) const {lfgt_error_point(locals,point);}
    void reraise(nb::python_error& error) const {
        lfgo_op(locals,"reraise");
        if(!lfgt_driver) return;
        auto found=lfgt_driver->states.find(locals.frame);
        if(found!=lfgt_driver->states.end() && found->second.reported_error==error.value().ptr()) {
            found->second.return_point=85;
            found->second.escaping_error=true;
        }
    }
    nb::object returned(nb::object result,int point) const {lfgo_op(locals,"return");lfgt_return(locals,point);return result;}
};

nb::object lfgt_prepare(nb::object replacement, nb::object original, nb::tuple profile, nb::tuple points,
                        nb::object instructions) {
    if (!PyCode_Check(replacement.ptr()) || !PyCode_Check(original.ptr()) ||
        !lf_grid_profile_code(original, profile) || points.size() < 2 || points.size() > 512)
        throw nb::type_error("complete grid source compiler controls required");
    auto* source = reinterpret_cast<PyCodeObject*>(original.ptr());
    auto* shell = reinterpret_cast<PyCodeObject*>(replacement.ptr());
    bool indices=PyUnicode_CompareWithASCIIString(source->co_name,"indices")==0 && shell->co_nlocals==9;
    bool getter=PyUnicode_CompareWithASCIIString(source->co_name,"__getitem__")==0 &&
        (shell->co_nlocals==14 || shell->co_nlocals==16);
    bool child=PyUnicode_CompareWithASCIIString(source->co_name,"<listcomp>")==0 && shell->co_nlocals==3;
    if(!indices && !getter && !child) throw nb::type_error("unsupported native grid trace declaration");
    if(points.size()!=(indices?16:getter?89:5)) throw nb::type_error("native source point declaration changed");
    nb::object bytecode = lh_check(PyObject_GetAttrString(original.ptr(), "co_code"));
    nb::object transport = lh_check(PyObject_GetAttrString(replacement.ptr(), "co_code"));
    Py_ssize_t length = PyBytes_GET_SIZE(bytecode.ptr()), size = PyBytes_GET_SIZE(transport.ptr());
    if (length < size || length % 2) throw nb::type_error("source position storage is too short");
    for (nb::handle point : points) {
        if (!PyTuple_CheckExact(point.ptr()) || PyTuple_GET_SIZE(point.ptr()) != 2 ||
            !PyLong_CheckExact(PyTuple_GET_ITEM(point.ptr(), 0)) || !PyLong_CheckExact(PyTuple_GET_ITEM(point.ptr(), 1)))
            throw nb::type_error("source line points must be offset/line pairs");
        long offset = PyLong_AsLong(PyTuple_GET_ITEM(point.ptr(), 0));
        long line = PyLong_AsLong(PyTuple_GET_ITEM(point.ptr(), 1));
        if (offset < 0 || offset >= length || offset % 2 || PyCode_Addr2Line(source, static_cast<int>(offset)) != line)
            throw nb::type_error("source line point disagrees with compiler metadata");
    }
    // Pad only with inert NOPs. No original executable policy is retained.
    std::string bytes(PyBytes_AS_STRING(transport.ptr()), static_cast<size_t>(size));
    while (static_cast<Py_ssize_t>(bytes.size()) < length) { bytes.push_back(9); bytes.push_back(0); }
    Py_ssize_t count = PyTuple_GET_SIZE(shell->co_consts);
    PyObject* old = PyTuple_GET_ITEM(shell->co_consts, count - 1);
    if (!PyTuple_CheckExact(old) || PyTuple_GET_SIZE(old) != 3)
        throw nb::type_error("prepared grid source payload required");
    nb::object constants = lh_check(PySequence_List(shell->co_consts));
    nb::object payload;
    if (instructions.is_none()) payload = nb::make_tuple(nb::handle(PyTuple_GET_ITEM(old, 0)),
        nb::handle(PyTuple_GET_ITEM(old, 1)), nb::handle(PyTuple_GET_ITEM(old, 2)), points);
    else {
        lfgo_validate(source, instructions);
        instructions=lfgo_with_lines(source,instructions);
        payload = nb::make_tuple(nb::handle(PyTuple_GET_ITEM(old, 0)),nb::handle(PyTuple_GET_ITEM(old, 1)),
            nb::handle(PyTuple_GET_ITEM(old, 2)), points, instructions);
    }
    if (PyList_SetItem(constants.ptr(), count - 1, payload.release().ptr()) < 0) throw nb::python_error();
    nb::dict keywords;
    keywords["co_code"] = nb::bytes(bytes.data(), bytes.size());
    keywords["co_linetable"] = nb::borrow<nb::object>(source->co_linetable);
    keywords["co_consts"] = lh_check(PySequence_Tuple(constants.ptr()));
    nb::object replace = lh_check(PyObject_GetAttrString(replacement.ptr(), "replace"));
    nb::object arguments = nb::tuple();
    nb::object result=lh_check(PyObject_Call(replace.ptr(), arguments.ptr(), keywords.ptr()));
    if(!instructions.is_none())lfga_prepare(source,reinterpret_cast<PyCodeObject*>(result.ptr()),instructions);
    return result;
}

void lfgt_exports(nb::module_& module) {
    module.def("prepare_grid_line_transport_probe", &lfgt_prepare,nb::arg("replacement"),nb::arg("original"),
        nb::arg("profile"),nb::arg("points"),nb::arg("instructions").none()=nb::none());
    module.def("call_grid_line_transport_probe", [](nb::object function, nb::tuple arguments) {
        LFGridLineDriver scope;
        try {return lh_check(PyObject_CallObject(function.ptr(), arguments.ptr()));}
        catch(nb::python_error& error) {
            // A callback may disable tracing before the interpreter unwinds
            // the transport shell, so its exception proxy never gets called.
            nb::object traceback=nb::steal<nb::object>(PyException_GetTraceback(error.value().ptr()));
            if(PyErr_Occurred()) throw nb::python_error();
            if(traceback.is_valid()) for(const auto& entry:scope.states)
                lfgt_strip_transport(entry.first,entry.second,error.value().ptr(),traceback.ptr());
            throw;
        }
    });
}
