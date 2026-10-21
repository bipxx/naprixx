#include "arrays.hpp"
#include "event_protocol.hpp"
#include "collection_protocol.hpp"
#include <array>
#include <initializer_list>
#include <utility>

namespace nx {
namespace {

nb::object vb_checked(PyObject* value) {
    if (!value) throw nb::python_error();
    return nb::steal<nb::object>(value);
}
nb::object vb_attr(nb::object value, const char* name) {
    return vb_checked(PyObject_GetAttrString(value.ptr(), name));
}
nb::object vb_item(nb::object value, nb::object key) {
    return vb_checked(PyObject_GetItem(value.ptr(), key.ptr()));
}
nb::object vb_call0(nb::object callable) { return vb_checked(PyObject_CallNoArgs(callable.ptr())); }
nb::object vb_call1(nb::object callable, nb::object value) {
    return vb_checked(PyObject_CallOneArg(callable.ptr(), value.ptr()));
}
nb::object vb_call2(nb::object callable, nb::object first, nb::object second) {
    return vb_checked(PyObject_CallFunctionObjArgs(callable.ptr(), first.ptr(), second.ptr(), nullptr));
}
nb::object vb_call3(nb::object callable, nb::object first, nb::object second, nb::object third) {
    return vb_checked(PyObject_CallFunctionObjArgs(callable.ptr(), first.ptr(), second.ptr(), third.ptr(), nullptr));
}
nb::object vb_call_kw(nb::object callable, nb::tuple args, nb::dict keywords) {
    return vb_checked(PyObject_Call(callable.ptr(), args.ptr(), keywords.ptr()));
}
void vb_store(nb::handle self, const char* name, nb::object value) {
    if (PyObject_SetAttrString(self.ptr(), name, value.ptr()) < 0) throw nb::python_error();
}
void vb_set(nb::handle self, nb::object key, nb::object value) {
    if (PyObject_SetItem(self.ptr(), key.ptr(), value.ptr()) < 0) throw nb::python_error();
}
bool vb_truth(nb::object value) {
    const int result = PyObject_IsTrue(value.ptr());
    if (result < 0) throw nb::python_error();
    return result != 0;
}
nb::object vb_compare(nb::object first, nb::object second, int operation) {
    return vb_checked(PyObject_RichCompare(first.ptr(), second.ptr(), operation));
}
bool vb_test(nb::object first, nb::object second, int operation) {
    return vb_truth(vb_compare(std::move(first), std::move(second), operation));
}
nb::object vb_multiply(nb::object first, nb::object second) {
    return vb_checked(PyNumber_Multiply(first.ptr(), second.ptr()));
}
nb::object vb_subtract(nb::object first, nb::object second) {
    return vb_checked(PyNumber_Subtract(first.ptr(), second.ptr()));
}
nb::object vb_divide(nb::object first, nb::object second) {
    return vb_checked(PyNumber_TrueDivide(first.ptr(), second.ptr()));
}
nb::object vb_matmul(nb::object first, nb::object second) {
    return vb_checked(PyNumber_MatrixMultiply(first.ptr(), second.ptr()));
}
nb::object vb_negate(nb::object value) { return vb_checked(PyNumber_Negative(value.ptr())); }
nb::object vb_slice(nb::object start, nb::object stop, nb::object step) {
    return vb_checked(PySlice_New(start.ptr(), stop.ptr(), step.ptr()));
}
nb::object vb_all() { return vb_slice(nb::none(), nb::none(), nb::none()); }
nb::object vb_reverse() { return vb_slice(nb::none(), nb::none(), nb::int_(-1)); }
nb::object vb_prefix(nb::object stop) { return vb_slice(nb::none(), std::move(stop), nb::none()); }
nb::object vb_iter(nb::object value) { return vb_checked(PyObject_GetIter(value.ptr())); }
nb::object vb_layer(nb::object self) { return vb_attr(std::move(self), "layer"); }
nb::object vb_node(nb::object self) { return vb_attr(std::move(self), "node"); }
nb::object vb_slice_input(nb::object self) { return vb_attr(vb_layer(std::move(self)), "_slice_input"); }
nb::object vb_optional(nb::object value) { return value.is_valid() ? value : nb::none(); }
[[noreturn]] void vb_raise(nb::object error) {
    if (PyExceptionInstance_Check(error.ptr()))
        PyErr_SetObject(reinterpret_cast<PyObject*>(Py_TYPE(error.ptr())), error.ptr());
    else if (PyExceptionClass_Check(error.ptr())) PyErr_SetNone(error.ptr());
    else PyErr_SetString(PyExc_TypeError, "exceptions must derive from BaseException");
    throw nb::python_error();
}
struct BaseNamespace {
    nb::dict globals;
    nb::object builtins;
    explicit BaseNamespace(nb::dict namespace_)
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
        nb::object message = vb_checked(PyUnicode_FromFormat("name '%s' is not defined", name));
        nb::object error = vb_call1(nb::borrow<nb::object>(PyExc_NameError), std::move(message));
        if (PyObject_SetAttrString(error.ptr(), "name", key.ptr()) < 0) throw nb::python_error();
        vb_raise(std::move(error));
    }
    nb::object numpy(const char* name) const { return vb_attr(get("np"), name); }
};
struct BasePair { nb::object first, second; };
BasePair vb_unpack_pair(nb::object value) {
    nb::object iterator = vb_iter(std::move(value));
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

// Per-call references are only the original body's assigned locals. They are
// visible to cyclic GC and a saved facade traceback, never controller state.
struct VispyBaseLocals {
    enum class Layout { Empty, Units, Scale, Blending, Matrix };
    Layout layout = Layout::Empty;
    nb::object reg, matrix, unused, upper_tri, blending, blending_kwargs;
    nb::object src_color_blending, dst_color_blending, dims_displayed, transform;
    nb::object units_scale, translate, layer_scale, data_level, level_factors;
    nb::object displayed_downsample, affine_matrix, child_offset, cp0;
    nb::object offset_matrix, offset, affine_offset, child_matrix, child;
    VispyBaseLocals() = default;
    VispyBaseLocals(VispyBaseLocals&&) = default;
    ~VispyBaseLocals() { clear(); }
    void clear() {
        std::array<nb::object, 24> old = {
            std::move(reg), std::move(matrix), std::move(unused), std::move(upper_tri),
            std::move(blending), std::move(blending_kwargs), std::move(src_color_blending),
            std::move(dst_color_blending), std::move(dims_displayed), std::move(transform),
            std::move(units_scale), std::move(translate), std::move(layer_scale), std::move(data_level),
            std::move(level_factors), std::move(displayed_downsample), std::move(affine_matrix),
            std::move(child_offset), std::move(cp0), std::move(offset_matrix), std::move(offset),
            std::move(affine_offset), std::move(child_matrix), std::move(child),
        };
        const Layout previous = layout;
        layout = Layout::Empty;
        const auto release = [&](std::initializer_list<size_t> order) {
            for (size_t index : order) old[index].reset();
        };
        switch (previous) {
            case Layout::Units: release({0}); break;
            case Layout::Scale: release({1, 2, 3}); break;
            case Layout::Blending: release({4, 5, 6, 7}); break;
            case Layout::Matrix: release({8, 9, 10, 11, 1, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23}); break;
            case Layout::Empty: break;
        }
        for (nb::object& value : old) value.reset();
    }
};
int vb_locals_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) {
        const auto& s = *nb::inst_ptr<VispyBaseLocals>(self);
        Py_VISIT(s.reg.ptr()); Py_VISIT(s.matrix.ptr()); Py_VISIT(s.unused.ptr()); Py_VISIT(s.upper_tri.ptr());
        Py_VISIT(s.blending.ptr()); Py_VISIT(s.blending_kwargs.ptr());
        Py_VISIT(s.src_color_blending.ptr()); Py_VISIT(s.dst_color_blending.ptr());
        Py_VISIT(s.dims_displayed.ptr()); Py_VISIT(s.transform.ptr()); Py_VISIT(s.units_scale.ptr());
        Py_VISIT(s.translate.ptr()); Py_VISIT(s.layer_scale.ptr()); Py_VISIT(s.data_level.ptr());
        Py_VISIT(s.level_factors.ptr()); Py_VISIT(s.displayed_downsample.ptr()); Py_VISIT(s.affine_matrix.ptr());
        Py_VISIT(s.child_offset.ptr()); Py_VISIT(s.cp0.ptr()); Py_VISIT(s.offset_matrix.ptr());
        Py_VISIT(s.offset.ptr()); Py_VISIT(s.affine_offset.ptr()); Py_VISIT(s.child_matrix.ptr()); Py_VISIT(s.child.ptr());
    }
    return 0;
}
int vb_locals_clear(PyObject* self) {
    if (nb::inst_ready(self)) nb::inst_ptr<VispyBaseLocals>(self)->clear();
    return 0;
}
PyType_Slot vb_locals_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(vb_locals_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(vb_locals_clear)}, {0, nullptr},
};

