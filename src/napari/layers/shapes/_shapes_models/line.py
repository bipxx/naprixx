from naparixx import _native
import numpy as np

from napari.layers.shapes._shapes_models.shape import Shape
from napari.layers.shapes._shapes_utils import create_box


class Line(Shape):
    """Class for a single line segment

    Parameters
    ----------
    data : (2, D) array
        Line vertices.
    edge_width : float
        thickness of lines and edges.
    z_index : int
        Specifier of z order priority. Shapes with higher z order are displayed
        ontop of others.
    dims_order : (D,) list
        Order that the dimensions are to be rendered in.
    """

    def __init__(
        self,
        data,
        *,
        edge_width=1,
        z_index=0,
        dims_order=None,
        ndisplay=2,
    ) -> None:
        super_provider = lambda _receiver=self: super()
        owner = _native.shape_model_locals(('self', 'data', 'edge_width', 'z_index', 'dims_order', 'ndisplay'), {'self': self, 'data': data, 'edge_width': edge_width, 'z_index': z_index, 'dims_order': dims_order, 'ndisplay': ndisplay})
        del self, data, edge_width, z_index, dims_order, ndisplay
        return _native.shape_line_init(globals(), (None, ('edge_width', 'z_index', 'dims_order', 'ndisplay'), False, 'line'), super_provider, owner)

    @property
    def data(self):
        """(2, D) array: line vertices."""
        owner = _native.shape_model_locals(('self',), {'self': self})
        del self
        return _native.shape_line_data(globals(), ('(2, D) array: line vertices.',), owner)

    @data.setter
    def data(self, data):
        owner = _native.shape_model_locals(('self', 'data'), {'self': self, 'data': data})
        del self, data
        return _native.shape_line_set_data(globals(), (None, 1, 2, 'Data shape does not match a line. A line expects two end vertices, ', ' provided.', 0, ('axis',)), owner)

    def _update_displayed_data(self) -> None:
        """Update the data that is to be displayed."""
        # For path connect every all data
        owner = _native.shape_model_locals(('self',), {'self': self})
        del self
        return _native.shape_line_update(globals(), ('Update the data that is to be displayed.', False, ('face', 'closed'), None, 'int'), owner)
