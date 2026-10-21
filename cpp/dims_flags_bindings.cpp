#include "dims_flags_bindings.hpp"

#include <nanobind/stl/shared_ptr.h>
#include <memory>
#include <unordered_map>

namespace nx {
namespace {
using app::DimsFlag;
using app::DimsFlags;
using app::DimsFlagValue;
using app::Index;

constexpr const char* names[] = {"_play_ready", "_scroll_progress", "_validating"};
struct FlagRecord;
std::unordered_map<PyObject*, FlagRecord*> dictionaries;
std::unordered_map<const DimsFlags*, FlagRecord*> states;
int watcher_id = -1;

struct FlagRecord {
    std::shared_ptr<DimsFlags> state;
    nb::object dictionary_owner;
    PyObject* dictionary = nullptr; // Borrowed from the native GC-traversed handle.
    bool watched = false, dirty = true;
    ~FlagRecord();
    void clear() noexcept;
};

int flag_index(PyObject* key) {
    if (!key || !PyUnicode_CheckExact(key)) return -1;
    for (unsigned index = 0; index < std::size(names); ++index)
        if (PyUnicode_CompareWithASCIIString(key, names[index]) == 0)
            return static_cast<int>(index);
    return -1;
}

DimsFlagValue decode(PyObject* value) {
    if (!value) {
        DimsFlagValue result; result.present = false; return result;
    }
    if (PyBool_Check(value)) return DimsFlagValue::from_bool(value == Py_True);
    if (PyLong_CheckExact(value)) {
        const auto integer = PyLong_AsLongLong(value);
        if (!PyErr_Occurred()) return DimsFlagValue::from_integer(integer);
        PyErr_Clear();
    } else if (PyFloat_CheckExact(value)) {
        return DimsFlagValue::from_real(PyFloat_AS_DOUBLE(value));
    }
    return DimsFlagValue{};
}

void refresh(FlagRecord& record) {
    if (!record.dictionary) return;
    for (unsigned index = 0; index < std::size(names); ++index) {
        PyObject* value = PyDict_GetItemString(record.dictionary, names[index]);
        if (PyErr_Occurred()) throw nb::python_error();
        record.state->values[index] = decode(value);
    }
    record.dirty = false;
}

void changed(FlagRecord& record, int index, PyObject* value) {
    if (index < 0) record.dirty = true;
    else record.state->values[static_cast<unsigned>(index)] = decode(value);
}

#if PY_VERSION_HEX >= 0x030C0000
int dictionary_changed(PyDict_WatchEvent event, PyObject* dictionary,
                       PyObject* key, PyObject* value) noexcept {
    auto found = dictionaries.find(dictionary);
    if (found == dictionaries.end()) return 0;
    auto& record = *found->second;
    if (event == PyDict_EVENT_DEALLOCATED) {
        record.dictionary = nullptr;
        record.watched = false;
        dictionaries.erase(found);
        return 0;
    }
    PyObject *type = nullptr, *error = nullptr, *traceback = nullptr;
    PyErr_Fetch(&type, &error, &traceback);
    // Only exact primitives are inspected; no Python call or dict mutation.
    if (event == PyDict_EVENT_CLEARED) {
        for (auto& flag : record.state->values) flag = decode(nullptr);
    } else if (event == PyDict_EVENT_CLONED) {
        Py_ssize_t position = 0;
        PyObject *source_key = nullptr, *source_value = nullptr;
        while (PyDict_Next(key, &position, &source_key, &source_value))
            changed(record, flag_index(source_key), source_value);
    } else changed(record, flag_index(key), value);
    PyErr_Clear();
    PyErr_Restore(type, error, traceback);
    return 0;
}
#endif

void FlagRecord::clear() noexcept {
    if (dictionary) {
        auto found = dictionaries.find(dictionary);
        if (found != dictionaries.end() && found->second == this) {
#if PY_VERSION_HEX >= 0x030C0000
            if (watched && watcher_id >= 0) {
                PyObject *type = nullptr, *error = nullptr, *traceback = nullptr;
                PyErr_Fetch(&type, &error, &traceback);
                if (PyDict_Unwatch(watcher_id, dictionary) < 0) PyErr_Clear();
                PyErr_Restore(type, error, traceback);
            }
#endif
            dictionaries.erase(found);
        }
    }
    auto found = states.find(state.get());
    if (found != states.end() && found->second == this) states.erase(found);
    dictionary = nullptr;
    watched = false;
    dirty = true;
    // Unregister all raw addresses and clear persistent edges before releasing
    // a payload that can call back into a model or another native owner.
    auto old_state = std::move(state);
    nb::object old_dictionary = std::move(dictionary_owner);
}
FlagRecord::~FlagRecord() { clear(); }
int record_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(nb::handle(self)))
        Py_VISIT(nb::inst_ptr<FlagRecord>(nb::handle(self))->dictionary_owner.ptr());
    return 0;
}
int record_clear(PyObject* self) {
    if (nb::inst_ready(nb::handle(self))) nb::inst_ptr<FlagRecord>(nb::handle(self))->clear();
    return 0;
}
PyType_Slot record_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(record_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(record_clear)},
    {0, nullptr}
};

