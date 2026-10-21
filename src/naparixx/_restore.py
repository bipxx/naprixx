"""Importable pickle constructors for native application snapshots."""

from . import _native


def slice_plan(ndisplay, order):
    return _native.SlicePlan(ndisplay, order)


def layer_load(loaded, request_id):
    result = _native.LayerLoadState()
    result.loaded = loaded
    result.request_id = request_id
    return result


def dims(*args):
    return _native.make_dims_state(*args)


def camera(*args):
    return _native.restore_camera_state(*args)


def grid(*args):
    return _native.make_grid_state(*args)
