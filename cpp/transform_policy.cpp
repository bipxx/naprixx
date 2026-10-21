#include "arrays.hpp"
#include "collection_protocol.hpp"
#include <array>

// Complete source operations. NumPy/SciPy and dynamic public utility calls are
// declared primitives; no normalized vector or typed-matrix policy is used here.
namespace nx {
namespace {
nb::object pu_check(PyObject* value) { if (!value) throw nb::python_error(); return nb::steal<nb::object>(value); }
nb::object pu_attr(nb::object value, const char* name) { return pu_check(PyObject_GetAttrString(value.ptr(), name)); }
nb::object pu_item(nb::object value, nb::object key) { return pu_check(PyObject_GetItem(value.ptr(), key.ptr())); }
nb::object pu_call0(nb::object fn) { return pu_check(PyObject_CallNoArgs(fn.ptr())); }
nb::object pu_call1(nb::object fn, nb::object a) { return pu_check(PyObject_CallOneArg(fn.ptr(), a.ptr())); }
nb::object pu_call2(nb::object fn, nb::object a, nb::object b) { return pu_check(PyObject_CallFunctionObjArgs(fn.ptr(), a.ptr(), b.ptr(), nullptr)); }
nb::object pu_kw(nb::object fn, nb::tuple args, nb::dict kw) { return pu_check(PyObject_Call(fn.ptr(), args.ptr(), kw.ptr())); }
nb::object pu_cmp(nb::object a, nb::object b, int op) { return pu_check(PyObject_RichCompare(a.ptr(), b.ptr(), op)); }
bool pu_truth(nb::object value) { int r = PyObject_IsTrue(value.ptr()); if (r < 0) throw nb::python_error(); return r != 0; }
void pu_store(nb::object& slot, nb::object value) { nb::object old = std::move(slot); slot = std::move(value); old.reset(); }
nb::object pu_optional(const nb::object& value) { return value.is_valid() ? value : nb::none(); }
nb::object pu_slice(nb::object start) { return pu_check(PySlice_New(start.ptr(), Py_None, Py_None)); }
nb::object pu_negative(nb::object value) { return pu_check(PyNumber_Negative(value.ptr())); }
nb::object pu_format(nb::object value) { return pu_check(PyObject_Format(value.ptr(), nullptr)); }
nb::object pu_concat(nb::object a, nb::object b) { return pu_check(PyUnicode_Concat(a.ptr(), b.ptr())); }
void pu_setitem(nb::object target, nb::object key, nb::object value) { if (PyObject_SetItem(target.ptr(), key.ptr(), value.ptr()) < 0) throw nb::python_error(); }
nb::object pu_binary(nb::object a, nb::object b, binaryfunc operation) { return pu_check(operation(a.ptr(), b.ptr())); }
[[noreturn]] void pu_raise(nb::object factory, nb::object message) {
    nb::object error = pu_call1(std::move(factory), std::move(message));
    if (PyExceptionClass_Check(error.ptr())) {
        nb::object value = pu_call0(error);
        if (!PyExceptionInstance_Check(value.ptr())) {
            PyErr_Format(PyExc_TypeError, "calling %R should have returned an instance of BaseException, not %R",
                error.ptr(), reinterpret_cast<PyObject*>(Py_TYPE(value.ptr())));
            throw nb::python_error();
        }
        error = std::move(value);
    } else if (!PyExceptionInstance_Check(error.ptr())) {
        PyErr_SetString(PyExc_TypeError, "exceptions must derive from BaseException"); throw nb::python_error();
    }
    PyErr_SetObject(reinterpret_cast<PyObject*>(Py_TYPE(error.ptr())), error.ptr()); throw nb::python_error();
}
struct PolicyNamespace {
    nb::dict globals; nb::object builtins;
    explicit PolicyNamespace(nb::dict ns) : globals(std::move(ns)), builtins(nb::borrow<nb::object>(PyEval_GetBuiltins())) {}
    nb::object get(const char* name) const {
        nb::object key = nb::str(name); PyObject* value = PyDict_GetItemWithError(globals.ptr(), key.ptr());
        if (value) return nb::borrow<nb::object>(value); if (PyErr_Occurred()) throw nb::python_error();
        value = PyObject_GetItem(builtins.ptr(), key.ptr()); if (value) return nb::steal<nb::object>(value);
        if (!PyErr_ExceptionMatches(PyExc_KeyError)) throw nb::python_error(); PyErr_Clear();
        nb::object error = pu_call1(nb::borrow<nb::object>(PyExc_NameError), pu_check(PyUnicode_FromFormat("name '%s' is not defined", name)));
        if (PyObject_SetAttrString(error.ptr(), "name", key.ptr()) < 0) throw nb::python_error();
        PyErr_SetObject(PyExc_NameError, error.ptr()); throw nb::python_error();
    }
    nb::object np(const char* name) const { return pu_attr(get("np"), name); }
};

#define PU_FIELDS(X) \
 X(rotate_mat) X(scale_mat) X(shear_mat) X(ndim) X(full_scale) X(full_rotate) X(full_shear) \
 X(translate_arr) X(scale_arr) X(full_rotate_mat) X(rotate_mat_ndim) X(cos_theta) X(sin_theta) \
 X(cos_alpha) X(sin_alpha) X(R_alpha) X(cos_beta) X(sin_beta) X(R_beta) X(cos_gamma) X(sin_gamma) X(R_gamma) \
 X(angle_radians) X(full_shear_mat) X(shear_mat_ndim) X(n) X(N) X(inds) X(upper_tri) X(full_matrix) \
 X(scipy) X(rotate) X(tri) X(scale_with_sign) X(scale) X(normalize) X(tri_normalized) X(shear) X(matrix) X(non_diag)
enum PolicyField {
#define PU_ENUM(name) P_##name,
 PU_FIELDS(PU_ENUM)
#undef PU_ENUM
 P_count
};
struct TransformPolicyLocals {
    enum class Layout { Generic, Rotate, Shear, Decompose } layout = Layout::Generic;
    std::array<nb::object, P_count> values;
    TransformPolicyLocals() = default; TransformPolicyLocals(TransformPolicyLocals&&) = default;
    ~TransformPolicyLocals() { clear(); }
    nb::object& operator[](PolicyField i) { return values[i]; }
    void clear() {
        auto old = std::move(values); for (auto& value : values) value.reset();
        const auto previous = layout; layout = Layout::Generic;
        if (previous == Layout::Rotate) for (auto i : {P_full_rotate_mat, P_rotate_mat, P_rotate_mat_ndim}) old[i].reset();
        if (previous == Layout::Shear) for (auto i : {P_full_shear_mat, P_shear_mat, P_shear_mat_ndim}) old[i].reset();
        if (previous == Layout::Decompose) for (auto i : {P_scipy, P_n, P_rotate, P_tri, P_upper_tri, P_scale_with_sign, P_scale, P_normalize, P_tri_normalized, P_shear}) old[i].reset();
        for (auto& value : old) value.reset();
    }
};
struct TransformCoerceLocals {
    nb::object affine;
    TransformCoerceLocals() = default; TransformCoerceLocals(TransformCoerceLocals&&) = default;
    ~TransformCoerceLocals() { clear(); }
    void clear() { nb::object old = std::move(affine); old.reset(); }
};
int pu_locals_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self)); if (nb::inst_ready(self)) for (const auto& value : nb::inst_ptr<TransformPolicyLocals>(self)->values) Py_VISIT(value.ptr()); return 0;
}
int pu_locals_clear(PyObject* self) { if (nb::inst_ready(self)) nb::inst_ptr<TransformPolicyLocals>(self)->clear(); return 0; }
int pu_coerce_traverse(PyObject* self, visitproc visit, void* arg) { Py_VISIT(Py_TYPE(self)); if (nb::inst_ready(self)) Py_VISIT(nb::inst_ptr<TransformCoerceLocals>(self)->affine.ptr()); return 0; }
int pu_coerce_clear(PyObject* self) { if (nb::inst_ready(self)) nb::inst_ptr<TransformCoerceLocals>(self)->clear(); return 0; }
PyType_Slot pu_locals_slots[] = {{Py_tp_traverse, reinterpret_cast<void*>(pu_locals_traverse)}, {Py_tp_clear, reinterpret_cast<void*>(pu_locals_clear)}, {0, nullptr}};
PyType_Slot pu_coerce_slots[] = {{Py_tp_traverse, reinterpret_cast<void*>(pu_coerce_traverse)}, {Py_tp_clear, reinterpret_cast<void*>(pu_coerce_clear)}, {0, nullptr}};

