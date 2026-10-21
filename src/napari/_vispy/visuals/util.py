from typing import TYPE_CHECKING

from naparixx import _native

if TYPE_CHECKING:
    from vispy.visuals.visual import Visual
else:

    class Visual:
        pass


class TextureMixin(_native.VispyNamespaceBase, Visual):
    """Store texture format passed to VisPy classes.

    We need to refer back to the texture format, but VisPy
    stores it in a private attribute — ``node._texture.internalformat``.
    This mixin is added to our Node subclasses to avoid having to
    access private VisPy attributes.
    """

    def __init__(self, *args, texture_format: str | None, **kwargs) -> None:
        _native.vispy_visual_texture_initialize(
            self, args, texture_format, kwargs, lambda receiver: super().__init__
        )

    @property
    def texture_format(self) -> str | None:
        return _native.vispy_visual_texture_format(self)
