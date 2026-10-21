from __future__ import annotations

from dataclasses import dataclass
from copy import copy
from functools import wraps
from typing import TYPE_CHECKING

import numpy as np
from pydantic import (
    ConfigDict,
    Field,
    PrivateAttr,
    field_validator,
    model_validator,
)

from naparixx import _native

from napari.utils.camera_orientations import (
    DEFAULT_ORIENTATION_TYPED,
    AxesOrientation2D,
    AxesOrientation3D,
    Handedness,
    HorizontalAxisOrientation,
    VerticalAxisOrientation,
)
from napari.utils.events import EventedModel
from napari.utils.misc import ensure_n_tuple

if TYPE_CHECKING:
    import numpy.typing as npt


_SYNCED_CAMERA_DESCRIPTION = (
    "Controls how camera state is managed when switching between\n"
    "2D and 3D views. When checked, camera center and zoom are\n"
    "shared between views, with the depth (Z) component synced via\n"
    "the dims slider. When unchecked, each mode remembers\n"
    "its own camera state independently."
)


@dataclass(frozen=True)
class _CameraState:
    """Captured camera state for a single ndisplay mode.

    This is a lightweight private data container used internally by
    :class:`Camera` to preserve per-mode center, zoom, and angles when
    switching between 2D and 3D views.
    """

    center: tuple[float, float, float] | tuple[float, float]
    zoom: float
    angles: tuple[float, float, float]


