"""Layer contracts compared with the pinned Python implementations."""

import copy
import dataclasses
import gc
import importlib.util
import pickle
import sys
import weakref
from pathlib import Path

import dask.array as da
import numpy as np
import pytest

from napari.components import Dims, LayerList
from napari.layers import Image, Points
from napari.layers._scalar_field._slice import _ScalarFieldSliceRequest
from napari.layers.points._points_constants import PointsProjectionMode
from napari.layers.utils._slice_input import _SliceInput, _ThickNDSlice
from napari.layers.vectors._slice import _VectorSliceRequest
from napari.layers.vectors._vectors_constants import VectorsProjectionMode
from napari.utils import naming
from napari.utils.transforms import Affine
from naparixx import native

ROOT = Path(__file__).resolve().parents[1]


@pytest.fixture(scope="module")
def reference():
    paths = {
        "transforms": "utils/transforms/transform_utils.py",
        "input": "layers/utils/_slice_input.py",
        "scalar": "layers/_scalar_field/_slice.py",
        "image": "layers/image/_slice.py",
        "vectors": "layers/vectors/_slice.py",
        "points": "layers/points/_slice.py",
        "surface": "layers/surface/_slice.py",
        "affine": "utils/transforms/transforms.py",
        "naming": "utils/naming.py",
    }
    result = {}
    for key, relative in paths.items():
        path = ROOT / ".upstream/napari/src/napari" / relative
        if not path.exists():
            pytest.skip("Run tools/fetch_upstream.py for differential layer tests")
        name = f"_naparixx_reference_layer_{key}"
        spec = importlib.util.spec_from_file_location(name, path)
        module = importlib.util.module_from_spec(spec)
        sys.modules[name] = module
        spec.loader.exec_module(module)
        result[key] = module
        if key == "affine":
            for name, function in vars(result["transforms"]).items():
                if callable(function) and hasattr(module, name):
                    setattr(module, name, function)
    yield result
    for key in result:
        sys.modules.pop(f"_naparixx_reference_layer_{key}", None)


@pytest.mark.parametrize("ndim", [2, 3, 4, 7])
@pytest.mark.parametrize("upper", [True, False])
def test_decomposition_matches_reference(reference, ndim, upper):
    rng = np.random.default_rng(ndim)
    for _ in range(15):
        matrix = rng.normal(size=(ndim, ndim))
        expected = reference["transforms"].decompose_linear_matrix(matrix, upper)
        actual = native.transform_decompose(matrix[::-1, ::-1][::-1, ::-1], upper)
        for left, right in zip(actual, expected, strict=True):
            np.testing.assert_allclose(left, right, atol=2e-12, rtol=2e-12)
        np.testing.assert_allclose(
            native.transform_compose(*actual), matrix, atol=3e-12, rtol=3e-12
        )


@pytest.mark.parametrize("ndim", [2, 3, 5, 8])
def test_native_inverse_with_strides_and_singular_inputs(ndim):
    rng = np.random.default_rng(ndim)
    values = rng.normal(size=(ndim, ndim)) + np.eye(ndim)
    values = values[::-1, ::-1]
    result = native.transform_inverse(values)
    np.testing.assert_allclose(result @ values, np.eye(ndim), atol=2e-12)
    assert result.flags.owndata is False  # owned by the C++ capsule
    with pytest.raises(np.linalg.LinAlgError, match="Singular"):
        native.transform_inverse(np.zeros((ndim, ndim)))


@pytest.mark.parametrize("ndisplay", [-5, -1, 0, 1, 2, 3, 5])
def test_slice_axis_and_resize_semantics(reference, ndisplay):
    plan = _SliceInput(ndisplay, _ThickNDSlice.make_full(ndim=4), (2, 0, 3, 1))
    old = reference["input"]._SliceInput(plan.ndisplay, plan.world_slice, plan.order)
    assert plan.displayed == old.displayed
    assert plan.not_displayed == old.not_displayed
    for ndim in [0, 2, 4, 7]:
        new, expected = plan.with_ndim(ndim), old.with_ndim(ndim)
        assert new.order == expected.order
        assert new.world_slice == expected.world_slice
    for clone in [
        copy.copy(plan),
        copy.deepcopy(plan),
        pickle.loads(pickle.dumps(plan)),
    ]:
        assert clone.order == plan.order
        assert clone.displayed == plan.displayed


