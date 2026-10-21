#include "arrays.hpp"
#include "collection_protocol.hpp"

#include <array>
#include <utility>

namespace nx {
namespace {

nb::object shape_checked(PyObject* value) {
    if (!value) throw nb::python_error();
    return nb::steal<nb::object>(value);
}
nb::object shape_attr(nb::object value, const char* name) {
    return shape_checked(PyObject_GetAttrString(value.ptr(), name));
}
nb::object shape_item(nb::object value, nb::object key) {
    return shape_checked(PyObject_GetItem(value.ptr(), key.ptr()));
}
nb::object shape_call0(nb::object callback) {
    return shape_checked(PyObject_CallNoArgs(callback.ptr()));
}
nb::object shape_call1(nb::object callback, nb::object value) {
    return shape_checked(PyObject_CallOneArg(callback.ptr(), value.ptr()));
}
nb::object shape_call2(nb::object callback, nb::object first, nb::object second) {
    return shape_checked(PyObject_CallFunctionObjArgs(callback.ptr(), first.ptr(), second.ptr(), nullptr));
}
bool shape_truth(nb::object value) {
    const int result = PyObject_IsTrue(value.ptr());
    if (result < 0) throw nb::python_error();
    return result != 0;
}
nb::object shape_subtract(nb::object first, nb::object second) {
    return shape_checked(PyNumber_Subtract(first.ptr(), second.ptr()));
}
void shape_store(nb::object self, const char* name, nb::object value) {
    if (PyObject_SetAttrString(self.ptr(), name, value.ptr()) < 0) throw nb::python_error();
}
void shape_replace(nb::object& target, nb::object value) {
    nb::object previous = std::move(target);
    target = std::move(value);
    previous.reset();
}
nb::object shape_optional(const nb::object& value) {
    return value.is_valid() ? value : nb::none();
}

struct ShapeNamespace {
    nb::dict globals;
    nb::object builtins;
    explicit ShapeNamespace(nb::dict namespace_)
        : globals(std::move(namespace_)), builtins(nb::borrow<nb::object>(PyEval_GetBuiltins())) {}
    nb::object get(const char* name) const {
        nb::object key = nb::str(name);
        PyObject* found = PyDict_GetItemWithError(globals.ptr(), key.ptr());
        if (found) return nb::borrow<nb::object>(found);
        if (PyErr_Occurred()) throw nb::python_error();
        found = PyObject_GetItem(builtins.ptr(), key.ptr());
        if (found) return nb::steal<nb::object>(found);
        if (!PyErr_ExceptionMatches(PyExc_KeyError)) throw nb::python_error();
        PyErr_Clear();
        nb::object message = shape_checked(PyUnicode_FromFormat("name '%s' is not defined", name));
        nb::object error = shape_call1(nb::borrow<nb::object>(PyExc_NameError), std::move(message));
        if (PyObject_SetAttrString(error.ptr(), "name", key.ptr()) < 0) throw nb::python_error();
        PyErr_SetObject(PyExc_NameError, error.ptr());
        throw nb::python_error();
    }
    nb::object numpy(const char* name) const { return shape_attr(get("np"), name); }
};

// Each selected scalar body assigns only one of these original named locals.
// They are per-call references, not copies of layer/model/cache authority.
struct ScalarShapeLocals {
    nb::object shape, extent, data;
    ScalarShapeLocals() = default;
    ScalarShapeLocals(ScalarShapeLocals&&) = default;
    ~ScalarShapeLocals() { clear(); }
    void clear() {
        std::array<nb::object, 3> previous = {
            std::move(shape), std::move(extent), std::move(data),
        };
        for (auto& value : previous) value.reset();
    }
};
struct ImageShapeLocals {
    nb::object shapes;
    ImageShapeLocals() = default;
    ImageShapeLocals(ImageShapeLocals&&) = default;
    ~ImageShapeLocals() { clear(); }
    void clear() { nb::object previous = std::move(shapes); previous.reset(); }
};
int scalar_shape_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) {
        const auto& state = *nb::inst_ptr<ScalarShapeLocals>(self);
        Py_VISIT(state.shape.ptr()); Py_VISIT(state.extent.ptr()); Py_VISIT(state.data.ptr());
    }
    return 0;
}
int scalar_shape_clear(PyObject* self) {
    if (nb::inst_ready(self)) nb::inst_ptr<ScalarShapeLocals>(self)->clear();
    return 0;
}
PyType_Slot scalar_shape_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(scalar_shape_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(scalar_shape_clear)}, {0, nullptr},
};
int image_shape_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) Py_VISIT(nb::inst_ptr<ImageShapeLocals>(self)->shapes.ptr());
    return 0;
}
int image_shape_clear(PyObject* self) {
    if (nb::inst_ready(self)) nb::inst_ptr<ImageShapeLocals>(self)->clear();
    return 0;
}
PyType_Slot image_shape_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(image_shape_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(image_shape_clear)}, {0, nullptr},
};

