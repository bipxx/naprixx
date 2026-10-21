"""Independent original compiled centers oracle; no production fallback."""

from __future__ import annotations

import ast
import hashlib
import sys
from pathlib import Path
from types import ModuleType

import numpy as np
import pytest
from numba import njit
from numba.core import types
from numba.core.registry import CPUDispatcher

from napari.layers.shapes import _accelerated_triangulate_numba as actual

ROOT = Path(__file__).resolve().parents[1]
FROZEN = ROOT / (
    ".artifacts/shape-helper-closure-before/current/"
    "napari/layers/shapes/_accelerated_triangulate_numba.py"
)
PINNED = ROOT / (
    ".upstream/napari/src/napari/layers/shapes/_accelerated_triangulate_numba.py"
)
ORIGINAL_SHA = "50dd996c7ebf92b84a6bb0944884eb6e4b2e8a7a9eea25571d0d466b1bfdec90"
OWNED = (
    "_dot",
    "_cross_z",
    "_sign_abs",
    "_orthogonal_vector",
    "_set_centers_and_offsets",
)
J = 3
ROWS = 12

# Geometry and dtype choices are source inputs, not formulas copied from a kernel.
CASES = (
    (
        "collinear_signedzero_f32",
        "f32",
        "f32",
        "f32",
        (1.0, -0.0),
        (1.0, -0.0),
        10.0,
        10.0,
        -1.0,
        False,
        "plain",
        2,
    ),
    (
        "miter_f64",
        "f64",
        "f64",
        "f64",
        (1.0, 0.0),
        (0.8, 0.6),
        10.0,
        10.0,
        -1.0,
        False,
        "plain",
        2,
    ),
    (
        "bevel_positive_f32",
        "f32",
        "f32",
        "f32",
        (1.0, 0.0),
        (0.0, 1.0),
        10.0,
        10.0,
        0.1,
        False,
        "plain",
        3,
    ),
    (
        "bevel_negative_f64",
        "f64",
        "f64",
        "f64",
        (1.0, 0.0),
        (0.0, -1.0),
        10.0,
        10.0,
        0.1,
        False,
        "plain",
        3,
    ),
    (
        "always_bevel_mixed",
        "f32",
        "f64",
        "f64",
        (1.0, 0.0),
        (0.8, 0.6),
        2.75,
        0.5,
        -1.0,
        True,
        "plain",
        3,
    ),
    (
        "limit_bevel_f32",
        "f32",
        "f32",
        "f32",
        (1.0, 0.0),
        (0.6, 0.8),
        10.0,
        10.0,
        0.9,
        False,
        "plain",
        3,
    ),
    (
        "cutoff_first_f64",
        "f64",
        "f64",
        "f64",
        (1.0, 0.0),
        (0.9999995, 0.001),
        0.03125,
        0.5,
        -1.0,
        True,
        "plain",
        3,
    ),
    (
        "cutoff_second_f32",
        "f32",
        "f32",
        "f32",
        (1.0, 0.0),
        (0.8, 0.6),
        1.0,
        0.015625,
        -1.0,
        True,
        "plain",
        3,
    ),
    (
        "precision_miter_mixed",
        "f32",
        "f64",
        "f64",
        (0.7071067690849304, 0.7071067690849304),
        (0.6, -0.8),
        10.0,
        10.0,
        -1.0,
        False,
        "plain",
        2,
    ),
    (
        "collinear_always_f64",
        "f64",
        "f64",
        "f64",
        (1.0, -0.0),
        (1.0, -0.0),
        10.0,
        10.0,
        -1.0,
        True,
        "plain",
        3,
    ),
    (
        "readonly_negative_inputs_f32",
        "f32",
        "f32",
        "f32",
        (1.0, 0.0),
        (0.8, 0.6),
        10.0,
        10.0,
        -1.0,
        False,
        "readonly_inputs",
        2,
    ),
    (
        "negative_outputs_f64",
        "f64",
        "f64",
        "f64",
        (1.0, 0.0),
        (0.0, -1.0),
        10.0,
        10.0,
        0.1,
        False,
        "negative_outputs",
        3,
    ),
    (
        "vec1_centers_overlap_f32",
        "f32",
        "f32",
        "f32",
        (1.0, 0.0),
        (0.8, 0.6),
        10.0,
        10.0,
        -1.0,
        False,
        "vec1_centers",
        2,
    ),
    (
        "vec2_centers_overlap_f64",
        "f64",
        "f64",
        "f64",
        (1.0, 0.0),
        (0.8, 0.6),
        10.0,
        10.0,
        -1.0,
        False,
        "vec2_centers",
        2,
    ),
    (
        "centers_offsets_same_array_f32",
        "f32",
        "f32",
        "f32",
        (1.0, 0.0),
        (0.0, 1.0),
        10.0,
        10.0,
        0.1,
        False,
        "centers_offsets",
        3,
    ),
    (
        "vertex_offsets_overlap_f64",
        "f64",
        "f64",
        "f64",
        (1.0, 0.0),
        (0.0, 1.0),
        10.0,
        10.0,
        0.1,
        False,
        "vertex_offsets",
        3,
    ),
)


