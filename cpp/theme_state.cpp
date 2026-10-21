// One isolated, zero-added-layout binding; real BaseModel slots stay canonical.
#include <nanobind/nanobind.h>
#include <Python.h>
#include <array>
#include <limits>
#include <unordered_map>
#include <utility>
#include <functional>
#include <initializer_list>
#include <vector>
#include <cstring>

namespace nx {
namespace nb = nanobind;
namespace {
constexpr const char* names[] = {
    "__dict__", "__pydantic_fields_set__", "__pydantic_extra__"
};
constexpr const char* metadata_name = "_theme_native_publication_metadata";

// This journal contains no model field/fields-set/extra/private objects. The
// only per-instance edge is a genuine weakref; its numbers are diagnostics.
struct PublicationEntry {
    nb::object weakref;
    std::array<unsigned long long, 3> counts{};
};
struct PublicationJournal;
struct PublicationJournal {
    std::array<nb::object, 3> descriptors;
    std::unordered_map<PyObject*, PublicationEntry> entries;
    bool bound = false;

    PublicationJournal() = default;
    PublicationJournal(const PublicationJournal&) = delete;
    PublicationJournal(PublicationJournal&&) noexcept = default;
    ~PublicationJournal() { clear(); }
    void clear() noexcept {
        bound = false;
        auto old_entries = std::move(entries);
        auto old_descriptors = std::move(descriptors);
    }
};
struct PublicationSlot {
    // Each descriptor has its own explicit GC-visible immutable journal edge.
    // A saved descriptor stays safe even if its owning class is later mutated.
    nb::object journal;
    unsigned index;
};

nb::object checked(PyObject* object) {
    if (!object) throw nb::python_error();
    return nb::steal<nb::object>(object);
}
int journal_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (!nb::inst_ready(self)) return 0;
    const auto& journal = *nb::inst_ptr<PublicationJournal>(self);
    for (const auto& descriptor : journal.descriptors) Py_VISIT(descriptor.ptr());
    for (const auto& pair : journal.entries) Py_VISIT(pair.second.weakref.ptr());
    return 0;
}
int journal_clear(PyObject* self) {
    if (nb::inst_ready(self)) nb::inst_ptr<PublicationJournal>(self)->clear();
    return 0;
}
PyType_Slot journal_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(journal_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(journal_clear)}, {0, nullptr}
};

nb::object journal_from_type(PyObject* type) {
    nb::object handle = checked(PyObject_GetAttrString(type, metadata_name));
    if (!nb::isinstance<PublicationJournal>(handle) || !nb::inst_ready(handle)) {
        PyErr_SetString(PyExc_TypeError, "Theme publication metadata was replaced");
        throw nb::python_error();
    }
    auto& journal = *nb::inst_ptr<PublicationJournal>(handle);
    if (!journal.bound) {
        PyErr_SetString(PyExc_RuntimeError, "bind_base_model must precede Theme allocation");
        throw nb::python_error();
    }
    return handle;
}
PublicationEntry& allocated_entry(PublicationJournal& journal, PyObject* self) {
    auto found = journal.entries.find(self);
    if (found == journal.entries.end()) {
        PyErr_SetString(PyExc_RuntimeError, "Theme instance bypassed native allocation");
        throw nb::python_error();
    }
    return found->second;
}

PyObject* forget_allocation(PyObject* context, PyObject*) {
    PyObject* holder = PyTuple_GetItem(context, 0);
    PyObject* key = PyTuple_GetItem(context, 1);
    if (!holder || !key) return nullptr;
    void* address = PyLong_AsVoidPtr(key);
    if (PyErr_Occurred()) return nullptr;
    if (nb::inst_ready(holder))
        nb::inst_ptr<PublicationJournal>(holder)->entries.erase(static_cast<PyObject*>(address));
    Py_RETURN_NONE;
}
PyMethodDef forget_definition = {
    "_forget_theme_allocation", forget_allocation, METH_O, nullptr
};

PyObject* namespace_new(PyTypeObject* type, PyObject*, PyObject*) {
    try {
        nb::object holder = journal_from_type(reinterpret_cast<PyObject*>(type));
        auto& journal = *nb::inst_ptr<PublicationJournal>(holder);
        // The combined type's solid BaseModel layout supplies all real stores,
        // GC and weakref slots. This native base contributes zero payload bytes.
        nb::object model = checked(PyType_GenericAlloc(type, 0));
        nb::object key = checked(PyLong_FromVoidPtr(model.ptr()));
        nb::object context = checked(PyTuple_Pack(2, holder.ptr(), key.ptr()));
        nb::object cleanup = checked(PyCFunction_NewEx(&forget_definition, context.ptr(), nullptr));
        PublicationEntry entry;
        entry.weakref = checked(PyWeakref_NewRef(model.ptr(), cleanup.ptr()));
        const bool inserted = journal.entries.emplace(model.ptr(), std::move(entry)).second;
        if (!inserted) {
            PyErr_SetString(PyExc_RuntimeError, "Theme allocation identity already registered");
            throw nb::python_error();
        }
        return model.release().ptr();
    } catch (nb::python_error& error) { error.restore(); }
      catch (const std::bad_alloc&) { PyErr_NoMemory(); }
    return nullptr;
}

// An ordinary explicit native callable is not CPython's inherited
// tp_new_wrapper. The class creation namespace binds it as static __new__, so
// both type calls and public cls.__new__(cls) reach the same native kernel.
PyObject* allocate_theme(PyObject* context, PyObject* args, PyObject* kwargs) {
    PyObject* native_base = PyTuple_GetItem(context, 0);
    PyObject* base_model = PyTuple_GetItem(context, 2);
    if (!native_base || !base_model) return nullptr;
    if (!PyTuple_Check(args) || PyTuple_Size(args) < 1) {
        PyErr_SetString(PyExc_TypeError, "allocate_theme requires a concrete Theme class");
        return nullptr;
    }
    PyObject* concrete = PyTuple_GetItem(args, 0);
    if (!PyType_Check(concrete) ||
        !PyType_IsSubtype(reinterpret_cast<PyTypeObject*>(concrete), reinterpret_cast<PyTypeObject*>(native_base)) ||
        !PyType_IsSubtype(reinterpret_cast<PyTypeObject*>(concrete), reinterpret_cast<PyTypeObject*>(base_model))) {
        PyErr_SetString(PyExc_TypeError, "allocate_theme requires the genuine combined Theme/BaseModel subtype");
        return nullptr;
    }
    // The existing kernel owns allocation/weak registration before validation,
    // and it never creates/copies model values or adopts a preexisting instance.
    return namespace_new(reinterpret_cast<PyTypeObject*>(concrete), args, kwargs);
}
PyMethodDef allocation_definition = {
    "allocate_theme", reinterpret_cast<PyCFunction>(allocate_theme),
    METH_VARARGS | METH_KEYWORDS,
    "Allocate the actual combined Theme layout and register before validation."
};

PyObject* read_slot(PublicationJournal& journal, unsigned index, PyObject* self) {
    const auto& descriptor = journal.descriptors[index];
    auto get = reinterpret_cast<descrgetfunc>(PyType_GetSlot(Py_TYPE(descriptor.ptr()), Py_tp_descr_get));
    // Direct genuine descriptor invocation reads the existing BaseModel slot.
    return get(descriptor.ptr(), self, reinterpret_cast<PyObject*>(Py_TYPE(self)));
}
int publish_slot(PublicationJournal& journal, unsigned index, PyObject* self, PyObject* value) {
    try {
        auto& entry = allocated_entry(journal, self);
        bool counted = entry.counts[index] != std::numeric_limits<unsigned long long>::max();
        if (counted) ++entry.counts[index];
        const auto& descriptor = journal.descriptors[index];
        auto set = reinterpret_cast<descrsetfunc>(PyType_GetSlot(Py_TYPE(descriptor.ptr()), Py_tp_descr_set));
        // Only the original slot setter owns/retains the actual value. Count is
        // already visible when its obsolete payload finalizer can reenter.
        const int status = set(descriptor.ptr(), self, value);
        if (status < 0 && counted) --entry.counts[index];
        return status;
    } catch (nb::python_error& error) { error.restore(); }
      catch (const std::bad_alloc&) { PyErr_NoMemory(); }
    return -1;
}

