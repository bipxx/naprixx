#include "collection_bindings.hpp"
#include "collection_core.hpp"
#include "collection_protocol.hpp"
#include "iterator_frame.hpp"
#include <limits>
#include <optional>

namespace nx {
namespace nb = nanobind;
namespace {
using namespace collections;
nb::object n(const char* name) { return global("napari.utils.events.containers._nested_list", name); }
nb::object append_index(nb::handle prefix, nb::handle index) {
    PyObject* raw = PySequence_List(prefix.ptr());
    if (!raw) {
        // Starred tuple construction distinguishes missing iteration support
        // from an exception raised by a provider's actual iterator protocol.
        if (PyErr_ExceptionMatches(PyExc_TypeError) &&
            Py_TYPE(prefix.ptr())->tp_iter == nullptr && !PySequence_Check(prefix.ptr())) {
            PyErr_Clear();
            PyErr_Format(PyExc_TypeError, "Value after * must be an iterable, not %.200s",
                         Py_TYPE(prefix.ptr())->tp_name);
        }
        throw nb::python_error();
    }
    nb::list values = nb::steal<nb::list>(raw);
    values.append(index);
    return checked(PyList_AsTuple(values.ptr()));
}
nb::tuple star_last(nb::handle supplied) {
    nb::object iterator = checked(PyObject_GetIter(supplied.ptr()));
    nb::object first = checked(PySequence_List(iterator.ptr()));
    if (nb::len(first) == 0) throw nb::value_error("not enough values to unpack (expected at least 1, got 0)");
    nb::object last = first.attr("pop")();
    return nb::make_tuple(first, last);
}
nb::object ensure_index(nb::object index) {
    if (instance(index, n("slice")) || instance(index, n("int"))) return nb::make_tuple(index);
    if (instance(index, n("tuple"))) return index;
    error(PyExc_TypeError, message({nb::str("Invalid nested index: "), formatted(index), nb::str(". Must be an int or tuple")}));
}
nb::object split_index(nb::handle supplied) {
    nb::object index = n("ensure_tuple_index")(supplied);
    if (truth(index)) {
        nb::tuple parts = star_last(index);
        nb::object first = parts[0], last = parts[1];
        for (nb::handle parent : nb::iter(first))
            if (!instance(parent, n("int"))) throw nb::value_error("The parent index must be a tuple of int");
        nb::object cast = n("cast"), type = n("ParentIndex"), tuple = n("tuple");
        return nb::make_tuple(cast(type, tuple(first)), last);
    }
    return nb::make_tuple(n("ParentIndex")(nb::tuple()), nb::int_(-1));
}
nb::tuple split(nb::handle supplied) { return pair(n("split_nested_index")(supplied)); }

struct NestedMoveIterator : IteratorState, IteratorFrame {
    bool pending = false;
    std::int64_t margin = 0;
    std::optional<app::NestedMoveLedger> ledger;
    app::NestedMoveLedger::Path source_parent, destination_parent;
    std::int64_t source_row = 0, destination_row = 0;
    void gc_clear() {
        exhausted = true;
        ledger.reset();
        release_frame();
    }
};
struct NestedIndexIterator : IteratorState, IteratorFrame {
    enum class Phase { entries, item, delegate } phase = Phase::entries;
    void gc_clear() {
        exhausted = true;
        release_frame();
    }
};
bool narrow(nb::handle value, std::int64_t margin, std::int64_t& number) {
    if (!PyLong_CheckExact(value.ptr())) return false;
    int overflow = 0;
    number = PyLong_AsLongLongAndOverflow(value.ptr(), &overflow);
    if (overflow) return false;
    if (PyErr_Occurred()) throw nb::python_error();
    const auto max = std::numeric_limits<std::int64_t>::max();
    return number >= -max + margin && number <= max - margin;
}
bool native_path(nb::handle value, std::int64_t margin, app::NestedMoveLedger::Path& path, bool allow_int = true) {
    std::int64_t row = 0;
    if (allow_int && narrow(value, margin, row)) { path.push_back(row); return true; }
    if (!PyTuple_CheckExact(value.ptr())) return false;
    for (nb::handle element : nb::borrow<nb::tuple>(value)) {
        if (!narrow(element, margin, row)) return false;
        path.push_back(row);
    }
    return true;
}
nb::tuple python_path(const app::NestedMoveLedger::Path& path) {
    nb::list values;
    for (auto index : path) values.append(nb::int_(index));
    return nb::cast<nb::tuple>(checked(PyList_AsTuple(values.ptr())));
}
nb::object count_before(nb::handle values, nb::handle row) {
    nb::object count = nb::int_(0);
    for (nb::handle value : nb::iter(values))
        count = add(count, checked(PyObject_RichCompare(value.ptr(), row.ptr(), Py_LE)));
    return count;
}
void initialize_moves(nb::handle iterator, NestedMoveIterator& state) {
    nb::dict values = frame(iterator);
    nb::object self = values["self"], sources = values["sources"], supplied = values["destination"];
    nb::tuple parts = split(supplied);
    nb::object parent = parts[0], row = parts[1];
    if (instance(row, n("slice"))) throw nb::type_error("Destination index may not be a slice");
    nb::object cast = n("cast"), int_type = n("int");
    row = cast(int_type, self.attr("_non_negative_index")(parent, row));
    values["parent"] = parent; values["row"] = row;
    nb::object default_dict = n("defaultdict");
    values["popped"] = default_dict(n("list"));
    values["dumped"] = nb::list();
    nb::object sort = n("sorted");
    nb::object sorted = sort(sources, nb::arg("reverse") = true);
    values["sources_snapshot"] = sorted;
    values["iterator"] = checked(PyObject_GetIter(sorted.ptr()));
    // Only inspect a builtin list's native size. A sorting override may
    // return a one-shot iterator or a list subclass with length hooks.
    nb::object builtin_sort = nb::module_::import_("builtins").attr("sorted");
    const bool ordinary_sort = PyCFunction_Check(sort.ptr()) && sort.is(builtin_sort);
    const auto size = ordinary_sort && PyList_CheckExact(sorted.ptr()) ?
        static_cast<std::size_t>(PyList_GET_SIZE(sorted.ptr())) :
        static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max());
    if (size < static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max() / 4)) {
        const auto margin = state.margin = static_cast<std::int64_t>(size) * 2 + 2;
        std::int64_t destination = 0;
        bool standard = narrow(row, margin, destination) &&
                        native_path(parent, margin, state.destination_parent, false);
        for (nb::handle source : nb::iter(sorted)) {
            app::NestedMoveLedger::Path path;
            if (!native_path(source, margin, path)) { standard = false; break; }
        }
        nb::object builtin_defaultdict = nb::module_::import_("_collections").attr("defaultdict");
        nb::object popped = values["popped"];
        if (standard && type_of(popped).is(builtin_defaultdict)) state.ledger.emplace();
    }
    state.started = true;
}
nb::object advance_moves(nb::handle iterator, NestedMoveIterator& state) {
    if (!state.started) initialize_moves(iterator, state);
    nb::dict values = frame(iterator);
    nb::object self = values["self"], parent = values["parent"], destination = values["row"];
    nb::object popped = values["popped"], dumped = values["dumped"], sources = values["iterator"];
    if (state.pending) {
        if (state.ledger) state.ledger->commit(state.source_parent, state.source_row, state.destination_row);
        else {
            nb::object source_parent = values["source_parent"], source_row = values["source_row"];
            item(popped, source_parent).attr("append")(source_row);
            nb::object append = dumped.attr("append");
            nb::object decrement = values["decrement"];
            append(subtract(destination, decrement));
        }
        state.pending = false;
    }
    while (nb::object supplied = next_value(sources)) {
        nb::object index = supplied;
        if (state.ledger) {
            app::NestedMoveLedger::Path path;
            if (!native_path(supplied, 0, path)) throw nb::type_error("invalid ordinary nested index");
            if (path.empty()) throw nb::index_error("Group cannot move itself");
            path = state.ledger->shifted(std::move(path), state.destination_parent.size());
            index = python_path(path);
        } else {
          if (instance(index, n("int")) || instance(index, n("slice"))) index = nb::make_tuple(index);
          if (compare(index, nb::tuple())) throw nb::index_error("Group cannot move itself");
          nb::object parent_length = length(parent);
          if (compare(length(index), parent_length, Py_GT)) {
            nb::object parts = n("list")(index);
            if (instance(item(parts, parent_length), n("slice")))
                error(PyExc_NotImplementedError, nb::str("Can't yet deal with slice source indices in multimove"));
            nb::object position = item(parts, parent_length);
            nb::object increase = nb::int_(0);
            // Reload the target for each comparator, as the source generator
            // expression does; provider operators may mutate protocol objects.
            for (nb::handle previous : nb::iter(dumped))
                increase = add(increase, checked(PyObject_RichCompare(previous.ptr(), item(parts, parent_length).ptr(), Py_LE)));
            set_item(parts, parent_length, inplace_add(position, increase));
            index = n("tuple")(parts);
          }
        }
        nb::tuple split_values = split(index);
        nb::object source_parent = split_values[0], source_row = split_values[1];
        if (instance(source_row, n("slice"))) throw nb::type_error("Terminal source index may not be a slice");
        if (compare(source_row, nb::int_(0), Py_LT)) source_row = inplace_add(source_row, length(item(self, source_parent)));
        app::NestedMoveLedger::Path native_parent;
        std::int64_t native_row = 0;
        if (state.ledger && (!native_path(source_parent, state.margin, native_parent, false) ||
                             !narrow(source_row, state.margin, native_row))) {
            // A public split hook can change representations between yields.
            // Transfer numeric history once, then continue native protocol
            // control with those provider objects; never rerun a Python plan.
            for (const auto& record : state.ledger->records()) {
                item(popped, python_path(std::get<0>(record))).attr("append")(nb::int_(std::get<1>(record)));
                dumped.attr("append")(nb::int_(std::get<2>(record)));
            }
            state.ledger.reset();
        }
        nb::object decrement;
        if (state.ledger) {
            source_row = inplace_subtract(source_row, nb::int_(state.ledger->removed_before(native_parent, native_row)));
            decrement = nb::int_(state.ledger->removed_before(state.destination_parent, nb::cast<std::int64_t>(destination)));
        } else {
            nb::object previous = popped.attr("get")(source_parent, nb::list());
            source_row = inplace_subtract(source_row, count_before(previous, source_row));
            previous = popped.attr("get")(parent, nb::list());
            decrement = count_before(previous, destination);
        }
        if (compare(source_parent, parent) && compare(source_row, subtract(destination, decrement))) continue;
        values["source_parent"] = source_parent; values["source_row"] = source_row; values["decrement"] = decrement;
        state.pending = true;
        if (state.ledger) {
            state.source_parent = native_parent;
            state.source_row = nb::cast<std::int64_t>(source_row);
            state.destination_row = nb::cast<std::int64_t>(subtract(destination, decrement));
        }
        return nb::make_tuple(append_index(source_parent, source_row), append_index(parent, subtract(destination, decrement)));
    }
    return {};
}

