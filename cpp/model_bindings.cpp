#include "arrays.hpp"
#include "change_journal.hpp"
#include "model_helpers.hpp"
#include "model_transaction.hpp"
#include "collection_protocol.hpp"

#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>
#include <functional>
#include <string>

namespace nx {
bool theme_assignment_dispatch(nb::handle model, nb::handle target,
                               nb::handle name, nb::handle value);
namespace {
using app::ChangeJournal;

// Expose the implicit heap-type dependency to Python's garbage collector.
// Neither native value type stores Python values or callbacks.
int value_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    return 0;
}

PyType_Slot value_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(value_traverse)},
    {0, nullptr}
};

// This mapping shares native name storage and has no hidden Python references.
struct PrimaryChanges {
    explicit PrimaryChanges(const ChangeJournal& journal) : state(journal) {}
    ChangeJournal state;
    nb::dict snapshot() const {
        nb::dict result;
        for (const auto& name : state.primary_names()) result[nb::str(name.c_str())] = nb::none();
        return result;
    }
};

// A Python-visible owner for the canonical primary view. The view and the
// independent core depth share only Python-free name buffers; queue payloads
// continue to belong exclusively to the model's native private-map owner.
struct ModelJournal {
    ChangeJournal state;
    nb::object primary_view;
    // Legacy diagnostics use a weak native-map owner, never another primary
    // payload edge. Application control reads the model's exact attributes.
    nb::object private_owner;
    ModelJournal() : primary_view(nb::cast(PrimaryChanges(state))) {}
    ModelJournal(ChangeJournal value, nb::object view)
        : state(std::move(value)), primary_view(std::move(view)) {}
    ModelJournal(ModelJournal&&) noexcept = default;
    ModelJournal(const ModelJournal& source)
        : state(source.state), primary_view(source.primary_view) {}
    ~ModelJournal() { clear_reference(); }
    void clear_reference() noexcept {
        nb::object previous = std::move(primary_view);
        nb::object previous_owner = std::move(private_owner);
    }
    nb::object mapping() const {
        if (!private_owner.is_valid()) return {};
        nb::object owner = private_owner();
        if (owner.is_none()) return {};
        nb::object result = owner.attr("private_mapping");
        return result.is_none() ? nb::object() : result;
    }
    nb::object primary() const {
        nb::object map = mapping();
        return map.is_valid() ? nb::object(map[nb::str("_primary_changes")]) : primary_view;
    }
};

int journal_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (!nb::inst_ready(nb::handle(self))) return 0;
    Py_VISIT(nb::inst_ptr<ModelJournal>(nb::handle(self))->primary_view.ptr());
    Py_VISIT(nb::inst_ptr<ModelJournal>(nb::handle(self))->private_owner.ptr());
    return 0;
}

int journal_clear(PyObject* self) {
    if (nb::inst_ready(nb::handle(self)))
        nb::inst_ptr<ModelJournal>(nb::handle(self))->clear_reference();
    return 0;
}

PyType_Slot journal_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(journal_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(journal_clear)},
    {0, nullptr}
};

nb::object deep_copy_journal(nb::object self, nb::handle memo) {
    const auto& source = nb::cast<ModelJournal&>(self);
    nb::object actual_memo = memo.is_none() ? nb::object(nb::dict())
                                          : nb::borrow<nb::object>(memo);
    ChangeJournal state = source.state.deep_copy();
    nb::object view = nb::cast(PrimaryChanges(state));
    // A reentrant copier may inspect the memo before view copying finishes.
    // Publish a fully constructed journal with a valid canonical view first.
    nb::object result = nb::cast(ModelJournal(std::move(state), std::move(view)));
    actual_memo[nb::int_(reinterpret_cast<std::uintptr_t>(self.ptr()))] = result;
    // This legacy helper view represents only Python-free core names. Never
    // adopt caller-memo Python payloads into it. Pydantic copies the actual
    // application primary mapping independently with the caller's exact memo.
    return result;
}

[[noreturn]] void missing_key(nb::str name) {
    PyErr_SetObject(PyExc_KeyError, name.ptr());
    throw nb::python_error();
}

bool truth(nb::handle object) {
    const auto result = PyObject_IsTrue(object.ptr());
    if (result < 0) throw nb::python_error();
    return result != 0;
}

bool contains(nb::handle container, nb::handle key) {
    const auto result = PySequence_Contains(container.ptr(), key.ptr());
    if (result < 0) throw nb::python_error();
    return result != 0;
}

