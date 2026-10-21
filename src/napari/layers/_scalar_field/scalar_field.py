from __future__ import annotations

import types
from abc import ABC, abstractmethod
from contextlib import nullcontext
from functools import lru_cache
from typing import TYPE_CHECKING, cast

import numpy as np
from numpy import typing as npt

from naparixx import native as _native

from napari.layers._data_protocols import LayerDataProtocol
from napari.layers._multiscale_data import MultiScaleData
from napari.layers._scalar_field._slice import (
    _ScalarFieldSliceRequest,
    _ScalarFieldSliceResponse,
)
from napari.layers.base import Layer, _LayerSlicingState
from napari.layers.image._image_constants import Interpolation, VolumeDepiction
from napari.layers.image._image_mouse_bindings import (
    move_plane_along_normal as plane_drag_callback,
    set_plane_position as plane_double_click_callback,
)
from napari.layers.image._image_utils import guess_multiscale
from napari.layers.utils._slice_input import (
    _SliceInput,
    _ThickNDSlice,
)
from napari.layers.utils.layer_utils import (
    compute_multiscale_level_and_corners,
    expand_corners_to_chunk_boundaries,
)
from napari.layers.utils.plane import SlicingPlane
from napari.types import LayerDataType
from napari.utils._dask_utils import DaskIndexer
from napari.utils._dtype import normalize_dtype
from napari.utils._xarray_utils import _get_xr_metadata
from napari.utils.events import Event
from napari.utils.events.event import WarningEmitter
from napari.utils.events.event_utils import connect_no_arg
from napari.utils.geometry import clamp_point_to_bounding_box
from napari.utils.naming import magic_name
from napari.utils.transforms import Affine

if TYPE_CHECKING:
    from collections.abc import Callable, Sequence

    from napari.components import Dims


__all__ = ('ScalarFieldBase',)


def _make_level_materializer(
    data: MultiScaleData,
) -> Callable[[int], np.ndarray]:
    """Return a cached function that materializes one level of *data* at a time.

    The returned callable accepts a ``level`` integer and returns
    ``np.asarray(data[level])``, caching the last result so repeated slice
    requests at the same level avoid redundant data fetches.  Replacing
    ``data`` by constructing a new materializer automatically abandons the old
    cache without an explicit ``cache_clear()``.
    """

    @lru_cache(maxsize=1)
    def _materializer(level: int) -> np.ndarray:
        return _native.scalar_shape_materialize(
            level, globals(), lambda: data
        )

    return _native.scalar_shape_materializer_factory(_materializer)


