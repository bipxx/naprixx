// Experimental completion-only VM bridge. Native policy chooses the endpoint;
// the bridge carries its result to that genuine frame's source RETURN_VALUE.
nb::object lfgr_routes(PyCodeObject* code) {
    Py_ssize_t size=PyTuple_GET_SIZE(code->co_consts);
    if(size<2)return {};
    PyObject* marker=PyTuple_GET_ITEM(code->co_consts,size-2);
    if(!PyTuple_CheckExact(marker) || PyTuple_GET_SIZE(marker)!=2)return {};
    PyObject* tag=PyTuple_GET_ITEM(marker,0);
    if(!PyUnicode_CheckExact(tag) || PyUnicode_CompareWithASCIIString(tag,"naparixx.grid.return.v2"))return {};
    PyObject* routes=PyTuple_GET_ITEM(marker,1);
    if(!PyTuple_CheckExact(routes) || (PyTuple_GET_SIZE(routes)!=1 && PyTuple_GET_SIZE(routes)!=4))
        throw nb::type_error("native completion routes changed");
    for(Py_ssize_t i=0;i<PyTuple_GET_SIZE(routes);++i) {
        PyObject* value=PyTuple_GET_ITEM(routes,i);
        if(!PyLong_CheckExact(value))throw nb::type_error("native completion route type changed");
        long point=PyLong_AsLong(value);if(PyErr_Occurred())throw nb::python_error();
        if((PyTuple_GET_SIZE(routes)==4 && point!=73+i) || (PyTuple_GET_SIZE(routes)==1 && point!=4 && point!=15))
            throw nb::type_error("native completion route declaration changed");
    }
    return nb::borrow<nb::object>(routes);
}

PyObject* lfgr_at(PyObject*,PyObject* requested) {
    try {
        if(!PyLong_CheckExact(requested))throw nb::type_error("native completion route must be an integer");
        long request=PyLong_AsLong(requested);if(PyErr_Occurred())throw nb::python_error();
        PyFrameObject* frame=PyEval_GetFrame();
        if(!frame)throw nb::type_error("native completion frame is missing");
        nb::object owner_code=lh_check(reinterpret_cast<PyObject*>(PyFrame_GetCode(frame)));
        auto* code=reinterpret_cast<PyCodeObject*>(owner_code.ptr());
        nb::object routes=lfgr_routes(code);
        if(!routes.is_valid())throw nb::type_error("native completion frame declaration changed");
        // The sole carrier occupies the canonical VM operand stack. Error
        // probing transfers lasti below the exception for RERAISE 1.
        PyObject*& slot=frame->f_frame->localsplus[code->co_nlocalsplus];
        if(!slot || !PyTuple_CheckExact(slot) || PyTuple_GET_SIZE(slot)!=3 ||
           !PyLong_CheckExact(PyTuple_GET_ITEM(slot,1)) || !PyBool_Check(PyTuple_GET_ITEM(slot,2)))
            throw nb::type_error("native completion result carrier changed");
        PyObject* selected=PyTuple_GET_ITEM(slot,1);
        bool failed=PyTuple_GET_ITEM(slot,2)==Py_True;
        if(request==-1) {
            if(failed) {
                long offset=PyLong_AsLong(selected);if(PyErr_Occurred())throw nb::python_error();
                if(offset<0 || offset>=_PyCode_NBYTES(code)/2 || !PyExceptionInstance_Check(PyTuple_GET_ITEM(slot,0)))
                    throw nb::type_error("native error completion changed");
                PyObject* carrier=slot;
                PyObject* exception=Py_NewRef(PyTuple_GET_ITEM(carrier,0));
                slot=Py_NewRef(selected);Py_DECREF(carrier);return exception;
            }
            if(PyTuple_GET_SIZE(routes.ptr())==1) {
                PyObject* carrier=slot;slot=Py_NewRef(PyTuple_GET_ITEM(carrier,0));Py_DECREF(carrier);
            }
            Py_RETURN_NONE;
        }
        if(failed)throw nb::type_error("native error completion was not dispatched");
        int admitted=PySequence_Contains(routes.ptr(),selected);
        if(admitted<0)throw nb::python_error();
        if(!admitted)throw nb::type_error("native completion endpoint changed");
        int equal=PyObject_RichCompareBool(selected,requested,Py_EQ);
        if(equal<0)throw nb::python_error();
        if(!equal)Py_RETURN_FALSE;
        PyObject* carrier=slot;slot=Py_NewRef(PyTuple_GET_ITEM(carrier,0));Py_DECREF(carrier);
        Py_RETURN_TRUE;
    } catch(...) {lf_adapter_error();return nullptr;}
}
PyMethodDef lfgr_at_definition={"_labels_frontend_grid_return_at",lfgr_at,METH_O,nullptr};

