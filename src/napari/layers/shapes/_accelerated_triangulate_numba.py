"""Triangulation utilities"""

from __future__ import annotations

from enum import Enum
from math import atan2, pi
from typing import Literal, overload

import numpy as np
from numba import njit
from numba.core import types
from numba.typed import List

from napari.layers.shapes.shape_types import (
    CoordinateArray,
    CoordinateArray2D,
    CoordinateArray3D,
    EdgeArray,
)



# Numba lowering/type declarations; all numerical decisions execute in C++.
import operator as _shape_numba_operator
from numba import objmode as _shape_numba_objmode
from numba.core.registry import cpu_target as _shape_numba_target
from numba.extending import overload as _shape_numba_overload
from numba.np.numpy_support import as_dtype as _shape_numba_as_dtype
from naparixx import _native


def _shape_numba_bridge(operation, arguments, literals, layout):
    locals_owner = _native.shape_accel_numba_locals(operation, arguments, layout)
    del arguments
    return _native.shape_accel_numba_call(operation, literals, _native.shape_accel_namespace(), locals_owner, None)


@_shape_numba_overload(_shape_numba_bridge, prefer_literal=True)
def _shape_numba_bridge_typing(operation, arguments, literals, layout):
    result_type, compile_types = _native.shape_accel_numba_signature(operation, arguments, _native.shape_accel_namespace())

    def implementation(operation, arguments, literals, layout):
        with _shape_numba_objmode(result=result_type):
            locals_owner = _native.shape_accel_numba_locals(operation, arguments, layout)
            result = _native.shape_accel_numba_call(operation, literals, _shape_numba_bridge.__globals__, locals_owner, compile_types)
        return result

    return implementation

class Orientation(Enum):
    """Orientation of a triangle.

    The terms assume napari's preferred coordinate frame, in which
    the 0th axis, y, is pointing down, and the 1st axis, x, is pointing
    right. If one of the axes is flipped, the observed orientation
    would also flip.
    """

    clockwise = -1
    collinear = 0
    anticlockwise = 1


@njit(cache=True, inline='always')
def _dot(v0: np.ndarray, v1: np.ndarray) -> float:
    """Return the dot product of two 2D vectors.

    If the vectors have norm 1, this is the cosine of the angle between them.
    """
    return _shape_numba_bridge('_dot', (v0, v1), ('Return the dot product of two 2D vectors.\n\n    If the vectors have norm 1, this is the cosine of the angle between them.\n    ', 0, 1), ('v0', 'v1'))


@njit(cache=True, inline='always')
def _cross_z(v0: np.ndarray, v1: np.ndarray) -> float:
    """Return the z-magnitude of the cross-product between two vectors.

    If the vectors have norm 1, this is the sine of the angle between them.
    """
    return _shape_numba_bridge('_cross_z', (v0, v1), ('Return the z-magnitude of the cross-product between two vectors.\n\n    If the vectors have norm 1, this is the sine of the angle between them.\n    ', 0, 1), ('v0', 'v1'))


@njit(cache=True, inline='always')
def _sign_abs(f: float) -> tuple[float, float]:
    """Return 1, -1, or 0 based on the sign of f, as well as the abs of f.

    The order of if-statements shows what the function is optimized for given
    the early returns. In this case, an array with many positive values will
    execute more quickly than one with many negative values.
    """
    return _shape_numba_bridge('_sign_abs', (f,), ('Return 1, -1, or 0 based on the sign of f, as well as the abs of f.\n\n    The order of if-statements shows what the function is optimized for given\n    the early returns. In this case, an array with many positive values will\n    execute more quickly than one with many negative values.\n    ', 0, 1.0, -1.0, (0.0, 0.0)), ('f',))


@njit(cache=True, inline='always')
def _orthogonal_vector(v: np.ndarray, ccw: bool = True) -> np.ndarray:
    """Return an orthogonal vector to v (2D).

    Parameters
    ----------
    v : np.ndarray of float, shape (2,)
        The input vector.
    ccw : bool
        Whether you want the orthogonal vector in the counterclockwise
        direction (in the napari reference frame) or clockwise.

    Returns
    -------
    np.ndarray, shape (2,)
        A vector orthogonal to the input vector, and of the same magnitude.
    """
    return _shape_numba_bridge('_orthogonal_vector', (v, ccw), ('Return an orthogonal vector to v (2D).\n\n    Parameters\n    ----------\n    v : np.ndarray of float, shape (2,)\n        The input vector.\n    ccw : bool\n        Whether you want the orthogonal vector in the counterclockwise\n        direction (in the napari reference frame) or clockwise.\n\n    Returns\n    -------\n    np.ndarray, shape (2,)\n        A vector orthogonal to the input vector, and of the same magnitude.\n    ', 1, 0), ('v', 'ccw'))


