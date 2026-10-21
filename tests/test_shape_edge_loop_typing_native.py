"""Independent original compiled edge-loop oracle over initialized real graphs."""

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
SCOPE = (
    "_dot",
    "_cross_z",
    "_sign_abs",
    "_orthogonal_vector",
    "_set_centers_and_offsets",
    "_generate_2D_edge_meshes_loop",
)
DATA = {
    "two": (
        ((0.0, -0.0), (3.0, 0.0)),
        ((1.0, 0.0), (123.125, -121.5)),
        (1.5, 1103.125),
    ),
    "two_rounding": (
        ((16777216.125, -0.0), (16777218.5, 3.125)),
        ((0.6, 0.8), (123.125, -121.5)),
        (1.75, 1103.125),
    ),
    "line_open": (
        ((0.0, 0.0), (3.0, 0.0), (8.0, 0.0), (13.0, 0.0)),
        ((1.0, 0.0), (1.0, 0.0), (1.0, 0.0), (123.125, -121.5)),
        (1.5, 2.5, 2.5, 1103.125),
    ),
    "turns_open": (
        ((0.0, 0.0), (3.0, 0.0), (3.0, 4.0), (8.0, 4.0)),
        ((1.0, 0.0), (0.0, 1.0), (1.0, 0.0), (123.125, -121.5)),
        (1.5, 2.0, 2.5, 1103.125),
    ),
    "rectangle": (
        ((0.0, 0.0), (0.0, 4.0), (4.0, 4.0), (4.0, 0.0)),
        ((0.0, 1.0), (1.0, 0.0), (0.0, -1.0), (-1.0, 0.0)),
        (2.0, 2.0, 2.0, 2.0),
    ),
    "bevel_open": (
        ((0.0, 0.0), (0.25, 0.0), (0.25, 0.75)),
        ((1.0, 0.0), (0.0, 1.0), (123.125, -121.5)),
        (0.125, 0.375, 1103.125),
    ),
    "line_closed": (
        ((0.0, 0.0), (2.0, 0.0), (5.0, 0.0)),
        ((1.0, 0.0), (1.0, 0.0), (-1.0, 0.0)),
        (1.0, 1.5, 2.5),
    ),
    "unequal_closed": (
        (
            (0.125, -0.25),
            (3.125, -0.25),
            (3.125, 3.75),
            (9.875, 2.125),
            (21.75, -4.625),
        ),
        ((1.0, 0.0), (0.0, 1.0), (-0.6, -0.8)),
        (0.5, 0.75, 1.25, 0.125),
    ),
}
# id, data, six array dtypes, closed, cos dtype/value, bevel, layouts, graph, capacities
CASES = (
    (
        "open_two_f32_exact_head_tail",
        "two",
        "f32",
        "f32",
        "f32",
        "f32",
        "f32",
        "i32",
        False,
        "f32",
        -1.0,
        False,
        "C",
        "C",
        "disjoint",
        4,
        2,
    ),
    (
        "open_two_mixed_stored_endpoint_rounding",
        "two_rounding",
        "f64",
        "f64",
        "f64",
        "f64",
        "f32",
        "i64",
        False,
        "f64",
        -1.0,
        False,
        "C",
        "C",
        "disjoint",
        4,
        2,
    ),
    (
        "open_four_collinear_exact_miter",
        "line_open",
        "f32",
        "f32",
        "f32",
        "f32",
        "f32",
        "i32",
        False,
        "f32",
        -1.0,
        False,
        "C",
        "C",
        "disjoint",
        8,
        6,
    ),
    (
        "open_four_f64_turns_fortran",
        "turns_open",
        "f64",
        "f64",
        "f64",
        "f64",
        "f64",
        "i64",
        False,
        "f64",
        -0.75,
        False,
        "F",
        "F",
        "disjoint",
        8,
        6,
    ),
    (
        "closed_four_always_bevel_exact_capacity",
        "rectangle",
        "f32",
        "f32",
        "f32",
        "f32",
        "f32",
        "i32",
        True,
        "f32",
        -0.75,
        True,
        "C",
        "C",
        "disjoint",
        14,
        12,
    ),
    (
        "open_three_mixed_limit_bevel_clamp",
        "bevel_open",
        "f64",
        "f32",
        "f64",
        "f32",
        "f64",
        "i64",
        False,
        "f32",
        0.5,
        False,
        "C",
        "C",
        "disjoint",
        7,
        5,
    ),
    (
        "readonly_positive_inputs_positive_outputs",
        "turns_open",
        "f64",
        "f32",
        "f64",
        "f64",
        "f32",
        "i32",
        False,
        "f64",
        -0.75,
        False,
        "positive",
        "positive",
        "disjoint",
        8,
        6,
    ),
    (
        "readonly_negative_inputs_negative_outputs",
        "line_closed",
        "f64",
        "f64",
        "f64",
        "f64",
        "f64",
        "i64",
        True,
        "f64",
        -0.5,
        False,
        "negative",
        "negative",
        "disjoint",
        10,
        8,
    ),
    (
        "closed_independent_negative_wrap_lengths",
        "unequal_closed",
        "f64",
        "f32",
        "f64",
        "f64",
        "f32",
        "i64",
        True,
        "f64",
        -0.75,
        True,
        "C",
        "C",
        "disjoint",
        11,
        9,
    ),
    (
        "closed_path_centers_physical_alias",
        "line_closed",
        "f32",
        "f32",
        "f32",
        "f32",
        "f32",
        "i32",
        True,
        "f32",
        -1.0,
        False,
        "C",
        "C",
        "path_centers",
        8,
        6,
    ),
    (
        "open_centers_offsets_same_array",
        "turns_open",
        "f32",
        "f32",
        "f32",
        "f32",
        "f32",
        "i64",
        False,
        "f32",
        -0.75,
        False,
        "C",
        "C",
        "same_outputs",
        8,
        6,
    ),
    (
        "closed_internally_overlapping_output_rows",
        "line_closed",
        "f64",
        "f64",
        "f64",
        "f64",
        "f64",
        "i32",
        True,
        "f64",
        -1.0,
        False,
        "C",
        "C",
        "overlap_rows",
        8,
        6,
    ),
)


