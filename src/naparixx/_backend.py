"""Control explicit native dispatch in the bundled napari source fork."""

import importlib
from functools import wraps
from threading import RLock

from . import _adapters, _native

NAPARI_VERSION = "0.9.2"
_LOCK = RLock()
_ENABLED = True

_SPECS = (
    ("napari.utils.transforms.transforms", "Affine", "__call__", "affine_adapter"),
    (
        "napari.utils.transforms.transforms",
        "ScaleTranslate",
        "__call__",
        "scale_translate_adapter",
    ),
    (
        "napari.layers.points._slice",
        "_PointSliceRequest",
        "_get_slice_data",
        "point_slice_adapter",
    ),
    ("napari.layers.utils.layer_utils", None, "calc_data_range", "range_adapter"),
    (
        "napari.layers.labels._labels_utils",
        None,
        "interpolate_coordinates",
        "interpolation_adapter",
    ),
    (
        "napari.layers.labels._labels_utils",
        None,
        "first_nonzero_coordinate",
        "ray_adapter",
    ),
    ("napari.layers.utils.layer_utils", None, "segment_normal", "normal_adapter"),
    ("napari.utils.geometry", None, "project_points_onto_plane", "projection_adapter"),
    ("napari.layers.points._points_utils", None, "points_in_box", "box_adapter"),
)


def dispatch(factory):
    """Bind a numerical implementation when its source module is imported.

    Callables and aliases keep their identity when dispatch is toggled. The
    Generic implementations remain available when optimized array kernels
    cannot handle a representation. Converted application paths stay in C++
    through both settings; unconverted numerical helpers retain Python code.
    """

    def decorate(original):
        adapter = _native.backend_make_adapter(factory, lambda: original, globals())

        @wraps(original)
        def call(*args, **kwargs):
            return _native.backend_dispatch(
                lambda: adapter, lambda: original, args, kwargs, globals()
            )

        return call

    return decorate


def enable():
    """Enable numerical C++ dispatch; application models are always native."""

    global _ENABLED
    locals_owner = _native.backend_control_locals()
    return _native.backend_enable(globals(), locals_owner)


def disable():
    """Use generic numerical dispatch without replacing callables.

    This is a verification switch. It does not disable native application
    models or restore a different napari installation.
    """

    global _ENABLED
    return _native.backend_disable(globals())


def backend_status():
    """Report activation and registered operations without changing state."""

    locals_owner = _native.backend_control_locals()
    return _native.backend_status(globals(), locals_owner)