nb::object getattr_default(nb::handle object, nb::handle name) {
    auto* value = PyObject_GetAttr(object.ptr(), name.ptr());
    if (value) return nb::steal<nb::object>(value);
    if (!PyErr_ExceptionMatches(PyExc_AttributeError)) throw nb::python_error();
    PyErr_Clear();
    return nb::module_::import_("builtins").attr("object")();
}

void delay_change(nb::handle model, bool enter) {
    nb::object old = model.attr("_delay_check_semaphore");
    nb::int_ one(1);
    nb::object value = collections::checked(enter
        ? PyNumber_InPlaceAdd(old.ptr(), one.ptr())
        : PyNumber_InPlaceSubtract(old.ptr(), one.ptr()));
    // BINARY_OP consumes the old operand before STORE_ATTR; the new operand
    // is consumed before the next expression invokes the check hook.
    old.reset();
    collections::set_attr(model, "_delay_check_semaphore", value);
}

void check_emit(nb::handle model) {
    // Preserve protected-method overrides supplied by plugin subclasses.
    model.attr("_check_if_values_changed_and_emit_if_needed")();
}

void next_setattr(nb::handle model, nb::str name, nb::handle value) {
    nb::object base = nb::module_::import_("napari.utils.events.evented_model")
                          .attr("EventedModel");
    // This reaches the Pydantic validation adapter, respecting plugin mixins
    // inserted between EventedModel and BaseModel in a subclass's MRO.
    nb::object target = nb::module_::import_("builtins").attr("super")(base, model)
        .attr("__setattr__");
    if (!theme_assignment_dispatch(model, target, name, value))
        target(name, value);
}

void delayed(nb::handle model, const std::function<void()>& action) {
    nb::object context = nb::module_::import_("napari.utils.events.evented_model")
        .attr("ComparisonDelayer")(model);
    collections::with_context(context, action);
}

void super_setattr(nb::handle model, nb::str name, nb::handle value) {
    nb::dict properties = nb::cast<nb::dict>(model.attr("__properties__"));
    if (properties.contains(name)) {
        nb::object property = properties[name];
        nb::object setter = property.attr("fset");
        if (setter.is_none()) {
            const auto message = "can't set attribute '" + nb::cast<std::string>(name) + "'";
            throw nb::attribute_error(message.c_str());
        }
        setter(model, value);
    } else {
        next_setattr(model, name, value);
    }
}

nb::object pick_equality(nb::handle object) {
    nb::module_ builtins = nb::module_::import_("builtins");
    nb::object type = PyType_Check(object.ptr()) ? nb::borrow<nb::object>(object)
                                               : builtins.attr("type")(object);
    nb::module_ operators = nb::module_::import_("operator");
    for (nb::handle base : type.attr("mro")()) {
        const auto module = nb::cast<std::string>(base.attr("__module__"));
        const auto name = module.substr(0, module.find('.')) + "." +
                          nb::cast<std::string>(base.attr("__name__"));
        if (name == "dask.Array" || name == "dask.Delayed" || name == "zarr.Array")
            return operators.attr("is_");
        if (name == "numpy.ndarray" || name == "xarray.DataArray")
            return nb::module_::import_("napari.utils.misc").attr("_quiet_array_equal");
        if (name == "pandas.DataFrame")
            return nb::module_::import_("napari.utils.misc").attr("_pandas_dataframe_equal");
    }
    return operators.attr("eq");
}

nb::tuple different(nb::handle model, nb::str name, nb::handle old) {
    nb::object current = getattr_default(model, name);
    nb::dict comparators = nb::cast<nb::dict>(model.attr("__eq_operators__"));
    // The public hook normally forwards to pick_equality here, but plugins may
    // replace it. Keep that override behavior while owning the selection logic.
    nb::object comparator = comparators.contains(name) ? nb::borrow<nb::object>(comparators[name])
        : nb::module_::import_("napari.utils.events.evented_model")
              .attr("pick_equality_operator")(current);
    return nb::make_tuple(!truth(comparator(current, old)), current);
}

void capture(nb::handle model, nb::str name) {
    // Queue views are live and may be edited or replaced while delayed.
    // Capture membership must come from that queue, not the name journal.
    if (contains(model.attr("_changes_queue"), name)) return;
    nb::object value = getattr_default(model, name);
    model.attr("_changes_queue")[name] = value;
}

