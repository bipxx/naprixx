#include "arrays.hpp"
#include "collection_protocol.hpp"

#include <array>
#include <utility>

// STAGED ONLY: no CMake/module integration and no production source edits.
// Module globals are the sole authority for activation, locking and specs.
// Genuine Python closures/decorators retain function and binding metadata;
// these operations own adapter selection, activation and reporting policy.
namespace nx {
namespace {

nb::object bc_checked(PyObject* value) {
    if (!value) throw nb::python_error();
    return nb::steal<nb::object>(value);
}
nb::object bc_attr(nb::object value, const char* name) {
    return bc_checked(PyObject_GetAttrString(value.ptr(), name));
}
nb::object bc_item(nb::object value, nb::object key) {
    return bc_checked(PyObject_GetItem(value.ptr(), key.ptr()));
}
nb::object bc_call0(nb::object callback) {
    return bc_checked(PyObject_CallNoArgs(callback.ptr()));
}
nb::object bc_call1(nb::object callback, nb::object value) {
    return bc_checked(PyObject_CallOneArg(callback.ptr(), value.ptr()));
}
nb::object bc_call2(nb::object callback, nb::object first, nb::object second) {
    return bc_checked(PyObject_CallFunctionObjArgs(callback.ptr(), first.ptr(), second.ptr(), nullptr));
}
nb::object bc_call3(nb::object callback, nb::object first, nb::object second, nb::object third) {
    return bc_checked(PyObject_CallFunctionObjArgs(callback.ptr(), first.ptr(), second.ptr(), third.ptr(), nullptr));
}
bool bc_truth(nb::object value) {
    const int result = PyObject_IsTrue(value.ptr());
    if (result < 0) throw nb::python_error();
    return result != 0;
}
nb::object bc_compare(nb::object first, nb::object second, int operation) {
    return bc_checked(PyObject_RichCompare(first.ptr(), second.ptr(), operation));
}
void bc_replace(nb::object& target, nb::object value) {
    nb::object previous = std::move(target);
    target = std::move(value);
    previous.reset();
}
nb::object bc_optional(const nb::object& value) {
    return value.is_valid() ? value : nb::none();
}
nb::object bc_literal(const char* value) {
    return bc_checked(PyUnicode_InternFromString(value));
}
[[noreturn]] void bc_raise(nb::object exception) {
    if (PyExceptionClass_Check(exception.ptr())) {
        nb::object value = bc_call0(exception);
        if (!PyExceptionInstance_Check(value.ptr())) {
            PyErr_Format(PyExc_TypeError, "calling %R should have returned an instance of BaseException, not %R",
                exception.ptr(), reinterpret_cast<PyObject*>(Py_TYPE(value.ptr())));
            throw nb::python_error();
        }
        exception = std::move(value);
    } else if (!PyExceptionInstance_Check(exception.ptr())) {
        PyErr_SetString(PyExc_TypeError, "exceptions must derive from BaseException");
        throw nb::python_error();
    }
    PyErr_SetObject(reinterpret_cast<PyObject*>(Py_TYPE(exception.ptr())), exception.ptr());
    throw nb::python_error();
}

struct BackendNamespace {
    nb::dict globals;
    nb::object builtins;
    explicit BackendNamespace(nb::dict value)
        : globals(std::move(value)), builtins(nb::borrow<nb::object>(PyEval_GetBuiltins())) {}
    nb::object get(const char* name) const {
        PyObject* value = PyDict_GetItemString(globals.ptr(), name);
        if (value) return nb::borrow<nb::object>(value);
        if (PyErr_Occurred()) throw nb::python_error();
        nb::object key = nb::str(name);
        value = PyObject_GetItem(builtins.ptr(), key.ptr());
        if (value) return nb::steal<nb::object>(value);
        if (!PyErr_ExceptionMatches(PyExc_KeyError)) throw nb::python_error();
        PyErr_Clear();
        nb::object message = bc_checked(PyUnicode_FromFormat("name '%s' is not defined", name));
        nb::object error = bc_call1(nb::borrow<nb::object>(PyExc_NameError), std::move(message));
        if (PyObject_SetAttrString(error.ptr(), "name", key.ptr()) < 0) throw nb::python_error();
        bc_raise(std::move(error));
    }
    void enabled(bool value) const {
        if (PyDict_SetItemString(globals.ptr(), "_ENABLED", value ? Py_True : Py_False) < 0)
            throw nb::python_error();
    }
};

// Only original assigned names: enable's napari and status's four inlined
// comprehension targets. No namespace, lock, specs, adapter or state mirror.
struct BackendControlLocals {
    nb::object napari, module, klass, attribute, underscore;
    BackendControlLocals() = default;
    BackendControlLocals(BackendControlLocals&&) = default;
    ~BackendControlLocals() { clear(); }
    void clear_comprehension() {
        std::array<nb::object, 4> previous = {
            std::move(module), std::move(klass), std::move(attribute), std::move(underscore),
        };
        // PEP709 restores its saved UNBOUND targets in reverse store order.
        for (size_t index : {3u, 2u, 1u, 0u}) previous[index].reset();
    }
    void clear() {
        std::array<nb::object, 5> previous = {
            std::move(napari), std::move(module), std::move(klass),
            std::move(attribute), std::move(underscore),
        };
        for (auto& edge : previous) edge.reset();
    }
};
int bc_locals_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) {
        const auto& state = *nb::inst_ptr<BackendControlLocals>(self);
        Py_VISIT(state.napari.ptr()); Py_VISIT(state.module.ptr());
        Py_VISIT(state.klass.ptr()); Py_VISIT(state.attribute.ptr());
        Py_VISIT(state.underscore.ptr());
    }
    return 0;
}
int bc_locals_clear(PyObject* self) {
    if (nb::inst_ready(self)) nb::inst_ptr<BackendControlLocals>(self)->clear();
    return 0;
}
PyType_Slot bc_locals_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(bc_locals_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(bc_locals_clear)}, {0, nullptr},
};

