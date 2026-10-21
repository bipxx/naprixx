// Experimental declaration-point capture; no function code publication.
// Code constants contain a weak marker, never a strong owner->code->owner cycle.
namespace nx {
struct LFProducerDeclaration {
    int entry=-1;
    int factory_begin=-1,factory_end=-1,apply_begin=-1,apply_end=-1;
    nb::object parent,code,function,original;
};
struct LFProducerScope {
    nb::object module,body,marker;
    std::vector<LFProducerDeclaration> declarations;
    bool closed=false;
};
struct LFProducerMarker { nb::object weak_scope; bool closed=false; };
struct LFProducerIdentityTicket {};
struct LFProducerTicket { nb::object scope,frame; int slot=-1; bool consumed=false; };

void lfp_frame(LFProducerScope& scope,const LFProducerDeclaration& declaration,bool applying,PyFrameObject* expected=nullptr) {
    if (scope.closed) throw nb::type_error("producer capture is closed");
    PyFrameObject* frame=PyEval_GetFrame();
    if (!frame || (expected && frame!=expected)) throw nb::type_error("actual defining frame is required");
    nb::object code=lh_check(reinterpret_cast<PyObject*>(PyFrame_GetCode(frame)));
    nb::object globals=lh_check(PyFrame_GetGlobals(frame));
    if (code.ptr()!=declaration.parent.ptr() || globals.ptr()!=PyModule_GetDict(scope.module.ptr()))
        throw nb::type_error("producer defining frame/code/namespace changed");
    const int offset=PyFrame_GetLasti(frame);
    int begin=applying?declaration.apply_begin:declaration.factory_begin;
    int end=applying?declaration.apply_end:declaration.factory_end;
    if (offset<begin || offset>=end) throw nb::type_error("producer declaration instruction changed");
    PyObject* name=PyModule_GetNameObject(scope.module.ptr());
    if (!name) throw nb::python_error();
    nb::object held=nb::steal<nb::object>(name);
    PyObject* registered=PyDict_GetItemWithError(PyImport_GetModuleDict(),name);
    if (!registered && PyErr_Occurred()) throw nb::python_error();
    if (registered!=scope.module.ptr()) throw nb::type_error("producer module registration changed");
}
nb::object lfp_ticket(LFProducerMarker& marker,int entry) {
    if (marker.closed) {
        // Existing adapter factories may be called again after observation.
        // They keep their real Python decorator semantics but cannot enroll
        // another canonical producer after the capture has closed.
        if (entry==36 || entry==38) return nb::cast(LFProducerIdentityTicket{});
        throw nb::type_error("producer capture is closed");
    }
    PyObject* raw=PyWeakref_GetObject(marker.weak_scope.ptr());
    if (!raw || raw==Py_None) throw nb::type_error("producer scope has expired");
    nb::object owner=nb::borrow<nb::object>(raw);
    auto& scope=*nb::inst_ptr<LFProducerScope>(owner);
    for (size_t i=0;i<scope.declarations.size();++i) {
        const auto& declaration=scope.declarations[i];
        if (declaration.entry!=entry) continue;
        lfp_frame(scope,declaration,false);
        if (declaration.function.is_valid()) throw nb::type_error("producer declaration already captured");
        LFProducerTicket ticket;
        ticket.scope=owner;
        ticket.frame=nb::borrow<nb::object>(reinterpret_cast<PyObject*>(PyEval_GetFrame()));
        ticket.slot=static_cast<int>(i);
        return nb::cast(std::move(ticket));
    }
    throw nb::type_error("producer declaration entry is not enrolled");
}
nb::object lfp_capture(LFProducerTicket& ticket,nb::object function) {
    if (ticket.consumed || !ticket.scope.is_valid() || !ticket.frame.is_valid())
        throw nb::type_error("producer declaration ticket is consumed");
    auto& scope=*nb::inst_ptr<LFProducerScope>(ticket.scope);
    if (ticket.slot<0 || static_cast<size_t>(ticket.slot)>=scope.declarations.size())
        throw nb::type_error("producer declaration ticket is invalid");
    auto& declaration=scope.declarations[ticket.slot];
    lfp_frame(scope,declaration,true,reinterpret_cast<PyFrameObject*>(ticket.frame.ptr()));
    if (declaration.function.is_valid()) throw nb::type_error("producer declaration already captured");
    if (declaration.entry<50) {
        if (!PyFunction_Check(function.ptr()) || PyFunction_GetCode(function.ptr())!=declaration.code.ptr() ||
            PyFunction_GetGlobals(function.ptr())!=PyModule_GetDict(scope.module.ptr()))
            throw nb::type_error("actual newly declared function is required");
    } else {
        if (!PyType_Check(function.ptr()) || !(reinterpret_cast<PyTypeObject*>(function.ptr())->tp_flags&Py_TPFLAGS_HEAPTYPE))
            throw nb::type_error("actual newly built source class is required");
        auto* type=reinterpret_cast<PyHeapTypeObject*>(function.ptr());
        auto* body=reinterpret_cast<PyCodeObject*>(declaration.code.ptr());
        if (!PyUnicode_CheckExact(type->ht_qualname)) throw nb::type_error("exact source class name is required");
        int same=PyObject_RichCompareBool(type->ht_qualname,body->co_qualname,Py_EQ);
        if (same<0) throw nb::python_error();
        nb::object actual=lfc_dict_value(type->ht_type.tp_dict,"__module__");
        nb::object expected=lfc_dict_value(PyModule_GetDict(scope.module.ptr()),"__name__");
        if (!PyUnicode_CheckExact(actual.ptr()) || !PyUnicode_CheckExact(expected.ptr()))
            throw nb::type_error("exact source class/module names are required");
        int module_same=PyObject_RichCompareBool(actual.ptr(),expected.ptr(),Py_EQ);
        if (module_same<0) throw nb::python_error();
        if (!same || !module_same) throw nb::type_error("actual source class identity metadata changed");
    }
    declaration.function=function;
    ticket.consumed=true;
    ticket.frame.reset();ticket.scope.reset();
    return function;
}
nb::object lfp_patch(nb::object code,nb::handle marker) {
    auto* raw=reinterpret_cast<PyCodeObject*>(code.ptr());
    nb::object constants=lh_check(PyTuple_New(PyTuple_GET_SIZE(raw->co_consts)));
    for (Py_ssize_t i=0;i<PyTuple_GET_SIZE(raw->co_consts);++i) {
        PyObject* value=PyTuple_GET_ITEM(raw->co_consts,i);
        nb::object replacement;
        if (PyCode_Check(value)) replacement=lfp_patch(nb::borrow<nb::object>(value),marker);
        else if (PyUnicode_CheckExact(value) && PyUnicode_CompareWithASCIIString(value,"naparixx.producer.marker.v1")==0)
            replacement=nb::borrow<nb::object>(marker);
        else replacement=nb::borrow<nb::object>(value);
        PyTuple_SET_ITEM(constants.ptr(),i,replacement.release().ptr());
    }
    nb::dict kwargs;kwargs["co_consts"]=constants;
    nb::object replace=lh_check(PyObject_GetAttrString(code.ptr(),"replace"));
    nb::object args=lh_check(PyTuple_New(0));
    return lh_check(PyObject_Call(replace.ptr(),args.ptr(),kwargs.ptr()));
}
void lfp_find(nb::handle parent,const char* qualname,int occurrence,int& seen,nb::object& found_parent,nb::object& found) {
    auto* code=reinterpret_cast<PyCodeObject*>(parent.ptr());
    for (Py_ssize_t i=0;i<PyTuple_GET_SIZE(code->co_consts);++i) {
        PyObject* value=PyTuple_GET_ITEM(code->co_consts,i);
        if (!PyCode_Check(value)) continue;
        auto* child=reinterpret_cast<PyCodeObject*>(value);
        if (PyUnicode_CompareWithASCIIString(child->co_qualname,qualname)==0 && ++seen==occurrence) {
            found_parent=nb::borrow<nb::object>(parent);found=nb::borrow<nb::object>(value);
        }
        lfp_find(nb::handle(value),qualname,occurrence,seen,found_parent,found);
    }
}
nb::object lfp_prepare(nb::object module,nb::object code,nb::tuple rows,nb::object original_body) {
    if (!PyModule_CheckExact(module.ptr()) || !PyCode_Check(code.ptr()) || rows.size()==0)
        throw nb::type_error("actual module, compiled declaration body and entries are required");
    LFProducerScope value;value.module=module;
    nb::object owner=nb::cast(std::move(value));
    LFProducerMarker marker;marker.weak_scope=lh_check(PyWeakref_NewRef(owner.ptr(),nullptr));
    nb::object callback=nb::cast(std::move(marker));
    auto& scope=*nb::inst_ptr<LFProducerScope>(owner);
    scope.marker=callback;
    scope.body=lfp_patch(code,callback);
    for (nb::handle item:rows) {
        if (!PyTuple_CheckExact(item.ptr()) || PyTuple_GET_SIZE(item.ptr())!=7)
            throw nb::type_error("producer declaration metadata changed");
        nb::tuple row=nb::borrow<nb::tuple>(item);
        LFProducerDeclaration declaration;
        declaration.entry=nb::cast<int>(row[0]);
        const char* qualname=PyUnicode_AsUTF8(row[1].ptr());
        if (!qualname) throw nb::python_error();
        int occurrence=nb::cast<int>(row[2]),seen=0;
        declaration.factory_begin=nb::cast<int>(row[3]);declaration.factory_end=nb::cast<int>(row[4]);
        declaration.apply_begin=nb::cast<int>(row[5]);declaration.apply_end=nb::cast<int>(row[6]);
        if (declaration.entry<0 || declaration.entry>=54 || occurrence<1 || declaration.factory_begin<0 ||
            declaration.factory_end<=declaration.factory_begin || declaration.apply_begin<declaration.factory_end ||
            declaration.apply_end<=declaration.apply_begin)
            throw nb::type_error("invalid producer declaration metadata");
        for (const auto& prior:scope.declarations)
            if (prior.entry==declaration.entry) throw nb::type_error("duplicate producer declaration entry");
        lfp_find(scope.body,qualname,occurrence,seen,declaration.parent,declaration.code);
        if (!declaration.code.is_valid()) throw nb::type_error("producer code declaration is missing");
        if (!original_body.is_none()) {
            if (!PyCode_Check(original_body.ptr())) throw nb::type_error("original compiled module control is required");
            nb::object ignored_parent;
            int original_seen=0;
            lfp_find(original_body,qualname,occurrence,original_seen,ignored_parent,declaration.original);
            if (!declaration.original.is_valid()) throw nb::type_error("original source declaration is missing");
        }
        scope.declarations.push_back(std::move(declaration));
    }
    return owner;
}
nb::object lfp_finish(LFProducerScope& scope) {
    if (scope.closed) throw nb::type_error("producer capture is already closed");
    for (const auto& declaration:scope.declarations)
        if (!declaration.function.is_valid()) throw nb::type_error("producer declarations are incomplete");
    scope.closed=true;
    nb::inst_ptr<LFProducerMarker>(scope.marker)->closed=true;
    return nb::none();
}
nb::object lfp_validate(nb::tuple owners,nb::tuple wrapper_profile) {
    LFCanonicalBindings canonical=lfc_collect(wrapper_profile);
    std::vector<bool> seen(54,false);
    for (nb::handle owner:owners) {
        if (!nb::isinstance<LFProducerScope>(owner) || !nb::inst_ready(owner))
            throw nb::type_error("actual producer capture is required");
        const auto& scope=*nb::inst_ptr<LFProducerScope>(owner);
        if (!scope.closed) throw nb::type_error("producer capture must finish before validation");
        for (const auto& declaration:scope.declarations) {
            if (seen[declaration.entry]) throw nb::type_error("duplicate captured frontend entry");
            seen[declaration.entry]=true;
            if (declaration.entry<50) {
                PyObject* function=canonical.functions[declaration.entry].ptr();
                if (function!=declaration.function.ptr() || PyFunction_GetCode(function)!=declaration.code.ptr() ||
                    PyFunction_GetGlobals(function)!=PyModule_GetDict(scope.module.ptr()))
                    throw nb::type_error("canonical function differs from its captured defining execution");
            } else {
                const char* modules[]={"napari.layers.labels.labels","napari.layers.labels.labels",
                    "napari.layers.labels._labels_mouse_bindings","napari.layers.base.base"};
                const char* names[]={"Labels","WrongSelectedLabelError","BrushSizeOnMouseMove","Layer"};
                nb::object module=lfc_dict_value(PyImport_GetModuleDict(),modules[declaration.entry-50]);
                nb::object cls=lfc_dict_value(PyModule_GetDict(module.ptr()),names[declaration.entry-50]);
                if (module.ptr()!=scope.module.ptr() || cls.ptr()!=declaration.function.ptr())
                    throw nb::type_error("canonical class differs from its captured defining execution");
            }
        }
    }
    for (bool captured:seen) if (!captured) throw nb::type_error("complete fifty-function/four-class producer capture is required");
    return lfc_tuple(canonical.functions);
}
void lfp_validate_snapshot(nb::handle owners,nb::handle wrapper_profile) {
    (void)lfp_validate(nb::borrow<nb::tuple>(owners),nb::borrow<nb::tuple>(wrapper_profile));
}
nb::object lfp_prepare_native(nb::tuple owners,nb::tuple shells,nb::tuple source_profiles,
        nb::tuple shell_profiles,nb::tuple expression_shells,nb::tuple expression_profiles,nb::tuple wrapper_profile) {
    (void)lfp_validate(owners,wrapper_profile);
    if (source_profiles.size()!=50) throw nb::type_error("complete original compiler controls are required");
    LFCanonicalSnapshot result;
    result.producer_scopes=owners;result.wrapper_profile=wrapper_profile;
    result.bindings=lfc_collect(wrapper_profile);
    std::vector<nb::object> originals(50);
    for (nb::handle owner:owners) {
        const auto& scope=*nb::inst_ptr<LFProducerScope>(owner);
        for (const auto& declaration:scope.declarations) {
            if (declaration.entry>=50) continue;
            if (!declaration.original.is_valid() || !lf_grid_profile_code(declaration.original,source_profiles[declaration.entry]))
                throw nb::type_error("captured original source compiler profile does not match");
            auto* source=reinterpret_cast<PyCodeObject*>(declaration.original.ptr());
            auto* actual=reinterpret_cast<PyCodeObject*>(declaration.code.ptr());
            if (source->co_argcount!=actual->co_argcount || source->co_posonlyargcount!=actual->co_posonlyargcount ||
                source->co_kwonlyargcount!=actual->co_kwonlyargcount || source->co_flags!=actual->co_flags)
                throw nb::type_error("captured source argument/generator ABI changed");
            for (const char* name:{"co_varnames","co_freevars","co_cellvars"}) {
                nb::object expected=lh_check(PyObject_GetAttrString(declaration.original.ptr(),name));
                nb::object found=lh_check(PyObject_GetAttrString(declaration.code.ptr(),name));
                int same=PyObject_RichCompareBool(expected.ptr(),found.ptr(),Py_EQ);
                if (same<0) throw nb::python_error();
                if (!same) throw nb::type_error("captured source local/cell binding ABI changed");
            }
            // Only the two factories contain a selected nested declaration.
            // Other bodies must still match the full uninstrumented profile.
            if (declaration.entry!=35 && declaration.entry!=37 &&
                !lf_grid_profile_code(declaration.code,source_profiles[declaration.entry]))
                throw nb::type_error("non-factory source body changed during capture");
            originals[declaration.entry]=declaration.original;
        }
    }
    nb::tuple parents=nb::make_tuple(originals[10],originals[11],originals[29],originals[3]);
    nb::object expressions=lfe_prepare_codes_impl(parents,source_profiles,expression_shells,expression_profiles,true);
    for (int i=0;i<50;++i)
        result.prepared.push_back(lfu_prepare_code(reinterpret_cast<PyCodeObject*>(originals[i].ptr()),
            i,shells,source_profiles,shell_profiles,expressions));
    result.validate();
    return nb::cast(std::move(result));
}
int lfp_scope_traverse(PyObject* self,visitproc visit,void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) {
        const auto& scope=*nb::inst_ptr<LFProducerScope>(self);
        Py_VISIT(scope.module.ptr());Py_VISIT(scope.body.ptr());Py_VISIT(scope.marker.ptr());
        for (const auto& declaration:scope.declarations) {
            Py_VISIT(declaration.parent.ptr());Py_VISIT(declaration.code.ptr());Py_VISIT(declaration.function.ptr());Py_VISIT(declaration.original.ptr());
        }
    }
    return 0;
}
int lfp_scope_clear(PyObject* self) {
    if (nb::inst_ready(self)) {
        auto& scope=*nb::inst_ptr<LFProducerScope>(self);
        scope.closed=true;scope.module.reset();scope.body.reset();scope.marker.reset();scope.declarations.clear();
    }
    return 0;
}
int lfp_ticket_traverse(PyObject* self,visitproc visit,void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) {
        const auto& ticket=*nb::inst_ptr<LFProducerTicket>(self);
        Py_VISIT(ticket.scope.ptr());Py_VISIT(ticket.frame.ptr());
    }
    return 0;
}
int lfp_ticket_clear(PyObject* self) {
    if (nb::inst_ready(self)) {
        auto& ticket=*nb::inst_ptr<LFProducerTicket>(self);
        ticket.consumed=true;ticket.scope.reset();ticket.frame.reset();
    }
    return 0;
}
PyType_Slot lfp_scope_slots[]={{Py_tp_traverse,reinterpret_cast<void*>(lfp_scope_traverse)},
    {Py_tp_clear,reinterpret_cast<void*>(lfp_scope_clear)},{0,nullptr}};
