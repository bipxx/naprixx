"""Keep napari's Python contracts at the boundary of native numerical code."""

from functools import wraps

import numpy as np

from . import _native

_REAL_DTYPES = frozenset(
    np.dtype(t)
    for t in (
        np.bool_,
        np.int8,
        np.uint8,
        np.int16,
        np.uint16,
        np.int32,
        np.uint32,
        np.int64,
        np.uint64,
        np.float32,
        np.float64,
    )
)
_FLOAT_DTYPES = frozenset((np.dtype(np.float32), np.dtype(np.float64)))


def _double(value):
    return _native.transform_adapter_double(value, globals())


def _plain_real(value):
    return type(value) is np.ndarray and value.dtype in _REAL_DTYPES


def affine_adapter(original):
    @wraps(original)
    def call(self, coords):
        locals_owner = _native.transform_adapter_locals()
        return _native.affine_adapter_call(
            lambda: original, self, coords, globals(), locals_owner
        )

    return call


def scale_translate_adapter(original):
    @wraps(original)
    def call(self, coords):
        locals_owner = _native.transform_adapter_locals()
        return _native.scale_translate_adapter_call(
            lambda: original, self, coords, globals(), locals_owner
        )

    return call


def point_slice_adapter(original):
    modes = {"none": 0, "all": 1, "rescale_linear": 2, "rescale_spherical": 3}

    @wraps(original)
    def call(self, not_disp):
        if not (
            type(self.data) is np.ndarray
            and self.data.ndim == 2
            and self.data.dtype in _FLOAT_DTYPES
            and _plain_real(self.size)
            and type(self.shown) is np.ndarray
            and self.shown.dtype == np.bool_
            and str(self.projection_mode) in modes
        ):
            return original(self, not_disp)
        point, left, right = self.data_slice[not_disp].as_array()
        mode = modes[str(self.projection_mode)]
        if point.dtype != np.float64 or (mode == 3 and self.size.dtype != np.float64):
            return original(self, not_disp)
        indices, size = _native.slice_points(
            self.data,
            _double(self.size),
            self.shown,
            list(not_disp),
            _double(point),
            _double(left),
            _double(right),
            mode,
        )
        if mode in (0, 1) and indices.size:
            size = size.astype(self.size.dtype, copy=False)
        return indices.astype(int, copy=False), size

    return call


def range_adapter(original):
    @wraps(original)
    def call(data, rgb=False, dtype=None):
        return _native.range_adapter_call(
            original, data, rgb=rgb, dtype=dtype
        )

    return call


def interpolation_adapter(original):
    @wraps(original)
    def call(old_coord, new_coord, brush_size):
        if old_coord is None:
            old_coord = new_coord
        if new_coord is None:
            new_coord = old_coord
        old, new = np.asarray(old_coord), np.asarray(new_coord)
        if (
            old.ndim != 1
            or new.ndim != 1
            or old.shape != new.shape
            or old.dtype != np.float64
            or new.dtype != np.float64
            or old.size == 0
            or not np.isfinite(brush_size)
            or brush_size <= 0
            or not np.isfinite(old).all()
            or not np.isfinite(new).all()
        ):
            return original(old_coord, new_coord, brush_size)
        return _native.interpolate_coordinates(old, new, brush_size)

    return call


def ray_adapter(original):
    @wraps(original)
    def call(data, start_point, end_point):
        start, end = np.asarray(start_point), np.asarray(end_point)
        if (
            not _plain_real(data)
            or data.ndim == 0
            or data.size == 0
            or start.shape != (data.ndim,)
            or end.shape != (data.ndim,)
            or start.dtype != np.float64
            or end.dtype != np.float64
            or not np.isfinite(start).all()
            or not np.isfinite(end).all()
        ):
            return original(data, start_point, end_point)
        result = _native.first_nonzero_coordinate(data, start, end)
        return None if result is None else result.astype(int, copy=False)

    return call


def normal_adapter(original):
    def call_factory():
        def call(a, b, p=(0, 0, 1)):
            locals_owner = _native.shape_accel_locals(('a', 'b', 'p', 'first', 'second', 'reference'), {'a': a, 'b': b, 'p': p}, {})
            del a, b, p
            return _native.shape_accel_normal_adapter_call(lambda: original, _native.shape_accel_namespace(), (None, (1, 2), -1, (2, 3), (3,)), locals_owner)
        return call
    return _native.shape_accel_normal_adapter_factory(original, _native.shape_accel_namespace(), call_factory)


def projection_adapter(original):
    def call_factory(decorator):
        @decorator
        def call(points, plane_point, plane_normal):
            def generator_factory(iterator):
                yield from iterator
            owner = _native.viewer_geometry_locals()
            return _native.viewer_geometry_projection_adapter_call(lambda: original, points, plane_point, plane_normal, globals(), generator_factory, (3,), owner)
        return call
    return _native.viewer_geometry_projection_adapter_factory(original, globals(), call_factory)


def box_adapter(original):
    @wraps(original)
    def call(corners, points, sizes):
        box, array, size = map(np.asarray, (corners, points, sizes))
        if (
            box.ndim != 2
            or box.shape[1] != 2
            or not len(box)
            or array.ndim != 2
            or array.shape[1] != 2
            or size.shape != (len(array),)
            or any(a.dtype not in _FLOAT_DTYPES for a in (box, array, size))
            or size.dtype != np.float64
        ):
            return original(corners, points, sizes)
        return _native.points_in_box(_double(box), _double(array), _double(size))

    return call