nb::object scalar_shape_dtype(nb::object self, nb::dict namespace_) {
    ShapeNamespace names(std::move(namespace_));
    nb::object normalize = names.get("normalize_dtype");
    nb::object dtype = shape_attr(shape_attr(self, "_data"), "dtype");
    return shape_call1(std::move(normalize), std::move(dtype));
}
nb::object scalar_shape_ndim(nb::object self, nb::dict namespace_) {
    ShapeNamespace names(std::move(namespace_));
    nb::object length = names.get("len");
    nb::object first = shape_item(shape_attr(self, "level_shapes"), nb::int_(0));
    return shape_call1(std::move(length), std::move(first));
}
nb::object scalar_shape_get_levels(nb::object self, nb::dict namespace_, ScalarShapeLocals& owner) {
    ShapeNamespace names(std::move(namespace_));
    shape_replace(owner.data, shape_attr(self, "data"));
    nb::object matches;
    {
        nb::object instance = names.get("isinstance");
        nb::object type = names.get("MultiScaleData");
        matches = shape_call2(std::move(instance), owner.data, std::move(type));
    }
    if (shape_truth(std::move(matches))) return shape_attr(owner.data, "shapes");
    // The original single-scale branch deliberately reads self.data again.
    nb::object shape = shape_attr(shape_attr(self, "data"), "shape");
    nb::list result;
    result.append(shape);
    return result;
}
nb::object scalar_shape_levels(nb::object self, nb::dict namespace_) {
    ShapeNamespace names(std::move(namespace_));
    nb::object array = names.numpy("array");
    nb::object levels = shape_call0(shape_attr(self, "_get_level_shapes"));
    return shape_call1(std::move(array), std::move(levels));
}
nb::object scalar_shape_downsample(nb::object self, nb::dict namespace_) {
    ShapeNamespace names(std::move(namespace_));
    nb::object divide = names.numpy("divide");
    nb::object first = shape_item(shape_attr(self, "level_shapes"), nb::int_(0));
    nb::object levels = shape_attr(self, "level_shapes");
    return shape_call2(std::move(divide), std::move(first), std::move(levels));
}

