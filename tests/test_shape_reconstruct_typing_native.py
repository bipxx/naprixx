"""Independent original compiled reconstruction traversal and storage contracts."""

from __future__ import annotations

import ast
import hashlib
import sys
from pathlib import Path
from types import ModuleType

import numpy as np
import pytest
from numba import njit, typeof
from numba.core.registry import CPUDispatcher
from numba.typed import List

from napari.layers.shapes import _accelerated_triangulate_numba as actual

ROOT = Path(__file__).resolve().parents[1]
FROZEN = ROOT / (
    ".artifacts/shape-helper-closure-before/current/"
    "napari/layers/shapes/_accelerated_triangulate_numba.py"
)
PINNED = (
    ROOT / ".upstream/napari/src/napari/layers/shapes/_accelerated_triangulate_numba.py"
)
ORIGINAL_SHA = "50dd996c7ebf92b84a6bb0944884eb6e4b2e8a7a9eea25571d0d466b1bfdec90"
SCOPE = "reconstruct_polygons_from_edges"
DATA = {
    "triangle": ((-0.0, 0.0), (2.125, -0.0), (0.625, 3.75)),
    "quad": ((-0.0, 0.0), (2.125, -0.0), (3.625, 3.75), (-1.5, 2.25)),
    "disconnected": (
        (-0.0, 0.0),
        (2.125, -0.0),
        (0.625, 3.75),
        (1709.0, -1709.0),
        (6.25, 2.0),
        (8.5, 2.0),
        (7.0, 5.125),
    ),
    "branch": (
        (-0.0, 0.0),
        (2.125, -0.0),
        (3.625, 3.75),
        (-1.5, 2.25),
        (5.5, 4.125),
    ),
    "three_dimensional": (
        (-0.0, 0.0, 1.0000000000000002),
        (2.125, -0.0, 1.0000000596046448),
        (3.625, 3.75, -0.0),
        (-1.5, 2.25, 10000000000000000.0),
    ),
    "copy_nonfinite": (
        (-0.0, np.inf, np.nan),
        (np.nan, -np.inf, 1.0000001192092896),
        (2.5, -0.0, 0.0),
        (1709.0, -1709.0, -0.0),
    ),
    "zero_vertices": (),
}
# id, vertex data, width, coordinate dtype/layout, edge rows/layout
# Every endpoint is in [-len(vertices), len(vertices)); no unchecked OOB domain.
CASES = (
    (
        "single_triangle_f32_c",
        "triangle",
        2,
        "f32",
        "C",
        ((0, 1), (1, 2), (2, 0)),
        "C",
    ),
    (
        "reversed_cycle_f64_fortran",
        "triangle",
        2,
        "f64",
        "F",
        ((1, 0), (2, 1), (0, 2)),
        "F",
    ),
    (
        "disconnected_cycles_first_edge_order",
        "disconnected",
        2,
        "f32",
        "positive",
        ((4, 5), (5, 6), (6, 4), (0, 1), (1, 2), (2, 0)),
        "positive",
    ),
    (
        "interleaved_disconnected_edges_negative_strides",
        "disconnected",
        2,
        "f64",
        "negative",
        ((0, 1), (4, 5), (2, 1), (6, 5), (2, 0), (6, 4)),
        "negative",
    ),
    (
        "open_chain_first_available_incident",
        "quad",
        2,
        "f32",
        "negative",
        ((0, 1), (2, 1), (2, 3)),
        "F",
    ),
    (
        "branching_incidence_append_order",
        "branch",
        2,
        "f64",
        "positive",
        ((0, 1), (1, 2), (1, 3), (3, 0), (2, 4)),
        "C",
    ),
    (
        "self_edge_then_cycle_keeps_source_duplicates",
        "triangle",
        2,
        "f32",
        "F",
        ((0, 0), (0, 1), (1, 2), (2, 0)),
        "negative",
    ),
    (
        "duplicate_edge_closes_then_leaves_open_chain",
        "triangle",
        2,
        "f64",
        "C",
        ((0, 1), (0, 1), (1, 2), (2, 0)),
        "positive",
    ),
    (
        "mixed_endpoint_orientation_cycle",
        "quad",
        2,
        "f32",
        "F",
        ((0, 1), (2, 1), (2, 3), (0, 3)),
        "C",
    ),
    (
        "three_columns_f64_bits_negative_vertices",
        "three_dimensional",
        3,
        "f64",
        "negative",
        ((0, 1), (1, 2), (2, 3), (3, 0)),
        "positive",
    ),
    (
        "three_columns_f32_nonfinite_copy_only",
        "copy_nonfinite",
        3,
        "f32",
        "positive",
        ((0, 1), (1, 2), (2, 0)),
        "F",
    ),
    (
        "safe_negative_endpoints_preserve_literal_indices",
        "quad",
        2,
        "f64",
        "F",
        ((-4, -3), (-2, -3), (-2, -1), (-4, -1)),
        "negative",
    ),
    ("zero_edges_populated_vertices_typed_result", "quad", 2, "f32", "F", (), "C"),
    ("zero_vertices_zero_edges_typed_result", "zero_vertices", 2, "f64", "C", (), "C"),
)


