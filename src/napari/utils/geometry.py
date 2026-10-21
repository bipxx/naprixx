from naparixx import _native

from naparixx._backend import dispatch as _native_dispatch
import numpy as np
import numpy.typing as npt

# normal vectors for a 3D axis-aligned box
# coordinates are ordered [z, y, x]
FACE_NORMALS = {
    'x_pos': np.array([0, 0, 1]),
    'x_neg': np.array([0, 0, -1]),
    'y_pos': np.array([0, 1, 0]),
    'y_neg': np.array([0, -1, 0]),
    'z_pos': np.array([1, 0, 0]),
    'z_neg': np.array([-1, 0, 0]),
}


@_native_dispatch("projection_adapter")
def project_points_onto_plane(
    points: np.ndarray, plane_point: np.ndarray, plane_normal: np.ndarray
) -> tuple[np.ndarray, np.ndarray]:
    """Project points on to a plane.

    Plane is defined by a point and a normal vector. This function
    is designed to work with points and planes in 3D.

    Parameters
    ----------
    points : np.ndarray
        The coordinate of the point to be projected. The points
        should be 3D and have shape shape (N,3) for N points.
    plane_point : np.ndarray
        The point on the plane used to define the plane.
        Should have shape (3,).
    plane_normal : np.ndarray
        The normal vector used to define the plane.
        Should be a unit vector and have shape (3,).

    Returns
    -------
    projected_point : np.ndarray
        The point that has been projected to the plane.
        This is always an Nx3 array.
    signed_distance_to_plane : np.ndarray
        The signed projection distance between the points and the plane.
        Positive values indicate the point is on the positive normal side of the plane.
        Negative values indicate the point is on the negative normal side of the plane.
    """
    owner = _native.viewer_geometry_projection_locals(points, plane_point, plane_normal)
    del points, plane_point, plane_normal
    return _native.viewer_geometry_project_points(globals(), owner)


def rotation_matrix_from_vectors_2d(
    vec_1: np.ndarray, vec_2: np.ndarray
) -> np.ndarray:
    """Calculate the 2D rotation matrix to rotate vec_1 onto vec_2

    Parameters
    ----------
    vec_1 : np.ndarray
        The (2,) array containing the starting vector.
    vec_2 : np.ndarray
        The (2,) array containing the destination vector.

    Returns
    -------
    rotation_matrix : np.ndarray
        The (2, 2) tranformation matrix that rotates vec_1 to vec_2.
    """
    # ensure unit vectors
    vec_1 = vec_1 / np.linalg.norm(vec_1)
    vec_2 = vec_2 / np.linalg.norm(vec_2)

    # calculate the rotation matrix
    diagonal_1 = (vec_1[0] * vec_2[0]) + (vec_1[1] * vec_2[1])
    diagonal_2 = (vec_1[0] * vec_2[1]) - (vec_2[0] * vec_1[0])
    rotation_matrix = np.array(
        [[diagonal_1, -1 * diagonal_2], [diagonal_2, diagonal_1]]
    )

    return rotation_matrix


def rotation_matrix_from_vectors_3d(
    vec_1: np.ndarray, vec_2: np.ndarray
) -> np.ndarray:
    """Calculate the rotation matrix that aligns vec1 to vec2.

    Parameters
    ----------
    vec_1 : np.ndarray
        The vector you want to rotate
    vec_2 : np.ndarray
        The vector you would like to align to.

    Returns
    -------
    rotation_matrix : np.ndarray
        The rotation matrix that aligns vec_1 with vec_2.
        That is rotation_matrix.dot(vec_1) == vec_2
    """
    owner = _native.viewer_geometry_rotation_locals(vec_1, vec_2)
    del vec_1, vec_2
    return _native.viewer_geometry_rotation_3d(globals(), owner)