struct VispyUnitsIterator {
    bool started = false, running = false, closed = false;
    nb::object iterator, x, y, reg, namespace_, builtins;
    VispyUnitsIterator() = default;
    VispyUnitsIterator(VispyUnitsIterator&&) = default;
    ~VispyUnitsIterator() { clear(); }
    void clear() {
        closed = true;
        nb::object old_iterator = std::move(iterator), old_x = std::move(x), old_y = std::move(y);
        nb::object old_reg = std::move(reg), old_namespace = std::move(namespace_), old_builtins = std::move(builtins);
        old_iterator.reset(); old_x.reset(); old_y.reset(); old_reg.reset();
        old_namespace.reset(); old_builtins.reset();
    }
};
struct BaseUnitsRunning {
    VispyUnitsIterator& state;
    explicit BaseUnitsRunning(VispyUnitsIterator& value) : state(value) {
        if (state.running) throw nb::value_error("generator already executing");
        state.running = true;
    }
    ~BaseUnitsRunning() { state.running = false; }
};
[[noreturn]] void vb_generator_raise(nb::object error, bool convert_stop) {
    if (convert_stop && PyErr_GivenExceptionMatches(error.ptr(), PyExc_StopIteration)) {
        nb::object replacement = vb_checked(PyObject_CallFunction(PyExc_RuntimeError, "s", "generator raised StopIteration"));
        PyException_SetCause(replacement.ptr(), error.inc_ref().ptr());
        PyException_SetContext(replacement.ptr(), error.inc_ref().ptr());
        vb_raise(std::move(replacement));
    }
    vb_raise(std::move(error));
}
nb::object vb_units_next(nb::handle self) {
    auto& state = *nb::inst_ptr<VispyUnitsIterator>(self);
    BaseUnitsRunning running(state);
    if (state.closed) throw nb::stop_iteration();
    state.started = true;
    try {
        PyObject* next = PyIter_Next(state.iterator.ptr());
        if (!next) {
            if (PyErr_Occurred()) throw nb::python_error();
            state.clear(); throw nb::stop_iteration();
        }
        BasePair pair = vb_unpack_pair(nb::steal<nb::object>(next));
        state.x = std::move(pair.first);
        state.y = std::move(pair.second);
        nb::object numerator = vb_item(vb_call1(vb_attr(state.reg, "get_base_units"), state.y), nb::int_(0));
        nb::object denominator = vb_item(vb_call1(vb_attr(state.reg, "get_base_units"), state.x), nb::int_(0));
        return vb_divide(std::move(numerator), std::move(denominator));
    } catch (const nb::python_error& error) {
        events::HandledException active(error.value());
        state.clear();
        if (!error.matches(PyExc_StopIteration)) throw;
        vb_generator_raise(nb::borrow<nb::object>(error.value()), true);
    } catch (...) { state.clear(); throw; }
}
void vb_units_close(nb::handle self) {
    auto& state = *nb::inst_ptr<VispyUnitsIterator>(self);
    BaseUnitsRunning running(state);
    if (state.closed) return;
    if (!state.started) { state.clear(); return; }
    nb::object exit = vb_call0(nb::borrow<nb::object>(PyExc_GeneratorExit));
    events::HandledException active(exit);
    state.clear();
}
void vb_units_throw(nb::handle self, nb::args args) {
    nb::object error = collections::normalize_throw(args);
    auto& state = *nb::inst_ptr<VispyUnitsIterator>(self);
    BaseUnitsRunning running(state);
    const bool open = !state.closed;
    events::HandledException active(error);
    state.clear();
    vb_generator_raise(std::move(error), open);
}
int vb_units_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) {
        const auto& s = *nb::inst_ptr<VispyUnitsIterator>(self);
        Py_VISIT(s.iterator.ptr()); Py_VISIT(s.x.ptr()); Py_VISIT(s.y.ptr()); Py_VISIT(s.reg.ptr());
        Py_VISIT(s.namespace_.ptr()); Py_VISIT(s.builtins.ptr());
    }
    return 0;
}
int vb_units_clear(PyObject* self) {
    if (nb::inst_ready(self)) nb::inst_ptr<VispyUnitsIterator>(self)->clear();
    return 0;
}
void vb_units_finalize(PyObject* self) noexcept {
    if (!nb::inst_ready(self) || nb::inst_ptr<VispyUnitsIterator>(self)->closed) return;
    PyObject *type = nullptr, *value = nullptr, *traceback = nullptr;
    PyErr_Fetch(&type, &value, &traceback);
    try { vb_units_close(self); }
    catch (nb::python_error& error) { error.restore(); PyErr_WriteUnraisable(self); }
    catch (const std::exception& error) { PyErr_SetString(PyExc_RuntimeError, error.what()); PyErr_WriteUnraisable(self); }
    catch (...) { PyErr_SetString(PyExc_RuntimeError, "failed to close units generator"); PyErr_WriteUnraisable(self); }
    PyErr_Restore(type, value, traceback);
}
PyType_Slot vb_units_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(vb_units_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(vb_units_clear)},
    {Py_tp_finalize, reinterpret_cast<void*>(vb_units_finalize)}, {0, nullptr},
};
destructor vb_units_base_dealloc = nullptr;
void vb_units_dealloc(PyObject* self) noexcept {
    PyObject_GC_UnTrack(self);
    PyObject_ClearWeakRefs(self);
    PyObject_GC_Track(self);
    if (PyObject_CallFinalizerFromDealloc(self) < 0) return;
    vb_units_base_dealloc(self);
}