// Handwritten Theme canonical-state lifecycle bodies; metadata stores no values.
struct ThemeLifecycleBinding {
    nb::object base, allocator, pydantic_globals, evented_globals, memo_names, literal_names;
    nb::object constructor_literals, base_initializer, base_setter;
    nb::object base_selector, selector_literals, descriptor_frame;
    nb::object validate_assignment_handler, validate_assignment_code;
    nb::object validate_assignment_native_frame, validate_assignment_provider;
    std::array<nb::object,4> simple_handlers, simple_codes, simple_builtins;
    std::array<nb::object,4> simple_native_frames, simple_providers;
    std::array<nb::object,2> policy_handlers, policy_codes, policy_globals, policy_builtins;
    std::array<nb::object,2> policy_native_frames, policy_providers;
    ThemeLifecycleBinding() = default;
    ThemeLifecycleBinding(ThemeLifecycleBinding&&) noexcept = default;
    ThemeLifecycleBinding(const ThemeLifecycleBinding&) = delete;
    void clear() noexcept {
        auto a = std::move(base), b = std::move(allocator);
        auto c = std::move(pydantic_globals), d = std::move(evented_globals);
        auto e = std::move(memo_names), f = std::move(literal_names);
        auto g = std::move(constructor_literals), h = std::move(base_initializer);
        auto i = std::move(base_setter);
        auto j = std::move(base_selector), k = std::move(selector_literals);
        auto l = std::move(descriptor_frame);
        auto m = std::move(validate_assignment_handler), n = std::move(validate_assignment_code);
        auto o = std::move(validate_assignment_native_frame), p = std::move(validate_assignment_provider);
        auto q = std::move(simple_handlers), r = std::move(simple_codes);
        auto s = std::move(simple_builtins), t = std::move(simple_native_frames);
        auto u = std::move(simple_providers);
        auto v = std::move(policy_handlers), w = std::move(policy_codes);
        auto x = std::move(policy_globals), y = std::move(policy_builtins);
        auto z = std::move(policy_native_frames), aa = std::move(policy_providers);
    }
    ~ThemeLifecycleBinding() { clear(); }
};
struct ThemeLifecycleLocals {
    int entry = -1;
    nb::object binding, self, memo, cls, m, copied, private_value, k, v, update, deep, state;
    ThemeLifecycleLocals() = default;
    ThemeLifecycleLocals(ThemeLifecycleLocals&&) noexcept = default;
    ThemeLifecycleLocals(const ThemeLifecycleLocals&) = delete;
    void clear() noexcept {
        // Only original assigned locals plus immutable binding metadata. Each
        // entry follows its source locals order; inner comprehension bindings
        // never remain in these outer k/v fields after that RHS returns.
        std::array<nb::object, 12> old{
            std::move(binding), std::move(self), std::move(memo), std::move(cls),
            std::move(m), std::move(copied), std::move(private_value),
            std::move(k), std::move(v), std::move(update), std::move(deep), std::move(state)};
        constexpr unsigned order[5][11] = {
            {1,3,4,2,5,6,7,8,9,10,11},
            {1,2,3,4,5,6,7,8,9,10,11},
            {1,9,10,5,2,7,8,3,4,6,11},
            {1,6,2,3,4,5,7,8,9,10,11},
            {1,11,2,3,4,5,6,7,8,9,10}};
        if (entry >= 0 && entry < 5)
            for (unsigned index : order[entry]) old[index].reset();
        else for (unsigned index = 1; index < old.size(); ++index) old[index].reset();
        old[0].reset();
    }
    ~ThemeLifecycleLocals() { clear(); }
};
int lifecycle_binding_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (!nb::inst_ready(self)) return 0;
    const auto& b = *nb::inst_ptr<ThemeLifecycleBinding>(self);
    Py_VISIT(b.base.ptr()); Py_VISIT(b.allocator.ptr());
    Py_VISIT(b.pydantic_globals.ptr()); Py_VISIT(b.evented_globals.ptr()); Py_VISIT(b.memo_names.ptr());
    Py_VISIT(b.literal_names.ptr());
    Py_VISIT(b.constructor_literals.ptr()); Py_VISIT(b.base_initializer.ptr());
    Py_VISIT(b.base_setter.ptr());
    Py_VISIT(b.base_selector.ptr()); Py_VISIT(b.selector_literals.ptr());
    Py_VISIT(b.descriptor_frame.ptr());
    Py_VISIT(b.validate_assignment_handler.ptr()); Py_VISIT(b.validate_assignment_code.ptr());
    Py_VISIT(b.validate_assignment_native_frame.ptr()); Py_VISIT(b.validate_assignment_provider.ptr());
    for (unsigned entry=0; entry<4; ++entry) {
        Py_VISIT(b.simple_handlers[entry].ptr()); Py_VISIT(b.simple_codes[entry].ptr());
        Py_VISIT(b.simple_builtins[entry].ptr()); Py_VISIT(b.simple_native_frames[entry].ptr());
        Py_VISIT(b.simple_providers[entry].ptr());
    }
    for (unsigned entry=0; entry<2; ++entry) {
        Py_VISIT(b.policy_handlers[entry].ptr()); Py_VISIT(b.policy_codes[entry].ptr());
        Py_VISIT(b.policy_globals[entry].ptr()); Py_VISIT(b.policy_builtins[entry].ptr());
        Py_VISIT(b.policy_native_frames[entry].ptr()); Py_VISIT(b.policy_providers[entry].ptr());
    }
    return 0;
}
int lifecycle_binding_clear(PyObject* self) {
    if (nb::inst_ready(self)) nb::inst_ptr<ThemeLifecycleBinding>(self)->clear();
    return 0;
}
PyType_Slot lifecycle_binding_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(lifecycle_binding_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(lifecycle_binding_clear)}, {0,nullptr}};
int lifecycle_locals_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (!nb::inst_ready(self)) return 0;
    const auto& o = *nb::inst_ptr<ThemeLifecycleLocals>(self);
    Py_VISIT(o.binding.ptr()); Py_VISIT(o.self.ptr()); Py_VISIT(o.memo.ptr());
    Py_VISIT(o.cls.ptr()); Py_VISIT(o.m.ptr()); Py_VISIT(o.copied.ptr());
    Py_VISIT(o.private_value.ptr()); Py_VISIT(o.k.ptr()); Py_VISIT(o.v.ptr());
    Py_VISIT(o.update.ptr()); Py_VISIT(o.deep.ptr()); Py_VISIT(o.state.ptr());
    return 0;
}
int lifecycle_locals_clear(PyObject* self) {
    if (nb::inst_ready(self)) nb::inst_ptr<ThemeLifecycleLocals>(self)->clear();
    return 0;
}
PyType_Slot lifecycle_locals_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(lifecycle_locals_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(lifecycle_locals_clear)}, {0,nullptr}};
// Immutable declaration metadata only. These are the actual compiled provider
// literal objects, not strings reconstructed by the native policy calls.
nb::object life_source_literal(nb::handle constants, const char* text) {
    PyObject* selected = nullptr;
    for (Py_ssize_t index = 0; index < PyTuple_Size(constants.ptr()); ++index) {
        PyObject* constant = PyTuple_GetItem(constants.ptr(), index);
        if (!constant) throw nb::python_error();
        Py_ssize_t count = PyTuple_CheckExact(constant) ? PyTuple_Size(constant) : 1;
        for (Py_ssize_t item = 0; item < count; ++item) {
            PyObject* value = PyTuple_CheckExact(constant) ? PyTuple_GetItem(constant,item) : constant;
            if (!value) throw nb::python_error();
            if (!PyUnicode_CheckExact(value)) continue;
            int comparison = PyUnicode_CompareWithASCIIString(value,text);
            if (comparison == -1 && PyErr_Occurred()) throw nb::python_error();
            if (comparison != 0) continue;
            if (selected && selected != value)
                throw nb::type_error("one exact compiled source literal object required");
            selected = value;
        }
    }
    if (!selected) throw nb::type_error("compiled BaseModel source literal metadata is missing");
    return nb::borrow<nb::object>(selected);
}
nb::object life_source_literals(nb::handle model_type) {
    constexpr const char* methods[] = {"__copy__","__deepcopy__","model_copy","__getstate__","__setstate__"};
    constexpr const char* words[5][4] = {
        {"__dict__","__pydantic_extra__","__pydantic_fields_set__","__pydantic_private__"},
        {"__dict__","__pydantic_extra__","__pydantic_fields_set__","__pydantic_private__"},
        {"extra","allow",nullptr,nullptr},
        {"__dict__","__pydantic_extra__","__pydantic_fields_set__","__pydantic_private__"},
        {"__pydantic_fields_set__","__pydantic_extra__","__pydantic_private__","__dict__"}};
    nb::object result = checked(PyTuple_New(5));
    for (unsigned entry = 0; entry < 5; ++entry) {
        nb::object function = checked(PyObject_GetAttrString(model_type.ptr(),methods[entry]));
        nb::object code = checked(PyObject_GetAttrString(function.ptr(),"__code__"));
        nb::object constants = checked(PyObject_GetAttrString(code.ptr(),"co_consts"));
        if (!PyTuple_CheckExact(constants.ptr())) throw nb::type_error("genuine compiled source constant tuple required");
        unsigned count = entry == 2 ? 2 : 4;
        nb::object values = checked(PyTuple_New(count));
        for (unsigned index = 0; index < count; ++index) {
            nb::object value = life_source_literal(constants,words[entry][index]);
            PyTuple_SET_ITEM(values.ptr(),index,value.release().ptr());
        }
        PyTuple_SET_ITEM(result.ptr(),entry,values.release().ptr());
    }
    return result;
}
nb::object life_literal(const ThemeLifecycleBinding& b, unsigned entry, unsigned index) {
    if (!b.literal_names.is_valid()) throw nb::type_error("Theme source literal metadata was cleared");
    PyObject* values = PyTuple_GetItem(b.literal_names.ptr(),entry);
    if (!values) throw nb::python_error();
    PyObject* value = PyTuple_GetItem(values,index);
    if (!value) throw nb::python_error();
    return nb::borrow<nb::object>(value);
}
using LifeExpr = std::function<nb::object()>;
void life_assign(nb::object& target, nb::object value) {
    nb::object previous = std::move(target); target = std::move(value); previous.reset();
}
nb::object life_attr(nb::object receiver, const char* name) {
    PyObject* result = PyObject_GetAttrString(receiver.ptr(), name);
    receiver.reset(); return checked(result);
}
nb::object life_global(nb::handle globals, const char* name) {
    PyObject* value = PyDict_GetItemString(globals.ptr(), name);
    if (value) return nb::borrow<nb::object>(value);
    if (PyErr_Occurred()) throw nb::python_error();
    PyObject* builtins = PyDict_GetItemString(globals.ptr(), "__builtins__");
    nb::object found;
    if (builtins && PyDict_Check(builtins)) {
        value = PyDict_GetItemString(builtins, name);
        if (value) return nb::borrow<nb::object>(value);
        if (PyErr_Occurred()) throw nb::python_error();
    } else if (builtins) {
        found = nb::steal<nb::object>(PyObject_GetAttrString(builtins, name));
        if (found.is_valid()) return found;
        if (!PyErr_ExceptionMatches(PyExc_AttributeError)) throw nb::python_error();
        PyErr_Clear();
    }
    PyErr_Format(PyExc_NameError, "name '%s' is not defined", name);
    throw nb::python_error();
}
nb::object life_call(nb::object function, std::initializer_list<LifeExpr> positional = {},
                     std::initializer_list<LifeExpr> keywords = {}, nb::object names = {}) {
    std::vector<nb::object> values;
    values.reserve(positional.size() + keywords.size());
    for (const auto& expression : positional) values.push_back(expression());
    for (const auto& expression : keywords) values.push_back(expression());
    if (keywords.size() && (!names.is_valid() || !PyTuple_CheckExact(names.ptr()) ||
            PyTuple_Size(names.ptr()) != static_cast<Py_ssize_t>(keywords.size())))
        throw nb::type_error("matching compiled source keyword-name tuple required");
    std::vector<PyObject*> arguments;
    arguments.reserve(values.size());
    for (const auto& value : values) arguments.push_back(value.ptr());
    PyObject* result = PyObject_Vectorcall(function.ptr(), arguments.data(), positional.size(), names.ptr());
    function.reset(); for (auto& value : values) value.reset(); names.reset();
    return checked(result);
}
bool life_truth(nb::object value) {
    int status = PyObject_IsTrue(value.ptr()); value.reset();
    if (status < 0) throw nb::python_error(); return status != 0;
}
bool life_contains(LifeExpr item, LifeExpr container) {
    nb::object key = item(), target = container();
    int status = PySequence_Contains(target.ptr(), key.ptr()); key.reset(); target.reset();
    if (status < 0) throw nb::python_error(); return status != 0;
}
nb::object life_iterator(nb::object value) {
    PyObject* result = PyObject_GetIter(value.ptr()); value.reset(); return checked(result);
}
nb::object life_next(nb::handle iterator) {
    PyObject* value = PyIter_Next(iterator.ptr());
    if (!value && PyErr_Occurred()) throw nb::python_error();
    return nb::steal<nb::object>(value);
}
std::array<nb::object,2> life_unpack_two(nb::object value) {
    PyObject* raw = PyObject_GetIter(value.ptr());
    if (!raw) {
        nb::python_error error;
        if (error.matches(PyExc_TypeError) && Py_TYPE(value.ptr())->tp_iter == nullptr && !PySequence_Check(value.ptr())) {
            PyErr_Format(PyExc_TypeError, "cannot unpack non-iterable %.200s object", Py_TYPE(value.ptr())->tp_name);
            throw nb::python_error();
        }
        throw error;
    }
    nb::object iterator = nb::steal<nb::object>(raw);
    std::array<nb::object,2> pair;
    for (unsigned index = 0; index < 2; ++index) {
        pair[index] = life_next(iterator);
        if (!pair[index].is_valid()) {
            PyErr_Format(PyExc_ValueError, "not enough values to unpack (expected 2, got %u)", index);
            throw nb::python_error();
        }
    }
    nb::object extra = life_next(iterator);
    if (extra.is_valid()) throw nb::value_error("too many values to unpack (expected 2)");
    iterator.reset(); value.reset(); return pair;
}
void life_set_item(LifeExpr value, LifeExpr receiver, LifeExpr key) {
    nb::object result = value(), target = receiver(), index = key();
    int status = PyObject_SetItem(target.ptr(), index.ptr(), result.ptr());
    result.reset(); target.reset(); index.reset(); if (status < 0) throw nb::python_error();
}
void life_publish(LifeExpr value, LifeExpr receiver, const char* name) {
    nb::object result = value(), target = receiver();
    int status = PyObject_SetAttrString(target.ptr(), name, result.ptr());
    result.reset(); target.reset(); if (status < 0) throw nb::python_error();
}
#define LE(value) [&]()->nb::object{return (value);}
#define LG(name) life_global(b.pydantic_globals, name)
#define EG(name) life_global(b.evented_globals, name)
#define LA(value,name) life_attr((value),name)
// This is the genuine RHS new private map, not a parallel live authority. Its
// k/v belong only to the comprehension; source outer loop locals stay intact.
nb::object life_filter_private(ThemeLifecycleBinding& b, LifeExpr input) {
    nb::object function = LA(input(), "items");
    nb::object iterator = life_iterator(life_call(std::move(function)));
    nb::object result = checked(PyDict_New());
    nb::object k, v;
    try {
        while (nb::object item = life_next(iterator)) {
            auto pair = life_unpack_two(std::move(item));
            life_assign(k, std::move(pair[0])); life_assign(v, std::move(pair[1]));
            nb::object marker = LG("PydanticUndefined");
            bool retain = v.ptr() != marker.ptr(); marker.reset();
            if (retain && PyObject_SetItem(result.ptr(), k.ptr(), v.ptr()) < 0) throw nb::python_error();
        }
    } catch (...) { iterator.reset(); result.reset(); k.reset(); v.reset(); throw; }
    iterator.reset(); k.reset(); v.reset(); return result;
}
nb::object life_copy_core(ThemeLifecycleBinding& b, ThemeLifecycleLocals& o, bool deep) {
    life_assign(o.cls, life_call(LG("type"), {LE(o.self)}));
    life_assign(o.m, life_call(LA(o.cls, "__new__"), {LE(o.cls)}));
    const char* data_names[] = {"__dict__", "__pydantic_extra__", "__pydantic_fields_set__"};
    for (unsigned index = 0; index < 3; ++index) {
        const char* name = data_names[index];
        life_call(LG("_object_setattr"), {LE(o.m), [&]()->nb::object{return life_literal(b,deep ? 1 : 0,index);}, [&]()->nb::object {
            nb::object function = LG(deep && index < 2 ? "deepcopy" : "copy");
            if (deep && index < 2) return life_call(std::move(function), {[&]()->nb::object{return LA(o.self,name);}}, {LE(o.memo)}, b.memo_names);
            return life_call(std::move(function), {[&]()->nb::object{return LA(o.self,name);}});
        }});
    }
    bool absent = !life_truth(life_call(LG("hasattr"), {LE(o.self), [&]()->nb::object{return life_literal(b,deep ? 1 : 0,3);}}));
    if (!absent) { nb::object value = LA(o.self,"__pydantic_private__"); absent = value.is_none(); }
    if (absent) life_call(LG("_object_setattr"), {LE(o.m), [&]()->nb::object{return life_literal(b,deep ? 1 : 0,3);}, LE(nb::none())});
    else life_call(LG("_object_setattr"), {LE(o.m), [&]()->nb::object{return life_literal(b,deep ? 1 : 0,3);}, [&]()->nb::object {
        if (!deep) return life_filter_private(b, [&]()->nb::object{return LA(o.self,"__pydantic_private__");});
        nb::object function = LG("deepcopy");
        return life_call(std::move(function), {[&]()->nb::object{return life_filter_private(b,[&]()->nb::object{return LA(o.self,"__pydantic_private__");});}}, {LE(o.memo)}, b.memo_names);
    }});
    return o.m;
}
nb::object life_copy_finish(ThemeLifecycleBinding& b, ThemeLifecycleLocals& o, bool deep) {
    // The completed inner BaseModel frame has returned. Its result is a
    // transient return lease; cls/m must not survive into outer finish errors.
    nb::object result = life_copy_core(b,o,deep);
    o.cls.reset(); o.m.reset();
    if (!deep) {
        // Actual accepted copy_state locals survive through the target store.
        // RHS temporary ends first, then state/copy/result unwind on error.
        nb::object copy = EG("copy");
        nb::object state = LA(o.self,"_change_state");
        nb::object replacement = copy(state);
        int status = PyObject_SetAttrString(result.ptr(),"_change_state",replacement.ptr());
        replacement.reset();
        if (status < 0) throw nb::python_error();
    } else {
        if (PyObject_SetAttrString(result.ptr(),"_primary_view",Py_None) < 0)
            throw nb::python_error();
    }
    return result;
}
nb::object life_model_copy(ThemeLifecycleBinding& b, ThemeLifecycleLocals& o) {
    if (life_truth(o.deep) && life_truth(o.update)) {
        life_assign(o.copied, life_call(LA(o.self,"__copy__")));
        life_assign(o.memo, checked(PyDict_New()));
        nb::object iterator = life_iterator(life_call(LA(LA(o.copied,"__dict__"),"items")));
        try { while (nb::object item = life_next(iterator)) {
            auto pair = life_unpack_two(std::move(item));
            life_assign(o.k,std::move(pair[0])); life_assign(o.v,std::move(pair[1]));
            if (!life_contains(LE(o.k),LE(o.update))) life_set_item([&]()->nb::object{return life_call(LG("deepcopy"),{LE(o.v),LE(o.memo)});},[&]()->nb::object{return LA(o.copied,"__dict__");},LE(o.k));
        }} catch (...) {iterator.reset();throw;} iterator.reset();
        nb::object extra = LA(o.copied,"__pydantic_extra__"); bool has_extra = !extra.is_none(); extra.reset();
        if (has_extra) {
            iterator = life_iterator(life_call(LA(LA(o.copied,"__pydantic_extra__"),"items")));
            try { while (nb::object item = life_next(iterator)) {
                auto pair=life_unpack_two(std::move(item));life_assign(o.k,std::move(pair[0]));life_assign(o.v,std::move(pair[1]));
                if(!life_contains(LE(o.k),LE(o.update)))life_set_item([&]()->nb::object{return life_call(LG("deepcopy"),{LE(o.v),LE(o.memo)});},[&]()->nb::object{return LA(o.copied,"__pydantic_extra__");},LE(o.k));
            }}catch(...){iterator.reset();throw;}iterator.reset();
        }
        nb::object private_value=LA(o.copied,"__pydantic_private__");bool has_private=!private_value.is_none();private_value.reset();
        if(has_private)life_publish([&]()->nb::object{return life_call(LG("deepcopy"),{[&]()->nb::object{return life_filter_private(b,[&]()->nb::object{return LA(o.copied,"__pydantic_private__");});},LE(o.memo)});},LE(o.copied),"__pydantic_private__");
    } else {
        const char* method = life_truth(o.deep) ? "__deepcopy__" : "__copy__";
        life_assign(o.copied,life_call(LA(o.self,method)));
    }
    if(life_truth(o.update)) {
        nb::object left=life_call(LA(LA(o.self,"model_config"),"get"),{[&]()->nb::object{return life_literal(b,2,0);}});
        nb::object right=life_literal(b,2,1);PyObject* raw=PyObject_RichCompare(left.ptr(),right.ptr(),Py_EQ);left.reset();right.reset();
        if(life_truth(checked(raw))) {
            nb::object iterator=life_iterator(life_call(LA(o.update,"items")));
            try {while(nb::object item=life_next(iterator)) {
                auto pair=life_unpack_two(std::move(item));life_assign(o.k,std::move(pair[0]));life_assign(o.v,std::move(pair[1]));
                if(life_contains(LE(o.k),[&]()->nb::object{return LA(o.self,"__pydantic_fields__");}))life_set_item(LE(o.v),[&]()->nb::object{return LA(o.copied,"__dict__");},LE(o.k));
                else {
                    nb::object extra=LA(o.copied,"__pydantic_extra__");bool absent=extra.is_none();extra.reset();
                    if(absent)life_publish([&]()->nb::object{return checked(PyDict_New());},LE(o.copied),"__pydantic_extra__");
                    life_set_item(LE(o.v),[&]()->nb::object{return LA(o.copied,"__pydantic_extra__");},LE(o.k));
                }
            }}catch(...){iterator.reset();throw;}iterator.reset();
        } else life_call(LA(LA(o.copied,"__dict__"),"update"),{LE(o.update)});
        life_call(LA(LA(o.copied,"__pydantic_fields_set__"),"update"),{[&]()->nb::object{return life_call(LA(o.update,"keys"));}});
    }
    return o.copied;
}
nb::object life_getstate(ThemeLifecycleBinding& b, ThemeLifecycleLocals& o) {
    life_assign(o.private_value,LA(o.self,"__pydantic_private__"));
    if(life_truth(o.private_value))life_assign(o.private_value,life_filter_private(b,LE(o.private_value)));
    std::array<nb::object,4> values{LA(o.self,"__dict__"),LA(o.self,"__pydantic_extra__"),LA(o.self,"__pydantic_fields_set__"),o.private_value};
    nb::object result=checked(PyDict_New());
    for(unsigned index=0;index<4;++index) {
        nb::object key=life_literal(b,3,index);
        if(PyDict_SetItem(result.ptr(),key.ptr(),values[index].ptr())<0)throw nb::python_error();
    }
    for(auto& value:values)value.reset();return result;
}
nb::object life_setstate(ThemeLifecycleBinding& b, ThemeLifecycleLocals& o) {
    for(unsigned index=0;index<4;++index)life_call(LG("_object_setattr"),{LE(o.self),[&]()->nb::object{return life_literal(b,4,index);},[&]()->nb::object{return life_call(LA(o.state,"get"),{[&]()->nb::object{return life_literal(b,4,index);},[&]()->nb::object{return checked(PyDict_New());}});}});
    return nb::none();
}
nb::object lifecycle_locals(ThemeLifecycleBinding& b,int entry,nb::args args) {
    constexpr int count[]={1,2,3,1,2};
    if(entry<0||entry>=5||args.size()!=static_cast<size_t>(count[entry]))throw nb::type_error("exact original lifecycle parameter binding required");
    nb::object result=nb::cast(ThemeLifecycleLocals{});auto& o=*nb::inst_ptr<ThemeLifecycleLocals>(result);
    o.entry=entry;o.binding=nb::borrow<nb::object>(nb::find(&b));o.self=nb::borrow<nb::object>(args[0]);
    if(entry==1)o.memo=nb::borrow<nb::object>(args[1]);
    if(entry==2){o.update=nb::borrow<nb::object>(args[1]);o.deep=nb::borrow<nb::object>(args[2]);}
    if(entry==4)o.state=nb::borrow<nb::object>(args[1]);return result;
}
nb::object lifecycle_run(ThemeLifecycleBinding& b,ThemeLifecycleLocals& o) {
    if(o.binding.ptr()!=nb::find(&b).ptr())throw nb::type_error("lifecycle owner belongs to another binding");
    switch(o.entry){case 0:return life_copy_finish(b,o,false);case 1:return life_copy_finish(b,o,true);case 2:return life_model_copy(b,o);case 3:return life_getstate(b,o);case 4:return life_setstate(b,o);}
    throw nb::value_error("unknown original lifecycle entry");
}

// Source constructor frames own only their actual parameters/assigned locals.
// Metadata providers contain delegation only; the selected policy lives here.
struct ThemeConstructorLocals {
    int entry = -1;
    nb::object binding, self, kwargs, data, tracebackhide, validated_self;
    nb::object super_provider, base_provider;
    ThemeConstructorLocals() = default;
    ThemeConstructorLocals(ThemeConstructorLocals&&) noexcept = default;
    ThemeConstructorLocals(const ThemeConstructorLocals&) = delete;
    void clear() noexcept {
        auto old_self = std::move(self), old_kwargs = std::move(kwargs);
        auto old_data = std::move(data), old_hide = std::move(tracebackhide);
        auto old_validated = std::move(validated_self);
        auto old_super = std::move(super_provider), old_base = std::move(base_provider);
        auto old_binding = std::move(binding);
        old_self.reset();
        if (entry == 0) old_kwargs.reset();
        else { old_data.reset(); old_hide.reset(); old_validated.reset(); }
        old_super.reset(); old_base.reset(); old_binding.reset();
    }
    ~ThemeConstructorLocals() { clear(); }
};
int constructor_locals_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (!nb::inst_ready(self)) return 0;
    const auto& o = *nb::inst_ptr<ThemeConstructorLocals>(self);
    Py_VISIT(o.binding.ptr()); Py_VISIT(o.self.ptr()); Py_VISIT(o.kwargs.ptr());
    Py_VISIT(o.data.ptr()); Py_VISIT(o.tracebackhide.ptr()); Py_VISIT(o.validated_self.ptr());
    Py_VISIT(o.super_provider.ptr()); Py_VISIT(o.base_provider.ptr());
    return 0;
}
int constructor_locals_clear(PyObject* self) {
    if (nb::inst_ready(self)) nb::inst_ptr<ThemeConstructorLocals>(self)->clear();
    return 0;
}
PyType_Slot constructor_locals_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(constructor_locals_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(constructor_locals_clear)}, {0,nullptr}};

