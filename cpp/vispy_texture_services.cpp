#include "arrays.hpp"
#include "collection_protocol.hpp"
#include <exception>
#include <utility>

namespace nx {
namespace {
nb::object texture_checked(PyObject* value) {
    if (!value) throw nb::python_error();
    return nb::steal<nb::object>(value);
}
nb::object texture_attr(nb::object value, const char* name) {
    return texture_checked(PyObject_GetAttrString(value.ptr(), name));
}
nb::object texture_call0(nb::object callable) {
    return texture_checked(PyObject_CallNoArgs(callable.ptr()));
}
nb::object texture_call1(nb::object callable, nb::object value) {
    return texture_checked(PyObject_CallOneArg(callable.ptr(), value.ptr()));
}
nb::object texture_call2(nb::object callable, nb::object first, nb::object second) {
    return texture_checked(PyObject_CallFunctionObjArgs(callable.ptr(), first.ptr(), second.ptr(), nullptr));
}
nb::object texture_call3(nb::object callable, nb::object first, nb::object second, nb::object third) {
    return texture_checked(PyObject_CallFunctionObjArgs(callable.ptr(), first.ptr(), second.ptr(), third.ptr(), nullptr));
}
bool texture_truth(nb::object value) {
    int result = PyObject_IsTrue(value.ptr());
    if (result < 0) throw nb::python_error();
    return result != 0;
}
bool texture_test(nb::object first, nb::object second, int operation) {
    return texture_truth(texture_checked(PyObject_RichCompare(first.ptr(), second.ptr(), operation)));
}
bool texture_exception_matches(nb::handle exception, nb::object type) {
    if (PyTuple_Check(type.ptr())) {
        for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(type.ptr()); ++i)
            if (!PyExceptionClass_Check(PyTuple_GET_ITEM(type.ptr(), i)))
                throw nb::type_error("catching classes that do not inherit from BaseException is not allowed");
    } else if (!PyExceptionClass_Check(type.ptr())) {
        throw nb::type_error("catching classes that do not inherit from BaseException is not allowed");
    }
    return PyErr_GivenExceptionMatches(exception.ptr(), type.ptr()) != 0;
}
nb::object texture_format(nb::object value) {
    return texture_checked(PyObject_Format(value.ptr(), nb::str("").ptr()));
}
nb::object texture_concat(nb::object first, nb::object second) {
    return texture_checked(PyUnicode_Concat(first.ptr(), second.ptr()));
}
[[noreturn]] void texture_raise(nb::object exception) {
    if (PyExceptionInstance_Check(exception.ptr()))
        PyErr_SetObject(reinterpret_cast<PyObject*>(Py_TYPE(exception.ptr())), exception.ptr());
    else if (PyExceptionClass_Check(exception.ptr())) PyErr_SetNone(exception.ptr());
    else PyErr_SetString(PyExc_TypeError, "exceptions must derive from BaseException");
    throw nb::python_error();
}
struct TextureNamespace {
    nb::dict globals;
    nb::object builtins;
    explicit TextureNamespace(nb::dict ns)
        : globals(std::move(ns)), builtins(nb::borrow<nb::object>(PyEval_GetBuiltins())) {}
    TextureNamespace(nb::dict ns, nb::object builtin_namespace)
        : globals(std::move(ns)), builtins(std::move(builtin_namespace)) {}
    nb::object get(const char* name) const {
        nb::object key = nb::str(name);
        PyObject* result = PyDict_GetItemWithError(globals.ptr(), key.ptr());
        if (result) return nb::borrow<nb::object>(result);
        if (PyErr_Occurred()) throw nb::python_error();
        result = PyObject_GetItem(builtins.ptr(), key.ptr());
        if (result) return nb::steal<nb::object>(result);
        if (!PyErr_ExceptionMatches(PyExc_KeyError)) throw nb::python_error();
        PyErr_Clear();
        nb::object message = texture_checked(PyUnicode_FromFormat("name '%s' is not defined", name));
        nb::object error = texture_call1(nb::borrow<nb::object>(PyExc_NameError), std::move(message));
        if (PyObject_SetAttrString(error.ptr(), "name", key.ptr()) < 0) throw nb::python_error();
        texture_raise(std::move(error));
    }
    nb::object numpy(const char* name) const { return texture_attr(get("np"), name); }
};

// These are only the original assigned function locals which cannot be
// returned to the facade when a context exit or dtype conversion raises.
struct VispyTextureServiceLocals {
    nb::object max_size_2d, max_size_3d, GL_MAX_3D_TEXTURE_SIZE, dtype_, dtype;
    void clear() {
        nb::object old_2d = std::move(max_size_2d), old_3d = std::move(max_size_3d);
        nb::object old_enum = std::move(GL_MAX_3D_TEXTURE_SIZE), old_dtype = std::move(dtype_);
        nb::object old_source_dtype = std::move(dtype);
    }
};
int texture_locals_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) {
        const auto& state = *nb::inst_ptr<VispyTextureServiceLocals>(self);
        Py_VISIT(state.max_size_2d.ptr()); Py_VISIT(state.max_size_3d.ptr());
        Py_VISIT(state.GL_MAX_3D_TEXTURE_SIZE.ptr()); Py_VISIT(state.dtype_.ptr()); Py_VISIT(state.dtype.ptr());
    }
    return 0;
}
int texture_locals_clear(PyObject* self) {
    if (nb::inst_ready(self)) nb::inst_ptr<VispyTextureServiceLocals>(self)->clear();
    return 0;
}
PyType_Slot texture_locals_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(texture_locals_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(texture_locals_clear)}, {0, nullptr},
};
nb::object texture_optional(const nb::object& value) { return value.is_valid() ? value : nb::none(); }

