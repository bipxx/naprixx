// Handwritten nd_grid.__getitem__ policy. NumPy BSD-3-Clause notice is retained
// in ../../licenses/NumPy-BSD-3-Clause.txt; see grid_provider.inc attribution.
nb::object lf_grid_getitem(LabelsHistoryLocals& source, const LFGridFastLocals& locals,
                            nb::object comprehension) {
    LFGridGetterLines T{locals};
    auto G = [&](const char* name) { return lh_global(source, name); };
    auto L = [&](const char* name) { return locals.load(name); };
    auto A = [&](nb::object value, const char* name) { return lh_attr(source, std::move(value), name); };
    auto N = [&](const char* name) { return A(G("_nx"), name); };
    auto binary = [](binaryfunc operation, nb::object left, nb::object right) {
        return lh_check(operation(left.ptr(), right.ptr()));
    };
    auto complex_step = [&]() {
        auto classes = [&]() {
            nb::object floating = N("complexfloating"), complex = G("complex");
            return nb::make_tuple(floating, complex);
        };
        // CALL releases its operands before the conditional truth test. Do
        // not keep additional C++ owners across an observable __bool__ call.
        return lh_truth(lh_call(G("isinstance"), {LFG(L("step")), LFG(classes())}));
    };
    auto step_unequal_one = [&]() {
        nb::object value = L("step");
        PyObject* result = PyObject_RichCompare(value.ptr(), nb::int_(1).ptr(), Py_NE);
        value.reset();
        return lh_truth(lh_check(result));
    };
    auto normalized_start = [&](int line) {
        T.line(line);
        if (L("start").is_none()) {T.line(line+1);locals.store("start", nb::int_(0));}
    };
    auto normalized_step = [&](int line) {
        T.line(line);
        if (L("step").is_none()) {T.line(line+1);locals.store("step", nb::int_(1));}
    };
    auto complex_stride = [&](const char* receiver,int line) {
        T.line(line);
        nb::object stop = A(L(receiver), "stop"), start = L("start");
        nb::object numerator = binary(PyNumber_Subtract, std::move(stop), std::move(start));
        nb::object denominator = lh_call(G("float"),
            {LFG(binary(PyNumber_Subtract, L("step"), nb::int_(1)))});
        locals.store("step", binary(PyNumber_TrueDivide, std::move(numerator), std::move(denominator)));
    };
    T.line(147);
    try {
        T.line(148);
        locals.store("size", nb::list());
        T.line(151);
        nb::object initial = lh_check(PyList_New(1));
        PyList_SET_ITEM(initial.ptr(), 0, PyLong_FromLong(0));
        locals.store("num_list", std::move(initial));
        T.line(152);
        nb::object iterator = lh_iter(T.call(78,G("range"),
            {LFG(T.call(77,G("len"),{LFG(L("key"))}))}));
        while (nb::object value = lh_next(iterator)) {
            locals.store("k", std::move(value));
            T.line(153);
            locals.store("step", A(lh_index(LFG(L("key")), LFG(L("k"))), "step"));
            T.line(154);
            locals.store("start", A(lh_index(LFG(L("key")), LFG(L("k"))), "start"));
            T.line(155);
            locals.store("stop", A(lh_index(LFG(L("key")), LFG(L("k"))), "stop"));
            normalized_start(156); normalized_step(158);
            T.line(160);
            if (complex_step()) {
                T.line(161);
                locals.store("step", lh_call(G("abs"), {LFG(L("step"))}));
                T.line(162);
                lh_call(A(L("size"), "append"), {LFG(lh_call(G("int"), {LFG(L("step"))}))});
            } else {
                T.line(164);
                nb::object append=A(L("size"),"append");
                T.line(165);
                auto ratio = [&]() {
                    nb::object stop = L("stop"), start = L("start");
                    nb::object difference = binary(PyNumber_Subtract, std::move(stop), std::move(start));
                    return binary(PyNumber_TrueDivide, std::move(difference), L("step"));
                };
                nb::object count=lh_call(A(G("math"),"ceil"),{LFG(ratio())});
                T.point(72);
                lh_call(std::move(append),{LFG(std::move(count))});
            }
            T.line(166);
            nb::object current = L("num_list");
            nb::object start = L("start"), stop = L("stop"), step = L("step");
            nb::object tail = lh_check(PyList_New(3));
            PyList_SET_ITEM(tail.ptr(), 0, start.release().ptr());
            PyList_SET_ITEM(tail.ptr(), 1, stop.release().ptr());
            PyList_SET_ITEM(tail.ptr(), 2, step.release().ptr());
            locals.store("num_list", binary(PyNumber_InPlaceAdd, std::move(current), std::move(tail)));
            T.point(64);
        }
        iterator.reset();
        T.line(167);
        nb::object result_type = N("result_type");
        nb::object arguments = lh_check(PySequence_Tuple(L("num_list").ptr()));
        PyObject* raw_type = PyObject_CallObject(result_type.ptr(), arguments.ptr());
        // CALL_FUNCTION_EX releases its callable/argument temporaries before
        // STORE_FAST; finalizers may inspect or mutate the source frame here.
        result_type.reset(); arguments.reset();
        locals.store("typ", lh_check(raw_type));
        T.line(168);
        if (lh_truth(A(L("self"), "sparse"))) {
            nb::object function;
            if (!comprehension.is_none()) {
                T.line(169);
                function = lh_check(PyFunction_New(comprehension.ptr(), source.source_globals.ptr()));
            }
            T.line(170);
            auto repeated = [&]() {
                nb::object typ = nb::make_tuple(L("typ"));
                nb::object count = lh_call(G("len"), {LFG(L("size"))});
                return binary(PyNumber_Multiply, std::move(typ), std::move(count));
            };
            nb::object zipped = lh_iter(lh_call(G("zip"), {LFG(L("size")), LFG(repeated())}));
            if (function.is_valid()) {
                T.point(71);
                locals.store("nn", lh_call(std::move(function), {LFG(std::move(zipped))}));
            } else {
                // PEP 709 saves/clears both parent slots after GET_ITER. Normal
                // completion stores nn, then restores x/t; the exception path
                // restores t/x before the enclosing except becomes active.
                T.line(169);
                nb::object saved_x = locals.take("_x"), saved_t = locals.take("_t");
                try {
                    nb::object result=lf_grid_comprehension_body(source,locals,zipped);
                    T.point(70);
                    locals.store("nn",std::move(result));
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
            T.line(172);
            locals.store("nn", lh_call(N("indices"), {LFG(L("size")), LFG(L("typ"))}));
        }
        T.line(173);
        iterator = lh_iter(lh_call(G("enumerate"), {LFG(L("key"))}));
        while (nb::object pair = lh_next(iterator)) {
            auto values = lf_unpack(std::move(pair), 2);
            locals.store("k", std::move(values[0])); locals.store("kk", std::move(values[1]));
            T.line(174);
            locals.store("step", A(L("kk"), "step"));
            T.line(175);
            locals.store("start", A(L("kk"), "start"));
            normalized_start(176); normalized_step(178);
            T.line(180);
            if (complex_step()) {
                T.line(181);
                locals.store("step", lh_call(G("int"), {LFG(lh_call(G("abs"), {LFG(L("step"))}))}));
                T.line(182);
                if (step_unequal_one()) complex_stride("kk",183);
            }
            T.line(184);
            auto scaled = [&]() {
                nb::object item = lh_index(LFG(L("nn")), LFG(L("k")));
                nb::object value = binary(PyNumber_Multiply, std::move(item), L("step"));
                return binary(PyNumber_Add, std::move(value), L("start"));
            };
            lh_set_item(LFG(scaled()), LFG(L("nn")), LFG(L("k")));
            T.point(65);
        }
        iterator.reset();
        T.line(185);
        if (lh_truth(A(L("self"), "sparse"))) {
            T.line(186);
            nb::object axes = lh_check(PyList_New(1));
            PyList_SET_ITEM(axes.ptr(), 0, N("newaxis").release().ptr());
            nb::object count = lh_call(G("len"), {LFG(L("size"))});
            locals.store("slobj", binary(PyNumber_Multiply, std::move(axes), std::move(count)));
            T.line(187);
            iterator = lh_iter(lh_call(G("range"), {LFG(lh_call(G("len"), {LFG(L("size"))}))}));
            while (nb::object value = lh_next(iterator)) {
                locals.store("k", std::move(value));
                T.line(188);
                lh_set_item(LFG(lh_call(G("slice"), {LFG(nb::none()), LFG(nb::none())})),
                            LFG(L("slobj")), LFG(L("k")));
                T.line(189);
                auto sliced = [&]() {
                    nb::object current = lh_index(LFG(L("nn")), LFG(L("k")));
                    nb::object key = lh_call(G("tuple"), {LFG(L("slobj"))});
                    return lh_item(std::move(current), std::move(key));
                };
                lh_set_item(LFG(sliced()), LFG(L("nn")), LFG(L("k")));
                T.line(190);
                lh_set_item(LFG(N("newaxis")), LFG(L("slobj")), LFG(L("k")));
                T.point(66);
            }
            iterator.reset();
            T.line(191);
            return T.returned(lh_call(G("tuple"),{LFG(L("nn"))}),73);
        }
        T.line(192);
        return T.returned(L("nn"),74);
    } catch (nb::python_error& error) {
        T.exception(error);
        LHHandledException handled(error.value());
        T.line(193);
        nb::object first = G("IndexError"), second = G("TypeError");
        if (!lf_matches_handler(error.value(), nb::make_tuple(first, second))) throw;
        try {
        T.line(194);locals.store("step", A(L("key"), "step"));
        T.line(195);
        locals.store("stop", A(L("key"), "stop"));
        T.line(196);
        locals.store("start", A(L("key"), "start"));
        normalized_start(197);
        T.line(199);
        if (complex_step()) {
            T.line(201);
            locals.store("step_float", lh_call(G("abs"), {LFG(L("step"))}));
            T.line(202);
            nb::object value = lh_call(G("int"), {LFG(L("step_float"))});
            locals.store("step", value); locals.store("length", std::move(value));
            T.line(203);if (step_unequal_one()) complex_stride("key",204);
            T.line(205);
            locals.store("typ", lh_call(N("result_type"), {LFG(L("start")), LFG(L("stop")), LFG(L("step_float"))}));
            T.line(206);
            nb::object array = T.call(80,N("arange"), {LFG(nb::int_(0)), LFG(L("length")), LFG(nb::int_(1))},
                                      {LFG(L("typ"))}, lh_kw(source, "dtype"));
            nb::object scaled = binary(PyNumber_Multiply, std::move(array), L("step"));
            return T.returned(binary(PyNumber_Add,std::move(scaled),L("start")),75);
        }
        T.line(208);
        return T.returned(T.call(79,N("arange"),{LFG(L("start")),LFG(L("stop")),LFG(L("step"))}),76);
        } catch(nb::python_error& fallback_error) {
            T.exception(fallback_error,true);
            throw;
        }
    }
}
