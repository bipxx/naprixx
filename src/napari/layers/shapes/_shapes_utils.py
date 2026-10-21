from __future__ import annotations
from naparixx import _native

import itertools
import tempfile
import typing
from typing import TYPE_CHECKING, overload

import numpy as np
from skimage import measure
from skimage.draw import line, polygon2mask
from vispy.geometry import Triangulation
from vispy.visuals.tube import _frenet_frames

from napari.layers.shapes import (
    _accelerated_triangulate_dispatch as _triangulate_dispatch,
    _accelerated_triangulate_python as _triangulate_py,
)
from napari.layers.shapes.shape_types import (
    BoxArray,
    CoordinateArray,
    CoordinateArray2D,
    CoordinateArray3D,
    EdgeArray,
    TriangleArray,
)

if TYPE_CHECKING:
    import numpy.typing as npt

try:
    # see https://github.com/vispy/vispy/issues/1029
    from triangle import triangulate
except ModuleNotFoundError:
    triangulate = None



# Immutable original function constants; code objects are omitted.
_shape_geometry_literals = (('Find an axis along which the input points are planar.\n\n    If points are 2D, they are returned unchanged.\n\n    If there is a planar axis, return the corresponding 2D points, the axis\n    position, and the coordinate of the plane.\n\n    If there is *no* planar axis, return an empty dataset.\n\n    Parameters\n    ----------\n    points : array, shape (npoints, ndim)\n        An array of point coordinates. ``ndim`` must be 2 or 3.\n\n    Returns\n    -------\n    points2d : array, shape (npoints | 0, 2)\n        Array of 2D points. May be empty if input points are not planar along\n        any axis.\n    axis_idx : int | None\n        The axis along which points are planar.\n    value : float | None\n        The coordinate of the points along ``axis``.\n    ', 1, 2, None, ('axis',), 0, (0, 2), ('dtype',)), ('Return a fan triangulation of a given polygon.\n\n    https://en.wikipedia.org/wiki/Fan_triangulation\n\n    Parameters\n    ----------\n    poly: numpy array of float, shape (N, 3) or (N, 2)\n        Polygon vertices, in order.\n\n    Returns\n    -------\n    vertices : numpy array of float, shape (N, 3) or (N, 2)\n        The vertices of the triangulation. In this case, the input array.\n    triangles : numpy array of int, shape (N, 3)\n        The triangles of the triangulation, as triplets of indices into the\n        vertices array.\n    ', 2, 3, ('dtype',), 1, None), ('Checks which boxes contain the origin. Boxes need not be axis aligned\n\n    Parameters\n    ----------\n    boxes : (N, 8, 2) array\n        Array of N boxes that should be checked\n\n    Returns\n    -------\n    inside : (N,) array of bool\n        True if corresponding box contains the origin.\n    ', None, 0, 6, 4, 1, ('axis',)), ('Determines which triangles intersect an axis aligned box.\n\n    Parameters\n    ----------\n    triangles : (N, 3, 2) array\n        Array of vertices of triangles to be tested\n    corners : (2, 2) array\n        Array specifying corners of a box\n\n    Returns\n    -------\n    intersects : (N,) array of bool\n        Array with `True` values for triangles intersecting the box\n    ',), ('Determines which triangles have vertices inside an axis aligned box.\n\n    Parameters\n    ----------\n    triangles : (N, 3, 2) array\n        Array of vertices of triangles to be tested\n    corners : (2, 2) array\n        Array specifying corners of a box\n\n    Returns\n    -------\n    inside : (N,) array of bool\n        Array with `True` values for triangles with vertices inside the box\n    ', 0, 4, None, -1, ('dtype',), 3, 1, ('axis',)), ('Determines which triangles have edges that intersect the edges of an\n    axis aligned box.\n\n    Parameters\n    ----------\n    triangles : (N, 3, 2) array\n        Array of vertices of triangles to be tested\n    corners : (2, 2) array\n        Array specifying corners of a box\n\n    Returns\n    -------\n    intersects : (N,) array of bool\n        Array with `True` values for triangles with edges that intersect the\n        edges of the box.\n    ', (0, 2, 4, 6), ('dtype',), 3, None, 1, 4), ('Determines if line segment p1q1 intersects line segment p2q2\n\n    Parameters\n    ----------\n    p1 : (2,) array\n        Array of first point of first line segment\n    q1 : (2,) array\n        Array of second point of first line segment\n    p2 : (2,) array\n        Array of first point of second line segment\n    q2 : (2,) array\n        Array of second point of second line segment\n\n    Returns\n    -------\n    intersects : bool\n        Bool indicating if line segment p1q1 intersects line segment p2q2\n    ', 0), ("Determines if a set of N line segments intersects with a single line segment.\n\n    This implementation is vectorized to be fast for many segments. It uses\n    the 'orientation' method, which is a vectorized version of the algorithm\n    used elsewhere in this codebase, making it easier to review and maintain.\n\n    The algorithm is based on checking if the endpoints of each segment lie on\n    opposite sides of the other segment.\n\n    Parameters\n    ----------\n    p1 : (N, 2) array\n        Array of first points of N line segments.\n    q1 : (N, 2) array\n        Array of second points of N line segments.\n    p2 : (2,) array\n        First point of the single line segment.\n    q2 : (2,) array\n        Second point of the single line segment.\n\n    Returns\n    -------\n    intersects : (N,) array of bool\n        Array with `True` values for line segments in the set that intersect\n        the single line segment.\n    ", 0), ('Check if point(s) q are on the segment(s) from p to r.\n\n    This function is vectorized and assumes that the points are already\n    known to be collinear.\n\n    Parameters\n    ----------\n    p : (..., 2) array\n        Array of first points of segments.\n    q : (..., 2) array\n        Array of points to check.\n    r : (..., 2) array\n        Array of second points of segments.\n\n    Returns\n    -------\n    on : (...) array of bool\n        Array of booleans with `True` if q is on pr.\n    ', (Ellipsis, 0), (Ellipsis, 1)), ('Determines if a list of 2D points are collinear.\n\n    Parameters\n    ----------\n    points : (N, 2) array\n        Points to be tested for collinearity\n\n    Returns\n    -------\n    val : bool\n        True is all points are collinear, False otherwise.\n    ', 3, True, None, 2, None), ('Calculate the distance between a point and line segments and returns the\n    index of the closest line. First calculates the distance to the infinite\n    line, then checks if the projected point lies between the line segment\n    endpoints. If not, calculates distance to the endpoints\n\n    Parameters\n    ----------\n    point : np.ndarray\n        1x2 array of specifying the point\n    lines : np.ndarray\n        Nx2x2 array of line segments\n\n    Returns\n    -------\n    index : int\n        Integer index of the closest line\n    location : float\n        Normalized location of intersection of the distance normal to the line\n        closest. Less than 0 means an intersection before the line segment\n        starts. Between 0 and 1 means an intersection inside the line segment.\n        Greater than 1 means an intersection after the line segment ends\n    ', None, 1, 0, True, ('axis', 'keepdims'), (Ellipsis, 0), (Ellipsis, 1), ('axis',), 0.5), ('Creates the axis aligned interaction box of a list of points\n\n    Parameters\n    ----------\n    data : np.ndarray\n        Nx2 array of points whose interaction box is to be found\n\n    Returns\n    -------\n    box : np.ndarray\n        9x2 array of vertices of the interaction box. The first 8 points are\n        the corners and midpoints of the box in clockwise order starting in the\n        upper-left corner. The last point is the center of the box\n    ', None, 0, ('axis',), 1, 2, 4), ('Converts the four corners of a rectangle into a interaction box like\n    representation. If the rectangle is not axis aligned the resulting box\n    representation will not be axis aligned either\n\n    Parameters\n    ----------\n    data : np.ndarray\n        4xD array of corner points to be converted to a box like representation\n\n    Returns\n    -------\n    box : np.ndarray\n        9xD array of vertices of the interaction box. The first 8 points are\n        the corners and midpoints of the box in clockwise order starting in the\n        upper-left corner. The last point is the center of the box\n    ', 0, 4, 'Data shape does not match expected `[4, D]` shape specifying corners for the rectangle', 1, 2, 3, ('axis',)), ('Finds the four corners of the interaction box defined by an array of\n    points\n\n    Parameters\n    ----------\n    data : np.ndarray\n        Nx2 array of points whose interaction box is to be found\n\n    Returns\n    -------\n    corners : np.ndarray\n        4x2 array of corners of the bounding box\n    ', 0, ('axis',), 1), ('Expands a center and radii into a four corner rectangle\n\n    Parameters\n    ----------\n    center : np.ndarray\n        Length 2 array of the center coordinates.\n    radii : np.ndarray\n        Length 2 array of the two radii.\n\n    Returns\n    -------\n    corners : np.ndarray\n        4x2 array of corners of the bounding box.\n    ',), ("Determines the triangulation of a path. The resulting `offsets` can\n    multiplied by a `width` scalar and be added to the resulting `centers`\n    to generate the vertices of the triangles for the triangulation, i.e.\n    `vertices = centers + width*offsets`. Using the `centers` and `offsets`\n    representation thus allows for the computed triangulation to be\n    independent of the line width.\n\n    Parameters\n    ----------\n    corners : np.ndarray\n        4xD array of four bounding corners of the ellipse. The ellipse will\n        still be computed properly even if the rectangle determined by the\n        corners is not axis aligned. D in {2,3}\n    num_segments : int\n        Integer determining the number of segments to use when triangulating\n        the ellipse\n\n    Returns\n    -------\n    vertices : np.ndarray\n        Mx2/Mx3 array coordinates of vertices for triangulating an ellipse.\n        Includes the center vertex of the ellipse, followed by `num_segments`\n        vertices around the boundary of the ellipse (M = `num_segments`+1)\n    triangles : np.ndarray\n        Px3 array of the indices of the vertices for the triangles of the\n        triangulation. Has length (P) given by `num_segments`,\n        (P = M-1 = num_segments)\n\n    Notes\n    -----\n    Despite it's name the ellipse will have num_segments-1 segments on their outline.\n    That is to say num_segments=7 will lead to ellipses looking like hexagons.\n\n    The behavior of this function is not well defined if the ellipse is degenerate\n    in the current plane/volume you are currently observing.\n\n\n    ", 0, 4, 'Data shape does not match expected `[4, D]` shape specifying corners for the ellipse', frozenset({(4, 2), (4, 3)}), ('axis',), 1, 2, (4, 2), (2, 2), (2, 3), ('dtype',), 360, None, (0, 1, 1), (-1, 2)), ("Remove triangles that are not inside the polygon.\n\n    Unlike VisPy, triangle's constrained Delaunay triangulation\n    returns triangles inside the hole as well. (I guess in case you want\n    to render holes but in a different color, for example.) In our case,\n    we want to get rid of them, so we cull them with some NumPy\n    calculations.\n\n    Parameters\n    ----------\n    vertices: np.ndarray[np.floating], shape (N, 2)\n        The vertices of the triangulation.\n    triangles: np.ndarray[np.intp], shape (M, 3)\n        Triangles in the triangulation, defined by three indices into the\n        vertex array.\n    poly: np.ndarray[np.floating], shape (P, 2)\n        The vertices of the polygon, in order. Holes in the polygon are\n        defined by an embedded polygon that starts from an arbitrary point in\n        the enclosing polygon and wind in the opposite direction.\n\n    Returns\n    -------\n    culled_triangles: np.ndarray[np.intp], shape (P, 3), P ≤ M\n        A subset of the input triangles.\n    ", 1, ('axis',)), ('Ensure vertices are planar along a given axis.\n\n    If an axis and value are provided, this function inserts the value as\n    a new coordinate along the given axis.\n    Used to convert 2D vertices in 3D vertices along the plane.\n\n    Parameters\n    ----------\n    vertices: np.ndarray[np.floating], shape (N, 2)\n        The vertices of the triangulation.\n    axis: int\n        The axis along which the vertices are planar.\n    value: float\n        The coordinate of the plane.\n\n    Returns\n    -------\n    new_vertices: np.ndarray[np.floating], shape (N, 3)\n        The vertices of the triangulation with the given axis fixed.\n    ', 1, ('axis',)), ('Determines the triangulation of the face and edges of a shape.\n\n    Parameters\n    ----------\n    polygon_vertices : np.ndarray\n        Nx2 array of vertices of shape to be triangulated\n\n    Returns\n    -------\n    face_tri : tuple[np.ndarray, np.ndarray]\n        Tuple of vertices and triangles of the face.\n    edge_tri : tuple[np.ndarray, np.ndarray, np.ndarray]\n        Tuple of vertices, offsets, and triangles of the edges.\n    ', 0, 1, ('dtype',), (0, 3), True, ('closed',), ('axis', 'value'), ('close',)), ('Triangulate edges when vertices form multiple disjoint polygons.\n\n    Parameters\n    ----------\n    vertices: np.ndarray\n        Nx2 or Nx3 array of vertices of shape to be triangulated.\n        Cannot contain duplicated vertices.\n    edges: np.ndarray\n        list of edges encoded as vertices indices.\n        Cannot contain duplicated vertices.\n\n    Returns\n    -------\n    edges_tri: tuple[ndarray, ndarray, ndarray]\n        vertices, offsets, and triangles of the edge triangulation(s).\n    ', 0, True, ('closed',)), ('Determines the triangulation of the face of a shape.\n\n    Parameters\n    ----------\n    polygon_vertices : np.ndarray\n        Nx2 array of vertices of shape to be triangulated\n\n    Returns\n    -------\n    vertices : np.ndarray\n        Mx2 array vertices of the triangles.\n    triangles : np.ndarray\n        Px3 array of the indices of the vertices that will form the\n        triangles of the triangulation\n    ', True, ('close',)), (None, 'vispy', ('backend',), 'Triangulation failed. Data saved to ', ' and '), (None, ('vertices', 'segments'), 'p', ('opts',), 'vertices', 'triangles'), ('Determines the triangulation of a path.\n\n    The resulting `offsets` can multiplied by a `width` scalar and be added\n    to the resulting `centers` to generate the vertices of the triangles for\n    the triangulation, i.e. `vertices = centers + width*offsets`. Using the\n    `centers` and `offsets` representation thus allows for the computed\n    triangulation to be independent of the line width.\n\n    Parameters\n    ----------\n    path : np.ndarray\n        Nx2 or Nx3 array of central coordinates of path to be triangulated\n    closed : bool\n        Bool which determines if the path is closed or not.\n\n    Returns\n    -------\n    centers : np.ndarray\n        Mx2 or Mx3 array central coordinates of path triangles.\n    offsets : np.ndarray\n        Mx2 or Mx3 array of the offsets to the central coordinates that need to\n        be scaled by the line width and then added to the centers to\n        generate the actual vertices of the triangulation\n    triangles : np.ndarray\n        Px3 array of the indices of the vertices that will form the\n        triangles of the triangulation\n    ', 2, True, 1, None, -1, ('axis',), 0, ('dtype',), ('closed',)), ("Generates list of mesh vertices and triangles from a path\n\n    Adapted from vispy.visuals.TubeVisual\n    https://github.com/vispy/vispy/blob/main/vispy/visuals/tube.py\n\n    Parameters\n    ----------\n    path : (N, 3) array\n        Vertices specifying the path.\n    closed : bool\n        Bool which determines if the path is closed or not.\n    tube_points : int\n        The number of points in the circle-approximating polygon of the\n        tube's cross section.\n\n    Returns\n    -------\n    centers : (M, 3) array\n        Vertices of all triangles for the lines\n    offsets : (M, D) array\n        offsets of all triangles for the lines\n    triangles : (P, 3) array\n        Vertex indices that form the mesh triangles\n    ", 0, -1, ('axis',), 1, 3, ('dtype',), 2, -1.0, None), ('Converts a path to a boolean mask with `True` for points lying along\n    each edge.\n\n    Parameters\n    ----------\n    mask_shape : array (2,)\n        Shape of mask to be generated.\n    vertices : array (N, 2)\n        Vertices of the path.\n\n    Returns\n    -------\n    mask : np.ndarray\n        Boolean array with `True` for points along the path\n\n    ', ('dtype',), 0, 1, ('axis',), -1, False), ('Converts a polygon to a boolean mask with `True` for points\n    lying inside the shape. Uses the bounding box of the vertices to reduce\n    computation time.\n\n    Parameters\n    ----------\n    mask_shape : np.ndarray | tuple\n        1x2 array of shape of mask to be generated.\n    vertices : np.ndarray\n        Nx2 array of the vertices of the polygon.\n\n    Returns\n    -------\n    mask : np.ndarray\n        Boolean array with `True` for points inside the polygon\n    ',), ('Converts a polygon to a boolean mask with `True` for points\n    lying inside the shape. Loops through all indices in the grid\n\n    Parameters\n    ----------\n    shape : np.ndarray | tuple\n        1x2 array of shape of mask to be generated.\n    vertices : np.ndarray\n        Nx2 array of the vertices of the polygon.\n\n    Returns\n    -------\n    mask : np.ndarray\n        Boolean array with `True` for points inside the polygon\n    ', 0, 1, ('dtype',)), ('Tests points for being inside a polygon using the ray casting algorithm\n\n    Parameters\n    ----------\n    points : np.ndarray\n        Mx2 array of points to be tested\n    vertices : np.ndarray\n        Nx2 array of the vertices of the polygon.\n\n    Returns\n    -------\n    inside : np.ndarray\n        Length M boolean array with `True` for points inside the polygon\n    ', ('dtype',), 1, None, 1e-12, 0), ('Separates shape_type from data if present, and returns both.\n\n    Parameters\n    ----------\n    data : Array | Tuple(Array,str) | List[Array | Tuple(Array, str)] | Tuple(List[Array], str)\n        list or array of vertices belonging to each shape, optionally containing shape type strings\n    shape_type : str | None\n        metadata shape type string, or None if none was passed\n\n    Returns\n    -------\n    data : Array | List[Array]\n        list or array of vertices belonging to each shape\n    shape_type : List[str] | None\n        type of each shape in data, or None if none was passed\n    ', 1, 0, None), ('If all shapes in current_type are of identical shape type,\n       return this shape type, else "polygon" as lowest common\n       denominator type.\n\n    Parameters\n    ----------\n    current_type : list of str\n        list of current shape types\n\n    Returns\n    -------\n    default_type : str\n        default shape type\n    ', 'polygon', 0, None), ('Checks whether data is a list of the same type of shape, one shape, or\n    a list of different shapes and returns the dimensionality of the shape/s.\n\n    Parameters\n    ----------\n    data : (N, ) list of array\n        List of shape data, where each element is an (N, D) array of the\n        N vertices of a shape in D dimensions.\n\n    Returns\n    -------\n    ndim : int\n        Dimensionality of the shape/s in data\n    ', ('dtype',), 3, 2, 0, 1), ('Determine number of shapes in the data.\n\n    Parameters\n    ----------\n    data : list or np.ndarray\n        Can either be no shapes, if empty, a\n        single shape or a list of shapes.\n\n    Returns\n    -------\n    n_shapes : int\n        Number of new shapes\n    ', 0, 1), ('Raises error if a shape in data has invalid number of vertices.\n\n    Checks whether all shapes in data have a valid number of vertices\n    for the given shape type and vertex information. Rectangles and\n    ellipses can have either 2 or 4 vertices per shape,\n    lines can have only 2, while paths and polygons have a minimum\n    number of vertices, but no maximum.\n\n    One of valid_vertices or min_vertices must be passed to the\n    function.\n\n    Parameters\n    ----------\n    data : Array | Tuple(Array,str) | List[Array | Tuple(Array, str)] | Tuple(List[Array], str)\n        List of shape data, where each element is either an (N, D) array of the\n        N vertices of a shape in D dimensions or a tuple containing an array of\n        the N vertices and the shape_type string. Can be an 3-dimensional array\n        if each shape has the same number of vertices.\n    shape_type : str\n        Type of shape being validated (for detailed error message)\n    min_vertices : int or None\n        Minimum number of vertices for the shape type, by default None\n    valid_vertices : Tuple(int) or None\n        Valid number of vertices for the shape type in data, by default None\n\n    Raises\n    ------\n    ValueError\n        Raised if a shape is found with invalid number of vertices\n    ', 1, 2, ' ', ' has invalid number of vertices: ', '.', None), ('Calculate the perpendicular distance of a point to a given euclidean line.\n\n    Calculates the shortest distance of a point to a euclidean line defined by a line_start point and a line_end point.\n    Works up to any dimension.\n\n    Parameters\n    ---------\n    point : np.ndarray\n        A point defined by a numpy array of shape (viewer.ndims,)  which is part of a polygon shape.\n    line_start : np.ndarray\n        A point defined by a numpy array of shape (viewer.ndims,)  used to define the starting point of a line.\n    line_end : np.ndarray\n        A point defined by a numpy array of shape (viewer.ndims,)  used to define the end point of a line.\n\n    Returns\n    -------\n    float\n        A float number representing the distance of point to a euclidean line defined by line_start and line_end.\n    ',), ('Reduce the number of vertices that make up a polygon.\n\n    Implementation of the Ramer-Douglas-Peucker algorithm based on:\n    https://github.com/fhirschmann/rdp/blob/master/rdp. This algorithm reduces the amounts of points in a polyline or\n    in this case reduces the number of vertices in a polygon shape.\n\n    Parameters\n    ----------\n    vertices : np.ndarray\n        A numpy array of shape (n, viewer.ndims) containing the vertices of a polygon shape.\n    epsilon : float\n        A float representing the maximum distance threshold. When the perpendicular distance of a point to a given line\n        is higher, subsequent refinement occurs.\n\n    Returns\n    -------\n    np.ndarray\n        A numpy array of shape (n, viewer.ndims) containing the vertices of a polygon shape.\n    ', -1, 0.0, 1, 0, None), ('Save data to temporary files for debugging.\n\n    This function saves input data when triangulation fails.\n    It saves the same data to both a .npz file and a .txt file within the\n    temporary directory, and returns the paths to the saved files.\n\n    Parameters\n    ----------\n    data : np.ndarray\n        The data to save.\n    target_dir: str or None\n        Path to the directory where the files will be saved.\n        If None, the default\n    backend: str, options\n        The backend used for triangulation. This is used to generate the\n        filename prefix.\n\n    Returns\n    -------\n    tuple[str, str]\n        The paths to the saved files.\n\n    Notes\n    -----\n    Use TMPDIR environment variable to set the temporary directory.\n    ', False, '.npz', 'napari_', '_triang_', ('delete', 'suffix', 'prefix', 'dir'), ('data',), None, '.txt', 'w', ('delete', 'suffix', 'prefix', 'mode', 'dir')))