std::array<nb::object, 2> pu_pair(nb::object result) {
    PyObject* raw_iterator = PyObject_GetIter(result.ptr());
    if (!raw_iterator) {
        nb::python_error error;
        if (Py_TYPE(result.ptr())->tp_iter == nullptr && !PySequence_Check(result.ptr()) && error.matches(PyExc_TypeError)) {
            PyErr_Format(PyExc_TypeError, "cannot unpack non-iterable %.200s object", Py_TYPE(result.ptr())->tp_name);
            throw nb::python_error();
        }
        throw error;
    }
    nb::object iterator = nb::steal<nb::object>(raw_iterator); std::array<nb::object, 2> values;
    for (size_t i = 0; i != 2; ++i) {
        PyObject* value = PyIter_Next(iterator.ptr());
        if (!value) { if (PyErr_Occurred()) throw nb::python_error(); PyErr_Format(PyExc_ValueError, "not enough values to unpack (expected 2, got %zu)", i); throw nb::python_error(); }
        values[i] = nb::steal<nb::object>(value);
    }
    nb::object extra = nb::steal<nb::object>(PyIter_Next(iterator.ptr()));
    if (!extra.is_valid() && PyErr_Occurred()) throw nb::python_error();
    if (extra.is_valid()) { PyErr_SetString(PyExc_ValueError, "too many values to unpack (expected 2)"); throw nb::python_error(); }
    iterator.reset(); result.reset(); return values;
}

