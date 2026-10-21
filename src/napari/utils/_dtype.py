from typing import TYPE_CHECKING, Final

import numpy as np

from naparixx import _native

if TYPE_CHECKING:
    from numpy.typing import DTypeLike

_np_uints: Final[dict[int, np.dtype]] = {
    8: np.dtype(np.uint8),
    16: np.dtype(np.uint16),
    32: np.dtype(np.uint32),
    64: np.dtype(np.uint64),
}

_np_ints: Final[dict[int, np.dtype]] = {
    8: np.dtype(np.int8),
    16: np.dtype(np.int16),
    32: np.dtype(np.int32),
    64: np.dtype(np.int64),
}

_np_floats: Final[dict[int, np.dtype]] = {
    16: np.dtype(np.float16),
    32: np.dtype(np.float32),
    64: np.dtype(np.float64),
}

_np_complex: Final[dict[int, np.dtype]] = {
    64: np.dtype(np.complex64),
    128: np.dtype(np.complex128),
}

_np_kinds: Final[dict[str, dict[int, np.dtype]]] = {
    'uint': _np_uints,
    'int': _np_ints,
    'float': _np_floats,
    'complex': _np_complex,
}


def _normalize_str_by_bit_depth(dtype_str: str, kind: str) -> np.dtype:
    return _native.dtype_normalize_str_by_bit_depth(
        dtype_str, kind, globals()
    )


def normalize_dtype(dtype_spec: 'DTypeLike') -> np.dtype:
    """Return a proper NumPy type given ~any duck array dtype.

    Parameters
    ----------
    dtype_spec : numpy dtype, numpy type, torch dtype, tensorstore dtype, etc
        A type that can be interpreted as a NumPy numeric data type, e.g.
        'uint32', np.uint8, torch.float32, etc.

    Returns
    -------
    dtype : numpy.dtype
        The corresponding dtype.

    Notes
    -----
    half-precision floats are not supported.
    """
    return _native.dtype_normalize(dtype_spec, globals())


def get_dtype_limits(dtype_spec: 'DTypeLike') -> tuple[float, float]:
    """Return machine limits for numeric types.

    Parameters
    ----------
    dtype_spec : numpy dtype, numpy type, torch dtype, tensorstore dtype, etc
        A type that can be interpreted as a NumPy numeric data type, e.g.
        'uint32', np.uint8, torch.float32, etc.

    Returns
    -------
    limits : tuple
        The smallest/largest numbers expressible by the type.
    """
    return _native.dtype_limits(dtype_spec, globals())


vispy_texture_dtype = np.float32
