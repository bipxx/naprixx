from typing import TYPE_CHECKING

from vispy.scene.visuals import create_visual_node
from vispy.visuals.image import ImageVisual
from vispy.visuals.shaders import Function, FunctionChain

from napari._vispy.visuals.util import TextureMixin
from naparixx import _native

if TYPE_CHECKING:
    from vispy.visuals.visual import VisualView


class LabelVisual(TextureMixin, ImageVisual):
    """Visual subclass displaying a 2D array of labels."""

    def _build_color_transform(self) -> FunctionChain:
        """Build the color transform function chain."""
        locals_owner = _native.vispy_image_visual_locals()
        return _native.vispy_visual_label_color_transform(
            self, globals(), locals_owner
        )


BaseLabel = create_visual_node(LabelVisual)


class LabelNode(BaseLabel):
    def _compute_bounds(
        self, axis: int, view: 'VisualView'
    ) -> tuple[float, float] | None:
        return _native.vispy_visual_label_bounds(self, axis, view)
