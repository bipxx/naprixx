"""Independent full-original compiled convexity and edge-generation contracts."""

from __future__ import annotations

import ast
import hashlib
import sys
from enum import Enum
from math import atan2, pi
from pathlib import Path
from types import ModuleType

import numpy as np
import pytest
from numba import njit, typeof
from numba.core.registry import CPUDispatcher

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
SCOPE = (
    "Orientation",
    "_dot",
    "_cross_z",
    "_sign_abs",
    "_orthogonal_vector",
    "_calc_output_size",
    "_set_centers_and_offsets",
    "_normalize_triangle_orientation",
    "_orientation",
    "_are_polar_angles_monotonic",
    "is_convex",
    "_direction_vec_and_half_length",
    "_generate_2D_edge_meshes_loop",
    "_cut_end_if_repetition",
    "generate_2D_edge_meshes",
)
DATA = {
    "two_points": ((0.0, -0.0), (3.0, 4.0)),
    "collinear_three": ((0.0, -0.0), (1.0, 2.0), (2.0, 4.0)),
    "collinear_four": ((0.0, -0.0), (1.0, 2.0), (2.0, 4.0), (3.0, 6.0)),
    "early_concave": ((0.0, 0.0), (4.0, 0.0), (1.0, 1.0), (0.0, 4.0), (1e200, -1e200)),
    "rectangle": ((0.0, 0.0), (0.0, 4.0), (4.0, 4.0), (4.0, 0.0)),
    "reverse_rectangle": ((4.0, 0.0), (4.0, 4.0), (0.0, 4.0), (0.0, 0.0)),
    "large_octagon": (
        (100000000.0, 0.0),
        (70710680.0, 70710680.0),
        (0.0, 100000000.0),
        (-70710680.0, 70710680.0),
        (-100000000.0, 0.0),
        (-70710680.0, -70710680.0),
        (0.0, -100000000.0),
        (70710680.0, -70710680.0),
    ),
    "shifted_polygon": (
        (8.00000011920929, 0.125),
        (4.00000011920929, 8.125),
        (-3.9999998807907104, 8.125),
        (-7.99999988079071, 0.125),
        (-3.9999998807907104, -7.875),
        (4.00000011920929, -7.875),
    ),
    "fractional_rectangle": (
        (0.125, -0.25),
        (0.125, 4.125),
        (4.375, 4.125),
        (4.375, -0.25),
    ),
    "acute_path": ((0.0, 0.0), (10.0, 1.0), (9.0, 1.1)),
    "repeated_pair": ((1.25, -0.0), (1.25, -0.0)),
    "rounded_pair": ((1.0, -0.0), (1.0000000000000002, 0.0)),
}
# id, dataset, dtype, layout
CONVEX_CASES = (
    ("fewer_two_f32_source_false", "two_points", "f32", "C"),
    ("collinear_triangle_f64_source_true", "collinear_three", "f64", "F"),
    ("all_collinear_four_f32", "collinear_four", "f32", "C"),
    ("concave_early_flip_before_finite_canary", "early_concave", "f64", "positive"),
    ("rectangle_forward_f32_negative_strides", "rectangle", "f32", "negative"),
    ("rectangle_reverse_f64_fortran", "reverse_rectangle", "f64", "F"),
    ("f32_sequential_mean_cancellation", "large_octagon", "f32", "C"),
    ("f64_mean_commit_then_centroid_f32", "shifted_polygon", "f64", "negative"),
)
# id, dataset, dtype, layout, closed, limit dtype/value, bevel, default call
EDGE_CASES = (
    ("open_two_nonunit_f32", "two_points", "f32", "C", False, "f64", 3.0, False, False),
    (
        "open_default_f64_conversion",
        "fractional_rectangle",
        "f64",
        "C",
        False,
        "f64",
        3.0,
        False,
        True,
    ),
    (
        "closed_rectangle_f32_fortran",
        "rectangle",
        "f32",
        "F",
        True,
        "f64",
        3.0,
        False,
        False,
    ),
    (
        "open_always_bevel_positive_strides",
        "acute_path",
        "f32",
        "positive",
        False,
        "f64",
        3.0,
        True,
        False,
    ),
    (
        "closed_always_bevel_f64_negative_strides",
        "fractional_rectangle",
        "f64",
        "negative",
        True,
        "f64",
        3.0,
        True,
        False,
    ),
    (
        "open_acute_limit_trigger_f64_path_f32_limit",
        "acute_path",
        "f64",
        "F",
        False,
        "f32",
        1.25,
        False,
        False,
    ),
    (
        "repeated_pair_trim_before_short_and_zero_limit",
        "repeated_pair",
        "f32",
        "positive",
        False,
        "f64",
        0.0,
        False,
        False,
    ),
    (
        "f64_pair_rounds_same_f32_closed_short_zero_limit",
        "rounded_pair",
        "f64",
        "F",
        True,
        "f32",
        0.0,
        True,
        False,
    ),
)


