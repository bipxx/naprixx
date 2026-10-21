// Handwritten numeric.indices; NumPy notice is retained in grid_provider.inc
// and ../../licenses/NumPy-BSD-3-Clause.txt. Observer hooks do not run policy.
nb::object lf_grid_indices(LabelsHistoryLocals& source,const LFGridFastLocals& locals) {
    LFGridObservedOps O{locals};
    auto G=[&](const char* name){lfgo_op(locals,"global",name);return lh_global(source,name);};
    auto L=[&](const char* name){lfgo_op(locals,"load",name);return locals.load(name);};
    auto S=[&](const char* name,nb::object value){lfgo_op(locals,"store",name);locals.store(name,std::move(value));};
    lfgt_line(locals,0);
    S("dimensions",O.call(G("tuple"),{LFG(L("dimensions"))}));
    lfgt_line(locals,1);
    S("N",O.call(G("len"),{LFG(L("dimensions"))}));
    lfgt_line(locals,2);
    nb::object one=O.constant(nb::make_tuple(1)), n=L("N");
    S("shape",O.binary(PyNumber_Multiply,std::move(one),std::move(n),"*"));
    lfgt_line(locals,3);
    if(O.truth(L("sparse"))) {
        lfgt_line(locals,4); S("res",O.constant(nb::tuple()));
    } else {
        lfgt_line(locals,5);
        auto full_shape=[&](){
            nb::object prefix=O.tuple(L("N")), dimensions=L("dimensions");
            return O.binary(PyNumber_Add,std::move(prefix),std::move(dimensions),"+");
        };
        S("res",O.call(G("empty"),{LFG(full_shape())},{LFG(L("dtype"))},lh_kw(source,"dtype")));
    }
    lfgt_line(locals,6);
    nb::object iterator=O.iter(O.call(G("enumerate"),{LFG(L("dimensions"))}));
    while(nb::object pair=O.next(iterator)) {
        auto values=O.unpack(std::move(pair));
        S("i",std::move(values[0])); S("dim",std::move(values[1]));
        lfgt_line(locals,8);
        nb::object reshape=O.attr(source,O.call(G("arange"),{LFG(L("dim"))},
            {LFG(L("dtype"))},lh_kw(source,"dtype")),"reshape");
        lfgt_line(locals,9);
        nb::object receiver=L("shape"), start=O.constant(nb::none()), stop=L("i");
        nb::object left=O.slice(std::move(receiver),std::move(start),std::move(stop));
        nb::object middle=O.tuple(L("dim"));
        nb::object first=O.binary(PyNumber_Add,std::move(left),std::move(middle),"+");
        receiver=L("shape");
        nb::object index=L("i"), increment=O.constant(nb::int_(1));
        start=O.binary(PyNumber_Add,std::move(index),std::move(increment),"+");
        stop=O.constant(nb::none());
        nb::object right=O.slice(std::move(receiver),std::move(start),std::move(stop));
        nb::object shape=O.binary(PyNumber_Add,std::move(first),std::move(right),"+");
        lfgt_line(locals,10);
        S("idx",O.call(std::move(reshape),{LFG(std::move(shape))}));
        lfgt_line(locals,11);
        if(O.truth(L("sparse"))) {
            lfgt_line(locals,12);
            nb::object current=L("res"), item=O.tuple(L("idx"));
            S("res",O.binary(PyNumber_Add,std::move(current),std::move(item),"+"));
        } else {
            lfgt_line(locals,13);
            nb::object value=L("idx"), target=L("res"), key=L("i");
            O.setitem(std::move(value),std::move(target),std::move(key));
        }
        lfgt_line(locals,7);
    }
    nb::object points=lfgt_points(locals.code);
    if(points.is_valid()) lfgo_align(locals,PyLong_AsLong(PyTuple_GET_ITEM(PyTuple_GET_ITEM(points.ptr(),14),0)));
    iterator.reset();
    lfgt_line(locals,14);
    nb::object result=L("res");
    lfgo_op(locals,"return");
    return result;
}
