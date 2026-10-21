from naparixx import _native
import numpy as np


def calculate_barycentric_coordinates(
    point: np.ndarray, triangle_vertices: np.ndarray
) -> np.ndarray:
    """Calculate the barycentric coordinates for a point in a triangle.

    http://gamedev.stackexchange.com/questions/23743/whats-the-most-efficient-way-to-find-barycentric-coordinates

    Parameters
    ----------
    point : np.ndarray
        The coordinates of the point for which to calculate the barycentric coordinate.
    triangle_vertices : np.ndarray
        (3, D) array containing the triangle vertices.

    Returns
    -------
    barycentric_coorinates : np.ndarray
        The barycentric coordinate [u, v, w], where u, v, and w are the
        barycentric coordinates for the first, second, third triangle
        vertex, respectively.
    """
    owner = _native.viewer_geometry_locals()
    return _native.viewer_geometry_barycentric(point, triangle_vertices, globals(), owner)
