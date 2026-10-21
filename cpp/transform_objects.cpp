#include "transform_bindings.hpp"
#include "event_protocol.hpp"
#include "collection_protocol.hpp"
#include <nanobind/stl/string.h>
#include <array>
#include <string>
#include <utility>
#include <vector>

// STAGED COMPLETE OBJECT BOUNDARY. Not a build input or installed evidence.
// These operations retain the existing native algorithms, with source globals,
// original callback capture points and genuine deferred lexical-super glue.
// The TransformNamespaceBase staged separately owns the one live dictionary.
// Per-call locals below retain only original assigned local references; they
// contain no self, namespace, cache, array copy or controller state authority.
namespace nx {
namespace {
namespace nb = nanobind;
using events::checked;
using events::truth;
using events::contains;
using events::compare;
using events::item;
using events::index;
using events::set_item;
using events::set_attr;
using events::formatted;
using events::message;

struct TransformGlobals {
    nb::dict globals;
    nb::object builtins;
    explicit TransformGlobals(nb::dict values)
        : globals(std::move(values)), builtins(nb::borrow<nb::object>(PyEval_GetBuiltins())) {}
    nb::object get(const char* name) const {
        PyObject* value = PyDict_GetItemString(globals.ptr(), name);
        if (value) return nb::borrow<nb::object>(value);
        if (PyErr_Occurred()) throw nb::python_error();
        value = PyDict_Check(builtins.ptr()) ? PyDict_GetItemString(builtins.ptr(), name) : nullptr;
        if (value) return nb::borrow<nb::object>(value);
        if (PyErr_Occurred()) throw nb::python_error();
        auto text = checked(PyUnicode_FromFormat("name '%s' is not defined", name));
        auto exception = checked(PyObject_CallOneArg(PyExc_NameError, text.ptr()));
        if (PyObject_SetAttrString(exception.ptr(), "name", nb::str(name).ptr()) < 0) throw nb::python_error();
        PyErr_SetObject(PyExc_NameError, exception.ptr());
        throw nb::python_error();
    }
};
template <typename Result, typename... Args>
auto from_globals(Result (*function)(const TransformGlobals&, Args...)) {
    return [function](Args... values, nb::dict namespace_) -> Result {
        TransformGlobals scope(std::move(namespace_));
        return function(scope, values...);
    };
}
nb::object attr(nb::object value, const char* name) { return checked(PyObject_GetAttrString(value.ptr(), name)); }
nb::object own(nb::handle value) { return nb::borrow<nb::object>(value); }
nb::object call0(nb::object callback) { return checked(PyObject_CallNoArgs(callback.ptr())); }
nb::object call1(nb::object callback, nb::object value) { return checked(PyObject_CallOneArg(callback.ptr(), value.ptr())); }
nb::object call2(nb::object callback, nb::object first, nb::object second) { return checked(PyObject_CallFunctionObjArgs(callback.ptr(), first.ptr(), second.ptr(), nullptr)); }
nb::object call3(nb::object callback, nb::object first, nb::object second, nb::object third) { return checked(PyObject_CallFunctionObjArgs(callback.ptr(), first.ptr(), second.ptr(), third.ptr(), nullptr)); }
nb::object add(nb::handle a, nb::handle b) { return checked(PyNumber_Add(a.ptr(), b.ptr())); }
nb::object subtract(nb::handle a, nb::handle b) { return checked(PyNumber_Subtract(a.ptr(), b.ptr())); }
nb::object multiply(nb::handle a, nb::handle b) { return checked(PyNumber_Multiply(a.ptr(), b.ptr())); }
nb::object divide(nb::handle a, nb::handle b) { return checked(PyNumber_TrueDivide(a.ptr(), b.ptr())); }
nb::object matmul(nb::handle a, nb::handle b) { return checked(PyNumber_MatrixMultiply(a.ptr(), b.ptr())); }
nb::object negate(nb::handle value) { return checked(PyNumber_Negative(value.ptr())); }
nb::object slice(nb::handle start, nb::handle stop, nb::handle step = nb::none()) { return checked(PySlice_New(start.ptr(), stop.ptr(), step.ptr())); }
nb::object block() { auto s = slice(nb::none(), nb::int_(-1)); return nb::make_tuple(s, s); }
nb::object shift_key() { return nb::make_tuple(slice(nb::none(), nb::int_(-1)), -1); }
nb::object cache(nb::handle self) { return self.attr("_cache_dict"); }
bool cached(nb::handle self, const char* key) { return contains(cache(self), nb::str(key)); }
nb::object cached_value(nb::handle self, const char* key) { return item(cache(self), nb::str(key)); }
void publish(nb::handle self, const char* key, nb::object value) { auto target = cache(self); set_item(target, nb::str(key), value); }
nb::object np(const TransformGlobals& ns, const char* field) { return attr(ns.get("np"), field); }
nb::object length(const TransformGlobals& ns, nb::object value) { return call1(ns.get("len"), std::move(value)); }
nb::object vector(const TransformGlobals& ns, const char* name, nb::handle value, nb::object ndim) {
    auto callback = ns.get(name);
    return callback(value, nb::arg("ndim") = ndim);
}
nb::object super_attr(nb::handle provider, nb::handle self, const char* field) {
    return attr(call1(own(provider), own(self)), field);
}
void clean(nb::handle self) { call0(self.attr("_clean_cache")); }
[[noreturn]] void raise_call(nb::object factory, nb::object text) {
    auto exception = call1(std::move(factory), std::move(text));
    if (PyExceptionClass_Check(exception.ptr())) {
        auto value = call0(exception);
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
nb::object triangular_error(nb::handle value) {
    return message({nb::str("Only upper triangular or lower triangular matrices are accepted for shear, got "), formatted(value), nb::str(". For other matrices, set the affine_matrix or linear_matrix directly.")});
}

enum Field { Transforms, Tr, Val, Scale, Translate, Ndim, Linear, Rotate, Shear, Labels, Units, I, N, NotAxes, Matrix, AffineMatrix, Axes, AxesLabels, FieldCount };
const char* field_names[] = {"transforms", "tr", "val", "scale", "translate", "ndim", "linear_matrix", "rotate", "shear", "axis_labels", "units", "i", "n", "not_axes", "matrix", "affine_matrix", "axes", "axes_labels"};
struct TransformObjectLocals {
    std::array<nb::object, FieldCount> refs;
    std::array<int, FieldCount> order{};
    size_t count = 0;
    TransformObjectLocals() = default;
    TransformObjectLocals(TransformObjectLocals&&) = default;
    ~TransformObjectLocals() { clear(); }
    nb::object& operator[](Field field) { return refs[field]; }
    void clear() {
        auto detached = std::move(refs);
        for (size_t j = 0; j < count; ++j) detached[order[j]].reset();
        for (auto& value : detached) value.reset();
    }
};
void replace(nb::object& target, nb::object value) { auto previous = std::move(target); target = std::move(value); previous.reset(); }
TransformObjectLocals object_locals(const std::string& layout, nb::tuple initial) {
    TransformObjectLocals owner;
    std::vector<int> fields;
    size_t initials = 0;
    // Each order follows original co_varnames, not the order stores happen.
    if (layout == "chain_init") { fields = {Transforms, Tr}; initials = 1; }
    else if (layout == "chain_delete") fields = {Val};
    else if (layout == "scale_init") { fields = {Scale, Translate}; initials = 2; }
    else if (layout == "scale_compose") fields = {Scale, Translate};
    else if (layout == "scale_expand") fields = {N, NotAxes, Scale, Translate};
    else if (layout == "affine_init") { fields = {Translate, Linear, Ndim, Rotate, Shear}; initials = 5; }
    else if (layout == "affine_labels") { fields = {Labels}; initials = 1; }
    else if (layout == "affine_units") { fields = {Units}; initials = 1; }
    else if (layout == "affine_scale") { fields = {Scale, I}; initials = 1; }
    else if (layout == "affine_shear") { fields = {Shear}; initials = 1; }
    else if (layout == "affine_matrix") fields = {Matrix};
    else if (layout == "affine_compose") fields = {AffineMatrix};
    else if (layout == "affine_slice") { fields = {Axes, Linear, Units, AxesLabels}; initials = 1; }
    else if (layout == "affine_replace") fields = {Linear, Translate};
    else if (layout == "affine_expand") fields = {N, NotAxes, Linear, Translate};
    else if (layout == "composite_init") { fields = {Ndim}; initials = 1; }
    else if (layout == "composite_expand") fields = {N, NotAxes, Rotate, Shear, Translate, Scale};
    else { PyErr_SetString(PyExc_ValueError, "unknown transform local layout"); throw nb::python_error(); }
    if (nb::len(initial) != initials) { PyErr_SetString(PyExc_TypeError, "incorrect transform local initial values"); throw nb::python_error(); }
    for (int field : fields) owner.order[owner.count++] = field;
    for (size_t j = 0; j < initials; ++j) owner.refs[owner.order[j]] = own(initial[j]);
    return owner;
}
int locals_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) for (const auto& value : nb::inst_ptr<TransformObjectLocals>(self)->refs) Py_VISIT(value.ptr());
    return 0;
}
int locals_clear(PyObject* self) { if (nb::inst_ready(self)) nb::inst_ptr<TransformObjectLocals>(self)->clear(); return 0; }
PyType_Slot locals_slots[] = {{Py_tp_traverse, reinterpret_cast<void*>(locals_traverse)}, {Py_tp_clear, reinterpret_cast<void*>(locals_clear)}, {0, nullptr}};

// Generator policy lives here; a genuine Python generator delegates protocol
// only. The iterator retains the original .0/current binding and function
// namespace/builtins while suspended, with no persistent transform root.
struct TransformExpressionIterator {
    enum Kind { Diagonal, InitialLabels, PlainLabels } kind = Diagonal;
    nb::object iterator, current, globals, builtins;
    bool started = false, running = false, closed = false;
    TransformExpressionIterator() = default;
    TransformExpressionIterator(TransformExpressionIterator&&) = default;
    ~TransformExpressionIterator() { clear(); }
    void clear() {
        std::array<nb::object, 4> previous = {std::move(iterator), std::move(current), std::move(globals), std::move(builtins)};
        closed = true;
        for (auto& edge : previous) edge.reset();
    }
    nb::object next() {
        if (running) { PyErr_SetString(PyExc_ValueError, "generator already executing"); throw nb::python_error(); }
        if (closed) throw nb::stop_iteration();
        running = true;
        started = true;
        try {
            auto value = nb::steal<nb::object>(PyIter_Next(iterator.ptr()));
            if (!value.is_valid()) {
                if (PyErr_Occurred()) throw nb::python_error();
                running = false; clear(); throw nb::stop_iteration();
            }
            replace(current, std::move(value));
            TransformGlobals ns(nb::borrow<nb::dict>(globals)); ns.builtins = builtins;
            nb::object result;
            if (kind == Diagonal) result = call3(ns.get("getattr"), current, nb::str("_is_diagonal"), nb::bool_(false));
            else if (kind == InitialLabels) result = message({nb::str("axis "), formatted(current)});
            else result = call1(ns.get("str"), current);
            running = false;
            return result;
        } catch (const nb::python_error& error) {
            events::HandledException handled(error.value());
            running = false;
            // The genuine adapter traceback can retain the original .0 and
            // current binding after frame failure. Mark closed without
            // detaching those edges; close on a failed frame is a no-op.
            closed = true;
            if (error.matches(PyExc_StopIteration)) collections::raise_exception(error.value(), true);
            throw;
        } catch (...) { running = false; clear(); throw; }
    }
    nb::object send(nb::object value) {
        if (closed) throw nb::stop_iteration();
        if (!value.is_none()) {
            if (!started) { PyErr_SetString(PyExc_TypeError, "can't send non-None value to a just-started generator"); throw nb::python_error(); }
            // The genexpr resumes and discards each sent value.
        }
        return next();
    }
    void close() {
        if (running) { PyErr_SetString(PyExc_ValueError, "generator already executing"); throw nb::python_error(); }
        if (closed) return;
        if (!started) { clear(); return; }
        auto exception = call0(nb::borrow<nb::object>(PyExc_GeneratorExit));
        events::HandledException handled(exception);
        clear();
    }
    nb::object throw_(nb::args args) {
        auto exception = collections::normalize_throw(args);
        if (running) { PyErr_SetString(PyExc_ValueError, "generator already executing"); throw nb::python_error(); }
        const bool open = !closed;
        closed = true;
        events::HandledException handled(exception);
        collections::raise_exception(exception, open);
    }
};
int expression_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (nb::inst_ready(self)) {
        auto& s = *nb::inst_ptr<TransformExpressionIterator>(self);
        Py_VISIT(s.iterator.ptr()); Py_VISIT(s.current.ptr()); Py_VISIT(s.globals.ptr()); Py_VISIT(s.builtins.ptr());
    }
    return 0;
}
int expression_clear(PyObject* self) { if (nb::inst_ready(self)) nb::inst_ptr<TransformExpressionIterator>(self)->clear(); return 0; }
PyType_Slot expression_slots[] = {{Py_tp_traverse, reinterpret_cast<void*>(expression_traverse)}, {Py_tp_clear, reinterpret_cast<void*>(expression_clear)}, {0, nullptr}};
nb::object expression(const TransformGlobals& ns, nb::object values, TransformExpressionIterator::Kind kind, nb::handle factory, const char* qualname) {
    TransformExpressionIterator iterator;
    iterator.kind = kind;
    iterator.iterator = checked(PyObject_GetIter(values.ptr()));
    values.reset();
    iterator.globals = ns.globals; iterator.builtins = ns.builtins;
    auto result = call1(own(factory), nb::cast(std::move(iterator)));
    set_attr(result, "__name__", nb::str("<genexpr>"));
    set_attr(result, "__qualname__", nb::str(qualname));
    return result;
}
nb::object default_labels(const TransformGlobals& ns, nb::handle self, nb::object count, bool initial, nb::handle factory) {
    auto consumer = ns.get("tuple");
    auto range = ns.get("range");
    if (!initial) count = self.attr("ndim");
    auto start = negate(count);
    auto values = call2(std::move(range), std::move(start), nb::int_(0));
    auto generator = expression(ns, std::move(values), initial ? TransformExpressionIterator::InitialLabels : TransformExpressionIterator::PlainLabels,
        factory, initial ? "Affine.__init__.<locals>.<genexpr>" : "Affine.axis_labels.<locals>.<genexpr>");
    return call1(std::move(consumer), std::move(generator));
}
nb::object infer(const TransformGlobals& ns, nb::handle scale, nb::handle translate, nb::handle rotate, nb::handle shear) {
    return ns.get("infer_ndim")(nb::arg("scale") = scale, nb::arg("translate") = translate, nb::arg("rotate") = rotate, nb::arg("shear") = shear);
}

void initialize(const TransformGlobals& ns, nb::handle self, nb::handle function, nb::handle inverse, nb::handle name) {
    set_attr(self, "func", function); set_attr(self, "_inverse_func", inverse); set_attr(self, "name", name); set_attr(self, "_cache_dict", nb::dict());
    if (function.is(attr(ns.get("tz"), "identity"))) set_attr(self, "_inverse_func", attr(ns.get("tz"), "identity"));
}
nb::object inverse_transform(const TransformGlobals& ns, nb::handle self) {
    if (self.attr("_inverse_func").is_none()) raise_call(ns.get("ValueError"), nb::str("Inverse function was not provided."));
    if (!cached(self, "inverse")) {
        auto callback = ns.get("Transform"); nb::object inverse = self.attr("_inverse_func"), function = self.attr("func");
        publish(self, "inverse", call2(std::move(callback), std::move(inverse), std::move(function)));
    }
    return cached_value(self, "inverse");
}
nb::object compose_transform(const TransformGlobals& ns, nb::handle self, nb::handle other) {
    auto callback = ns.get("TransformChain"); nb::list values; values.append(self); values.append(other); return call1(std::move(callback), std::move(values));
}
void initialize_chain(const TransformGlobals& ns, nb::handle self, TransformObjectLocals& s, nb::handle super_provider, nb::handle lookup_factory) {
    if (s[Transforms].is_none()) replace(s[Transforms], nb::list());
    auto callback = super_attr(super_provider, self, "__init__");
    auto transform = ns.get("Transform"); auto str = ns.get("str");
    auto lookup_lambda = call0(own(lookup_factory));
    set_attr(lookup_lambda, "__name__", nb::str("<lambda>"));
    set_attr(lookup_lambda, "__qualname__", nb::str("TransformChain.__init__.<locals>.<lambda>"));
    nb::dict lookup; set_item(lookup, str, lookup_lambda);
    callback(nb::arg("data") = s[Transforms], nb::arg("basetype") = transform, nb::arg("lookup") = lookup);
    callback.reset(); transform.reset(); str.reset(); lookup_lambda.reset(); lookup.reset();
    call1(attr(ns.get("Transform"), "__init__"), own(self));
    auto iterator = checked(PyObject_GetIter(self.ptr()));
    while (auto current = nb::steal<nb::object>(PyIter_Next(iterator.ptr()))) {
        replace(s[Tr], std::move(current));
        if (truth(call2(ns.get("hasattr"), s[Tr], nb::str("changed")))) {
            auto connect = attr(attr(s[Tr], "changed"), "connect"); nb::object clean_callback = self.attr("_clean_cache"); call1(std::move(connect), std::move(clean_callback));
        }
    }
    if (PyErr_Occurred()) throw nb::python_error();
}
nb::object call_chain(const TransformGlobals& ns, nb::handle self, nb::handle coords) {
    auto pipe = attr(ns.get("tz"), "pipe");
    auto expanded = checked(PySequence_Tuple(self.ptr()));
    const auto size = PyTuple_GET_SIZE(expanded.ptr());
    auto args = checked(PyTuple_New(size + 1));
    PyTuple_SET_ITEM(args.ptr(), 0, own(coords).release().ptr());
    for (Py_ssize_t j = 0; j < size; ++j) PyTuple_SET_ITEM(args.ptr(), j + 1, own(nb::handle(PyTuple_GET_ITEM(expanded.ptr(), j))).release().ptr());
    expanded.reset();
    return checked(PyObject_CallObject(pipe.ptr(), args.ptr()));
}
nb::object item_key(nb::handle key) { return message({nb::str("getitem_"), formatted(key)}); }
nb::object chain_get(const TransformGlobals&, nb::handle self, nb::handle key, nb::handle super_provider) {
    auto membership_key = item_key(key); auto membership_cache = cache(self); bool hit = contains(membership_cache, membership_key); membership_cache.reset(); membership_key.reset();
    if (!hit) {
        auto callback = super_attr(super_provider, self, "__getitem__"); auto result = call1(std::move(callback), own(key));
        auto target = cache(self); auto target_key = item_key(key); set_item(target, target_key, result);
    }
    auto target = cache(self); auto target_key = item_key(key); return item(target, target_key);
}
void chain_set(const TransformGlobals& ns, nb::handle self, nb::handle key, nb::handle value, nb::handle super_provider) {
    if (contains(self, key)) {
        auto hasattr = ns.get("hasattr"); auto current = item(self, key);
        if (truth(call2(std::move(hasattr), std::move(current), nb::str("changed")))) {
            auto disconnect = attr(attr(item(self, key), "changed"), "disconnect"); nb::object callback = self.attr("_clean_cache"); call1(std::move(disconnect), std::move(callback));
        }
    }
    call2(super_attr(super_provider, self, "__setitem__"), own(key), own(value));
    if (truth(call2(ns.get("hasattr"), own(value), nb::str("changed")))) {
        auto connect = attr(attr(own(value), "changed"), "connect"); nb::object callback = self.attr("_clean_cache"); call1(std::move(connect), std::move(callback));
    }
    clean(self);
}
void chain_delete(const TransformGlobals& ns, nb::handle self, nb::handle key, TransformObjectLocals& s, nb::handle super_provider) {
    replace(s[Val], item(self, key));
    if (truth(call2(ns.get("hasattr"), s[Val], nb::str("changed")))) {
        auto disconnect = attr(attr(s[Val], "changed"), "disconnect"); nb::object callback = self.attr("_clean_cache"); call1(std::move(disconnect), std::move(callback));
    }
    call1(super_attr(super_provider, self, "__delitem__"), own(key)); clean(self);
}
nb::object inverse_chain(const TransformGlobals& ns, nb::handle self) {
    if (!cached(self, "inverse")) {
        auto constructor = ns.get("TransformChain"); auto reversed = item(self, slice(nb::none(), nb::none(), nb::int_(-1))); nb::list values;
        auto iterator = checked(PyObject_GetIter(reversed.ptr())); reversed.reset(); nb::object current;
        while (auto value = nb::steal<nb::object>(PyIter_Next(iterator.ptr()))) { replace(current, std::move(value)); values.append(current.attr("inverse")); }
        if (PyErr_Occurred()) throw nb::python_error();
        iterator.reset(); current.reset(); publish(self, "inverse", call1(std::move(constructor), std::move(values)));
    }
    return cached_value(self, "inverse");
}
nb::object diagonal_chain(const TransformGlobals& ns, nb::handle self, nb::handle generator_factory) {
    auto all = ns.get("all");
    auto values = expression(ns, own(self), TransformExpressionIterator::Diagonal, generator_factory, "TransformChain._is_diagonal.<locals>.<genexpr>");
    if (truth(call1(std::move(all), std::move(values)))) return nb::bool_(true);
    auto getattr = ns.get("getattr"); nb::object simplified = self.attr("simplified");
    return call3(std::move(getattr), std::move(simplified), nb::str("_is_diagonal"), nb::bool_(false));
}
nb::object simplify(const TransformGlobals& ns, nb::handle self) {
    if (compare(length(ns, own(self)), nb::int_(0))) raise_call(ns.get("ValueError"), nb::str("Cannot simplify an empty transform chain."));
    if (compare(length(ns, own(self)), nb::int_(1))) return index(self, 0);
    if (!cached(self, "simplified")) {
        auto pipe = attr(ns.get("tz"), "pipe"); auto first = index(self, 0); auto remaining = item(self, slice(nb::int_(1), nb::none())); nb::list functions;
        auto iterator = checked(PyObject_GetIter(remaining.ptr())); remaining.reset(); nb::object current;
        while (auto value = nb::steal<nb::object>(PyIter_Next(iterator.ptr()))) { replace(current, std::move(value)); functions.append(current.attr("compose")); }
        if (PyErr_Occurred()) throw nb::python_error();
        iterator.reset(); current.reset();
        auto args = checked(PyTuple_New(nb::len(functions) + 1)); PyTuple_SET_ITEM(args.ptr(), 0, first.release().ptr());
        for (size_t j = 0; j < nb::len(functions); ++j) PyTuple_SET_ITEM(args.ptr(), j + 1, own(functions[j]).release().ptr()); functions.reset();
        auto result = checked(PyObject_CallObject(pipe.ptr(), args.ptr())); args.reset(); pipe.reset(); publish(self, "simplified", std::move(result));
    }
    return cached_value(self, "simplified");
}
nb::object map_chain(const TransformGlobals& ns, nb::handle self, nb::handle axes, const char* method) {
    auto constructor = ns.get("TransformChain"); nb::list values;
    for (nb::handle value : nb::iter(self)) values.append(call1(value.attr(method), own(axes)));
    return call1(std::move(constructor), std::move(values));
}

void initialize_scale(const TransformGlobals& ns, nb::handle self, nb::handle name, TransformObjectLocals& s, nb::handle super_provider) {
    auto callback = super_attr(super_provider, self, "__init__"); callback(nb::arg("name") = name); callback.reset();
    auto left = length(ns, s[Scale]); auto right = length(ns, s[Translate]); bool scale_longer = compare(left, right, Py_GT); left.reset(); right.reset();
    if (scale_longer) {
        left = length(ns, s[Scale]); right = length(ns, s[Translate]); auto count = subtract(left, right); left.reset(); right.reset();
        nb::list zero; zero.append(0); auto padding = multiply(zero, count); count.reset(); zero.reset();
        auto value = call1(ns.get("list"), s[Translate]); auto result = add(padding, value); padding.reset(); value.reset(); replace(s[Translate], std::move(result));
    }
    left = length(ns, s[Translate]); right = length(ns, s[Scale]); bool translate_longer = compare(left, right, Py_GT); left.reset(); right.reset();
    if (translate_longer) {
        left = length(ns, s[Translate]); right = length(ns, s[Scale]); auto count = subtract(left, right); left.reset(); right.reset();
        nb::list one; one.append(1); auto padding = multiply(one, count); count.reset(); one.reset();
        auto value = call1(ns.get("list"), s[Scale]); auto result = add(padding, value); padding.reset(); value.reset(); replace(s[Scale], std::move(result));
    }
    set_attr(self, "scale", call1(np(ns, "array"), s[Scale]));
    set_attr(self, "translate", call1(np(ns, "array"), s[Translate]));
}
nb::object inverse_scale(const TransformGlobals& ns, nb::handle self) {
    auto constructor = ns.get("ScaleTranslate");
    nb::object scale = self.attr("scale"); auto reciprocal = divide(nb::int_(1), scale); scale.reset();
    scale = self.attr("scale"); auto negative = divide(nb::int_(-1), scale); scale.reset();
    nb::object translate = self.attr("translate"); auto shift = multiply(negative, translate); negative.reset(); translate.reset();
    return call2(std::move(constructor), std::move(reciprocal), std::move(shift));
}
nb::object compose_scale(const TransformGlobals& ns, nb::handle self, nb::handle other, TransformObjectLocals& s, nb::handle super_provider) {
    auto isinstance = ns.get("isinstance"); auto type = ns.get("ScaleTranslate");
    if (!truth(call2(std::move(isinstance), own(other), std::move(type)))) call1(super_attr(super_provider, self, "compose"), own(other));
    nb::object left = self.attr("scale"), right = other.attr("scale"); auto scale = multiply(left, right); left.reset(); right.reset(); replace(s[Scale], std::move(scale));
    left = self.attr("translate"); nb::object factor = self.attr("scale"); right = other.attr("translate");
    auto product = multiply(factor, right); factor.reset(); right.reset(); auto shift = add(left, product); left.reset(); product.reset(); replace(s[Translate], std::move(shift));
    return call2(ns.get("ScaleTranslate"), s[Scale], s[Translate]);
}
nb::object slice_scale(const TransformGlobals& ns, nb::handle self, nb::handle axes) {
    auto constructor = ns.get("ScaleTranslate"); nb::object scale = self.attr("scale"); auto selected_scale = item(scale, axes); scale.reset();
    nb::object translate = self.attr("translate"); auto selected_translate = item(translate, axes); translate.reset(); nb::object name = self.attr("name");
    return constructor(selected_scale, selected_translate, nb::arg("name") = name);
}
void expansion(const TransformGlobals& ns, nb::handle self, nb::handle axes, TransformObjectLocals& s) {
    auto left = length(ns, own(axes));
    auto len = ns.get("len"); nb::object scale = self.attr("scale"); auto right = call1(std::move(len), std::move(scale));
    auto count = add(left, right); left.reset(); right.reset(); replace(s[N], std::move(count));
    auto range = call1(ns.get("range"), s[N]); nb::list remaining;
    auto iterator = checked(PyObject_GetIter(range.ptr())); range.reset(); nb::object current;
    while (auto i = nb::steal<nb::object>(PyIter_Next(iterator.ptr()))) { replace(current, std::move(i)); if (!contains(axes, current)) remaining.append(current); }
    if (PyErr_Occurred()) throw nb::python_error();
    iterator.reset(); current.reset(); replace(s[NotAxes], std::move(remaining));
}
nb::object expand_scale(const TransformGlobals& ns, nb::handle self, nb::handle axes, TransformObjectLocals& s) {
    expansion(ns, self, axes, s);
    replace(s[Scale], call1(np(ns, "ones"), s[N]));
    nb::object value = self.attr("scale"); set_item(s[Scale], s[NotAxes], value); value.reset();
    replace(s[Translate], call1(np(ns, "zeros"), s[N]));
    value = self.attr("translate"); set_item(s[Translate], s[NotAxes], value); value.reset();
    auto constructor = ns.get("ScaleTranslate"); nb::object name = self.attr("name");
    return constructor(s[Scale], s[Translate], nb::arg("name") = name);
}

void initialize_affine(const TransformGlobals& ns, nb::handle self, nb::handle scale, nb::handle affine_matrix, nb::handle axis_labels,
    nb::handle name, nb::handle units, TransformObjectLocals& s, nb::handle super_provider, nb::handle generator_factory) {
    auto callback = super_attr(super_provider, self, "__init__"); callback(nb::arg("name") = name); callback.reset();
    set_attr(self, "_upper_triangular", nb::bool_(true));
    if (s[Ndim].is_none()) replace(s[Ndim], infer(ns, scale, s[Translate], s[Rotate], s[Shear]));
    if (!affine_matrix.is_none()) {
        replace(s[Linear], item(affine_matrix, block())); replace(s[Translate], item(affine_matrix, shift_key()));
    } else if (!s[Linear].is_none()) replace(s[Linear], call1(np(ns, "array"), s[Linear]));
    else {
        if (s[Rotate].is_none()) replace(s[Rotate], call1(np(ns, "eye"), s[Ndim]));
        if (s[Shear].is_none()) replace(s[Shear], call1(np(ns, "eye"), s[Ndim]));
        else {
            auto array = call1(np(ns, "array"), s[Shear]); auto dimensions = attr(std::move(array), "ndim");
            bool two = compare(dimensions, nb::int_(2)); dimensions.reset();
            if (two) {
                if (truth(call1(ns.get("is_matrix_triangular"), s[Shear]))) set_attr(self, "_upper_triangular", call1(ns.get("is_matrix_upper_triangular"), s[Shear]));
                else { auto error = ns.get("ValueError"); raise_call(std::move(error), triangular_error(s[Shear])); }
            }
        }
        replace(s[Linear], call3(ns.get("compose_linear_matrix"), s[Rotate], own(scale), s[Shear]));
    }
    auto max = ns.get("max"); auto size = index(attr(s[Linear], "shape"), 0); auto dimensions = call2(std::move(max), s[Ndim], std::move(size)); replace(s[Ndim], std::move(dimensions));
    set_attr(self, "_linear_matrix", call2(ns.get("embed_in_identity_matrix"), s[Linear], s[Ndim]));
    set_attr(self, "_translate", vector(ns, "translate_to_vector", s[Translate], s[Ndim]));
    set_attr(self, "_axis_labels", default_labels(ns, self, s[Ndim], true, generator_factory));
    auto registry = call0(attr(ns.get("pint"), "get_application_registry")); auto pixel = attr(std::move(registry), "pixel"); auto values = nb::make_tuple(pixel); pixel.reset();
    auto repeated = multiply(values, s[Ndim]); values.reset(); set_attr(self, "_units", repeated); repeated.reset();
    set_attr(self, "axis_labels", axis_labels); set_attr(self, "units", units);
}
void set_labels(const TransformGlobals& ns, nb::handle self, TransformObjectLocals& s, nb::handle generator_factory) {
    if (s[Labels].is_none()) replace(s[Labels], default_labels(ns, self, nb::object(), false, generator_factory));
    auto count = length(ns, s[Labels]); nb::object ndim = self.attr("ndim"); bool different = compare(count, ndim, Py_NE); count.reset(); ndim.reset();
    if (different) {
        auto error = ns.get("ValueError"); auto labels_text = checked(PyObject_Repr(s[Labels].ptr())); ndim = self.attr("ndim"); auto ndim_text = formatted(ndim); ndim.reset();
        raise_call(std::move(error), message({nb::str("axis_labels="), std::move(labels_text), nb::str(" must have length ndim="), std::move(ndim_text), nb::str(".")}));
    }
    replace(s[Labels], call1(ns.get("tuple"), s[Labels])); set_attr(self, "_axis_labels", s[Labels]);
}
void set_units(const TransformGlobals& ns, nb::handle self, TransformObjectLocals& s) {
    replace(s[Units], call1(ns.get("get_units_from_name"), s[Units]));
    auto isinstance = ns.get("isinstance"); auto unit = attr(ns.get("pint"), "Unit");
    if (truth(call2(std::move(isinstance), s[Units], std::move(unit)))) { auto value = nb::make_tuple(s[Units]); nb::object ndim = self.attr("ndim"); auto result = multiply(value, ndim); value.reset(); ndim.reset(); replace(s[Units], std::move(result)); }
    auto count = length(ns, s[Units]); nb::object ndim = self.attr("ndim"); bool different = compare(count, ndim, Py_NE); count.reset(); ndim.reset();
    if (different) { auto error = ns.get("ValueError"); auto repr = checked(PyObject_Repr(s[Units].ptr())); ndim = self.attr("ndim"); auto ndim_text = formatted(ndim); ndim.reset(); raise_call(std::move(error), message({nb::str("units="), std::move(repr), nb::str(" must have length ndim="), std::move(ndim_text), nb::str(".")})); }
    set_attr(self, "_units", s[Units]);
}
nb::object scale_affine(const TransformGlobals& ns, nb::handle self) {
    if (truth(self.attr("_is_diagonal"))) { auto diag = np(ns, "diag"); nb::object linear = self.attr("_linear_matrix"); return call1(std::move(diag), std::move(linear)); }
    call0(self.attr("_setup_decompose_linear_matrix_cache")); return index(cached_value(self, "decompose_linear_matrix"), 1);
}
void set_scale_affine(const TransformGlobals& ns, nb::handle self, TransformObjectLocals& s) {
    if (truth(self.attr("_is_diagonal"))) {
        auto callback = ns.get("scale_to_vector"); nb::object ndim = self.attr("ndim"); auto scale = callback(s[Scale], nb::arg("ndim") = ndim); callback.reset(); ndim.reset(); replace(s[Scale], std::move(scale));
        auto range = ns.get("range"); auto count = length(ns, s[Scale]); auto values = call1(std::move(range), std::move(count)); auto iterator = checked(PyObject_GetIter(values.ptr())); values.reset();
        while (auto i = nb::steal<nb::object>(PyIter_Next(iterator.ptr()))) {
            replace(s[I], std::move(i)); auto value = item(s[Scale], s[I]); nb::object matrix = self.attr("_linear_matrix"); auto key = nb::make_tuple(s[I], s[I]); set_item(matrix, key, value);
        }
        if (PyErr_Occurred()) throw nb::python_error();
    } else { auto callback = ns.get("compose_linear_matrix"); nb::object rotate = self.attr("rotate"), shear = self.attr("_shear_cache"); auto linear = call3(std::move(callback), std::move(rotate), s[Scale], std::move(shear)); set_attr(self, "_linear_matrix", linear); }
    clean(self);
}
void setup_decomposition(const TransformGlobals& ns, nb::handle self) {
    if (cached(self, "decompose_linear_matrix")) return;
    auto callback = ns.get("decompose_linear_matrix"); nb::object matrix = self.attr("linear_matrix"), upper = self.attr("_upper_triangular");
    auto result = callback(matrix, nb::arg("upper_triangular") = upper); callback.reset(); matrix.reset(); upper.reset(); publish(self, "decompose_linear_matrix", std::move(result));
}
nb::object decomposition(nb::handle self, Py_ssize_t field) { call0(self.attr("_setup_decompose_linear_matrix_cache")); return index(cached_value(self, "decompose_linear_matrix"), field); }
void set_rotate_affine(const TransformGlobals& ns, nb::handle self, nb::handle value) {
    auto callback = ns.get("compose_linear_matrix"); nb::object scale = self.attr("scale"), shear = self.attr("_shear_cache"); auto result = call3(std::move(callback), own(value), std::move(scale), std::move(shear)); set_attr(self, "_linear_matrix", result); result.reset(); clean(self);
}
nb::object shear_affine(const TransformGlobals& ns, nb::handle self) {
    if (truth(self.attr("_is_diagonal"))) { auto zeros = np(ns, "zeros"); nb::object ndim = self.attr("ndim"); return call1(std::move(zeros), nb::make_tuple(ndim)); }
    return decomposition(self, 2);
}
void set_shear_affine(const TransformGlobals& ns, nb::handle self, TransformObjectLocals& s) {
    replace(s[Shear], call1(np(ns, "asarray"), s[Shear]));
    if (compare(attr(s[Shear], "ndim"), nb::int_(2))) {
        if (truth(call1(ns.get("is_matrix_triangular"), s[Shear]))) set_attr(self, "_upper_triangular", call1(ns.get("is_matrix_upper_triangular"), s[Shear]));
        else { auto error = ns.get("ValueError"); raise_call(std::move(error), triangular_error(s[Shear])); }
    } else set_attr(self, "_upper_triangular", nb::bool_(true));
    auto callback = ns.get("compose_linear_matrix"); nb::object rotate = self.attr("rotate"), scale = self.attr("scale"); auto result = call3(std::move(callback), std::move(rotate), std::move(scale), s[Shear]); set_attr(self, "_linear_matrix", result); result.reset(); clean(self);
}
nb::object homogeneous(const TransformGlobals& ns, nb::handle self, TransformObjectLocals& s) {
    auto eye = np(ns, "eye"); nb::object ndim = self.attr("ndim"); auto rows = add(ndim, nb::int_(1)); ndim.reset(); ndim = self.attr("ndim"); auto columns = add(ndim, nb::int_(1)); ndim.reset();
    replace(s[Matrix], call2(std::move(eye), std::move(rows), std::move(columns)));
    nb::object value = self.attr("_linear_matrix"); auto key = block(); set_item(s[Matrix], key, value); key.reset(); value.reset();
    value = self.attr("_translate"); key = shift_key(); set_item(s[Matrix], key, value); return s[Matrix];
}
void set_homogeneous(const TransformGlobals&, nb::handle self, nb::handle value) { set_attr(self, "_linear_matrix", item(value, block())); set_attr(self, "_translate", item(value, shift_key())); clean(self); }
nb::object inverse_affine(const TransformGlobals& ns, nb::handle self) {
    if (!cached(self, "inverse")) {
        auto constructor = ns.get("Affine"); auto inverse = attr(attr(ns.get("np"), "linalg"), "inv"); nb::object matrix = self.attr("affine_matrix"); auto inverted = call1(std::move(inverse), std::move(matrix)); nb::object units = self.attr("units");
        auto result = constructor(nb::arg("affine_matrix") = inverted, nb::arg("units") = units); constructor.reset(); inverted.reset(); units.reset(); publish(self, "inverse", std::move(result));
    }
    return cached_value(self, "inverse");
}
nb::object compose_affine(const TransformGlobals& ns, nb::handle self, nb::handle other, TransformObjectLocals& s, nb::handle super_provider) {
    auto isinstance = ns.get("isinstance"); auto affine = ns.get("Affine");
    if (!truth(call2(std::move(isinstance), own(other), std::move(affine)))) return call1(super_attr(super_provider, self, "compose"), own(other));
    nb::object left = self.attr("affine_matrix"), right = other.attr("affine_matrix"); auto result = matmul(left, right); left.reset(); right.reset(); replace(s[AffineMatrix], std::move(result));
    return ns.get("Affine")(nb::arg("affine_matrix") = s[AffineMatrix]);
}
nb::list selected_metadata(nb::handle self, const char* field, nb::handle axes) { nb::list result; for (nb::handle axis : nb::iter(axes)) result.append(item(self.attr(field), axis)); return result; }
nb::object slice_affine(const TransformGlobals& ns, nb::handle self, TransformObjectLocals& s) {
    replace(s[Axes], call1(ns.get("list"), s[Axes]));
    if (truth(self.attr("_is_diagonal"))) { auto diag = np(ns, "diag"); nb::object scale = self.attr("scale"); auto value = item(scale, s[Axes]); scale.reset(); replace(s[Linear], call1(std::move(diag), std::move(value))); }
    else { nb::object linear = self.attr("linear_matrix"); auto key = call2(np(ns, "ix_"), s[Axes], s[Axes]); auto result = item(linear, key); linear.reset(); key.reset(); replace(s[Linear], std::move(result)); }
    replace(s[Units], selected_metadata(self, "units", s[Axes])); replace(s[AxesLabels], selected_metadata(self, "axis_labels", s[Axes]));
    auto constructor = ns.get("Affine"); nb::object translate = self.attr("translate"); auto shift = item(translate, s[Axes]); translate.reset(); auto count = length(ns, s[Axes]); nb::object name = self.attr("name");
    return constructor(nb::arg("linear_matrix") = s[Linear], nb::arg("translate") = shift, nb::arg("ndim") = count, nb::arg("name") = name, nb::arg("units") = s[Units], nb::arg("axis_labels") = s[AxesLabels]);
}
nb::object replace_affine(const TransformGlobals& ns, nb::handle self, nb::handle axes, nb::handle other, TransformObjectLocals& s) {
    auto count = length(ns, own(axes)); nb::object ndim = other.attr("ndim"); bool different = compare(count, ndim, Py_NE); count.reset(); ndim.reset();
    if (different) raise_call(ns.get("ValueError"), nb::str("Dimensionality of provided axes list and transform differ."));
    auto copy = np(ns, "copy"); nb::object linear = self.attr("linear_matrix"); replace(s[Linear], call1(std::move(copy), std::move(linear)));
    linear = other.attr("linear_matrix"); auto key = call2(np(ns, "ix_"), own(axes), own(axes)); set_item(s[Linear], key, linear); key.reset(); linear.reset();
    copy = np(ns, "copy"); nb::object translate = self.attr("translate"); replace(s[Translate], call1(std::move(copy), std::move(translate)));
    translate = other.attr("translate"); set_item(s[Translate], axes, translate); translate.reset();
    auto constructor = ns.get("Affine"); count = length(ns, own(axes)); nb::object name = self.attr("name");
    return constructor(nb::arg("linear_matrix") = s[Linear], nb::arg("translate") = s[Translate], nb::arg("ndim") = count, nb::arg("name") = name);
}
nb::object expand_affine(const TransformGlobals& ns, nb::handle self, nb::handle axes, TransformObjectLocals& s) {
    expansion(ns, self, axes, s); replace(s[Linear], call1(np(ns, "eye"), s[N]));
    nb::object linear = self.attr("linear_matrix"); auto key = call2(np(ns, "ix_"), s[NotAxes], s[NotAxes]); set_item(s[Linear], key, linear); key.reset(); linear.reset();
    replace(s[Translate], call1(np(ns, "zeros"), s[N])); nb::object translate = self.attr("translate"); set_item(s[Translate], s[NotAxes], translate); translate.reset();
    auto constructor = ns.get("Affine"); nb::object name = self.attr("name");
    return constructor(nb::arg("linear_matrix") = s[Linear], nb::arg("translate") = s[Translate], nb::arg("ndim") = s[N], nb::arg("name") = name);
}
nb::object diagonal_affine(const TransformGlobals& ns, nb::handle self) {
    if (!cached(self, "_is_diagonal")) { auto callback = ns.get("is_diagonal"); nb::object linear = self.attr("linear_matrix"); auto result = callback(linear, nb::arg("tol") = 1e-8); callback.reset(); linear.reset(); publish(self, "_is_diagonal", std::move(result)); }
    return cached_value(self, "_is_diagonal");
}
void initialize_composite(const TransformGlobals& ns, nb::handle self, nb::handle scale, nb::handle translate, nb::handle labels, nb::handle rotate, nb::handle shear, nb::handle name, nb::handle units, TransformObjectLocals& s, nb::handle super_provider) {
    auto callback = super_attr(super_provider, self, "__init__"); callback(scale, translate, nb::arg("axis_labels") = labels, nb::arg("rotate") = rotate, nb::arg("shear") = shear, nb::arg("ndim") = s[Ndim], nb::arg("name") = name, nb::arg("units") = units); callback.reset();
    if (s[Ndim].is_none()) replace(s[Ndim], infer(ns, scale, translate, rotate, shear));
    set_attr(self, "_translate", vector(ns, "translate_to_vector", translate, s[Ndim])); set_attr(self, "_scale", vector(ns, "scale_to_vector", scale, s[Ndim]));
    set_attr(self, "_rotate", vector(ns, "rotate_to_matrix", rotate, s[Ndim])); set_attr(self, "_shear", vector(ns, "shear_to_matrix", shear, s[Ndim]));
    set_attr(self, "_linear_matrix", call0(self.attr("_make_linear_matrix")));
}
void set_composite(const TransformGlobals& ns, nb::handle self, nb::handle value, const char* field, const char* converter) {
    auto callback = ns.get(converter); nb::object ndim = self.attr("ndim"); auto converted = callback(value, nb::arg("ndim") = ndim); callback.reset(); ndim.reset(); set_attr(self, field, converted); converted.reset();
    auto result = call0(self.attr("_make_linear_matrix")); set_attr(self, "_linear_matrix", result); result.reset(); clean(self);
}
nb::object shear_composite(const TransformGlobals& ns, nb::handle self) {
    auto callback = ns.get("is_matrix_upper_triangular"); nb::object shear = self.attr("_shear");
    if (truth(call1(std::move(callback), std::move(shear)))) { shear = self.attr("_shear"); auto triu = np(ns, "triu_indices"); nb::object ndim = self.attr("ndim"); auto key = triu(nb::arg("n") = ndim, nb::arg("k") = 1); return item(shear, key); }
    return self.attr("_shear");
}
nb::object slice_composite(const TransformGlobals& ns, nb::handle self, nb::handle axes) {
    auto constructor = ns.get("CompositeAffine"); nb::object value = self.attr("_scale"); auto scale = item(value, axes); value.reset();
    value = self.attr("_translate"); auto translate = item(value, axes); value.reset();
    value = self.attr("_rotate"); auto key = call2(np(ns, "ix_"), own(axes), own(axes)); auto rotate = item(value, key); value.reset(); key.reset();
    value = self.attr("_shear"); key = call2(np(ns, "ix_"), own(axes), own(axes)); auto shear = item(value, key); value.reset(); key.reset();
    auto count = length(ns, own(axes)); nb::object name = self.attr("name"); auto units = selected_metadata(self, "units", axes); auto labels = selected_metadata(self, "axis_labels", axes);
    return constructor(nb::arg("scale") = scale, nb::arg("translate") = translate, nb::arg("rotate") = rotate, nb::arg("shear") = shear, nb::arg("ndim") = count, nb::arg("name") = name, nb::arg("units") = units, nb::arg("axis_labels") = labels);
}
nb::object expand_composite(const TransformGlobals& ns, nb::handle self, nb::handle axes, TransformObjectLocals& s) {
    expansion(ns, self, axes, s); replace(s[Rotate], call1(np(ns, "eye"), s[N]));
    nb::object value = self.attr("_rotate"); auto key = call2(np(ns, "ix_"), s[NotAxes], s[NotAxes]); set_item(s[Rotate], key, value); key.reset(); value.reset();
    replace(s[Shear], call1(np(ns, "eye"), s[N])); value = self.attr("_shear"); key = call2(np(ns, "ix_"), s[NotAxes], s[NotAxes]); set_item(s[Shear], key, value); key.reset(); value.reset();
    replace(s[Translate], call1(np(ns, "zeros"), s[N])); value = self.attr("_translate"); set_item(s[Translate], s[NotAxes], value); value.reset();
    replace(s[Scale], call1(np(ns, "ones"), s[N])); value = self.attr("_scale"); set_item(s[Scale], s[NotAxes], value); value.reset();
    auto constructor = ns.get("CompositeAffine"); nb::object name = self.attr("name");
    return constructor(nb::arg("translate") = s[Translate], nb::arg("scale") = s[Scale], nb::arg("rotate") = s[Rotate], nb::arg("shear") = s[Shear], nb::arg("ndim") = s[N], nb::arg("name") = name);
}
nb::object make_linear(const TransformGlobals& ns, nb::handle self) {
    nb::object rotate = self.attr("_rotate"), shear = self.attr("_shear"); auto intermediate = matmul(rotate, shear); rotate.reset(); shear.reset();
    auto diag = np(ns, "diag"); nb::object scale = self.attr("_scale"); auto diagonal = call1(std::move(diag), std::move(scale)); return matmul(intermediate, diagonal);
}
} // namespace

