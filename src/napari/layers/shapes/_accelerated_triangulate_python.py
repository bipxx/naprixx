from collections import defaultdict
from typing import Literal, overload

import numpy as np
import numpy.typing as npt

from napari.layers.shapes.shape_types import (
    CoordinateArray,
    CoordinateArray2D,
    CoordinateArray3D,
    EdgeArray,
)
from napari.layers.utils.layer_utils import segment_normal

from naparixx import _native


def remove_path_duplicates_py(
    data: npt.NDArray[np.float32], closed: bool
) -> npt.NDArray[np.float32]:
    # We add the first data point at the end to get the same length bool
    # array as the data, and also to work on closed shapes; the last value
    # in the diff array compares the last and first vertex.
    locals_owner = _native.shape_accel_locals(('data', 'closed', 'diff', 'dup', 'indices'), {'data': data, 'closed': closed}, {})
    del data, closed
    return _native.shape_accel_remove_duplicates_py(_native.shape_accel_namespace(), (None, 0, 1, ('axis',), -1, True, False, -2), locals_owner)


def _mirror_point(x: np.ndarray, y: np.ndarray) -> np.ndarray:
    locals_owner = _native.shape_accel_locals(('x', 'y'), {'x': x, 'y': y}, {})
    del x, y
    return _native.shape_accel_mirror(_native.shape_accel_namespace(), (None, 2), locals_owner)


def _sign_nonzero(x: np.ndarray) -> npt.NDArray[np.int32]:
    locals_owner = _native.shape_accel_locals(('x', 'y'), {'x': x}, {})
    del x
    return _native.shape_accel_sign_nonzero(_native.shape_accel_namespace(), (None, 1, 0), locals_owner)


def _sign_cross(x: np.ndarray, y: np.ndarray) -> npt.NDArray[np.int32]:
    """sign of cross product (faster for 2d)"""
    locals_owner = _native.shape_accel_locals(('x', 'y'), {'x': x, 'y': y}, {})
    del x, y
    return _native.shape_accel_sign_cross(_native.shape_accel_namespace(), ('sign of cross product (faster for 2d)', 1, 2, None, 0, 3), locals_owner)