// Own the genexpr's .0 and last mat only while suspended; the surrounding
// genuine yield-from generator preserves category/public names, not code/frame.
struct TransformMatrixIterator {
    nb::object iterator, mat, builtins; nb::dict globals;
    bool started = false, exhausted = false, running = false;
    TransformMatrixIterator(nb::object it, nb::dict ns, nb::object bi) : iterator(std::move(it)), builtins(std::move(bi)), globals(std::move(ns)) {}
    TransformMatrixIterator(TransformMatrixIterator&&) = default;
    ~TransformMatrixIterator() { clear(); }
    void clear() { std::array<nb::object, 4> old = {std::move(iterator), std::move(mat), std::move(builtins), std::move(globals)}; exhausted = true; for (auto& value : old) value.reset(); }
};
int pu_iterator_traverse(PyObject* self, visitproc visit, void* arg) { Py_VISIT(Py_TYPE(self)); if (nb::inst_ready(self)) { auto& s = *nb::inst_ptr<TransformMatrixIterator>(self); Py_VISIT(s.iterator.ptr()); Py_VISIT(s.mat.ptr()); Py_VISIT(s.builtins.ptr()); Py_VISIT(s.globals.ptr()); } return 0; }
int pu_iterator_clear(PyObject* self) { if (nb::inst_ready(self)) nb::inst_ptr<TransformMatrixIterator>(self)->clear(); return 0; }
PyType_Slot pu_iterator_slots[] = {{Py_tp_traverse, reinterpret_cast<void*>(pu_iterator_traverse)}, {Py_tp_clear, reinterpret_cast<void*>(pu_iterator_clear)}, {0, nullptr}};
struct MatrixRunning {
    TransformMatrixIterator& s; explicit MatrixRunning(TransformMatrixIterator& value) : s(value) { if (s.running) throw nb::value_error("generator already executing"); s.running = true; }
    ~MatrixRunning() { s.running = false; }
};
nb::object pu_iterator_send(TransformMatrixIterator& s, nb::object sent) {
    MatrixRunning active(s); if (s.exhausted) throw nb::stop_iteration();
    if (!s.started && !sent.is_none()) throw nb::type_error("can't send non-None value to a just-started generator"); s.started = true;
    try {
        PyObject* value = PyIter_Next(s.iterator.ptr());
        if (!value) { if (PyErr_Occurred()) throw nb::python_error(); s.clear(); throw nb::stop_iteration(); }
        pu_store(s.mat, nb::steal<nb::object>(value));
        return pu_item(pu_attr(s.mat, "shape"), nb::int_(0));
    } catch (const nb::python_error& error) {
        events::HandledException handled(error.value());
        // The genuine adapter's saved traceback still owns this delegate and
        // the original .0/mat references. Closing that failed frame is a no-op.
        s.exhausted = true;
        if (error.matches(PyExc_StopIteration)) collections::raise_exception(error.value(), true); throw;
    }
}
void pu_iterator_close(TransformMatrixIterator& s) {
    if (s.exhausted) return; MatrixRunning active(s);
    if (!s.started) { s.clear(); return; }
    nb::object exit = pu_call0(nb::borrow<nb::object>(PyExc_GeneratorExit)); events::HandledException handled(exit); s.clear();
}
nb::object pu_iterator_throw(TransformMatrixIterator& s, nb::args args) {
    MatrixRunning active(s); nb::object error = collections::normalize_throw(args);
    const bool was_exhausted = s.exhausted; s.exhausted = true;
    events::HandledException handled(error); collections::raise_exception(error, !was_exhausted);
}

