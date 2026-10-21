from __future__ import annotations

from naparixx import native as _native

import copy
import inspect
import itertools
import logging
import os.path
import uuid
from abc import ABC, ABCMeta, abstractmethod
from collections import defaultdict
from collections.abc import Callable, Generator, Hashable, Mapping, Sequence
from contextlib import contextmanager
from functools import cached_property
from typing import TYPE_CHECKING, Any, ClassVar

import magicgui as mgui
import numpy as np
import pint
from npe2 import plugin_manager as pm

from napari.layers.base._base_constants import (
    BaseProjectionMode,
    Blending,
    LayerLock,
    Mode,
)
from napari.layers.base._base_mouse_bindings import (
    highlight_box_handles,
    transform_with_box,
)
from napari.layers.utils._slice_input import (
    _SliceInput,
    _ThickNDSlice,
    apply_units_to_transform,
)
from napari.layers.utils.interactivity_utils import (
    drag_data_to_projected_distance,
)
from napari.layers.utils.layer_utils import (
    Extent,
    coerce_affine,
    convert_to_uint8,
    dims_displayed_world_to_layer,
    get_extent_world,
)
from napari.layers.utils.plane import ClippingPlane, ClippingPlaneList
from napari.settings import get_settings
from napari.types import LayerDataType
from napari.utils._dask_utils import configure_dask
from napari.utils._magicgui import (
    add_layer_to_viewer,
    add_layers_to_viewer,
    get_layers,
)
from napari.utils.events import EmitterGroup, Event, EventedDict
from napari.utils.geometry import (
    find_front_back_face,
    intersect_line_with_axis_aligned_bounding_box_3d,
)
from napari.utils.key_bindings import KeymapProvider
from napari.utils.migrations import _DeprecatingDict
from napari.utils.misc import StringEnum
from napari.utils.mouse_bindings import MousemapProvider
from napari.utils.naming import magic_name
from napari.utils.status_messages import (
    generate_layer_status_strings,
)
from napari.utils.transforms import Affine, CompositeAffine, TransformChain

if TYPE_CHECKING:
    import numpy.typing as npt

    from napari.components.dims import Dims
    from napari.components.overlays import BoundingBoxOverlay, Overlay
    from napari.layers._source import Source

from psygnal import Signal

logger = logging.getLogger('napari.layers.base.base')

__all__ = ('Layer', '_LayerSlicingState', 'no_op')

Array1dOfInts = np.ndarray[tuple[int], np.dtype[np.integer]]
ListOrArrayOfInts = list[int] | Array1dOfInts


def no_op(layer: Layer, event: Event) -> None:
    """
    A convenient no-op event for the layer mouse binding.

    This makes it easier to handle many cases by inserting this as
    as place holder

    Parameters
    ----------
    layer : Layer
        Current layer on which this will be bound as a callback
    event : Event
        event that triggered this mouse callback.

    Returns
    -------
    None

    """


class PostInit(ABCMeta):
    def __init__(self, *args, **kwargs):
        locals_owner = _native.layer_postinit_locals()
        _native.layer_postinit_initialize(
            self, args, kwargs, lambda self: super().__init__, globals(), locals_owner
        )

    def __call__(self, *args, **kwargs):
        locals_owner = _native.layer_postinit_locals()
        return _native.layer_postinit_construct(
            self, args, kwargs, lambda self: super().__call__, globals(), locals_owner
        )


class _LayerSlicingState(_native.LayerNamespaceBase, ABC):
    @property
    def _loaded(self):
        return self._load_state.loaded

    @_loaded.setter
    def _loaded(self, value):
        self._load_state.loaded = value

    @property
    def _last_slice_id(self):
        return self._load_state.request_id

    @_last_slice_id.setter
    def _last_slice_id(self, value):
        self._load_state.request_id = value

    layer: Layer
    slice_done = Signal()
    loaded_data = Signal()

    def __init__(self, layer: Layer, data: LayerDataType, cache: bool):
        self._loaded: bool
        self._last_slice_id: int
        self._units: tuple[pint.Unit, ...] | None
        _native.layer_slicing_construction_initialize(
            self, layer, data, cache, globals()
        )

    def set_view_slice(self) -> None:
        _native.layer_slicing_set_view_slice(self)

    def _slice_indices(
        self, slice_input: _SliceInput, dims: Dims
    ) -> _ThickNDSlice[float | int]:
        """Get the indices of the current slice in the full data array."""
        locals_owner = _native.layer_viewport_locals()
        return _native.layer_viewport_slice_indices(
            self, slice_input, dims, globals(), locals_owner
        )

    @property
    def data_slice(self) -> _ThickNDSlice:
        """Slice in data coordinates."""
        locals_owner = _native.layer_data_slice_locals()
        return _native.layer_slicing_data_slice(self, globals(), locals_owner)

    def update_dims(self):
        _native.layer_viewport_state_update_dims(self)

    def set_slice_input_from_dims(self, dims: Dims, force: bool) -> bool:
        locals_owner = _native.layer_viewport_locals()
        return _native.layer_viewport_set_input_from_dims(
            self, dims, force, locals_owner
        )

    def set_slice_input(self, slice_input: _SliceInput, force: bool) -> bool:
        return _native.layer_viewport_set_input(
            self, slice_input, force, globals()
        )

    @property
    def ndim(self) -> int:
        return _native.layer_viewport_state_ndim(self)

    def make_slice_input(
        self,
        dims: Dims,
    ) -> _SliceInput:
        order: tuple[int, ...]
        locals_owner = _native.layer_viewport_locals()
        return _native.layer_viewport_make_input(self, dims, globals(), locals_owner)

    def _world_to_layer_dims(
        self,
        *,
        world_dims: Sequence[int] | Array1dOfInts,
        ndim_world: int,
    ) -> Array1dOfInts:
        """Map world dimensions to layer dimensions while maintaining order.

        This is used to map dimensions from the full world space defined by ``Dims``
        to the subspace that a layer inhabits, so that those can be used to index the
        layer's data and associated coordinates.

        For example a world ``Dims.order`` of [2, 1, 0, 3] would map to [0, 1] for a
        layer with two dimensions and [1, 0, 2] for a layer with three dimensions
        as those correspond to the relative order of the last two and three world dimensions
        respectively.

        Let's keep in mind a few facts:

         - each dimension index is present exactly once.
         - the lowest represented dimension index will be 0

        That is to say both the `world_dims` input and return results are _some_
        permutation of 0...N

        Examples
        --------

        `[2, 1, 0, 3]`  sliced in N=2 dimensions.

          - we want to keep the N=2 dimensions with the biggest index
          - `[2, None, None, 3]`
          - we filter the None
          - `[2, 3]`
          - reindex so that the lowest dimension is 0 by subtracting 2 from all indices
          - `[0, 1]`

          `[2, 1, 0, 3]`  sliced in N=3 dimensions.

          - we want to keep the N=3 dimensions with the biggest index
          - `[2, 1, None, 3]`
          - we filter the None
          - `[2, 1, 3]`
          - reindex so that the lowest dimension is 0 by subtracting 1 from all indices
          - `[1, 0, 2]`

        Conveniently if the world (layer) dimension is bigger than our displayed
        dims, we can return everything



        Parameters
        ----------
        world_dims : ndarray
            The world dimensions.
        ndim_world : int
            The number of dimensions in the world coordinate system.

        Returns
        -------
        ndarray
            The corresponding layer dimensions with the same ordering as the given world dimensions.
        """
        return _native.layer_viewport_state_world_dims(
            self, world_dims, ndim_world
        )

    @staticmethod
    def _world_to_layer_dims_impl(
        world_dims: Sequence[int] | Array1dOfInts,
        ndim_world: int,
        ndim: int,
    ) -> Array1dOfInts:
        """
        Static for ease of testing
        """
        return _native.world_to_layer_axes(world_dims, ndim_world, ndim)

    @abstractmethod
    def _set_view_slice(self):
        raise NotImplementedError

    @property
    def loaded(self) -> bool:
        """True if this layer is fully loaded in memory, False otherwise.

        Layers that only support sync slicing are always fully loaded.
        Layers that support async slicing can be temporarily not loaded
        while slicing is occurring.
        """
        return self._loaded

    def _set_loaded(self, loaded: bool) -> None:
        """Set the loaded state and notify a change with the loaded event."""
        _native.layer_slicing_set_loaded(self, loaded)

    def _set_unloaded_slice_id(self, slice_id: int) -> None:
        """Set this layer to be unloaded and associated with a pending slice ID.

        This is private but accessed externally because it is related to slice
        state, which is intended to be moved off the layer in the future.
        """
        _native.layer_set_unloaded_slice_id(self, slice_id)

    def _update_loaded_slice_id(self, slice_id: int) -> None:
        """Potentially update the loaded state based on the given completed slice ID.

        This is private but accessed externally because it is related to slice
        state, which is intended to be moved off the layer in the future.
        """
        _native.layer_update_loaded_slice_id(self, slice_id)