def generate_2D_edge_meshes_py(
    path: npt.NDArray[np.float32],
    closed: bool = False,
    limit: float = 3,
    bevel: bool = False,
) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Determines the triangulation of a path in 2D. The resulting `offsets`
    can be multiplied by a `width` scalar and be added to the resulting
    `centers` to generate the vertices of the triangles for the triangulation,
    i.e. `vertices = centers + width*offsets`. Using the `centers` and
    `offsets` representation thus allows for the computed triangulation to be
    independent of the line width.

    Parameters
    ----------
    path : np.ndarray
        Nx2 or Nx3 array of central coordinates of path to be triangulated
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
        Mx2 or Mx3 array central coordinates of path triangles.
    offsets : np.ndarray
        Mx2 or Mx3 array of the offsets to the central coordinates that need to
        be scaled by the line width and then added to the centers to
        generate the actual vertices of the triangulation
    triangles : np.ndarray
        Px3 array of the indices of the vertices that will form the
        triangles of the triangulation
    """
    centers: object
    offsets: object
    triangles: object
    locals_owner = _native.shape_accel_locals(('path', 'closed', 'limit', 'bevel', '_ext_point1', '_ext_point2', 'full_path', 'full_normals', 'miters', 'mf_dot', 'miter_lengths_squared', 'miter_signs', 'triangles0', 'idx_bevel', 'idx_offset', 'idx_bevel_outside', 'idx_bevel_inside', 'sign_bevel', '_nonspecial', 'centers_bevel', 'offsets_bevel', 'n_centers', 'triangles_bevel', 'a', 'b', 'c', 'flip_idx', 'centers', 'offsets', 'triangles'), {'path': path, 'closed': closed, 'limit': limit, 'bevel': bevel}, {'centers': lambda: centers, 'offsets': lambda: offsets, 'triangles': lambda: triangles})  # noqa: F821 - genuine unbound source cells
    del path, closed, limit, bevel
    def generator_factory(iterator):
        yield from iterator
    return _native.shape_accel_edges_py(_native.shape_accel_namespace(), ('Determines the triangulation of a path in 2D. The resulting `offsets`\n    can be multiplied by a `width` scalar and be added to the resulting\n    `centers` to generate the vertices of the triangles for the triangulation,\n    i.e. `vertices = centers + width*offsets`. Using the `centers` and\n    `offsets` representation thus allows for the computed triangulation to be\n    independent of the line width.\n\n    Parameters\n    ----------\n    path : np.ndarray\n        Nx2 or Nx3 array of central coordinates of path to be triangulated\n    closed : bool\n        Bool which determines if the path is closed or not\n    limit : float\n        Miter limit which determines when to switch from a miter join to a\n        bevel join\n    bevel : bool\n        Bool which if True causes a bevel join to always be used. If False\n        a bevel join will only be used when the miter limit is exceeded\n\n    Returns\n    -------\n    centers : np.ndarray\n        Mx2 or Mx3 array central coordinates of path triangles.\n    offsets : np.ndarray\n        Mx2 or Mx3 array of the offsets to the central coordinates that need to\n        be scaled by the line width and then added to the centers to\n        generate the actual vertices of the triangulation\n    triangles : np.ndarray\n        Px3 array of the indices of the vertices that will form the\n        triangles of the triangulation\n    ', ('dtype',), 0, -2, 1, -1, ('axis',), None, 0.5, 'ij,ij->i', 1e-10, ('out', 'where'), 2, (0, 1, 3), (0, 3, 2), -0.5, (0, 1, 2), None, 3), locals_owner, generator_factory)


def create_box_from_bounding_py(bounding_box: npt.NDArray) -> npt.NDArray:
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
    locals_owner = _native.shape_accel_locals(('bounding_box', 'tl', 'br', 'tr', 'bl'), {'bounding_box': bounding_box}, {})
    del bounding_box
    return _native.shape_accel_box_py(_native.shape_accel_namespace(), ('Creates the axis aligned interaction box of a bounding box\n\n    Parameters\n    ----------\n    bounding_box : np.ndarray\n        2x2 array of the bounding box. The first row is the minimum values and\n        the second row is the maximum values\n\n    Returns\n    -------\n    box : np.ndarray\n        9x2 array of vertices of the interaction box. The first 8 points are\n        the corners and midpoints of the box in clockwise order starting in the\n        upper-left corner. The last point is the center of the box\n    ', ((0, 0), (0, 1)), ((1, 1), (0, 1)), ((1, 0), (0, 1)), ((0, 1), (0, 1)), 2), locals_owner)


@overload
def reconstruct_polygons_from_edges_py(
    vertices: CoordinateArray2D, edges: EdgeArray
) -> list[CoordinateArray2D]: ...


@overload
def reconstruct_polygons_from_edges_py(
    vertices: CoordinateArray3D, edges: EdgeArray
) -> list[CoordinateArray3D]: ...


def reconstruct_polygons_from_edges_py(
    vertices: CoordinateArray, edges: EdgeArray
) -> list[CoordinateArray2D] | list[CoordinateArray3D]:
    """
    Reconstruct polygons from vertices and edges.

    Parameters
    ----------
    vertices : np.ndarray
        Array of vertex coordinates with shape (N, 2) or (N, 3)
    edges : np.ndarray
        Array of edge indices with shape (M, 2)

    Returns
    -------
    list of np.ndarray
        List of polygons, where each polygon is an array of vertex coordinates
    """
    locals_owner = _native.shape_accel_locals(('vertices', 'edges', 'adjacency', 'edge_', 'v1', 'v2', 'edge', 'unvisited_edges', 'polygons', 'current_vertex', 'start_vertex', 'polygon_indices', 'next_vertex', 'neighbor', 'polygon_vertices'), {'vertices': vertices, 'edges': edges}, {})
    del vertices, edges
    return _native.shape_accel_reconstruct_py(_native.shape_accel_namespace(), ('\n    Reconstruct polygons from vertices and edges.\n\n    Parameters\n    ----------\n    vertices : np.ndarray\n        Array of vertex coordinates with shape (N, 2) or (N, 3)\n    edges : np.ndarray\n        Array of edge indices with shape (M, 2)\n\n    Returns\n    -------\n    list of np.ndarray\n        List of polygons, where each polygon is an array of vertex coordinates\n    ', 0, 1, None), locals_owner)


def normalize_vertices_and_edges_py(
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
    locals_owner = _native.shape_accel_locals(('vertices', 'close', 'vertex_to_idx', 'new_vertices', 'edges', 'prev_idx', 'i', 'vertex', 'vertex_t', 'current_idx', 'edge', 'idx', 'new_vertices_array', 'edges_array'), {'vertices': vertices, 'close': close}, {})
    del vertices, close
    return _native.shape_accel_normalize_py(_native.shape_accel_namespace(), ("Get a list of edges that must be in triangulation for a path or polygon.\n\n    This function ensures that:\n\n    - no nodes are repeated, as this can cause problems with triangulation\n      algorithms.\n    - edges that appear twice are discarded. This allows representation of\n      polygons with holes in them.\n\n    Parameters\n    ----------\n    vertices: np.ndarray[np.floating], shape (N, 2)\n        The 2D coordinates of the polygon's vertices. They are expected to be\n        in the order in which they appear in the polygon: that is, vertices\n        that follow each other are expected to be connected to each other. The\n        exception is if the same edge is visited twice (in any direction): such\n        edges are removed.\n\n        Holes are expected to be represented by a polygon embedded within the\n        larger polygon but winding in the opposite direction.\n\n    close: bool\n        Whether to close the polygon or treat it as a path. Note: this argument\n        has no effect if the last vertex is equal to the first one — then the\n        closing is explicit.\n\n    Returns\n    -------\n    new_vertices: np.ndarray[np.floating], shape (M, 2)\n        Vertices with duplicate nodes removed.\n    edges: np.ndarray[np.intp], shape (P, 2)\n        List of edges in the polygon, expressed as an array of pairs of vertex\n        indices. This is usually [(0, 1), (1, 2), ... (N-1, 0)], but edges\n        that are visited twice are removed.\n    ", 0, -1, None, True, 1, (0, 0), ('dtype',)), locals_owner)


def _are_polar_angles_monotonic(poly: npt.NDArray, orientation_: int) -> bool:
    """Check whether a polygon with same oriented angles between successive edges is simple.

    A polygon is considered simple if its edges do not intersect themselves.
    This is determined by checking whether the angles between successive
    vertices, measured from the centroid, increase consistently around the
    polygon in a counterclockwise (or clockwise) direction. If the angles
    from one vertex to the next increase, the polygon is simple.

    Parameters
    ----------
    poly: numpy array of floats, shape (N, 2)
        polygon vertices, in order.
    orientation_: int
        The orientation of the polygon. A value of `1` indicates clockwise
        and `-1` indicates counterclockwise orientation.

    Returns
    -------
    bool:
        if all angles are increasing return True, otherwise False
    """
    locals_owner = _native.shape_accel_locals(('poly', 'orientation_', 'centroid', 'angles', 'shifted_angles'), {'poly': poly, 'orientation_': orientation_}, {})
    del poly, orientation_
    return _native.shape_accel_monotonic_py(_native.shape_accel_namespace(), ('Check whether a polygon with same oriented angles between successive edges is simple.\n\n    A polygon is considered simple if its edges do not intersect themselves.\n    This is determined by checking whether the angles between successive\n    vertices, measured from the centroid, increase consistently around the\n    polygon in a counterclockwise (or clockwise) direction. If the angles\n    from one vertex to the next increase, the polygon is simple.\n\n    Parameters\n    ----------\n    poly: numpy array of floats, shape (N, 2)\n        polygon vertices, in order.\n    orientation_: int\n        The orientation of the polygon. A value of `1` indicates clockwise\n        and `-1` indicates counterclockwise orientation.\n\n    Returns\n    -------\n    bool:\n        if all angles are increasing return True, otherwise False\n    ', 0, 3, False, 1, None, -1, ('axis',), 2), locals_owner)


def orientation(p: np.ndarray, q: np.ndarray, r: np.ndarray) -> np.ndarray:
    """Determines oritentation of ordered triplet (p, q, r)

    Parameters
    ----------
    p : (2,) array
        Array of first point of triplet
    q : (2,) array
        Array of second point of triplet
    r : (2,) array
        Array of third point of triplet

    Returns
    -------
    val : int
        One of (-1, 0, 1). 0 if p, q, r are collinear, 1 if clockwise, and -1
        if counterclockwise. (These definitions assume napari's default
        reference frame, in which the 0th axis is y pointing down, and the
        1st axis is x, pointing right.)
    """
    locals_owner = _native.shape_accel_locals(('p', 'q', 'r', 'val'), {'p': p, 'q': q, 'r': r}, {})
    del p, q, r
    return _native.shape_accel_orientation_py(_native.shape_accel_namespace(), ("Determines oritentation of ordered triplet (p, q, r)\n\n    Parameters\n    ----------\n    p : (2,) array\n        Array of first point of triplet\n    q : (2,) array\n        Array of second point of triplet\n    r : (2,) array\n        Array of third point of triplet\n\n    Returns\n    -------\n    val : int\n        One of (-1, 0, 1). 0 if p, q, r are collinear, 1 if clockwise, and -1\n        if counterclockwise. (These definitions assume napari's default\n        reference frame, in which the 0th axis is y pointing down, and the\n        1st axis is x, pointing right.)\n    ", 1, 0), locals_owner)


def vectorized_orientation(
    p: npt.NDArray, q: npt.NDArray, r: npt.NDArray
) -> npt.NDArray:
    """Determines orientation of ordered triplet(s) (p, q, r).

    This implementation is vectorized and works for arrays of points.

    Parameters
    ----------
    p : (..., 2) array
        Array of first points of triplets.
    q : (..., 2) array
        Array of second points of triplets.
    r : (..., 2) array
        Array of third points of triplets.

    Returns
    -------
    val : (...) array of int
        An array of values, one for each triplet. 0 if p, q, r are collinear,
        1 if clockwise, and -1 if counterclockwise. (These definitions assume
        napari's default reference frame, in which the 0th axis is y pointing
        down, and the 1st axis is x, pointing right.)
    """
    locals_owner = _native.shape_accel_locals(('p', 'q', 'r', 'val'), {'p': p, 'q': q, 'r': r}, {})
    del p, q, r
    return _native.shape_accel_vectorized_orientation(_native.shape_accel_namespace(), ("Determines orientation of ordered triplet(s) (p, q, r).\n\n    This implementation is vectorized and works for arrays of points.\n\n    Parameters\n    ----------\n    p : (..., 2) array\n        Array of first points of triplets.\n    q : (..., 2) array\n        Array of second points of triplets.\n    r : (..., 2) array\n        Array of third points of triplets.\n\n    Returns\n    -------\n    val : (...) array of int\n        An array of values, one for each triplet. 0 if p, q, r are collinear,\n        1 if clockwise, and -1 if counterclockwise. (These definitions assume\n        napari's default reference frame, in which the 0th axis is y pointing\n        down, and the 1st axis is x, pointing right.)\n    ",), locals_owner)


def cross_2d(a: npt.NDArray, b: npt.NDArray) -> npt.NDArray:
    """Calculate the magnitude of the cross product of two 2D vectors.

    This is the explicit implementation of the 2D cross product magnitude,
    which was provided by numpy developer Sebastian Berg
    (seberg) to avoid a NumPy 2.0 deprecation warning.
    See this issue fordetails:
    https://github.com/numpy/numpy/issues/26620#issuecomment-2150748569

    Parameters
    ----------
    a : (..., 2) array
        An array of 2D vectors.
    b : (..., 2) array
        An array of 2D vectors.

    Returns
    -------
    cross_product : (...) array
        The magnitude of the cross product.
    """
    locals_owner = _native.shape_accel_locals(('a', 'b'), {'a': a, 'b': b}, {})
    del a, b
    return _native.shape_accel_cross2d(_native.shape_accel_namespace(), ('Calculate the magnitude of the cross product of two 2D vectors.\n\n    This is the explicit implementation of the 2D cross product magnitude,\n    which was provided by numpy developer Sebastian Berg\n    (seberg) to avoid a NumPy 2.0 deprecation warning.\n    See this issue fordetails:\n    https://github.com/numpy/numpy/issues/26620#issuecomment-2150748569\n\n    Parameters\n    ----------\n    a : (..., 2) array\n        An array of 2D vectors.\n    b : (..., 2) array\n        An array of 2D vectors.\n\n    Returns\n    -------\n    cross_product : (...) array\n        The magnitude of the cross product.\n    ', (..., 0), (..., 1)), locals_owner)


def _common_orientation(poly: npt.NDArray) -> int | None:
    """Check whether a polygon has the same orientation for all its angles.
    and return the orientation.

    Parameters
    ----------
    poly: numpy array of floats, shape (N, 3)
        Polygon vertices, in order.

    Returns
    -------
    int or None
        if all angles have same orientation return it, otherwise None.
        Possible values: -1 if all angles are counterclockwise, 0 if all angles
        are collinear, 1 if all angles are clockwise.(These definitions
        assume napari's default reference frame, in which the 0th axis is y,
        pointing down, and the 1st axis is x, pointing right.)
    """
    locals_owner = _native.shape_accel_locals(('poly', 'fst', 'snd', 'thrd', 'orn_set'), {'poly': poly}, {})
    del poly
    return _native.shape_accel_common_orientation(_native.shape_accel_namespace(), ("Check whether a polygon has the same orientation for all its angles.\n    and return the orientation.\n\n    Parameters\n    ----------\n    poly: numpy array of floats, shape (N, 3)\n        Polygon vertices, in order.\n\n    Returns\n    -------\n    int or None\n        if all angles have same orientation return it, otherwise None.\n        Possible values: -1 if all angles are counterclockwise, 0 if all angles\n        are collinear, 1 if all angles are clockwise.(These definitions\n        assume napari's default reference frame, in which the 0th axis is y,\n        pointing down, and the 1st axis is x, pointing right.)\n    ", 0, 3, None, -2, 1, -1, 2), locals_owner)


def is_convex_py(poly: npt.NDArray) -> bool:
    """Check whether a polygon is convex.

    Parameters
    ----------
    poly: numpy array of floats, shape (N, 3)
        Polygon vertices, in order.

    Returns
    -------
    bool
        True if the given polygon is convex.
    """
    locals_owner = _native.shape_accel_locals(('poly', 'orientation_'), {'poly': poly}, {})
    del poly
    return _native.shape_accel_convex_py(_native.shape_accel_namespace(), ('Check whether a polygon is convex.\n\n    Parameters\n    ----------\n    poly: numpy array of floats, shape (N, 3)\n        Polygon vertices, in order.\n\n    Returns\n    -------\n    bool\n        True if the given polygon is convex.\n    ', 0, False), locals_owner)