struct VispyOpenGLContextIterator {
    enum class Stage { Created, Suspended, Closed };
    Stage stage = Stage::Created;
    bool running = false;
    nb::object namespace_, builtins, canvas;
    void clear() {
        stage = Stage::Closed;
        nb::object previous_canvas = std::move(canvas);
        nb::object previous_namespace = std::move(namespace_), previous_builtins = std::move(builtins);
    }
};
struct TextureRunning {
    VispyOpenGLContextIterator& state;
    explicit TextureRunning(VispyOpenGLContextIterator& value) : state(value) {
        if (state.running) throw nb::value_error("generator already executing");
        state.running = true;
    }
    ~TextureRunning() { state.running = false; }
};
void texture_context_finally(VispyOpenGLContextIterator& state) {
    if (state.canvas.is_valid() && !state.canvas.is_none())
        texture_call0(texture_attr(state.canvas, "close"));
}
[[noreturn]] void texture_generator_raise(nb::object exception, bool convert_stop) {
    collections::raise_exception(exception, convert_stop);
}
nb::object texture_context_next(nb::handle self) {
    auto& state = *nb::inst_ptr<VispyOpenGLContextIterator>(self);
    TextureRunning running(state);
    if (state.stage == VispyOpenGLContextIterator::Stage::Closed) throw nb::stop_iteration();
    try {
        if (state.stage == VispyOpenGLContextIterator::Stage::Created) {
            TextureNamespace ns(nb::borrow<nb::dict>(state.namespace_), state.builtins);
            const bool create = texture_call0(ns.get("get_current_canvas")).is_none();
            nb::object canvas = nb::none();
            if (create) {
                nb::object callable = ns.get("Canvas");
                nb::dict keywords;
                keywords["show"] = nb::bool_(false);
                canvas = texture_checked(PyObject_Call(callable.ptr(), nb::tuple().ptr(), keywords.ptr()));
            }
            state.canvas = std::move(canvas);
            state.stage = VispyOpenGLContextIterator::Stage::Suspended;
            return nb::none();
        }
        texture_context_finally(state);
    } catch (const nb::python_error& error) {
        events::HandledException active(error.value());
        state.clear();
        if (!error.matches(PyExc_StopIteration)) throw;
        texture_generator_raise(nb::borrow<nb::object>(error.value()), true);
    } catch (...) { state.clear(); throw; }
    state.clear();
    throw nb::stop_iteration();
}
void texture_context_close(nb::handle self) {
    auto& state = *nb::inst_ptr<VispyOpenGLContextIterator>(self);
    TextureRunning running(state);
    if (state.stage == VispyOpenGLContextIterator::Stage::Closed) return;
    if (state.stage == VispyOpenGLContextIterator::Stage::Created) { state.clear(); return; }
    nb::object exit = texture_call0(nb::borrow<nb::object>(PyExc_GeneratorExit));
    events::HandledException active(exit);
    try { texture_context_finally(state); }
    catch (const nb::python_error& error) {
        if (error.value().ptr() != exit.ptr()) PyException_SetContext(error.value().ptr(), exit.inc_ref().ptr());
        events::HandledException cleanup(error.value());
        state.clear();
        if (error.matches(PyExc_GeneratorExit)) return;
        if (!error.matches(PyExc_StopIteration)) throw;
        texture_generator_raise(nb::borrow<nb::object>(error.value()), true);
    } catch (...) { state.clear(); throw; }
    state.clear();
}
void texture_context_throw(nb::handle self, nb::args args) {
    nb::object exception = collections::normalize_throw(args);
    auto& state = *nb::inst_ptr<VispyOpenGLContextIterator>(self);
    TextureRunning running(state);
    const bool open = state.stage != VispyOpenGLContextIterator::Stage::Closed;
    events::HandledException active(exception);
    try {
        if (state.stage == VispyOpenGLContextIterator::Stage::Suspended) texture_context_finally(state);
    } catch (const nb::python_error& cleanup) {
        if (cleanup.value().ptr() != exception.ptr())
            PyException_SetContext(cleanup.value().ptr(), exception.inc_ref().ptr());
        events::HandledException cleanup_active(cleanup.value());
        state.clear();
        if (!cleanup.matches(PyExc_StopIteration)) throw;
        texture_generator_raise(nb::borrow<nb::object>(cleanup.value()), true);
    } catch (...) { state.clear(); throw; }
    state.clear();
    texture_generator_raise(std::move(exception), open);
}
int texture_context_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) {
        const auto& state = *nb::inst_ptr<VispyOpenGLContextIterator>(self);
        Py_VISIT(state.namespace_.ptr()); Py_VISIT(state.builtins.ptr()); Py_VISIT(state.canvas.ptr());
    }
    return 0;
}
int texture_context_clear(PyObject* self) {
    if (nb::inst_ready(self)) nb::inst_ptr<VispyOpenGLContextIterator>(self)->clear();
    return 0;
}
void texture_context_finalize(PyObject* self) noexcept {
    if (!nb::inst_ready(self) || nb::inst_ptr<VispyOpenGLContextIterator>(self)->stage ==
                               VispyOpenGLContextIterator::Stage::Closed) return;
    PyObject *type = nullptr, *value = nullptr, *traceback = nullptr;
    PyErr_Fetch(&type, &value, &traceback);
    try { texture_context_close(self); }
    catch (nb::python_error& error) { error.restore(); PyErr_WriteUnraisable(self); }
    catch (const std::exception& error) { PyErr_SetString(PyExc_RuntimeError, error.what()); PyErr_WriteUnraisable(self); }
    catch (...) { PyErr_SetString(PyExc_RuntimeError, "failed to close OpenGL context"); PyErr_WriteUnraisable(self); }
    PyErr_Restore(type, value, traceback);
}
PyType_Slot texture_context_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(texture_context_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(texture_context_clear)},
    {Py_tp_finalize, reinterpret_cast<void*>(texture_context_finalize)}, {0, nullptr},
};
destructor texture_context_base_dealloc = nullptr;
void texture_context_dealloc(PyObject* self) noexcept {
    // CPython generator deallocation kills its weakrefs before finally runs.
    // nanobind does that only in the saved deallocator, so move this boundary
    // ahead of the resurrection-aware finalizer, then retain its destruction.
    PyObject_GC_UnTrack(self);
    PyObject_ClearWeakRefs(self);
    PyObject_GC_Track(self);
    if (PyObject_CallFinalizerFromDealloc(self) < 0) return;
    texture_context_base_dealloc(self);
}
nb::object vispy_texture_context_generator(nb::dict namespace_) {
    VispyOpenGLContextIterator state;
    state.namespace_ = std::move(namespace_);
    state.builtins = nb::borrow<nb::object>(PyEval_GetBuiltins());
    return nb::cast(std::move(state), nb::rv_policy::move);
}