void setattr_impl(nb::handle model, nb::str name, nb::handle value) {
    nb::object events = model.attr("events");
    if (!contains(events, name)) {
        model.attr("_super_setattr_")(name, value);
        return;
    }
    nb::dict dependencies = nb::cast<nb::dict>(model.attr("__field_dependents__"));
    nb::list observed;
    if (dependencies.contains(name)) {
        for (nb::handle dependency : dependencies[name]) {
            nb::object emitter = nb::steal<nb::object>(PyObject_GetAttr(events.ptr(), dependency.ptr()));
            if (!emitter.is_valid()) throw nb::python_error();
            if (truth(emitter.attr("callbacks"))) observed.append(dependency);
        }
    }
    nb::object emitter = nb::steal<nb::object>(PyObject_GetAttr(events.ptr(), name.ptr()));
    if (!emitter.is_valid()) throw nb::python_error();
    if (truth(emitter.attr("callbacks")) || truth(model.attr("_events").attr("callbacks")) ||
        nb::len(observed)) {
        capture(model, name);
        for (nb::handle dependency : observed) capture(model, nb::borrow<nb::str>(dependency));
    }
    model.attr("_super_setattr_")(name, value);
}

void set_model(nb::handle model, nb::str name, nb::handle value) {
    nb::object events = nb::steal<nb::object>(PyObject_GetAttrString(model.ptr(), "events"));
    if (!events.is_valid()) {
        if (!PyErr_ExceptionMatches(PyExc_AttributeError)) throw nb::python_error();
        PyErr_Clear();
    }
    if (!events.is_valid() || !contains(events, name)) {
        next_setattr(model, name, value);
        return;
    }
    delayed(model, [&] {
        model.attr("_primary_changes")[name] = nb::none();
        model.attr("_setattr_impl")(name, value);
    });
}

void emit_changes(nb::handle model) {
    if (truth(collections::checked(PyObject_RichCompare(
            nb::object(model.attr("_delay_check_semaphore")).ptr(), nb::int_(0).ptr(), Py_GT))) ||
        !nb::len(nb::object(model.attr("_changes_queue")))) {
        if (truth(collections::checked(PyObject_RichCompare(
                nb::object(model.attr("_delay_check_semaphore")).ptr(), nb::int_(0).ptr(), Py_EQ))))
            model.attr("_primary_changes").attr("clear")();
        return;
    }
    nb::list to_emit;
    // Python's last old_value and comparison result remain live until the
    // function returns, including through callbacks and reentrant draining.
    nb::object old, result;
    for (nb::handle name : model.attr("_primary_changes")) {
        if (!contains(model.attr("_changes_queue"), name)) continue;
        old = model.attr("_changes_queue")[name];
        result = model.attr("_check_if_differ")(name, old);
        if (truth(result[nb::int_(0)]))
            to_emit.append(nb::make_tuple(name, result[nb::int_(1)]));
        model.attr("_changes_queue").attr("pop")(name);
    }
    if (!nb::len(to_emit)) {
        model.attr("_changes_queue").attr("clear")();
        model.attr("_primary_changes").attr("clear")();
        return;
    }
    for (nb::handle item : model.attr("_changes_queue").attr("items")()) {
        nb::object name = item[nb::int_(0)];
        old = item[nb::int_(1)];
        result = model.attr("_check_if_differ")(name, old);
        if (truth(result[nb::int_(0)]))
            to_emit.append(nb::make_tuple(name, result[nb::int_(1)]));
    }
    // Keep this order: releasing queue payloads can reenter before primaries
    // are cleared. Re-read both attributes after arbitrary provider hooks.
    model.attr("_changes_queue").attr("clear")();
    model.attr("_primary_changes").attr("clear")();
    delayed(model, [&] {
        for (nb::handle item : to_emit) {
            nb::object name = item[nb::int_(0)];
            nb::object events = model.attr("events");
            nb::object emitter = nb::steal<nb::object>(PyObject_GetAttr(events.ptr(), name.ptr()));
            if (!emitter.is_valid()) throw nb::python_error();
            emitter(nb::arg("value") = item[nb::int_(1)]);
        }
        nb::object events = model.attr("events");
        if (nb::len(nb::object(events.attr("callbacks")))) {
            nb::object first = to_emit[0];
            events(nb::arg("type_name") = first[nb::int_(0)],
                   nb::arg("value") = first[nb::int_(1)]);
        }
    });
}

