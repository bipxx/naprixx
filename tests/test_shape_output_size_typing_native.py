"""Independent original compiled output-size and genuine dot oracle."""

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
PINNED = (
    ROOT / ".upstream/napari/src/napari/layers/shapes/_accelerated_triangulate_numba.py"
)
ORIGINAL_SHA = "50dd996c7ebf92b84a6bb0944884eb6e4b2e8a7a9eea25571d0d466b1bfdec90"
SCOPE = ("_dot", "_calc_output_size")
DATA = {
    "open_line": ((1.0, 0.0), (1.0, 0.0), (1.0, 0.0), (123.125, -121.5)),
    "rectangle": ((0.0, 1.0), (1.0, 0.0), (0.0, -1.0), (-1.0, 0.0)),
    "one_turn": ((1.0, 0.0), (0.0, 1.0), (123.125, -121.5)),
    "rounding": ((0.6, 0.8), (0.6, 0.8), (123.125, -121.5)),
    "empty": (),
    "one": ((1.0, -0.0),),
}
# id, data, vector dtype, closed, limit dtype/value, bevel, layout, graph mode
CASES = (
    (
        "open_f32_miter_unused_tail_canary",
        "open_line",
        "f32",
        False,
        "f32",
        0.0,
        False,
        "C",
        "ordinary",
    ),
    (
        "closed_f64_counts_each_turn",
        "rectangle",
        "f64",
        True,
        "f64",
        0.25,
        False,
        "F",
        "ordinary",
    ),
    (
        "open_always_bevel_skips_all_nonfinite_rows",
        "one_turn",
        "f32",
        False,
        "f64",
        0.0,
        True,
        "C",
        "nonfinite_all",
    ),
    (
        "closed_always_bevel_readonly_negative",
        "rectangle",
        "f64",
        True,
        "f32",
        -0.5,
        True,
        "negative",
        "ordinary",
    ),
    (
        "open_dot_below_positive_subnormal_limit",
        "one_turn",
        "f32",
        False,
        "f32",
        1.401298464324817e-45,
        False,
        "C",
        "ordinary",
    ),
    (
        "open_dot_equals_signed_zero_limit",
        "one_turn",
        "f32",
        False,
        "f32",
        -0.0,
        False,
        "C",
        "ordinary",
    ),
    (
        "open_dot_above_negative_subnormal_limit",
        "one_turn",
        "f32",
        False,
        "f32",
        -1.401298464324817e-45,
        False,
        "C",
        "ordinary",
    ),
    (
        "mixed_f32_dot_f64_limit_rounding",
        "rounding",
        "f32",
        False,
        "f64",
        1.00000006,
        False,
        "C",
        "ordinary",
    ),
    (
        "readonly_positive_f32_mixed_limit",
        "open_line",
        "f32",
        False,
        "f64",
        -0.25,
        False,
        "positive",
        "ordinary",
    ),
    (
        "readonly_negative_f64_mixed_limit",
        "rectangle",
        "f64",
        True,
        "f32",
        0.125,
        False,
        "negative",
        "ordinary",
    ),
    (
        "empty_open_always_bevel_original_no_clamp",
        "empty",
        "f32",
        False,
        "f64",
        -0.5,
        True,
        "C",
        "ordinary",
    ),
    (
        "one_closed_wraps_same_vector",
        "one",
        "f64",
        True,
        "f64",
        1.0000000000000002,
        False,
        "F",
        "ordinary",
    ),
)


@njit(cache=True)
def compiled_current(vectors, closed, limit, bevel):
    result = actual._calc_output_size(vectors, closed, limit, bevel)
    return result, vectors


@pytest.fixture(scope="module")
def original_compiled():
    raw = FROZEN.read_bytes()
    assert hashlib.sha256(raw).hexdigest() == ORIGINAL_SHA
    assert raw == PINNED.read_bytes()
    tree = ast.parse(raw)
    pinned_tree = ast.parse(PINNED.read_bytes())
    selected = [
        n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name in SCOPE
    ]
    pinned_selected = [
        n
        for n in pinned_tree.body
        if isinstance(n, ast.FunctionDef) and n.name in SCOPE
    ]
    assert tuple(n.name for n in selected) == SCOPE
    assert [ast.dump(n) for n in selected] == [ast.dump(n) for n in pinned_selected]
    future = next(
        n
        for n in tree.body
        if isinstance(n, ast.ImportFrom) and n.module == "__future__"
    )
    name = "_naparixx_test_output_size_original_oracle"
    assert name not in sys.modules
    module = ModuleType(name)
    module.__file__ = str(FROZEN)
    module.__dict__.update(np=np, njit=njit)
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
    assert isinstance(module._dot, CPUDispatcher)
    assert isinstance(module._calc_output_size, CPUDispatcher)
    return module._calc_output_size


def _graph(case):
    _, data, vector_dtype, closed, limit_dtype, limit_value, bevel, layout, mode = case
    values = DATA[data]
    dtype = np.float32 if vector_dtype == "f32" else np.float64
    if layout == "C":
        root = np.full((len(values) + 2, 2), 1103.125, dtype=dtype)
        vectors = root[: len(values)]
    elif layout == "F":
        root = np.full((len(values), 4), 1103.125, dtype=dtype, order="F")
        vectors = root[:, :2]
    else:
        root = np.full((len(values) * 2, 4), 1103.125, dtype=dtype)
        vectors = root[::2, ::2] if layout == "positive" else root[-1::-2, 3::-2]
    if values:
        vectors[:] = values
    if mode == "nonfinite_all":
        vectors[:] = (np.nan, np.inf)
        limit_value = np.nan
    if layout in ("positive", "negative"):
        vectors.flags.writeable = False
    limit = (np.float32 if limit_dtype == "f32" else np.float64)(limit_value)
    return (vectors, closed, limit, bevel), root


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
def test_original_compiled_output_size_counts_precision_and_storage(
    original_compiled, case
):
    args, root = _graph(case)
    expected_args, expected_root = _graph(case)
    before = _bits(root)
    expected_before = _bits(expected_root)
    expected = original_compiled(*expected_args)
    result, returned_vectors = compiled_current(*args)
    assert type(result) is type(expected) is int
    assert result == expected
    assert returned_vectors is args[0]
    assert type(args[0]) is np.ndarray
    assert _metadata(args[0]) == _metadata(expected_args[0])
    np.testing.assert_array_equal(_bits(root), before)
    np.testing.assert_array_equal(_bits(expected_root), expected_before)
    np.testing.assert_array_equal(_bits(root), _bits(expected_root))
    signature = tuple(typeof(arg) for arg in args)
    expected_signature = tuple(typeof(arg) for arg in expected_args)
    assert (
        signature in compiled_current.signatures
        and expected_signature in original_compiled.signatures
    )
    actual_types = compiled_current.overloads[signature].signature.return_type.types
    original_type = original_compiled.overloads[
        expected_signature
    ].signature.return_type
    assert actual_types[0] == original_type
    assert actual_types[1] == signature[0]
    assert (
        compiled_current.nopython_signatures and original_compiled.nopython_signatures
    )