// Read the exact immutable KW_NAMES/constant objects of the real provider.
nb::object constructor_source_literals(nb::handle original_initializer) {
    nb::object code = checked(PyObject_GetAttrString(original_initializer.ptr(),"__code__"));
    nb::object constants = checked(PyObject_GetAttrString(code.ptr(),"co_consts"));
    if (!PyTuple_CheckExact(constants.ptr()))
        throw nb::type_error("genuine compiled constructor constants required");
    nb::object result = checked(PyTuple_New(6));
    constexpr const char* keyword[] = {"self_instance", "stacklevel"};
    for (unsigned slot=0; slot<2; ++slot) {
        PyObject* selected = nullptr;
        for (Py_ssize_t index=0; index<PyTuple_Size(constants.ptr()); ++index) {
            PyObject* value = PyTuple_GetItem(constants.ptr(),index);
            if (!value) throw nb::python_error();
            if (!PyTuple_CheckExact(value) || PyTuple_Size(value)!=1) continue;
            PyObject* name = PyTuple_GetItem(value,0);
            if (!name) throw nb::python_error();
            if (!PyUnicode_CheckExact(name)) continue;
            int same = PyUnicode_CompareWithASCIIString(name,keyword[slot]);
            if (same==-1 && PyErr_Occurred()) throw nb::python_error();
            if (same==0) {
                if (selected && selected!=value)
                    throw nb::type_error("unique constructor source keyword tuple required");
                selected=value;
            }
        }
        if (!selected) throw nb::type_error("constructor source keyword tuple missing");
        PyTuple_SET_ITEM(result.ptr(),slot,Py_NewRef(selected));
    }
    PyObject* level = nullptr;
    for (Py_ssize_t index=0; index<PyTuple_Size(constants.ptr()); ++index) {
        PyObject* value = PyTuple_GetItem(constants.ptr(),index);
        if (!value) throw nb::python_error();
        if (PyLong_CheckExact(value) && PyLong_AsLong(value)==2) {
            if (level && level!=value)
                throw nb::type_error("unique source stacklevel constant required");
            level=value;
        }
        if (PyErr_Occurred()) throw nb::python_error();
    }
    if (!level) throw nb::type_error("source stacklevel constant missing");
    PyTuple_SET_ITEM(result.ptr(),2,Py_NewRef(level));
    constexpr const char* fragments[] = {
        "A custom validator is returning a value other than `self`.\n"
        "Returning anything other than `self` from a top level model validator isn't supported when validating via `__init__`.\n"
        "See the `model_validator` docs (https://pydantic.dev/docs/validation/",
        "/concepts/validators/#model-validators) for more details."};
    // The source joins the first two adjacent literals with the f-string prefix
    // into one compiled constant, then FORMAT_VALUE and its exact suffix.
    nb::object prefix = life_source_literal(constants,fragments[0]);
    nb::object suffix = life_source_literal(constants,fragments[1]);
    PyTuple_SET_ITEM(result.ptr(),3,prefix.release().ptr());
    PyTuple_SET_ITEM(result.ptr(),4,suffix.release().ptr());
    // True is the source-assigned __tracebackhide__ object.
    PyTuple_SET_ITEM(result.ptr(),5,Py_NewRef(Py_True));
    return result;
}
nb::object constructor_literal(ThemeLifecycleBinding& b, unsigned index) {
    if (!b.constructor_literals.is_valid())
        throw nb::type_error("constructor source metadata was cleared");
    PyObject* value=PyTuple_GetItem(b.constructor_literals.ptr(),index);
    if (!value) throw nb::python_error();
    return nb::borrow<nb::object>(value);
}
nb::object constructor_call_expanded(nb::object function, nb::object kwargs) {
    // Original CALL_FUNCTION_EX: receiver/callable has already been evaluated;
    // args is the genuine empty tuple and **kwargs is a fresh exact dictionary.
    nb::object args=checked(PyTuple_New(0));
    nb::object expanded=checked(PyDict_Copy(kwargs.ptr()));
    kwargs.reset();
    PyObject* raw=PyObject_Call(function.ptr(),args.ptr(),expanded.ptr());
    function.reset(); args.reset(); expanded.reset();
    return checked(raw);
}
nb::object constructor_base_run(ThemeLifecycleBinding& b, ThemeConstructorLocals& o) {
    life_assign(o.tracebackhide,constructor_literal(b,5));
    life_assign(o.validated_self,life_call(
        life_attr(life_attr(o.self,"__pydantic_validator__"),"validate_python"),
        {LE(o.data)}, {LE(o.self)}, constructor_literal(b,0)));
    if (o.self.ptr()!=o.validated_self.ptr()) {
        // Source LOAD_GLOBAL warnings/LOAD_ATTR warn precedes every format RHS.
        nb::object warn=life_attr(life_global(b.pydantic_globals,"warnings"),"warn");
        nb::object prefix=constructor_literal(b,3);
        nb::object version=life_call(life_global(b.pydantic_globals,"version_short"));
        PyObject* formatted_raw=PyObject_Format(version.ptr(),nullptr);
        version.reset();
        nb::object formatted=checked(formatted_raw);
        nb::object suffix=constructor_literal(b,4);
        nb::object fragments=checked(PyTuple_Pack(3,prefix.ptr(),formatted.ptr(),suffix.ptr()));
        nb::object separator=checked(PyUnicode_FromString(""));
        PyObject* joined=PyUnicode_Join(separator.ptr(),fragments.ptr());
        // BUILD_STRING drops its source operands in their forward stack order.
        // Remove each extra tuple lease before that operand's final release.
        std::array<nb::object*,3> parts{&prefix,&formatted,&suffix};
        for (unsigned index=0; index<parts.size(); ++index) {
            PyObject* prior=PyTuple_GET_ITEM(fragments.ptr(),index);
            PyTuple_SET_ITEM(fragments.ptr(),index,Py_NewRef(Py_None));
            Py_DECREF(prior); parts[index]->reset();
        }
        fragments.reset(); separator.reset();
        nb::object message=checked(joined);
        life_call(std::move(warn),{[&]()->nb::object{return std::move(message);}},
                  {[&]()->nb::object{return constructor_literal(b,2);}},constructor_literal(b,1));
    }
    return nb::none();
}
nb::object constructor_evented_run(ThemeLifecycleBinding& b, ThemeConstructorLocals& o) {
    // The provider has the actual Evented lexical __class__ cell and genuine
    // source globals. Replaced zero-argument super callbacks see zero args.
    nb::object target=life_attr(life_call(o.super_provider,{LE(o.self)}),"__init__");
    const bool direct_base=PyMethod_Check(target.ptr()) &&
        PyMethod_GET_FUNCTION(target.ptr())==b.base_initializer.ptr() &&
        PyMethod_GET_SELF(target.ptr())==o.self.ptr();
    if (direct_base) {
        // A thin parameter-only envelope gives the source inner **data namespace
        // and warning frame depth. It contains no BaseModel algorithm/body.
        nb::object bound=checked(PyMethod_New(o.base_provider.ptr(),o.self.ptr()));
        try { constructor_call_expanded(std::move(bound),o.kwargs); }
        catch (...) { target.reset(); throw; }
        target.reset();
    } else {
        // A real intervening plugin/mixin constructor remains a public override
        // callback. Its downstream policy is not declared native by this packet.
        constructor_call_expanded(std::move(target),o.kwargs);
    }
    life_call(life_attr(life_global(b.evented_globals,"_native"),"model_initialize_events"),
              {LE(o.self)});
    return nb::none();
}
nb::object constructor_locals(ThemeLifecycleBinding& b, int entry, nb::args args) {
    const unsigned count=entry==0?4:2;
    if ((entry!=0 && entry!=1) || args.size()!=count || !PyDict_CheckExact(args[1].ptr()))
        throw nb::type_error("exact source constructor parameters required");
    nb::object result=nb::cast(ThemeConstructorLocals{});
    auto& o=*nb::inst_ptr<ThemeConstructorLocals>(result);
    o.entry=entry; o.binding=nb::borrow<nb::object>(nb::find(&b));
    o.self=nb::borrow<nb::object>(args[0]);
    if (entry==0) {
        o.kwargs=nb::borrow<nb::object>(args[1]);
        o.super_provider=nb::borrow<nb::object>(args[2]);
        o.base_provider=nb::borrow<nb::object>(args[3]);
    } else o.data=nb::borrow<nb::object>(args[1]);
    return result;
}
void constructor_run(ThemeLifecycleBinding& b, ThemeConstructorLocals& o) {
    if (o.binding.ptr()!=nb::find(&b).ptr())
        throw nb::type_error("constructor owner belongs to another binding");
    if (o.entry==0) { constructor_evented_run(b,o); return; }
    if (o.entry==1) { constructor_base_run(b,o); return; }
    throw nb::value_error("unknown source constructor entry");
}


// Source-assigned locals only, retained on the thin Python error frame. The
// model's public handler mapping is always fresh; this owner is no value map.
struct ThemeAssignmentLocals {
    nb::object binding, self, name, value, setattr_handler;
    ThemeAssignmentLocals() = default;
    ThemeAssignmentLocals(ThemeAssignmentLocals&&) noexcept = default;
    ThemeAssignmentLocals(const ThemeAssignmentLocals&) = delete;
    void clear() noexcept {
        auto old_self=std::move(self), old_name=std::move(name);
        auto old_value=std::move(value), old_handler=std::move(setattr_handler);
        auto old_binding=std::move(binding);
        old_self.reset(); old_name.reset(); old_value.reset(); old_handler.reset();
        old_binding.reset();
    }
    ~ThemeAssignmentLocals() { clear(); }
};
int assignment_locals_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (!nb::inst_ready(self)) return 0;
    const auto& o=*nb::inst_ptr<ThemeAssignmentLocals>(self);
    Py_VISIT(o.binding.ptr()); Py_VISIT(o.self.ptr()); Py_VISIT(o.name.ptr());
    Py_VISIT(o.value.ptr()); Py_VISIT(o.setattr_handler.ptr());
    return 0;
}
int assignment_locals_clear(PyObject* self) {
    if (nb::inst_ready(self)) nb::inst_ptr<ThemeAssignmentLocals>(self)->clear();
    return 0;
}
PyType_Slot assignment_locals_slots[] = {
    {Py_tp_traverse,reinterpret_cast<void*>(assignment_locals_traverse)},
    {Py_tp_clear,reinterpret_cast<void*>(assignment_locals_clear)}, {0,nullptr}};

// Immutable per-Theme declaration metadata. It contains no model instance,
// handler, field value or mapping. Each reload creates its own class edge.
struct ThemeAssignmentMetadata {
    nb::object binding, provider;
    ThemeAssignmentMetadata() = default;
    ThemeAssignmentMetadata(ThemeAssignmentMetadata&&) noexcept = default;
    ThemeAssignmentMetadata(const ThemeAssignmentMetadata&) = delete;
    void clear() noexcept {
        auto old_binding=std::move(binding), old_provider=std::move(provider);
    }
    ~ThemeAssignmentMetadata() { clear(); }
};
int assignment_metadata_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (!nb::inst_ready(self)) return 0;
    const auto& metadata=*nb::inst_ptr<ThemeAssignmentMetadata>(self);
    Py_VISIT(metadata.binding.ptr()); Py_VISIT(metadata.provider.ptr());
    return 0;
}
int assignment_metadata_clear(PyObject* self) {
    if (nb::inst_ready(self)) nb::inst_ptr<ThemeAssignmentMetadata>(self)->clear();
    return 0;
}
PyType_Slot assignment_metadata_slots[] = {
    {Py_tp_traverse,reinterpret_cast<void*>(assignment_metadata_traverse)},
    {Py_tp_clear,reinterpret_cast<void*>(assignment_metadata_clear)}, {0,nullptr}};
constexpr const char* assignment_metadata_name="_theme_native_assignment_metadata";

nb::object assignment_locals(ThemeLifecycleBinding& b, nb::handle self,
                             nb::handle name, nb::handle value) {
    nb::object result=nb::cast(ThemeAssignmentLocals{});
    auto& o=*nb::inst_ptr<ThemeAssignmentLocals>(result);
    o.binding=nb::borrow<nb::object>(nb::find(&b));
    o.self=nb::borrow<nb::object>(self); o.name=nb::borrow<nb::object>(name);
    o.value=nb::borrow<nb::object>(value);
    return result;
}

// Captured identity plus immutable reviewed code metadata, never a mapping-key
// inference at invocation. A different or mutated callable remains a callback.
bool validate_assignment_tuple(PyObject* tuple, const char* const* words, unsigned count) {
    if (!PyTuple_CheckExact(tuple) || PyTuple_GET_SIZE(tuple)!=count) return false;
    for (unsigned i=0; i<count; ++i) {
        PyObject* value=PyTuple_GET_ITEM(tuple,i);
        if (!PyUnicode_CheckExact(value) || PyUnicode_CompareWithASCIIString(value,words[i])!=0)
            return false;
    }
    return true;
}
bool validate_assignment_code_reviewed(PyObject* raw) {
    if (!PyCode_Check(raw)) return false;
    const auto* code=reinterpret_cast<PyCodeObject*>(raw);
    if (code->co_argcount!=3 || code->co_posonlyargcount!=0 || code->co_kwonlyargcount!=0 ||
        code->co_nlocals!=3 || code->co_stacksize!=5 || code->co_flags!=16777219 ||
        code->co_firstlineno!=129 || !PyUnicode_CheckExact(code->co_name) ||
        PyUnicode_CompareWithASCIIString(code->co_name,"<lambda>")!=0 ||
        !PyUnicode_CheckExact(code->co_qualname) ||
        PyUnicode_CompareWithASCIIString(code->co_qualname,"<lambda>")!=0 ||
        !PyTuple_CheckExact(code->co_consts) || PyTuple_GET_SIZE(code->co_consts)!=1 ||
        PyTuple_GET_ITEM(code->co_consts,0)!=Py_None ||
        !PyBytes_CheckExact(code->co_exceptiontable) || PyBytes_GET_SIZE(code->co_exceptiontable)!=0)
        return false;
    constexpr const char* names[]={"__pydantic_validator__","validate_assignment"};
    constexpr const char* parameters[]={"model","name","val"};
    nb::object varnames=checked(PyObject_GetAttrString(raw,"co_varnames"));
    nb::object freevars=checked(PyObject_GetAttrString(raw,"co_freevars"));
    nb::object cellvars=checked(PyObject_GetAttrString(raw,"co_cellvars"));
    if (!validate_assignment_tuple(code->co_names,names,2) ||
        !validate_assignment_tuple(varnames.ptr(),parameters,3) ||
        !validate_assignment_tuple(freevars.ptr(),nullptr,0) ||
        !validate_assignment_tuple(cellvars.ptr(),nullptr,0)) return false;
    // These bytes are source-only compiler metadata for the pinned line129
    // expression, not executable/generated application implementation.
#if PY_VERSION_HEX >= 0x030D0000 && PY_VERSION_HEX < 0x030E0000
    constexpr unsigned char expected[]={149,0,85,0,82,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,82,3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,88,1,85,2,53,3,0,0,0,0,0,0,36,0};
#elif PY_VERSION_HEX >= 0x030C0000 && PY_VERSION_HEX < 0x030D0000
    constexpr unsigned char expected[]={151,0,124,0,106,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,106,3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,124,0,124,1,124,2,171,3,0,0,0,0,0,0,83,0};
#elif PY_VERSION_HEX >= 0x030B0000 && PY_VERSION_HEX < 0x030C0000
    constexpr unsigned char expected[]={151,0,124,0,106,0,0,0,0,0,0,0,0,0,160,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,124,0,124,1,124,2,166,3,0,0,171,3,0,0,0,0,0,0,0,0,83,0};
#else
    return false; // Unsupported compiler metadata leaves the genuine callback OPEN.
#endif
#if PY_VERSION_HEX >= 0x030B0000 && PY_VERSION_HEX < 0x030E0000
    nb::object instructions=checked(PyCode_GetCode(reinterpret_cast<PyCodeObject*>(raw)));
    return PyBytes_CheckExact(instructions.ptr()) && PyBytes_GET_SIZE(instructions.ptr())==sizeof(expected) &&
        std::memcmp(PyBytes_AS_STRING(instructions.ptr()),expected,sizeof(expected))==0;
#endif
}
bool validate_assignment_declaration_unchanged(const ThemeLifecycleBinding& b, PyObject* function) {
    if (!PyFunction_Check(function) || !b.validate_assignment_code.is_valid() ||
        PyFunction_GetCode(function)!=b.validate_assignment_code.ptr() ||
        PyFunction_GetGlobals(function)!=b.pydantic_globals.ptr() ||
        PyFunction_GetDefaults(function)!=nullptr || PyFunction_GetKwDefaults(function)!=nullptr ||
        PyFunction_GetClosure(function)!=nullptr) return false;
    const auto* declaration=reinterpret_cast<PyFunctionObject*>(function);
    return PyUnicode_CheckExact(declaration->func_name) &&
        PyUnicode_CompareWithASCIIString(declaration->func_name,"<lambda>")==0 &&
        PyUnicode_CheckExact(declaration->func_qualname) &&
        PyUnicode_CompareWithASCIIString(declaration->func_qualname,"<lambda>")==0;
}
void capture_validate_assignment(ThemeLifecycleBinding& b) {
    PyObject* mapping=PyDict_GetItemString(b.pydantic_globals.ptr(),"_SIMPLE_SETATTR_HANDLERS");
    if (!mapping || !PyDict_CheckExact(mapping)) return;
    PyObject* function=PyDict_GetItemString(mapping,"validate_assignment");
    if (!function || !PyFunction_Check(function)) return;
    PyObject* code=PyFunction_GetCode(function);
    if (!validate_assignment_code_reviewed(code)) return;
    b.validate_assignment_code=nb::borrow<nb::object>(code);
    if (!validate_assignment_declaration_unchanged(b,function)) {
        b.validate_assignment_code.reset(); return;
    }
    b.validate_assignment_handler=nb::borrow<nb::object>(function);
}

// Only the original lambda parameters; no validator, model-value map or binding.
// The source-located declaration frame owns this object on success and error.
struct ThemeValidateAssignmentLocals {
    nb::object model, name, val;
    ThemeValidateAssignmentLocals() = default;
    ThemeValidateAssignmentLocals(ThemeValidateAssignmentLocals&&) noexcept = default;
    ThemeValidateAssignmentLocals(const ThemeValidateAssignmentLocals&) = delete;
    void clear() noexcept {
        auto old_model=std::move(model), old_name=std::move(name), old_val=std::move(val);
        old_model.reset(); old_name.reset(); old_val.reset();
    }
    ~ThemeValidateAssignmentLocals() { clear(); }
};
int validate_assignment_locals_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (!nb::inst_ready(self)) return 0;
    const auto& o=*nb::inst_ptr<ThemeValidateAssignmentLocals>(self);
    Py_VISIT(o.model.ptr()); Py_VISIT(o.name.ptr()); Py_VISIT(o.val.ptr());
    return 0;
}
int validate_assignment_locals_clear(PyObject* self) {
    if (nb::inst_ready(self)) nb::inst_ptr<ThemeValidateAssignmentLocals>(self)->clear();
    return 0;
}
PyType_Slot validate_assignment_locals_slots[]={
    {Py_tp_traverse,reinterpret_cast<void*>(validate_assignment_locals_traverse)},
    {Py_tp_clear,reinterpret_cast<void*>(validate_assignment_locals_clear)}, {0,nullptr}};
nb::object validate_assignment_frame(nb::handle args) {
    if (!PyTuple_CheckExact(args.ptr()))
        throw nb::type_error("actual validate_assignment source parameters required");
    const Py_ssize_t count=PyTuple_GET_SIZE(args.ptr());
    if (count==3) {
        nb::object result=nb::cast(ThemeValidateAssignmentLocals{});
        auto& o=*nb::inst_ptr<ThemeValidateAssignmentLocals>(result);
        o.model=nb::borrow<nb::object>(PyTuple_GET_ITEM(args.ptr(),0));
        o.name=nb::borrow<nb::object>(PyTuple_GET_ITEM(args.ptr(),1));
        o.val=nb::borrow<nb::object>(PyTuple_GET_ITEM(args.ptr(),2));
        return result;
    }
    if (count!=1 || !nb::isinstance<ThemeValidateAssignmentLocals>(nb::handle(PyTuple_GET_ITEM(args.ptr(),0))) ||
        !nb::inst_ready(PyTuple_GET_ITEM(args.ptr(),0)))
        throw nb::type_error("actual validate_assignment source owner required");
    auto& o=*nb::inst_ptr<ThemeValidateAssignmentLocals>(PyTuple_GET_ITEM(args.ptr(),0));
    if (!o.model.is_valid() || !o.name.is_valid() || !o.val.is_valid())
        throw nb::type_error("validate_assignment source owner was cleared");
    // Original expression order: fresh validator, fresh method, exact3args.
    // No source parameters or validator result are coerced or mirrored.
    return life_call(life_attr(life_attr(o.model,"__pydantic_validator__"),"validate_assignment"),
                     {LE(o.model),LE(o.name),LE(o.val)});
}
PyObject* validate_assignment_frame_callback(PyObject*, PyObject* args) {
    try { return validate_assignment_frame(nb::handle(args)).release().ptr(); }
    catch (nb::python_error& error) { error.restore(); }
    catch (const nb::builtin_exception& error) {
        PyErr_SetString(error.type()==nb::exception_type::type_error?PyExc_TypeError:PyExc_RuntimeError,error.what());
    }
    catch (const std::bad_alloc&) { PyErr_NoMemory(); }
    catch (const std::exception& error) { PyErr_SetString(PyExc_RuntimeError,error.what()); }
    catch (...) { PyErr_SetString(PyExc_SystemError,"untranslated Theme validate_assignment callback exception"); }
    return nullptr;
}
PyMethodDef validate_assignment_frame_definition={
    "theme_validate_assignment_frame",validate_assignment_frame_callback,METH_VARARGS,
    "Source-local transfer/native validation forwarding without application context."};
void bind_validate_assignment(ThemeLifecycleBinding& b, nb::handle factory) {
    if (!PyFunction_Check(factory.ptr()))
        throw nb::type_error("validate_assignment declaration factory required");
    // Real module reload reuses this binding. Its source-only declaration frame
    // captures no Theme class or binding, so reuse it without rerunning factory
    // metadata or replacing frame identity. The current original handler/code
    // mutation guards remain at every assignment_invoke call.
    if (b.validate_assignment_provider.is_valid()) return;
    if (!b.validate_assignment_handler.is_valid()) return;
    if (!validate_assignment_declaration_unchanged(b,b.validate_assignment_handler.ptr()))
        return; // Late declaration mutation leaves the actual callback in control.
    nb::object provider=life_call(nb::borrow<nb::object>(factory),
        {[&]()->nb::object{return b.validate_assignment_code;},
         [&]()->nb::object{return b.validate_assignment_native_frame;}});
    if (!PyFunction_Check(provider.ptr()) || PyFunction_GetClosure(provider.ptr())!=nullptr ||
        PyFunction_GetGlobals(provider.ptr())!=b.pydantic_globals.ptr())
        throw nb::type_error("module-free validate_assignment declaration frame required");
    b.validate_assignment_provider=std::move(provider);
}
// Four captured source declarations, not a value/store/handler authority map.
constexpr const char* simple_handler_keys[]={"model_field","private","cached_property","extra_known"};
constexpr const char* simple_handler_names[]={
    "_model_field_setattr_handler","_private_setattr_handler","<lambda>","<lambda>"};