nb::object quiet_array_equal(nb::args args, nb::kwargs kwargs) {
    nb::object context = nb::module_::import_("warnings").attr("catch_warnings")();
    context.attr("__enter__")();
    try {
        nb::module_::import_("warnings").attr("filterwarnings")("ignore", "elementwise comparison");
        nb::object function = nb::module_::import_("numpy").attr("array_equal");
        nb::object result = nb::steal<nb::object>(PyObject_Call(function.ptr(), args.ptr(), kwargs.ptr()));
        if (!result.is_valid()) throw nb::python_error();
        context.attr("__exit__")(nb::none(), nb::none(), nb::none());
        return result;
    } catch (const nb::python_error& error) {
        context.attr("__exit__")(error.type(), error.value(), error.traceback());
        throw;
    }
}

} // namespace

void attach_transaction_diagnostics(nb::handle mapping, nb::handle owner) {
    // Bypass Python mapping/subclass hooks: this optional legacy diagnostic is
    // neither application state nor a prerequisite for provider-backed maps.
    if (!PyDict_CheckExact(mapping.ptr())) return;
    nb::object held_mapping = nb::borrow<nb::object>(mapping);
    PyObject* value = PyDict_GetItemString(mapping.ptr(), "_change_state");
    if (!value || !nb::isinstance<ModelJournal>(nb::handle(value)) ||
        !nb::inst_ready(nb::handle(value))) return;
    // Weakref allocation can trigger cyclic GC and arbitrary finalizers.
    // Keep the exact mapping and selected journal alive across that boundary.
    nb::object held_journal = nb::borrow<nb::object>(value);
    auto& journal = nb::cast<ModelJournal&>(held_journal);
    if (journal.private_owner.is_valid()) {
        nb::object previous = journal.private_owner();
        // Pydantic's intermediate shallow private map still holds the source
        // journal. Do not redirect a saved source diagnostic to the new model;
        // the subsequent native journal copy is unbound and attaches separately.
        if (!previous.is_none()) return;
    }
    journal.private_owner = collections::checked(PyWeakref_NewRef(owner.ptr(), nullptr));
}