void vispy_base_initialize(nb::object self, nb::object layer, nb::object node, nb::object font_info,
                           nb::object base_factory, nb::dict globals) {
    vb_call0(vb_call1(std::move(base_factory), self));
    vb_store(self, "events", nb::none());
    vb_store(self, "layer", layer);
    vb_store(self, "font_info", std::move(font_info));
    vb_store(self, "_array_like", nb::bool_(false));
    vb_store(self, "node", std::move(node));
    vb_store(self, "first_visible", nb::bool_(false));
    vb_store(self, "_world_units", vb_attr(layer, "units"));
    vb_store(self, "_world_to_layer_units_scale", vb_multiply(nb::make_tuple(nb::int_(1)), vb_attr(std::move(layer), "ndim")));
    BaseNamespace ns(std::move(globals));
    BasePair sizes = vb_unpack_pair(vb_call0(ns.get("get_max_texture_sizes")));
    vb_store(self, "MAX_TEXTURE_SIZE_2D", std::move(sizes.first));
    vb_store(self, "MAX_TEXTURE_SIZE_3D", std::move(sizes.second));
    const char* events[] = {"refresh", "set_data", "visible", "opacity", "blending", "scale", "translate", "rotate", "shear", "affine", "units"};
    const char* callbacks[] = {"_on_refresh_change", "_on_data_change", "_on_visible_change", "_on_opacity_change", "_on_blending_change", "_on_matrix_change", "_on_matrix_change", "_on_matrix_change", "_on_matrix_change", "_on_matrix_change", "_recalculate_units_scale"};
    for (size_t i = 0; i < 11; ++i) {
        nb::object connect = vb_attr(vb_attr(vb_attr(vb_layer(self), "events"), events[i]), "connect");
        nb::object callback = vb_attr(self, callbacks[i]);
        vb_call1(std::move(connect), std::move(callback));
    }
    nb::object connect = vb_attr(vb_attr(vb_attr(vb_layer(self), "experimental_clipping_planes"), "events"), "connect");
    nb::object callback = vb_attr(std::move(self), "_on_experimental_clipping_planes_change");
    vb_call1(std::move(connect), std::move(callback));
}
nb::object vispy_base_world_units(nb::object self) { return vb_attr(std::move(self), "_world_units"); }
void vispy_base_set_world_units(nb::object self, nb::object value, nb::dict globals) {
    if (value.is_none()) {
        vb_store(self, "_world_units", vb_attr(vb_layer(self), "units"));
        vb_store(self, "_world_to_layer_units_scale", vb_multiply(nb::make_tuple(nb::int_(1)), vb_attr(vb_layer(self), "ndim")));
    } else {
        nb::object start = vb_negate(vb_attr(vb_layer(self), "ndim"));
        nb::object units = vb_item(std::move(value), vb_slice(std::move(start), nb::none(), nb::none()));
        vb_store(self, "_world_units", std::move(units));
        vb_call0(vb_attr(self, "_recalculate_units_scale"));
    }
    vb_call0(vb_attr(std::move(self), "_on_matrix_change"));
}
void vispy_base_recalculate_units_scale(nb::object self, nb::dict globals, VispyBaseLocals& owner) {
    owner.layout = VispyBaseLocals::Layout::Units;
    BaseNamespace ns(std::move(globals));
    owner.reg = vb_call0(vb_attr(ns.get("pint"), "get_application_registry"));
    nb::object consumer = ns.get("tuple");
    nb::object zip = ns.get("zip");
    nb::object world = vb_attr(self, "_world_units");
    nb::object layer = vb_attr(vb_layer(self), "units");
    nb::dict keywords;
    keywords["strict"] = nb::bool_(false);
    nb::object iterator = vb_iter(vb_call_kw(std::move(zip), nb::make_tuple(std::move(world), std::move(layer)), std::move(keywords)));
    VispyUnitsIterator state;
    state.iterator = std::move(iterator);
    state.reg = owner.reg;
    state.namespace_ = ns.globals;
    state.builtins = ns.builtins;
    nb::object scale = vb_call1(std::move(consumer), nb::cast(std::move(state), nb::rv_policy::move));
    vb_store(self, "_world_to_layer_units_scale", std::move(scale));
}
nb::object vispy_base_master_transform(nb::object self, nb::dict globals) {
    BaseNamespace ns(std::move(globals));
    nb::object check = ns.get("isinstance");
    nb::object transform = vb_attr(vb_node(self), "transform");
    nb::object type = ns.get("MatrixTransform");
    if (!vb_truth(vb_call2(std::move(check), std::move(transform), std::move(type)))) {
        nb::object replacement = vb_call0(ns.get("MatrixTransform"));
        vb_store(vb_node(self), "transform", std::move(replacement));
    }
    return vb_attr(vb_node(std::move(self)), "transform");
}
nb::object vispy_base_translate(nb::object self) {
    nb::object matrix = vb_attr(vb_attr(std::move(self), "_master_transform"), "matrix");
    return vb_item(std::move(matrix), nb::make_tuple(nb::int_(-1), vb_all()));
}
nb::object vispy_base_scale(nb::object self, nb::dict globals, VispyBaseLocals& owner) {
    owner.layout = VispyBaseLocals::Layout::Scale;
    nb::object matrix = vb_attr(vb_attr(std::move(self), "_master_transform"), "matrix");
    owner.matrix = vb_item(std::move(matrix), nb::make_tuple(vb_prefix(nb::int_(-1)), vb_prefix(nb::int_(-1))));
    BaseNamespace ns(std::move(globals));
    BasePair pair = vb_unpack_pair(vb_call1(vb_attr(ns.numpy("linalg"), "qr"), owner.matrix));
    owner.unused = std::move(pair.first);
    owner.upper_tri = std::move(pair.second);
    return vb_call0(vb_attr(vb_call1(ns.numpy("diag"), owner.upper_tri), "copy"));
}
nb::object vispy_base_order(nb::object self) { return vb_attr(vb_node(std::move(self)), "order"); }
void vispy_base_set_order(nb::object self, nb::object order) {
    vb_store(vb_node(self), "order", std::move(order));
    vb_call0(vb_attr(std::move(self), "_on_blending_change"));
}
void vispy_base_data_change(nb::object self, nb::dict globals) { vb_raise(BaseNamespace(std::move(globals)).get("NotImplementedError")); }
void vispy_base_refresh(nb::object self) { vb_call0(vb_attr(vb_node(std::move(self)), "update")); }
void vispy_base_visible(nb::object self) {
    nb::object visible = vb_attr(vb_layer(self), "visible");
    vb_store(vb_node(std::move(self)), "visible", std::move(visible));
}
void vispy_base_opacity(nb::object self) {
    nb::object opacity = vb_attr(vb_layer(self), "opacity");
    vb_store(vb_node(std::move(self)), "opacity", std::move(opacity));
}
void vb_expand_keywords(nb::handle callback, nb::dict& keywords, nb::object mapping) {
    if (_PyDict_MergeEx(keywords.ptr(), mapping.ptr(), 2) >= 0) return;
    if (PyErr_ExceptionMatches(PyExc_AttributeError)) {
        PyErr_Clear();
        nb::object label = vb_checked(_PyObject_FunctionStr(callback.ptr()));
        PyErr_Format(PyExc_TypeError, "%U argument after ** must be a mapping, not %.200s", label.ptr(), Py_TYPE(mapping.ptr())->tp_name);
    } else if (PyErr_ExceptionMatches(PyExc_KeyError)) {
        PyObject *type = nullptr, *value = nullptr, *traceback = nullptr;
        PyErr_Fetch(&type, &value, &traceback); PyErr_NormalizeException(&type, &value, &traceback);
        nb::object old_type = nb::steal<nb::object>(type), old_value = nb::steal<nb::object>(value), old_traceback = nb::steal<nb::object>(traceback);
        nb::object key = vb_item(vb_attr(old_value, "args"), nb::int_(0));
        nb::object label = vb_checked(_PyObject_FunctionStr(callback.ptr()));
        PyErr_Format(PyExc_TypeError, "%U got multiple values for keyword argument '%S'", label.ptr(), key.ptr());
    }
    throw nb::python_error();
}
void vispy_base_blending(nb::object self, nb::object event, nb::dict globals, VispyBaseLocals& owner) {
    owner.layout = VispyBaseLocals::Layout::Blending;
    owner.blending = vb_attr(vb_layer(self), "blending");
    BaseNamespace ns(std::move(globals));
    nb::object cast = ns.get("cast");
    nb::object type = ns.get("dict");
    nb::object modes = ns.get("BLENDING_MODES");
    nb::object mode = vb_item(std::move(modes), owner.blending);
    owner.blending_kwargs = vb_call0(vb_attr(vb_call2(std::move(cast), std::move(type), std::move(mode)), "copy"));
    if (vb_truth(vb_attr(self, "first_visible"))) {
        if (vb_test(owner.blending, nb::str("minimum"), Py_EQ)) {
            owner.src_color_blending = nb::str("one");
            owner.dst_color_blending = nb::str("zero");
        } else if (vb_test(owner.blending, nb::str("additive"), Py_EQ)) {
            owner.src_color_blending = nb::str("src_alpha");
            owner.dst_color_blending = nb::str("zero");
        } else {
            owner.src_color_blending = nb::str("src_alpha");
            owner.dst_color_blending = nb::str("one_minus_src_alpha");
        }
        nb::dict keywords;
        keywords["depth_test"] = vb_item(owner.blending_kwargs, nb::str("depth_test"));
        keywords["cull_face"] = nb::bool_(false);
        keywords["blend"] = nb::bool_(true);
        keywords["blend_func"] = nb::make_tuple(owner.src_color_blending, owner.dst_color_blending, nb::str("one"), nb::str("one"));
        keywords["blend_equation"] = nb::str("func_add");
        owner.blending_kwargs = std::move(keywords);
    }
    nb::object callback = vb_attr(vb_node(self), "set_gl_state");
    nb::dict keywords;
    vb_expand_keywords(callback, keywords, owner.blending_kwargs);
    vb_call_kw(std::move(callback), nb::tuple(), std::move(keywords));
    vb_call0(vb_attr(vb_node(std::move(self)), "update"));
}

