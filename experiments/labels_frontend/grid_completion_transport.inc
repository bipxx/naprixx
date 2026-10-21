// Experimental completion-only VM bridge. Native policy chooses the endpoint;
// the bridge carries its result to that genuine frame's source RETURN_VALUE.
nb::object lfgr_routes(PyCodeObject* code) {
    Py_ssize_t size=PyTuple_GET_SIZE(code->co_consts);
    if(size<2)return {};
    PyObject* marker=PyTuple_GET_ITEM(code->co_consts,size-2);
    if(!PyTuple_CheckExact(marker) || PyTuple_GET_SIZE(marker)!=2)return {};
    PyObject* tag=PyTuple_GET_ITEM(marker,0);
    if(!PyUnicode_CheckExact(tag) || PyUnicode_CompareWithASCIIString(tag,"naparixx.grid.return.v1"))return {};
    PyObject* routes=PyTuple_GET_ITEM(marker,1);
    if(!PyTuple_CheckExact(routes) || (PyTuple_GET_SIZE(routes)!=1 && PyTuple_GET_SIZE(routes)!=4))
        throw nb::type_error("native completion routes changed");
    return nb::borrow<nb::object>(routes);
}

PyObject* lfgr_at(PyObject*,PyObject* requested) {
    try {
        if(!PyLong_CheckExact(requested))throw nb::type_error("native completion route must be an integer");
        PyFrameObject* frame=PyEval_GetFrame();
        if(!frame)throw nb::type_error("native completion frame is missing");
        nb::object owner_code=lh_check(reinterpret_cast<PyObject*>(PyFrame_GetCode(frame)));
        auto* code=reinterpret_cast<PyCodeObject*>(owner_code.ptr());
        nb::object routes=lfgr_routes(code);
        if(!routes.is_valid() || PyTuple_GET_SIZE(routes.ptr())!=4)
            throw nb::type_error("native completion frame declaration changed");
        // The bridge leaves the sole result carrier at the bottom of the VM
        // operand stack. No source local or owning frame reference is added.
        PyObject*& slot=frame->f_frame->localsplus[code->co_nlocalsplus];
        if(!slot || !PyTuple_CheckExact(slot) || PyTuple_GET_SIZE(slot)!=2 ||
           !PyLong_CheckExact(PyTuple_GET_ITEM(slot,1)))
            throw nb::type_error("native completion result carrier changed");
        PyObject* selected=PyTuple_GET_ITEM(slot,1);
        int admitted=PySequence_Contains(routes.ptr(),selected);
        if(admitted<0)throw nb::python_error();
        if(!admitted)throw nb::type_error("native completion endpoint changed");
        int equal=PyObject_RichCompareBool(selected,requested,Py_EQ);
        if(equal<0)throw nb::python_error();
        if(!equal)Py_RETURN_FALSE;
        PyObject* carrier=slot;
        slot=Py_NewRef(PyTuple_GET_ITEM(carrier,0));
        Py_DECREF(carrier);
        Py_RETURN_TRUE;
    } catch(...) {lf_adapter_error();return nullptr;}
}
PyMethodDef lfgr_at_definition={"_labels_frontend_grid_return_at",lfgr_at,METH_O,nullptr};

void lfgr_jump(std::string& bytes,Py_ssize_t at,Py_ssize_t target) {
    if(at<0 || at+8>static_cast<Py_ssize_t>(bytes.size()) || target<at+8 || target%2)
        throw nb::type_error("native completion jump does not fit its declaration");
    uint32_t distance=static_cast<uint32_t>((target-at-8)/2);
    for(int i=0;i<3;++i) {
        bytes[at+2*i]=static_cast<char>(EXTENDED_ARG);
        bytes[at+2*i+1]=static_cast<char>((distance>>(24-8*i))&255);
    }
    bytes[at+6]=static_cast<char>(JUMP_FORWARD);bytes[at+7]=static_cast<char>(distance&255);
}

