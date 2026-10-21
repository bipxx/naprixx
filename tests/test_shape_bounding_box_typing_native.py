"""Independent genuine original bounding-box compiled and py_func contracts."""

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
SCOPE = "create_box_from_bounding"
# id, first two rows/columns, dtype, layout, logical input shape
CASES = (
    (
        "f32_add_before_division_rounding",
        ((16777216.0, 1.0000001192092896), (1.0, 1.0)),
        "f32",
        "C",
        (2, 2),
    ),
    (
        "f32_midpoint_cancellation",
        ((16777216.0, -33554432.0), (-16777214.0, 33554428.0)),
        "f32",
        "C",
        (2, 2),
    ),
    (
        "f64_midpoint_float32_tie_boundary",
        ((1.0000000000000002, -1.0000001192092896), (1.0000001192092896, -1.0)),
        "f64",
        "C",
        (2, 2),
    ),
    (
        "f64_large_cancellation_and_small_values",
        ((10000000000000000.0, 1e-30), (-9999999999999998.0, 3e-30)),
        "f64",
        "C",
        (2, 2),
    ),
    ("f32_signed_zero_midpoints", ((-0.0, -0.0), (-0.0, 0.0)), "f32", "C", (2, 2)),
    (
        "readonly_f64_fortran_reversed_bounds",
        ((8.125, 5.25), (-3.5, -7.75)),
        "f64",
        "F",
        (2, 2),
    ),
    (
        "readonly_f32_positive_row_column_strides",
        ((0.6, -0.8), (0.8, 0.6)),
        "f32",
        "positive",
        (2, 2),
    ),
    (
        "readonly_f64_negative_row_column_strides",
        (
            (0.3333333333333333, -0.7777777777777778),
            (-0.45454545454545453, 0.14285714285714285),
        ),
        "f64",
        "negative",
        (2, 2),
    ),
    (
        "larger_f32_fortran_unused_nonfinite_tail",
        ((2.25, -8.5), (9.75, 3.125)),
        "f32",
        "F",
        (4, 5),
    ),
    (
        "safe_int32_compiled_generic_domain",
        ((1, 3), (8, -6)),
        "i32",
        "positive",
        (2, 2),
    ),
)


@njit(cache=True)
def compiled_current(bounding_box):
    result = actual.create_box_from_bounding(bounding_box)
    return result, bounding_box


@pytest.fixture(scope="module")
def original_compiled():
    raw = FROZEN.read_bytes()
    assert hashlib.sha256(raw).hexdigest() == ORIGINAL_SHA
    assert raw == PINNED.read_bytes()
    tree = ast.parse(raw)
    pinned_tree = ast.parse(PINNED.read_bytes())
    selected = next(
        node
        for node in tree.body
        if isinstance(node, ast.FunctionDef) and node.name == SCOPE
    )
    pinned_selected = next(
        node
        for node in pinned_tree.body
        if isinstance(node, ast.FunctionDef) and node.name == SCOPE
    )
    assert ast.dump(selected) == ast.dump(pinned_selected)
    future = next(
        node
        for node in tree.body
        if isinstance(node, ast.ImportFrom) and node.module == "__future__"
    )
    name = "_naparixx_test_bounding_box_original_oracle"
    assert name not in sys.modules
    module = ModuleType(name)
    module.__file__ = str(FROZEN)
    module.__dict__.update(np=np, njit=njit)
    sys.modules[name] = module
    exec(
        compile(
            ast.Module(body=[future, selected], type_ignores=[]),
            str(FROZEN),
            "exec",
            dont_inherit=True,
        ),
        module.__dict__,
    )
    assert isinstance(module.create_box_from_bounding, CPUDispatcher)
    assert isinstance(actual.create_box_from_bounding, CPUDispatcher)
    return module.create_box_from_bounding


def _graph(case):
    _, first_values, dtype_name, layout, shape = case
    dtype = {"f32": np.float32, "f64": np.float64, "i32": np.int32}[dtype_name]
    rows, columns = shape
    if layout == "C":
        root = np.full((rows + 2, columns), 1703, dtype=dtype)
        array = root[1 : rows + 1]
    elif layout == "F":
        root = np.full((rows, columns + 2), 1703, dtype=dtype, order="F")
        array = root[:, 1 : columns + 1]
    else:
        root = np.full((2 * rows + 2, 2 * columns + 2), 1703, dtype=dtype)
        if layout == "positive":
            array = root[1 : 1 + 2 * rows : 2, 1 : 1 + 2 * columns : 2]
        else:
            array = root[2 * rows : 0 : -2, 2 * columns : 0 : -2]
    if shape != (2, 2):
        array[:] = np.nan
        array[2:, 0::2] = np.inf
        array[:, 2::2] = -np.inf
        array[-1, -1] = -0.0
    array[:2, :2] = first_values
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


