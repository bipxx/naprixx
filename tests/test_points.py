import numpy as np
import pytest

from napari.layers.points._points_constants import PointsProjectionMode
from napari.layers.points._points_utils import points_in_box
from napari.layers.points._slice import _PointSliceRequest
from napari.layers.utils._slice_input import _SliceInput, _ThickNDSlice
from naparixx import _adapters, native


def make_request(data, mode, sizes=None, shown=None):
    dims = data.shape[1]
    point = tuple(0.2 for _ in range(dims))
    left = tuple(0.0 if c == 1 else 1.7 for c in range(dims))
    right = tuple(0.0 if c == 1 else 2.1 for c in range(dims))
    thick = _ThickNDSlice(point, left, right)
    order = (1, 3, 0, 2, 4) if dims == 5 else tuple(range(dims))
    return _PointSliceRequest(
        slice_input=_SliceInput(ndisplay=2, world_slice=thick, order=order),
        data=data,
        data_slice=thick,
        projection_mode=mode,
        size=np.full(len(data), 4.0) if sizes is None else sizes,
        shown=np.ones(len(data), dtype=bool) if shown is None else shown,
    )


@pytest.mark.parametrize("mode", list(PointsProjectionMode))
@pytest.mark.parametrize("dtype", [np.float32, np.float64])
@pytest.mark.parametrize("layout", ["normal", "reversed", "fortran", "readonly"])
def test_native_point_slicing_matches_latest_modes(mode, dtype, layout):
    rng = np.random.default_rng(45)
    data = rng.normal(size=(400, 5)).astype(dtype)
    if layout == "reversed":
        data = data[::-1, ::-1]
    elif layout == "fortran":
        data = np.asfortranarray(data)
    elif layout == "readonly":
        data.flags.writeable = False
    request = make_request(
        data, mode, rng.uniform(0.1, 8, len(data)), rng.random(len(data)) > 0.3
    )
    axes = list(request.slice_input.not_displayed)
    expected = request._get_slice_data(axes)
    actual = _adapters.point_slice_adapter(_PointSliceRequest._get_slice_data)(
        request, axes
    )
    np.testing.assert_array_equal(actual[0], expected[0])
    np.testing.assert_allclose(actual[1], expected[1], atol=1e-12, rtol=1e-12)
    assert actual[0].dtype == expected[0].dtype
    assert actual[1].dtype == expected[1].dtype


@pytest.mark.parametrize("mode", list(PointsProjectionMode))
def test_slice_boundaries_and_hidden_points(mode):
    data = np.array([[-0.3, 0, 0], [0.7, 0, 0], [0.700001, 0, 0], [0.2, 0, 0]])
    request = make_request(data, mode, shown=np.array([True, True, False, False]))
    expected = request._get_slice_data([0])
    actual = _adapters.point_slice_adapter(_PointSliceRequest._get_slice_data)(
        request, [0]
    )
    np.testing.assert_array_equal(actual[0], expected[0])
    np.testing.assert_allclose(actual[1], expected[1], atol=1e-13)


def test_slice_none_and_empty_cases():
    for data in (np.empty((0, 4)), np.full((9, 4), 100.0)):
        request = make_request(data, PointsProjectionMode.NONE)
        result = _adapters.point_slice_adapter(_PointSliceRequest._get_slice_data)(
            request, [0, 1]
        )
        assert result[0].shape == result[1].shape == (0,)


def test_invalid_slice_axes_raise():
    data = np.ones((5, 3))
    vector = np.ones(1)
    for axes in ([4], [-1]):
        with pytest.raises(ValueError):
            native.slice_points(
                data, np.ones(5), np.ones(5, bool), axes, vector, vector, vector, 0
            )


def test_integer_slice_spec_keeps_upstream_error():
    request = make_request(np.zeros((3, 3)), PointsProjectionMode.NONE)
    object.__setattr__(request, "data_slice", _ThickNDSlice.make_full(ndim=3))
    call = _adapters.point_slice_adapter(_PointSliceRequest._get_slice_data)
    with pytest.raises(TypeError):
        call(request, [0])


def test_box_selection_preserves_upstream_corner_rule():
    corners = np.array([[0.0, 0.0], [2.0, 2.0]])
    points = np.array([[1.0, 1.0], [1.0, 1.0], [3.0, 3.0], [0.0, 0.0]])
    sizes = np.array([10.0, 0.5, 2.0, 0.0])
    actual = native.points_in_box(corners, points, sizes)
    assert actual == points_in_box(corners, points, sizes)
    assert 0 not in actual
    assert native.points_in_box(corners, np.empty((0, 2)), np.empty(0)) == []


def test_random_box_selection():
    rng = np.random.default_rng(80)
    call = _adapters.box_adapter(points_in_box)
    for _ in range(12):
        corners = rng.normal(size=(2, 2))
        points = rng.normal(size=(91, 2))[::-1]
        sizes = rng.uniform(0, 3, len(points))
        assert call(corners, points, sizes) == points_in_box(corners, points, sizes)


@pytest.mark.parametrize("mode", list(PointsProjectionMode))
def test_slice_helper_with_no_axes_still_applies_shown_mask(mode):
    data = np.zeros((3, 2))
    request = make_request(
        data,
        mode,
        sizes=np.array([-4.0, 3.0, 2.0]),
        shown=np.array([True, False, True]),
    )
    expected = request._get_slice_data([])
    actual = _adapters.point_slice_adapter(_PointSliceRequest._get_slice_data)(
        request, []
    )
    np.testing.assert_array_equal(actual[0], expected[0])
    np.testing.assert_allclose(actual[1], expected[1])
    # Request.__call__ intentionally bypasses this helper for fully displayed
    # points and returns all indices, as upstream does.
    np.testing.assert_array_equal(request().indices, [0, 1, 2])


def test_float32_box_boundaries_keep_upstream_rounding():
    point = np.array([[1.0, 1.0]], dtype=np.float32)
    sizes = np.array([np.finfo(np.float32).eps], dtype=np.float32)
    corners = np.array([[1.0, 1.0], [1.0, 1.0]], dtype=np.float32)
    call = _adapters.box_adapter(points_in_box)
    assert call(corners, point, sizes) == points_in_box(corners, point, sizes)
