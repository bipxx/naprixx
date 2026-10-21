#include "collection_bindings.hpp"
#include "collection_protocol.hpp"

namespace nx {
namespace nb = nanobind;
namespace {
using namespace collections;
nb::object s(const char* name) { return global("napari.utils.events.containers._selection", name); }
nb::object u(const char* name) { return global("napari.utils.events.containers._set", name); }
nb::object l(const char* name) { return global("napari.utils.events.containers._selectable_list", name); }
nb::set singleton(nb::handle value) { nb::set result; result.add(value); return result; }
nb::object sequence_like(nb::handle value, const char* module) {
    nb::list types;
    for (const char* name : {"list", "tuple", "set", "frozenset", "GeneratorType", "deque"}) types.append(global(module, name));
    return global(module, "isinstance")(value, checked(PyList_AsTuple(types.ptr())));
}
nb::object set_repr(nb::handle self) {
    nb::object name = type_of(self).attr("__name__");
    nb::object values = object_repr(self.attr("_set"));
    return message({formatted(name), nb::str("("), values, nb::str(")")});
}
void emit_change(nb::handle self, nb::object added, nb::object removed) {
    if (added.is_none()) added = u("set")();
    if (removed.is_none()) removed = u("set")();
    self.attr("events").attr("changed")(nb::arg("added") = added, nb::arg("removed") = removed);
}
void set_init(nb::handle self, nb::handle data) {
    nb::object existing = optional_attribute(self, "events");
    if (existing.is_valid() && instance(self.attr("events"), u("EmitterGroup"))) self.attr("events").attr("add")(nb::arg("changed") = nb::none());
    else set_attr(self, "events", u("EmitterGroup")(nb::arg("source") = self, nb::arg("changed") = nb::none()));
    set_attr(self, "_set", u("set")());
    self.attr("update")(data);
}
void set_update(nb::handle self, nb::handle others) {
    nb::object difference = u("set")(others).attr("difference");
    nb::object to_add = difference(self.attr("_set"));
    if (truth(to_add)) {
        nb::set checked_values;
        for (nb::handle value : nb::iter(to_add)) checked_values.add(self.attr("_pre_add_hook")(value));
        to_add = checked_values;
        self.attr("_set").attr("update")(to_add);
        nb::object emit = self.attr("_emit_change");
        nb::object added = u("set")(to_add), removed = u("set")();
        emit(nb::arg("added") = added, nb::arg("removed") = removed);
    }
}
void set_difference_update(nb::handle self, nb::handle others) {
    nb::object removed = self.attr("_set").attr("intersection")(others);
    if (truth(removed)) {
        self.attr("_set").attr("difference_update")(removed);
        nb::object emit = self.attr("_emit_change");
        nb::object added = u("set")(), removed_copy = u("set")(removed);
        emit(nb::arg("added") = added, nb::arg("removed") = removed_copy);
    }
}
nb::object set_schema(nb::handle cls, nb::handle source, nb::handle handler) {
    nb::object instance_schema = u("core_schema").attr("is_instance_schema")(cls);
    nb::object args = u("get_args")(source), mutable_type;
    nb::object generate = handler.attr("generate_schema");
    if (truth(args)) mutable_type = item(u("MutableSet"), item(args, nb::int_(0)));
    else mutable_type = u("MutableSet");
    nb::object schema = generate(mutable_type);
    nb::object validate = u("core_schema").attr("no_info_after_validator_function");
    nb::object validator = validate(u("EventedSet"), schema);
    nb::list choices; choices.append(instance_schema); choices.append(validator);
    return u("core_schema").attr("union_schema")(choices);
}
nb::object selection_schema(nb::handle cls, nb::handle source, nb::handle handler, nb::handle serialize_callback) {
    nb::object instance_schema = s("core_schema").attr("is_instance_schema")(cls);
    nb::object args = s("get_args")(source), mutable_schema, dictionary;
    if (truth(args)) {
        nb::object generate = handler.attr("generate_schema");
        nb::object item_schema = generate(item(args, nb::int_(0)));
        generate = handler.attr("generate_schema");
        nb::object mutable_type = item(s("MutableSet"), item(args, nb::int_(0)));
        mutable_schema = generate(mutable_type);
        nb::object union_schema = s("core_schema").attr("union_schema");
        nb::list choices; choices.append(item_schema); choices.append(s("core_schema").attr("none_schema")());
        nb::object current_schema = union_schema(choices);
        nb::object typed_dict = s("core_schema").attr("typed_dict_schema");
        nb::object field = s("core_schema").attr("typed_dict_field");
        nb::object selection_field = field(s("core_schema").attr("list_schema")(item_schema));
        field = s("core_schema").attr("typed_dict_field");
        nb::object current_field = field(current_schema, nb::arg("required") = false);
        nb::dict fields; fields["selection"] = selection_field; fields["_current"] = current_field;
        dictionary = typed_dict(fields);
    } else {
        nb::object generate = handler.attr("generate_schema");
        mutable_schema = generate(s("MutableSet"));
        nb::object dict_schema = s("core_schema").attr("dict_schema");
        nb::object keys = s("core_schema").attr("str_schema")(), values = s("core_schema").attr("any_schema")();
        dictionary = dict_schema(nb::arg("keys_schema") = keys, nb::arg("values_schema") = values);
    }
    nb::list choices; choices.append(mutable_schema); choices.append(dictionary);
    nb::object input = s("core_schema").attr("union_schema")(choices);
    nb::object validate = s("core_schema").attr("no_info_after_validator_function");
    nb::object validator = validate(cls.attr("_validate_selection"), input);
    nb::object serializer = s("core_schema").attr("plain_serializer_function_ser_schema")(serialize_callback, nb::arg("when_used") = "json");
    choices = nb::list(); choices.append(instance_schema); choices.append(validator);
    return s("core_schema").attr("union_schema")(choices, nb::arg("serialization") = serializer);
}
nb::object selection_serialize(nb::handle value) {
    nb::object selected = s("EventedSet")(value).attr("_json_encode")();
    nb::dict result; result["selection"] = selected; result["_current"] = value.attr("_current");
    return result;
}
void call_super_init(nb::handle super, nb::tuple args, nb::handle kwargs) {
    nb::object initialize = super.attr("__init__");
    nb::dict keywords = events::keyword_mapping(initialize, kwargs);
    checked(PyObject_Call(initialize.ptr(), args.ptr(), keywords.ptr()));
}
void select_next(nb::handle self, nb::handle step, nb::handle shift) {
    if (truth(self.attr("selection")) && truth(self.attr("selection").attr("_current"))) {
        nb::object index = self.attr("index");
        nb::object current = self.attr("selection").attr("_current");
        nb::object next_index = add(index(current), step);
        if (compare(length(self), next_index, Py_GT) && compare(next_index, nb::int_(0), Py_GE)) {
            nb::object next = item(self, next_index);
            if (truth(shift)) {
                if (contains(self.attr("selection"), next)) {
                    nb::object remove = self.attr("selection").attr("remove");
                    remove(self.attr("selection").attr("_current"));
                    set_attr(self.attr("selection"), "_current", next);
                } else {
                    self.attr("selection").attr("add")(next);
                    set_attr(self.attr("selection"), "_current", next);
                }
            } else set_attr(self.attr("selection"), "active", next);
        }
    } else if (compare(length(self), nb::int_(0), Py_GT)) {
        nb::object next = item(self, nb::int_(compare(step, nb::int_(0), Py_GT) ? -1 : 0));
        set_attr(self.attr("selection"), "active", next);
    }
}
} // namespace

void bind_selection(nb::module_& m) {
    m.def("evented_set_sequence_like", [](nb::handle value) -> nb::object { return sequence_like(value, "napari.utils.events.containers._set"); }, nb::arg("value").none());
    m.def("selection_sequence_like", [](nb::handle value) -> nb::object { return sequence_like(value, "napari.utils.events.containers._selection"); }, nb::arg("value").none());
    m.def("evented_set_init", &set_init, nb::arg("self").none(), nb::arg("data").none());
    m.def("evented_set_contains", [](nb::handle self, nb::handle value) -> bool { return contains(self.attr("_set"), value); }, nb::arg("self").none(), nb::arg("value").none());
    m.def("evented_set_iter", [](nb::handle self) -> nb::object { return u("iter")(self.attr("_set")); }, nb::arg("self").none());
    m.def("evented_set_len", [](nb::handle self) -> nb::object { return u("len")(self.attr("_set")); }, nb::arg("self").none());
    m.def("evented_set_pre_add", [](nb::object value) -> nb::object { return value; }, nb::arg("value").none());
    m.def("evented_set_emit_change", &emit_change, nb::arg("self").none(), nb::arg("added").none(), nb::arg("removed").none());
    m.def("evented_set_add", [](nb::handle self, nb::object value) {
        if (!contains(self, value)) {
            value = self.attr("_pre_add_hook")(value);
            self.attr("_set").attr("add")(value);
            nb::object emit = self.attr("_emit_change");
            nb::object added = singleton(value), removed = u("set")();
            emit(nb::arg("added") = added, nb::arg("removed") = removed);
        }
    }, nb::arg("self").none(), nb::arg("value").none());
    m.def("evented_set_discard", [](nb::handle self, nb::handle value) {
        if (contains(self, value)) {
            self.attr("_set").attr("discard")(value);
            nb::object emit = self.attr("_emit_change");
            nb::object added = u("set")(), removed = singleton(value);
            emit(nb::arg("added") = added, nb::arg("removed") = removed);
        }
    }, nb::arg("self").none(), nb::arg("value").none());
    m.def("evented_set_clear", [](nb::handle self) {
        if (truth(self.attr("_set"))) {
            nb::object values = u("set")(self);
            self.attr("_set").attr("clear")();
            nb::object emit = self.attr("_emit_change");
            emit(nb::arg("added") = u("set")(), nb::arg("removed") = values);
        }
    }, nb::arg("self").none());
    m.def("evented_set_repr", &set_repr, nb::arg("self").none());
    m.def("evented_set_update", &set_update, nb::arg("self").none(), nb::arg("others").none());
    m.def("evented_set_copy", [](nb::handle self) -> nb::object { nb::object factory = type_of(self); return factory(self.attr("_set")); }, nb::arg("self").none());
    for (const char* operation : {"difference", "intersection", "symmetric_difference", "union"}) {
        const std::string entry = std::string("evented_set_") + operation;
        m.def(entry.c_str(), [operation](nb::handle self, nb::handle others) -> nb::object {
            nb::object factory = type_of(self);
            return factory(self.attr("_set").attr(operation)(others));
        }, nb::arg("self").none(), nb::arg("others").none());
    }
    m.def("evented_set_difference_update", &set_difference_update, nb::arg("self").none(), nb::arg("others").none());
    m.def("evented_set_intersection_update", [](nb::handle self, nb::handle others) {
        nb::object update = self.attr("difference_update");
        update(self.attr("_set").attr("symmetric_difference")(others));
    }, nb::arg("self").none(), nb::arg("others").none());
    for (const char* operation : {"issubset", "issuperset"}) {
        const std::string entry = std::string("evented_set_") + operation;
        m.def(entry.c_str(), [operation](nb::handle self, nb::handle others) -> nb::object { return self.attr("_set").attr(operation)(others); }, nb::arg("self").none(), nb::arg("others").none());
    }
    m.def("evented_set_symmetric_difference_update", [](nb::handle self, nb::handle others) {
        nb::object difference = u("set")(others).attr("difference");
        nb::object added = difference(self.attr("_set"));
        nb::object removed = self.attr("_set").attr("intersection")(others);
        self.attr("_set").attr("difference_update")(removed);
        self.attr("_set").attr("update")(added);
        self.attr("_emit_change")(nb::arg("added") = added, nb::arg("removed") = removed);
    }, nb::arg("self").none(), nb::arg("others").none());
    m.def("evented_set_schema", &set_schema, nb::arg("cls").none(), nb::arg("source").none(), nb::arg("handler").none());
    m.def("evented_set_json", [](nb::handle self) -> nb::object { return u("list")(self); }, nb::arg("self").none());

    m.def("selection_init", [](nb::handle self, nb::handle data, nb::handle super) {
        set_attr(self, "_active", nb::none()); set_attr(self, "_current_", nb::none());
        set_attr(self, "events", s("EmitterGroup")(nb::arg("source") = self, nb::arg("_current") = nb::none(), nb::arg("active") = nb::none()));
        super.attr("__init__")(nb::arg("data") = data);
        self.attr("_update_active")();
    }, nb::arg("self").none(), nb::arg("data").none(), nb::arg("super"));
    m.def("selection_emit_change", [](nb::handle self, nb::object added, nb::object removed, nb::handle super) -> nb::object {
        if (added.is_none()) added = s("set")();
        if (removed.is_none()) removed = s("set")();
        self.attr("_update_active")();
        return super.attr("_emit_change")(nb::arg("added") = added, nb::arg("removed") = removed);
    }, nb::arg("self").none(), nb::arg("added").none(), nb::arg("removed").none(), nb::arg("super"));
    m.def("selection_repr", &set_repr, nb::arg("self").none());
    m.def("selection_hash", [](nb::handle self) -> nb::object { return s("id")(self); }, nb::arg("self").none());
    m.def("selection_current_get", [](nb::handle self) -> nb::object { return self.attr("_current_"); }, nb::arg("self").none());
    m.def("selection_current_set", [](nb::handle self, nb::handle value) {
        if (compare(value, self.attr("_current_"))) return;
        set_attr(self, "_current_", value);
        self.attr("events").attr("_current")(nb::arg("value") = value);
    }, nb::arg("self").none(), nb::arg("value").none());
    m.def("selection_active_get", [](nb::handle self) -> nb::object { return self.attr("_active"); }, nb::arg("self").none());
    m.def("selection_active_set", [](nb::handle self, nb::handle value) {
        if (compare(value, self.attr("_active"))) return;
        set_attr(self, "_active", value);
        if (value.is_none()) self.attr("clear")(); else self.attr("select_only")(value);
        set_attr(self, "_current", value);
        self.attr("events").attr("active")(nb::arg("value") = value);
    }, nb::arg("self").none(), nb::arg("value").none());
    m.def("selection_update_active", [](nb::handle self) {
        if (compare(length(self), nb::int_(1))) {
            nb::object next = s("next"), iterator = s("iter")(self);
            set_attr(self, "active", next(iterator));
        } else if (!self.attr("_active").is_none()) {
            set_attr(self, "_active", nb::none());
            self.attr("events").attr("active")(nb::arg("value") = nb::none());
        }
    }, nb::arg("self").none());
    m.def("selection_clear", [](nb::handle self, nb::handle keep, nb::handle super) {
        if (!truth(keep)) set_attr(self, "_current", nb::none());
        super.attr("clear")();
    }, nb::arg("self").none(), nb::arg("keep").none(), nb::arg("super"));
    m.def("selection_add_remove", [](nb::handle self, nb::handle add_values, nb::handle remove_values) {
        self.attr("_set").attr("update")(add_values);
        self.attr("_set").attr("difference_update")(remove_values);
        self.attr("_update_active")();
        nb::object emit = self.attr("_emit_change");
        nb::object added = s("set")(add_values), removed = s("set")(remove_values);
        emit(nb::arg("added") = added, nb::arg("removed") = removed);
    }, nb::arg("self").none(), nb::arg("add").none(), nb::arg("remove").none());
    m.def("selection_toggle", [](nb::handle self, nb::handle value) { nb::object update = self.attr("symmetric_difference_update"); update(singleton(value)); }, nb::arg("self").none(), nb::arg("value").none());
    m.def("selection_only", [](nb::handle self, nb::handle value) { nb::object update = self.attr("intersection_update"); update(singleton(value)); self.attr("add")(value); }, nb::arg("self").none(), nb::arg("value").none());
    m.def("selection_schema", &selection_schema, nb::arg("cls").none(), nb::arg("source").none(), nb::arg("handler").none(), nb::arg("serializer").none());
    m.def("selection_serialize", &selection_serialize, nb::arg("value").none());
    m.def("selection_validate", [](nb::handle cls, nb::handle value) -> nb::object {
        nb::object data, current;
        if (instance(value, s("dict"))) { data = value.attr("get")("selection", nb::list()); current = value.attr("get")("_current", nb::none()); }
        else if (instance(value, s("Selection"))) { data = value.attr("_set"); current = value.attr("_current"); }
        else { data = nb::borrow<nb::object>(value); current = nb::none(); }
        nb::object result = cls(nb::arg("data") = data);
        set_attr(result, "_current_", current);
        return result;
    }, nb::arg("cls").none(), nb::arg("value").none());
    m.def("selection_json", [](nb::handle self, nb::handle super) -> nb::object {
        nb::object values = super.attr("_json_encode")();
        nb::dict result; result["selection"] = values; result["_current"] = self.attr("_current"); return result;
    }, nb::arg("self").none(), nb::arg("super"));
    m.def("selectable_init", [](nb::handle self, nb::tuple args, nb::handle kwargs, nb::handle super) {
        set_attr(self, "_selection", s("Selection")()); call_super_init(super, args, kwargs);
    }, nb::arg("self").none(), nb::arg("args"), nb::arg("kwargs").none(), nb::arg("super"));
    m.def("selectable_get", [](nb::handle self) -> nb::object { return self.attr("_selection"); }, nb::arg("self").none());
    m.def("selectable_set", [](nb::handle self, nb::handle values) {
        self.attr("_selection").attr("intersection_update")(values);
        self.attr("_selection").attr("update")(values);
    }, nb::arg("self").none(), nb::arg("values").none());
    m.def("selectable_list_init", [](nb::handle self, nb::tuple args, nb::handle kwargs, nb::handle super) {
        set_attr(self, "_activate_on_insert", nb::bool_(true));
        call_super_init(super, args, kwargs);
        nb::object hook = self.attr("_preselect_hook"); set_attr(self.attr("selection"), "_pre_add_hook", hook);
    }, nb::arg("self").none(), nb::arg("args"), nb::arg("kwargs").none(), nb::arg("super"));
    m.def("selectable_list_preselect", [](nb::handle self, nb::object value) -> nb::object {
        if (!contains(self, value)) error(PyExc_ValueError, message({nb::str("Cannot select item that is not in list: "), object_repr(value)}));
        return value;
    }, nb::arg("self").none(), nb::arg("value").none());
    m.def("selectable_list_process_delete", [](nb::handle self, nb::handle value) { self.attr("selection").attr("discard")(value); }, nb::arg("self").none(), nb::arg("value").none());
    m.def("selectable_list_insert", [](nb::handle self, nb::handle index, nb::handle value, nb::handle super) {
        super.attr("insert")(index, value);
        if (truth(self.attr("_activate_on_insert"))) set_attr(self.attr("selection"), "active", value);
    }, nb::arg("self").none(), nb::arg("index").none(), nb::arg("value").none(), nb::arg("super"));
    m.def("selectable_list_select_all", [](nb::handle self) { self.attr("selection").attr("update")(self); }, nb::arg("self").none());
    m.def("selectable_list_remove_selected", [](nb::handle self) {
        nb::object index = nb::int_(0), list = l("list");
        nb::object values = list(self.attr("selection"));
        for (nb::handle value : nb::iter(values)) { index = self.attr("index")(value); self.attr("remove")(value); }
        nb::object next; bool add;
        if (instance(index, l("int"))) { nb::object max = l("max"); next = max(nb::int_(0), subtract(index, nb::int_(1))); add = compare(length(self), next, Py_GT); }
        else {
            nb::object iterator = checked(PyObject_GetIter(index.ptr()));
            nb::object roots = checked(PySequence_List(iterator.ptr()));
            if (nb::len(roots) == 0) throw nb::value_error("not enough values to unpack (expected at least 1, got 0)");
            nb::object last = roots.attr("pop")();
            if (compare(last, nb::int_(1), Py_GE)) {
                nb::object root = l("tuple")(roots);
                nb::list path = nb::cast<nb::list>(checked(PySequence_List(root.ptr())));
                path.append(subtract(last, nb::int_(1))); next = checked(PyList_AsTuple(path.ptr()));
            } else next = l("tuple")(roots);
            add = compare(length(self), item(next, nb::int_(0)), Py_GT);
        }
        if (add) { nb::object add_method = self.attr("selection").attr("add"); add_method(item(self, next)); }
    }, nb::arg("self").none());
    m.def("selectable_list_move_selected", [](nb::handle self, nb::handle index, nb::handle insert) {
        l("warnings").attr("warn")("move_selected is deprecated since 0.4.16. Please use layers.move_multiple with layers.selection instead.", l("FutureWarning"), nb::arg("stacklevel") = 2);
        nb::object value = item(self, index), moving;
        if (!contains(self.attr("selection"), value)) { nb::object select = self.attr("selection").attr("select_only"); select(item(self, index)); nb::list indices; indices.append(index); moving = indices; }
        else {
            nb::list indices;
            nb::object entries = l("enumerate")(self);
            for (nb::handle entry : nb::iter(entries)) { nb::tuple parts = pair(entry); if (contains(self.attr("selection"), parts[1])) indices.append(parts[0]); }
            moving = indices;
        }
        nb::object offset = checked(PyObject_RichCompare(insert.ptr(), index.ptr(), Py_GE));
        nb::object move_multiple = self.attr("move_multiple"); move_multiple(moving, add(insert, offset));
    }, nb::arg("self").none(), nb::arg("index").none(), nb::arg("insert").none());
    m.def("selectable_list_select_next", &select_next, nb::arg("self").none(), nb::arg("step").none(), nb::arg("shift").none());
    m.def("selectable_list_select_previous", [](nb::handle self, nb::handle shift) { self.attr("select_next")(nb::int_(-1), nb::arg("shift") = shift); }, nb::arg("self").none(), nb::arg("shift").none());
}
} // namespace nx
