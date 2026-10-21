from naparixx import native as _native

from typing import TYPE_CHECKING

import numpy as np
import numpy.typing as npt

from napari.layers._scalar_field._slice import _ScalarFieldSliceRequest
from napari.layers.image._image_constants import ImageProjectionMode
from napari.types import ArrayLike

if TYPE_CHECKING:
    from collections.abc import Callable


def _scalar_executor_legacy_image_glue(data, axis):
    def generator_factory(iterator):
        yield from iterator

    return (lambda: data, lambda: axis, generator_factory)


class _ImageSliceRequest(_ScalarFieldSliceRequest):
    @staticmethod
    def _project_slice(
        data: ArrayLike, axis: tuple[int, ...], mode: ImageProjectionMode
    ) -> npt.NDArray:
        """Project a thick slice along axis based on mode."""
        func: Callable

        def generator_factory(iterator):
            yield from iterator

        locals_owner = _native.scalar_executor_locals()
        return _native.scalar_executor_image_project(
            globals(),
            lambda: data,
            lambda: axis,
            mode,
            generator_factory,
            locals_owner,
        )
