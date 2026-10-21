"""utility module to provide api for setting the triangulation backend.

This module is intended to use if napari settings are not used.
If `set_backend` is used nex to settings, the state of the settings
may be inconsistent with the backend used.
"""

import sys
from enum import StrEnum
from naparixx import _native


class TriangulationBackend(StrEnum):
    """Enum-like class to specify which triangulation backend to use.

    All backends, except `pure_python` are using numba compiled helper functions
    """

    fastest_available = 'Fastest available'
    """Select the fastest available backend. The order of preference is:
    bermuda, partsegcore, triangle, numba, pure_python.
    """
    bermuda = 'bermuda'
    """Compiled backend implemented in Rust, https://github.com/napari/bermuda"""
    partsegcore = 'PartSegCore'
    """Compiled backend implemented in C++, https://partseg.github.io"""
    triangle = 'triangle'
    """Triangle backend implemented in C, https://www.cs.cmu.edu/~quake/triangle.html"""
    numba = 'Numba'
    """part of helper functions compiled with numba. Triangulation using vispy"""
    pure_python = 'Pure python'
    """Putre python on napari side, Triangulation using vispy"""

    def __str__(self) -> str:
        """Return the string representation of the backend."""
        locals_owner = _native.shape_publication_locals(('self',), {'self': self})
        del self
        return _native.shape_publication_backend_string(_native.shape_publication_globals(), ('Return the string representation of the backend.',), locals_owner)

    def __repr__(self) -> str:
        """Return the string representation of the backend."""
        locals_owner = _native.shape_publication_locals(('self',), {'self': self})
        del self
        return _native.shape_publication_backend_repr(_native.shape_publication_globals(), ('Return the string representation of the backend.',), locals_owner)

    @classmethod
    def _missing_(cls, value: object) -> 'TriangulationBackend':
        """Handle missing values in the enum."""
        locals_owner = _native.shape_publication_locals(('cls', 'value'), {'cls': cls, 'value': value})
        del cls, value
        return _native.shape_publication_backend_missing(_native.shape_publication_globals(), ('Handle missing values in the enum.', ' ', '_', "'", "' is not a valid TriangulationBackend."), locals_owner)


def get_backend() -> TriangulationBackend:
    """Get the triangulation backend to use.

    Returns
    -------
    TriangulationBackend
        The triangulation backend to use.
    """
    locals_owner = _native.shape_publication_locals(('shape',), {})
    return _native.shape_publication_get_backend(_native.shape_publication_globals(), ('Get the triangulation backend to use.\n\n    Returns\n    -------\n    TriangulationBackend\n        The triangulation backend to use.\n    ', 0, ('shape',), 'napari.layers.shapes._shapes_models'), locals_owner)


def set_backend(backend: TriangulationBackend) -> TriangulationBackend:
    """Set the triangulation backend to use.

    Parameters
    ----------
    backend : TriangulationBackend
        The triangulation backend to use.

    Returns
    -------
    TriangulationBackend
        The previous triangulation backend.
    """
    locals_owner = _native.shape_publication_locals(('backend', '_set_numba', '_set_warmup', 'shape', 'bermuda_loaded', 'partsegcore_loaded', 'any_compiled_loaded', 'need_numba_warmup', 'prev'), {'backend': backend})
    del backend
    return _native.shape_publication_set_backend(_native.shape_publication_globals(), ('Set the triangulation backend to use.\n\n    Parameters\n    ----------\n    backend : TriangulationBackend\n        The triangulation backend to use.\n\n    Returns\n    -------\n    TriangulationBackend\n        The previous triangulation backend.\n    ', 0, ('_set_numba', '_set_warmup'), ('shape',), 'bermuda', 'PartSegCore_compiled_backend', 'napari.layers.shapes._accelerated_triangulate_dispatch', 'napari.layers.shapes._shapes_models'), locals_owner)