PyType_Slot lfp_ticket_slots[]={{Py_tp_traverse,reinterpret_cast<void*>(lfp_ticket_traverse)},
    {Py_tp_clear,reinterpret_cast<void*>(lfp_ticket_clear)},{0,nullptr}};
void lfp_exports(nb::module_& m) {
    nb::class_<LFProducerScope>(m,"ProducerScope",nb::is_weak_referenceable(),nb::type_slots(lfp_scope_slots))
        .def_prop_ro("code",[](const LFProducerScope& scope){return scope.body;})
        .def("finish",&lfp_finish);
    nb::class_<LFProducerMarker>(m,"_ProducerMarker").def("__call__",&lfp_ticket);
    nb::class_<LFProducerIdentityTicket>(m,"_ProducerIdentityTicket")
        .def("__call__",[](LFProducerIdentityTicket&,nb::object value){return value;});
    nb::class_<LFProducerTicket>(m,"_ProducerTicket",nb::is_weak_referenceable(),nb::type_slots(lfp_ticket_slots))
        .def("__call__",&lfp_capture);
    m.def("prepare_producer_capture_probe",&lfp_prepare,nb::arg("module"),nb::arg("code"),nb::arg("rows"),nb::arg("original_body")=nb::none());
    m.def("validate_captured_frontend_probe",&lfp_validate);
    m.def("prepare_captured_frontend_probe",&lfp_prepare_native);
}
}
