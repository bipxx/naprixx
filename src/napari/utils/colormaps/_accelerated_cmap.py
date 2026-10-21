"""
Colormap utility functions to be sped-up by numba JIT.

These should stay in a separate module because they need to be reloaded during
testing, which can break instance/class relationships when done dynamically.
See https://github.com/napari/napari/pull/7025#issuecomment-2186190719.
"""

from importlib.metadata import version
from typing import TYPE_CHECKING

import numpy as np
from packaging.version import parse

from napari.utils.colormap_backend import ColormapBackend
from naparixx import _native

try:
    import numba
except ImportError:
    numba = None


try:
    from PartSegCore_compiled_backend import (
        napari_mapping as partsegcore_mapping,
    )
except ImportError:
    partsegcore_mapping = None

_native.colormap_initial_prange(globals())

if TYPE_CHECKING:
    from numba import typed

    from napari.utils.colormaps import DirectLabelColormap


_native.colormap_initial_backend(globals())


__all__ = (
    'labels_raw_to_texture_direct',
    'minimum_dtype_for_labels',
    'zero_preserving_modulo',
    'zero_preserving_modulo_numpy',
)

MAPPING_OF_UNKNOWN_VALUE = 0
# For direct mode we map all unknown values to a single value
# for simplicity of implementation we select 0


def minimum_dtype_for_labels(num_colors: int) -> np.dtype:
    """Return the minimum texture dtype that can hold given number of colors.

    Parameters
    ----------
    num_colors : int
        Number of unique colors in the data.

    Returns
    -------
    np.dtype
        Minimum dtype that can hold the number of colors.
    """
    return _native.colormap_minimum_dtype(num_colors, globals())


def zero_preserving_modulo_numpy(
    values: np.ndarray, n: int, dtype: np.dtype, to_zero: int = 0
) -> np.ndarray:
    """``(values - 1) % n + 1``, but with one specific value mapped to 0.

    This ensures (1) an output value in [0, n] (inclusive), and (2) that
    no nonzero values in the input are zero in the output, other than the
    ``to_zero`` value.

    Parameters
    ----------
    values : np.ndarray
        The dividend of the modulo operator.
    n : int
        The divisor.
    dtype : np.dtype
        The desired dtype for the output array.
    to_zero : int, optional
        A specific value to map to 0. (By default, 0 itself.)

    Returns
    -------
    np.ndarray
        The result: 0 for the ``to_zero`` value, ``values % n + 1``
        everywhere else.
    """
    res = _native.colormap_modulo_numpy_result(values, n, dtype, globals())
    return _native.colormap_modulo_zero_result(values, res, to_zero)


def _zero_preserving_modulo_loop(
    values: np.ndarray, n: int, dtype: np.dtype, to_zero: int = 0
) -> np.ndarray:
    """``(values - 1) % n + 1``, but with one specific value mapped to 0.

    This ensures (1) an output value in [0, n] (inclusive), and (2) that
    no nonzero values in the input are zero in the output, other than the
    ``to_zero`` value.

    Parameters
    ----------
    values : np.ndarray
        The dividend of the modulo operator.
    n : int
        The divisor.
    dtype : np.dtype
        The desired dtype for the output array.
    to_zero : int, optional
        A specific value to map to 0. (By default, 0 itself.)

    Returns
    -------
    np.ndarray
        The result: 0 for the ``to_zero`` value, ``values % n + 1``
        everywhere else.
    """
    result = _native.colormap_modulo_loop_result(values, dtype, globals())
    return _native.colormap_modulo_loop_apply(
        values, n, to_zero, result, globals()
    )


def _zero_preserving_modulo_inner_loop(
    values: np.ndarray, n: int, to_zero: int, out: np.ndarray
) -> np.ndarray:
    """``(values - 1) % n + 1``, but with one specific value mapped to 0.

    This ensures (1) an output value in [0, n] (inclusive), and (2) that
    no nonzero values in the input are zero in the output, other than the
    ``to_zero`` value.

    Parameters
    ----------
    values : np.ndarray
        The dividend of the modulo operator.
    n : int
        The divisor.
    to_zero : int
        A specific value to map to 0. (Usually, 0 itself.)
    out : np.ndarray
        Preallocated output array

    Returns
    -------
    np.ndarray
        The result: 0 for the ``to_zero`` value, ``values % n + 1``
        everywhere else.
    """
    locals_owner = _native.colormap_inner_loop_locals()
    return _native.colormap_zero_preserving_modulo_inner(
        values, n, to_zero, out, globals(), locals_owner
    )