@njit(cache=True)
def compiled_current(
    centers,
    offsets,
    triangles,
    vertex,
    vec1,
    vec2,
    half_vec1_len,
    half_vec2_len,
    j,
    cos_limit,
    always_bevel,
):
    added = actual._set_centers_and_offsets(
        centers,
        offsets,
        triangles,
        vertex,
        vec1,
        vec2,
        half_vec1_len,
        half_vec2_len,
        j,
        cos_limit,
        always_bevel,
    )
    return added, centers, offsets, triangles


@pytest.fixture(scope="module")
def original_compiled():
    raw = FROZEN.read_bytes()
    assert hashlib.sha256(raw).hexdigest() == ORIGINAL_SHA
    assert raw == PINNED.read_bytes()
    tree = ast.parse(raw)
    pinned_tree = ast.parse(PINNED.read_bytes())
    definitions = [
        node
        for node in tree.body
        if isinstance(node, ast.FunctionDef) and node.name in OWNED
    ]
    pinned_definitions = [
        node
        for node in pinned_tree.body
        if isinstance(node, ast.FunctionDef) and node.name in OWNED
    ]
    assert tuple(node.name for node in definitions) == OWNED
    assert [ast.dump(node) for node in definitions] == [
        ast.dump(node) for node in pinned_definitions
    ]
    future = next(
        node
        for node in tree.body
        if isinstance(node, ast.ImportFrom) and node.module == "__future__"
    )
    selected = ast.Module(body=[future, *definitions], type_ignores=[])
    name = "_naparixx_test_centers_original_oracle"
    assert name not in sys.modules
    module = ModuleType(name)
    module.__file__ = str(FROZEN)
    module.__dict__.update(np=np, njit=njit)
    sys.modules[name] = module
    exec(compile(selected, str(FROZEN), "exec", dont_inherit=True), module.__dict__)
    assert all(isinstance(getattr(module, name), CPUDispatcher) for name in OWNED)
    original = module._set_centers_and_offsets

    @njit(cache=True)
    def consumer(
        centers,
        offsets,
        triangles,
        vertex,
        vec1,
        vec2,
        half_vec1_len,
        half_vec2_len,
        j,
        cos_limit,
        always_bevel,
    ):
        added = original(
            centers,
            offsets,
            triangles,
            vertex,
            vec1,
            vec2,
            half_vec1_len,
            half_vec2_len,
            j,
            cos_limit,
            always_bevel,
        )
        return added, centers, offsets, triangles

    return consumer


def _dtype(name):
    return np.float32 if name == "f32" else np.float64


def _vector(values, dtype, readonly, roots, key):
    if readonly:
        storage = np.full(4, 811.5, dtype=dtype)
        view = storage[2::-2]
        view[:] = values
        view.flags.writeable = False
    else:
        storage = np.array(values, dtype=dtype)
        view = storage
    roots[key] = storage
    return view