def rotate_points(
    points: np.ndarray,
    current_plane_normal: np.ndarray,
    new_plane_normal: np.ndarray,
) -> tuple[np.ndarray, np.ndarray]:
    """Rotate points using a rotation matrix defined by the rotation from
    current_plane to new_plane.

    Parameters
    ----------
    points : np.ndarray
        The points to rotate. They should all lie on the same plane with the
        normal vector current_plane_normal. Should be (NxD) array.
    current_plane_normal : np.ndarray
        The normal vector for the plane the points currently reside on.
    new_plane_normal : np.ndarray
        The normal vector for the plane the points will be rotated to.

    Returns
    -------
    rotated_points : np.ndarray
        The points that have been rotated
    rotation_matrix : np.ndarray
        The rotation matrix used for rotating the points.
    """
    owner = _native.viewer_geometry_locals()
    return _native.viewer_geometry_rotate_points(points, current_plane_normal, new_plane_normal, globals(), owner)


def point_in_bounding_box(point: np.ndarray, bounding_box: np.ndarray) -> bool:
    """Determine whether an nD point is inside an nD bounding box.

    Parameters
    ----------
    point : np.ndarray
        (n,) array containing nD point coordinates to check.
    bounding_box : np.ndarray
        (2, n) array containing the min and max of the nD bounding box.
        As returned by `Layer._extent_data`.
    """
    return bool(
        np.all(point >= bounding_box[0]) and np.all(point <= bounding_box[1])
    )


def clamp_point_to_bounding_box(point: np.ndarray, bounding_box: np.ndarray):
    """Ensure that a point is inside of the bounding box. If the point has a
    coordinate outside of the bounding box, the value is clipped to the max
    extent of the bounding box.

    Parameters
    ----------
    point : np.ndarray
        n-dimensional point as an (n,) ndarray. Multiple points can
        be passed as an (n, D) array.
    bounding_box : np.ndarray
        n-dimensional bounding box as a (n, 2) ndarray

    Returns
    -------
    clamped_point : np.ndarray
        `point` clamped to the limits of `bounding_box`
    """
    owner = _native.viewer_geometry_locals()
    return _native.viewer_geometry_clamp_point(point, bounding_box, globals(), owner)


def face_coordinate_from_bounding_box(
    bounding_box: np.ndarray, face_normal: np.ndarray
) -> float:
    """Get the coordinate for a given face in an axis-aligned bounding box.
    For example, if the bounding box has extents [[0, 10], [0, 20], [0, 30]]
    (ordered zyx), then the face with normal [0, 1, 0] is described by
    y=20. Thus, the face_coordinate in this case is 20.

    Parameters
    ----------
    bounding_box : np.ndarray
        n-dimensional bounding box as a (n, 2) ndarray.
        Each row should contain the [min, max] extents for the
        axis.
    face_normal : np.ndarray
        normal vector of the face as an (n,)  ndarray

    Returns
    -------
    face_coordinate : float
        The value where the bounding box face specified by face_normal intersects
        the axis its normal is aligned with.
    """
    owner = _native.viewer_geometry_locals()
    return _native.viewer_geometry_face_coordinate(bounding_box, face_normal, globals(), owner)


def intersect_line_with_axis_aligned_plane(
    plane_intercept: float,
    plane_normal: np.ndarray,
    line_start: np.ndarray,
    line_direction: np.ndarray,
) -> np.ndarray:
    """Find the intersection of a line with an axis aligned plane.

    Parameters
    ----------
    plane_intercept : float
        The coordinate that the plane intersects on the axis to which plane is
        normal.
        For example, if the plane is described by y=42, plane_intercept is 42.
    plane_normal : np.ndarray
        normal vector of the plane as an (n,)  ndarray
    line_start : np.ndarray
        start point of the line as an (n,) ndarray
    line_direction : np.ndarray
        direction vector of the line as an (n,) ndarray

    Returns
    -------
    intersection_point : np.ndarray
        point where the line intersects the axis aligned plane
    """
    # find the axis the plane exists in
    owner = _native.viewer_geometry_locals()
    return _native.viewer_geometry_axis_plane(plane_intercept, plane_normal, line_start, line_direction, globals(), owner)


