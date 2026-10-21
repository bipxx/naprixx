// Handwritten nd_grid.__getitem__ policy. NumPy BSD-3-Clause notice is retained
// in ../../licenses/NumPy-BSD-3-Clause.txt; see grid_provider.inc attribution.
nb::object lf_grid_getitem(LabelsHistoryLocals& source, const LFGridFastLocals& locals,
                            nb::object comprehension) {
    LFGridGetterLines T{locals};
    LFGridObservedOps O{locals};
    auto G = [&](const char* name) { lfgo_op(locals,"global",name);return lh_global(source, name); };
    auto L = [&](const char* name) { lfgo_op(locals,"load",name);return locals.load(name); };
    auto A = [&](nb::object value, const char* name) { return O.attr(source,std::move(value),name); };
    auto N = [&](const char* name) { return A(G("_nx"), name); };
    auto S=[&](const char* name,nb::object value){lfgo_op(locals,"store",name);locals.store(name,std::move(value));};
    auto binary = [&](binaryfunc operation, nb::object left, nb::object right) {
        const char* symbol=operation==PyNumber_Subtract?"-":operation==PyNumber_TrueDivide?"/":
            operation==PyNumber_Multiply?"*":operation==PyNumber_InPlaceAdd?"+=":"+";
        return O.binary(operation,std::move(left),std::move(right),symbol);
    };
    auto complex_step = [&]() {
        auto classes = [&]() {
            nb::object floating = N("complexfloating"), complex = G("complex");
            return O.pack(std::move(floating), std::move(complex));
        };
        // CALL releases its operands before the conditional truth test. Do
        // not keep additional C++ owners across an observable __bool__ call.
        return O.truth(T.call(-1,G("isinstance"), {LFG(L("step")), LFG(classes())}));
    };
    auto step_unequal_one = [&]() {
        nb::object value = L("step");
        nb::object one=O.constant(nb::int_(1));
        lfgo_op(locals,"compare","!=");
        PyObject* result = PyObject_RichCompare(value.ptr(),one.ptr(),Py_NE);
        one.reset();
        value.reset();
#if PY_VERSION_HEX >= 0x030D0000
        bool truth=lh_truth(lh_check(result));
        long target=lfgo_op(locals,"branch-truth");
        if(!truth)lfgo_branch(locals,target);
        return truth;
#else
        return O.truth(lh_check(result));
#endif
    };
    auto normalized_start = [&](int line) {
        T.line(line);
        if (O.is_none(L("start"))) {T.line(line+1);S("start", O.constant(nb::int_(0)));}
    };
    auto normalized_step = [&](int line) {
        T.line(line);
        if (O.is_none(L("step"))) {T.line(line+1);S("step", O.constant(nb::int_(1)));}
    };
    auto complex_stride = [&](const char* receiver,int line) {
        T.line(line);
        nb::object stop = A(L(receiver), "stop"), start = L("start");
        nb::object numerator = binary(PyNumber_Subtract, std::move(stop), std::move(start));
        nb::object denominator = T.call(-1,G("float"),
            {LFG(([&](){nb::object step=L("step"),one=O.constant(nb::int_(1));return binary(PyNumber_Subtract,std::move(step),std::move(one));})())});
        S("step", binary(PyNumber_TrueDivide, std::move(numerator), std::move(denominator)));
    };
    T.line(147);
    try {
        T.line(148);
        S("size", O.list());
        T.line(151);
        nb::object initial = O.list({LFG(O.constant(nb::int_(0)))});
        S("num_list", std::move(initial));
        T.line(152);
        nb::object iterator = O.iter(T.call(78,G("range"),
            {LFG(T.call(77,G("len"),{LFG(L("key"))}))}));
        while (nb::object value = O.next(iterator)) {
            S("k", std::move(value));
            T.line(153);
            S("step", A(O.index(LFG(L("key")),LFG(L("k"))), "step"));
            T.line(154);
            S("start", A(O.index(LFG(L("key")),LFG(L("k"))), "start"));
            T.line(155);
            S("stop", A(O.index(LFG(L("key")),LFG(L("k"))), "stop"));
            normalized_start(156); normalized_step(158);
            T.line(160);
            if (complex_step()) {
                T.line(161);
                S("step", T.call(-1,G("abs"), {LFG(L("step"))}));
                T.line(162);
                T.call(-1,A(L("size"), "append"), {LFG(T.call(-1,G("int"), {LFG(L("step"))}))});
            } else {
                T.line(164);
                nb::object append=A(L("size"),"append");
                T.line(165);
                auto ratio = [&]() {
                    nb::object stop = L("stop"), start = L("start");
                    nb::object difference = binary(PyNumber_Subtract, std::move(stop), std::move(start));
                    return binary(PyNumber_TrueDivide, std::move(difference), L("step"));
                };
                nb::object count=T.call(-1,A(G("math"),"ceil"),{LFG(ratio())});
                T.point(72);
                T.call(-1,std::move(append),{LFG(std::move(count))});
            }
            T.line(166);
            nb::object current = L("num_list");
            nb::object start = L("start"), stop = L("stop"), step = L("step");
            nb::object tail = O.list({LFG(std::move(start)),LFG(std::move(stop)),LFG(std::move(step))});
            S("num_list", binary(PyNumber_InPlaceAdd, std::move(current), std::move(tail)));
            T.point(64);
        }
        iterator.reset();
        T.line(167);
        nb::object result_type = N("result_type");
        nb::object arguments = lh_check(PySequence_Tuple(L("num_list").ptr()));
        lfgo_op(locals,"starcall");
        PyObject* raw_type = PyObject_CallObject(result_type.ptr(), arguments.ptr());
        // CALL_FUNCTION_EX releases its callable/argument temporaries before
        // STORE_FAST; finalizers may inspect or mutate the source frame here.
        result_type.reset(); arguments.reset();
        S("typ", lh_check(raw_type));
        T.line(168);
        if (O.truth(A(L("self"), "sparse"))) {
            nb::object function;
            if (!comprehension.is_none()) {
                T.line(169);
                lfgo_op(locals,"const");lfgo_op(locals,"function");
                function = lh_check(PyFunction_New(comprehension.ptr(), source.source_globals.ptr()));
            }
            T.line(170);
            auto repeated = [&]() {
                nb::object typ = O.tuple(L("typ"));
                nb::object count = T.call(-1,G("len"), {LFG(L("size"))});
                return binary(PyNumber_Multiply, std::move(typ), std::move(count));
            };
            nb::object zipped = T.call(-1,G("zip"), {LFG(L("size")), LFG(repeated())});
            if(function.is_valid()) T.point(71);
            zipped=O.iter(std::move(zipped));
            if (function.is_valid()) {
                S("nn", T.call(-1,std::move(function), {LFG(std::move(zipped))}));
            } else {
                // PEP 709 saves/clears both parent slots after GET_ITER. Normal
                // completion stores nn, then restores x/t; the exception path
                // restores t/x before the enclosing except becomes active.
                T.line(169);
                lfgo_op(locals,"take","_x");nb::object saved_x=locals.take("_x");
                lfgo_op(locals,"take","_t");nb::object saved_t=locals.take("_t");
                try {
                    nb::object result=lf_grid_comprehension_body(source,locals,zipped);
                    T.point(70);
                    S("nn",std::move(result));
                } catch (...) {
                    zipped.reset();
                    S("_t", std::move(saved_t));
                    S("_x", std::move(saved_x));
                    throw;
                }
                zipped.reset();
                S("_x", std::move(saved_x));
                S("_t", std::move(saved_t));
            }
        } else {
            T.line(172);
            S("nn", T.call(-1,N("indices"), {LFG(L("size")), LFG(L("typ"))}));
        }
        T.line(173);
        iterator = O.iter(T.call(-1,G("enumerate"), {LFG(L("key"))}));
        while (nb::object pair = O.next(iterator)) {
            auto values = O.unpack(std::move(pair));
            S("k", std::move(values[0])); S("kk", std::move(values[1]));
            T.line(174);
            S("step", A(L("kk"), "step"));
            T.line(175);
            S("start", A(L("kk"), "start"));
            normalized_start(176); normalized_step(178);
            T.line(180);
            if (complex_step()) {
                T.line(181);
                S("step", T.call(-1,G("int"), {LFG(T.call(-1,G("abs"), {LFG(L("step"))}))}));
                T.line(182);
                if (step_unequal_one()) complex_stride("kk",183);
            }
            T.line(184);
            auto scaled = [&]() {
                nb::object item = O.index(LFG(L("nn")),LFG(L("k")));
                nb::object value = binary(PyNumber_Multiply, std::move(item), L("step"));
                return binary(PyNumber_Add, std::move(value), L("start"));
            };
            O.set_index(LFG(scaled()), LFG(L("nn")), LFG(L("k")));
            T.point(65);
        }
        iterator.reset();
        T.line(185);
        if (O.truth(A(L("self"), "sparse"))) {
            T.line(186);
            nb::object axes=O.list({LFG(N("newaxis"))});
            nb::object count = T.call(-1,G("len"), {LFG(L("size"))});
            S("slobj", binary(PyNumber_Multiply, std::move(axes), std::move(count)));
            T.line(187);
            iterator = O.iter(T.call(-1,G("range"), {LFG(T.call(-1,G("len"), {LFG(L("size"))}))}));
            while (nb::object value = O.next(iterator)) {
                S("k", std::move(value));
                T.line(188);
                O.set_index(LFG(T.call(-1,G("slice"), {LFG(O.constant(nb::none())), LFG(O.constant(nb::none()))})),
                            LFG(L("slobj")), LFG(L("k")));
                T.line(189);
                auto sliced = [&]() {
                    nb::object current = O.index(LFG(L("nn")),LFG(L("k")));
                    nb::object key = T.call(-1,G("tuple"), {LFG(L("slobj"))});
                    return O.item(std::move(current), std::move(key));
                };
                O.set_index(LFG(sliced()), LFG(L("nn")), LFG(L("k")));
                T.line(190);
                O.set_index(LFG(N("newaxis")), LFG(L("slobj")), LFG(L("k")));
                T.point(66);
            }
            iterator.reset();
            T.line(191);
            return T.returned(T.call(-1,G("tuple"),{LFG(L("nn"))}),73);
        }
        T.line(192);
        return T.returned(L("nn"),74);
    } catch (nb::python_error& error) {
        T.exception(error);
        LHHandledException handled(error.value());
        T.line(193);
        try {
        T.error_point(86);nb::object first = G("IndexError");
        T.error_point(87);nb::object second = G("TypeError");
        T.error_point(88);bool matches=lf_matches_handler(error.value(), nb::make_tuple(first, second));
        T.error_point(-1);
        if (!matches) {
            T.reraise(error);
            throw;
        }
        T.line(194);S("step", A(L("key"), "step"));
        T.line(195);
        S("stop", A(L("key"), "stop"));
        T.line(196);
        S("start", A(L("key"), "start"));
        normalized_start(197);
        T.line(199);
        if (complex_step()) {
            T.line(201);
            S("step_float", T.call(-1,G("abs"), {LFG(L("step"))}));
            T.line(202);
            nb::object value = T.call(-1,G("int"), {LFG(L("step_float"))});
            S("step", value); S("length", std::move(value));
            T.line(203);if (step_unequal_one()) complex_stride("key",204);
            T.line(205);
            S("typ", T.call(-1,N("result_type"), {LFG(L("start")), LFG(L("stop")), LFG(L("step_float"))}));
            T.line(206);
            nb::object array = T.call(80,N("arange"), {LFG(O.constant(nb::int_(0))), LFG(L("length")), LFG(O.constant(nb::int_(1)))},
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
