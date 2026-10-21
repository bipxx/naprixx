#include "arrays.hpp"
#include "iterator_frame.hpp"
#include <utility>

namespace nx {
namespace {

nb::object cache_checked(PyObject* value) {
    if (!value) throw nb::python_error();
    return nb::steal<nb::object>(value);
}
nb::object cache_attr(nb::object value, const char* name) {
    return cache_checked(PyObject_GetAttrString(value.ptr(), name));
}
nb::object cache_item(nb::object value, nb::object key) {
    return cache_checked(PyObject_GetItem(value.ptr(), key.ptr()));
}
nb::object cache_iter(nb::object value) {
    return cache_checked(PyObject_GetIter(value.ptr()));
}
nb::object cache_call0(nb::object callable) {
    return cache_checked(PyObject_CallNoArgs(callable.ptr()));
}
nb::object cache_call1(nb::object callable, nb::object value) {
    return cache_checked(PyObject_CallOneArg(callable.ptr(), value.ptr()));
}
nb::object cache_call2(nb::object callable, nb::object first, nb::object second) {
    return cache_checked(PyObject_CallFunctionObjArgs(
        callable.ptr(), first.ptr(), second.ptr(), nullptr));
}
nb::object cache_call_kw(nb::object callable, nb::tuple arguments,
                         const char* keyword, nb::object value) {
    nb::dict keywords;
    keywords[nb::str(keyword)] = value;
    return cache_checked(PyObject_Call(callable.ptr(), arguments.ptr(), keywords.ptr()));
}
bool cache_truth(nb::handle value) {
    const int result = PyObject_IsTrue(value.ptr());
    if (result < 0) throw nb::python_error();
    return result != 0;
}
bool cache_contains(nb::object container, nb::object value) {
    const int result = PySequence_Contains(container.ptr(), value.ptr());
    if (result < 0) throw nb::python_error();
    return result != 0;
}
nb::object cache_compare(nb::object first, nb::object second, int operation) {
    return cache_checked(PyObject_RichCompare(first.ptr(), second.ptr(), operation));
}
bool cache_test(nb::object first, nb::object second, int operation) {
    return cache_truth(cache_compare(std::move(first), std::move(second), operation));
}
nb::object cache_add(nb::object first, nb::object second) {
    return cache_checked(PyNumber_Add(first.ptr(), second.ptr()));
}
void cache_set(nb::handle target, nb::object key, nb::object value) {
    if (PyObject_SetItem(target.ptr(), key.ptr(), value.ptr()) < 0)
        throw nb::python_error();
}

struct CacheNamespace {
    nb::dict globals;
    nb::object builtins;
    explicit CacheNamespace(nb::dict namespace_)
        : globals(std::move(namespace_)), builtins(nb::borrow<nb::object>(PyEval_GetBuiltins())) {}
    CacheNamespace(nb::dict namespace_, nb::object builtins_)
        : globals(std::move(namespace_)), builtins(std::move(builtins_)) {}
    nb::object get(const char* name) const {
        nb::object key = nb::str(name);
        PyObject* found = PyDict_GetItemWithError(globals.ptr(), key.ptr());
        if (found) return nb::borrow<nb::object>(found);
        if (PyErr_Occurred()) throw nb::python_error();
        found = PyObject_GetItem(builtins.ptr(), key.ptr());
        if (found) return nb::steal<nb::object>(found);
        if (!PyErr_ExceptionMatches(PyExc_KeyError)) throw nb::python_error();
        PyErr_Clear();
        nb::object message = cache_checked(PyUnicode_FromFormat("name '%s' is not defined", name));
        nb::object error = cache_call1(nb::borrow<nb::object>(PyExc_NameError), std::move(message));
        if (PyObject_SetAttrString(error.ptr(), "name", key.ptr()) < 0) throw nb::python_error();
        PyErr_SetObject(PyExc_NameError, error.ptr());
        throw nb::python_error();
    }
    nb::object numpy(const char* name) const { return cache_attr(get("np"), name); }
    nb::object accelerated(const char* name) const { return cache_attr(get("_accel_cmap"), name); }
};

// Only original named loop locals belong here. Outer dictionaries, dtype
// objects, mapper, imports and cache keys remain named facade locals. No cache,
// iterator, self or namespace root is stored in this traceback owner.
struct DirectColormapCacheLocals {
    nb::object label, color, color_tup;
    nb::object k, v;
    nb::object key, val;
    void clear() {
        nb::object old_label = std::move(label), old_color = std::move(color);
        nb::object old_color_tup = std::move(color_tup), old_k = std::move(k);
        nb::object old_v = std::move(v), old_key = std::move(key), old_val = std::move(val);
        old_label.reset(); old_color.reset(); old_color_tup.reset();
        old_k.reset(); old_v.reset(); old_key.reset(); old_val.reset();
    }
};
int cache_locals_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(nb::handle(self))) {
        auto* state = nb::inst_ptr<DirectColormapCacheLocals>(nb::handle(self));
        Py_VISIT(state->label.ptr()); Py_VISIT(state->color.ptr());
        Py_VISIT(state->color_tup.ptr()); Py_VISIT(state->k.ptr());
        Py_VISIT(state->v.ptr()); Py_VISIT(state->key.ptr()); Py_VISIT(state->val.ptr());
    }
    return 0;
}
int cache_locals_clear(PyObject* self) {
    if (nb::inst_ready(nb::handle(self)))
        nb::inst_ptr<DirectColormapCacheLocals>(nb::handle(self))->clear();
    return 0;
}
PyType_Slot cache_locals_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(cache_locals_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(cache_locals_clear)}, {0, nullptr},
};
nb::object cache_optional(nb::object value) { return value.is_valid() ? value : nb::none(); }