@njit(cache=True)
def compiled_current(
    path, closed, cos_limit, bevel, directions, limits, centers, offsets, triangles
):
    result = actual._generate_2D_edge_meshes_loop(
        path, closed, cos_limit, bevel, directions, limits, centers, offsets, triangles
    )
    return result, path, directions, limits, centers, offsets, triangles


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
    name = "_naparixx_test_edge_loop_original_oracle"
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
    for function_name in SCOPE:
        assert isinstance(getattr(module, function_name), CPUDispatcher)
    return module._generate_2D_edge_meshes_loop


def _dtype(name):
    return {"f32": np.float32, "f64": np.float64, "i32": np.int32, "i64": np.int64}[
        name
    ]


def _read_input(values, dtype, layout):
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


def _write_output(rows, columns, dtype, layout):
    sentinel = 11357 if np.issubdtype(dtype, np.integer) else 1103.125
    if layout == "C":
        root = np.full((rows + 2, columns), sentinel, dtype=dtype)
        array = root[:rows]
        mask = np.zeros(root.shape, dtype=np.bool_)
        mask[:rows] = True
    elif layout == "F":
        root = np.full((rows, columns + 2), sentinel, dtype=dtype, order="F")
        array = root[:, :columns]
        mask = np.zeros(root.shape, dtype=np.bool_)
        mask[:, :columns] = True
    else:
        root = np.full((rows * 2, columns * 2), sentinel, dtype=dtype)
        mask = np.zeros(root.shape, dtype=np.bool_)
        if layout == "positive":
            array = root[::2, ::2]
            mask[::2, ::2] = True
        else:
            array = root[-1::-2, -1::-2]
            mask[-1::-2, -1::-2] = True
    return array, root, mask


