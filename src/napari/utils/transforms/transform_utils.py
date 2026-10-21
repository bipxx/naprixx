import numpy as np
from naparixx import native as _native
import numpy.typing as npt


def compose_linear_matrix(rotate, scale, shear) -> npt.NDArray:
    """Compose linear transform matrix from rotate, shear, scale.

    Parameters
    ----------
    rotate : float, 3-tuple of float, or n-D array.
        If a float convert into a 2D rotation matrix using that value as an
        angle. If 3-tuple convert into a 3D rotation matrix, using a yaw,
        pitch, roll convention. Otherwise assume an nD rotation. Angles are
        assumed to be in degrees. They can be converted from radians with
        np.degrees if needed.
    scale : 1-D array
        A 1-D array of factors to scale each axis by. Scale is broadcast to 1
        in leading dimensions, so that, for example, a scale of [4, 18, 34] in
        3D can be used as a scale of [1, 4, 18, 34] in 4D without modification.
        An empty translation vector implies no scaling.
    shear : 1-D array or n-D array
        Either a vector of upper triangular values, or an nD shear matrix with
        ones along the main diagonal.

    Returns
    -------
    matrix : array
        nD array representing the composed linear transform.
    """
    locals_owner = _native.policy_transform_locals()
    def generator_factory(iterator):
        yield from iterator

    return _native.policy_compose_linear_matrix(
        rotate, scale, shear, globals(), locals_owner, generator_factory
    )


def infer_ndim(
    *, scale=None, translate=None, rotate=None, shear=None, linear_matrix=None
):
    """Infer the dimensionality of a transformation from its input components.

    This is most useful when the dimensions of the inputs do not match, either
    when broadcasting is desired or when an input represents a parameterization
    of a transform component (e.g. rotate as an angle of a 2D rotation).

    Parameters
    ----------
    rotate : float, 3-tuple of float, or n-D array.
        If a float convert into a 2D rotation matrix using that value as an
        angle. If 3-tuple convert into a 3D rotation matrix, using a yaw,
        pitch, roll convention. Otherwise assume an nD rotation. Angles are
        assumed to be in degrees. They can be converted from radians with
        np.degrees if needed.
    translate : 1-D array
        A 1-D array of factors to shift each axis by. Translation is broadcast
        to 0 in leading dimensions, so that, for example, a translation of
        [4, 18, 34] in 3D can be used as a translation of [0, 4, 18, 34] in 4D
        without modification. An empty translation vector implies no
        translation.
    scale : 1-D array
        A 1-D array of factors to scale each axis by. Scale is broadcast to 1
        in leading dimensions, so that, for example, a scale of [4, 18, 34] in
        3D can be used as a scale of [1, 4, 18, 34] in 4D without modification.
        An empty translation vector implies no scaling.
    shear : 1-D array or n-D array
        Either a vector of upper triangular values, or an nD shear matrix with
        ones along the main diagonal.

    Returns
    -------
    ndim : int
        The maximum dimensionality of the component inputs.
    """
    locals_owner = _native.policy_transform_locals()
    return _native.policy_infer_ndim(
        scale, translate, rotate, shear, linear_matrix, globals(), locals_owner
    )


def translate_to_vector(translate, *, ndim):
    """Convert a translate input into an n-dimensional transform component.

    Parameters
    ----------
    translate : 1-D array
        A 1-D array of factors to shift each axis by. Translation is padded
        with 0 in leading dimensions, so that, for example, a translation of
        [4, 18, 34] in 3D can be used as a translation of [0, 4, 18, 34] in 4D
        without modification. An empty vector implies no translation.
    ndim : int
        The desired dimensionality of the output transform component.

    Returns
    -------
    np.ndarray
        The translate component as a 1D numpy array of length ndim.
    """
    locals_owner = _native.policy_transform_locals()
    return _native.policy_translate_to_vector(translate, ndim, globals(), locals_owner)