def find_planar_axis(
    points: CoordinateArray,
) -> tuple[CoordinateArray2D, int | None, float | None]:
    """Find an axis along which the input points are planar.

    If points are 2D, they are returned unchanged.

    If there is a planar axis, return the corresponding 2D points, the axis
    position, and the coordinate of the plane.

    If there is *no* planar axis, return an empty dataset.

    Parameters
    ----------
    points : array, shape (npoints, ndim)
        An array of point coordinates. ``ndim`` must be 2 or 3.

    Returns
    -------
    points2d : array, shape (npoints | 0, 2)
        Array of 2D points. May be empty if input points are not planar along
        any axis.
    axis_idx : int | None
        The axis along which points are planar.
    value : float | None
        The coordinate of the points along ``axis``.
    """
    owner = _native.shape_geometry_locals(('points', 'ndim', 'axis_idx', 'values'), {'points': points}, {})
    del points
    return _native.shape_geometry_find_planar_axis(globals(), _shape_geometry_literals[0], owner)


@typing.overload
def _fan_triangulation(
    poly: CoordinateArray2D,
) -> tuple[CoordinateArray2D, TriangleArray]: ...


@typing.overload
def _fan_triangulation(
    poly: CoordinateArray3D,
) -> tuple[CoordinateArray3D, TriangleArray]: ...


