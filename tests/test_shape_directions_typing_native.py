"""Independent compiled original directions oracle; unspecified tails stay unread."""

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
NONUNIT = ((0.0, 0.0), (3.0, 4.0), (3.0, 9.0), (9.0, 9.0))
ROUND32 = ((0.125, -0.125), (0.333333343, 0.777777791), (4.875, -3.25), (4.875, 2.25))
ROUND64 = (
    (16777216.125, 0.3),
    (16777218.5, 4.9),
    (16777219.75, -2.4),
    (16777227.375, -2.1),
)
CASES = (
    ("open_f32_c_nonunit", "f32", False, "C", NONUNIT),
    ("closed_f32_c_nonunit", "f32", True, "C", NONUNIT),
    ("open_f64_c_rounding", "f64", False, "C", ROUND64),
    ("closed_f64_f_rounding", "f64", True, "F", ROUND64),
    ("open_f32_f_rounding", "f32", False, "F", ROUND32),
    ("closed_f64_readonly_positive_stride", "f64", True, "readonly_positive", NONUNIT),
    (
        "open_f32_readonly_negative_rows",
        "f32",
        False,
        "readonly_negative_rows",
        ROUND32,
    ),
    (
        "closed_f64_readonly_negative_rows_columns",
        "f64",
        True,
        "readonly_negative_both",
        ROUND64,
    ),
    (
        "closed_f32_zero_adjacent_prospective",
        "f32",
        True,
        "C",
        ((0.0, 0.0), (0.0, 0.0), (3.0, 4.0), (8.0, 1.0)),
    ),
    ("open_two_points_f64", "f64", False, "C", ((0.125, -0.25), (2.375, 7.125))),
)


@njit(cache=True)
def compiled_current(path, closed):
    normals, half = actual._direction_vec_and_half_length(path, closed)
    return normals, half, path


@njit(cache=True)
def compiled_return_identity(normals, half):
    return normals, half


@pytest.fixture(scope="module")
def original_compiled():
    raw = FROZEN.read_bytes()
    assert hashlib.sha256(raw).hexdigest() == ORIGINAL_SHA
    assert raw == PINNED.read_bytes()
    tree = ast.parse(raw)
    function = next(
        node
        for node in tree.body
        if isinstance(node, ast.FunctionDef)
        and node.name == "_direction_vec_and_half_length"
    )
    pinned_function = next(
        node
        for node in ast.parse(PINNED.read_bytes()).body
        if isinstance(node, ast.FunctionDef)
        and node.name == "_direction_vec_and_half_length"
    )
    assert ast.dump(function) == ast.dump(pinned_function)
    future = next(
        node
        for node in tree.body
        if isinstance(node, ast.ImportFrom) and node.module == "__future__"
    )
    selected = ast.Module(body=[future, function], type_ignores=[])
    name = "_naparixx_test_directions_original_oracle"
    assert name not in sys.modules
    module = ModuleType(name)
    module.__file__ = str(FROZEN)
    module.__dict__.update(np=np, njit=njit)
    sys.modules[name] = module
    exec(compile(selected, str(FROZEN), "exec", dont_inherit=True), module.__dict__)
    result = module._direction_vec_and_half_length
    assert isinstance(result, CPUDispatcher)
    return result


def _path(case):
    _, dtype_name, _, layout, values = case
    dtype = np.float32 if dtype_name == "f32" else np.float64
    if layout in ("C", "F"):
        path = np.array(values, dtype=dtype, order=layout)
        storage = path
    else:
        storage = np.full((len(values) * 2, 4), 1103.125, dtype=dtype)
        if layout == "readonly_positive":
            path = storage[::2, ::2]
        elif layout == "readonly_negative_rows":
            path = storage[-1::-2, ::2]
        else:
            path = storage[-1::-2, 3::-2]
        path[:] = values
        path.flags.writeable = False
    return path, storage


def _assigned_bits(array, count):
    # Slice before copying/viewing: unspecified open-tail data is never read.
    computed = array[:count].copy(order="C")
    bits = np.uint32 if computed.dtype.itemsize == 4 else np.uint64
    return computed.view(bits)


def _metadata(array):
    return (
        type(array),
        array.dtype,
        array.shape,
        array.ndim,
        array.strides,
        array.flags.c_contiguous,
        array.flags.f_contiguous,
        array.flags.writeable,
    )


@pytest.mark.parametrize("case", CASES, ids=[case[0] for case in CASES])
def test_original_compiled_directions_assigned_rows_and_metadata(
    original_compiled, case
):
    path, storage = _path(case)
    expected_path, expected_storage = _path(case)
    before = storage.copy()
    closed = case[2]
    count = len(path) if closed else len(path) - 1
    expected_normals, expected_half = original_compiled(expected_path, closed)
    normals, half, returned_path = compiled_current(path, closed)
    assert returned_path is path
    assert type(normals) is type(half) is np.ndarray
    assert _metadata(normals) == _metadata(expected_normals)
    assert _metadata(half) == _metadata(expected_half)
    assert normals.shape == (len(path), 2) and half.shape == (len(path),)
    np.testing.assert_array_equal(
        _assigned_bits(normals, count), _assigned_bits(expected_normals, count)
    )
    np.testing.assert_array_equal(
        _assigned_bits(half, count), _assigned_bits(expected_half, count)
    )
    assert (
        compiled_current.nopython_signatures and original_compiled.nopython_signatures
    )
    signature = (typeof(path), typeof(closed))
    expected_signature = (typeof(expected_path), typeof(closed))
    assert signature in compiled_current.signatures
    assert expected_signature in original_compiled.signatures
    actual_types = compiled_current.overloads[signature].signature.return_type.types[:2]
    expected_types = original_compiled.overloads[
        expected_signature
    ].signature.return_type.types
    assert actual_types == expected_types
    echoed = compiled_return_identity(normals, half)
    assert echoed[0] is normals and echoed[1] is half
    assert not np.shares_memory(normals, path)
    assert not np.shares_memory(half, path)
    assert not np.shares_memory(normals, half)
    np.testing.assert_array_equal(storage, before)
    np.testing.assert_array_equal(expected_storage, before)
