from __future__ import annotations

from collections.abc import Sequence

import numpy as np

from napari.layers._data_protocols import LayerDataProtocol, assert_protocol
from naparixx import native as _native


# note: this also implements `LayerDataProtocol`, but we don't need to inherit.
class MultiScaleData(
    _native.MultiScaleNamespaceBase, Sequence[LayerDataProtocol]
):
    """Wrapper for multiscale data, to provide consistent API.

    :class:`LayerDataProtocol` is the subset of the python Array API that we
    expect array-likes to provide.  Multiscale data is just a sequence of
    array-likes (providing, e.g. `shape`, `dtype`, `__getitem__`).

    Parameters
    ----------
    data : Sequence[LayerDataProtocol]
        Levels of multiscale data, from larger to smaller.
    max_size : Sequence[int], optional
        Maximum size of a displayed tile in pixels, by default`data[-1].shape`

    Raises
    ------
    ValueError
        If `data` is empty or is not a list, tuple, or ndarray.
    TypeError
        If any of the items in `data` don't provide `LayerDataProtocol`.
    """

    def __init__(
        self,
        data: Sequence[LayerDataProtocol],
    ) -> None:
        self._data: list[LayerDataProtocol]
        locals_owner = _native.multiscale_init_locals()
        return _native.multiscale_init(self, data, globals(), locals_owner)

    @property
    def size(self) -> int:
        """Return size of the first scale.."""
        return _native.multiscale_size(self)

    @property
    def ndim(self) -> int:
        """Return ndim of the first scale.."""
        return _native.multiscale_ndim(self)

    @property
    def dtype(self) -> np.dtype:
        """Return dtype of the first scale.."""
        return _native.multiscale_dtype(self)

    @property
    def shape(self) -> tuple[int, ...]:
        """Shape of multiscale is just the biggest shape."""
        return _native.multiscale_shape(self)

    @property
    def shapes(self) -> tuple[tuple[int, ...], ...]:
        """Tuple shapes for all scales."""
        def _shape_generator(native_iterator):
            yield from native_iterator

        return _native.multiscale_shapes(self, globals(), _shape_generator)

    def __getitem__(self, key: int | tuple[slice, ...]) -> LayerDataProtocol:
        """Multiscale indexing."""
        return _native.multiscale_getitem(self, key)

    def __len__(self) -> int:
        return _native.multiscale_len(self, globals())

    def __eq__(self, other) -> bool:
        return _native.multiscale_eq(self, other)

    def __add__(self, other) -> bool:
        return _native.multiscale_add(self, other)

    def __mul__(self, other) -> bool:
        return _native.multiscale_mul(self, other)

    def __rmul__(self, other) -> bool:
        return _native.multiscale_rmul(self, other)

    def __array__(self) -> np.ndarray:
        return _native.multiscale_array(self, globals())

    def __repr__(self) -> str:
        return _native.multiscale_repr(self, globals())
