#include "dims_dictionary.hpp"

#include <nanobind/stl/shared_ptr.h>
#include <memory>
#include <unordered_map>
#include <vector>

namespace nx {
namespace {
using app::DimsField;
using app::DimsState;
using app::Index;

constexpr const char* names[] = {
    "ndim", "ndisplay", "order", "axis_labels", "rollable", "range",
    "margin_left", "margin_right", "point", "last_used"
};

struct DictionaryRecord;
using Records = std::vector<DictionaryRecord*>;
std::unordered_map<PyObject*, Records> dictionaries;
std::unordered_map<const DimsState*, DictionaryRecord*> states;
int watcher_id = -1;

struct DictionaryRecord {
    std::shared_ptr<DimsState> state;
    nb::object dictionary_owner;
    PyObject* dictionary = nullptr;  // Borrowed from the native GC-traversed handle.
    int paused = 0;
    bool watched = false;
    bool dirty = false;
    ~DictionaryRecord();
    void clear() noexcept;
};

int field_index(PyObject* key) {
    // Calling a user key's __eq__/__hash__ is forbidden in a dict watcher.
    if (!key || !PyUnicode_CheckExact(key)) return -1;
    for (unsigned index = 0; index < std::size(names); ++index)
        if (PyUnicode_CompareWithASCIIString(key, names[index]) == 0)
            return static_cast<int>(index);
    return -1;
}

bool primitive_index(PyObject* value, Index& result) {
    if (!PyLong_CheckExact(value)) return false;
    const auto converted = PyLong_AsLongLong(value);
    if (PyErr_Occurred()) { PyErr_Clear(); return false; }
    result = converted;
    return true;
}

bool decode(DimsState& state, DimsField field, PyObject* value) {
    if (field == DimsField::Ndim || field == DimsField::Ndisplay || field == DimsField::LastUsed) {
        Index result;
        if (!primitive_index(value, result)) return false;
        if (field == DimsField::Ndim) state.ndim = result;
        else if (field == DimsField::Ndisplay) state.ndisplay = result;
        else state.last_used = result;
        return true;
    }
    if (!PyTuple_CheckExact(value)) return false;
    const auto count = PyTuple_GET_SIZE(value);
    if (field == DimsField::Order) {
        std::vector<Index> result;
        result.reserve(count);
        for (Py_ssize_t index = 0; index < count; ++index) {
            Index axis;
            if (!primitive_index(PyTuple_GET_ITEM(value, index), axis)) return false;
            result.push_back(axis);
        }
        state.order = std::move(result);
    } else if (field == DimsField::AxisLabels) {
        std::vector<std::string> result;
        result.reserve(count);
        for (Py_ssize_t index = 0; index < count; ++index) {
            PyObject* label = PyTuple_GET_ITEM(value, index);
            if (!PyUnicode_CheckExact(label)) return false;
            Py_ssize_t length = 0;
            const char* text = PyUnicode_AsUTF8AndSize(label, &length);
            if (!text) { PyErr_Clear(); return false; }
            result.emplace_back(text, static_cast<size_t>(length));
        }
        state.axis_labels = std::move(result);
    } else if (field == DimsField::Rollable) {
        std::vector<bool> result;
        result.reserve(count);
        for (Py_ssize_t index = 0; index < count; ++index) {
            PyObject* flag = PyTuple_GET_ITEM(value, index);
            if (!PyBool_Check(flag)) return false;
            result.push_back(flag == Py_True);
        }
        state.rollable = std::move(result);
    } else if (field == DimsField::Ranges) {
        std::vector<app::Range> result;
        result.reserve(count);
        for (Py_ssize_t index = 0; index < count; ++index) {
            PyObject* range = PyTuple_GET_ITEM(value, index);
            if (!PyTuple_Check(range) || PyTuple_GET_SIZE(range) != 3) return false;
            app::Range coordinates;
            for (unsigned position = 0; position < 3; ++position) {
                PyObject* coordinate = PyTuple_GET_ITEM(range, position);
                if (!PyFloat_CheckExact(coordinate)) return false;
                coordinates[position] = PyFloat_AS_DOUBLE(coordinate);
            }
            result.push_back(coordinates);
        }
        state.ranges = std::move(result);
    } else {
        std::vector<double> result;
        result.reserve(count);
        for (Py_ssize_t index = 0; index < count; ++index) {
            PyObject* coordinate = PyTuple_GET_ITEM(value, index);
            if (!PyFloat_CheckExact(coordinate)) return false;
            result.push_back(PyFloat_AS_DOUBLE(coordinate));
        }
        if (field == DimsField::Point) state.point = std::move(result);
        else if (field == DimsField::MarginLeft) state.margin_left = std::move(result);
        else state.margin_right = std::move(result);
    }
    return true;
}

void changed(DictionaryRecord& record, DimsField field, PyObject* value) noexcept {
    record.state->present(field, value != nullptr);
    record.state->external(field, value != nullptr);
    if (!value) return;
    try {
        // No Python code or dictionary mutation is permitted here. Only exact
        // primitive representations are copied into independent C++ storage.
        decode(*record.state, field, value);
    } catch (...) {
        // Allocation failure cannot reject a Python dict write. Provider mode
        // remains selected; a later binding operation can retry conversion.
        record.dirty = true;
    }
}

void refresh(DictionaryRecord& record) {
    if (record.paused || !record.dictionary) return;
    for (unsigned index = 0; index < std::size(names); ++index) {
        const auto field = static_cast<DimsField>(index);
        PyObject* value = PyDict_GetItemString(record.dictionary, names[index]);
        if (PyErr_Occurred()) throw nb::python_error();
        record.state->present(field, value != nullptr);
        if (!value) record.state->external(field, false);
        else if (!decode(*record.state, field, value)) record.state->external(field, true);
    }
    record.dirty = false;
}

#if PY_VERSION_HEX >= 0x030C0000
int dictionary_changed(PyDict_WatchEvent event, PyObject* dictionary,
                       PyObject* key, PyObject* value) noexcept {
    auto found = dictionaries.find(dictionary);
    if (found == dictionaries.end()) return 0;
    if (event == PyDict_EVENT_DEALLOCATED) {
        for (auto* record : found->second) {
            record->dictionary = nullptr;
            record->watched = false;
        }
        dictionaries.erase(found);
        return 0;
    }
    PyObject *error_type = nullptr, *error_value = nullptr, *error_traceback = nullptr;
    PyErr_Fetch(&error_type, &error_value, &error_traceback);
    for (auto* record : found->second) {
        if (record->paused) continue;
        if (event == PyDict_EVENT_CLEARED) {
            for (unsigned index = 0; index < std::size(names); ++index)
                changed(*record, static_cast<DimsField>(index), nullptr);
        } else if (event == PyDict_EVENT_CLONED) {
            Py_ssize_t position = 0;
            PyObject *source_key = nullptr, *source_value = nullptr;
            while (PyDict_Next(key, &position, &source_key, &source_value)) {
                const int index = field_index(source_key);
                if (index >= 0) changed(*record, static_cast<DimsField>(index), source_value);
                else record->dirty = true;
            }
        } else {
            const int index = field_index(key);
            if (index >= 0) changed(*record, static_cast<DimsField>(index), value);
            else record->dirty = true;
        }
    }
    PyErr_Clear();
    PyErr_Restore(error_type, error_value, error_traceback);
    return 0;
}
#endif

void detach(DictionaryRecord& record) {
    if (!record.dictionary) return;
    auto found = dictionaries.find(record.dictionary);
    if (found != dictionaries.end()) {
        auto& rows = found->second;
        rows.erase(std::remove(rows.begin(), rows.end(), &record), rows.end());
        if (rows.empty()) {
#if PY_VERSION_HEX >= 0x030C0000
            if (record.watched && watcher_id >= 0) {
                // Destruction must preserve an already pending exception.
                PyObject *type = nullptr, *value = nullptr, *traceback = nullptr;
                PyErr_Fetch(&type, &value, &traceback);
                if (PyDict_Unwatch(watcher_id, record.dictionary) < 0) PyErr_Clear();
                PyErr_Restore(type, value, traceback);
            }
#endif
            dictionaries.erase(found);
        }
    }
    record.dictionary = nullptr;
    record.watched = false;
}

void DictionaryRecord::clear() noexcept {
    detach(*this);
    auto found = states.find(state.get());
    if (found != states.end() && found->second == this) states.erase(found);
    paused = 0;
    watched = false;
    dirty = true;
    // Clear persistent edges before a dictionary payload can reenter. Keep the
    // core alive until after its observer and all borrowed addresses are gone.
    auto old_state = std::move(state);
    nb::object old_dictionary = std::move(dictionary_owner);
}
DictionaryRecord::~DictionaryRecord() { clear(); }
int record_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(nb::handle(self)))
        Py_VISIT(nb::inst_ptr<DictionaryRecord>(nb::handle(self))->dictionary_owner.ptr());
    return 0;
}
int record_clear(PyObject* self) {
    if (nb::inst_ready(nb::handle(self))) nb::inst_ptr<DictionaryRecord>(nb::handle(self))->clear();
    return 0;
}
PyType_Slot record_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(record_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(record_clear)},
    {0, nullptr}
};
} // namespace