// RLock is an external primitive. Perform true type-special lookup, saving
// both methods before enter; release the consumed manager before invocation.
// The saved exit survives suppression truth and handled-exception restoration.
// A successful return value remains alive until normal exit has completed.
template <typename Body> nb::object bc_with(nb::object manager, Body body) {
    nb::object enter = collections::context_method(manager, "__enter__", false);
    nb::object exit = collections::context_method(manager, "__exit__", true);
    manager.reset();
    PyObject* entered = PyObject_CallNoArgs(enter.ptr());
    enter.reset();
    bc_checked(entered).reset();
    nb::object result;
    try { result = body(); }
    catch (const nb::python_error& original) {
        events::HandledException active(original.value());
        nb::object traceback = original.traceback();
        if (!traceback.is_valid()) traceback = nb::none();
        try {
            if (!bc_truth(bc_call3(exit, nb::borrow<nb::object>(original.type()),
                    nb::borrow<nb::object>(original.value()), std::move(traceback)))) throw;
        } catch (const nb::python_error& cleanup) {
            if (cleanup.value().ptr() != original.value().ptr())
                PyException_SetContext(cleanup.value().ptr(), original.value().inc_ref().ptr());
            throw;
        }
        return nb::none();
    }
    bc_call3(std::move(exit), nb::none(), nb::none(), nb::none());
    return result;
}

std::array<nb::object, 4> bc_unpack_spec(nb::object source) {
    nb::object iterator = nb::steal<nb::object>(PyObject_GetIter(source.ptr()));
    if (!iterator.is_valid()) {
        if (PyErr_ExceptionMatches(PyExc_TypeError) && !Py_TYPE(source.ptr())->tp_iter
            && !PySequence_Check(source.ptr()))
            PyErr_Format(PyExc_TypeError, "cannot unpack non-iterable %.200s object", Py_TYPE(source.ptr())->tp_name);
        throw nb::python_error();
    }
    std::array<nb::object, 4> values;
    for (Py_ssize_t index = 0; index < 4; ++index) {
        values[static_cast<size_t>(index)] = nb::steal<nb::object>(PyIter_Next(iterator.ptr()));
        if (!values[static_cast<size_t>(index)].is_valid()) {
            if (PyErr_Occurred()) throw nb::python_error();
            PyErr_Format(PyExc_ValueError, "not enough values to unpack (expected 4, got %zd)", index);
            throw nb::python_error();
        }
    }
    nb::object extra = nb::steal<nb::object>(PyIter_Next(iterator.ptr()));
    if (extra.is_valid()) {
        PyErr_SetString(PyExc_ValueError, "too many values to unpack (expected 4)");
        throw nb::python_error();
    }
    if (PyErr_Occurred()) throw nb::python_error();
    return values;
}

