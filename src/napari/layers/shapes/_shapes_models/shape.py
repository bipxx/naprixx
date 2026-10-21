from __future__ import annotations
from naparixx import _native

import sys
from abc import ABC, abstractmethod
from functools import cached_property
from typing import Literal

import numpy as np
import numpy.typing as npt

from napari.layers.shapes._accelerated_triangulate_dispatch import (
    remove_path_duplicates,
)
from napari.layers.shapes._shapes_utils import (
    _save_failed_triangulation,
    find_planar_axis,
    is_collinear,
    path_to_mask,
    poly_to_mask,
    triangulate_edge,
    triangulate_face,
    triangulate_face_and_edges,
    triangulate_face_triangle,
    triangulate_face_vispy,
)
from napari.layers.shapes.shape_types import (
    BoxArray,
    CoordinateArray,
    TriangleArray,
)
from napari.utils.misc import argsort
from napari.utils.triangulation_backend import TriangulationBackend

try:
    import bermuda
except ImportError:
    bermuda = None

try:
    from PartSegCore_compiled_backend import (
        triangulate as partsegcore_triangulate,
    )
except ImportError:
    partsegcore_triangulate = None


TRIANGULATION_BACKEND = TriangulationBackend.pure_python


class Shape(_native.ShapeModelNamespaceBase, ABC):
    """Base class for a single shape

    Parameters
    ----------
    data : (N, D) array
        Vertices specifying the shape.
    edge_width : float
        thickness of lines and edges.
    z_index : int
        Specifier of z order priority. Shapes with higher z order are displayed
        ontop of others.
    dims_order : (D,) list
        Order that the dimensions are to be rendered in.
    ndisplay : int
        Number of displayed dimensions.

    Attributes
    ----------
    data : (N, D) array
        Vertices specifying the shape.
    data_displayed : (N, 2) array
        Vertices of the shape that are currently displayed. Only 2D rendering
        currently supported.
    edge_width : float
        thickness of lines and edges.
    name : str
        Name of shape type.
    z_index : int
        Specifier of z order priority. Shapes with higher z order are displayed
        ontop of others.
    dims_order : (D,) list
        Order that the dimensions are rendered in.
    ndisplay : int
        Number of dimensions to be displayed, must be 2 as only 2D rendering
        currently supported.
    displayed : tuple
        List of dimensions that are displayed.
    not_displayed : tuple
        List of dimensions that are not displayed.
    slice_key : (2, M) array
        Min and max values of the M non-displayed dimensions, useful for
        slicing multidimensional shapes.

    Notes
    -----
    _closed : bool
        Bool if shape edge is a closed path or not
    _box : np.ndarray
        9x2 array of vertices of the interaction box. The first 8 points are
        the corners and midpoints of the box in clockwise order starting in the
        upper-left corner. The last point is the center of the box
    _face_vertices : np.ndarray
        Qx2 array of vertices of all triangles for the shape face
    _face_triangles : np.ndarray
        Px3 array of vertex indices that form the triangles for the shape face
    _edge_vertices : np.ndarray
        Rx2 array of centers of vertices of triangles for the shape edge.
        These values should be added to the scaled `_edge_offsets` to get the
        actual vertex positions. The scaling corresponds to the width of the
        edge
    _edge_offsets : np.ndarray
        Sx2 array of offsets of vertices of triangles for the shape edge. For
        These values should be scaled and added to the `_edge_vertices` to get
        the actual vertex positions. The scaling corresponds to the width of
        the edge
    _edge_triangles : np.ndarray
        Tx3 array of vertex indices that form the triangles for the shape edge
    _filled : bool
        Flag if array is filled or not.
    _use_face_vertices : bool
        Flag to use face vertices for mask generation.
    """

    slice_key: np.ndarray[tuple[Literal[2], int], np.dtype[np.int64]]

    def __init__(
        self,
        *,
        shape_type='rectangle',
        edge_width=1,
        z_index=0,
        dims_order=None,
        ndisplay=2,
    ) -> None:
        owner = _native.shape_model_locals(('self', 'shape_type', 'edge_width', 'z_index', 'dims_order', 'ndisplay'), {'self': self, 'shape_type': shape_type, 'edge_width': edge_width, 'z_index': z_index, 'dims_order': dims_order, 'ndisplay': ndisplay})
        del self, shape_type, edge_width, z_index, dims_order, ndisplay
        return _native.shape_model_init(globals(), (None, 2, 0, ('dtype',), (0, 3), (9, 2), False, True, ''), owner)

    def __new__(cls, *args, **kwargs):
        super_provider = lambda _receiver=cls: super()
        owner = _native.shape_model_locals(('cls', 'args', 'kwargs'), {'cls': cls, 'args': args, 'kwargs': kwargs})
        del cls, args, kwargs
        return _native.shape_model_new(globals(), (None, 'triangle'), super_provider, owner)

    @property
    @abstractmethod
    def data(self):
        # user writes own docstring
        owner = _native.shape_model_locals(('self',), {'self': self})
        del self
        return _native.shape_model_abstract_data(globals(), (None,), owner)

    @data.setter
    @abstractmethod
    def data(self, data):
        owner = _native.shape_model_locals(('self', 'data'), {'self': self, 'data': data})
        del self, data
        return _native.shape_model_abstract_set_data(globals(), (None,), owner)

    @abstractmethod
    def _update_displayed_data(self) -> None:
        owner = _native.shape_model_locals(('self',), {'self': self})
        del self
        return _native.shape_model_abstract_update(globals(), (None,), owner)

    @property
    def ndisplay(self):
        """int: Number of displayed dimensions."""
        owner = _native.shape_model_locals(('self',), {'self': self})
        del self
        return _native.shape_model_ndisplay(globals(), ('int: Number of displayed dimensions.',), owner)

    @ndisplay.setter
    def ndisplay(self, ndisplay):
        owner = _native.shape_model_locals(('self', 'ndisplay'), {'self': self, 'ndisplay': ndisplay})
        del self, ndisplay
        return _native.shape_model_set_ndisplay(globals(), (None,), owner)

    @property
    def dims_order(self):
        """(D,) list: Order that the dimensions are rendered in."""
        owner = _native.shape_model_locals(('self',), {'self': self})
        del self
        return _native.shape_model_dims_order(globals(), ('(D,) list: Order that the dimensions are rendered in.',), owner)

    @dims_order.setter
    def dims_order(self, dims_order):
        owner = _native.shape_model_locals(('self', 'dims_order'), {'self': self, 'dims_order': dims_order})
        del self, dims_order
        return _native.shape_model_set_dims_order(globals(), (None,), owner)

    @cached_property
    def dims_displayed(self):
        """tuple: Dimensions that are displayed."""
        owner = _native.shape_model_locals(('self',), {'self': self})
        del self
        return _native.shape_model_dims_displayed(globals(), ('tuple: Dimensions that are displayed.', None), owner)

    @property
    def bounding_box(self) -> np.ndarray:
        """(2, N) array, bounding box of the object."""
        # We add +-0.5 to handle edge width
        owner = _native.shape_model_locals(('self',), {'self': self})
        del self
        return _native.shape_model_bounding_box(globals(), ('(2, N) array, bounding box of the object.', None, -0.5, 0.5), owner)

    @property
    def dims_not_displayed(self):
        """tuple: Dimensions that are not displayed."""
        owner = _native.shape_model_locals(('self',), {'self': self})
        del self
        return _native.shape_model_dims_not_displayed(globals(), ('tuple: Dimensions that are not displayed.', None), owner)

    @cached_property
    def data_displayed(self) -> CoordinateArray:
        """(N, 2) array: Vertices of the shape that are currently displayed."""
        owner = _native.shape_model_locals(('self',), {'self': self})
        del self
        return _native.shape_model_data_displayed(globals(), ('(N, 2) array: Vertices of the shape that are currently displayed.', None), owner)

    @property
    def edge_width(self):
        """float: thickness of lines and edges."""
        owner = _native.shape_model_locals(('self',), {'self': self})
        del self
        return _native.shape_model_edge_width(globals(), ('float: thickness of lines and edges.',), owner)

    @edge_width.setter
    def edge_width(self, edge_width):
        owner = _native.shape_model_locals(('self', 'edge_width'), {'self': self, 'edge_width': edge_width})
        del self, edge_width
        return _native.shape_model_set_edge_width(globals(), (None,), owner)

    @property
    def z_index(self):
        """int: z order priority of shape. Shapes with higher z order displayed
        ontop of others.
        """
        owner = _native.shape_model_locals(('self',), {'self': self})
        del self
        return _native.shape_model_z_index(globals(), ('int: z order priority of shape. Shapes with higher z order displayed\n        ontop of others.\n        ',), owner)

    @z_index.setter
    def z_index(self, z_index):
        owner = _native.shape_model_locals(('self', 'z_index'), {'self': self, 'z_index': z_index})
        del self, z_index
        return _native.shape_model_set_z_index(globals(), (None,), owner)

    @property
    def vertices_count(self) -> int:
        """int: Number of vertices in the shape."""
        owner = _native.shape_model_locals(('self',), {'self': self})
        del self
        return _native.shape_model_vertices_count(globals(), ('int: Number of vertices in the shape.', 0), owner)

    @property
    def triangles_count(self) -> int:
        """int: Number of triangles in the shape."""
        owner = _native.shape_model_locals(('self',), {'self': self})
        del self
        return _native.shape_model_triangles_count(globals(), ('int: Number of triangles in the shape.', 0), owner)

    @property
    def face_triangles_count(self) -> int:
        """int: Number of triangles in the face of the shape."""
        owner = _native.shape_model_locals(('self',), {'self': self})
        del self
        return _native.shape_model_face_triangles_count(globals(), ('int: Number of triangles in the face of the shape.', 0), owner)

    @property
    def face_vertices_count(self) -> int:
        """int: Number of vertices in the face of the shape."""
        owner = _native.shape_model_locals(('self',), {'self': self})
        del self
        return _native.shape_model_face_vertices_count(globals(), ('int: Number of vertices in the face of the shape.', 0), owner)

    @property
    def edge_triangles_count(self) -> int:
        """int: Number of triangles in the edge of the shape."""
        owner = _native.shape_model_locals(('self',), {'self': self})
        del self
        return _native.shape_model_edge_triangles_count(globals(), ('int: Number of triangles in the edge of the shape.', 0), owner)

    @property
    def edge_vertices_count(self) -> int:
        """int: Number of vertices in the edge of the shape."""
        owner = _native.shape_model_locals(('self',), {'self': self})
        del self
        return _native.shape_model_edge_vertices_count(globals(), ('int: Number of vertices in the edge of the shape.', 0), owner)

    def _set_empty_edge(self) -> None:
        owner = _native.shape_model_locals(('self',), {'self': self})
        del self
        return _native.shape_model_empty_edge(globals(), (None, 0, ('dtype',), (0, 3)), owner)

    def _set_empty_face(self) -> None:
        owner = _native.shape_model_locals(('self',), {'self': self})
        del self
        return _native.shape_model_empty_face(globals(), (None, 0, ('dtype',), (0, 3)), owner)

    def _set_meshes_compiled_3d(
        self,
        data: CoordinateArray,
        closed: bool = True,
        face: bool = True,
        edge: bool = True,
    ):
        owner = _native.shape_model_locals(('self', 'data', 'closed', 'face', 'edge', 'face_triangles', 'face_vertices', 'centers', 'offsets', 'edge_triangles'), {'self': self, 'data': data, 'closed': closed, 'face': face, 'edge': edge})
        del self, data, closed, face, edge
        return _native.shape_model_meshes_3d(globals(), (None, ('closed',)), owner)

    def _set_meshes(  # noqa: B027
        self,
        data: CoordinateArray,
        closed: bool = True,
        face: bool = True,
        edge: bool = True,
    ) -> None: ...

    def _set_meshes_compiled_bermuda(
        self,
        data: CoordinateArray,
        closed: bool = True,
        face: bool = True,
        edge: bool = True,
    ) -> None:
        """Sets the face and edge meshes from a set of points.

        Uses bermuda compiled backend for triangulation.

        Parameters
        ----------
        data : np.ndarray
            Nx2 or Nx3 array specifying the shape to be triangulated
        closed : bool
            Bool which determines if the edge is closed or not
        face : bool
            Bool which determines if the face need to be traingulated
        edge : bool
            Bool which determines if the edge need to be traingulated
        """
        owner = _native.shape_model_locals(('self', 'data', 'closed', 'face', 'edge', 'triangles', 'vertices', 'centers', 'offsets', 'edge_triangles', 'e', 'path', 'text_path'), {'self': self, 'data': data, 'closed': closed, 'face': face, 'edge': edge})
        del self, data, closed, face, edge
        return _native.shape_model_meshes_bermuda(globals(), ('Sets the face and edge meshes from a set of points.\n\n        Uses bermuda compiled backend for triangulation.\n\n        Parameters\n        ----------\n        data : np.ndarray\n            Nx2 or Nx3 array specifying the shape to be triangulated\n        closed : bool\n            Bool which determines if the edge is closed or not\n        face : bool\n            Bool which determines if the face need to be traingulated\n        edge : bool\n            Bool which determines if the edge need to be traingulated\n        ', 1, 3, ('closed', 'face', 'edge'), None, 'bermuda', ('backend',), 'Triangulation failed. Data saved to ', ' and ', ('closed',)), owner)

    def _set_meshes_compiled_partseg(
        self,
        data: CoordinateArray,
        closed: bool = True,
        face: bool = True,
        edge: bool = True,
    ) -> None:
        """Sets the face and edge meshes from a set of points.

        Uses PartSegCore compiled backend for triangulation.

        Parameters
        ----------
        data : np.ndarray
            Nx2 or Nx3 array specifying the shape to be triangulated
        closed : bool
            Bool which determines if the edge is closed or not
        face : bool
            Bool which determines if the face need to be traingulated
        edge : bool
            Bool which determines if the edge need to be traingulated
        """
        owner = _native.shape_model_locals(('self', 'data', 'closed', 'face', 'edge', 'triangles', 'vertices', 'centers', 'offsets', 'edge_triangles', 'e', 'path', 'text_path'), {'self': self, 'data': data, 'closed': closed, 'face': face, 'edge': edge})
        del self, data, closed, face, edge
        return _native.shape_model_meshes_partseg(globals(), ('Sets the face and edge meshes from a set of points.\n\n        Uses PartSegCore compiled backend for triangulation.\n\n        Parameters\n        ----------\n        data : np.ndarray\n            Nx2 or Nx3 array specifying the shape to be triangulated\n        closed : bool\n            Bool which determines if the edge is closed or not\n        face : bool\n            Bool which determines if the face need to be traingulated\n        edge : bool\n            Bool which determines if the edge need to be traingulated\n        ', 1, 3, ('closed', 'face', 'edge'), None, True, ('split_edges',), 'partsegcore', ('backend',), 'Triangulation failed. Data saved to ', ' and ', ('closed',)), owner)

    def _set_meshes_triangle(
        self,
        data: CoordinateArray,
        closed: bool = True,
        face: bool = True,
        edge: bool = True,
    ) -> None:
        """Sets the face and edge meshes from a set of points.

        Uses the triangle package to triangulate the polygon face

        Parameters
        ----------
        data : np.ndarray
            Nx2 or Nx3 array specifying the shape to be triangulated
        closed : bool
            Bool which determines if the edge is closed or not
        face : bool
            Bool which determines if the face need to be traingulated
        edge : bool
            Bool which determines if the edge need to be traingulated
        """
        owner = _native.shape_model_locals(('self', 'data', 'closed', 'face', 'edge', 'f_vertices', 'f_triangles', 'centers', 'offsets', 'triangles', 'ndim', 'data2d', 'axis', 'value', 'vertices'), {'self': self, 'data': data, 'closed': closed, 'face': face, 'edge': edge})
        del self, data, closed, face, edge
        return _native.shape_model_meshes_triangle(globals(), ('Sets the face and edge meshes from a set of points.\n\n        Uses the triangle package to triangulate the polygon face\n\n        Parameters\n        ----------\n        data : np.ndarray\n            Nx2 or Nx3 array specifying the shape to be triangulated\n        closed : bool\n            Bool which determines if the edge is closed or not\n        face : bool\n            Bool which determines if the face need to be traingulated\n        edge : bool\n            Bool which determines if the edge need to be traingulated\n        ', ('closed',), None, 1, 3, ('axis',), 0), owner)

    def _set_meshes_py(
        self,
        data: CoordinateArray,
        closed: bool = True,
        face: bool = True,
        edge: bool = True,
    ) -> None:
        """Sets the face and edge meshes from a set of points.

        Parameters
        ----------
        data : np.ndarray
            Nx2 or Nx3 array specifying the shape to be triangulated
        closed : bool
            Bool which determines if the edge is closed or not
        face : bool
            Bool which determines if the face need to be traingulated
        edge : bool
            Bool which determines if the edge need to be traingulated
        """
        owner = _native.shape_model_locals(('self', 'data', 'closed', 'face', 'edge', 'f_vertices', 'f_triangles', 'centers', 'offsets', 'triangles', 'ndim', 'data2d', 'axis', 'value', 'vertices'), {'self': self, 'data': data, 'closed': closed, 'face': face, 'edge': edge})
        del self, data, closed, face, edge
        return _native.shape_model_meshes_py(globals(), ('Sets the face and edge meshes from a set of points.\n\n        Parameters\n        ----------\n        data : np.ndarray\n            Nx2 or Nx3 array specifying the shape to be triangulated\n        closed : bool\n            Bool which determines if the edge is closed or not\n        face : bool\n            Bool which determines if the face need to be traingulated\n        edge : bool\n            Bool which determines if the edge need to be traingulated\n        ', ('closed',), None, 1, 3, ('axis',), 0), owner)

    def _triangulate_edge(
        self, data: CoordinateArray, closed: bool
    ) -> tuple[CoordinateArray, CoordinateArray, TriangleArray]:
        """Triangulate the edge of the shape.

        Parameters
        ----------
        data : np.ndarray
            Nx2 or Nx3 array specifying the shape to be triangulated
        closed : bool
            Bool which determines if the edge is closed or not

        Returns
        -------
        tuple
            Tuple of (centers, offsets, triangles) where centers is a 2D array
            of the centers of the triangles, offsets is a 2D array of the
            offsets of the triangles, and triangles is a 2D array of the
            triangles.
        """
        owner = _native.shape_model_locals(('self', 'data', 'closed'), {'self': self, 'data': data, 'closed': closed})
        del self, data, closed
        return _native.shape_model_triangulate_edge(globals(), ('Triangulate the edge of the shape.\n\n        Parameters\n        ----------\n        data : np.ndarray\n            Nx2 or Nx3 array specifying the shape to be triangulated\n        closed : bool\n            Bool which determines if the edge is closed or not\n\n        Returns\n        -------\n        tuple\n            Tuple of (centers, offsets, triangles) where centers is a 2D array\n            of the centers of the triangles, offsets is a 2D array of the\n            offsets of the triangles, and triangles is a 2D array of the\n            triangles.\n        ', ('closed',)), owner)

    def _triangulate_edge_partseg(
        self, data: CoordinateArray, closed: bool
    ) -> tuple[CoordinateArray, CoordinateArray, TriangleArray]:
        owner = _native.shape_model_locals(('self', 'data', 'closed'), {'self': self, 'data': data, 'closed': closed})
        del self, data, closed
        return _native.shape_model_triangulate_partseg(globals(), (None, ('closed',)), owner)

    def _triangulate_edge_bermuda(
        self, data: CoordinateArray, closed: bool
    ) -> tuple[CoordinateArray, CoordinateArray, TriangleArray]:
        owner = _native.shape_model_locals(('self', 'data', 'closed'), {'self': self, 'data': data, 'closed': closed})
        del self, data, closed
        return _native.shape_model_triangulate_bermuda(globals(), (None, ('closed',)), owner)

    def _all_triangles(self):
        """Return all triangles for the shape

        Returns
        -------
        np.ndarray
            Nx3 array of vertex indices that form the triangles for the shape
        """
        return _native.viewer_shape_all_triangles(self, globals())

    def transform(self, transform: npt.NDArray) -> None:
        """Performs a linear transform on the shape

        Parameters
        ----------
        transform : np.ndarray
            2x2 array specifying linear transform.
        """
        owner = _native.shape_model_locals(('self', 'transform', 'points', 'centers', 'offsets', 'triangles'), {'self': self, 'transform': transform})
        del self, transform
        return _native.shape_model_transform(globals(), ('Performs a linear transform on the shape\n\n        Parameters\n        ----------\n        transform : np.ndarray\n            2x2 array specifying linear transform.\n        ', None, 'data_displayed', ('closed',), 0, ('axis',)), owner)

    def shift(self, shift: npt.NDArray) -> None:
        """Performs a 2D shift on the shape

        Parameters
        ----------
        shift : np.ndarray
            length 2 array specifying shift of shapes.
        """
        owner = _native.shape_model_locals(('self', 'shift'), {'self': self, 'shift': shift})
        del self, shift
        return _native.shape_model_shift(globals(), ('Performs a 2D shift on the shape\n\n        Parameters\n        ----------\n        shift : np.ndarray\n            length 2 array specifying shift of shapes.\n        ', None), owner)

    def scale(self, scale, center=None):
        """Performs a scaling on the shape

        Parameters
        ----------
        scale : float, list
            scalar or list specifying rescaling of shape.
        center : list
            length 2 list specifying coordinate of center of scaling.
        """
        owner = _native.shape_model_locals(('self', 'scale', 'center', 'transform'), {'self': self, 'scale': scale, 'center': center})
        del self, scale, center
        return _native.shape_model_scale(globals(), ('Performs a scaling on the shape\n\n        Parameters\n        ----------\n        scale : float, list\n            scalar or list specifying rescaling of shape.\n        center : list\n            length 2 list specifying coordinate of center of scaling.\n        ', 0, 1, None), owner)

    def rotate(self, angle, center=None):
        """Performs a rotation on the shape

        Parameters
        ----------
        angle : float
            angle specifying rotation of shape in degrees. CCW is positive.
        center : list
            length 2 list specifying coordinate of fixed point of the rotation.
        """
        owner = _native.shape_model_locals(('self', 'angle', 'center', 'theta', 'transform'), {'self': self, 'angle': angle, 'center': center})
        del self, angle, center
        return _native.shape_model_rotate(globals(), ('Performs a rotation on the shape\n\n        Parameters\n        ----------\n        angle : float\n            angle specifying rotation of shape in degrees. CCW is positive.\n        center : list\n            length 2 list specifying coordinate of fixed point of the rotation.\n        ', None), owner)

    def flip(self, axis, center=None):
        """Performs a flip on the shape, either horizontal or vertical.

        Parameters
        ----------
        axis : int
            integer specifying axis of flip. `0` flips horizontal, `1` flips
            vertical.
        center : list
            length 2 list specifying coordinate of center of flip axes.
        """
        owner = _native.shape_model_locals(('self', 'axis', 'center', 'transform'), {'self': self, 'axis': axis, 'center': center})
        del self, axis, center
        return _native.shape_model_flip(globals(), ('Performs a flip on the shape, either horizontal or vertical.\n\n        Parameters\n        ----------\n        axis : int\n            integer specifying axis of flip. `0` flips horizontal, `1` flips\n            vertical.\n        center : list\n            length 2 list specifying coordinate of center of flip axes.\n        ', 0, 1, -1, 'Axis not recognized, must be one of "{{0, 1}}"', None), owner)

    def to_mask(self, mask_shape=None, zoom_factor=1, offset=(0, 0)):
        """Convert the shape vertices to a boolean mask.

        Set points to `True` if they are lying inside the shape if the shape is
        filled, or if they are lying along the boundary of the shape if the
        shape is not filled. Negative points or points outside the mask_shape
        after the zoom and offset are clipped.

        Parameters
        ----------
        mask_shape : (D,) array
            Shape of mask to be generated. If non specified, takes the max of
            the displayed vertices.
        zoom_factor : float
            Premultiplier applied to coordinates before generating mask. Used
            for generating as downsampled mask.
        offset : 2-tuple
            Offset subtracted from coordinates before multiplying by the
            zoom_factor. Used for putting negative coordinates into the mask.

        Returns
        -------
        mask : np.ndarray
            Boolean array with `True` for points inside the shape
        """
        owner = _native.shape_model_locals(('self', 'mask_shape', 'zoom_factor', 'offset', 'embedded', 'shape_plane', 'd', 'data', 'mask_p', 'mask', 'slice_key', 'i', 'displayed_order'), {'self': self, 'mask_shape': mask_shape, 'zoom_factor': zoom_factor, 'offset': offset})
        del self, mask_shape, zoom_factor, offset
        return _native.shape_model_to_mask(globals(), ('Convert the shape vertices to a boolean mask.\n\n        Set points to `True` if they are lying inside the shape if the shape is\n        filled, or if they are lying along the boundary of the shape if the\n        shape is not filled. Negative points or points outside the mask_shape\n        after the zoom and offset are clipped.\n\n        Parameters\n        ----------\n        mask_shape : (D,) array\n            Shape of mask to be generated. If non specified, takes the max of\n            the displayed vertices.\n        zoom_factor : float\n            Premultiplier applied to coordinates before generating mask. Used\n            for generating as downsampled mask.\n        offset : 2-tuple\n            Offset subtracted from coordinates before multiplying by the\n            zoom_factor. Used for putting negative coordinates into the mask.\n\n        Returns\n        -------\n        mask : np.ndarray\n            Boolean array with `True` for points inside the shape\n        ', None, 0, ('axis',), 'int', 2, False, 1, True, 'mask shape length must either be 2 or the same as the dimensionality of the shape, expected ', ' got ', '.', ('dtype',), 'Internal error: self.slice_key is None'), owner)

    def _clean_cache(self) -> None:
        owner = _native.shape_model_locals(('self',), {'self': self})
        del self
        return _native.shape_model_clean_cache(globals(), (None, 'dims_displayed', 'data_displayed'), owner)