// Shared expression implementation only; the two owning operations assign
// their original shape local before entering this exact source RHS sequence.
nb::object shape_extent_rhs(const ShapeNamespace& names, nb::object shape) {
    nb::object vstack = names.numpy("vstack");
    nb::object zeros = names.numpy("zeros");
    nb::object length = names.get("len");
    nb::object count = shape_call1(std::move(length), shape);
    nb::object minimum = shape_call1(std::move(zeros), std::move(count));
    nb::object maximum = shape_subtract(std::move(shape), nb::int_(1));
    nb::list rows;
    rows.append(minimum); rows.append(maximum);
    minimum.reset(); maximum.reset();
    return shape_call1(std::move(vstack), std::move(rows));
}
nb::object scalar_shape_extent(nb::object self, nb::dict namespace_, ScalarShapeLocals& owner) {
    ShapeNamespace names(std::move(namespace_));
    shape_replace(owner.shape, shape_item(shape_attr(self, "level_shapes"), nb::int_(0)));
    return shape_extent_rhs(names, owner.shape);
}
nb::object scalar_shape_level_extent(nb::object self, nb::dict namespace_, ScalarShapeLocals& owner) {
    ShapeNamespace names(std::move(namespace_));
    nb::object levels = shape_attr(self, "level_shapes");
    nb::object level = shape_attr(self, "data_level");
    shape_replace(owner.shape, shape_item(std::move(levels), std::move(level)));
    return shape_extent_rhs(names, owner.shape);
}
nb::object shape_augmented_rhs(nb::object extent, nb::object negative_half, nb::object positive_half) {
    nb::list lower, upper, offsets;
    lower.append(negative_half); upper.append(positive_half);
    offsets.append(lower); offsets.append(upper);
    lower.reset(); upper.reset();
    return shape_checked(PyNumber_Add(extent.ptr(), offsets.ptr()));
}
nb::object scalar_shape_extent_augmented(nb::object self, nb::object negative_half,
    nb::object positive_half, ScalarShapeLocals& owner) {
    shape_replace(owner.extent, shape_attr(self, "_extent_data"));
    return shape_augmented_rhs(owner.extent, std::move(negative_half), std::move(positive_half));
}
nb::object scalar_shape_level_extent_augmented(nb::object self, nb::object negative_half,
    nb::object positive_half, ScalarShapeLocals& owner) {
    shape_replace(owner.extent, shape_attr(self, "_extent_level_data"));
    return shape_augmented_rhs(owner.extent, std::move(negative_half), std::move(positive_half));
}
void scalar_shape_reset_thumbnail(nb::object self, nb::dict namespace_, ScalarShapeLocals& owner) {
    ShapeNamespace names(std::move(namespace_));
    shape_replace(owner.data, shape_attr(self, "_data"));
    nb::object matches;
    {
        nb::object instance = names.get("isinstance");
        nb::object type = names.get("MultiScaleData");
        matches = shape_call2(std::move(instance), owner.data, std::move(type));
    }
    if (shape_truth(std::move(matches))) {
        nb::object thumbnail_level;
        {
            nb::object length = names.get("len");
            nb::object count = shape_call1(std::move(length), owner.data);
            thumbnail_level = shape_subtract(std::move(count), nb::int_(1));
        }
        shape_store(self, "_thumbnail_level", std::move(thumbnail_level));
        bool two_dimensions;
        {
            nb::object ndim = shape_call0(shape_attr(self, "_get_ndim"));
            nb::object compared = shape_checked(PyObject_RichCompare(ndim.ptr(), nb::int_(2).ptr(), Py_EQ));
            ndim.reset();
            two_dimensions = shape_truth(std::move(compared));
        }
        nb::object materializer;
        if (two_dimensions) {
            nb::object factory = names.get("_make_level_materializer");
            materializer = shape_call1(std::move(factory), owner.data);
        } else materializer = nb::none();
        shape_store(self, "_level_materializer", std::move(materializer));
    } else {
        shape_store(self, "_thumbnail_level", nb::int_(0));
        shape_store(self, "_level_materializer", nb::none());
    }
}
nb::object scalar_shape_materializer_factory(nb::object materializer) {
    // The genuine nested declaration, sole data cell and actual external
    // lru_cache decorator are reviewed metadata. No second capture/cache exists.
    return materializer;
}
nb::object scalar_shape_materialize(nb::object level, nb::dict namespace_, nb::object data_getter) {
    ShapeNamespace names(std::move(namespace_));
    nb::object asarray = names.numpy("asarray");
    // LOAD_DEREF follows the np.asarray capture, including descriptor callbacks.
    nb::object data = shape_call0(std::move(data_getter));
    nb::object selected = shape_item(std::move(data), std::move(level));
    return shape_call1(std::move(asarray), std::move(selected));
}

nb::object image_shape_levels(nb::object self, nb::object bind_super, ImageShapeLocals& owner) {
    nb::object shapes;
    {
        // Pure receiver-first provider preserves actual zero-argument super
        // and its original lexical __class__ cell, never a fixed base method.
        nb::object callback = shape_call1(std::move(bind_super), self);
        shapes = shape_call0(std::move(callback));
    }
    shape_replace(owner.shapes, std::move(shapes));
    if (shape_truth(shape_attr(self, "rgb"))) {
        nb::object iterator = shape_checked(PyObject_GetIter(owner.shapes.ptr()));
        nb::list converted;
        nb::object current;
        while (true) {
            PyObject* next = PyIter_Next(iterator.ptr());
            if (!next) { if (PyErr_Occurred()) throw nb::python_error(); break; }
            shape_replace(current, nb::steal<nb::object>(next));
            nb::object stop = nb::int_(-1);
            nb::object key = shape_checked(PySlice_New(Py_None, stop.ptr(), Py_None));
            nb::object item = shape_item(current, std::move(key));
            converted.append(item);
        }
        iterator.reset(); current.reset();
        shape_replace(owner.shapes, std::move(converted));
    }
    return owner.shapes;
}