nb::object bc_operation_names(BackendNamespace& ns, BackendControlLocals& state) {
    nb::object iterator = bc_checked(PyObject_GetIter(ns.get("_SPECS").ptr()));
    nb::object result = nb::list();
    try {
        while (true) {
            PyObject* next = PyIter_Next(iterator.ptr());
            if (!next) {
                if (PyErr_Occurred()) throw nb::python_error();
                break;
            }
            auto values = bc_unpack_spec(nb::steal<nb::object>(next));
            bc_replace(state.module, std::move(values[0]));
            bc_replace(state.klass, std::move(values[1]));
            bc_replace(state.attribute, std::move(values[2]));
            bc_replace(state.underscore, std::move(values[3]));
            nb::object module_text = events::formatted(state.module);
            nb::object middle;
            if (bc_truth(state.klass)) middle = bc_checked(PyNumber_Add(state.klass.ptr(), bc_literal(".").ptr()));
            else middle = bc_literal("");
            nb::object middle_text = events::formatted(std::move(middle));
            middle.reset();
            nb::object attribute_text = events::formatted(state.attribute);
            nb::object name = events::message({module_text, bc_literal("."), middle_text, attribute_text});
            if (PyList_Append(result.ptr(), name.ptr()) < 0) throw nb::python_error();
        }
    } catch (...) {
        // PEP709 cleanup drops iterator/list, then restores the four target
        // slots to their original UNBOUND state without PUSH_EXC_INFO.
        iterator.reset(); result.reset(); state.clear_comprehension();
        throw;
    }
    iterator.reset();
    state.clear_comprehension();
    return result;
}