bool simple_assignment_code_reviewed(PyObject* raw, unsigned entry) {
    if (entry>=4 || !PyCode_Check(raw)) return false;
    const auto* code=reinterpret_cast<PyCodeObject*>(raw);
    constexpr int first_lines[]={109,114,131,132}, stack_sizes[]={3,5,4,5};
    if (code->co_argcount!=3 || code->co_posonlyargcount!=0 || code->co_kwonlyargcount!=0 ||
        code->co_nlocals!=3 || code->co_stacksize!=stack_sizes[entry] || code->co_flags!=16777219 ||
        code->co_firstlineno!=first_lines[entry] || !PyUnicode_CheckExact(code->co_name) ||
        PyUnicode_CompareWithASCIIString(code->co_name,simple_handler_names[entry])!=0 ||
        !PyUnicode_CheckExact(code->co_qualname) ||
        PyUnicode_CompareWithASCIIString(code->co_qualname,simple_handler_names[entry])!=0 ||
        !PyTuple_CheckExact(code->co_consts) || PyTuple_GET_SIZE(code->co_consts)!=(entry==1?2:1) ||
        PyTuple_GET_ITEM(code->co_consts,0)!=Py_None ||
        !PyBytes_CheckExact(code->co_exceptiontable) || PyBytes_GET_SIZE(code->co_exceptiontable)!=0)
        return false;
    if (entry==1) {
        PyObject* name=PyTuple_GET_ITEM(code->co_consts,1);
        if (!PyUnicode_CheckExact(name) || PyUnicode_CompareWithASCIIString(name,"__pydantic_private__")!=0)
            return false;
    }
    constexpr const char* parameters[]={"model","name","val"};
    constexpr const char* field_names[]={"__dict__","__pydantic_fields_set__","add"};
    constexpr const char* private_names[]={"getattr","object","__setattr__","__pydantic_private__"};
    constexpr const char* cached_names[]={"__dict__","__setitem__"};
    constexpr const char* extra_names[]={"_object_setattr"};
    const char* const* expected_names[]={field_names,private_names,cached_names,extra_names};
    constexpr unsigned counts[]={3,4,2,1};
    nb::object varnames=checked(PyObject_GetAttrString(raw,"co_varnames"));
    nb::object freevars=checked(PyObject_GetAttrString(raw,"co_freevars"));
    nb::object cellvars=checked(PyObject_GetAttrString(raw,"co_cellvars"));
    if (!validate_assignment_tuple(code->co_names,expected_names[entry],counts[entry]) ||
        !validate_assignment_tuple(varnames.ptr(),parameters,3) ||
        !validate_assignment_tuple(freevars.ptr(),nullptr,0) ||
        !validate_assignment_tuple(cellvars.ptr(),nullptr,0)) return false;
    // Source-only compiler metadata for code recognition; never executed.
#if PY_VERSION_HEX >= 0x30d0000 && PY_VERSION_HEX < 0x30e0000
    constexpr unsigned char expected0[]={149,0,88,32,82,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,85,1,39,0,0,0,85,0,82,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,82,5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,85,1,53,1,0,0,0,0,0,0,32,0,103,0};
    constexpr unsigned char expected1[]={149,0,91,1,0,0,0,0,0,0,0,0,85,0,83,1,83,0,53,3,0,0,0,0,0,0,99,23,0,0,91,2,0,0,0,0,0,0,0,0,82,5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,85,0,83,1,48,0,53,3,0,0,0,0,0,0,32,0,88,32,82,6,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,85,1,39,0,0,0,103,0};
    constexpr unsigned char expected2[]={149,0,85,0,82,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,82,3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,88,18,53,2,0,0,0,0,0,0,36,0};
    constexpr unsigned char expected3[]={149,0,91,1,0,0,0,0,0,0,0,0,88,1,85,2,53,3,0,0,0,0,0,0,36,0};
#elif PY_VERSION_HEX >= 0x30c0000 && PY_VERSION_HEX < 0x30d0000
    constexpr unsigned char expected0[]={151,0,124,2,124,0,106,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,124,1,60,0,0,0,124,0,106,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,106,5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,124,1,171,1,0,0,0,0,0,0,1,0,121,0};
    constexpr unsigned char expected1[]={151,0,116,1,0,0,0,0,0,0,0,0,124,0,100,1,100,0,171,3,0,0,0,0,0,0,128,23,116,2,0,0,0,0,0,0,0,0,106,5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,124,0,100,1,105,0,171,3,0,0,0,0,0,0,1,0,124,2,124,0,106,6,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,124,1,60,0,0,0,121,0};
    constexpr unsigned char expected2[]={151,0,124,0,106,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,106,3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,124,1,124,2,171,2,0,0,0,0,0,0,83,0};
    constexpr unsigned char expected3[]={151,0,116,1,0,0,0,0,0,0,0,0,124,0,124,1,124,2,171,3,0,0,0,0,0,0,83,0};
#elif PY_VERSION_HEX >= 0x30b0000 && PY_VERSION_HEX < 0x30c0000
    constexpr unsigned char expected0[]={151,0,124,2,124,0,106,0,0,0,0,0,0,0,0,0,124,1,60,0,0,0,124,0,106,1,0,0,0,0,0,0,0,0,160,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,124,1,166,1,0,0,171,1,0,0,0,0,0,0,0,0,1,0,100,0,83,0};
    constexpr unsigned char expected1[]={151,0,116,1,0,0,0,0,0,0,0,0,0,0,124,0,100,1,100,0,166,3,0,0,171,3,0,0,0,0,0,0,0,0,128,28,116,2,0,0,0,0,0,0,0,0,0,0,160,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,124,0,100,1,105,0,166,3,0,0,171,3,0,0,0,0,0,0,0,0,1,0,124,2,124,0,106,3,0,0,0,0,0,0,0,0,124,1,60,0,0,0,100,0,83,0};
    constexpr unsigned char expected2[]={151,0,124,0,106,0,0,0,0,0,0,0,0,0,160,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,124,1,124,2,166,2,0,0,171,2,0,0,0,0,0,0,0,0,83,0};
    constexpr unsigned char expected3[]={151,0,116,1,0,0,0,0,0,0,0,0,0,0,124,0,124,1,124,2,166,3,0,0,171,3,0,0,0,0,0,0,0,0,83,0};
#else
    return false; // Future/unknown compiler metadata remains OPEN.
#endif
#if PY_VERSION_HEX >= 0x030B0000 && PY_VERSION_HEX < 0x030E0000
    const unsigned char* expected[]={expected0,expected1,expected2,expected3};
    constexpr size_t sizes[]={sizeof(expected0),sizeof(expected1),sizeof(expected2),sizeof(expected3)};
    nb::object instructions=checked(PyCode_GetCode(reinterpret_cast<PyCodeObject*>(raw)));
    return PyBytes_CheckExact(instructions.ptr()) && PyBytes_GET_SIZE(instructions.ptr())==sizes[entry] &&
        std::memcmp(PyBytes_AS_STRING(instructions.ptr()),expected[entry],sizes[entry])==0;
#endif
}
bool simple_assignment_declaration_unchanged(const ThemeLifecycleBinding& b, unsigned entry,
                                           PyObject* function) {
    if (entry>=4 || !PyFunction_Check(function) || !b.simple_codes[entry].is_valid() ||
        !b.simple_builtins[entry].is_valid() ||
        PyFunction_GetCode(function)!=b.simple_codes[entry].ptr() ||
        PyFunction_GetGlobals(function)!=b.pydantic_globals.ptr() ||
        PyFunction_GetDefaults(function)!=nullptr || PyFunction_GetKwDefaults(function)!=nullptr ||
        PyFunction_GetClosure(function)!=nullptr) return false;
    const auto* declaration=reinterpret_cast<PyFunctionObject*>(function);
    return declaration->func_builtins==b.simple_builtins[entry].ptr() &&
        PyUnicode_CheckExact(declaration->func_name) &&
        PyUnicode_CompareWithASCIIString(declaration->func_name,simple_handler_names[entry])==0 &&
        PyUnicode_CheckExact(declaration->func_qualname) &&
        PyUnicode_CompareWithASCIIString(declaration->func_qualname,simple_handler_names[entry])==0;
}
void capture_simple_assignment_handlers(ThemeLifecycleBinding& b) {
    if (!PyDict_CheckExact(b.pydantic_globals.ptr())) return;
    PyObject* mapping=PyDict_GetItemString(b.pydantic_globals.ptr(),"_SIMPLE_SETATTR_HANDLERS");
    if (!mapping || !PyDict_CheckExact(mapping)) return;
    for (unsigned entry=0; entry<4; ++entry) {
        PyObject* function=PyDict_GetItemString(mapping,simple_handler_keys[entry]);
        if (!function || !PyFunction_Check(function)) continue;
        PyObject* code=PyFunction_GetCode(function);
        if (!simple_assignment_code_reviewed(code,entry)) continue;
        auto* declaration=reinterpret_cast<PyFunctionObject*>(function);
        if (!declaration->func_builtins) continue;
        b.simple_codes[entry]=nb::borrow<nb::object>(code);
        b.simple_builtins[entry]=nb::borrow<nb::object>(declaration->func_builtins);
        if (!simple_assignment_declaration_unchanged(b,entry,function)) {
            b.simple_codes[entry].reset(); b.simple_builtins[entry].reset(); continue;
        }
        b.simple_handlers[entry]=nb::borrow<nb::object>(function);
    }
}

// Actual original three parameters plus exact source namespace edges only.
// Dictionary CONTENT is live and authoritative, never copied into metadata.
struct ThemeSimpleAssignmentLocals {
    nb::object globals, builtins, model, name, val;
    ThemeSimpleAssignmentLocals() = default;
    ThemeSimpleAssignmentLocals(ThemeSimpleAssignmentLocals&&) noexcept = default;
    ThemeSimpleAssignmentLocals(const ThemeSimpleAssignmentLocals&) = delete;
    void clear() noexcept {
        auto old_model=std::move(model), old_name=std::move(name), old_val=std::move(val);
        auto old_globals=std::move(globals), old_builtins=std::move(builtins);
        old_model.reset(); old_name.reset(); old_val.reset();
        old_globals.reset(); old_builtins.reset();
    }
    ~ThemeSimpleAssignmentLocals() { clear(); }
};
int simple_assignment_locals_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (!nb::inst_ready(self)) return 0;
    const auto& o=*nb::inst_ptr<ThemeSimpleAssignmentLocals>(self);
    Py_VISIT(o.model.ptr()); Py_VISIT(o.name.ptr()); Py_VISIT(o.val.ptr());
    Py_VISIT(o.globals.ptr()); Py_VISIT(o.builtins.ptr());
    return 0;
}
int simple_assignment_locals_clear(PyObject* self) {
    if (nb::inst_ready(self)) nb::inst_ptr<ThemeSimpleAssignmentLocals>(self)->clear();
    return 0;
}
PyType_Slot simple_assignment_locals_slots[]={
    {Py_tp_traverse,reinterpret_cast<void*>(simple_assignment_locals_traverse)},
    {Py_tp_clear,reinterpret_cast<void*>(simple_assignment_locals_clear)}, {0,nullptr}};
nb::object simple_assignment_global(const ThemeSimpleAssignmentLocals& o, const char* name) {
    nb::object key=checked(PyUnicode_InternFromString(name));
    PyObject* value=PyDict_GetItemWithError(o.globals.ptr(),key.ptr());
    if (value) return nb::borrow<nb::object>(value);
    if (PyErr_Occurred()) throw nb::python_error();
    if (PyDict_CheckExact(o.builtins.ptr())) {
        value=PyDict_GetItemWithError(o.builtins.ptr(),key.ptr());
        if (value) return nb::borrow<nb::object>(value);
        if (PyErr_Occurred()) throw nb::python_error();
    } else {
        value=PyObject_GetItem(o.builtins.ptr(),key.ptr());
        if (value) return nb::steal<nb::object>(value);
        if (!PyErr_ExceptionMatches(PyExc_KeyError)) throw nb::python_error();
        PyErr_Clear();
    }
    nb::object message=checked(PyUnicode_FromFormat("name '%s' is not defined",name));
    nb::object error=checked(PyObject_CallFunctionObjArgs(PyExc_NameError,message.ptr(),nullptr));
    if (PyObject_SetAttrString(error.ptr(),"name",key.ptr())<0) throw nb::python_error();
    PyErr_SetObject(PyExc_NameError,error.ptr());
    throw nb::python_error();
}
nb::object simple_assignment_frame(nb::handle args, unsigned entry) {
    if (!PyTuple_CheckExact(args.ptr()) || entry>=4)
        throw nb::type_error("actual simple assignment source parameters required");
    const Py_ssize_t count=PyTuple_GET_SIZE(args.ptr());
    if (count==5) {
        if (!PyDict_CheckExact(PyTuple_GET_ITEM(args.ptr(),0)))
            throw nb::type_error("actual simple assignment source globals required");
        nb::object result=nb::cast(ThemeSimpleAssignmentLocals{});
        auto& o=*nb::inst_ptr<ThemeSimpleAssignmentLocals>(result);
        o.globals=nb::borrow<nb::object>(PyTuple_GET_ITEM(args.ptr(),0));
        o.builtins=nb::borrow<nb::object>(PyTuple_GET_ITEM(args.ptr(),1));
        o.model=nb::borrow<nb::object>(PyTuple_GET_ITEM(args.ptr(),2));
        o.name=nb::borrow<nb::object>(PyTuple_GET_ITEM(args.ptr(),3));
        o.val=nb::borrow<nb::object>(PyTuple_GET_ITEM(args.ptr(),4));
        return result;
    }
    if (count!=1 || !nb::isinstance<ThemeSimpleAssignmentLocals>(nb::handle(PyTuple_GET_ITEM(args.ptr(),0))) ||
        !nb::inst_ready(PyTuple_GET_ITEM(args.ptr(),0)))
        throw nb::type_error("actual simple assignment source owner required");
    auto& o=*nb::inst_ptr<ThemeSimpleAssignmentLocals>(PyTuple_GET_ITEM(args.ptr(),0));
    if (!o.globals.is_valid() || !o.builtins.is_valid() || !o.model.is_valid() ||
        !o.name.is_valid() || !o.val.is_valid())
        throw nb::type_error("simple assignment source owner was cleared");
    if (entry==0) {
        life_set_item(LE(o.val),[&]()->nb::object{return life_attr(o.model,"__dict__");},LE(o.name));
        life_call(life_attr(life_attr(o.model,"__pydantic_fields_set__"),"add"),{LE(o.name)});
        return nb::none();
    }
    if (entry==1) {
        nb::object private_name=checked(PyUnicode_InternFromString("__pydantic_private__"));
        // Resolve getattr first; object is loaded only on the actual None branch.
        nb::object current=life_call(simple_assignment_global(o,"getattr"),
            {LE(o.model),LE(private_name),[&]()->nb::object{return nb::none();}});
        const bool absent=current.is_none(); current.reset();
        if (absent)
            life_call(life_attr(simple_assignment_global(o,"object"),"__setattr__"),
                {LE(o.model),LE(private_name),[&]()->nb::object{return checked(PyDict_New());}});
        life_set_item(LE(o.val),[&]()->nb::object{return life_attr(o.model,"__pydantic_private__");},LE(o.name));
        return nb::none();
    }
    if (entry==2)
        return life_call(life_attr(life_attr(o.model,"__dict__"),"__setitem__"),{LE(o.name),LE(o.val)});
    return life_call(simple_assignment_global(o,"_object_setattr"),{LE(o.model),LE(o.name),LE(o.val)});
}
template<unsigned Entry> PyObject* simple_assignment_frame_callback(PyObject*, PyObject* args) {
    try { return simple_assignment_frame(nb::handle(args),Entry).release().ptr(); }
    catch (nb::python_error& error) { error.restore(); }
    catch (const nb::builtin_exception& error) {
        PyErr_SetString(error.type()==nb::exception_type::type_error?PyExc_TypeError:PyExc_RuntimeError,error.what());
    }
    catch (const std::bad_alloc&) { PyErr_NoMemory(); }
    catch (const std::exception& error) { PyErr_SetString(PyExc_RuntimeError,error.what()); }
    catch (...) { PyErr_SetString(PyExc_SystemError,"untranslated Theme simple assignment callback exception"); }
    return nullptr;
}
PyMethodDef simple_assignment_frame_definitions[]={
    {"theme_model_field_assignment_frame",simple_assignment_frame_callback<0>,METH_VARARGS,"Source-local/native model field assignment."},
    {"theme_private_assignment_frame",simple_assignment_frame_callback<1>,METH_VARARGS,"Source-local/native private assignment."},
    {"theme_cached_property_assignment_frame",simple_assignment_frame_callback<2>,METH_VARARGS,"Source-local/native cached property assignment."},
    {"theme_extra_known_assignment_frame",simple_assignment_frame_callback<3>,METH_VARARGS,"Source-local/native known extra assignment."}};
void bind_simple_assignment_handlers(ThemeLifecycleBinding& b, nb::handle factory) {
    if (!PyFunction_Check(factory.ptr()))
        throw nb::type_error("simple assignment declaration factory required");
    for (unsigned entry=0; entry<4; ++entry) {
        // A real Theme reload reuses each namespace-only declaration frame.
        if (b.simple_providers[entry].is_valid()) continue;
        if (!b.simple_handlers[entry].is_valid()) continue;
        if (!simple_assignment_declaration_unchanged(b,entry,b.simple_handlers[entry].ptr())) continue;
        if (!b.simple_native_frames[entry].is_valid())
            throw nb::type_error("simple assignment native frame metadata is missing");
        nb::object provider=life_call(nb::borrow<nb::object>(factory),
            {[&]()->nb::object{return b.simple_codes[entry];},
             [&]()->nb::object{return b.simple_native_frames[entry];},
             [&]()->nb::object{return b.pydantic_globals;},
             [&]()->nb::object{return b.simple_builtins[entry];}});
        PyObject* closure=PyFunction_Check(provider.ptr())?PyFunction_GetClosure(provider.ptr()):nullptr;
        if (!PyFunction_Check(provider.ptr()) || PyFunction_GetGlobals(provider.ptr())!=b.pydantic_globals.ptr() ||
            !closure || !PyTuple_CheckExact(closure) || PyTuple_GET_SIZE(closure)!=2 ||
            !PyCell_Check(PyTuple_GET_ITEM(closure,0)) || !PyCell_Check(PyTuple_GET_ITEM(closure,1)) ||
            PyCell_GET(PyTuple_GET_ITEM(closure,0))!=b.simple_builtins[entry].ptr() ||
            PyCell_GET(PyTuple_GET_ITEM(closure,1))!=b.pydantic_globals.ptr())
            throw nb::type_error("actual source namespace-only simple assignment declaration frame required");
        b.simple_providers[entry]=std::move(provider);
    }
}