@pytest.mark.parametrize("large_translate", [False, True])
@pytest.mark.parametrize("tiny_off_diagonal", [False, True])
def test_slice_coordinates_match_reference(
    reference, large_translate, tiny_off_diagonal
):
    linear = np.eye(4)
    linear[0, 2] = 1e-9 if tiny_off_diagonal else 0.2
    linear[1, 3] = 1e-9 if tiny_off_diagonal else 0.3
    translate = np.ones(4) * (1e20 if large_translate else 0.123)
    transform = Affine(linear_matrix=linear, translate=translate, ndim=4)
    world = _ThickNDSlice.make_full(
        point=(1e10, 2e10, 3e10, 4e10),
        margin_left=(0.4, 0.5, 0.6, 0.7),
        margin_right=(1.4, 1.5, 1.6, 1.7),
    )
    plan = _SliceInput(2, world, (0, 2, 1, 3))
    old = reference["input"]._SliceInput(plan.ndisplay, world, plan.order)
    assert plan.is_orthogonal(transform) == old.is_orthogonal(transform)
    with (
        pytest.warns(UserWarning)
        if not plan.is_orthogonal(transform)
        else _no_warning()
    ):
        result = plan.data_slice(transform)
    with (
        pytest.warns(UserWarning) if not old.is_orthogonal(transform) else _no_warning()
    ):
        expected = old.data_slice(transform)
    np.testing.assert_allclose(result.as_array(), expected.as_array(), atol=1e-12)


def test_slice_allows_shorter_world_coordinates(reference):
    world = _ThickNDSlice.make_full(point=(1.0, 2.0))
    plan = _SliceInput(2, world, (0, 1, 2, 3))
    old = reference["input"]._SliceInput(plan.ndisplay, world, plan.order)
    transform = Affine(ndim=4)
    np.testing.assert_equal(
        plan.data_slice(transform).as_array(), old.data_slice(transform).as_array()
    )


@pytest.mark.parametrize("dtype", [np.float32, np.float64, np.complex128, object])
@pytest.mark.parametrize("kind", ["Affine", "ScaleTranslate"])
def test_coordinate_calls_match_original_implementation(reference, dtype, kind):
    from napari.utils.transforms import ScaleTranslate

    old_type = getattr(reference["affine"], kind)
    new_type = Affine if kind == "Affine" else ScaleTranslate
    scale, translate = np.array([2, 3], dtype=dtype), np.array([1, -1], dtype=dtype)
    new, old = (
        new_type(scale=scale, translate=translate),
        old_type(scale=scale, translate=translate),
    )
    coords = np.arange(12).reshape(6, 2).astype(dtype)[::-1]
    if dtype is np.complex128:
        coords += 2j
    expected = old(coords)
    actual = new(coords)
    np.testing.assert_equal(actual, expected)
    assert actual.dtype == expected.dtype


@pytest.mark.parametrize("mode", list(PointsProjectionMode))
@pytest.mark.parametrize("dtype", [np.float32, np.float64])
def test_point_requests_match_original_implementation(reference, mode, dtype):
    from napari.layers.points._slice import _PointSliceRequest

    rng = np.random.default_rng(4)
    data = rng.normal(size=(100, 4)).astype(dtype)[::-1]
    size = rng.uniform(1, 5, len(data)).astype(dtype)
    shown = rng.random(len(data)) > 0.3
    thick = _ThickNDSlice.make_full(
        point=(0.1,) * 4, margin_left=(0.8,) * 4, margin_right=(1.5,) * 4
    )
    plan = _SliceInput(2, thick, (3, 1, 0, 2))
    new = _PointSliceRequest(plan, data, thick, mode, size, shown)
    old = reference["points"]._PointSliceRequest(plan, data, thick, mode, size, shown)
    actual, expected = new(), old()
    np.testing.assert_equal(actual.indices, expected.indices)
    np.testing.assert_allclose(actual.size, expected.size, atol=2e-6)
    assert actual.size.dtype == expected.size.dtype


@pytest.mark.parametrize("colors", [False, True])
def test_surface_filtering_and_face_reindexing_match_reference(reference, colors):
    from napari.layers.surface._slice import _SurfaceSliceRequest
    from napari.layers.surface._surface_constants import SurfaceProjectionMode

    vertices = np.array(
        [[0, 1, 2], [0, 2, 3], [0, 3, 4], [1, 2, 3], [0, 4, 5]], dtype=np.float32
    )
    faces = np.array([[0, 1, 2], [0, 1, 3], [2, 4, 1], [-1, 1, 2]], dtype=np.int32)
    data = (vertices, faces, np.arange(5))
    thick = _ThickNDSlice.make_full(point=(0.0, np.nan, np.nan))
    plan = _SliceInput(2, thick, (0, 2, 1))
    vertex_colors = np.arange(20).reshape(5, 4) if colors else None
    texcoords = np.arange(10).reshape(5, 2)
    new = _SurfaceSliceRequest(
        plan, data, vertex_colors, texcoords, thick, SurfaceProjectionMode.NONE
    )
    old = reference["surface"]._SurfaceSliceRequest(
        plan, data, vertex_colors, texcoords, thick, SurfaceProjectionMode.NONE
    )
    actual, expected = new(), old()
    for field in ["vertices", "faces", "values", "vertex_colors", "texcoords"]:
        np.testing.assert_equal(getattr(actual, field), getattr(expected, field))
    assert actual.vertices.dtype == vertices.dtype