@njit(cache=True, inline='always')
def _calc_output_size(
    direction_vectors: np.ndarray,
    closed: bool,
    cos_miter_limit: float,
    bevel: bool,
) -> int:
    """Calculate the size of the output arrays for the triangulation.

    Parameters
    ----------
    direction_vectors : np.ndarray
        Nx2 array of direction vectors along the path. The direction vectors
        have norm 1, so computing the cosine between them is just the dot
        product.
    closed : bool
        True if shape edge is a closed path.
    cos_miter_limit : float
        Miter limit which determines when to switch from a miter join to a
        bevel join
    bevel : bool
        If True, a bevel join is always used.
        If False, a bevel join will be used when the miter limit is exceeded.

    Returns
    -------
    int
        number of points in the output arrays

    Notes
    -----
    We use a cosine miter limit instead of maximum miter length for performance
    reasons. The cosine miter limit is related to the miter length by the
    following equation:

    .. math::

        c = \frac{1}{2 (l/2)^2} - 1 = \frac{2}{l^2} - 1
    """
    return _shape_numba_bridge('_calc_output_size', (direction_vectors, closed, cos_miter_limit, bevel), ('Calculate the size of the output arrays for the triangulation.\n\n    Parameters\n    ----------\n    direction_vectors : np.ndarray\n        Nx2 array of direction vectors along the path. The direction vectors\n        have norm 1, so computing the cosine between them is just the dot\n        product.\n    closed : bool\n        True if shape edge is a closed path.\n    cos_miter_limit : float\n        Miter limit which determines when to switch from a miter join to a\n        bevel join\n    bevel : bool\n        If True, a bevel join is always used.\n        If False, a bevel join will be used when the miter limit is exceeded.\n\n    Returns\n    -------\n    int\n        number of points in the output arrays\n\n    Notes\n    -----\n    We use a cosine miter limit instead of maximum miter length for performance\n    reasons. The cosine miter limit is related to the miter length by the\n    following equation:\n\n    .. math::\n\n        c = \x0crac{1}{2 (l/2)^2} - 1 = \x0crac{2}{l^2} - 1\n    ', 2, 0, 1), ('direction_vectors', 'closed', 'cos_miter_limit', 'bevel', 'n', 'point_count', 'bevel_count', 'start', 'stop', 'i', 'cos_angle'))


@njit(cache=True, inline='always')
def _set_centers_and_offsets(
    centers: np.ndarray,
    offsets: np.ndarray,
    triangles: np.ndarray,
    vertex: np.ndarray,
    vec1: np.ndarray,
    vec2: np.ndarray,
    half_vec1_len: float,
    half_vec2_len: float,
    j: int,
    cos_limit: float,
    always_bevel: bool,
) -> int:
    """Set the centers, offsets, and triangles for a given path and position.

    This function computes the positions of the vertices of the edge triangles
    at the given path vertex and towards the next path vertex, including, if
    needed, the miter join triangle overlapping the path vertex.

    If a miter join is needed, this function will add three triangles.
    Otherwise, it will add two triangles (dividing parallelogram of the
    next edge into two triangles).

    The positions of the triangle vertices are given as the path vertex
    (repeated once for each triangle vertex), and offset vectors from that
    vertex.

    The added triangles "optimistically" index into vertices past the vertices
    added in this iteration (indices j+3 and j+4).

    Parameters
    ----------
    centers : np.ndarray of float
        Mx2 output array of central coordinates of path triangles.
    offsets : np.ndarray of float
        Mx2 output array of the offsets from the central coordinates. Offsets
        need to be scaled by the line width and then added to the centers to
        generate the actual vertices of the triangulation.
    triangles : np.ndarray of int
        (M-2)x3 output array of the indices of the vertices that will form the
        triangles of the triangulation.
    vertex : np.ndarray
        The path vertex for which the centers, offsets and triangles are
        being calculated.
    vec1 : np.ndarray
        The norm-1 direction vector from the previous path vertex to the
        current path vertex.
    vec2 : np.ndarray
        The norm-1 direction vector from the current path vertex to the next
        path vertex.
    half_vec1_len : float
        Half the length of the segment between the previous path vertex and the
        current path vertex (used for bevel join calculation).
    half_vec2_len : float
        Half the length of the segment between the current path vertex and the
        next path vertex (used for bevel join calculation).
    j : int
        The current index in the ouput arrays.
    cos_limit : float
        Miter limit which determines when to switch from a miter join to a
        bevel join, to avoid very sharp shape angles.
    always_bevel : bool
        If True, a bevel join is always used.
        If False, a bevel join will only be used when the miter limit is
        exceeded.

    Returns
    -------
    int in {2, 3}
        The number of triangles, centers and offsets added to the arrays
    """
    return _shape_numba_bridge('_set_centers_and_offsets', (centers, offsets, triangles, vertex, vec1, vec2, half_vec1_len, half_vec2_len, j, cos_limit, always_bevel), ('Set the centers, offsets, and triangles for a given path and position.\n\n    This function computes the positions of the vertices of the edge triangles\n    at the given path vertex and towards the next path vertex, including, if\n    needed, the miter join triangle overlapping the path vertex.\n\n    If a miter join is needed, this function will add three triangles.\n    Otherwise, it will add two triangles (dividing parallelogram of the\n    next edge into two triangles).\n\n    The positions of the triangle vertices are given as the path vertex\n    (repeated once for each triangle vertex), and offset vectors from that\n    vertex.\n\n    The added triangles "optimistically" index into vertices past the vertices\n    added in this iteration (indices j+3 and j+4).\n\n    Parameters\n    ----------\n    centers : np.ndarray of float\n        Mx2 output array of central coordinates of path triangles.\n    offsets : np.ndarray of float\n        Mx2 output array of the offsets from the central coordinates. Offsets\n        need to be scaled by the line width and then added to the centers to\n        generate the actual vertices of the triangulation.\n    triangles : np.ndarray of int\n        (M-2)x3 output array of the indices of the vertices that will form the\n        triangles of the triangulation.\n    vertex : np.ndarray\n        The path vertex for which the centers, offsets and triangles are\n        being calculated.\n    vec1 : np.ndarray\n        The norm-1 direction vector from the previous path vertex to the\n        current path vertex.\n    vec2 : np.ndarray\n        The norm-1 direction vector from the current path vertex to the next\n        path vertex.\n    half_vec1_len : float\n        Half the length of the segment between the previous path vertex and the\n        current path vertex (used for bevel join calculation).\n    half_vec2_len : float\n        Half the length of the segment between the current path vertex and the\n        next path vertex (used for bevel join calculation).\n    j : int\n        The current index in the ouput arrays.\n    cos_limit : float\n        Miter limit which determines when to switch from a miter join to a\n        bevel join, to avoid very sharp shape angles.\n    always_bevel : bool\n        If True, a bevel join is always used.\n        If False, a bevel join will only be used when the miter limit is\n        exceeded.\n\n    Returns\n    -------\n    int in {2, 3}\n        The number of triangles, centers and offsets added to the arrays\n    ', 2, 0, 1, ('dtype',), 0.5, -1.0, 1.0, ('ccw',), 3, 4), ('centers', 'offsets', 'triangles', 'vertex', 'vec1', 'vec2', 'half_vec1_len', 'half_vec2_len', 'j', 'cos_limit', 'always_bevel', 'cos_angle', 'sin_angle', 'bevel', 'i', 'miter', 'scale_factor', 'sign', 'mag', 'clockwise', 'counterclockwise', 'invert'))