void bind_models(nb::module_& m) {
    bind_model_transaction(m);
    bind_model_helpers(m);
    nb::class_<ChangeJournal::PrimaryIterator>(m, "PrimaryChangesIterator",
            nb::type_slots(value_slots))
        .def("__iter__", [](ChangeJournal::PrimaryIterator& iterator)
                -> ChangeJournal::PrimaryIterator& { return iterator; },
             nb::rv_policy::reference_internal)
        .def("__next__", [](ChangeJournal::PrimaryIterator& iterator) {
            auto result = iterator.next();
            if (!result) throw nb::stop_iteration();
            return nb::str(result->c_str());
        });
    nb::class_<PrimaryChanges>(m, "PrimaryChanges", nb::type_slots(value_slots))
        .def("__len__", [](const PrimaryChanges& map) { return map.state.primary_names().size(); })
        .def("__iter__", [](const PrimaryChanges& map) {
            return map.state.iter_primary();
        })
        .def("__contains__", [](const PrimaryChanges& map, const std::string& name) {
            return map.state.primary(name);
        })
        .def("__getitem__", [](const PrimaryChanges& map, nb::str name) {
            if (!map.state.primary(nb::cast<std::string>(name))) missing_key(name);
            return nb::none();
        })
        .def("__setitem__", [](PrimaryChanges& map, const std::string& name, nb::handle) {
            map.state.mark_primary(name);
        }, nb::arg("name"), nb::arg("value").none())
        .def("__delitem__", [](PrimaryChanges& map, nb::str name) {
            if (!map.state.forget_primary(nb::cast<std::string>(name))) missing_key(name);
        })
        .def("clear", [](PrimaryChanges& map) { map.state.clear_primary(); })
        .def("keys", [](const PrimaryChanges& map) { return map.snapshot().attr("keys")(); })
        .def("values", [](const PrimaryChanges& map) { return map.snapshot().attr("values")(); })
        .def("items", [](const PrimaryChanges& map) { return map.snapshot().attr("items")(); })
        .def("copy", &PrimaryChanges::snapshot)
        .def("get", [](const PrimaryChanges& map, const std::string& name, nb::object fallback) {
            return map.state.primary(name) ? nb::object(nb::none()) : fallback;
        }, nb::arg("name"), nb::arg("default").none() = nb::none())
        .def("pop", [](PrimaryChanges& map, nb::str name) {
            if (!map.state.forget_primary(nb::cast<std::string>(name))) missing_key(name);
            return nb::none();
        })
        .def("pop", [](PrimaryChanges& map, const std::string& name, nb::object fallback) {
            return map.state.forget_primary(name) ? nb::object(nb::none()) : fallback;
        }, nb::arg("name"), nb::arg("default").none())
        .def("__eq__", [](const PrimaryChanges& map, nb::handle other) {
            return map.snapshot().equal(other);
        }, nb::arg("other").none())
        .def("__repr__", [](const PrimaryChanges& map) { return nb::repr(map.snapshot()); })
        .def("__copy__", [](const PrimaryChanges& map) { return map.snapshot(); })
        .def("__deepcopy__", [](const PrimaryChanges& map, nb::handle) {
            return PrimaryChanges(map.state.deep_copy());
        });
    nb::class_<ModelJournal>(m, "ChangeJournal", nb::is_weak_referenceable(),
                             nb::type_slots(journal_slots))
        .def(nb::init<>())
        .def("enter", [](ModelJournal& state) { state.state.enter(); })
        .def("leave", [](ModelJournal& state) { state.state.leave(); })
        .def("clear", [](ModelJournal& state) {
            state.state.clear();
            state.primary().attr("clear")();
        })
        .def("mark_primary", [](ModelJournal& state, const std::string& name) {
            state.primary()[nb::str(name.data(), name.size())] = nb::none();
        })
        .def("capture", [](ModelJournal& state, const std::string& name) { state.state.capture(name); })
        .def("emissions", [](const ModelJournal& state, const std::vector<bool>& changed) {
            return state.state.emissions(changed);
        })
        .def_prop_rw("depth", [](const ModelJournal& state) -> nb::object {
            nb::object map = state.mapping();
            return map.is_valid() ? nb::object(map[nb::str("_delay_check_semaphore")])
                                  : nb::object(nb::int_(state.state.depth()));
        }, [](ModelJournal& state, nb::object value) {
            nb::object map = state.mapping();
            if (map.is_valid()) map[nb::str("_delay_check_semaphore")] = value;
            else state.state.set_depth(nb::cast<std::int64_t>(value));
        })
        .def_prop_ro("primary_names", [](const ModelJournal& state) {
            return nb::module_::import_("builtins").attr("list")(state.primary());
        })
        .def_prop_ro("pending_names", [](const ModelJournal& state) { return state.state.pending_names(); })
        .def("__copy__", [](const ModelJournal& state) { return ModelJournal(state); })
        .def("__deepcopy__", &deep_copy_journal, nb::arg("memo").none());
    m.def("model_super_setattr", super_setattr,
          nb::arg("model"), nb::arg("name"), nb::arg("value").none());
    m.def("model_setattr", set_model,
          nb::arg("model"), nb::arg("name"), nb::arg("value").none());
    m.def("model_setattr_impl", setattr_impl,
          nb::arg("model"), nb::arg("name"), nb::arg("value").none());
    m.def("model_different", different,
          nb::arg("model"), nb::arg("name"), nb::arg("old").none());
    m.def("model_emit_changes", emit_changes);
    m.def("model_delay_enter", [](nb::handle model) { delay_change(model, true); });
    m.def("model_delay_leave", [](nb::handle model) { delay_change(model, false); check_emit(model); });
    m.def("model_delayer_exit", [](nb::handle context) {
        { nb::object target = context.attr("_target"); delay_change(target, false); }
        check_emit(context.attr("_target"));
    });
    m.def("model_primary_changes", [](nb::handle model) {
        if (has_model_transaction_owner(model))
            return nb::object(model.attr("_primary_changes"));
        nb::object result = model.attr("_primary_view");
        if (result.is_none()) {
            nb::object state = model.attr("_change_state");
            result = nb::cast<ModelJournal&>(state).primary_view;
            model.attr("__pydantic_private__")[nb::str("_primary_view")] = result;
        }
        return result;
    });
    m.def("pick_equality_operator", pick_equality, nb::arg("object").none());
    m.def("quiet_array_equal", quiet_array_equal);
    m.def("pandas_dataframe_equal", [](nb::handle left, nb::handle right) {
        return left.attr("equals")(right);
    }, nb::arg("left").none(), nb::arg("right").none());
}

} // namespace nx
