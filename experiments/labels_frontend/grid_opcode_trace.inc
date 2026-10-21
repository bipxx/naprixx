// Source instruction metadata schedules observer events only. Algorithms and
// operand evaluation remain in handwritten C++; no Python policy is evaluated.
void lfgo_validate(PyCodeObject* code, nb::handle rows) {
    if (!PyTuple_CheckExact(rows.ptr()) || PyTuple_GET_SIZE(rows.ptr()) < 2)
        throw nb::type_error("source instruction controls must be a tuple");
    long previous = -1;
    for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(rows.ptr()); ++i) {
        PyObject* row = PyTuple_GET_ITEM(rows.ptr(), i);
        if (!PyTuple_CheckExact(row) || (PyTuple_GET_SIZE(row) != 4 && PyTuple_GET_SIZE(row) != 5) ||
            !PyLong_CheckExact(PyTuple_GET_ITEM(row,0)) || !PyUnicode_CheckExact(PyTuple_GET_ITEM(row,1)) ||
            !PyTuple_CheckExact(PyTuple_GET_ITEM(row,2)) || !PyLong_CheckExact(PyTuple_GET_ITEM(row,3)))
            throw nb::type_error("source instruction control shape changed");
        long offset = PyLong_AsLong(PyTuple_GET_ITEM(row,0));
        long target = PyLong_AsLong(PyTuple_GET_ITEM(row,3));
        if (PyErr_Occurred()) throw nb::python_error();
        if (offset <= previous || offset < 0 || offset >= _PyCode_NBYTES(code) || offset % 2 ||
            target < -1 || target >= _PyCode_NBYTES(code) || (target >= 0 && target % 2))
            throw nb::type_error("source instruction control range changed");
        previous = offset;
        if(PyTuple_GET_SIZE(row)==5) {
            if(!PyLong_CheckExact(PyTuple_GET_ITEM(row,4))) throw nb::type_error("source opcode line changed");
            long line=PyLong_AsLong(PyTuple_GET_ITEM(row,4));
            if(PyErr_Occurred()) throw nb::python_error();
            if(line < -1) throw nb::type_error("source opcode line range changed");
        }
        PyObject* actions = PyTuple_GET_ITEM(row,2);
        for (Py_ssize_t j = 0; j < PyTuple_GET_SIZE(actions); ++j) {
            PyObject* action = PyTuple_GET_ITEM(actions,j);
            if (!PyTuple_CheckExact(action) || PyTuple_GET_SIZE(action) != 2 ||
                !PyUnicode_CheckExact(PyTuple_GET_ITEM(action,0)) || !PyUnicode_CheckExact(PyTuple_GET_ITEM(action,1)))
                throw nb::type_error("source instruction action changed");
        }
    }
}
nb::object lfgo_with_lines(PyCodeObject* source,nb::handle rows) {
    nb::object result=lh_check(PyTuple_New(PyTuple_GET_SIZE(rows.ptr())));
    for(Py_ssize_t i=0;i<PyTuple_GET_SIZE(rows.ptr());++i) {
        PyObject* row=PyTuple_GET_ITEM(rows.ptr(),i);
        int offset=static_cast<int>(PyLong_AsLong(PyTuple_GET_ITEM(row,0)));
        nb::object copy=nb::make_tuple(nb::handle(PyTuple_GET_ITEM(row,0)),nb::handle(PyTuple_GET_ITEM(row,1)),
            nb::handle(PyTuple_GET_ITEM(row,2)),nb::handle(PyTuple_GET_ITEM(row,3)),nb::int_(PyCode_Addr2Line(source,offset)));
        PyTuple_SET_ITEM(result.ptr(),i,copy.release().ptr());
    }
    return result;
}

