from naparixx import _native
import typing
from collections.abc import Generator, Iterable, Sequence
from contextlib import contextmanager
from functools import cached_property, wraps
from itertools import repeat
from typing import Literal, TypedDict

import numpy as np
import numpy.typing as npt

from napari.layers.shapes._mesh import Mesh
from napari.layers.shapes._shapes_constants import ShapeType, shape_classes
from napari.layers.shapes._shapes_models import Line, Path, Shape
from napari.layers.shapes._shapes_utils import triangles_intersect_box
from napari.layers.shapes.shape_types import (
    CoordinateArray,
    CoordinateDtype,
    IndexArray,
    IndexDtype,
    ShapeColor,
    ShapeColorArray,
    ShapeColorDtype,
    TriangleArray,
    TriangleDtype,
    ZOrderArray,
    ZOrderDtype,
)
from napari.utils.geometry import (
    inside_triangles,
    intersect_line_with_triangles,
    line_in_triangles_3d,
)


class MeshArrayDict(TypedDict):
    """Mesh array dict used for adding multiple shapes at once.

    This type is helper for functions used when adding multiple shapes at once.
    To ensure names and types of arrays are consistent, we use a TypedDict.

    Fields
    ------
    z_index : ZOrderArray
        Information about z-ndex of shapes.
    vertices : CoordinateArray
        array of shape vertices
    mesh_vertices : CoordinateArray
        array of face triangulation vertices.
    mesh_vertices_centers : CoordinateArray
        array of face triangulation vertices centers.
        It is required for update of edges triangulation.
    mesh_vertices_offsets : CoordinateArray
        array of face triangulation vertices offsets.
        It is required for update of edges triangulation.
    mesh_triangles : TriangleArray
        array of triangles in mesh.
    mesh_triangles_colors : ShapeColorArray
        colors of triangles in mesh.
    vertices_index : IndexArray
        array with information which range of vertices belongs to which shape.
    mesh_triangles_index : IndexArray
        array with information which range of triangles belongs to which shape.
    mesh_vertices_index : IndexArray
        array with information which range of mesh vertices belongs to which shape.
    """

    z_index: ZOrderArray  # information about z-order of shapes
    vertices: CoordinateArray  # vertices of shapes

    mesh_vertices: CoordinateArray
    mesh_vertices_centers: CoordinateArray
    mesh_vertices_offsets: CoordinateArray

    mesh_triangles: TriangleArray
    mesh_triangles_colors: ShapeColorArray

    vertices_index: IndexArray  # offset of vertices for each shape
    mesh_triangles_index: (
        IndexArray  # offset of triangles and mesh vertices for each shape
    )
    mesh_vertices_index: IndexArray  # offset of mesh vertices for each shape


_SizeInformation = tuple[int, int, int, int, int]


def _ensure_color_arrays(shapes, face_colors=None, edge_colors=None):
    """Return as many face and edge colors as there are shapes in the input.

    Parameters
    ----------
    shapes : iterable of Shape
        Each Shape must be a subclass of Shape
    face_colors : iterable of face_color or None
        If None, default face colors will be used
    edge_colors : iterable of edge_color or None
        If None, default edge colors will be used

    Returns
    -------
    face_colors : np.ndarray
        Array of face colors
    edge_colors : np.ndarray
        Array of edge colors
    """
    owner = _native.shape_state_transfer_locals(('shapes', 'face_colors', 'edge_colors', 'v'), {'face_colors': face_colors, 'edge_colors': edge_colors})
    del face_colors, edge_colors
    return _native.shape_ensure_color_arrays(shapes, globals(), owner)


def _calculate_array_sizes(shapes: Iterable[Shape]) -> _SizeInformation:
    """Calculate sizes needed for array preallocation.

    Parameters
    ----------
    shapes : iterable of Shape
        Each Shape must be a subclass of Shape

    Returns
    -------
    n_vertices : int
        Total number of vertices
    n_indices : int
        Total number of indices
    n_mesh_vertices : int
        Total number of mesh vertices
    n_face_tri : int
        Total number of face triangles
    n_edge_tri : int
        Total number of edge triangles
    """
    owner = _native.shape_state_locals(('shapes', 'n_vertices', 'n_indices', 'n_mesh_vertices', 'n_face_tri', 'n_edge_tri', 'shape'))
    return _native.shape_calculate_array_sizes(shapes, globals(), owner)


def _preallocate_arrays(
    shapes: Sequence[Shape], sizes: _SizeInformation
) -> MeshArrayDict:
    """Preallocate arrays for storing shape data.

    Parameters
    ----------
    shapes : Sequence of Shape
        Each Shape must be a subclass of Shape
    sizes : tuple
        Tuple containing sizes for preallocation:
        (n_vertices, n_indices, n_mesh_vertices,
        n_face_tri, n_edge_tri)

    Returns
    -------
    arrays : dict
        Dictionary containing preallocated arrays
    """
    owner = _native.shape_state_locals(('shapes', 'sizes', 'n_shapes', 'n_vertices', '_n_indices', 'n_mesh_vertices', 'n_face_tri', 'n_edge_tri', 'dim', 'z_index', 'vertices_index', 'mesh_triangles_index', 'mesh_vertices_index', 'vertices', 'mesh_vertices', 'mesh_vertices_centers', 'mesh_vertices_offsets', 'total_triangles', 'mesh_triangles', 'mesh_triangles_colors'))
    return _native.shape_preallocate_arrays(shapes, sizes, globals(), owner)


def _fill_arrays(
    start_mesh_index: int,
    start_triangle_index: int,
    start_vertices_index: int,
    shapes: Iterable[Shape],
    face_colors: np.ndarray,
    edge_colors: np.ndarray,
    arrays: MeshArrayDict,
) -> None:
    """Fill pre-allocated arrays with shape data.

    Parameters
    ----------
    start_mesh_index : int
        The number of mesh vertices in the shape list before adding these
        shapes.
    start_triangle_index : int
        The number of mesh triangles in the shape list before adding these
    shapes : iterable of Shape
        Each Shape must be a subclass of Shape
    face_colors : np.ndarray
        Array of face colors
    edge_colors : np.ndarray
        Array of edge colors
    arrays : dict
        Dictionary containing preallocated arrays
    """
    owner = _native.shape_state_locals(('start_mesh_index', 'start_triangle_index', 'start_vertices_index', 'shapes', 'face_colors', 'edge_colors', 'arrays', 'z_index', 'vertices', 'mesh_vertices', 'mesh_vertices_centers', 'mesh_vertices_offsets', 'mesh_triangles', 'mesh_triangles_colors', 'vertices_index', 'mesh_triangles_index', 'mesh_vertices_index', 'vertices_offset', 'mesh_vertices_offset', 'triangles_offset', 'i', 'shape', 'face_color', 'edge_color', 'vertices_', 'n_vertices', 'face_vertices', 'n_face_vertices', 'face_vertices_slice', 'face_triangles', 'n_face_triangles', 'face_triangles_slice', 'edge_vertices', 'edge_offsets', 'n_edge_vertices', 'curr_vertices', 'edge_vertices_slice', 'edge_triangles', 'n_edge_triangles', 'edge_triangles_slice'))
    return _native.shape_fill_arrays(start_mesh_index, start_triangle_index, start_vertices_index, shapes, face_colors, edge_colors, arrays, globals(), owner)