def _fan_triangulation(
    poly: CoordinateArray,
) -> tuple[CoordinateArray, TriangleArray]:
    """Return a fan triangulation of a given polygon.

    https://en.wikipedia.org/wiki/Fan_triangulation

    Parameters
    ----------
    poly: numpy array of float, shape (N, 3) or (N, 2)
        Polygon vertices, in order.

    Returns
    -------
    vertices : numpy array of float, shape (N, 3) or (N, 2)
        The vertices of the triangulation. In this case, the input array.
    triangles : numpy array of int, shape (N, 3)
        The triangles of the triangulation, as triplets of indices into the
        vertices array.
    """
    owner = _native.shape_geometry_locals(('poly', 'vertices', 'triangles'), {'poly': poly}, {})
    del poly
    return _native.shape_geometry_fan_triangulation(globals(), _shape_geometry_literals[1], owner)


def inside_boxes(boxes):
    """Checks which boxes contain the origin. Boxes need not be axis aligned

    Parameters
    ----------
    boxes : (N, 8, 2) array
        Array of N boxes that should be checked

    Returns
    -------
    inside : (N,) array of bool
        True if corresponding box contains the origin.
    """

    owner = _native.shape_geometry_locals(('boxes', 'AB', 'AM', 'BC', 'BM', 'ABAM', 'ABAB', 'BCBM', 'BCBC', 'c1', 'c2', 'c3', 'c4', 'inside'), {'boxes': boxes}, {})
    del boxes
    return _native.shape_geometry_inside_boxes(globals(), _shape_geometry_literals[2], owner)


