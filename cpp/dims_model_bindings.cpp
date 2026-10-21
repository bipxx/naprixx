#include "application.hpp"
#include "arrays.hpp"
#include "dims_dictionary.hpp"
#include "dims_flags_bindings.hpp"
#include "dims_namespace_owner.hpp"

#include <nanobind/stl/array.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>
#include <exception>
#include <functional>
#include <algorithm>
#include <iterator>
#include <numeric>
#include <stdexcept>
#include <string>

namespace nx {
namespace {
using app::DimsField;
using app::DimsState;
using app::Index;
using app::Range;

constexpr const char* fields[] = {
    "ndim", "ndisplay", "order", "axis_labels", "rollable", "range",
    "margin_left", "margin_right", "point", "last_used"
};

nb::object generic_get(nb::handle model, const char* name) {
    nb::str key(name);
    auto result = nb::steal<nb::object>(PyObject_GenericGetAttr(model.ptr(), key.ptr()));
    if (!result.is_valid()) throw nb::python_error();
    return result;
}

nb::object private_values(nb::handle model) {
    return dims_namespace_storage(model);
}

nb::dict raw_fields(nb::handle model) {
    return nb::cast<nb::dict>(generic_get(model, "__dict__"));
}

void prepare_flags(nb::handle model, nb::handle values) {
    if (values.is_none()) return;
    nb::object storage = nb::borrow<nb::object>(values);
    nb::object raw = generic_get(model, "__pydantic_private__");
    if (!PyDict_Check(raw.ptr())) {
        storage[nb::str("_native_flag_record")] = nb::none();
        storage[nb::str("_native_flags")] = nb::none();
        return;
    }
    nb::object record = storage[nb::str("_native_flag_record")];
    nb::object previous = record.is_none() ? nb::none() : record.attr("dictionary");
    if (previous.ptr() != raw.ptr()) {
        record = dims_flag_record(nb::borrow<nb::dict>(raw));
        storage[nb::str("_native_flag_record")] = record;
        storage[nb::str("_native_flags")] = dims_flag_owner(record);
    }
    refresh_dims_flag_record(record);
}

int validating_truth(nb::handle model) {
    // Invoke the public hook even for primitive values: plugin subclasses can
    // override attribute access. Use native state only for its exact view.
    nb::object value = model.attr("_validating");
    nb::object values = private_values(model);
    prepare_flags(model, values);
    if (!values.is_none()) {
        nb::object storage = nb::borrow<nb::object>(values);
        nb::object state = storage[nb::str("_native_flags")];
        nb::object raw = generic_get(model, "__pydantic_private__");
        if (!state.is_none() && PyDict_Check(raw.ptr()) &&
            PyDict_GetItemString(raw.ptr(), "_validating") == value.ptr()) {
            const auto truth = nb::cast<app::DimsFlags&>(state).value(app::DimsFlag::Validating).truth();
            if (truth) return static_cast<int>(*truth);
        }
    }
    return dims_flag_truth(value);
}

int depth(nb::handle values) {
    if (values.is_none()) return 0;
    nb::object data = nb::borrow<nb::object>(values);
    auto name = nb::str("_native_boundary_depth");
    return nb::cast<int>(data[name]);
}

nb::object owner(nb::handle values) {
    if (values.is_none()) return nb::none();
    nb::object data = nb::borrow<nb::object>(values);
    auto name = nb::str("_native_state");
    return nb::borrow<nb::object>(data[name]);
}

nb::object super_model(nb::handle model) {
    nb::object type = nb::module_::import_("napari.components.dims").attr("Dims");
    return nb::module_::import_("builtins").attr("super")(type, model);
}

nb::object dictionary_record(nb::handle values) {
    if (values.is_none()) return nb::none();
    nb::object data = nb::borrow<nb::object>(values);
    return nb::borrow<nb::object>(data[nb::str("_native_dictionary_record")]);
}

void prepare_dictionary(nb::handle model, nb::handle values) {
    nb::object record = dictionary_record(values);
    if (record.is_none() || depth(values)) return;
    nb::dict raw = raw_fields(model);
    nb::object previous = record.attr("dictionary");
    if (previous.ptr() != raw.ptr()) {
        nb::object projection = nb::steal<nb::object>(PyDictProxy_New(raw.ptr()));
        if (!projection.is_valid()) throw nb::python_error();
        attach_dims_dictionary(record, raw);
        // __dict__ replacement is deliberately unvalidated.
        auto& core = nb::cast<DimsState&>(owner(values));
        for (unsigned index = 0; index < std::size(fields); ++index)
            core.external(static_cast<DimsField>(index), true);
        // Drop the adapter's stale view while preserving any views held by
        // callers. 'previous' pins old payloads until refresh finishes below.
        nb::borrow<nb::object>(values)[nb::str("_native_projection")] = std::move(projection);
    }
    refresh_dims_dictionary(record);
}

nb::object field_value(nb::handle model, DimsField field, nb::handle values) {
    const auto name = fields[static_cast<unsigned>(field)];
    prepare_dictionary(model, values);
    nb::dict raw = raw_fields(model);
    nb::str key(name);
    if (!raw.contains(key)) {
        PyErr_Format(PyExc_AttributeError, "'%s' object has no attribute '%s'",
                     Py_TYPE(model.ptr())->tp_name, name);
        throw nb::python_error();
    }
    return nb::borrow<nb::object>(raw[key]);
}

nb::dict projected_fields(nb::handle model, nb::handle values) {
    prepare_dictionary(model, values);
    return raw_fields(model);
}

void set_core(DimsState& state, DimsField field, nb::handle value) {
    switch (field) {
    case DimsField::Ndim: state.ndim = nb::cast<Index>(value); break;
    case DimsField::Ndisplay: state.ndisplay = nb::cast<Index>(value); break;
    case DimsField::Order: state.order = nb::cast<std::vector<Index>>(value); break;
    case DimsField::AxisLabels: state.axis_labels = nb::cast<std::vector<std::string>>(value); break;
    case DimsField::Rollable: state.rollable = nb::cast<std::vector<bool>>(value); break;
    case DimsField::Ranges: state.ranges = nb::cast<std::vector<Range>>(value); break;
    case DimsField::MarginLeft: state.margin_left = nb::cast<std::vector<double>>(value); break;
    case DimsField::MarginRight: state.margin_right = nb::cast<std::vector<double>>(value); break;
    case DimsField::Point: state.point = nb::cast<std::vector<double>>(value); break;
    case DimsField::LastUsed: state.last_used = nb::cast<Index>(value); break;
    }
}

bool canonical_value(DimsField field, nb::handle value) {
    if (field == DimsField::Ndim || field == DimsField::Ndisplay || field == DimsField::LastUsed)
        return PyLong_CheckExact(value.ptr());
    if (!PyTuple_CheckExact(value.ptr())) return false;
    for (nb::handle item : nb::borrow<nb::tuple>(value)) {
        if (field == DimsField::Order) {
            if (!PyLong_CheckExact(item.ptr())) return false;
        } else if (field == DimsField::AxisLabels) {
            if (!PyUnicode_CheckExact(item.ptr())) return false;
        } else if (field == DimsField::Rollable) {
            if (!PyBool_Check(item.ptr())) return false;
        } else if (field == DimsField::Ranges) {
            nb::object range_type = nb::module_::import_("napari.components.dims").attr("RangeTuple");
            if (reinterpret_cast<PyObject*>(Py_TYPE(item.ptr())) != range_type.ptr()) return false;
            if (!PyTuple_Check(item.ptr()) || PyTuple_GET_SIZE(item.ptr()) != 3) return false;
            for (Py_ssize_t position = 0; position < 3; ++position)
                if (!PyFloat_CheckExact(PyTuple_GET_ITEM(item.ptr(), position))) return false;
        } else if (!PyFloat_CheckExact(item.ptr())) return false;
    }
    return true;
}

void finish_boundary(nb::handle model, nb::object values, nb::handle trusted = nb::none()) {
    prepare_flags(model, values);
    nb::object previous = owner(values);
    DimsState state = previous.is_none() ? DimsState{} : nb::cast<DimsState&>(previous);
    nb::dict old_providers = nb::cast<nb::dict>(values[nb::str("_native_overrides")]);
    nb::object record = dictionary_record(values);
    nb::object previous_dictionary = record.is_none() ? nb::none() : record.attr("dictionary");
    nb::dict raw = raw_fields(model);
    for (unsigned index = 0; index < std::size(fields); ++index) {
        const auto field = static_cast<DimsField>(index);
        nb::str key(fields[index]);
        if (!raw.contains(key)) {
            state.present(field, false);
            state.external(field, false);
            continue;
        }
        state.present(field, true);
        nb::object value = nb::borrow<nb::object>(raw[key]);
        const int trusted_field = trusted.is_none() ? 0 : PySequence_Contains(trusted.ptr(), key.ptr());
        if (trusted_field < 0) throw nb::python_error();
        // Plugin schema/validator results can intentionally have different
        // public types from the standard field. Their object representation is
        // a GC-visible provider, rather than silently converting the facade.
        bool provider = trusted_field != 0 || !canonical_value(field, value);
        if (old_providers.contains(key) && old_providers[key].ptr() == value.ptr()) provider = true;
        if (!previous.is_none() && nb::cast<DimsState&>(previous).external(field) &&
            !previous_dictionary.is_none() &&
            PyDict_GetItem(previous_dictionary.ptr(), key.ptr()) == value.ptr()) provider = true;
        // Adopting a trusted value must not run its sequence, conversion, or
        // error-producing protocols. Those belong to the public operation
        // that actually consumes the field, as they do in Pydantic.
        if (!provider) {
            try {
                if (field == DimsField::Ranges) {
                    // RangeTuple is a tuple subtype, so its Python iterator
                    // can be replaced independently of these primitive slots.
                    std::vector<Range> ranges;
                    ranges.reserve(PyTuple_GET_SIZE(value.ptr()));
                    for (Py_ssize_t row = 0; row < PyTuple_GET_SIZE(value.ptr()); ++row) {
                        PyObject* range = PyTuple_GET_ITEM(value.ptr(), row);
                        ranges.push_back({
                            PyFloat_AS_DOUBLE(PyTuple_GET_ITEM(range, 0)),
                            PyFloat_AS_DOUBLE(PyTuple_GET_ITEM(range, 1)),
                            PyFloat_AS_DOUBLE(PyTuple_GET_ITEM(range, 2))});
                    }
                    state.ranges = std::move(ranges);
                } else set_core(state, field, value);
            } catch (const nb::cast_error&) {
                // Exact primitives can still exceed the native numeric
                // representation. Preserve them at the provider edge.
                provider = true;
            }
        }
        state.external(field, provider);
    }
    if (record.is_none()) {
        record = make_dims_dictionary_record(state);
        values[nb::str("_native_dictionary_record")] = record;
        values[nb::str("_native_state")] = dims_dictionary_owner(record);
    } else nb::cast<DimsState&>(previous) = std::move(state);
    attach_dims_dictionary(record, raw);
    values[nb::str("_native_overrides")] = nb::dict();
    values[nb::str("_native_projection")] =
        nb::module_::import_("types").attr("MappingProxyType")(raw);
    values[nb::str("_native_candidate")] = nb::none();
    // The plain dict is a compatibility representation. Its native watcher
    // synchronizes direct writes, including C API writes, with this owner.
}

void boundary(nb::handle model, const std::function<void()>& action) {
    nb::object values = private_values(model);
    const int previous = depth(values);
    if (previous == 0) {
        nb::dict materialized = projected_fields(model, values);
        nb::dict raw = raw_fields(model);
        if (PyDict_Update(raw.ptr(), materialized.ptr()) < 0) throw nb::python_error();
    }
    values[nb::str("_native_boundary_depth")] = nb::int_(previous + 1);
    nb::object record = dictionary_record(values);
    if (!record.is_none()) pause_dims_dictionary(record, previous + 1);
    auto finish = [&] {
        try {
            if (previous == 0) finish_boundary(model, values);
        } catch (...) {
            values[nb::str("_native_boundary_depth")] = nb::int_(previous);
            nb::object current = dictionary_record(values);
            if (!current.is_none()) pause_dims_dictionary(current, previous);
            throw;
        }
        values[nb::str("_native_boundary_depth")] = nb::int_(previous);
        nb::object current = dictionary_record(values);
        if (!current.is_none()) pause_dims_dictionary(current, previous);
    };
    try {
        action();
    } catch (...) {
        const auto original = std::current_exception();
        finish();
        std::rethrow_exception(original);
    }
    finish();
}

void set_field(nb::handle model, DimsField field, nb::handle value) {
    const auto name = fields[static_cast<unsigned>(field)];
    boundary(model, [&] {
        nb::object config = model.attr("model_config");
        const int validating = PyObject_IsTrue(config.attr("get")("validate_assignment", false).ptr());
        if (validating < 0) throw nb::python_error();
        if (validating) {
            model.attr("__pydantic_validator__").attr("validate_assignment")(model, name, value);
        } else {
            raw_fields(model)[nb::str(name)] = value;
            model.attr("__pydantic_fields_set__").attr("add")(name);
            nb::object data = private_values(model);
            nb::dict providers = nb::cast<nb::dict>(data[nb::str("_native_overrides")]);
            providers[nb::str(name)] = value;
        }
    });
}

void delete_field(nb::handle model, DimsField field) {
    const auto name = fields[static_cast<unsigned>(field)];
    nb::dict raw = raw_fields(model);
    if (PyDict_DelItemString(raw.ptr(), name) < 0) {
            if (!PyErr_ExceptionMatches(PyExc_KeyError)) throw nb::python_error();
            PyErr_Clear();
            PyErr_Format(PyExc_AttributeError, "'%s' object has no attribute '%s'",
                         Py_TYPE(model.ptr())->tp_name, name);
            throw nb::python_error();
    }
}

using BinaryNumber = PyObject* (*)(PyObject*, PyObject*);

nb::object number(BinaryNumber operation, nb::handle left, nb::handle right) {
    nb::object result = nb::steal<nb::object>(operation(left.ptr(), right.ptr()));
    if (!result.is_valid()) throw nb::python_error();
    return result;
}

nb::object nonzero_step(nb::handle range) {
    nb::object step = range.attr("step");
    const int nonzero = PyObject_IsTrue(step.ptr());
    if (nonzero < 0) throw nb::python_error();
    return nonzero ? step : nb::int_(1);
}

nb::object computed_model(nb::handle model, const std::string& name);

template <typename Value>
nb::tuple numeric_tuple(const std::vector<Value>& values) {
    nb::list result;
    for (const auto& value : values) result.append(nb::cast(value));
    return nb::tuple(result);
}

nb::tuple range_nsteps(nb::handle ranges) {
    nb::list result;
    for (nb::handle range : ranges) {
        nb::object span = number(PyNumber_Subtract, range.attr("stop"), range.attr("start"));
        nb::object quotient = number(PyNumber_TrueDivide, span, nonzero_step(range));
        nb::object count = nb::steal<nb::object>(PyNumber_Long(quotient.ptr()));
        if (!count.is_valid()) throw nb::python_error();
        result.append(number(PyNumber_Add, count, nb::int_(1)));
    }
    return nb::tuple(result);
}

nb::object computed_protocol(nb::handle model, const std::string& name) {
    nb::module_ builtins = nb::module_::import_("builtins");
    if (name == "displayed" || name == "not_displayed") {
        nb::object order = model.attr("order");
        nb::object count = model.attr("ndisplay");
        nb::object negative = nb::steal<nb::object>(PyNumber_Negative(count.ptr()));
        if (!negative.is_valid()) throw nb::python_error();
        nb::object slice = builtins.attr("slice")(
            name == "displayed" ? negative : nb::none(),
            name == "displayed" ? nb::none() : negative);
        return order[slice];
    }
    if (name == "displayed_order") {
        // Preserve plugin overrides of displayed. Sorting decisions are native;
        // Python supplies the values' comparison/indexing protocols only.
        nb::object displayed = model.attr("displayed");
        std::vector<Index> indices(nb::len(displayed));
        std::iota(indices.begin(), indices.end(), Index{0});
        std::stable_sort(indices.begin(), indices.end(), [&](Index left, Index right) {
            nb::object a = displayed[nb::int_(left)];
            nb::object b = displayed[nb::int_(right)];
            const int less = PyObject_RichCompareBool(a.ptr(), b.ptr(), Py_LT);
            if (less < 0) throw nb::python_error();
            return less != 0;
        });
        nb::list result;
        for (auto index : indices) result.append(nb::int_(index));
        return nb::tuple(result);
    }
    nb::list result;
    if (name == "nsteps") return range_nsteps(model.attr("range"));
    if (name == "thickness" || name == "thickness_step") {
        nb::object left = model.attr(name == "thickness" ? "margin_left" : "margin_left_step");
        nb::object right = model.attr(name == "thickness" ? "margin_right" : "margin_right_step");
        for (nb::handle item : builtins.attr("zip")(left, right)) {
            nb::tuple pair = nb::borrow<nb::tuple>(item);
            result.append(number(PyNumber_Add, pair[0], pair[1]));
        }
        return nb::tuple(result);
    }
    const char* value_name = name == "current_step" ? "point"
                           : name == "margin_left_step" ? "margin_left" : "margin_right";
    nb::object positions = model.attr(value_name);
    nb::object ranges = model.attr("range");
    for (nb::handle item : builtins.attr("zip")(positions, ranges)) {
        nb::tuple pair = nb::borrow<nb::tuple>(item);
        nb::object position = pair[0];
        nb::object range = pair[1];
        if (name == "current_step") position = number(PyNumber_Subtract, position, range.attr("start"));
        result.append(builtins.attr("round")(number(PyNumber_TrueDivide, position, nonzero_step(range))));
    }
    return nb::tuple(result);
}

nb::object computed_model(nb::handle model, const std::string& name) {
    std::vector<DimsField> required;
    if (name == "nsteps") required = {DimsField::Ranges};
    else if (name == "current_step") required = {DimsField::Point, DimsField::Ranges};
    else if (name == "margin_left_step") required = {DimsField::MarginLeft, DimsField::Ranges};
    else if (name == "margin_right_step") required = {DimsField::MarginRight, DimsField::Ranges};
    else if (name == "thickness") required = {DimsField::MarginLeft, DimsField::MarginRight};
    else if (name == "displayed" || name == "not_displayed") required = {DimsField::Order, DimsField::Ndisplay};
    else if (name == "displayed_order" || name == "thickness_step")
        return computed_protocol(model, name);
    else throw nb::attribute_error(name.c_str());

    DimsState state;
    bool typed = true;
    nb::object storage = private_values(model);
    nb::object persistent = owner(storage);
    for (auto field : required) {
        // Access through the public descriptor to preserve plugin overrides and
        // report only the missing/bad fields used by this computed property.
        nb::object value = nb::getattr(model, fields[static_cast<unsigned>(field)]);
        if (!depth(storage) && !persistent.is_none() && nb::cast<DimsState&>(persistent).external(field))
            typed = false;
        if (!canonical_value(field, value)) typed = false;
        if (typed) {
            try { set_core(state, field, value); }
            catch (const nb::cast_error&) { typed = false; }
        }
    }
    if (!typed) return computed_protocol(model, name);
    try {
        if (name == "nsteps") return numeric_tuple(state.nsteps());
        if (name == "current_step") return numeric_tuple(state.current_step());
        if (name == "margin_left_step") return numeric_tuple(state.margin_steps(true));
        if (name == "margin_right_step") return numeric_tuple(state.margin_steps(false));
        if (name == "thickness") return numeric_tuple(state.thickness());
        return numeric_tuple(state.displayed(name == "displayed"));
    } catch (const std::overflow_error&) {
        // The adapter retains Python's arbitrary-sized integer operations.
        return computed_protocol(model, name);
    } catch (const std::invalid_argument&) {
        return computed_protocol(model, name);
    }
}

void set_computed(nb::handle model, const std::string& name, nb::handle value) {
    nb::module_ builtins = nb::module_::import_("builtins");
    nb::list result;
    if (name == "thickness" || name == "thickness_step") {
        for (nb::handle item : value)
            result.append(number(name == "thickness" ? PyNumber_TrueDivide : PyNumber_FloorDivide,
                                 item, nb::int_(2)));
        nb::tuple divided(result);
        nb::setattr(model, name == "thickness" ? "margin_left" : "margin_left_step", divided);
        nb::setattr(model, name == "thickness" ? "margin_right" : "margin_right_step", divided);
        return;
    }
    nb::object ranges = model.attr("range");
    nb::object pairs = name == "nsteps" ? builtins.attr("zip")(ranges, value)
                                       : builtins.attr("zip")(value, ranges);
    nb::object range_type = nb::module_::import_("napari.components.dims").attr("RangeTuple");
    for (nb::handle item : pairs) {
        nb::tuple pair = nb::borrow<nb::tuple>(item);
        if (name == "nsteps") {
            nb::object range = pair[0];
            nb::object count = pair[1];
            nb::object span = number(PyNumber_Subtract, range.attr("stop"), range.attr("start"));
            nb::object step = number(PyNumber_TrueDivide, span, number(PyNumber_Subtract, count, nb::int_(1)));
            result.append(range_type(range.attr("start"), range.attr("stop"), step));
        } else {
            nb::object range = pair[1];
            nb::object position = number(PyNumber_Multiply, pair[0], nonzero_step(range));
            if (name == "current_step") position = number(PyNumber_Add, range.attr("start"), position);
            result.append(position);
        }
    }
    if (name == "nsteps") model.attr("range") = nb::tuple(result);
    else if (name == "current_step") model.attr("point") = nb::tuple(result);
    else if (name == "margin_left_step") model.attr("margin_left") = nb::tuple(result);
    else if (name == "margin_right_step") model.attr("margin_right") = nb::tuple(result);
    else throw nb::attribute_error(name.c_str());
}

void set_on_axes(nb::handle model, const std::string& name, nb::handle axis, nb::handle value) {
    const bool ranges = name == "range";
    nb::object sanitized = model.attr("_sanitize_input")(axis, value,
        nb::arg("value_is_sequence") = ranges);
    nb::object axes = sanitized[nb::int_(0)];
    nb::object values = sanitized[nb::int_(1)];
    const char* field = name == "current_step" ? "point" : name.c_str();
    nb::module_ builtins = nb::module_::import_("builtins");
    nb::object updated = builtins.attr("list")(nb::getattr(model, field));
    for (nb::handle item : builtins.attr("zip")(axes, values)) {
        nb::tuple pair = nb::borrow<nb::tuple>(item);
        nb::object next = pair[1];
        if (name == "current_step") {
            nb::object ranges_value = model.attr("range");
            nb::object range = ranges_value[pair[0]];
            next = number(PyNumber_Add, range.attr("start"),
                          number(PyNumber_Multiply, next, nonzero_step(range)));
        }
        updated[pair[0]] = next;
    }
    nb::setattr(model, field, nb::tuple(updated));
}

nb::object repeated(nb::handle value, nb::handle count) {
    return number(PyNumber_Multiply, nb::make_tuple(value), count);
}

void execute_model(nb::handle model, const std::string& name, nb::handle axis, int direction) {
    nb::module_ builtins = nb::module_::import_("builtins");
    if (name == "reset") {
        // Query ndim at each transition: callbacks may change it between the
        // original reset assignments. Labels and units are intentionally kept.
        model.attr("range") = repeated(nb::make_tuple(0, 2, 1), model.attr("ndim"));
        model.attr("point") = repeated(nb::int_(0), model.attr("ndim"));
        model.attr("order") = builtins.attr("tuple")(builtins.attr("range")(model.attr("ndim")));
        model.attr("margin_left") = repeated(nb::int_(0), model.attr("ndim"));
        model.attr("margin_right") = repeated(nb::int_(0), model.attr("ndim"));
        model.attr("rollable") = repeated(nb::bool_(true), model.attr("ndim"));
        return;
    }
    if (name == "transpose") {
        nb::object order = builtins.attr("list")(model.attr("order"));
        nb::object left = order[nb::int_(-2)];
        nb::object right = order[nb::int_(-1)];
        order[nb::int_(-2)] = right;
        order[nb::int_(-1)] = left;
        model.attr("order") = order;
        return;
    }
    if (name == "increment") {
        nb::object selected = axis.is_none() ? nb::borrow<nb::object>(model.attr("last_used"))
                                            : nb::borrow<nb::object>(axis);
        nb::object steps = model.attr("current_step");
        nb::object current = steps[selected];
        // Keep the public method hook: plugins can override set_current_step.
        model.attr("set_current_step")(selected, number(PyNumber_Add, current, nb::int_(direction)));
        return;
    }
    if (name == "focus") {
        nb::list sliders;
        for (nb::handle selected : model.attr("not_displayed")) {
            nb::object steps = model.attr("nsteps");
            nb::object count = steps[selected];
            const int available = PyObject_RichCompareBool(count.ptr(), nb::int_(1).ptr(), Py_GT);
            if (available < 0) throw nb::python_error();
            if (available) sliders.append(selected);
        }
        const auto size = static_cast<Index>(nb::len(sliders));
        if (!size) return;
        const auto index = nb::cast<Index>(sliders.attr("index")(model.attr("last_used")));
        model.attr("last_used") = sliders[static_cast<nb::ssize_t>((index + direction + size) % size)];
        return;
    }
    if (name == "roll") {
        nb::module_ numpy = nb::module_::import_("numpy");
        nb::object order = numpy.attr("array")(model.attr("order"));
        nb::object step_values = numpy.attr("array")(model.attr("nsteps"));
        nb::object movable = numpy.attr("logical_and")(model.attr("rollable"),
            numpy.attr("greater")(step_values, 1));
        nb::object valid = movable[order];
        nb::object selected = order[valid];
        // Gather/assign keep external NumPy dtype/broadcasting semantics; the
        // actual cyclic permutation is handwritten C++ over the selected view.
        const auto size = nb::len(selected);
        if (size) {
            nb::object previous = selected[nb::int_(size - 1)];
            for (size_t index = size - 1; index > 0; --index)
                selected[nb::int_(index)] = selected[nb::int_(index - 1)];
            selected[nb::int_(0)] = previous;
        }
        order[valid] = selected;
        model.attr("order") = order;
        return;
    }
    if (name == "center") {
        nb::list positions;
        for (nb::handle steps : model.attr("nsteps")) {
            nb::object halfway = number(PyNumber_TrueDivide,
                number(PyNumber_Subtract, steps, nb::int_(1)), nb::int_(2));
            nb::object position = nb::steal<nb::object>(PyNumber_Long(halfway.ptr()));
            if (!position.is_valid()) throw nb::python_error();
            positions.append(position);
        }
        model.attr("current_step") = positions;
        return;
    }
    throw nb::attribute_error(name.c_str());
}

DimsState raw_state(nb::handle model) {
    DimsState state;
    nb::dict raw = raw_fields(model);
    for (unsigned index = 0; index < std::size(fields); ++index)
        set_core(state, static_cast<DimsField>(index), raw[nb::str(fields[index])]);
    return state;
}

struct DimsValidationGenerator {
    enum class Stage { Created, Suspended, Closed };
    Stage stage = Stage::Created;
};

void close_validation_generator(nb::handle self) {
    auto& generator = *nb::inst_ptr<DimsValidationGenerator>(self);
    const bool restore = generator.stage == DimsValidationGenerator::Stage::Suspended;
    generator.stage = DimsValidationGenerator::Stage::Closed;
    nb::object model = self.attr("_model");
    nb::object previous = self.attr("_previous");
    // References live in dynamic_attr's GC-traversed Python dictionary, never
    // in the C++ generator state. Release them even when a plugin setter fails.
    self.attr("_model") = nb::none();
    self.attr("_previous") = nb::none();
    if (restore) model.attr("_validating") = previous;
}

nb::object next_validation_generator(nb::handle self) {
    auto& generator = *nb::inst_ptr<DimsValidationGenerator>(self);
    if (generator.stage == DimsValidationGenerator::Stage::Created) {
        nb::object model = self.attr("_model");
        self.attr("_previous") = model.attr("_validating");
        try {
            model.attr("_validating") = nb::bool_(true);
        } catch (...) {
            close_validation_generator(self);
            throw;
        }
        generator.stage = DimsValidationGenerator::Stage::Suspended;
        return nb::none();
    }
    if (generator.stage == DimsValidationGenerator::Stage::Suspended)
        close_validation_generator(self);
    throw nb::stop_iteration();
}

void throw_validation_generator(nb::handle self, nb::args args) {
    if (args.size() < 1 || args.size() > 3) {
        PyErr_Format(PyExc_TypeError, "throw expected %s %d argument%s, got %zd",
                     args.size() < 1 ? "at least" : "at most",
                     args.size() < 1 ? 1 : 3, args.size() < 1 ? "" : "s", args.size());
        throw nb::python_error();
    }
    nb::object supplied = args[0];
    nb::object value = args.size() > 1 ? nb::borrow<nb::object>(args[1]) : nb::none();
    nb::object traceback = args.size() > 2 ? nb::borrow<nb::object>(args[2]) : nb::none();
    if (!traceback.is_none() && !PyTraceBack_Check(traceback.ptr()))
        throw nb::type_error("throw() third argument must be a traceback object");
    nb::object exception;
    if (PyExceptionInstance_Check(supplied.ptr())) {
        if (!value.is_none()) throw nb::type_error("instance exception may not have a separate value");
        exception = supplied;
    } else if (PyExceptionClass_Check(supplied.ptr())) {
        PyErr_SetObject(supplied.ptr(), value.ptr());
        PyObject *type = nullptr, *normalized = nullptr, *trace = nullptr;
        PyErr_Fetch(&type, &normalized, &trace);
        PyErr_NormalizeException(&type, &normalized, &trace);
        nb::object owned_type = nb::steal<nb::object>(type);
        exception = nb::steal<nb::object>(normalized);
        Py_XDECREF(trace);
        if (PyErr_Occurred()) throw nb::python_error();
    } else {
        PyErr_Format(PyExc_TypeError,
                     "exceptions must be classes or instances deriving from BaseException, not %s",
                     Py_TYPE(supplied.ptr())->tp_name);
        throw nb::python_error();
    }
    if (!traceback.is_none() && PyException_SetTraceback(exception.ptr(), traceback.ptr()) < 0)
        throw nb::python_error();
    const bool running = nb::inst_ptr<DimsValidationGenerator>(self)->stage !=
                         DimsValidationGenerator::Stage::Closed;
    close_validation_generator(self);
    if (running && PyObject_IsInstance(exception.ptr(), PyExc_StopIteration) == 1) {
        nb::object wrapped = nb::steal<nb::object>(
            PyObject_CallFunction(PyExc_RuntimeError, "s", "generator raised StopIteration"));
        if (!wrapped.is_valid()) throw nb::python_error();
        PyException_SetCause(wrapped.ptr(), exception.inc_ref().ptr());
        PyException_SetContext(wrapped.ptr(), exception.inc_ref().ptr());
        PyErr_SetObject(PyExc_RuntimeError, wrapped.ptr());
    } else {
        PyErr_SetObject(reinterpret_cast<PyObject*>(Py_TYPE(exception.ptr())), exception.ptr());
    }
    throw nb::python_error();
}

void finalize_validation_generator(PyObject* self) noexcept {
    if (!nb::inst_ready(self) || nb::inst_ptr<DimsValidationGenerator>(self)->stage ==
                                DimsValidationGenerator::Stage::Closed) return;
    PyObject *type = nullptr, *value = nullptr, *traceback = nullptr;
    PyErr_Fetch(&type, &value, &traceback);
    try {
        close_validation_generator(self);
    } catch (nb::python_error& error) {
        error.restore();
        PyErr_WriteUnraisable(self);
    } catch (const std::exception& error) {
        PyErr_SetString(PyExc_RuntimeError, error.what());
        PyErr_WriteUnraisable(self);
    } catch (...) {
        PyErr_SetString(PyExc_RuntimeError, "failed to close dimensional validation context");
        PyErr_WriteUnraisable(self);
    }
    PyErr_Restore(type, value, traceback);
}

const PyType_Slot validation_generator_slots[] = {
    {Py_tp_finalize, reinterpret_cast<void*>(finalize_validation_generator)},
    {0, nullptr}
};

destructor validation_generator_base_dealloc = nullptr;

void deallocate_validation_generator(PyObject* self) noexcept {
    // nanobind's default deallocator clears dynamic_attr without invoking
    // tp_finalize on ordinary reference-count destruction. Run CPython's
    // resurrection-aware finalizer first, then retain nanobind's destruction.
    if (PyObject_CallFinalizerFromDealloc(self) < 0) return;
    validation_generator_base_dealloc(self);
}

template <typename Action>
void in_context(nb::object context, const Action& action) {
    context.attr("__enter__")();
    try {
        action();
    } catch (nb::python_error& failure) {
        failure.restore();
        PyObject *type = nullptr, *value = nullptr, *traceback = nullptr;
        PyErr_Fetch(&type, &value, &traceback);
        auto error_type = nb::steal<nb::object>(type);
        auto error_value = value ? nb::steal<nb::object>(value) : nb::none();
        auto error_traceback = traceback ? nb::steal<nb::object>(traceback) : nb::none();
        const auto suppressed = context.attr("__exit__")(error_type, error_value, error_traceback);
        const int suppress = PyObject_IsTrue(suppressed.ptr());
        if (suppress < 0) throw nb::python_error();
        if (suppress) return;
        PyErr_Restore(error_type.release().ptr(), error_value.release().ptr(),
                      error_traceback.is_none() ? nullptr : error_traceback.release().ptr());
        throw nb::python_error();
    } catch (...) {
        context.attr("__exit__")(nb::none(), nb::none(), nb::none());
        throw;
    }
    context.attr("__exit__")(nb::none(), nb::none(), nb::none());
}

nb::object validate_model(nb::object model) {
    const int validating = validating_truth(model);
    if (validating < 0) throw nb::python_error();
    if (validating) return model;
    boundary(model, [&] {
        const Index dimensionality = nb::cast<Index>(model.attr("ndim"));
        nb::module_ builtins = nb::module_::import_("builtins");
        nb::object resize = nb::module_::import_("naparixx._native").attr("ensure_len");
        nb::object range_type = nb::module_::import_("napari.components.dims").attr("RangeTuple");
        nb::object order;
        in_context(model.attr("events").attr("blocker_all")(), [&] {
            in_context(model.attr("_validating_ctx")(), [&] {
                // Apply each field before planning the next. Plugin validators
                // can change later inputs; upstream observes those changes.
                nb::object ranges = resize(model.attr("range"), dimensionality, nb::make_tuple(0.0, 2.0, 1.0));
                nb::list named_ranges;
                for (nb::handle range : ranges) named_ranges.append(range_type(*nb::tuple(range)));
                model.attr("range") = nb::tuple(named_ranges);

                nb::object positions = resize(model.attr("point"), dimensionality, 0.0);
                ranges = model.attr("range");
                nb::object normalized_point;
                if (canonical_value(DimsField::Point, positions) && canonical_value(DimsField::Ranges, ranges)) {
                    DimsState point;
                    point.ndim = dimensionality;
                    set_core(point, DimsField::Point, positions);
                    set_core(point, DimsField::Ranges, ranges);
                    point.normalize_point();
                    point.point.resize(std::min(point.point.size(), point.ranges.size()));
                    normalized_point = numeric_tuple(point.point);
                } else {
                    nb::list clipped;
                    nb::object clip = nb::module_::import_("numpy").attr("clip");
                    for (nb::handle item : builtins.attr("zip")(positions, ranges)) {
                        nb::tuple pair = nb::borrow<nb::tuple>(item);
                        nb::object range = pair[1];
                        clipped.append(clip(pair[0], range.attr("start"), range.attr("stop")));
                    }
                    normalized_point = nb::tuple(clipped);
                }
                model.attr("point") = normalized_point;
                for (const auto* name : {"margin_left", "margin_right"})
                    nb::setattr(model, name, resize(nb::getattr(model, name), dimensionality, 0.0));

                order = model.attr("order");
                const auto count = static_cast<Index>(nb::len(order));
                if (count < dimensionality) {
                    nb::list extended;
                    const Index offset = dimensionality - count;
                    for (Index index = 0; index < offset; ++index) extended.append(nb::int_(index));
                    for (nb::handle axis : order) extended.append(number(PyNumber_Add, axis, nb::int_(offset)));
                    order = nb::tuple(extended);
                } else if (count > dimensionality) {
                    nb::object slice = builtins.attr("slice")(-dimensionality, nb::none());
                    nb::object remaining = order[slice];
                    nb::object sorted = builtins.attr("sorted")(remaining);
                    nb::dict positions_by_axis;
                    Index position = 0;
                    for (nb::handle axis : sorted) positions_by_axis[axis] = nb::int_(position++);
                    nb::list reduced;
                    for (nb::handle axis : remaining) reduced.append(positions_by_axis[axis]);
                    order = nb::tuple(reduced);
                }
                model.attr("order") = order;
                nb::object actual = builtins.attr("set")(model.attr("order"));
                nb::object expected = builtins.attr("set")(builtins.attr("range")(dimensionality));
                if (!actual.equal(expected)) {
                    nb::object message = builtins.attr("str")(model.attr("order"));
                    const auto detail = "Invalid ordering " + nb::cast<std::string>(message) + " for " + std::to_string(dimensionality) + " dimensions";
                    PyErr_SetString(PyExc_ValueError, detail.c_str());
                    throw nb::python_error();
                }
            });
        });

        nb::object labels = model.attr("axis_labels");
        const Index labels_count = static_cast<Index>(nb::len(labels));
        if (labels_count < dimensionality) {
            nb::list extended;
            for (Index index = -dimensionality; index < -labels_count; ++index)
                extended.append(nb::str(std::to_string(index).c_str()));
            for (nb::handle label : labels) extended.append(label);
            model.attr("axis_labels") = nb::tuple(extended);
        } else if (labels_count > dimensionality) {
            nb::object slice = builtins.attr("slice")(-dimensionality, nb::none());
            model.attr("axis_labels") = labels[slice];
        }
        in_context(model.attr("_validating_ctx")(), [&] {
            model.attr("rollable") = resize(model.attr("rollable"), dimensionality, true);
        });
        nb::object last_used = model.attr("last_used");
        nb::object display_count = model.attr("ndisplay");
        nb::object ranges = model.attr("range");
        nb::object steps = model.attr("_nsteps_from_range")(ranges);
        nb::object negative = nb::steal<nb::object>(PyNumber_Negative(display_count.ptr()));
        if (!negative.is_valid()) throw nb::python_error();
        nb::object slice = builtins.attr("slice")(nb::none(), negative);
        nb::object axes = order[slice];
        nb::list sliders;
        for (nb::handle axis : axes) {
            const int in_range = PyObject_RichCompareBool(nb::int_(nb::len(steps)).ptr(), axis.ptr(), Py_GT);
            if (in_range < 0) throw nb::python_error();
            if (in_range) {
                nb::object count = steps[axis];
                const int available = PyObject_RichCompareBool(count.ptr(), nb::int_(1).ptr(), Py_GT);
                if (available < 0) throw nb::python_error();
                if (available) sliders.append(axis);
            }
        }
        const int visible = PySequence_Contains(sliders.ptr(), last_used.ptr());
        if (visible < 0) throw nb::python_error();
        if (nb::len(sliders) && !visible) model.attr("last_used") = sliders[0];
    });
    return model;
}

nb::object getattribute(nb::handle model, nb::str name) {
    const std::string key = nb::cast<std::string>(name);
    if (key == "_native_flags" || key == "_native_flag_record" ||
        key == "_play_ready" || key == "_scroll_progress" || key == "_validating") {
        nb::object values = private_values(model);
        prepare_flags(model, values);
        if (!values.is_none() && (key == "_native_flags" || key == "_native_flag_record"))
            return values[name];
    }
    if (key == "__dict__") return projected_fields(model, private_values(model));
    if (key == "_native_state") {
        nb::object values = private_values(model);
        prepare_dictionary(model, values);
        if (!values.is_none()) {
            nb::object data = nb::borrow<nb::object>(values);
            // After validators can run between individual assignments. Their
            // computed properties must read the current validation values,
            // rather than the proposed fully normalized state.
            if (depth(values)) return nb::cast(raw_state(model));
            nb::object state = owner(values);
            if (!state.is_none()) {
                nb::dict providers = raw_fields(model);
                for (unsigned index = 0; index < std::size(fields); ++index)
                    if (nb::cast<DimsState&>(state).external(static_cast<DimsField>(index)) &&
                        providers.contains(fields[index]))
                        try {
                            set_core(nb::cast<DimsState&>(state), static_cast<DimsField>(index),
                                     providers[nb::str(fields[index])]);
                        } catch (const nb::cast_error&) {
                            // The numeric cache is optional for an opaque
                            // provider. Public operations select dependencies.
                        }
                return state;
            }
        }
    }
    if (key == "_native_overrides") {
        nb::object values = private_values(model);
        prepare_dictionary(model, values);
        nb::dict providers;
        nb::object state = owner(values);
        if (!state.is_none()) {
            nb::dict raw = raw_fields(model);
            for (unsigned index = 0; index < std::size(fields); ++index)
                if (nb::cast<DimsState&>(state).external(static_cast<DimsField>(index)) &&
                    raw.contains(fields[index])) providers[nb::str(fields[index])] = raw[nb::str(fields[index])];
        }
        return providers;
    }
    if (key == "_native_boundary_depth" || key == "_native_candidate" ||
        key == "_native_dictionary_record" || key == "_native_projection") {
        nb::object values = private_values(model);
        if (key == "_native_projection") prepare_dictionary(model, values);
        if (!values.is_none()) return values[name];
    }
    return super_model(model).attr("__getattribute__")(name);
}

nb::object copy_model(nb::handle model, bool deep, nb::handle memo) {
    nb::object shared_memo = deep && memo.is_none() ? nb::dict() : nb::borrow<nb::object>(memo);
    nb::object result = deep ? super_model(model).attr("__deepcopy__")(shared_memo)
                             : super_model(model).attr("__copy__")();
    nb::object values = private_values(result);
    nb::object source_values = private_values(model);
    nb::object original = owner(source_values);
    if (!original.is_none()) values[nb::str("_native_state")] = nb::cast(DimsState(nb::cast<DimsState&>(original)));
    nb::object source_providers = model.attr("_native_overrides");
    values[nb::str("_native_overrides")] = deep
        ? nb::module_::import_("copy").attr("deepcopy")(source_providers, shared_memo)
        : nb::borrow<nb::object>(source_providers);
    finish_boundary(result, values);
    return result;
}

nb::object model_copy(nb::handle model, nb::handle update, bool deep) {
    nb::object result = deep ? model.attr("__deepcopy__")() : model.attr("__copy__")();
    nb::object values = private_values(result);
    nb::dict raw = raw_fields(result);
    nb::dict current = projected_fields(result, values);
    if (PyDict_Update(raw.ptr(), current.ptr()) < 0) throw nb::python_error();
    if (!update.is_none() && nb::len(update)) {
        nb::object declared = result.attr("__class__").attr("model_fields");
        nb::object config = result.attr("model_config");
        const bool allow = nb::cast<std::string>(nb::str(config.attr("get")("extra"))) == "allow";
        nb::object extras = result.attr("__pydantic_extra__");
        if (allow && extras.is_none()) {
            extras = nb::dict();
            result.attr("__pydantic_extra__") = extras;
        }
        for (auto item : update.attr("items")()) {
            nb::tuple pair = nb::borrow<nb::tuple>(item);
            nb::object key = pair[0];
            nb::object value = pair[1];
            const int field = PySequence_Contains(declared.ptr(), key.ptr());
            if (field < 0) throw nb::python_error();
            if (allow && !field) extras[key] = value;
            else raw[key] = value;
        }
        result.attr("__pydantic_fields_set__").attr("update")(update.attr("keys")());
    }
    finish_boundary(result, values, update);
    return result;
}

void setstate(nb::handle model, nb::handle state) {
    super_model(model).attr("__setstate__")(state);
    nb::str slot("_naparixx_dims_owner");
    if (PyObject_GenericSetAttr(model.ptr(), slot.ptr(), nb::none().ptr()) < 0)
        throw nb::python_error();
    nb::object values = private_values(model);
    finish_boundary(model, values);
}

void super_setattr(nb::handle model, nb::str name, nb::handle value) {
    auto assign = [&] {
        nb::module_::import_("naparixx._native").attr("model_super_setattr")(model, name, value);
    };
    nb::object declared = model.attr("__class__").attr("model_fields");
    const int field = PySequence_Contains(declared.ptr(), name.ptr());
    if (field < 0) throw nb::python_error();
    if (field) boundary(model, [&] {
        assign();
        nb::object config = model.attr("model_config");
        const int validating = PyObject_IsTrue(config.attr("get")("validate_assignment", false).ptr());
        if (validating < 0) throw nb::python_error();
        if (!validating) {
            nb::object data = private_values(model);
            nb::dict providers = nb::cast<nb::dict>(data[nb::str("_native_overrides")]);
            nb::dict raw = raw_fields(model);
            if (raw.contains(name)) providers[name] = raw[name];
        }
    });
    else assign();
}

void setattr_model(nb::handle model, nb::str name, nb::handle value) {
    const std::string key = nb::cast<std::string>(name);
    if (key == "_native_state" || key == "_native_boundary_depth" ||
        key == "_native_candidate" || key == "_native_overrides") {
        nb::object values = private_values(model);
        if (!values.is_none()) {
            values[name] = value;
            if (key == "_native_state") values[nb::str("_native_projection")] = nb::none();
            return;
        }
    }
    auto assign = [&] { super_model(model).attr("__setattr__")(name, value); };
    nb::object declared = model.attr("__class__").attr("model_fields");
    const int field = PySequence_Contains(declared.ptr(), name.ptr());
    if (field < 0) throw nb::python_error();
    if (!field) {
        assign();
        if (key == "__dict__") prepare_dictionary(model, private_values(model));
        if (key == "__pydantic_private__" || key == "_play_ready" ||
            key == "_scroll_progress" || key == "_validating")
            prepare_flags(model, private_values(model));
        return;
    }
    nb::object events = model.attr("events");
    const int evented = PySequence_Contains(events.ptr(), name.ptr());
    if (evented < 0) throw nb::python_error();
    // Pydantic model_construct intentionally creates no field emitters. The
    // EventedModel early path skips _super_setattr_, so materialize its schema
    // values here before validation reads or replaces the physical dict.
    if (!evented) boundary(model, [&] { super_setattr(model, name, value); });
    else assign();
}

nb::object construct_model(nb::object type, nb::handle field_set, nb::dict supplied) {
    nb::object base = nb::module_::import_("napari.components.dims").attr("Dims");
    nb::object method = nb::module_::import_("builtins").attr("super")(base, type)
                            .attr("model_construct");
    nb::object result = method(field_set, **supplied);
    finish_boundary(result, private_values(result), supplied);
    return result;
}

void initialize_model(nb::handle model, nb::dict supplied) {
    super_model(model).attr("__init__")(**supplied);
    nb::object values = private_values(model);
    // Plugin subclasses can replace _check_dims entirely. State adoption must
    // not depend on that particular validator running, nor add validation that
    // the subclass intentionally removed.
    if (owner(values).is_none()) finish_boundary(model, values);
}
} // namespace

void bind_dims_model(nb::module_& module) {
    bind_dims_namespace(module);
    auto generator_type = nb::class_<DimsValidationGenerator>(module, "DimsValidationGenerator",
            nb::dynamic_attr(), nb::is_weak_referenceable(),
            nb::type_slots(validation_generator_slots))
        .def("__iter__", [](nb::object self) { return self; })
        .def("__next__", &next_validation_generator)
        .def("send", [](nb::handle self, nb::handle value) {
            if (nb::inst_ptr<DimsValidationGenerator>(self)->stage ==
                DimsValidationGenerator::Stage::Created && !value.is_none())
                throw nb::type_error("can't send non-None value to a just-started generator");
            return next_validation_generator(self);
        }, nb::arg("value").none())
        .def("throw", &throw_validation_generator)
        .def("close", &close_validation_generator);
    auto* python_type = reinterpret_cast<PyTypeObject*>(generator_type.ptr());
    validation_generator_base_dealloc = python_type->tp_dealloc;
    python_type->tp_dealloc = &deallocate_validation_generator;
    PyType_Modified(python_type);
    module.def("dims_validation_generator", [](nb::handle model) {
        nb::object result = nb::cast(DimsValidationGenerator{});
        result.attr("_model") = model;
        result.attr("_previous") = nb::none();
        return result;
    });
    module.def("dims_field_get", [](nb::handle model, unsigned index) {
        if (index >= std::size(fields)) throw nb::index_error("unknown dimension field");
        if (model.is_none()) {
            // Pydantic subclasses inherit FieldInfo defaults. Returning a
            // descriptor here would incorrectly become a new field default.
            throw nb::attribute_error(fields[index]);
        }
        return field_value(model, static_cast<DimsField>(index), private_values(model));
    }, nb::arg("model").none(), nb::arg("index"));
    module.def("dims_field_set", [](nb::handle model, unsigned index, nb::handle value) {
        if (index >= std::size(fields)) throw nb::index_error("unknown dimension field");
        set_field(model, static_cast<DimsField>(index), value);
    }, nb::arg("model"), nb::arg("index"), nb::arg("value").none());
    module.def("dims_field_delete", [](nb::handle model, unsigned index) {
        if (index >= std::size(fields)) throw nb::index_error("unknown dimension field");
        delete_field(model, static_cast<DimsField>(index));
    });
    module.def("dims_computed", &computed_model);
    module.def("dims_range_nsteps", &range_nsteps);
    module.def("dims_set_computed", &set_computed, nb::arg("model"),
               nb::arg("name"), nb::arg("value").none());
    module.def("dims_set_on_axes", &set_on_axes, nb::arg("model"), nb::arg("name"),
               nb::arg("axis").none(), nb::arg("value").none());
    module.def("dims_execute", &execute_model, nb::arg("model"), nb::arg("name"),
               nb::arg("axis").none() = nb::none(), nb::arg("direction") = 0);
    module.def("install_dims_fields", [](nb::object type, nb::object descriptor) {
        for (unsigned index = 0; index < std::size(fields); ++index) {
            // Store only schema-routing metadata in the Python descriptor.
            // Persistent numeric state remains in the model's one C++ owner.
            nb::setattr(type, fields[index], descriptor(index));
        }
        install_dims_namespace(type);
    });
    module.def("dims_getattribute", &getattribute);
    module.def("dims_super_setattr", &super_setattr, nb::arg("model"),
               nb::arg("name"), nb::arg("value").none());
    module.def("dims_setattr", &setattr_model, nb::arg("model"),
               nb::arg("name"), nb::arg("value").none());
    module.def("dims_validate_model", &validate_model);
    module.def("dims_copy", &copy_model, nb::arg("model"), nb::arg("deep") = false,
               nb::arg("memo").none() = nb::none());
    module.def("dims_model_copy", &model_copy, nb::arg("model"), nb::arg("update").none(),
               nb::arg("deep"));
    module.def("dims_setstate", &setstate);
    module.def("dims_construct", &construct_model, nb::arg("type"),
               nb::arg("field_set").none(), nb::arg("values"));
    module.def("dims_initialize", &initialize_model);
}
} // namespace nx