@njit(cache=True, inline='always')
def _normalize_triangle_orientation(
    triangles: np.ndarray, centers: np.ndarray, offsets: np.ndarray
) -> None:
    """Ensure vertices of all triangles are listed in the same orientation.

    In terms of napari's preferred coordinate frame (axis 0, y, is pointing
    down, axis 1, x is pointing right), this orientation is positive for
    anti-clockwise and negative for clockwise. This function normalises all
    triangle data in-place to have positive orientation.

    The orientation is useful to check if a point is inside a triangle.

    Parameters
    ----------
    triangles : np.ndarray
        (M-2)x3 array of the indices of the vertices that will form the
        triangles of the triangulation
    centers : np.ndarray
        Mx2 array central coordinates of path triangles.
    offsets : np.ndarray
        Mx2 array of the offsets to the central coordinates. Offsets need to
        be scaled by the line width and then added to the centers to
        generate the actual vertices of the triangulation
    """
    return _shape_numba_bridge('_normalize_triangle_orientation', (triangles, centers, offsets), ("Ensure vertices of all triangles are listed in the same orientation.\n\n    In terms of napari's preferred coordinate frame (axis 0, y, is pointing\n    down, axis 1, x is pointing right), this orientation is positive for\n    anti-clockwise and negative for clockwise. This function normalises all\n    triangle data in-place to have positive orientation.\n\n    The orientation is useful to check if a point is inside a triangle.\n\n    Parameters\n    ----------\n    triangles : np.ndarray\n        (M-2)x3 array of the indices of the vertices that will form the\n        triangles of the triangulation\n    centers : np.ndarray\n        Mx2 array central coordinates of path triangles.\n    offsets : np.ndarray\n        Mx2 array of the offsets to the central coordinates. Offsets need to\n        be scaled by the line width and then added to the centers to\n        generate the actual vertices of the triangulation\n    ", 0, 1, 2, None), ('triangles', 'centers', 'offsets', 'i', 'ti', 'p1', 'p2', 'p3'))


@njit(cache=True, inline='always')
def _orientation(
    p1: np.ndarray, p2: np.ndarray, p3: np.ndarray
) -> Orientation:
    """Compute the orientation of three points.

    In terms of napari's preferred coordinate frame (axis 0, y, is pointing
    down, axis 1, x is pointing right), this orientation is positive for
    anti-clockwise and negative for clockwise.

    Parameters
    ----------
    p1, p2, p3: np.ndarray, shape (2,)
        The three points to check.

    Returns
    -------
    float
        Positive if anti-clockwise, negative if clockwise, 0 if collinear.
    """
    return _shape_numba_bridge('_orientation', (p1, p2, p3), ("Compute the orientation of three points.\n\n    In terms of napari's preferred coordinate frame (axis 0, y, is pointing\n    down, axis 1, x is pointing right), this orientation is positive for\n    anti-clockwise and negative for clockwise.\n\n    Parameters\n    ----------\n    p1, p2, p3: np.ndarray, shape (2,)\n        The three points to check.\n\n    Returns\n    -------\n    float\n        Positive if anti-clockwise, negative if clockwise, 0 if collinear.\n    ", 0, 1), ('p1', 'p2', 'p3', 'val'))