def triangles_intersect_box(triangles, corners):
    """Determines which triangles intersect an axis aligned box.

    Parameters
    ----------
    triangles : (N, 3, 2) array
        Array of vertices of triangles to be tested
    corners : (2, 2) array
        Array specifying corners of a box

    Returns
    -------
    intersects : (N,) array of bool
        Array with `True` values for triangles intersecting the box
    """

    owner = _native.shape_geometry_locals(('triangles', 'corners', 'vertices_inside', 'edge_intersects', 'intersects'), {'triangles': triangles, 'corners': corners}, {})
    del triangles, corners
    return _native.shape_geometry_triangles_intersect_box(globals(), _shape_geometry_literals[3], owner)


def triangle_vertices_inside_box(triangles, corners):
    """Determines which triangles have vertices inside an axis aligned box.

    Parameters
    ----------
    triangles : (N, 3, 2) array
        Array of vertices of triangles to be tested
    corners : (2, 2) array
        Array specifying corners of a box

    Returns
    -------
    inside : (N,) array of bool
        Array with `True` values for triangles with vertices inside the box
    """
    owner = _native.shape_geometry_locals(('triangles', 'corners', 'box', 'vertices_inside', 'i', 'below_top', 'above_bottom', 'inside'), {'triangles': triangles, 'corners': corners}, {})
    del triangles, corners
    return _native.shape_geometry_triangle_vertices_inside_box(globals(), _shape_geometry_literals[4], owner)


def triangle_edges_intersect_box(
    triangles: npt.NDArray, corners: npt.NDArray
) -> npt.NDArray:
    """Determines which triangles have edges that intersect the edges of an
    axis aligned box.

    Parameters
    ----------
    triangles : (N, 3, 2) array
        Array of vertices of triangles to be tested
    corners : (2, 2) array
        Array specifying corners of a box

    Returns
    -------
    intersects : (N,) array of bool
        Array with `True` values for triangles with edges that intersect the
        edges of the box.
    """
    owner = _native.shape_geometry_locals(('triangles', 'corners', 'box', 'all_intersects', 'i', 'p1', 'q1', 'j', 'p2', 'q2', 'intersects'), {'triangles': triangles, 'corners': corners}, {})
    del triangles, corners
    return _native.shape_geometry_triangle_edges_intersect_box(globals(), _shape_geometry_literals[5], owner)


def lines_intersect(p1, q1, p2, q2):
    """Determines if line segment p1q1 intersects line segment p2q2

    Parameters
    ----------
    p1 : (2,) array
        Array of first point of first line segment
    q1 : (2,) array
        Array of second point of first line segment
    p2 : (2,) array
        Array of first point of second line segment
    q2 : (2,) array
        Array of second point of second line segment

    Returns
    -------
    intersects : bool
        Bool indicating if line segment p1q1 intersects line segment p2q2
    """
    # Wrap inputs into (1, 2) arrays to use the vectorized function
    owner = _native.shape_geometry_locals(('p1', 'q1', 'p2', 'q2', 'p1_arr', 'q1_arr'), {'p1': p1, 'q1': q1, 'p2': p2, 'q2': q2}, {})
    del p1, q1, p2, q2
    return _native.shape_geometry_lines_intersect(globals(), _shape_geometry_literals[6], owner)


