"""Independent original compiled normalization and deduplication contracts."""

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
SCOPE = ("remove_path_duplicates", "normalize_vertices_and_edges")
DATA = {
    "open_chain": ((0.0, 0.0), (2.0, 0.0), (3.0, 1.0), (2.0, 2.0), (0.0, 2.0)),
    "explicit_endpoint": ((0.0, -0.0), (2.0, 0.0), (2.0, 2.0), (0.0, 2.0), (0.0, 0.0)),
    "revisited_vertex": (
        (0.0, 0.0),
        (2.0, 0.0),
        (2.0, 2.0),
        (2.0, 0.0),
        (3.0, 1.0),
        (4.0, 2.0),
    ),
    "hole_bridge": (
        (0.0, 0.0),
        (6.0, 0.0),
        (6.0, 6.0),
        (0.0, 6.0),
        (0.0, 0.0),
        (2.0, 2.0),
        (3.0, 4.0),
        (4.0, 2.0),
        (2.0, 2.0),
        (0.0, 0.0),
    ),
    "narrowing_keys": (
        (1.0, 0.0),
        (1.0000000596046448, 0.0),
        (2.0, 1.0),
        (2.0, 2.0),
        (3.0, 2.0),
    ),
    "signed_zero_keys": ((-0.0, 0.0), (2.0, 0.0), (2.0, 2.0), (0.0, -0.0), (0.0, 2.0)),
    "two_distinct": ((0.0, -0.0), (3.0, 4.0)),
    "two_equal": ((-0.0, 0.0), (0.0, -0.0)),
    "no_duplicates": ((0.0, 0.0), (1.0, 0.0), (2.0, 1.0), (3.0, 2.0)),
    "last_pair_only": ((0.0, 0.0), (1.0, 0.0), (2.0, 1.0), (2.0, 1.0)),
    "earlier_duplicate": ((-0.0, 0.0), (0.0, -0.0), (1.0, 0.0), (2.0, 1.0), (3.0, 2.0)),
    "earlier_duplicate_3d": (
        (0.0, 0.0, -0.0),
        (0.0, 0.0, 0.0),
        (1.0, 0.0, 2.0),
        (1.0, 1.0, 3.0),
        (2.0, 1.0, 4.0),
    ),
    "closure_endpoint": ((0.0, -0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 0.0)),
    "unwritten_tail": ((0.0, 0.0), (0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (1.0, 1.0)),
    "closed_3d": (
        (0.0, 0.0, -0.0),
        (0.0, 0.0, 0.0),
        (1.0, 0.0, 2.0),
        (1.0, 1.0, 3.0),
        (0.0, 0.0, 0.0),
    ),
}
# id, dataset, dtype, readonly physical layout, close
NORMALIZE_CASES = (
    ("open_f32_exact_edge_row_order", "open_chain", "f32", "C", False),
    ("explicit_close_f64_fortran", "open_chain", "f64", "F", True),
    (
        "endpoint_trim_implicit_close_negative_strides",
        "explicit_endpoint",
        "f32",
        "negative",
        False,
    ),
    (
        "first_seen_vertex_and_even_edge_toggle",
        "revisited_vertex",
        "f64",
        "positive",
        False,
    ),
    ("hole_bridge_cancelled_both_directions", "hole_bridge", "f32", "F", False),
    (
        "f64_distinct_keys_narrow_to_equal_f32_rows",
        "narrowing_keys",
        "f64",
        "positive",
        False,
    ),
    (
        "first_seen_signed_zero_key_negative_strides",
        "signed_zero_keys",
        "f32",
        "negative",
        False,
    ),
    ("closed_two_points_original_zero_edge_shape", "two_distinct", "f64", "F", True),
)
# id, dataset, dtype, readonly physical layout, closed, source-written prefix
# None means all output rows are source-written. The one partial case writes
# rows 0..2; source np.empty row 3 remains unspecified and is never read here.
DEDUP_CASES = (
    ("two_equal_points_return_exact_alias", "two_equal", "f32", "positive", True, None),
    ("no_count_f64_fortran_exact_alias", "no_duplicates", "f64", "F", False, None),
    (
        "final_adjacent_pair_excluded_from_count",
        "last_pair_only",
        "f32",
        "negative",
        False,
        None,
    ),
    (
        "earlier_duplicate_f32_fresh_output",
        "earlier_duplicate",
        "f32",
        "C",
        False,
        None,
    ),
    (
        "three_columns_f64_signed_zero_retained",
        "earlier_duplicate_3d",
        "f64",
        "positive",
        False,
        None,
    ),
    (
        "closed_endpoint_count_and_stop_before_last_row",
        "closure_endpoint",
        "f32",
        "negative",
        True,
        None,
    ),
    (
        "earlier_duplicate_last_pair_source_written_prefix",
        "unwritten_tail",
        "f64",
        "F",
        False,
        3,
    ),
    ("three_columns_closed_count_two_f32", "closed_3d", "f32", "F", True, None),
)


@njit(cache=True)
def compiled_normalize_current(vertices, close):
    new_vertices, edges = actual.normalize_vertices_and_edges(vertices, close)
    return new_vertices, edges, vertices


@njit(cache=True)
def compiled_dedup_current(path, closed):
    result = actual.remove_path_duplicates(path, closed)
    return result, path


@pytest.fixture(scope="module")
def original_family():
    raw = FROZEN.read_bytes()
    assert hashlib.sha256(raw).hexdigest() == ORIGINAL_SHA
    assert raw == PINNED.read_bytes()
    tree = ast.parse(raw)
    pinned = ast.parse(PINNED.read_bytes())
    selected = [
        n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name in SCOPE
    ]
    pinned_selected = [
        n for n in pinned.body if isinstance(n, ast.FunctionDef) and n.name in SCOPE
    ]
    assert tuple(n.name for n in selected) == SCOPE
    assert [ast.dump(n) for n in selected] == [ast.dump(n) for n in pinned_selected]
    future = next(
        n
        for n in tree.body
        if isinstance(n, ast.ImportFrom) and n.module == "__future__"
    )
    name = "_naparixx_test_normalize_dedup_full_original_oracle"
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
    for source_name in SCOPE:
        assert isinstance(getattr(module, source_name), CPUDispatcher)
        assert isinstance(getattr(actual, source_name), CPUDispatcher)
    return module


def _graph(dataset, dtype_name, layout):
    values = DATA[dataset]
    dtype = np.float32 if dtype_name == "f32" else np.float64
    rows, columns = len(values), len(values[0])
    if layout == "C":
        root = np.full((rows + 2, columns), 1703.125, dtype=dtype)
        array = root[1 : rows + 1]
    elif layout == "F":
        root = np.full((rows, columns + 2), 1703.125, dtype=dtype, order="F")
        array = root[:, 1 : columns + 1]
    else:
        root = np.full((2 * rows + 2, 2 * columns + 2), 1703.125, dtype=dtype)
        array = (
            root[1 : 1 + 2 * rows : 2, 1 : 1 + 2 * columns : 2]
            if layout == "positive"
            else root[2 * rows : 0 : -2, 2 * columns : 0 : -2]
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


@pytest.mark.parametrize("case", NORMALIZE_CASES, ids=[c[0] for c in NORMALIZE_CASES])
def test_original_compiled_normalize_exact_order_precision_and_storage(
    original_family, case
):
    _, dataset, dtype_name, layout, close = case
    vertices, root = _graph(dataset, dtype_name, layout)
    expected_vertices, expected_root = _graph(dataset, dtype_name, layout)
    before, expected_before = _bits(root), _bits(expected_root)
    if case[0] == "closed_two_points_original_zero_edge_shape":
        with pytest.raises(IndexError) as original_error:
            original_family.normalize_vertices_and_edges(expected_vertices, close)
        assert type(original_error.value) is IndexError
        assert str(original_error.value) == "getitem out of range"
        with pytest.raises(type(original_error.value)) as actual_error:
            compiled_normalize_current(vertices, close)
        assert type(actual_error.value) is type(original_error.value)
        assert str(actual_error.value) == str(original_error.value)
        assert _metadata(vertices) == _metadata(expected_vertices)
        _unchanged_roots(root, before, expected_root, expected_before)
        signature = (typeof(vertices), typeof(close))
        original_signature = (typeof(expected_vertices), typeof(close))
        assert signature in compiled_normalize_current.signatures
        assert (
            original_signature
            in original_family.normalize_vertices_and_edges.signatures
        )
        current_types = compiled_normalize_current.overloads[
            signature
        ].signature.return_type.types
        original_type = original_family.normalize_vertices_and_edges.overloads[
            original_signature
        ].signature.return_type
        assert current_types[:2] == original_type.types
        assert current_types[2] == signature[0]
        assert compiled_normalize_current.nopython_signatures
        assert original_family.normalize_vertices_and_edges.nopython_signatures
        return
    expected = original_family.normalize_vertices_and_edges(expected_vertices, close)
    observed = compiled_normalize_current(vertices, close)
    assert type(observed) is type(expected) is tuple
    assert len(observed) == 3 and len(expected) == 2
    assert observed[2] is vertices
    assert _metadata(vertices) == _metadata(expected_vertices)
    for index, expected_array in enumerate(expected):
        actual_array = observed[index]
        assert type(actual_array) is type(expected_array) is np.ndarray
        assert _metadata(actual_array) == _metadata(expected_array)
        np.testing.assert_array_equal(_bits(actual_array), _bits(expected_array))
        for input_array in (vertices, expected_vertices, root, expected_root):
            assert actual_array is not input_array and expected_array is not input_array
            assert not np.shares_memory(actual_array, input_array)
            assert not np.shares_memory(expected_array, input_array)
        for other in observed[:index]:
            assert not np.shares_memory(actual_array, other)
    _unchanged_roots(root, before, expected_root, expected_before)
    signature = (typeof(vertices), typeof(close))
    original_signature = (typeof(expected_vertices), typeof(close))
    assert signature in compiled_normalize_current.signatures
    assert original_signature in original_family.normalize_vertices_and_edges.signatures
    current_types = compiled_normalize_current.overloads[
        signature
    ].signature.return_type.types
    original_type = original_family.normalize_vertices_and_edges.overloads[
        original_signature
    ].signature.return_type
    assert current_types[:2] == original_type.types
    assert current_types[2] == signature[0]
    assert compiled_normalize_current.nopython_signatures
    assert original_family.normalize_vertices_and_edges.nopython_signatures


@pytest.mark.parametrize("case", DEDUP_CASES, ids=[c[0] for c in DEDUP_CASES])
def test_original_compiled_dedup_alias_defined_prefix_types_and_storage(
    original_family, case
):
    _, dataset, dtype_name, layout, closed, written_prefix = case
    path, root = _graph(dataset, dtype_name, layout)
    expected_path, expected_root = _graph(dataset, dtype_name, layout)
    before, expected_before = _bits(root), _bits(expected_root)
    expected = original_family.remove_path_duplicates(expected_path, closed)
    result, returned_path = compiled_dedup_current(path, closed)
    assert returned_path is path
    assert type(result) is type(expected) is np.ndarray
    assert _metadata(result) == _metadata(expected)
    assert _metadata(path) == _metadata(expected_path)
    if expected is expected_path:
        assert result is path
        np.testing.assert_array_equal(_bits(result), _bits(expected))
    else:
        assert result is not path
        for input_array in (path, expected_path, root, expected_root):
            assert not np.shares_memory(result, input_array)
            assert not np.shares_memory(expected, input_array)
        if written_prefix is None:
            np.testing.assert_array_equal(_bits(result), _bits(expected))
        else:
            assert 0 < written_prefix < len(expected)
            np.testing.assert_array_equal(
                _bits(result[:written_prefix]), _bits(expected[:written_prefix])
            )
    _unchanged_roots(root, before, expected_root, expected_before)
    signature = (typeof(path), typeof(closed))
    original_signature = (typeof(expected_path), typeof(closed))
    assert signature in compiled_dedup_current.signatures
    assert original_signature in original_family.remove_path_duplicates.signatures
    current_types = compiled_dedup_current.overloads[
        signature
    ].signature.return_type.types
    original_type = original_family.remove_path_duplicates.overloads[
        original_signature
    ].signature.return_type
    assert current_types[0] == original_type
    assert current_types[1] == signature[0]
    assert compiled_dedup_current.nopython_signatures
    assert original_family.remove_path_duplicates.nopython_signatures