void vispy_base_matrix(nb::object self, nb::dict globals, VispyBaseLocals& owner) {
    owner.layout = VispyBaseLocals::Layout::Matrix;
    owner.dims_displayed = vb_attr(vb_slice_input(self), "displayed");
    BaseNamespace ns(std::move(globals));
    nb::object length = ns.get("len");
    nb::object count = vb_call1(std::move(length), vb_attr(self, "_world_to_layer_units_scale"));
    nb::object ndim = vb_attr(vb_layer(self), "ndim");
    if (vb_test(std::move(count), std::move(ndim), Py_NE)) {
        vb_store(self, "_world_units", vb_attr(vb_layer(self), "units"));
        vb_store(self, "_world_to_layer_units_scale", vb_multiply(nb::make_tuple(nb::int_(1)), vb_attr(vb_layer(self), "ndim")));
    }
    owner.transform = vb_call1(vb_attr(vb_attr(vb_attr(vb_layer(self), "_transforms"), "simplified"), "set_slice"), owner.dims_displayed);
    {
        nb::list selected;
        nb::object iterator = vb_iter(owner.dims_displayed);
        for (;;) {
            PyObject* next = PyIter_Next(iterator.ptr());
            if (!next) { if (PyErr_Occurred()) throw nb::python_error(); break; }
            nb::object index = nb::steal<nb::object>(next);
            selected.append(vb_item(vb_attr(self, "_world_to_layer_units_scale"), std::move(index)));
        }
        owner.units_scale = vb_item(std::move(selected), vb_reverse());
    }
    owner.translate = vb_multiply(vb_item(vb_attr(owner.transform, "translate"), vb_reverse()), owner.units_scale);
    nb::object linear = vb_item(vb_attr(owner.transform, "linear_matrix"), nb::make_tuple(vb_reverse(), vb_reverse()));
    owner.matrix = vb_multiply(vb_attr(std::move(linear), "T"), owner.units_scale);
    bool adjust_volume = vb_truth(vb_attr(self, "_array_like"));
    if (adjust_volume) adjust_volume = vb_test(vb_attr(vb_slice_input(self), "ndisplay"), nb::int_(3), Py_EQ);
    if (adjust_volume) adjust_volume = vb_truth(vb_attr(vb_layer(self), "multiscale"));
    if (adjust_volume) {
        nb::object has = ns.get("hasattr");
        nb::object layer = vb_layer(self);
        adjust_volume = vb_truth(vb_call2(std::move(has), std::move(layer), nb::str("downsample_factors")));
    }
    if (adjust_volume) {
        nb::object asarray = ns.numpy("asarray");
        nb::object scale = vb_attr(vb_layer(self), "scale");
        owner.layer_scale = vb_item(vb_item(vb_call1(std::move(asarray), std::move(scale)), owner.dims_displayed), vb_reverse());
        nb::object get = ns.get("getattr");
        nb::object layer = vb_layer(self);
        owner.data_level = vb_call3(std::move(get), std::move(layer), nb::str("data_level"), nb::int_(0));
        owner.level_factors = vb_item(vb_attr(vb_layer(self), "downsample_factors"), owner.data_level);
        owner.displayed_downsample = vb_item(vb_item(owner.level_factors, owner.dims_displayed), vb_reverse());
        nb::object adjustment = vb_multiply(vb_divide(vb_subtract(owner.displayed_downsample, nb::int_(1)), nb::int_(2)), owner.layer_scale);
        owner.translate = vb_checked(PyNumber_InPlaceAdd(owner.translate.ptr(), adjustment.ptr()));
    }
    owner.affine_matrix = vb_call1(ns.numpy("eye"), nb::int_(4));
    nb::object rows = vb_item(vb_attr(owner.matrix, "shape"), nb::int_(0));
    nb::object columns = vb_item(vb_attr(owner.matrix, "shape"), nb::int_(1));
    vb_set(owner.affine_matrix, nb::make_tuple(vb_prefix(std::move(rows)), vb_prefix(std::move(columns))), owner.matrix);
    nb::object end = vb_call1(ns.get("len"), owner.translate);
    vb_set(owner.affine_matrix, nb::make_tuple(nb::int_(-1), vb_prefix(std::move(end))), owner.translate);
    nb::object zeros = ns.numpy("zeros");
    owner.child_offset = vb_call1(std::move(zeros), vb_call1(ns.get("len"), owner.dims_displayed));
    bool volume_children = vb_truth(vb_attr(self, "_array_like"));
    if (volume_children) volume_children = vb_test(vb_attr(vb_slice_input(self), "ndisplay"), nb::int_(3), Py_EQ);
    if (volume_children) volume_children = vb_truth(vb_attr(vb_layer(self), "multiscale"));
    if (volume_children) {
        owner.cp0 = vb_item(vb_item(vb_item(vb_attr(vb_layer(self), "corner_pixels"), nb::int_(0)), owner.dims_displayed), vb_reverse());
        nb::object any = ns.numpy("any");
        nb::object unequal = vb_compare(owner.cp0, nb::int_(0), Py_NE);
        if (vb_truth(vb_call1(std::move(any), std::move(unequal)))) {
            nb::object cast = vb_attr(owner.cp0, "astype");
            owner.child_offset = vb_negate(vb_call1(std::move(cast), ns.get("float")));
        }
    }
    bool image_children = vb_truth(vb_attr(self, "_array_like"));
    if (image_children) image_children = vb_test(vb_attr(vb_slice_input(self), "ndisplay"), nb::int_(2), Py_EQ);
    if (image_children) {
        owner.offset_matrix = vb_attr(vb_call1(vb_attr(vb_attr(vb_layer(self), "_data_to_world"), "set_slice"), owner.dims_displayed), "linear_matrix");
        nb::object negative = vb_negate(owner.offset_matrix);
        nb::object ones = ns.numpy("ones");
        nb::object size = vb_item(vb_attr(owner.offset_matrix, "shape"), nb::int_(1));
        owner.offset = vb_divide(vb_matmul(std::move(negative), vb_call1(std::move(ones), std::move(size))), nb::int_(2));
        owner.affine_offset = vb_call1(ns.numpy("eye"), nb::int_(4));
        nb::object shifted = vb_multiply(vb_item(owner.offset, vb_reverse()), owner.units_scale);
        nb::object stop = vb_call1(ns.get("len"), owner.offset);
        vb_set(owner.affine_offset, nb::make_tuple(nb::int_(-1), vb_prefix(std::move(stop))), std::move(shifted));
        owner.affine_matrix = vb_matmul(owner.affine_matrix, owner.affine_offset);
        if (vb_truth(vb_attr(vb_layer(self), "multiscale"))) {
            nb::object ones = ns.numpy("ones");
            nb::object size = vb_item(vb_attr(owner.offset_matrix, "shape"), nb::int_(1));
            nb::object half = vb_divide(vb_call1(std::move(ones), std::move(size)), nb::int_(2));
            nb::object corners = vb_item(vb_item(vb_item(vb_attr(vb_layer(self), "corner_pixels"), nb::int_(0)), owner.dims_displayed), vb_reverse());
            owner.child_offset = vb_subtract(std::move(half), std::move(corners));
        } else {
            nb::object full = ns.numpy("full");
            nb::object size = vb_item(vb_attr(owner.offset_matrix, "shape"), nb::int_(1));
            owner.child_offset = vb_call2(std::move(full), std::move(size), nb::float_(0.5));
        }
    }
    vb_store(vb_attr(self, "_master_transform"), "matrix", owner.affine_matrix);
    owner.child_matrix = vb_call1(ns.numpy("eye"), nb::int_(4));
    nb::object stop = vb_call1(ns.get("len"), owner.child_offset);
    vb_set(owner.child_matrix, nb::make_tuple(nb::int_(-1), vb_prefix(std::move(stop))), owner.child_offset);
    nb::object iterator = vb_iter(vb_attr(vb_node(std::move(self)), "children"));
    for (;;) {
        PyObject* next = PyIter_Next(iterator.ptr());
        if (!next) { if (PyErr_Occurred()) throw nb::python_error(); break; }
        owner.child = nb::steal<nb::object>(next);
        vb_store(vb_attr(owner.child, "transform"), "matrix", owner.child_matrix);
    }
}
void vispy_base_clipping_planes(nb::object self, nb::dict globals) {
    BaseNamespace ns(std::move(globals));
    nb::object has = ns.get("hasattr");
    nb::object node = vb_node(self);
    if (!vb_truth(vb_call2(std::move(has), std::move(node), nb::str("clipping_planes")))) return;
    has = ns.get("hasattr");
    nb::object layer = vb_layer(self);
    if (!vb_truth(vb_call2(std::move(has), std::move(layer), nb::str("experimental_clipping_planes")))) return;
    nb::object array = vb_call0(vb_attr(vb_attr(vb_layer(self), "experimental_clipping_planes"), "as_array"));
    nb::object reversed = vb_item(std::move(array), nb::make_tuple(nb::borrow<nb::object>(Py_Ellipsis), vb_reverse()));
    vb_store(vb_node(std::move(self)), "clipping_planes", std::move(reversed));
}
void vispy_base_view_direction(nb::object self, nb::object view, nb::object up) {}
void vispy_base_reset(nb::object self) {
    const char* callbacks[] = {"_on_visible_change", "_on_opacity_change", "_on_blending_change", "_on_matrix_change", "_on_experimental_clipping_planes_change", "_on_view_direction_change"};
    for (const char* callback : callbacks) vb_call0(vb_attr(self, callback));
}
void vispy_base_poll(nb::object self, nb::object event) {}
void vispy_base_close(nb::object self, nb::dict globals) {
    BaseNamespace ns(std::move(globals));
    nb::object disconnect = ns.get("disconnect_events");
    nb::object events = vb_attr(vb_layer(self), "events");
    vb_call2(std::move(disconnect), std::move(events), self);
    nb::object transform = vb_call0(ns.get("MatrixTransform"));
    vb_store(vb_node(self), "transform", std::move(transform));
    vb_store(vb_node(std::move(self)), "parent", nb::none());
}