// The public object is a genuine Python generator supplied by pure yield-from
// glue. This native delegate owns only the original suspended iterator/im and
// source-frame namespace/builtins. It has no backing-list or model snapshot.
struct MultiScaleShapeIterator {
    bool started = false, running = false, closed = false;
    nb::object iterator, im, namespace_, builtins;
    MultiScaleShapeIterator() = default;
    MultiScaleShapeIterator(MultiScaleShapeIterator&&) = default;
    ~MultiScaleShapeIterator() { clear(); }
    void clear() {
        closed = true;
        std::array<nb::object, 4> previous = {
            std::move(iterator), std::move(im), std::move(namespace_), std::move(builtins),
        };
        for (auto& value : previous) value.reset();
    }
};
struct ShapeRunning {
    MultiScaleShapeIterator& state;
    explicit ShapeRunning(MultiScaleShapeIterator& state_) : state(state_) {
        if (state.running) throw nb::value_error("generator already executing");
        state.running = true;
    }
    ~ShapeRunning() { state.running = false; }
};
nb::object multiscale_shape_next(nb::handle self) {
    auto& state = *nb::inst_ptr<MultiScaleShapeIterator>(self);
    ShapeRunning running(state);
    if (state.closed) throw nb::stop_iteration();
    state.started = true;
    try {
        PyObject* next = PyIter_Next(state.iterator.ptr());
        if (!next) {
            if (PyErr_Occurred()) throw nb::python_error();
            state.clear();
            throw nb::stop_iteration();
        }
        shape_replace(state.im, nb::steal<nb::object>(next));
        return shape_attr(state.im, "shape");
    } catch (const nb::python_error& error) {
        events::HandledException active(error.value());
        // The genuine adapter traceback owns this delegate on error. Retain
        // only the original assigned edges there, like the original genexpr
        // traceback; normal exhaustion/close/GC releases all edges.
        state.closed = true;
        if (error.matches(PyExc_StopIteration)) collections::raise_exception(error.value(), true);
        throw;
    } catch (...) { state.clear(); throw; }
}
void multiscale_shape_close(nb::handle self) {
    auto& state = *nb::inst_ptr<MultiScaleShapeIterator>(self);
    ShapeRunning running(state);
    if (state.closed) return;
    if (!state.started) { state.clear(); return; }
    nb::object exit = shape_call0(nb::borrow<nb::object>(PyExc_GeneratorExit));
    events::HandledException active(exit);
    state.clear();
}
void multiscale_shape_throw(nb::handle self, nb::args args) {
    nb::object exception = collections::normalize_throw(args);
    auto& state = *nb::inst_ptr<MultiScaleShapeIterator>(self);
    ShapeRunning running(state);
    const bool open = !state.closed;
    state.closed = true;
    events::HandledException active(exception);
    collections::raise_exception(exception, open);
}
int multiscale_shape_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) {
        const auto& state = *nb::inst_ptr<MultiScaleShapeIterator>(self);
        Py_VISIT(state.iterator.ptr()); Py_VISIT(state.im.ptr());
        Py_VISIT(state.namespace_.ptr()); Py_VISIT(state.builtins.ptr());
    }
    return 0;
}
int multiscale_shape_clear(PyObject* self) {
    if (nb::inst_ready(self)) nb::inst_ptr<MultiScaleShapeIterator>(self)->clear();
    return 0;
}
PyType_Slot multiscale_shape_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(multiscale_shape_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(multiscale_shape_clear)}, {0, nullptr},
};

nb::object multiscale_dtype(nb::object self) {
    return shape_attr(shape_item(shape_attr(self, "_data"), nb::int_(0)), "dtype");
}
nb::object multiscale_shape(nb::object self) {
    return shape_attr(shape_item(shape_attr(self, "_data"), nb::int_(0)), "shape");
}
nb::object multiscale_shapes(nb::object self, nb::dict namespace_, nb::object generator_factory) {
    ShapeNamespace names(std::move(namespace_));
    nb::object consumer = names.get("tuple");
    nb::object iterator = shape_checked(PyObject_GetIter(shape_attr(self, "_data").ptr()));
    MultiScaleShapeIterator state;
    state.iterator = std::move(iterator);
    state.namespace_ = names.globals;
    state.builtins = names.builtins;
    nb::object native_iterator = nb::cast(std::move(state));
    nb::object generator = shape_call1(std::move(generator_factory), std::move(native_iterator));
    // Writable public generator metadata is distinct from its private frame/code.
    shape_store(generator, "__name__", nb::str("<genexpr>"));
    shape_store(generator, "__qualname__", nb::str("MultiScaleData.shapes.<locals>.<genexpr>"));
    return shape_call1(std::move(consumer), std::move(generator));
}
nb::object multiscale_getitem(nb::object self, nb::object key) {
    return shape_item(shape_attr(self, "_data"), std::move(key));
}
nb::object multiscale_len(nb::object self, nb::dict namespace_) {
    ShapeNamespace names(std::move(namespace_));
    nb::object length = names.get("len");
    nb::object data = shape_attr(self, "_data");
    return shape_call1(std::move(length), std::move(data));
}

} // namespace