nb::object assignment_invoke(ThemeLifecycleBinding& b, ThemeAssignmentLocals& o) {
    for (unsigned entry=0; entry<4; ++entry) {
        if (!b.simple_handlers[entry].is_valid() ||
            o.setattr_handler.ptr()!=b.simple_handlers[entry].ptr()) continue;
        // Global/builtin CONTENT mutation is not a handler-body fallback guard.
        // The native body resolves actual replacements at source lookup points.
        if (!b.simple_codes[entry].is_valid() || !b.simple_builtins[entry].is_valid() ||
            !b.pydantic_globals.is_valid())
            throw nb::type_error("simple assignment declaration metadata is missing");
        if (!simple_assignment_declaration_unchanged(b,entry,o.setattr_handler.ptr()))
            return life_call(o.setattr_handler,{LE(o.self),LE(o.name),LE(o.value)});
        if (!b.simple_providers[entry].is_valid() || !b.simple_native_frames[entry].is_valid())
            throw nb::type_error("simple assignment declaration metadata is missing");
        return life_call(b.simple_providers[entry],{LE(o.self),LE(o.name),LE(o.value)});
    }
    const bool recognized=b.validate_assignment_handler.is_valid() &&
        o.setattr_handler.ptr()==b.validate_assignment_handler.ptr() &&
        b.validate_assignment_provider.is_valid() &&
        validate_assignment_declaration_unchanged(b,o.setattr_handler.ptr());
    if (!recognized)
        return life_call(o.setattr_handler,{LE(o.self),LE(o.name),LE(o.value)});
    return life_call(b.validate_assignment_provider,{LE(o.self),LE(o.name),LE(o.value)});
}

nb::object assignment_select(ThemeLifecycleBinding& b, ThemeAssignmentLocals& o);
void assignment_run(ThemeLifecycleBinding& b, ThemeAssignmentLocals& o) {
    if (o.binding.ptr()!=nb::find(&b).ptr())
        throw nb::type_error("assignment owner belongs to another binding");
    life_assign(o.setattr_handler,life_call(
        life_attr(life_attr(o.self,"__pydantic_setattr_handlers__"),"get"),
        {LE(o.name)}));
    // The source tests identity with None, never handler truthiness.
    if (!o.setattr_handler.is_none()) {
        assignment_invoke(b,o);
    } else {
        life_assign(o.setattr_handler,assignment_select(b,o));
        if (!o.setattr_handler.is_none()) {
            assignment_invoke(b,o);
            // Evaluate the actual receiver AGAIN after the handler succeeds.
            // Reentrant hooks can replace its mapping or its handler entry.
            life_set_item(LE(o.setattr_handler),
                [&]()->nb::object{return life_attr(o.self,"__pydantic_setattr_handlers__");},
                LE(o.name));
        }
    }
}

// The public 3.12+ accessor returns a NEW reference and reaches static builtin
// per-interpreter dictionaries. Never pass a possibly NULL raw tp_dict to a
// PyDict function. The 3.11 branch gives the existing dictionary the same lease.
nb::object assignment_type_dictionary(PyTypeObject* type) {
#if PY_VERSION_HEX >= 0x030C0000
    PyObject* raw=PyType_GetDict(type);
#else
    PyObject* raw=Py_XNewRef(type->tp_dict);
#endif
    if (!raw) {
        if (PyErr_Occurred()) throw nb::python_error();
        return {};
    }
    nb::object dictionary=nb::steal<nb::object>(raw);
    if (!PyDict_CheckExact(dictionary.ptr()))
        throw nb::type_error("genuine type declaration dictionary required");
    return dictionary;
}


// Selector owner retains only actual source parameters, fast locals and cells.
struct ThemeSelectorLocals {
    nb::object binding, metadata, self, name, value, cls, attr, attribute;
    ThemeSelectorLocals() = default;
    ThemeSelectorLocals(ThemeSelectorLocals&&) noexcept = default;
    ThemeSelectorLocals(const ThemeSelectorLocals&) = delete;
    void clear() noexcept {
        auto old_self=std::move(self), old_name=std::move(name);
        auto old_value=std::move(value), old_cls=std::move(cls);
        auto old_attr=std::move(attr), old_attribute=std::move(attribute);
        auto old_metadata=std::move(metadata), old_binding=std::move(binding);
        old_self.reset(); old_name.reset(); old_value.reset(); old_cls.reset();
        old_attr.reset(); old_attribute.reset();
        old_metadata.reset(); old_binding.reset();
    }
    ~ThemeSelectorLocals() { clear(); }
};
int selector_locals_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (!nb::inst_ready(self)) return 0;
    const auto& o=*nb::inst_ptr<ThemeSelectorLocals>(self);
    Py_VISIT(o.binding.ptr()); Py_VISIT(o.metadata.ptr());
    Py_VISIT(o.self.ptr()); Py_VISIT(o.name.ptr()); Py_VISIT(o.value.ptr());
    Py_VISIT(o.cls.ptr()); Py_VISIT(o.attr.ptr()); Py_VISIT(o.attribute.ptr());
    return 0;
}
int selector_locals_clear(PyObject* self) {
    if (nb::inst_ready(self)) nb::inst_ptr<ThemeSelectorLocals>(self)->clear();
    return 0;
}
PyType_Slot selector_locals_slots[] = {
    {Py_tp_traverse,reinterpret_cast<void*>(selector_locals_traverse)},
    {Py_tp_clear,reinterpret_cast<void*>(selector_locals_clear)}, {0,nullptr}};

struct ThemeDescriptorLocals {
    nb::object attribute, model, name, val;
    ThemeDescriptorLocals() = default;
    ThemeDescriptorLocals(ThemeDescriptorLocals&&) noexcept = default;
    ThemeDescriptorLocals(const ThemeDescriptorLocals&) = delete;
    void clear() noexcept {
        auto old_model=std::move(model), old_name=std::move(name), old_val=std::move(val);
        auto old_attribute=std::move(attribute);
        old_model.reset(); old_name.reset(); old_val.reset();
        old_attribute.reset();
    }
    ~ThemeDescriptorLocals() { clear(); }
};
int descriptor_locals_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (!nb::inst_ready(self)) return 0;
    const auto& o=*nb::inst_ptr<ThemeDescriptorLocals>(self);
    Py_VISIT(o.attribute.ptr()); Py_VISIT(o.model.ptr());
    Py_VISIT(o.name.ptr()); Py_VISIT(o.val.ptr());
    return 0;
}
int descriptor_locals_clear(PyObject* self) {
    if (nb::inst_ready(self)) nb::inst_ptr<ThemeDescriptorLocals>(self)->clear();
    return 0;
}
PyType_Slot descriptor_locals_slots[] = {
    {Py_tp_traverse,reinterpret_cast<void*>(descriptor_locals_traverse)},
    {Py_tp_clear,reinterpret_cast<void*>(descriptor_locals_clear)}, {0,nullptr}};

// Immutable class declaration metadata, with no model/handler/value authority.
struct ThemeSelectorMetadata {
    nb::object binding, provider, private_factory, property_factory;
    ThemeSelectorMetadata() = default;
    ThemeSelectorMetadata(ThemeSelectorMetadata&&) noexcept = default;
    ThemeSelectorMetadata(const ThemeSelectorMetadata&) = delete;
    void clear() noexcept {
        auto old_binding=std::move(binding), old_provider=std::move(provider);
        auto old_private=std::move(private_factory), old_property=std::move(property_factory);
    }
    ~ThemeSelectorMetadata() { clear(); }
};
int selector_metadata_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (!nb::inst_ready(self)) return 0;
    const auto& metadata=*nb::inst_ptr<ThemeSelectorMetadata>(self);
    Py_VISIT(metadata.binding.ptr()); Py_VISIT(metadata.provider.ptr());
    Py_VISIT(metadata.private_factory.ptr()); Py_VISIT(metadata.property_factory.ptr());
    return 0;
}
int selector_metadata_clear(PyObject* self) {
    if (nb::inst_ready(self)) nb::inst_ptr<ThemeSelectorMetadata>(self)->clear();
    return 0;
}
PyType_Slot selector_metadata_slots[] = {
    {Py_tp_traverse,reinterpret_cast<void*>(selector_metadata_traverse)},
    {Py_tp_clear,reinterpret_cast<void*>(selector_metadata_clear)}, {0,nullptr}};
constexpr const char* selector_metadata_name="_theme_native_selector_metadata";

nb::object selector_metadata(ThemeLifecycleBinding& b, nb::handle self) {
    if (!b.base.is_valid() ||
        !PyObject_TypeCheck(self.ptr(),reinterpret_cast<PyTypeObject*>(b.base.ptr())))
        throw nb::type_error("native selector requires a genuine Theme receiver");
    PyObject* mro=Py_TYPE(self.ptr())->tp_mro;
    if (!mro || !PyTuple_CheckExact(mro))
        throw nb::type_error("Theme selector declaration MRO is missing");
    for (Py_ssize_t index=0; index<PyTuple_GET_SIZE(mro); ++index) {
        auto* type=reinterpret_cast<PyTypeObject*>(PyTuple_GET_ITEM(mro,index));
        nb::object dictionary=assignment_type_dictionary(type);
        if (!dictionary.is_valid()) continue;
        PyObject* raw=PyDict_GetItemString(dictionary.ptr(),selector_metadata_name);
        if (!raw) {
            if (PyErr_Occurred()) throw nb::python_error();
            continue;
        }
        if (!nb::isinstance<ThemeSelectorMetadata>(nb::handle(raw)) || !nb::inst_ready(raw))
            throw nb::type_error("Theme selector declaration metadata was replaced");
        const auto& metadata=*nb::inst_ptr<ThemeSelectorMetadata>(raw);
        if (metadata.binding.ptr()!=nb::find(&b).ptr() || !metadata.provider.is_valid() ||
            !metadata.private_factory.is_valid() || !metadata.property_factory.is_valid())
            throw nb::type_error("Theme selector declaration metadata was cleared or belongs to another binding");
        return nb::borrow<nb::object>(raw);
    }
    throw nb::type_error("Theme selector declaration metadata is missing");
}

nb::object selector_source_literals(nb::handle original) {
    nb::object code=checked(PyObject_GetAttrString(original.ptr(),"__code__"));
    nb::object constants=checked(PyObject_GetAttrString(code.ptr(),"co_consts"));
    if (!PyTuple_CheckExact(constants.ptr()))
        throw nb::type_error("compiled selector source constants required");
    constexpr const char* words[]={
        "private","__set__","cached_property","validate_assignment","extra","allow",
        "extra_known","model_field"," is a ClassVar of `",
        "` and cannot be set on an instance. If you want to set a value on the class, use `",
        "."," = value`.","\"","\" object has no field \""};
    nb::object result=checked(PyTuple_New(16));
    for (unsigned index=0; index<14; ++index) {
        nb::object value=life_source_literal(constants,words[index]);
        PyTuple_SET_ITEM(result.ptr(),index,value.release().ptr());
    }
    for (Py_ssize_t index=0; index<PyTuple_GET_SIZE(constants.ptr()); ++index) {
        PyObject* value=PyTuple_GET_ITEM(constants.ptr(),index);
        if (!PyCode_Check(value)) continue;
        nb::object freevars=checked(PyObject_GetAttrString(value,"co_freevars"));
        if (!PyTuple_CheckExact(freevars.ptr()) || PyTuple_GET_SIZE(freevars.ptr())!=1) continue;
        PyObject* name=PyTuple_GET_ITEM(freevars.ptr(),0);
        if (!PyUnicode_CheckExact(name)) continue;
        unsigned slot=16;
        if (PyUnicode_CompareWithASCIIString(name,"attribute")==0) slot=14;
        else if (PyUnicode_CompareWithASCIIString(name,"attr")==0) slot=15;
        if (PyErr_Occurred()) throw nb::python_error();
        if (slot==16) continue;
        if (PyTuple_GET_ITEM(result.ptr(),slot))
            throw nb::type_error("unique original descriptor code metadata required");
        PyTuple_SET_ITEM(result.ptr(),slot,Py_NewRef(value));
    }
    if (!PyTuple_GET_ITEM(result.ptr(),14) || !PyTuple_GET_ITEM(result.ptr(),15))
        throw nb::type_error("original private/property descriptor code metadata missing");
    return result;
}
nb::object selector_literal(ThemeLifecycleBinding& b, unsigned index) {
    if (!b.selector_literals.is_valid())
        throw nb::type_error("Theme selector source metadata was cleared");
    PyObject* value=PyTuple_GetItem(b.selector_literals.ptr(),index);
    if (!value) throw nb::python_error();
    return nb::borrow<nb::object>(value);
}
nb::object selector_format(nb::object value, bool repr=false) {
    if (repr) {
        PyObject* raw=PyObject_Repr(value.ptr()); value.reset(); value=checked(raw);
    }
    PyObject* raw=PyObject_Format(value.ptr(),nullptr); value.reset(); return checked(raw);
}
nb::object selector_join(std::initializer_list<LifeExpr> expressions) {
    nb::object values=checked(PyTuple_New(static_cast<Py_ssize_t>(expressions.size())));
    Py_ssize_t index=0;
    for (const auto& expression:expressions) {
        nb::object value=expression();
        PyTuple_SET_ITEM(values.ptr(),index++,value.release().ptr());
    }
    nb::object separator=checked(PyUnicode_FromString(""));
    PyObject* joined=PyUnicode_Join(separator.ptr(),values.ptr());
    // BUILD_STRING consumes its source operands in forward stack order.
    for (Py_ssize_t slot=0; slot<PyTuple_GET_SIZE(values.ptr()); ++slot) {
        PyObject* old=PyTuple_GET_ITEM(values.ptr(),slot);
        PyTuple_SET_ITEM(values.ptr(),slot,Py_NewRef(Py_None)); Py_DECREF(old);
    }
    values.reset(); separator.reset(); return checked(joined);
}
// LOAD_GLOBAL belongs to the retained genuine selector function. Its original
// func_builtins remains authoritative when module __builtins__ is replaced;
// source global/builtin dictionary CONTENT is always read freshly.
nb::object selector_source_global(ThemeLifecycleBinding& b, const char* name) {
    if (!b.base_selector.is_valid() || !PyFunction_Check(b.base_selector.ptr()))
        throw nb::type_error("selector source namespace declaration metadata is missing");
    PyObject* globals=PyFunction_GetGlobals(b.base_selector.ptr());
    PyObject* builtins=reinterpret_cast<PyFunctionObject*>(b.base_selector.ptr())->func_builtins;
    if (!globals || !PyDict_CheckExact(globals) || !builtins)
        throw nb::type_error("selector source namespace declaration metadata was cleared");
    // The already GC-visible source function retains both namespace objects.
    // No table values, alternate globals or model state are captured here.
    nb::object key=checked(PyUnicode_InternFromString(name));
    PyObject* value=PyDict_GetItemWithError(globals,key.ptr());
    if (value) return nb::borrow<nb::object>(value);
    if (PyErr_Occurred()) throw nb::python_error();
    if (PyDict_CheckExact(builtins)) {
        value=PyDict_GetItemWithError(builtins,key.ptr());
        if (value) return nb::borrow<nb::object>(value);
        if (PyErr_Occurred()) throw nb::python_error();
    } else {
        value=PyObject_GetItem(builtins,key.ptr());
        if (value) return nb::steal<nb::object>(value);
        if (!PyErr_ExceptionMatches(PyExc_KeyError)) throw nb::python_error();
        PyErr_Clear();
    }
    nb::object message=checked(PyUnicode_FromFormat("name '%s' is not defined",name));
    nb::object error=checked(PyObject_CallFunctionObjArgs(PyExc_NameError,message.ptr(),nullptr));
    if (PyObject_SetAttrString(error.ptr(),"name",key.ptr())<0) throw nb::python_error();
    PyErr_SetObject(PyExc_NameError,error.ptr());
    throw nb::python_error();
}