def vectorized_lines_intersect(
    p1: npt.NDArray, q1: npt.NDArray, p2: npt.NDArray, q2: npt.NDArray
) -> npt.NDArray:
    """Determines if a set of N line segments intersects with a single line segment.

    This implementation is vectorized to be fast for many segments. It uses
    the 'orientation' method, which is a vectorized version of the algorithm
    used elsewhere in this codebase, making it easier to review and maintain.

    The algorithm is based on checking if the endpoints of each segment lie on
    opposite sides of the other segment.

    Parameters
    ----------
    p1 : (N, 2) array
        Array of first points of N line segments.
    q1 : (N, 2) array
        Array of second points of N line segments.
    p2 : (2,) array
        First point of the single line segment.
    q2 : (2,) array
        Second point of the single line segment.

    Returns
    -------
    intersects : (N,) array of bool
        Array with `True` values for line segments in the set that intersect
        the single line segment.
    """
    # Determine four orientations
    owner = _native.shape_geometry_locals(('p1', 'q1', 'p2', 'q2', 'o1', 'o2', 'o3', 'o4', 'intersects'), {'p1': p1, 'q1': q1, 'p2': p2, 'q2': q2}, {})
    del p1, q1, p2, q2
    return _native.shape_geometry_vectorized_lines_intersect(globals(), _shape_geometry_literals[7], owner)


def on_segment(p: npt.NDArray, q: npt.NDArray, r: npt.NDArray) -> npt.NDArray:
    """Check if point(s) q are on the segment(s) from p to r.

    This function is vectorized and assumes that the points are already
    known to be collinear.

    Parameters
    ----------
    p : (..., 2) array
        Array of first points of segments.
    q : (..., 2) array
        Array of points to check.
    r : (..., 2) array
        Array of second points of segments.

    Returns
    -------
    on : (...) array of bool
        Array of booleans with `True` if q is on pr.
    """
    owner = _native.shape_geometry_locals(('p', 'q', 'r'), {'p': p, 'q': q, 'r': r}, {})
    del p, q, r
    return _native.shape_geometry_on_segment(globals(), _shape_geometry_literals[8], owner)


def is_collinear(points: npt.NDArray) -> bool:
    """Determines if a list of 2D points are collinear.

    Parameters
    ----------
    points : (N, 2) array
        Points to be tested for collinearity

    Returns
    -------
    val : bool
        True is all points are collinear, False otherwise.
    """
    def generator_factory(iterator):
        yield from iterator
    _cell_points = lambda: points
    owner = _native.shape_geometry_locals(('points', 'points'), {}, {'points': _cell_points.__closure__[0]})
    return _native.shape_geometry_is_collinear(globals(), _shape_geometry_literals[9], generator_factory, owner)


def point_to_lines(point, lines):
    """Calculate the distance between a point and line segments and returns the
    index of the closest line. First calculates the distance to the infinite
    line, then checks if the projected point lies between the line segment
    endpoints. If not, calculates distance to the endpoints

    Parameters
    ----------
    point : np.ndarray
        1x2 array of specifying the point
    lines : np.ndarray
        Nx2x2 array of line segments

    Returns
    -------
    index : int
        Integer index of the closest line
    location : float
        Normalized location of intersection of the distance normal to the line
        closest. Less than 0 means an intersection before the line segment
        starts. Between 0 and 1 means an intersection inside the line segment.
        Greater than 1 means an intersection after the line segment ends
    """

    # shift and normalize vectors
    owner = _native.shape_geometry_locals(('point', 'lines', 'lines_vectors', 'point_vectors', 'end_point_vectors', 'norm_lines', 'reject', 'unit_lines', 'line_dist', 'line_loc', 'index', 'location'), {'point': point, 'lines': lines}, {})
    del point, lines
    return _native.shape_geometry_point_to_lines(globals(), _shape_geometry_literals[10], owner)


def create_box(data: CoordinateArray2D) -> BoxArray:
    """Creates the axis aligned interaction box of a list of points

    Parameters
    ----------
    data : np.ndarray
        Nx2 array of points whose interaction box is to be found

    Returns
    -------
    box : np.ndarray
        9x2 array of vertices of the interaction box. The first 8 points are
        the corners and midpoints of the box in clockwise order starting in the
        upper-left corner. The last point is the center of the box
    """
    owner = _native.shape_geometry_locals(('data', 'min_val', 'max_val', 'tl', 'tr', 'br', 'bl', 'box'), {'data': data}, {})
    del data
    return _native.shape_geometry_create_box(globals(), _shape_geometry_literals[11], owner)


def rectangle_to_box(
    data: np.ndarray[tuple[typing.Literal[4], int], np.dtype[np.float32]],
) -> BoxArray:
    """Converts the four corners of a rectangle into a interaction box like
    representation. If the rectangle is not axis aligned the resulting box
    representation will not be axis aligned either

    Parameters
    ----------
    data : np.ndarray
        4xD array of corner points to be converted to a box like representation

    Returns
    -------
    box : np.ndarray
        9xD array of vertices of the interaction box. The first 8 points are
        the corners and midpoints of the box in clockwise order starting in the
        upper-left corner. The last point is the center of the box
    """
    owner = _native.shape_geometry_locals(('data', 'box'), {'data': data}, {})
    del data
    return _native.shape_geometry_rectangle_to_box(globals(), _shape_geometry_literals[12], owner)


def find_corners(data: npt.NDArray) -> npt.NDArray:
    """Finds the four corners of the interaction box defined by an array of
    points

    Parameters
    ----------
    data : np.ndarray
        Nx2 array of points whose interaction box is to be found

    Returns
    -------
    corners : np.ndarray
        4x2 array of corners of the bounding box
    """
    owner = _native.shape_geometry_locals(('data', 'min_val', 'max_val', 'tl', 'tr', 'br', 'bl', 'corners'), {'data': data}, {})
    del data
    return _native.shape_geometry_find_corners(globals(), _shape_geometry_literals[13], owner)


def center_radii_to_corners(
    center: npt.NDArray, radii: npt.NDArray
) -> npt.NDArray:
    """Expands a center and radii into a four corner rectangle

    Parameters
    ----------
    center : np.ndarray
        Length 2 array of the center coordinates.
    radii : np.ndarray
        Length 2 array of the two radii.

    Returns
    -------
    corners : np.ndarray
        4x2 array of corners of the bounding box.
    """
    owner = _native.shape_geometry_locals(('center', 'radii', 'data', 'corners'), {'center': center, 'radii': radii}, {})
    del center, radii
    return _native.shape_geometry_center_radii_to_corners(globals(), _shape_geometry_literals[14], owner)


def triangulate_ellipse(
    corners: CoordinateArray, num_segments: int = 100
) -> tuple[CoordinateArray, TriangleArray]:
    """Determines the triangulation of a path. The resulting `offsets` can
    multiplied by a `width` scalar and be added to the resulting `centers`
    to generate the vertices of the triangles for the triangulation, i.e.
    `vertices = centers + width*offsets`. Using the `centers` and `offsets`
    representation thus allows for the computed triangulation to be
    independent of the line width.

    Parameters
    ----------
    corners : np.ndarray
        4xD array of four bounding corners of the ellipse. The ellipse will
        still be computed properly even if the rectangle determined by the
        corners is not axis aligned. D in {2,3}
    num_segments : int
        Integer determining the number of segments to use when triangulating
        the ellipse

    Returns
    -------
    vertices : np.ndarray
        Mx2/Mx3 array coordinates of vertices for triangulating an ellipse.
        Includes the center vertex of the ellipse, followed by `num_segments`
        vertices around the boundary of the ellipse (M = `num_segments`+1)
    triangles : np.ndarray
        Px3 array of the indices of the vertices for the triangles of the
        triangulation. Has length (P) given by `num_segments`,
        (P = M-1 = num_segments)

    Notes
    -----
    Despite it's name the ellipse will have num_segments-1 segments on their outline.
    That is to say num_segments=7 will lead to ellipses looking like hexagons.

    The behavior of this function is not well defined if the ellipse is degenerate
    in the current plane/volume you are currently observing.


    """
    owner = _native.shape_geometry_locals(('corners', 'num_segments', 'center', 'adjusted', 'ax1', 'ax2', 'transform', 'v2d', 'theta', 'vertices', 'triangles'), {'corners': corners, 'num_segments': num_segments}, {})
    del corners, num_segments
    return _native.shape_geometry_triangulate_ellipse(globals(), _shape_geometry_literals[15], __debug__, owner)