struct LFGridOpcodeContext {
    nb::object rows;
    LFGridOpcodeState* state = nullptr;
};
LFGridOpcodeContext lfgo_context(const LFGridFastLocals& locals) {
    if (!lfgt_driver || PyThreadState_Get()->tracing) return {};
    PyObject* constants = locals.code->co_consts;
    PyObject* payload = PyTuple_GET_ITEM(constants,PyTuple_GET_SIZE(constants)-1);
    if (!PyTuple_CheckExact(payload) || PyTuple_GET_SIZE(payload) != 5) return {};
    PyObject* rows = PyTuple_GET_ITEM(payload,4);
    lfgo_validate(locals.code, nb::handle(rows));
    auto found = lfgt_driver->states.find(locals.frame);
    if (found == lfgt_driver->states.end()) return {};
    if(PyThreadState_Get()->c_tracefunc!=lfgt_proxy && !(
        !PyThreadState_Get()->c_tracefunc && found->second.trailing_opcode)) return {};
    return {nb::borrow<nb::object>(rows), &found->second};
}
PyObject* lfgo_row(const LFGridOpcodeContext& context) {
    if (context.state->cursor < 0 || context.state->cursor >= PyTuple_GET_SIZE(context.rows.ptr()))
        throw nb::type_error("native source instruction cursor escaped its controls");
    return PyTuple_GET_ITEM(context.rows.ptr(),context.state->cursor);
}
void lfgo_emit(const LFGridFastLocals& locals, PyObject* row) {
    if (!locals.frame->f_trace_opcodes) return;
    auto found=lfgt_driver->states.find(locals.frame);
    if(PyThreadState_Get()->c_tracefunc!=lfgt_proxy &&
       (found==lfgt_driver->states.end() || !found->second.trailing_opcode)) return;
    if(found!=lfgt_driver->states.end()) found->second.trailing_opcode=false;
    int offset = static_cast<int>(PyLong_AsLong(PyTuple_GET_ITEM(row,0)));
    int line=static_cast<int>(PyLong_AsLong(PyTuple_GET_ITEM(row,4)));
    LFGridLinePosition position(locals.frame,locals.code,offset,line);
    PyThreadState* thread = PyThreadState_Get();
    PyThreadState_EnterTracing(thread);
    int status = lfgt_driver->callback(lfgt_driver->previous_object.ptr(),locals.frame,PyTrace_OPCODE,Py_None);
    PyThreadState_LeaveTracing(thread);
    if (status < 0) throw nb::python_error();
    if (position.jumped()) throw nb::type_error("native opcode pilot does not implement debugger jumps");
}
void lfgo_seek(const LFGridOpcodeContext& context, long offset) {
    for (Py_ssize_t i=0;i<PyTuple_GET_SIZE(context.rows.ptr());++i)
        if (PyLong_AsLong(PyTuple_GET_ITEM(PyTuple_GET_ITEM(context.rows.ptr(),i),0))==offset) {
            context.state->cursor=i; return;
        }
    throw nb::type_error("native source branch target is missing");
}
std::pair<std::string,std::string> lfgo_action(PyObject* value) {
    const char* kind=PyUnicode_AsUTF8(PyTuple_GET_ITEM(value,0));
    if(!kind) throw nb::python_error();
    const char* name=PyUnicode_AsUTF8(PyTuple_GET_ITEM(value,1));
    if(!name) throw nb::python_error();
    return {kind,name};
}
long lfgo_op(const LFGridFastLocals& locals,const char* kind,const char* name="") {
    auto context=lfgo_context(locals);
    if (!context.state) return -1;
    auto matches=[&](const auto& action){return action.first==kind && (!*name || action.second==name);};
    if (!context.state->pending.empty()) {
        auto action=context.state->pending.front();
        if (!matches(action)) throw nb::type_error("native fused instruction operand order changed");
        context.state->pending.erase(context.state->pending.begin()); return -1;
    }
    for (;;) {
        PyObject* row=lfgo_row(context), *actions=PyTuple_GET_ITEM(row,2);
        if (!PyTuple_GET_SIZE(actions)) {
            ++context.state->cursor; lfgo_emit(locals,row); continue;
        }
        auto action=lfgo_action(PyTuple_GET_ITEM(actions,0));
        // 3.13 loads the keyword-name tuple immediately before CALL_KW.
        if (std::string(kind)=="call" && action.first=="const") {
            ++context.state->cursor; lfgo_emit(locals,row); continue;
        }
        if (!matches(action)) {
            PyErr_Format(PyExc_RuntimeError,"native opcode expected %s:%s, found %s:%s at %ld",
                kind,name,action.first.c_str(),action.second.c_str(),PyLong_AsLong(PyTuple_GET_ITEM(row,0)));
            throw nb::python_error();
        }
        for (Py_ssize_t i=1;i<PyTuple_GET_SIZE(actions);++i)
            context.state->pending.push_back(lfgo_action(PyTuple_GET_ITEM(actions,i)));
        ++context.state->cursor;
        long target=PyLong_AsLong(PyTuple_GET_ITEM(row,3));
        lfgo_emit(locals,row); return target;
    }
}
void lfgo_branch(const LFGridFastLocals& locals,long target) {
    auto context=lfgo_context(locals);
    if (context.state && target>=0) lfgo_seek(context,target);
}
void lfgo_align(const LFGridFastLocals& locals,long offset) {
    auto context=lfgo_context(locals);
    if (!context.state) return;
    if (!context.state->pending.empty()) throw nb::type_error("native line split a fused instruction");
    for (Py_ssize_t walked=0;walked<=PyTuple_GET_SIZE(context.rows.ptr());++walked) {
        PyObject* row=lfgo_row(context), *actions=PyTuple_GET_ITEM(row,2);
        if (PyLong_AsLong(PyTuple_GET_ITEM(row,0))==offset) return;
        if (PyTuple_GET_SIZE(actions)) {
            auto action=lfgo_action(PyTuple_GET_ITEM(actions,0));
            if (action.first!="jump") {
                PyErr_Format(PyExc_TypeError,"native line %ld skipped %s:%s at %ld",offset,
                    action.first.c_str(),action.second.c_str(),PyLong_AsLong(PyTuple_GET_ITEM(row,0)));
                throw nb::python_error();
            }
            long target=PyLong_AsLong(PyTuple_GET_ITEM(row,3));
            lfgo_emit(locals,row); lfgo_seek(context,target);
        } else {++context.state->cursor; lfgo_emit(locals,row);}
    }
    throw nb::type_error("source observer branch controls contain a cycle");
}

