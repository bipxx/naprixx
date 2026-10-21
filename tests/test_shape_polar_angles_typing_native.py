"""Original compiled polar-angle oracle; float32 libm precision is authoritative."""

from __future__ import annotations

import ast
import hashlib
import sys
from math import atan2, pi
from pathlib import Path
from types import ModuleType

import numpy as np
import pytest
from numba import njit, typeof
from numba.core.registry import CPUDispatcher

from napari.layers.shapes import _accelerated_triangulate_numba as actual
from napari.layers.shapes.shape_types import CoordinateArray2D

ROOT = Path(__file__).resolve().parents[1]
FROZEN = ROOT / (
    ".artifacts/shape-helper-closure-before/current/"
    "napari/layers/shapes/_accelerated_triangulate_numba.py"
)
PINNED = (
    ROOT / ".upstream/napari/src/napari/layers/shapes/_accelerated_triangulate_numba.py"
)
ORIGINAL_SHA = "50dd996c7ebf92b84a6bb0944884eb6e4b2e8a7a9eea25571d0d466b1bfdec90"
DATA = {
    "wrap": (((1.0, 0.0), (0.0, 1.0), (-1.0, 0.0), (0.0, -1.0)), (0.0, 0.0)),
    "single": (((1.0, -0.0),), (0.0, 0.0)),
    "descending": (((1.0, 0.0), (0.0, 1.0), (1.0, 0.0)), (0.0, 0.0)),
    "translated": (
        (
            (16777216.125, 0.125),
            (16777220.375, 3.25),
            (16777216.625, 8.125),
            (16777212.875, 3.5),
        ),
        (16777216.125, 3.125),
    ),
    "branchcut": (((-1.0, 0.0), (-1.0, -0.0), (0.0, -1.0), (1.0, -0.0)), (0.0, 0.0)),
    "rounded_tie": (((1.0, 8.0), (1.0000001192092896, 8.0), (1.0, 8.0)), (0.0, 0.0)),
    "early_tail": (((1.0, 0.0), (0.0, 1.0), (1.0, 0.0), (0.125, 1.75)), (0.0, 0.0)),
}
# id, data, vertices dtype, centroid dtype, vertices/centroid layout, graph mode
CASES = (
    ("f32_increasing_wrap", "wrap", "f32", "f32", "C", "C", "disjoint"),
    ("one_vertex_f64_start_only", "single", "f64", "f64", "F", "C", "disjoint"),
    ("f32_early_descending", "descending", "f32", "f32", "C", "C", "disjoint"),
    (
        "mixed_f32_vertices_f64_centroid",
        "translated",
        "f32",
        "f64",
        "C",
        "C",
        "disjoint",
    ),
    (
        "mixed_f64_vertices_f32_centroid",
        "translated",
        "f64",
        "f32",
        "F",
        "C",
        "disjoint",
    ),
    ("f32_signed_zero_pi_branchcut", "branchcut", "f32", "f32", "C", "C", "disjoint"),
    ("f64_signed_zero_pi_branchcut", "branchcut", "f64", "f64", "F", "C", "disjoint"),
    (
        "f32_atan2_one_ulp_rounded_tie",
        "rounded_tie",
        "f32",
        "f32",
        "C",
        "C",
        "disjoint",
    ),
    (
        "readonly_positive_vertices_negative_centroid",
        "wrap",
        "f32",
        "f32",
        "positive",
        "negative",
        "disjoint",
    ),
    (
        "readonly_negative_vertices_positive_centroid",
        "wrap",
        "f64",
        "f64",
        "negative",
        "positive",
        "disjoint",
    ),
    (
        "centroid_is_readonly_vertex_row",
        "wrap",
        "f32",
        "f32",
        "C",
        "C",
        "shared_centroid",
    ),
    (
        "early_stop_before_initialized_nonfinite_tail",
        "early_tail",
        "f64",
        "f64",
        "C",
        "C",
        "nonfinite_tail",
    ),
)


@njit(cache=True)
def compiled_current(vertices, centroid):
    result = actual._are_polar_angles_monotonic(vertices, centroid)
    return result, vertices, centroid