@njit(cache=True)
def compiled_reconstruct_current(vertices, edges):
    polygons = actual.reconstruct_polygons_from_edges(vertices, edges)
    return polygons, vertices, edges


def _source_nodes(tree):
    selected = []
    for node in tree.body:
        if isinstance(node, ast.ImportFrom) and node.module in {
            "__future__",
            "typing",
            "numba",
            "numba.core",
            "numba.typed",
            "napari.layers.shapes.shape_types",
        }:
            selected.append(node)
        elif isinstance(node, ast.Import) and any(
            alias.name == "numpy" for alias in node.names
        ):
            selected.append(node)
        elif isinstance(node, ast.FunctionDef) and node.name == SCOPE:
            selected.append(node)
    return selected


@pytest.fixture(scope="module")
def original_compiled():
    raw = FROZEN.read_bytes()
    assert hashlib.sha256(raw).hexdigest() == ORIGINAL_SHA
    assert raw == PINNED.read_bytes()
    selected = _source_nodes(ast.parse(raw))
    pinned_selected = _source_nodes(ast.parse(PINNED.read_bytes()))
    assert ast.dump(ast.Module(body=selected, type_ignores=[])) == ast.dump(
        ast.Module(body=pinned_selected, type_ignores=[])
    )
    definitions = [node for node in selected if isinstance(node, ast.FunctionDef)]
    assert len(definitions) == 3
    assert [ast.unparse(node.decorator_list[0]) for node in definitions] == [
        "overload",
        "overload",
        "njit(cache=True)",
    ]
    name = "_naparixx_test_reconstruct_original_oracle"
    assert name not in sys.modules
    module = ModuleType(name)
    module.__file__ = str(FROZEN)
    sys.modules[name] = module
    try:
        exec(
            compile(
                ast.Module(body=selected, type_ignores=[]),
                str(FROZEN),
                "exec",
                dont_inherit=True,
            ),
            module.__dict__,
        )
        original = module.reconstruct_polygons_from_edges
        assert isinstance(original, CPUDispatcher)
        assert isinstance(actual.reconstruct_polygons_from_edges, CPUDispatcher)
        assert module.List is List
        for name in (
            "CoordinateArray",
            "CoordinateArray2D",
            "CoordinateArray3D",
            "EdgeArray",
        ):
            assert module.__dict__[name] is actual.__dict__[name]
        assert original.py_func.__code__.co_varnames[:2] == (
            actual.reconstruct_polygons_from_edges.py_func.__code__.co_varnames[:2]
        )
        assert original.py_func.__annotations__ == (
            actual.reconstruct_polygons_from_edges.py_func.__annotations__
        )
        assert not original.py_func.__code__.co_freevars
        yield original
    finally:
        del sys.modules[module.__name__]


def _storage(values, columns, dtype, layout):
    rows = len(values)
    if layout == "C":
        root = np.full((rows + 2, columns), 1709, dtype=dtype)
        array = root[1 : rows + 1]
    elif layout == "F":
        root = np.full((rows, columns + 2), 1709, dtype=dtype, order="F")
        array = root[:, 1 : columns + 1]
    else:
        root = np.full((2 * rows + 2, 2 * columns + 2), 1709, dtype=dtype)
        if layout == "positive":
            array = root[1 : 1 + 2 * rows : 2, 1 : 1 + 2 * columns : 2]
        else:
            array = root[2 * rows : 0 : -2, 2 * columns : 0 : -2]
    array[:] = np.asarray(values, dtype=dtype).reshape(rows, columns)
    array.flags.writeable = False
    return array, root


def _graph(case):
    _, dataset, columns, dtype, layout, edge_rows, edge_layout = case
    vertices, vertex_root = _storage(
        DATA[dataset], columns, np.float32 if dtype == "f32" else np.float64, layout
    )
    edges, edge_root = _storage(edge_rows, 2, np.int64, edge_layout)
    return vertices, edges, vertex_root, edge_root


