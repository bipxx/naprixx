// Derived from NumPy's nd_grid.__getitem__ and numeric.indices.
// Copyright (c) 2005-2025, NumPy Developers. All rights reserved.
// BSD-3-Clause notice: ../../licenses/NumPy-BSD-3-Clause.txt.
namespace nx {
#define LFG(value) [&]() -> nb::object { return (value); }
// Handwritten policies for the exact NumPy grid bodies selected by FEC2.
// Staged only: genuine-function admission and runtime qualification are required.
// Source globals/builtins/metadata belong to each genuine function generation.
// No ndarray/model/cache mirror or retained executable source body is used.
// The genuine frame owns these locals. No C++ carrier mirrors their mutable
// values; callback writes through the supported interpreter frame interface
// are therefore observed by the next source LOAD_FAST equivalent.
struct LFGridFastLocals {
    PyFrameObject* frame;
    PyCodeObject* code;
    int* completion = nullptr;
    PyObject*& slot(const char* name) const {
        for (int i = 0; i < code->co_nlocals; ++i)
            if (PyUnicode_CompareWithASCIIString(PyTuple_GET_ITEM(code->co_localsplusnames, i), name) == 0)
                return frame->f_frame->localsplus[i];
        throw nb::type_error("Grid source local declaration is missing");
    }
    nb::object load(const char* name) const {
        PyObject* value = slot(name);
        if (!value) {
            PyErr_Format(PyExc_UnboundLocalError,
                "cannot access local variable '%s' where it is not associated with a value", name);
            throw nb::python_error();
        }
        return nb::borrow<nb::object>(value);
    }
    void store(const char* name, nb::object value) const {
        PyObject*& target = slot(name);
        PyObject* previous = target;
        target = value.release().ptr();
        Py_XDECREF(previous);
    }
    nb::object take(const char* name) const {
        PyObject*& target = slot(name);
        PyObject* previous = target;
        target = nullptr;
        return nb::steal<nb::object>(previous);
    }
};

#include "grid_line_trace.inc"

#include "grid_indices.inc"

nb::object lf_grid_comprehension_body(LabelsHistoryLocals& source, const LFGridFastLocals& locals,
                                      nb::object& iterator) {
    bool child=PyUnicode_CompareWithASCIIString(locals.code->co_name,"<listcomp>")==0;
    LFGridObservedOps O{locals};
    if(child) lfgt_line(locals,0);
    nb::object result = O.list();
    if(child) {lfgo_op(locals,"load",".0");iterator=locals.load(".0");}
    if(!child) lfgt_line(locals,67);
    try {
#if PY_VERSION_HEX >= 0x030D0000
    // 3.13's inlined comprehension re-applies GET_ITER after clearing the
    // saved locals and allocating its list. A foreign iterator may redirect it.
    if(!child) iterator=O.iter(std::move(iterator));
#endif
    while (nb::object pair = ([&](){
        long target=lfgo_op(locals,"next");nb::object value=lfgt_next(locals,iterator,child?3:68);
        if(!value.is_valid()) {
            lfgo_branch(locals,target);auto context=lfgo_context(locals);
            if(context.state && PyUnicode_CompareWithASCIIString(PyTuple_GET_ITEM(lfgo_row(context),1),"END_FOR")==0) {
                ++context.state->cursor;
                if(PyUnicode_CompareWithASCIIString(PyTuple_GET_ITEM(lfgo_row(context),1),"POP_TOP")==0)++context.state->cursor;
            }
        }
        return value;
    })()) {
        if(child) lfgt_line(locals,1);
        auto values = O.unpack(std::move(pair));
        lfgo_op(locals,"store","_x");
        locals.store("_x", std::move(values[0]));
        lfgo_op(locals,"store","_t");
        locals.store("_t", std::move(values[1]));
        lfgt_line(locals,child?2:69);
        lfgo_op(locals,"global","_nx");
        nb::object arange = O.attr(source, lh_global(source, "_nx"), "arange");
        nb::object value = O.call(std::move(arange), {LFG(([&](){lfgo_op(locals,"load","_x");return locals.load("_x");})())},
                                   {LFG(([&](){lfgo_op(locals,"load","_t");return locals.load("_t");})())}, lh_kw(source, "dtype"));
        lfgo_op(locals,"append");
        if (PyList_Append(result.ptr(), value.ptr()) < 0) throw nb::python_error();
        lfgt_line(locals,child?3:68);
    }
    } catch (...) {
        // The source unwinds the loop iterator before discarding the partial
        // list and restoring PEP 709's saved parent locals.
        iterator.reset();
        result.reset();
        throw;
    }
    iterator.reset();
    if(child) lfgo_op(locals,"return");
    return result;
}

nb::object lf_grid_sparse_comp(LabelsHistoryLocals& source, const LFGridFastLocals& locals) {
    nb::object iterator;
    return lf_grid_comprehension_body(source, locals, iterator);
}

#include "grid_getitem.inc"

#undef LFG

PyObject* lf_grid_frame(PyObject*, PyObject* arguments) {
    try {
        Py_ssize_t count = PyTuple_GET_SIZE(arguments);
        if (count < 1 || !PyLong_CheckExact(PyTuple_GET_ITEM(arguments, 0)))
            throw nb::type_error("Grid policy entry is required");
        long entry = PyLong_AsLong(PyTuple_GET_ITEM(arguments, 0));
        if (entry == -1 && PyErr_Occurred()) throw nb::python_error();
        if (count != 1 || (entry != 0 && entry != 1 && entry != -2))
            throw nb::type_error("Grid policy parameter declaration changed");
        PyFrameObject* frame = PyEval_GetFrame();
        if (!frame) throw nb::type_error("Grid policy genuine function frame is required");
        nb::object code = lh_check(reinterpret_cast<PyObject*>(PyFrame_GetCode(frame)));
        auto* actual = reinterpret_cast<PyCodeObject*>(code.ptr());
        if (PyTuple_GET_SIZE(actual->co_consts) == 0)
            throw nb::type_error("Grid policy inert metadata is missing");
        LabelsHistoryLocals source;
        source.source_globals = lh_check(PyFrame_GetGlobals(frame));
        source.source_builtins = lh_check(PyFrame_GetBuiltins(frame));
        nb::object payload = nb::borrow<nb::object>(
            PyTuple_GET_ITEM(actual->co_consts, PyTuple_GET_SIZE(actual->co_consts) - 1));
        if (!PyTuple_CheckExact(payload.ptr()) || (PyTuple_GET_SIZE(payload.ptr()) < 3 || PyTuple_GET_SIZE(payload.ptr()) > 5) ||
            PyUnicode_CompareWithASCIIString(PyTuple_GET_ITEM(payload.ptr(), 0), "naparixx.grid.v2") != 0)
            throw nb::type_error("Grid policy payload changed");
        nb::object routes=lfgr_routes(actual);
        std::unique_ptr<LFGridLineDriver> local_driver;
        if(routes.is_valid() && !lfgt_driver)local_driver=std::make_unique<LFGridLineDriver>();
        if(lfgt_driver && (PyTuple_GET_SIZE(payload.ptr())==5 || routes.is_valid())) {
            lfgt_driver->states[frame]=LFGridOpcodeState{};
            // RESUME changes instrumentation for the whole source code even
            // when this invocation fails before reaching an attribute site.
            lfga_sync({frame,actual});
        }
        // Source positions stay active inside provider callbacks, but the
        // interpreter must resume its physical transport instruction on exit.
        LFGridLinePosition physical(frame,actual,PyFrame_GetLasti(frame),PyFrame_GetLineNumber(frame));
        int completion=-1;LFGridFastLocals locals{frame,actual,&completion};
        try {
        source.source_metadata = nb::borrow<nb::object>(PyTuple_GET_ITEM(payload.ptr(), 1));
        lh_metadata_values(source.source_metadata, 0);
        lh_metadata_values(source.source_metadata, 1);
        nb::object comprehension = nb::borrow<nb::object>(PyTuple_GET_ITEM(payload.ptr(), 2));
        nb::object result;
        if(entry==-2)result=lf_grid_sparse_comp(source,locals);
        else if(entry==0)result=lf_grid_getitem(source,locals,comprehension);
        else {
            try {result=lf_grid_indices(source,locals);}
            catch(nb::python_error& error) {lfgt_exception(locals,error,true);throw;}
        }
        if(routes.is_valid()) {
            if(PyTuple_GET_SIZE(routes.ptr())==1)completion=static_cast<int>(PyLong_AsLong(PyTuple_GET_ITEM(routes.ptr(),0)));
            if(completion<0)throw nb::type_error("native completion endpoint is missing");
            return nb::make_tuple(result,nb::int_(completion),nb::bool_(false)).release().ptr();
        }
        return result.release().ptr();
        } catch(nb::python_error& error) {
            if(!routes.is_valid())throw;
            auto found=lfgt_driver->states.find(frame);
            if(found==lfgt_driver->states.end() || found->second.reported_error!=error.value().ptr())
                lfgt_exception(locals,error,true);
            int offset=lfgr_error_endpoint(locals);
            return nb::make_tuple(error.value(),nb::int_(offset/2),nb::bool_(true)).release().ptr();
        }
    } catch (...) { lf_adapter_error(); return nullptr; }
}
PyMethodDef lf_grid_frame_definition = {
    "_labels_frontend_grid_frame", lf_grid_frame, METH_VARARGS, nullptr};

nb::object lf_grid_prepare(nb::object function, nb::object defining_globals,
                           nb::object shell, nb::tuple complete_profile,
                           nb::tuple shell_profile, int entry,
                           nb::object comp_shell = nb::none(), nb::object comp_profile = nb::none()) {
    if (entry != 0 && entry != 1 && entry != -2)
        throw nb::value_error("Grid policy entry must name getitem or indices");
    if (!PyDict_CheckExact(defining_globals.ptr()) || !PyFunction_Check(shell.ptr()))
        throw nb::type_error("Grid policy defining dictionary and genuine shell are required");
    if (!PyFunction_Check(function.ptr()) ||
        PyFunction_GetGlobals(function.ptr()) != defining_globals.ptr() ||
        !lf_grid_profile_code(nb::handle(PyFunction_GetCode(function.ptr())), complete_profile))
        return nb::none();
    if (!lf_grid_profile_code(nb::handle(PyFunction_GetCode(shell.ptr())), shell_profile))
        throw nb::type_error("Grid policy complete declaration profile changed");
    // Preparation does not mutate either genuine function. The full handoff
    // prepares and validates every replacement before publishing any code.
    auto* original = reinterpret_cast<PyCodeObject*>(PyFunction_GetCode(function.ptr()));
    nb::object shell_code = lh_check(PyObject_GetAttrString(shell.ptr(), "__code__"));
    auto* transport = reinterpret_cast<PyCodeObject*>(shell_code.ptr());
    if (transport->co_argcount != original->co_argcount ||
        transport->co_posonlyargcount != original->co_posonlyargcount ||
        transport->co_kwonlyargcount != original->co_kwonlyargcount)
        throw nb::type_error("Grid policy declaration signature changed");
    if (original->co_argcount != (entry == 0 ? 2 : entry == 1 ? 3 : 1) ||
        original->co_kwonlyargcount != 0 || original->co_posonlyargcount != 0 ||
        PyFunction_GetClosure(function.ptr()) != nullptr ||
        PyFunction_GetClosure(shell.ptr()) != nullptr ||
        (original->co_flags & (CO_GENERATOR | CO_COROUTINE | CO_ASYNC_GENERATOR | CO_VARARGS | CO_VARKEYWORDS)) ||
        (transport->co_flags & (CO_GENERATOR | CO_COROUTINE | CO_ASYNC_GENERATOR | CO_VARARGS | CO_VARKEYWORDS)))
        throw nb::type_error("Grid policy unsupported declaration shape");
    const char* names0[] = {"self", "key"};
    const char* names1[] = {"dimensions", "dtype", "sparse"};
    for (int i = 0; i < original->co_argcount; ++i) {
        const char* name = entry == 0 ? names0[i] : entry == 1 ? names1[i] : ".0";
        nb::object source_names = lh_check(PyObject_GetAttrString(reinterpret_cast<PyObject*>(original), "co_varnames"));
        nb::object shell_names = lh_check(PyObject_GetAttrString(shell_code.ptr(), "co_varnames"));
        if (PyUnicode_CompareWithASCIIString(PyTuple_GET_ITEM(source_names.ptr(), i), name) != 0 ||
            PyUnicode_CompareWithASCIIString(PyTuple_GET_ITEM(shell_names.ptr(), i), entry == -2 ? "_iterator" : name) != 0)
            throw nb::type_error("Grid policy parameter name changed");
    }
    nb::object comprehension = nb::none();
    for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(original->co_consts); ++i) {
        PyObject* value = PyTuple_GET_ITEM(original->co_consts, i);
        if (!PyCode_Check(value)) continue;
        auto* child = reinterpret_cast<PyCodeObject*>(value);
        if (entry != 0 || !comprehension.is_none() ||
            PyUnicode_CompareWithASCIIString(child->co_name, "<listcomp>") != 0 ||
            comp_shell.is_none() || !PyTuple_CheckExact(comp_profile.ptr()))
            throw nb::type_error("Grid nested declaration is unsupported");
        nb::object child_function = lh_check(PyFunction_New(value, defining_globals.ptr()));
        nb::handle constants_profile(PyTuple_GET_ITEM(complete_profile.ptr(), 7));
        nb::handle child_atom(PyTuple_GET_ITEM(PyTuple_GET_ITEM(constants_profile.ptr(), 1), i));
        nb::tuple child_profile = nb::borrow<nb::tuple>(PyTuple_GET_ITEM(child_atom.ptr(), 1));
        comprehension = lf_grid_prepare(child_function, defining_globals, comp_shell,
            child_profile, nb::borrow<nb::tuple>(comp_profile), -2);
        if (comprehension.is_none()) throw nb::type_error("Grid comprehension profile changed");
    }
    int entry_constants = 0;
    for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(transport->co_consts); ++i) {
        PyObject* value = PyTuple_GET_ITEM(transport->co_consts, i);
        if (value == Py_None || value == Py_Ellipsis) continue;
        if (!PyLong_CheckExact(value) || PyLong_AsLong(value) != entry)
            throw nb::type_error("Grid policy declaration entry changed");
        ++entry_constants;
    }
    if (entry_constants != 1)
        throw nb::type_error("Grid policy declaration entry is missing");
    nb::object module_name = nb::str("naparixx._native");
    nb::object frame_call = lh_check(PyCFunction_NewEx(
        &lf_grid_frame_definition, nullptr, module_name.ptr()));
    nb::object constants = lh_check(PyTuple_New(PyTuple_GET_SIZE(transport->co_consts) + 1));
    bool marker = false;
    for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(transport->co_consts); ++i) {
        PyObject* value = PyTuple_GET_ITEM(transport->co_consts, i);
        if (value == Py_Ellipsis) { marker = true; value = frame_call.ptr(); }
        PyTuple_SET_ITEM(constants.ptr(), i, Py_NewRef(value));
    }
    if (!marker) throw nb::type_error("Grid policy owner-free transport marker is missing");
    PyTuple_SET_ITEM(constants.ptr(), PyTuple_GET_SIZE(transport->co_consts),
                     nb::make_tuple("naparixx.grid.v2", le_metadata(original), comprehension).release().ptr());
    nb::dict keywords;
    keywords["co_consts"] = constants;
    keywords["co_filename"] = nb::borrow<nb::object>(original->co_filename);
    keywords["co_name"] = nb::borrow<nb::object>(original->co_name);
    keywords["co_qualname"] = nb::borrow<nb::object>(original->co_qualname);
    keywords["co_firstlineno"] = nb::int_(original->co_firstlineno);
    keywords["co_flags"] = nb::int_(original->co_flags);
    if (entry == 0) {
#if PY_VERSION_HEX < 0x030C0000
        const char* names[] = {"self", "key", "size", "num_list", "k", "step", "start", "stop", "typ", "nn", "kk", "slobj", "step_float", "length"};
#else
        const char* names[] = {"self", "key", "size", "num_list", "k", "step", "start", "stop", "typ", "_x", "_t", "nn", "kk", "slobj", "step_float", "length"};
#endif
        constexpr int size = sizeof(names) / sizeof(names[0]);
        if (original->co_nlocals != size || PyTuple_GET_SIZE(original->co_localsplusnames) != size)
            throw nb::type_error("Grid getitem local layout changed");
        for (int i = 0; i < size; ++i)
            if (PyUnicode_CompareWithASCIIString(PyTuple_GET_ITEM(original->co_localsplusnames, i), names[i]) != 0)
                throw nb::type_error("Grid getitem local name changed");
    }
    if (entry == 1) {
        const char* names[] = {"dimensions", "dtype", "sparse", "N", "shape", "res", "i", "dim", "idx"};
        if (original->co_nlocals != 9 || PyTuple_GET_SIZE(original->co_localsplusnames) != 9)
            throw nb::type_error("Grid indices local layout changed");
        for (int i = 0; i < 9; ++i)
            if (PyUnicode_CompareWithASCIIString(PyTuple_GET_ITEM(original->co_localsplusnames, i), names[i]) != 0)
                throw nb::type_error("Grid indices local name changed");
    }
    if (entry == -2) {
        if (original->co_nlocals != 3 || PyTuple_GET_SIZE(original->co_localsplusnames) != 3 ||
            PyUnicode_CompareWithASCIIString(PyTuple_GET_ITEM(original->co_localsplusnames, 1), "_x") != 0 ||
            PyUnicode_CompareWithASCIIString(PyTuple_GET_ITEM(original->co_localsplusnames, 2), "_t") != 0)
            throw nb::type_error("Grid comprehension local layout changed");
    }
    {
        keywords["co_varnames"] = lh_check(PyObject_GetAttrString(reinterpret_cast<PyObject*>(original), "co_varnames"));
        keywords["co_nlocals"] = nb::int_(original->co_nlocals);
    }
    nb::object replace = lh_check(PyObject_GetAttrString(shell_code.ptr(), "replace"));
    nb::object args = nb::tuple();
    nb::object replacement = lh_check(PyObject_Call(replace.ptr(), args.ptr(), keywords.ptr()));
    return replacement;
}