void lfgr_prepare(std::string& bytes,nb::object& constants,PyCodeObject* source,PyCodeObject* shell,
                  nb::handle points,nb::handle completion,int& stacksize) {
    if(!PyTuple_CheckExact(completion.ptr()) || PyTuple_GET_SIZE(completion.ptr())!=2 ||
       !PyCode_Check(PyTuple_GET_ITEM(completion.ptr(),0)) || !PyTuple_CheckExact(PyTuple_GET_ITEM(completion.ptr(),1)))
        throw nb::type_error("complete native return bridge declaration required");
    auto* donor=reinterpret_cast<PyCodeObject*>(PyTuple_GET_ITEM(completion.ptr(),0));
    if(!lf_grid_profile_code(nb::handle(reinterpret_cast<PyObject*>(donor)),nb::borrow<nb::tuple>(PyTuple_GET_ITEM(completion.ptr(),1))) ||
       donor->co_nlocals || donor->co_argcount || donor->co_nlocalsplus)
        throw nb::type_error("native return bridge profile changed");
    std::vector<int> indices;
    Py_ssize_t count=PyTuple_GET_SIZE(points.ptr());
    if(count==89)indices={73,74,75,76};else if(count==16)indices={15};else if(count==5)indices={4};
    else throw nb::type_error("native completion source family changed");
    nb::object source_bytes=lh_check(PyObject_GetAttrString(reinterpret_cast<PyObject*>(source),"co_code"));
    auto endpoint=[&](int index) {
        Py_ssize_t target=PyLong_AsSsize_t(PyTuple_GET_ITEM(PyTuple_GET_ITEM(points.ptr(),index),0));
        if(PyErr_Occurred())throw nb::python_error();
        if(target<0 || target+2>static_cast<Py_ssize_t>(bytes.size()) || target%2 ||
           static_cast<unsigned char>(PyBytes_AS_STRING(source_bytes.ptr())[target])!=RETURN_VALUE)
            throw nb::type_error("native completion source return changed");
        return target;
    };
    Py_ssize_t at=_PyCode_NBYTES(shell)-2;
    if(at<2 || static_cast<unsigned char>(bytes[at])!=RETURN_VALUE)
        throw nb::type_error("native completion shell return changed");
    nb::object payload=nb::borrow<nb::object>(PyList_GET_ITEM(constants.ptr(),PyList_GET_SIZE(constants.ptr())-1));
    if(PySequence_DelItem(constants.ptr(),PyList_GET_SIZE(constants.ptr())-1)<0)throw nb::python_error();
    if(indices.size()==1)lfgr_jump(bytes,at,endpoint(indices[0]));
    else {
        nb::object raw=lh_check(PyObject_GetAttrString(reinterpret_cast<PyObject*>(donor),"co_code"));
        std::string transport(PyBytes_AS_STRING(raw.ptr()),PyBytes_GET_SIZE(raw.ptr()));
        if(transport.size()<2 || static_cast<unsigned char>(transport[0])!=RESUME ||
           at+static_cast<Py_ssize_t>(transport.size())-2>=endpoint(indices[0]))
            throw nb::type_error("native completion bridge does not fit source storage");
        nb::object selector=lh_check(PyCFunction_NewEx(&lfgr_at_definition,nullptr,nullptr));
        Py_ssize_t base=PyList_GET_SIZE(constants.ptr());
        if(base+PyTuple_GET_SIZE(donor->co_consts)>256)throw nb::type_error("native completion constants overflow");
        for(Py_ssize_t i=0;i<PyTuple_GET_SIZE(donor->co_consts);++i) {
            PyObject* value=PyTuple_GET_ITEM(donor->co_consts,i);
            if(PyList_Append(constants.ptr(),PyCode_Check(value)?selector.ptr():value)<0)throw nb::python_error();
        }
        size_t route=0;
        for(Py_ssize_t i=2;i<static_cast<Py_ssize_t>(transport.size());i+=2) {
            int opcode=static_cast<unsigned char>(transport[i]);
            if(opcode==LOAD_CONST
#if PY_VERSION_HEX >= 0x030C0000
               || opcode==RETURN_CONST
#endif
            )transport[i+1]=static_cast<char>(base+static_cast<unsigned char>(transport[i+1]));
            if(opcode==MAKE_FUNCTION) {
                if(transport[i+1])throw nb::type_error("native completion closure changed");
                transport[i]=static_cast<char>(NOP);
            }
            if(opcode==NOP && i+8<=static_cast<Py_ssize_t>(transport.size()) &&
               static_cast<unsigned char>(transport[i+2])==NOP && static_cast<unsigned char>(transport[i+4])==NOP) {
                if(route>=indices.size())throw nb::type_error("native completion route count changed");
                int returned=static_cast<unsigned char>(transport[i+6]);
#if PY_VERSION_HEX >= 0x030C0000
                bool return_shape=returned==RETURN_CONST;
#else
                bool return_shape=returned==LOAD_CONST && i+10<=static_cast<Py_ssize_t>(transport.size()) &&
                    static_cast<unsigned char>(transport[i+8])==RETURN_VALUE;
#endif
                int constant=static_cast<unsigned char>(transport[i+7]);
                if(!return_shape || constant>=PyTuple_GET_SIZE(donor->co_consts) ||
                   PyTuple_GET_ITEM(donor->co_consts,constant)!=Py_None)
                    throw nb::type_error("native completion return block changed");
                lfgr_jump(bytes,at+i-2,endpoint(indices[route++]));
                i+=6;
            }
        }
        if(route!=indices.size())throw nb::type_error("native completion routes are incomplete");
        // Copy ordinary donor instructions without replacing the checked jumps.
        for(Py_ssize_t i=2;i<static_cast<Py_ssize_t>(transport.size());i+=2) {
            Py_ssize_t target=at+i-2;
            if(static_cast<unsigned char>(bytes[target])==EXTENDED_ARG) {i+=6;continue;}
            bytes[target]=transport[i];bytes[target+1]=transport[i+1];
        }
        stacksize+=donor->co_stacksize;
    }
    for(int index:indices)bytes[endpoint(index)]=static_cast<char>(RETURN_VALUE);
    nb::object routes=lh_check(PyTuple_New(indices.size()));
    for(size_t i=0;i<indices.size();++i)PyTuple_SET_ITEM(routes.ptr(),i,nb::int_(indices[i]).release().ptr());
    nb::object marker=nb::make_tuple(nb::str("naparixx.grid.return.v1"),routes);
    if(PyList_Append(constants.ptr(),marker.ptr())<0 || PyList_Append(constants.ptr(),payload.ptr())<0)throw nb::python_error();
}

void lfgr_error_completion(const LFGridFastLocals& locals,LFGridLinePosition& physical) {
    if(!lfgt_driver || !lfgr_routes(locals.code).is_valid())return;
    auto found=lfgt_driver->states.find(locals.frame);if(found==lfgt_driver->states.end())return;
    int point=found->second.return_point;
    if(point>=0) {
        nb::object points=lfgt_points(locals.code);PyObject* row=PyTuple_GET_ITEM(points.ptr(),point);
        lfgt_set_position(locals,static_cast<int>(PyLong_AsLong(PyTuple_GET_ITEM(row,0))),
            static_cast<int>(PyLong_AsLong(PyTuple_GET_ITEM(row,1))));
    } else if(found->second.return_offset>=0)
        lfgt_set_position(locals,found->second.return_offset,found->second.return_line);
    else return;
    physical.restore=false;
}
