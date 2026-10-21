// Opt-in native source-line transport pilot. No application startup integration.
struct LFGridLineDriver;
thread_local LFGridLineDriver* lfgt_driver = nullptr;
int lfgt_proxy(PyObject*, PyFrameObject*, int, PyObject*);

nb::object lfgt_points(PyCodeObject* code) {
    PyObject* constants = code->co_consts;
    if (PyTuple_GET_SIZE(constants) == 0) return {};
    PyObject* payload = PyTuple_GET_ITEM(constants, PyTuple_GET_SIZE(constants) - 1);
    if (!PyTuple_CheckExact(payload) || PyTuple_GET_SIZE(payload) != 4) return {};
    PyObject* tag = PyTuple_GET_ITEM(payload, 0);
    if (!PyUnicode_CheckExact(tag) || PyUnicode_CompareWithASCIIString(tag, "naparixx.grid.v2") != 0) return {};
    PyObject* points = PyTuple_GET_ITEM(payload, 3);
    if (!PyTuple_CheckExact(points) || PyTuple_GET_SIZE(points) != 16)
        throw nb::type_error("native line point metadata changed");
    for (Py_ssize_t i = 0; i < 16; ++i) {
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

struct LFGridLineDriver {
    PyThreadState* thread = PyThreadState_Get();
    Py_tracefunc previous = thread->c_tracefunc;
    nb::object previous_object = nb::borrow<nb::object>(thread->c_traceobj);
    LFGridLineDriver* parent = lfgt_driver;
    Py_tracefunc callback = parent && previous == lfgt_proxy ? parent->callback : previous;
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

int lfgt_dispatch(PyFrameObject* frame, int what, PyObject* arg,
                  PyCodeObject* code, nb::handle points, int index) {
    PyObject* point = PyTuple_GET_ITEM(points.ptr(), index);
    int offset = static_cast<int>(PyLong_AsLong(PyTuple_GET_ITEM(point, 0)));
    int line = static_cast<int>(PyLong_AsLong(PyTuple_GET_ITEM(point, 1)));
    LFGridLinePosition position(frame, code, offset, line);
    int result = lfgt_driver->callback(lfgt_driver->previous_object.ptr(), frame, what, arg);
    if (result == 0 && position.jumped()) {
        PyErr_SetString(PyExc_NotImplementedError, "native line pilot does not implement debugger jumps");
        return -1;
    }
    return result;
}

int lfgt_proxy(PyObject*, PyFrameObject* frame, int what, PyObject* arg) {
    try {
        if (!lfgt_driver || !lfgt_driver->callback) return 0;
        nb::object code = lh_check(reinterpret_cast<PyObject*>(PyFrame_GetCode(frame)));
        auto* actual = reinterpret_cast<PyCodeObject*>(code.ptr());
        nb::object points = lfgt_points(actual);
        if (!points.is_valid())
            return lfgt_driver->callback(lfgt_driver->previous_object.ptr(), frame, what, arg);
        // Suppress interop-shell events; native statements supply source lines.
        if (what == PyTrace_LINE || what == PyTrace_OPCODE) return 0;
        if (what == PyTrace_RETURN)
            return lfgt_dispatch(frame, what, arg, actual, points, 15);
        return lfgt_driver->callback(lfgt_driver->previous_object.ptr(), frame, what, arg);
    } catch (...) { lf_adapter_error(); return -1; }
}

void lfgt_line(const LFGridFastLocals& locals, int index) {
    if (!lfgt_driver || !lfgt_driver->callback ||
        PyThreadState_Get()->c_tracefunc != lfgt_proxy || PyThreadState_Get()->tracing ||
        !locals.frame->f_trace_lines) return;
    nb::object points = lfgt_points(locals.code);
    if (!points.is_valid()) return;
    PyThreadState* thread = PyThreadState_Get();
    PyThreadState_EnterTracing(thread);
    int status;
    try { status = lfgt_dispatch(locals.frame, PyTrace_LINE, Py_None, locals.code, points, index); }
    catch (...) { PyThreadState_LeaveTracing(thread); throw; }
    PyThreadState_LeaveTracing(thread);
    if (status < 0) throw nb::python_error();
}

nb::object lfgt_prepare(nb::object replacement, nb::object original, nb::tuple profile, nb::tuple points) {
    if (!PyCode_Check(replacement.ptr()) || !PyCode_Check(original.ptr()) ||
        !lf_grid_profile_code(original, profile) || points.size() != 16)
        throw nb::type_error("complete indices source compiler controls required");
    auto* source = reinterpret_cast<PyCodeObject*>(original.ptr());
    auto* shell = reinterpret_cast<PyCodeObject*>(replacement.ptr());
    if (PyUnicode_CompareWithASCIIString(source->co_name, "indices") != 0 || shell->co_nlocals != 9)
        throw nb::type_error("line transport currently owns indices only");
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
    nb::object payload = nb::make_tuple(nb::handle(PyTuple_GET_ITEM(old, 0)),
        nb::handle(PyTuple_GET_ITEM(old, 1)), nb::handle(PyTuple_GET_ITEM(old, 2)), points);
    if (PyList_SetItem(constants.ptr(), count - 1, payload.release().ptr()) < 0) throw nb::python_error();
    nb::dict keywords;
    keywords["co_code"] = nb::bytes(bytes.data(), bytes.size());
    keywords["co_linetable"] = nb::borrow<nb::object>(source->co_linetable);
    keywords["co_consts"] = lh_check(PySequence_Tuple(constants.ptr()));
    nb::object replace = lh_check(PyObject_GetAttrString(replacement.ptr(), "replace"));
    nb::object arguments = nb::tuple();
    return lh_check(PyObject_Call(replace.ptr(), arguments.ptr(), keywords.ptr()));
}

void lfgt_exports(nb::module_& module) {
    module.def("prepare_grid_line_transport_probe", &lfgt_prepare);
    module.def("call_grid_line_transport_probe", [](nb::object function, nb::tuple arguments) {
        LFGridLineDriver scope;
        return lh_check(PyObject_CallObject(function.ptr(), arguments.ptr()));
    });
}
