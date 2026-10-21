from naparixx import _native
import numpy as np

from napari.layers.shapes._accelerated_triangulate_dispatch import (
    create_box_from_bounding,
)
from napari.layers.shapes._shapes_models.shape import (
    Shape,
    remove_path_duplicates,
)


class PolygonBase(Shape):
    """Class for a polygon or path.

    Parameters
    ----------
    data : np.ndarray
        NxD array of vertices specifying the path.
    edge_width : float
        thickness of lines and edges.
    z_index : int
        Specifier of z order priority. Shapes with higher z order are displayed
        ontop of others.
    dims_order : (D,) list
        Order that the dimensions are to be rendered in.
    closed : bool
        Bool if shape edge is a closed path or not.
    filled : bool
        Flag if array is filled or not.
    name : str
        Name of the shape.
    interpolation_order : int
        Order of spline interpolation for the sides. 1 means no interpolation.
    """

    def __init__(
        self,
        data,
        *,
        edge_width=1,
        z_index=0,
        dims_order=None,
        ndisplay=2,
        filled=True,
        closed=True,
        name='polygon',
        interpolation_order=1,
        interpolation_sampling=50,
    ) -> None:
        super_provider = lambda _receiver=self: super()
        owner = _native.shape_model_locals(('self', 'data', 'edge_width', 'z_index', 'dims_order', 'ndisplay', 'filled', 'closed', 'name', 'interpolation_order', 'interpolation_sampling'), {'self': self, 'data': data, 'edge_width': edge_width, 'z_index': z_index, 'dims_order': dims_order, 'ndisplay': ndisplay, 'filled': filled, 'closed': closed, 'name': name, 'interpolation_order': interpolation_order, 'interpolation_sampling': interpolation_sampling})
        del self, data, edge_width, z_index, dims_order, ndisplay, filled, closed, name, interpolation_order, interpolation_sampling
        return _native.shape_polygon_base_init(globals(), (None, ('edge_width', 'z_index', 'dims_order', 'ndisplay')), super_provider, owner)

    @property
    def data(self):
        """np.ndarray: NxD array of vertices."""
        owner = _native.shape_model_locals(('self',), {'self': self})
        del self
        return _native.shape_polygon_base_data(globals(), ('np.ndarray: NxD array of vertices.',), owner)

    @data.setter
    def data(self, data):
        owner = _native.shape_model_locals(('self', 'data'), {'self': self, 'data': data})
        del self, data
        return _native.shape_polygon_base_set_data(globals(), (None, 1, 2, 'Shape needs at least two unique vertices, ', ' provided.', 0, ('axis',)), owner)

    def _update_displayed_data(self) -> None:
        """Update the data that is to be displayed."""
        owner = _native.shape_model_locals(('self', 'data', 'splev', 'splprep', 'data_spline', 'tck', '_', 'u', 'bbox'), {'self': self})
        del self
        return _native.shape_polygon_base_update(globals(), ('Update the data that is to be displayed.', 1, 0, ('splev', 'splprep'), True, ('closed',), None, ('axis',), ('s', 'k', 'per'), -1, ('face', 'closed'), 'scipy.interpolate'), owner)