class Camera(EventedModel):
    """Camera object modeling position and view of the camera.

    Attributes
    ----------
    center : 3-tuple
        Center of rotation for the camera.
        In 2D viewing the last two values are used.
    zoom : float
        Scale from canvas pixels to world pixels.
    angles : 3-tuple
        Euler angles of camera when viewing in 3D, in degrees.
        The angles rotate the camera about the three displayed dimensions,
        in the same order as they appear in Dims.order.
        Euler angles in 3D do not uniquely represent an orientation, so
        different angle triplets can produce the same view.
        Stored or returned angle values may differ from those that were set,
        while still representing an equivalent camera orientation.
    perspective : float
        Perspective (aka "field of view" in vispy) of the camera (if 3D).
    mouse_pan : bool
        If the camera interactive panning with the mouse is enabled or not.
    mouse_zoom : bool
        If the camera interactive zooming with the mouse is enabled or not.
    """

    # fields
    center: tuple[float, float, float] | tuple[float, float] = (
        0.0,
        0.0,
        0.0,
    )
    zoom: float = 1.0
    angles: tuple[float, float, float] = (0.0, 0.0, 0.0)
    perspective: float = 0
    mouse_pan: bool = True
    mouse_zoom: bool = True
    orientation: AxesOrientation3D = DEFAULT_ORIENTATION_TYPED
    synced: bool = Field(True, description=_SYNCED_CAMERA_DESCRIPTION)

    # Per-mode camera state cache for the "separate" (synced=False) mode.
    _native_state: _native.CameraState = PrivateAttr(
        default_factory=_native.CameraState
    )
    model_config = ConfigDict(dependencies=_native.camera_property_dependencies())

    @model_validator(mode="after")
    def _sync_native_state(self):
        return _native.camera_sync(self)

    def __copy__(self):
        return _native.camera_shallow_copy(self, super())

    def model_copy(self, *, update=None, deep=False):
        return _native.camera_copy_model(self, super(), update, deep)

    @wraps(EventedModel.model_post_init)
    def model_post_init(self, context, /):
        return _native.component_model_post_init(self, super(), context)

    @wraps(EventedModel.__deepcopy__)
    def __deepcopy__(self, memo=None):
        return _native.component_model_deep_copy(self, super(), memo)

    @wraps(EventedModel.__setstate__)
    def __setstate__(self, state):
        return _native.component_model_restore(self, super(), state)

    def _cache_state(self, ndisplay_mode: int) -> None:
        """Save current camera state for a given ndisplay mode."""
        self._native_state.cache(ndisplay_mode)

    def _pop_cached_state(self, ndisplay_mode: int) -> _CameraState | None:
        """Retrieve and remove cached state for a given ndisplay mode."""
        return _native.camera_pop_cache(self, ndisplay_mode)

    @field_validator("center", "angles", mode="before")
    @classmethod
    def _ensure_3_tuple(cls, v):
        return ensure_n_tuple(v, n=3)

    @property
    def view_direction(self) -> tuple[float, float, float]:
        """3D view direction vector of the camera.

        View direction is calculated from the Euler angles and returned as a
        3-tuple. This direction is in 3D scene coordinates, the world coordinate
        system for three currently displayed dimensions.
        """
        return self._native_state.directions()[0]

    @property
    def up_direction(self) -> tuple[float, float, float]:
        """3D direction vector pointing up on the canvas.

        Up direction is calculated from the Euler angles and returned as a
        3-tuple. This direction is in 3D scene coordinates, the world coordinate
        system for three currently displayed dimensions.
        """
        return self._native_state.directions()[1]

    def set_view_direction(
        self,
        view_direction: tuple[float, float, float],
        up_direction: tuple[float, float, float] = (0, -1, 0),
    ):
        """Set camera angles from direction vectors.

        Both the view direction and the up direction are specified in 3D scene
        coordinates, the world coordinate system for three currently displayed
        dimensions.

        The provided up direction must not be parallel to the provided
        view direction. The provided up direction does not need to be orthogonal
        to the view direction. The final up direction will be a vector orthogonal
        to the view direction, aligned with the provided up direction.

        Parameters
        ----------
        view_direction : 3-tuple of float
            The desired view direction vector in 3D scene coordinates, the world
            coordinate system for three currently displayed dimensions.
        up_direction : 3-tuple of float
            A direction vector which will point upwards on the canvas. Defaults
            to (0, -1, 0) unless the view direction is parallel to the y-axis,
            in which case will default to (-1, 0, 0).
        """
        _native.camera_set_direction(self, view_direction, up_direction)

    def calculate_nd_view_direction(
        self, ndim: int, dims_displayed: tuple[int, ...]
    ) -> npt.NDArray[np.float64] | None:
        """Calculate the nD view direction vector of the camera.

        Parameters
        ----------
        ndim : int
            Number of dimensions in which to embed the 3D view vector.
        dims_displayed : Tuple[int]
            Dimensions in which to embed the 3D view vector.

        Returns
        -------
        view_direction_nd : np.ndarray
            nD view direction vector as an (ndim, ) ndarray
        """
        return self._native_state.embed_direction(ndim, dims_displayed)

    def calculate_nd_up_direction(
        self, ndim: int, dims_displayed: tuple[int, ...]
    ) -> np.ndarray | None:
        """Calculate the nD up direction vector of the camera.

        Parameters
        ----------
        ndim : int
            Number of dimensions in which to embed the 3D view vector.
        dims_displayed : Tuple[int]
            Dimensions in which to embed the 3D view vector.

        Returns
        -------
        up_direction_nd : np.ndarray
            nD view direction vector as an (ndim, ) ndarray
        """
        return self._native_state.embed_direction(ndim, dims_displayed, up=True)

    @property
    def orientation2d(
        self,
    ) -> AxesOrientation2D:
        return self.orientation[1:]

    @orientation2d.setter
    def orientation2d(
        self,
        value: AxesOrientation2D,
    ) -> None:
        _native.camera_set_orientation2d(self, value)

    @property
    def handedness(self) -> Handedness:
        """Right or left-handedness of the current orientation."""
        # we know default orientation is right-handed, so an odd number of
        # differences from default means left-handed.
        return Handedness.LEFT if self._native_state.left_handed else Handedness.RIGHT


_native.install_camera_namespace(Camera)