struct VispyDisconnectEventsLocals {
    nb::object em;
    void clear() { nb::object old = std::move(em); }
};
int vb_disconnect_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) Py_VISIT(nb::inst_ptr<VispyDisconnectEventsLocals>(self)->em.ptr());
    return 0;
}
int vb_disconnect_clear(PyObject* self) {
    if (nb::inst_ready(self)) nb::inst_ptr<VispyDisconnectEventsLocals>(self)->clear();
    return 0;
}
PyType_Slot vb_disconnect_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(vb_disconnect_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(vb_disconnect_clear)}, {0, nullptr},
};
void vispy_disconnect_events(nb::object emitter, nb::object listener, VispyDisconnectEventsLocals& owner) {
    nb::object iterator = vb_iter(vb_call0(vb_attr(vb_attr(std::move(emitter), "emitters"), "values")));
    for (;;) {
        PyObject* next = PyIter_Next(iterator.ptr());
        if (!next) { if (PyErr_Occurred()) throw nb::python_error(); break; }
        owner.em = nb::steal<nb::object>(next);
        vb_call1(vb_attr(owner.em, "disconnect"), listener);
    }
}

} // namespace

void bind_vispy_base_layer(nb::module_& module) {
    nb::class_<VispyBaseLocals>(module, "VispyBaseLocals", nb::is_weak_referenceable(), nb::type_slots(vb_locals_slots))
        .def_prop_ro("reg", [](const VispyBaseLocals& s) { return vb_optional(s.reg); })
        .def_prop_ro("matrix", [](const VispyBaseLocals& s) { return vb_optional(s.matrix); })
        .def_prop_ro("_", [](const VispyBaseLocals& s) { return vb_optional(s.unused); })
        .def_prop_ro("upper_tri", [](const VispyBaseLocals& s) { return vb_optional(s.upper_tri); })
        .def_prop_ro("blending", [](const VispyBaseLocals& s) { return vb_optional(s.blending); })
        .def_prop_ro("blending_kwargs", [](const VispyBaseLocals& s) { return vb_optional(s.blending_kwargs); })
        .def_prop_ro("src_color_blending", [](const VispyBaseLocals& s) { return vb_optional(s.src_color_blending); })
        .def_prop_ro("dst_color_blending", [](const VispyBaseLocals& s) { return vb_optional(s.dst_color_blending); })
        .def_prop_ro("dims_displayed", [](const VispyBaseLocals& s) { return vb_optional(s.dims_displayed); })
        .def_prop_ro("transform", [](const VispyBaseLocals& s) { return vb_optional(s.transform); })
        .def_prop_ro("units_scale", [](const VispyBaseLocals& s) { return vb_optional(s.units_scale); })
        .def_prop_ro("translate", [](const VispyBaseLocals& s) { return vb_optional(s.translate); })
        .def_prop_ro("layer_scale", [](const VispyBaseLocals& s) { return vb_optional(s.layer_scale); })
        .def_prop_ro("data_level", [](const VispyBaseLocals& s) { return vb_optional(s.data_level); })
        .def_prop_ro("level_factors", [](const VispyBaseLocals& s) { return vb_optional(s.level_factors); })
        .def_prop_ro("displayed_downsample", [](const VispyBaseLocals& s) { return vb_optional(s.displayed_downsample); })
        .def_prop_ro("affine_matrix", [](const VispyBaseLocals& s) { return vb_optional(s.affine_matrix); })
        .def_prop_ro("child_offset", [](const VispyBaseLocals& s) { return vb_optional(s.child_offset); })
        .def_prop_ro("cp0", [](const VispyBaseLocals& s) { return vb_optional(s.cp0); })
        .def_prop_ro("offset_matrix", [](const VispyBaseLocals& s) { return vb_optional(s.offset_matrix); })
        .def_prop_ro("offset", [](const VispyBaseLocals& s) { return vb_optional(s.offset); })
        .def_prop_ro("affine_offset", [](const VispyBaseLocals& s) { return vb_optional(s.affine_offset); })
        .def_prop_ro("child_matrix", [](const VispyBaseLocals& s) { return vb_optional(s.child_matrix); })
        .def_prop_ro("child", [](const VispyBaseLocals& s) { return vb_optional(s.child); });
    nb::class_<VispyDisconnectEventsLocals>(module, "VispyDisconnectEventsLocals", nb::is_weak_referenceable(), nb::type_slots(vb_disconnect_slots))
        .def_prop_ro("em", [](const VispyDisconnectEventsLocals& s) { return vb_optional(s.em); });
    nb::class_<VispyUnitsIterator> units_type(module, "VispyUnitsIterator", nb::is_weak_referenceable(), nb::type_slots(vb_units_slots));
    units_type.def("__iter__", [](nb::handle self) { return nb::borrow<nb::object>(self); })
        .def("__next__", &vb_units_next)
        .def("send", [](nb::handle self, nb::object value) {
            auto& state = *nb::inst_ptr<VispyUnitsIterator>(self);
            if (state.closed) throw nb::stop_iteration();
            if (!state.started && !value.is_none()) throw nb::type_error("can't send non-None value to a just-started generator");
            return vb_units_next(self);
        }, nb::arg("value").none())
        .def("close", &vb_units_close).def("throw", &vb_units_throw);
    auto* type = reinterpret_cast<PyTypeObject*>(units_type.ptr());
    vb_units_base_dealloc = type->tp_dealloc;
    type->tp_dealloc = vb_units_dealloc;
    PyType_Modified(type);
    module.def("vispy_base_locals", []() { return VispyBaseLocals{}; });
    module.def("vispy_disconnect_events_locals", []() { return VispyDisconnectEventsLocals{}; });
    module.def("vispy_base_initialize", &vispy_base_initialize, nb::arg("self").none(), nb::arg("layer").none(), nb::arg("node").none(), nb::arg("font_info").none(), nb::arg("base_factory").none(), nb::arg("namespace"));
    module.def("vispy_base_world_units", &vispy_base_world_units, nb::arg("self").none());
    module.def("vispy_base_set_world_units", &vispy_base_set_world_units, nb::arg("self").none(), nb::arg("value").none(), nb::arg("namespace"));
    module.def("vispy_base_recalculate_units_scale", &vispy_base_recalculate_units_scale, nb::arg("self").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("vispy_base_master_transform", &vispy_base_master_transform, nb::arg("self").none(), nb::arg("namespace"));
    module.def("vispy_base_translate", &vispy_base_translate, nb::arg("self").none());
    module.def("vispy_base_scale", &vispy_base_scale, nb::arg("self").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("vispy_base_order", &vispy_base_order, nb::arg("self").none());
    module.def("vispy_base_set_order", &vispy_base_set_order, nb::arg("self").none(), nb::arg("order").none());
    module.def("vispy_base_data_change", &vispy_base_data_change, nb::arg("self").none(), nb::arg("namespace"));
    module.def("vispy_base_refresh", &vispy_base_refresh, nb::arg("self").none());
    module.def("vispy_base_visible", &vispy_base_visible, nb::arg("self").none());
    module.def("vispy_base_opacity", &vispy_base_opacity, nb::arg("self").none());
    module.def("vispy_base_blending", &vispy_base_blending, nb::arg("self").none(), nb::arg("event").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("vispy_base_matrix", &vispy_base_matrix, nb::arg("self").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("vispy_base_clipping_planes", &vispy_base_clipping_planes, nb::arg("self").none(), nb::arg("namespace"));
    module.def("vispy_base_view_direction", &vispy_base_view_direction, nb::arg("self").none(), nb::arg("view").none(), nb::arg("up").none());
    module.def("vispy_base_reset", &vispy_base_reset, nb::arg("self").none());
    module.def("vispy_base_poll", &vispy_base_poll, nb::arg("self").none(), nb::arg("event").none());
    module.def("vispy_base_close", &vispy_base_close, nb::arg("self").none(), nb::arg("namespace"));
    module.def("vispy_disconnect_events", &vispy_disconnect_events, nb::arg("emitter").none(), nb::arg("listener").none(), nb::arg("locals_owner"));
}

} // namespace nx