void bind_transform_objects(nb::module_& m) {
    auto locals = nb::class_<TransformObjectLocals>(m, "TransformObjectLocals", nb::is_weak_referenceable(), nb::type_slots(locals_slots));
    for (int j = 0; j < FieldCount; ++j) locals.def_prop_ro(field_names[j], [j](const TransformObjectLocals& s) { return s.refs[j].is_valid() ? s.refs[j] : nb::object(nb::none()); });
    nb::class_<TransformExpressionIterator>(m, "TransformExpressionIterator", nb::is_weak_referenceable(), nb::type_slots(expression_slots))
        .def("__iter__", [](TransformExpressionIterator& s) -> TransformExpressionIterator& { return s; }, nb::rv_policy::reference_internal)
        .def("__next__", &TransformExpressionIterator::next)
        .def("send", &TransformExpressionIterator::send, nb::arg("value").none())
        .def("throw", &TransformExpressionIterator::throw_)
        .def("close", &TransformExpressionIterator::close);
    m.def("transform_object_locals", &object_locals, nb::arg("layout"), nb::arg("initial"));
    m.def("transform_chain_lookup", from_globals(+[](const TransformGlobals&, nb::handle value) -> nb::object { return value.attr("name"); }), nb::arg("value").none(), nb::arg("namespace"));
    m.def("transform_initialize", from_globals(&initialize), nb::arg("self").none(), nb::arg("function").none(), nb::arg("inverse").none(), nb::arg("name").none(), nb::arg("namespace"));
    m.def("transform_base_call", from_globals(+[](const TransformGlobals&, nb::handle self, nb::handle coords) -> nb::object { return call1(self.attr("func"), own(coords)); }), nb::arg("self").none(), nb::arg("coords").none(), nb::arg("namespace"));
    m.def("transform_base_inverse", from_globals(&inverse_transform), nb::arg("self").none(), nb::arg("namespace"));
    m.def("transform_base_compose", from_globals(&compose_transform), nb::arg("self").none(), nb::arg("other").none(), nb::arg("namespace"));
    m.def("transform_base_diagonal", from_globals(+[](const TransformGlobals&, nb::handle) { return false; }), nb::arg("self").none(), nb::arg("namespace"));
    m.def("transform_subset_error", from_globals(+[](const TransformGlobals& ns) { raise_call(ns.get("NotImplementedError"), nb::str("Cannot subset arbitrary transforms.")); }), nb::arg("namespace"));
    m.def("transform_clean_cache", from_globals(+[](const TransformGlobals&, nb::handle self) { call0(attr(cache(self), "clear")); call0(attr(self.attr("changed"), "emit")); }), nb::arg("self").none(), nb::arg("namespace"));
    m.def("transform_chain_initialize", from_globals(&initialize_chain), nb::arg("self").none(), nb::arg("locals_owner"), nb::arg("super_provider").none(), nb::arg("lookup_factory").none(), nb::arg("namespace"));
    m.def("transform_chain_call", from_globals(&call_chain), nb::arg("self").none(), nb::arg("coords").none(), nb::arg("namespace"));
    m.def("transform_chain_newlike", from_globals(+[](const TransformGlobals& ns, nb::handle values) { return call1(ns.get("TransformChain"), own(values)); }), nb::arg("values").none(), nb::arg("namespace"));
    m.def("transform_chain_get", from_globals(&chain_get), nb::arg("self").none(), nb::arg("key").none(), nb::arg("super_provider").none(), nb::arg("namespace"));
    m.def("transform_chain_set", from_globals(&chain_set), nb::arg("self").none(), nb::arg("key").none(), nb::arg("value").none(), nb::arg("super_provider").none(), nb::arg("namespace"));
    m.def("transform_chain_delete", from_globals(&chain_delete), nb::arg("self").none(), nb::arg("key").none(), nb::arg("locals_owner"), nb::arg("super_provider").none(), nb::arg("namespace"));
    m.def("transform_chain_inverse", from_globals(&inverse_chain), nb::arg("self").none(), nb::arg("namespace"));
    m.def("transform_chain_diagonal", from_globals(&diagonal_chain), nb::arg("self").none(), nb::arg("generator_factory").none(), nb::arg("namespace"));
    m.def("transform_chain_simplified", from_globals(&simplify), nb::arg("self").none(), nb::arg("namespace"));
    m.def("transform_chain_slice", from_globals(+[](const TransformGlobals& ns, nb::handle self, nb::handle axes) { return map_chain(ns, self, axes, "set_slice"); }), nb::arg("self").none(), nb::arg("axes").none(), nb::arg("namespace"));
    m.def("transform_chain_expand", from_globals(+[](const TransformGlobals& ns, nb::handle self, nb::handle axes) { return map_chain(ns, self, axes, "expand_dims"); }), nb::arg("self").none(), nb::arg("axes").none(), nb::arg("namespace"));
    m.def("transform_scale_initialize", from_globals(&initialize_scale), nb::arg("self").none(), nb::arg("name").none(), nb::arg("locals_owner"), nb::arg("super_provider").none(), nb::arg("namespace"));
    m.def("transform_scale_inverse", from_globals(&inverse_scale), nb::arg("self").none(), nb::arg("namespace"));
    m.def("transform_scale_compose", from_globals(&compose_scale), nb::arg("self").none(), nb::arg("other").none(), nb::arg("locals_owner"), nb::arg("super_provider").none(), nb::arg("namespace"));
    m.def("transform_scale_slice", from_globals(&slice_scale), nb::arg("self").none(), nb::arg("axes").none(), nb::arg("namespace"));
    m.def("transform_scale_expand", from_globals(&expand_scale), nb::arg("self").none(), nb::arg("axes").none(), nb::arg("locals_owner"), nb::arg("namespace"));
    m.def("transform_scale_diagonal", from_globals(+[](const TransformGlobals&, nb::handle) { return true; }), nb::arg("self").none(), nb::arg("namespace"));
    m.def("transform_affine_initialize", from_globals(&initialize_affine), nb::arg("self").none(), nb::arg("scale").none(), nb::arg("affine_matrix").none(), nb::arg("axis_labels").none(), nb::arg("name").none(), nb::arg("units").none(), nb::arg("locals_owner"), nb::arg("super_provider").none(), nb::arg("generator_factory").none(), nb::arg("namespace"));
    m.def("transform_affine_ndim", from_globals(+[](const TransformGlobals&, nb::handle self) { return index(attr(self.attr("_linear_matrix"), "shape"), 0); }), nb::arg("self").none(), nb::arg("namespace"));
    m.def("transform_affine_labels_get", from_globals(+[](const TransformGlobals&, nb::handle self) -> nb::object { return self.attr("_axis_labels"); }), nb::arg("self").none(), nb::arg("namespace"));
    m.def("transform_affine_labels_set", from_globals(&set_labels), nb::arg("self").none(), nb::arg("locals_owner"), nb::arg("generator_factory").none(), nb::arg("namespace"));
    m.def("transform_affine_units_get", from_globals(+[](const TransformGlobals&, nb::handle self) -> nb::object { return self.attr("_units"); }), nb::arg("self").none(), nb::arg("namespace"));
    m.def("transform_affine_units_set", from_globals(&set_units), nb::arg("self").none(), nb::arg("locals_owner"), nb::arg("namespace"));
    m.def("transform_affine_scale_get", from_globals(&scale_affine), nb::arg("self").none(), nb::arg("namespace"));
    m.def("transform_affine_scale_set", from_globals(&set_scale_affine), nb::arg("self").none(), nb::arg("locals_owner"), nb::arg("namespace"));
    m.def("transform_affine_physical_scale", from_globals(+[](const TransformGlobals& ns, nb::handle self) { auto tuple = ns.get("tuple"); auto multiply = np(ns, "multiply"); nb::object scale = self.attr("scale"), units = self.attr("units"); auto value = call2(std::move(multiply), std::move(scale), std::move(units)); return call1(std::move(tuple), std::move(value)); }), nb::arg("self").none(), nb::arg("namespace"));
    m.def("transform_affine_translate_get", from_globals(+[](const TransformGlobals&, nb::handle self) -> nb::object { return self.attr("_translate"); }), nb::arg("self").none(), nb::arg("namespace"));
    m.def("transform_affine_translate_set", from_globals(+[](const TransformGlobals& ns, nb::handle self, nb::handle value) { auto callback = ns.get("translate_to_vector"); nb::object ndim = self.attr("ndim"); auto result = callback(value, nb::arg("ndim") = ndim); callback.reset(); ndim.reset(); set_attr(self, "_translate", result); result.reset(); clean(self); }), nb::arg("self").none(), nb::arg("translate").none(), nb::arg("namespace"));
    m.def("transform_affine_decompose", from_globals(&setup_decomposition), nb::arg("self").none(), nb::arg("namespace"));
    m.def("transform_affine_rotate_get", from_globals(+[](const TransformGlobals&, nb::handle self) { return decomposition(self, 0); }), nb::arg("self").none(), nb::arg("namespace"));
    m.def("transform_affine_rotate_set", from_globals(&set_rotate_affine), nb::arg("self").none(), nb::arg("rotate").none(), nb::arg("namespace"));
    m.def("transform_affine_shear_get", from_globals(&shear_affine), nb::arg("self").none(), nb::arg("namespace"));
    m.def("transform_affine_shear_set", from_globals(&set_shear_affine), nb::arg("self").none(), nb::arg("locals_owner"), nb::arg("namespace"));
    m.def("transform_affine_shear_cache", from_globals(+[](const TransformGlobals&, nb::handle self) { return decomposition(self, 2); }), nb::arg("self").none(), nb::arg("namespace"));
    m.def("transform_affine_linear_get", from_globals(+[](const TransformGlobals&, nb::handle self) -> nb::object { return self.attr("_linear_matrix"); }), nb::arg("self").none(), nb::arg("namespace"));
    m.def("transform_affine_linear_set", from_globals(+[](const TransformGlobals& ns, nb::handle self, nb::handle value) { auto callback = ns.get("embed_in_identity_matrix"); nb::object ndim = self.attr("ndim"); auto result = callback(value, nb::arg("ndim") = ndim); callback.reset(); ndim.reset(); set_attr(self, "_linear_matrix", result); result.reset(); clean(self); }), nb::arg("self").none(), nb::arg("linear_matrix").none(), nb::arg("namespace"));
    m.def("transform_affine_matrix_get", from_globals(&homogeneous), nb::arg("self").none(), nb::arg("locals_owner"), nb::arg("namespace"));
    m.def("transform_affine_matrix_set", from_globals(&set_homogeneous), nb::arg("self").none(), nb::arg("affine_matrix").none(), nb::arg("namespace"));
    m.def("transform_affine_array", from_globals(+[](const TransformGlobals&, nb::handle self) -> nb::object { return self.attr("affine_matrix"); }), nb::arg("self").none(), nb::arg("namespace"));
    m.def("transform_affine_inverse", from_globals(&inverse_affine), nb::arg("self").none(), nb::arg("namespace"));
    m.def("transform_affine_compose", from_globals(&compose_affine), nb::arg("self").none(), nb::arg("other").none(), nb::arg("locals_owner"), nb::arg("super_provider").none(), nb::arg("namespace"));
    m.def("transform_affine_slice", from_globals(&slice_affine), nb::arg("self").none(), nb::arg("locals_owner"), nb::arg("namespace"));
    m.def("transform_affine_replace", from_globals(&replace_affine), nb::arg("self").none(), nb::arg("axes").none(), nb::arg("other").none(), nb::arg("locals_owner"), nb::arg("namespace"));
    m.def("transform_affine_expand", from_globals(&expand_affine), nb::arg("self").none(), nb::arg("axes").none(), nb::arg("locals_owner"), nb::arg("namespace"));
    m.def("transform_affine_diagonal", from_globals(&diagonal_affine), nb::arg("self").none(), nb::arg("namespace"));
    m.def("transform_composite_initialize", from_globals(&initialize_composite), nb::arg("self").none(), nb::arg("scale").none(), nb::arg("translate").none(), nb::arg("axis_labels").none(), nb::arg("rotate").none(), nb::arg("shear").none(), nb::arg("name").none(), nb::arg("units").none(), nb::arg("locals_owner"), nb::arg("super_provider").none(), nb::arg("namespace"));
    m.def("transform_composite_scale_get", from_globals(+[](const TransformGlobals&, nb::handle self) -> nb::object { return self.attr("_scale"); }), nb::arg("self").none(), nb::arg("namespace"));
    m.def("transform_composite_scale_set", from_globals(+[](const TransformGlobals& ns, nb::handle self, nb::handle value) { set_composite(ns, self, value, "_scale", "scale_to_vector"); }), nb::arg("self").none(), nb::arg("scale").none(), nb::arg("namespace"));
    m.def("transform_composite_rotate_get", from_globals(+[](const TransformGlobals&, nb::handle self) -> nb::object { return self.attr("_rotate"); }), nb::arg("self").none(), nb::arg("namespace"));
    m.def("transform_composite_rotate_set", from_globals(+[](const TransformGlobals& ns, nb::handle self, nb::handle value) { set_composite(ns, self, value, "_rotate", "rotate_to_matrix"); }), nb::arg("self").none(), nb::arg("rotate").none(), nb::arg("namespace"));
    m.def("transform_composite_shear_get", from_globals(&shear_composite), nb::arg("self").none(), nb::arg("namespace"));
    m.def("transform_composite_shear_set", from_globals(+[](const TransformGlobals& ns, nb::handle self, nb::handle value) { set_composite(ns, self, value, "_shear", "shear_to_matrix"); }), nb::arg("self").none(), nb::arg("shear").none(), nb::arg("namespace"));
    m.def("transform_composite_super_get", from_globals(+[](const TransformGlobals&, nb::handle self, nb::handle provider, nb::handle field) { auto proxy = call1(own(provider), own(self)); return checked(PyObject_GetAttr(proxy.ptr(), field.ptr())); }), nb::arg("self").none(), nb::arg("super_provider").none(), nb::arg("field").none(), nb::arg("namespace"));
    m.def("transform_composite_matrix_error", from_globals(+[](const TransformGlobals& ns, const std::string& field) { raise_call(ns.get("NotImplementedError"), nb::str((field + " cannot be set directly for a CompositeAffine transform").c_str())); }), nb::arg("field"), nb::arg("namespace"));
    m.def("transform_composite_slice", from_globals(&slice_composite), nb::arg("self").none(), nb::arg("axes").none(), nb::arg("namespace"));
    m.def("transform_composite_expand", from_globals(&expand_composite), nb::arg("self").none(), nb::arg("axes").none(), nb::arg("locals_owner"), nb::arg("namespace"));
    m.def("transform_composite_make_linear", from_globals(&make_linear), nb::arg("self").none(), nb::arg("namespace"));
}
} // namespace nx