@pytest.fixture(scope="module")
def original_compiled():
    raw = FROZEN.read_bytes()
    assert hashlib.sha256(raw).hexdigest() == ORIGINAL_SHA
    assert raw == PINNED.read_bytes()
    tree = ast.parse(raw)
    function = next(
        n
        for n in tree.body
        if isinstance(n, ast.FunctionDef) and n.name == "_are_polar_angles_monotonic"
    )
    pinned_function = next(
        n
        for n in ast.parse(PINNED.read_bytes()).body
        if isinstance(n, ast.FunctionDef) and n.name == function.name
    )
    assert ast.dump(function) == ast.dump(pinned_function)
    future = next(
        n
        for n in tree.body
        if isinstance(n, ast.ImportFrom) and n.module == "__future__"
    )
    name = "_naparixx_test_polar_angles_original_oracle"
    assert name not in sys.modules
    module = ModuleType(name)
    module.__file__ = str(FROZEN)
    module.__dict__.update(
        np=np, njit=njit, atan2=atan2, pi=pi, CoordinateArray2D=CoordinateArray2D
    )
    sys.modules[name] = module
    exec(
        compile(
            ast.Module(body=[future, function], type_ignores=[]),
            str(FROZEN),
            "exec",
            dont_inherit=True,
        ),
        module.__dict__,
    )
    assert isinstance(module._are_polar_angles_monotonic, CPUDispatcher)
    return module._are_polar_angles_monotonic


def _input(values, dtype, layout):
    if layout in ("C", "F"):
        root = np.array(values, dtype=dtype, order=layout)
        return root, root
    if isinstance(values[0], tuple):
        root = np.full((len(values) * 2, 4), 1103.125, dtype=dtype)
        array = root[::2, ::2] if layout == "positive" else root[-1::-2, 3::-2]
    else:
        root = np.full(len(values) * 2, 1103.125, dtype=dtype)
        array = root[::2] if layout == "positive" else root[-1::-2]
    array[:] = values
    array.flags.writeable = False
    return array, root


def _graph(case):
    _, data, vertex_dtype, centroid_dtype, vertex_layout, centroid_layout, mode = case
    vertices_values, centroid_values = DATA[data]
    vertices, vertex_root = _input(
        vertices_values,
        np.float32 if vertex_dtype == "f32" else np.float64,
        vertex_layout,
    )
    centroid, centroid_root = _input(
        centroid_values,
        np.float32 if centroid_dtype == "f32" else np.float64,
        centroid_layout,
    )
    if mode == "nonfinite_tail":
        vertices[-1] = (np.nan, np.inf)
    if mode == "shared_centroid":
        vertices.flags.writeable = False
        centroid = vertices[1]
        return vertices, centroid, {"shared": vertex_root}
    return vertices, centroid, {"vertices": vertex_root, "centroid": centroid_root}


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


@pytest.mark.parametrize("case", CASES, ids=[case[0] for case in CASES])
def test_original_compiled_polar_boolean_precision_storage_and_types(
    original_compiled, case
):
    vertices, centroid, roots = _graph(case)
    expected_vertices, expected_centroid, expected_roots = _graph(case)
    before = {name: _bits(root) for name, root in roots.items()}
    expected_before = {name: _bits(root) for name, root in expected_roots.items()}
    expected = original_compiled(expected_vertices, expected_centroid)
    result, returned_vertices, returned_centroid = compiled_current(vertices, centroid)
    assert type(result) is type(expected) is bool
    assert result is expected
    assert returned_vertices is vertices and returned_centroid is centroid
    assert type(vertices) is type(centroid) is np.ndarray
    assert _metadata(vertices) == _metadata(expected_vertices)
    assert _metadata(centroid) == _metadata(expected_centroid)
    assert np.shares_memory(vertices, centroid) == np.shares_memory(
        expected_vertices, expected_centroid
    )
    assert roots.keys() == expected_roots.keys()
    for name, root in roots.items():
        np.testing.assert_array_equal(_bits(root), before[name])
        np.testing.assert_array_equal(
            _bits(expected_roots[name]), expected_before[name]
        )
        np.testing.assert_array_equal(_bits(root), _bits(expected_roots[name]))
    signature = (typeof(vertices), typeof(centroid))
    expected_signature = (typeof(expected_vertices), typeof(expected_centroid))
    assert (
        signature in compiled_current.signatures
        and expected_signature in original_compiled.signatures
    )
    actual_types = compiled_current.overloads[signature].signature.return_type.types
    original_type = original_compiled.overloads[
        expected_signature
    ].signature.return_type
    assert actual_types[0] == original_type
    assert actual_types[1:] == signature
    assert (
        compiled_current.nopython_signatures and original_compiled.nopython_signatures
    )
