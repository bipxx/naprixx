from naparixx import _native
from napari.layers.shapes._shapes_models._polygon_base import PolygonBase


class Path(PolygonBase):
    """Class for a single path, which is a sequence of line segments.

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
    """

    def __init__(
        self,
        data,
        *,
        edge_width=1,
        z_index=0,
        dims_order=None,
        ndisplay=2,
        interpolation_order=1,
    ) -> None:
        super_provider = lambda _receiver=self: super()
        owner = _native.shape_model_locals(('self', 'data', 'edge_width', 'z_index', 'dims_order', 'ndisplay', 'interpolation_order'), {'self': self, 'data': data, 'edge_width': edge_width, 'z_index': z_index, 'dims_order': dims_order, 'ndisplay': ndisplay, 'interpolation_order': interpolation_order})
        del self, data, edge_width, z_index, dims_order, ndisplay, interpolation_order
        return _native.shape_path_init(globals(), (None, False, 'path', ('edge_width', 'z_index', 'dims_order', 'ndisplay', 'filled', 'closed', 'name', 'interpolation_order')), super_provider, owner)