void sync(const DimsFlags& state) {
    auto found = states.find(&state);
    if (found != states.end()) refresh(*found->second);
}

nb::object projected_value(const DimsFlags& state, DimsFlag flag) {
    sync(state);
    const auto& value = state.value(flag);
    if (!value.present) {
        PyErr_Format(PyExc_AttributeError, "Deleted application flag: %s", names[static_cast<unsigned>(flag)]);
        throw nb::python_error();
    }
    if (value.kind == DimsFlagValue::Kind::Boolean) return nb::bool_(value.boolean);
    if (value.kind == DimsFlagValue::Kind::Integer) return nb::int_(value.integer);
    if (value.kind == DimsFlagValue::Kind::Real) return nb::float_(value.real);
    throw nb::type_error("Unvalidated flag requires its Python provider");
}
} // namespace

nb::object dims_flag_record(nb::dict dictionary) {
    auto found = dictionaries.find(dictionary.ptr());
    if (found != dictionaries.end())
        return nb::cast(found->second, nb::rv_policy::reference);
    nb::object result = nb::cast(FlagRecord{});
    auto& record = *nb::inst_ptr<FlagRecord>(result);
    record.state = std::make_shared<DimsFlags>();
    record.dictionary_owner = dictionary;
    record.dictionary = dictionary.ptr();
    dictionaries[dictionary.ptr()] = &record;
    states[record.state.get()] = &record;
#if PY_VERSION_HEX >= 0x030C0000
    if (watcher_id < 0) {
        watcher_id = PyDict_AddWatcher(&dictionary_changed);
        if (watcher_id < 0) PyErr_Clear();
    }
    if (watcher_id >= 0 && PyDict_Watch(watcher_id, dictionary.ptr()) == 0)
        record.watched = true;
    else PyErr_Clear();
#endif
    refresh(record);
    return result;
}

nb::object dims_flag_owner(nb::handle handle) {
    return nb::cast(nb::inst_ptr<FlagRecord>(handle)->state);
}

void refresh_dims_flag_record(nb::handle handle) {
    auto& record = *nb::inst_ptr<FlagRecord>(handle);
    if (record.dirty || !record.watched) refresh(record);
}

int dims_flag_truth(nb::handle value) {
    auto native = decode(value.ptr()).truth();
    return native ? static_cast<int>(*native) : PyObject_IsTrue(value.ptr());
}

void bind_dims_flags(nb::module_& module) {
    nb::class_<FlagRecord>(module, "DimsFlagRecord", nb::type_slots(record_slots), nb::is_weak_referenceable())
        .def_prop_ro("dictionary", [](const FlagRecord& record) -> nb::object {
            return record.dictionary_owner.is_valid() ? record.dictionary_owner : nb::none();
        })
        .def_prop_ro("watched", [](const FlagRecord& record) { return record.watched; });
    nb::class_<DimsFlags>(module, "DimsFlags", nb::is_weak_referenceable())
        .def_prop_ro("play_ready", [](const DimsFlags& state) { return projected_value(state, DimsFlag::PlayReady); })
        .def_prop_ro("scroll_progress", [](const DimsFlags& state) { return projected_value(state, DimsFlag::ScrollProgress); })
        .def_prop_ro("validating", [](const DimsFlags& state) { return projected_value(state, DimsFlag::Validating); })
        .def("__copy__", [](const DimsFlags& state) { sync(state); return DimsFlags(state); })
        .def("__deepcopy__", [](const DimsFlags& state, nb::dict) { sync(state); return DimsFlags(state); });
}
} // namespace nx
