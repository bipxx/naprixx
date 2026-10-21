#include "component_state.hpp"

#include <nanobind/stl/array.h>
#include <nanobind/stl/string.h>
#include <algorithm>
#include <exception>
#include <new>
#include <unordered_map>
#include <vector>

namespace nx {
namespace {
constexpr const char* camera_names[] = {
    "center", "zoom", "angles", "perspective", "mouse_pan", "mouse_zoom", "orientation", "synced"
};
constexpr const char* grid_names[] = {"stride", "shape", "enabled", "spacing"};

struct StateRecord {
    enum class Kind { Camera, Grid } kind = Kind::Camera;
    void* state = nullptr;
    // Strong adapter references are native fields traversed explicitly by GC.
    // Read-only properties expose them without allowing the raw state address
    // to outlive its owner or replacing a borrowed dictionary/weakref behind us.
    nb::object owner, dictionary_owner, model_ref_owner;
    PyObject* dictionary = nullptr;
    PyObject* model_ref = nullptr;
    unsigned known = 0;
    int paused = 0;
    bool watched = false, dirty = true;
    ~StateRecord();
    void clear() noexcept;
    unsigned count() const { return kind == Kind::Camera ? 8u : 4u; }
    const char* name(unsigned index) const { return kind == Kind::Camera ? camera_names[index] : grid_names[index]; }
};
std::unordered_map<const void*, StateRecord*> states;
std::unordered_map<PyObject*, std::vector<StateRecord*>> dictionaries;
int watcher_id = -1;

bool active(const StateRecord& record) noexcept {
    if (!record.state) return false;
    const auto found = states.find(record.state);
    return found != states.end() && found->second == &record;
}
nb::object registered(const void* address) {
    const auto found = states.find(address);
    return found == states.end() ? nb::none()
        : nb::borrow<nb::object>(nb::find(found->second));
}
void release_pause(const void* address, PyObject* owner, bool dirty = false) noexcept {
    const auto found = states.find(address);
    if (found == states.end() || found->second->owner.ptr() != owner) return;
    auto& record = *found->second;
    if (record.paused) --record.paused;
    if (dirty) record.dirty = true;
}

nb::object checked(PyObject* value) {
    if (!value) throw nb::python_error();
    return nb::steal<nb::object>(value);
}
nb::object generic(nb::handle model, const char* name) {
    return checked(PyObject_GenericGetAttr(model.ptr(), nb::str(name).ptr()));
}
bool truth(nb::handle value) {
    const int result = PyObject_IsTrue(value.ptr());
    if (result < 0) throw nb::python_error();
    return result != 0;
}

bool integer(PyObject* value, app::Index& result) {
    if (!PyLong_CheckExact(value)) return false;
    result = PyLong_AsLongLong(value);
    if (!PyErr_Occurred()) return true;
    PyErr_Clear();
    return false;
}
bool grid_integer(PyObject* value, app::Index& result, std::string& wide) {
    if (!PyLong_CheckExact(value)) return false;
    int overflow = 0;
    const auto number = PyLong_AsLongLongAndOverflow(value, &overflow);
    if (PyErr_Occurred()) throw nb::python_error();
    if (!overflow) { result = number; wide.clear(); return true; }
    // Base 16 has no interpreter decimal-digit limit and exact PyLong values
    // cannot dispatch a plugin override in a dictionary watcher callback.
    nb::object representation = checked(PyNumber_ToBase(value, 16));
    const char* text = PyUnicode_AsUTF8(representation.ptr());
    if (!text) throw nb::python_error();
    wide = text;
    result = overflow < 0 ? -1 : 1;
    return true;
}
bool grid_shape(PyObject* value, app::GridState& state) {
    if (!PyTuple_CheckExact(value) || PyTuple_GET_SIZE(value) != 2) return false;
    app::Cell shape;
    std::array<std::string, 2> wide;
    for (unsigned index = 0; index < 2; ++index)
        if (!grid_integer(PyTuple_GET_ITEM(value, index), shape[index], wide[index])) return false;
    state.shape = shape; state.wide_shape = std::move(wide);
    return true;
}
nb::object grid_value(app::Index value, const std::string& wide) {
    if (wide.empty()) return nb::int_(value);
    return checked(PyLong_FromString(wide.c_str(), nullptr, 0));
}
bool real(PyObject* value, double& result) {
    if (PyFloat_CheckExact(value)) { result = PyFloat_AS_DOUBLE(value); return true; }
    if (!PyLong_CheckExact(value)) return false;
    result = PyLong_AsDouble(value);
    if (!PyErr_Occurred()) return true;
    PyErr_Clear();
    return false;
}
template<class Array, class Decode>
bool sequence(PyObject* value, Array& result, Decode decode) {
    if (!PyTuple_CheckExact(value) || PyTuple_GET_SIZE(value) != static_cast<Py_ssize_t>(result.size())) return false;
    Array decoded;
    for (size_t index = 0; index < decoded.size(); ++index)
        if (!decode(PyTuple_GET_ITEM(value, index), decoded[index])) return false;
    result = std::move(decoded);
    return true;
}
bool text(PyObject* value, std::string& result) {
    // str enums have an ordinary unicode payload; this does not call __str__.
    if (!PyUnicode_Check(value)) return false;
    Py_ssize_t size;
    const char* bytes = PyUnicode_AsUTF8AndSize(value, &size);
    if (!bytes) { PyErr_Clear(); return false; }
    result.assign(bytes, static_cast<size_t>(size));
    return true;
}
bool decode(app::CameraState& state, unsigned index, PyObject* value) {
    if (!value) return false;
    switch (index) {
    case 0: return sequence(value, state.center, real);
    case 1: return real(value, state.zoom);
    case 2: return sequence(value, state.angles, real);
    case 3: return real(value, state.perspective);
    case 4: if (!PyBool_Check(value)) return false; state.mouse_pan = value == Py_True; return true;
    case 5: if (!PyBool_Check(value)) return false; state.mouse_zoom = value == Py_True; return true;
    case 6: return sequence(value, state.orientation, text);
    default: if (!PyBool_Check(value)) return false; state.synced = value == Py_True; return true;
    }
}
bool decode(app::GridState& state, unsigned index, PyObject* value) {
    if (!value) return false;
    switch (index) {
    case 0: return grid_integer(value, state.stride, state.wide_stride);
    case 1: return grid_shape(value, state);
    case 2: if (!PyBool_Check(value)) return false; state.enabled = value == Py_True; return true;
    default: return real(value, state.spacing);
    }
}
void changed(StateRecord& record, unsigned index, PyObject* value) noexcept {
    if (!active(record)) return;
    const unsigned bit = 1u << index;
    record.known &= ~bit;
    if (record.paused) { record.dirty = true; return; }
    try {
        const bool valid = record.kind == StateRecord::Kind::Camera
            ? decode(*static_cast<app::CameraState*>(record.state), index, value)
            : decode(*static_cast<app::GridState*>(record.state), index, value);
        if (valid) record.known |= bit;
    } catch (...) { record.dirty = true; }
}
void refresh(StateRecord& record) {
    if (!active(record) || record.paused || !record.dictionary) return;
    for (unsigned index = 0; index < record.count(); ++index) {
        PyObject* value = PyDict_GetItemString(record.dictionary, record.name(index));
        if (PyErr_Occurred()) throw nb::python_error();
        if (!active(record)) return;
        changed(record, index, value);
    }
    if (active(record)) record.dirty = false;
}

#if PY_VERSION_HEX >= 0x030C0000
int dictionary_changed(PyDict_WatchEvent event, PyObject* dictionary, PyObject* key, PyObject* value) noexcept {
    auto found = dictionaries.find(dictionary);
    if (found == dictionaries.end()) return 0;
    if (event == PyDict_EVENT_DEALLOCATED) {
        for (auto* record : found->second) { record->dictionary = nullptr; record->watched = false; }
        dictionaries.erase(found);
        return 0;
    }
    PyObject *type = nullptr, *error = nullptr, *traceback = nullptr;
    PyErr_Fetch(&type, &error, &traceback);
    // Keep records alive without carrying a registry iterator across a decode.
    // Watch callbacks are noexcept, including failure to allocate this snapshot.
    std::vector<nb::object> records;
    try {
        for (auto* record : found->second)
            records.push_back(nb::borrow<nb::object>(nb::find(record)));
    } catch (...) {
        // A failed observer snapshot must fall back to a later full refresh.
        // Otherwise a watched record could keep stale canonical values.
        auto current = dictionaries.find(dictionary);
        if (current != dictionaries.end())
            for (auto* record : current->second) {
                record->known = 0;
                record->dirty = true;
            }
        PyErr_Clear();
        PyErr_Restore(type, error, traceback);
        return 0;
    }
    for (const auto& handle : records) {
        auto* record = nb::inst_ptr<StateRecord>(handle);
        if (!active(*record)) continue;
        if (event == PyDict_EVENT_CLEARED) { record->known = 0; record->dirty = true; continue; }
        if (event == PyDict_EVENT_CLONED || !key || !PyUnicode_CheckExact(key)) { record->dirty = true; continue; }
        bool matched = false;
        for (unsigned index = 0; index < record->count(); ++index) {
            if (PyUnicode_CompareWithASCIIString(key, record->name(index)) != 0) continue;
            changed(*record, index, value);
            matched = true;
            break;
        }
        if (!matched) record->dirty = true;
    }
    PyErr_Clear();
    PyErr_Restore(type, error, traceback);
    return 0;
}
#endif

void detach(StateRecord& record) noexcept {
    if (!record.dictionary) return;
    auto found = dictionaries.find(record.dictionary);
    if (found != dictionaries.end()) {
        auto& rows = found->second;
        rows.erase(std::remove(rows.begin(), rows.end(), &record), rows.end());
        if (rows.empty()) {
#if PY_VERSION_HEX >= 0x030C0000
            if (record.watched && watcher_id >= 0) {
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
void StateRecord::clear() noexcept {
    detach(*this);
    auto found = states.find(state);
    if (found != states.end() && found->second == this) states.erase(found);
    state = nullptr;
    model_ref = nullptr;
    known = 0;
    dirty = true;
    // Null all persistent handles before a provider finalizer can reenter.
    // Release the native owner last, after unregistering the watcher/maps.
    nb::object old_owner = std::move(owner);
    nb::object old_dictionary = std::move(dictionary_owner);
    nb::object old_model_ref = std::move(model_ref_owner);
}
StateRecord::~StateRecord() { clear(); }
int record_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (!nb::inst_ready(nb::handle(self))) return 0;
    auto* record = nb::inst_ptr<StateRecord>(nb::handle(self));
    Py_VISIT(record->owner.ptr());
    Py_VISIT(record->dictionary_owner.ptr());
    Py_VISIT(record->model_ref_owner.ptr());
    return 0;
}
int record_clear(PyObject* self) {
    if (nb::inst_ready(nb::handle(self))) nb::inst_ptr<StateRecord>(nb::handle(self))->clear();
    return 0;
}
PyType_Slot record_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(record_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(record_clear)},
    {0, nullptr}
};
void adopt(StateRecord& record, nb::dict dictionary) {
    if (dictionary.ptr() == record.dictionary) return;
    detach(record);
    nb::object old_dictionary = std::move(record.dictionary_owner);
    record.dictionary = dictionary.ptr();
    record.dictionary_owner = std::move(dictionary);
    dictionaries[record.dictionary].push_back(&record);
#if PY_VERSION_HEX >= 0x030C0000
    if (watcher_id < 0) {
        watcher_id = PyDict_AddWatcher(&dictionary_changed);
        if (watcher_id < 0) PyErr_Clear();
    }
    if (watcher_id >= 0 && PyDict_Watch(watcher_id, record.dictionary) == 0) record.watched = true;
    else PyErr_Clear();
#endif
    record.dirty = true;
}

// nanobind dynamic_attr stores this implementation relationship in the real
// instance dictionary. GenericGetDict deliberately bypasses subclass attribute
// hooks; no parallel Python namespace or setter is an authority for the record.
nb::object native_attributes(nb::handle owner) {
    return checked(PyObject_GenericGetDict(owner.ptr(), nullptr));
}
struct CameraPublication {
    nb::object previous, next, attributes, previous_backlink;
    const void* address = nullptr;
    PyObject** attribute_pointer = nullptr;
};
CameraPublication stage_camera_record(nb::handle handle, PyObject* dictionary,
                                      nb::handle key) {
    CameraPublication staged;
    staged.previous = nb::borrow<nb::object>(handle);
    auto& previous = *nb::inst_ptr<StateRecord>(staged.previous);
    if (!active(previous)) return staged;
    staged.next = nb::cast(StateRecord{});
    if (!active(previous)) return staged;
    auto& next = *nb::inst_ptr<StateRecord>(staged.next);
    next.kind = previous.kind;
    next.owner = previous.owner;
    next.model_ref_owner = previous.model_ref_owner;
    next.model_ref = next.model_ref_owner.ptr();
    // Watcher rows can be allocated here without granting authority: state is
    // NULL until commit, and every observer checks registry identity.
    if (dictionary) adopt(next, nb::borrow<nb::dict>(dictionary));
    staged.attributes = native_attributes(next.owner);
    staged.attribute_pointer = _PyObject_GetDictPtr(next.owner.ptr());
    if (!staged.attribute_pointer) {
        PyErr_SetString(PyExc_AttributeError, "Native component state has no __dict__");
        throw nb::python_error();
    }
    PyObject* backlink = PyDict_GetItemWithError(staged.attributes.ptr(), key.ptr());
    if (PyErr_Occurred()) throw nb::python_error();
    if (backlink) staged.previous_backlink = nb::borrow<nb::object>(backlink);
    // Reserve an absent key before physical publication. Existing-key updates
    // use the prepared exact key and cannot resize a standard attribute dict.
    if (backlink != handle.ptr() &&
        PyDict_SetItem(staged.attributes.ptr(), key.ptr(), handle.ptr()) < 0)
        throw nb::python_error();
    if (!active(previous)) return staged;
    staged.address = previous.state;
    return staged;
}
bool staged_current(const CameraPublication& staged, nb::handle key) {
    if (!staged.address || !staged.next.is_valid()) return false;
    auto& previous = *nb::inst_ptr<StateRecord>(staged.previous);
    if (!active(previous) || !staged.attribute_pointer ||
        *staged.attribute_pointer != staged.attributes.ptr()) return false;
    PyObject* backlink = PyDict_GetItemWithError(staged.attributes.ptr(), key.ptr());
    if (PyErr_Occurred()) throw nb::python_error();
    return backlink == staged.previous.ptr() && active(previous);
}
void activate_camera_record(CameraPublication& staged) noexcept {
    auto& previous = *nb::inst_ptr<StateRecord>(staged.previous);
    auto& next = *nb::inst_ptr<StateRecord>(staged.next);
    next.state = previous.state;
    next.paused = previous.paused;
    // This key was already allocated for the predecessor. No node allocation
    // occurs while making the successor authoritative.
    states.find(staged.address)->second = &next;
}
void retire_camera_record(CameraPublication& staged) noexcept {
    auto& previous = *nb::inst_ptr<StateRecord>(staged.previous);
    detach(previous);
    previous.state = nullptr;
    previous.paused = 0;
    previous.known = 0;
    previous.dirty = true;
}
void restore_camera_record(CameraPublication& staged, nb::handle key,
                           nb::python_error& error) noexcept {
    auto& previous = *nb::inst_ptr<StateRecord>(staged.previous);
    auto& next = *nb::inst_ptr<StateRecord>(staged.next);
    const auto found = states.find(staged.address);
    if (found == states.end() || found->second != &next) return;
    // Keep predecessor watcher rows until all backlink writes succeed so this
    // error rollback needs neither a registry allocation nor a new watcher.
    found->second = &previous;
    next.state = nullptr;
    next.paused = 0;
    if (PyDict_SetItem(staged.attributes.ptr(), key.ptr(), staged.previous.ptr()) < 0) {
        // A malformed externally replaced native namespace can make even an
        // existing-key rollback fail. Preserve that exception as context rather
        // than hiding it or replacing the original publication error.
        nb::python_error rollback;
        PyObject* context = PyException_GetContext(error.value().ptr());
        if (context) PyException_SetContext(rollback.value().ptr(), context);
        PyException_SetContext(error.value().ptr(), rollback.value().inc_ref().ptr());
    }
}
nb::object rotate_camera_record(StateRecord& previous, PyObject* dictionary) {
    if (!active(previous)) return nb::none();
    nb::str key("_model_dictionary_record");
    CameraPublication staged = stage_camera_record(nb::find(&previous), dictionary, key);
    if (!staged_current(staged, key)) return nb::none();
    activate_camera_record(staged);
    if (PyDict_SetItem(staged.attributes.ptr(), key.ptr(), staged.next.ptr()) < 0) {
        nb::python_error error;
        restore_camera_record(staged, key, error);
        throw error;
    }
    retire_camera_record(staged);
    return staged.next;
}

int set_component_fields(PyObject* model, PyObject* value, StateRecord::Kind kind) {
    if (value && !PyDict_Check(value)) {
        PyErr_Format(PyExc_TypeError, "__dict__ must be set to a dictionary, not a '%.200s'",
                     Py_TYPE(value)->tp_name);
        return -1;
    }
    try {
        PyObject** pointer = _PyObject_GetDictPtr(model);
        if (!pointer) {
            if (!PyErr_Occurred())
                PyErr_SetString(PyExc_AttributeError, "This object has no __dict__");
            return -1;
        }
        if (*pointer == value) return 0;
        // Pin the old physical map and every predecessor before any allocation
        // can run GC. The requested map is retained by staged successor edges.
        nb::object old_physical = nb::borrow<nb::object>(*pointer);
        std::vector<nb::object> records;
        for (const auto& entry : states) {
            auto* record = entry.second;
            if (active(*record) && record->kind == kind && record->model_ref &&
                PyWeakref_GetObject(record->model_ref) == model)
                records.push_back(nb::borrow<nb::object>(nb::find(record)));
        }
        nb::str key("_model_dictionary_record");
        std::vector<CameraPublication> staged;
        staged.reserve(records.size());
        for (const auto& handle : records) {
            staged.push_back(stage_camera_record(handle, value, key));
            if (*pointer != old_physical.ptr()) return 0;
        }
        for (const auto& publication : staged)
            if (!staged_current(publication, key)) return 0;
        if (*pointer != old_physical.ptr()) return 0;
        nb::object previous = nb::steal<nb::object>(*pointer);
        *pointer = Py_XNewRef(value);
        for (auto& publication : staged) activate_camera_record(publication);
        try {
            for (auto& publication : staged)
                if (PyDict_SetItem(publication.attributes.ptr(), key.ptr(), publication.next.ptr()) < 0)
                    throw nb::python_error();
        } catch (nb::python_error& error) {
            // Ordinary allocation failures occurred during staging. Preserve a
            // remaining CAPI exception and restore the last complete graph.
            if (*pointer == value) {
                nb::object rejected = nb::steal<nb::object>(*pointer);
                *pointer = previous.release().ptr();
            }
            for (auto& publication : staged) restore_camera_record(publication, key, error);
            throw;
        }
        for (auto& publication : staged) retire_camera_record(publication);
        // Drop obsolete internal record aliases while the old physical map is
        // still pinned. Its finalizers see the fully published new namespace.
        records.clear();
        staged.clear();
        return 0;
    } catch (nb::python_error& error) {
        error.restore();
    } catch (const std::bad_alloc&) {
        PyErr_NoMemory();
    } catch (const std::exception& error) {
        PyErr_SetString(PyExc_RuntimeError, error.what());
    }
    return -1;
}
int set_camera_fields(PyObject* model, PyObject* value, void*) {
    return set_component_fields(model, value, StateRecord::Kind::Camera);
}
int set_grid_fields(PyObject* model, PyObject* value, void*) {
    return set_component_fields(model, value, StateRecord::Kind::Grid);
}
PyGetSetDef camera_namespace_getset = {
    const_cast<char*>("__dict__"), PyObject_GenericGetDict,
    set_camera_fields, nullptr, nullptr
};
PyGetSetDef grid_namespace_getset = {
    const_cast<char*>("__dict__"), PyObject_GenericGetDict,
    set_grid_fields, nullptr, nullptr
};

nb::object link(nb::handle model, nb::handle state) {
    // attach_component_state can pass a borrowed private-map entry. Allocating
    // the GC-tracked record may run a finalizer that removes that entry.
    nb::object model_pin = nb::borrow<nb::object>(model);
    nb::object state_pin = nb::borrow<nb::object>(state);
    void* address = nullptr;
    auto kind = StateRecord::Kind::Camera;
    if (nb::isinstance<app::CameraState>(state)) address = nb::inst_ptr<app::CameraState>(state);
    else if (nb::isinstance<app::GridState>(state)) { address = nb::inst_ptr<app::GridState>(state); kind = StateRecord::Kind::Grid; }
    else return nb::none();
    nb::object handle = registered(address);
    const bool created = handle.is_none();
    if (created) {
        handle = nb::cast(StateRecord{});
        auto* record = nb::inst_ptr<StateRecord>(handle);
        record->kind = kind;
        record->owner = nb::borrow<nb::object>(state);
        {
            nb::object attributes = native_attributes(state);
            nb::str key("_model_dictionary_record");
            PyObject* previous = PyDict_GetItemWithError(attributes.ptr(), key.ptr());
            if (PyErr_Occurred()) throw nb::python_error();
            nb::object previous_backlink = nb::borrow<nb::object>(previous);
            // Allocate the registry node before publishing a new relationship.
            // Subclass setattr/data-descriptor hooks do not own this native edge.
            states[address] = record;
            record->state = address;
            if (PyDict_SetItem(attributes.ptr(), key.ptr(), handle.ptr()) < 0) {
                nb::python_error error;
                const auto found = states.find(address);
                if (found != states.end() && found->second == record) states.erase(found);
                record->state = nullptr;
                throw error;
            }
        }
    }
    auto& record = *nb::inst_ptr<StateRecord>(handle);
    if (!active(record)) return registered(address);
    nb::object weak = checked(PyWeakref_NewRef(model.ptr(), nullptr));
    if (!active(record)) return registered(address);
    record.model_ref = weak.ptr();
    record.model_ref_owner = std::move(weak);
    nb::dict dictionary = nb::cast<nb::dict>(generic(model, "__dict__"));
    if (!active(record)) return registered(address);
    if (!created && record.dictionary != dictionary.ptr())
        handle = rotate_camera_record(record, dictionary.ptr());
    else adopt(record, std::move(dictionary));
    if (handle.is_none()) return registered(address);
    auto& current = *nb::inst_ptr<StateRecord>(handle);
    if (active(current) && (!current.watched || current.dirty)) refresh(current);
    return active(current) ? handle : registered(address);
}
bool prepare(StateRecord& record) {
    if (!active(record)) return false;
    if (record.paused) return true;
    if (record.model_ref) {
        PyObject* model = PyWeakref_GetObject(record.model_ref);
        if (model && model != Py_None) {
            nb::object model_owner = nb::borrow<nb::object>(model);
            nb::dict dictionary = nb::cast<nb::dict>(generic(model_owner, "__dict__"));
            if (!active(record)) return false;
            if (record.dictionary != dictionary.ptr()) {
                rotate_camera_record(record, dictionary.ptr());
                return false;
            }
            adopt(record, std::move(dictionary));
            if (!active(record)) return false;
        }
    }
    if (!record.watched || record.dirty) refresh(record);
    return active(record);
}

void assign_field(app::CameraState& state, app::CameraState&& candidate, unsigned index) {
    switch (index) {
    case 0: state.center = candidate.center; break;
    case 1: state.zoom = candidate.zoom; break;
    case 2: state.angles = candidate.angles; break;
    case 3: state.perspective = candidate.perspective; break;
    case 4: state.mouse_pan = candidate.mouse_pan; break;
    case 5: state.mouse_zoom = candidate.mouse_zoom; break;
    case 6: state.orientation = std::move(candidate.orientation); break;
    default: state.synced = candidate.synced; break;
    }
}
void assign_field(app::GridState& state, app::GridState&& candidate, unsigned index) {
    switch (index) {
    case 0: state.stride = candidate.stride; state.wide_stride = std::move(candidate.wide_stride); break;
    case 1: state.shape = candidate.shape; state.wide_shape = std::move(candidate.wide_shape); break;
    case 2: state.enabled = candidate.enabled; break;
    default: state.spacing = candidate.spacing; break;
    }
}
bool require(StateRecord& record, unsigned required) {
    if (!prepare(record)) return false;
    if (record.paused || (record.known & required) == required) return true;
    for (unsigned index = 0; index < record.count(); ++index) {
        const unsigned bit = 1u << index;
        if (!(required & bit) || (record.known & bit)) continue;
        PyObject* raw = record.dictionary ? PyDict_GetItemString(record.dictionary, record.name(index)) : nullptr;
        if (!active(record)) return false;
        if (!raw) {
            if (PyErr_Occurred()) throw nb::python_error();
            PyErr_Format(PyExc_AttributeError, "'%s' object has no attribute '%s'",
                record.kind == StateRecord::Kind::Camera ? "Camera" : "GridCanvas", record.name(index));
            throw nb::python_error();
        }
        // Provider conversion can publish a successor and release its input
        // map. Keep the input alive and never commit its result to a successor.
        nb::object value = nb::borrow<nb::object>(raw);
        if (record.kind == StateRecord::Kind::Camera) {
            app::CameraState state = *static_cast<app::CameraState*>(record.state);
            switch (index) {
            case 0: state.center = nb::cast<app::Vec3>(value); break;
            case 1: state.zoom = nb::cast<double>(value); break;
            case 2: state.angles = nb::cast<app::Vec3>(value); break;
            case 3: state.perspective = nb::cast<double>(value); break;
            case 4: state.mouse_pan = truth(value); break;
            case 5: state.mouse_zoom = truth(value); break;
            case 6: state.orientation = nb::cast<std::array<std::string, 3>>(value); break;
            default: state.synced = truth(value); break;
            }
            if (!active(record)) return false;
            assign_field(*static_cast<app::CameraState*>(record.state), std::move(state), index);
        } else {
            app::GridState state = *static_cast<app::GridState*>(record.state);
            switch (index) {
            case 0: set_grid_stride(state, value); break;
            case 1: set_grid_shape(state, value); break;
            case 2: state.enabled = truth(value); break;
            default: state.spacing = nb::cast<double>(value); break;
            }
            if (!active(record)) return false;
            assign_field(*static_cast<app::GridState*>(record.state), std::move(state), index);
        }
    }
    return active(record);
}
template<class State>
void sync(const State& state, unsigned required) {
    for (;;) {
        nb::object handle = registered(&state);
        if (handle.is_none()) return;
        if (require(*nb::inst_ptr<StateRecord>(handle), required)) return;
        // Releasing this retired record may run finalizers. Reacquire only
        // after that release, using the canonical registry rather than a raw
        // pointer retained across a provider callback.
    }
}

template<class Array>
nb::tuple tuple(const Array& value) {
    nb::tuple result = nb::steal<nb::tuple>(PyTuple_New(static_cast<Py_ssize_t>(value.size())));
    if (!result.is_valid()) throw nb::python_error();
    for (size_t index = 0; index < value.size(); ++index) {
        nb::object item = nb::cast(value[index]);
        PyTuple_SET_ITEM(result.ptr(), static_cast<Py_ssize_t>(index), item.release().ptr());
    }
    return result;
}
nb::object encoded(const app::CameraState& state, unsigned index) {
    switch (index) {
    case 0: return tuple(state.center);
    case 1: return nb::float_(state.zoom);
    case 2: return tuple(state.angles);
    case 3: return nb::float_(state.perspective);
    case 4: return nb::bool_(state.mouse_pan);
    case 5: return nb::bool_(state.mouse_zoom);
    case 6: {
        nb::module_ enums = nb::module_::import_("napari.utils.camera_orientations");
        return nb::make_tuple(enums.attr("DepthAxisOrientation")(state.orientation[0]),
            enums.attr("VerticalAxisOrientation")(state.orientation[1]),
            enums.attr("HorizontalAxisOrientation")(state.orientation[2]));
    }
    default: return nb::bool_(state.synced);
    }
}
nb::object encoded(const app::GridState& state, unsigned index) {
    switch (index) {
    case 0: return grid_stride_value(state);
    case 1: return grid_shape_value(state);
    case 2: return nb::bool_(state.enabled);
    default: return nb::float_(state.spacing);
    }
}
bool equal_field(const app::CameraState& a, const app::CameraState& b, unsigned index) {
    switch (index) {
    case 0: return a.center == b.center;
    case 1: return a.zoom == b.zoom;
    case 2: return a.angles == b.angles;
    case 3: return a.perspective == b.perspective;
    case 4: return a.mouse_pan == b.mouse_pan;
    case 5: return a.mouse_zoom == b.mouse_zoom;
    case 6: return a.orientation == b.orientation;
    default: return a.synced == b.synced;
    }
}
bool equal_field(const app::GridState& a, const app::GridState& b, unsigned index) {
    switch (index) {
    case 0: return a.stride == b.stride && a.wide_stride == b.wide_stride;
    case 1: return a.shape == b.shape && a.wide_shape == b.wide_shape;
    case 2: return a.enabled == b.enabled;
    default: return a.spacing == b.spacing;
    }
}
template<class State>
void publish(State& state) {
    nb::object handle;
    // Adopt a replaced dictionary without overwriting this native transition.
    for (;;) {
        handle = registered(&state);
        if (handle.is_none()) return;
        auto& current = *nb::inst_ptr<StateRecord>(handle);
        if (current.paused) return;
        if (current.model_ref) {
            PyObject* model = PyWeakref_GetObject(current.model_ref);
            if (model && model != Py_None) {
                nb::object model_owner = nb::borrow<nb::object>(model);
                nb::dict dictionary = nb::cast<nb::dict>(generic(model_owner, "__dict__"));
                if (!active(current)) continue;
                if (current.dictionary != dictionary.ptr()) {
                    rotate_camera_record(current, dictionary.ptr());
                    continue;
                }
                adopt(current, std::move(dictionary));
                if (!active(current)) continue;
            }
        }
        break;
    }
    auto& record = *nb::inst_ptr<StateRecord>(handle);
    if (!record.dictionary) return;
    nb::object owner = record.owner;
    ++record.paused;
    try {
        for (unsigned index = 0; index < record.count(); ++index) {
            if (!active(record)) break;
            PyObject* raw = PyDict_GetItemString(record.dictionary, record.name(index));
            if (PyErr_Occurred()) throw nb::python_error();
            if (!active(record)) break;
            nb::object input = raw ? nb::borrow<nb::object>(raw) : nb::none();
            State current = state;
            if (decode(current, index, raw) && equal_field(current, state, index)) continue;
            nb::object value = encoded(state, index);
            if (!active(record)) break;
            if (PyDict_SetItemString(record.dictionary, record.name(index), value.ptr()) < 0) throw nb::python_error();
        }
    } catch (...) {
        release_pause(&state, owner.ptr(), true);
        throw;
    }
    release_pause(&state, owner.ptr());
    if (active(record)) {
        record.known = (1u << record.count()) - 1;
        record.dirty = false;
    }
}
} // namespace

void set_grid_stride(app::GridState& state, nb::handle value) {
    if (!grid_integer(value.ptr(), state.stride, state.wide_stride)) {
        state.stride = nb::cast<app::Index>(value);
        state.wide_stride.clear();
    }
}
void set_grid_shape(app::GridState& state, nb::handle value) {
    if (!grid_shape(value.ptr(), state)) {
        state.shape = nb::cast<app::Cell>(value);
        state.wide_shape = {};
    }
}
nb::object grid_stride_value(const app::GridState& state) {
    return grid_value(state.stride, state.wide_stride);
}
nb::tuple grid_shape_value(const app::GridState& state) {
    return nb::make_tuple(grid_value(state.shape[0], state.wide_shape[0]),
                          grid_value(state.shape[1], state.wide_shape[1]));
}
void attach_component_state(nb::handle model) {
    PyObject* raw = PyObject_GenericGetAttr(model.ptr(), nb::str("__pydantic_private__").ptr());
    if (!raw) {
        if (PyErr_ExceptionMatches(PyExc_AttributeError)) { PyErr_Clear(); return; }
        throw nb::python_error();
    }
    nb::object storage = nb::steal<nb::object>(raw);
    if (!PyDict_Check(storage.ptr())) return;
    PyObject* state = PyDict_GetItemString(storage.ptr(), "_native_state");
    if (state) link(model, nb::handle(state));
}
void refresh_component_model(nb::handle model) {
    // Existing records identify real owners without probing private attributes
    // on duck-typed provider objects. Retain the records before any descriptor
    // can reenter and mutate the registries during dictionary adoption.
    std::vector<nb::object> records;
    for (const auto& entry : states) {
        auto* record = entry.second;
        if (record->model_ref && PyWeakref_GetObject(record->model_ref) == model.ptr())
            records.push_back(nb::borrow<nb::object>(nb::find(record)));
    }
    for (const auto& record : records) prepare(*nb::inst_ptr<StateRecord>(record));
}
void sync_component_state(const app::CameraState& state, unsigned required) { sync(state, required); }
void sync_component_state(const app::GridState& state, unsigned required) { sync(state, required); }
void publish_component_state(app::CameraState& state) { publish(state); }
void publish_component_state(app::GridState& state) { publish(state); }
ComponentSyncGuard::ComponentSyncGuard(nb::handle model, nb::handle state) {
    nb::object handle = link(model, state);
    if (!handle.is_none()) {
        auto& record = *nb::inst_ptr<StateRecord>(handle);
        if (active(record)) {
            owner_ = record.owner;
            address_ = record.state;
            ++record.paused;
        }
    }
}
ComponentSyncGuard::~ComponentSyncGuard() {
    if (address_) release_pause(address_, owner_.ptr());
}
void install_camera_namespace(nb::handle type) {
    auto* model_type = reinterpret_cast<PyTypeObject*>(type.ptr());
    nb::object descriptor = checked(PyDescr_NewGetSet(model_type, &camera_namespace_getset));
    if (PyDict_SetItemString(model_type->tp_dict, "__dict__", descriptor.ptr()) < 0)
        throw nb::python_error();
    PyType_Modified(model_type);
}
void install_grid_namespace(nb::handle type) {
    auto* model_type = reinterpret_cast<PyTypeObject*>(type.ptr());
    nb::object descriptor = checked(PyDescr_NewGetSet(model_type, &grid_namespace_getset));
    if (PyDict_SetItemString(model_type->tp_dict, "__dict__", descriptor.ptr()) < 0)
        throw nb::python_error();
    PyType_Modified(model_type);
}
void bind_component_state(nb::module_& module) {
    nb::class_<StateRecord>(module, "ComponentStateRecord", nb::type_slots(record_slots), nb::is_weak_referenceable())
        .def_prop_ro("owner", [](const StateRecord& record) -> nb::object {
            return record.owner.is_valid() ? record.owner : nb::none();
        })
        .def_prop_ro("dictionary", [](const StateRecord& record) -> nb::object {
            return record.dictionary_owner.is_valid() ? record.dictionary_owner : nb::none();
        })
        .def_prop_ro("model_ref", [](const StateRecord& record) -> nb::object {
            return record.model_ref_owner.is_valid() ? record.model_ref_owner : nb::none();
        })
        .def_prop_ro("watched", [](const StateRecord& record) { return record.watched; });
    module.def("install_camera_namespace", &install_camera_namespace, nb::arg("type"));
    module.def("install_grid_namespace", &install_grid_namespace, nb::arg("type"));
    module.def("component_model_post_init", [](nb::handle self, nb::handle parent, nb::handle context) {
        parent.attr("model_post_init")(context);
        attach_component_state(self);
    }, nb::arg("self").none(), nb::arg("parent"), nb::arg("context").none());
    module.def("component_model_deep_copy", [](nb::handle, nb::handle parent, nb::handle memo) {
        nb::object result = parent.attr("__deepcopy__")(memo);
        attach_component_state(result);
        return result;
    }, nb::arg("self").none(), nb::arg("parent"), nb::arg("memo").none());
    module.def("component_model_restore", [](nb::handle self, nb::handle parent, nb::handle state) {
        parent.attr("__setstate__")(state);
        attach_component_state(self);
    }, nb::arg("self").none(), nb::arg("parent"), nb::arg("state").none());
}
} // namespace nx
