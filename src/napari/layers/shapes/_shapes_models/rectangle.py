from naparixx import _native
import numpy as np

from napari.layers.shapes._shapes_models.shape import Shape
from napari.layers.shapes._shapes_utils import find_corners, rectangle_to_box
from napari.types import ArrayLike


class Rectangle(Shape):
    """Class for a single rectangle

    Parameters
    ----------
    data : (4, D) or (2, 2) array
        Either a (2, 2) array specifying the two corners of an axis aligned
        rectangle, or a (4, D) array specifying the four corners of a bounding
        box that contains the rectangle. These need not be axis aligned.
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
        data: ArrayLike,
        *,
        edge_width=1,
        z_index=0,
        dims_order=None,
        ndisplay=2,
    ) -> None:
        super_provider = lambda _receiver=self: super()
        owner = _native.shape_model_locals(('self', 'data', 'edge_width', 'z_index', 'dims_order', 'ndisplay'), {'self': self, 'data': data, 'edge_width': edge_width, 'z_index': z_index, 'dims_order': dims_order, 'ndisplay': ndisplay})
        del self, data, edge_width, z_index, dims_order, ndisplay
        return _native.shape_rectangle_init(globals(), (None, ('edge_width', 'z_index', 'dims_order', 'ndisplay'), True, 'rectangle'), super_provider, owner)

    @property
    def data(self) -> np.ndarray:
        """(4, D) array: rectangle vertices."""
        owner = _native.shape_model_locals(('self',), {'self': self})
        del self
        return _native.shape_rectangle_data(globals(), ('(4, D) array: rectangle vertices.',), owner)

    @data.setter
    def data(self, data: ArrayLike):
        owner = _native.shape_model_locals(('self', 'data'), {'self': self, 'data': data})
        del self, data
        return _native.shape_rectangle_set_data(globals(), (None, 1, 2, 4, 'Data shape does not match a rectangle. Rectangle expects four corner vertices, ', ' provided.', 0, ('axis',)), owner)

    def _update_displayed_data(self) -> None:
        """Update the data that is to be displayed."""
        # Add four boundary lines and then two triangles for each
        owner = _native.shape_model_locals(('self', 'data_displayed'), {'self': self})
        del self
        return _native.shape_rectangle_update(globals(), ('Update the data that is to be displayed.', False, ('face',), (0, 1, 2), (0, 2, 3), None, 'int'), owner)