def bounding_box_to_face_vertices(
    bounding_box: np.ndarray,
) -> dict[str, np.ndarray]:
    """From a layer bounding box (N, 2), N=ndim, return a dictionary containing
    the vertices of each face of the bounding_box.

    Parameters
    ----------
    bounding_box : np.ndarray
        (N, 2), N=ndim array with the min and max value for each dimension of
        the bounding box. The bounding box is take form the last
        three rows, which are assumed to be in order (z, y, x).

    Returns
    -------
    face_coords : Dict[str, np.ndarray]
        A dictionary containing the coordinates for the vertices for each face.
        The keys are strings: 'x_pos', 'x_neg', 'y_pos', 'y_neg', 'z_pos', 'z_neg'.
        'x_pos' is the face with the normal in the positive x direction and
        'x_neg' is the face with the normal in the negative direction.
        Coordinates are ordered (z, y, x).
    """
    owner = _native.viewer_geometry_locals()
    return _native.viewer_geometry_bbox_faces(bounding_box, globals(), ("x_pos", "x_neg", "y_pos", "y_neg", "z_pos", "z_neg"), owner)


def inside_triangles(triangles):
    """Checks which triangles contain the origin

    Parameters
    ----------
    triangles : (N, 3, 2) array
        Array of N triangles that should be checked

    Returns
    -------
    inside : (N,) array of bool
        Array with `True` values for triangles containing the origin
    """
    owner = _native.viewer_geometry_locals()
    return _native.viewer_geometry_inside_triangles(triangles, globals(), owner)


def intersect_line_with_plane_3d(
    line_position: npt.ArrayLike,
    line_direction: npt.ArrayLike,
    plane_position: npt.ArrayLike,
    plane_normal: npt.ArrayLike,
) -> np.ndarray:
    """Find the intersection of a line with an arbitrarily oriented plane in 3D.
    The line is defined by a position and a direction vector.
    The plane is defined by a position and a normal vector.
    https://en.wikipedia.org/wiki/Line%E2%80%93plane_intersection
    Parameters
    ----------
    line_position : np.ndarray
        a position on a 3D line with shape (3,).
    line_direction : np.ndarray
        direction of the 3D line with shape (3,).
    plane_position : np.ndarray
        a position on a plane in 3D with shape (3,).
    plane_normal : np.ndarray
        a vector normal to the plane in 3D with shape (3,).
    Returns
    -------
    plane_intersection : np.ndarray
        the intersection of the line with the plane, shape (3,)
    """
    # cast to arrays
    line_position = np.asarray(line_position, dtype=float)
    line_direction = np.asarray(line_direction, dtype=float)
    plane_position = np.asarray(plane_position, dtype=float)
    plane_normal = np.asarray(plane_normal, dtype=float)

    # project direction between line and plane onto the plane normal
    line_plane_direction = plane_position - line_position
    line_plane_on_plane_normal = np.dot(line_plane_direction, plane_normal)

    # project line direction onto the plane normal
    line_direction_on_plane_normal = np.dot(line_direction, plane_normal)

    # find scale factor for line direction
    scale_factor = line_plane_on_plane_normal / line_direction_on_plane_normal

    return line_position + (scale_factor * line_direction)


def intersect_line_with_multiple_planes_3d(
    line_position: np.ndarray,
    line_direction: np.ndarray,
    plane_position: np.ndarray,
    plane_normal: np.ndarray,
) -> np.ndarray:
    """Find the intersection of a line with multiple arbitrarily oriented planes in 3D.
    The line is defined by a position and a direction vector.
    The plane is defined by a position and a normal vector.
    https://en.wikipedia.org/wiki/Line%E2%80%93plane_intersection

    Parameters
    ----------
    line_position : np.ndarray
        a position on a 3D line with shape (3,).
    line_direction : np.ndarray
        direction of the 3D line with shape (3,).
    plane_position : np.ndarray
        point on a plane in 3D with shape (n, 3) for n planes.
    plane_normal : np.ndarray
        a vector normal to the plane in 3D with shape (n,3) for n planes.

    Returns
    -------
    plane_intersection : np.ndarray
        the intersection of the line with the plane, shape (3,)
    """
    # cast to arrays
    owner = _native.viewer_geometry_planes_locals(line_position, line_direction, plane_position, plane_normal)
    del line_position, line_direction, plane_position, plane_normal
    return _native.viewer_geometry_multiple_planes(globals(), owner)