@mgui.register_type(choices=get_layers, return_callback=add_layer_to_viewer)
class Layer(
    _native.LayerNamespaceBase,
    KeymapProvider,
    MousemapProvider,
    ABC,
    metaclass=PostInit,
):
    """Base layer class.

    Parameters
    ----------
    data : array or list of array
        Data that the layer is visualizing. Can be N-dimensional.
    ndim : int
        Number of spatial dimensions.
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
    experimental_clipping_planes : list of dicts, list of ClippingPlane, or ClippingPlaneList
        Each dict defines a clipping plane in 3D in data coordinates.
        Valid dictionary keys are {'position', 'normal', and 'enabled'}.
        Values on the negative side of the normal are discarded if the plane is enabled.
    metadata : dict
        Layer metadata.
    mode: str
        The layer's interactive mode.
    multiscale : bool
        Whether the data is multiscale or not. Multiscale data is
        represented by a list of data objects and should go from largest to
        smallest.
    name : str, optional
        Name of the layer. If not provided then will be guessed using heuristics.
    opacity : float
        Opacity of the layer visual, between 0.0 and 1.0.
    projection_mode : str
        How data outside the viewed dimensions but inside the thick Dims slice will
        be projected onto the viewed dimensions. Must fit to cls._projectionclass.
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
    affine : n-D array or napari.utils.transforms.Affine
        (N+1, N+1) affine transformation matrix in homogeneous coordinates.
        The first (N, N) entries correspond to a linear transform and
        the final column is a length N translation vector and a 1 or a napari
        `Affine` transform object. Applied as an extra transform on top of the
        provided scale, rotate, and shear values.
    axis_labels : tuple of str
        Dimension names of the layer data.
    blending : Blending
        Determines how RGB and alpha values get mixed.

        * ``Blending.OPAQUE``
          Allows for only the top layer to be visible and corresponds to
          ``depth_test=True``, ``cull_face=False``, ``blend=False``.
        * ``Blending.TRANSLUCENT``
          Allows for multiple layers to be blended with different opacity and
          corresponds to ``depth_test=True``, ``cull_face=False``,
          ``blend=True``, ``blend_func=('src_alpha', 'one_minus_src_alpha')``,
          and ``blend_equation=('func_add')``.
        * ``Blending.TRANSLUCENT_NO_DEPTH``
          Allows for multiple layers to be blended with different opacity, but
          no depth testing is performed. Corresponds to ``depth_test=False``,
          ``cull_face=False``, ``blend=True``,
          ``blend_func=('src_alpha', 'one_minus_src_alpha')``, and
          ``blend_equation=('func_add')``.
        * ``Blending.ADDITIVE``
          Allows for multiple layers to be blended together with different
          colors and opacity. Useful for creating overlays. It corresponds to
          ``depth_test=False``, ``cull_face=False``, ``blend=True``,
          ``blend_func=('src_alpha', 'one')``, and ``blend_equation=('func_add')``.
        * ``Blending.MINIMUM``
            Allows for multiple layers to be blended together such that
            the minimum of each RGB component and alpha are selected.
            Useful for creating overlays with inverted colormaps. It
            corresponds to ``depth_test=False``, ``cull_face=False``, ``blend=True``,
            ``blend_equation=('min')``.
    cache : bool
        Whether slices of out-of-core datasets should be cached upon retrieval.
        Currently, this only applies to dask arrays.
    corner_pixels : array
        Coordinates of the top-left and bottom-right canvas pixels in the data
        coordinates of each layer. For multiscale data the coordinates are in
        the space of the currently viewed data level, not the highest resolution
        level.
    cursor : str
        String identifying which cursor displayed over canvas.
    cursor_size : int | None
        Size of cursor if custom. None yields default size
    help : str
        Displayed in status bar bottom right.
    mouse_pan : bool
        Determine if canvas interactive panning is enabled with the mouse.
    mouse_zoom : bool
        Determine if canvas interactive zooming is enabled with the mouse.
    multiscale : bool
        Whether the data is multiscale or not. Multiscale data is
        represented by a list of data objects and should go from largest to
        smallest.
    name : str
        Unique name of the layer.
    ndim : int
        Dimensionality of the layer.
    opacity : float
        Opacity of the layer visual, between 0.0 and 1.0.
    projection_mode : str
        How data outside the viewed dimensions but inside the thick Dims slice will
        be projected onto the viewed dimenions.
    rotate : float, 3-tuple of float, or n-D array.
        If a float convert into a 2D rotation matrix using that value as an
        angle. If 3-tuple convert into a 3D rotation matrix, using a yaw,
        pitch, roll convention. Otherwise assume an nD rotation. Angles are
        assumed to be in degrees. They can be converted from radians with
        np.degrees if needed.
    scale : tuple of float
        Scale factors for the layer.
    scale_factor : float
        Conversion factor from canvas coordinates to image coordinates, which
        depends on the current zoom level.
    shear : 1-D array or n-D array
        Either a vector of upper triangular values, or an nD shear matrix with
        ones along the main diagonal.
    source : Source
        source of the layer (such as a plugin or widget)
    status : str
        Displayed in status bar bottom left.
    translate : tuple of float
        Translation values for the layer.
    thumbnail : (N, M, 4) array
        Array of thumbnail data for the layer.
    unique_id : Hashable
        Unique id of the layer. Guaranteed to be unique across the lifetime
        of a viewer.
    visible : bool
        Whether the layer visual is currently being displayed.
    units : tuple of pint.Unit
        Units of the layer data in world coordinates.
        If not explicitly set, these default to pixel.
    z_index : int
        Depth of the layer visual relative to other visuals in the scenecanvas.

    Notes
    -----
    Must define the following:

    * `_extent_data`: property
    * `data` property (setter & getter)

    May define the following:

    * `_set_view_slice()`: called to set currently viewed slice
    * `_basename()`: base/default name of the layer
    """

    _modeclass: type[StringEnum] = Mode
    _projectionclass: type[StringEnum] = BaseProjectionMode

    ModeCallable = Callable[
        ['Layer', Event], Generator[None, None, None] | None
    ]

    _drag_modes: ClassVar[dict[StringEnum, ModeCallable]] = {
        Mode.PAN_ZOOM: no_op,
        Mode.TRANSFORM: transform_with_box,
    }

    _move_modes: ClassVar[dict[StringEnum, ModeCallable]] = {
        Mode.PAN_ZOOM: no_op,
        Mode.TRANSFORM: highlight_box_handles,
    }
    _cursor_modes: ClassVar[dict[StringEnum, str]] = {
        Mode.PAN_ZOOM: 'standard',
        Mode.TRANSFORM: 'standard',
    }
    events: EmitterGroup

    def __init__(
        self,
        data,
        ndim,
        *,
        affine=None,
        axis_labels=None,
        blending='translucent',
        cache=True,  # this should move to future "data source" object.
        experimental_clipping_planes=None,
        metadata=None,
        mode='pan_zoom',
        multiscale=False,
        name=None,
        opacity=1.0,
        projection_mode='none',
        rotate=None,
        scale=None,
        shear=None,
        translate=None,
        units=None,
        visible=True,
        locked: bool | LayerLock = False,
    ) -> None:
        self._unique_id: uuid.UUID | None
        self._visible_mode: str | None
        self._transforms: TransformChain[Affine]
        self._overlays: EventedDict[str, Overlay]
        locals_owner = _native.layer_construction_locals(name, scale, translate)
        del name, scale, translate
        _native.layer_construction_initialize(
            self,
            data,
            ndim,
            affine,
            axis_labels,
            blending,
            cache,
            experimental_clipping_planes,
            metadata,
            mode,
            multiscale,
            opacity,
            projection_mode,
            rotate,
            shear,
            units,
            visible,
            locked,
            (32, 32, 4),
            lambda self: super().__init__,
            globals(),
            locals_owner,
        )

    @property
    def _slice_input(self):
        return _native.layer_viewport_slice_input(self)

    def _post_init(self):
        """Post init hook for subclasses to use."""

    def __str__(self) -> str:
        """Return self.name."""
        return self.name

    def __repr__(self) -> str:
        cls = type(self)
        return f'<{cls.__name__} layer {self.name!r} at {hex(id(self))}>'

    def _mode_setter_helper(self, mode_in: Mode | str) -> StringEnum:
        """
        Helper to manage callbacks in multiple layers

        This will return a valid mode for the current layer, to for example
        refuse to set a mode that is not supported by the layer if it is not editable.

        This will as well manage the mouse callbacks.


        Parameters
        ----------
        mode : type(self._modeclass) | str
            New mode for the current layer.

        Returns
        -------
        mode : type(self._modeclass)
            New mode for the current layer.

        """
        mode = self._modeclass(mode_in)
        # Sub-classes can have their own Mode enum, so need to get members
        # from the specific mode class set on this layer.
        PAN_ZOOM = self._modeclass.PAN_ZOOM  # pyrefly: ignore [missing-attribute]
        TRANSFORM = self._modeclass.TRANSFORM  # pyrefly: ignore [missing-attribute]
        assert mode is not None

        if not self.editable or not self.visible:
            mode = PAN_ZOOM
        if mode == self._mode:
            return mode

        if mode not in self._modeclass:
            raise ValueError(f'Mode not recognized: {mode}')

        for callback_list, mode_dict in [
            (self.mouse_drag_callbacks, self._drag_modes),
            (self.mouse_move_callbacks, self._move_modes),
            (
                self.mouse_double_click_callbacks,
                getattr(
                    self, '_double_click_modes', defaultdict(lambda: no_op)
                ),
            ),
        ]:
            if mode_dict[self._mode] in callback_list:
                callback_list.remove(mode_dict[self._mode])
            callback_list.append(mode_dict[mode])
        self.cursor = self._cursor_modes[mode]

        self.mouse_pan = mode == PAN_ZOOM
        self._overlays['transform_box'].visible = mode == TRANSFORM

        if mode == TRANSFORM:
            self.help = 'hold <space> to move camera, hold <shift> to preserve aspect ratio and rotate in 45掳 increments'
        elif mode == PAN_ZOOM:
            self.help = ''

        return mode

    def update_transform_box_visibility(self, visible):
        if 'transform_box' in self._overlays:
            TRANSFORM = self._modeclass.TRANSFORM  # pyrefly: ignore [missing-attribute]
            self._overlays['transform_box'].visible = (
                self.mode == TRANSFORM and visible
            )

    def update_highlight_visibility(self, visible):
        self._highlight_visible = visible
        self._set_highlight(force=True)

    @property
    def mode(self) -> str:
        """str: Interactive mode

        Interactive mode. The normal, default mode is PAN_ZOOM, which
        allows for normal interactivity with the canvas.

        TRANSFORM allows for manipulation of the layer transform.
        """
        return str(self._mode)

    @mode.setter
    def mode(self, mode: Mode | str) -> None:
        mode_enum = self._mode_setter_helper(mode)
        if mode_enum == self._mode:
            return
        self._mode = mode_enum

        self.events.mode(mode=str(mode_enum))

    @property
    def projection_mode(self):
        """Mode of projection of the thick slice onto the viewed dimensions.

        The sliced data is described by an n-dimensional bounding box ("thick slice"),
        which needs to be projected onto the visible dimensions to be visible.
        The projection mode controls the projection logic.
        """
        return _native.layer_geometry_projection_get(self)

    @projection_mode.setter
    def projection_mode(self, mode):
        locals_owner = _native.layer_geometry_projection_locals(mode)
        del mode
        _native.layer_geometry_projection_set(self, globals(), locals_owner)

    @property
    def unique_id(self) -> Hashable:
        """Unique ID of the layer.

        This is guaranteed to be unique to this specific layer instance
        over the lifetime of the program.
        """
        if self._unique_id is None:
            self._unique_id = uuid.uuid4()
        return self._unique_id

    @classmethod
    def _basename(cls) -> str:
        return f'{cls.__name__}'

    @property
    def name(self) -> str:
        """str: Unique name of the layer."""
        return self._name

    @name.setter
    def name(self, name: str | None) -> None:
        if name == self.name:
            return
        if not name:
            name = self._basename()
        self._name = str(name)
        self.events.name()

    @property
    def metadata(self) -> dict:
        """Key/value map for user-stored data."""
        return self._metadata

    @metadata.setter
    def metadata(self, value: dict) -> None:
        self._metadata.clear()
        self._metadata.update(value)
        self.events.metadata()

    @property
    def source(self) -> Source:
        return self._source

    def _set_source(self, source: Source) -> None:
        if any(
            getattr(self._source, attr)
            for attr in [
                'path',
                'reader_plugin',
                'sample',
                'widget',
                'parent',
            ]
        ):
            raise ValueError(
                f'Tried to set source on layer {self.name} when source is already set to {self._source}'
            )
        self._source = source

    @property
    def loaded(self) -> bool:
        """bool: Whether the layer has been loaded into memory."""
        return self._slicing_state.loaded

    @property
    def opacity(self) -> float:
        """float: Opacity value between 0.0 and 1.0."""
        return self._opacity

    @opacity.setter
    def opacity(self, opacity: float) -> None:
        if not 0.0 <= opacity <= 1.0:
            raise ValueError(
                f'opacity must be between 0.0 and 1.0; got {opacity}'
            )

        self._opacity = float(opacity)
        self._update_thumbnail()
        self.events.opacity()

    @property
    def blending(self) -> str:
        """Blending mode: Determines how RGB and alpha values get mixed.

        Blending.OPAQUE
            Allows for only the top layer to be visible and corresponds to
            depth_test=True, cull_face=False, blend=False.
        Blending.TRANSLUCENT
            Allows for multiple layers to be blended with different opacity
            and corresponds to depth_test=True, cull_face=False,
            blend=True, blend_func=('src_alpha', 'one_minus_src_alpha'),
            and blend_equation=('func_add').
        Blending.TRANSLUCENT_NO_DEPTH
          Allows for multiple layers to be blended with different opacity, but
          no depth testing is performed. Corresponds to ``depth_test=False``,
          cull_face=False, blend=True, blend_func=('src_alpha', 'one_minus_src_alpha'),
          and blend_equation=('func_add').
        Blending.ADDITIVE
            Allows for multiple layers to be blended together with
            different colors and opacity. Useful for creating overlays. It
            corresponds to depth_test=False, cull_face=False, blend=True,
            blend_func=('src_alpha', 'one'), and blend_equation=('func_add').
        Blending.MINIMUM
            Allows for multiple layers to be blended together such that
            the minimum of each RGB component and alpha are selected.
            Useful for creating overlays with inverted colormaps. It
            corresponds to depth_test=False, cull_face=False, blend=True,
            blend_equation=('min').
        """
        return str(self._blending)

    @blending.setter
    def blending(self, blending):
        self._blending = Blending(blending)
        self.events.blending()

    @property
    def visible(self) -> bool:
        """bool: Whether the visual is currently being displayed."""
        return self._visible

    @visible.setter
    def visible(self, visible: bool) -> None:
        self._visible = visible

        if visible:
            # needed because things might have changed while invisible
            # and refresh is noop while invisible
            self.refresh(extent=False)
        self._on_visible_changed()
        self.events.visible()

    def _on_visible_changed(self) -> None:
        """Execute side-effects on this layer related to changes of the visible state."""
        if self.visible and self._visible_mode:
            self.mode = self._visible_mode
        else:
            self._visible_mode = self.mode
            self.mode = self._modeclass.PAN_ZOOM  # pyrefly: ignore [missing-attribute]

    @property
    def editable(self) -> bool:
        """bool: Whether the current layer data is editable from the viewer."""
        return _native.layer_viewport_editable(self)

    @editable.setter
    def editable(self, editable: bool) -> None:
        _native.layer_viewport_editable_set(self, editable)

    @property
    def locked(self) -> LayerLock:
        """LayerLock: which UI operations are locked on this layer.

        ``bool(layer.locked)`` is truthy when any lock flag is set. Setting
        ``True``/``False`` is equivalent to ``LayerLock.ALL``/``LayerLock.NONE``.
        """
        return self._locked

    @locked.setter
    def locked(self, locked: bool | LayerLock) -> None:
        new_lock = self._coerce_lock(locked)
        if self._locked == new_lock:
            return
        self._locked = new_lock
        self.events.locked()

    @staticmethod
    def _coerce_lock(value: bool | LayerLock) -> LayerLock:
        if isinstance(value, LayerLock):
            return value
        return LayerLock.ALL if value else LayerLock.NONE

    def _reset_editable(self) -> None:
        """Reset this layer's editable state based on layer properties."""
        _native.layer_viewport_reset_editable(self)

    def _on_editable_changed(self) -> None:
        """Executes side-effects on this layer related to changes of the editable state."""

    @property
    def axis_labels(self) -> tuple[str, ...]:
        """tuple of axis labels for the layer."""
        return _native.layer_dims_axis_labels_get(self)

    @axis_labels.setter
    def axis_labels(self, axis_labels: Sequence[str] | None) -> None:
        locals_owner = _native.viewer_dims_reconciliation_locals()
        _native.layer_dims_axis_labels_set(self, axis_labels, locals_owner)

    def _has_default_axis_labels(self) -> bool:
        """Return True if axis labels are the default indices."""
        locals_owner = _native.viewer_dims_reconciliation_locals()
        return _native.layer_dims_has_default_axis_labels(
            self, globals(), locals_owner,
            lambda native_iterator: (yield from native_iterator),
        )

    @property
    def units(self) -> tuple[pint.Unit, ...]:
        """List of units for the layer."""
        return _native.layer_geometry_units(self)

    @units.setter
    def units(self, units: Sequence[pint.Unit | str] | None) -> None:
        locals_owner = _native.layer_viewport_locals()
        _native.layer_viewport_units_set(self, units, locals_owner)

    @property
    def scale(self) -> npt.NDArray:
        """array: Anisotropy factors to scale data into world coordinates."""
        return self._transforms['data2physical'].scale

    @scale.setter
    def scale(self, scale: npt.NDArray | None) -> None:
        if scale is None:
            scale = np.array([1] * self.ndim)
        self._transforms['data2physical'].scale = np.array(scale)
        self._clear_extent()
        self.refresh(extent=False)
        # self.refresh()
        self.events.scale()

    @property
    def scale_factor(self):
        """float: Conversion factor from canvas coordinates to image coordinates."""
        return _native.layer_viewport_scale_factor(self)

    @scale_factor.setter
    def scale_factor(self, scale_factor):
        _native.layer_viewport_scale_factor_set(self, scale_factor)

    @property
    def translate(self) -> npt.NDArray:
        """array: Factors to shift the layer by in units of world coordinates."""
        return self._transforms['data2physical'].translate

    @translate.setter
    def translate(self, translate: npt.ArrayLike) -> None:
        self._transforms['data2physical'].translate = np.array(translate)
        self._clear_extent()
        self.refresh(extent=False)
        self.events.translate()

    @property
    def rotate(self) -> np.ndarray[tuple[int, int], np.dtype[np.float64]]:
        """array: Rotation matrix in world coordinates."""
        return self._transforms['data2physical'].rotate

    @rotate.setter
    def rotate(self, rotate: npt.NDArray) -> None:
        self._transforms['data2physical'].rotate = rotate
        self._clear_extent()
        self.refresh(extent=False)
        self.events.rotate()

    @property
    def shear(self) -> npt.NDArray:
        """array: Shear matrix in world coordinates."""
        return self._transforms['data2physical'].shear

    @shear.setter
    def shear(self, shear: npt.NDArray) -> None:
        self._transforms['data2physical'].shear = shear
        self._clear_extent()
        self.refresh(extent=False)
        self.events.shear()

    @property
    def affine(self) -> Affine:
        """napari.utils.transforms.Affine: Extra affine transform to go from physical to world coordinates."""
        return self._transforms['physical2world']

    @affine.setter
    def affine(self, affine: npt.ArrayLike | Affine) -> None:
        # Assignment by transform name is not supported by TransformChain and
        # EventedList, so use the integer index instead. For more details, see:
        # https://github.com/napari/napari/issues/3058
        self._transforms[2] = coerce_affine(
            affine, ndim=self.ndim, name='physical2world'
        )
        self._clear_extent()
        self.refresh(extent=False)
        self.events.affine()

    def _reset_affine(self) -> None:
        self.affine = self._initial_affine

    def _update_dims(self) -> None:
        """Update the dimensionality of transforms and slices when data changes."""
        locals_owner = _native.layer_viewport_locals()
        _native.layer_viewport_update_dims(self, globals(), locals_owner)

    @property
    @abstractmethod
    def data(self):
        # user writes own docstring
        raise NotImplementedError

    @data.setter
    @abstractmethod
    def data(self, data):
        raise NotImplementedError

    @property
    @abstractmethod
    def _extent_data(self) -> np.ndarray:
        """Extent of layer in data coordinates.

        Returns
        -------
        extent_data : array, shape (2, D)
        """
        raise NotImplementedError

    @property
    def _extent_data_augmented(self) -> np.ndarray:
        """Extent of layer in data coordinates.

        Differently from Layer._extent_data, this also includes the "size" of
        data points; for example, Point sizes and Image pixel width are included.

        Returns
        -------
        extent_data : array, shape (2, D)
        """
        return self._extent_data

    @property
    def _extent_world(self) -> np.ndarray:
        """Range of layer in world coordinates.

        Returns
        -------
        extent_world : array, shape (2, D)
        """
        # Get full nD bounding box
        return _native.layer_geometry_extent_world(self, globals())

    @property
    def _extent_world_augmented(self) -> np.ndarray:
        """Range of layer in world coordinates.

        Differently from Layer._extent_world, this also includes the "size" of
        data points; for example, Point sizes and Image pixel width are included.

        Returns
        -------
        extent_world : array, shape (2, D)
        """
        # Get full nD bounding box
        return _native.layer_geometry_extent_world_augmented(self, globals())

    @cached_property
    def extent(self) -> Extent:
        """Extent of layer in data and world coordinates.

        For image-like layers, these coordinates are the locations of the
        pixels in `Layer.data` which are treated like sample points that are
        centered in the rendered version of those pixels.
        For other layers, these coordinates are the points or vertices stored
        in `Layer.data`.
        Lower and upper bounds are inclusive.
        """
        locals_owner = _native.layer_geometry_locals()
        return _native.layer_geometry_extent(self, globals(), locals_owner)

    @cached_property
    def _extent_augmented(self) -> Extent:
        """Augmented extent of layer in data and world coordinates.

        Differently from Layer.extent, this also includes the "size" of data
        points; for example, Point sizes and Image pixel width are included.

        For image-like layers, these coordinates are the locations of the
        pixels in `Layer.data` which are treated like sample points that are
        centered in the rendered version of those pixels.
        For other layers, these coordinates are the points or vertices stored
        in `Layer.data`.
        """
        locals_owner = _native.layer_geometry_locals()
        return _native.layer_geometry_extent_augmented(self, globals(), locals_owner)

    def _clear_extent(self) -> None:
        """Clear extent cache and emit extent event."""
        _native.layer_clear_extent(self)

    @property
    def _data_slice(self) -> _ThickNDSlice:
        """Slice in data coordinates."""
        return self._slicing_state.data_slice

    @abstractmethod
    def _get_ndim(self) -> int:
        raise NotImplementedError

    def _get_base_state(self) -> dict[str, Any]:
        """Get dictionary of attributes on base layer.

        This is useful for serialization and deserialization of the layer.
        And similarly for plugins to pass state without direct dependencies on napari types.

        Returns
        -------
        dict of str to Any
            Dictionary of attributes on base layer.
        """
        base_dict = {
            'affine': self.affine.affine_matrix,
            'axis_labels': self.axis_labels,
            'blending': self.blending,
            'experimental_clipping_planes': [
                plane.model_dump()
                for plane in self.experimental_clipping_planes
            ],
            'metadata': self.metadata,
            'name': self.name,
            'opacity': self.opacity,
            'projection_mode': self.projection_mode,
            'rotate': [list(r) for r in self.rotate],
            'scale': list(self.scale),
            'shear': list(self.shear),
            'translate': list(self.translate),
            'units': self.units,
            'visible': self.visible,
        }
        return base_dict

    @abstractmethod
    def _get_state(self) -> dict[str, Any]:
        raise NotImplementedError

    @property
    def _type_string(self) -> str:
        return self.__class__.__name__.lower()

    def as_layer_data_tuple(self):
        state = self._get_state()
        state.pop('data', None)
        if hasattr(self.__init__, '_rename_argument'):
            state = _DeprecatingDict(state)
            for element in self.__init__._rename_argument:
                state.set_deprecated_from_rename(**element._asdict())
        return self.data, state, self._type_string

    @property
    def thumbnail(self) -> npt.NDArray[np.uint8]:
        """array: Integer array of thumbnail for the layer"""
        return _native.layer_thumbnail_get(self)

    @thumbnail.setter
    def thumbnail(self, thumbnail: npt.NDArray) -> None:
        locals_owner = _native.layer_publication_locals(thumbnail)
        del thumbnail
        _native.layer_thumbnail_set(self, globals(), locals_owner)

    @property
    def ndim(self) -> int:
        """int: Number of dimensions in the data."""
        return self._ndim

    @property
    def help(self) -> str:
        """str: displayed in status bar bottom right."""
        return self._help

    @help.setter
    def help(self, help_text: str) -> None:
        if help_text == self.help:
            return
        self._help = help_text
        self.events.help(help=help_text)

    @property
    def mouse_pan(self) -> bool:
        """bool: Determine if canvas interactive panning is enabled with the mouse."""
        return self._mouse_pan

    @mouse_pan.setter
    def mouse_pan(self, mouse_pan: bool) -> None:
        if mouse_pan == self._mouse_pan:
            return
        self._mouse_pan = mouse_pan
        self.events.mouse_pan(mouse_pan=mouse_pan)

    @property
    def mouse_zoom(self) -> bool:
        """bool: Determine if canvas interactive zooming is enabled with the mouse."""
        return self._mouse_zoom

    @mouse_zoom.setter
    def mouse_zoom(self, mouse_zoom: bool) -> None:
        if mouse_zoom == self._mouse_zoom:
            return
        self._mouse_zoom = mouse_zoom
        self.events.mouse_zoom(mouse_zoom=mouse_zoom)

    @property
    def cursor(self) -> str:
        """str: String identifying cursor displayed over canvas."""
        return self._cursor

    @cursor.setter
    def cursor(self, cursor: str) -> None:
        if cursor == self.cursor:
            return
        self._cursor = cursor
        self.events.cursor(cursor=cursor)

    @property
    def cursor_size(self) -> int:
        """int: Size of cursor if custom. None yields default size."""
        return self._cursor_size

    @cursor_size.setter
    def cursor_size(self, cursor_size: int) -> None:
        if cursor_size == self.cursor_size:
            return
        self._cursor_size = cursor_size
        self.events.cursor_size(cursor_size=cursor_size)

    @property
    def experimental_clipping_planes(self) -> ClippingPlaneList:
        return self._experimental_clipping_planes

    @experimental_clipping_planes.setter
    def experimental_clipping_planes(
        self,
        value: dict
        | ClippingPlane
        | list[ClippingPlane | dict]
        | ClippingPlaneList,
    ) -> None:
        self._experimental_clipping_planes.clear()
        if value is None:
            return

        if isinstance(value, ClippingPlane | dict):
            value = [value]
        for new_plane in value:
            plane = ClippingPlane()
            plane.update(new_plane)
            self._experimental_clipping_planes.append(plane)

    @property
    def bounding_box(self) -> BoundingBoxOverlay:
        return self._overlays['bounding_box']  # pyrefly: ignore [bad-return]

    @property
    def name_overlay(self) -> Overlay:
        return self._overlays['layer_name']

    def set_view_slice(self) -> None:
        _native.layer_set_view_slice(self)

    @abstractmethod
    def _set_view_slice(self):
        raise NotImplementedError

    def _slice_dims(
        self,
        dims: Dims,
        force: bool = False,
    ) -> None:
        """Slice data with values from a global dims model.

        Note this will likely be moved off the base layer soon.

        Parameters
        ----------
        dims : Dims
            The dims model to use to slice this layer.
        force : bool
            True if slicing should be forced to occur, even when some cache thinks
            it already has a valid slice ready. False otherwise.
        """
        locals_owner = _native.layer_viewport_locals()
        _native.layer_viewport_slice_dims(
            self,
            dims,
            force,
            globals(),
            'Layer._slice_dims: %s, dims=%s, force=%s',
            locals_owner,
        )

    def _world_to_layer_dims(
        self,
        *,
        world_dims: Sequence[int] | Array1dOfInts,
        ndim_world: int,
    ) -> Array1dOfInts:
        """For docs to build

        Remove after clean guides/rendering notebook
        """
        return _native.layer_viewport_world_dims(self, world_dims, ndim_world)

    @abstractmethod
    def _update_thumbnail(self):
        raise NotImplementedError

    @abstractmethod
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
        raise NotImplementedError

    def get_value(
        self,
        position: npt.ArrayLike,
        *,
        view_direction: npt.ArrayLike | None = None,
        dims_displayed: list[int] | None = None,
        world: bool = False,
    ) -> tuple[int, ...] | None:
        """Value of the data at a position.

        If the layer is not visible, return None.

        Parameters
        ----------
        position : tuple of float
            Position in either data or world coordinates.
        view_direction : Optional[np.ndarray]
            A unit vector giving the direction of the ray in nD world coordinates.
            The default value is None.
        dims_displayed : Optional[List[int]]
            A list of the dimensions currently being displayed in the viewer.
            The default value is None.
        world : bool
            If True the position is taken to be in world coordinates
            and converted into data coordinates. False by default.

        Returns
        -------
        value : tuple, None
            Value of the data. If the layer is not visible return None.
        """
        owner = _native.viewer_status_value_locals(position, view_direction, dims_displayed)
        del position, view_direction, dims_displayed
        return _native.viewer_layer_get_value(self, world, globals(), "pan_zoom", owner)

    def _get_value_3d(
        self,
        start_point: np.ndarray | None,
        end_point: np.ndarray | None,
        dims_displayed: list[int],
    ) -> float | int | tuple[float | int | None, int | None] | None:
        """Get the layer data value along a ray

        Parameters
        ----------
        start_point : np.ndarray
            The start position of the ray used to interrogate the data.
        end_point : np.ndarray
            The end position of the ray used to interrogate the data.
        dims_displayed : List[int]
            The indices of the dimensions currently displayed in the Viewer.

        Returns
        -------
        value
            The data value along the supplied ray.
        """
        return _native.viewer_layer_value_3d_none(self, start_point, end_point, dims_displayed)

    def projected_distance_from_mouse_drag(
        self,
        start_position: npt.ArrayLike,
        end_position: npt.ArrayLike,
        view_direction: npt.ArrayLike,
        vector: np.ndarray,
        dims_displayed: list[int],
    ) -> npt.NDArray:
        """Calculate the length of the projection of a line between two mouse
        clicks onto a vector (or array of vectors) in data coordinates.

        Parameters
        ----------
        start_position : np.ndarray
            Starting point of the drag vector in data coordinates
        end_position : np.ndarray
            End point of the drag vector in data coordinates
        view_direction : np.ndarray
            Vector defining the plane normal of the plane onto which the drag
            vector is projected.
        vector : np.ndarray
            (3,) unit vector or (n, 3) array thereof on which to project the drag
            vector from start_event to end_event. This argument is defined in data
            coordinates.
        dims_displayed : List[int]
            (3,) list of currently displayed dimensions

        Returns
        -------
        projected_distance : (1, ) or (n, ) np.ndarray of float
        """
        start_position = np.asarray(start_position)
        end_position = np.asarray(end_position)
        view_direction = np.asarray(view_direction)

        start_position = self._world_to_displayed_data(
            start_position, dims_displayed
        )
        end_position = self._world_to_displayed_data(
            end_position, dims_displayed
        )
        view_direction = self._world_to_displayed_data_ray(
            view_direction, dims_displayed
        )
        return drag_data_to_projected_distance(
            start_position, end_position, view_direction, vector
        )

    @contextmanager
    def block_update_properties(self) -> Generator[None, None, None]:
        previous = self._update_properties
        self._update_properties = False
        try:
            yield
        finally:
            self._update_properties = previous

    def _set_highlight(self, force: bool = False) -> None:
        """Render layer highlights when appropriate.

        Parameters
        ----------
        force : bool
            Bool that forces a redraw to occur when `True`.
        """

    @contextmanager
    def _block_refresh(self):
        """Prevent refresh calls from updating view."""
        previous = self._refresh_blocked
        self._refresh_blocked = True
        try:
            yield
        finally:
            self._refresh_blocked = previous

    def refresh(
        self,
        event: Event | None = None,
        *,
        thumbnail: bool = True,
        data_displayed: bool = True,
        highlight: bool = True,
        extent: bool = True,
        force: bool = False,
    ) -> None:
        """Refresh all layer data based on current view slice."""
        _native.layer_refresh(
            self,
            event,
            thumbnail,
            data_displayed,
            highlight,
            extent,
            force,
            globals(),
            'Layer.refresh blocked: %s',
            'Layer.refresh: %s',
        )

    def _refresh_sync(
        self,
        *,
        thumbnail: bool = False,
        data_displayed: bool = False,
        highlight: bool = False,
        extent: bool = False,
        force: bool = False,
    ) -> None:
        _native.layer_refresh_sync(
            self,
            globals(),
            thumbnail=thumbnail,
            data_displayed=data_displayed,
            highlight=highlight,
            extent=extent,
            force=force,
        )

    def world_to_data(
        self, position: npt.ArrayLike
    ) -> np.ndarray[tuple[int], np.dtype[np.floating]]:
        """Convert from world coordinates to data coordinates.

        Parameters
        ----------
        position : tuple, list, 1D array
            Position in world coordinates. If longer then the
            number of dimensions of the layer, the later
            dimensions will be used.

        Returns
        -------
        tuple
            Position in data coordinates.
        """
        owner = _native.viewer_status_position_locals(position)
        del position
        return _native.viewer_layer_world_to_data(self, globals(), owner)

    def data_to_world(self, position):
        """Convert from data coordinates to world coordinates.

        Parameters
        ----------
        position : tuple, list, 1D array
            Position in data coordinates. If longer then the
            number of dimensions of the layer, the later
            dimensions will be used.

        Returns
        -------
        tuple
            Position in world coordinates.
        """
        if len(position) >= self.ndim:
            coords = list(position[-self.ndim :])
        else:
            coords = [0] * (self.ndim - len(position)) + list(position)

        return tuple(self._transforms[1:].simplified(coords))

    def _world_to_displayed_data(
        self, position: np.ndarray, dims_displayed: list[int]
    ) -> npt.NDArray:
        """Convert world to data coordinates for displayed dimensions only.

        Parameters
        ----------
        position : tuple, list, 1D array
            Position in world coordinates. If longer then the
            number of dimensions of the layer, the later
            dimensions will be used.
        dims_displayed : list[int]
            Indices of displayed dimensions of the data.

        Returns
        -------
        tuple
            Position in data coordinates for the displayed dimensions only
        """
        owner = _native.viewer_status_provider_locals()
        return _native.viewer_layer_world_displayed(self, position, dims_displayed, owner)

    @property
    def _data_to_world(self) -> Affine:
        """The transform from data to world coordinates.

        This affine transform is composed from the affine property and the
        other transform properties in the following order:

        affine * (rotate * shear * scale + translate)
        """
        locals_owner = _native.layer_geometry_locals()
        return _native.layer_geometry_data_to_world(self, locals_owner)

    def _world_to_data_ray(self, vector: npt.ArrayLike) -> npt.NDArray:
        """Convert a vector defining an orientation from world coordinates to data coordinates.
        For example, this would be used to convert the view ray.

        Parameters
        ----------
        vector : tuple, list, 1D array
            A vector in world coordinates.

        Returns
        -------
        tuple
            Vector in data coordinates.
        """
        owner = _native.viewer_status_provider_locals()
        return _native.viewer_layer_world_ray(self, vector, globals(), owner)

    def _world_to_displayed_data_ray(
        self, vector_world: npt.ArrayLike, dims_displayed: ListOrArrayOfInts
    ) -> np.ndarray:
        """Convert an orientation from world to displayed data coordinates.

        For example, this would be used to convert the view ray.

        Parameters
        ----------
        vector_world : 1D array
            A vector in world coordinates.

        Returns
        -------
        tuple
            Vector in data coordinates.
        """
        owner = _native.viewer_status_provider_locals()
        return _native.viewer_layer_world_displayed_ray(self, vector_world, dims_displayed, globals(), owner)

    def _world_to_displayed_data_normal(
        self, vector_world: npt.ArrayLike, dims_displayed: list[int]
    ) -> np.ndarray:
        """Convert a normal vector defining an orientation from world coordinates to data coordinates.

        Parameters
        ----------
        vector_world : tuple, list, 1D array
            A vector in world coordinates.
        dims_displayed : list[int]
            Indices of displayed dimensions of the data.

        Returns
        -------
        np.ndarray
            Transformed normal vector (unit vector) in data coordinates.

        Notes
        -----
        This method is adapted from napari-threedee under BSD-3-Clause License.
        For more information see also:
        https://www.scratchapixel.com/lessons/mathematics-physics-for-computer-graphics/geometry/transforming-normals.html
        """

        # the napari transform is from layer -> world.
        # We want the inverse of the world ->  layer, so we just take the napari transform
        inverse_transform = self._transforms[1:].simplified.linear_matrix

        # Extract the relevant submatrix based on dims_displayed
        submatrix = inverse_transform[np.ix_(dims_displayed, dims_displayed)]
        transpose_inverse_transform = submatrix.T

        # transform the vector
        transformed_vector = np.matmul(
            transpose_inverse_transform, vector_world
        )

        transformed_vector /= np.linalg.norm(transformed_vector)

        return transformed_vector

    def _display_bounding_box(self, dims_displayed: list[int]) -> npt.NDArray:
        """An axis aligned (ndisplay, 2) bounding box around the data"""
        return _native.viewer_layer_display_bbox(self, dims_displayed)

    def _display_bounding_box_augmented(
        self, dims_displayed: list[int]
    ) -> npt.NDArray:
        """An augmented, axis-aligned (ndisplay, 2) bounding box.

        This bounding box includes the size of the layer in best resolution, including required padding
        """
        return self._extent_data_augmented[:, dims_displayed].T

    def _display_bounding_box_augmented_data_level(
        self, dims_displayed: list[int]
    ) -> npt.NDArray:
        """An augmented, axis-aligned (ndisplay, 2) bounding box.

        If the layer is multiscale layer, then returns the
        bounding box of the data at the current level
        """
        return self._display_bounding_box_augmented(dims_displayed)

    def click_plane_from_click_data(
        self,
        click_position: npt.ArrayLike,
        view_direction: npt.ArrayLike,
        dims_displayed: list[int],
    ) -> tuple[np.ndarray, np.ndarray]:
        """Calculate a (point, normal) plane parallel to the canvas in data
        coordinates, centered on the centre of rotation of the camera.

        Parameters
        ----------
        click_position : np.ndarray
            click position in world coordinates from mouse event.
        view_direction : np.ndarray
            view direction in world coordinates from mouse event.
        dims_displayed : List[int]
            dimensions of the data array currently in view.

        Returns
        -------
        click_plane : Tuple[np.ndarray, np.ndarray]
            tuple of (plane_position, plane_normal) in data coordinates.
        """
        click_position = np.asarray(click_position)
        view_direction = np.asarray(view_direction)
        plane_position = self.world_to_data(click_position)[dims_displayed]
        plane_normal = self._world_to_data_ray(view_direction)[dims_displayed]
        return plane_position, plane_normal

    def get_ray_intersections(
        self,
        position: npt.ArrayLike,
        view_direction: npt.ArrayLike,
        dims_displayed: list[int],
        world: bool = True,
    ) -> tuple[np.ndarray | None, np.ndarray | None]:
        """Get the start and end point for the ray extending
        from a point through the data bounding box.

        Parameters
        ----------
        position
            the position of the point in nD coordinates. World vs. data
            is set by the world keyword argument.
        view_direction : np.ndarray
            a unit vector giving the direction of the ray in nD coordinates.
            World vs. data is set by the world keyword argument.
        dims_displayed : List[int]
            a list of the dimensions currently being displayed in the viewer.
        world : bool
            True if the provided coordinates are in world coordinates.
            Default value is True.

        Returns
        -------
        start_point : np.ndarray
            The point on the axis-aligned data bounding box that the cursor click
            intersects with. This is the point closest to the camera.
            The point is the full nD coordinates of the layer data.
            If the click does not intersect the axis-aligned data bounding box,
            None is returned.
        end_point : np.ndarray
            The point on the axis-aligned data bounding box that the cursor click
            intersects with. This is the point farthest from the camera.
            The point is the full nD coordinates of the layer data.
            If the click does not intersect the axis-aligned data bounding box,
            None is returned.
        """
        owner = _native.viewer_status_ray_locals(position, view_direction)
        del position, view_direction
        return _native.viewer_layer_ray_intersections(self, dims_displayed, world, globals(), owner)

    def _get_offset_data_position(self, position: npt.NDArray) -> npt.NDArray:
        """Adjust position for offset between viewer and data coordinates."""
        return _native.viewer_layer_offset_position(self, position, globals())

    def _get_ray_intersections(
        self,
        position: npt.NDArray,
        view_direction: np.ndarray,
        dims_displayed: list[int],
        bounding_box: npt.NDArray,
        world: bool = True,
    ) -> tuple[np.ndarray | None, np.ndarray | None]:
        """Get the start and end point for the ray extending
        from a point through the data bounding box.

        Parameters
        ----------
        position
            the position of the point in nD coordinates. World vs. data
            is set by the world keyword argument.
        view_direction : np.ndarray
            a unit vector giving the direction of the ray in nD coordinates.
            World vs. data is set by the world keyword argument.
        dims_displayed : List[int]
            a list of the dimensions currently being displayed in the viewer.
        world : bool
            True if the provided coordinates are in world coordinates.
            Default value is True.
        bounding_box : np.ndarray
            A (2, 3) bounding box around the data currently in view

        Returns
        -------
        start_point : np.ndarray
            The point on the axis-aligned data bounding box that the cursor click
            intersects with. This is the point closest to the camera.
            The point is the full nD coordinates of the layer data.
            If the click does not intersect the axis-aligned data bounding box,
            None is returned.
        end_point : np.ndarray
            The point on the axis-aligned data bounding box that the cursor click
            intersects with. This is the point farthest from the camera.
            The point is the full nD coordinates of the layer data.
            If the click does not intersect the axis-aligned data bounding box,
            None is returned."""
        # get the view direction and click position in data coords
        # for the displayed dimensions only
        owner = _native.viewer_status_position_locals(position)
        del position
        return _native.viewer_layer_ray_intersections_internal(self, view_direction, dims_displayed, bounding_box, world, globals(), owner)

    def _update_level_and_corners(
        self, data_bbox_int, shape_threshold, displayed_axes
    ):
        """Update the data level and corner pixels for the current viewport.

        Sets ``self.corner_pixels`` (and ``self._data_level`` for multiscale
        layers) based on the viewport bounding box, and calls
        ``self.refresh()`` when the level or visible region changes.

        For non-multiscale layers this clips the viewport bounding box to the
        data extent. Overridden in ScalarFieldBase for multiscale logic.

        Parameters
        ----------
        data_bbox_int : numpy.ndarray, shape (2, ndisplay)
            Integer bounding box of the viewport in data coordinates.
        shape_threshold : tuple
            Maximum displayed tile size in pixels (canvas shape).
        displayed_axes : list of int
            Indices of the currently displayed dimensions.
        """
        # The stored corner_pixels attribute must contain valid indices.
        locals_owner = _native.layer_viewport_locals()
        _native.layer_viewport_update_corners(
            self, data_bbox_int, shape_threshold, displayed_axes, globals(), locals_owner
        )

    def _update_draw(
        self, scale_factor, corner_pixels_displayed, shape_threshold
    ):
        """Update canvas scale and corner values on draw.

        For layer multiscale determining if a new resolution level or tile is
        required.

        Parameters
        ----------
        scale_factor : float
            Scale factor going from canvas to world coordinates.
        corner_pixels_displayed : array, shape (2, 2)
            Coordinates of the top-left and bottom-right canvas pixels in
            world coordinates.
        shape_threshold : tuple
            Requested shape of field of view in data coordinates.
        """
        locals_owner = _native.layer_viewport_locals()
        _native.layer_viewport_update_draw(
            self,
            scale_factor,
            corner_pixels_displayed,
            shape_threshold,
            globals(),
            locals_owner,
        )

    def _get_source_info(self) -> dict:
        owner = _native.viewer_status_provider_locals()
        return _native.viewer_layer_source_info(self, globals(), ("plugin", "sample", "widget", ""), owner)

    def get_source_str(self) -> str:
        source_info = self._get_source_info()
        source_str = source_info['layer_name']
        if source_info['layer_base'] != source_info['layer_name']:
            source_str += '\n' + source_info['layer_base']
        if source_info['source_type']:
            source_str += (
                '\n'
                + source_info['source_type']
                + ' : '
                + source_info['plugin']
            )

        return source_str

    def _use_integer_coords_in_status(self) -> bool:
        """If scale, translation, and units are default, use int coords.

        This matches the legacy behavior of the viewer status bar and is
        probably close to what users want, especially for images, in the case
        where metadata is absent.

        See https://github.com/napari/napari/pull/9287 for further discussion.
        """
        def generator_factory(iterator):
            yield from iterator
        return _native.viewer_layer_integer_coords(self, globals(), generator_factory)

    def get_status(
        self,
        position: npt.ArrayLike | None = None,
        *,
        view_direction: npt.ArrayLike | None = None,
        dims_displayed: list[int] | None = None,
        world: bool = False,
        value: Any | None = None,
    ) -> dict[str, str]:
        """
        Status message information of the data at a coordinate position.

        Parameters
        ----------
        position : tuple of float
            Position in either data or world coordinates.
        view_direction : Optional[np.ndarray]
            A unit vector giving the direction of the ray in nD world coordinates.
            The default value is None.
        dims_displayed : Optional[List[int]]
            A list of the dimensions currently being displayed in the viewer.
            The default value is None.
        world : bool
            If True the position is taken to be in world coordinates
            and converted into data coordinates. False by default.
        value : Any
            Pre-computed value. In some cases,

        Returns
        -------
        status_dict : dict
            Dictionary containing a information that can be used as a status update.
        """
        owner = _native.viewer_status_status_locals(position, value)
        del position, value
        return _native.viewer_layer_status(self, view_direction, dims_displayed, world, globals(), ("", ": "), owner)

    def _get_tooltip_text(
        self,
        position: npt.NDArray,
        *,
        view_direction: np.ndarray | None = None,
        dims_displayed: list[int] | None = None,
        world: bool = False,
    ) -> str:
        """
        tooltip message of the data at a coordinate position.

        Parameters
        ----------
        position : ndarray
            Position in either data or world coordinates.
        view_direction : Optional[ndarray]
            A unit vector giving the direction of the ray in nD world coordinates.
            The default value is None.
        dims_displayed : Optional[List[int]]
            A list of the dimensions currently being displayed in the viewer.
            The default value is None.
        world : bool
            If True the position is taken to be in world coordinates
            and converted into data coordinates. False by default.

        Returns
        -------
        msg : string
            String containing a message that can be used as a tooltip.
        """
        return _native.viewer_layer_tooltip_empty(self, position, view_direction, dims_displayed, world, "")

    def save(self, path: str, plugin: str | None = None) -> list[str]:
        """Save this layer to ``path`` with default (or specified) plugin.

        Parameters
        ----------
        path : str
            A filepath, directory, or URL to open.  Extensions may be used to
            specify output format (provided a plugin is available for the
            requested format).
        plugin : str, optional
            Name of the plugin to use for saving. If ``None`` then all plugins
            corresponding to appropriate hook specification will be looped
            through to find the first one that can save the data.

        Returns
        -------
        list of str
            File paths of any files that were written.
        """
        from napari.plugins.io import save_layers

        return save_layers(path, [self], plugin=plugin)

    def __copy__(self):
        """Create a copy of this layer.

        Returns
        -------
        layer : napari.layers.Layer
            Copy of this layer.

        Notes
        -----
        This method is defined for purpose of asv memory benchmarks.
        The copy of data is intentional for properly estimating memory
        usage for layer.

        If you want a to copy a layer without coping the data please use
        `layer.create(*layer.as_layer_data_tuple())`

        If you change this method, validate if memory benchmarks are still
        working properly.
        """
        data, meta, layer_type = self.as_layer_data_tuple()
        return self.create(copy.copy(data), meta=meta, layer_type=layer_type)

    @classmethod
    def create(
        cls,
        data: Any,
        meta: Mapping | None = None,
        layer_type: str | None = None,
    ) -> Layer:
        """Create layer from `data` of type `layer_type`.

        Primarily intended for usage by reader plugin hooks and creating a
        layer from an unwrapped layer data tuple.

        Parameters
        ----------
        data : Any
            Data in a format that is valid for the corresponding `layer_type`.
        meta : dict, optional
            Dict of keyword arguments that will be passed to the corresponding
            layer constructor.  If any keys in `meta` are not valid for the
            corresponding layer type, an exception will be raised.
        layer_type : str
            Type of layer to add. Must be the (case insensitive) name of a
            Layer subclass.  If not provided, the layer is assumed to
            be "image", unless data.dtype is one of (np.int32, np.uint32,
            np.int64, np.uint64), in which case it is assumed to be "labels".

        Raises
        ------
        ValueError
            If ``layer_type`` is not one of the recognized layer types.
        TypeError
            If any keyword arguments in ``meta`` are unexpected for the
            corresponding `add_*` method for this layer_type.

        Examples
        --------
        A typical use case might be to upack a tuple of layer data with a
        specified layer_type.

        >>> data = (
        ...     np.random.random((10, 2)) * 20,
        ...     {'face_color': 'blue'},
        ...     'points',
        ... )
        >>> Layer.create(*data)

        """
        from napari import layers
        from napari.layers.image._image_utils import guess_labels

        layer_type = (layer_type or '').lower()

        # assumes that big integer type arrays are likely labels.
        if not layer_type:
            layer_type = guess_labels(data)

        if layer_type is None or layer_type not in layers.NAMES:
            raise ValueError(
                f"Unrecognized layer_type: '{layer_type}'. Must be one of: {layers.NAMES}."
            )

        Cls = getattr(layers, layer_type.title())

        try:
            return Cls(data, **(meta or {}))
        except Exception as exc:
            if 'unexpected keyword argument' not in str(exc):
                raise

            bad_key = str(exc).split('keyword argument ')[-1]
            raise TypeError(
                f'_add_layer_from_data received an unexpected keyword argument ({bad_key}) for layer type {layer_type}'
            ) from exc

    @abstractmethod
    def _get_layer_slicing_state(
        self, data: LayerDataType, cache: bool
    ) -> _LayerSlicingState:
        """Return a LayerSlicer instance appropriate for this layer."""
        raise NotImplementedError


mgui.register_type(type_=list[Layer], return_callback=add_layers_to_viewer)