# It is important to contain at least one abstractmethod to properly exclude this class
# in creating NAMES set inside of napari.layers.__init__
# Mixin must come before Layer
class ScalarFieldBase(Layer, ABC):
    """Base class for volumetric layers.

    Parameters
    ----------
    data : array or list of array
        Image data. Can be N >= 2 dimensional. If the last dimension has length
        3 or 4 can be interpreted as RGB or RGBA if rgb is `True`. If a
        list and arrays are decreasing in shape then the data is treated as
        a multiscale image. Please note multiscale rendering is only
        supported in 2D. In 3D, only the lowest resolution scale is
        displayed.
    affine : n-D array or napari.utils.transforms.Affine
        (N+1, N+1) affine transformation matrix in homogeneous coordinates.
        The first (N, N) entries correspond to a linear transform and
        the final column is a length N translation vector and a 1 or a napari
        `Affine` transform object. Applied as an extra transform on top of the
        provided scale, rotate, and shear values.
    axis_labels : tuple of str, optional
        Dimension names of the layer data.
        If not provided, axis_labels will be set to (..., '-2', '-1').
    blending : str
        One of a list of preset blending modes that determines how RGB and
        alpha values of the layer visual get mixed. Allowed values are
        {'opaque', 'translucent', 'translucent_no_depth', 'additive', and 'minimum'}.
    cache : bool
        Whether slices of out-of-core datasets should be cached upon retrieval.
        Currently, this only applies to dask arrays.
    custom_interpolation_kernel_2d : np.ndarray
        Convolution kernel used with the 'custom' interpolation mode in 2D rendering.
    depiction : str
        3D Depiction mode. Must be one of {'volume', 'plane'}.
        The default value is 'volume'.
    experimental_clipping_planes : list of dicts, list of ClippingPlane, or ClippingPlaneList
        Each dict defines a clipping plane in 3D in data coordinates.
        Valid dictionary keys are {'position', 'normal', and 'enabled'}.
        Values on the negative side of the normal are discarded if the plane is enabled.
    metadata : dict
        Layer metadata.
    multiscale : bool
        Whether the data is a multiscale image or not. Multiscale data is
        represented by a list of array like image data. If not specified by
        the user and if the data is a list of arrays that decrease in shape
        then it will be taken to be multiscale. The first image in the list
        should be the largest. Please note multiscale rendering is only
        supported in 2D. In 3D, only the lowest resolution scale is
        displayed.
    name : str
        Name of the layer. If not provided then will be guessed using heuristics.
    ndim : int
        Number of dimensions in the data.
    opacity : float
        Opacity of the layer visual, between 0.0 and 1.0.
    plane : dict or SlicingPlane
        Properties defining plane rendering in 3D. Properties are defined in
        data coordinates. Valid dictionary keys are
        {'position', 'normal', 'thickness', and 'enabled'}.
    projection_mode : str
        How data outside the viewed dimensions but inside the thick Dims slice will
        be projected onto the viewed dimensions. Must fit to cls._projectionclass.
    rendering : str
        Rendering mode used by vispy. Must be one of our supported
        modes.
    rotate : float, 3-tuple of float, or n-D array.
        If a float convert into a 2D rotation matrix using that value as an
        angle. If 3-tuple convert into a 3D rotation matrix, using a yaw,
        pitch, roll convention. Otherwise assume an nD rotation. Angles are
        assumed to be in degrees. They can be converted from radians with
        np.degrees if needed.
    scale : tuple of float
        Scale factors for the layer.
    shear : 1-D array or n-D array
        Either a vector of upper triangular values, or an nD shear matrix with
        ones along the main diagonal.
    translate : tuple of float
        Translation values for the layer.
    units : tuple of str or pint.Unit, optional
        Units of the layer data in world coordinates.
        If not provided, the default units are assumed to be pixels.
    visible : bool
        Whether the layer visual is currently being displayed.


    Attributes
    ----------
    data : array or list of array
        Image data. Can be N dimensional. If the last dimension has length
        3 or 4 can be interpreted as RGB or RGBA if rgb is `True`. If a list
        and arrays are decreasing in shape then the data is treated as a
        multiscale image. Please note multiscale rendering is only
        supported in 2D. In 3D, only the lowest resolution scale is
        displayed.
    axis_labels : tuple of str
        Dimension names of the layer data.
    custom_interpolation_kernel_2d : np.ndarray
        Convolution kernel used with the 'custom' interpolation mode in 2D rendering.
    depiction : str
        3D Depiction mode used by vispy. Must be one of our supported modes.
    experimental_clipping_planes : ClippingPlaneList
        Clipping planes defined in data coordinates, used to clip the volume.
    metadata : dict
        Image metadata.
    mode : str
        Interactive mode. The normal, default mode is PAN_ZOOM, which
        allows for normal interactivity with the canvas.

        In TRANSFORM mode the image can be transformed interactively.
    multiscale : bool
        Whether the data is a multiscale image or not. Multiscale data is
        represented by a list of array like image data. The first image in the
        list should be the largest. Please note multiscale rendering is only
        supported in 2D. In 3D, only the lowest resolution scale is
        displayed.
    plane : SlicingPlane or dict
        Properties defining plane rendering in 3D. Valid dictionary keys are
        {'position', 'normal', 'thickness'}.
    rendering : str
        Rendering mode used by vispy. Must be one of our supported
        modes.
    units: tuple of pint.Unit
        Units of the layer data in world coordinates.

    Notes
    -----
    _data_view : array (N, M), (N, M, 3), or (N, M, 4)
        Image data for the currently viewed slice. Must be 2D image data, but
        can be multidimensional for RGB or RGBA images if multidimensional is
        `True`.
    """

    _interpolation2d: Interpolation
    _interpolation3d: Interpolation
    _level_materializer: Callable[[int], np.ndarray] | None
    _slicing_state: ScalarFieldSlicingState

    def __init__(
        self,
        data,
        *,
        affine=None,
        axis_labels=None,
        blending='translucent',
        cache=True,
        custom_interpolation_kernel_2d=None,
        depiction='volume',
        experimental_clipping_planes=None,
        metadata=None,
        multiscale=None,
        name=None,
        ndim=None,
        opacity=1.0,
        plane=None,
        projection_mode='none',
        rendering='mip',
        rotate=None,
        scale=None,
        shear=None,
        translate=None,
        units=None,
        visible=True,
    ):
        locals_owner = _native.scalar_field_init_locals(
            data, axis_labels, multiscale, name, ndim, scale, translate, units
        )
        del data, axis_labels, multiscale, name, ndim, scale, translate, units
        self._locked_data_level: int | None
        _native.scalar_field_init(
            self,
            affine,
            blending,
            cache,
            custom_interpolation_kernel_2d,
            depiction,
            experimental_clipping_planes,
            metadata,
            opacity,
            plane,
            projection_mode,
            rendering,
            rotate,
            shear,
            visible,
            globals(),
            locals_owner,
            lambda self: super().__init__,
        )

    def _post_init(self):
        # Trigger generation of view slice and thumbnail
        _native.scalar_field_post_init(self)

    def _slice_dtype(self):
        """Return the dtype of the slice view.

        Overridden in Labels subclass to properly handle the
        32 and 64 bits dtypes.
        """
        return _native.scalar_data_slice_dtype(self)

    @property
    def _data_view(self) -> np.ndarray:
        """Viewable image for the current slice. (compatibility)"""
        return _native.scalar_data_view(self)

    @property
    def _slice(self) -> _ScalarFieldSliceResponse:
        return _native.scalar_data_slice(self)

    @property
    def dtype(self):
        return _native.scalar_shape_dtype(self, globals())

    @property
    def data_raw(
        self,
    ) -> LayerDataProtocol | Sequence[LayerDataProtocol]:
        """Data, exactly as provided by the user."""
        return _native.scalar_data_raw(self)

    @property
    def data(self) -> LayerDataProtocol | MultiScaleData:
        """Data, possibly in multiscale wrapper. Obeys LayerDataProtocol."""
        return _native.scalar_data_data(self)

    @data.setter
    def data(self, data: LayerDataProtocol | MultiScaleData) -> None:
        return _native.scalar_data_set(self, data, globals())

    def _get_ndim(self) -> int:
        """Determine number of dimensions of the layer."""
        return _native.scalar_shape_ndim(self, globals())

    @property
    def _extent_data(self) -> np.ndarray:
        """Extent of layer in data coordinates.

        Returns
        -------
        extent_data : array, shape (2, D)
        """
        locals_owner = _native.scalar_shape_locals()
        return _native.scalar_shape_extent(self, globals(), locals_owner)

    @property
    def _extent_data_augmented(self) -> np.ndarray:
        locals_owner = _native.scalar_shape_locals()
        return _native.scalar_shape_extent_augmented(
            self, -0.5, +0.5, locals_owner
        )

    @property
    def _extent_level_data(self) -> np.ndarray:
        """Extent of layer, accounting for current multiscale level, in data coordinates.

        Returns
        -------
        extent_data : array, shape (2, D)
        """
        locals_owner = _native.scalar_shape_locals()
        return _native.scalar_shape_level_extent(self, globals(), locals_owner)

    @property
    def _extent_level_data_augmented(self) -> np.ndarray:
        locals_owner = _native.scalar_shape_locals()
        return _native.scalar_shape_level_extent_augmented(
            self, -0.5, +0.5, locals_owner
        )

    @property
    def data_level(self) -> int:
        """int: Current level of multiscale, or 0 if image."""
        return _native.scalar_data_level(self)

    @data_level.setter
    def data_level(self, level: int) -> None:
        return _native.scalar_data_set_level(self, level)

    @property
    def locked_data_level(self) -> int | None:
        """int or None: Locked multiscale resolution level.

        When set to an integer, forces rendering at the given multiscale
        level instead of automatic level selection based on the viewport.
        Set to ``None`` to restore automatic behaviour.


        .. versionadded:: 0.7.1
        """
        return _native.scalar_data_locked_level(self)

    @locked_data_level.setter
    def locked_data_level(self, level: int | None) -> None:
        locals_owner = _native.scalar_data_controller_locals()
        return _native.scalar_data_set_locked_level(
            self, level, globals(), locals_owner
        )

    def _reset_data_level(self) -> None:
        """Reset ``_locked_data_level`` and ``_data_level`` for new data.

        Called from the ``data`` setter of subclasses when the underlying
        array is replaced.  Uses the coarsest level for multiscale data
        (matching ``__init__`` behaviour) and 0 for single-scale data.
        """
        return _native.scalar_data_reset_level(self, globals())

    def _update_level_and_corners(
        self, data_bbox_int, shape_threshold, displayed_axes
    ):
        """Update the data level and corner pixels for the current viewport.

        For multiscale layers, selects the appropriate resolution level
        (locked, 2D auto, or 3D coarsest), computes corner pixels for that
        level, and refreshes the layer when the level or visible region
        changes. For non-multiscale data, delegates to the base implementation.
        """
        def generator_factory(iterator):
            yield from iterator

        locals_owner = _native.scalar_data_controller_locals()
        return _native.scalar_data_update_viewport(
            self,
            data_bbox_int,
            shape_threshold,
            displayed_axes,
            globals(),
            lambda self: super()._update_level_and_corners,
            generator_factory,
            locals_owner,
        )

    def _reset_thumbnail_level_data(self) -> None:
        """Set ``_thumbnail_level`` and ``_level_materializer`` for the current data.

        Called once during ``__init__`` and again whenever ``data`` is replaced.
        Single-scale and 3D multiscale layers set ``_level_materializer`` to
        ``None``; only 2D multiscale layers cache the thumbnail level.
        """
        locals_owner = _native.scalar_shape_locals()
        _native.scalar_shape_reset_thumbnail(self, globals(), locals_owner)

    def _get_level_shapes(self) -> Sequence[tuple[int, ...]]:
        locals_owner = _native.scalar_shape_locals()
        return _native.scalar_shape_get_levels(self, globals(), locals_owner)

    @property
    def level_shapes(self) -> np.ndarray:
        """array: Shapes of each level of the multiscale or just of image."""
        return _native.scalar_shape_levels(self, globals())

    @property
    def downsample_factors(self) -> np.ndarray:
        """list: Downsample factors for each level of the multiscale."""
        return _native.scalar_shape_downsample(self, globals())

    @property
    def depiction(self):
        """The current 3D depiction mode.

        Selects a preset depiction mode in vispy
            * volume: images are rendered as 3D volumes.
            * plane: images are rendered as 2D planes embedded in 3D.
                plane position, normal, and thickness are attributes of
                layer.plane which can be modified directly.
        """
        return str(self._depiction)

    @depiction.setter
    def depiction(self, depiction: str | VolumeDepiction) -> None:
        """Set the current 3D depiction mode."""
        self._depiction = VolumeDepiction(depiction)
        self._update_plane_callbacks()
        self.events.depiction()

    def _reset_plane_parameters(self):
        """Set plane attributes to something valid."""
        self.plane.position = np.array(self.data.shape) / 2  # pyrefly: ignore [bad-assignment]
        self.plane.normal = (1, 0, 0)

    def _update_plane_callbacks(self):
        """Set plane callbacks depending on depiction mode."""
        plane_drag_callback_connected = (
            plane_drag_callback in self.mouse_drag_callbacks
        )
        double_click_callback_connected = (
            plane_double_click_callback in self.mouse_double_click_callbacks
        )
        if self.depiction == VolumeDepiction.VOLUME:
            if plane_drag_callback_connected:
                self.mouse_drag_callbacks.remove(plane_drag_callback)
            if double_click_callback_connected:
                self.mouse_double_click_callbacks.remove(
                    plane_double_click_callback
                )
        elif self.depiction == VolumeDepiction.PLANE:
            if not plane_drag_callback_connected:
                self.mouse_drag_callbacks.append(plane_drag_callback)
            if not double_click_callback_connected:
                self.mouse_double_click_callbacks.append(
                    plane_double_click_callback
                )

    @property
    def plane(self):
        return self._plane

    @plane.setter
    def plane(self, value: dict | SlicingPlane) -> None:
        self._plane.update(value)
        self.events.plane()

    @property
    def custom_interpolation_kernel_2d(self):
        return self._custom_interpolation_kernel_2d

    @custom_interpolation_kernel_2d.setter
    def custom_interpolation_kernel_2d(self, value):
        if value is None:
            value = [[1]]
        self._custom_interpolation_kernel_2d = np.array(value, np.float32)
        self.events.custom_interpolation_kernel_2d()

    @abstractmethod
    def _raw_to_displayed(self, raw: np.ndarray) -> np.ndarray:
        """Determine displayed image from raw image.

        For normal image layers, just return the actual image.

        Parameters
        ----------
        raw : array
            Raw array.

        Returns
        -------
        image : array
            Displayed array.
        """
        raise NotImplementedError

    def _set_view_slice(self):
        raise NotImplementedError

    def _get_value(self, position):
        """Value of the data at a position in data coordinates.

        Parameters
        ----------
        position : tuple
            Position in data coordinates.

        Returns
        -------
        value : tuple
            Value of the data.
        """
        def generator_factory(iterator):
            yield from iterator
        owner = _native.viewer_status_provider_locals()
        return _native.viewer_scalar_get_value(self, position, globals(), generator_factory, owner)

    def _get_value_ray(
        self,
        start_point: np.ndarray | None,
        end_point: np.ndarray | None,
        dims_displayed: list[int],
    ) -> int | None:
        """Get the first non-background value encountered along a ray.

        Parameters
        ----------
        start_point : np.ndarray
            (n,) array containing the start point of the ray in data coordinates.
        end_point : np.ndarray
            (n,) array containing the end point of the ray in data coordinates.
        dims_displayed : List[int]
            The indices of the dimensions currently displayed in the viewer.

        Returns
        -------
        value : Optional[int]
            The first non-background value encountered along the ray. If none
            was encountered or the viewer is in 2D mode, returns None.
        """
        owner = _native.viewer_status_sample_locals(start_point, end_point)
        del start_point, end_point
        return _native.viewer_scalar_value_ray(self, dims_displayed, globals(), owner)

    @abstractmethod
    def _calculate_value_from_ray(self, values):
        raise NotImplementedError

    def _get_value_3d(
        self,
        start_point: np.ndarray | None,
        end_point: np.ndarray | None,
        dims_displayed: list[int],
    ) -> int | tuple[int, int | None] | None:
        """Get the first non-background value encountered along a ray.

        Parameters
        ----------
        start_point : np.ndarray
            (n,) array containing the start point of the ray in data coordinates.
        end_point : np.ndarray
            (n,) array containing the end point of the ray in data coordinates.
        dims_displayed : List[int]
            The indices of the dimensions currently displayed in the viewer.

        Returns
        -------
        value : int or tuple
            The first non-zero value encountered along the ray. If a
            non-zero value is not encountered, returns None.
            If multiscale is True, returns a tuple of (data_level, value).
        """
        owner = _native.viewer_status_provider_locals()
        return _native.viewer_scalar_value_3d(self, start_point, end_point, dims_displayed, owner)

    def _get_offset_data_position(self, position: npt.NDArray) -> npt.NDArray:
        """Adjust position for offset between viewer and data coordinates.

        VisPy considers the coordinate system origin to be the canvas corner,
        while napari considers the origin to be the **center** of the corner
        pixel. To get the correct value under the mouse cursor, we need to
        shift the position by 0.5 pixels on each axis.
        """
        return _native.viewer_scalar_offset(self, position, 0.5)

    def _display_bounding_box_at_level(
        self, dims_displayed: list[int], data_level: int
    ) -> npt.NDArray:
        """An axis aligned (ndisplay, 2) bounding box around the data at a given level"""
        shape = self.level_shapes[data_level]
        extent_at_level = np.vstack([np.zeros(len(shape)), shape - 1])
        return extent_at_level[:, dims_displayed].T

    def _display_bounding_box_augmented_data_level(
        self, dims_displayed: list[int]
    ) -> npt.NDArray:
        """An augmented, axis-aligned (ndisplay, 2) bounding box.
        If the layer is multiscale layer, then returns the
        bounding box of the data at the current level
        """
        return self._extent_level_data_augmented[:, dims_displayed].T

    def _get_layer_slicing_state(
        self, data: LayerDataType, cache: bool
    ) -> ScalarFieldSlicingState:
        return ScalarFieldSlicingState(layer=self, data=data, cache=cache)