// These helpers are owned application policies. Capture source declarations,
// never model/config/field values or alternate authoritative namespace tables.
constexpr const char* selector_policy_names[]={"is_valid_field_name","_check_frozen"};
bool selector_policy_code_reviewed(PyObject* raw, unsigned entry) {
    if (entry>=2 || !PyCode_Check(raw)) return false;
    const auto* code=reinterpret_cast<PyCodeObject*>(raw);
    constexpr int argcounts[]={1,3}, localcounts[]={1,4}, stacks[]={3,7}, lines[]={836,96};
    if (code->co_argcount!=argcounts[entry] || code->co_posonlyargcount!=0 ||
        code->co_kwonlyargcount!=0 || code->co_nlocals!=localcounts[entry] ||
        code->co_stacksize!=stacks[entry] || code->co_flags!=16777219 ||
        code->co_firstlineno!=lines[entry] || !PyUnicode_CheckExact(code->co_name) ||
        PyUnicode_CompareWithASCIIString(code->co_name,selector_policy_names[entry])!=0 ||
        !PyUnicode_CheckExact(code->co_qualname) ||
        PyUnicode_CompareWithASCIIString(code->co_qualname,selector_policy_names[entry])!=0 ||
        !PyTuple_CheckExact(code->co_consts) || PyTuple_GET_SIZE(code->co_consts)!=(entry==0?2:6) ||
        PyTuple_GET_ITEM(code->co_consts,0)!=Py_None ||
        !PyBytes_CheckExact(code->co_exceptiontable) || PyBytes_GET_SIZE(code->co_exceptiontable)!=0)
        return false;
    constexpr const char* predicate_parameters[]={"name"}, *predicate_names[]={"startswith"};
    constexpr const char* frozen_parameters[]={"model_cls","name","value","error_type"};
    constexpr const char* frozen_names[]={"model_config","get","getattr","__pydantic_fields__",
        "ValidationError","from_exception_data","__name__"};
    nb::object varnames=checked(PyObject_GetAttrString(raw,"co_varnames"));
    nb::object freevars=checked(PyObject_GetAttrString(raw,"co_freevars"));
    nb::object cellvars=checked(PyObject_GetAttrString(raw,"co_cellvars"));
    if (!validate_assignment_tuple(varnames.ptr(),entry==0?predicate_parameters:frozen_parameters,entry==0?1:4) ||
        !validate_assignment_tuple(code->co_names,entry==0?predicate_names:frozen_names,entry==0?1:7) ||
        !validate_assignment_tuple(freevars.ptr(),nullptr,0) ||
        !validate_assignment_tuple(cellvars.ptr(),nullptr,0)) return false;
    if (entry==0) {
        PyObject* prefix=PyTuple_GET_ITEM(code->co_consts,1);
        if (!PyUnicode_CheckExact(prefix) || PyUnicode_CompareWithASCIIString(prefix,"_")!=0) return false;
    } else {
        constexpr unsigned indexes[]={1,2,4};
        constexpr const char* words[]={"frozen","frozen_instance","frozen_field"};
        for (unsigned i=0; i<3; ++i) {
            PyObject* value=PyTuple_GET_ITEM(code->co_consts,indexes[i]);
            if (!PyUnicode_CheckExact(value) || PyUnicode_CompareWithASCIIString(value,words[i])!=0) return false;
        }
        constexpr const char* keys[]={"type","loc","input"};
        if (PyTuple_GET_ITEM(code->co_consts,3)!=Py_False ||
            !validate_assignment_tuple(PyTuple_GET_ITEM(code->co_consts,5),keys,3)) return false;
    }
    // Compiler metadata only, source-verified without executing provider bodies.
#if PY_VERSION_HEX >= 0x30d0000 && PY_VERSION_HEX < 0x30e0000
    constexpr unsigned char expected0[]={149,0,85,0,82,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,83,1,53,1,0,0,0,0,0,0,40,0,0,0,0,0,0,0,43,0,36,0};
    constexpr unsigned char expected1[]={149,0,85,0,82,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,82,3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,83,1,53,1,0,0,0,0,0,0,40,0,0,0,0,0,0,0,97,3,0,0,83,2,110,3,79,47,91,5,0,0,0,0,0,0,0,0,85,0,82,6,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,82,3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,85,1,53,1,0,0,0,0,0,0,83,1,83,3,53,3,0,0,0,0,0,0,40,0,0,0,0,0,0,0,97,3,0,0,83,4,110,3,79,1,103,0,91,8,0,0,0,0,0,0,0,0,82,10,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,34,0,85,0,82,12,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,88,49,52,1,85,2,83,5,46,3,47,1,53,2,0,0,0,0,0,0,101,1};
#elif PY_VERSION_HEX >= 0x30c0000 && PY_VERSION_HEX < 0x30d0000
    constexpr unsigned char expected0[]={151,0,124,0,106,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,100,1,171,1,0,0,0,0,0,0,12,0,83,0};
    constexpr unsigned char expected1[]={151,0,124,0,106,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,106,3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,100,1,171,1,0,0,0,0,0,0,114,3,100,2,125,3,110,42,116,5,0,0,0,0,0,0,0,0,124,0,106,6,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,106,3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,124,1,171,1,0,0,0,0,0,0,100,1,100,3,171,3,0,0,0,0,0,0,114,3,100,4,125,3,110,1,121,0,116,9,0,0,0,0,0,0,0,0,106,10,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,124,0,106,12,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,124,3,124,1,102,1,124,2,100,5,156,3,103,1,171,2,0,0,0,0,0,0,130,1};
#elif PY_VERSION_HEX >= 0x30b0000 && PY_VERSION_HEX < 0x30c0000
    constexpr unsigned char expected0[]={151,0,124,0,160,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,100,1,166,1,0,0,171,1,0,0,0,0,0,0,0,0,12,0,83,0};
    constexpr unsigned char expected1[]={151,0,124,0,106,0,0,0,0,0,0,0,0,0,160,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,100,1,166,1,0,0,171,1,0,0,0,0,0,0,0,0,114,3,100,2,125,3,110,46,116,5,0,0,0,0,0,0,0,0,0,0,124,0,106,3,0,0,0,0,0,0,0,0,160,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,124,1,166,1,0,0,171,1,0,0,0,0,0,0,0,0,100,1,100,3,166,3,0,0,171,3,0,0,0,0,0,0,0,0,114,3,100,4,125,3,110,2,100,0,83,0,116,9,0,0,0,0,0,0,0,0,0,0,106,5,0,0,0,0,0,0,0,0,124,0,106,6,0,0,0,0,0,0,0,0,124,3,124,1,102,1,124,2,100,5,156,3,103,1,166,2,0,0,171,2,0,0,0,0,0,0,0,0,130,1};
#else
    return false; // Unknown compiler metadata/native ownership remains OPEN.
#endif
#if PY_VERSION_HEX >= 0x030B0000 && PY_VERSION_HEX < 0x030E0000
    const unsigned char* expected[]={expected0,expected1};
    constexpr size_t sizes[]={sizeof(expected0),sizeof(expected1)};
    nb::object instructions=checked(PyCode_GetCode(reinterpret_cast<PyCodeObject*>(raw)));
    return PyBytes_CheckExact(instructions.ptr()) && PyBytes_GET_SIZE(instructions.ptr())==sizes[entry] &&
        std::memcmp(PyBytes_AS_STRING(instructions.ptr()),expected[entry],sizes[entry])==0;
#endif
}
bool selector_policy_declaration_unchanged(const ThemeLifecycleBinding& b, unsigned entry,
                                         PyObject* function) {
    if (entry>=2 || !PyFunction_Check(function) || !b.policy_codes[entry].is_valid() ||
        !b.policy_globals[entry].is_valid() || !b.policy_builtins[entry].is_valid() ||
        PyFunction_GetCode(function)!=b.policy_codes[entry].ptr() ||
        PyFunction_GetGlobals(function)!=b.policy_globals[entry].ptr() ||
        PyFunction_GetDefaults(function)!=nullptr || PyFunction_GetKwDefaults(function)!=nullptr ||
        PyFunction_GetClosure(function)!=nullptr) return false;
    const auto* declaration=reinterpret_cast<PyFunctionObject*>(function);
    return declaration->func_builtins==b.policy_builtins[entry].ptr() &&
        PyUnicode_CheckExact(declaration->func_name) &&
        PyUnicode_CompareWithASCIIString(declaration->func_name,selector_policy_names[entry])==0 &&
        PyUnicode_CheckExact(declaration->func_qualname) &&
        PyUnicode_CompareWithASCIIString(declaration->func_qualname,selector_policy_names[entry])==0;
}
void capture_selector_owned_policies(ThemeLifecycleBinding& b) {
    if (!PyDict_CheckExact(b.pydantic_globals.ptr())) return;
    PyObject* fields=PyDict_GetItemString(b.pydantic_globals.ptr(),"_fields");
    PyObject* namespaces[]={fields && PyModule_Check(fields)?PyModule_GetDict(fields):nullptr,
                            b.pydantic_globals.ptr()};
    for (unsigned entry=0; entry<2; ++entry) {
        PyObject* globals=namespaces[entry];
        if (!globals || !PyDict_CheckExact(globals)) continue;
        PyObject* function=PyDict_GetItemString(globals,selector_policy_names[entry]);
        if (!function || !PyFunction_Check(function)) continue;
        PyObject* code=PyFunction_GetCode(function);
        if (!selector_policy_code_reviewed(code,entry) || PyFunction_GetGlobals(function)!=globals) continue;
        const auto* declaration=reinterpret_cast<PyFunctionObject*>(function);
        if (!declaration->func_builtins) continue;
        b.policy_codes[entry]=nb::borrow<nb::object>(code);
        b.policy_globals[entry]=nb::borrow<nb::object>(globals);
        b.policy_builtins[entry]=nb::borrow<nb::object>(declaration->func_builtins);
        if (!selector_policy_declaration_unchanged(b,entry,function)) {
            b.policy_codes[entry].reset(); b.policy_globals[entry].reset(); b.policy_builtins[entry].reset();
            continue;
        }
        b.policy_handlers[entry]=nb::borrow<nb::object>(function);
    }
}

// Actual helper parameters, its assigned error_type, and original namespaces.
// No model/config/field copies; callbacks keep their genuine partial writes.
struct ThemeSelectorPolicyLocals {
    nb::object globals, builtins, name, model_cls, value, error_type;
    ThemeSelectorPolicyLocals() = default;
    ThemeSelectorPolicyLocals(ThemeSelectorPolicyLocals&&) noexcept = default;
    ThemeSelectorPolicyLocals(const ThemeSelectorPolicyLocals&) = delete;
    void clear() noexcept {
        auto old_model_cls=std::move(model_cls), old_name=std::move(name), old_value=std::move(value);
        auto old_error_type=std::move(error_type), old_globals=std::move(globals), old_builtins=std::move(builtins);
        old_model_cls.reset(); old_name.reset(); old_value.reset(); old_error_type.reset();
        old_globals.reset(); old_builtins.reset();
    }
    ~ThemeSelectorPolicyLocals() { clear(); }
};
int selector_policy_locals_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (!nb::inst_ready(self)) return 0;
    const auto& o=*nb::inst_ptr<ThemeSelectorPolicyLocals>(self);
    Py_VISIT(o.model_cls.ptr()); Py_VISIT(o.name.ptr()); Py_VISIT(o.value.ptr());
    Py_VISIT(o.error_type.ptr()); Py_VISIT(o.globals.ptr()); Py_VISIT(o.builtins.ptr());
    return 0;
}
int selector_policy_locals_clear(PyObject* self) {
    if (nb::inst_ready(self)) nb::inst_ptr<ThemeSelectorPolicyLocals>(self)->clear();
    return 0;
}
PyType_Slot selector_policy_locals_slots[]={
    {Py_tp_traverse,reinterpret_cast<void*>(selector_policy_locals_traverse)},
    {Py_tp_clear,reinterpret_cast<void*>(selector_policy_locals_clear)}, {0,nullptr}};
nb::object selector_policy_global(const ThemeSelectorPolicyLocals& o, const char* name) {
    nb::object key=checked(PyUnicode_InternFromString(name));
    PyObject* value=PyDict_GetItemWithError(o.globals.ptr(),key.ptr());
    if (value) return nb::borrow<nb::object>(value);
    if (PyErr_Occurred()) throw nb::python_error();
    if (PyDict_CheckExact(o.builtins.ptr())) {
        value=PyDict_GetItemWithError(o.builtins.ptr(),key.ptr());
        if (value) return nb::borrow<nb::object>(value);
        if (PyErr_Occurred()) throw nb::python_error();
    } else {
        value=PyObject_GetItem(o.builtins.ptr(),key.ptr());
        if (value) return nb::steal<nb::object>(value);
        if (!PyErr_ExceptionMatches(PyExc_KeyError)) throw nb::python_error();
        PyErr_Clear();
    }
    nb::object message=checked(PyUnicode_FromFormat("name '%s' is not defined",name));
    nb::object error=checked(PyObject_CallFunctionObjArgs(PyExc_NameError,message.ptr(),nullptr));
    if (PyObject_SetAttrString(error.ptr(),"name",key.ptr())<0) throw nb::python_error();
    PyErr_SetObject(PyExc_NameError,error.ptr()); throw nb::python_error();
}
[[noreturn]] void selector_raise(nb::object raised);
nb::object selector_policy_frame(nb::handle args, unsigned entry) {
    if (!PyTuple_CheckExact(args.ptr()) || entry>=2)
        throw nb::type_error("actual selector policy source parameters required");
    const Py_ssize_t count=PyTuple_GET_SIZE(args.ptr());
    if (count==(entry==0?3:5)) {
        if (!PyDict_CheckExact(PyTuple_GET_ITEM(args.ptr(),0)))
            throw nb::type_error("actual selector policy source globals required");
        nb::object result=nb::cast(ThemeSelectorPolicyLocals{});
        auto& o=*nb::inst_ptr<ThemeSelectorPolicyLocals>(result);
        o.globals=nb::borrow<nb::object>(PyTuple_GET_ITEM(args.ptr(),0));
        o.builtins=nb::borrow<nb::object>(PyTuple_GET_ITEM(args.ptr(),1));
        if (entry==0) o.name=nb::borrow<nb::object>(PyTuple_GET_ITEM(args.ptr(),2));
        else {
            o.model_cls=nb::borrow<nb::object>(PyTuple_GET_ITEM(args.ptr(),2));
            o.name=nb::borrow<nb::object>(PyTuple_GET_ITEM(args.ptr(),3));
            o.value=nb::borrow<nb::object>(PyTuple_GET_ITEM(args.ptr(),4));
        }
        return result;
    }
    if (count!=1 || !nb::isinstance<ThemeSelectorPolicyLocals>(nb::handle(PyTuple_GET_ITEM(args.ptr(),0))) ||
        !nb::inst_ready(PyTuple_GET_ITEM(args.ptr(),0)))
        throw nb::type_error("actual selector policy source owner required");
    auto& o=*nb::inst_ptr<ThemeSelectorPolicyLocals>(PyTuple_GET_ITEM(args.ptr(),0));
    if (!o.globals.is_valid() || !o.builtins.is_valid() || !o.name.is_valid() ||
        (entry==1 && (!o.model_cls.is_valid() || !o.value.is_valid())))
        throw nb::type_error("selector policy source owner was cleared");
    if (entry==0) {
        nb::object answer=life_call(life_attr(o.name,"startswith"),
            {[&]()->nb::object{return checked(PyUnicode_InternFromString("_"));}});
        return nb::bool_(!life_truth(std::move(answer)));
    }
    if (life_truth(life_call(life_attr(life_attr(o.model_cls,"model_config"),"get"),
        {[&]()->nb::object{return checked(PyUnicode_InternFromString("frozen"));}}))) {
        life_assign(o.error_type,checked(PyUnicode_InternFromString("frozen_instance")));
    } else {
        // Python resolves the getattr callable BEFORE evaluating its arguments.
        nb::object getter=selector_policy_global(o,"getattr");
        nb::object field=life_call(life_attr(life_attr(o.model_cls,"__pydantic_fields__"),"get"),{LE(o.name)});
        nb::object frozen=life_call(std::move(getter),
            {[&]()->nb::object{return std::move(field);},
             [&]()->nb::object{return checked(PyUnicode_InternFromString("frozen"));},
             [&]()->nb::object{return nb::bool_(false);}});
        if (!life_truth(std::move(frozen))) return nb::none();
        life_assign(o.error_type,checked(PyUnicode_InternFromString("frozen_field")));
    }
    // Constructor attribute resolution precedes the title and error payload.
    nb::object constructor=life_attr(selector_policy_global(o,"ValidationError"),"from_exception_data");
    nb::object raised=life_call(std::move(constructor),
        {[&]()->nb::object{return life_attr(o.model_cls,"__name__");}, [&]()->nb::object {
            nb::object error_type=o.error_type;
            nb::object location=checked(PyTuple_Pack(1,o.name.ptr()));
            nb::object input=o.value;
            nb::object keys=checked(PyTuple_New(3));
            constexpr const char* words[]={"type","loc","input"};
            for (unsigned index=0; index<3; ++index) {
                nb::object key=checked(PyUnicode_InternFromString(words[index]));
                PyTuple_SET_ITEM(keys.ptr(),index,key.release().ptr());
            }
            nb::object details=checked(PyDict_New());
            PyObject* values[]={error_type.ptr(),location.ptr(),input.ptr()};
            for (unsigned index=0; index<3; ++index)
                if (PyDict_SetItem(details.ptr(),PyTuple_GET_ITEM(keys.ptr(),index),values[index])<0)
                    throw nb::python_error();
            // BUILD_CONST_KEY_MAP consumes its values and constant key tuple.
            error_type.reset(); location.reset(); input.reset(); keys.reset();
            nb::object errors=checked(PyList_New(1));
            PyList_SET_ITEM(errors.ptr(),0,details.release().ptr());
            return errors;
        }});
    selector_raise(std::move(raised));
}
template<unsigned Entry> PyObject* selector_policy_frame_callback(PyObject*, PyObject* args) {
    try { return selector_policy_frame(nb::handle(args),Entry).release().ptr(); }
    catch (nb::python_error& error) { error.restore(); }
    catch (const nb::builtin_exception& error) {
        PyErr_SetString(error.type()==nb::exception_type::type_error?PyExc_TypeError:PyExc_RuntimeError,error.what());
    }
    catch (const std::bad_alloc&) { PyErr_NoMemory(); }
    catch (const std::exception& error) { PyErr_SetString(PyExc_RuntimeError,error.what()); }
    catch (...) { PyErr_SetString(PyExc_SystemError,"untranslated Theme owned selector policy exception"); }
    return nullptr;
}
PyMethodDef selector_policy_frame_definitions[]={
    {"theme_field_name_policy_frame",selector_policy_frame_callback<0>,METH_VARARGS,"Source-local/native owned field-name policy."},
    {"theme_frozen_policy_frame",selector_policy_frame_callback<1>,METH_VARARGS,"Source-local/native owned frozen policy."}};
void bind_selector_owned_policies(ThemeLifecycleBinding& b, nb::handle factory) {
    if (!PyFunction_Check(factory.ptr())) throw nb::type_error("selector policy declaration factory required");
    for (unsigned entry=0; entry<2; ++entry) {
        if (b.policy_providers[entry].is_valid()) continue; // Idempotent real Theme reload.
        if (!b.policy_handlers[entry].is_valid()) continue;
        if (!selector_policy_declaration_unchanged(b,entry,b.policy_handlers[entry].ptr())) continue;
        if (!b.policy_native_frames[entry].is_valid())
            throw nb::type_error("selector policy native frame metadata is missing");
        nb::object provider=life_call(nb::borrow<nb::object>(factory),
            {[&]()->nb::object{return b.policy_codes[entry];},
             [&]()->nb::object{return b.policy_native_frames[entry];},
             [&]()->nb::object{return b.policy_globals[entry];},
             [&]()->nb::object{return b.policy_builtins[entry];}});
        PyObject* closure=PyFunction_Check(provider.ptr())?PyFunction_GetClosure(provider.ptr()):nullptr;
        if (!PyFunction_Check(provider.ptr()) || PyFunction_GetGlobals(provider.ptr())!=b.policy_globals[entry].ptr() ||
            !closure || !PyTuple_CheckExact(closure) || PyTuple_GET_SIZE(closure)!=2 ||
            !PyCell_Check(PyTuple_GET_ITEM(closure,0)) || !PyCell_Check(PyTuple_GET_ITEM(closure,1)) ||
            PyCell_GET(PyTuple_GET_ITEM(closure,0))!=b.policy_builtins[entry].ptr() ||
            PyCell_GET(PyTuple_GET_ITEM(closure,1))!=b.policy_globals[entry].ptr())
            throw nb::type_error("actual namespace-only selector policy declaration frame required");
        b.policy_providers[entry]=std::move(provider);
    }
}
nb::object selector_policy_call(ThemeLifecycleBinding& b, unsigned entry, nb::object target,
                                std::initializer_list<LifeExpr> arguments) {
    if (entry>=2) throw nb::value_error("unknown owned selector policy");
    if (!b.policy_handlers[entry].is_valid() || target.ptr()!=b.policy_handlers[entry].ptr())
        return life_call(std::move(target),arguments);
    if (!b.policy_codes[entry].is_valid() || !b.policy_globals[entry].is_valid() ||
        !b.policy_builtins[entry].is_valid())
        throw nb::type_error("selector policy declaration metadata is missing");
    if (!selector_policy_declaration_unchanged(b,entry,target.ptr()))
        return life_call(std::move(target),arguments);
    if (!b.policy_providers[entry].is_valid() || !b.policy_native_frames[entry].is_valid())
        throw nb::type_error("selector policy declaration metadata is missing");
    // Namespace VALUES remain live; only foreign/function mutation is a callback.
    try {
        nb::object result=life_call(b.policy_providers[entry],arguments);
        target.reset(); return result;
    } catch (...) { target.reset(); throw; }
}

nb::object selector_handler(ThemeLifecycleBinding& b, unsigned literal) {
    nb::object mapping=selector_source_global(b,"_SIMPLE_SETATTR_HANDLERS");
    nb::object key=selector_literal(b,literal);
    PyObject* value=PyObject_GetItem(mapping.ptr(),key.ptr());
    mapping.reset(); key.reset(); return checked(value);
}
[[noreturn]] void selector_raise(nb::object raised) {
    if (PyExceptionClass_Check(raised.ptr())) PyErr_SetNone(raised.ptr());
    else if (PyExceptionInstance_Check(raised.ptr()))
        PyErr_SetObject(PyExceptionInstance_Class(raised.ptr()),raised.ptr());
    else PyErr_SetString(PyExc_TypeError,"exceptions must derive from BaseException");
    throw nb::python_error();
}
nb::object descriptor_frame(nb::handle args) {
    const Py_ssize_t size=PyTuple_GET_SIZE(args.ptr());
    if (size==4) {
        nb::object result=nb::cast(ThemeDescriptorLocals{});
        auto& o=*nb::inst_ptr<ThemeDescriptorLocals>(result);
        o.attribute=nb::borrow<nb::object>(PyTuple_GET_ITEM(args.ptr(),0));
        o.model=nb::borrow<nb::object>(PyTuple_GET_ITEM(args.ptr(),1));
        o.name=nb::borrow<nb::object>(PyTuple_GET_ITEM(args.ptr(),2));
        o.val=nb::borrow<nb::object>(PyTuple_GET_ITEM(args.ptr(),3));
        return result;
    }
    PyObject* owner=size==1?PyTuple_GET_ITEM(args.ptr(),0):nullptr;
    if (!owner || !nb::isinstance<ThemeDescriptorLocals>(nb::handle(owner)) ||
        !nb::inst_ready(owner)) throw nb::type_error("original descriptor parameters or local owner required");
    auto& o=*nb::inst_ptr<ThemeDescriptorLocals>(owner);
    // Delayed lookup, actual descriptor receiver, exact model/value arguments.
    return life_call(life_attr(o.attribute,"__set__"),{LE(o.model),LE(o.val)});
}
// This constant callable has no application context or Python heap-type edge:
// m_self=NULL, m_module is an immutable name string, m_ml is static native code.
// Thus untracked CodeType constants never hide a binding/model/descriptor edge.
PyObject* descriptor_frame_callback(PyObject*, PyObject* args) {
    try { return descriptor_frame(nb::handle(args)).release().ptr(); }
    catch (nb::python_error& error) { error.restore(); }
    catch (const nb::builtin_exception& error) { PyErr_SetString(PyExc_TypeError,error.what()); }
    catch (const std::bad_alloc&) { PyErr_NoMemory(); }
    catch (const std::exception& error) { PyErr_SetString(PyExc_RuntimeError,error.what()); }
    catch (...) { PyErr_SetString(PyExc_SystemError,"untranslated Theme descriptor callback exception"); }
    return nullptr;
}
PyMethodDef descriptor_frame_definition={
    "theme_descriptor_frame",descriptor_frame_callback,METH_VARARGS,
    "Source-local transfer/native descriptor invocation without application context."};
