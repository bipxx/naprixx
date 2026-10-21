from __future__ import annotations

from naparixx import native as _native

from dataclasses import dataclass, field
from typing import TYPE_CHECKING, Any

import numpy as np
import numpy.typing as npt

from napari.layers.base._slice import _next_request_id
from napari.layers.utils._slice_input import _SliceInput, _ThickNDSlice
from napari.types import ArrayLike
from napari.utils._dask_utils import DaskIndexer
from napari.utils._dtype import normalize_dtype
from napari.utils.misc import reorder_after_dim_reduction
from napari.utils.transforms import Affine

if TYPE_CHECKING:
    from collections.abc import Callable

    from numpy.typing import DTypeLike


def _slice_dataclass_view_init_template():
    _object_capture = None

    def __init__(self, raw, view):
        return _native.slice_dataclass_view_init(
            self, raw, view, lambda: _object_capture
        )

    return __init__


def _slice_dataclass_response_init_template():
    _object_capture = None

    def __init__(
        self, image, thumbnail, tile_to_data, slice_input, request_id, empty=False
    ):
        return _native.slice_dataclass_response_init(
            self,
            image,
            thumbnail,
            tile_to_data,
            slice_input,
            request_id,
            empty,
            lambda: _object_capture,
        )

    return __init__


def _slice_dataclass_request_init_template():
    _object_capture = None
    _sentinel_capture = None
    _id_factory_capture = None

    def __init__(
        self,
        slice_input,
        data_at_data_level,
        data_at_thumbnail_level,
        dtype,
        dask_indexer,
        data_slice,
        projection_mode,
        multiscale,
        corner_pixels,
        rgb,
        data_level,
        thumbnail_level,
        level_shapes,
        downsample_factors,
        id=None,
    ):
        return _native.slice_dataclass_request_init(
            self,
            slice_input,
            data_at_data_level,
            data_at_thumbnail_level,
            dtype,
            dask_indexer,
            data_slice,
            projection_mode,
            multiscale,
            corner_pixels,
            rgb,
            data_level,
            thumbnail_level,
            level_shapes,
            downsample_factors,
            id,
            lambda: _object_capture,
            lambda: _sentinel_capture,
            lambda: _id_factory_capture,
        )

    return __init__


@dataclass(frozen=True)
class _ScalarFieldView(_native.SliceDataclassNamespaceBase):
    """A raw image and a potentially different viewable version of it.

    This is only needed for labels, and not other image layers, because sliced labels
    data are passed to vispy as floats in [0, 1] to use continuous colormaps.
    In that case, a conversion function is defined by `Labels._raw_to_displayed` to
    handle the desired colormapping behavior.

    For non-labels image layers the raw and viewable images should be the same instance
    and no conversion should be necessary.

    This is defined for images in general because `Labels` and `_ImageBase` share
    code through inheritance.

    Attributes
    ----------
    raw : array
        The raw image.
    view : array
        The viewable image, which should either be the same instance of raw, or a
        converted version of it.
    """

    raw: np.ndarray
    view: np.ndarray

    @classmethod
    def from_view(cls, view: np.ndarray) -> _ScalarFieldView:
        """Makes an image view from the view where no conversion is needed."""
        return _native.scalar_first_refresh_view(cls, view)

    @classmethod
    def from_raw(
        cls, *, raw: np.ndarray, converter: Callable[[np.ndarray], np.ndarray]
    ) -> _ScalarFieldView:
        """Makes an image view from the raw image and a conversion function."""
        return _native.scalar_view_from_raw(cls, raw, converter)


_native.slice_dataclass_install_init(
    _ScalarFieldView, _ScalarFieldView.__init__, _slice_dataclass_view_init_template()
)


@dataclass(frozen=True)
class _ScalarFieldSliceResponse(_native.SliceDataclassNamespaceBase):
    """Contains all the output data of slicing an image layer.

    Attributes
    ----------
    image : _ScalarFieldView
        The sliced image data.
    thumbnail: _ScalarFieldView
        The thumbnail image data. This may come from a different resolution to the sliced image
        data for multi-scale images. Otherwise, it's the same instance as data.
    tile_to_data: Affine
        The affine transform from the sliced data to the full data at the highest resolution.
        For single-scale images, this will be the identity matrix.
    slice_input : _SliceInput
        Describes the slicing plane or bounding box in the layer's dimensions.
    request_id : int
        The identifier of the request from which this was generated.
    """

    image: _ScalarFieldView = field(repr=False)
    thumbnail: _ScalarFieldView = field(repr=False)
    tile_to_data: Affine = field(repr=False)
    slice_input: _SliceInput
    request_id: int
    empty: bool = False

    @classmethod
    def make_empty(
        cls,
        *,
        slice_input: _SliceInput,
        rgb: bool,
        request_id: int | None = None,
        dtype: DTypeLike = np.uint8,
    ) -> _ScalarFieldSliceResponse:
        """Returns an empty image slice response.

        An empty slice indicates that there is no valid slice data for an
        image layer, but allows other functionality that relies on slice
        data existing to continue to work without special casing.

        Parameters
        ----------
        slice_input : _SliceInput
            Describes the slicing plane or bounding box in the layer's dimensions.
        rgb : bool
            True if the underlying image is an RGB or RGBA image (i.e. that the
            last dimension represents a color channel that should not be sliced),
            False otherwise.
        request_id : int | None
            The request id for which we are responding with an empty slice.
            If None, a new request id will be returned, which guarantees that
            the empty slice never appears as loaded. (Used for layer
            initialization before attempting data loading.)
        dtype : np.dtype
            The dtype of the empty image slice.
            Must match expected view dtype.
        """
        locals_owner = _native.scalar_empty_response_locals(request_id)
        del request_id
        return _native.scalar_first_refresh_empty_response(
            slice_input, rgb, dtype, (1,), (3,), globals(), locals_owner
        )

    def to_displayed(
        self, converter: Callable[[np.ndarray], np.ndarray]
    ) -> _ScalarFieldSliceResponse:
        """
        Returns a raw slice converted for display,
        which is needed for Labels and Image.

        Parameters
        ----------
        converter : Callable[[np.ndarray], np.ndarray]
            A function that converts the raw image to a vispy viewable image.

        Returns
        -------
        _ScalarFieldSliceResponse
            Contains the converted image and thumbnail.
        """
        return _native.scalar_response_to_displayed(self, converter, globals())


