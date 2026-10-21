// PyIter_Next suppresses StopIteration. A traced FOR_ITER reports an exception
// raised by a Python iterator before suppressing it, so retain that distinction.
nb::object lfgt_next(const LFGridFastLocals& locals,nb::handle iterator,int point) {
    if(!lfgt_driver || PyThreadState_Get()->c_tracefunc!=lfgt_proxy || PyThreadState_Get()->tracing ||
       !lfgt_points(locals.code).is_valid()) return lh_next(iterator);
    PyObject* result=Py_TYPE(iterator.ptr())->tp_iternext(iterator.ptr());
    if(result) return nb::steal<nb::object>(result);
    if(!PyErr_Occurred()) return {};
    nb::python_error error;
    if(!error.matches(PyExc_StopIteration)) throw error;
    nb::object traceback=nb::steal<nb::object>(PyException_GetTraceback(error.value().ptr()));
    if(PyErr_Occurred()) throw nb::python_error();
    nb::object argument=nb::make_tuple(nb::handle(reinterpret_cast<PyObject*>(Py_TYPE(error.value().ptr()))),
        error.value(),traceback.is_valid()?traceback:nb::none());
    nb::object points=lfgt_points(locals.code);
    PyThreadState* thread=PyThreadState_Get();
    PyThreadState_EnterTracing(thread);
    int status;
    try {status=lfgt_dispatch(locals.frame,PyTrace_EXCEPTION,argument.ptr(),locals.code,points,point);}
    catch(...) {PyThreadState_LeaveTracing(thread);throw;}
    PyThreadState_LeaveTracing(thread);
    if(status<0) throw nb::python_error();
    return {};
}
