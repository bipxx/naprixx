from __future__ import annotations

from typing import TYPE_CHECKING

import numpy as np

from napari._vispy.layers.scalar_field import (
    _VISPY_FORMAT_TO_DTYPE,
    ScalarFieldLayerNode,
    VispyScalarFieldBaseLayer,
)
from napari._vispy.layers.tiled_image import TiledImageNode
from napari._vispy.utils.gl import get_gl_extensions, get_max_texture_sizes
from napari._vispy.visuals.image import Image as ImageNode
from napari._vispy.visuals.volume import Volume as VolumeNode
from napari.layers.base._base_constants import Blending
from napari.layers.image.image import Image
from napari.utils.colormaps.colormap_utils import (
    _coerce_contrast_limits,
    _napari_cmap_to_vispy,
)

from naparixx import _native

if TYPE_CHECKING:
    from vispy.scene import Node


class ImageLayerNode(ScalarFieldLayerNode):
    def __init__(
        self, custom_node: Node = None, texture_format: str | None = None
    ) -> None:
        locals_owner = _native.vispy_image_format_locals(texture_format)
        del texture_format
        _native.vispy_image_node_initialize(
            self, custom_node, globals(), locals_owner
        )

    def get_node(
        self,
        ndisplay: int,
        dtype: np.dtype | None = None,
        shape: tuple | None = None,
    ) -> Node:
        """Return the relevant Vispy VisualNode for current visualization.

        - For small 2D images, this is an Image node.
        - For large 2D images, this is our custom TiledImage node.
        - For 3D images, this is a Volume node.

        Parameters
        ----------
        ndisplay : {2, 3}
            The current number of displayed dimensions.
        dtype : np.dtype
            The dtype of the current data.
        shape : tuple[int, ...]
            The shape of the current data slice.

        Returns:
        node : vispy.scene.Node
            The Node instance to use to display the data.
        """
        locals_owner = _native.vispy_image_locals()
        return _native.vispy_image_get_node(
            self, ndisplay, dtype, shape, globals(), locals_owner
        )


class VispyImageLayer(VispyScalarFieldBaseLayer):
    layer: Image

    def __init__(
        self,
        layer: Image,
        node=None,
        texture_format='auto',
        layer_node_class=ImageLayerNode,
        **kwargs,
    ) -> None:
        _native.vispy_image_initialize(
            self,
            layer,
            node,
            texture_format,
            layer_node_class,
            kwargs,
            lambda self: super().__init__,
        )

    def _on_axis_order_change(self, event=None) -> None:
        locals_owner = _native.vispy_image_locals()
        _native.vispy_image_axis_order(self, globals(), locals_owner)

    def _on_interpolation_change(self) -> None:
        _native.vispy_image_interpolation(self)

    def _on_rendering_change(self) -> None:
        _native.vispy_image_rendering(
            self, lambda self: super()._on_rendering_change
        )

    def _on_colormap_change(self, event=None) -> None:
        _native.vispy_image_colormap(self, globals())

    def _update_mip_minip_cutoff(self) -> None:
        _native.vispy_image_cutoffs(self, globals())

    def _on_contrast_limits_change(self) -> None:
        _native.vispy_image_contrast_limits(self, globals())

    def _on_blending_change(self, event=None) -> None:
        _native.vispy_image_blending(
            self, lambda self: super()._on_blending_change
        )

    def _on_gamma_change(self) -> None:
        _native.vispy_image_gamma(self)

    def _on_iso_threshold_change(self) -> None:
        locals_owner = _native.vispy_image_locals()
        _native.vispy_image_iso_threshold(self, globals(), locals_owner)

    def _on_attenuation_change(self) -> None:
        _native.vispy_image_attenuation(self, globals())

    def reset(self, event=None) -> None:
        _native.vispy_image_reset(self, lambda self: super().reset)