def _cull_triangles_not_in_poly(vertices, triangles, poly):
    """Remove triangles that are not inside the polygon.

    Unlike VisPy, triangle's constrained Delaunay triangulation
    returns triangles inside the hole as well. (I guess in case you want
    to render holes but in a different color, for example.) In our case,
    we want to get rid of them, so we cull them with some NumPy
    calculations.

    Parameters
    ----------
    vertices: np.ndarray[np.floating], shape (N, 2)
        The vertices of the triangulation.
    triangles: np.ndarray[np.intp], shape (M, 3)
        Triangles in the triangulation, defined by three indices into the
        vertex array.
    poly: np.ndarray[np.floating], shape (P, 2)
        The vertices of the polygon, in order. Holes in the polygon are
        defined by an embedded polygon that starts from an arbitrary point in
        the enclosing polygon and wind in the opposite direction.

    Returns
    -------
    culled_triangles: np.ndarray[np.intp], shape (P, 3), P ≤ M
        A subset of the input triangles.
    """
    owner = _native.shape_geometry_locals(('vertices', 'triangles', 'poly', 'centers', 'in_poly'), {'vertices': vertices, 'triangles': triangles, 'poly': poly}, {})
    del vertices, triangles, poly
    return _native.shape_geometry_cull_triangles_not_in_poly(globals(), _shape_geometry_literals[16], owner)


def _fix_vertices_if_needed(
    vertices: CoordinateArray2D, axis: int | None, value: float | None
) -> CoordinateArray:
    """Ensure vertices are planar along a given axis.

    If an axis and value are provided, this function inserts the value as
    a new coordinate along the given axis.
    Used to convert 2D vertices in 3D vertices along the plane.

    Parameters
    ----------
    vertices: np.ndarray[np.floating], shape (N, 2)
        The vertices of the triangulation.
    axis: int
        The axis along which the vertices are planar.
    value: float
        The coordinate of the plane.

    Returns
    -------
    new_vertices: np.ndarray[np.floating], shape (N, 3)
        The vertices of the triangulation with the given axis fixed.
    """
    owner = _native.shape_geometry_locals(('vertices', 'axis', 'value', 'new_vertices'), {'vertices': vertices, 'axis': axis, 'value': value}, {})
    del vertices, axis, value
    return _native.shape_geometry_fix_vertices_if_needed(globals(), _shape_geometry_literals[17], owner)


def triangulate_face_and_edges(
    polygon_vertices: CoordinateArray,
    triangulate_face_: typing.Callable,
) -> tuple[
    tuple[CoordinateArray, TriangleArray],
    tuple[CoordinateArray, CoordinateArray, TriangleArray],
]:
    """Determines the triangulation of the face and edges of a shape.

    Parameters
    ----------
    polygon_vertices : np.ndarray
        Nx2 array of vertices of shape to be triangulated

    Returns
    -------
    face_tri : tuple[np.ndarray, np.ndarray]
        Tuple of vertices and triangles of the face.
    edge_tri : tuple[np.ndarray, np.ndarray, np.ndarray]
        Tuple of vertices, offsets, and triangles of the edges.
    """
    owner = _native.shape_geometry_locals(('polygon_vertices', 'triangulate_face_', 'data2d', 'axis', 'value', 'face_tri', 'edge_tri', 'vertices', 'triangles', 'raw_vertices', 'edges', 'face_tri_'), {'polygon_vertices': polygon_vertices, 'triangulate_face_': triangulate_face_}, {})
    del polygon_vertices, triangulate_face_
    return _native.shape_geometry_triangulate_face_and_edges(globals(), _shape_geometry_literals[18], owner)


@overload
def reconstruct_and_triangulate_edge(
    vertices: CoordinateArray2D, edges: EdgeArray
) -> tuple[CoordinateArray2D, CoordinateArray2D, TriangleArray]: ...


@overload
def reconstruct_and_triangulate_edge(
    vertices: CoordinateArray3D, edges: EdgeArray
) -> tuple[CoordinateArray3D, CoordinateArray3D, TriangleArray]: ...


def reconstruct_and_triangulate_edge(
    vertices: CoordinateArray, edges: EdgeArray
) -> tuple[CoordinateArray, CoordinateArray, TriangleArray]:
    """Triangulate edges when vertices form multiple disjoint polygons.

    Parameters
    ----------
    vertices: np.ndarray
        Nx2 or Nx3 array of vertices of shape to be triangulated.
        Cannot contain duplicated vertices.
    edges: np.ndarray
        list of edges encoded as vertices indices.
        Cannot contain duplicated vertices.

    Returns
    -------
    edges_tri: tuple[ndarray, ndarray, ndarray]
        vertices, offsets, and triangles of the edge triangulation(s).
    """
    owner = _native.shape_geometry_locals(('vertices', 'edges', 'polygon_list', 'centers_list', 'offset_list', 'triangles_list', 'offset_idx', 'polygon', 'centers', 'offset', 'triangles'), {'vertices': vertices, 'edges': edges}, {})
    del vertices, edges
    return _native.shape_geometry_reconstruct_and_triangulate_edge(globals(), _shape_geometry_literals[19], owner)


def triangulate_face(
    polygon_vertices: CoordinateArray2D,
    triangulate_face_: typing.Callable,
) -> tuple[CoordinateArray2D, TriangleArray]:
    """Determines the triangulation of the face of a shape.

    Parameters
    ----------
    polygon_vertices : np.ndarray
        Nx2 array of vertices of shape to be triangulated

    Returns
    -------
    vertices : np.ndarray
        Mx2 array vertices of the triangles.
    triangles : np.ndarray
        Px3 array of the indices of the vertices that will form the
        triangles of the triangulation
    """
    owner = _native.shape_geometry_locals(('polygon_vertices', 'triangulate_face_', 'raw_vertices', 'edges'), {'polygon_vertices': polygon_vertices, 'triangulate_face_': triangulate_face_}, {})
    del polygon_vertices, triangulate_face_
    return _native.shape_geometry_triangulate_face(globals(), _shape_geometry_literals[20], owner)


@overload
def triangulate_face_vispy(
    raw_vertices: CoordinateArray2D,
    edges: EdgeArray,
    polygon_vertices: CoordinateArray,
) -> tuple[CoordinateArray2D, TriangleArray]: ...


@overload
def triangulate_face_vispy(
    raw_vertices: CoordinateArray3D,
    edges: EdgeArray,
    polygon_vertices: CoordinateArray,
) -> tuple[CoordinateArray3D, TriangleArray]: ...


def triangulate_face_vispy(
    raw_vertices: CoordinateArray,
    edges: EdgeArray,
    polygon_vertices: CoordinateArray,
) -> tuple[CoordinateArray, TriangleArray]:
    owner = _native.shape_geometry_locals(('raw_vertices', 'edges', 'polygon_vertices', 'tri', 'vertices', 'triangles', 'e', 'path', 'text_path'), {'raw_vertices': raw_vertices, 'edges': edges, 'polygon_vertices': polygon_vertices}, {})
    del raw_vertices, edges, polygon_vertices
    return _native.shape_geometry_triangulate_face_vispy(globals(), _shape_geometry_literals[21], owner)


