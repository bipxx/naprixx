// Derived from NumPy's nd_grid.__getitem__ and numeric.indices.
// Copyright (c) 2005-2025, NumPy Developers. All rights reserved.
// BSD-3-Clause notice: ../../licenses/NumPy-BSD-3-Clause.txt.
namespace nx {
#define LFG(value) [&]() -> nb::object { return (value); }
// Handwritten policies for the exact NumPy grid bodies selected by FEC2.
// Staged only: genuine-function admission and runtime qualification are required.
// Source globals/builtins/metadata belong to each genuine function generation.
// No ndarray/model/cache mirror or retained executable source body is used.
nb::object lf_grid_indices(LabelsHistoryLocals& source, nb::object dimensions,
                           nb::object dtype, nb::object sparse) {
    auto G = [&](const char* name) { return lh_global(source, name); };
    auto A = [&](nb::object value, const char* name) {
        return lh_attr(source, std::move(value), name);
    };
    dimensions = lh_call(G("tuple"), {LFG(dimensions)});
    nb::object n = lh_call(G("len"), {LFG(dimensions)});
    nb::object one = nb::make_tuple(1);
    nb::object shape = lh_check(PyNumber_Multiply(one.ptr(), n.ptr()));
    nb::object result;
    if (lh_truth(sparse)) {
        result = nb::tuple();
    } else {
        nb::object prefix = nb::make_tuple(n);
        nb::object full = lh_check(PyNumber_Add(prefix.ptr(), dimensions.ptr()));
        result = lh_call(G("empty"), {LFG(full)}, {LFG(dtype)}, lh_kw(source, "dtype"));
    }
    nb::object iterator = lh_iter(lh_call(G("enumerate"), {LFG(dimensions)}));
    while (nb::object pair = lh_next(iterator)) {
        // The iterator is genuine enumerate; preserve tuple unpack errors if
        // its defining global is deliberately replaced by an application callback.
        nb::object unpack = lh_iter(pair);
        nb::object i = lh_next(unpack), dim = lh_next(unpack);
        if (!i.is_valid() || !dim.is_valid()) {
            PyErr_Format(PyExc_ValueError, "not enough values to unpack (expected 2, got %d)", i.is_valid() ? 1 : 0);
            throw nb::python_error();
        }
        if (lh_next(unpack).is_valid()) {
            PyErr_SetString(PyExc_ValueError, "too many values to unpack (expected 2)");
            throw nb::python_error();
        }
        nb::object values = lh_call(G("arange"), {LFG(dim)}, {LFG(dtype)}, lh_kw(source, "dtype"));
        nb::object left_key = lh_check(PySlice_New(Py_None, i.ptr(), Py_None));
        nb::object left = lh_item(shape, left_key);
        nb::object middle = nb::make_tuple(dim);
        nb::object index_next = lh_check(PyNumber_Add(i.ptr(), nb::int_(1).ptr()));
        nb::object right_key = lh_check(PySlice_New(index_next.ptr(), Py_None, Py_None));
        nb::object right = lh_item(shape, right_key);
        nb::object first = lh_check(PyNumber_Add(left.ptr(), middle.ptr()));
        nb::object reshaped_shape = lh_check(PyNumber_Add(first.ptr(), right.ptr()));
        nb::object idx = lh_call(A(values, "reshape"), {LFG(reshaped_shape)});
        if (lh_truth(sparse)) {
            nb::object item = nb::make_tuple(idx);
            result = lh_check(PyNumber_Add(result.ptr(), item.ptr()));
        } else {
            if (PyObject_SetItem(result.ptr(), i.ptr(), idx.ptr()) < 0) throw nb::python_error();
        }
    }
    return result;
}

// The genuine frame owns these locals. No C++ carrier mirrors their mutable
// values; callback writes through the supported interpreter frame interface
// are therefore observed by the next source LOAD_FAST equivalent.
struct LFGridFastLocals {
    PyFrameObject* frame;
    PyCodeObject* code;
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
};

nb::object lf_grid_sparse_comp(LabelsHistoryLocals& source, const LFGridFastLocals& locals) {
    nb::object iterator = locals.load(".0");
    nb::object result = lh_check(PyList_New(0));
    while (nb::object pair = lh_next(iterator)) {
        auto values = lf_unpack(std::move(pair), 2);
        locals.store("_x", std::move(values[0]));
        locals.store("_t", std::move(values[1]));
        nb::object arange = lh_attr(source, lh_global(source, "_nx"), "arange");
        nb::object value = lh_call(arange, {LFG(locals.load("_x"))},
                                   {LFG(locals.load("_t"))}, lh_kw(source, "dtype"));
        if (PyList_Append(result.ptr(), value.ptr()) < 0) throw nb::python_error();
    }
    return result;
}

nb::object lf_grid_getitem(LabelsHistoryLocals& source, nb::object self, nb::object key,
                            nb::object comprehension) {
    auto G = [&](const char* name) { return lh_global(source, name); };
    auto A = [&](nb::object value, const char* name) {
        return lh_attr(source, std::move(value), name);
    };
    auto nx = [&]() { return G("_nx"); };
    auto attr = [&](nb::object item, const char* name) { return A(std::move(item), name); };
    auto complex_step = [&](nb::object step) {
        nb::object classes = nb::make_tuple(attr(nx(), "complexfloating"), G("complex"));
        return lh_truth(lh_call(G("isinstance"), {LFG(step), LFG(classes)}));
    };
    auto arithmetic = [](binaryfunc function, nb::object a, nb::object b) {
        return lh_check(function(a.ptr(), b.ptr()));
    };
    auto append = [&](nb::object list, nb::object item) {
        lh_call(attr(std::move(list), "append"), {LFG(item)});
    };
    try {
        nb::object size = lh_check(PyList_New(0));
        nb::object num_list = lh_check(PyList_New(1));
        PyList_SET_ITEM(num_list.ptr(), 0, PyLong_FromLong(0));
        nb::object count = lh_call(G("len"), {LFG(key)});
        nb::object iterator = lh_iter(lh_call(G("range"), {LFG(count)}));
        while (nb::object k = lh_next(iterator)) {
            nb::object step = attr(lh_item(key, k), "step");
            nb::object start = attr(lh_item(key, k), "start");
            nb::object stop = attr(lh_item(key, k), "stop");
            if (start.is_none()) start = nb::int_(0);
            if (step.is_none()) step = nb::int_(1);
            if (complex_step(step)) {
                step = lh_call(G("abs"), {LFG(step)});
                append(size, lh_call(G("int"), {LFG(step)}));
            } else {
                nb::object diff = arithmetic(PyNumber_Subtract, stop, start);
                nb::object ratio = arithmetic(PyNumber_TrueDivide, diff, step);
                append(size, lh_call(attr(G("math"), "ceil"), {LFG(ratio)}));
            }
            nb::object tail = nb::make_tuple(start, stop, step);
            // In-place list extension uses the actual list's source protocol.
            nb::object as_list = lh_check(PySequence_List(tail.ptr()));
            num_list = lh_check(PyNumber_InPlaceAdd(num_list.ptr(), as_list.ptr()));
        }
        nb::object result_type = attr(nx(), "result_type");
        nb::object args = lh_check(PySequence_Tuple(num_list.ptr()));
        nb::object typ = lh_check(PyObject_CallObject(result_type.ptr(), args.ptr()));
        nb::object nn;
        if (lh_truth(attr(self, "sparse"))) {
            // CPython 3.11 creates the comprehension function before evaluating
            // zip/GET_ITER. Its builtins snapshot belongs to that creation.
            nb::object comp_function;
            if (!comprehension.is_none())
                comp_function = lh_check(PyFunction_New(comprehension.ptr(), source.source_globals.ptr()));
            nb::object zip_callable = G("zip");
            nb::object size_len = lh_call(G("len"), {LFG(size)});
            nb::object repeated = arithmetic(PyNumber_Multiply, nb::make_tuple(typ), size_len);
            nb::object zipped = lh_iter(lh_call(zip_callable, {LFG(size), LFG(repeated)}));
            if (comp_function.is_valid()) {
                nn = lh_call(comp_function, {LFG(zipped)});
            } else {
            nn = lh_check(PyList_New(0));
            while (nb::object pair = lh_next(zipped)) {
                nb::object pair_iter = lh_iter(pair);
                nb::object x = lh_next(pair_iter), t = lh_next(pair_iter);
                if (!x.is_valid() || !t.is_valid()) {
                    PyErr_Format(PyExc_ValueError, "not enough values to unpack (expected 2, got %d)", x.is_valid() ? 1 : 0);
                    throw nb::python_error();
                }
                if (lh_next(pair_iter).is_valid()) throw nb::value_error("too many values to unpack (expected 2)");
                append(nn, lh_call(attr(nx(), "arange"), {LFG(x)}, {LFG(t)}, lh_kw(source, "dtype")));
            }
            }
        } else {
            // Admission must route the genuine default indices body to
            // lf_grid_indices; a deliberate foreign callable stays a callback.
            nn = lh_call(attr(nx(), "indices"), {LFG(size), LFG(typ)});
        }
        iterator = lh_iter(lh_call(G("enumerate"), {LFG(key)}));
        while (nb::object pair = lh_next(iterator)) {
            nb::object pair_iter = lh_iter(pair);
            nb::object k = lh_next(pair_iter), kk = lh_next(pair_iter);
            if (!k.is_valid() || !kk.is_valid()) {
                PyErr_Format(PyExc_ValueError, "not enough values to unpack (expected 2, got %d)", k.is_valid() ? 1 : 0);
                throw nb::python_error();
            }
            if (lh_next(pair_iter).is_valid()) throw nb::value_error("too many values to unpack (expected 2)");
            nb::object step = attr(kk, "step"), start = attr(kk, "start");
            if (start.is_none()) start = nb::int_(0);
            if (step.is_none()) step = nb::int_(1);
            if (complex_step(step)) {
                step = lh_call(G("int"), {LFG(lh_call(G("abs"), {LFG(step)}))});
                int unequal = PyObject_RichCompareBool(step.ptr(), nb::int_(1).ptr(), Py_NE);
                if (unequal < 0) throw nb::python_error();
                if (unequal) {
                    nb::object numerator = arithmetic(PyNumber_Subtract, attr(kk, "stop"), start);
                    nb::object denom = lh_call(G("float"), {LFG(arithmetic(PyNumber_Subtract, step, nb::int_(1)))});
                    step = arithmetic(PyNumber_TrueDivide, numerator, denom);
                }
            }
            nb::object scaled = arithmetic(PyNumber_Multiply, lh_item(nn, k), step);
            nb::object value = arithmetic(PyNumber_Add, scaled, start);
            if (PyObject_SetItem(nn.ptr(), k.ptr(), value.ptr()) < 0) throw nb::python_error();
        }
        if (lh_truth(attr(self, "sparse"))) {
            nb::object length = lh_call(G("len"), {LFG(size)});
            nb::object initial = lh_check(PyList_New(1));
            PyList_SET_ITEM(initial.ptr(), 0, attr(nx(), "newaxis").release().ptr());
            nb::object slobj = arithmetic(PyNumber_Multiply, initial, length);
            iterator = lh_iter(lh_call(G("range"), {LFG(lh_call(G("len"), {LFG(size)}))}));
            while (nb::object k = lh_next(iterator)) {
                nb::object full = lh_call(G("slice"), {LFG(nb::none()), LFG(nb::none())});
                if (PyObject_SetItem(slobj.ptr(), k.ptr(), full.ptr()) < 0) throw nb::python_error();
                nb::object current = lh_item(nn, k);
                nb::object axis_key = lh_call(G("tuple"), {LFG(slobj)});
                nb::object value = lh_item(current, axis_key);
                if (PyObject_SetItem(nn.ptr(), k.ptr(), value.ptr()) < 0) throw nb::python_error();
                nb::object newaxis = attr(nx(), "newaxis");
                if (PyObject_SetItem(slobj.ptr(), k.ptr(), newaxis.ptr()) < 0) throw nb::python_error();
            }
            return lh_call(G("tuple"), {LFG(nn)});
        }
        return nn;
    } catch (nb::python_error& error) {
        LHHandledException handled(error.value());
        nb::object first=G("IndexError"),second=G("TypeError");
        if (!lf_matches_handler(error.value(),nb::make_tuple(first,second))) throw;
        nb::object step = attr(key, "step"), stop = attr(key, "stop"), start = attr(key, "start");
        if (start.is_none()) start = nb::int_(0);
        if (complex_step(step)) {
            nb::object step_float = lh_call(G("abs"), {LFG(step)});
            nb::object length = lh_call(G("int"), {LFG(step_float)});
            step = length;
            int unequal = PyObject_RichCompareBool(step.ptr(), nb::int_(1).ptr(), Py_NE);
            if (unequal < 0) throw nb::python_error();
            if (unequal) {
                nb::object numerator = arithmetic(PyNumber_Subtract, attr(key, "stop"), start);
                nb::object denominator = lh_call(G("float"), {LFG(arithmetic(PyNumber_Subtract, step, nb::int_(1)))});
                step = arithmetic(PyNumber_TrueDivide, numerator, denominator);
            }
            nb::object typ = lh_call(attr(nx(), "result_type"), {LFG(start), LFG(stop), LFG(step_float)});
            nb::object values = lh_call(attr(nx(), "arange"), {LFG(nb::int_(0)), LFG(length), LFG(nb::int_(1))}, {LFG(typ)}, lh_kw(source, "dtype"));
            return arithmetic(PyNumber_Add, arithmetic(PyNumber_Multiply, values, step), start);
        }
        return lh_call(attr(nx(), "arange"), {LFG(start), LFG(stop), LFG(step)});
    }
}

#undef LFG

PyObject* lf_grid_frame(PyObject*, PyObject* arguments) {
    try {
        Py_ssize_t count = PyTuple_GET_SIZE(arguments);
        if (count < 1 || !PyLong_CheckExact(PyTuple_GET_ITEM(arguments, 0)))
            throw nb::type_error("Grid policy entry is required");
        long entry = PyLong_AsLong(PyTuple_GET_ITEM(arguments, 0));
        if (entry == -1 && PyErr_Occurred()) throw nb::python_error();
        if ((entry != 0 || count != 3) && (entry != 1 || count != 4) && (entry != -2 || count != 1))
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
        if (!PyTuple_CheckExact(payload.ptr()) || PyTuple_GET_SIZE(payload.ptr()) != 3 ||
            PyUnicode_CompareWithASCIIString(PyTuple_GET_ITEM(payload.ptr(), 0), "naparixx.grid.v2") != 0)
            throw nb::type_error("Grid policy payload changed");
        source.source_metadata = nb::borrow<nb::object>(PyTuple_GET_ITEM(payload.ptr(), 1));
        lh_metadata_values(source.source_metadata, 0);
        lh_metadata_values(source.source_metadata, 1);
        auto arg = [&](int position) {
            return nb::borrow<nb::object>(PyTuple_GET_ITEM(arguments, position));
        };
        nb::object comprehension = nb::borrow<nb::object>(PyTuple_GET_ITEM(payload.ptr(), 2));
        if (entry == -2) return lf_grid_sparse_comp(source, {frame, actual}).release().ptr();
        return (entry == 0 ? lf_grid_getitem(source, arg(1), arg(2), comprehension) :
                lf_grid_indices(source, arg(1), arg(2), arg(3))).release().ptr();
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
    if (entry == -2) {
        if (original->co_nlocals != 3 || PyTuple_GET_SIZE(original->co_localsplusnames) != 3 ||
            PyUnicode_CompareWithASCIIString(PyTuple_GET_ITEM(original->co_localsplusnames, 1), "_x") != 0 ||
            PyUnicode_CompareWithASCIIString(PyTuple_GET_ITEM(original->co_localsplusnames, 2), "_t") != 0)
            throw nb::type_error("Grid comprehension local layout changed");
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