def _assert_output(actual_result, original_result, *inputs):
    assert type(actual_result) is type(original_result) is np.ndarray
    assert _metadata(actual_result) == _metadata(original_result)
    assert actual_result.shape == (9, 2)
    assert actual_result.dtype == np.dtype("float32")
    assert actual_result.flags.c_contiguous and actual_result.flags.writeable
    np.testing.assert_array_equal(_bits(actual_result), _bits(original_result))
    for array in inputs:
        assert not np.shares_memory(actual_result, array)
        assert not np.shares_memory(original_result, array)


@pytest.mark.parametrize("case", CASES, ids=[case[0] for case in CASES])
def test_original_compiled_bounding_box_precision_layout_and_storage(
    original_compiled, case
):
    bounding_box, root = _graph(case)
    original_input, original_root = _graph(case)
    before = _bits(root)
    original_before = _bits(original_root)
    original_result = original_compiled(original_input)
    result, returned_input = compiled_current(bounding_box)
    assert returned_input is bounding_box
    assert _metadata(bounding_box) == _metadata(original_input)
    _assert_output(
        result, original_result, bounding_box, original_input, root, original_root
    )
    np.testing.assert_array_equal(_bits(root), before)
    np.testing.assert_array_equal(_bits(original_root), original_before)
    np.testing.assert_array_equal(_bits(root), _bits(original_root))
    second_original = original_compiled(original_input)
    second_result, second_input = compiled_current(bounding_box)
    assert second_input is bounding_box
    assert second_result is not result and second_original is not original_result
    _assert_output(second_result, second_original, result, original_result)
    np.testing.assert_array_equal(_bits(root), before)
    np.testing.assert_array_equal(_bits(original_root), original_before)
    signature = (typeof(bounding_box),)
    original_signature = (typeof(original_input),)
    assert signature in compiled_current.signatures
    assert original_signature in original_compiled.signatures
    actual_types = compiled_current.overloads[signature].signature.return_type.types
    original_type = original_compiled.overloads[
        original_signature
    ].signature.return_type
    assert actual_types[0] == original_type
    assert actual_types[1] == signature[0]
    assert (
        compiled_current.nopython_signatures and original_compiled.nopython_signatures
    )


class ScalarIndexProvider:
    def __init__(self):
        self.data = np.array(((8.125, -0.0), (-3.5, 0.0)), dtype=np.float64)
        self.calls = []

    def __getitem__(self, key):
        self.calls.append(key)
        return self.data[key]


@pytest.mark.parametrize("kind", ("scalar_index_provider", "short_ndarray"))
def test_original_bounding_box_py_func_provider_and_short_error(
    original_compiled, kind
):
    if kind == "scalar_index_provider":
        bounding_box = ScalarIndexProvider()
        original_input = ScalarIndexProvider()
        before = _bits(bounding_box.data)
        original_before = _bits(original_input.data)
        original_result = original_compiled.py_func(original_input)
        result = actual.create_box_from_bounding.py_func(bounding_box)
        _assert_output(result, original_result, bounding_box.data, original_input.data)
        assert bounding_box.calls == original_input.calls
        assert len(bounding_box.calls) == 4
        np.testing.assert_array_equal(_bits(bounding_box.data), before)
        np.testing.assert_array_equal(_bits(original_input.data), original_before)
    else:
        bounding_box = np.array(((1.25, -0.0),), dtype=np.float32)
        original_input = bounding_box.copy()
        before = _bits(bounding_box)
        original_before = _bits(original_input)
        with pytest.raises(IndexError) as expected_error:
            original_compiled.py_func(original_input)
        with pytest.raises(type(expected_error.value)) as observed_error:
            actual.create_box_from_bounding.py_func(bounding_box)
        assert observed_error.value.args == expected_error.value.args
        np.testing.assert_array_equal(_bits(bounding_box), before)
        np.testing.assert_array_equal(_bits(original_input), original_before)