@njit(cache=True, inline='always')
def _are_polar_angles_monotonic(
    vertices: CoordinateArray2D, centroid: np.ndarray
) -> bool:
    """Check if vertices are monastical in a polar system.

    The function checks if in a polar coordinate system
    with point 0 in centroid, the angle component
    of vertices coordinate is increasing.

    This is a second part of the polygon convexity check.

    Parameters
    ----------
    vertices: np.ndarray
        Array of vertex coordinates with shape (N, 2)

    Returns
    -------
    bool
        True if the polygon is simple, False otherwise
    """
    return _shape_numba_bridge('_are_polar_angles_monotonic', (vertices, centroid), ('Check if vertices are monastical in a polar system.\n\n    The function checks if in a polar coordinate system\n    with point 0 in centroid, the angle component\n    of vertices coordinate is increasing.\n\n    This is a second part of the polygon convexity check.\n\n    Parameters\n    ----------\n    vertices: np.ndarray\n        Array of vertex coordinates with shape (N, 2)\n\n    Returns\n    -------\n    bool\n        True if the polygon is simple, False otherwise\n    ', 0, 1, 0.0, 2, False, True), ('vertices', 'centroid', 'start_angle', 'prev_angle', 'i', 'angle'))


@njit(cache=True, inline='always')
def is_convex(vertices: CoordinateArray2D) -> bool:
    """Check if a polygon is convex.

    A polygon is convex when all its internal angles
    are less than or equal to 180 degrees and its edges don't
    self-intersect.

    This function determines convexity by:
    1. Checking if all non-collinear angles have the same
        orientation (clockwise or counterclockwise)
    2. Verifying that the vertices' polar angles are monotonic,
        relative to the polygon's centroid.

    - If the vertices are ordered counterclockwise, the order is
    reversed before checking.
    - Polygons with fewer than 3 vertices are not considered convex.
    - A triangle is always convex.

    Parameters
    ----------
    vertices : np.ndarray
        Array of vertex coordinates with shape (N, 2),
        where N is the number of vertices

    Returns
    -------
    bool
        True if the polygon is convex, False otherwise
    """
    return _shape_numba_bridge('is_convex', (vertices,), ("Check if a polygon is convex.\n\n    A polygon is convex when all its internal angles\n    are less than or equal to 180 degrees and its edges don't\n    self-intersect.\n\n    This function determines convexity by:\n    1. Checking if all non-collinear angles have the same\n        orientation (clockwise or counterclockwise)\n    2. Verifying that the vertices' polar angles are monotonic,\n        relative to the polygon's centroid.\n\n    - If the vertices are ordered counterclockwise, the order is\n    reversed before checking.\n    - Polygons with fewer than 3 vertices are not considered convex.\n    - A triangle is always convex.\n\n    Parameters\n    ----------\n    vertices : np.ndarray\n        Array of vertex coordinates with shape (N, 2),\n        where N is the number of vertices\n\n    Returns\n    -------\n    bool\n        True if the polygon is convex, False otherwise\n    ", 3, False, True, 0, 2, 1, ('dtype',), None, -1), ('vertices', 'orientation_', 'idx', 'n_points', 'orientation_set', 'p1', 'p2', 'p3', 'current_orientation', 'idx0', 'idx1', 'idx2', 'triangle_orientation', 'centroid'))


@njit(cache=True, inline='always')
def _direction_vec_and_half_length(
    path: np.ndarray, closed: bool
) -> tuple[np.ndarray, np.ndarray]:
    """Calculate the normal vector and its half-length.

    Parameters
    ----------
    path : np.ndarray
        Mx2 array representing path for which to calculate the direction
        vectors and the length of each segment.
        This function assumes that there is no point repetition in the path:
        consecutive points should not have identical coordinates.
    closed : bool
        True if the input path is closed (forms a loop): the edge from the
        last node in the path back to the first one is automatically inserted
        if so.

    Returns
    -------
    np.ndarray
        Mx2 array representing containing normal vectors of the path.
        If closed is False, the last vector has unspecified value.
    np.ndarray
        The array of limit of length of inner vectors in bevel joins.
        To reduce the graphical artifacts, the inner vectors are limited
        to half of the length of the adjacent egdes in path.
    """
    return _shape_numba_bridge('_direction_vec_and_half_length', (path, closed), ('Calculate the normal vector and its half-length.\n\n    Parameters\n    ----------\n    path : np.ndarray\n        Mx2 array representing path for which to calculate the direction\n        vectors and the length of each segment.\n        This function assumes that there is no point repetition in the path:\n        consecutive points should not have identical coordinates.\n    closed : bool\n        True if the input path is closed (forms a loop): the edge from the\n        last node in the path back to the first one is automatically inserted\n        if so.\n\n    Returns\n    -------\n    np.ndarray\n        Mx2 array representing containing normal vectors of the path.\n        If closed is False, the last vector has unspecified value.\n    np.ndarray\n        The array of limit of length of inner vectors in bevel joins.\n        To reduce the graphical artifacts, the inner vectors are limited\n        to half of the length of the adjacent egdes in path.\n    ', ('dtype',), 1, 0, 2, 0.5), ('path', 'closed', 'normals', 'vec_len_arr', 'i', 'vec_diff'))