@njit(cache=True)
def compiled_convex_current(vertices):
    result = actual.is_convex(vertices)
    return result, vertices


@njit(cache=True)
def compiled_edges_current(path, closed, limit, bevel):
    centers, offsets, triangles = actual.generate_2D_edge_meshes(
        path, closed, limit, bevel
    )
    return centers, offsets, triangles, path


@njit(cache=True)
def compiled_edges_default_current(path):
    centers, offsets, triangles = actual.generate_2D_edge_meshes(path)
    return centers, offsets, triangles, path


@pytest.fixture(scope="module")
def original_family():
    raw = FROZEN.read_bytes()
    assert hashlib.sha256(raw).hexdigest() == ORIGINAL_SHA
    assert raw == PINNED.read_bytes()
    tree = ast.parse(raw)
    pinned = ast.parse(PINNED.read_bytes())
    selected = [
        n
        for n in tree.body
        if isinstance(n, (ast.ClassDef, ast.FunctionDef)) and n.name in SCOPE
    ]
    pinned_selected = [
        n
        for n in pinned.body
        if isinstance(n, (ast.ClassDef, ast.FunctionDef)) and n.name in SCOPE
    ]
    assert tuple(n.name for n in selected) == SCOPE
    assert [ast.dump(n) for n in selected] == [ast.dump(n) for n in pinned_selected]
    future = next(
        n
        for n in tree.body
        if isinstance(n, ast.ImportFrom) and n.module == "__future__"
    )
    name = "_naparixx_test_convex_edge_full_original_oracle"
    assert name not in sys.modules
    module = ModuleType(name)
    module.__file__ = str(FROZEN)
    module.__dict__.update(np=np, njit=njit, Enum=Enum, atan2=atan2, pi=pi)
    sys.modules[name] = module
    exec(
        compile(
            ast.Module(body=[future, *selected], type_ignores=[]),
            str(FROZEN),
            "exec",
            dont_inherit=True,
        ),
        module.__dict__,
    )
    assert issubclass(module.Orientation, Enum)
    for source_name in SCOPE[1:]:
        assert isinstance(getattr(module, source_name), CPUDispatcher)
    assert isinstance(actual.is_convex, CPUDispatcher)
    assert isinstance(actual.generate_2D_edge_meshes, CPUDispatcher)
    return module


def _graph(dataset, dtype_name, layout):
    values = DATA[dataset]
    dtype = np.float32 if dtype_name == "f32" else np.float64
    rows = len(values)
    if layout == "C":
        root = np.full((rows + 2, 2), 1703.125, dtype=dtype)
        array = root[1 : rows + 1]
    elif layout == "F":
        root = np.full((rows, 4), 1703.125, dtype=dtype, order="F")
        array = root[:, 1:3]
    else:
        root = np.full((2 * rows + 2, 6), 1703.125, dtype=dtype)
        array = (
            root[1 : 1 + 2 * rows : 2, 1:5:2]
            if layout == "positive"
            else root[2 * rows : 0 : -2, 4:0:-2]
        )
    array[:] = values
    array.flags.writeable = False
    return array, root


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