struct LFGridObservedOps {
    const LFGridFastLocals& locals;
    nb::object list(std::initializer_list<LHExpr> items={}) {
        std::vector<nb::object> values;for(const auto& item:items) values.push_back(item());
        std::string count=std::to_string(values.size());lfgo_op(locals,"list",count.c_str());
        nb::object result=lh_check(PyList_New(values.size()));
        for(size_t i=0;i<values.size();++i) PyList_SET_ITEM(result.ptr(),i,values[i].release().ptr());
        return result;
    }
    nb::object pack(nb::object first,nb::object second) {
        lfgo_op(locals,"tuple","2");return nb::make_tuple(first,second);
    }
    nb::object item(nb::object value,nb::object key) {
        lfgo_op(locals,"item");return lh_item(std::move(value),std::move(key));
    }
    nb::object index(const LHExpr& value,const LHExpr& key) {
        nb::object receiver=value(),index=key();return item(std::move(receiver),std::move(index));
    }
    void set_index(const LHExpr& value,const LHExpr& receiver,const LHExpr& key) {
        nb::object item=value(),target=receiver(),index=key();
        setitem(std::move(item),std::move(target),std::move(index));
    }
    bool is_none(nb::object value) {
        long target=lfgo_op(locals,"not-none");bool result=value.is_none();
        value.reset();if(!result)lfgo_branch(locals,target);return result;
    }
    nb::object constant(nb::object value) {lfgo_op(locals,"const"); return value;}
    nb::object tuple(nb::object value) {lfgo_op(locals,"tuple","1"); return nb::make_tuple(value);}
    nb::object binary(binaryfunc operation,nb::object left,nb::object right,const char* name) {
        lfgo_op(locals,"binary",name); return lh_check(operation(left.ptr(),right.ptr()));
    }
    nb::object call(nb::object function,std::initializer_list<LHExpr> args={},
                    std::initializer_list<LHExpr> kw={},nb::object names={},int point=-1) {
        std::vector<nb::object> values;
        for (const auto& value:args) values.push_back(value());
        for (const auto& value:kw) values.push_back(value());
        lfgo_op(locals,"call");
        std::vector<PyObject*> raw; for (const auto& value:values) raw.push_back(value.ptr());
        if(point>=0) {
            int error_point=point;
#if PY_VERSION_HEX < 0x030D0000
            PyObject* callable=PyMethod_Check(function.ptr())?PyMethod_GET_FUNCTION(function.ptr()):function.ptr();
            if(PyFunction_Check(callable)) error_point+=4;
#endif
            lfgt_error_point(locals,error_point);
        }
        PyObject* result=PyObject_Vectorcall(function.ptr(),raw.data(),args.size(),kw.size()?names.ptr():nullptr);
        function.reset(); for (auto& value:values) value.reset(); names.reset();
        if(result && point>=0) lfgt_error_point(locals,-1);
        return lh_check(result);
    }
    bool truth(nb::object value) {
        long target=lfgo_op(locals,"truth"); bool result=lh_truth(std::move(value));
#if PY_VERSION_HEX >= 0x030D0000
        target=lfgo_op(locals,"branch-truth");
#endif
        if (!result) lfgo_branch(locals,target); return result;
    }
    nb::object iter(nb::object value) {lfgo_op(locals,"iter"); return lh_iter(std::move(value));}
    nb::object next(nb::handle iterator) {
        long target=lfgo_op(locals,"next"); nb::object value=lh_next(iterator);
        if (!value.is_valid()) {
            lfgo_branch(locals,target);
            auto context=lfgo_context(locals);
            if(context.state && PyUnicode_CompareWithASCIIString(PyTuple_GET_ITEM(lfgo_row(context),1),"END_FOR")==0) {
                ++context.state->cursor; // FOR_ITER exhaustion performs these cleanups itself.
                if(PyUnicode_CompareWithASCIIString(PyTuple_GET_ITEM(lfgo_row(context),1),"POP_TOP")==0)
                    ++context.state->cursor;
            }
        }
        return value;
    }
    std::vector<nb::object> unpack(nb::object value) {
        lfgo_op(locals,"unpack","2"); return lf_unpack(std::move(value),2);
    }
    nb::object attr(LabelsHistoryLocals& source,nb::object value,const char* name) {
        lfgo_op(locals,"attr",name); return lh_attr(source,std::move(value),name);
    }
    nb::object slice(nb::object value,nb::object start,nb::object stop) {
#if PY_VERSION_HEX < 0x030C0000
        lfgo_op(locals,"build-slice","2");
        nb::object key=lh_check(PySlice_New(start.ptr(),stop.ptr(),Py_None));
        start.reset(); stop.reset(); lfgo_op(locals,"item");
#else
        lfgo_op(locals,"slice");
        nb::object key=lh_check(PySlice_New(start.ptr(),stop.ptr(),Py_None));
        start.reset(); stop.reset();
#endif
        return lh_item(std::move(value),std::move(key));
    }
    void setitem(nb::object value,nb::object receiver,nb::object key) {
        lfgo_op(locals,"setitem");
        int status=PyObject_SetItem(receiver.ptr(),key.ptr(),value.ptr());
        value.reset();receiver.reset();key.reset();
        if(status<0) throw nb::python_error();
    }
};