@njit(cache=True, inline='always')
def _generate_2D_edge_meshes_loop(
    path: np.ndarray,
    closed: bool,
    cos_limit: float,
    bevel: bool,
    direction_vectors: np.ndarray,
    bevel_limit_array: np.ndarray,
    centers: np.ndarray,
    offsets: np.ndarray,
    triangles: np.ndarray,
) -> None:
    """Main loop for :py:func:`generate_2D_edge_meshes`.

    Parameters
    ----------
    path : np.ndarray
        Nx2 array of central coordinates of path to be triangulated
    closed : bool
        Bool which determines if the path is closed or not
    cos_limit : float
        Miter limit which determines when to switch from a miter join to a
        bevel join
    bevel : bool
        Bool which if True causes a bevel join to always be used. If False
        a bevel join will only be used when the miter limit is exceeded
    direction_vectors : np.ndarray
        Nx2 array of normal vectors of the path
    bevel_limit_array : np.ndarray
        The array of limit of length of inner vectors in bevel joins.
        To reduce the graphical artifacts, the inner vectors are limited
        to half of the length of the adjacent edges in path.
    centers : np.ndarray
        Mx2 array to put central coordinates of path triangles.
    offsets : np.ndarray
        Mx2 array to put the offsets to the central coordinates that need to
        be scaled by the line width and then added to the centers to
        generate the actual vertices of the triangulation
    triangles : np.ndarray
        (M-2)x3 array to put the indices of the vertices that will form the
        triangles of the triangulation
    """
    return _shape_numba_bridge('_generate_2D_edge_meshes_loop', (path, closed, cos_limit, bevel, direction_vectors, bevel_limit_array, centers, offsets, triangles), ('Main loop for :py:func:`generate_2D_edge_meshes`.\n\n    Parameters\n    ----------\n    path : np.ndarray\n        Nx2 array of central coordinates of path to be triangulated\n    closed : bool\n        Bool which determines if the path is closed or not\n    cos_limit : float\n        Miter limit which determines when to switch from a miter join to a\n        bevel join\n    bevel : bool\n        Bool which if True causes a bevel join to always be used. If False\n        a bevel join will only be used when the miter limit is exceeded\n    direction_vectors : np.ndarray\n        Nx2 array of normal vectors of the path\n    bevel_limit_array : np.ndarray\n        The array of limit of length of inner vectors in bevel joins.\n        To reduce the graphical artifacts, the inner vectors are limited\n        to half of the length of the adjacent edges in path.\n    centers : np.ndarray\n        Mx2 array to put central coordinates of path triangles.\n    offsets : np.ndarray\n        Mx2 array to put the offsets to the central coordinates that need to\n        be scaled by the line width and then added to the centers to\n        generate the actual vertices of the triangulation\n    triangles : np.ndarray\n        (M-2)x3 array to put the indices of the vertices that will form the\n        triangles of the triangulation\n    ', 0, None, 2, 0.5, True, ('ccw',), 1, (0, 1, 2), (1, 2, 3), -1, -2), ('path', 'closed', 'cos_limit', 'bevel', 'direction_vectors', 'bevel_limit_array', 'centers', 'offsets', 'triangles', 'j', 'i'))


@njit(cache=True, inline='always')
def _cut_end_if_repetition(path: np.ndarray) -> np.ndarray:
    """Cut the last point of the path if it is the same as the second to last point.

    Parameters
    ----------
    path : np.ndarray
        Nx2 array of central coordinates of path to be triangulated

    Returns
    -------
    np.ndarray
        Nx2 or (N-1)x2  array of central coordinates of path to be triangulated
    """
    return _shape_numba_bridge('_cut_end_if_repetition', (path,), ('Cut the last point of the path if it is the same as the second to last point.\n\n    Parameters\n    ----------\n    path : np.ndarray\n        Nx2 array of central coordinates of path to be triangulated\n\n    Returns\n    -------\n    np.ndarray\n        Nx2 or (N-1)x2  array of central coordinates of path to be triangulated\n    ', ('dtype',), -1, -2, None), ('path', 'path_'))