def scale_to_vector(scale, *, ndim):
    """Convert a scale input into an n-dimensional transform component.

    Parameters
    ----------
    scale : 1-D array
        A 1-D array of factors to scale each axis by. Scale is padded with 1
        in leading dimensions, so that, for example, a scale of [4, 18, 34] in
        3D can be used as a scale of [1, 4, 18, 34] in 4D without modification.
        An empty vector implies no scaling.
    ndim : int
        The desired dimensionality of the output transform component.

    Returns
    -------
    np.ndarray
        The scale component as a 1D numpy array of length ndim.
    """
    locals_owner = _native.policy_transform_locals()
    return _native.policy_scale_to_vector(scale, ndim, globals(), locals_owner)


def rotate_to_matrix(rotate, *, ndim):
    """Convert a rotate input into an n-dimensional transform component.

    Parameters
    ----------
    rotate : float, 3-tuple of float, or n-D array.
        If a float convert into a 2D rotation matrix using that value as an
        angle. If 3-tuple convert into a 3D rotation matrix, using a yaw,
        pitch, roll convention. Otherwise assume an nD rotation. Angles are
        assumed to be in degrees. They can be converted from radians with
        np.degrees if needed.
    ndim : int
        The desired dimensionality of the output transform component.

    Returns
    -------
    np.ndarray
        The rotate component as a 2D numpy array of size ndim.
    """
    locals_owner = _native.policy_transform_locals()
    return _native.policy_rotate_to_matrix(rotate, ndim, globals(), locals_owner)


def _make_rotate_mat(rotate):
    return _native.policy_make_rotate_mat(rotate, globals())


def _make_2d_rotation(theta_degrees):
    """Makes a 2D rotation matrix from an angle in degrees."""
    locals_owner = _native.policy_transform_locals()
    return _native.policy_make_2d_rotation(theta_degrees, globals(), locals_owner)


def _make_3d_rotation(alpha_degrees, beta_degrees, gamma_degrees):
    """Makes a 3D rotation matrix from roll, pitch, and yaw in degrees.

    For more details, see: https://en.wikipedia.org/wiki/Rotation_matrix#General_rotations
    """
    locals_owner = _native.policy_transform_locals()
    return _native.policy_make_3d_rotation(
        alpha_degrees, beta_degrees, gamma_degrees, globals(), locals_owner
    )


def _cos_sin_degrees(angle_degrees):
    locals_owner = _native.policy_transform_locals()
    return _native.policy_cos_sin_degrees(angle_degrees, globals(), locals_owner)


def shear_to_matrix(shear, *, ndim):
    """Convert a shear input into an n-dimensional transform component.

    Parameters
    ----------
    shear : 1-D array or n-D array
        Either a vector of upper triangular values, or an nD shear matrix with
        ones along the main diagonal.
    ndim : int
        The desired dimensionality of the output transform matrix.

    Returns
    -------
    np.ndarray
        The shear component as a triangular 2D numpy array of size ndim.
    """
    locals_owner = _native.policy_transform_locals()
    return _native.policy_shear_to_matrix(shear, ndim, globals(), locals_owner)


def _make_shear_mat(shear):
    # Check if an upper-triangular representation of shear or
    # a full nD shear matrix has been passed
    return _native.policy_make_shear_mat(shear, globals())


def expand_upper_triangular(vector):
    """Expand a vector into an upper triangular matrix.

    Decomposition is based on code from https://github.com/matthew-brett/transforms3d.
    In particular, the `striu2mat` function in the `shears` module.
    https://github.com/matthew-brett/transforms3d/blob/0.3.1/transforms3d/shears.py#L30-L77.

    Parameters
    ----------
    vector : np.array
        1D vector of length M

    Returns
    -------
    upper_tri : np.array shape (N, N)
        Upper triangular matrix.
    """
    locals_owner = _native.policy_transform_locals()
    return _native.policy_expand_upper_triangular(vector, globals(), locals_owner)


