"""guess_rgb, guess_multiscale, guess_labels."""

from __future__ import annotations

from naparixx import native as _native

import itertools
from typing import TYPE_CHECKING, Any, Literal

import numpy as np

from napari.layers._data_protocols import LayerDataProtocol
from napari.layers._multiscale_data import MultiScaleData

if TYPE_CHECKING:
    from collections.abc import Sequence


def guess_rgb(shape: tuple[int, ...], min_side_len: int = 30) -> bool:
    """Guess if the passed shape comes from rgb data.

    If last dim is 3 or 4 and other dims are larger (>30), assume the data is
    rgb, including rgba.

    Parameters
    ----------
    shape : list of int
        Shape of the data that should be checked.

    Returns
    -------
    bool
        If data is rgb or not.
    """
    return _native.guess_rgb(shape, min_side_len)


def guess_multiscale(
    data: MultiScaleData | list | tuple,
) -> tuple[bool, LayerDataProtocol | Sequence[LayerDataProtocol]]:
    """Guess whether the passed data is multiscale, process it accordingly.

    If shape of arrays along first axis is strictly decreasing, the data is
    multiscale. If it is the same shape everywhere, it is not. Various
    ambiguous conditions in between will result in a ValueError being raised,
    or in an "unwrapping" of data, if data contains only one element.

    Parameters
    ----------
    data : array or list of array
        Data that should be checked.

    Returns
    -------
    multiscale : bool
        True if the data is thought to be multiscale, False otherwise.
    data : list or array
        The input data, perhaps with the leading axis removed.
    """
    # If the data has ndim and is not one-dimensional then cannot be multiscale
    # If data is a zarr array, this check ensure that subsets of it are not
    # instantiated. (`for d in data` instantiates `d` as a NumPy array if
    # `data` is a zarr array.)
    return _native.guess_multiscale(data)


def guess_labels(data: Any) -> Literal['labels', 'image']:
    """Guess if array contains labels data."""

    return _native.guess_labels(data)
