#include "arrays.hpp"
#include "collection_protocol.hpp"

#include <array>
#include <cwchar>
#include <climits>
#ifdef MS_WINDOWS
#include <direct.h>
#else
#include <unistd.h>
#endif
#include <osdefs.h>
#include <string>
#include <utility>

namespace nx {
namespace {
nb::object lr_checked(PyObject* result) {
    if (!result) throw nb::python_error();
    return nb::steal<nb::object>(result);
}
nb::object lr_attr(nb::object object, const char* name) {
    return lr_checked(PyObject_GetAttrString(object.ptr(), name));
}
nb::object lr_item(nb::object object, nb::object key) {
    return lr_checked(PyObject_GetItem(object.ptr(), key.ptr()));
}
nb::object lr_call0(nb::object callback) {
    return lr_checked(PyObject_CallNoArgs(callback.ptr()));
}
nb::object lr_call1(nb::object callback, nb::object value) {
    return lr_checked(PyObject_CallOneArg(callback.ptr(), value.ptr()));
}
nb::object lr_call2(nb::object callback, nb::object first, nb::object second) {
    return lr_checked(PyObject_CallFunctionObjArgs(callback.ptr(), first.ptr(), second.ptr(), nullptr));
}
nb::object lr_call3(nb::object callback, nb::object first, nb::object second, nb::object third) {
    return lr_checked(PyObject_CallFunctionObjArgs(callback.ptr(), first.ptr(), second.ptr(), third.ptr(), nullptr));
}
nb::object lr_kw(nb::object callback, nb::tuple args, nb::dict kwargs) {
    return lr_checked(PyObject_Call(callback.ptr(), args.ptr(), kwargs.ptr()));
}
nb::object lr_subtract(nb::object first, nb::object second) {
    return lr_checked(PyNumber_Subtract(first.ptr(), second.ptr()));
}
bool lr_truth(nb::object value) {
    int result = PyObject_IsTrue(value.ptr());
    if (result < 0) throw nb::python_error();
    return result != 0;
}
bool lr_compare(nb::object first, nb::object second, int op) {
    nb::object result = lr_checked(PyObject_RichCompare(first.ptr(), second.ptr(), op));
    first.reset(); second.reset();
    return lr_truth(std::move(result));
}
void lr_replace(nb::object& target, nb::object value) {
    nb::object previous = std::move(target);
    target = std::move(value);
    previous.reset();
}
nb::object lr_optional(const nb::object& value) {
    return value.is_valid() ? value : nb::none();
}

struct ResponseNamespace {
    nb::dict globals;
    nb::object builtins;
    explicit ResponseNamespace(nb::dict namespace_)
        : globals(std::move(namespace_)), builtins(nb::borrow<nb::object>(PyEval_GetBuiltins())) {}
    ResponseNamespace(nb::dict namespace_, nb::object builtins_)
        : globals(std::move(namespace_)), builtins(std::move(builtins_)) {}
    nb::object builtin(const char* name) const {
        nb::object key = nb::str(name);
        PyObject* result = PyObject_GetItem(builtins.ptr(), key.ptr());
        if (result) return nb::steal<nb::object>(result);
        if (!PyErr_ExceptionMatches(PyExc_KeyError)) throw nb::python_error();
        PyErr_Clear();
        if (std::string(name) == "__import__") {
            PyErr_SetString(PyExc_ImportError, "__import__ not found");
            throw nb::python_error();
        }
        nb::object message = lr_checked(PyUnicode_FromFormat("name '%s' is not defined", name));
        nb::object error = lr_call1(nb::borrow<nb::object>(PyExc_NameError), std::move(message));
        if (PyObject_SetAttrString(error.ptr(), "name", key.ptr()) < 0) throw nb::python_error();
        PyErr_SetObject(PyExc_NameError, error.ptr());
        throw nb::python_error();
    }
    nb::object get(const char* name) const {
        nb::object key = nb::str(name);
        PyObject* result = PyDict_GetItemWithError(globals.ptr(), key.ptr());
        if (result) return nb::borrow<nb::object>(result);
        if (PyErr_Occurred()) throw nb::python_error();
        return builtin(name);
    }
    nb::object import_scipy(nb::object name, nb::object fromlist) const {
        nb::object importer = builtin("__import__");
        PyObject* raw_locals = PyEval_GetLocals();
        if (!raw_locals && PyErr_Occurred()) throw nb::python_error();
        nb::object locals = nb::borrow<nb::object>(raw_locals ? raw_locals : Py_None);
        nb::object level = nb::int_(0);
        return lr_checked(PyObject_CallFunctionObjArgs(importer.ptr(), name.ptr(),
            globals.ptr(), locals.ptr(), fromlist.ptr(), level.ptr(), nullptr));
    }
};

// Complete CPython IMPORT_FROM primitive for the source-verified 3.12,
// 3.13 and 3.14 profiles. These are interpreter protocol/error semantics,
// not napari state. Primary ceval.c/moduleobject.c/errors.c sources:
// https://raw.githubusercontent.com/python/cpython/v3.12.8/Python/ceval.c
// https://raw.githubusercontent.com/python/cpython/v3.13.14/Python/ceval.c
// https://raw.githubusercontent.com/python/cpython/v3.14.4/Python/ceval.c
// Compiled/installed 3.13/3.14 application parity is a separate open gate.
nb::object lr_optional_attr(nb::handle object, const char* name) {
    PyObject* result = PyObject_GetAttrString(object.ptr(), name);
    if (result) return nb::steal<nb::object>(result);
    if (!PyErr_ExceptionMatches(PyExc_AttributeError)) throw nb::python_error();
    PyErr_Clear();
    return nb::object();
}
[[noreturn]] void lr_raise_import(nb::object message, nb::handle name,
    nb::handle path, nb::handle requested_name) {
    if (!message.is_valid()) {
        PyErr_SetString(PyExc_TypeError, "expected a message argument");
        throw nb::python_error();
    }
    nb::dict kwargs;
    kwargs["name"] = name.is_valid() ? nb::borrow<nb::object>(name) : nb::none();
    kwargs["path"] = path.is_valid() ? nb::borrow<nb::object>(path) : nb::none();
#if PY_VERSION_HEX >= 0x030C0000
    kwargs["name_from"] = requested_name;
#endif
    nb::tuple args = nb::make_tuple(message);
    nb::object error = lr_kw(nb::borrow<nb::object>(PyExc_ImportError), std::move(args), std::move(kwargs));
    PyErr_SetObject(reinterpret_cast<PyObject*>(Py_TYPE(error.ptr())), error.ptr());
    throw nb::python_error();
}
#if PY_VERSION_HEX >= 0x030D0000
// These exported CPython protocol primitives read the actual interpreter
// configuration/sys dictionary/CWD. No sys.path approximation or snapshot.
extern "C" PyAPI_FUNC(const PyConfig*) _Py_GetConfig(void);
extern "C" PyAPI_FUNC(int) _PySys_GetOptionalAttrString(const char*, PyObject**);
// Public/CRT equivalent of the identical CPython 3.13.14/3.14.4
// fileutils.c CWD primitive. A private non-exported helper is not linked.
wchar_t* lr_wgetcwd(wchar_t* buffer, size_t capacity) {
#ifdef MS_WINDOWS
    int bounded = static_cast<int>(capacity < INT_MAX ? capacity : INT_MAX);
    return _wgetcwd(buffer, bounded);
#else
    char filename[MAXPATHLEN];
    if (!getcwd(filename, sizeof(filename))) return nullptr;
    size_t length = 0;
    wchar_t* wide = Py_DecodeLocale(filename, &length);
    if (!wide) return nullptr;
    if (capacity <= length) { PyMem_RawFree(wide); return nullptr; }
    std::wcsncpy(buffer, wide, capacity);
    PyMem_RawFree(wide);
    return buffer;
#endif
}
nb::object lr_spec_file_origin(nb::handle spec) {
    nb::object located = lr_optional_attr(spec, "has_location");
    if (!located.is_valid()) return nb::object();
    bool has_location = lr_truth(std::move(located));
    if (!has_location) return nb::object();
    nb::object origin = lr_optional_attr(spec, "origin");
    if (origin.is_valid() && !PyUnicode_Check(origin.ptr())) origin.reset();
    return origin;
}
bool lr_possibly_shadowing(nb::handle origin) {
    if (!origin.is_valid()) return false;
    const PyConfig* config = _Py_GetConfig();
    if (config->safe_path) return false;
    wchar_t root[MAXPATHLEN + 1];
    Py_ssize_t size = PyUnicode_AsWideChar(origin.ptr(), root, MAXPATHLEN);
    if (size < 0) throw nb::python_error();
    root[size] = L'\0';
    wchar_t* separator = std::wcsrchr(root, SEP);
    if (!separator) return false;
    if (std::wcscmp(separator + 1, L"__init__.py") == 0) {
        *separator = L'\0';
        separator = std::wcsrchr(root, SEP);
        if (!separator) return false;
    }
    *separator = L'\0';
    const wchar_t* path0 = config->sys_path_0;
    if (!path0) return false;
    wchar_t cwd[MAXPATHLEN];
    if (!path0[0]) {
        if (!lr_wgetcwd(cwd, MAXPATHLEN)) return false;
        path0 = cwd;
    }
    return std::wcscmp(path0, root) == 0;
}
nb::object lr_import_ndimage(nb::handle module, nb::handle requested_name) {
    PyObject* result = PyObject_GetAttr(module.ptr(), requested_name.ptr());
    if (result) return nb::steal<nb::object>(result);
    if (!PyErr_ExceptionMatches(PyExc_AttributeError)) throw nb::python_error();
    PyErr_Clear();
    nb::object mod_name = lr_optional_attr(module, "__name__");
    if (mod_name.is_valid() && !PyUnicode_Check(mod_name.ptr())) mod_name.reset();
    if (mod_name.is_valid()) {
        nb::object fullname = lr_checked(PyUnicode_FromFormat("%U.%U", mod_name.ptr(), requested_name.ptr()));
        result = PyImport_GetModule(fullname.ptr());
        fullname.reset();
        if (result) return nb::steal<nb::object>(result);
        if (PyErr_Occurred()) throw nb::python_error();
    }
    PyObject* raw_mod_name = mod_name.ptr();
    nb::object name_or_unknown = mod_name.is_valid() ? std::move(mod_name) : nb::str("<unknown module name>");
    nb::object origin, spec, message;
    try {
        spec = lr_optional_attr(module, "__spec__");
        if (!spec.is_valid()) {
            message = lr_checked(PyUnicode_FromFormat(
                "cannot import name %R from %R (unknown location)", requested_name.ptr(), name_or_unknown.ptr()));
        } else {
            origin = lr_spec_file_origin(spec);
            bool shadowing = lr_possibly_shadowing(origin);
            bool shadowing_stdlib = false;
            if (shadowing) {
                PyObject* raw_stdlib = nullptr;
                if (_PySys_GetOptionalAttrString("stdlib_module_names", &raw_stdlib) < 0) throw nb::python_error();
                nb::object stdlib = nb::steal<nb::object>(raw_stdlib);
                if (stdlib.is_valid() && PyAnySet_Check(stdlib.ptr())) {
                    int contains = PySet_Contains(stdlib.ptr(), name_or_unknown.ptr());
                    if (contains < 0) throw nb::python_error();
                    shadowing_stdlib = contains != 0;
                }
            }
            if (!origin.is_valid() && PyModule_Check(module.ptr())) {
                origin = nb::steal<nb::object>(PyModule_GetFilenameObject(module.ptr()));
                if (!origin.is_valid()) {
                    if (!PyErr_ExceptionMatches(PyExc_SystemError)) throw nb::python_error();
                    PyErr_Clear();
                }
            }
            if (shadowing_stdlib) {
                message = lr_checked(PyUnicode_FromFormat(
                    "cannot import name %R from %R (consider renaming %R since it has the same name as the standard library module named %R and prevents importing that standard library module)",
                    requested_name.ptr(), name_or_unknown.ptr(), origin.ptr(), name_or_unknown.ptr()));
            } else {
                nb::object initializing = lr_optional_attr(spec, "_initializing");
                bool is_initializing = initializing.is_valid() && lr_truth(std::move(initializing));
                if (is_initializing && shadowing) {
                    message = lr_checked(PyUnicode_FromFormat(
                        "cannot import name %R from %R (consider renaming %R if it has the same name as a library you intended to import)",
                        requested_name.ptr(), name_or_unknown.ptr(), origin.ptr()));
                } else if (is_initializing && origin.is_valid()) {
                    message = lr_checked(PyUnicode_FromFormat(
                        "cannot import name %R from partially initialized module %R (most likely due to a circular import) (%S)",
                        requested_name.ptr(), name_or_unknown.ptr(), origin.ptr()));
                } else if (is_initializing) {
                    message = lr_checked(PyUnicode_FromFormat(
                        "cannot import name %R from partially initialized module %R (most likely due to a circular import)",
                        requested_name.ptr(), name_or_unknown.ptr()));
                } else if (origin.is_valid()) {
                    message = lr_checked(PyUnicode_FromFormat("cannot import name %R from %R (%S)",
                        requested_name.ptr(), name_or_unknown.ptr(), origin.ptr()));
                } else {
                    message = lr_checked(PyUnicode_FromFormat("cannot import name %R from %R (unknown location)",
                        requested_name.ptr(), name_or_unknown.ptr()));
                }
            }
        }
        lr_raise_import(std::move(message), nb::handle(raw_mod_name), origin, requested_name);
    } catch (...) {
        // Actual import_from error cleanup order, including callback failures.
        message.reset(); origin.reset(); spec.reset(); name_or_unknown.reset();
        throw;
    }
}
#else
bool lr_spec_initializing(nb::handle spec) {
    if (spec.is_valid()) {
        nb::object value = nb::steal<nb::object>(PyObject_GetAttrString(spec.ptr(), "_initializing"));
        if (!value.is_valid() && PyErr_ExceptionMatches(PyExc_AttributeError)) {
            PyErr_Clear();
            return false;
        }
        if (value.is_valid()) {
            int initializing = PyObject_IsTrue(value.ptr());
            value.reset();
            if (initializing >= 0) return initializing != 0;
        }
    }
    PyErr_Clear();
    return false;
}
nb::object lr_import_ndimage(nb::handle module, nb::handle requested_name) {
    PyObject* result = PyObject_GetAttr(module.ptr(), requested_name.ptr());
    if (result) return nb::steal<nb::object>(result);
    if (!PyErr_ExceptionMatches(PyExc_AttributeError)) throw nb::python_error();
    PyErr_Clear();
    nb::object pkgname = nb::steal<nb::object>(PyObject_GetAttrString(module.ptr(), "__name__"));
    if (pkgname.is_valid() && !PyUnicode_Check(pkgname.ptr())) pkgname.reset();
    if (pkgname.is_valid()) {
        nb::object fullname = lr_checked(PyUnicode_FromFormat("%U.%U", pkgname.ptr(), requested_name.ptr()));
        result = PyImport_GetModule(fullname.ptr());
        fullname.reset();
        if (result) return nb::steal<nb::object>(result);
        if (PyErr_Occurred()) throw nb::python_error();
    }
    nb::object pkgpath = nb::steal<nb::object>(PyModule_GetFilenameObject(module.ptr()));
    PyObject* raw_pkgname = pkgname.ptr();
    nb::object name_or_unknown = pkgname.is_valid() ? std::move(pkgname) : nb::str("<unknown module name>");
    nb::object message;
    nb::handle error_path;
    if (!pkgpath.is_valid() || !PyUnicode_Check(pkgpath.ptr())) {
        PyErr_Clear();
        message = nb::steal<nb::object>(PyUnicode_FromFormat(
            "cannot import name %R from %R (unknown location)", requested_name.ptr(), name_or_unknown.ptr()));
    } else {
        nb::object spec = nb::steal<nb::object>(PyObject_GetAttrString(module.ptr(), "__spec__"));
        bool initializing = lr_spec_initializing(spec);
        spec.reset();
        const char* format = initializing
            ? "cannot import name %R from partially initialized module %R (most likely due to a circular import) (%S)"
            : "cannot import name %R from %R (%S)";
        message = nb::steal<nb::object>(PyUnicode_FromFormat(format, requested_name.ptr(), name_or_unknown.ptr(), pkgpath.ptr()));
        error_path = pkgpath;
    }
    try { lr_raise_import(std::move(message), nb::handle(raw_pkgname), error_path, requested_name); }
    catch (...) {
        // Actual 3.12 error cleanup order.
        message.reset(); name_or_unknown.reset(); pkgpath.reset();
        throw;
    }
}
#endif

// Only original assigned method locals survive a saved facade traceback.
// Layer and colormap objects remain their existing authoritative owners.
struct LabelsResponseRawLocals {
    nb::object data_slice, labels, sliced_labels;
    LabelsResponseRawLocals() = default;
    LabelsResponseRawLocals(LabelsResponseRawLocals&&) = default;
    ~LabelsResponseRawLocals() { clear(); }
    void clear() {
        std::array<nb::object, 3> old = {
            std::move(data_slice), std::move(labels), std::move(sliced_labels)};
        for (auto& edge : old) edge.reset();
    }
};
struct LabelsResponseContourLocals {
    nb::object contour_offset, expanded_slice, sliced_labels, delta_slice;
    LabelsResponseContourLocals() = default;
    LabelsResponseContourLocals(LabelsResponseContourLocals&&) = default;
    ~LabelsResponseContourLocals() { clear(); }
    void clear() {
        std::array<nb::object, 4> old = {std::move(contour_offset), std::move(expanded_slice),
            std::move(sliced_labels), std::move(delta_slice)};
        for (auto& edge : old) edge.reset();
    }
};
struct LabelsResponseMorphologyLocals {
    nb::object ndi, struct_elem, thick_struct_elem, dilated_labels,
        eroded_labels, not_boundaries, contours;
    LabelsResponseMorphologyLocals() = default;
    LabelsResponseMorphologyLocals(LabelsResponseMorphologyLocals&&) = default;
    ~LabelsResponseMorphologyLocals() { clear(); }
    void clear() {
        std::array<nb::object, 7> old = {std::move(ndi), std::move(struct_elem),
            std::move(thick_struct_elem), std::move(dilated_labels),
            std::move(eroded_labels), std::move(not_boundaries), std::move(contours)};
        for (auto& edge : old) edge.reset();
    }
};
template <typename T> int lr_clear(PyObject* self) {
    if (nb::inst_ready(self)) nb::inst_ptr<T>(self)->clear();
    return 0;
}
int lr_raw_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) {
        const auto& s = *nb::inst_ptr<LabelsResponseRawLocals>(self);
        Py_VISIT(s.data_slice.ptr()); Py_VISIT(s.labels.ptr()); Py_VISIT(s.sliced_labels.ptr());
    }
    return 0;
}
int lr_contour_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) {
        const auto& s = *nb::inst_ptr<LabelsResponseContourLocals>(self);
        Py_VISIT(s.contour_offset.ptr()); Py_VISIT(s.expanded_slice.ptr());
        Py_VISIT(s.sliced_labels.ptr()); Py_VISIT(s.delta_slice.ptr());
    }
    return 0;
}
int lr_morphology_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) {
        const auto& s = *nb::inst_ptr<LabelsResponseMorphologyLocals>(self);
        Py_VISIT(s.ndi.ptr()); Py_VISIT(s.struct_elem.ptr()); Py_VISIT(s.thick_struct_elem.ptr());
        Py_VISIT(s.dilated_labels.ptr()); Py_VISIT(s.eroded_labels.ptr());
        Py_VISIT(s.not_boundaries.ptr()); Py_VISIT(s.contours.ptr());
    }
    return 0;
}
PyType_Slot lr_raw_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(lr_raw_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(lr_clear<LabelsResponseRawLocals>)}, {0, nullptr}};
PyType_Slot lr_contour_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(lr_contour_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(lr_clear<LabelsResponseContourLocals>)}, {0, nullptr}};
PyType_Slot lr_morphology_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(lr_morphology_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(lr_clear<LabelsResponseMorphologyLocals>)}, {0, nullptr}};