void lfgr_jump(std::string& bytes,Py_ssize_t at,Py_ssize_t target) {
    if(at<0 || at+8>static_cast<Py_ssize_t>(bytes.size()) || target<0 || target%2)
        throw nb::type_error("native completion jump does not fit its declaration");
    bool forward=target>=at+8;
    uint32_t distance=static_cast<uint32_t>((forward?target-at-8:at+8-target)/2);
    for(int i=0;i<3;++i) {
        bytes[at+2*i]=static_cast<char>(EXTENDED_ARG);
        bytes[at+2*i+1]=static_cast<char>((distance>>(24-8*i))&255);
    }
    bytes[at+6]=static_cast<char>(forward?JUMP_FORWARD:JUMP_BACKWARD_NO_INTERRUPT);
    bytes[at+7]=static_cast<char>(distance&255);
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
        if(target<0 || target+2>PyBytes_GET_SIZE(source_bytes.ptr()) || target%2 ||
           static_cast<unsigned char>(PyBytes_AS_STRING(source_bytes.ptr())[target])!=RETURN_VALUE)
            throw nb::type_error("native completion source return changed");
        return target;
    };
    Py_ssize_t at=_PyCode_NBYTES(shell)-2;
    if(at<2 || static_cast<unsigned char>(bytes[at])!=RETURN_VALUE)
        throw nb::type_error("native completion shell return changed");
    nb::object payload=nb::borrow<nb::object>(PyList_GET_ITEM(constants.ptr(),PyList_GET_SIZE(constants.ptr())-1));
    if(PySequence_DelItem(constants.ptr(),PyList_GET_SIZE(constants.ptr())-1)<0)throw nb::python_error();
    nb::object raw=lh_check(PyObject_GetAttrString(reinterpret_cast<PyObject*>(donor),"co_code"));
    std::string transport(PyBytes_AS_STRING(raw.ptr()),PyBytes_GET_SIZE(raw.ptr()));
    if(transport.size()<2 || static_cast<unsigned char>(transport[0])!=RESUME)
        throw nb::type_error("native completion bridge entry changed");
    Py_ssize_t conditional=-1;
    for(Py_ssize_t i=2;i<static_cast<Py_ssize_t>(transport.size());i+=2) {
#if PY_VERSION_HEX >= 0x030C0000
        bool match=static_cast<unsigned char>(transport[i])==POP_JUMP_IF_NONE;
#else
        bool match=static_cast<unsigned char>(transport[i])==POP_JUMP_FORWARD_IF_NONE;
#endif
        if(match) {conditional=i;break;}
    }
    if(conditional<0)throw nb::type_error("native completion error probe changed");
    transport.insert(conditional,std::string({static_cast<char>(COPY),static_cast<char>(1)}));
    nb::object selector=lh_check(PyCFunction_NewEx(&lfgr_at_definition,nullptr,nullptr));
    Py_ssize_t base=PyList_GET_SIZE(constants.ptr());
    if(base+PyTuple_GET_SIZE(donor->co_consts)>256)throw nb::type_error("native completion constants overflow");
    for(Py_ssize_t i=0;i<PyTuple_GET_SIZE(donor->co_consts);++i) {
        PyObject* value=PyTuple_GET_ITEM(donor->co_consts,i);
        if(PyList_Append(constants.ptr(),PyCode_Check(value)?selector.ptr():value)<0)throw nb::python_error();
    }
    std::vector<Py_ssize_t> blocks;bool error_block=false,drop=false;
    for(Py_ssize_t i=2;i<static_cast<Py_ssize_t>(transport.size());i+=2) {
        int opcode=static_cast<unsigned char>(transport[i]);
        if(opcode==NOP && i+8<=static_cast<Py_ssize_t>(transport.size()) &&
           static_cast<unsigned char>(transport[i+2])==NOP && static_cast<unsigned char>(transport[i+4])==NOP) {
            int returned=static_cast<unsigned char>(transport[i+6]);
#if PY_VERSION_HEX >= 0x030C0000
            bool return_shape=returned==RETURN_CONST;int span=8;
#else
            bool return_shape=returned==LOAD_CONST && i+10<=static_cast<Py_ssize_t>(transport.size()) &&
                static_cast<unsigned char>(transport[i+8])==RETURN_VALUE;int span=10;
#endif
            int constant=static_cast<unsigned char>(transport[i+7]);
            if(!return_shape || constant>=PyTuple_GET_SIZE(donor->co_consts) || PyTuple_GET_ITEM(donor->co_consts,constant)!=Py_None)
                throw nb::type_error("native completion return block changed");
            if(!error_block) {
                for(int j=0;j<span;j+=2) {transport[i+j]=static_cast<char>(NOP);transport[i+j+1]=0;}
                transport[i]=static_cast<char>(RERAISE);transport[i+1]=1;error_block=true;
            } else blocks.push_back(i);
            i+=span-2;continue;
        }
        if(error_block && !drop && opcode==NOP) {
            transport[i]=static_cast<char>(POP_TOP);drop=true;
            if(indices.size()==1) {transport.resize(i+2+8,static_cast<char>(0));blocks.push_back(i+2);break;}
        }
        if(opcode==LOAD_CONST
#if PY_VERSION_HEX >= 0x030C0000
           || opcode==RETURN_CONST
#endif
        )transport[i+1]=static_cast<char>(base+static_cast<unsigned char>(transport[i+1]));
        if(opcode==MAKE_FUNCTION) {
            if(transport[i+1])throw nb::type_error("native completion closure changed");
            transport[i]=static_cast<char>(NOP);
        }
    }
    if(!error_block || !drop || blocks.size()!=indices.size())throw nb::type_error("native completion routes are incomplete");
    Py_ssize_t bridge=static_cast<Py_ssize_t>(bytes.size());
    bytes.append(transport.data()+2,transport.size()-2);
    lfgr_jump(bytes,at,bridge);
    for(size_t i=0;i<blocks.size();++i)lfgr_jump(bytes,bridge+blocks[i]-2,endpoint(indices[i]));
    stacksize+=donor->co_stacksize+1;
    for(int index:indices)bytes[endpoint(index)]=static_cast<char>(RETURN_VALUE);
    nb::object routes=lh_check(PyTuple_New(indices.size()));
    for(size_t i=0;i<indices.size();++i)PyTuple_SET_ITEM(routes.ptr(),i,nb::int_(indices[i]).release().ptr());
    nb::object marker=nb::make_tuple(nb::str("naparixx.grid.return.v2"),routes);
    if(PyList_Append(constants.ptr(),marker.ptr())<0 || PyList_Append(constants.ptr(),payload.ptr())<0)throw nb::python_error();
}

int lfgr_error_endpoint(const LFGridFastLocals& locals) {
    if(lfgt_driver) {
        auto found=lfgt_driver->states.find(locals.frame);
        if(found!=lfgt_driver->states.end()) {
            int point=found->second.return_point;
            if(point>=0) {
                nb::object points=lfgt_points(locals.code);
                return static_cast<int>(PyLong_AsLong(PyTuple_GET_ITEM(PyTuple_GET_ITEM(points.ptr(),point),0)));
            }
            if(found->second.return_offset>=0)return found->second.return_offset;
        }
    }
    return PyFrame_GetLasti(locals.frame);
}