nb::object selector_descriptor(ThemeLifecycleBinding& b, ThemeSelectorLocals& o, bool property) {
    const auto& metadata=*nb::inst_ptr<ThemeSelectorMetadata>(o.metadata);
    nb::object factory=property?metadata.property_factory:metadata.private_factory;
    nb::object native_frame=b.descriptor_frame;
    return life_call(std::move(factory),
        {[&]()->nb::object{return property?o.attr:o.attribute;},
         [&]()->nb::object{return selector_literal(b,property?15:14);},
         [&]()->nb::object{return std::move(native_frame);}});
}
nb::object selector_locals(ThemeLifecycleBinding& b, nb::handle self,
                           nb::handle name, nb::handle value) {
    nb::object metadata=selector_metadata(b,self);
    nb::object result=nb::cast(ThemeSelectorLocals{});
    auto& o=*nb::inst_ptr<ThemeSelectorLocals>(result);
    o.binding=nb::borrow<nb::object>(nb::find(&b)); o.metadata=std::move(metadata);
    o.self=nb::borrow<nb::object>(self); o.name=nb::borrow<nb::object>(name);
    o.value=nb::borrow<nb::object>(value);
    return result;
}
nb::object selector_run(ThemeLifecycleBinding& b, ThemeSelectorLocals& o) {
    if (o.binding.ptr()!=nb::find(&b).ptr())
        throw nb::type_error("selector local owner belongs to another binding");
    life_assign(o.cls,life_attr(o.self,"__class__"));
    if (life_contains(LE(o.name),[&]()->nb::object{return life_attr(o.cls,"__class_vars__");})) {
        nb::object exception=selector_source_global(b,"AttributeError");
        nb::object message=selector_join({
            [&]()->nb::object{return selector_format(o.name,true);},
            [&]()->nb::object{return selector_literal(b,8);},
            [&]()->nb::object{return selector_format(life_attr(o.cls,"__name__"));},
            [&]()->nb::object{return selector_literal(b,9);},
            [&]()->nb::object{return selector_format(life_attr(o.cls,"__name__"));},
            [&]()->nb::object{return selector_literal(b,10);},
            [&]()->nb::object{return selector_format(o.name);},
            [&]()->nb::object{return selector_literal(b,11);}});
        nb::object raised=life_call(std::move(exception),{[&]()->nb::object{return std::move(message);}});
        selector_raise(std::move(raised));
    }
    if (!life_truth(selector_policy_call(b,0,life_attr(selector_source_global(b,"_fields"),"is_valid_field_name"),{LE(o.name)}))) {
        life_assign(o.attribute,life_call(life_attr(life_attr(o.cls,"__private_attributes__"),"get"),{LE(o.name)}));
        if (!o.attribute.is_none()) {
            if (life_truth(life_call(selector_source_global(b,"hasattr"),
                {LE(o.attribute),[&]()->nb::object{return selector_literal(b,1);}})))
                return selector_descriptor(b,o,false);
            return selector_handler(b,0);
        }
        life_call(selector_source_global(b,"_object_setattr"),{LE(o.self),LE(o.name),LE(o.value)});
        return nb::none();
    }
    life_assign(o.attr,life_call(selector_source_global(b,"getattr"),
        {LE(o.cls),LE(o.name),[&]()->nb::object{return selector_source_global(b,"_ATTRIBUTE_MISSING");}}));
    if (life_truth(life_call(selector_source_global(b,"isinstance"),
        {LE(o.attr),[&]()->nb::object{return selector_source_global(b,"cached_property");}})))
        return selector_handler(b,2);
    selector_policy_call(b,1,selector_source_global(b,"_check_frozen"),{LE(o.cls),LE(o.name),LE(o.value)});
    if (life_truth(life_call(selector_source_global(b,"isinstance"),
        {LE(o.attr),[&]()->nb::object{return selector_source_global(b,"property");}})))
        return selector_descriptor(b,o,true);
    if (life_truth(life_call(life_attr(life_attr(o.cls,"model_config"),"get"),
        {[&]()->nb::object{return selector_literal(b,3);}}))) return selector_handler(b,3);
    if (!life_contains(LE(o.name),[&]()->nb::object{return life_attr(o.cls,"__pydantic_fields__");})) {
        nb::object extra=life_call(life_attr(life_attr(o.cls,"model_config"),"get"),
            {[&]()->nb::object{return selector_literal(b,4);}});
        nb::object allow=selector_literal(b,5);
        PyObject* comparison=PyObject_RichCompare(extra.ptr(),allow.ptr(),Py_NE);
        extra.reset(); allow.reset();
        if (life_truth(checked(comparison))) {
            nb::object exception=selector_source_global(b,"ValueError");
            nb::object message=selector_join({
                [&]()->nb::object{return selector_literal(b,12);},
                [&]()->nb::object{return selector_format(life_attr(o.cls,"__name__"));},
                [&]()->nb::object{return selector_literal(b,13);},
                [&]()->nb::object{return selector_format(o.name);},
                [&]()->nb::object{return selector_literal(b,12);}});
            nb::object raised=life_call(std::move(exception),{[&]()->nb::object{return std::move(message);}});
            selector_raise(std::move(raised));
        }
        nb::object missing=selector_source_global(b,"_ATTRIBUTE_MISSING");
        const bool absent=o.attr.ptr()==missing.ptr(); missing.reset();
        if (absent) {
            life_set_item(LE(o.value),[&]()->nb::object{return life_attr(o.self,"__pydantic_extra__");},LE(o.name));
            life_call(life_attr(life_attr(o.self,"__pydantic_fields_set__"),"add"),{LE(o.name)});
            return nb::none();
        }
        return selector_handler(b,6);
    }
    return selector_handler(b,7);
}
nb::object assignment_select(ThemeLifecycleBinding& b, ThemeAssignmentLocals& o) {
    nb::object target=life_attr(o.self,"_setattr_handler");
    const bool selected=b.base.is_valid() && b.base_selector.is_valid() &&
        PyObject_TypeCheck(o.self.ptr(),reinterpret_cast<PyTypeObject*>(b.base.ptr())) &&
        PyMethod_Check(target.ptr()) && PyMethod_GET_SELF(target.ptr())==o.self.ptr() &&
        PyMethod_GET_FUNCTION(target.ptr())==b.base_selector.ptr();
    if (!selected) return life_call(std::move(target),{LE(o.name),LE(o.value)});
    nb::object metadata=selector_metadata(b,o.self);
    const auto& declaration=*nb::inst_ptr<ThemeSelectorMetadata>(metadata);
    nb::object bound=checked(PyMethod_New(declaration.provider.ptr(),o.self.ptr()));
    try {
        nb::object result=life_call(std::move(bound),{LE(o.name),LE(o.value)});
        target.reset(); return result;
    } catch (...) { target.reset(); throw; }
}
void bind_selector(ThemeLifecycleBinding& b, nb::handle model_type, nb::handle provider,
                   nb::handle private_factory, nb::handle property_factory) {
    if (!PyType_Check(model_type.ptr()) || !b.base.is_valid() ||
        !PyType_IsSubtype(reinterpret_cast<PyTypeObject*>(model_type.ptr()),reinterpret_cast<PyTypeObject*>(b.base.ptr())) ||
        !PyFunction_Check(provider.ptr()) || !PyFunction_Check(private_factory.ptr()) ||
        !PyFunction_Check(property_factory.ptr()))
        throw nb::type_error("genuine Theme selector declaration metadata required");
    auto* type=reinterpret_cast<PyTypeObject*>(model_type.ptr());
    nb::object dictionary=assignment_type_dictionary(type);
    if (!dictionary.is_valid()) throw nb::type_error("Theme declaration dictionary is missing");
    if (PyDict_GetItemString(dictionary.ptr(),selector_metadata_name))
        throw nb::value_error("Theme selector declaration metadata is immutable");
    nb::object wrapped=checked(PyObject_GetAttrString(provider.ptr(),"__wrapped__"));
    if (wrapped.ptr()!=b.base_selector.ptr())
        throw nb::type_error("selector provider must retain genuine BaseModel metadata");
    nb::object metadata=nb::cast(ThemeSelectorMetadata{});
    auto& declaration=*nb::inst_ptr<ThemeSelectorMetadata>(metadata);
    declaration.binding=nb::borrow<nb::object>(nb::find(&b));
    declaration.provider=nb::borrow<nb::object>(provider);
    declaration.private_factory=nb::borrow<nb::object>(private_factory);
    declaration.property_factory=nb::borrow<nb::object>(property_factory);
    if (PyObject_SetAttrString(model_type.ptr(),selector_metadata_name,metadata.ptr())<0)
        throw nb::python_error();
}

void bind_assignment(ThemeLifecycleBinding& b, nb::handle model_type,
                     nb::handle provider) {
    if (!PyType_Check(model_type.ptr()) ||
        !PyType_IsSubtype(reinterpret_cast<PyTypeObject*>(model_type.ptr()),
                          reinterpret_cast<PyTypeObject*>(b.base.ptr())) ||
        !PyFunction_Check(provider.ptr()))
        throw nb::type_error("genuine Theme declaration and thin assignment function required");
    auto* type=reinterpret_cast<PyTypeObject*>(model_type.ptr());
    nb::object dictionary=assignment_type_dictionary(type);
    if (!dictionary.is_valid())
        throw nb::type_error("Theme declaration dictionary is missing");
    if (PyDict_GetItemString(dictionary.ptr(),assignment_metadata_name))
        throw nb::value_error("Theme assignment declaration metadata is immutable");
    nb::object source=checked(PyObject_GetAttrString(provider.ptr(),"__wrapped__"));
    if (source.ptr()!=b.base_setter.ptr())
        throw nb::type_error("assignment provider must retain the genuine BaseModel callable metadata");
    nb::object metadata=nb::cast(ThemeAssignmentMetadata{});
    auto& declaration=*nb::inst_ptr<ThemeAssignmentMetadata>(metadata);
    declaration.binding=nb::borrow<nb::object>(nb::find(&b));
    declaration.provider=nb::borrow<nb::object>(provider);
    if (PyObject_SetAttrString(model_type.ptr(),assignment_metadata_name,metadata.ptr())<0)
        throw nb::python_error();
}

#undef LE
#undef LG
#undef EG
#undef LA

PyObject* make_theme_state_bindings(PyObject* context, PyObject* args) {
    try {
        PyObject* base = PyTuple_GetItem(context, 0);
        PyObject* holder = PyTuple_GetItem(context, 1);
        PyObject* module_name = PyTuple_GetItem(context, 2);
        if (!base || !holder || !module_name) throw nb::python_error();
        if (!PyTuple_Check(args) || PyTuple_Size(args) != 4)
            throw nb::type_error("actual BaseModel, two source namespaces and compiled memo-name tuple required");
        PyObject* model_type = PyTuple_GetItem(args, 0);
        PyObject* pydantic_globals = PyTuple_GetItem(args, 1);
        PyObject* evented_globals = PyTuple_GetItem(args, 2);
        PyObject* memo_names = PyTuple_GetItem(args, 3);
        if (!PyType_Check(model_type) || !PyDict_Check(pydantic_globals) || !PyDict_Check(evented_globals) ||
            !PyTuple_CheckExact(memo_names) || PyTuple_Size(memo_names) != 1 ||
            !PyUnicode_CheckExact(PyTuple_GetItem(memo_names,0)) ||
            PyUnicode_CompareWithASCIIString(PyTuple_GetItem(memo_names,0),"memo") != 0)
            throw nb::type_error("genuine model/namespace and memo keyword metadata required");
        auto& journal = *nb::inst_ptr<PublicationJournal>(holder);
        if (journal.bound) throw nb::value_error("Theme state bindings are already created");
        nb::object declaration = checked(PyObject_GetAttrString(model_type, "__dict__"));
        std::array<nb::object,3> originals, replacements;
        for (unsigned index=0; index<3; ++index) {
            originals[index] = checked(PyMapping_GetItemString(declaration.ptr(),names[index]));
            if (!PyType_GetSlot(Py_TYPE(originals[index].ptr()),Py_tp_descr_get) ||
                !PyType_GetSlot(Py_TYPE(originals[index].ptr()),Py_tp_descr_set))
                throw nb::type_error("writable genuine BaseModel storage descriptor required");
            replacements[index] = nb::cast(PublicationSlot{nb::borrow<nb::object>(holder),index});
        }
        nb::object literal_names = life_source_literals(nb::handle(model_type));
        nb::object base_initializer = checked(PyObject_GetAttrString(model_type,"__init__"));
        nb::object constructor_literals = constructor_source_literals(base_initializer);
        nb::object namespace_ = checked(PyDict_New());
        nb::object empty_slots = checked(PyTuple_New(0));
        nb::object allocation_context = checked(PyTuple_Pack(3,base,holder,model_type));
        nb::object allocator = checked(PyCFunction_NewEx(&allocation_definition,allocation_context.ptr(),module_name));
        nb::object static_allocator = checked(PyStaticMethod_New(allocator.ptr()));
        if (PyDict_SetItemString(namespace_.ptr(),"__slots__",empty_slots.ptr())<0 ||
            PyDict_SetItemString(namespace_.ptr(),"__module__",module_name)<0 ||
            PyDict_SetItemString(namespace_.ptr(),"__new__",static_allocator.ptr())<0)
            throw nb::python_error();
        for (unsigned index=0; index<3; ++index)
            if (PyDict_SetItemString(namespace_.ptr(),names[index],replacements[index].ptr())<0)
                throw nb::python_error();
        nb::object type_name = checked(PyUnicode_FromString("ThemeNamespaceBase"));
        nb::object bases = checked(PyTuple_Pack(1,base));
        nb::object prepared = checked(PyObject_CallFunctionObjArgs(reinterpret_cast<PyObject*>(&PyType_Type),type_name.ptr(),bases.ptr(),namespace_.ptr(),nullptr));
        nb::object result = nb::cast(ThemeLifecycleBinding{});
        auto& binding = *nb::inst_ptr<ThemeLifecycleBinding>(result);
        binding.base = std::move(prepared); binding.allocator = std::move(allocator);
        binding.pydantic_globals = nb::borrow<nb::object>(pydantic_globals);
        binding.evented_globals = nb::borrow<nb::object>(evented_globals);
        binding.memo_names = nb::borrow<nb::object>(memo_names);
        binding.literal_names = std::move(literal_names);
        binding.constructor_literals = std::move(constructor_literals);
        binding.base_initializer = std::move(base_initializer);
        binding.base_setter = checked(PyObject_GetAttrString(model_type,"__setattr__"));
        binding.base_selector = checked(PyObject_GetAttrString(model_type,"_setattr_handler"));
        binding.selector_literals = selector_source_literals(binding.base_selector);
        binding.descriptor_frame = checked(PyCFunction_NewEx(&descriptor_frame_definition,nullptr,module_name));
        capture_validate_assignment(binding);
        binding.validate_assignment_native_frame = checked(PyCFunction_NewEx(&validate_assignment_frame_definition,nullptr,module_name));
        capture_simple_assignment_handlers(binding);
        for (unsigned entry=0; entry<4; ++entry)
            binding.simple_native_frames[entry]=checked(PyCFunction_NewEx(&simple_assignment_frame_definitions[entry],nullptr,module_name));
        capture_selector_owned_policies(binding);
        for (unsigned entry=0; entry<2; ++entry)
            binding.policy_native_frames[entry]=checked(PyCFunction_NewEx(&selector_policy_frame_definitions[entry],nullptr,module_name));
        journal.descriptors = std::move(originals); journal.bound = true;
        return result.release().ptr();
    } catch (nb::python_error& error) { error.restore(); }
      catch (const nb::builtin_exception& error) {
          PyObject* type = PyExc_RuntimeError;
          switch (error.type()) {
              case nb::exception_type::stop_iteration: type=PyExc_StopIteration; break;
              case nb::exception_type::index_error: type=PyExc_IndexError; break;
              case nb::exception_type::key_error: type=PyExc_KeyError; break;
              case nb::exception_type::value_error: type=PyExc_ValueError; break;
              case nb::exception_type::type_error: type=PyExc_TypeError; break;
              case nb::exception_type::buffer_error: type=PyExc_BufferError; break;
              case nb::exception_type::import_error: type=PyExc_ImportError; break;
              case nb::exception_type::attribute_error: type=PyExc_AttributeError; break;
              default: break;
          }
          PyErr_SetString(type,error.what());
      } catch (const std::bad_alloc&) { PyErr_NoMemory(); }
        catch (const std::exception& error) { PyErr_SetString(PyExc_RuntimeError,error.what()); }
        catch (...) { PyErr_SetString(PyExc_SystemError,"untranslated Theme binding exception"); }
    return nullptr;
}
PyMethodDef bindings_definition = {
    "make_theme_state_bindings", make_theme_state_bindings, METH_VARARGS,
    "Create module-free native lifecycle bindings over genuine physical stores."
};

// Production declaration metadata reuse only; no model/class/value authority.
struct ThemeBindingMetadata {
    nb::object binding, model_type;
    bool building = false;
    ThemeBindingMetadata() = default;
    ThemeBindingMetadata(ThemeBindingMetadata&&) noexcept = default;
    ThemeBindingMetadata(const ThemeBindingMetadata&) = delete;
    void clear() noexcept {
        auto old_binding = std::move(binding), old_type = std::move(model_type);
    }
    ~ThemeBindingMetadata() { clear(); }
};
int theme_metadata_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (!nb::inst_ready(self)) return 0;
    const auto& metadata = *nb::inst_ptr<ThemeBindingMetadata>(self);
    Py_VISIT(metadata.binding.ptr()); Py_VISIT(metadata.model_type.ptr());
    return 0;
}
int theme_metadata_clear(PyObject* self) {
    if (nb::inst_ready(self)) nb::inst_ptr<ThemeBindingMetadata>(self)->clear();
    return 0;
}
PyType_Slot theme_metadata_slots[] = {
    {Py_tp_traverse,reinterpret_cast<void*>(theme_metadata_traverse)},
    {Py_tp_clear,reinterpret_cast<void*>(theme_metadata_clear)}, {0,nullptr}};