std::pair<nb::object, nb::object> lr_unpack_pair(nb::object pair) {
    PyObject* raw_iterator = PyObject_GetIter(pair.ptr());
    if (!raw_iterator) {
        if (PyErr_ExceptionMatches(PyExc_TypeError) && !Py_TYPE(pair.ptr())->tp_iter &&
            !PySequence_Check(pair.ptr())) {
            PyErr_Format(PyExc_TypeError, "cannot unpack non-iterable %.200s object", Py_TYPE(pair.ptr())->tp_name);
        }
        throw nb::python_error();
    }
    nb::object iterator = nb::steal<nb::object>(raw_iterator);
    auto next = [&](int count) {
        PyObject* result = PyIter_Next(iterator.ptr());
        if (!result) {
            if (!PyErr_Occurred()) PyErr_Format(PyExc_ValueError,
                "not enough values to unpack (expected 2, got %d)", count);
            throw nb::python_error();
        }
        return nb::steal<nb::object>(result);
    };
    nb::object first = next(0), second = next(1);
    nb::object extra = nb::steal<nb::object>(PyIter_Next(iterator.ptr()));
    if (extra.is_valid()) {
        extra.reset();
        PyErr_SetString(PyExc_ValueError, "too many values to unpack (expected 2)");
        throw nb::python_error();
    }
    if (PyErr_Occurred()) throw nb::python_error();
    return {std::move(first), std::move(second)};
}

