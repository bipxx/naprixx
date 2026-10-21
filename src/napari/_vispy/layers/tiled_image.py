import itertools
from typing import Any

import numpy as np
import numpy.typing as npt
from vispy.scene.visuals import Compound, Image
from vispy.visuals import BaseVisual
from vispy.visuals.transforms.linear import STTransform

from naparixx import _native

# attributes that are passed from the tiled node to the child image nodes.
PASS_THROUGH_ATTRIBUTES = {
    'cmap',
    'clim',
    'opacity',
    'gamma',
    'interpolation',
    'custom_kernel',
}


class TiledImageNode(_native.VispyNamespaceBase, Compound):
    """Custom Vispy scenegraph Node to display large images.

    Vispy 2D rendering works by drawing images as 2D OpenGL textures.
    OpenGL has a texture size limit (driver and hardware dependent), which
    means that some images are too large to be displayed by a single
    texture. This class automatically splits those images into tiles not
    exceeding the texture size, and displays each tile as a texture, offset
    by the appropriate amount in visual space.

    Attributes such as colormap and contrast limits are passed through to
    the child nodes using setattr.

    Attributes
    ----------
    data : np.ndarray
        The data to be displayed.
    texture_format : str
        The texture format (uint8, uint16, float32).
    tile_size : int
        The maximum tile size, used to split the image.
    adopted_children : list[Image]
        The child Image nodes containing the component tiles of the image.
        Note: we use the term "adopted children" because "children" is a
        protected attribute of the Node class, and contains other VisualNodes.
    offsets : list[tuple[int, int]]
        The x/y offset of each tile (in the same order as `adopted_children`).
    """

    def __init__(
        self,
        data: np.ndarray,
        tile_size: int,
        texture_format: str | None = None,
    ) -> None:
        self.adopted_children: list[Image]
        self.offsets: list[tuple[int, int]]
        self.data: npt.ArrayLike | None
        _native.vispy_visual_tiled_initialize(
            self, data, tile_size, texture_format, lambda receiver: super().__init__
        )

    def add_subvisual(self, visual: BaseVisual) -> None:
        """Override for CompoundVisual to not update subvisual transforms.

        CompoundVisual normally sets the transforms for all subvisuals to be
        the same as the parent as they are added to the CompoundVisual. For the
        tiled image layer, that is totally not what we want! So we make sure to
        just update the parent.

        See:
        https://github.com/napari/napari/pull/8395#issuecomment-3776882524
        """
        _native.vispy_visual_tiled_add_subvisual(self, visual)

    def _transform_changed(self, event: Any = None) -> None:
        """Override for CompoundVisual to not update subvisual transforms.

        CompoundVisual normally sets the transforms for all subvisuals to be
        the same as the parent. For the tiled image layer, that is totally not
        what we want! So we make sure to just update the parent.

        See:
        https://github.com/napari/napari/pull/8395#issuecomment-3776882524
        """
        _native.vispy_visual_tiled_transform_changed(self, event, globals())

    def handle_axis_change(self) -> None:
        """Handle changes in axis order by updating tile transforms."""

        locals_owner = _native.vispy_image_visual_locals()
        _native.vispy_visual_tiled_axis_change(self, globals(), locals_owner)

    def set_data(self, data: np.ndarray) -> None:
        self._subvisuals: list[BaseVisual]
        locals_owner = _native.vispy_image_visual_locals()
        _native.vispy_visual_tiled_set_data(self, data, globals(), locals_owner)

    def set_gl_state(self, *args: Any, **kwargs: Any) -> None:
        locals_owner = _native.vispy_image_visual_locals()
        _native.vispy_visual_tiled_gl_state(self, args, kwargs, locals_owner)

    def __getattr__(self, name: str) -> Any:
        return _native.vispy_visual_tiled_getattr(self, name, globals())

    def __setattr__(self, name: str, value: Any) -> None:
        locals_owner = _native.vispy_image_visual_locals()
        _native.vispy_visual_tiled_setattr(
            self,
            name,
            value,
            lambda receiver: super().__setattr__,
            globals(),
            locals_owner,
        )


def make_tiles(
    image: np.ndarray, tile_size: int
) -> list[tuple[tuple[int, int], np.ndarray]]:
    """Split a large image into a list of tiles and offsets.

    Supports input shapes (H, W) for grayscale or (H, W, 3) for RGB.

    Parameters
    ----------
    image : np.ndarray, shape (H, W[, 3])
        The input image.
    tile_size : int
        The maximum size of any tile along any axis.

    Returns
    -------
    tile_list : list[tuple[tuple[int, int], np.ndarray]]
        List of x, y offsets and corresponding array tiles.
    """
    locals_owner = _native.vispy_image_visual_locals()
    return _native.vispy_visual_make_tiles(
        image, tile_size, globals(), locals_owner
    )