if _native.colormap_numpy_clip_version(globals()):

    def clip(data: np.ndarray, min_val: int, max_val: int) -> np.ndarray:
        """
        Clip data to the given range.
        """
        dtype_info = _native.colormap_clip_info(data, globals())
        min_val = _native.colormap_clip_min(min_val, dtype_info, globals())
        max_val = _native.colormap_clip_max(max_val, dtype_info, globals())
        return _native.colormap_clip(data, min_val, max_val, globals())
else:

    def clip(data: np.ndarray, min_val: int, max_val: int) -> np.ndarray:
        return _native.colormap_clip(data, min_val, max_val, globals())


def _labels_raw_to_texture_direct_numpy(
    data: np.ndarray, direct_colormap: 'DirectLabelColormap'
) -> np.ndarray:
    """Convert labels data to the data type used in the texture.

    This implementation uses numpy vectorized operations.

    See `_cast_labels_data_to_texture_dtype_direct` for more details.
    """
    if _native.colormap_direct_selection(direct_colormap):
        return _native.colormap_direct_selected(data, direct_colormap, globals())
    mapper = _native.colormap_direct_mapper(direct_colormap)
    if _native.colormap_direct_has_negative(direct_colormap, globals()):
        half_shape = _native.colormap_direct_half_shape(mapper)
        data = _native.colormap_direct_clip_negative(
            data, half_shape, globals()
        )
    else:
        data = _native.colormap_direct_clip_positive(data, mapper, globals())
    return _native.colormap_direct_index(mapper, data)


def _labels_raw_to_texture_direct_loop(
    data: np.ndarray, direct_colormap: 'DirectLabelColormap'
) -> np.ndarray:
    """
    Cast direct labels to the minimum type.

    Parameters
    ----------
    data : np.ndarray
        The input data array.
    direct_colormap : DirectLabelColormap
        The direct colormap.

    Returns
    -------
    np.ndarray
        The cast data array.
    """
    if _native.colormap_direct_selection(direct_colormap):
        return _native.colormap_direct_selected(data, direct_colormap, globals())
    dkt = _native.colormap_direct_typed_mapping(data, direct_colormap)
    target_dtype = _native.colormap_direct_target_dtype(
        direct_colormap, globals()
    )
    result_array = _native.colormap_direct_result_array(
        data, target_dtype, globals()
    )
    return _native.colormap_direct_loop_apply(
        data, dkt, result_array, globals()
    )


def _labels_raw_to_texture_direct_inner_loop(
    data: np.ndarray, dkt: 'typed.Dict', out: np.ndarray
) -> np.ndarray:
    """
    Relabel data using typed dict with mapping unknown labels to default value
    """
    # The numba typed dict does not provide official Api for
    # determine key and value types
    locals_owner = _native.colormap_inner_loop_locals()
    return _native.colormap_labels_raw_to_texture_direct_inner(
        data, dkt, out, globals(), locals_owner
    )


def zero_preserving_modulo_partsegcore(
    values: np.ndarray, n: int, dtype: np.dtype, to_zero: int = 0
) -> np.ndarray:
    out = _native.colormap_partseg_modulo_out(values, dtype, globals())
    return _native.colormap_partseg_modulo_apply(
        values, n, to_zero, out, globals()
    )


def labels_raw_to_texture_direct_partsegcore(
    data: np.ndarray, direct_colormap: 'DirectLabelColormap'
) -> np.ndarray:
    if _native.colormap_direct_selection(direct_colormap):
        dkt = _native.colormap_partseg_selection_dict(direct_colormap)
    else:
        iinfo = _native.colormap_clip_info(data, globals())
        dkt = _native.colormap_partseg_mapping_dict(direct_colormap, iinfo)
    target_dtype = _native.colormap_direct_target_dtype(
        direct_colormap, globals()
    )
    out = _native.colormap_partseg_direct_out(data, target_dtype, globals())
    return _native.colormap_partseg_direct_apply(data, dkt, out, globals())


_native.colormap_initial_aliases(globals())

_native.colormap_compile_inner_loops(globals())


def set_colormap_backend(backend: ColormapBackend) -> None:
    """Set the colormap backend to use.

    Parameters
    ----------
    backend : ColormapBackend
        The colormap backend to use.
    """
    global \
        COLORMAP_BACKEND, \
        labels_raw_to_texture_direct, \
        zero_preserving_modulo, \
        prange
    return _native.colormap_set_backend(backend, globals())


_native.colormap_initial_select_backend(globals())