enum class LabelsSliceKind { Raw, Delta };
struct LabelsResponseSliceIterator {
    LabelsSliceKind kind = LabelsSliceKind::Raw;
    bool started = false, running = false, closed = false;
    nb::object iterator, size, s1, s2, namespace_, builtins;
    LabelsResponseSliceIterator() = default;
    LabelsResponseSliceIterator(LabelsResponseSliceIterator&&) = default;
    ~LabelsResponseSliceIterator() { clear(); }
    void clear() {
        closed = true;
        std::array<nb::object, 6> old = {std::move(iterator), std::move(size),
            std::move(s1), std::move(s2), std::move(namespace_), std::move(builtins)};
        for (auto& edge : old) edge.reset();
    }
};
struct LabelsSliceRunning {
    LabelsResponseSliceIterator& s;
    explicit LabelsSliceRunning(LabelsResponseSliceIterator& state) : s(state) {
        if (s.running) throw nb::value_error("generator already executing");
        s.running = true;
    }
    ~LabelsSliceRunning() { s.running = false; }
};
nb::object lr_slice_next(nb::handle self) {
    auto& s = *nb::inst_ptr<LabelsResponseSliceIterator>(self);
    LabelsSliceRunning active(s);
    if (s.closed) throw nb::stop_iteration();
    s.started = true;
    try {
        PyObject* result = PyIter_Next(s.iterator.ptr());
        if (!result) {
            if (PyErr_Occurred()) throw nb::python_error();
            s.clear(); throw nb::stop_iteration();
        }
        nb::object item = nb::steal<nb::object>(result);
        ResponseNamespace names(nb::borrow<nb::dict>(s.namespace_), s.builtins);
        if (s.kind == LabelsSliceKind::Raw) {
            lr_replace(s.size, std::move(item));
            return lr_call2(names.get("slice"), nb::int_(0), s.size);
        }
        auto pair = lr_unpack_pair(std::move(item));
        lr_replace(s.s1, std::move(pair.first));
        lr_replace(s.s2, std::move(pair.second));
        nb::object factory = names.get("slice");
        nb::object first = lr_attr(s.s1, "start");
        nb::object second = lr_attr(s.s2, "start");
        nb::object start = lr_subtract(std::move(first), std::move(second));
        first = lr_attr(s.s1, "stop");
        second = lr_attr(s.s2, "start");
        nb::object stop = lr_subtract(std::move(first), std::move(second));
        return lr_call2(std::move(factory), std::move(start), std::move(stop));
    } catch (const nb::python_error& error) {
        events::HandledException handled(error.value());
        s.closed = true;
        // A saved genuine adapter traceback retains only source genexpr edges.
        // Do not drop them merely because subsequent close sees a closed frame.
        if (error.matches(PyExc_StopIteration)) collections::raise_exception(error.value(), true);
        throw;
    } catch (...) { s.clear(); throw; }
}
void lr_slice_close(nb::handle self) {
    auto& s = *nb::inst_ptr<LabelsResponseSliceIterator>(self);
    LabelsSliceRunning active(s);
    if (s.closed) return;
    if (!s.started) { s.clear(); return; }
    nb::object exit = lr_call0(nb::borrow<nb::object>(PyExc_GeneratorExit));
    events::HandledException handled(exit);
    s.clear();
}
void lr_slice_throw(nb::handle self, nb::args args) {
    nb::object exception = collections::normalize_throw(args);
    auto& s = *nb::inst_ptr<LabelsResponseSliceIterator>(self);
    LabelsSliceRunning active(s);
    bool open = !s.closed;
    s.closed = true;
    events::HandledException handled(exception);
    collections::raise_exception(exception, open);
}
int lr_slice_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) {
        const auto& s = *nb::inst_ptr<LabelsResponseSliceIterator>(self);
        Py_VISIT(s.iterator.ptr()); Py_VISIT(s.size.ptr()); Py_VISIT(s.s1.ptr()); Py_VISIT(s.s2.ptr());
        Py_VISIT(s.namespace_.ptr()); Py_VISIT(s.builtins.ptr());
    }
    return 0;
}
void lr_slice_finalize(PyObject* self) noexcept {
    if (!nb::inst_ready(self) || nb::inst_ptr<LabelsResponseSliceIterator>(self)->closed) return;
    PyObject *type = nullptr, *value = nullptr, *trace = nullptr;
    PyErr_Fetch(&type, &value, &trace);
    try { lr_slice_close(self); }
    catch (nb::python_error& error) { error.restore(); PyErr_WriteUnraisable(self); }
    catch (const std::exception& error) { PyErr_SetString(PyExc_RuntimeError, error.what()); PyErr_WriteUnraisable(self); }
    catch (...) { PyErr_SetString(PyExc_RuntimeError, "failed to close Labels slice generator"); PyErr_WriteUnraisable(self); }
    PyErr_Restore(type, value, trace);
}
PyType_Slot lr_slice_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(lr_slice_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(lr_clear<LabelsResponseSliceIterator>)},
    {Py_tp_finalize, reinterpret_cast<void*>(lr_slice_finalize)}, {0, nullptr}};