nb::object policy_compose_linear_matrix(nb::object rotate, nb::object scale, nb::object shear, nb::dict namespace_, TransformPolicyLocals& s, nb::object generator_factory) {
    PolicyNamespace ns(std::move(namespace_));
    pu_store(s[P_rotate_mat], pu_call1(ns.get("_make_rotate_mat"), rotate));
    pu_store(s[P_scale_mat], pu_call1(ns.np("diag"), scale));
    pu_store(s[P_shear_mat], pu_call1(ns.get("_make_shear_mat"), shear));
    nb::object max = ns.get("max"); nb::tuple matrices = nb::make_tuple(s[P_rotate_mat], s[P_scale_mat], s[P_shear_mat]);
    nb::object iterator = pu_check(PyObject_GetIter(matrices.ptr())); matrices.reset();
    nb::object delegate = nb::cast(TransformMatrixIterator(std::move(iterator), ns.globals, ns.builtins));
    nb::object generator = pu_call1(std::move(generator_factory), std::move(delegate));
    if (PyObject_SetAttrString(generator.ptr(), "__name__", nb::str("<genexpr>").ptr()) < 0 ||
        PyObject_SetAttrString(generator.ptr(), "__qualname__", nb::str("compose_linear_matrix.<locals>.<genexpr>").ptr()) < 0) throw nb::python_error();
    pu_store(s[P_ndim], pu_call1(std::move(max), std::move(generator)));
    pu_store(s[P_full_scale], pu_call2(ns.get("embed_in_identity_matrix"), s[P_scale_mat], s[P_ndim]));
    pu_store(s[P_full_rotate], pu_call2(ns.get("embed_in_identity_matrix"), s[P_rotate_mat], s[P_ndim]));
    pu_store(s[P_full_shear], pu_call2(ns.get("embed_in_identity_matrix"), s[P_shear_mat], s[P_ndim]));
    nb::object partial = pu_binary(s[P_full_rotate], s[P_full_shear], PyNumber_MatrixMultiply);
    return pu_binary(std::move(partial), s[P_full_scale], PyNumber_MatrixMultiply);
}
nb::object policy_infer_ndim(nb::object scale, nb::object translate, nb::object rotate, nb::object shear, nb::object linear_matrix, nb::dict namespace_, TransformPolicyLocals& s) {
    PolicyNamespace ns(std::move(namespace_)); pu_store(s[P_ndim], nb::int_(0));
    if (!scale.is_none()) { nb::object max = ns.get("max"), len = ns.get("len"); nb::object n = pu_call1(std::move(len), scale); pu_store(s[P_ndim], pu_call2(std::move(max), s[P_ndim], std::move(n))); }
    if (!translate.is_none()) { nb::object max = ns.get("max"), len = ns.get("len"); nb::object n = pu_call1(std::move(len), translate); pu_store(s[P_ndim], pu_call2(std::move(max), s[P_ndim], std::move(n))); }
    if (!rotate.is_none()) { nb::object max = ns.get("max"); nb::object n = pu_item(pu_attr(pu_call1(ns.get("_make_rotate_mat"), rotate), "shape"), nb::int_(0)); pu_store(s[P_ndim], pu_call2(std::move(max), s[P_ndim], std::move(n))); }
    if (!shear.is_none()) { nb::object max = ns.get("max"); nb::object n = pu_item(pu_attr(pu_call1(ns.get("_make_shear_mat"), shear), "shape"), nb::int_(0)); pu_store(s[P_ndim], pu_call2(std::move(max), s[P_ndim], std::move(n))); }
    return s[P_ndim];
}
nb::object pu_vector(nb::object value, nb::object ndim, nb::dict namespace_, TransformPolicyLocals& s, bool scaling) {
    PolicyNamespace ns(std::move(namespace_)); PolicyField field = scaling ? P_scale_arr : P_translate_arr;
    pu_store(s[field], pu_call1(ns.np(scaling ? "ones" : "zeros"), ndim));
    if (!value.is_none()) {
        nb::object payload = value, target = s[field]; nb::object len = ns.get("len");
        nb::object start = pu_negative(pu_call1(std::move(len), value));
        pu_setitem(std::move(target), pu_slice(std::move(start)), std::move(payload));
    }
    return s[field];
}
nb::object policy_translate_to_vector(nb::object translate, nb::object ndim, nb::dict namespace_, TransformPolicyLocals& s) { return pu_vector(std::move(translate), std::move(ndim), std::move(namespace_), s, false); }
nb::object policy_scale_to_vector(nb::object scale, nb::object ndim, nb::dict namespace_, TransformPolicyLocals& s) { return pu_vector(std::move(scale), std::move(ndim), std::move(namespace_), s, true); }
nb::object pu_component_matrix(nb::object value, nb::object ndim, nb::dict namespace_, TransformPolicyLocals& s, bool rotating) {
    PolicyNamespace ns(std::move(namespace_)); const auto full = rotating ? P_full_rotate_mat : P_full_shear_mat;
    const auto matrix = rotating ? P_rotate_mat : P_shear_mat; const auto rank = rotating ? P_rotate_mat_ndim : P_shear_mat_ndim;
    s.layout = rotating ? TransformPolicyLocals::Layout::Rotate : TransformPolicyLocals::Layout::Shear;
    pu_store(s[full], pu_call1(ns.np("eye"), ndim));
    if (!value.is_none()) {
        pu_store(s[matrix], pu_call1(ns.get(rotating ? "_make_rotate_mat" : "_make_shear_mat"), value));
        pu_store(s[rank], pu_item(pu_attr(s[matrix], "shape"), nb::int_(0)));
        nb::object payload = s[matrix], target = s[full];
        nb::object first = pu_slice(pu_negative(s[rank])); nb::object second = pu_slice(pu_negative(s[rank]));
        pu_setitem(std::move(target), nb::make_tuple(std::move(first), std::move(second)), std::move(payload));
    }
    return s[full];
}
nb::object policy_rotate_to_matrix(nb::object rotate, nb::object ndim, nb::dict namespace_, TransformPolicyLocals& s) { return pu_component_matrix(std::move(rotate), std::move(ndim), std::move(namespace_), s, true); }
nb::object policy_shear_to_matrix(nb::object shear, nb::object ndim, nb::dict namespace_, TransformPolicyLocals& s) { return pu_component_matrix(std::move(shear), std::move(ndim), std::move(namespace_), s, false); }
nb::object policy_make_rotate_mat(nb::object rotate, nb::dict namespace_) {
    PolicyNamespace ns(std::move(namespace_)); if (pu_truth(pu_call1(ns.np("isscalar"), rotate))) return pu_call1(ns.get("_make_2d_rotation"), std::move(rotate));
    if (pu_truth(pu_cmp(pu_attr(pu_call1(ns.np("array"), rotate), "ndim"), nb::int_(1), Py_EQ))) {
        if (pu_truth(pu_cmp(pu_call1(ns.get("len"), rotate), nb::int_(3), Py_EQ))) {
            nb::object fn = ns.get("_make_3d_rotation");
            // CALL_FUNCTION_EX rewrites only the truly non-iterable case;
            // custom iteration failures keep their original exception.
            if (Py_TYPE(rotate.ptr())->tp_iter == nullptr && !PySequence_Check(rotate.ptr())) {
                nb::object label = pu_check(_PyObject_FunctionStr(fn.ptr()));
                PyErr_Format(PyExc_TypeError, "%U argument after * must be an iterable, not %.200s", label.ptr(), Py_TYPE(rotate.ptr())->tp_name);
                throw nb::python_error();
            }
            nb::object args = pu_check(PySequence_Tuple(rotate.ptr()));
            return pu_check(PyObject_CallObject(fn.ptr(), args.ptr()));
        }
    }
    return pu_call1(ns.np("array"), std::move(rotate));
}
nb::list pu_row(nb::object a, nb::object b) { nb::list row; row.append(std::move(a)); row.append(std::move(b)); return row; }
nb::list pu_row3(nb::object a, nb::object b, nb::object c) { nb::list row = pu_row(std::move(a), std::move(b)); row.append(std::move(c)); return row; }
nb::object policy_make_2d_rotation(nb::object theta_degrees, nb::dict namespace_, TransformPolicyLocals& s) {
    PolicyNamespace ns(std::move(namespace_)); auto pair = pu_pair(pu_call1(ns.get("_cos_sin_degrees"), theta_degrees));
    pu_store(s[P_cos_theta], std::move(pair[0])); pu_store(s[P_sin_theta], std::move(pair[1]));
    nb::object array = ns.np("array"); nb::list rows;
    rows.append(pu_row(s[P_cos_theta], pu_negative(s[P_sin_theta]))); rows.append(pu_row(s[P_sin_theta], s[P_cos_theta]));
    return pu_call1(std::move(array), std::move(rows));
}
nb::object policy_make_3d_rotation(nb::object alpha_degrees, nb::object beta_degrees, nb::object gamma_degrees, nb::dict namespace_, TransformPolicyLocals& s) {
    PolicyNamespace ns(std::move(namespace_)); auto pair = pu_pair(pu_call1(ns.get("_cos_sin_degrees"), alpha_degrees));
    pu_store(s[P_cos_alpha], std::move(pair[0])); pu_store(s[P_sin_alpha], std::move(pair[1])); nb::object array = ns.np("array"); nb::list rows;
    rows.append(pu_row3(s[P_cos_alpha], pu_negative(s[P_sin_alpha]), nb::int_(0))); rows.append(pu_row3(s[P_sin_alpha], s[P_cos_alpha], nb::int_(0))); rows.append(pu_row3(nb::int_(0), nb::int_(0), nb::int_(1)));
    pu_store(s[P_R_alpha], pu_call1(std::move(array), std::move(rows)));
    pair = pu_pair(pu_call1(ns.get("_cos_sin_degrees"), beta_degrees)); pu_store(s[P_cos_beta], std::move(pair[0])); pu_store(s[P_sin_beta], std::move(pair[1])); array = ns.np("array"); rows = nb::list();
    rows.append(pu_row3(s[P_cos_beta], nb::int_(0), s[P_sin_beta])); rows.append(pu_row3(nb::int_(0), nb::int_(1), nb::int_(0))); rows.append(pu_row3(pu_negative(s[P_sin_beta]), nb::int_(0), s[P_cos_beta]));
    pu_store(s[P_R_beta], pu_call1(std::move(array), std::move(rows)));
    pair = pu_pair(pu_call1(ns.get("_cos_sin_degrees"), gamma_degrees)); pu_store(s[P_cos_gamma], std::move(pair[0])); pu_store(s[P_sin_gamma], std::move(pair[1])); array = ns.np("array"); rows = nb::list();
    rows.append(pu_row3(nb::int_(1), nb::int_(0), nb::int_(0))); rows.append(pu_row3(nb::int_(0), s[P_cos_gamma], pu_negative(s[P_sin_gamma]))); rows.append(pu_row3(nb::int_(0), s[P_sin_gamma], s[P_cos_gamma]));
    pu_store(s[P_R_gamma], pu_call1(std::move(array), std::move(rows)));
    nb::object partial = pu_binary(s[P_R_alpha], s[P_R_beta], PyNumber_MatrixMultiply); return pu_binary(std::move(partial), s[P_R_gamma], PyNumber_MatrixMultiply);
}
nb::object policy_cos_sin_degrees(nb::object angle_degrees, nb::dict namespace_, TransformPolicyLocals& s) {
    PolicyNamespace ns(std::move(namespace_)); pu_store(s[P_angle_radians], pu_call1(ns.np("deg2rad"), angle_degrees));
    nb::object cosine = pu_call1(ns.np("cos"), s[P_angle_radians]); nb::object sine = pu_call1(ns.np("sin"), s[P_angle_radians]); return nb::make_tuple(std::move(cosine), std::move(sine));
}
nb::object policy_make_shear_mat(nb::object shear, nb::dict namespace_) {
    PolicyNamespace ns(std::move(namespace_));
    if (pu_truth(pu_call1(ns.np("isscalar"), shear))) pu_raise(ns.get("ValueError"), nb::str("Scalars are not valid values for shear. Shear must be an upper triangular vector or square matrix with ones along the main diagonal."));
    if (pu_truth(pu_cmp(pu_attr(pu_call1(ns.np("array"), shear), "ndim"), nb::int_(1), Py_EQ))) return pu_call1(ns.get("expand_upper_triangular"), std::move(shear));
    if (!pu_truth(pu_call1(ns.get("is_matrix_triangular"), shear))) {
        nb::object error = ns.get("ValueError"); nb::object formatted = pu_format(shear);
        nb::object message = pu_concat(nb::str("Only upper triangular or lower triangular matrices are accepted for shear, got "), std::move(formatted));
        message = pu_concat(std::move(message), nb::str(". For other matrices, set the affine_matrix or linear_matrix directly.")); pu_raise(std::move(error), std::move(message));
    }
    return pu_call1(ns.np("array"), std::move(shear));
}
nb::object pu_upper_mask(PolicyNamespace& ns, nb::object dimension) {
    nb::object triu = ns.np("triu"); nb::object ones = ns.np("ones"); nb::tuple shape = nb::make_tuple(dimension, dimension);
    nb::object array = pu_call1(std::move(ones), std::move(shape)); array = pu_call2(std::move(triu), std::move(array), nb::int_(1));
    nb::object astype = pu_attr(std::move(array), "astype"); return pu_call1(std::move(astype), ns.get("bool"));
}
nb::object policy_expand_upper_triangular(nb::object vector, nb::dict namespace_, TransformPolicyLocals& s) {
    PolicyNamespace ns(std::move(namespace_)); pu_store(s[P_n], pu_call1(ns.get("len"), vector));
    nb::object sqrt = ns.np("sqrt"); nb::object value = pu_binary(nb::int_(8), s[P_n], PyNumber_Multiply); value = pu_binary(std::move(value), nb::int_(1), PyNumber_Add); value = pu_call1(std::move(sqrt), std::move(value));
    value = pu_binary(nb::int_(-1), std::move(value), PyNumber_Add); value = pu_binary(std::move(value), nb::float_(2.0), PyNumber_TrueDivide); value = pu_binary(std::move(value), nb::int_(1), PyNumber_Add); pu_store(s[P_N], std::move(value));
    if (pu_truth(pu_cmp(pu_call1(ns.np("floor"), s[P_N]), s[P_N], Py_NE))) {
        nb::object error = ns.get("ValueError"); value = pu_format(s[P_n]); value = pu_concat(std::move(value), nb::str(" is a strange number of shear elements")); pu_raise(std::move(error), std::move(value));
    }
    pu_store(s[P_N], pu_call1(ns.get("int"), s[P_N])); pu_store(s[P_inds], pu_upper_mask(ns, s[P_N])); pu_store(s[P_upper_tri], pu_call1(ns.np("eye"), s[P_N]));
    pu_setitem(s[P_upper_tri], s[P_inds], vector); return s[P_upper_tri];
}
bool pu_bad_square(nb::object matrix) {
    if (pu_truth(pu_cmp(pu_attr(matrix, "ndim"), nb::int_(2), Py_NE))) return true;
    nb::object a = pu_item(pu_attr(matrix, "shape"), nb::int_(0)); nb::object b = pu_item(pu_attr(matrix, "shape"), nb::int_(1)); return pu_truth(pu_cmp(std::move(a), std::move(b), Py_NE));
}
nb::object policy_embed_in_identity_matrix(nb::object matrix, nb::object ndim, nb::dict namespace_, TransformPolicyLocals& s) {
    PolicyNamespace ns(std::move(namespace_));
    if (pu_bad_square(matrix)) { nb::object error = ns.get("ValueError"); nb::object message = pu_concat(nb::str("Improper transform matrix "), pu_format(matrix)); pu_raise(std::move(error), std::move(message)); }
    if (pu_truth(pu_cmp(pu_item(pu_attr(matrix, "shape"), nb::int_(0)), ndim, Py_EQ))) return matrix;
    pu_store(s[P_full_matrix], pu_call1(ns.np("eye"), ndim));
    nb::object payload = matrix, target = s[P_full_matrix]; nb::object first = pu_slice(pu_negative(pu_item(pu_attr(matrix, "shape"), nb::int_(0)))); nb::object second = pu_slice(pu_negative(pu_item(pu_attr(matrix, "shape"), nb::int_(1))));
    pu_setitem(std::move(target), nb::make_tuple(std::move(first), std::move(second)), std::move(payload)); return s[P_full_matrix];
}
nb::object policy_decompose_linear_matrix(nb::object matrix, nb::object upper_triangular, nb::dict namespace_, TransformPolicyLocals& s, nb::object reshape_shape) {
    PolicyNamespace ns(std::move(namespace_)); s.layout = TransformPolicyLocals::Layout::Decompose;
    nb::object importer = nb::steal<nb::object>(PyObject_GetItem(ns.builtins.ptr(), nb::str("__import__").ptr()));
    if (!importer.is_valid()) { if (!PyErr_ExceptionMatches(PyExc_KeyError)) throw nb::python_error(); PyErr_Clear(); PyErr_SetString(PyExc_ImportError, "__import__ not found"); throw nb::python_error(); }
    nb::object imported = pu_check(PyObject_CallFunctionObjArgs(importer.ptr(), nb::str("scipy.linalg").ptr(), ns.globals.ptr(), PyEval_GetLocals(), Py_None, nb::int_(0).ptr(), nullptr)); importer.reset(); pu_store(s[P_scipy], std::move(imported));
    pu_store(s[P_n], pu_item(pu_attr(matrix, "shape"), nb::int_(0)));
    if (pu_truth(upper_triangular)) {
        nb::object qr = pu_attr(pu_attr(s[P_scipy], "linalg"), "qr"); auto pair = pu_pair(pu_call1(std::move(qr), matrix)); pu_store(s[P_rotate], std::move(pair[0])); pu_store(s[P_tri], std::move(pair[1]));
    } else {
        nb::object rq = pu_attr(pu_attr(s[P_scipy], "linalg"), "rq"); nb::object transpose = pu_attr(matrix, "T"); auto pair = pu_pair(pu_call1(std::move(rq), std::move(transpose))); pu_store(s[P_upper_tri], std::move(pair[0])); pu_store(s[P_rotate], std::move(pair[1]));
        pu_store(s[P_rotate], pu_attr(s[P_rotate], "T")); pu_store(s[P_tri], pu_attr(s[P_upper_tri], "T"));
    }
    nb::object value = pu_call1(ns.np("diag"), s[P_tri]); value = pu_call0(pu_attr(std::move(value), "copy")); pu_store(s[P_scale_with_sign], std::move(value));
    pu_store(s[P_scale], pu_call1(ns.np("abs"), s[P_scale_with_sign])); pu_store(s[P_normalize], pu_binary(s[P_scale], s[P_scale_with_sign], PyNumber_TrueDivide));
    value = pu_call1(pu_attr(s[P_normalize], "reshape"), std::move(reshape_shape)); value = pu_binary(s[P_tri], std::move(value), PyNumber_InPlaceMultiply); pu_store(s[P_tri], std::move(value));
    pu_store(s[P_rotate], pu_binary(s[P_rotate], s[P_normalize], PyNumber_InPlaceMultiply));
    nb::object left = s[P_tri]; nb::object inv = pu_attr(ns.np("linalg"), "inv"); value = pu_call1(ns.np("diag"), s[P_scale]); value = pu_call1(std::move(inv), std::move(value));
    value = pu_binary(std::move(left), std::move(value), PyNumber_MatrixMultiply); pu_store(s[P_tri_normalized], std::move(value));
    if (pu_truth(upper_triangular)) { nb::object target = s[P_tri_normalized]; nb::object mask = pu_upper_mask(ns, s[P_n]); pu_store(s[P_shear], pu_item(std::move(target), std::move(mask))); }
    else pu_store(s[P_shear], s[P_tri_normalized]);
    return nb::make_tuple(s[P_rotate], s[P_scale], s[P_shear]);
}
nb::object policy_shear_matrix_from_angle(nb::object angle, nb::object ndim, nb::object axes, nb::dict namespace_, TransformPolicyLocals& s) {
    PolicyNamespace ns(std::move(namespace_)); pu_store(s[P_matrix], pu_call1(ns.np("eye"), ndim));
    nb::object tan = ns.np("tan"), deg2rad = ns.np("deg2rad"); nb::object value = pu_binary(nb::int_(90), angle, PyNumber_Subtract); value = pu_call1(std::move(deg2rad), std::move(value)); value = pu_call1(std::move(tan), std::move(value));
    pu_setitem(s[P_matrix], axes, std::move(value)); return s[P_matrix];
}
nb::object pu_triangular(nb::object matrix, nb::dict namespace_, const char* projection) { PolicyNamespace ns(std::move(namespace_)); nb::object allclose = ns.np("allclose"); nb::object projected = pu_call1(ns.np(projection), matrix); return pu_call2(std::move(allclose), std::move(matrix), std::move(projected)); }
nb::object policy_is_matrix_upper_triangular(nb::object matrix, nb::dict namespace_) { return pu_triangular(std::move(matrix), std::move(namespace_), "triu"); }
nb::object policy_is_matrix_lower_triangular(nb::object matrix, nb::dict namespace_) { return pu_triangular(std::move(matrix), std::move(namespace_), "tril"); }
nb::object policy_is_matrix_triangular(nb::object matrix, nb::dict namespace_) { PolicyNamespace ns(std::move(namespace_)); nb::object upper = pu_call1(ns.get("is_matrix_upper_triangular"), matrix); if (pu_truth(upper)) return upper; upper.reset(); return pu_call1(ns.get("is_matrix_lower_triangular"), std::move(matrix)); }
nb::object policy_is_diagonal(nb::object matrix, nb::object tol, nb::dict namespace_, TransformPolicyLocals& s) {
    PolicyNamespace ns(std::move(namespace_)); if (pu_bad_square(matrix)) { nb::object error = ns.get("ValueError"); nb::object shape = pu_attr(matrix, "shape"); nb::object message = pu_concat(nb::str("matrix must be square, but shape="), pu_format(std::move(shape))); pu_raise(std::move(error), std::move(message)); }
    nb::object target = matrix; nb::object eye = ns.np("eye"); nb::object dim = pu_item(pu_attr(matrix, "shape"), nb::int_(0)); nb::dict kw; kw["dtype"] = ns.get("bool");
    nb::object mask = pu_kw(std::move(eye), nb::make_tuple(std::move(dim)), std::move(kw)); mask = pu_check(PyNumber_Invert(mask.ptr())); pu_store(s[P_non_diag], pu_item(std::move(target), std::move(mask)));
    if (pu_truth(pu_cmp(tol, nb::int_(0), Py_EQ))) return pu_cmp(pu_call1(ns.np("count_nonzero"), s[P_non_diag]), nb::int_(0), Py_EQ);
    nb::object max = ns.np("max"); nb::object values = pu_call1(ns.np("abs"), s[P_non_diag]); return pu_cmp(pu_call1(std::move(max), std::move(values)), std::move(tol), Py_LE);
}
nb::object policy_transform_coerce(nb::object ndim, nb::object name, nb::dict namespace_, TransformCoerceLocals& s) {
    PolicyNamespace ns(std::move(namespace_));
    if (s.affine.is_none()) {
        nb::object ctor = ns.get("Affine"), eye = ns.np("eye"); nb::object dimension = pu_binary(ndim, nb::int_(1), PyNumber_Add); nb::object matrix = pu_call1(std::move(eye), std::move(dimension)); nb::dict kw; kw["affine_matrix"] = std::move(matrix); kw["ndim"] = ndim; pu_store(s.affine, pu_kw(std::move(ctor), nb::tuple(), std::move(kw)));
    } else {
        nb::object isinstance = ns.get("isinstance"); nb::object type = ns.np("ndarray");
        if (pu_truth(pu_call2(std::move(isinstance), s.affine, std::move(type)))) {
            nb::object ctor = ns.get("Affine"); nb::dict kw; kw["affine_matrix"] = s.affine; kw["ndim"] = ndim; pu_store(s.affine, pu_kw(std::move(ctor), nb::tuple(), std::move(kw)));
        } else {
            isinstance = ns.get("isinstance"); type = ns.get("list");
            if (pu_truth(pu_call2(std::move(isinstance), s.affine, std::move(type)))) {
                nb::object ctor = ns.get("Affine"), array = ns.np("array"); nb::object matrix = pu_call1(std::move(array), s.affine); nb::dict kw; kw["affine_matrix"] = std::move(matrix); kw["ndim"] = ndim; pu_store(s.affine, pu_kw(std::move(ctor), nb::tuple(), std::move(kw)));
            } else {
                isinstance = ns.get("isinstance"); type = ns.get("Affine");
                if (!pu_truth(pu_call2(std::move(isinstance), s.affine, std::move(type)))) {
                    nb::object error = ns.get("TypeError"); nb::object actual = pu_call1(ns.get("type"), s.affine); nb::object message = pu_concat(nb::str("affine input not recognized. must be either napari.utils.transforms.Affine or ndarray. Got "), pu_format(std::move(actual))); pu_raise(std::move(error), std::move(message));
                }
            }
        }
    }
    if (!name.is_none() && PyObject_SetAttrString(s.affine.ptr(), "name", name.ptr()) < 0) throw nb::python_error(); return s.affine;
}
} // namespace