# Note: removing this decorator will double execution time.
@njit(cache=True)
def generate_2D_edge_meshes(
    path: np.ndarray,
    closed: bool = False,
    limit: float = 3.0,
    bevel: bool = False,
) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Determines the triangulation of a path in 2D.

    The resulting `offsets`
    can be multiplied by a `width` scalar and be added to the resulting
    `centers` to generate the vertices of the triangles for the triangulation,
    i.e. `vertices = centers + width*offsets`. By using the `centers` and
    `offsets` representation, the computed triangulation can be
    independent of the line width.

    Parameters
    ----------
    path : np.ndarray
        Nx2 array of central coordinates of path to be triangulated
    closed : bool
        Bool which determines if the path is closed or not
    limit : float
        Miter limit which determines when to switch from a miter join to a
        bevel join
    bevel : bool
        Bool which if True causes a bevel join to always be used. If False
        a bevel join will only be used when the miter limit is exceeded

    Returns
    -------
    centers : np.ndarray
        Mx2 array central coordinates of path triangles.
    offsets : np.ndarray
        Mx2 array of the offsets to the central coordinates that need to
        be scaled by the line width and then added to the centers to
        generate the actual vertices of the triangulation
    triangles : np.ndarray
        (M-2)x3 array of the indices of the vertices that will form the
        triangles of the triangulation
    """
    return _shape_numba_bridge('generate_2D_edge_meshes', (path, closed, limit, bevel), ('Determines the triangulation of a path in 2D.\n\n    The resulting `offsets`\n    can be multiplied by a `width` scalar and be added to the resulting\n    `centers` to generate the vertices of the triangles for the triangulation,\n    i.e. `vertices = centers + width*offsets`. By using the `centers` and\n    `offsets` representation, the computed triangulation can be\n    independent of the line width.\n\n    Parameters\n    ----------\n    path : np.ndarray\n        Nx2 array of central coordinates of path to be triangulated\n    closed : bool\n        Bool which determines if the path is closed or not\n    limit : float\n        Miter limit which determines when to switch from a miter join to a\n        bevel join\n    bevel : bool\n        Bool which if True causes a bevel join to always be used. If False\n        a bevel join will only be used when the miter limit is exceeded\n\n    Returns\n    -------\n    centers : np.ndarray\n        Mx2 array central coordinates of path triangles.\n    offsets : np.ndarray\n        Mx2 array of the offsets to the central coordinates that need to\n        be scaled by the line width and then added to the centers to\n        generate the actual vertices of the triangulation\n    triangles : np.ndarray\n        (M-2)x3 array of the indices of the vertices that will form the\n        triangles of the triangulation\n    ', 2, (4, 2), ('dtype',), 0, 1, 3, (2, 3), (0, 1, 3), (1, 3, 2), 1.0), ('path', 'closed', 'limit', 'bevel', 'centers', 'triangles', 'cos_limit', 'direction_vectors', 'bevel_limit_array', 'point_count', 'offsets'))


@njit(cache=True)
def remove_path_duplicates(path: np.ndarray, closed: bool) -> np.ndarray:
    """Remove consecutive duplicates from a path.

    Parameters
    ----------
    path : np.ndarray
        Nx2 or Nx3 array of central coordinates of path to be deduplicated

    Returns
    -------
    np.ndarray
        Nx2 or Nx3 array of central coordinates of deduplicated path
    """
    return _shape_numba_bridge('remove_path_duplicates', (path, closed), ('Remove consecutive duplicates from a path.\n\n    Parameters\n    ----------\n    path : np.ndarray\n        Nx2 or Nx3 array of central coordinates of path to be deduplicated\n\n    Returns\n    -------\n    np.ndarray\n        Nx2 or Nx3 array of central coordinates of deduplicated path\n    ', 2, 0, 1, -1, ('dtype',)), ('path', 'closed', 'dup_count', 'i', 'target_len', 'new_path', 'index'))


@njit(cache=True)
def create_box_from_bounding(bounding_box: np.ndarray) -> np.ndarray:
    """Creates the axis aligned interaction box of a bounding box

    Parameters
    ----------
    bounding_box : np.ndarray
        2x2 array of the bounding box. The first row is the minimum values and
        the second row is the maximum values

    Returns
    -------
    box : np.ndarray
        9x2 array of vertices of the interaction box. The first 8 points are
        the corners and midpoints of the box in clockwise order starting in the
        upper-left corner. The last point is the center of the box
    """
    return _shape_numba_bridge('create_box_from_bounding', (bounding_box,), ('Creates the axis aligned interaction box of a bounding box\n\n    Parameters\n    ----------\n    bounding_box : np.ndarray\n        2x2 array of the bounding box. The first row is the minimum values and\n        the second row is the maximum values\n\n    Returns\n    -------\n    box : np.ndarray\n        9x2 array of vertices of the interaction box. The first 8 points are\n        the corners and midpoints of the box in clockwise order starting in the\n        upper-left corner. The last point is the center of the box\n    ', (0, 0), (1, 0), (0, 1), (1, 1), (9, 2), ('dtype',), 0, 2, 1, 3, 4, 5, 6, 7, 8), ('bounding_box', 'x_min', 'x_max', 'y_min', 'y_max', 'result'))


@overload
def reconstruct_polygons_from_edges(
    vertices: CoordinateArray2D, edges: EdgeArray
) -> list[CoordinateArray2D]: ...


@overload
def reconstruct_polygons_from_edges(
    vertices: CoordinateArray3D, edges: EdgeArray
) -> list[CoordinateArray3D]: ...


@njit(cache=True)
def reconstruct_polygons_from_edges(
    vertices: CoordinateArray, edges: EdgeArray
) -> list[CoordinateArray2D] | list[CoordinateArray3D]:
    """Reconstruct polygons from vertices and edges.

    This function takes the output from `normalize_vertices_and_edges` — which
    is a vertex set and a list of possibly-disjoint edges — and produces a list
    of independent polygons.

    The algorithm reconstructs sub polygons using recursion.
    Starting from the first edge, it traverses the graph until it reaches starting vertex.
    After it, the algorithm iterates until reach the first non visited edge.
    So the implementation has O(M) complexity.

    Parameters
    ----------
    vertices : np.ndarray
        Array of vertex coordinates with shape (N, 2) or (N, 3)
        Cannot contain repeated vertices.
    edges : np.ndarray
        Array of edge indices with shape (M, 2)
        Cannot contain repeated edges.


    Returns
    -------
    list of np.ndarray
        List of polygons, where each polygon is an array of vertex coordinates
    """
    return _shape_numba_bridge('reconstruct_polygons_from_edges', (vertices, edges), ('Reconstruct polygons from vertices and edges.\n\n    This function takes the output from `normalize_vertices_and_edges` — which\n    is a vertex set and a list of possibly-disjoint edges — and produces a list\n    of independent polygons.\n\n    The algorithm reconstructs sub polygons using recursion.\n    Starting from the first edge, it traverses the graph until it reaches starting vertex.\n    After it, the algorithm iterates until reach the first non visited edge.\n    So the implementation has O(M) complexity.\n\n    Parameters\n    ----------\n    vertices : np.ndarray\n        Array of vertex coordinates with shape (N, 2) or (N, 3)\n        Cannot contain repeated vertices.\n    edges : np.ndarray\n        Array of edge indices with shape (M, 2)\n        Cannot contain repeated edges.\n\n\n    Returns\n    -------\n    list of np.ndarray\n        List of polygons, where each polygon is an array of vertex coordinates\n    ', 0, ('dtype',), 1, True, False), ('vertices', 'edges', 'n_edges', 'visited', 'n_vertices', 'incident', '_', 'i', 'start', 'end', 'polygons', 'poly', 'start_v', 'current_v', 'closed', 'found', 'edge_idx', 'a', 'b', 'next_v', 'polygon'))


@njit(cache=True)
def normalize_vertices_and_edges(
    vertices: CoordinateArray2D, close: bool = False
) -> tuple[
    CoordinateArray2D,
    np.ndarray[tuple[int, Literal[2]], np.dtype[np.int64]],
]:
    """Get a list of edges that must be in triangulation for a path or polygon.

    This function ensures that:

    - no nodes are repeated, as this can cause problems with triangulation
      algorithms.
    - edges that appear twice are discarded. This allows representation of
      polygons with holes in them.

    Parameters
    ----------
    vertices: np.ndarray[np.floating], shape (N, 2)
        The 2D coordinates of the polygon's vertices. They are expected to be
        in the order in which they appear in the polygon: that is, vertices
        that follow each other are expected to be connected to each other. The
        exception is if the same edge is visited twice (in any direction): such
        edges are removed.

        Holes are expected to be represented by a polygon embedded within the
        larger polygon but winding in the opposite direction.

    close: bool
        Whether to close the polygon or treat it as a path. Note: this argument
        has no effect if the last vertex is equal to the first one — then the
        closing is explicit.

    Returns
    -------
    new_vertices: np.ndarray[np.floating], shape (M, 2)
        Vertices with duplicate nodes removed.
    edges: np.ndarray[np.intp], shape (P, 2)
        List of edges in the polygon, expressed as an array of pairs of vertex
        indices. This is usually [(0, 1), (1, 2), ... (N-1, 0)], but edges
        that are visited twice are removed.
    """
    return _shape_numba_bridge('normalize_vertices_and_edges', (vertices, close), ("Get a list of edges that must be in triangulation for a path or polygon.\n\n    This function ensures that:\n\n    - no nodes are repeated, as this can cause problems with triangulation\n      algorithms.\n    - edges that appear twice are discarded. This allows representation of\n      polygons with holes in them.\n\n    Parameters\n    ----------\n    vertices: np.ndarray[np.floating], shape (N, 2)\n        The 2D coordinates of the polygon's vertices. They are expected to be\n        in the order in which they appear in the polygon: that is, vertices\n        that follow each other are expected to be connected to each other. The\n        exception is if the same edge is visited twice (in any direction): such\n        edges are removed.\n\n        Holes are expected to be represented by a polygon embedded within the\n        larger polygon but winding in the opposite direction.\n\n    close: bool\n        Whether to close the polygon or treat it as a path. Note: this argument\n        has no effect if the last vertex is equal to the first one — then the\n        closing is explicit.\n\n    Returns\n    -------\n    new_vertices: np.ndarray[np.floating], shape (M, 2)\n        Vertices with duplicate nodes removed.\n    edges: np.ndarray[np.intp], shape (P, 2)\n        List of edges in the polygon, expressed as an array of pairs of vertex\n        indices. This is usually [(0, 1), (1, 2), ... (N-1, 0)], but edges\n        that are visited twice are removed.\n    ", (0, 0), (-1, 0), (0, 1), (-1, 1), None, -1, True, 0, 1, 2, ('dtype',)), ('vertices', 'close', 'vertex_to_idx', 'new_vertices', 'edges', 'prev_idx', 'i', 'vertex', 'vertex_t', 'current_idx', 'edge', 'idx', 'new_vertices_array', 'edges_array'))


def _shape_native_pyfunc_generate_2D_edge_meshes(path: np.ndarray, closed: bool=False, limit: float=3.0, bevel: bool=False) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Determines the triangulation of a path in 2D.

    The resulting `offsets`
    can be multiplied by a `width` scalar and be added to the resulting
    `centers` to generate the vertices of the triangles for the triangulation,
    i.e. `vertices = centers + width*offsets`. By using the `centers` and
    `offsets` representation, the computed triangulation can be
    independent of the line width.

    Parameters
    ----------
    path : np.ndarray
        Nx2 array of central coordinates of path to be triangulated
    closed : bool
        Bool which determines if the path is closed or not
    limit : float
        Miter limit which determines when to switch from a miter join to a
        bevel join
    bevel : bool
        Bool which if True causes a bevel join to always be used. If False
        a bevel join will only be used when the miter limit is exceeded

    Returns
    -------
    centers : np.ndarray
        Mx2 array central coordinates of path triangles.
    offsets : np.ndarray
        Mx2 array of the offsets to the central coordinates that need to
        be scaled by the line width and then added to the centers to
        generate the actual vertices of the triangulation
    triangles : np.ndarray
        (M-2)x3 array of the indices of the vertices that will form the
        triangles of the triangulation
    """
    locals_owner = _native.shape_accel_locals(('path', 'closed', 'limit', 'bevel', 'centers', 'triangles', 'cos_limit', 'direction_vectors', 'bevel_limit_array', 'point_count', 'offsets'), {'path': path, 'closed': closed, 'limit': limit, 'bevel': bevel}, {})
    del path, closed, limit, bevel
    return _native.shape_accel_numba_edges(_native.shape_accel_namespace(), ('Determines the triangulation of a path in 2D.\n\n    The resulting `offsets`\n    can be multiplied by a `width` scalar and be added to the resulting\n    `centers` to generate the vertices of the triangles for the triangulation,\n    i.e. `vertices = centers + width*offsets`. By using the `centers` and\n    `offsets` representation, the computed triangulation can be\n    independent of the line width.\n\n    Parameters\n    ----------\n    path : np.ndarray\n        Nx2 array of central coordinates of path to be triangulated\n    closed : bool\n        Bool which determines if the path is closed or not\n    limit : float\n        Miter limit which determines when to switch from a miter join to a\n        bevel join\n    bevel : bool\n        Bool which if True causes a bevel join to always be used. If False\n        a bevel join will only be used when the miter limit is exceeded\n\n    Returns\n    -------\n    centers : np.ndarray\n        Mx2 array central coordinates of path triangles.\n    offsets : np.ndarray\n        Mx2 array of the offsets to the central coordinates that need to\n        be scaled by the line width and then added to the centers to\n        generate the actual vertices of the triangulation\n    triangles : np.ndarray\n        (M-2)x3 array of the indices of the vertices that will form the\n        triangles of the triangulation\n    ', 2, (4, 2), ('dtype',), 0, 1, 3, (2, 3), (0, 1, 3), (1, 3, 2), 1.0), locals_owner)

