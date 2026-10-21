import numpy as np
import pytest

from napari.utils.transforms import Affine, CompositeAffine, ScaleTranslate
from naparixx import _adapters, native


@pytest.mark.parametrize("kind", [Affine, CompositeAffine, ScaleTranslate])
@pytest.mark.parametrize("dims", [2, 3, 5])
@pytest.mark.parametrize("layout", ["normal", "reversed", "fortran", "readonly"])
def test_transform_parity(kind, dims, layout):
    transform = kind(scale=(1.3, -2.0), translate=(-3.0, 4.7))
    coords = np.random.default_rng(5).normal(size=(17, dims))
    if layout == "reversed":
        coords = coords[::-2, ::-1]
    elif layout == "fortran":
        coords = np.asfortranarray(coords)
    elif layout == "readonly":
        coords.flags.writeable = False
    original = kind.__call__
    factory = (
        _adapters.scale_translate_adapter
        if kind is ScaleTranslate
        else _adapters.affine_adapter
    )
    actual = factory(original)(transform, coords)
    expected = original(transform, coords)
    np.testing.assert_allclose(actual, expected, atol=1e-13, rtol=1e-13)
    assert actual.dtype == expected.dtype
    single = factory(original)(transform, coords[0])
    np.testing.assert_allclose(single, original(transform, coords[0]), atol=1e-13)
    assert single.shape == (dims,)


@pytest.mark.parametrize("dtype", [np.int32, np.int64, np.float32, np.float64])
def test_scale_translate_dtype_and_fallback(dtype):
    transform = ScaleTranslate(
        scale=np.array([2, 3], dtype=dtype),
        translate=np.array([1, -2], dtype=dtype),
    )
    coords = np.arange(12, dtype=dtype).reshape(6, 2)
    actual = _adapters.scale_translate_adapter(ScaleTranslate.__call__)(
        transform, coords
    )
    expected = transform(coords)
    np.testing.assert_array_equal(actual, expected)
    assert actual.dtype == expected.dtype


def test_affine_rotation_and_empty_coordinates():
    transform = Affine(rotate=27, shear=(0.7,), translate=(4.0, -2.0))
    call = _adapters.affine_adapter(Affine.__call__)
    for coords in (np.empty((0, 2)), np.ones((13, 4)), np.array([4.0, 6.0])):
        np.testing.assert_allclose(
            call(transform, coords), transform(coords), atol=1e-13
        )


def test_affine_nonfinite_padded_dimensions():
    transform = Affine(scale=(2.0, 3.0))
    coords = np.array([[np.nan, 1, 2], [np.inf, 3, 4]])
    with np.errstate(invalid="ignore"):
        expected = transform(coords)
        actual = _adapters.affine_adapter(Affine.__call__)(transform, coords)
    np.testing.assert_allclose(actual, expected, equal_nan=True)


def test_unsupported_coordinates_keep_upstream_behavior():
    transform = Affine()
    coords = np.array([1 + 2j, 3 + 4j])
    call = _adapters.affine_adapter(Affine.__call__)
    np.testing.assert_array_equal(call(transform, coords), transform(coords))
    with pytest.raises(ValueError):
        call(transform, np.array([1.0]))


def test_native_output_owns_memory():
    coords = np.arange(20, dtype=float).reshape(10, 2)
    output = native.affine(coords, np.eye(2), np.zeros(2))
    assert not np.shares_memory(coords, output)
    del coords
    import gc

    gc.collect()
    np.testing.assert_array_equal(output, np.arange(20).reshape(10, 2))


def test_native_validation():
    with pytest.raises(ValueError):
        native.affine(np.ones((3, 2)), np.ones((2, 3)), np.zeros(2))
    with pytest.raises(TypeError):
        native.affine(np.ones((3, 2), dtype=np.float32), np.eye(2), np.zeros(2))