_native.slice_dataclass_install_init(
    _ScalarFieldSliceResponse, _ScalarFieldSliceResponse.__init__, _slice_dataclass_response_init_template()
)


@dataclass(frozen=True)
class _ScalarFieldSliceRequest(_native.SliceDataclassNamespaceBase):
    """A callable that stores all the input data needed to slice an image layer.

    This should be treated as a deeply immutable structure, even though some
    fields can be modified in place. It is like a function that has captured
    all its inputs already.

    In general, calling an instance of this may take a long time, so you may
    want to run it off the main thread.

    Attributes
    ----------
    slice_input : _SliceInput
        Describes the slicing plane or bounding box in the layer's dimensions.
    data_at_data_level : Any
        The pre-selected data source accounting for the current data level.
    data_at_thumbnail_level : Any
        The pre-selected data source for the thumbnail.
    dtype : DTypeLike
        The dtype of the layer's data.
    data_slice : _ThickNDSlice
        The slicing coordinates and margins in data space.
    others
        See the corresponding attributes in `Layer` and `Image`.
    id : int
        The identifier of this slice request.
    """

    slice_input: _SliceInput
    data_at_data_level: Any = field(repr=False)
    data_at_thumbnail_level: Any = field(repr=False)
    dtype: DTypeLike = field(repr=False)
    dask_indexer: DaskIndexer
    data_slice: _ThickNDSlice
    projection_mode: Any
    multiscale: bool = field(repr=False)
    corner_pixels: np.ndarray
    rgb: bool = field(repr=False)
    data_level: int = field(repr=False)
    thumbnail_level: int = field(repr=False)
    level_shapes: np.ndarray = field(repr=False)
    downsample_factors: np.ndarray = field(repr=False)
    id: int = field(default_factory=_next_request_id)

    def __call__(self) -> _ScalarFieldSliceResponse:
        return _native.scalar_executor_request(self, globals())

    def _call_single_scale(self) -> _ScalarFieldSliceResponse:
        locals_owner = _native.scalar_executor_locals()
        return _native.scalar_executor_single(self, globals(), locals_owner)

    def _call_multi_scale(self) -> _ScalarFieldSliceResponse:
        # Calculate the tile-to-data transform.
        locals_owner = _native.scalar_executor_locals()
        return _native.scalar_executor_multi(self, globals(), locals_owner)

    def _thick_slice_at_level(self, level: int) -> _ThickNDSlice:
        """
        Get the data_slice rescaled for a specific level.
        """
        locals_owner = _native.scalar_executor_locals()
        return _native.scalar_executor_at_level(
            self, level, globals(), locals_owner
        )

    def _project_thick_slice(
        self, data: ArrayLike, data_slice: _ThickNDSlice
    ) -> np.ndarray:
        """
        Slice the given data with the given data slice and project the extra dims.

        This is also responsible for materializing the data if it is backed
        by a lazy store or compute graph (e.g. dask).
        """

        locals_owner = _native.scalar_executor_locals()
        return _native.scalar_executor_project_thick(
            self, data, data_slice, globals(), locals_owner
        )

    @staticmethod
    def _project_slice(
        data: ArrayLike, axis: tuple[int, ...], mode: Any
    ) -> npt.NDArray:
        return _native.scalar_executor_project_abstract(
            data, axis, mode, globals()
        )

    def _get_order(self) -> tuple[int, ...]:
        """Return the ordered displayed dimensions, but reduced to fit in the slice space."""
        locals_owner = _native.scalar_executor_locals()
        return _native.scalar_executor_order(self, globals(), locals_owner)

    def _slice_out_of_bounds(self) -> bool:
        """Check if the data slice is out of bounds for any dimension."""
        locals_owner = _native.scalar_executor_locals()
        return _native.scalar_executor_out_of_bounds(
            self, globals(), locals_owner
        )

    @staticmethod
    def _point_to_slices(
        point: tuple[float, ...],
    ) -> tuple[slice | int, ...]:
        # no need to check out of bounds here cause it's guaranteed

        # values in point and margins are np.nan if no slicing should happen along that dimension
        # which is always the case for displayed dims, so that becomes `slice(None)` for actually
        # indexing the layer.
        # For the rest, indices are rounded to the closest integer
        def generator_factory(iterator):
            yield from iterator

        return _native.scalar_executor_point_to_slices(
            point, globals(), generator_factory
        )

    @staticmethod
    def _data_slice_to_slices(
        data_slice: _ThickNDSlice, dims_displayed: list[int]
    ) -> tuple[slice, ...]:
        locals_owner = _native.scalar_executor_locals()
        return _native.scalar_executor_data_to_slices(
            data_slice, dims_displayed, globals(), locals_owner
        )


_native.slice_dataclass_install_init(
    _ScalarFieldSliceRequest, _ScalarFieldSliceRequest.__init__, _slice_dataclass_request_init_template()
)