generate_2D_edge_meshes = _native.shape_accel_numba_install_py_func(generate_2D_edge_meshes, _shape_native_pyfunc_generate_2D_edge_meshes)


def _shape_native_pyfunc_normalize_vertices_and_edges(vertices: CoordinateArray2D, close: bool=False) -> tuple[CoordinateArray2D, np.ndarray[tuple[int, Literal[2]], np.dtype[np.int64]]]:
    """Get a list of edges that must be in triangulation for a path or polygon.

    This function ensures that:

    - no nodes are repeated, as this can cause problems with triangulation
      algorithms.
    - edges that appear twice are discarded. This allows representation of
      polygons with holes in them.

    Parameters
    ----------
    vertices: np.ndarray[np.floating], shape (N, 2)
        The 2D coordinates of the polygon's vertices. They are expected to be
        in the order in which they appear in the polygon: that is, vertices
        that follow each other are expected to be connected to each other. The
        exception is if the same edge is visited twice (in any direction): such
        edges are removed.

        Holes are expected to be represented by a polygon embedded within the
        larger polygon but winding in the opposite direction.

    close: bool
        Whether to close the polygon or treat it as a path. Note: this argument
        has no effect if the last vertex is equal to the first one — then the
        closing is explicit.

    Returns
    -------
    new_vertices: np.ndarray[np.floating], shape (M, 2)
        Vertices with duplicate nodes removed.
    edges: np.ndarray[np.intp], shape (P, 2)
        List of edges in the polygon, expressed as an array of pairs of vertex
        indices. This is usually [(0, 1), (1, 2), ... (N-1, 0)], but edges
        that are visited twice are removed.
    """
    locals_owner = _native.shape_accel_locals(('vertices', 'close', 'vertex_to_idx', 'new_vertices', 'edges', 'prev_idx', 'i', 'vertex', 'vertex_t', 'current_idx', 'edge', 'idx', 'new_vertices_array', 'edges_array'), {'vertices': vertices, 'close': close}, {})
    del vertices, close
    return _native.shape_accel_numba_normalize(_native.shape_accel_namespace(), ("Get a list of edges that must be in triangulation for a path or polygon.\n\n    This function ensures that:\n\n    - no nodes are repeated, as this can cause problems with triangulation\n      algorithms.\n    - edges that appear twice are discarded. This allows representation of\n      polygons with holes in them.\n\n    Parameters\n    ----------\n    vertices: np.ndarray[np.floating], shape (N, 2)\n        The 2D coordinates of the polygon's vertices. They are expected to be\n        in the order in which they appear in the polygon: that is, vertices\n        that follow each other are expected to be connected to each other. The\n        exception is if the same edge is visited twice (in any direction): such\n        edges are removed.\n\n        Holes are expected to be represented by a polygon embedded within the\n        larger polygon but winding in the opposite direction.\n\n    close: bool\n        Whether to close the polygon or treat it as a path. Note: this argument\n        has no effect if the last vertex is equal to the first one — then the\n        closing is explicit.\n\n    Returns\n    -------\n    new_vertices: np.ndarray[np.floating], shape (M, 2)\n        Vertices with duplicate nodes removed.\n    edges: np.ndarray[np.intp], shape (P, 2)\n        List of edges in the polygon, expressed as an array of pairs of vertex\n        indices. This is usually [(0, 1), (1, 2), ... (N-1, 0)], but edges\n        that are visited twice are removed.\n    ", (0, 0), (-1, 0), (0, 1), (-1, 1), None, -1, True, 0, 1, 2, ('dtype',)), locals_owner)

normalize_vertices_and_edges = _native.shape_accel_numba_install_py_func(normalize_vertices_and_edges, _shape_native_pyfunc_normalize_vertices_and_edges)

