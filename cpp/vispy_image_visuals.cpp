#include "arrays.hpp"

#include <array>
#include <initializer_list>
#include <utility>

namespace nx {
namespace {

nb::object iv_checked(PyObject* value) {
    if (!value) throw nb::python_error();
    return nb::steal<nb::object>(value);
}
nb::object iv_attr(nb::object value, const char* name) {
    return iv_checked(PyObject_GetAttrString(value.ptr(), name));
}
nb::object iv_item(nb::object value, nb::object key) {
    return iv_checked(PyObject_GetItem(value.ptr(), key.ptr()));
}
nb::object iv_call0(nb::object callable) {
    return iv_checked(PyObject_CallNoArgs(callable.ptr()));
}
nb::object iv_call1(nb::object callable, nb::object value) {
    return iv_checked(PyObject_CallOneArg(callable.ptr(), value.ptr()));
}
nb::object iv_call2(nb::object callable, nb::object first, nb::object second) {
    return iv_checked(PyObject_CallFunctionObjArgs(callable.ptr(), first.ptr(), second.ptr(), nullptr));
}
nb::object iv_call3(nb::object callable, nb::object first, nb::object second, nb::object third) {
    return iv_checked(PyObject_CallFunctionObjArgs(callable.ptr(), first.ptr(), second.ptr(), third.ptr(), nullptr));
}
nb::object iv_call_kw(nb::object callable, nb::tuple args, nb::dict keywords) {
    return iv_checked(PyObject_Call(callable.ptr(), args.ptr(), keywords.ptr()));
}
void iv_store(nb::handle self, const char* name, nb::object value) {
    if (PyObject_SetAttrString(self.ptr(), name, value.ptr()) < 0) throw nb::python_error();
}
void iv_set_item(nb::handle value, nb::object key, nb::object item) {
    if (PyObject_SetItem(value.ptr(), key.ptr(), item.ptr()) < 0) throw nb::python_error();
}
nb::object iv_iter(nb::object value) { return iv_checked(PyObject_GetIter(value.ptr())); }
nb::object iv_next(nb::handle iterator) {
    PyObject* value = PyIter_Next(iterator.ptr());
    if (!value && PyErr_Occurred()) throw nb::python_error();
    return nb::steal<nb::object>(value);
}
bool iv_truth(nb::object value) {
    const int result = PyObject_IsTrue(value.ptr());
    if (result < 0) throw nb::python_error();
    return result != 0;
}
bool iv_test(nb::object left, nb::object right, int operation) {
    return iv_truth(iv_checked(PyObject_RichCompare(left.ptr(), right.ptr(), operation)));
}
bool iv_contains(nb::object container, nb::object value) {
    const int result = PySequence_Contains(container.ptr(), value.ptr());
    if (result < 0) throw nb::python_error();
    return result != 0;
}
nb::object iv_add(nb::object left, nb::object right) {
    return iv_checked(PyNumber_Add(left.ptr(), right.ptr()));
}
nb::object iv_slice(nb::object start, nb::object stop) {
    return iv_checked(PySlice_New(start.ptr(), stop.ptr(), Py_None));
}
nb::object iv_optional(nb::object value) { return value.is_valid() ? value : nb::none(); }

// Each operation reads the executing facade's actual module namespace anew.
// This is a call-stack adapter, not a persistent provider/state snapshot.
struct ImageVisualNamespace {
    nb::dict globals;
    nb::object builtins;
    explicit ImageVisualNamespace(nb::dict value)
        : globals(std::move(value)), builtins(nb::borrow<nb::object>(PyEval_GetBuiltins())) {}
    nb::object get(const char* name) const {
        nb::object key = nb::str(name);
        PyObject* found = PyDict_GetItemWithError(globals.ptr(), key.ptr());
        if (found) return nb::borrow<nb::object>(found);
        if (PyErr_Occurred()) throw nb::python_error();
        found = PyObject_GetItem(builtins.ptr(), key.ptr());
        if (found) return nb::steal<nb::object>(found);
        if (!PyErr_ExceptionMatches(PyExc_KeyError)) throw nb::python_error();
        PyErr_Clear();
        nb::object message = iv_checked(PyUnicode_FromFormat("name '%s' is not defined", name));
        nb::object error = iv_call1(nb::borrow<nb::object>(PyExc_NameError), std::move(message));
        if (PyObject_SetAttrString(error.ptr(), "name", key.ptr()) < 0) throw nb::python_error();
        PyErr_SetObject(PyExc_NameError, error.ptr());
        throw nb::python_error();
    }
    void publish(const char* name, nb::object value) const {
        if (PyDict_SetItemString(globals.ptr(), name, value.ptr()) < 0) throw nb::python_error();
    }
};

struct ImageVisualPair { nb::object first, second; };
ImageVisualPair iv_unpack_pair(nb::object value) {
    nb::object iterator = iv_iter(std::move(value));
    nb::object first = iv_next(iterator);
    if (!first.is_valid()) throw nb::value_error("not enough values to unpack (expected 2, got 0)");
    nb::object second = iv_next(iterator);
    if (!second.is_valid()) throw nb::value_error("not enough values to unpack (expected 2, got 1)");
    nb::object extra = iv_next(iterator);
    if (extra.is_valid()) throw nb::value_error("too many values to unpack (expected 2)");
    return {std::move(first), std::move(second)};
}

// Only original assigned method locals survive native unwind in this owner.
// Controller/visual dictionaries and actual backend objects remain authoritative.
// There are no self, module namespace, callback or iterator owning edges here.
struct VispyImageVisualLocals {
    enum class Layout { Empty, Axis, Data, Child, Tiles, Color };
    Layout layout = Layout::Empty;
    nb::object h, w, unused, tile_list, tiles, ch, offset, dat, child, funcs;
    VispyImageVisualLocals() = default;
    VispyImageVisualLocals(VispyImageVisualLocals&&) = default;
    ~VispyImageVisualLocals() { clear(); }
    void clear() {
        std::array<nb::object, 10> old = {
            std::move(h), std::move(w), std::move(unused), std::move(tile_list),
            std::move(tiles), std::move(ch), std::move(offset), std::move(dat),
            std::move(child), std::move(funcs),
        };
        const Layout previous = layout;
        layout = Layout::Empty;
        const auto release = [&](std::initializer_list<size_t> order) {
            for (size_t index : order) old[index].reset();
        };
        switch (previous) {
            case Layout::Axis: release({5, 6}); break;
            case Layout::Data: release({4, 2, 5, 7, 8, 6}); break;
            case Layout::Child: release({8}); break;
            case Layout::Tiles: release({0, 1, 2, 3}); break;
            case Layout::Color: release({9}); break;
            case Layout::Empty: break;
        }
        for (nb::object& value : old) value.reset();
    }
};
int iv_locals_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) {
        const auto& state = *nb::inst_ptr<VispyImageVisualLocals>(self);
        Py_VISIT(state.h.ptr()); Py_VISIT(state.w.ptr()); Py_VISIT(state.unused.ptr());
        Py_VISIT(state.tile_list.ptr()); Py_VISIT(state.tiles.ptr()); Py_VISIT(state.ch.ptr());
        Py_VISIT(state.offset.ptr()); Py_VISIT(state.dat.ptr()); Py_VISIT(state.child.ptr()); Py_VISIT(state.funcs.ptr());
    }
    return 0;
}
int iv_locals_clear(PyObject* self) {
    if (nb::inst_ready(self)) nb::inst_ptr<VispyImageVisualLocals>(self)->clear();
    return 0;
}
PyType_Slot iv_locals_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(iv_locals_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(iv_locals_clear)}, {0, nullptr},
};

