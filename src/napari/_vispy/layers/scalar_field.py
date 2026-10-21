from __future__ import annotations

import warnings
from abc import ABC, abstractmethod
from typing import TYPE_CHECKING

import numpy as np
from vispy.visuals import ImageVisual

from naparixx import _native
from napari._vispy.layers.base import VispyBaseLayer
from napari._vispy.layers.tiled_image import TiledImageNode
from napari._vispy.utils.gl import fix_data_dtype
from napari._vispy.visuals.labels import LabelNode
from napari._vispy.visuals.volume import Volume as VolumeNode
from napari.layers._scalar_field.scalar_field import ScalarFieldBase

if TYPE_CHECKING:
    from vispy.scene import Node


class ScalarFieldLayerNode(_native.VispyNamespaceBase, ABC):
    """Abstract base class for scalar field layer nodes."""

    @abstractmethod
    def __init__(self, node=None, texture_format='auto') -> None:
        _native.vispy_base_data_change(self, globals())

    @abstractmethod
    def get_node(
        self,
        ndisplay: int,
        dtype: np.dtype | None = None,
        shape: tuple | None = None,
    ) -> Node:
        """Return the appropriate node for the given ndisplay and dtype."""
        _native.vispy_base_data_change(self, globals())


class VispyScalarFieldBaseLayer(VispyBaseLayer[ScalarFieldBase]):
    def __init__(
        self,
        layer: ScalarFieldBase,
        node=None,
        texture_format='auto',
        layer_node_class=ScalarFieldLayerNode,
        **kwargs,
    ) -> None:
        # Use custom node from caller, or our standard image/volume nodes.
        _native.vispy_scalar_initialize(
            self,
            layer,
            node,
            texture_format,
            layer_node_class,
            kwargs,
            lambda self: super().__init__,
            globals(),
        )

    def _on_display_change(self, data=None) -> None:
        locals_owner = _native.vispy_scalar_field_data_locals(data)
        del data
        _native.vispy_scalar_display_change(self, globals(), locals_owner)

    def _on_data_change(self) -> None:
        locals_owner = _native.vispy_scalar_field_locals()
        _native.vispy_scalar_data_change(self, globals(), locals_owner)

    def _on_custom_interpolation_kernel_2d_change(self) -> None:
        _native.vispy_scalar_interpolation(self)

    def _on_rendering_change(self) -> None:
        _native.vispy_scalar_rendering(self, globals())

    def _on_depiction_change(self) -> None:
        _native.vispy_scalar_depiction(self, globals())

    def _on_blending_change(self, event=None) -> None:
        _native.vispy_scalar_call_base(super()._on_blending_change)

    def _on_plane_thickness_change(self) -> None:
        _native.vispy_scalar_plane(self, 'thickness', globals())

    def _on_plane_position_change(self) -> None:
        _native.vispy_scalar_plane(self, 'position', globals())

    def _on_plane_normal_change(self) -> None:
        _native.vispy_scalar_plane(self, 'normal', globals())

    def _on_colormap_change(self, event=None) -> None:
        _native.vispy_scalar_abstract_colormap(globals())

    def reset(self, event=None) -> None:
        _native.vispy_scalar_reset(self, lambda self: super().reset)

    def downsample_texture(
        self, data: np.ndarray, MAX_TEXTURE_SIZE: int
    ) -> np.ndarray:
        """Downsample data based on maximum allowed texture size.

        Parameters
        ----------
        data : array
            Data to be downsampled if needed.
        MAX_TEXTURE_SIZE : int
            Maximum allowed texture size.

        Returns
        -------
        data : array
            Data that now fits inside texture.
        """
        locals_owner = _native.vispy_scalar_field_data_locals(data)
        del data
        return _native.vispy_scalar_downsample_texture(
            self, MAX_TEXTURE_SIZE, globals(), locals_owner
        )


_VISPY_FORMAT_TO_DTYPE: dict[str | None, np.dtype] = {
    'r8': np.dtype(np.uint8),
    'r16': np.dtype(np.uint16),
    'r32f': np.dtype(np.float32),
}
_DTYPE_TO_VISPY_FORMAT = {v: k for k, v in _VISPY_FORMAT_TO_DTYPE.items()}

# this is moved after reverse mapping is defined
# to always have non None values in _DTYPE_TO_VISPY_FORMAT
_VISPY_FORMAT_TO_DTYPE[None] = np.dtype(np.float32)


def get_dtype_from_vispy_texture_format(format_str: str) -> np.dtype:
    """Get the numpy dtype from a vispy texture format string.

    Parameters
    ----------
    format_str : str
        The vispy texture format string.

    Returns
    -------
    dtype : numpy.dtype
        The numpy dtype corresponding to the vispy texture format string.
    """
    return _native.vispy_scalar_dtype_from_format(format_str, globals())