class ScalarFieldSlicingState(_LayerSlicingState):
    layer: ScalarFieldBase
    _slice_request_class = _ScalarFieldSliceRequest

    def __init__(
        self, layer: ScalarFieldBase, data: LayerDataType, cache: bool
    ):
        _native.scalar_slicing_state_init(
            self, layer, data, cache, globals(), lambda self: super().__init__
        )

    def _set_view_slice(self):
        locals_owner = _native.scalar_first_refresh_locals()
        _native.scalar_first_refresh_set_view(self, globals(), locals_owner)

    def set_slice_input(self, slice_input: _SliceInput, force: bool) -> bool:
        locals_owner = _native.scalar_data_controller_locals()
        return _native.scalar_data_set_slice_input(
            self,
            slice_input,
            force,
            globals(),
            lambda self: super().set_slice_input,
            locals_owner,
        )

    def _make_slice_request(self, dims: Dims) -> _ScalarFieldSliceRequest:
        """Make an image slice request based on the given dims and this image."""
        locals_owner = _native.scalar_data_controller_locals()
        return _native.scalar_data_make_request(self, dims, locals_owner)

    def _make_slice_request_internal(
        self,
        *,
        slice_input: _SliceInput,
        data_slice: _ThickNDSlice,
        dask_indexer: DaskIndexer,
    ) -> _ScalarFieldSliceRequest:
        """Needed to support old-style sync slicing through _slice_dims and
        _set_view_slice.

        This is temporary scaffolding that should go away once we have completed
        the async slicing project: https://github.com/napari/napari/issues/4795
        """
        data_at_thumbnail_level: (
            LayerDataProtocol | MultiScaleData | np.ndarray
        )
        data_at_data_level: LayerDataProtocol | MultiScaleData | np.ndarray
        locals_owner = _native.scalar_first_refresh_locals()
        return _native.scalar_first_refresh_make_request(
            self, slice_input, data_slice, dask_indexer, globals(), locals_owner
        )

    def _update_slice_response(
        self, response: _ScalarFieldSliceResponse
    ) -> None:
        """Update the slice output state currently on the layer. Currently used
        for both sync and async slicing.
        """
        response = _native.scalar_prepare_slice_response(self, response)
        _native.scalar_apply_slice_response(self, response)