def _graph(case):
    (
        _,
        data,
        path_dtype,
        vector_dtype,
        length_dtype,
        center_dtype,
        offset_dtype,
        triangle_dtype,
        closed,
        cos_dtype,
        cos_value,
        bevel,
        input_layout,
        output_layout,
        mode,
        center_rows,
        triangle_rows,
    ) = case
    path_values, vector_values, length_values = DATA[data]
    arrays = {}
    roots = {}
    masks = {}
    for name, values, dtype_name in (
        ("path", path_values, path_dtype),
        ("directions", vector_values, vector_dtype),
        ("limits", length_values, length_dtype),
    ):
        arrays[name], roots[name] = _read_input(
            values, _dtype(dtype_name), input_layout
        )
        masks[name] = np.zeros(roots[name].shape, dtype=np.bool_)
    for name, rows, columns, dtype_name in (
        ("centers", center_rows, 2, center_dtype),
        ("offsets", center_rows, 2, offset_dtype),
        ("triangles", triangle_rows, 3, triangle_dtype),
    ):
        arrays[name], roots[name], masks[name] = _write_output(
            rows, columns, _dtype(dtype_name), output_layout
        )
    if mode == "path_centers":
        arrays["path"] = arrays["centers"][: len(path_values)]
        arrays["path"][:] = path_values
        arrays["path"].flags.writeable = False
        del roots["path"]
        del masks["path"]
    elif mode == "same_outputs":
        arrays["offsets"] = arrays["centers"]
        del roots["offsets"]
        del masks["offsets"]
    elif mode == "overlap_rows":
        dtype = _dtype(center_dtype)
        roots["centers"] = np.full(6, 1103.125, dtype=dtype)
        arrays["centers"] = np.ndarray(
            (center_rows, 2),
            dtype=dtype,
            buffer=roots["centers"],
            strides=(0, dtype().itemsize),
        )
        masks["centers"] = np.zeros(6, dtype=np.bool_)
        masks["centers"][:2] = True
        dtype = _dtype(offset_dtype)
        roots["offsets"] = np.full(center_rows + 3, 1103.125, dtype=dtype)
        arrays["offsets"] = np.ndarray(
            (center_rows, 2),
            dtype=dtype,
            buffer=roots["offsets"],
            strides=(dtype().itemsize, dtype().itemsize),
        )
        masks["offsets"] = np.zeros(center_rows + 3, dtype=np.bool_)
        masks["offsets"][: center_rows + 1] = True
    args = (
        arrays["path"],
        closed,
        _dtype(cos_dtype)(cos_value),
        bevel,
        arrays["directions"],
        arrays["limits"],
        arrays["centers"],
        arrays["offsets"],
        arrays["triangles"],
    )
    return args, arrays, roots, masks


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
def test_original_compiled_edge_loop_real_storage_precision_and_types(
    original_compiled, case
):
    args, arrays, roots, masks = _graph(case)
    expected_args, expected_arrays, expected_roots, expected_masks = _graph(case)
    before = {name: _bits(root) for name, root in roots.items()}
    expected_before = {name: _bits(root) for name, root in expected_roots.items()}
    expected_result = original_compiled(*expected_args)
    result = compiled_current(*args)
    assert result[0] is expected_result is None
    for returned, array in zip(result[1:], arrays.values(), strict=True):
        assert returned is array
        assert type(array) is np.ndarray
    assert roots.keys() == expected_roots.keys()
    for name, root in roots.items():
        np.testing.assert_array_equal(_bits(root), _bits(expected_roots[name]))
        np.testing.assert_array_equal(
            _bits(root)[~masks[name]], before[name][~masks[name]]
        )
        np.testing.assert_array_equal(
            _bits(expected_roots[name])[~expected_masks[name]],
            expected_before[name][~expected_masks[name]],
        )
    np.testing.assert_array_equal(arrays["triangles"], expected_arrays["triangles"])
    names = tuple(arrays)
    for name in names:
        assert _metadata(arrays[name]) == _metadata(expected_arrays[name])
    for left_index, left in enumerate(names):
        for right in names[left_index + 1 :]:
            assert np.shares_memory(arrays[left], arrays[right]) == np.shares_memory(
                expected_arrays[left], expected_arrays[right]
            )
            assert (arrays[left] is arrays[right]) == (
                expected_arrays[left] is expected_arrays[right]
            )
    signature = tuple(typeof(arg) for arg in args)
    expected_signature = tuple(typeof(arg) for arg in expected_args)
    assert signature in compiled_current.signatures
    assert expected_signature in original_compiled.signatures
    actual_return_types = compiled_current.overloads[
        signature
    ].signature.return_type.types
    original_return_type = original_compiled.overloads[
        expected_signature
    ].signature.return_type
    assert actual_return_types[0] == original_return_type
    assert actual_return_types[1:] == tuple(typeof(array) for array in arrays.values())
    assert (
        compiled_current.nopython_signatures and original_compiled.nopython_signatures
    )