def _no_warning():
    from contextlib import nullcontext

    return nullcontext()


@pytest.mark.parametrize("point", [(-2.5, -1.5, 0.5, 1.5, np.nan), (0, 3.49, 3.5, 4.5)])
def test_point_index_rounding_matches_reference(reference, point):
    assert _ScalarFieldSliceRequest._point_to_slices(point) == reference[
        "scalar"
    ]._ScalarFieldSliceRequest._point_to_slices(point)


@pytest.mark.parametrize("point", [-0.5, 0, 1.49999, 1.5, 2.5, 8.0])
@pytest.mark.parametrize("margin", [0.0, 0.49, 0.5, 1.0, 5.0])
def test_thick_index_rounding_matches_reference(reference, point, margin):
    thick = _ThickNDSlice.make_full(
        point=(point, np.nan),
        margin_left=(margin, np.nan),
        margin_right=(margin + 0.001, np.nan),
    )
    expected = reference["scalar"]._ScalarFieldSliceRequest._data_slice_to_slices(
        thick, [1]
    )
    assert _ScalarFieldSliceRequest._data_slice_to_slices(thick, [1]) == expected


@pytest.mark.parametrize("multiscale", [False, True])
@pytest.mark.parametrize("rgb", [False, True])
@pytest.mark.parametrize("mode", ["none", "max", "min", "sum", "mean"])
def test_full_image_slice_matches_reference(reference, multiscale, rgb, mode):
    shape = (6, 8, 10, 3) if rgb else (6, 8, 10)
    values = np.arange(np.prod(shape), dtype=np.uint16).reshape(shape)
    data = [values, values[::2, ::2, ::2]] if multiscale else values
    image = Image(data, rgb=rgb, multiscale=multiscale, projection_mode=mode)
    dims = Dims(
        ndim=3,
        order=(0, 2, 1),
        point=(2, 0, 0),
        margin_left=(1, 0, 0),
        margin_right=(1, 0, 0),
    )
    request = image._slicing_state._make_slice_request(dims)
    old_type = type(
        "ReferenceImageRequest",
        (reference["scalar"]._ScalarFieldSliceRequest,),
        {
            "_project_slice": staticmethod(
                reference["image"]._ImageSliceRequest._project_slice
            )
        },
    )
    old = old_type(
        **{f.name: getattr(request, f.name) for f in dataclasses.fields(request)}
    )
    result, expected = request(), old()
    np.testing.assert_array_equal(result.image.raw, expected.image.raw)
    np.testing.assert_array_equal(result.thumbnail.raw, expected.thumbnail.raw)
    np.testing.assert_array_equal(
        result.tile_to_data.affine_matrix, expected.tile_to_data.affine_matrix
    )
    assert result.image.raw.dtype == expected.image.raw.dtype
    assert result.empty == expected.empty
    assert (result.image is result.thumbnail) == (expected.image is expected.thumbnail)


def test_dask_only_materializes_requested_chunks():
    reads = []

    def record(array, block_info=None):
        if block_info:
            reads.append(block_info[None]["chunk-location"][0])
        return array

    data = da.from_array(np.arange(4 * 8 * 8).reshape(4, 8, 8), chunks=(1, 8, 8))
    data = data.map_blocks(record, dtype=data.dtype)
    image = Image(data, contrast_limits=(0, 255))
    reads.clear()
    request = image._slicing_state._make_slice_request(Dims(ndim=3, point=(2, 0, 0)))
    result = request()
    assert reads == [2]
    np.testing.assert_array_equal(result.image.raw, np.asarray(data.compute())[2])