struct CachePair { nb::object first, second; };
CachePair cache_unpack_pair(nb::object item) {
    nb::object iterator = cache_iter(std::move(item));
    nb::object first = nb::steal<nb::object>(PyIter_Next(iterator.ptr()));
    if (!first.is_valid()) {
        if (PyErr_Occurred()) throw nb::python_error();
        throw nb::value_error("not enough values to unpack (expected 2, got 0)");
    }
    nb::object second = nb::steal<nb::object>(PyIter_Next(iterator.ptr()));
    if (!second.is_valid()) {
        if (PyErr_Occurred()) throw nb::python_error();
        throw nb::value_error("not enough values to unpack (expected 2, got 1)");
    }
    nb::object extra = nb::steal<nb::object>(PyIter_Next(iterator.ptr()));
    if (extra.is_valid()) throw nb::value_error("too many values to unpack (expected 2)");
    if (PyErr_Occurred()) throw nb::python_error();
    return {std::move(first), std::move(second)};
}

nb::object direct_cache_unique_count(nb::object self, nb::dict globals) {
    CacheNamespace namespace_(std::move(globals));
    nb::object length = namespace_.get("len");
    nb::object values = cache_attr(cache_attr(std::move(self), "color_dict"), "values");
    nb::object iterator = cache_iter(cache_call0(std::move(values)));
    nb::object unique = cache_checked(PySet_New(nullptr));
    nb::object x;
    for (;;) {
        PyObject* next = PyIter_Next(iterator.ptr());
        if (!next) { if (PyErr_Occurred()) throw nb::python_error(); break; }
        x = nb::steal<nb::object>(next);
        nb::object converted = cache_call1(namespace_.get("tuple"), x);
        if (PySet_Add(unique.ptr(), converted.ptr()) < 0) throw nb::python_error();
    }
    x.reset();
    iterator.reset();
    return cache_call1(std::move(length), std::move(unique));
}
void direct_cache_base_clear(nb::object self, nb::dict globals) {
    CacheNamespace namespace_(std::move(globals));
    nb::object replacement = cache_call0(namespace_.get("_RebuildableCache"));
    if (PyObject_SetAttrString(self.ptr(), "_cache_mapping", replacement.ptr()) < 0)
        throw nb::python_error();
    replacement.reset();
    replacement = cache_call0(namespace_.get("_RebuildableCache"));
    if (PyObject_SetAttrString(self.ptr(), "_cache_other", replacement.ptr()) < 0)
        throw nb::python_error();
}
nb::object direct_cache_call_base(nb::object callback) {
    return cache_call0(std::move(callback));
}
void direct_cache_clear_cached_properties(nb::object self) {
    for (const char* name : {"_num_unique_colors", "_label_mapping_and_color_dict", "_array_map"}) {
        if (cache_contains(cache_attr(self, "__dict__"), nb::str(name))) {
            nb::object dictionary = cache_attr(self, "__dict__");
            if (PyObject_DelItem(dictionary.ptr(), nb::str(name).ptr()) < 0)
                throw nb::python_error();
        }
    }
}
bool direct_cache_selection(nb::object self, nb::object apply) {
    if (!cache_truth(cache_attr(std::move(self), "use_selection"))) return false;
    return cache_truth(apply);
}
nb::object direct_cache_selected_mapping(nb::object self, nb::dict globals) {
    CacheNamespace namespace_(std::move(globals));
    nb::dict labels;
    cache_set(labels, cache_attr(self, "selection"), nb::int_(1));
    cache_set(labels, nb::none(), nb::int_(0));
    nb::dict colors;
    nb::object zeros = cache_call1(namespace_.numpy("array"), nb::make_tuple(0, 0, 0, 0));
    cache_set(colors, nb::int_(0), std::move(zeros));
    nb::object get = cache_attr(cache_attr(self, "color_dict"), "get");
    nb::object selection = cache_attr(self, "selection");
    nb::object fallback = cache_attr(std::move(self), "default_color");
    nb::object color = cache_call2(std::move(get), std::move(selection), std::move(fallback));
    cache_set(colors, nb::int_(1), std::move(color));
    return nb::make_tuple(std::move(labels), std::move(colors));
}
nb::object direct_cache_canonical_mapping(nb::object self) {
    return cache_attr(std::move(self), "_label_mapping_and_color_dict");
}
nb::object direct_cache_empty_colors() { return nb::dict(); }
nb::object direct_cache_reserved_labels(nb::dict globals) {
    CacheNamespace namespace_(std::move(globals));
    nb::dict result;
    cache_set(result, nb::none(), namespace_.accelerated("MAPPING_OF_UNKNOWN_VALUE"));
    return result;
}
nb::object direct_cache_reserved_colors(nb::object self, nb::dict globals) {
    CacheNamespace namespace_(std::move(globals));
    nb::dict result;
    nb::object key = namespace_.accelerated("MAPPING_OF_UNKNOWN_VALUE");
    nb::object color = cache_attr(std::move(self), "default_color");
    cache_set(result, std::move(key), std::move(color));
    return result;
}
void direct_cache_fill_canonical(nb::object self, nb::object color_to_labels,
    nb::object labels_to_new_labels, nb::object new_color_dict, nb::dict globals,
    DirectColormapCacheLocals& locals) {
    CacheNamespace namespace_(std::move(globals));
    nb::object items = cache_attr(cache_attr(std::move(self), "color_dict"), "items");
    nb::object iterator = cache_iter(cache_call0(std::move(items)));
    for (;;) {
        PyObject* next = PyIter_Next(iterator.ptr());
        if (!next) { if (PyErr_Occurred()) throw nb::python_error(); return; }
        CachePair pair = cache_unpack_pair(nb::steal<nb::object>(next));
        locals.label = std::move(pair.first);
        locals.color = std::move(pair.second);
        if (locals.label.is_none()) continue;
        locals.color_tup = cache_call1(namespace_.get("tuple"), locals.color);
        if (!cache_contains(color_to_labels, locals.color_tup)) {
            nb::list group;
            group.append(locals.label);
            cache_set(color_to_labels, locals.color_tup, std::move(group));
            nb::object index = cache_call1(namespace_.get("len"), new_color_dict);
            cache_set(labels_to_new_labels, locals.label, std::move(index));
            nb::object color = locals.color;
            index = cache_item(labels_to_new_labels, locals.label);
            cache_set(new_color_dict, std::move(index), std::move(color));
        } else {
            nb::object group = cache_item(color_to_labels, locals.color_tup);
            cache_call1(cache_attr(std::move(group), "append"), locals.label);
            group = cache_item(color_to_labels, locals.color_tup);
            nb::object first = cache_item(std::move(group), nb::int_(0));
            nb::object index = cache_item(labels_to_new_labels, std::move(first));
            cache_set(labels_to_new_labels, locals.label, std::move(index));
        }
    }
}
nb::object direct_cache_mapping_result(nb::object labels, nb::object colors) {
    return nb::make_tuple(std::move(labels), std::move(colors));
}
nb::object direct_cache_typed_key(nb::object dtype) {
    nb::object text = cache_checked(PyObject_Format(dtype.ptr(), nb::str("").ptr()));
    return cache_checked(PyUnicode_FromFormat("_%U_typed_dict", text.ptr()));
}
bool direct_cache_typed_hit(nb::object self, nb::object key) {
    return cache_contains(cache_attr(std::move(self), "_cache_other"), std::move(key));
}
nb::object direct_cache_typed_get(nb::object self, nb::object key) {
    return cache_item(cache_attr(std::move(self), "_cache_other"), std::move(key));
}
nb::object direct_cache_target_type(nb::object self, nb::dict globals) {
    CacheNamespace namespace_(std::move(globals));
    nb::object minimum = namespace_.accelerated("minimum_dtype_for_labels");
    nb::object count = cache_add(cache_attr(std::move(self), "_num_unique_colors"), nb::int_(2));
    return cache_call1(std::move(minimum), std::move(count));
}
nb::object direct_cache_typed_new(nb::object typed, nb::object types,
    nb::object data_dtype, nb::object target_type, nb::dict globals) {
    CacheNamespace namespace_(std::move(globals));
    nb::object empty = cache_attr(cache_attr(std::move(typed), "Dict"), "empty");
    nb::object getter = namespace_.get("getattr");
    nb::object name = cache_attr(std::move(data_dtype), "name");
    nb::object key_type = cache_call2(std::move(getter), types, std::move(name));
    getter = namespace_.get("getattr");
    name = cache_attr(std::move(target_type), "name");
    nb::object value_type = cache_call2(std::move(getter), std::move(types), std::move(name));
    nb::dict kwargs;
    kwargs["key_type"] = key_type;
    kwargs["value_type"] = value_type;
    key_type.reset(); value_type.reset();
    return cache_checked(PyObject_Call(empty.ptr(), nb::tuple().ptr(), kwargs.ptr()));
}
nb::object direct_cache_iinfo(nb::object dtype, nb::dict globals) {
    return cache_call1(CacheNamespace(std::move(globals)).numpy("iinfo"), std::move(dtype));
}
void direct_cache_typed_fill(nb::object self, nb::object data_dtype,
    nb::object target_type, nb::object dkt, nb::object iinfo,
    DirectColormapCacheLocals& locals) {
    nb::object mapping = cache_item(cache_attr(std::move(self), "_label_mapping_and_color_dict"), nb::int_(0));
    nb::object iterator = cache_iter(cache_call0(cache_attr(std::move(mapping), "items")));
    for (;;) {
        PyObject* next = PyIter_Next(iterator.ptr());
        if (!next) { if (PyErr_Occurred()) throw nb::python_error(); return; }
        CachePair pair = cache_unpack_pair(nb::steal<nb::object>(next));
        locals.k = std::move(pair.first);
        locals.v = std::move(pair.second);
        if (locals.k.is_none()) continue;
        if (!cache_test(cache_attr(iinfo, "min"), locals.k, Py_LE)) continue;
        if (!cache_test(locals.k, cache_attr(iinfo, "max"), Py_LE)) continue;
        nb::object value = cache_call1(cache_attr(target_type, "type"), locals.v);
        nb::object key = cache_call1(cache_attr(data_dtype, "type"), locals.k);
        cache_set(dkt, std::move(key), std::move(value));
    }
}
void direct_cache_typed_publish(nb::object self, nb::object key, nb::object dkt) {
    cache_set(cache_attr(std::move(self), "_cache_other"), std::move(key), std::move(dkt));
}