destructor lr_slice_base_dealloc = nullptr;
void lr_slice_dealloc(PyObject* self) noexcept {
    PyObject_GC_UnTrack(self); PyObject_ClearWeakRefs(self); PyObject_GC_Track(self);
    if (PyObject_CallFinalizerFromDealloc(self) < 0) return;
    lr_slice_base_dealloc(self);
}
nb::object lr_generator(LabelsSliceKind kind, nb::object iterator,
    const ResponseNamespace& names, nb::object generator_factory, const char* qualname) {
    LabelsResponseSliceIterator state;
    state.kind = kind; state.iterator = std::move(iterator);
    state.namespace_ = names.globals; state.builtins = names.builtins;
    nb::object delegate = nb::cast(std::move(state), nb::rv_policy::move);
    nb::object generator = lr_call1(std::move(generator_factory), std::move(delegate));
    nb::object name = nb::str("<genexpr>"), qualified = nb::str(qualname);
    if (PyObject_SetAttrString(generator.ptr(), "__name__", name.ptr()) < 0 ||
        PyObject_SetAttrString(generator.ptr(), "__qualname__", qualified.ptr()) < 0) throw nb::python_error();
    return generator;
}

nb::object labels_response_raw_to_displayed(nb::object self, nb::object raw,
    nb::dict namespace_, nb::object generator_factory, LabelsResponseRawLocals& owner) {
    ResponseNamespace names(std::move(namespace_));
    if (owner.data_slice.is_none()) {
        nb::object tuple_factory = names.get("tuple");
        nb::object shape = lr_attr(raw, "shape");
        nb::object iterator = lr_checked(PyObject_GetIter(shape.ptr()));
        shape.reset();
        nb::object generator = lr_generator(LabelsSliceKind::Raw, std::move(iterator),
            names, generator_factory, "Labels._raw_to_displayed.<locals>.<genexpr>");
        lr_replace(owner.data_slice, lr_call1(std::move(tuple_factory), std::move(generator)));
    }
    lr_replace(owner.labels, raw);
    {
        nb::object callback = lr_attr(self, "_calculate_contour");
        lr_replace(owner.sliced_labels, lr_call2(std::move(callback), owner.labels, owner.data_slice));
    }
    if (owner.sliced_labels.is_none())
        lr_replace(owner.sliced_labels, lr_item(owner.labels, owner.data_slice));
    nb::object callback = lr_attr(lr_attr(self, "colormap"), "_data_to_texture");
    return lr_call1(std::move(callback), owner.sliced_labels);
}
nb::object labels_response_calculate_contour(nb::object self, nb::object labels,
    nb::object data_slice, nb::dict namespace_, nb::object generator_factory,
    nb::object warning_message, LabelsResponseContourLocals& owner) {
    ResponseNamespace names(std::move(namespace_));
    if (lr_compare(lr_attr(self, "contour"), nb::int_(1), Py_LT)) return nb::none();
    if (lr_compare(lr_attr(labels, "ndim"), nb::int_(2), Py_GT)) {
        nb::object callback = lr_attr(names.get("warnings"), "warn");
        lr_call1(std::move(callback), warning_message);
        return nb::none();
    }
    {
        nb::object maximum = names.get("max");
        nb::object integer = names.get("int");
        nb::object thickness = lr_attr(self, "contour");
        nb::object converted = lr_call1(std::move(integer), std::move(thickness));
        lr_replace(owner.contour_offset, lr_call2(std::move(maximum), nb::int_(1), std::move(converted)));
    }
    {
        nb::object callback = names.get("expand_slice");
        nb::object shape = lr_attr(labels, "shape");
        lr_replace(owner.expanded_slice, lr_call3(std::move(callback), data_slice,
            std::move(shape), owner.contour_offset));
    }
    {
        nb::object callback = names.get("get_contours");
        nb::object selected = lr_item(labels, owner.expanded_slice);
        nb::object thickness = lr_attr(self, "contour");
        nb::object background = lr_attr(lr_attr(self, "colormap"), "background_value");
        lr_replace(owner.sliced_labels, lr_call3(std::move(callback), std::move(selected),
            std::move(thickness), std::move(background)));
    }
    {
        nb::object tuple_factory = names.get("tuple");
        nb::object zip_factory = names.get("zip");
        nb::tuple args = nb::make_tuple(data_slice, owner.expanded_slice);
        nb::dict kwargs; kwargs["strict"] = nb::bool_(false);
        nb::object zipped = lr_kw(std::move(zip_factory), std::move(args), std::move(kwargs));
        nb::object iterator = lr_checked(PyObject_GetIter(zipped.ptr()));
        zipped.reset();
        nb::object generator = lr_generator(LabelsSliceKind::Delta, std::move(iterator),
            names, generator_factory, "Labels._calculate_contour.<locals>.<genexpr>");
        lr_replace(owner.delta_slice, lr_call1(std::move(tuple_factory), std::move(generator)));
    }
    return lr_item(owner.sliced_labels, owner.delta_slice);
}
nb::object labels_response_get_contours(nb::object labels, nb::object thickness,
    nb::object background_label, nb::dict namespace_, nb::object import_name,
    nb::object import_fromlist, LabelsResponseMorphologyLocals& owner) {
    ResponseNamespace names(std::move(namespace_));
    {
        nb::object module = names.import_scipy(import_name, import_fromlist);
        lr_replace(owner.ndi, lr_import_ndimage(module, lr_item(import_fromlist, nb::int_(0))));
        module.reset();
    }
    {
        nb::object callback = lr_attr(owner.ndi, "generate_binary_structure");
        nb::object ndim = lr_attr(labels, "ndim");
        lr_replace(owner.struct_elem, lr_call2(std::move(callback), std::move(ndim), nb::int_(1)));
    }
    {
        nb::object callback = lr_attr(owner.ndi, "iterate_structure");
        nb::object iterated = lr_call2(std::move(callback), owner.struct_elem, thickness);
        nb::object astype = lr_attr(std::move(iterated), "astype");
        nb::object dtype = names.get("bool");
        lr_replace(owner.thick_struct_elem, lr_call1(std::move(astype), std::move(dtype)));
    }
    {
        nb::object callback = lr_attr(owner.ndi, "grey_dilation");
        nb::tuple args = nb::make_tuple(labels);
        nb::dict kwargs; kwargs["footprint"] = owner.struct_elem;
        lr_replace(owner.dilated_labels, lr_kw(std::move(callback), std::move(args), std::move(kwargs)));
    }
    {
        nb::object callback = lr_attr(owner.ndi, "grey_erosion");
        nb::tuple args = nb::make_tuple(labels);
        nb::dict kwargs; kwargs["footprint"] = owner.thick_struct_elem;
        lr_replace(owner.eroded_labels, lr_kw(std::move(callback), std::move(args), std::move(kwargs)));
    }
    lr_replace(owner.not_boundaries, lr_checked(PyObject_RichCompare(
        owner.dilated_labels.ptr(), owner.eroded_labels.ptr(), Py_EQ)));
    lr_replace(owner.contours, lr_call0(lr_attr(labels, "copy")));
    if (PyObject_SetItem(owner.contours.ptr(), owner.not_boundaries.ptr(), background_label.ptr()) < 0)
        throw nb::python_error();
    return owner.contours;
}
} // namespace