void bind_transform_family_policy(nb::module_& m) {
    auto locals = nb::class_<TransformPolicyLocals>(m, "TransformPolicyLocals", nb::is_weak_referenceable(), nb::type_slots(pu_locals_slots));
#define PU_PROP(name) locals.def_prop_ro(#name, [](const TransformPolicyLocals& s) { return pu_optional(s.values[P_##name]); });
    PU_FIELDS(PU_PROP)
#undef PU_PROP
    nb::class_<TransformCoerceLocals>(m, "TransformCoerceLocals", nb::is_weak_referenceable(), nb::type_slots(pu_coerce_slots)).def_prop_ro("affine", [](const TransformCoerceLocals& s) { return pu_optional(s.affine); });
    nb::class_<TransformMatrixIterator>(m, "TransformMatrixIterator", nb::is_weak_referenceable(), nb::type_slots(pu_iterator_slots))
        .def("__iter__", [](nb::handle self) { return nb::borrow<nb::object>(self); })
        .def("__next__", [](TransformMatrixIterator& s) { return pu_iterator_send(s, nb::none()); })
        .def("send", &pu_iterator_send, nb::arg("value").none()).def("close", &pu_iterator_close).def("throw", &pu_iterator_throw);
    m.def("policy_transform_locals", [] { return TransformPolicyLocals(); });
    m.def("policy_transform_coerce_locals", [](nb::object affine) { TransformCoerceLocals s; s.affine = std::move(affine); return s; }, nb::arg("affine").none());
    m.def("policy_compose_linear_matrix", &policy_compose_linear_matrix, nb::arg("rotate").none(), nb::arg("scale").none(), nb::arg("shear").none(), nb::arg("namespace"), nb::arg("locals_owner"), nb::arg("generator_factory").none());
    m.def("policy_infer_ndim", &policy_infer_ndim, nb::arg("scale").none(), nb::arg("translate").none(), nb::arg("rotate").none(), nb::arg("shear").none(), nb::arg("linear_matrix").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    m.def("policy_translate_to_vector", &policy_translate_to_vector, nb::arg("translate").none(), nb::arg("ndim").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    m.def("policy_scale_to_vector", &policy_scale_to_vector, nb::arg("scale").none(), nb::arg("ndim").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    m.def("policy_rotate_to_matrix", &policy_rotate_to_matrix, nb::arg("rotate").none(), nb::arg("ndim").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    m.def("policy_make_rotate_mat", &policy_make_rotate_mat, nb::arg("rotate").none(), nb::arg("namespace"));
    m.def("policy_make_2d_rotation", &policy_make_2d_rotation, nb::arg("theta_degrees").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    m.def("policy_make_3d_rotation", &policy_make_3d_rotation, nb::arg("alpha_degrees").none(), nb::arg("beta_degrees").none(), nb::arg("gamma_degrees").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    m.def("policy_cos_sin_degrees", &policy_cos_sin_degrees, nb::arg("angle_degrees").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    m.def("policy_shear_to_matrix", &policy_shear_to_matrix, nb::arg("shear").none(), nb::arg("ndim").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    m.def("policy_make_shear_mat", &policy_make_shear_mat, nb::arg("shear").none(), nb::arg("namespace"));
    m.def("policy_expand_upper_triangular", &policy_expand_upper_triangular, nb::arg("vector").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    m.def("policy_embed_in_identity_matrix", &policy_embed_in_identity_matrix, nb::arg("matrix").none(), nb::arg("ndim").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    m.def("policy_decompose_linear_matrix", &policy_decompose_linear_matrix, nb::arg("matrix").none(), nb::arg("upper_triangular").none(), nb::arg("namespace"), nb::arg("locals_owner"), nb::arg("reshape_shape").none());
    m.def("policy_shear_matrix_from_angle", &policy_shear_matrix_from_angle, nb::arg("angle").none(), nb::arg("ndim").none(), nb::arg("axes").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    m.def("policy_is_matrix_upper_triangular", &policy_is_matrix_upper_triangular, nb::arg("matrix").none(), nb::arg("namespace"));
    m.def("policy_is_matrix_lower_triangular", &policy_is_matrix_lower_triangular, nb::arg("matrix").none(), nb::arg("namespace"));
    m.def("policy_is_matrix_triangular", &policy_is_matrix_triangular, nb::arg("matrix").none(), nb::arg("namespace"));
    m.def("policy_is_diagonal", &policy_is_diagonal, nb::arg("matrix").none(), nb::arg("tol").none(), nb::arg("namespace"), nb::arg("locals_owner"));
    m.def("policy_transform_coerce", &policy_transform_coerce, nb::arg("ndim").none(), nb::arg("name").none(), nb::arg("namespace"), nb::arg("locals_owner"));
}
} // namespace nx