@overload
def triangulate_face_triangle(
    raw_vertices: CoordinateArray2D,
    edges: EdgeArray,
    polygon_vertices: CoordinateArray,
) -> tuple[CoordinateArray2D, TriangleArray]: ...


@overload
def triangulate_face_triangle(
    raw_vertices: CoordinateArray3D,
    edges: EdgeArray,
    polygon_vertices: CoordinateArray,
) -> tuple[CoordinateArray3D, TriangleArray]: ...


def triangulate_face_triangle(
    raw_vertices: CoordinateArray,
    edges: EdgeArray,
    polygon_vertices: CoordinateArray,
) -> tuple[CoordinateArray, TriangleArray]:
    owner = _native.shape_geometry_locals(('raw_vertices', 'edges', 'polygon_vertices', 'res', 'vertices', 'raw_triangles', 'triangles'), {'raw_vertices': raw_vertices, 'edges': edges, 'polygon_vertices': polygon_vertices}, {})
    del raw_vertices, edges, polygon_vertices
    return _native.shape_geometry_triangulate_face_triangle(globals(), _shape_geometry_literals[22], owner)


@typing.overload
def triangulate_edge(
    path: CoordinateArray2D, closed: bool = False
) -> tuple[CoordinateArray2D, CoordinateArray2D, TriangleArray]: ...


@typing.overload
def triangulate_edge(
    path: CoordinateArray3D, closed: bool = False
) -> tuple[CoordinateArray3D, CoordinateArray3D, TriangleArray]: ...