def _bits(array):
    return array.copy(order="C").view(
        np.uint32 if array.dtype.itemsize == 4 else np.uint64
    )


def _metadata(array):
    return (
        type(array),
        array.dtype,
        array.shape,
        array.strides,
        array.flags.c_contiguous,
        array.flags.f_contiguous,
        array.flags.writeable,
    )


def _assert_results(polygons, expected, vertices, *inputs):
    assert type(polygons) is type(expected) is List
    assert typeof(polygons) == typeof(expected)
    assert len(polygons) == len(expected)
    arrays = tuple(polygons)
    original_arrays = tuple(expected)
    for index, (array, original_array) in enumerate(
        zip(arrays, original_arrays, strict=True)
    ):
        assert type(array) is type(original_array) is np.ndarray
        assert _metadata(array) == _metadata(original_array)
        assert array.dtype == vertices.dtype
        assert array.ndim == 2 and array.shape[1] == vertices.shape[1]
        assert array.flags.c_contiguous and array.flags.writeable
        np.testing.assert_array_equal(_bits(array), _bits(original_array))
        for source in inputs:
            assert not np.shares_memory(array, source)
            assert not np.shares_memory(original_array, source)
        for other in arrays[:index] + original_arrays:
            assert not np.shares_memory(array, other)
        for other in original_arrays[:index]:
            assert not np.shares_memory(original_array, other)
    return arrays, original_arrays


@pytest.mark.parametrize("case", CASES, ids=[case[0] for case in CASES])
def test_original_compiled_reconstruct_traversal_types_and_storage(
    original_compiled, case
):
    vertices, edges, vertex_root, edge_root = _graph(case)
    original_vertices, original_edges, original_vertex_root, original_edge_root = (
        _graph(case)
    )
    roots = (vertex_root, edge_root, original_vertex_root, original_edge_root)
    before = tuple(_bits(root) for root in roots)
    # These rows contain only initialized, in-bounds source accesses.
    assert all(
        -len(vertices) <= int(endpoint) < len(vertices) for endpoint in edges.flat
    )
    expected = original_compiled(original_vertices, original_edges)
    polygons, returned_vertices, returned_edges = compiled_reconstruct_current(
        vertices, edges
    )
    assert returned_vertices is vertices and returned_edges is edges
    assert _metadata(vertices) == _metadata(original_vertices)
    assert _metadata(edges) == _metadata(original_edges)
    arrays, original_arrays = _assert_results(
        polygons,
        expected,
        vertices,
        vertices,
        edges,
        original_vertices,
        original_edges,
        *roots,
    )
    for root, snapshot in zip(roots, before, strict=True):
        np.testing.assert_array_equal(_bits(root), snapshot)
    signature = (typeof(vertices), typeof(edges))
    original_signature = (typeof(original_vertices), typeof(original_edges))
    assert signature in compiled_reconstruct_current.signatures
    assert original_signature in original_compiled.signatures
    returned_types = compiled_reconstruct_current.overloads[
        signature
    ].signature.return_type.types
    original_return_type = original_compiled.overloads[
        original_signature
    ].signature.return_type
    assert returned_types[0] == original_return_type == typeof(expected)
    assert returned_types[1:] == signature
    assert (
        compiled_reconstruct_current.nopython_signatures
        and original_compiled.nopython_signatures
    )
    # Mutate a returned array, then read it through the genuine result list.
    # This establishes shared ownership within that result and independence
    # from readonly sources without assuming repeat boxing returns one PyObject.
    if arrays:
        other_before = tuple(_bits(array) for array in arrays[1:])
        original_other_before = tuple(_bits(array) for array in original_arrays[1:])
        arrays[0][0, 0] = 1723
        original_arrays[0][0, 0] = 1723
        assert np.shares_memory(polygons[0], arrays[0])
        assert np.shares_memory(expected[0], original_arrays[0])
        np.testing.assert_array_equal(_bits(polygons[0]), _bits(expected[0]))
        for array, snapshot in zip(arrays[1:], other_before, strict=True):
            np.testing.assert_array_equal(_bits(array), snapshot)
        for array, snapshot in zip(
            original_arrays[1:], original_other_before, strict=True
        ):
            np.testing.assert_array_equal(_bits(array), snapshot)
    for root, snapshot in zip(roots, before, strict=True):
        np.testing.assert_array_equal(_bits(root), snapshot)