// The source lazy generator captures its own namespace for fresh abs lookups.
// That reference belongs only to its suspension lifetime; method traceback
// owners and model/cache authorities never store a namespace or iterator.
struct DirectCacheKeyIterator : collections::IteratorFrame {
    bool absolute = false, started = false, running = false, closed = false;
    void gc_clear() { closed = true; release_frame(); }
};
struct CacheRunning {
    DirectCacheKeyIterator& state;
    explicit CacheRunning(DirectCacheKeyIterator& value) : state(value) {
        if (state.running) throw nb::value_error("generator already executing");
        state.running = true;
    }
    ~CacheRunning() { state.running = false; }
};
struct CacheHandledException {
    PyObject* previous = PyErr_GetHandledException();
    explicit CacheHandledException(nb::handle error) {
        Py_INCREF(error.ptr()); PyErr_SetExcInfo(nullptr, error.ptr(), nullptr);
    }
    ~CacheHandledException() { PyErr_SetExcInfo(nullptr, previous, nullptr); }
};
[[noreturn]] void cache_generator_raise(nb::object exception, bool convert_stop) {
    if (convert_stop && PyErr_GivenExceptionMatches(exception.ptr(), PyExc_StopIteration)) {
        nb::object converted = cache_checked(PyObject_CallFunction(
            PyExc_RuntimeError, "s", "generator raised StopIteration"));
        PyException_SetCause(converted.ptr(), exception.inc_ref().ptr());
        PyException_SetContext(converted.ptr(), exception.inc_ref().ptr());
        PyErr_SetObject(PyExc_RuntimeError, converted.ptr());
    } else PyErr_SetObject(reinterpret_cast<PyObject*>(Py_TYPE(exception.ptr())), exception.ptr());
    throw nb::python_error();
}
nb::object direct_cache_key_next(nb::handle self) {
    auto& state = *nb::inst_ptr<DirectCacheKeyIterator>(self);
    CacheRunning running(state);
    state.started = true;
    if (state.closed) throw nb::stop_iteration();
    try {
        nb::dict frame = state.frame();
        nb::object iterator = frame[".0"];
        for (;;) {
            PyObject* next = PyIter_Next(iterator.ptr());
            if (!next) {
                if (PyErr_Occurred()) throw nb::python_error();
                state.gc_clear(); throw nb::stop_iteration();
            }
            nb::object x = nb::steal<nb::object>(next);
            frame["x"] = x;
            if (x.is_none()) continue;
            if (!state.absolute) return cache_compare(std::move(x), nb::int_(0), Py_LT);
            nb::object globals = frame["namespace"];
            nb::object builtins = frame["builtins"];
            CacheNamespace namespace_(nb::borrow<nb::dict>(globals), std::move(builtins));
            return cache_call1(namespace_.get("abs"), std::move(x));
        }
    } catch (const nb::python_error& error) {
        CacheHandledException handled(error.value());
        state.gc_clear();
        if (!error.matches(PyExc_StopIteration)) throw;
        cache_generator_raise(nb::borrow<nb::object>(error.value()), true);
    } catch (...) { state.gc_clear(); throw; }
}
void direct_cache_key_close(nb::handle self) {
    auto& state = *nb::inst_ptr<DirectCacheKeyIterator>(self);
    CacheRunning running(state); state.gc_clear();
}
nb::object cache_generator_exception(nb::args args) {
    if (args.size() < 1 || args.size() > 3) {
        PyErr_Format(PyExc_TypeError, "throw expected %s %d argument%s, got %zd",
            args.size() < 1 ? "at least" : "at most", args.size() < 1 ? 1 : 3,
            args.size() < 1 ? "" : "s", args.size());
        throw nb::python_error();
    }
    nb::object supplied = args[0];
    nb::object value = args.size() > 1 ? nb::borrow<nb::object>(args[1]) : nb::none();
    nb::object traceback = args.size() > 2 ? nb::borrow<nb::object>(args[2]) : nb::none();
    if (!traceback.is_none() && !PyTraceBack_Check(traceback.ptr()))
        throw nb::type_error("throw() third argument must be a traceback object");
    nb::object error;
    if (PyExceptionInstance_Check(supplied.ptr())) {
        if (!value.is_none()) throw nb::type_error("instance exception may not have a separate value");
        error = std::move(supplied);
    } else if (PyExceptionClass_Check(supplied.ptr())) {
        PyErr_SetObject(supplied.ptr(), value.ptr());
        PyObject *type = nullptr, *normalized = nullptr, *trace = nullptr;
        PyErr_Fetch(&type, &normalized, &trace); PyErr_NormalizeException(&type, &normalized, &trace);
        Py_XDECREF(type); Py_XDECREF(trace); error = nb::steal<nb::object>(normalized);
        if (PyErr_Occurred()) throw nb::python_error();
    } else {
        PyErr_Format(PyExc_TypeError,
            "exceptions must be classes or instances deriving from BaseException, not %s",
            Py_TYPE(supplied.ptr())->tp_name);
        throw nb::python_error();
    }
    if (!traceback.is_none() && PyException_SetTraceback(error.ptr(), traceback.ptr()) < 0)
        throw nb::python_error();
    return error;
}
void direct_cache_key_throw(nb::handle self, nb::args args) {
    nb::object error = cache_generator_exception(args);
    auto& state = *nb::inst_ptr<DirectCacheKeyIterator>(self);
    CacheRunning running(state);
    const bool open = !state.closed;
    CacheHandledException handled(error); state.gc_clear();
    cache_generator_raise(std::move(error), open);
}
nb::object direct_cache_key_generator(nb::object self, nb::dict globals, bool absolute) {
    nb::object iterator = cache_iter(cache_attr(std::move(self), "color_dict"));
    DirectCacheKeyIterator state;
    state.absolute = absolute;
    state.frame()[".0"] = iterator; iterator.reset();
    state.frame()["namespace"] = std::move(globals);
    state.frame()["builtins"] = nb::borrow<nb::object>(PyEval_GetBuiltins());
    return nb::cast(std::move(state), nb::rv_policy::move);
}
nb::object direct_cache_array_max(nb::object self, nb::dict globals) {
    CacheNamespace namespace_(std::move(globals));
    nb::object maximum = namespace_.get("max");
    nb::object generator = direct_cache_key_generator(std::move(self), namespace_.globals, true);
    return cache_call_kw(std::move(maximum), nb::make_tuple(std::move(generator)), "default", nb::int_(0));
}
nb::object direct_cache_array_extent(nb::object self, nb::object maximum, nb::dict globals) {
    CacheNamespace namespace_(std::move(globals));
    nb::object any = namespace_.get("any");
    nb::object generator = direct_cache_key_generator(std::move(self), namespace_.globals, false);
    if (cache_truth(cache_call1(std::move(any), std::move(generator))))
        return cache_checked(PyNumber_InPlaceMultiply(maximum.ptr(), nb::int_(2).ptr()));
    return maximum;
}
void direct_cache_array_check(nb::object maximum, nb::dict globals) {
    if (!cache_test(std::move(maximum), nb::int_(65536), Py_GT)) return;
    CacheNamespace namespace_(std::move(globals));
    nb::object error = cache_call1(namespace_.get("RuntimeError"), nb::str(
        "Cannot use numpy implementation for large values of labels direct colormap. Please install numba."));
    if (PyExceptionInstance_Check(error.ptr()))
        PyErr_SetObject(reinterpret_cast<PyObject*>(Py_TYPE(error.ptr())), error.ptr());
    else if (PyExceptionClass_Check(error.ptr())) PyErr_SetNone(error.ptr());
    else PyErr_SetString(PyExc_TypeError, "exceptions must derive from BaseException");
    throw nb::python_error();
}
nb::object direct_cache_array_mapping(nb::object self) {
    return cache_item(cache_call0(cache_attr(std::move(self), "_values_mapping_to_minimum_values_set")), nb::int_(0));
}
nb::object direct_cache_array_new(nb::object maximum, nb::object dtype, nb::dict globals) {
    CacheNamespace namespace_(std::move(globals));
    nb::object full = namespace_.numpy("full");
    nb::object size = cache_add(std::move(maximum), nb::int_(2));
    nb::object unknown = namespace_.accelerated("MAPPING_OF_UNKNOWN_VALUE");
    return cache_call_kw(std::move(full), nb::make_tuple(std::move(size), std::move(unknown)), "dtype", std::move(dtype));
}
void direct_cache_array_fill(nb::object mapping, nb::object mapper, DirectColormapCacheLocals& locals) {
    nb::object iterator = cache_iter(cache_call0(cache_attr(std::move(mapping), "items")));
    for (;;) {
        PyObject* next = PyIter_Next(iterator.ptr());
        if (!next) { if (PyErr_Occurred()) throw nb::python_error(); return; }
        CachePair pair = cache_unpack_pair(nb::steal<nb::object>(next));
        locals.key = std::move(pair.first); locals.val = std::move(pair.second);
        if (!locals.key.is_none()) cache_set(mapper, locals.key, locals.val);
    }
}
nb::object direct_cache_default_color(nb::object self, nb::dict globals) {
    CacheNamespace namespace_(std::move(globals));
    nb::object get = cache_attr(cache_attr(std::move(self), "color_dict"), "get");
    nb::object fallback = cache_call1(namespace_.numpy("array"), nb::make_tuple(0, 0, 0, 0));
    return cache_call2(std::move(get), nb::none(), std::move(fallback));
}
} // namespace