void bind_labels_response_display(nb::module_& module) {
    nb::class_<LabelsResponseRawLocals>(module, "LabelsResponseRawLocals",
        nb::is_weak_referenceable(), nb::type_slots(lr_raw_slots))
        .def_prop_ro("data_slice", [](const LabelsResponseRawLocals& s) { return lr_optional(s.data_slice); })
        .def_prop_ro("labels", [](const LabelsResponseRawLocals& s) { return lr_optional(s.labels); })
        .def_prop_ro("sliced_labels", [](const LabelsResponseRawLocals& s) { return lr_optional(s.sliced_labels); });
    nb::class_<LabelsResponseContourLocals>(module, "LabelsResponseContourLocals",
        nb::is_weak_referenceable(), nb::type_slots(lr_contour_slots))
        .def_prop_ro("contour_offset", [](const LabelsResponseContourLocals& s) { return lr_optional(s.contour_offset); })
        .def_prop_ro("expanded_slice", [](const LabelsResponseContourLocals& s) { return lr_optional(s.expanded_slice); })
        .def_prop_ro("sliced_labels", [](const LabelsResponseContourLocals& s) { return lr_optional(s.sliced_labels); })
        .def_prop_ro("delta_slice", [](const LabelsResponseContourLocals& s) { return lr_optional(s.delta_slice); });
    nb::class_<LabelsResponseMorphologyLocals>(module, "LabelsResponseMorphologyLocals",
        nb::is_weak_referenceable(), nb::type_slots(lr_morphology_slots))
        .def_prop_ro("ndi", [](const LabelsResponseMorphologyLocals& s) { return lr_optional(s.ndi); })
        .def_prop_ro("struct_elem", [](const LabelsResponseMorphologyLocals& s) { return lr_optional(s.struct_elem); })
        .def_prop_ro("thick_struct_elem", [](const LabelsResponseMorphologyLocals& s) { return lr_optional(s.thick_struct_elem); })
        .def_prop_ro("dilated_labels", [](const LabelsResponseMorphologyLocals& s) { return lr_optional(s.dilated_labels); })
        .def_prop_ro("eroded_labels", [](const LabelsResponseMorphologyLocals& s) { return lr_optional(s.eroded_labels); })
        .def_prop_ro("not_boundaries", [](const LabelsResponseMorphologyLocals& s) { return lr_optional(s.not_boundaries); })
        .def_prop_ro("contours", [](const LabelsResponseMorphologyLocals& s) { return lr_optional(s.contours); });
    auto delegate = nb::class_<LabelsResponseSliceIterator>(module, "LabelsResponseSliceIterator",
        nb::is_weak_referenceable(), nb::type_slots(lr_slice_slots))
        .def("__iter__", [](nb::handle self) { return nb::borrow<nb::object>(self); })
        .def("__next__", &lr_slice_next)
        .def("send", [](nb::handle self, nb::object value) {
            auto& s = *nb::inst_ptr<LabelsResponseSliceIterator>(self);
            if (s.closed) return lr_slice_next(self);
            if (!s.started && !value.is_none()) throw nb::type_error("can't send non-None value to a just-started generator");
            return lr_slice_next(self);
        }, nb::arg("value").none())
        .def("throw", &lr_slice_throw)
        .def("close", &lr_slice_close);
    auto* delegate_type = reinterpret_cast<PyTypeObject*>(delegate.ptr());
    lr_slice_base_dealloc = delegate_type->tp_dealloc;
    delegate_type->tp_dealloc = lr_slice_dealloc;
    PyType_Modified(delegate_type);
    module.def("labels_response_raw_locals", [](nb::object data_slice) {
        LabelsResponseRawLocals s; s.data_slice = std::move(data_slice); return s;
    }, nb::arg("data_slice").none());
    module.def("labels_response_contour_locals", [] { return LabelsResponseContourLocals(); });
    module.def("labels_response_morphology_locals", [] { return LabelsResponseMorphologyLocals(); });
    module.def("labels_response_raw_to_displayed", &labels_response_raw_to_displayed,
        nb::arg("self").none(), nb::arg("raw").none(), nb::arg("namespace"),
        nb::arg("generator_factory").none(), nb::arg("locals_owner"));
    module.def("labels_response_calculate_contour", &labels_response_calculate_contour,
        nb::arg("self").none(), nb::arg("labels").none(), nb::arg("data_slice").none(),
        nb::arg("namespace"), nb::arg("generator_factory").none(),
        nb::arg("warning_message").none(), nb::arg("locals_owner"));
    module.def("labels_response_get_contours", &labels_response_get_contours,
        nb::arg("labels").none(), nb::arg("thickness").none(), nb::arg("background_label").none(),
        nb::arg("namespace"), nb::arg("import_name").none(),
        nb::arg("import_fromlist").none(), nb::arg("locals_owner"));
}
} // namespace nx