def intersect_line_with_triangles(
    line_point: np.ndarray, line_direction: np.ndarray, triangles: np.ndarray
) -> np.ndarray:
    """Find the intersection of a ray with a set of triangles.

    This function does not test whether the ray intersects the triangles, so you should
    have tested for intersection first. See line_in_triangles_3d() for testing for
    intersection.

    Parameters
    ----------
    line_point : np.ndarray
        The (3,) array containing the starting point of the ray.
    line_direction : np.ndarray
        The (3,) array containing the unit vector in the direction of the ray.
    triangles : np.ndarray
        The 3D vertices of the triangles. Should be (n, 3, 3) for n triangles. Axis 1
        indexes each vertex and axis 2 contains the coordinates. That to access the
        0th vertex from triangle index 3, one would use: triangles[3, 0, :].

    Returns
    -------
    intersection_points : np.ndarray
        (n, 3) array containing the point at which the specified ray intersects
        the each triangle.
    """
    owner = _native.viewer_geometry_locals()
    return _native.viewer_geometry_triangle_intersections(line_point, line_direction, triangles, globals(), owner)


def point_in_quadrilateral_2d(
    point: np.ndarray, quadrilateral: np.ndarray
) -> bool:
    """Determines whether a point is inside a 2D quadrilateral.

    Parameters
    ----------
    point : np.ndarray
        (2,) array containing coordinates of a point.
    quadrilateral : np.ndarray
        (4, 2) array containing the coordinates for the 4 corners
        of a quadrilateral. The vertices should be in clockwise order
        such that indexing with [0, 1, 2], and [0, 2, 3] results in
        the two non-overlapping triangles that divide the
        quadrilateral.

    Returns
    -------

    """
    owner = _native.viewer_geometry_locals()
    return _native.viewer_geometry_point_quadrilateral(point, quadrilateral, globals(), owner)


def line_in_quadrilateral_3d(
    line_point: np.ndarray,
    line_direction: np.ndarray,
    quadrilateral: np.ndarray,
) -> bool:
    """Determine if a line goes tbrough any of a  set of quadrilaterals.

    For example, this could be used to determine if a click was
    in a specific face of a bounding box.

    Parameters
    ----------
    line_point : np.ndarray
        (3,) array containing the location that was clicked. This
        should be in the same coordinate system as the vertices.
    line_direction : np.ndarray
        (3,) array describing the direction camera is pointing in
        the scene. This should be in the same coordinate system as
        the vertices.
    quadrilateral : np.ndarray
        (4, 3) array containing the coordinates for the 4 corners
        of a quadrilateral. The vertices should be in clockwise order
        such that indexing with [0, 1, 2], and [0, 2, 3] results in
        the two non-overlapping triangles that divide the quadrilateral.


    Returns
    -------
    in_region : bool
        True if the click is in the region specified by vertices.
    """

    # project the vertices of the bound region on to the view plane
    owner = _native.viewer_geometry_locals()
    return _native.viewer_geometry_line_quadrilateral(line_point, line_direction, quadrilateral, globals(), owner)


def line_in_triangles_3d(
    line_point: np.ndarray, line_direction: np.ndarray, triangles: np.ndarray
):
    """Determine if a line goes through any of a set of triangles.

    For example, this could be used to determine if a click was
    in a triangle of a mesh.

    Parameters
    ----------
    line_point : np.ndarray
        (3,) array containing the location that was clicked. This
        should be in the same coordinate system as the vertices.
    line_direction : np.ndarray
        (3,) array describing the direction camera is pointing in
        the scene. This should be in the same coordinate system as
        the vertices.
    triangles : np.ndarray
        (n, 3, 3) array containing the coordinates for the 3 corners
        of n triangles.

    Returns
    -------
    in_triangles : np.ndarray
        (n,) boolean array that is True of the ray intersects the triangle
    """
    owner = _native.viewer_geometry_locals()
    return _native.viewer_geometry_line_triangles(line_point, line_direction, triangles, globals(), owner)


def find_front_back_face(
    click_pos: np.ndarray, bounding_box: np.ndarray, view_dir: np.ndarray
):
    """Find the faces of an axis aligned bounding box a
    click intersects with.

    Parameters
    ----------
    click_pos : np.ndarray
        (3,) array containing the location that was clicked.
    bounding_box : np.ndarray
        (N, 2), N=ndim array with the min and max value for each dimension of
        the bounding box. The bounding box is take form the last
        three rows, which are assumed to be in order (z, y, x).
        This should be in the same coordinate system as click_pos.
    view_dir
        (3,) array describing the direction camera is pointing in
        the scene. This should be in the same coordinate system as click_pos.

    Returns
    -------
    front_face_normal : np.ndarray
        The (3,) normal vector of the face closest to the camera the click
        intersects with.
    back_face_normal : np.ndarray
        The (3,) normal vector of the face farthest from the camera the click
        intersects with.
    """
    owner = _native.viewer_geometry_locals()
    return _native.viewer_geometry_front_back_face(click_pos, bounding_box, view_dir, globals(), owner)


