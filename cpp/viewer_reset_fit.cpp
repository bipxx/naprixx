#include "arrays.hpp"
#include <array>
#include <functional>
#include <initializer_list>
#include <utility>
#include <vector>

namespace nx {
namespace {
nb::object rf_check(PyObject* value) {
    if (!value) throw nb::python_error();
    return nb::steal<nb::object>(value);
}
nb::object rf_attr(nb::object receiver, const char* name) {
    return rf_check(PyObject_GetAttrString(receiver.ptr(), name));
}
nb::object rf_item(nb::object receiver, nb::object index) {
    return rf_check(PyObject_GetItem(receiver.ptr(), index.ptr()));
}
void rf_assign(nb::object& target, nb::object value) {
    nb::object previous = std::move(target);
    target = std::move(value);
    previous.reset();
}
bool rf_truth(nb::object value) {
    int result = PyObject_IsTrue(value.ptr());
    value.reset();
    if (result < 0) throw nb::python_error();
    return result != 0;
}
using RFExpr = std::function<nb::object()>;
using RFKeyword = std::pair<const char*, RFExpr>;
// The vectors hold only this expression's genuine transient CALL operands.
// They do not copy camera/grid state or retain a second model authority.
nb::object rf_call(nb::object function,
                   std::initializer_list<RFExpr> positional = {},
                   std::initializer_list<RFKeyword> keywords = {},
                   nb::object keyword_names = {}) {
    std::vector<nb::object> values;
    values.reserve(positional.size() + keywords.size());
    for (const auto& expression : positional) values.push_back(expression());
    for (const auto& keyword : keywords) values.push_back(keyword.second());
    // These are the genuine stable compiled facade KW_NAMES constants.
    // No keyword tuple/key is synthesized or copied at each source call.
    nb::object names = std::move(keyword_names);
    if (keywords.size() && (!names.is_valid() || !PyTuple_CheckExact(names.ptr()) ||
        PyTuple_GET_SIZE(names.ptr()) != static_cast<Py_ssize_t>(keywords.size())))
        throw nb::type_error("source keyword-name metadata must be the matching tuple");
    std::vector<PyObject*> operands;
    operands.reserve(values.size());
    for (const auto& value : values) operands.push_back(value.ptr());
    PyObject* result = PyObject_Vectorcall(function.ptr(), operands.data(),
                                          positional.size(), names.ptr());
    function.reset();
    for (auto& value : values) value.reset();
    names.reset();
    return rf_check(result);
}
nb::object rf_binary(RFExpr left, RFExpr right, binaryfunc operation) {
    nb::object lhs = left();
    nb::object rhs = right();
    PyObject* result = operation(lhs.ptr(), rhs.ptr());
    lhs.reset();
    rhs.reset();
    return rf_check(result);
}
nb::object rf_compare(RFExpr left, RFExpr right, int operation) {
    nb::object lhs = left();
    nb::object rhs = right();
    PyObject* result = PyObject_RichCompare(lhs.ptr(), rhs.ptr(), operation);
    lhs.reset();
    rhs.reset();
    return rf_check(result);
}
void rf_publish(RFExpr value, RFExpr receiver, const char* name) {
    nb::object result = value();
    nb::object target = receiver();
    int status = PyObject_SetAttrString(target.ptr(), name, result.ptr());
    result.reset();
    target.reset();
    if (status < 0) throw nb::python_error();
}
nb::tuple rf_tuple(std::initializer_list<RFExpr> expressions) {
    nb::tuple result = nb::steal<nb::tuple>(PyTuple_New(
        static_cast<Py_ssize_t>(expressions.size())));
    if (!result.is_valid()) throw nb::python_error();
    Py_ssize_t index = 0;
    for (const auto& expression : expressions) {
        nb::object value = expression();
        PyTuple_SET_ITEM(result.ptr(), index++, value.release().ptr());
    }
    return result;
}
std::array<nb::object, 3> rf_unpack_three(nb::object value) {
    PyObject* raw = PyObject_GetIter(value.ptr());
    if (!raw) {
        nb::python_error error;
        if (error.matches(PyExc_TypeError) &&
            Py_TYPE(value.ptr())->tp_iter == nullptr &&
            !PySequence_Check(value.ptr())) {
            PyErr_Format(PyExc_TypeError, "cannot unpack non-iterable %.200s object",
                         Py_TYPE(value.ptr())->tp_name);
            throw nb::python_error();
        }
        throw error;
    }
    nb::object iterator = nb::steal<nb::object>(raw);
    std::array<nb::object, 3> result;
    for (size_t index = 0; index < result.size(); ++index) {
        PyObject* item = PyIter_Next(iterator.ptr());
        if (!item) {
            if (PyErr_Occurred()) throw nb::python_error();
            PyErr_Format(PyExc_ValueError,
                         "not enough values to unpack (expected 3, got %zu)", index);
            throw nb::python_error();
        }
        result[index] = nb::steal<nb::object>(item);
    }
    nb::object extra = nb::steal<nb::object>(PyIter_Next(iterator.ptr()));
    if (PyErr_Occurred()) throw nb::python_error();
    if (extra.is_valid()) {
        PyErr_SetString(PyExc_ValueError, "too many values to unpack (expected 3)");
        throw nb::python_error();
    }
    iterator.reset();
    value.reset();
    return result;
}
struct ResetFitGlobals {
    nb::dict values;
    nb::object builtins;
    explicit ResetFitGlobals(nb::dict source)
        : values(std::move(source)),
          builtins(nb::borrow<nb::object>(PyEval_GetBuiltins())) {}
    nb::object get(const char* name) const {
        nb::object key = nb::str(name);
        PyObject* value = PyDict_GetItemWithError(values.ptr(), key.ptr());
        if (value) return nb::borrow<nb::object>(value);
        if (PyErr_Occurred()) throw nb::python_error();
        value = PyObject_GetItem(builtins.ptr(), key.ptr());
        if (value) return nb::steal<nb::object>(value);
        if (!PyErr_ExceptionMatches(PyExc_KeyError)) throw nb::python_error();
        PyErr_Clear();
        nb::object message = rf_check(PyUnicode_FromFormat("name '%s' is not defined", name));
        nb::object error = rf_call(nb::borrow<nb::object>(PyExc_NameError),
                                  {[&]() -> nb::object { return message; }});
        if (PyObject_SetAttrString(error.ptr(), "name", key.ptr()) < 0)
            throw nb::python_error();
        PyErr_SetObject(PyExc_NameError, error.ptr());
        throw nb::python_error();
    }
};
// Only the assigned source locals exist here. The flag selects their source
// cleanup order; it holds no Viewer/Canvas/camera/grid model state.
struct ViewerResetFitLocals {
    nb::object extent, underscore, scene_size, corner, result;
    bool scene_parameters = false;
    explicit ViewerResetFitLocals(bool scene = false) : scene_parameters(scene) {}
    ViewerResetFitLocals(ViewerResetFitLocals&&) = default;
    void clear() {
        std::array<nb::object, 5> values;
        if (scene_parameters) {
            values = {std::move(extent), std::move(scene_size), std::move(corner),
                      std::move(underscore), std::move(result)};
        } else {
            values = {std::move(underscore), std::move(scene_size), std::move(corner),
                      std::move(result), std::move(extent)};
        }
        for (auto& value : values) value.reset();
    }
    ~ViewerResetFitLocals() { clear(); }
};
int rf_owner_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) {
        auto& owner = *nb::inst_ptr<ViewerResetFitLocals>(self);
        for (nb::object* value : {&owner.extent, &owner.underscore,
                                 &owner.scene_size, &owner.corner, &owner.result})
            Py_VISIT(value->ptr());
    }
    return 0;
}
int rf_owner_clear(PyObject* self) {
    if (nb::inst_ready(self)) nb::inst_ptr<ViewerResetFitLocals>(self)->clear();
    return 0;
}
PyType_Slot rf_owner_slots[] = {
    {Py_tp_traverse, reinterpret_cast<void*>(rf_owner_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(rf_owner_clear)}, {0, nullptr}};
#define RX(value) [&]() -> nb::object { return (value); }
void viewer_reset_view(nb::object self, nb::object layers, nb::object margin,
                       nb::object reset_camera_angle, nb::object reset_angles,
                       nb::tuple reset_keyword_names) {
    if (rf_truth(rf_compare(RX(rf_attr(rf_attr(self, "dims"), "ndisplay")),
                            RX(nb::int_(3)), Py_EQ)) && rf_truth(reset_camera_angle))
        rf_publish(RX(reset_angles), RX(rf_attr(rf_attr(self, "scene"), "camera")), "angles");
    rf_call(rf_attr(self, "fit_to_view"), {},
            {{"layers", RX(layers)}, {"margin", RX(margin)}}, reset_keyword_names);
}
void viewer_fit_to_view(nb::object self, nb::object layers, nb::object margin,
                        nb::dict namespace_, ViewerResetFitLocals& owner,
                        nb::tuple scene_keyword_names, nb::tuple event_keyword_names) {
    ResetFitGlobals ns(std::move(namespace_));
    auto values = rf_unpack_three(rf_call(rf_attr(self, "_get_scene_parameters"), {},
                                          {{"layers", RX(layers)}}, scene_keyword_names));
    rf_assign(owner.underscore, std::move(values[0]));
    rf_assign(owner.scene_size, std::move(values[1]));
    rf_assign(owner.corner, std::move(values[2]));
    rf_assign(owner.result, rf_call(rf_attr(ns.get("_native"), "fit_view"), {
        RX(owner.corner), RX(owner.scene_size), RX(rf_attr(rf_attr(self, "dims"), "ndisplay")),
        RX(rf_attr(rf_attr(rf_attr(self, "scene"), "camera"), "view_direction")),
        RX(rf_attr(rf_attr(rf_attr(self, "scene"), "camera"), "up_direction")),
        RX(rf_call(rf_attr(rf_attr(self, "canvas"), "viewbox_size"), {RX(rf_attr(self, "layers"))})),
        RX(rf_attr(rf_attr(self, "canvas"), "size")), RX(margin)}));
    rf_publish(RX(rf_attr(owner.result, "center")), RX(rf_attr(rf_attr(self, "scene"), "camera")), "center");
    rf_publish(RX(rf_attr(owner.result, "zoom")), RX(rf_attr(rf_attr(self, "scene"), "camera")), "zoom");
    rf_call(rf_attr(rf_attr(self, "events"), "reset_view"), {}, {
        {"center", RX(rf_attr(rf_attr(rf_attr(self, "scene"), "camera"), "center"))},
        {"zoom", RX(rf_attr(rf_attr(rf_attr(self, "scene"), "camera"), "zoom"))},
        {"angles", RX(rf_attr(rf_attr(rf_attr(self, "scene"), "camera"), "angles"))}}, event_keyword_names);
}
nb::object viewer_get_scene_parameters(nb::object self, nb::object layers,
                                       ViewerResetFitLocals& owner, nb::tuple extent_keyword_names) {
    rf_assign(owner.extent, rf_call(rf_attr(self, "_sliced_extent_world_augmented"), {},
                                   {{"layers", RX(layers)}}, extent_keyword_names));
    rf_assign(owner.scene_size, rf_binary(RX(rf_item(owner.extent, nb::int_(1))),
                                          RX(rf_item(owner.extent, nb::int_(0))),
                                          PyNumber_Subtract));
    rf_assign(owner.corner, rf_item(owner.extent, nb::int_(0)));
    return rf_tuple({RX(owner.extent), RX(owner.scene_size), RX(owner.corner)});
}
nb::object canvas_viewbox_size(nb::object self, nb::object layers) {
    return rf_call(rf_attr(rf_attr(rf_attr(self, "grid"), "_native_state"), "viewbox_size"), {
        RX(rf_attr(self, "size")),
        RX(rf_call(rf_attr(rf_attr(self, "grid"), "_layout"), {RX(layers)}))});
}
#undef RX
} // namespace
void bind_viewer_reset_fit(nb::module_& module) {
    auto owner = nb::class_<ViewerResetFitLocals>(module, "ViewerResetFitLocals",
        nb::is_weak_referenceable(), nb::type_slots(rf_owner_slots));
#define RF_FIELD(name) owner.def_prop_ro(#name, [](const ViewerResetFitLocals& value) { return value.name.is_valid() ? value.name : nb::none(); });
    RF_FIELD(extent) RF_FIELD(scene_size) RF_FIELD(corner) RF_FIELD(result)
#undef RF_FIELD
    owner.def_prop_ro("_", [](const ViewerResetFitLocals& value) {
        return value.underscore.is_valid() ? value.underscore : nb::none();
    });
    module.def("viewer_reset_fit_locals", [](bool scene_parameters) {
        return ViewerResetFitLocals(scene_parameters);
    }, nb::arg("scene_parameters") = false);
    module.def("viewer_reset_view", &viewer_reset_view, nb::arg("self").none(),
        nb::arg("layers").none(), nb::arg("margin").none(),
        nb::arg("reset_camera_angle").none(), nb::arg("reset_angles").none(),
        nb::arg("reset_keyword_names"));
    module.def("viewer_fit_to_view", &viewer_fit_to_view, nb::arg("self").none(),
        nb::arg("layers").none(), nb::arg("margin").none(), nb::arg("namespace"),
        nb::arg("locals_owner"), nb::arg("scene_keyword_names"), nb::arg("event_keyword_names"));
    module.def("viewer_get_scene_parameters", &viewer_get_scene_parameters,
        nb::arg("self").none(), nb::arg("layers").none(), nb::arg("locals_owner"),
        nb::arg("extent_keyword_names"));
    module.def("canvas_viewbox_size", &canvas_viewbox_size, nb::arg("self").none(),
        nb::arg("layers").none());
}
} // namespace nx