@pytest.mark.parametrize("suppress", [False, True])
def test_image_slice_exits_context_after_lazy_io_error(suppress):
    image = Image(np.zeros((3, 4, 5)))
    request = image._slicing_state._make_slice_request(Dims(ndim=3))
    failure = OSError("lazy slice could not be read")
    calls = []

    class BrokenData:
        shape = (3, 4, 5)

        def __getitem__(self, key):
            raise failure

    class Context:
        def __enter__(self):
            calls.append("enter")

        def __exit__(self, error_type, error, traceback):
            assert error_type is OSError
            assert error is failure
            assert traceback is not None
            calls.append("exit")
            return suppress

    request = dataclasses.replace(
        request, data_at_data_level=BrokenData(), dask_indexer=Context
    )
    if suppress:
        assert request() is None
    else:
        with pytest.raises(OSError, match="lazy slice could not be read"):
            request()
    assert calls == ["enter", "exit"]


@pytest.mark.parametrize("asynchronous", [False, True])
def test_slicer_submits_every_layer_from_a_one_shot_iterator(asynchronous):
    from napari.components._layer_slicer import _LayerSlicer

    dims = Dims(ndim=3, point=(2, 0, 0))
    layers = [Image(np.arange(60).reshape(3, 4, 5) + offset) for offset in (0, 100)]
    slicer = _LayerSlicer()
    slicer._force_sync = not asynchronous
    try:
        future = slicer.submit(layers=(layer for layer in layers), dims=dims)
        if asynchronous:
            responses = future.result(timeout=5)
            assert {ref() for ref in responses} == set(layers)
            for ref, response in responses.items():
                np.testing.assert_array_equal(response.image.raw, ref().data[2])
            slicer.wait_until_idle(timeout=5)
        else:
            assert future is None
            for layer in layers:
                np.testing.assert_array_equal(layer._slice.image.raw, layer.data[2])
    finally:
        slicer.shutdown()


@pytest.mark.parametrize("mode", list(VectorsProjectionMode))
@pytest.mark.parametrize("ndisplay", [2, 3, 5])
def test_vectors_match_reference(reference, mode, ndisplay):
    rng = np.random.default_rng(0)
    data = rng.normal(size=(100, 2, 5))[::2, :, ::-1]
    thick = _ThickNDSlice.make_full(
        point=(0,) * 5, margin_left=(0.8,) * 5, margin_right=(1.2,) * 5
    )
    plan = _SliceInput(ndisplay, thick, (3, 0, 2, 4, 1))
    for values in [data, data[:0]]:
        request = _VectorSliceRequest(plan, values, thick, mode, 2)
        old = reference["vectors"]._VectorSliceRequest(plan, values, thick, mode, 2)
        result, expected = request(), old()
        np.testing.assert_array_equal(result.indices, expected.indices)
        np.testing.assert_allclose(result.alphas, expected.alphas)


def test_request_loading_events_ignore_stale_completions():
    layer = Points(np.zeros((1, 3)), projection_mode=PointsProjectionMode.NONE)
    state = layer._slicing_state
    events = []
    state.loaded_data.connect(lambda: events.append(layer.loaded))
    state._set_unloaded_slice_id(41)
    state._set_unloaded_slice_id(42)
    state._update_loaded_slice_id(41)
    assert not layer.loaded
    state._update_loaded_slice_id(42)
    state._update_loaded_slice_id(42)
    assert events == [False, True]
    for clone in [
        copy.copy(state._load_state),
        copy.deepcopy(state._load_state),
        pickle.loads(pickle.dumps(state._load_state)),
    ]:
        clone.begin(43)
        assert clone.loaded is False
        assert state._load_state.loaded is True


@pytest.mark.parametrize(
    "name",
    [
        "layer [7]",
        "[42]",
        "layer []",
        "layer [٣٢]",
        "layer\u2003[१२]",
        "layer [" + "9" * 100 + "]",
        "layer[12]",
        "名字 🧪",
    ],
)
def test_unicode_and_large_name_counters(reference, name):
    assert naming.inc_name_count(name) == reference["naming"].inc_name_count(name)


def test_repeated_unit_selection_does_not_keep_temporary_objects():
    def exercise():
        layers = LayerList(
            [
                Image(np.zeros((2, 2)), units=("mm", "mm")),
                Image(np.zeros((2, 2)), units=("um", "um")),
            ]
        )
        for _ in range(100):
            assert (
                str(layers._get_units([layer.extent for layer in layers])[0])
                == "micrometer"
            )
        return [weakref.ref(layer._slicing_state._load_state) for layer in layers]

    refs = exercise()
    gc.collect()
    assert all(ref() is None for ref in refs)


def test_data_classification_preserves_none_behavior():
    from napari.layers.image._image_utils import guess_labels, guess_multiscale

    assert guess_labels(None) == "image"
    with pytest.raises(TypeError, match="not iterable"):
        guess_multiscale(None)
