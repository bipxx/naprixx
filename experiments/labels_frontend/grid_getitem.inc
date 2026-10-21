// Handwritten nd_grid.__getitem__ policy. NumPy BSD-3-Clause notice is retained
// in ../../licenses/NumPy-BSD-3-Clause.txt; see grid_provider.inc attribution.
nb::object lf_grid_getitem(LabelsHistoryLocals& source, const LFGridFastLocals& locals,
                            nb::object comprehension) {
    auto G = [&](const char* name) { return lh_global(source, name); };
    auto L = [&](const char* name) { return locals.load(name); };
    auto A = [&](nb::object value, const char* name) { return lh_attr(source, std::move(value), name); };
    auto N = [&](const char* name) { return A(G("_nx"), name); };
    auto binary = [](binaryfunc operation, nb::object left, nb::object right) {
        return lh_check(operation(left.ptr(), right.ptr()));
    };
    auto complex_step = [&]() {
        nb::object callable = G("isinstance"), step = L("step");
        nb::object floating = N("complexfloating"), complex = G("complex");
        nb::object classes = nb::make_tuple(floating, complex);
        return lh_truth(lh_call(std::move(callable), {LFG(step), LFG(classes)}));
    };
    auto step_unequal_one = [&]() {
        nb::object value = L("step");
        int result = PyObject_RichCompareBool(value.ptr(), nb::int_(1).ptr(), Py_NE);
        if (result < 0) throw nb::python_error();
        return result != 0;
    };
    auto normalized_start = [&]() {
        if (L("start").is_none()) locals.store("start", nb::int_(0));
    };
    auto normalized_step = [&]() {
        if (L("step").is_none()) locals.store("step", nb::int_(1));
    };
    auto complex_stride = [&](const char* receiver) {
        nb::object stop = A(L(receiver), "stop"), start = L("start");
        nb::object numerator = binary(PyNumber_Subtract, std::move(stop), std::move(start));
        nb::object denominator = lh_call(G("float"),
            {LFG(binary(PyNumber_Subtract, L("step"), nb::int_(1)))});
        locals.store("step", binary(PyNumber_TrueDivide, std::move(numerator), std::move(denominator)));
    };
    try {
        locals.store("size", nb::list());
        nb::object initial = lh_check(PyList_New(1));
        PyList_SET_ITEM(initial.ptr(), 0, PyLong_FromLong(0));
        locals.store("num_list", std::move(initial));
        nb::object iterator = lh_iter(lh_call(G("range"),
            {LFG(lh_call(G("len"), {LFG(L("key"))}))}));
        while (nb::object value = lh_next(iterator)) {
            locals.store("k", std::move(value));
            locals.store("step", A(lh_index(LFG(L("key")), LFG(L("k"))), "step"));
            locals.store("start", A(lh_index(LFG(L("key")), LFG(L("k"))), "start"));
            locals.store("stop", A(lh_index(LFG(L("key")), LFG(L("k"))), "stop"));
            normalized_start(); normalized_step();
            if (complex_step()) {
                locals.store("step", lh_call(G("abs"), {LFG(L("step"))}));
                lh_call(A(L("size"), "append"), {LFG(lh_call(G("int"), {LFG(L("step"))}))});
            } else {
                auto ratio = [&]() {
                    nb::object stop = L("stop"), start = L("start");
                    nb::object difference = binary(PyNumber_Subtract, std::move(stop), std::move(start));
                    return binary(PyNumber_TrueDivide, std::move(difference), L("step"));
                };
                lh_call(A(L("size"), "append"), {LFG(lh_call(A(G("math"), "ceil"), {LFG(ratio())}))});
            }
            nb::object current = L("num_list");
            nb::object start = L("start"), stop = L("stop"), step = L("step");
            nb::object tail = lh_check(PyList_New(3));
            PyList_SET_ITEM(tail.ptr(), 0, start.release().ptr());
            PyList_SET_ITEM(tail.ptr(), 1, stop.release().ptr());
            PyList_SET_ITEM(tail.ptr(), 2, step.release().ptr());
            locals.store("num_list", binary(PyNumber_InPlaceAdd, std::move(current), std::move(tail)));
        }
        iterator.reset();
        nb::object result_type = N("result_type");
        nb::object arguments = lh_check(PySequence_Tuple(L("num_list").ptr()));
        locals.store("typ", lh_check(PyObject_CallObject(result_type.ptr(), arguments.ptr())));
        result_type.reset(); arguments.reset();
        if (lh_truth(A(L("self"), "sparse"))) {
            nb::object function;
            if (!comprehension.is_none())
                function = lh_check(PyFunction_New(comprehension.ptr(), source.source_globals.ptr()));
            auto repeated = [&]() {
                nb::object typ = nb::make_tuple(L("typ"));
                nb::object count = lh_call(G("len"), {LFG(L("size"))});
                return binary(PyNumber_Multiply, std::move(typ), std::move(count));
            };
            nb::object zipped = lh_iter(lh_call(G("zip"), {LFG(L("size")), LFG(repeated())}));
            if (function.is_valid()) {
                locals.store("nn", lh_call(std::move(function), {LFG(zipped)}));
            } else {
                // PEP 709 saves/clears both parent slots after GET_ITER. Normal
                // completion stores nn, then restores x/t; the exception path
                // restores t/x before the enclosing except becomes active.
                nb::object saved_x = locals.take("_x"), saved_t = locals.take("_t");
                try {
                    locals.store("nn", lf_grid_comprehension_body(source, locals, zipped));
                } catch (...) {
                    zipped.reset();
                    locals.store("_t", std::move(saved_t));
                    locals.store("_x", std::move(saved_x));
                    throw;
                }
                zipped.reset();
                locals.store("_x", std::move(saved_x));
                locals.store("_t", std::move(saved_t));
            }
        } else {
            locals.store("nn", lh_call(N("indices"), {LFG(L("size")), LFG(L("typ"))}));
        }
        iterator = lh_iter(lh_call(G("enumerate"), {LFG(L("key"))}));
        while (nb::object pair = lh_next(iterator)) {
            auto values = lf_unpack(std::move(pair), 2);
            locals.store("k", std::move(values[0])); locals.store("kk", std::move(values[1]));
            locals.store("step", A(L("kk"), "step"));
            locals.store("start", A(L("kk"), "start"));
            normalized_start(); normalized_step();
            if (complex_step()) {
                locals.store("step", lh_call(G("int"), {LFG(lh_call(G("abs"), {LFG(L("step"))}))}));
                if (step_unequal_one()) complex_stride("kk");
            }
            auto scaled = [&]() {
                nb::object item = lh_index(LFG(L("nn")), LFG(L("k")));
                nb::object value = binary(PyNumber_Multiply, std::move(item), L("step"));
                return binary(PyNumber_Add, std::move(value), L("start"));
            };
            lh_set_item(LFG(scaled()), LFG(L("nn")), LFG(L("k")));
        }
        iterator.reset();
        if (lh_truth(A(L("self"), "sparse"))) {
            nb::object axes = lh_check(PyList_New(1));
            PyList_SET_ITEM(axes.ptr(), 0, N("newaxis").release().ptr());
            nb::object count = lh_call(G("len"), {LFG(L("size"))});
            locals.store("slobj", binary(PyNumber_Multiply, std::move(axes), std::move(count)));
            iterator = lh_iter(lh_call(G("range"), {LFG(lh_call(G("len"), {LFG(L("size"))}))}));
            while (nb::object value = lh_next(iterator)) {
                locals.store("k", std::move(value));
                lh_set_item(LFG(lh_call(G("slice"), {LFG(nb::none()), LFG(nb::none())})),
                            LFG(L("slobj")), LFG(L("k")));
                auto sliced = [&]() {
                    nb::object current = lh_index(LFG(L("nn")), LFG(L("k")));
                    nb::object key = lh_call(G("tuple"), {LFG(L("slobj"))});
                    return lh_item(std::move(current), std::move(key));
                };
                lh_set_item(LFG(sliced()), LFG(L("nn")), LFG(L("k")));
                lh_set_item(LFG(N("newaxis")), LFG(L("slobj")), LFG(L("k")));
            }
            iterator.reset();
            return lh_call(G("tuple"), {LFG(L("nn"))});
        }
        return L("nn");
    } catch (nb::python_error& error) {
        LHHandledException handled(error.value());
        nb::object first = G("IndexError"), second = G("TypeError");
        if (!lf_matches_handler(error.value(), nb::make_tuple(first, second))) throw;
        locals.store("step", A(L("key"), "step"));
        locals.store("stop", A(L("key"), "stop"));
        locals.store("start", A(L("key"), "start"));
        normalized_start();
        if (complex_step()) {
            locals.store("step_float", lh_call(G("abs"), {LFG(L("step"))}));
            nb::object value = lh_call(G("int"), {LFG(L("step_float"))});
            locals.store("step", value); locals.store("length", std::move(value));
            if (step_unequal_one()) complex_stride("key");
            locals.store("typ", lh_call(N("result_type"), {LFG(L("start")), LFG(L("stop")), LFG(L("step_float"))}));
            nb::object array = lh_call(N("arange"), {LFG(nb::int_(0)), LFG(L("length")), LFG(nb::int_(1))},
                                      {LFG(L("typ"))}, lh_kw(source, "dtype"));
            nb::object scaled = binary(PyNumber_Multiply, std::move(array), L("step"));
            return binary(PyNumber_Add, std::move(scaled), L("start"));
        }
        return lh_call(N("arange"), {LFG(L("start")), LFG(L("stop")), LFG(L("step"))});
    }
}