def _unchanged_roots(root, before, expected_root, expected_before):
    np.testing.assert_array_equal(_bits(root), before)
    np.testing.assert_array_equal(_bits(expected_root), expected_before)
    np.testing.assert_array_equal(_bits(root), _bits(expected_root))


@pytest.mark.parametrize("case", CONVEX_CASES, ids=[c[0] for c in CONVEX_CASES])
def test_original_compiled_convexity_precision_early_exit_and_storage(
    original_family, case
):
    _, dataset, dtype_name, layout = case
    vertices, root = _graph(dataset, dtype_name, layout)
    expected_vertices, expected_root = _graph(dataset, dtype_name, layout)
    before, expected_before = _bits(root), _bits(expected_root)
    expected = original_family.is_convex(expected_vertices)
    result, returned_vertices = compiled_convex_current(vertices)
    assert type(result) is type(expected) is bool
    assert result is expected
    assert returned_vertices is vertices
    assert _metadata(vertices) == _metadata(expected_vertices)
    _unchanged_roots(root, before, expected_root, expected_before)
    signature = (typeof(vertices),)
    original_signature = (typeof(expected_vertices),)
    assert signature in compiled_convex_current.signatures
    assert original_signature in original_family.is_convex.signatures
    current_types = compiled_convex_current.overloads[
        signature
    ].signature.return_type.types
    original_type = original_family.is_convex.overloads[
        original_signature
    ].signature.return_type
    assert current_types[0] == original_type
    assert current_types[1] == signature[0]
    assert (
        compiled_convex_current.nopython_signatures
        and original_family.is_convex.nopython_signatures
    )


@pytest.mark.parametrize("case", EDGE_CASES, ids=[c[0] for c in EDGE_CASES])
def test_original_compiled_edge_generation_values_types_and_storage(
    original_family, case
):
    _, dataset, dtype_name, layout, closed, limit_name, limit_value, bevel, defaults = (
        case
    )
    path, root = _graph(dataset, dtype_name, layout)
    expected_path, expected_root = _graph(dataset, dtype_name, layout)
    before, expected_before = _bits(root), _bits(expected_root)
    if defaults:
        args, expected_args = (path,), (expected_path,)
        consumer = compiled_edges_default_current
        expected = original_family.generate_2D_edge_meshes(*expected_args)
    else:
        limit = (np.float32 if limit_name == "f32" else np.float64)(limit_value)
        args, expected_args = (
            (path, closed, limit, bevel),
            (expected_path, closed, limit, bevel),
        )
        consumer = compiled_edges_current
        expected = original_family.generate_2D_edge_meshes(*expected_args)
    observed = consumer(*args)
    assert type(observed) is type(expected) is tuple
    assert len(observed) == 4 and len(expected) == 3
    assert observed[3] is path
    assert _metadata(path) == _metadata(expected_path)
    for index, expected_array in enumerate(expected):
        actual_array = observed[index]
        assert type(actual_array) is type(expected_array) is np.ndarray
        assert _metadata(actual_array) == _metadata(expected_array)
        np.testing.assert_array_equal(_bits(actual_array), _bits(expected_array))
        for input_array in (path, expected_path, root, expected_root):
            assert not np.shares_memory(actual_array, input_array)
            assert not np.shares_memory(expected_array, input_array)
        for other in observed[:index]:
            assert not np.shares_memory(actual_array, other)
    _unchanged_roots(root, before, expected_root, expected_before)
    signature = tuple(typeof(arg) for arg in args)
    assert signature in consumer.signatures
    current_types = consumer.overloads[signature].signature.return_type.types
    original_returns = original_family.generate_2D_edge_meshes.overloads
    original_types = {
        str(impl.signature.return_type) for impl in original_returns.values()
    }
    assert len(original_types) == 1
    original_type = next(iter(original_returns.values())).signature.return_type
    assert current_types[:3] == original_type.types
    assert current_types[3] == signature[0]
    assert (
        consumer.nopython_signatures
        and original_family.generate_2D_edge_meshes.nopython_signatures
    )