def _batch_dec(meth):
    """
    Decorator to apply `self.batched_updates` to the current method.
    """

    @wraps(meth)
    def _wrapped(self, *args, **kwargs):
        owner = _native.shape_state_locals(('self', 'args', 'kwargs'))
        return _native.shape_batch_wrapped(self, args, kwargs, lambda: meth, globals(), owner)

    return _wrapped


class ShapeList(_native.ShapeListNamespaceBase):
    """List of shapes class.

    Parameters
    ----------
    data : list
        List of Shape objects
    ndisplay : int
        Number of displayed dimensions.

    Attributes
    ----------
    shapes : (N, ) list
        Shape objects.
    data : (N, ) list of (M, D) array
        Data arrays for each shape.
    ndisplay : int
        Number of displayed dimensions.
    slice_keys : (N, 2, P) array
        Array of slice keys for each shape. Each slice key has the min and max
        values of the P non-displayed dimensions, useful for slicing
        multidimensional shapes. If the both min and max values of shape are
        equal then the shape is entirely contained within the slice specified
        by those values.
    shape_types : (N, ) list of str
        Name of shape type for each shape.
    edge_color : (N x 4) np.ndarray
        Array of RGBA edge colors for each shape.
    face_color : (N x 4) np.ndarray
        Array of RGBA face colors for each shape.
    edge_widths : (N, ) list of float
        Edge width for each shape.
    z_indices : (N, ) list of int
        z-index for each shape.

    Notes
    -----
    _vertices : np.ndarray
        MxN array of all displayed vertices from all shapes where N is equal to ndisplay
    _index : np.ndarray
        Length M array with the index (0, ..., N-1) of each shape that each
        vertex corresponds to
    _z_index : np.ndarray
        Length N array with z_index of each shape
    _z_order : np.ndarray
        Length N array with z_order of each shape. This must be a permutation
        of (0, ..., N-1).
    _mesh : Mesh
        Mesh object containing all the mesh information that will ultimately
        be rendered.
    """

    def __init__(
        self, data: typing.Iterable[Shape] = (), ndisplay: int = 2
    ) -> None:
        owner = _native.shape_state_transfer_locals(('self', 'data', 'ndisplay'), {'data': data})
        del data
        return _native.shape_list_init(self, ndisplay, globals(), (0, 4), owner)

    def _vertices_slice(self, shape_index: int | np.integer) -> slice:
        """Return the slice of vertices for a given shape index."""
        owner = _native.shape_state_locals(('self', 'shape_index', 'start', 'shape'))
        return _native.shape_list_vertices_slice(self, shape_index, globals(), owner)

    def _vertices_slice_available(self, shape_index: int) -> slice:
        """Return the available slice of vertices for a given shape index."""
        owner = _native.shape_state_locals(('self', 'shape_index', 'start', 'end'))
        return _native.shape_list_vertices_slice_available(self, shape_index, globals(), owner)

    def _mesh_vertices_slice(self, shape_index: int) -> slice:
        """Return the slice of mesh vertices for a given shape index."""
        owner = _native.shape_state_locals(('self', 'shape_index', 'start', 'shape'))
        return _native.shape_list_mesh_vertices_slice(self, shape_index, globals(), owner)

    def _mesh_vertices_slice_available(self, shape_index: int) -> slice:
        """Return the available slice of mesh vertices for a given shape index."""
        owner = _native.shape_state_locals(('self', 'shape_index', 'start', 'end'))
        return _native.shape_list_mesh_vertices_slice_available(self, shape_index, globals(), owner)

    def _mesh_triangles_slice(self, shape_index: int | np.integer) -> slice:
        """Return the slice of mesh triangles for a given shape index."""
        owner = _native.shape_state_locals(('self', 'shape_index', 'start', 'shape'))
        return _native.shape_list_mesh_triangles_slice(self, shape_index, globals(), owner)

    @staticmethod
    def _slicess_to_array(slices: Sequence[slice]) -> np.ndarray:
        def generator_factory(iterator):
            yield from iterator
        owner = _native.shape_state_locals(('slices', 'count', 'res', 'begin', 'r', 'end'))
        return _native.shape_list_slicess_to_array(slices, globals(), generator_factory, owner)

    def _mesh_triangles_range_seq(
        self, shape_indexes: IndexArray
    ) -> np.ndarray | slice:
        """Return the range of mesh triangles for a sequence of shape indexes."""
        owner = _native.shape_state_locals(('self', 'shape_indexes', 'start', 'end', 'end_shape', 'i', 'ranges'))
        return _native.shape_list_mesh_triangles_range_seq(self, shape_indexes, globals(), owner)

    def _vertices_range_seq(
        self, shape_indexes: IndexArray
    ) -> np.ndarray | slice:
        """Return the range of vertices for a sequence of shape indexes."""
        owner = _native.shape_state_locals(('self', 'shape_indexes', 'start', 'end', 'end_shape', 'i', 'slicess'))
        return _native.shape_list_vertices_range_seq(self, shape_indexes, globals(), owner)

    def _mesh_triangles_slice_available(self, shape_index: int) -> slice:
        """Return the available slice of mesh triangles for a given shape index."""
        owner = _native.shape_state_locals(('self', 'shape_index', 'start', 'end'))
        return _native.shape_list_mesh_triangles_slice_available(self, shape_index, globals(), owner)

    def _mesh_vertices_face_slice(self, shape_index: int) -> slice:
        """Return the slice of mesh vertices of face triangles for a given shape index."""
        owner = _native.shape_state_locals(('self', 'shape_index', 'shape', 'start'))
        return _native.shape_list_mesh_vertices_face_slice(self, shape_index, globals(), owner)

    def _mesh_triangles_face_slice(self, shape_index: int) -> slice:
        """Return the slice of mesh triangles of face triangles for a given shape index."""
        owner = _native.shape_state_locals(('self', 'shape_index', 'shape', 'start'))
        return _native.shape_list_mesh_triangles_face_slice(self, shape_index, globals(), owner)

    def _mesh_vertices_edge_slice(self, shape_index: int) -> slice:
        """Return the slice of mesh vertices of edge triangles for a given shape index."""
        owner = _native.shape_state_locals(('self', 'shape_index', 'shape', 'start'))
        return _native.shape_list_mesh_vertices_edge_slice(self, shape_index, globals(), owner)

    def _mesh_triangles_edge_slice(self, shape_index: int) -> slice:
        """Return the slice of mesh triangles of edge triangles for a given shape index."""
        owner = _native.shape_state_locals(('self', 'shape_index', 'shape', 'start'))
        return _native.shape_list_mesh_triangles_edge_slice(self, shape_index, globals(), owner)

    @contextmanager
    def batched_updates(self) -> Generator[None, None, None]:
        """
        Reentrant context manager to batch the display update

        `_update_displayed()` is called at _most_ once on exit of the context
        manager.

        There are two reason for this:

         1. Some updates are triggered by events, but sometimes multiple pieces
            of data that trigger events must be set before the data can be
            recomputed. For example changing number of dimension cause broacast
            error on partially update structures.
         2. Performance. Ideally we want to update the display only once.

        If no direct or indirect call to `_update_displayed()` are made inside
        the context manager, no the update logic is not called on exit.



        """
        owner = _native.shape_state_locals(('self',))
        yield from _native.shape_list_batched_updates(self, globals(), __debug__, owner)

    @property
    def data(self) -> list[npt.NDArray]:
        """list of (M, D) array: data arrays for each shape."""
        return [s.data for s in self.shapes]

    @property
    def ndisplay(self) -> int:
        """int: Number of displayed dimensions."""
        owner = _native.shape_state_locals(('self',))
        return _native.shape_list_ndisplay(self, globals(), owner)

    @ndisplay.setter
    def ndisplay(self, ndisplay: int) -> None:
        owner = _native.shape_state_locals(('self', 'ndisplay', 'shapes', 'face_color', 'edge_color', 'shape'))
        return _native.shape_list_set_ndisplay(self, ndisplay, globals(), owner)

    @property
    def slice_keys(
        self,
    ) -> np.ndarray[tuple[int, Literal[2], int], np.dtype[np.int64]]:
        """(N, 2, P) array: slice key for each shape."""
        owner = _native.shape_state_locals(('self', 's'))
        return _native.shape_list_slice_keys(self, globals(), owner)

    @property
    def shape_types(self) -> list[str]:
        """list of str: shape types for each shape."""
        return [s.name for s in self.shapes]

    @property
    def edge_color(self) -> ShapeColorArray:
        """(N x 4) np.ndarray: Array of RGBA edge colors for each shape"""
        return self._edge_color

    @edge_color.setter
    def edge_color(self, edge_color: npt.NDArray) -> None:
        self._set_color(edge_color, 'edge')

    @property
    def face_color(self) -> ShapeColorArray:
        """(N x 4) np.ndarray: Array of RGBA face colors for each shape"""
        return self._face_color

    @face_color.setter
    def face_color(self, face_color: npt.NDArray) -> None:
        self._set_color(face_color, 'face')

    @_batch_dec
    def _set_color(
        self, colors: npt.NDArray, attribute: Literal['edge', 'face']
    ) -> None:
        """Set the face_color or edge_color property

        Parameters
        ----------
        colors : (N, 4) np.ndarray
            The value for setting edge or face_color. There must
            be one color for each shape
        attribute : str in {'edge', 'face'}
            The name of the attribute to set the color of.
            Should be 'edge' for edge_color or 'face' for face_color.
        """
        n_shapes = len(self.data)
        if not np.array_equal(colors.shape, (n_shapes, 4)):
            raise ValueError(
                f'{attribute}_color must have shape ({n_shapes}, 4)'
            )

        update_method = getattr(self, f'update_{attribute}_colors')
        indices = np.arange(len(colors))
        update_method(indices, colors, update=False)
        self._update_displayed()

    @property
    def edge_widths(self) -> list[float]:
        """list of float: edge width for each shape."""
        return [s.edge_width for s in self.shapes]

    @property
    def z_indices(self) -> list[int]:
        """list of int: z-index for each shape."""
        return [s.z_index for s in self.shapes]

    @property
    def slice_key(self):
        """list: slice key for slicing n-dimensional shapes."""
        owner = _native.shape_state_locals(('self',))
        return _native.shape_list_slice_key(self, globals(), owner)

    @slice_key.setter
    @_batch_dec
    def slice_key(self, slice_key):
        owner = _native.shape_state_transfer_locals(('self', 'slice_key'), {'slice_key': slice_key})
        del slice_key
        return _native.shape_list_set_slice_key(self, globals(), owner)

    def _update_displayed_triangles_to_shape_index(
        self, displayed_indices: IndexArray
    ) -> None:
        """Update the displayed triangles to shape index mapping."""
        owner = _native.shape_state_locals(('self', 'displayed_indices', 'shift_idx', 'i', 'begin', 'end', 'elem_num'))
        return _native.shape_list_update_triangle_map(self, displayed_indices, globals(), owner)

    def _update_displayed_vertices_to_shape_num(
        self, displayed_indices: IndexArray
    ) -> None:
        """Update the displayed vertices to shape index mapping."""
        owner = _native.shape_state_locals(('self', 'displayed_indices', 'shift_idx', 'i', 'begin', 'end', 'elem_num'))
        return _native.shape_list_update_vertex_map(self, displayed_indices, globals(), owner)

    def _update_displayed(self) -> None:
        """Update the displayed data based on the slice key.

        This method must be called from within the `batched_updates` context
        manager:
        """
        owner = _native.shape_state_locals(('self', 'slice_key', 'disp_indices', 'z_order', 'triangle_ranges', 'vertices_range'))
        return _native.shape_list_update_displayed(self, globals(), __debug__, owner)

    def add(
        self,
        shape: Shape | Sequence[Shape],
        face_color=None,
        edge_color=None,
        shape_index=None,
        z_refresh=True,
    ):
        """Adds a single Shape object (single add mode) or multiple Shapes (multiple shape mode, which is much faster)

        If shape is a single instance of subclass Shape then single add mode will be used, otherwise multiple add mode

        Parameters
        ----------
        shape : single Shape or iterable of Shape
            Each shape must be a subclass of Shape, one of "{'Line', 'Rectangle',
            'Ellipse', 'Path', 'Polygon'}"
        face_color : color (or iterable of colors of same length as shape)
        edge_color : color (or iterable of colors of same length as shape)
        shape_index : None | int
            If int then edits the shape date at current index. To be used in
            conjunction with `remove` when renumber is `False`. If None, then
            appends a new shape to end of shapes list
            Must be None in multiple shape mode.
        z_refresh : bool
            If set to true, the mesh elements are reindexed with the new z order.
            When shape_index is provided, z_refresh will be overwritten to false,
            as the z indices will not change.
            When adding a batch of shapes, set to false  and then call
            ShapesList._update_z_order() once at the end.
        """
        # single shape mode
        owner = _native.shape_state_locals(('self', 'shape', 'face_color', 'edge_color', 'shape_index', 'z_refresh'))
        return _native.shape_list_add(self, shape, face_color, edge_color, shape_index, z_refresh, globals(), owner)

    def _add_single_shape(
        self,
        shape,
        face_color=None,
        edge_color=None,
        shape_index=None,
        z_refresh=True,
    ):
        """Adds a single Shape object

        Parameters
        ----------
        shape : subclass Shape
            Must be a subclass of Shape, one of "{'Line', 'Rectangle',
            'Ellipse', 'Path', 'Polygon'}"
        shape_index : None | int
            If int then edits the shape date at current index. To be used in
            conjunction with `remove` when renumber is `False`. If None, then
            appends a new shape to end of shapes list
        z_refresh : bool
            If set to true, the mesh elements are reindexed with the new z order.
            When shape_index is provided, z_refresh will be overwritten to false,
            as the z indices will not change.
            When adding a batch of shapes, set to false  and then call
            ShapesList._update_z_order() once at the end.
        """
        owner = _native.shape_state_transfer_locals(('self', 'shape', 'face_color', 'edge_color', 'shape_index', 'z_refresh', 'vertices_', 'm', 'vertices', 'triangles', 'color_array'), {'z_refresh': z_refresh})
        del z_refresh
        return _native.shape_list_add_single(self, shape, face_color, edge_color, shape_index, globals(), owner)

    def _extend_meshes(self, face_colors, edge_colors, arrays: MeshArrayDict):
        """Assemble mesh properties from filled arrays.

        Parameters
        ----------
        face_colors : np.ndarray
            Array of face colors
        edge_colors : np.ndarray
            Array of edge colors
        arrays : dict
            Dictionary containing filled arrays
        """
        owner = _native.shape_state_locals(('self', 'face_colors', 'edge_colors', 'arrays', 'z_index', 'vertices', 'mesh_vertices', 'mesh_vertices_centers', 'mesh_vertices_offsets', 'mesh_triangles', 'mesh_triangles_colors', 'vertices_index', 'mesh_triangles_index', 'mesh_vertices_index'))
        return _native.shape_list_extend_meshes(self, face_colors, edge_colors, arrays, globals(), owner)

    def _add_multiple_shapes(
        self,
        shapes: Sequence[Shape],
        face_colors=None,
        edge_colors=None,
        z_refresh=True,
    ) -> None:
        """Add multiple shapes at once (faster than adding them one by one)

        Parameters
        ----------
        shapes : iterable of Shape
            Each Shape must be a subclass of Shape, one of "{'Line', 'Rectangle',
            'Ellipse', 'Path', 'Polygon'}"
        face_colors : iterable of face_color
        edge_colors : iterable of edge_color
        z_refresh : bool
            If set to true, the mesh elements are reindexed with the new z order.
            When shape_index is provided, z_refresh will be overwritten to false,
            as the z indices will not change.
            When adding a batch of shapes, set to false  and then call
            ShapesList._update_z_order() once at the end.

        TODO: Currently shares a lot of code with `add()`, with the
        difference being that `add()` supports inserting shapes at a specific
        `shape_index`, whereas `add_multiple` will append them as a full batch
        """
        def generator_factory(iterator):
            yield from iterator
        owner = _native.shape_state_transfer_locals(('self', 'shapes', 'face_colors', 'edge_colors', 'z_refresh', 'sizes', 'arrays'), {'face_colors': face_colors, 'edge_colors': edge_colors})
        del face_colors, edge_colors
        return _native.shape_list_add_multiple(self, shapes, z_refresh, globals(), generator_factory, owner)

    @_batch_dec
    def remove_all(self):
        """Removes all shapes"""
        owner = _native.shape_state_locals(('self',))
        return _native.shape_list_remove_all(self, globals(), (0, 4), owner)

    @_batch_dec
    def update(self, index: int) -> None:
        """update shape at index `index`"""
        self._update_vertices(index)
        self._update_mesh_triangles(index)
        self._update_mesh_vertices(index, edge=True, face=True)
        self._update_displayed()

    def _update_vertices(self, index: int) -> None:
        shape = self.shapes[index]
        vertices_slice = self._vertices_slice_available(index)
        curr_vert_count = vertices_slice.stop - vertices_slice.start
        if shape.data_displayed.shape[0] == curr_vert_count:
            # If the number of vertices is the same, just update the data
            self._vertices[vertices_slice] = shape.data_displayed
        elif shape.data_displayed.shape[0] < curr_vert_count:
            # To avoid relocation, we add first point few times for padding.
            new_slice = slice(
                vertices_slice.start,
                vertices_slice.start + shape.data_displayed.shape[0],
            )
            padding_slice = slice(new_slice.stop, vertices_slice.stop)
            self._vertices[new_slice] = shape.data_displayed
            self._vertices[padding_slice] = shape.data_displayed[0]
        else:
            # there are more vertices in the shape than in the mesh
            before_array = self._vertices[: vertices_slice.start]
            after_array = self._vertices[vertices_slice.stop :]
            self._vertices = np.concatenate(
                [before_array, shape.data_displayed, after_array]
            )
            self._vertices_index[index + 1 :] += (
                shape.data_displayed.shape[0] - curr_vert_count
            )

    def _update_mesh_triangles(self, index: int) -> None:
        """Updates the mesh triangle data for a single shape located at index.

        Parameters
        ----------
        index : int
            Location in list of the shape to be changed.
        """
        shape = self.shapes[index]
        if index == 0:
            triangle_shift = 0
        else:
            triangle_shift = self._mesh_vertices_slice_available(
                index - 1
            ).stop
        triangles_slice = self._mesh_triangles_slice_available(index)
        current_triangles_count = triangles_slice.stop - triangles_slice.start
        new_triangle_count = shape.triangles_count
        prev_vertices_slice = self._mesh_vertices_slice_available(index)
        prev_vertices_count = (
            prev_vertices_slice.stop - prev_vertices_slice.start
        )
        new_vertices_count = shape.vertices_count
        if new_triangle_count <= current_triangles_count:
            face_slice = slice(
                triangles_slice.start,
                triangles_slice.start + shape.face_triangles_count,
            )
            edge_slice = slice(
                triangles_slice.start + shape.face_triangles_count,
                triangles_slice.start + shape.triangles_count,
            )
            self._mesh.triangles[face_slice] = (
                shape._face_triangles + triangle_shift
            )
            self._mesh.triangles_colors[face_slice] = self._face_color[index]
            self._mesh.triangles[edge_slice] = shape._edge_triangles + (
                triangle_shift + shape.face_vertices_count
            )
            self._mesh.triangles_colors[edge_slice] = self._edge_color[index]
            if new_triangle_count < current_triangles_count:
                padding_slice = slice(
                    triangles_slice.start + shape.triangles_count,
                    triangles_slice.stop,
                )
                self._mesh.triangles[padding_slice] = triangle_shift
            if new_vertices_count > prev_vertices_count:
                # Shift triangles indices if more vertices were added
                self._mesh.triangles[triangles_slice.stop :] += (
                    new_vertices_count - prev_vertices_count
                )
        else:
            # there are more triangles in the shape than in the mesh
            before_array = self._mesh.triangles[: triangles_slice.start]
            after_array = self._mesh.triangles[triangles_slice.stop :]
            after_array += new_vertices_count - prev_vertices_count
            self._mesh.triangles = np.concatenate(
                [
                    before_array,
                    shape._face_triangles + triangle_shift,
                    shape._edge_triangles
                    + (triangle_shift + shape.face_vertices_count),
                    after_array,
                ]
            )
            self._mesh.triangles_index[index + 1 :] += (
                new_triangle_count - current_triangles_count
            )
            # Update triangles colors
            self._mesh.triangles_colors = np.concatenate(
                [
                    self._mesh.triangles_colors[: triangles_slice.start],
                    np.repeat(
                        [self._face_color[index]],
                        shape.face_triangles_count,
                        axis=0,
                    ),
                    np.repeat(
                        [self._edge_color[index]],
                        shape.edge_triangles_count,
                        axis=0,
                    ),
                    self._mesh.triangles_colors[triangles_slice.stop :],
                ]
            )
            self._update_z_order()

    def remove(self, index: int, renumber: bool = True) -> None:
        """Removes a single shape located at index.

        Parameters
        ----------
        index : int
            Location in list of the shape to be removed.
        renumber : bool
            Bool to indicate whether to renumber all shapes or not. If not the
            expectation is that this shape is being immediately added back to the
            list using `add_shape`.
        """
        self.remove_multiple([index], renumber=renumber)

    @_batch_dec
    def remove_multiple(self, indices: list[int], renumber: bool = True):
        """Removes multiple shapes located at indices.

        Parameters
        ----------
        indices : list of int
            Locations in list of the shapes to be removed. Assumed to be sorted descending.
        renumber : bool
            Bool to indicate whether to renumber all shapes or not.
        """
        if not indices:
            return

        # Remove indices
        vert_slices = [self._vertices_slice_available(i) for i in indices]
        vert_indices_to_del = np.concatenate(
            [np.arange(s.start, s.stop) for s in vert_slices]
        )
        self._vertices = np.delete(self._vertices, vert_indices_to_del, axis=0)  # pyrefly: ignore [no-matching-overload]

        vert_counts = np.diff(self._vertices_index)
        new_vert_counts = np.delete(vert_counts, indices)
        self._vertices_index = np.concatenate(
            ([0], np.cumsum(new_vert_counts))
        ).astype(IndexDtype)

        # Remove vertices
        mesh_vert_slices = [
            self._mesh_vertices_slice_available(i) for i in indices
        ]
        mesh_vert_indices_to_del = np.concatenate(
            [np.arange(s.start, s.stop) for s in mesh_vert_slices]
        )
        # Get the shift for triangles caused by the removal of mesh vertices
        deleted_vertex_shift = np.zeros(len(self._mesh.vertices), dtype=int)
        deleted_vertex_shift[mesh_vert_indices_to_del] = 1
        deleted_vertex_shift = np.cumsum(deleted_vertex_shift)
        self._mesh.vertices = np.delete(  # pyrefly: ignore [no-matching-overload]
            self._mesh.vertices, mesh_vert_indices_to_del, axis=0
        )
        self._mesh.vertices_centers = np.delete(  # pyrefly: ignore [no-matching-overload]
            self._mesh.vertices_centers, mesh_vert_indices_to_del, axis=0
        )
        self._mesh.vertices_offsets = np.delete(  # pyrefly: ignore [no-matching-overload]
            self._mesh.vertices_offsets, mesh_vert_indices_to_del, axis=0
        )

        mesh_vert_counts = np.diff(self._mesh.vertices_index)
        new_mesh_vert_counts = np.delete(mesh_vert_counts, indices)
        self._mesh.vertices_index = np.concatenate(
            ([0], np.cumsum(new_mesh_vert_counts))
        ).astype(IndexDtype)

        # Remove triangles
        mesh_tri_slices = [
            self._mesh_triangles_slice_available(i) for i in indices
        ]
        mesh_tri_indices_to_del = np.concatenate(
            [np.arange(s.start, s.stop) for s in mesh_tri_slices]
        )
        self._mesh.triangles -= deleted_vertex_shift[self._mesh.triangles]
        self._mesh.triangles = np.delete(  # pyrefly: ignore [no-matching-overload]
            self._mesh.triangles, mesh_tri_indices_to_del, axis=0
        )
        self._mesh.triangles_colors = np.delete(  # pyrefly: ignore [no-matching-overload]
            self._mesh.triangles_colors, mesh_tri_indices_to_del, axis=0
        )

        mesh_tri_counts = np.diff(self._mesh.triangles_index)
        new_mesh_tri_counts = np.delete(mesh_tri_counts, indices)
        self._mesh.triangles_index = np.concatenate(
            ([0], np.cumsum(new_mesh_tri_counts))
        ).astype(IndexDtype)

        if renumber:
            for i in indices:
                del self.shapes[i]
            self._z_index = np.delete(self._z_index, indices)
            self._update_z_order()

        self._clear_cache()

    @_batch_dec
    def _update_mesh_vertices(self, index, edge=False, face=False):
        """Updates the mesh vertex data and vertex data for a single shape
        located at index.

        Parameters
        ----------
        index : int
            Location in list of the shape to be changed.
        edge : bool
            Bool to indicate whether to update mesh vertices corresponding to
            edges
        face : bool
            Bool to indicate whether to update mesh vertices corresponding to
            faces and to update the underlying shape vertices
        """
        shape = self.shapes[index]
        if edge and face:
            shape_slice = self._mesh_vertices_slice_available(index)
            current_range = shape_slice.stop - shape_slice.start
            if current_range < shape.vertices_count:
                # account for edge width
                edge_vertices_with_width = (
                    shape._edge_vertices
                    + shape.edge_width * shape._edge_offsets
                )
                # need to allocate_more space
                self._mesh.vertices = np.concatenate(
                    [
                        self._mesh.vertices[: shape_slice.start],
                        shape._face_vertices,
                        edge_vertices_with_width,
                        self._mesh.vertices[shape_slice.stop :],
                    ]
                )
                self._mesh.vertices_centers = np.concatenate(
                    [
                        self._mesh.vertices_centers[: shape_slice.start],
                        shape._face_vertices,
                        shape._edge_vertices,
                        self._mesh.vertices_centers[shape_slice.stop :],
                    ]
                )
                self._mesh.vertices_offsets = np.concatenate(
                    [
                        self._mesh.vertices_offsets[: shape_slice.start],
                        np.zeros(
                            shape._face_vertices.shape,
                            dtype=shape._face_vertices.dtype,
                        ),
                        shape._edge_offsets,
                        self._mesh.vertices_offsets[shape_slice.stop :],
                    ]
                )
                diff = shape.vertices_count - current_range
                self._mesh.vertices_index[index + 1 :] += diff
                return
            if current_range > shape.vertices_count:
                zeros_slice = slice(
                    shape_slice.start + shape.vertices_count, shape_slice.stop
                )
                self._mesh.vertices[zeros_slice] = 0
                self._mesh.vertices_centers[zeros_slice] = 0
                self._mesh.vertices_offsets[zeros_slice] = 0

        if edge:
            indices = self._mesh_vertices_edge_slice(index)
            self._mesh.vertices[indices] = (
                shape._edge_vertices + shape.edge_width * shape._edge_offsets
            )
            self._mesh.vertices_centers[indices] = shape._edge_vertices
            self._mesh.vertices_offsets[indices] = shape._edge_offsets
            self._update_displayed()

        if face:
            indices = self._mesh_vertices_face_slice(index)
            self._mesh.vertices[indices] = shape._face_vertices
            self._mesh.vertices_centers[indices] = shape._face_vertices
            indices = self._vertices_slice(index)
            self._vertices[indices] = shape.data_displayed
            self._update_displayed()
        self._clear_cache()

    @_batch_dec
    def _update_z_order(self):
        """Updates the z order of the triangles given the z_index list"""
        owner = _native.shape_state_locals(('self', 'idx', 'counts', 'z', 'triangles_z_order'))
        return _native.shape_list_update_z_order(self, globals(), owner)

    def edit(
        self, index, data, face_color=None, edge_color=None, new_type=None
    ):
        """Updates the data of a single shape located at index. If
        `new_type` is not None then converts the shape type to the new type

        Parameters
        ----------
        index : int
            Location in list of the shape to be changed.
        data : np.ndarray
            NxD array of vertices.
        new_type : None | str | Shape
            If string , must be one of "{'line', 'rectangle', 'ellipse',
            'path', 'polygon'}".
        """
        if new_type is not None:
            cur_shape = self.shapes[index]
            if isinstance(new_type, str):
                shape_type = ShapeType(new_type)
                if shape_type in shape_classes:
                    shape_cls = shape_classes[shape_type]
                else:
                    raise ValueError(
                        f'{shape_type} must be one of {set(shape_classes)}'
                    )
            else:
                shape_cls = new_type
            shape = shape_cls(
                data,
                edge_width=cur_shape.edge_width,
                z_index=cur_shape.z_index,
                dims_order=cur_shape.dims_order,
            )
            self.shapes[index] = shape
        else:
            shape = self.shapes[index]
            shape.data = data

        if face_color is not None:
            self._face_color[index] = face_color
        if edge_color is not None:
            self._edge_color[index] = edge_color

        self.update(index)
        self._update_z_order()

    def update_edge_width(self, index, edge_width):
        """Updates the edge width of a single shape located at index.

        Parameters
        ----------
        index : int
            Location in list of the shape to be changed.
        edge_width : float
            thickness of lines and edges.
        """
        self.shapes[index].edge_width = edge_width
        self._update_mesh_vertices(index, edge=True)

    @_batch_dec
    def update_edge_color(
        self, index: int, edge_color: ShapeColor, update: bool = True
    ) -> None:
        """Updates the edge color of a single shape located at index.

        Parameters
        ----------
        index : int
            Location in list of the shape to be changed.
        edge_color : np.ndarray of shape (4,)
            Array of RGBA values for the edge color.
        update : bool
            If True, update the mesh with the new color property. Set to False to avoid
            repeated updates when modifying multiple shapes. Default is True.
        """
        self._edge_color[index] = edge_color
        indices = self._mesh_triangles_edge_slice(index)
        self._mesh.triangles_colors[indices] = self._edge_color[index]
        if update:
            self._update_displayed()

    @_batch_dec
    def update_edge_colors(
        self,
        indices: Iterable[int],
        edge_colors: ShapeColor | ShapeColorArray,
        update: bool = True,
    ) -> None:
        """same as update_edge_color() but for multiple indices/edgecolors at once"""
        edge_colors_: Iterable[ShapeColor]
        if edge_colors.ndim == 1:
            edge_colors_ = repeat(typing.cast(ShapeColor, edge_colors))
        elif edge_colors.ndim == 2 and edge_colors.shape[0] == 1:
            edge_colors_ = repeat(typing.cast(ShapeColor, edge_colors[0]))
        else:
            edge_colors_ = edge_colors
        for i, color in zip(indices, edge_colors_, strict=False):
            self.update_edge_color(i, color, update=False)
        if update:
            self._update_displayed()

    @_batch_dec
    def update_face_color(
        self, index: int, face_color: ShapeColor, update: bool = True
    ) -> None:
        """Updates the face color of a single shape located at index.

        Parameters
        ----------
        index : int
            Location in list of the shape to be changed.
        face_color : np.ndarray of shape (4,)
            Array of RGBA values for the face color.
        update : bool
            If True, update the mesh with the new color property. Set to False to avoid
            repeated updates when modifying multiple shapes. Default is True.
        """
        self._face_color[index] = face_color
        indices = self._mesh_triangles_face_slice(index)
        self._mesh.triangles_colors[indices] = face_color
        if update:
            self._update_displayed()

    @_batch_dec
    def update_face_colors(
        self,
        indices: Iterable[int],
        face_colors: ShapeColor | ShapeColorArray,
        update: bool = True,
    ) -> None:
        """same as update_face_color() but for multiple indices/facecolors at once"""
        face_colors_: Iterable[ShapeColor]
        if face_colors.ndim == 1:
            face_colors_ = repeat(typing.cast(ShapeColor, face_colors))
        elif face_colors.ndim == 2 and face_colors.shape[0] == 1:
            face_colors_ = repeat(typing.cast(ShapeColor, face_colors[0]))
        else:
            face_colors_ = face_colors

        for i, color in zip(indices, face_colors_, strict=False):
            self.update_face_color(i, color, update=False)
        if update:
            self._update_displayed()

    def update_dims_order(self, dims_order):
        """Updates dimensions order for all shapes.

        Parameters
        ----------
        dims_order : (D,) list
            Order that the dimensions are rendered in.
        """
        for index in range(len(self.shapes)):
            if self.shapes[index].dims_order != dims_order:
                shape = self.shapes[index]
                shape.dims_order = dims_order
                self.update(index)
        self._update_z_order()

    def update_z_index(self, index, z_index):
        """Updates the z order of a single shape located at index.

        Parameters
        ----------
        index : int
            Location in list of the shape to be changed.
        z_index : int
            Specifier of z order priority. Shapes with higher z order are
            displayed ontop of others.
        """
        self.shapes[index].z_index = z_index
        self._z_index[index] = z_index
        self._update_z_order()

    def shift(self, index, shift):
        """Performs a 2D shift on a single shape located at index

        Parameters
        ----------
        index : int
            Location in list of the shape to be changed.
        shift : np.ndarray
            length 2 array specifying shift of shapes.
        """
        self.shapes[index].shift(shift)
        self._update_mesh_vertices(index, edge=True, face=True)

    def scale(self, index, scale, center=None):
        """Performs a scaling on a single shape located at index

        Parameters
        ----------
        index : int
            Location in list of the shape to be changed.
        scale : float, list
            scalar or list specifying rescaling of shape.
        center : list
            length 2 list specifying coordinate of center of scaling.
        """
        self.shapes[index].scale(scale, center=center)
        self.update(index)
        self._update_z_order()

    def rotate(self, index, angle, center=None):
        """Performs a rotation on a single shape located at index

        Parameters
        ----------
        index : int
            Location in list of the shape to be changed.
        angle : float
            angle specifying rotation of shape in degrees.
        center : list
            length 2 list specifying coordinate of center of rotation.
        """
        self.shapes[index].rotate(angle, center=center)
        self._update_mesh_vertices(index, edge=True, face=True)

    def flip(self, index, axis, center=None):
        """Performs an vertical flip on a single shape located at index

        Parameters
        ----------
        index : int
            Location in list of the shape to be changed.
        axis : int
            integer specifying axis of flip. `0` flips horizontal, `1` flips
            vertical.
        center : list
            length 2 list specifying coordinate of center of flip axes.
        """
        self.shapes[index].flip(axis, center=center)
        self._update_mesh_vertices(index, edge=True, face=True)

    def transform(self, index, transform):
        """Performs a linear transform on a single shape located at index

        Parameters
        ----------
        index : int
            Location in list of the shape to be changed.
        transform : np.ndarray
            2x2 array specifying linear transform.
        """
        self.shapes[index].transform(transform)
        self.update(index)
        self._update_z_order()
        self._clear_cache()

    def outline(
        self, indices: int | Sequence[int]
    ) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
        """Finds outlines of shapes listed in indices

        Parameters
        ----------
        indices : int | Sequence[int]
            Location in list of the shapes to be outline.
            If sequence, all elements should be ints

        Returns
        -------
        centers : np.ndarray
            Nx2 array of centers of outline
        offsets : np.ndarray
            Nx2 array of offsets of outline
        triangles : np.ndarray
            Mx3 array of any indices of vertices for triangles of outline
        """
        if isinstance(indices, Sequence) and len(indices) == 1:
            indices = indices[0]
        if not isinstance(indices, Sequence):
            shape = self.shapes[indices]
            return (
                shape._edge_vertices,
                shape._edge_offsets,
                shape._edge_triangles,
            )
        return self.outlines(indices)

    def outlines(
        self, indices: Sequence[int]
    ) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
        """Finds outlines of shapes listed in indices, using chunked processing.

        Parameters
        ----------
        indices : Sequence[int]
            Location in list of the shapes to be outline.

        Returns
        -------
        centers : np.ndarray
            Nx2 array of centers of outline
        offsets : np.ndarray
            Nx2 array of offsets of outline
        triangles : np.ndarray
            Mx3 array of any indices of vertices for triangles of outline
        """
        # Based on benchmarking, a chunk_size of 500 provides a good balance
        # of performance for a wide range of shape counts.
        chunk_size = 500

        if not indices:
            return (
                np.empty((0, self.ndisplay), dtype=CoordinateDtype),
                np.empty((0, self.ndisplay), dtype=CoordinateDtype),
                np.empty((0, 3), dtype=TriangleDtype),
            )

        centers_blocks = []
        offsets_blocks = []
        triangles_blocks = []
        n_verts_cumsum = 0

        for start in range(0, len(indices), chunk_size):
            chunk_indices = indices[start : start + chunk_size]
            chunk_shapes = [self.shapes[i] for i in chunk_indices]

            chunk_centers = []
            chunk_offsets = []
            chunk_tris = []
            n_verts_per_shape = []
            n_tris_per_shape = []

            for s in chunk_shapes:
                verts = s._edge_vertices
                chunk_centers.append(verts)
                chunk_offsets.append(s._edge_offsets)
                tris = s._edge_triangles
                chunk_tris.append(tris)
                n_verts_per_shape.append(verts.shape[0])
                n_tris_per_shape.append(tris.shape[0])

            centers = np.concatenate(chunk_centers)
            offsets = np.concatenate(chunk_offsets)
            triangles = np.concatenate(chunk_tris)

            # Offset triangle indices within chunk and across blocks
            if triangles.size > 0:
                vert_offsets = np.zeros(
                    len(n_verts_per_shape), dtype=triangles.dtype
                )
                if len(vert_offsets) > 1:
                    np.cumsum(n_verts_per_shape[:-1], out=vert_offsets[1:])
                tri_offsets = np.repeat(vert_offsets, n_tris_per_shape)
                triangles = (
                    triangles + tri_offsets[:, np.newaxis] + n_verts_cumsum
                )

            centers_blocks.append(centers)
            offsets_blocks.append(offsets)
            triangles_blocks.append(triangles)
            n_verts_cumsum += centers.shape[0]

        return (
            np.concatenate(centers_blocks),
            np.concatenate(offsets_blocks),
            np.concatenate(triangles_blocks),
        )

    def shapes_in_box(
        self, corners: np.ndarray[tuple[Literal[2], Literal[2]]]
    ) -> list[int]:
        """Determines which shapes, if any, are inside an axis-aligned box.

        Looks only at displayed shapes

        Parameters
        ----------
        corners : np.ndarray
            2x2 array of two corners that will be used to create an
            axis-aligned box.

        Returns
        -------
        shapes : list of ints
            List of shapes that are inside the box.
        """
        shape_mins, shape_maxs = self._bounding_boxes
        if shape_mins.shape[0] == 0:
            return []

        selection_min = np.min(corners, axis=0)
        selection_max = np.max(corners, axis=0)

        # If the box encompasses all shapes, just get them directly
        layer_min = np.min(shape_mins, axis=0)
        layer_max = np.max(shape_maxs, axis=0)
        if np.all(selection_min <= layer_min) and np.all(
            selection_max >= layer_max
        ):
            return self._visible_shapes_indices.tolist()

        # Get shapes with bounding boxes intersecting the selection box
        intersects_mask = np.all(shape_maxs >= selection_min, axis=1) & np.all(
            shape_mins <= selection_max, axis=1
        )

        intersecting_indices = self._visible_shapes_indices[intersects_mask]

        if intersecting_indices.size == 0:
            return []

        shape_mins = shape_mins[intersects_mask]
        shape_maxs = shape_maxs[intersects_mask]

        shapes_full_in_mask = np.all(
            shape_maxs <= selection_max, axis=1
        ) & np.all(shape_mins >= selection_min, axis=1)

        return [
            num
            for num, full_in in zip(
                intersecting_indices, shapes_full_in_mask, strict=True
            )
            if full_in
            or triangles_intersect_box(
                self._mesh.vertices[
                    self._mesh.triangles[self._mesh_triangles_slice(num)]
                ],
                corners,
            ).any()
        ]

    @cached_property
    def _visible_shapes(self) -> list[tuple[int, Shape]]:
        owner = _native.viewer_status_provider_locals()
        return _native.viewer_shape_list_visible(self, globals(), owner)

    @cached_property
    def _bounding_boxes(
        self,
    ) -> tuple[
        np.ndarray[tuple[int, Literal[2, 3]]],
        np.ndarray[tuple[int, Literal[2, 3]]],
    ]:
        owner = _native.viewer_status_provider_locals()
        return _native.viewer_shape_list_boxes(self, globals(), owner)

    @cached_property
    def _visible_shapes_indices(
        self,
    ) -> np.ndarray[tuple[int], np.dtype[IndexDtype]]:
        owner = _native.shape_state_locals(('self', 's'))
        return _native.shape_list_visible_indices(self, globals(), owner)

    def inside(self, coord):
        """Determines if any shape at given coord by looking inside triangle
        meshes. Looks only at displayed shapes

        Parameters
        ----------
        coord : sequence of float
            Image coordinates to check if any shapes are at.

        Returns
        -------
        shape : int | None
            Index of shape if any that is at the coordinates. Returns `None`
            if no shape is found.
        """
        inside_indices: object
        def generator_factory(iterator):
            yield from iterator
        owner = _native.viewer_status_provider_locals()
        return _native.viewer_shape_list_inside(lambda: self, lambda: coord, lambda: inside_indices, globals(), generator_factory, owner)

    def _inside_3d(self, ray_position: np.ndarray, ray_direction: np.ndarray):
        """Determines if any shape is intersected by a ray by looking inside triangle
        meshes. Looks only at displayed shapes.

        Parameters
        ----------
        ray_position : np.ndarray
            (3,) array containing the location that was clicked. This
            should be in the same coordinate system as the vertices.
        ray_direction : np.ndarray
            (3,) array describing the direction camera is pointing in
            the scene. This should be in the same coordinate system as
            the vertices.

        Returns
        -------
        shape : int | None
            Index of shape if any that is at the coordinates. Returns `None`
            if no shape is found.
        intersection_point : Optional[np.ndarray]
            The point where the ray intersects the mesh face. If there was
            no intersection, returns None.
        """
        owner = _native.viewer_status_provider_locals()
        return _native.viewer_shape_list_inside_3d(self, ray_position, ray_direction, globals(), owner)

    def _triangle_intersection(
        self,
        triangle_indices: np.ndarray,
        ray_position: np.ndarray,
        ray_direction: np.ndarray,
    ):
        """Find the intersection of a ray with specified triangles.

        Parameters
        ----------
        triangle_indices : np.ndarray
            (n,) array of shape indices to find the intersection with the ray. The indices should
            correspond with self._mesh.displayed_triangles.
        ray_position : np.ndarray
            (3,) array with the coordinate of the starting point of the ray in layer coordinates.
            Only provide the 3 displayed dimensions.
        ray_direction : np.ndarray
            (3,) array of the normal direction of the ray in layer coordinates.
            Only provide the 3 displayed dimensions.

        Returns
        -------
        intersection_points : np.ndarray
            (n x 3) array of the intersection of the ray with each of the specified shapes in layer coordinates.
            Only the 3 displayed dimensions are provided.
        """
        owner = _native.viewer_status_provider_locals()
        return _native.viewer_shape_list_triangle_intersection(self, triangle_indices, ray_position, ray_direction, globals(), owner)

    def to_masks(self, mask_shape=None, zoom_factor=1, offset=(0, 0)):
        """Returns N binary masks, one for each shape, embedded in an array of
        shape `mask_shape`.

        Parameters
        ----------
        mask_shape : np.ndarray | tuple | None
            2-tuple defining shape of mask to be generated. If non specified,
            takes the max of all the vertices
        zoom_factor : float
            Premultiplier applied to coordinates before generating mask. Used
            for generating as downsampled mask.
        offset : 2-tuple
            Offset subtracted from coordinates before multiplying by the
            zoom_factor. Used for putting negative coordinates into the mask.

        Returns
        -------
        masks : (N, M, P) np.ndarray
            Array where there is one binary mask of shape MxP for each of
            N shapes
        """
        if mask_shape is None:
            mask_shape = self.displayed_vertices.max(axis=0).astype('int')

        masks = np.array(
            [
                s.to_mask(mask_shape, zoom_factor=zoom_factor, offset=offset)
                for s in self.shapes
            ]
        )

        return masks

    def to_labels(self, labels_shape=None, zoom_factor=1, offset=(0, 0)):
        """Returns a integer labels image, where each shape is embedded in an
        array of shape labels_shape with the value of the index + 1
        corresponding to it, and 0 for background. For overlapping shapes
        z-ordering will be respected.

        Parameters
        ----------
        labels_shape : np.ndarray | tuple | None
            2-tuple defining shape of labels image to be generated. If non
            specified, takes the max of all the vertices
        zoom_factor : float
            Premultiplier applied to coordinates before generating mask. Used
            for generating as downsampled mask.
        offset : 2-tuple
            Offset subtracted from coordinates before multiplying by the
            zoom_factor. Used for putting negative coordinates into the mask.

        Returns
        -------
        labels : np.ndarray
            MxP integer array where each value is either 0 for background or an
            integer up to N for points inside the corresponding shape.
        """
        if labels_shape is None:
            labels_shape = self.displayed_vertices.max(axis=0).astype(int)

        labels = np.zeros(labels_shape, dtype=int)

        for ind in self._z_order[::-1]:
            mask = self.shapes[ind].to_mask(
                labels_shape, zoom_factor=zoom_factor, offset=offset
            )
            labels[mask] = ind + 1

        return labels

    def to_colors(
        self, colors_shape=None, zoom_factor=1, offset=(0, 0), max_shapes=None
    ):
        """Rasterize shapes to an RGBA image array.

        Each shape is embedded in an array of shape `colors_shape` with the
        RGBA value of the shape, and 0 for background. For overlapping shapes
        z-ordering will be respected.

        Parameters
        ----------
        colors_shape : np.ndarray | tuple | None
            2-tuple defining shape of colors image to be generated. If non
            specified, takes the max of all the vertiecs
        zoom_factor : float
            Premultiplier applied to coordinates before generating mask. Used
            for generating as downsampled mask.
        offset : 2-tuple
            Offset subtracted from coordinates before multiplying by the
            zoom_factor. Used for putting negative coordinates into the mask.
        max_shapes : None | int
            If provided, this is the maximum number of shapes that will be rasterized.
            If the number of shapes in view exceeds max_shapes, max_shapes shapes
            will be randomly selected from the in view shapes. If set to None, no
            maximum is applied. The default value is None.

        Returns
        -------
        colors : (N, M, 4) array
            rgba array where each value is either 0 for background or the rgba
            value of the shape for points inside the corresponding shape.
        """
        if colors_shape is None:
            colors_shape = self.displayed_vertices.max(axis=0).astype(int)

        colors = np.zeros((*colors_shape, 4), dtype=float)
        colors[..., 3] = 1

        z_order = self._z_order
        shapes_in_view = np.argwhere(self._displayed)
        z_order_in_view_mask = np.isin(z_order, shapes_in_view)
        z_order_in_view = z_order[z_order_in_view_mask]

        # If there are too many shapes to render responsively, just render
        # the top max_shapes shapes
        if max_shapes is not None and len(z_order_in_view) > max_shapes:
            z_order_in_view = z_order_in_view[-max_shapes:]

        for ind in z_order_in_view:
            mask = self.shapes[ind].to_mask(
                colors_shape, zoom_factor=zoom_factor, offset=offset
            )
            if type(self.shapes[ind]) in [Path, Line]:
                col = self._edge_color[ind]
            else:
                col = self._face_color[ind]
            colors[mask, :] = col

        return colors

    def _clear_cache(self):
        owner = _native.shape_state_locals(('self',))
        return _native.shape_list_clear_cache(self, globals(), owner)