nb::object make_dims_dictionary_record(const DimsState& initial) {
    nb::object result = nb::cast(DictionaryRecord{});
    auto& record = *nb::inst_ptr<DictionaryRecord>(result);
    record.state = std::make_shared<DimsState>(initial);
    states[record.state.get()] = &record;
    return result;
}

nb::object dims_dictionary_owner(nb::handle handle) {
    return nb::cast(nb::inst_ptr<DictionaryRecord>(handle)->state);
}

void attach_dims_dictionary(nb::handle handle, nb::dict dictionary) {
    auto& record = *nb::inst_ptr<DictionaryRecord>(handle);
    if (record.dictionary == dictionary.ptr()) return;
    detach(record);
    // Releasing an old payload may run Python. Delay that until the new native
    // handle, observer and refresh flag are installed consistently.
    nb::object old_dictionary = std::move(record.dictionary_owner);
    record.dictionary_owner = dictionary;
    record.dictionary = dictionary.ptr();
    dictionaries[dictionary.ptr()].push_back(&record);
#if PY_VERSION_HEX >= 0x030C0000
    if (watcher_id < 0) {
        watcher_id = PyDict_AddWatcher(&dictionary_changed);
        if (watcher_id < 0) PyErr_Clear();
    }
    if (watcher_id >= 0 && PyDict_Watch(watcher_id, dictionary.ptr()) == 0)
        record.watched = true;
    else PyErr_Clear();
#endif
    // An already populated replacement dict emits no watcher events when it
    // is attached. Adopt its contents at the next binding boundary as well.
    record.dirty = true;
}

