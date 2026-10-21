from __future__ import annotations

from abc import ABC, abstractmethod
from typing import TYPE_CHECKING, Generic, Literal, TypeAlias, TypeVar, cast

import numpy as np
import pint
from vispy.visuals.transforms import MatrixTransform

from napari._vispy.utils.gl import BLENDING_MODES, get_max_texture_sizes
from napari.layers import Layer
from napari.utils.events import disconnect_events
from naparixx import _native

if TYPE_CHECKING:
    from vispy.scene import VisualNode

    from napari._vispy.utils.qt_font import FontInfo

_L = TypeVar('_L', bound=Layer)
Vector3: TypeAlias = np.ndarray[tuple[Literal[3]], np.dtype[np.floating]]


class VispyBaseLayer(_native.VispyNamespaceBase, ABC, Generic[_L]):
    """Base object for individual layer views

    Meant to be subclassed.

    Parameters
    ----------
    layer : napari.layers.Layer
        Layer model.
    node : vispy.scene.VisualNode
        Central node with which to interact with the visual.

    Attributes
    ----------
    layer : napari.layers.Layer
        Layer model.
    node : vispy.scene.VisualNode
        Central node with which to interact with the visual.
    scale : sequence of float
        Scale factors for the layer visual in the scenecanvas.
    translate : sequence of float
        Translation values for the layer visual in the scenecanvas.
    MAX_TEXTURE_SIZE_2D : int
        Max texture size allowed by the vispy canvas during 2D rendering.
    MAX_TEXTURE_SIZE_3D : int
        Max texture size allowed by the vispy canvas during 2D rendering.


    Notes
    -----
    _master_transform : vispy.visuals.transforms.MatrixTransform
        Transform positioning the layer visual inside the scenecanvas.
    """

    layer: _L

    def __init__(
        self, layer: _L, node: VisualNode, font_info: FontInfo
    ) -> None:
        _native.vispy_base_initialize(
            self, layer, node, font_info, lambda self: super().__init__, globals()
        )

    @property
    def world_units(self) -> tuple[pint.Unit, ...]:
        return _native.vispy_base_world_units(self)

    @world_units.setter
    def world_units(self, value: tuple[pint.Unit, ...] | None) -> None:
        _native.vispy_base_set_world_units(self, value, globals())

    def _recalculate_units_scale(self):
        """Calculate the scale factor between the layer units and the world units.

        This is used to convert the layer's data coordinates to world coordinates.
        If self._units is None, then the scale is set to 1 for all dimensions.
        """
        locals_owner = _native.vispy_base_locals()
        _native.vispy_base_recalculate_units_scale(self, globals(), locals_owner)

    @property
    def _master_transform(self):
        """vispy.visuals.transforms.MatrixTransform:
        Central node's firstmost transform.
        """
        # whenever a new parent is set, the transform is reset
        # to a NullTransform so we reset it here
        return _native.vispy_base_master_transform(self, globals())

    @property
    def translate(self):
        """sequence of float: Translation values."""
        return _native.vispy_base_translate(self)

    @property
    def scale(self):
        """sequence of float: Scale factors."""
        locals_owner = _native.vispy_base_locals()
        return _native.vispy_base_scale(self, globals(), locals_owner)

    @property
    def order(self):
        """int: Order in which the visual is drawn in the scenegraph.

        Lower values are closer to the viewer.
        """
        return _native.vispy_base_order(self)

    @order.setter
    def order(self, order):
        _native.vispy_base_set_order(self, order)

    @abstractmethod
    def _on_data_change(self):
        _native.vispy_base_data_change(self, globals())

    def _on_refresh_change(self):
        _native.vispy_base_refresh(self)

    def _on_visible_change(self):
        _native.vispy_base_visible(self)

    def _on_opacity_change(self):
        _native.vispy_base_opacity(self)

    def _on_blending_change(self, event=None):
        locals_owner = _native.vispy_base_locals()
        _native.vispy_base_blending(self, event, globals(), locals_owner)

    def _on_matrix_change(self):
        locals_owner = _native.vispy_base_locals()
        _native.vispy_base_matrix(self, globals(), locals_owner)

    def _on_experimental_clipping_planes_change(self):
        _native.vispy_base_clipping_planes(self, globals())

    def _on_view_direction_change(
        self,
        view: Vector3 | None = None,
        up: Vector3 | None = None,
    ):
        _native.vispy_base_view_direction(self, view, up)

    def reset(self):
        _native.vispy_base_reset(self)

    def _on_poll(self, event=None):
        """Called when camera moves, before we are drawn.

        Optionally called for some period once the camera stops, so the
        visual can finish up what it was doing, such as loading data into
        VRAM or animating itself.
        """
        _native.vispy_base_poll(self, event)

    def close(self):
        """Vispy visual is closing."""
        _native.vispy_base_close(self, globals())
