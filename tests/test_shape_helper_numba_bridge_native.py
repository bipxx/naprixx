"""Real Numba consumers and return-type contracts, before and after native port."""

from __future__ import annotations

import json
import time

import numpy as np
import pytest
from numba import njit
from numba.core import types
from numba.core.registry import CPUDispatcher
from numba.typed import List

from napari.layers.shapes import _accelerated_triangulate_numba as accelerated


@njit(cache=True)
def compiled_shape_helpers(points, bounds):
    path = accelerated.remove_path_duplicates(points, False)
    vertices, edges = accelerated.normalize_vertices_and_edges(points, True)
    polygons = accelerated.reconstruct_polygons_from_edges(vertices, edges)
    centers, offsets, triangles = accelerated.generate_2D_edge_meshes(path, True)
    box = accelerated.create_box_from_bounding(bounds)
    return centers, offsets, triangles, box, len(polygons), accelerated.is_convex(points)


@njit(cache=True)
def compiled_dot(first, second):
    return accelerated._dot(first, second)


def test_all_original_functions_remain_genuine_dispatchers():
    names = ("_dot", "_cross_z", "_sign_abs", "_orthogonal_vector", "_calc_output_size", "_set_centers_and_offsets", "_normalize_triangle_orientation", "_orientation", "_are_polar_angles_monotonic", "is_convex", "_direction_vec_and_half_length", "_generate_2D_edge_meshes_loop", "_cut_end_if_repetition", "generate_2D_edge_meshes", "remove_path_duplicates", "create_box_from_bounding", "reconstruct_polygons_from_edges", "normalize_vertices_and_edges")
    for name in names:
        function = getattr(accelerated, name)
        assert isinstance(function, CPUDispatcher), name
        assert function.py_func.__name__ == name
        assert function.py_func.__module__ == accelerated.__name__
        assert function._cache.__class__.__name__ == "FunctionCache"


@pytest.mark.parametrize("dtype", [np.float32, np.float64])
@pytest.mark.parametrize("order", ["C", "F", "strided"])
def test_compiled_workflow_preserves_array_types_layouts_and_values(dtype, order):
    points = np.array([[0, 0], [0, 4], [4, 4], [4, 0]], dtype=dtype, order="F" if order == "F" else "C")
    if order == "strided":
        expanded = np.empty((8, 2), dtype=dtype)
        expanded[::2] = points
        points = expanded[::2]
        assert not points.flags.c_contiguous
    bounds = np.array([[0, 0], [4, 4]], dtype=dtype)
    result = compiled_shape_helpers(points, bounds)
    direct = accelerated.generate_2D_edge_meshes.py_func(points, True)
    for observed, expected in zip(result[:3], direct, strict=True):
        np.testing.assert_array_equal(observed, expected)
    assert result[0].dtype == result[1].dtype == np.dtype("float32")
    assert result[2].dtype == np.dtype("int32")
    assert result[3].dtype == np.dtype("float32")
    np.testing.assert_array_equal(result[3][[0, 2, 4, 6]], [[0, 0], [4, 0], [4, 4], [0, 4]])
    assert result[4:] == (1, True)
    assert compiled_shape_helpers.nopython_signatures


@pytest.mark.parametrize("dtype, numba_type", [(np.float32, types.float32), (np.float64, types.float64)])
def test_scalar_precision_uses_actual_compiled_argument_type(dtype, numba_type):
    first = np.array([16777216, 1], dtype=dtype)
    second = np.array([1, 1], dtype=dtype)
    expected = dtype(16777216) + dtype(1)
    assert compiled_dot(first, second) == expected
    assert accelerated._dot.py_func(first, second) == expected
    signature = compiled_dot.signatures[-1]
    assert signature[0].dtype == numba_type
    assert compiled_dot.overloads[signature].signature.return_type == numba_type


def test_reconstruction_returns_real_typed_list_and_normalization_fixed_types():
    points = np.array([[0, 0], [0, 4], [4, 4], [4, 0]], dtype=np.float64)
    vertices, edges = accelerated.normalize_vertices_and_edges(points, True)
    assert vertices.dtype == np.dtype("float32")
    assert edges.dtype == np.dtype("int64")
    polygons = accelerated.reconstruct_polygons_from_edges(vertices, edges)
    assert isinstance(polygons, List)
    assert len(polygons) == 1
    assert polygons._numba_type_.item_type == types.Array(types.float32, 2, "C")
    assert polygons[0].dtype == np.dtype("float32")
    assert {tuple(row) for row in polygons[0]} == {tuple(row) for row in points}


def test_dedup_noop_keeps_actual_array_storage():
    points = np.array([[0, 0], [0, 4], [4, 4], [4, 0]], dtype=np.float32, order="F")
    result = accelerated.remove_path_duplicates(points, False)
    assert result is points
    assert result.flags.f_contiguous
    result[1, 0] = 2
    assert points[1, 0] == 2


def test_compiled_workflow_records_warm_latency_without_claiming_release_parity():
    points = np.array([[0, 0], [0, 4], [4, 4], [4, 0]], dtype=np.float32)
    bounds = np.array([[0, 0], [4, 4]], dtype=np.float32)
    compiled_shape_helpers(points, bounds)
    start = time.perf_counter()
    for _ in range(100):
        result = compiled_shape_helpers(points, bounds)
    elapsed = time.perf_counter() - start
    assert result[4:] == (1, True)
    print("SHAPE_HELPER_TIMING " + json.dumps({"workflow": "compiled_shape_helpers", "calls": 100, "elapsed_seconds": elapsed, "release_performance_proven": False}))
