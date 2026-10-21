import numpy as np
import pytest

from napari.layers.labels._labels_utils import (
    first_nonzero_coordinate,
    interpolate_coordinates,
)
from napari.layers.utils.layer_utils import segment_normal
from napari.utils.geometry import project_points_onto_plane
from naparixx import _adapters, native


@pytest.mark.parametrize("end", [(0.0, 10.0), (0.0, 1.375), (0.0, 1.625), (0.0, 1.0)])
def test_brush_interpolation_and_rounding(end):
    old, new = np.array([0.0, 1.0]), np.asarray(end)
    actual = native.interpolate_coordinates(old, new, 3)
    np.testing.assert_allclose(actual, interpolate_coordinates(old, new, 3), atol=1e-13)


def test_interpolation_accepts_missing_endpoint():
    coord = np.array([3.0, 7.0])
    call = _adapters.interpolation_adapter(interpolate_coordinates)
    np.testing.assert_array_equal(call(None, coord, 2), coord[None])
    np.testing.assert_array_equal(call(coord, None, 2), coord[None])
    with pytest.raises(ValueError):
        native.interpolate_coordinates(coord, coord, 0)


@pytest.mark.parametrize(
    "dtype", [np.bool_, np.uint8, np.int32, np.int64, np.uint64, np.float32, np.float64]
)
@pytest.mark.parametrize("layout", ["normal", "reversed", "fortran", "readonly"])
def test_native_ray_picking_matches_upstream(dtype, layout):
    data = np.zeros((11, 11, 11), dtype=dtype)
    data[4:7, 4:7, 4:7] = 1
    if layout == "reversed":
        data = data[::-1, ::-1, ::-1]
    elif layout == "fortran":
        data = np.asfortranarray(data)
    elif layout == "readonly":
        data.flags.writeable = False
    for start, end in (
        (np.zeros(3), np.full(3, 10.0)),
        (np.full(3, 10.0), np.zeros(3)),
        (np.zeros(3), np.array([0.0, 1.0, 1.0])),
        (np.array([-8.0, 6.0, 6.0]), np.array([20.0, 5.0, 5.0])),
        (np.array([5.0, 5.0, 5.0]), np.array([5.0, 5.0, 5.0])),
    ):
        expected = first_nonzero_coordinate(data, start, end)
        actual = native.first_nonzero_coordinate(data, start, end)
        if expected is None:
            assert actual is None
        else:
            np.testing.assert_array_equal(actual, expected)


@pytest.mark.parametrize("dims", [2, 3])
def test_segment_normals_with_degenerate_segments(dims):
    rng = np.random.default_rng(4)
    a, b = rng.normal(size=(15, dims)), rng.normal(size=(15, dims))
    b[3] = a[3]
    p = np.array([0.0, 0.0, 1.0])
    actual = native.segment_normal(a[::-1], b[::-1], p)
    np.testing.assert_allclose(actual, segment_normal(a[::-1], b[::-1], p), atol=1e-13)
    np.testing.assert_array_equal(actual[-4], np.zeros(dims))
    np.testing.assert_allclose(
        native.segment_normal(a[0], b[0], p), segment_normal(a[0], b[0], p)
    )


def test_plane_projection_shapes_and_distances():
    rng = np.random.default_rng(5)
    point = np.array([0.3, -4.0, 2.0])
    normal = np.array([1.0, 2.0, 3.0]) / np.sqrt(14)
    for points in (
        rng.normal(size=(32, 3))[::-1, ::-1],
        np.array([1.0, 2.0, 3.0]),
        np.empty((0, 3)),
    ):
        actual = native.project_points_onto_plane(points, point, normal)
        expected = project_points_onto_plane(points, point, normal)
        for result, reference in zip(actual, expected, strict=True):
            np.testing.assert_allclose(result, reference, atol=1e-13)
            assert result.shape == reference.shape