void bind_scalar_shape_extent(nb::module_& module) {
    nb::class_<ScalarShapeLocals>(module, "ScalarShapeLocals", nb::is_weak_referenceable(),
        nb::type_slots(scalar_shape_slots))
        .def_prop_ro("shape", [](const ScalarShapeLocals& s) { return shape_optional(s.shape); })
        .def_prop_ro("extent", [](const ScalarShapeLocals& s) { return shape_optional(s.extent); })
        .def_prop_ro("data", [](const ScalarShapeLocals& s) { return shape_optional(s.data); });
    nb::class_<ImageShapeLocals>(module, "ImageShapeLocals", nb::is_weak_referenceable(),
        nb::type_slots(image_shape_slots))
        .def_prop_ro("shapes", [](const ImageShapeLocals& s) { return shape_optional(s.shapes); });
    nb::class_<MultiScaleShapeIterator>(module, "MultiScaleShapeIterator", nb::is_weak_referenceable(),
        nb::type_slots(multiscale_shape_slots))
        .def("__iter__", [](nb::handle self) { return nb::borrow<nb::object>(self); })
        .def("__next__", &multiscale_shape_next)
        .def("send", [](nb::handle self, nb::object value) {
            const auto& state = *nb::inst_ptr<MultiScaleShapeIterator>(self);
            if (state.running) throw nb::value_error("generator already executing");
            if (!state.started && !value.is_none())
                throw nb::type_error("can't send non-None value to a just-started generator");
            return multiscale_shape_next(self);
        }, nb::arg("value").none())
        .def("close", &multiscale_shape_close)
        .def("throw", &multiscale_shape_throw);
    module.def("scalar_shape_locals", [] { return ScalarShapeLocals(); });
    module.def("image_shape_locals", [] { return ImageShapeLocals(); });
    module.def("scalar_shape_dtype", &scalar_shape_dtype, nb::arg("self").none(), nb::arg("namespace"));
    module.def("scalar_shape_ndim", &scalar_shape_ndim, nb::arg("self").none(), nb::arg("namespace"));
    module.def("scalar_shape_get_levels", &scalar_shape_get_levels,
        nb::arg("self").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("scalar_shape_levels", &scalar_shape_levels, nb::arg("self").none(), nb::arg("namespace"));
    module.def("scalar_shape_downsample", &scalar_shape_downsample, nb::arg("self").none(), nb::arg("namespace"));
    module.def("scalar_shape_extent", &scalar_shape_extent,
        nb::arg("self").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("scalar_shape_extent_augmented", &scalar_shape_extent_augmented,
        nb::arg("self").none(), nb::arg("negative_half").none(), nb::arg("positive_half").none(), nb::arg("locals_owner"));
    module.def("scalar_shape_level_extent", &scalar_shape_level_extent,
        nb::arg("self").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("scalar_shape_level_extent_augmented", &scalar_shape_level_extent_augmented,
        nb::arg("self").none(), nb::arg("negative_half").none(), nb::arg("positive_half").none(), nb::arg("locals_owner"));
    module.def("scalar_shape_reset_thumbnail", &scalar_shape_reset_thumbnail,
        nb::arg("self").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("scalar_shape_materializer_factory", &scalar_shape_materializer_factory, nb::arg("materializer").none());
    module.def("scalar_shape_materialize", &scalar_shape_materialize,
        nb::arg("level").none(), nb::arg("namespace"), nb::arg("data_getter").none());
    module.def("image_shape_levels", &image_shape_levels,
        nb::arg("self").none(), nb::arg("bind_super").none(), nb::arg("locals_owner"));
    module.def("multiscale_dtype", &multiscale_dtype, nb::arg("self").none());
    module.def("multiscale_shape", &multiscale_shape, nb::arg("self").none());
    module.def("multiscale_shapes", &multiscale_shapes,
        nb::arg("self").none(), nb::arg("namespace"), nb::arg("generator_factory").none());
    module.def("multiscale_getitem", &multiscale_getitem, nb::arg("self").none(), nb::arg("key").none());
    module.def("multiscale_len", &multiscale_len, nb::arg("self").none(), nb::arg("namespace"));
}
} // namespace nx
