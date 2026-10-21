"""OpenGL Utilities."""

from __future__ import annotations

from contextlib import contextmanager
from functools import lru_cache
from typing import TYPE_CHECKING, Any, cast

import numpy as np
import numpy.typing as npt
from vispy.app import Canvas
from vispy.gloo import gl
from vispy.gloo.context import get_current_canvas

from naparixx import _native
if TYPE_CHECKING:
    from collections.abc import Generator

texture_dtypes = [
    np.dtype(np.uint8),
    np.dtype(np.uint16),
    np.dtype(np.float32),
]


@contextmanager
def _opengl_context() -> Generator[None, None, None]:
    """Assure we are running with a valid OpenGL context.

    Only create a Canvas is one doesn't exist. Creating and closing a
    Canvas causes vispy to process Qt events which can cause problems.
    Ideally call opengl_context() on start after creating your first
    Canvas. However it will work either way.
    """
    return _native.vispy_texture_context_generator(globals())


@lru_cache(maxsize=1)
def get_gl_extensions() -> str:
    """Get basic info about the Gl capabilities of this machine"""
    return _native.vispy_image_gl_extensions(globals())


@lru_cache
def get_max_texture_sizes() -> tuple[int, int]:
    """Return the maximum texture sizes for 2D and 3D rendering.

    If this function is called without an OpenGL context it will create a
    temporary non-visible Canvas. Either way the lru_cache means subsequent
    calls to thing function will return the original values without
    actually running again.

    Returns
    -------
    Tuple[int, int]
        The max textures sizes for (2d, 3d) rendering.
    """
    locals_owner = _native.vispy_texture_service_locals()
    return _native.vispy_texture_max_sizes(globals(), locals_owner)


def fix_data_dtype(data: npt.NDArray) -> npt.NDArray:
    """Makes sure the dtype of the data is accetpable to vispy.

    Acceptable types are int8, uint8, int16, uint16, float32.

    Parameters
    ----------
    data : np.ndarray
        Data that will need to be of right type.

    Returns
    -------
    np.ndarray
        Data that is of right type and will be passed to vispy.
    """
    locals_owner = _native.vispy_texture_service_locals()
    return _native.vispy_texture_fix_data_dtype(data, globals(), locals_owner)


# blend_func parameters are multiplying:
# - source color
# - destination color
# - source alpha
# - destination alpha
# they do not apply to min/max blending equation

BLENDING_MODES = {
    'opaque': {
        'depth_test': True,
        'cull_face': False,
        'blend': False,
    },
    'translucent': {
        'depth_test': True,
        'cull_face': False,
        'blend': True,
        'blend_func': ('src_alpha', 'one_minus_src_alpha', 'one', 'one'),
        'blend_equation': 'func_add',
    },
    'translucent_no_depth': {
        'depth_test': False,
        'cull_face': False,
        'blend': True,
        'blend_func': ('src_alpha', 'one_minus_src_alpha', 'one', 'one'),
        'blend_equation': 'func_add',  # see vispy/vispy#2324
    },
    'additive': {
        'depth_test': False,
        'cull_face': False,
        'blend': True,
        'blend_func': ('src_alpha', 'dst_alpha', 'one', 'one'),
        'blend_equation': 'func_add',
    },
    'minimum': {
        'depth_test': False,
        'cull_face': False,
        'blend': True,
        'blend_equation': 'min',
    },
    'multiplicative': {
        'depth_test': False,
        'cull_face': False,
        'blend': True,
        'blend_func': ('dst_color', 'zero', 'one', 'one'),
        'blend_equation': 'func_add',
    },
}
