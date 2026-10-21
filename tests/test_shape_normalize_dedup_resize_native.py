"""Additive original compiled normalization resize and exception contracts."""

from __future__ import annotations

import hashlib
import importlib.util
import sys
from pathlib import Path

import numpy as np
import pytest
from numba import typeof

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / ".artifacts/test_shape_normalize_dedup_zero_edge_contract_correction.py"
BASE_SHA = "2a676ae5e0ba124a8f6e36dfa692b8cf43f38cfe9059c1609f1d3d95eb6b742e"
assert hashlib.sha256(BASE.read_bytes()).hexdigest() == BASE_SHA
NAME = "_naparixx_test_normalize_resize_frozen16"
if NAME in sys.modules:
    base = sys.modules[NAME]
else:
    spec = importlib.util.spec_from_file_location(NAME, BASE)
    assert spec is not None and spec.loader is not None
    base = importlib.util.module_from_spec(spec)
    sys.modules[NAME] = base
    spec.loader.exec_module(base)

# The unchanged fixture executes both exact original bodies/global declarations.
original_family = base.original_family
PATHS = {
    "grow12": tuple(range(12)),
    "grow_then_final_shrink": tuple(range(8)),
    "two_growths_then_cancel_shrink": (*range(40), *range(38, 3, -1)),
    "collision_reinsert": (0, 1, 2, 3, 4, 5, 6, 5, 6, 5, 6),
    "signedzero_implicit_close": (*range(11), -1, 11, 0),
    "missing_selfedge": (0, 0),
}
# id, coordinate-index path, dtype, readonly physical layout, signed-zero keys
CASES = (
    ("growth16_to64_final11_edges", "grow12", "f32", "C", False),
    (
        "growth_then_final_self_removal_shrink",
        "grow_then_final_shrink",
        "f64",
        "F",
        False,
    ),
    (
        "two_growths_cancellation_two_shrinks",
        "two_growths_then_cancel_shrink",
        "f32",
        "negative",
        False,
    ),
    (
        "collision_delete_reinsert_repeated",
        "collision_reinsert",
        "f64",
        "positive",
        False,
    ),
    (
        "growth_signedzero_keys_implicit_endpoint_close",
        "signedzero_implicit_close",
        "f64",
        "negative",
        True,
    ),
    (
        "identical_pair_missing_selfedge_exact_KeyError",
        "missing_selfedge",
        "f32",
        "positive",
        False,
    ),
)


def _graph(case):
    _, path_name, dtype_name, layout, signedzero = case
    # Fixture coordinates only; they do not compute expected edges or order.
    values = tuple(
        (0.0, -0.0)
        if index == -1
        else (-0.0, 0.0)
        if signedzero and index == 0
        else (float(index), float((index * index) % 17))
        for index in PATHS[path_name]
    )
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


def _assert_signatures(original_family, vertices, expected_vertices, close):
    signature = (typeof(vertices), typeof(close))
    original_signature = (typeof(expected_vertices), typeof(close))
    assert signature in base.compiled_normalize_current.signatures
    assert original_signature in original_family.normalize_vertices_and_edges.signatures
    current_types = base.compiled_normalize_current.overloads[
        signature
    ].signature.return_type.types
    original_type = original_family.normalize_vertices_and_edges.overloads[
        original_signature
    ].signature.return_type
    assert current_types[:2] == original_type.types
    assert current_types[2] == signature[0]
    assert base.compiled_normalize_current.nopython_signatures
    assert original_family.normalize_vertices_and_edges.nopython_signatures


@pytest.mark.parametrize("case", CASES, ids=[case[0] for case in CASES])
def test_original_compiled_normalize_resize_history_exact_order(original_family, case):
    vertices, root = _graph(case)
    expected_vertices, expected_root = _graph(case)
    before = base._bits(root)
    expected_before = base._bits(expected_root)
    close = False
    if case[1] == "missing_selfedge":
        with pytest.raises(KeyError) as original_error:
            original_family.normalize_vertices_and_edges(expected_vertices, close)
        assert type(original_error.value) is KeyError
        assert original_error.value.args == ("set.remove(): key not in set",)
        with pytest.raises(type(original_error.value)) as actual_error:
            base.compiled_normalize_current(vertices, close)
        assert type(actual_error.value) is type(original_error.value)
        assert actual_error.value.args == original_error.value.args
        assert str(actual_error.value) == str(original_error.value)
    else:
        expected = original_family.normalize_vertices_and_edges(
            expected_vertices, close
        )
        observed = base.compiled_normalize_current(vertices, close)
        assert type(observed) is type(expected) is tuple
        assert len(observed) == 3 and len(expected) == 2
        assert observed[2] is vertices
        for index, expected_array in enumerate(expected):
            actual_array = observed[index]
            assert type(actual_array) is type(expected_array) is np.ndarray
            assert base._metadata(actual_array) == base._metadata(expected_array)
            np.testing.assert_array_equal(
                base._bits(actual_array), base._bits(expected_array)
            )
            for input_array in (vertices, expected_vertices, root, expected_root):
                assert actual_array is not input_array
                assert expected_array is not input_array
                assert not np.shares_memory(actual_array, input_array)
                assert not np.shares_memory(expected_array, input_array)
            for other in observed[:index]:
                assert not np.shares_memory(actual_array, other)
    assert base._metadata(vertices) == base._metadata(expected_vertices)
    base._unchanged_roots(root, before, expected_root, expected_before)
    _assert_signatures(original_family, vertices, expected_vertices, close)