nb::object backend_make_adapter(nb::object factory, nb::object original_getter, nb::dict namespace_) {
    BackendNamespace ns(std::move(namespace_));
    nb::object lookup = ns.get("getattr");
    nb::object adapters = ns.get("_adapters");
    nb::object constructor = bc_call2(std::move(lookup), std::move(adapters), std::move(factory));
    // Lookup may rebind decorate's original cell before its source read.
    return bc_call1(std::move(constructor), bc_call0(std::move(original_getter)));
}
nb::object backend_dispatch(nb::object adapter_getter, nb::object original_getter, nb::tuple args,
        nb::dict kwargs, nb::dict namespace_) {
    BackendNamespace ns(std::move(namespace_));
    // The selected LOAD_DEREF follows truth evaluation. Getters close the
    // existing genuine cells, so a truth callback can update those cells.
    nb::object callable = bc_truth(ns.get("_ENABLED"))
        ? bc_call0(std::move(adapter_getter)) : bc_call0(std::move(original_getter));
    // CALL_FUNCTION_EX makes its own keyword dict after selecting callable.
    nb::object call_keywords = bc_checked(PyDict_Copy(kwargs.ptr()));
    return bc_checked(PyObject_Call(callable.ptr(), args.ptr(), call_keywords.ptr()));
}
nb::object backend_enable(nb::dict namespace_, BackendControlLocals& state) {
    BackendNamespace ns(std::move(namespace_));
    return bc_with(ns.get("_LOCK"), [&]() -> nb::object {
        bc_replace(state.napari, bc_call1(bc_attr(ns.get("importlib"), "import_module"), bc_literal("napari")));
        nb::object partition = bc_call1(bc_attr(bc_attr(state.napari, "__version__"), "partition"), bc_literal("+"));
        nb::object version = bc_item(std::move(partition), nb::int_(0));
        if (bc_truth(bc_compare(std::move(version), ns.get("NAPARI_VERSION"), Py_NE))) {
            nb::object constructor = ns.get("RuntimeError");
            nb::object required = events::formatted(ns.get("NAPARI_VERSION"));
            nb::object found = events::formatted(bc_attr(state.napari, "__version__"));
            nb::object message = events::message({bc_literal("naparixx requires napari=="), required,
                bc_literal("; found "), found, bc_literal(". Install the pinned release.")});
            bc_raise(bc_call1(std::move(constructor), std::move(message)));
        }
        nb::object native_version = bc_attr(ns.get("_native"), "napari_version");
        if (bc_truth(bc_compare(std::move(native_version), ns.get("NAPARI_VERSION"), Py_NE))) {
            nb::object constructor = ns.get("RuntimeError");
            bc_raise(bc_call1(std::move(constructor),
                bc_literal("native extension and Python compatibility versions differ")));
        }
        ns.enabled(true);
        return bc_call0(ns.get("backend_status"));
    });
}
nb::object backend_disable(nb::dict namespace_) {
    BackendNamespace ns(std::move(namespace_));
    return bc_with(ns.get("_LOCK"), [&]() -> nb::object {
        ns.enabled(false);
        return bc_call0(ns.get("backend_status"));
    });
}
nb::object backend_status(nb::dict namespace_, BackendControlLocals& state) {
    BackendNamespace ns(std::move(namespace_));
    return bc_with(ns.get("_LOCK"), [&]() -> nb::object {
        // BUILD_CONST_KEY_MAP evaluates all seven values before assembling
        // the dictionary. Preserve those leases through the comprehension.
        nb::object enabled = ns.get("_ENABLED");
        nb::object core = bc_literal("C++");
        nb::object components = nb::list();
        for (const char* name : {
            "Dims", "Camera", "GridCanvas", "Canvas viewbox sizing",
            "ViewerModel view navigation", "LayerList extents, units, names and uniqueness",
            "Affine and ScaleTranslate application", "Transform construction and decomposition",
            "Slice coordinate planning and unit conversion", "Image single-scale, multiscale and thick slicing",
            "Points, Vectors and Surface slice requests", "Layer loading state and slice task orchestration",
        }) {
            nb::object value = bc_literal(name);
            if (PyList_Append(components.ptr(), value.ptr()) < 0) throw nb::python_error();
        }
        nb::object integration = bc_literal("source fork; explicit calls");
        nb::object version = ns.get("NAPARI_VERSION");
        nb::object native_version = bc_attr(ns.get("_native"), "__version__");
        nb::object operations = bc_operation_names(ns, state);
        nb::dict result;
        if (PyDict_SetItemString(result.ptr(), "enabled", enabled.ptr()) < 0
            || PyDict_SetItemString(result.ptr(), "application_core", core.ptr()) < 0
            || PyDict_SetItemString(result.ptr(), "application_components", components.ptr()) < 0
            || PyDict_SetItemString(result.ptr(), "integration", integration.ptr()) < 0
            || PyDict_SetItemString(result.ptr(), "napari_version", version.ptr()) < 0
            || PyDict_SetItemString(result.ptr(), "native_version", native_version.ptr()) < 0
            || PyDict_SetItemString(result.ptr(), "operations", operations.ptr()) < 0)
            throw nb::python_error();
        return result;
    });
}

} // namespace

void bind_transform_family_backend(nb::module_& module) {
    nb::class_<BackendControlLocals>(module, "BackendControlLocals", nb::is_weak_referenceable(), nb::type_slots(bc_locals_slots))
        .def_prop_ro("napari", [](const BackendControlLocals& s) { return bc_optional(s.napari); })
        .def_prop_ro("module", [](const BackendControlLocals& s) { return bc_optional(s.module); })
        .def_prop_ro("klass", [](const BackendControlLocals& s) { return bc_optional(s.klass); })
        .def_prop_ro("attribute", [](const BackendControlLocals& s) { return bc_optional(s.attribute); })
        .def_prop_ro("_", [](const BackendControlLocals& s) { return bc_optional(s.underscore); });
    module.def("backend_control_locals", []() { return BackendControlLocals(); });
    module.def("backend_make_adapter", &backend_make_adapter,
        nb::arg("factory").none(), nb::arg("original_getter").none(), nb::arg("namespace"));
    module.def("backend_dispatch", &backend_dispatch,
        nb::arg("adapter_getter").none(), nb::arg("original_getter").none(), nb::arg("args"),
        nb::arg("kwargs"), nb::arg("namespace"));
    module.def("backend_enable", &backend_enable, nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("backend_disable", &backend_disable, nb::arg("namespace"));
    module.def("backend_status", &backend_status, nb::arg("namespace"), nb::arg("locals_owner"));
}

} // namespace nx