template <typename Function> void texture_with_context(nb::object context, Function body) {
    nb::object enter = collections::context_method(context, "__enter__", false);
    nb::object exit = collections::context_method(context, "__exit__", true);
    texture_call0(std::move(enter));
    context.reset();
    try { body(); }
    catch (const nb::python_error& original) {
        events::HandledException active(original.value());
        nb::object trace = original.traceback();
        if (!trace.is_valid()) trace = nb::none();
        try {
            if (!texture_truth(texture_call3(exit, nb::borrow<nb::object>(original.type()),
                                             nb::borrow<nb::object>(original.value()), std::move(trace)))) throw;
        } catch (const nb::python_error& cleanup) {
            if (cleanup.value().ptr() != original.value().ptr())
                PyException_SetContext(cleanup.value().ptr(), original.value().inc_ref().ptr());
            throw;
        }
        return;
    }
    texture_call3(std::move(exit), nb::none(), nb::none(), nb::none());
}
nb::object texture_assigned(const nb::object& value, const char* name) {
    if (value.is_valid()) return value;
    PyErr_Format(PyExc_UnboundLocalError, "cannot access local variable '%s' where it is not associated with a value", name);
    throw nb::python_error();
}
nb::tuple vispy_texture_max_sizes(nb::dict globals, VispyTextureServiceLocals& owner) {
    TextureNamespace ns(std::move(globals));
    texture_with_context(texture_call0(ns.get("_opengl_context")), [&] {
        nb::object callable = texture_attr(ns.get("gl"), "glGetParameter");
        nb::object parameter = texture_attr(ns.get("gl"), "GL_MAX_TEXTURE_SIZE");
        owner.max_size_2d = texture_call1(std::move(callable), std::move(parameter));
    });
    if (!texture_truth(texture_assigned(owner.max_size_2d, "max_size_2d"))) owner.max_size_2d = nb::none();
    texture_with_context(texture_call0(ns.get("_opengl_context")), [&] {
        owner.GL_MAX_3D_TEXTURE_SIZE = nb::int_(32883);
        nb::object callable = texture_attr(ns.get("gl"), "glGetParameter");
        owner.max_size_3d = texture_call1(std::move(callable), owner.GL_MAX_3D_TEXTURE_SIZE);
    });
    if (!texture_truth(texture_assigned(owner.max_size_3d, "max_size_3d"))) owner.max_size_3d = nb::none();
    return nb::make_tuple(texture_assigned(owner.max_size_2d, "max_size_2d"), texture_assigned(owner.max_size_3d, "max_size_3d"));
}
nb::object vispy_texture_fix_prepare(nb::object data, nb::dict globals) {
    TextureNamespace ns(std::move(globals));
    nb::object callable = ns.numpy("dtype");
    nb::object dtype = texture_attr(std::move(data), "dtype");
    return texture_call1(std::move(callable), std::move(dtype));
}
bool vispy_texture_fix_allowed(nb::object dtype, nb::dict globals) {
    TextureNamespace ns(std::move(globals));
    nb::object allowed = ns.get("texture_dtypes");
    int result = PySequence_Contains(allowed.ptr(), dtype.ptr());
    if (result < 0) throw nb::python_error();
    return result != 0;
}
nb::object vispy_texture_fix_target(nb::object dtype, nb::dict globals, VispyTextureServiceLocals& owner) {
    TextureNamespace ns(std::move(globals));
    try {
        nb::object callable = ns.get("cast");
        nb::object annotation = nb::str("type[np.unsignedinteger[Any] | np.floating[Any]]");
        nb::dict conversion;
        conversion["i"] = ns.numpy("float32");
        conversion["f"] = ns.numpy("float32");
        conversion["u"] = ns.numpy("uint16");
        conversion["b"] = ns.numpy("uint8");
        nb::object kind = texture_attr(dtype, "kind");
        nb::object target = texture_checked(PyObject_GetItem(conversion.ptr(), kind.ptr()));
        kind.reset(); conversion.reset();
        owner.dtype_ = texture_call2(std::move(callable), std::move(annotation), std::move(target));
        if (texture_test(owner.dtype_, ns.numpy("uint16"), Py_EQ) &&
            texture_test(texture_attr(dtype, "itemsize"), nb::int_(2), Py_GT))
            owner.dtype_ = ns.numpy("float32");
    } catch (const nb::python_error& original) {
        events::HandledException active(original.value());
        if (!texture_exception_matches(original.value(), ns.get("KeyError"))) throw;
        nb::object callable = ns.get("TypeError");
        nb::object message = texture_concat(nb::str("type "), texture_format(dtype));
        message = texture_concat(std::move(message), nb::str(" not allowed for texture; must be one of "));
        nb::object set = ns.get("set");
        nb::object allowed = ns.get("texture_dtypes");
        nb::object types = texture_call1(std::move(set), std::move(allowed));
        message = texture_concat(std::move(message), texture_format(std::move(types)));
        nb::object error = texture_call1(std::move(callable), std::move(message));
        if (PyExceptionInstance_Check(error.ptr())) {
            PyException_SetCause(error.ptr(), original.value().inc_ref().ptr());
            PyException_SetContext(error.ptr(), original.value().inc_ref().ptr());
        }
        texture_raise(std::move(error));
    }
    return std::move(owner.dtype_);
}
nb::object vispy_texture_fix_cast(nb::object data, nb::object dtype_) {
    return texture_call1(texture_attr(std::move(data), "astype"), std::move(dtype_));
}
nb::object vispy_texture_fix_data_dtype(nb::object data, nb::dict globals, VispyTextureServiceLocals& owner) {
    owner.dtype = vispy_texture_fix_prepare(data, globals);
    if (vispy_texture_fix_allowed(owner.dtype, globals)) return data;
    // Keep the old private target helper's exact return/owner contract while
    // publishing its result as the complete operation's original dtype_.
    owner.dtype_ = vispy_texture_fix_target(owner.dtype, std::move(globals), owner);
    return vispy_texture_fix_cast(std::move(data), owner.dtype_);
}
} // namespace