nb::object iv_strict_zip(nb::object self, nb::object second, const ImageVisualNamespace& ns) {
    nb::object callable = ns.get("zip");
    nb::object children = iv_attr(std::move(self), "adopted_children");
    nb::dict keywords;
    keywords["strict"] = nb::bool_(true);
    return iv_call_kw(std::move(callable), nb::make_tuple(std::move(children), std::move(second)), std::move(keywords));
}
void iv_transform_child(nb::object child, nb::object offset, const ImageVisualNamespace& ns) {
    nb::object callable = ns.get("STTransform");
    nb::object translate = iv_add(std::move(offset), nb::make_tuple(nb::int_(0)));
    nb::dict keywords;
    keywords["translate"] = std::move(translate);
    nb::object transform = iv_call_kw(std::move(callable), nb::tuple(), std::move(keywords));
    iv_store(std::move(child), "transform", std::move(transform));
}

void vispy_visual_tiled_initialize(nb::object self, nb::object data, nb::object tile_size,
                                   nb::object texture_format, nb::object super_bind) {
    iv_call0(iv_attr(self, "unfreeze"));
    iv_store(self, "texture_format", std::move(texture_format));
    iv_store(self, "adopted_children", nb::list());
    iv_store(self, "offsets", nb::list());
    iv_store(self, "tile_size", std::move(tile_size));
    iv_store(self, "data", nb::none());
    // The facade provider binds the actual lexical zero-argument super method
    // only here, after all original pre-super instance stores.
    iv_call1(iv_call1(std::move(super_bind), self), nb::list());
    iv_call1(iv_attr(std::move(self), "set_data"), std::move(data));
}
void vispy_visual_tiled_add_subvisual(nb::object self, nb::object visual) {
    iv_call1(iv_attr(iv_attr(std::move(self), "_subvisuals"), "append"), std::move(visual));
}
void vispy_visual_tiled_transform_changed(nb::object self, nb::object event, nb::dict globals) {
    ImageVisualNamespace ns(std::move(globals));
    nb::object callable = iv_attr(ns.get("BaseVisual"), "_transform_changed");
    iv_call1(std::move(callable), std::move(self));
}
void vispy_visual_tiled_axis_change(nb::object self, nb::dict globals, VispyImageVisualLocals& owner) {
    owner.layout = VispyImageVisualLocals::Layout::Axis;
    ImageVisualNamespace ns(std::move(globals));
    nb::object callable = ns.get("zip");
    nb::object children = iv_attr(self, "adopted_children");
    nb::object offsets = iv_attr(std::move(self), "offsets");
    nb::dict keywords;
    keywords["strict"] = nb::bool_(true);
    nb::object iterator = iv_iter(iv_call_kw(std::move(callable), nb::make_tuple(std::move(children), std::move(offsets)), std::move(keywords)));
    for (;;) {
        nb::object next = iv_next(iterator);
        if (!next.is_valid()) break;
        ImageVisualPair pair = iv_unpack_pair(std::move(next));
        owner.ch = std::move(pair.first);
        owner.offset = std::move(pair.second);
        iv_transform_child(owner.ch, owner.offset, ns);
    }
}
void vispy_visual_tiled_set_data(nb::object self, nb::object data, nb::dict globals, VispyImageVisualLocals& owner) {
    owner.layout = VispyImageVisualLocals::Layout::Data;
    ImageVisualNamespace ns(std::move(globals));
    nb::object callable = ns.get("make_tiles");
    nb::object tile_size = iv_attr(self, "tile_size");
    owner.tiles = iv_call2(std::move(callable), data, std::move(tile_size));
    {
        nb::object iterator = iv_iter(owner.tiles);
        nb::list offsets;
        nb::object of, unused;
        for (;;) {
            nb::object next = iv_next(iterator);
            if (!next.is_valid()) break;
            ImageVisualPair pair = iv_unpack_pair(std::move(next));
            // These are comprehension locals, not new outer method roots.
            of = std::move(pair.first);
            unused = std::move(pair.second);
            offsets.append(of);
        }
        iterator.reset();
        of.reset(); unused.reset();
        iv_store(self, "offsets", std::move(offsets));
    }
    iv_store(self, "data", std::move(data));
    callable = ns.get("len");
    nb::object children = iv_attr(self, "adopted_children");
    nb::object child_count = iv_call1(std::move(callable), std::move(children));
    callable = ns.get("len");
    nb::object tile_count = iv_call1(std::move(callable), owner.tiles);
    const bool reuse = iv_test(std::move(child_count), std::move(tile_count), Py_EQ);
    if (reuse) {
        nb::object iterator = iv_iter(iv_strict_zip(self, owner.tiles, ns));
        for (;;) {
            nb::object next = iv_next(iterator);
            if (!next.is_valid()) break;
            ImageVisualPair pair = iv_unpack_pair(std::move(next));
            owner.ch = std::move(pair.first);
            ImageVisualPair tile = iv_unpack_pair(std::move(pair.second));
            owner.unused = std::move(tile.first);
            owner.dat = std::move(tile.second);
            iv_call1(iv_attr(owner.ch, "set_data"), owner.dat);
        }
        return;
    }
    {
        nb::object iterator = iv_iter(iv_attr(self, "adopted_children"));
        for (;;) {
            nb::object next = iv_next(iterator);
            if (!next.is_valid()) break;
            owner.child = std::move(next);
            iv_store(owner.child, "parent", nb::none());
        }
    }
    iv_store(self, "_subvisuals", nb::list());
    {
        nb::object iterator = iv_iter(owner.tiles);
        nb::list replacements;
        nb::object unused, dat;
        for (;;) {
            nb::object next = iv_next(iterator);
            if (!next.is_valid()) break;
            ImageVisualPair pair = iv_unpack_pair(std::move(next));
            unused = std::move(pair.first);
            dat = std::move(pair.second);
            nb::object constructor = ns.get("Image");
            nb::dict keywords;
            keywords["data"] = dat;
            keywords["parent"] = self;
            keywords["texture_format"] = iv_attr(self, "texture_format");
            nb::object result = iv_call_kw(std::move(constructor), nb::tuple(), std::move(keywords));
            replacements.append(result);
        }
        iterator.reset();
        unused.reset(); dat.reset();
        iv_store(self, "adopted_children", std::move(replacements));
    }
    callable = ns.get("zip");
    children = iv_attr(self, "adopted_children");
    nb::object offsets = iv_attr(self, "offsets");
    nb::dict keywords;
    keywords["strict"] = nb::bool_(true);
    nb::object iterator = iv_iter(iv_call_kw(std::move(callable), nb::make_tuple(std::move(children), std::move(offsets)), std::move(keywords)));
    for (;;) {
        nb::object next = iv_next(iterator);
        if (!next.is_valid()) break;
        ImageVisualPair pair = iv_unpack_pair(std::move(next));
        owner.ch = std::move(pair.first);
        owner.offset = std::move(pair.second);
        iv_transform_child(owner.ch, owner.offset, ns);
        iv_call1(iv_attr(self, "add_subvisual"), owner.ch);
    }
}
void vispy_visual_tiled_gl_state(nb::object self, nb::tuple args, nb::dict kwargs, VispyImageVisualLocals& owner) {
    owner.layout = VispyImageVisualLocals::Layout::Child;
    nb::object iterator = iv_iter(iv_attr(std::move(self), "adopted_children"));
    for (;;) {
        nb::object next = iv_next(iterator);
        if (!next.is_valid()) break;
        owner.child = std::move(next);
        nb::object callable = iv_attr(owner.child, "set_gl_state");
        // Python's ** expansion supplies a fresh kwargs dictionary per call.
        nb::dict keywords;
        if (_PyDict_MergeEx(keywords.ptr(), kwargs.ptr(), 2) < 0) throw nb::python_error();
        iv_call_kw(std::move(callable), args, std::move(keywords));
    }
}
nb::object vispy_visual_tiled_getattr(nb::object self, nb::object name, nb::dict globals) {
    ImageVisualNamespace ns(std::move(globals));
    nb::object pass_through = ns.get("PASS_THROUGH_ATTRIBUTES");
    nb::object events = iv_checked(PySet_New(nullptr));
    nb::object event_name = nb::str("events");
    if (PySet_Add(events.ptr(), event_name.ptr()) < 0) throw nb::python_error();
    nb::object names = iv_checked(PyNumber_Or(pass_through.ptr(), events.ptr()));
    pass_through.reset(); events.reset(); event_name.reset();
    if (iv_contains(std::move(names), name)) {
        nb::object callable = ns.get("len");
        nb::object children = iv_attr(self, "adopted_children");
        if (iv_test(iv_call1(std::move(callable), std::move(children)), nb::int_(0), Py_GT)) {
            callable = ns.get("getattr");
            children = iv_attr(std::move(self), "adopted_children");
            nb::object first = iv_item(std::move(children), nb::int_(0));
            return iv_call2(std::move(callable), std::move(first), std::move(name));
        }
        return nb::none();
    }
    return iv_call1(iv_attr(std::move(self), "__getattribute__"), std::move(name));
}
void vispy_visual_tiled_setattr(nb::object self, nb::object name, nb::object value,
                              nb::object super_bind, nb::dict globals, VispyImageVisualLocals& owner) {
    owner.layout = VispyImageVisualLocals::Layout::Child;
    ImageVisualNamespace ns(std::move(globals));
    if (iv_contains(ns.get("PASS_THROUGH_ATTRIBUTES"), name)) {
        nb::object iterator = iv_iter(iv_attr(std::move(self), "adopted_children"));
        for (;;) {
            nb::object next = iv_next(iterator);
            if (!next.is_valid()) break;
            owner.child = std::move(next);
            nb::object callable = ns.get("setattr");
            iv_call3(std::move(callable), owner.child, name, value);
        }
        return;
    }
    nb::object callable = iv_call1(std::move(super_bind), self);
    iv_call2(std::move(callable), std::move(name), std::move(value));
}
nb::object vispy_visual_make_tiles(nb::object image, nb::object tile_size, nb::dict globals, VispyImageVisualLocals& owner) {
    owner.layout = VispyImageVisualLocals::Layout::Tiles;
    ImageVisualNamespace ns(std::move(globals));
    {
        nb::object iterator = iv_iter(iv_attr(image, "shape"));
        nb::object h = iv_next(iterator);
        if (!h.is_valid()) throw nb::value_error("not enough values to unpack (expected at least 2, got 0)");
        nb::object w = iv_next(iterator);
        if (!w.is_valid()) throw nb::value_error("not enough values to unpack (expected at least 2, got 1)");
        nb::object tail = iv_checked(PySequence_List(iterator.ptr()));
        iterator.reset();
        owner.h = std::move(h);
        owner.w = std::move(w);
        owner.unused = std::move(tail);
    }
    nb::object product = iv_attr(ns.get("itertools"), "product");
    nb::object callable = ns.get("range");
    nb::object rows = iv_call3(std::move(callable), nb::int_(0), owner.h, tile_size);
    callable = ns.get("range");
    nb::object columns = iv_call3(std::move(callable), nb::int_(0), owner.w, tile_size);
    nb::object iterator = iv_iter(iv_call2(std::move(product), std::move(rows), std::move(columns)));
    nb::list result;
    nb::object y, x;
    for (;;) {
        nb::object next = iv_next(iterator);
        if (!next.is_valid()) break;
        ImageVisualPair pair = iv_unpack_pair(std::move(next));
        y = std::move(pair.first);
        x = std::move(pair.second);
        nb::tuple offset = nb::make_tuple(x, y);
        nb::object y_stop = iv_add(y, tile_size);
        nb::object y_slice = iv_slice(y, std::move(y_stop));
        nb::object x_stop = iv_add(x, tile_size);
        nb::object x_slice = iv_slice(x, std::move(x_stop));
        nb::object tile = iv_item(image, nb::make_tuple(std::move(y_slice), std::move(x_slice)));
        result.append(nb::make_tuple(std::move(offset), std::move(tile)));
    }
    iterator.reset();
    y.reset(); x.reset();
    owner.tile_list = std::move(result);
    return owner.tile_list;
}

