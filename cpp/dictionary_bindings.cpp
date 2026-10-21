#include "collection_bindings.hpp"
#include "collection_protocol.hpp"

namespace nx {
namespace nb = nanobind;
namespace {
using namespace collections;

nb::object d(const char* name) {
    return global("napari.utils.events.containers._dict", name);
}
nb::object e(const char* name) {
    return global("napari.utils.events.containers._evented_dict", name);
}

void typed_init(nb::handle self, nb::object data, nb::handle basetype) {
    if (data.is_none()) data = nb::dict();
    set_attr(self, "_dict", nb::dict());
    nb::object types = instance(basetype, d("Sequence"))
        ? nb::borrow<nb::object>(basetype) : nb::make_tuple(basetype);
    set_attr(self, "_basetypes", types);
    self.attr("update")(data);
}

nb::object typed_check(nb::handle self, nb::object value) {
    if (truth(self.attr("_basetypes"))) {
        bool accepted = false;
        nb::object types = self.attr("_basetypes");
        for (nb::handle type : nb::iter(types)) {
            if (instance(value, type)) { accepted = true; break; }
        }
        if (!accepted) {
            nb::object value_type = formatted(d("type")(value));
            nb::object expected = formatted(self.attr("_basetypes"));
            error(PyExc_TypeError, message({nb::str("Cannot add object with type "),
                value_type, nb::str(" to TypedDict expecting type "), expected}));
        }
    }
    return value;
}

void evented_init(nb::handle self, nb::handle data, nb::handle basetype, nb::handle super) {
    nb::dict events;
    for (const char* name : {"changing", "changed", "adding", "added", "removing", "removed", "updated"})
        events[name] = nb::none();
    nb::object existing = optional_attribute(self, "events");
    bool existing_group = false;
    if (existing.is_valid()) {
        nb::object candidate = self.attr("events");
        nb::object group_type = e("EmitterGroup");
        existing_group = instance(candidate, group_type);
    }
    if (existing_group) {
        nb::object add = self.attr("events").attr("add");
        checked(PyObject_Call(add.ptr(), nb::tuple().ptr(), events.ptr()));
    } else {
        nb::object factory = e("EmitterGroup");
        nb::dict kwargs;
        kwargs["source"] = self; kwargs["auto_connect"] = false; kwargs["_connect_children"] = true;
        if (PyDict_Update(kwargs.ptr(), events.ptr()) < 0) throw nb::python_error();
        set_attr(self, "events", checked(PyObject_Call(factory.ptr(), nb::tuple().ptr(), kwargs.ptr())));
    }
    super.attr("__init__")(data, basetype);
}

void evented_set(nb::handle self, nb::handle key, nb::handle value, nb::handle super) {
    nb::object old = self.attr("_dict").attr("get")(key);
    if (value.is(old) || compare(value, old)) return;
    if (old.is_none()) {
        self.attr("events").attr("adding")(nb::arg("key") = key);
        super.attr("__setitem__")(key, value);
        self.attr("events").attr("added")(nb::arg("key") = key, nb::arg("value") = value);
        if (truth(self.attr("events").attr("callbacks"))) self.attr("_connect_child_emitters")(value);
    } else {
        self.attr("events").attr("changing")(nb::arg("key") = key);
        super.attr("__setitem__")(key, value);
        self.attr("events").attr("changed")(nb::arg("key") = key, nb::arg("old_value") = old, nb::arg("value") = value);
    }
}

void disconnect_child(nb::handle self, nb::handle child) {
    if (instance(child, e("PsygnalModel"))) {
        nb::object disconnect = child.attr("events").attr("disconnect");
        disconnect(self.attr("_reemit_child_event_psygnal"));
    }
    if (instance(child, e("SupportsEvents"))) {
        nb::object disconnect = child.attr("events").attr("disconnect");
        disconnect(self.attr("_reemit_child_event"));
    }
}

void connect_child(nb::handle self, nb::handle child) {
    if (instance(child, e("PsygnalModel"))) {
        nb::object connect = child.attr("events").attr("all").attr("connect");
        connect(self.attr("_reemit_child_event_psygnal"), nb::arg("unique") = true);
    } else if (instance(child, e("SupportsEvents"))) {
        nb::object source = child.attr("events").attr("source");
        if (source.is_none()) set_attr(child.attr("events"), "source", child);
        nb::object connect = child.attr("events").attr("connect");
        connect(self.attr("_reemit_child_event"));
    }
}

nb::object find_key(nb::handle self, nb::handle value) {
    nb::object entries = self.attr("_dict").attr("items")();
    for (nb::handle entry : nb::iter(entries)) {
        nb::tuple parts = pair(entry);
        nb::object key = parts[0], current = parts[1];
        if (current.is(value) || compare(current, value)) return key;
    }
    return nb::none();
}

nb::object namespace_get(nb::handle self, nb::handle name) {
    if (compare(name, nb::str("events"))) error(PyExc_AttributeError, name);
    try { return item(self, name); }
    catch (const nb::python_error& failure) {
        if (!failure.matches(PyExc_KeyError)) throw;
        HandledException active(failure.value());
        nb::object converted = e("AttributeError")(name);
        PyException_SetCause(converted.ptr(), failure.value().inc_ref().ptr());
        PyException_SetContext(converted.ptr(), failure.value().inc_ref().ptr());
        raise_exception(converted, false);
    }
}

nb::object namespace_dir(nb::handle self, nb::handle super) {
    nb::object sort = e("sorted"), set = e("set"), directory = super.attr("__dir__");
    nb::object existing = set(directory());
    nb::set visible;
    nb::object keys = self.attr("keys")();
    for (nb::handle key : nb::iter(keys)) {
        nb::object startswith = key.attr("startswith");
        if (!truth(startswith("_"))) visible.add(key);
    }
    return sort(checked(PyNumber_Or(existing.ptr(), visible.ptr())));
}
} // namespace

void bind_dictionaries(nb::module_& m) {
    m.def("typed_mapping_init", &typed_init, nb::arg("self").none(), nb::arg("data").none(), nb::arg("basetype").none());
    m.def("typed_mapping_setitem", [](nb::handle self, nb::handle key, nb::handle value) {
        nb::object check = self.attr("_type_check"), replacement = check(value);
        set_item(self.attr("_dict"), key, replacement);
    }, nb::arg("self").none(), nb::arg("key").none(), nb::arg("value").none());
    m.def("typed_mapping_delitem", [](nb::handle self, nb::handle key) {
        nb::object dictionary = self.attr("_dict");
        if (PyObject_DelItem(dictionary.ptr(), key.ptr()) < 0) throw nb::python_error();
    }, nb::arg("self").none(), nb::arg("key").none());
    m.def("typed_mapping_getitem", [](nb::handle self, nb::handle key) -> nb::object { return item(self.attr("_dict"), key); }, nb::arg("self").none(), nb::arg("key").none());
    m.def("typed_mapping_len", [](nb::handle self) -> nb::object { return d("len")(self.attr("_dict")); }, nb::arg("self").none());
    m.def("typed_mapping_iter", [](nb::handle self) -> nb::object { return d("iter")(self.attr("_dict")); }, nb::arg("self").none());
    m.def("typed_mapping_repr", [](nb::handle self) -> nb::object { return d("str")(self.attr("_dict")); }, nb::arg("self").none());
    m.def("typed_mapping_check", &typed_check, nb::arg("self").none(), nb::arg("value").none());
    m.def("typed_mapping_newlike", [](nb::handle self, nb::handle iterable) -> nb::object {
        nb::object factory = self.attr("__class__"), result = factory();
        nb::object types = self.attr("_basetypes");
        set_attr(result, "_basetypes", types);
        result.attr("update")(iterable);
        return result;
    }, nb::arg("self").none(), nb::arg("iterable").none());
    m.def("typed_mapping_copy", [](nb::handle self) -> nb::object { return self.attr("__newlike__")(self); }, nb::arg("self").none());

    m.def("evented_mapping_init", &evented_init, nb::arg("self").none(), nb::arg("data").none(), nb::arg("basetype").none(), nb::arg("super"));
    m.def("evented_mapping_first_callback", [](nb::handle self) {
        nb::object values = self.attr("_dict").attr("values")();
        for (nb::handle child : nb::iter(values)) self.attr("_connect_child_emitters")(child);
    }, nb::arg("self").none());
    m.def("evented_mapping_last_callback", [](nb::handle self) {
        if (truth(self.attr("events").attr("callbacks"))) return;
        nb::object values = self.attr("_dict").attr("values")();
        for (nb::handle child : nb::iter(values)) self.attr("_disconnect_child_emitters")(child);
    }, nb::arg("self").none());
    m.def("evented_mapping_setitem", &evented_set, nb::arg("self").none(), nb::arg("key").none(), nb::arg("value").none(), nb::arg("super"));
    m.def("evented_mapping_delitem", [](nb::handle self, nb::handle key) {
        self.attr("events").attr("removing")(nb::arg("key") = key);
        nb::object disconnect = self.attr("_disconnect_child_emitters");
        disconnect(item(self, key));
        nb::object value = self.attr("_dict").attr("pop")(key);
        self.attr("events").attr("removed")(nb::arg("key") = key, nb::arg("value") = value);
    }, nb::arg("self").none(), nb::arg("key").none());
    m.def("evented_mapping_reemit", [](nb::handle self, nb::handle event) {
        if (!optional_attribute(event, "key").is_valid()) {
            nb::object key = self.attr("key"), source = event.attr("source");
            set_attr(event, "key", key(source));
        }
        self.attr("events")(event);
    }, nb::arg("self").none(), nb::arg("event").none());
    m.def("evented_mapping_reemit_psygnal", [](nb::handle self, nb::handle event) {
        nb::object source = event.attr("signal").attr("instance"), key;
        nb::object path = event.attr("path");
        if (truth(path)) {
            nb::object method = self.attr("key");
            nb::object prefix = formatted(method(source));
            nb::object suffix = formatted(event.attr("path"));
            key = message({prefix, nb::str("."), suffix});
        } else key = self.attr("key")(source);
        nb::object factory = e("Event"), args = event.attr("args");
        nb::object result = factory(nb::arg("value") = args, nb::arg("key") = key, nb::arg("type_name") = "");
        result.attr("_push_source")(source);
        self.attr("events")(result);
    }, nb::arg("self").none(), nb::arg("event").none());
    m.def("evented_mapping_disconnect", &disconnect_child, nb::arg("self").none(), nb::arg("child").none());
    m.def("evented_mapping_connect", &connect_child, nb::arg("self").none(), nb::arg("child").none());
    m.def("evented_mapping_key", &find_key, nb::arg("self").none(), nb::arg("value").none());

    m.def("evented_namespace_getattr", &namespace_get, nb::arg("self").none(), nb::arg("name").none());
    m.def("evented_namespace_setattr", [](nb::handle self, nb::handle name, nb::handle value, nb::handle super) {
        if (contains(nb::make_tuple("events", "_dict", "_basetypes"), name) || contains(super.attr("__dir__")(), name))
            super.attr("__setattr__")(name, value);
        else set_item(self, name, value);
    }, nb::arg("self").none(), nb::arg("name").none(), nb::arg("value").none(), nb::arg("super"));
    m.def("evented_namespace_dir", &namespace_dir, nb::arg("self").none(), nb::arg("super"));
}
} // namespace nx