def embed_in_identity_matrix(matrix, ndim):
    """Embed an MxM matrix bottom right of larger NxN identity matrix.

    Parameters
    ----------
    matrix : np.array
        2D square matrix, MxM.
    ndim : int
        Integer with N >= M.

    Returns
    -------
    full_matrix : np.array shape (N, N)
        Larger matrix.
    """
    locals_owner = _native.policy_transform_locals()
    return _native.policy_embed_in_identity_matrix(
        matrix, ndim, globals(), locals_owner
    )


def decompose_linear_matrix(
    matrix, upper_triangular=True
) -> tuple[npt.NDArray, npt.NDArray, npt.NDArray]:
    """Decompose linear transform matrix into rotate, scale, shear.

    Decomposition is based on code from https://github.com/matthew-brett/transforms3d.
    In particular, the `decompose` function in the `affines` module.
    https://github.com/matthew-brett/transforms3d/blob/0.3.1/transforms3d/affines.py#L156-L246.

    Parameters
    ----------
    matrix : np.array shape (N, N)
        nD array representing the composed linear transform.
    upper_triangular : bool
        Whether to decompose shear into an upper triangular or
        lower triangular matrix.

    Returns
    -------
    rotate : float, 3-tuple of float, or n-D array.
        If a float convert into a 2D rotation matrix using that value as an
        angle. If 3-tuple convert into a 3D rotation matrix, using a yaw,
        pitch, roll convention. Otherwise assume an nD rotation. Angles are
        assumed to be in degrees. They can be converted from radians with
        np.degrees if needed.
    scale : 1-D array
        A 1-D array of factors to scale each axis by. Scale is broadcast to 1
        in leading dimensions, so that, for example, a scale of [4, 18, 34] in
        3D can be used as a scale of [1, 4, 18, 34] in 4D without modification.
        An empty translation vector implies no scaling.
    shear : 1-D array or n-D array
        1-D array of upper triangular values or an n-D matrix if lower
        triangular.
    """
    locals_owner = _native.policy_transform_locals()
    return _native.policy_decompose_linear_matrix(
        matrix, upper_triangular, globals(), locals_owner, (-1, 1)
    )


def shear_matrix_from_angle(angle, ndim=3, axes=(-1, 0)):
    """Create a shear matrix from an angle.

    Parameters
    ----------
    angle : float
        Angle in degrees.
    ndim : int
        Dimensionality of the shear matrix
    axes : 2-tuple of int
        Location of the angle in the shear matrix.
        Default is the lower left value.

    Returns
    -------
    matrix : np.ndarray
        Shear matrix with ones along the main diagonal
    """
    locals_owner = _native.policy_transform_locals()
    return _native.policy_shear_matrix_from_angle(
        angle, ndim, axes, globals(), locals_owner
    )


def is_matrix_upper_triangular(matrix):
    """Check if a matrix is upper triangular.

    Parameters
    ----------
    matrix : np.ndarray
        Matrix to be checked.

    Returns
    -------
    bool
        Whether matrix is upper triangular or not.
    """
    return _native.policy_is_matrix_upper_triangular(matrix, globals())


def is_matrix_lower_triangular(matrix):
    """Check if a matrix is lower triangular.

    Parameters
    ----------
    matrix : np.ndarray
        Matrix to be checked.

    Returns
    -------
    bool
        Whether matrix is lower triangular or not.
    """
    return _native.policy_is_matrix_lower_triangular(matrix, globals())


def is_matrix_triangular(matrix):
    """Check if a matrix is triangular.

    Parameters
    ----------
    matrix : np.ndarray
        Matrix to be checked.

    Returns
    -------
    bool
        Whether matrix is triangular or not.
    """
    return _native.policy_is_matrix_triangular(matrix, globals())


def is_diagonal(matrix, tol=1e-8):
    """Determine whether a matrix is diagonal up to some tolerance.

    Parameters
    ----------
    matrix : 2-D array
        The matrix to test.
    tol : float, optional
        Consider any entries with magnitude < `tol` as 0.

    Returns
    -------
    is_diag : bool
        True if matrix is diagonal, False otherwise.
    """
    locals_owner = _native.policy_transform_locals()
    return _native.policy_is_diagonal(matrix, tol, globals(), locals_owner)