def intersect_line_with_axis_aligned_bounding_box_3d(
    line_point: np.ndarray,
    line_direction: np.ndarray,
    bounding_box: np.ndarray,
    face_normal: np.ndarray,
):
    """Find the intersection of a ray with the specified face of an
    axis-aligned bounding box.

    Parameters
    ----------
    face_normal : np.ndarray
        The (3,) normal vector of the face the click intersects with.
    line_point : np.ndarray
        (3,) array containing the location that was clicked.
    bounding_box : np.ndarray
        (N, 2), N=ndim array with the min and max value for each dimension of
        the bounding box. The bounding box is take form the last
        three rows, which are assumed to be in order (z, y, x).
        This should be in the same coordinate system as click_pos.
    line_direction
        (3,) array describing the direction camera is pointing in
        the scene. This should be in the same coordinate system as click_pos.

    Returns
    -------
    intersection_point : np.ndarray
        (3,) array containing the coordinate for the intersection of the click on
        the specified face.
    """
    owner = _native.viewer_geometry_locals()
    return _native.viewer_geometry_bbox_intersection(line_point, line_direction, bounding_box, face_normal, globals(), owner)


def distance_between_point_and_line_3d(
    point: np.ndarray, line_position: np.ndarray, line_direction: np.ndarray
):
    """Determine the minimum distance between a point and a line in 3D.

    Parameters
    ----------
    point : np.ndarray
        (3,) array containing coordinates of a point in 3D space.
    line_position : np.ndarray
        (3,) array containing coordinates of a point on a line in 3D space.
    line_direction : np.ndarray
        (3,) array containing a vector describing the direction of a line in
        3D space.

    Returns
    -------
    distance : float
        The minimum distance between `point` and the line defined by
        `line_position` and `line_direction`.
    """
    line_direction_normalized = line_direction / np.linalg.norm(line_direction)
    projection_on_line_direction = np.dot(
        (point - line_position), line_direction
    )
    closest_point_on_line = (
        line_position
        + line_direction_normalized * projection_on_line_direction
    )
    distance = np.linalg.norm(point - closest_point_on_line)
    return distance


def find_nearest_triangle_intersection(
    ray_position: np.ndarray, ray_direction: np.ndarray, triangles: np.ndarray
) -> tuple[int | None, np.ndarray | None]:
    """Given an array of triangles, find the index and intersection location
    of a ray and the nearest triangle.

    This returns only the triangle closest to the the ray_position.

    Parameters
    ----------
    ray_position : np.ndarray
        The coordinate of the starting point of the ray.
    ray_direction : np.ndarray
        A unit vector describing the direction of the ray.
    triangles : np.ndarray
        (N, 3, 3) array containing the vertices of the triangles.

    Returns
    -------
    closest_intersected_triangle_index : int
        The index of the intersected triangle.
    intersection : np.ndarray
        The coordinate of where the ray intersects the triangle.
    """
    owner = _native.viewer_geometry_locals()
    return _native.viewer_geometry_nearest_triangle(ray_position, ray_direction, triangles, globals(), owner)


def get_center_bbox(roi: np.ndarray) -> tuple[list[float], int, int]:
    """Get the center coordinate, height, width of the roi.

    Parameters
    ----------
    roi : np.ndarray
        An array of shape (4, 2) representing a rectangular roi.

    Returns
    -------
    center_coords: list[float, float]
        center y and x coordinates of the roi
    height: int
        height of the roi in data pixels
    width: int
        width of the roi in data pixels

    """
    height, width = roi.max(axis=0) - roi.min(axis=0)
    min_y, min_x = roi.min(axis=0)
    center_coords = [min_y + height / 2, min_x + width / 2]

    return center_coords, height, width