nb::object resume_delegate(nb::handle delegate, nb::handle sent) {
    if (sent.is_none()) return next_value(delegate);
    try { return delegate.attr("send")(sent); }
    catch (const nb::python_error& failure) { if (!failure.matches(PyExc_StopIteration)) throw; return {}; }
}
nb::object advance_indices(nb::handle iterator, nb::handle sent) {
    auto& state = nb::cast<NestedIndexIterator&>(iterator);
    nb::dict values = frame(iterator);
    nb::object self = values["self"], root = values["root"];
    if (!state.started) {
        nb::object enumerate = n("enumerate");
        nb::object start = values["start"], stop = values["stop"];
        nb::object selected = events::slice(self, start, stop);
        values["entries"] = checked(PyObject_GetIter(enumerate(selected).ptr()));
        values["delegate"] = nb::none();
        state.started = true;
    }
    for (;;) {
        if (state.phase == NestedIndexIterator::Phase::delegate) {
            nb::object delegated = values["delegate"];
            nb::object result = resume_delegate(delegated, sent);
            if (result.is_valid()) return result;
            values["delegate"] = nb::none();
            state.phase = NestedIndexIterator::Phase::entries;
        } else if (state.phase == NestedIndexIterator::Phase::item) {
            nb::object child = values["item"];
            if (instance(child, n("NestableEventedList"))) {
                nb::object method = child.attr("_iter_indices");
                nb::object index = values["index"];
                nb::object result = method(nb::arg("root") = append_index(root, index));
                values["delegate"] = checked(PyObject_GetIter(result.ptr()));
                state.phase = NestedIndexIterator::Phase::delegate;
                sent = nb::none();
                continue;
            }
            state.phase = NestedIndexIterator::Phase::entries;
        }
        nb::object entries = values["entries"];
        nb::object next = next_value(entries);
        if (!next.is_valid()) return {};
        nb::tuple parts = pair(next);
        nb::object index = parts[0], child = parts[1];
        values["index"] = index; values["item"] = child;
        state.phase = NestedIndexIterator::Phase::item;
        return truth(root) ? append_index(root, index) : index;
    }
}
nb::object send_indices(nb::handle self, nb::handle sent) {
    auto& state = nb::cast<NestedIndexIterator&>(self);
    if (state.exhausted) throw nb::stop_iteration();
    Running active(state);
    if (!state.started && !sent.is_none()) throw nb::type_error("can't send non-None value to a just-started generator");
    nb::object result;
    try { result = advance_indices(self, sent); }
    catch (const nb::python_error& failure) {
        discard(self, state);
        if (failure.matches(PyExc_StopIteration)) raise_exception(failure.value(), true);
        throw;
    }
    if (!result.is_valid()) { discard(self, state); throw nb::stop_iteration(); }
    return result;
}
void close_delegate(nb::handle self) {
    auto& state = nb::cast<NestedIndexIterator&>(self);
    if (state.phase != NestedIndexIterator::Phase::delegate) return;
    nb::object delegate = frame(self)["delegate"];
    if (!delegate.is_none()) {
        nb::object close = optional_attribute(delegate, "close");
        if (close.is_valid()) close();
    }
}
void close_indices(nb::handle self) {
    auto& state = nb::cast<NestedIndexIterator&>(self);
    if (state.exhausted) return;
    Running active(state);
    try { close_delegate(self); }
    catch (const nb::python_error&) { discard(self, state); throw; }
    discard(self, state);
}
nb::object throw_indices(nb::handle self, nb::args args) {
    auto& state = nb::cast<NestedIndexIterator&>(self);
    if (args.size() < 1 || args.size() > 3) normalize_throw(args);
    if (state.exhausted) raise_exception(normalize_throw(args), false);
    Running active(state);
    try {
        nb::object supplied = args[0];
        if (PyErr_GivenExceptionMatches(supplied.ptr(), PyExc_GeneratorExit)) close_delegate(self);
        else if (state.phase == NestedIndexIterator::Phase::delegate) {
            nb::object delegate = frame(self)["delegate"];
            nb::object method = optional_attribute(delegate, "throw");
            if (method.is_valid()) {
                try { return checked(PyObject_CallObject(method.ptr(), args.ptr())); }
                catch (const nb::python_error& failure) {
                    if (!failure.matches(PyExc_StopIteration)) throw;
                }
                frame(self)["delegate"] = nb::none();
                state.phase = NestedIndexIterator::Phase::entries;
                nb::object result = advance_indices(self, nb::none());
                if (result.is_valid()) return result;
                discard(self, state);
                throw nb::stop_iteration();
            }
        }
        nb::object exception = normalize_throw(args);
        discard(self, state);
        raise_exception(exception, true);
    } catch (const nb::python_error& failure) {
        discard(self, state);
        if (failure.matches(PyExc_StopIteration)) raise_exception(failure.value(), true);
        throw;
    }
}

} // namespace
void bind_nested_collections(nb::module_& m) {
    nb::class_<NestedMoveIterator>(m, "NestedMoveIterator", nb::type_slots(iterator_frame_slots<NestedMoveIterator>()), nb::is_weak_referenceable())
        .def_prop_ro("frame", [](const NestedMoveIterator& state) { return state.frame(); })
        .def("__iter__", [](nb::object self) -> nb::object { return self; })
        .def("__next__", [](nb::handle self) -> nb::object { return resume<NestedMoveIterator>(self, nb::none(), advance_moves); })
        .def("send", [](nb::handle self, nb::object sent) -> nb::object { return resume<NestedMoveIterator>(self, sent, advance_moves); }, nb::arg("sent").none())
        .def("close", &close<NestedMoveIterator>)
        .def("throw", &throw_into<NestedMoveIterator>);
    nb::class_<NestedIndexIterator>(m, "NestedIndexIterator", nb::type_slots(iterator_frame_slots<NestedIndexIterator>()), nb::is_weak_referenceable())
        .def_prop_ro("frame", [](const NestedIndexIterator& state) { return state.frame(); })
        .def("__iter__", [](nb::object self) -> nb::object { return self; })
        .def("__next__", [](nb::handle self) -> nb::object { return send_indices(self, nb::none()); })
        .def("send", &send_indices, nb::arg("sent").none())
        .def("close", &close_indices)
        .def("throw", &throw_indices);
    m.def("nested_ensure_index", &ensure_index, nb::arg("index").none());
    m.def("nested_split_index", &split_index, nb::arg("index").none());
    m.def("nested_getitem", [](nb::handle self, nb::handle key, nb::handle super) -> nb::object {
        if (instance(key, n("tuple"))) {
            nb::object value = nb::borrow<nb::object>(self);
            for (nb::handle index : nb::iter(key)) {
                if (!instance(value, n("MutableSequence")))
                    error(PyExc_IndexError, message({nb::str("index out of range: "), formatted(key)}));
                value = item(value, index);
            }
            return value;
        }
        return super.attr("__getitem__")(key);
    }, nb::arg("self").none(), nb::arg("key").none(), nb::arg("super"));
    m.def("nested_setitem", [](nb::handle self, nb::handle key, nb::object value, nb::handle super) {
        if (instance(value, n("list"))) value = self.attr("__class__")(value);
        if (instance(key, n("tuple"))) {
            nb::tuple parts = split(key);
            item(self, parts[0]).attr("__setitem__")(parts[1], value);
            return;
        }
        self.attr("_connect_child_emitters")(value);
        super.attr("__setitem__")(key, value);
    }, nb::arg("self").none(), nb::arg("key").none(), nb::arg("value").none(), nb::arg("super"));
    m.def("nested_delitem_indices", [](nb::handle self, nb::handle key, nb::handle super) -> nb::object {
        if (!instance(key, n("tuple"))) return super.attr("_delitem_indices")(key);
        nb::tuple parts = split(key);
        nb::object parent = parts[0], row = parts[1], indices;
        if (instance(row, n("slice"))) {
            nb::object sorted = n("sorted"), range = n("range"), method = row.attr("indices");
            nb::object bounds = method(length(parent));
            nb::object values = checked(PyObject_CallObject(range.ptr(), bounds.ptr()));
            indices = sorted(values, nb::arg("reverse") = true);
        } else { nb::list values; values.append(row); indices = values; }
        nb::list result;
        for (nb::handle index : nb::iter(indices)) result.append(nb::make_tuple(item(self, parent), index));
        return result;
    }, nb::arg("self").none(), nb::arg("key").none(), nb::arg("super"));
    m.def("nested_insert", [](nb::handle self, nb::handle index, nb::object value, nb::handle super) {
        if (instance(value, n("list"))) value = self.attr("__newlike__")(value);
        super.attr("insert")(index, value);
    }, nb::arg("self").none(), nb::arg("index").none(), nb::arg("value").none(), nb::arg("super"));
    m.def("nested_reemit", [](nb::handle self, nb::handle event) {
        if (optional_attribute(event, "index").is_valid()) {
            nb::object index = self.attr("index")(event.attr("source"));
            nb::object original = n("ensure_tuple_index")(event.attr("index"));
            nb::list values; values.append(index);
            values.attr("extend")(original);
            nb::object combined = checked(PyList_AsTuple(values.ptr()));
            for (const char* name : {"index", "new_index"})
                if (optional_attribute(event, name).is_valid()) set_attr(event, name, combined);
        }
        nb::object emitter;
        if (instance(event.attr("source"), n("NestableEventedList"))) {
            nb::object get = n("getattr"), group = self.attr("events"), type = event.attr("type"), fallback = self.attr("events");
            emitter = get(group, type, fallback);
        } else emitter = self.attr("events");
        if (!optional_attribute(event, "index").is_valid()) {
            nb::object context = n("contextlib").attr("suppress")(n("ValueError"));
            with_context(context, [&] { nb::object get = self.attr("index"); set_attr(event, "index", get(event.attr("source"))); });
        }
        emitter(event);
    }, nb::arg("self").none(), nb::arg("event").none());
    m.def("nested_non_negative_index", [](nb::handle self, nb::handle parent, nb::object row) -> nb::object {
        nb::object group = item(self, parent);
        if (instance(row, n("int")) && compare(row, nb::int_(0), Py_LT)) row = inplace_add(row, add(length(group), nb::int_(1)));
        return row;
    }, nb::arg("self").none(), nb::arg("parent").none(), nb::arg("row").none());
    m.def("nested_move_iterator", [](nb::handle self, nb::handle sources, nb::handle destination) -> nb::object {
        nb::object result = nb::cast(NestedMoveIterator{});
        nb::dict values; values["self"] = self; values["sources"] = sources; values["destination"] = destination;
        nb::inst_ptr<NestedMoveIterator>(result)->frame_owner = values;
        return result;
    }, nb::arg("self").none(), nb::arg("sources").none(), nb::arg("destination").none());
    m.def("nested_move", [](nb::handle self, nb::object source, nb::object destination) -> bool {
        n("logger").attr("debug")("move(src_index=%s, dest_index=%s)", source, destination);
        nb::tuple src = split(source), dest = split(destination);
        nb::object source_parent = src[0], source_row = src[1], destination_parent = dest[0], destination_row = dest[1];
        destination_row = self.attr("_non_negative_index")(destination_parent, destination_row);
        destination = append_index(destination_parent, destination_row);
        if (instance(source_row, n("slice"))) throw nb::type_error("Terminal source index may not be a slice");
        if (instance(destination_row, n("slice"))) throw nb::type_error("Destination index may not be a slice");
        if (compare(source_row, nb::tuple())) throw nb::value_error("Group cannot move itself");
        if (compare(source_parent, destination_parent) && instance(destination_row, n("int"))) {
            if (compare(destination_row, source_row, Py_GT)) destination_row = inplace_subtract(destination_row, nb::int_(1));
            if (compare(source_row, destination_row)) return false;
        }
        self.attr("events").attr("moving")(nb::arg("index") = source, nb::arg("new_index") = destination);
        nb::object group = item(self, destination_parent), value;
        nb::object context = self.attr("events").attr("blocker_all")();
        with_context(context, [&] {
            value = item(self, source_parent).attr("pop")(source_row);
            group.attr("insert")(destination_row, value);
        });
        // A suppressing provider context may leave value unbound, just as the
        // original local-variable access does after the with statement.
        if (!value.is_valid()) {
#if PY_VERSION_HEX >= 0x030C0000
            error(PyExc_UnboundLocalError, nb::str("cannot access local variable 'value' where it is not associated with a value"));
#else
            error(PyExc_UnboundLocalError, nb::str("local variable 'value' referenced before assignment"));
#endif
        }
        self.attr("events").attr("moved")(nb::arg("index") = source, nb::arg("new_index") = destination, nb::arg("value") = value);
        self.attr("events").attr("reordered")(nb::arg("value") = self);
        return true;
    }, nb::arg("self").none(), nb::arg("source").none(), nb::arg("destination").none());
    m.def("nested_type_check", [](nb::handle self, nb::object value) -> nb::object {
        if (instance(value, n("list"))) return self.attr("__newlike__")(value);
        if (truth(self.attr("_basetypes"))) {
            nb::object bases = self.attr("_basetypes");
            nb::object types = add(bases, nb::make_tuple(n("NestableEventedList")));
            if (!instance(value, types)) error(PyExc_TypeError, message({nb::str("Cannot add object with type "), object_repr(type_of(value)),
                nb::str(" to TypedList expecting type "), object_repr(types)}));
        }
        return value;
    }, nb::arg("self").none(), nb::arg("value").none());
    m.def("nested_index_iterator", [](nb::handle self, nb::handle start, nb::handle stop, nb::handle root) -> nb::object {
        nb::object result = nb::cast(NestedIndexIterator{});
        nb::dict values; values["self"] = self; values["start"] = start; values["stop"] = stop; values["root"] = root;
        nb::inst_ptr<NestedIndexIterator>(result)->frame_owner = values;
        return result;
    }, nb::arg("self").none(), nb::arg("start").none(), nb::arg("stop").none(), nb::arg("root").none());
    m.def("nested_has_index", [](nb::handle self, nb::handle index) -> bool {
        if (instance(index, n("int"))) {
            nb::object negative_length = checked(PyNumber_Negative(length(self).ptr()));
            return compare(negative_length, index, Py_LE) && compare(index, length(self), Py_LT);
        }
        if (instance(index, n("tuple"))) {
            try { item(self, index); }
            catch (const nb::python_error& failure) { if (failure.matches(PyExc_IndexError)) return false; throw; }
            return true;
        }
        error(PyExc_TypeError, message({nb::str("Not supported index type "), formatted(type_of(index))}));
    }, nb::arg("self").none(), nb::arg("index").none());
}
} // namespace nx