void vispy_visual_texture_initialize(nb::object self, nb::tuple args, nb::object texture_format,
                                     nb::dict kwargs, nb::object super_bind) {
    nb::object callable = iv_call1(std::move(super_bind), self);
    nb::dict keywords;
    keywords["texture_format"] = texture_format;
    if (_PyDict_MergeEx(keywords.ptr(), kwargs.ptr(), 2) < 0) throw nb::python_error();
    iv_call_kw(std::move(callable), std::move(args), std::move(keywords));
    iv_call0(iv_attr(self, "unfreeze"));
    iv_store(self, "_texture_format", std::move(texture_format));
    iv_call0(iv_attr(std::move(self), "freeze"));
}
nb::object vispy_visual_texture_format(nb::object self) { return iv_attr(std::move(self), "_texture_format"); }
nb::object iv_bounds(nb::object self, nb::object axis) {
    if (iv_attr(self, "_data").is_none()) return nb::none();
    if (iv_test(axis, nb::int_(1), Py_GT)) return nb::make_tuple(nb::int_(0), nb::int_(0));
    nb::object size = iv_attr(std::move(self), "size");
    nb::object extent = iv_item(std::move(size), std::move(axis));
    return nb::make_tuple(nb::int_(0), std::move(extent));
}
nb::object vispy_visual_image_bounds(nb::object self, nb::object axis, nb::object view) {
    return iv_bounds(std::move(self), std::move(axis));
}
nb::object vispy_visual_label_bounds(nb::object self, nb::object axis, nb::object view) {
    return iv_bounds(std::move(self), std::move(axis));
}
nb::object vispy_visual_label_color_transform(nb::object self, nb::dict globals, VispyImageVisualLocals& owner) {
    owner.layout = VispyImageVisualLocals::Layout::Color;
    ImageVisualNamespace ns(std::move(globals));
    nb::object callable = ns.get("Function");
    nb::object templates = iv_attr(self, "_func_templates");
    nb::object template_ = iv_item(std::move(templates), nb::str("red_to_luminance"));
    nb::object first = iv_call1(std::move(callable), std::move(template_));
    callable = ns.get("Function");
    nb::object map = iv_attr(iv_attr(std::move(self), "cmap"), "glsl_map");
    nb::object second = iv_call1(std::move(callable), std::move(map));
    nb::list funcs;
    funcs.append(first); funcs.append(second);
    first.reset(); second.reset();
    owner.funcs = std::move(funcs);
    callable = ns.get("FunctionChain");
    nb::dict keywords;
    keywords["funcs"] = owner.funcs;
    return iv_call_kw(std::move(callable), nb::tuple(), std::move(keywords));
}
void vispy_visual_volume_initialize(nb::object self, nb::tuple args, nb::dict kwargs,
                                    nb::object super_bind, nb::dict globals) {
    nb::object callable = iv_call1(std::move(super_bind), self);
    nb::dict keywords;
    if (_PyDict_MergeEx(keywords.ptr(), kwargs.ptr(), 2) < 0) throw nb::python_error();
    iv_call_kw(std::move(callable), std::move(args), std::move(keywords));
    iv_call0(iv_attr(self, "unfreeze"));
    iv_store(self, "clamp_at_border", nb::bool_(false));
    ImageVisualNamespace ns(std::move(globals));
    nb::object fast = iv_attr(ns.get("IsoCategoricalGradientMode"), "FAST");
    nb::object value = iv_attr(std::move(fast), "value");
    iv_store(self, "iso_gradient_mode", std::move(value));
    iv_call0(iv_attr(std::move(self), "freeze"));
}
nb::object vispy_visual_volume_gradient(nb::object self, nb::dict globals) {
    ImageVisualNamespace ns(std::move(globals));
    nb::object callable = ns.get("str");
    nb::object value = iv_attr(std::move(self), "_iso_gradient_mode");
    return iv_call1(std::move(callable), std::move(value));
}
void vispy_visual_volume_set_gradient(nb::object self, nb::object value, nb::dict globals) {
    ImageVisualNamespace ns(std::move(globals));
    nb::object callable = ns.get("IsoCategoricalGradientMode");
    nb::object converted = iv_call1(std::move(callable), value);
    iv_store(self, "_iso_gradient_mode", std::move(converted));
    nb::object smooth = iv_attr(ns.get("IsoCategoricalGradientMode"), "SMOOTH");
    const bool use_smooth = iv_test(value, std::move(smooth), Py_EQ);
    nb::object shader = ns.get(use_smooth ? "SMOOTH_GRADIENT_SHADER" : "FAST_GRADIENT_SHADER");
    nb::object program = iv_attr(self, "shared_program");
    iv_store(std::move(program), "frag", std::move(shader));
    nb::object clamp = iv_attr(self, "_clamp_at_border");
    program = iv_attr(self, "shared_program");
    iv_set_item(program, nb::str("u_clamp_at_border"), std::move(clamp));
    program.reset();
    iv_call0(iv_attr(std::move(self), "update"));
}
nb::object vispy_visual_volume_border(nb::object self) { return iv_attr(std::move(self), "_clamp_at_border"); }
void vispy_visual_volume_set_border(nb::object self, nb::object value) {
    iv_store(self, "_clamp_at_border", value);
    nb::object clamp = iv_attr(self, "_clamp_at_border");
    nb::object program = iv_attr(self, "shared_program");
    iv_set_item(program, nb::str("u_clamp_at_border"), std::move(clamp));
    program.reset();
    iv_call0(iv_attr(std::move(self), "update"));
}
void vispy_visual_volume_shaders(nb::dict globals) {
    ImageVisualNamespace ns(std::move(globals));
    nb::object base = iv_attr(ns.get("BaseVolume"), "_shaders");
    ns.publish("shaders", iv_call0(iv_attr(std::move(base), "copy")));
    nb::object fragment = iv_item(ns.get("shaders"), nb::str("fragment"));
    nb::object split = iv_call1(iv_attr(std::move(fragment), "split"), nb::str("void main()"));
    ImageVisualPair pair = iv_unpack_pair(std::move(split));
    ns.publish("before", std::move(pair.first));
    ns.publish("after", std::move(pair.second));
    {
        nb::object left = ns.get("before");
        nb::object right = ns.get("FUNCTION_DEFINITIONS");
        left = iv_add(std::move(left), std::move(right));
        right = ns.get("FAST_GRADIENT_DEFINITION");
        left = iv_add(std::move(left), std::move(right));
        right = ns.get("CALCULATE_COLOR_DEFINITION");
        left = iv_add(std::move(left), std::move(right));
        left = iv_add(std::move(left), nb::str("void main()"));
        right = ns.get("after");
        ns.publish("FAST_GRADIENT_SHADER", iv_add(std::move(left), std::move(right)));
    }
    {
        nb::object left = ns.get("before");
        nb::object right = ns.get("FUNCTION_DEFINITIONS");
        left = iv_add(std::move(left), std::move(right));
        right = ns.get("SMOOTH_GRADIENT_DEFINITION");
        left = iv_add(std::move(left), std::move(right));
        right = ns.get("CALCULATE_COLOR_DEFINITION");
        left = iv_add(std::move(left), std::move(right));
        left = iv_add(std::move(left), nb::str("void main()"));
        right = ns.get("after");
        ns.publish("SMOOTH_GRADIENT_SHADER", iv_add(std::move(left), std::move(right)));
    }
    nb::object shader = ns.get("FAST_GRADIENT_SHADER");
    nb::object shaders = ns.get("shaders");
    iv_set_item(shaders, nb::str("fragment"), std::move(shader));
    shaders.reset();
    base = iv_attr(ns.get("BaseVolume"), "_rendering_methods");
    ns.publish("rendering_methods", iv_call0(iv_attr(std::move(base), "copy")));
    nb::object snippets = ns.get("ISO_CATEGORICAL_SNIPPETS");
    nb::object methods = ns.get("rendering_methods");
    iv_set_item(methods, nb::str("iso_categorical"), std::move(snippets));
    methods.reset();
    snippets = ns.get("TRANSLUCENT_CATEGORICAL_SNIPPETS");
    methods = ns.get("rendering_methods");
    iv_set_item(methods, nb::str("translucent_categorical"), std::move(snippets));
}

} // namespace

