#include "arrays.hpp"
#include "slicing.hpp"
#include <nanobind/stl/vector.h>
#include <nanobind/stl/string.h>

namespace nx {
namespace {
using namespace app;
nb::object numpy() { return nb::module_::import_("numpy"); }
nb::dict executor_namespace(const char* module_name) {
    nb::module_ module = nb::module_::import_(module_name);
    return nb::borrow<nb::dict>(PyModule_GetDict(module.ptr()));
}
Input<double> doubles(nb::handle value) {
    return nb::cast<Input<double>>(numpy().attr("asarray")(value, nb::arg("dtype") = "float64"));
}
Matrix matrix(nb::handle value) {
    auto input = doubles(value);
    if (input.ndim() != 2) throw std::invalid_argument("expected a two-dimensional array");
    Matrix result(input.shape(0), input.shape(1));
    for (size_t i = 0; i < result.rows; ++i)
        for (size_t j = 0; j < result.cols; ++j) result(i, j) = at(input, i, j);
    return result;
}
std::vector<double> vector(nb::handle value) {
    auto input = doubles(value);
    if (input.ndim() != 1) throw std::invalid_argument("expected a one-dimensional array");
    std::vector<double> result(input.shape(0));
    for (size_t i = 0; i < result.size(); ++i) result[i] = at(input, i);
    return result;
}
Output<double> array(const Matrix& value) { return from_vector(value.values, {value.rows, value.cols}); }
nb::tuple as_tuple(nb::handle value) {
    PyObject* result = PySequence_Tuple(value.ptr());
    if (!result) throw nb::python_error();
    return nb::steal<nb::tuple>(result);
}
nb::tuple selections(const Selections& values) {
    nb::list result;
    for (const auto& value : values) {
        if (value.kind == AxisSelection::Kind::index) result.append(value.start);
        else {
            nb::object begin = value.kind == AxisSelection::Kind::all ? nb::none() : nb::cast(value.start);
            nb::object end = value.kind == AxisSelection::Kind::all ? nb::none() : nb::cast(value.stop);
            PyObject* selected = PySlice_New(begin.ptr(), end.ptr(), Py_None);
            if (!selected) throw nb::python_error();
            result.append(nb::steal<nb::object>(selected));
        }
    }
    return as_tuple(result);
}
std::vector<Index> axes(nb::handle slice, const char* field) {
    return nb::cast<std::vector<Index>>(slice.attr(field));
}
nb::tuple order(nb::handle request) {
    auto result = scalar_executor_order_compat(nb::borrow<nb::object>(request), executor_namespace("napari.layers._scalar_field._slice"));
    return nb::borrow<nb::tuple>(result);
}
nb::object projected(nb::handle request, nb::handle data, nb::handle data_slice) {
    return scalar_executor_project_compat(nb::borrow<nb::object>(request),
        nb::borrow<nb::object>(data), nb::borrow<nb::object>(data_slice), executor_namespace("napari.layers._scalar_field._slice"));
}
nb::object view(nb::handle data) {
    return nb::module_::import_("napari.layers._scalar_field._slice").attr("_ScalarFieldView")(
        nb::arg("raw") = data, nb::arg("view") = data);
}
nb::object response(nb::handle request, nb::handle image, nb::handle thumbnail, nb::handle transform) {
    return nb::module_::import_("napari.layers._scalar_field._slice").attr("_ScalarFieldSliceResponse")(
        nb::arg("image") = image, nb::arg("thumbnail") = thumbnail, nb::arg("tile_to_data") = transform,
        nb::arg("slice_input") = request.attr("slice_input"), nb::arg("request_id") = request.attr("id"));
}
nb::object scalar_single(nb::handle request) {
    return scalar_executor_single_compat(nb::borrow<nb::object>(request), executor_namespace("napari.layers._scalar_field._slice"));
}
nb::object scalar_multi(nb::handle request) {
    return scalar_executor_multi_compat(nb::borrow<nb::object>(request), executor_namespace("napari.layers._scalar_field._slice"));
}
nb::object scalar_request(nb::handle request) {
    return scalar_executor_request_compat(nb::borrow<nb::object>(request), executor_namespace("napari.layers._scalar_field._slice"));
}
nb::object scalar_global(nb::handle namespace_, const char* name) {
    // The facade supplies its executing module's real globals. This keeps
    // runtime factory replacements visible even if sys.modules is replaced.
    if (!PyDict_Check(namespace_.ptr()))
        throw nb::type_error("namespace must be a dictionary");
    nb::str key(name);
    PyObject* value = PyDict_GetItemWithError(namespace_.ptr(), key.ptr());
    if (!value && PyErr_Occurred()) throw nb::python_error();
    if (!value) {
        // The native call has no Python frame of its own: these are the
        // executing facade's captured builtins, as for LOAD_GLOBAL.
        value = PyDict_GetItemWithError(PyEval_GetBuiltins(), key.ptr());
        if (!value && PyErr_Occurred()) throw nb::python_error();
    }
    if (!value) {
        PyObject* text = PyUnicode_FromFormat("name '%s' is not defined", name);
        if (!text) throw nb::python_error();
        nb::object message = nb::steal<nb::object>(text);
        nb::object error = nb::borrow<nb::object>(PyExc_NameError)(message,
            nb::arg("name") = key);
        PyErr_SetObject(PyExc_NameError, error.ptr());
        throw nb::python_error();
    }
    return nb::borrow<nb::object>(value);
}
bool scalar_truth(nb::handle value) {
    const int result = PyObject_IsTrue(value.ptr());
    if (result < 0) throw nb::python_error();
    return result != 0;
}
nb::object scalar_layer_attribute(nb::handle state, const char* name) {
    nb::object layer = state.attr("layer");
    return layer.attr(name);
}
nb::object scalar_view_from_raw(nb::handle cls, nb::handle raw, nb::handle converter) {
    nb::object owner = nb::borrow<nb::object>(cls);
    nb::object source = nb::borrow<nb::object>(raw);
    nb::object convert = nb::borrow<nb::object>(converter);
    nb::object displayed = convert(source);
    return owner(nb::arg("raw") = source, nb::arg("view") = displayed);
}
nb::object scalar_response_to_displayed(nb::handle response_, nb::handle converter,
    nb::handle namespace_) {
    nb::object source = nb::borrow<nb::object>(response_);
    nb::object convert = nb::borrow<nb::object>(converter);
    nb::object globals = nb::borrow<nb::object>(namespace_);
    nb::object image;
    {
        nb::object factory = scalar_global(globals, "_ScalarFieldView").attr("from_raw");
        nb::object raw = source.attr("image").attr("raw");
        image = factory(nb::arg("raw") = raw, nb::arg("converter") = convert);
    }
    nb::object thumbnail = image;
    bool different;
    {
        // Re-read both fields after the first conversion: the converter may
        // reenter and replace the response's image or thumbnail.
        nb::object current_thumbnail = source.attr("thumbnail");
        nb::object current_image = source.attr("image");
        different = current_thumbnail.ptr() != current_image.ptr();
        current_thumbnail.reset();
        current_image.reset();
    }
    if (different) {
        nb::object factory = scalar_global(globals, "_ScalarFieldView").attr("from_raw");
        nb::object raw = source.attr("thumbnail").attr("raw");
        thumbnail = factory(nb::arg("raw") = raw, nb::arg("converter") = convert);
    }
    nb::object factory = scalar_global(globals, "_ScalarFieldSliceResponse");
    nb::object tile_to_data = source.attr("tile_to_data");
    nb::object slice_input = source.attr("slice_input");
    nb::object request_id = source.attr("request_id");
    nb::object empty = source.attr("empty");
    return factory(nb::arg("image") = image, nb::arg("thumbnail") = thumbnail,
        nb::arg("tile_to_data") = tile_to_data, nb::arg("slice_input") = slice_input,
        nb::arg("request_id") = request_id, nb::arg("empty") = empty);
}
nb::object scalar_prepare_slice_response(nb::handle state_, nb::handle response_) {
    nb::object state = nb::borrow<nb::object>(state_);
    nb::object source = nb::borrow<nb::object>(response_);
    nb::object apply = source.attr("to_displayed");
    nb::object converter = scalar_layer_attribute(state, "_raw_to_displayed");
    return apply(converter);
}
void scalar_apply_slice_response(nb::handle state_, nb::handle response_) {
    nb::object state = nb::borrow<nb::object>(state_);
    nb::object displayed = nb::borrow<nb::object>(response_);
    {
        nb::object slice_input = displayed.attr("slice_input");
        state.attr("_slice_input") = slice_input;
    }
    {
        nb::object tile_to_data = displayed.attr("tile_to_data");
        nb::object transforms = scalar_layer_attribute(state, "_transforms");
        transforms[nb::int_(0)] = tile_to_data;
    }
    {
        nb::object tile_to_data = displayed.attr("tile_to_data");
        state.attr("transforms") = tile_to_data;
    }
    state.attr("_slice") = displayed;
}
void image_apply_slice_response(nb::handle state_, nb::handle response_,
    nb::handle base_apply_, nb::handle namespace_) {
    nb::object state = nb::borrow<nb::object>(state_);
    nb::object source = nb::borrow<nb::object>(response_);
    nb::object base_apply = nb::borrow<nb::object>(base_apply_);
    nb::object globals = nb::borrow<nb::object>(namespace_);
    base_apply(source);
    // Keep the original raw provider and selected input alive for the whole
    // method, like the Python locals, without materializing either one.
    nb::object data, input_data;
    if (scalar_truth(scalar_layer_attribute(state, "auto_contrast"))) {
        data = source.attr("image").attr("raw");
        input_data = scalar_truth(scalar_layer_attribute(state, "multiscale"))
            ? nb::object(data[nb::int_(-1)]) : data;
        nb::object limits;
        {
            // LOAD_GLOBAL and argument evaluation order remain observable to
            // monkeypatches and descriptor callbacks after base publication.
            nb::object calculate = scalar_global(globals, "calc_data_range");
            nb::object cast_data;
            {
                nb::object cast = scalar_global(globals, "typing").attr("cast");
                nb::object protocol = scalar_global(globals, "LayerDataProtocol");
                cast_data = cast(protocol, input_data);
            }
            nb::object rgb = scalar_layer_attribute(state, "rgb");
            nb::object dtype = scalar_layer_attribute(state, "dtype");
            limits = calculate(cast_data, nb::arg("rgb") = rgb, nb::arg("dtype") = dtype);
        }
        nb::object layer = state.attr("layer");
        layer.attr("contrast_limits") = limits;
    }
    if (scalar_truth(scalar_layer_attribute(state, "_should_calc_clims"))) {
        scalar_layer_attribute(state, "reset_contrast_limits_range")();
        scalar_layer_attribute(state, "reset_contrast_limits")();
        nb::object layer = state.attr("layer");
        layer.attr("_should_calc_clims") = nb::bool_(false);
    } else if (scalar_truth(scalar_layer_attribute(state, "auto_contrast"))) {
        scalar_layer_attribute(state, "reset_contrast_limits")();
    }
}
bool contrast_mode_equal(nb::handle mode, const char* expected) {
    nb::str value(expected);
    PyObject* compared = PyObject_RichCompare(mode.ptr(), value.ptr(), Py_EQ);
    if (!compared) throw nb::python_error();
    nb::object result = nb::steal<nb::object>(compared);
    return scalar_truth(result);
}
nb::object image_select_contrast_data(nb::handle layer_, nb::handle mode_) {
    nb::object layer = nb::borrow<nb::object>(layer_);
    nb::object mode = nb::borrow<nb::object>(mode_);
    if (contrast_mode_equal(mode, "data")) {
        const bool multiscale = scalar_truth(layer.attr("multiscale"));
        nb::object data = layer.attr("data");
        return multiscale ? nb::object(data[nb::int_(-1)]) : data;
    }
    if (contrast_mode_equal(mode, "slice"))
        return layer.attr("_slice").attr("image").attr("raw");
    // Resolve the error factory in the executing facade, like LOAD_GLOBAL.
    PyObject* namespace_ = PyEval_GetGlobals();
    if (!namespace_) namespace_ = PyEval_GetBuiltins();
    nb::object factory = scalar_global(nb::handle(namespace_), "ValueError");
    PyObject* text = PyUnicode_FromFormat("mode must be either 'data' or 'slice', got %R", mode.ptr());
    if (!text) throw nb::python_error();
    nb::object message = nb::steal<nb::object>(text);
    nb::object error = factory(message);
    if (PyExceptionInstance_Check(error.ptr())) {
        PyErr_SetObject(reinterpret_cast<PyObject*>(Py_TYPE(error.ptr())), error.ptr());
    } else if (PyExceptionClass_Check(error.ptr())) {
        PyErr_SetNone(error.ptr());
    } else {
        PyErr_SetString(PyExc_TypeError, "exceptions must derive from BaseException");
    }
    throw nb::python_error();
}
nb::object image_calc_data_range(nb::handle layer_, nb::handle input_data_, nb::handle namespace_) {
    nb::object layer = nb::borrow<nb::object>(layer_);
    nb::object input_data = nb::borrow<nb::object>(input_data_);
    nb::object globals = nb::borrow<nb::object>(namespace_);
    nb::object calculate = scalar_global(globals, "calc_data_range");
    nb::object cast_data;
    {
        nb::object cast = scalar_global(globals, "cast");
        nb::object protocol = scalar_global(globals, "LayerDataProtocol");
        cast_data = cast(protocol, input_data);
        cast.reset();
        protocol.reset();
    }
    nb::object rgb = layer.attr("rgb");
    nb::object dtype = layer.attr("dtype");
    nb::object result = calculate(cast_data, nb::arg("rgb") = rgb, nb::arg("dtype") = dtype);
    calculate.reset();
    cast_data.reset();
    rgb.reset();
    dtype.reset();
    return result;
}
nb::object image_prepare_raw_display(nb::handle layer_, nb::handle namespace_) {
    nb::object layer = nb::borrow<nb::object>(layer_);
    nb::object globals = nb::borrow<nb::object>(namespace_);
    nb::object coerce = scalar_global(globals, "_coerce_contrast_limits");
    nb::object limits = layer.attr("contrast_limits");
    nb::object result = coerce(limits);
    coerce.reset();
    limits.reset();
    return result;
}
nb::object image_raw_to_displayed(nb::handle layer_, nb::handle raw_,
    nb::handle fixed_contrast_info_, nb::handle namespace_) {
    nb::object layer = nb::borrow<nb::object>(layer_);
    nb::object raw = nb::borrow<nb::object>(raw_);
    nb::object info = nb::borrow<nb::object>(fixed_contrast_info_);
    nb::object globals = nb::borrow<nb::object>(namespace_);
    nb::object condition;
    {
        nb::object allclose = scalar_global(globals, "np").attr("allclose");
        nb::object fixed_limits = info.attr("contrast_limits");
        nb::object current_limits = layer.attr("contrast_limits");
        condition = allclose(fixed_limits, current_limits);
        allclose.reset();
        fixed_limits.reset();
        current_limits.reset();
    }
    const bool unchanged = scalar_truth(condition);
    condition.reset();
    if (unchanged) return raw;
    return info.attr("coerce_data")(raw);
}
nb::object intensity_select_contrast_mode(nb::handle layer_, nb::handle mode_) {
    nb::object layer = nb::borrow<nb::object>(layer_);
    nb::object mode = nb::borrow<nb::object>(mode_);
    return scalar_truth(mode) ? mode : nb::object(layer.attr("_auto_contrast_source"));
}
void intensity_reset_contrast_limits(nb::handle layer_, nb::handle mode_) {
    nb::object layer = nb::borrow<nb::object>(layer_);
    nb::object mode = nb::borrow<nb::object>(mode_);
    nb::object limits = layer.attr("_calc_data_range")(mode);
    layer.attr("contrast_limits") = limits;
}
nb::object intensity_contrast_dtype(nb::handle layer_, nb::handle namespace_) {
    nb::object layer = nb::borrow<nb::object>(layer_);
    nb::object globals = nb::borrow<nb::object>(namespace_);
    nb::object normalize = scalar_global(globals, "normalize_dtype");
    nb::object dtype = layer.attr("dtype");
    nb::object result = normalize(dtype);
    normalize.reset();
    dtype.reset();
    return result;
}
nb::tuple intensity_prepare_contrast_range(nb::handle layer_, nb::handle dtype_,
    nb::handle mode_, nb::handle namespace_) {
    nb::object layer = nb::borrow<nb::object>(layer_);
    nb::object dtype = nb::borrow<nb::object>(dtype_);
    nb::object mode = nb::borrow<nb::object>(mode_);
    nb::object globals = nb::borrow<nb::object>(namespace_);
    nb::object condition;
    {
        nb::object issubdtype = scalar_global(globals, "np").attr("issubdtype");
        nb::object integer = scalar_global(globals, "np").attr("integer");
        condition = issubdtype(dtype, integer);
        issubdtype.reset();
        integer.reset();
    }
    const bool is_integer = scalar_truth(condition);
    condition.reset();
    if (is_integer) {
        nb::object info = scalar_global(globals, "np").attr("iinfo")(dtype);
        return nb::make_tuple(mode, info, true);
    }
    nb::object selected = intensity_select_contrast_mode(layer, mode);
    return nb::make_tuple(selected, nb::none(), false);
}
void intensity_reset_contrast_limits_range(nb::handle layer_, nb::handle mode_,
    nb::handle info_, bool is_integer) {
    nb::object layer = nb::borrow<nb::object>(layer_);
    nb::object mode = nb::borrow<nb::object>(mode_);
    nb::object info = nb::borrow<nb::object>(info_);
    nb::object limits;
    if (is_integer) {
        nb::object minimum = info.attr("min");
        nb::object maximum = info.attr("max");
        limits = nb::make_tuple(minimum, maximum);
    } else {
        limits = layer.attr("_calc_data_range")(mode);
    }
    layer.attr("contrast_limits_range") = limits;
}
nb::object image_project(nb::handle data, const std::vector<Index>& axis, const std::string& mode) {
    return scalar_executor_image_project_compat(nb::borrow<nb::object>(data),
        nb::cast(axis), nb::cast(mode), executor_namespace("napari.layers.image._slice"));
}
nb::tuple vectors(nb::handle data, nb::handle slice, const std::vector<Index>& hidden, const std::string& mode) {
    auto input = doubles(data);
    if (input.ndim() != 3 || input.shape(1) != 2) throw std::invalid_argument("vector data must have shape (N, 2, D)");
    Matrix origins(input.shape(0), input.shape(2));
    for (size_t i = 0; i < origins.rows; ++i)
        for (size_t j = 0; j < origins.cols; ++j)
            origins(i, j) = read(input.data(), static_cast<Index>(i) * input.stride(0) + static_cast<Index>(j) * input.stride(2));
    auto thick = matrix(slice);
    VisibleVectors result;
    {
        nb::gil_scoped_release release;
        result = slice_vectors(origins, thick, hidden, mode != "none", mode == "fade");
    }
    return nb::make_tuple(from_vector(result.indices, {result.indices.size()}), from_vector(result.alphas, {result.alphas.size()}));
}
nb::object surface_response(nb::handle request, nb::handle vertices, nb::handle faces,
    nb::handle values, nb::handle colors, nb::handle texcoords) {
    return nb::module_::import_("napari.layers.surface._slice").attr("_SurfaceSliceResponse")(
        nb::arg("vertices") = vertices, nb::arg("faces") = faces, nb::arg("values") = values,
        nb::arg("vertex_colors") = colors, nb::arg("texcoords") = texcoords,
        nb::arg("slice_input") = request.attr("slice_input"), nb::arg("request_id") = request.attr("id"));
}
nb::object surface_empty(nb::handle request) {
    auto vertices = numpy().attr("empty")(nb::make_tuple(0, request.attr("slice_input").attr("ndisplay")), nb::arg("dtype") = "int");
    auto faces = numpy().attr("empty")(nb::make_tuple(0, 3), nb::arg("dtype") = "int");
    return surface_response(request, vertices, faces, nb::none(), nb::none(), nb::none());
}
nb::object surface(nb::handle request) {
    nb::object data = request.attr("data");
    if (!nb::len(data)) return surface_empty(request);
    const auto hidden = axes(request.attr("slice_input"), "not_displayed");
    if (hidden.empty()) return surface_response(request, data[nb::int_(0)], data[nb::int_(1)],
        nb::len(data) == 3 ? nb::object(data[nb::int_(2)]) : nb::none(),
        request.attr("vertex_colors"), request.attr("texcoords"));
    nb::object vertices_original = data[nb::int_(0)], values_original = data[nb::int_(2)];
    const Index extra = nb::cast<Index>(values_original.attr("ndim")) - 1;
    const auto displayed = axes(request.attr("slice_input"), "displayed");
    std::vector<Index> vertex_axes, hidden_vertex_axes;
    for (Index value : displayed) {
        if (value < extra) {
            if (PyErr_WarnEx(PyExc_UserWarning,
                "All extra dimensions corresponding to vertex values must be non-displayed dimensions. Data cannot be shown.", 2) < 0)
                throw nb::python_error();
            return surface_empty(request);
        }
        vertex_axes.push_back(value - extra);
    }
    for (Index value : hidden) if (value >= extra) hidden_vertex_axes.push_back(value - extra);
    auto origins = matrix(vertices_original);
    auto full_thick = matrix(request.attr("data_slice").attr("as_array")());
    Matrix thick(3, origins.cols);
    for (Index value : hidden_vertex_axes) {
        // Preserve the baseline's vertex-value dimension indexing.
        if (value >= static_cast<Index>(origins.cols) - extra) throw nb::index_error("surface slicing dimension is out of bounds");
        for (size_t row = 0; row < 3; ++row)
            thick(row, static_cast<size_t>(value)) = full_thick(row, static_cast<size_t>(value + extra));
    }
    nb::object faces_original = data[nb::int_(1)];
    nb::object face_kind = faces_original.attr("dtype").attr("kind");
    if (nb::cast<std::string>(face_kind) != "i" && nb::cast<std::string>(face_kind) != "u")
        throw nb::index_error("arrays used as indices must be of integer (or boolean) type");
    auto faces_input = nb::cast<Input<Index>>(numpy().attr("asarray")(faces_original, nb::arg("dtype") = "int64"));
    if (faces_input.ndim() != 2) throw std::invalid_argument("faces must be a two-dimensional array");
    const size_t width = faces_input.shape(1);
    std::vector<Index> original_faces(faces_input.size());
    for (size_t i = 0; i < faces_input.shape(0); ++i)
        for (size_t j = 0; j < width; ++j) original_faces[i * width + j] = at(faces_input, i, j);
    VisibleVectors visible;
    std::vector<Index> remapped;
    const bool project = nb::cast<std::string>(request.attr("projection_mode")) != "none";
    {
        nb::gil_scoped_release release;
        visible = slice_vectors(origins, thick, hidden_vertex_axes, project, false);
        remapped = reindex_faces(original_faces, width, origins.rows, visible.indices);
    }
    auto selected = nb::cast(from_vector(visible.indices, {visible.indices.size()}));
    nb::object vertices = vertices_original[numpy().attr("ix_")(selected, nb::cast(vertex_axes))];
    auto faces = nb::cast(from_vector(remapped, {remapped.size() / width, width}));
    nb::list values_indices;
    for (Index value : hidden) if (value < extra) {
        PyObject* number = PyLong_FromDouble(full_thick(0, static_cast<size_t>(value)));
        if (!number) throw nb::python_error();
        values_indices.append(nb::steal<nb::object>(number));
    }
    auto value_key = as_tuple(values_indices);
    nb::object values = nb::none(), colors = nb::none(), texcoords = nb::none();
    nb::object original_colors = request.attr("vertex_colors"), original_texture = request.attr("texcoords");
    if (!original_colors.is_none()) colors = original_colors[value_key][selected];
    else if (nb::len(data) == 3) values = values_original[value_key][selected];
    if (!original_texture.is_none()) texcoords = original_texture[selected];
    return surface_response(request, vertices, faces, values, colors, texcoords);
}
} // namespace

void bind_slicing(nb::module_& m) {
    m.def("point_slice_indices", [](nb::handle point) { return selections(point_selections(vector(point))); });
    m.def("thick_slice_indices", [](nb::handle thick, const std::vector<Index>& displayed) {
        return selections(thick_selections(matrix(thick), displayed));
    });
    m.def("scalar_slice_out_of_bounds", [](nb::handle thick, const std::vector<Index>& hidden, nb::handle shape, bool project) {
        return slice_out_of_bounds(matrix(thick), hidden, vector(shape), project);
    });
    m.def("scalar_slice_at_level", [](nb::handle thick, nb::handle factors, nb::handle shape) {
        return array(slice_at_level(matrix(thick), vector(factors), vector(shape)));
    });
    m.def("scalar_slice_order", &order);
    m.def("scalar_slice_single", &scalar_single);
    m.def("scalar_slice_request", &scalar_request);
    m.def("scalar_slice_multi", &scalar_multi);
    m.def("scalar_slice_project", &projected);
    m.def("scalar_view_from_raw", &scalar_view_from_raw);
    m.def("scalar_response_to_displayed", &scalar_response_to_displayed);
    m.def("scalar_prepare_slice_response", &scalar_prepare_slice_response);
    m.def("scalar_apply_slice_response", &scalar_apply_slice_response);
    m.def("image_apply_slice_response", &image_apply_slice_response);
    m.def("image_select_contrast_data", &image_select_contrast_data,
          nb::arg("layer").none(), nb::arg("mode").none());
    m.def("image_calc_data_range", &image_calc_data_range,
          nb::arg("layer").none(), nb::arg("input_data").none(), nb::arg("namespace").none());
    m.def("image_prepare_raw_display", &image_prepare_raw_display,
          nb::arg("layer").none(), nb::arg("namespace").none());
    m.def("image_raw_to_displayed", &image_raw_to_displayed,
          nb::arg("layer").none(), nb::arg("raw").none(),
          nb::arg("fixed_contrast_info").none(), nb::arg("namespace").none());
    m.def("intensity_select_contrast_mode", &intensity_select_contrast_mode,
          nb::arg("layer").none(), nb::arg("mode").none());
    m.def("intensity_reset_contrast_limits", &intensity_reset_contrast_limits,
          nb::arg("layer").none(), nb::arg("mode").none());
    m.def("intensity_contrast_dtype", &intensity_contrast_dtype,
          nb::arg("layer").none(), nb::arg("namespace").none());
    m.def("intensity_prepare_contrast_range", &intensity_prepare_contrast_range,
          nb::arg("layer").none(), nb::arg("dtype").none(),
          nb::arg("mode").none(), nb::arg("namespace").none());
    m.def("intensity_reset_contrast_limits_range", &intensity_reset_contrast_limits_range,
          nb::arg("layer").none(), nb::arg("mode").none(), nb::arg("info").none(),
          nb::arg("is_integer"));
    m.def("image_project_slice", &image_project);
    m.def("vector_slice_data", &vectors);
    m.def("vector_slice_request", [](nb::handle request) {
        return vectors(request.attr("data"), request.attr("data_slice").attr("as_array")(),
            axes(request.attr("slice_input"), "not_displayed"), nb::cast<std::string>(request.attr("projection_mode")));
    });
    m.def("surface_slice_request", &surface);
    m.def("surface_empty_response", &surface_empty);
}
} // namespace nx