void pause_dims_dictionary(nb::handle handle, int depth) {
    nb::inst_ptr<DictionaryRecord>(handle)->paused = depth;
}

void detach_dims_dictionary(nb::handle handle) {
    auto& record = *nb::inst_ptr<DictionaryRecord>(handle);
    detach(record);
    nb::object previous = std::move(record.dictionary_owner);
    for (unsigned index = 0; index < std::size(names); ++index) {
        const auto field = static_cast<DimsField>(index);
        record.state->present(field, false);
        record.state->external(field, false);
    }
    record.dirty = true;
}

void refresh_dims_dictionary(nb::handle handle) {
    auto& record = *nb::inst_ptr<DictionaryRecord>(handle);
    if (!record.watched || record.dirty) refresh(record);
}

void sync_dims_dictionary_state(const DimsState& state) {
    auto found = states.find(&state);
    if (found != states.end()) refresh(*found->second);
}

void bind_dims_dictionary(nb::module_& module) {
    nb::class_<DictionaryRecord>(module, "DimsDictionaryRecord",
            nb::type_slots(record_slots), nb::is_weak_referenceable())
        .def_prop_ro("dictionary", [](const DictionaryRecord& record) -> nb::object {
            return record.dictionary_owner.is_valid() ? record.dictionary_owner : nb::none();
        })
        .def_prop_ro("watched", [](const DictionaryRecord& record) { return record.watched; })
        .def_prop_ro("paused", [](const DictionaryRecord& record) { return record.paused; });
}
} // namespace nx
