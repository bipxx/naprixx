"""Module providing a unified interface for triangulation helper functions.

We don't want numba to be a required dependency. Therefore, for
numba-accelerated functions, we provide slower NumPy-only alternatives.

With this module, downstream modules can import these helper functions without
knowing which implementation is being used.
"""

from __future__ import annotations

from typing import TYPE_CHECKING

import numpy as np

from napari.layers.shapes import _accelerated_triangulate_python
from naparixx import _native

if TYPE_CHECKING:
    from types import ModuleType

_accelerated_triangulate_numba: ModuleType | None

_native.shape_publication_initialize(_native.shape_publication_globals(), ('napari.layers.shapes', ('_accelerated_triangulate_numba',), 0, False, ('remove_path_duplicates', 'create_box_from_bounding'), ('generate_2D_edge_meshes', 'is_convex', 'normalize_vertices_and_edges', 'reconstruct_polygons_from_edges')))


def _set_numba(value: bool) -> None:
    """Set the Numba backend to use.

    Parameters
    ----------
    value : bool
        If True, use the Numba backend. If False, use the pure Python backend.
    """
    locals_owner = _native.shape_publication_locals(('value', 'val', 'name'), {'value': value})
    del value
    return _native.shape_publication_set_numba(_native.shape_publication_globals(), ('Set the Numba backend to use.\n\n    Parameters\n    ----------\n    value : bool\n        If True, use the Numba backend. If False, use the pure Python backend.\n    ', None, '_py'), locals_owner)


def _set_warmup(value: bool) -> None:
    locals_owner = _native.shape_publication_locals(('value',), {'value': value})
    del value
    return _native.shape_publication_set_warmup(_native.shape_publication_globals(), (None,), locals_owner)


def warmup_universal_numba() -> None:
    """Warm up the functions that are used even with compiled backends."""
    locals_owner = _native.shape_publication_locals(('order', 'data', 'data2'), {})
    return _native.shape_publication_warmup_universal(_native.shape_publication_globals(), ('Warm up the functions that are used even with compiled backends.', None, True, ('C', 'F'), 0, 1, ('dtype', 'order'), 10, 15, False), locals_owner)


def warmup_numba_cache() -> None:
    locals_owner = _native.shape_publication_locals(('order', 'data', 'v', 'e'), {})
    return _native.shape_publication_warmup_cache(_native.shape_publication_globals(), (None, True, ('C', 'F'), 0, 1, ('dtype', 'order'), False), locals_owner)