PyObject* get_theme_state_bindings(PyObject* context, PyObject* args) {
    try {
        PyObject* factory = PyTuple_GetItem(context,0);
        PyObject* holder = PyTuple_GetItem(context,1);
        if (!factory || !holder) throw nb::python_error();
        if (!nb::inst_ready(holder)) throw nb::type_error("Theme binding metadata was cleared");
        if (!PyTuple_Check(args) || PyTuple_Size(args) != 4)
            throw nb::type_error("actual BaseModel, two source namespaces and compiled memo-name tuple required");
        PyObject* model_type = PyTuple_GetItem(args,0);
        PyObject* pydantic_globals = PyTuple_GetItem(args,1);
        PyObject* evented_globals = PyTuple_GetItem(args,2);
        PyObject* memo_names = PyTuple_GetItem(args,3);
        if (!PyType_Check(model_type) || !PyDict_Check(pydantic_globals) || !PyDict_Check(evented_globals) ||
            !PyTuple_CheckExact(memo_names) || PyTuple_Size(memo_names) != 1 ||
            !PyUnicode_CheckExact(PyTuple_GetItem(memo_names,0)) ||
            PyUnicode_CompareWithASCIIString(PyTuple_GetItem(memo_names,0),"memo") != 0)
            throw nb::type_error("genuine model/namespace and memo keyword metadata required");
        auto& metadata = *nb::inst_ptr<ThemeBindingMetadata>(holder);
        if (metadata.binding.is_valid()) {
            const auto& binding = *nb::inst_ptr<ThemeLifecycleBinding>(metadata.binding);
            if (metadata.model_type.ptr() != model_type ||
                binding.pydantic_globals.ptr() != pydantic_globals ||
                binding.evented_globals.ptr() != evented_globals)
                throw nb::type_error("Theme binding metadata belongs to another provider generation");
            return nb::borrow<nb::object>(metadata.binding).release().ptr();
        }
        if (metadata.building) {
            PyErr_SetString(PyExc_RuntimeError,"Theme binding metadata initialization is already active");
            throw nb::python_error();
        }
        metadata.building = true;
        nb::object result;
        try {
            result = checked(PyObject_CallObject(factory,args));
            metadata.model_type = nb::borrow<nb::object>(model_type);
            metadata.binding = result;
        } catch (...) { metadata.building = false; throw; }
        metadata.building = false;
        return result.release().ptr();
    } catch (nb::python_error& error) { error.restore(); }
      catch (const nb::builtin_exception& error) {
          PyObject* type = PyExc_RuntimeError;
          switch (error.type()) {
              case nb::exception_type::stop_iteration: type=PyExc_StopIteration; break;
              case nb::exception_type::index_error: type=PyExc_IndexError; break;
              case nb::exception_type::key_error: type=PyExc_KeyError; break;
              case nb::exception_type::value_error: type=PyExc_ValueError; break;
              case nb::exception_type::type_error: type=PyExc_TypeError; break;
              case nb::exception_type::buffer_error: type=PyExc_BufferError; break;
              case nb::exception_type::import_error: type=PyExc_ImportError; break;
              case nb::exception_type::attribute_error: type=PyExc_AttributeError; break;
              default: break;
          }
          PyErr_SetString(type,error.what());
      } catch (const std::bad_alloc&) { PyErr_NoMemory(); }
        catch (const std::exception& error) { PyErr_SetString(PyExc_RuntimeError,error.what()); }
        catch (...) { PyErr_SetString(PyExc_SystemError,"untranslated Theme metadata exception"); }
    return nullptr;
}
PyMethodDef theme_metadata_definition = {
    "theme_state_bindings",get_theme_state_bindings,METH_VARARGS,
    "Reuse immutable native Theme declaration metadata for the same providers."
};

PyObject* snapshot(PyObject* context, PyObject* self) {
    try {
        PyObject* base = PyTuple_GetItem(context, 0);
        PyObject* holder = PyTuple_GetItem(context, 1);
        if (!base || !holder) throw nb::python_error();
        if (!PyObject_TypeCheck(self, reinterpret_cast<PyTypeObject*>(base))) {
            PyErr_SetString(PyExc_TypeError, "theme_storage_snapshot requires a ThemeNamespaceBase instance");
            throw nb::python_error();
        }
        auto& journal = *nb::inst_ptr<PublicationJournal>(holder);
        const auto& entry = allocated_entry(journal, self);
        nb::object result = checked(PyDict_New());
        if (PyDict_SetItemString(result.ptr(), "owner", self) < 0) throw nb::python_error();
        constexpr const char* keys[] = {"dictionary", "fields_set", "extra"};
        constexpr const char* flags[] = {"dictionary_initialized", "fields_set_initialized", "extra_initialized"};
        for (unsigned index = 0; index < 3; ++index) {
            nb::object value = nb::steal<nb::object>(read_slot(journal, index, self));
            bool initialized = value.is_valid();
            if (!initialized) {
                if (!PyErr_ExceptionMatches(PyExc_AttributeError)) throw nb::python_error();
                PyErr_Clear();
                value = nb::none();
            }
            if (PyDict_SetItemString(result.ptr(), keys[index], value.ptr()) < 0 ||
                PyDict_SetItemString(result.ptr(), flags[index], initialized ? Py_True : Py_False) < 0)
                throw nb::python_error();
        }
        nb::object counts = checked(Py_BuildValue("(KKK)", entry.counts[0], entry.counts[1], entry.counts[2]));
        if (PyDict_SetItemString(result.ptr(), "publication_counts", counts.ptr()) < 0)
            throw nb::python_error();
        return result.release().ptr();
    } catch (nb::python_error& error) { error.restore(); }
      catch (const std::bad_alloc&) { PyErr_NoMemory(); }
    return nullptr;
}
PyMethodDef snapshot_definition = {
    "theme_storage_snapshot", snapshot, METH_O,
    "Read genuine existing slots and native publication counts without value adoption."
};
PyType_Slot namespace_slots[] = {
    {Py_tp_new, reinterpret_cast<void*>(namespace_new)}, {0, nullptr}
};
int slot_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) Py_VISIT(nb::inst_ptr<PublicationSlot>(self)->journal.ptr());
    return 0;
}
int slot_clear(PyObject* self) {
    if (nb::inst_ready(self)) nb::inst_ptr<PublicationSlot>(self)->journal.reset();
    return 0;
}
PyType_Slot descriptor_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(slot_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(slot_clear)}, {0, nullptr}
};
PublicationJournal& slot_journal(PublicationSlot& slot) {
    if (!slot.journal.is_valid() || !nb::inst_ready(slot.journal)) {
        PyErr_SetString(PyExc_RuntimeError, "Theme publication descriptor was cleared");
        throw nb::python_error();
    }
    auto& journal = *nb::inst_ptr<PublicationJournal>(slot.journal);
    if (!journal.bound) {
        PyErr_SetString(PyExc_RuntimeError, "Theme publication metadata is not bound");
        throw nb::python_error();
    }
    return journal;
}
} // namespace


// Shared routing has already resolved its genuine public super target. Detect
// actual registered native Theme allocations through the existing weak journal;
// no model hooks, new registry, field values or publication counters are used.
bool theme_assignment_dispatch(nb::handle model, nb::handle target,
                               nb::handle name, nb::handle value) {
    PyObject* mro=Py_TYPE(model.ptr())->tp_mro;
    if (!mro || !PyTuple_CheckExact(mro)) return false;
    PyObject* raw=nullptr;
    bool recognized=false;
    for (Py_ssize_t index=0; index<PyTuple_GET_SIZE(mro); ++index) {
        auto* type=reinterpret_cast<PyTypeObject*>(PyTuple_GET_ITEM(mro,index));
        nb::object dictionary=assignment_type_dictionary(type);
        if (!dictionary.is_valid()) continue;
        if (!raw) {
            raw=PyDict_GetItemString(dictionary.ptr(),assignment_metadata_name);
            if (PyErr_Occurred()) throw nb::python_error();
        }
        PyObject* journal_object=PyDict_GetItemString(dictionary.ptr(),metadata_name);
        if (PyErr_Occurred()) throw nb::python_error();
        if (!journal_object ||
            !nb::isinstance<PublicationJournal>(nb::handle(journal_object)) ||
            !nb::inst_ready(journal_object)) continue;
        const auto& journal=*nb::inst_ptr<PublicationJournal>(journal_object);
        auto found=journal.entries.find(model.ptr());
        if (found==journal.entries.end() || !found->second.weakref.is_valid()) continue;
        PyObject* referent=PyWeakref_GetObject(found->second.weakref.ptr());
        if (!referent) throw nb::python_error();
        if (referent==model.ptr()) recognized=true;
    }
    // Ordinary undeclared Evented/Base models keep their actual public route.
    if (!recognized) return false;
    if (!raw || !nb::isinstance<ThemeAssignmentMetadata>(nb::handle(raw)) ||
        !nb::inst_ready(raw))
        throw nb::type_error("Theme assignment declaration metadata is missing or replaced");
    nb::object metadata=nb::borrow<nb::object>(raw);
    const auto& declaration=*nb::inst_ptr<ThemeAssignmentMetadata>(metadata);
    if (!declaration.binding.is_valid() || !declaration.provider.is_valid() ||
        !nb::inst_ready(declaration.binding))
        throw nb::type_error("Theme assignment declaration metadata was cleared");
    auto& b=*nb::inst_ptr<ThemeLifecycleBinding>(declaration.binding);
    if (!b.base.is_valid() || !b.base_setter.is_valid())
        throw nb::type_error("Theme assignment binding metadata was cleared");
    if (!PyObject_TypeCheck(model.ptr(),reinterpret_cast<PyTypeObject*>(b.base.ptr())))
        throw nb::type_error("Theme assignment declaration belongs to another native binding");
    // Only genuinely different callbacks bypass the finite native default body.
    if (!PyMethod_Check(target.ptr()) || PyMethod_GET_SELF(target.ptr())!=model.ptr() ||
        PyMethod_GET_FUNCTION(target.ptr())!=b.base_setter.ptr()) return false;
    nb::object bound=checked(PyMethod_New(declaration.provider.ptr(),model.ptr()));
    life_call(std::move(bound),{[&]()->nb::object{return nb::borrow<nb::object>(name);},
                                [&]()->nb::object{return nb::borrow<nb::object>(value);}});
    return true;
}

void bind_theme_state(nb::module_& module) {
    nb::class_<ThemeBindingMetadata>(module,"_ThemeBindingMetadata",nb::type_slots(theme_metadata_slots),nb::is_weak_referenceable());
    nb::class_<PublicationJournal>(module, "_PublicationJournal", nb::type_slots(journal_slots), nb::is_weak_referenceable());
    nb::class_<PublicationSlot>(module, "_PublicationSlot", nb::type_slots(descriptor_slots), nb::is_weak_referenceable())
        .def("__get__", [](PublicationSlot& slot, nb::handle model, nb::handle) -> nb::object {
            if (model.is_none()) return nb::borrow<nb::object>(nb::find(&slot));
            return checked(read_slot(slot_journal(slot), slot.index, model.ptr()));
        }, nb::arg("model").none(), nb::arg("owner").none())
        .def("__set__", [](PublicationSlot& slot, nb::handle model, nb::handle value) {
            if (publish_slot(slot_journal(slot), slot.index, model.ptr(), value.ptr()) < 0)
                throw nb::python_error();
        }, nb::arg("model"), nb::arg("value").none())
        .def("__delete__", [](PublicationSlot& slot, nb::handle model) {
            if (publish_slot(slot_journal(slot), slot.index, model.ptr(), nullptr) < 0)
                throw nb::python_error();
        }, nb::arg("model"));
    nb::class_<ThemeLifecycleBinding>(module,"ThemeLifecycleBinding",nb::type_slots(lifecycle_binding_slots),nb::is_weak_referenceable())
        .def_prop_ro("base",[](const ThemeLifecycleBinding& b){return b.base;})
        .def_prop_ro("allocator",[](const ThemeLifecycleBinding& b){return b.allocator;})
        .def_prop_ro("literal_names",[](const ThemeLifecycleBinding& b){return b.literal_names;})
        .def("locals",&lifecycle_locals)
        .def("run",&lifecycle_run)
        .def_prop_ro("constructor_literals",[](const ThemeLifecycleBinding& b){return b.constructor_literals;})
        .def("init_locals",&constructor_locals,nb::arg("entry"),nb::arg("parameters"))
        .def("init_run",&constructor_run,nb::arg("locals_owner"))
        .def("assignment_locals",&assignment_locals,nb::arg("self"),nb::arg("name"),nb::arg("value").none())
        .def("assignment_run",&assignment_run,nb::arg("locals_owner"))
        .def("bind_assignment",&bind_assignment,nb::arg("model_type"),nb::arg("provider"))
        .def("selector_locals",&selector_locals,nb::arg("self"),nb::arg("name"),nb::arg("value").none())
        .def("selector_run",&selector_run,nb::arg("locals_owner"))
        .def_prop_ro("descriptor_frame",[](const ThemeLifecycleBinding& b){return b.descriptor_frame;})
        .def("bind_selector",&bind_selector,nb::arg("model_type"),nb::arg("provider"),
             nb::arg("private_factory"),nb::arg("property_factory"))
        .def("bind_validate_assignment",&bind_validate_assignment,nb::arg("factory"))
        .def("bind_simple_assignment_handlers",&bind_simple_assignment_handlers,nb::arg("factory"))
        .def("bind_selector_owned_policies",&bind_selector_owned_policies,nb::arg("factory"));
    auto source_locals = nb::class_<ThemeLifecycleLocals>(module,"ThemeLifecycleLocals",nb::type_slots(lifecycle_locals_slots),nb::is_weak_referenceable());
#define LIFE_PROP(name,field) source_locals.def_prop_ro(name,[](const ThemeLifecycleLocals& o){return o.field.is_valid()?o.field:nb::none();});
    LIFE_PROP("self",self) LIFE_PROP("memo",memo) LIFE_PROP("cls",cls) LIFE_PROP("m",m)
    LIFE_PROP("copied",copied) LIFE_PROP("private",private_value) LIFE_PROP("k",k) LIFE_PROP("v",v)
    LIFE_PROP("update",update) LIFE_PROP("deep",deep) LIFE_PROP("state",state)
#undef LIFE_PROP
    nb::class_<ThemeConstructorLocals>(module,"ThemeConstructorLocals",nb::type_slots(constructor_locals_slots),nb::is_weak_referenceable())
#define CTOR_PROP(name,field) .def_prop_ro(name,[](const ThemeConstructorLocals& o){return o.field.is_valid()?o.field:nb::none();})
        CTOR_PROP("self",self) CTOR_PROP("kwargs",kwargs) CTOR_PROP("data",data)
        CTOR_PROP("tracebackhide",tracebackhide) CTOR_PROP("validated_self",validated_self);
#undef CTOR_PROP
    nb::class_<ThemeAssignmentMetadata>(module,"_ThemeAssignmentMetadata",nb::type_slots(assignment_metadata_slots),nb::is_weak_referenceable());
    nb::class_<ThemeAssignmentLocals>(module,"ThemeAssignmentLocals",nb::type_slots(assignment_locals_slots),nb::is_weak_referenceable())
#define ASSIGN_PROP(name,field) .def_prop_ro(name,[](const ThemeAssignmentLocals& o){return o.field.is_valid()?o.field:nb::none();})
        ASSIGN_PROP("self",self) ASSIGN_PROP("name",name)
        ASSIGN_PROP("value",value) ASSIGN_PROP("setattr_handler",setattr_handler);
#undef ASSIGN_PROP
    nb::class_<ThemeSelectorMetadata>(module,"_ThemeSelectorMetadata",nb::type_slots(selector_metadata_slots),nb::is_weak_referenceable());
    auto selector_owner=nb::class_<ThemeSelectorLocals>(module,"ThemeSelectorLocals",nb::type_slots(selector_locals_slots),nb::is_weak_referenceable());
#define SELECT_PROP(name,field) selector_owner.def_prop_ro(name,[](const ThemeSelectorLocals& o){return o.field.is_valid()?o.field:nb::none();});
    SELECT_PROP("self",self) SELECT_PROP("name",name) SELECT_PROP("value",value)
    SELECT_PROP("cls",cls) SELECT_PROP("attr",attr) SELECT_PROP("attribute",attribute)
#undef SELECT_PROP
    auto descriptor_owner=nb::class_<ThemeDescriptorLocals>(module,"ThemeDescriptorLocals",nb::type_slots(descriptor_locals_slots),nb::is_weak_referenceable());
#define DESCR_PROP(name,field) descriptor_owner.def_prop_ro(name,[](const ThemeDescriptorLocals& o){return o.field.is_valid()?o.field:nb::none();});
    DESCR_PROP("model",model) DESCR_PROP("_name",name) DESCR_PROP("val",val)
    DESCR_PROP("attribute",attribute)
#undef DESCR_PROP
    auto validate_owner=nb::class_<ThemeValidateAssignmentLocals>(module,"ThemeValidateAssignmentLocals",nb::type_slots(validate_assignment_locals_slots),nb::is_weak_referenceable());
#define VALIDATE_PROP(name,field) validate_owner.def_prop_ro(name,[](const ThemeValidateAssignmentLocals& o){return o.field.is_valid()?o.field:nb::none();});
    VALIDATE_PROP("model",model) VALIDATE_PROP("name",name) VALIDATE_PROP("val",val)
#undef VALIDATE_PROP
    auto simple_owner=nb::class_<ThemeSimpleAssignmentLocals>(module,"ThemeSimpleAssignmentLocals",nb::type_slots(simple_assignment_locals_slots),nb::is_weak_referenceable());
#define SIMPLE_PROP(name,field) simple_owner.def_prop_ro(name,[](const ThemeSimpleAssignmentLocals& o){return o.field.is_valid()?o.field:nb::none();});
    SIMPLE_PROP("model",model) SIMPLE_PROP("name",name) SIMPLE_PROP("val",val)
#undef SIMPLE_PROP
    auto policy_owner=nb::class_<ThemeSelectorPolicyLocals>(module,"ThemeSelectorPolicyLocals",nb::type_slots(selector_policy_locals_slots),nb::is_weak_referenceable());
#define POLICY_PROP(name,field) policy_owner.def_prop_ro(name,[](const ThemeSelectorPolicyLocals& o){return o.field.is_valid()?o.field:nb::none();});
    POLICY_PROP("name",name) POLICY_PROP("model_cls",model_cls)
    POLICY_PROP("value",value) POLICY_PROP("error_type",error_type)
#undef POLICY_PROP
    nb::object holder = nb::cast(PublicationJournal{});
    PyType_Spec spec = {
        "naparixx._native._ThemeAllocationBase",
        static_cast<int>(sizeof(PyObject)), 0,
        Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE, namespace_slots
    };
    nb::object base = checked(PyType_FromSpec(&spec));
    if (PyObject_SetAttrString(base.ptr(), metadata_name, holder.ptr()) < 0)
        throw nb::python_error();
    module.attr("_ThemeAllocationBase") = base;
    // No callable/context owns its publication module. This immutable module
    // name string is declaration metadata, not a strong module back-edge.
    nb::object module_name = module.attr("__name__");
    nb::object context = checked(PyTuple_Pack(3,base.ptr(),holder.ptr(),module_name.ptr()));
    module.attr("make_theme_state_bindings") = checked(PyCFunction_NewEx(&bindings_definition,context.ptr(),module_name.ptr()));
    module.attr("theme_storage_snapshot") = checked(PyCFunction_NewEx(&snapshot_definition,context.ptr(),module_name.ptr()));
    nb::object metadata_owner = nb::cast(ThemeBindingMetadata{});
    nb::object actual_factory = module.attr("make_theme_state_bindings");
    nb::object metadata_context = checked(PyTuple_Pack(2,actual_factory.ptr(),metadata_owner.ptr()));
    module.attr("theme_state_bindings") = checked(PyCFunction_NewEx(&theme_metadata_definition,metadata_context.ptr(),module_name.ptr()));
}
} // namespace nx