void bind_vispy_image_visuals(nb::module_& module) {
    nb::class_<VispyImageVisualLocals>(module, "VispyImageVisualLocals", nb::is_weak_referenceable(), nb::type_slots(iv_locals_slots))
        .def_prop_ro("h", [](const VispyImageVisualLocals& s) { return iv_optional(s.h); })
        .def_prop_ro("w", [](const VispyImageVisualLocals& s) { return iv_optional(s.w); })
        .def_prop_ro("_", [](const VispyImageVisualLocals& s) { return iv_optional(s.unused); })
        .def_prop_ro("tile_list", [](const VispyImageVisualLocals& s) { return iv_optional(s.tile_list); })
        .def_prop_ro("tiles", [](const VispyImageVisualLocals& s) { return iv_optional(s.tiles); })
        .def_prop_ro("ch", [](const VispyImageVisualLocals& s) { return iv_optional(s.ch); })
        .def_prop_ro("offset", [](const VispyImageVisualLocals& s) { return iv_optional(s.offset); })
        .def_prop_ro("dat", [](const VispyImageVisualLocals& s) { return iv_optional(s.dat); })
        .def_prop_ro("child", [](const VispyImageVisualLocals& s) { return iv_optional(s.child); })
        .def_prop_ro("funcs", [](const VispyImageVisualLocals& s) { return iv_optional(s.funcs); });
    module.def("vispy_image_visual_locals", []() { return VispyImageVisualLocals{}; });
    module.def("vispy_visual_tiled_initialize", &vispy_visual_tiled_initialize, nb::arg("self").none(), nb::arg("data").none(), nb::arg("tile_size").none(), nb::arg("texture_format").none(), nb::arg("super_bind").none());
    module.def("vispy_visual_tiled_add_subvisual", &vispy_visual_tiled_add_subvisual, nb::arg("self").none(), nb::arg("visual").none());
    module.def("vispy_visual_tiled_transform_changed", &vispy_visual_tiled_transform_changed, nb::arg("self").none(), nb::arg("event").none(), nb::arg("namespace"));
    module.def("vispy_visual_tiled_axis_change", &vispy_visual_tiled_axis_change, nb::arg("self").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("vispy_visual_tiled_set_data", &vispy_visual_tiled_set_data, nb::arg("self").none(), nb::arg("data").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("vispy_visual_tiled_gl_state", &vispy_visual_tiled_gl_state, nb::arg("self").none(), nb::arg("args"), nb::arg("kwargs"), nb::arg("locals_owner"));
    module.def("vispy_visual_tiled_getattr", &vispy_visual_tiled_getattr, nb::arg("self").none(), nb::arg("name").none(), nb::arg("namespace"));
    module.def("vispy_visual_tiled_setattr", &vispy_visual_tiled_setattr, nb::arg("self").none(), nb::arg("name").none(), nb::arg("value").none(), nb::arg("super_bind").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("vispy_visual_make_tiles", &vispy_visual_make_tiles, nb::arg("image").none(), nb::arg("tile_size").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("vispy_visual_texture_initialize", &vispy_visual_texture_initialize, nb::arg("self").none(), nb::arg("args"), nb::arg("texture_format").none(), nb::arg("kwargs"), nb::arg("super_bind").none());
    module.def("vispy_visual_texture_format", &vispy_visual_texture_format, nb::arg("self").none());
    module.def("vispy_visual_image_bounds", &vispy_visual_image_bounds, nb::arg("self").none(), nb::arg("axis").none(), nb::arg("view").none());
    module.def("vispy_visual_label_bounds", &vispy_visual_label_bounds, nb::arg("self").none(), nb::arg("axis").none(), nb::arg("view").none());
    module.def("vispy_visual_label_color_transform", &vispy_visual_label_color_transform, nb::arg("self").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    module.def("vispy_visual_volume_initialize", &vispy_visual_volume_initialize, nb::arg("self").none(), nb::arg("args"), nb::arg("kwargs"), nb::arg("super_bind").none(), nb::arg("namespace"));
    module.def("vispy_visual_volume_gradient", &vispy_visual_volume_gradient, nb::arg("self").none(), nb::arg("namespace"));
    module.def("vispy_visual_volume_set_gradient", &vispy_visual_volume_set_gradient, nb::arg("self").none(), nb::arg("value").none(), nb::arg("namespace"));
    module.def("vispy_visual_volume_border", &vispy_visual_volume_border, nb::arg("self").none());
    module.def("vispy_visual_volume_set_border", &vispy_visual_volume_set_border, nb::arg("self").none(), nb::arg("value").none());
    module.def("vispy_visual_volume_shaders", &vispy_visual_volume_shaders, nb::arg("namespace"));
}

} // namespace nx
