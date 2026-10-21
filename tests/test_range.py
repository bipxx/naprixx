import numpy as np
import pytest
from dask import array as da

from napari.layers.utils.layer_utils import calc_data_range
from naparixx import _adapters, native

DTYPES = [
    np.bool_,
    np.int8,
    np.uint8,
    np.int16,
    np.uint16,
    np.int32,
    np.uint32,
    np.int64,
    np.uint64,
    np.float32,
    np.float64,
]


@pytest.mark.parametrize("dtype", DTYPES)
@pytest.mark.parametrize("layout", ["normal", "reversed", "fortran", "broadcast"])
def test_range_matches_upstream(dtype, layout):
    data = np.arange(63).reshape(3, 7, 3).astype(dtype)
    if layout == "reversed":
        data = data[::-1, ::-2, ::-1]
    elif layout == "fortran":
        data = np.asfortranarray(data)
    elif layout == "broadcast":
        data = np.broadcast_to(data[0], (5, 7, 3))
    data.flags.writeable = False
    expected = calc_data_range(data)
    assert native.calc_data_range(data) == expected
    assert _adapters.range_adapter(calc_data_range)(data) == expected


@pytest.mark.parametrize(
    "values",
    [
        [np.nan, np.inf, -np.inf],
        [1, np.nan, 8, np.inf],
        [0, 0],
        [5, 5],
        [-5, -5],
        [np.nan, 5, -np.inf],
    ],
)
def test_range_nonfinite_and_constant(values):
    data = np.asarray(values, dtype=np.float64)
    assert native.calc_data_range(data) == calc_data_range(data)


def test_large_integer_extrema_do_not_become_constant_before_float_conversion():
    for data in (
        np.array([2**63 + 1, 2**63 + 2], dtype=np.uint64),
        np.array([-(2**63), -(2**63) + 1], dtype=np.int64),
    ):
        assert native.calc_data_range(data) == calc_data_range(data)


def test_range_empty_and_scalar():
    with pytest.raises(ValueError):
        native.calc_data_range(np.empty((0, 3)))
    assert native.calc_data_range(np.empty(0, dtype=np.uint8)) == (0, 255)
    assert native.calc_data_range(np.array(3.0)) == calc_data_range(np.array(3.0))


def test_range_preserves_dtype_override_and_lazy_arrays():
    call = _adapters.range_adapter(calc_data_range)
    data = da.from_array(np.arange(24.0).reshape(4, 6), chunks=(2, 3))
    assert call(data) == calc_data_range(data)
    assert call(data, dtype=np.dtype("uint8")) == (0, 255)
    for data in (np.array([1, 2], dtype=">f8"), np.array([1, 2], dtype=np.float16)):
        assert call(data) == calc_data_range(data)


def test_large_volume_keeps_upstream_sampling():
    data = np.zeros((11, 1000, 1000), dtype=np.int16)
    data[3, 10, 10] = 900
    # This voxel is absent from napari's three sampled planes.
    assert _adapters.range_adapter(calc_data_range)(data) == calc_data_range(data)
    assert native.calc_data_range(data) != calc_data_range(data)


@pytest.mark.parametrize("count", [13, 67])
def test_unaligned_input_is_read_safely(count):
    data = np.ndarray(
        (count,), dtype=np.float64, buffer=bytearray(count * 8 + 1), offset=1
    )
    data[:] = np.arange(count)
    assert not data.flags.aligned
    assert native.calc_data_range(data) == calc_data_range(data)


@pytest.mark.parametrize("dtype", [np.float32, np.float64])
@pytest.mark.parametrize("position", [0, 1, 16, 31, 62, 66])
@pytest.mark.parametrize("value", [np.nan, np.inf, -np.inf])
def test_nonfinite_values_across_simd_lanes(dtype, position, value):
    data = np.linspace(-8, 9, 67, dtype=dtype)
    data[position] = value
    assert native.calc_data_range(data) == calc_data_range(data)