def _inputs(case):
    (
        _,
        vec1_dtype,
        vec2_dtype,
        output_dtype,
        first,
        second,
        half1,
        half2,
        limit,
        bevel,
        layout,
        _,
    ) = case
    dtype = _dtype(output_dtype)
    roots = {}
    if layout == "negative_outputs":
        roots["centers"] = np.full((ROWS * 2, 6), 701.25, dtype=dtype)
        roots["offsets"] = np.full((ROWS * 2, 6), -907.5, dtype=dtype)
        roots["triangles"] = np.full((ROWS * 2, 8), -1037, dtype=np.int64)
        centers = roots["centers"][-1::-2, 4:0:-2]
        offsets = roots["offsets"][-1::-2, 4:0:-2]
        triangles = roots["triangles"][-1::-2, 6:0:-2]
    else:
        roots["centers"] = np.full((ROWS, 2), 701.25, dtype=dtype)
        centers = roots["centers"]
        if layout == "centers_offsets":
            offsets = centers
        else:
            roots["offsets"] = np.full((ROWS, 2), -907.5, dtype=dtype)
            offsets = roots["offsets"]
        roots["triangles"] = np.full((ROWS, 3), -1037, dtype=np.int32)
        triangles = roots["triangles"]
    readonly = layout == "readonly_inputs"
    vertex = _vector((0.3125, -2.125), dtype, readonly, roots, "vertex")
    if layout == "vec1_centers":
        vec1 = centers[J]
        vec1[:] = first
    else:
        vec1 = _vector(first, _dtype(vec1_dtype), readonly, roots, "vec1")
    if layout == "vec2_centers":
        vec2 = centers[J + 1]
        vec2[:] = second
    else:
        vec2 = _vector(second, _dtype(vec2_dtype), readonly, roots, "vec2")
    if layout == "vertex_offsets":
        vertex = offsets[J + 1]
        vertex[:] = (0.3125, -2.125)
        roots.pop("vertex")
    args = (
        centers,
        offsets,
        triangles,
        vertex,
        vec1,
        vec2,
        dtype(half1),
        dtype(half2),
        J,
        dtype(limit),
        bevel,
    )
    return args, roots


def _exact_storage(actual_storage, expected_storage):
    assert actual_storage.dtype == expected_storage.dtype
    if actual_storage.dtype.kind == "f":
        bits = np.uint32 if actual_storage.dtype.itemsize == 4 else np.uint64
        np.testing.assert_array_equal(
            actual_storage.copy(order="C").view(bits),
            expected_storage.copy(order="C").view(bits),
        )
    else:
        np.testing.assert_array_equal(actual_storage, expected_storage)


@pytest.mark.parametrize("case", CASES, ids=[case[0] for case in CASES])
def test_original_compiled_centers_values_aliases_and_mutations(
    original_compiled, case
):
    actual_args, actual_roots = _inputs(case)
    expected_args, expected_roots = _inputs(case)
    actual_before = {name: value.copy() for name, value in actual_roots.items()}
    original_result = original_compiled(*expected_args)
    actual_result = compiled_current(*actual_args)
    assert actual_result[0] == original_result[0] == case[-1]
    assert (
        compiled_current.nopython_signatures and original_compiled.nopython_signatures
    )
    assert (
        compiled_current.overloads[
            compiled_current.signatures[-1]
        ].signature.return_type.types[0]
        == types.int64
    )
    assert (
        original_compiled.overloads[
            original_compiled.signatures[-1]
        ].signature.return_type.types[0]
        == types.int64
    )
    for index in range(3):
        assert actual_result[index + 1] is actual_args[index]
        assert original_result[index + 1] is expected_args[index]
    assert actual_roots.keys() == expected_roots.keys()
    for name in actual_roots:
        _exact_storage(actual_roots[name], expected_roots[name])
        # Every untouched physical element retains its initialized sentinel/value.
        expected_untouched = expected_roots[name] == actual_before[name]
        np.testing.assert_array_equal(
            actual_roots[name][expected_untouched],
            actual_before[name][expected_untouched],
        )
    if case[-2] == "centers_offsets":
        assert actual_result[1] is actual_result[2]
    if case[-2] in ("vec1_centers", "vec2_centers"):
        aliased_vector = actual_args[4 if case[-2] == "vec1_centers" else 5]
        assert np.shares_memory(aliased_vector, actual_args[0])
        np.testing.assert_array_equal(aliased_vector, actual_args[3])
    if case[-2] == "vertex_offsets":
        assert np.shares_memory(actual_args[3], actual_args[1])
    if case[-2] == "readonly_inputs":
        assert all(
            not value.flags.writeable and value.strides[0] < 0
            for value in actual_args[3:6]
        )
    if case[-2] == "negative_outputs":
        assert all(
            value.flags.writeable and all(stride < 0 for stride in value.strides)
            for value in actual_args[:3]
        )