LFCanonicalBindings lfg_collect() {
    LFCanonicalBindings result;
    nb::object tricks=lfc_module(result,"numpy.lib._index_tricks_impl");
    nb::object numeric=lfc_module(result,"numpy._core.numeric");
    nb::object cls=lfc_class(result,tricks,"nd_grid","numpy.lib._index_tricks_impl");
    nb::object getitem=lfc_member(result,cls,"__getitem__");
    lfc_function(result,getitem,tricks);
    lfc_function(result,lfc_dict_value(numeric.ptr(),"indices"),numeric);
    nb::object module=lfc_dict_value(tricks.ptr(),"_nx");
    if (!PyModule_CheckExact(module.ptr()) || PyModule_GetDict(module.ptr())!=numeric.ptr())
        throw nb::type_error("grid defining numeric module alias changed");
    result.keep(module);
    return result;
}
struct LFGridPlan {
    LFCanonicalBindings bindings;
    std::vector<nb::object> prepared;
    bool published=false;
    void validate() const {
        auto current=lfg_collect();
        if (current.anchors.size()!=bindings.anchors.size()) throw nb::type_error("grid provider bindings changed");
        for (size_t i=0;i<current.anchors.size();++i)
            if (current.anchors[i].ptr()!=bindings.anchors[i].ptr()) throw nb::type_error("grid provider bindings changed");
    }
};
nb::object lfg_prepare(nb::tuple shells,nb::tuple profiles,nb::tuple shell_profiles,
                       nb::object comp_shell,nb::object comp_profile) {
    if (shells.size()!=2 || profiles.size()!=2 || shell_profiles.size()!=2)
        throw nb::type_error("two complete grid provider controls required");
    LFGridPlan result;
    result.bindings=lfg_collect();
    for (int i=0;i<2;++i) {
        nb::object function=result.bindings.functions[i];
        nb::object code=lf_grid_prepare(function,nb::borrow<nb::object>(PyFunction_GetGlobals(function.ptr())),
            nb::borrow<nb::object>(shells[i]),nb::borrow<nb::tuple>(profiles[i]),nb::borrow<nb::tuple>(shell_profiles[i]),i,
            comp_shell,comp_profile);
        if (code.is_none()) throw nb::type_error("canonical grid source profile changed");
        result.prepared.push_back(code);
    }
    result.validate();
    return nb::cast(std::move(result));
}
nb::object lfg_publish(LFGridPlan& plan) {
    if (plan.published) throw std::runtime_error("grid provider plan is already published");
    std::vector<LFBChange> changes;
    auto anchors=plan.bindings.anchors;
    for (size_t i=0;i<2;++i) {
        changes.push_back(lfb_change(plan.bindings.functions[i],plan.prepared[i]));
        anchors[plan.bindings.code_anchors[i]]=plan.prepared[i];
    }
    lfb_execute(changes,[&](){plan.validate();},[&]() noexcept {
        plan.bindings.anchors.swap(anchors);plan.published=true;
    });
    return nb::none();
}
int lfg_traverse(PyObject* self,visitproc visit,void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) {
        const auto& plan=*nb::inst_ptr<LFGridPlan>(self);
        for (const auto& value:plan.bindings.anchors) Py_VISIT(value.ptr());
        for (const auto& value:plan.bindings.functions) Py_VISIT(value.ptr());
        for (const auto& value:plan.prepared) Py_VISIT(value.ptr());
    }
    return 0;
}
int lfg_clear(PyObject* self) {
    if (nb::inst_ready(self)) {
        auto& plan=*nb::inst_ptr<LFGridPlan>(self);
        plan.bindings.anchors.clear();plan.bindings.functions.clear();plan.prepared.clear();
    }
    return 0;
}
PyType_Slot lfg_slots[]={{Py_tp_traverse,reinterpret_cast<void*>(lfg_traverse)},
    {Py_tp_clear,reinterpret_cast<void*>(lfg_clear)},{0,nullptr}};
void lfg_exports(nb::module_& m) {
    lfgt_exports(m);
    nb::class_<LFGridPlan>(m,"GridProviderPlan",nb::is_weak_referenceable(),nb::type_slots(lfg_slots))
        .def("validate",&LFGridPlan::validate).def("publish_experiment",&lfg_publish)
        .def_prop_ro("functions",[](const LFGridPlan& value){value.validate();return lfc_tuple(value.bindings.functions);})
        .def_prop_ro("prepared_codes",[](const LFGridPlan& value){value.validate();return lfc_tuple(value.prepared);});
    m.def("prepare_grid_provider_plan_probe",&lfg_prepare,
        nb::arg("shells"),nb::arg("profiles"),nb::arg("shell_profiles"),
        nb::arg("comp_shell").none()=nb::none(),nb::arg("comp_profile").none()=nb::none());
    m.def("prepare_grid_function_probe",&lf_grid_prepare,
        nb::arg("function"),nb::arg("defining_globals"),nb::arg("shell"),
        nb::arg("profile"),nb::arg("shell_profile"),nb::arg("entry"),
        nb::arg("comp_shell").none()=nb::none(),nb::arg("comp_profile").none()=nb::none());
}
}
