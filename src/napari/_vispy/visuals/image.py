from vispy.scene.visuals import Image as BaseImage

from napari._vispy.visuals.util import TextureMixin
from naparixx import _native


# If data is not present, we need bounds to be None (see napari#3517)
class Image(TextureMixin, BaseImage):
    def _compute_bounds(self, axis, view):
        return _native.vispy_visual_image_bounds(self, axis, view)
