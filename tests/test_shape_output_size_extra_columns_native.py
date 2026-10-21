"""Original compiled output-size contracts with ignored extra columns."""

from __future__ import annotations

import hashlib
import importlib.util
import sys
from pathlib import Path

import numpy as np
import pytest
from numba import typeof

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / ".artifacts/test_shape_output_size_typing_contracts_next.py"
BASE_SHA = "8f1670e1b5d3bfb2d79044c51045d3a79d1f9841f4e18aef835b23b93a16514a"
assert hashlib.sha256(BASE.read_bytes()).hexdigest() == BASE_SHA
NAME = "_naparixx_test_output_size_extra_columns_frozen12"
if NAME in sys.modules:
    base = sys.modules[NAME]
else:
    spec = importlib.util.spec_from_file_location(NAME, BASE)
    assert spec is not None and spec.loader is not None
    base = importlib.util.module_from_spec(spec)
    sys.modules[NAME] = base
    spec.loader.exec_module(base)

# Reuse the unchanged genuine original closure and cached current consumer.
original_compiled = base.original_compiled

# id, columns, dtype, closed, limit dtype/value, bevel, layout, branch
CASES = (
    ("active_open_nx3_f32", 3, "f32", False, "f64", 0.125, False, "positive", "active"),
    ("active_closed_nx4_f64", 4, "f64", True, "f32", 0.25, False, "negative", "active"),
    ("always_bevel_open_nx3_f64", 3, "f64", False, "f32", 0.0, True, "C", "bevel"),
    ("always_bevel_closed_nx4_f32", 4, "f32", True, "f64", 0.0, True, "F", "bevel"),
    (
        "empty_loop_two_rows_nx3_f32",
        3,
        "f32",
        False,
        "f64",
        0.0,
        False,
        "negative",
        "empty_two",
    ),
    (
        "empty_loop_one_row_nx4_f64",
        4,
        "f64",
        False,
        "f32",
        0.0,
        False,
        "positive",
        "empty_one",
    ),
)


def _graph(case):
    _, columns, dtype_name, closed, limit_name, limit_value, bevel, layout, branch = (
        case
    )
    dtype = np.float32 if dtype_name == "f32" else np.float64
    points = ((0.0, 1.0), (1.0, 0.0), (0.0, -1.0), (-1.0, 0.0))
    if branch == "empty_two":
        points = points[:2]
    elif branch == "empty_one":
        points = points[:1]
    elif branch == "bevel" and not closed:
        points = points[:3]
    rows = len(points)
    if layout == "C":
        root = np.full((rows + 2, columns), 1703.125, dtype=dtype)
        vectors = root[1 : rows + 1]
    elif layout == "F":
        root = np.full((rows, columns + 2), 1703.125, dtype=dtype, order="F")
        vectors = root[:, 1 : columns + 1]
    else:
        root = np.full((2 * rows + 2, 2 * columns + 2), 1703.125, dtype=dtype)
        if layout == "positive":
            vectors = root[1 : 1 + 2 * rows : 2, 1 : 1 + 2 * columns : 2]
        else:
            vectors = root[2 * rows : 0 : -2, 2 * columns : 0 : -2]
    vectors[:, :2] = points
    vectors[:, 2] = np.nan
    if columns == 4:
        vectors[:, 3] = -np.inf
        vectors[0, 3] = -0.0
    if branch != "active":
        # Both source early branches consume no dot operand or threshold.
        vectors[:, :2] = (np.nan, np.inf)
        limit_value = np.nan
    elif not closed:
        # The open loop never consumes its initialized last direction.
        vectors[-1, :2] = (np.nan, np.inf)
    vectors.flags.writeable = False
    limit = (np.float32 if limit_name == "f32" else np.float64)(limit_value)
    return (vectors, closed, limit, bevel), root


@pytest.mark.parametrize("case", CASES, ids=[case[0] for case in CASES])
def test_original_compiled_output_size_ignores_extra_columns(original_compiled, case):
    args, root = _graph(case)
    expected_args, expected_root = _graph(case)
    before = base._bits(root)
    expected_before = base._bits(expected_root)
    assert args[0].shape[1] == expected_args[0].shape[1] == case[1]
    assert not args[0].flags.writeable
    expected = original_compiled(*expected_args)
    result, returned_vectors = base.compiled_current(*args)
    assert type(result) is type(expected) is int
    assert result == expected
    assert returned_vectors is args[0]
    assert type(args[0]) is np.ndarray
    assert base._metadata(args[0]) == base._metadata(expected_args[0])
    np.testing.assert_array_equal(base._bits(root), before)
    np.testing.assert_array_equal(base._bits(expected_root), expected_before)
    np.testing.assert_array_equal(base._bits(root), base._bits(expected_root))
    signature = tuple(typeof(arg) for arg in args)
    expected_signature = tuple(typeof(arg) for arg in expected_args)
    assert signature in base.compiled_current.signatures
    assert expected_signature in original_compiled.signatures
    actual_types = base.compiled_current.overloads[
        signature
    ].signature.return_type.types
    original_type = original_compiled.overloads[
        expected_signature
    ].signature.return_type
    assert actual_types[0] == original_type
    assert actual_types[1] == signature[0]
    assert base.compiled_current.nopython_signatures
    assert original_compiled.nopython_signatures