void bind_direct_colormap_cache(nb::module_& module) {
    auto locals = nb::class_<DirectColormapCacheLocals>(module, "DirectColormapCacheLocals",
        nb::is_weak_referenceable(), nb::type_slots(cache_locals_slots));
    locals.def_prop_ro("label", [](const DirectColormapCacheLocals& s) { return cache_optional(s.label); })
        .def_prop_ro("color", [](const DirectColormapCacheLocals& s) { return cache_optional(s.color); })
        .def_prop_ro("color_tup", [](const DirectColormapCacheLocals& s) { return cache_optional(s.color_tup); })
        .def_prop_ro("k", [](const DirectColormapCacheLocals& s) { return cache_optional(s.k); })
        .def_prop_ro("v", [](const DirectColormapCacheLocals& s) { return cache_optional(s.v); })
        .def_prop_ro("key", [](const DirectColormapCacheLocals& s) { return cache_optional(s.key); })
        .def_prop_ro("val", [](const DirectColormapCacheLocals& s) { return cache_optional(s.val); });
    nb::class_<DirectCacheKeyIterator>(module, "DirectColormapKeyIterator", nb::is_weak_referenceable(),
        nb::type_slots(collections::iterator_frame_slots<DirectCacheKeyIterator>()))
        .def("__iter__", [](nb::handle self) { return nb::borrow<nb::object>(self); })
        .def("__next__", &direct_cache_key_next)
        .def("send", [](nb::handle self, nb::object value) {
            auto& state = *nb::inst_ptr<DirectCacheKeyIterator>(self);
            if (state.closed) throw nb::stop_iteration();
            if (!state.started && !value.is_none())
                throw nb::type_error("can't send non-None value to a just-started generator");
            return direct_cache_key_next(self);
        }, nb::arg("value").none())
        .def("close", &direct_cache_key_close).def("throw", &direct_cache_key_throw);
    module.def("direct_colormap_cache_locals", []() { return DirectColormapCacheLocals{}; });
    module.def("direct_cache_unique_count", &direct_cache_unique_count, nb::arg("self").none(), nb::arg("namespace"));
    module.def("direct_cache_base_clear", &direct_cache_base_clear, nb::arg("self").none(), nb::arg("namespace"));
    module.def("direct_cache_call_base", &direct_cache_call_base, nb::arg("callback").none());
    module.def("direct_cache_clear_cached_properties", &direct_cache_clear_cached_properties, nb::arg("self").none());
    module.def("direct_cache_selection", &direct_cache_selection, nb::arg("self").none(), nb::arg("apply_selection").none());
    module.def("direct_cache_selected_mapping", &direct_cache_selected_mapping, nb::arg("self").none(), nb::arg("namespace"));
    module.def("direct_cache_canonical_mapping", &direct_cache_canonical_mapping, nb::arg("self").none());
    module.def("direct_cache_empty_colors", &direct_cache_empty_colors);
    module.def("direct_cache_reserved_labels", &direct_cache_reserved_labels, nb::arg("namespace"));
    module.def("direct_cache_reserved_colors", &direct_cache_reserved_colors, nb::arg("self").none(), nb::arg("namespace"));
    module.def("direct_cache_fill_canonical", &direct_cache_fill_canonical, nb::arg("self").none(), nb::arg("color_to_labels").none(), nb::arg("labels_to_new_labels").none(), nb::arg("new_color_dict").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("direct_cache_mapping_result", &direct_cache_mapping_result, nb::arg("labels_to_new_labels").none(), nb::arg("new_color_dict").none());
    module.def("direct_cache_typed_key", &direct_cache_typed_key, nb::arg("data_dtype").none());
    module.def("direct_cache_typed_hit", &direct_cache_typed_hit, nb::arg("self").none(), nb::arg("key").none());
    module.def("direct_cache_typed_get", &direct_cache_typed_get, nb::arg("self").none(), nb::arg("key").none());
    module.def("direct_cache_target_type", &direct_cache_target_type, nb::arg("self").none(), nb::arg("namespace"));
    module.def("direct_cache_typed_new", &direct_cache_typed_new, nb::arg("typed").none(), nb::arg("types").none(), nb::arg("data_dtype").none(), nb::arg("target_type").none(), nb::arg("namespace"));
    module.def("direct_cache_iinfo", &direct_cache_iinfo, nb::arg("data_dtype").none(), nb::arg("namespace"));
    module.def("direct_cache_typed_fill", &direct_cache_typed_fill, nb::arg("self").none(), nb::arg("data_dtype").none(), nb::arg("target_type").none(), nb::arg("dkt").none(), nb::arg("iinfo").none(), nb::arg("locals_owner"));
    module.def("direct_cache_typed_publish", &direct_cache_typed_publish, nb::arg("self").none(), nb::arg("key").none(), nb::arg("dkt").none());
    module.def("direct_cache_array_max", &direct_cache_array_max, nb::arg("self").none(), nb::arg("namespace"));
    module.def("direct_cache_array_extent", &direct_cache_array_extent, nb::arg("self").none(), nb::arg("max_value").none(), nb::arg("namespace"));
    module.def("direct_cache_array_check", &direct_cache_array_check, nb::arg("max_value").none(), nb::arg("namespace"));
    module.def("direct_cache_array_mapping", &direct_cache_array_mapping, nb::arg("self").none());
    module.def("direct_cache_array_new", &direct_cache_array_new, nb::arg("max_value").none(), nb::arg("dtype").none(), nb::arg("namespace"));
    module.def("direct_cache_array_fill", &direct_cache_array_fill, nb::arg("label_mapping").none(), nb::arg("mapper").none(), nb::arg("locals_owner"));
    module.def("direct_cache_default_color", &direct_cache_default_color, nb::arg("self").none(), nb::arg("namespace"));
}
} // namespace nx
