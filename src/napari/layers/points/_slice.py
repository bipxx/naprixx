from naparixx import native as _native


from naparixx._backend import dispatch as _native_dispatch
from dataclasses import dataclass, field
from typing import Any

import numpy as np
import numpy.typing as npt

from napari.layers.base._slice import _next_request_id
from napari.layers.points._points_constants import PointsProjectionMode
from napari.layers.utils._slice_input import _SliceInput, _ThickNDSlice


@dataclass(frozen=True)
class _PointSliceResponse:
    """Contains all the output data of slicing an points layer.

    Attributes
    ----------
    indices : array like
        Indices of the visible points.
    size: array like
        Sizes of the visible points, rescaled if necessary.
    slice_input : _SliceInput
        Describes the slicing plane or bounding box in the layer's dimensions.
    request_id : int
        The identifier of the request from which this was generated.
    """

    indices: npt.NDArray = field(repr=False)
    size: npt.NDArray = field(repr=False)
    slice_input: _SliceInput
    request_id: int


@dataclass(frozen=True)
class _PointSliceRequest:
    """A callable that stores all the input data needed to slice a Points layer.

    This should be treated a deeply immutable structure, even though some
    fields can be modified in place. It is like a function that has captured
    all its inputs already.

    In general, the calling an instance of this may take a long time, so you may
    want to run it off the main thread.

    Attributes
    ----------
    dims : _SliceInput
        Describes the slicing plane or bounding box in the layer's dimensions.
    data : Any
        The layer's data field, which is the main input to slicing.
    data_slice : _ThickNDSlice
        The slicing coordinates and margins in data space.
    size : array like
        Size of each point. This is used in calculating visibility.
    shown : array like
        Boolean array indicating if each point should be shown.
    others
        See the corresponding attributes in `Layer` and `Points`.
    """

    slice_input: _SliceInput
    data: Any = field(repr=False)
    data_slice: _ThickNDSlice = field(repr=False)
    projection_mode: PointsProjectionMode
    size: npt.NDArray = field(repr=False)
    shown: npt.NDArray = field(repr=False)
    id: int = field(default_factory=_next_request_id)

    def __call__(self) -> _PointSliceResponse:
        # Return early if no data
        indices, size = _native.point_slice_request(self)
        return _PointSliceResponse(
            indices=indices, size=size, slice_input=self.slice_input, request_id=self.id
        )

    @_native_dispatch("point_slice_adapter")
    def _get_slice_data(
        self, not_disp: list[int]
    ) -> tuple[npt.NDArray, npt.NDArray]:

        return _native.point_slice_data(self, not_disp)
