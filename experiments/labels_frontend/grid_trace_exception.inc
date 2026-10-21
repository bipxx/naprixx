// Report a native-caught error at its checked source call position before
// entering the source except handler. The original policy is never executed.
void lfgt_exception(const LFGridFastLocals& locals,nb::python_error& error) {
    if(!lfgt_driver || PyThreadState_Get()->c_tracefunc!=lfgt_proxy || PyThreadState_Get()->tracing) return;
    auto found=lfgt_driver->states.find(locals.frame);
    if(found==lfgt_driver->states.end() || found->second.error_point<0) return;
    nb::object points=lfgt_points(locals.code);
    if(!points.is_valid()) return;
    int point=found->second.error_point;
    PyObject* position=PyTuple_GET_ITEM(points.ptr(),point);
    nb::object traceback=nb::steal<nb::object>(PyException_GetTraceback(error.value().ptr()));
    if(PyErr_Occurred()) throw nb::python_error();
    nb::object previous=traceback.is_valid()?traceback:nb::none();
    nb::object target=nb::borrow<nb::object>(reinterpret_cast<PyObject*>(locals.frame));
    nb::object actual=lh_check(PyObject_CallFunctionObjArgs(reinterpret_cast<PyObject*>(&PyTraceBack_Type),
        previous.ptr(),target.ptr(),PyTuple_GET_ITEM(position,0),PyTuple_GET_ITEM(position,1),nullptr));
    if(PyException_SetTraceback(error.value().ptr(),actual.ptr())<0) throw nb::python_error();
    found->second.reported_error=error.value().ptr();
    found->second.return_point=point;
    nb::object argument=nb::make_tuple(nb::handle(reinterpret_cast<PyObject*>(Py_TYPE(error.value().ptr()))),error.value(),actual);
    PyThreadState* thread=PyThreadState_Get();PyThreadState_EnterTracing(thread);
    int status;
    try {status=lfgt_dispatch(locals.frame,PyTrace_EXCEPTION,argument.ptr(),locals.code,points,point);}
    catch(...) {PyThreadState_LeaveTracing(thread);throw;}
    PyThreadState_LeaveTracing(thread);
    if(status<0) throw nb::python_error();
}
