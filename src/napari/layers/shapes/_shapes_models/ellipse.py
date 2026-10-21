from naparixx import _native
import numpy as np

from napari.layers.shapes._shapes_models.shape import Shape
from napari.layers.shapes._shapes_utils import (
    center_radii_to_corners,
    rectangle_to_box,
    triangulate_edge,
    triangulate_ellipse,
)


class Ellipse(Shape):
    """Class for a single ellipse

    Parameters
    ----------
    data : (4, D) array or (2, 2) array.
        Either a (2, 2) array specifying the center and radii of an axis
        aligned ellipse, or a (4, D) array specifying the four corners of a
        bounding box that contains the ellipse. These need not be axis aligned.
    edge_width : float
        thickness of lines and edges.
    opacity : float
        Opacity of the shape, must be between 0 and 1.
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
        opacity=1.0,
        z_index=0,
        dims_order=None,
        ndisplay=2,
    ) -> None:
        super_provider = lambda _receiver=self: super()
        owner = _native.shape_model_locals(('self', 'data', 'edge_width', 'opacity', 'z_index', 'dims_order', 'ndisplay'), {'self': self, 'data': data, 'edge_width': edge_width, 'opacity': opacity, 'z_index': z_index, 'dims_order': dims_order, 'ndisplay': ndisplay})
        del self, data, edge_width, opacity, z_index, dims_order, ndisplay
        return _native.shape_ellipse_init(globals(), (None, ('edge_width', 'z_index', 'dims_order', 'ndisplay'), True, 'ellipse'), super_provider, owner)

    @property
    def data(self):
        """(4, D) array: ellipse vertices."""
        owner = _native.shape_model_locals(('self',), {'self': self})
        del self
        return _native.shape_ellipse_data(globals(), ('(4, D) array: ellipse vertices.',), owner)

    @data.setter
    def data(self, data):
        owner = _native.shape_model_locals(('self', 'data'), {'self': self, 'data': data})
        del self, data
        return _native.shape_ellipse_set_data(globals(), (None, 1, 2, 0, 4, 'Data shape does not match a ellipse. Ellipse expects four corner vertices, ', ' provided.', ('axis',)), owner)

    def _update_displayed_data(self) -> None:
        """Update the data that is to be displayed."""
        # Build boundary vertices with num_segments
        owner = _native.shape_model_locals(('self', 'vertices', 'triangles'), {'self': self})
        del self
        return _native.shape_ellipse_update(globals(), ('Update the data that is to be displayed.', 1, -1, False, ('face',), None, 'int'), owner)

    def transform(self, transform):
        """Performs a linear transform on the shape

        Parameters
        ----------
        transform : np.ndarray
            2x2 array specifying linear transform.
        """
        owner = _native.shape_model_locals(('self', 'transform', 'points', 'centers', 'offsets', 'triangles'), {'self': self, 'transform': transform})
        del self, transform
        return _native.shape_ellipse_transform(globals(), ('Performs a linear transform on the shape\n\n        Parameters\n        ----------\n        transform : np.ndarray\n            2x2 array specifying linear transform.\n        ', None, 1, -1, ('closed',), 0, ('axis',)), owner)