void bind_vispy_texture_services(nb::module_& m) {
    nb::class_<VispyTextureServiceLocals>(m, "VispyTextureServiceLocals", nb::is_weak_referenceable(), nb::type_slots(texture_locals_slots))
        .def_prop_ro("max_size_2d", [](const VispyTextureServiceLocals& s) { return texture_optional(s.max_size_2d); })
        .def_prop_ro("max_size_3d", [](const VispyTextureServiceLocals& s) { return texture_optional(s.max_size_3d); })
        .def_prop_ro("GL_MAX_3D_TEXTURE_SIZE", [](const VispyTextureServiceLocals& s) { return texture_optional(s.GL_MAX_3D_TEXTURE_SIZE); })
        .def_prop_ro("dtype_", [](const VispyTextureServiceLocals& s) { return texture_optional(s.dtype_); })
        .def_prop_ro("dtype", [](const VispyTextureServiceLocals& s) { return texture_optional(s.dtype); });
    auto generator = nb::class_<VispyOpenGLContextIterator>(m, "VispyOpenGLContextIterator", nb::is_weak_referenceable(), nb::type_slots(texture_context_slots))
        .def("__iter__", [](nb::handle self) { return nb::borrow<nb::object>(self); })
        .def("__next__", &texture_context_next)
        .def("send", [](nb::handle self, nb::object value) {
            const auto& state = *nb::inst_ptr<VispyOpenGLContextIterator>(self);
            if (state.stage == VispyOpenGLContextIterator::Stage::Created && !value.is_none())
                throw nb::type_error("can't send non-None value to a just-started generator");
            return texture_context_next(self);
        }, nb::arg("value").none())
        .def("throw", &texture_context_throw).def("close", &texture_context_close);
    auto* type = reinterpret_cast<PyTypeObject*>(generator.ptr());
    texture_context_base_dealloc = type->tp_dealloc;
    type->tp_dealloc = &texture_context_dealloc;
    PyType_Modified(type);
    m.def("vispy_texture_context_generator", &vispy_texture_context_generator, nb::arg("namespace"));
    m.def("vispy_texture_service_locals", [] { return VispyTextureServiceLocals{}; });
    m.def("vispy_texture_max_sizes", &vispy_texture_max_sizes, nb::arg("namespace"), nb::arg("locals_owner"));
    m.def("vispy_texture_fix_prepare", &vispy_texture_fix_prepare, nb::arg("data").none(), nb::arg("namespace"));
    m.def("vispy_texture_fix_allowed", &vispy_texture_fix_allowed, nb::arg("dtype").none(), nb::arg("namespace"));
    m.def("vispy_texture_fix_target", &vispy_texture_fix_target, nb::arg("dtype").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    m.def("vispy_texture_fix_cast", &vispy_texture_fix_cast, nb::arg("data").none(), nb::arg("dtype_").none());
    m.def("vispy_texture_fix_data_dtype", &vispy_texture_fix_data_dtype,
        nb::arg("data").none(), nb::arg("namespace"), nb::arg("locals_owner"));
}
} // namespace nx