def triangulate_edge(
    path: CoordinateArray, closed: bool = False
) -> tuple[CoordinateArray, CoordinateArray, TriangleArray]:
    """Determines the triangulation of a path.

    The resulting `offsets` can multiplied by a `width` scalar and be added
    to the resulting `centers` to generate the vertices of the triangles for
    the triangulation, i.e. `vertices = centers + width*offsets`. Using the
    `centers` and `offsets` representation thus allows for the computed
    triangulation to be independent of the line width.

    Parameters
    ----------
    path : np.ndarray
        Nx2 or Nx3 array of central coordinates of path to be triangulated
    closed : bool
        Bool which determines if the path is closed or not.

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

    owner = _native.shape_geometry_locals(('path', 'closed', 'idx', 'clean_path', 'centers', 'offsets', 'triangles'), {'path': path, 'closed': closed}, {})
    del path, closed
    return _native.shape_geometry_triangulate_edge(globals(), _shape_geometry_literals[23], owner)


def generate_tube_meshes(path, closed=False, tube_points=10):
    """Generates list of mesh vertices and triangles from a path

    Adapted from vispy.visuals.TubeVisual
    https://github.com/vispy/vispy/blob/main/vispy/visuals/tube.py

    Parameters
    ----------
    path : (N, 3) array
        Vertices specifying the path.
    closed : bool
        Bool which determines if the path is closed or not.
    tube_points : int
        The number of points in the circle-approximating polygon of the
        tube's cross section.

    Returns
    -------
    centers : (M, 3) array
        Vertices of all triangles for the lines
    offsets : (M, D) array
        offsets of all triangles for the lines
    triangles : (P, 3) array
        Vertex indices that form the mesh triangles
    """
    owner = _native.shape_geometry_locals(('path', 'closed', 'tube_points', 'points', '_tangents', 'normals', 'binormals', 'segments', 'grid', 'grid_off', 'i', 'pos', 'normal', 'binormal', 'v', 'cx', 'cy', 'indices', 'j', 'ip', 'jp', 'index_a', 'index_b', 'index_c', 'index_d', 'triangles', 'centers', 'offsets'), {'path': path, 'closed': closed, 'tube_points': tube_points}, {})
    del path, closed, tube_points
    return _native.shape_geometry_generate_tube_meshes(globals(), _shape_geometry_literals[24], owner)


def path_to_mask(
    mask_shape: npt.NDArray, vertices: npt.NDArray
) -> npt.NDArray[np.bool_]:
    """Converts a path to a boolean mask with `True` for points lying along
    each edge.

    Parameters
    ----------
    mask_shape : array (2,)
        Shape of mask to be generated.
    vertices : array (N, 2)
        Vertices of the path.

    Returns
    -------
    mask : np.ndarray
        Boolean array with `True` for points along the path

    """
    owner = _native.shape_geometry_locals(('mask_shape', 'vertices', 'mask', 'duplicates', 'iis', 'jjs', 'v1', 'v2', 'ii', 'jj'), {'mask_shape': mask_shape, 'vertices': vertices}, {})
    del mask_shape, vertices
    return _native.shape_geometry_path_to_mask(globals(), _shape_geometry_literals[25], owner)


def poly_to_mask(
    mask_shape: npt.ArrayLike, vertices: npt.ArrayLike
) -> npt.NDArray[np.bool_]:
    """Converts a polygon to a boolean mask with `True` for points
    lying inside the shape. Uses the bounding box of the vertices to reduce
    computation time.

    Parameters
    ----------
    mask_shape : np.ndarray | tuple
        1x2 array of shape of mask to be generated.
    vertices : np.ndarray
        Nx2 array of the vertices of the polygon.

    Returns
    -------
    mask : np.ndarray
        Boolean array with `True` for points inside the polygon
    """
    owner = _native.shape_geometry_locals(('mask_shape', 'vertices'), {'mask_shape': mask_shape, 'vertices': vertices}, {})
    del mask_shape, vertices
    return _native.shape_geometry_poly_to_mask(globals(), _shape_geometry_literals[26], owner)


def grid_points_in_poly(shape, vertices):
    """Converts a polygon to a boolean mask with `True` for points
    lying inside the shape. Loops through all indices in the grid

    Parameters
    ----------
    shape : np.ndarray | tuple
        1x2 array of shape of mask to be generated.
    vertices : np.ndarray
        Nx2 array of the vertices of the polygon.

    Returns
    -------
    mask : np.ndarray
        Boolean array with `True` for points inside the polygon
    """
    owner = _native.shape_geometry_locals(('shape', 'vertices', 'x', 'y', 'points', 'inside', 'mask'), {'shape': shape, 'vertices': vertices}, {})
    del shape, vertices
    return _native.shape_geometry_grid_points_in_poly(globals(), _shape_geometry_literals[27], owner)


def points_in_poly(points, vertices):
    """Tests points for being inside a polygon using the ray casting algorithm

    Parameters
    ----------
    points : np.ndarray
        Mx2 array of points to be tested
    vertices : np.ndarray
        Nx2 array of the vertices of the polygon.

    Returns
    -------
    inside : np.ndarray
        Length M boolean array with `True` for points inside the polygon
    """
    owner = _native.shape_geometry_locals(('points', 'vertices', 'n_verts', 'inside', 'j', 'i', 'cond_1', 'cond_2', 'cond_3', 'd', 'tolerance', 'cond_4', 'cond_5'), {'points': points, 'vertices': vertices}, {})
    del points, vertices
    return _native.shape_geometry_points_in_poly(globals(), _shape_geometry_literals[28], owner)


def extract_shape_type(data, shape_type=None):
    """Separates shape_type from data if present, and returns both.

    Parameters
    ----------
    data : Array | Tuple(Array,str) | List[Array | Tuple(Array, str)] | Tuple(List[Array], str)
        list or array of vertices belonging to each shape, optionally containing shape type strings
    shape_type : str | None
        metadata shape type string, or None if none was passed

    Returns
    -------
    data : Array | List[Array]
        list or array of vertices belonging to each shape
    shape_type : List[str] | None
        type of each shape in data, or None if none was passed
    """
    # Tuple for one shape or list of shapes with shape_type
    def generator_factory(iterator):
        yield from iterator
    owner = _native.shape_geometry_locals(('data', 'shape_type', 'datum'), {'data': data, 'shape_type': shape_type}, {})
    del data, shape_type
    return _native.shape_geometry_extract_shape_type(globals(), _shape_geometry_literals[29], generator_factory, owner)


def get_default_shape_type(current_type):
    """If all shapes in current_type are of identical shape type,
       return this shape type, else "polygon" as lowest common
       denominator type.

    Parameters
    ----------
    current_type : list of str
        list of current shape types

    Returns
    -------
    default_type : str
        default shape type
    """
    def generator_factory(iterator):
        yield from iterator
    first_type: object
    _cell_first_type = lambda: first_type
    owner = _native.shape_geometry_locals(('current_type', 'default', 'first_type'), {'current_type': current_type}, {'first_type': _cell_first_type.__closure__[0]})
    del current_type
    return _native.shape_geometry_get_default_shape_type(globals(), _shape_geometry_literals[30], generator_factory, owner)


def get_shape_ndim(data):
    """Checks whether data is a list of the same type of shape, one shape, or
    a list of different shapes and returns the dimensionality of the shape/s.

    Parameters
    ----------
    data : (N, ) list of array
        List of shape data, where each element is an (N, D) array of the
        N vertices of a shape in D dimensions.

    Returns
    -------
    ndim : int
        Dimensionality of the shape/s in data
    """
    # list of all the same shapes
    owner = _native.shape_geometry_locals(('data', 'ndim'), {'data': data}, {})
    del data
    return _native.shape_geometry_get_shape_ndim(globals(), _shape_geometry_literals[31], owner)


def number_of_shapes(data):
    """Determine number of shapes in the data.

    Parameters
    ----------
    data : list or np.ndarray
        Can either be no shapes, if empty, a
        single shape or a list of shapes.

    Returns
    -------
    n_shapes : int
        Number of new shapes
    """
    owner = _native.shape_geometry_locals(('data', 'n_shapes'), {'data': data}, {})
    del data
    return _native.shape_geometry_number_of_shapes(globals(), _shape_geometry_literals[32], owner)


def validate_num_vertices(
    data, shape_type, min_vertices=None, valid_vertices=None
):
    """Raises error if a shape in data has invalid number of vertices.

    Checks whether all shapes in data have a valid number of vertices
    for the given shape type and vertex information. Rectangles and
    ellipses can have either 2 or 4 vertices per shape,
    lines can have only 2, while paths and polygons have a minimum
    number of vertices, but no maximum.

    One of valid_vertices or min_vertices must be passed to the
    function.

    Parameters
    ----------
    data : Array | Tuple(Array,str) | List[Array | Tuple(Array, str)] | Tuple(List[Array], str)
        List of shape data, where each element is either an (N, D) array of the
        N vertices of a shape in D dimensions or a tuple containing an array of
        the N vertices and the shape_type string. Can be an 3-dimensional array
        if each shape has the same number of vertices.
    shape_type : str
        Type of shape being validated (for detailed error message)
    min_vertices : int or None
        Minimum number of vertices for the shape type, by default None
    valid_vertices : Tuple(int) or None
        Valid number of vertices for the shape type in data, by default None

    Raises
    ------
    ValueError
        Raised if a shape is found with invalid number of vertices
    """
    owner = _native.shape_geometry_locals(('data', 'shape_type', 'min_vertices', 'valid_vertices', 'n_shapes', 'shape'), {'data': data, 'shape_type': shape_type, 'min_vertices': min_vertices, 'valid_vertices': valid_vertices}, {})
    del data, shape_type, min_vertices, valid_vertices
    return _native.shape_geometry_validate_num_vertices(globals(), _shape_geometry_literals[33], owner)


def perpendicular_distance(
    point: npt.NDArray, line_start: npt.NDArray, line_end: npt.NDArray
) -> float:
    """Calculate the perpendicular distance of a point to a given euclidean line.

    Calculates the shortest distance of a point to a euclidean line defined by a line_start point and a line_end point.
    Works up to any dimension.

    Parameters
    ---------
    point : np.ndarray
        A point defined by a numpy array of shape (viewer.ndims,)  which is part of a polygon shape.
    line_start : np.ndarray
        A point defined by a numpy array of shape (viewer.ndims,)  used to define the starting point of a line.
    line_end : np.ndarray
        A point defined by a numpy array of shape (viewer.ndims,)  used to define the end point of a line.

    Returns
    -------
    float
        A float number representing the distance of point to a euclidean line defined by line_start and line_end.
    """

    owner = _native.shape_geometry_locals(('point', 'line_start', 'line_end', 't'), {'point': point, 'line_start': line_start, 'line_end': line_end}, {})
    del point, line_start, line_end
    return _native.shape_geometry_perpendicular_distance(globals(), _shape_geometry_literals[34], owner)


def rdp(vertices: npt.NDArray, epsilon: float) -> npt.NDArray:
    """Reduce the number of vertices that make up a polygon.

    Implementation of the Ramer-Douglas-Peucker algorithm based on:
    https://github.com/fhirschmann/rdp/blob/master/rdp. This algorithm reduces the amounts of points in a polyline or
    in this case reduces the number of vertices in a polygon shape.

    Parameters
    ----------
    vertices : np.ndarray
        A numpy array of shape (n, viewer.ndims) containing the vertices of a polygon shape.
    epsilon : float
        A float representing the maximum distance threshold. When the perpendicular distance of a point to a given line
        is higher, subsequent refinement occurs.

    Returns
    -------
    np.ndarray
        A numpy array of shape (n, viewer.ndims) containing the vertices of a polygon shape.
    """
    owner = _native.shape_geometry_locals(('vertices', 'epsilon', 'max_distance_index', 'max_distance', 'i', 'd', 'l1', 'l2'), {'vertices': vertices, 'epsilon': epsilon}, {})
    del vertices, epsilon
    return _native.shape_geometry_rdp(globals(), _shape_geometry_literals[35], owner)


def _save_failed_triangulation(
    data: np.ndarray, target_dir: str | None = None, backend: str = ''
) -> tuple[str, str]:
    """Save data to temporary files for debugging.

    This function saves input data when triangulation fails.
    It saves the same data to both a .npz file and a .txt file within the
    temporary directory, and returns the paths to the saved files.

    Parameters
    ----------
    data : np.ndarray
        The data to save.
    target_dir: str or None
        Path to the directory where the files will be saved.
        If None, the default
    backend: str, options
        The backend used for triangulation. This is used to generate the
        filename prefix.

    Returns
    -------
    tuple[str, str]
        The paths to the saved files.

    Notes
    -----
    Use TMPDIR environment variable to set the temporary directory.
    """
    owner = _native.shape_geometry_locals(('data', 'target_dir', 'backend', 'binary_file', 'text_file'), {'data': data, 'target_dir': target_dir, 'backend': backend}, {})
    del data, target_dir, backend
    try:
        return _native.shape_geometry_save_failed_triangulation(globals(), _shape_geometry_literals[36], owner)
    except:
        _native.shape_geometry_restore_context_traceback(owner)
        raise
