"""Original compiled triangle normalization oracle with real storage graphs."""

from __future__ import annotations

import ast
import hashlib
import sys
from enum import Enum
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
SCOPE = ("Orientation", "_normalize_triangle_orientation", "_orientation")
BASE = ((0.0, 0.0), (4.0, 0.0), (0.0, 3.0), (2.0, 0.0), (1.0, 2.0), (3.0, 2.0))
ROUND_CENTERS = (
    (16777216.125, 0.125),
    (16777218.375, 4.25),
    (16777220.625, -0.125),
    (16777218.875, 0.25),
    (16777216.375, 6.125),
    (16777220.125, 6.375),
)
ROUND_OFFSETS = (
    (0.375, 0.125),
    (-0.625, 0.375),
    (0.125, -0.375),
    (-0.125, 0.1),
    (0.625, 0.5),
    (-0.375, -0.5),
)
TRIANGLES = ((0, 2, 1), (0, 1, 2), (0, 3, 1))
CASES = (
    ("f32_i32_c_three_orientations", "f32", "f32", "i32", "C", "C", "basic"),
    ("f64_i64_f_three_orientations", "f64", "f64", "i64", "F", "F", "basic"),
    ("mixed_f32_centers_f64_offsets", "f32", "f64", "i32", "C", "C", "rounding"),
    ("mixed_f64_centers_f32_offsets", "f64", "f32", "i64", "C", "C", "rounding"),
    (
        "readonly_positive_points_positive_triangles",
        "f32",
        "f32",
        "i32",
        "positive",
        "positive",
        "basic",
    ),
    (
        "readonly_negative_points_negative_triangles",
        "f64",
        "f64",
        "i64",
        "negative",
        "negative",
        "basic",
    ),
    ("negative_indices_f32_i32", "f32", "f32", "i32", "C", "C", "negative_indices"),
    (
        "negative_indices_unequal_point_lengths_mixed",
        "f64",
        "f32",
        "i64",
        "C",
        "C",
        "negative_unequal_lengths",
    ),
    (
        "overlapping_triangle_rows_i64",
        "f32",
        "f32",
        "i64",
        "C",
        "overlap_rows",
        "basic",
    ),
    (
        "triangle_center_physical_alias_i64_f64",
        "f64",
        "f64",
        "i64",
        "C",
        "point_alias",
        "basic",
    ),
    (
        "centers_offsets_same_readonly_array",
        "f32",
        "f32",
        "i32",
        "C",
        "C",
        "same_points",
    ),
    ("triangle_rows_same_storage_i32", "f64", "f64", "i32", "C", "same_rows", "basic"),
)


@njit(cache=True)
def compiled_current(triangles, centers, offsets):
    result = actual._normalize_triangle_orientation(triangles, centers, offsets)
    return result, triangles, centers, offsets


@pytest.fixture(scope="module")
def original_compiled():
    raw = FROZEN.read_bytes()
    assert hashlib.sha256(raw).hexdigest() == ORIGINAL_SHA
    assert raw == PINNED.read_bytes()
    tree = ast.parse(raw)
    pinned_tree = ast.parse(PINNED.read_bytes())
    selected = [
        node
        for node in tree.body
        if isinstance(node, (ast.ClassDef, ast.FunctionDef)) and node.name in SCOPE
    ]
    pinned_selected = [
        node
        for node in pinned_tree.body
        if isinstance(node, (ast.ClassDef, ast.FunctionDef)) and node.name in SCOPE
    ]
    assert tuple(node.name for node in selected) == SCOPE
    assert [ast.dump(node) for node in selected] == [
        ast.dump(node) for node in pinned_selected
    ]
    future = next(
        node
        for node in tree.body
        if isinstance(node, ast.ImportFrom) and node.module == "__future__"
    )
    name = "_naparixx_test_normalize_triangles_original_oracle"
    assert name not in sys.modules
    module = ModuleType(name)
    module.__file__ = str(FROZEN)
    module.__dict__.update(np=np, njit=njit, Enum=Enum)
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
    assert isinstance(module._normalize_triangle_orientation, CPUDispatcher)
    assert isinstance(module._orientation, CPUDispatcher)
    assert issubclass(module.Orientation, Enum)
    return module


def _dtype(name):
    return {"f32": np.float32, "f64": np.float64, "i32": np.int32, "i64": np.int64}[
        name
    ]


def _points(values, dtype, layout):
    if layout in ("C", "F"):
        root = np.array(values, dtype=dtype, order=layout)
        return root, root
    root = np.full((len(values) * 2, 4), 1103.125, dtype=dtype)
    if layout == "positive":
        points = root[::2, ::2]
    else:
        points = root[-1::-2, 3::-2]
    points[:] = values
    points.flags.writeable = False
    return points, root


def _graph(case):
    (
        _,
        center_dtype,
        offset_dtype,
        triangle_dtype,
        point_layout,
        triangle_layout,
        mode,
    ) = case
    values = ROUND_CENTERS if mode == "rounding" else BASE
    offset_values = ROUND_OFFSETS if mode == "rounding" else ((0.0, 0.0),) * len(values)
    if mode == "negative_unequal_lengths":
        offset_values += ((0.0, 0.0),)
    centers, center_root = _points(values, _dtype(center_dtype), point_layout)
    offsets, offset_root = _points(offset_values, _dtype(offset_dtype), point_layout)
    roots = {"centers": center_root, "offsets": offset_root}
    masks = {name: np.zeros(root.shape, dtype=np.bool_) for name, root in roots.items()}
    dtype = _dtype(triangle_dtype)
    initial = TRIANGLES
    if mode in ("negative_indices", "negative_unequal_lengths"):
        initial = tuple(tuple(index - len(centers) for index in row) for row in initial)
    if triangle_layout == "point_alias":
        center_root = np.full((8, 2), 1103.125, dtype=np.float64)
        centers = center_root[:6]
        centers[:] = 0.0
        triangles = np.ndarray((1, 3), dtype=dtype, buffer=center_root, strides=(24, 8))
        triangles[:] = ((0, 2, 1),)
        centers.flags.writeable = False
        offsets = np.array(BASE, dtype=np.float64)
        roots = {"centers": center_root, "offsets": offsets}
        masks = {
            name: np.zeros(root.shape, dtype=np.bool_) for name, root in roots.items()
        }
        masks["centers"].flat[:3] = True
    else:
        if triangle_layout == "C":
            triangle_root = np.full((5, 3), 11357, dtype=dtype)
            triangles = triangle_root[:3]
            mask = np.zeros(triangle_root.shape, dtype=np.bool_)
            mask[:3] = True
            triangles[:] = initial
        elif triangle_layout == "F":
            triangle_root = np.full((3, 5), 11357, dtype=dtype, order="F")
            triangles = triangle_root[:, :3]
            mask = np.zeros(triangle_root.shape, dtype=np.bool_)
            mask[:, :3] = True
            triangles[:] = initial
        elif triangle_layout in ("positive", "negative"):
            triangle_root = np.full((6, 6), 11357, dtype=dtype)
            mask = np.zeros(triangle_root.shape, dtype=np.bool_)
            if triangle_layout == "positive":
                triangles = triangle_root[::2, ::2]
                mask[::2, ::2] = True
            else:
                triangles = triangle_root[-1::-2, 5::-2]
                mask[-1::-2, 5::-2] = True
            triangles[:] = initial
        elif triangle_layout == "overlap_rows":
            triangle_root = np.full(8, 11357, dtype=dtype)
            triangle_root[:4] = (0, 2, 1, 0)
            triangles = np.ndarray(
                (2, 3),
                dtype=dtype,
                buffer=triangle_root,
                strides=(dtype().itemsize, dtype().itemsize),
            )
            mask = np.zeros(triangle_root.shape, dtype=np.bool_)
            mask[:4] = True
        else:
            assert triangle_layout == "same_rows"
            triangle_root = np.full(6, 11357, dtype=dtype)
            triangle_root[:3] = (0, 2, 1)
            triangles = np.ndarray(
                (3, 3), dtype=dtype, buffer=triangle_root, strides=(0, dtype().itemsize)
            )
            mask = np.zeros(triangle_root.shape, dtype=np.bool_)
            mask[:3] = True
        roots["triangles"] = triangle_root
        masks["triangles"] = mask
    if mode == "same_points":
        centers.flags.writeable = False
        offsets = centers
        del roots["offsets"]
        del masks["offsets"]
    return triangles, centers, offsets, roots, masks


def _bits(array):
    bits = np.uint32 if array.dtype.itemsize == 4 else np.uint64
    return array.copy(order="C").view(bits)


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
def test_original_compiled_normalize_triangle_storage_and_types(
    original_compiled, case
):
    triangles, centers, offsets, roots, masks = _graph(case)
    (
        expected_triangles,
        expected_centers,
        expected_offsets,
        expected_roots,
        expected_masks,
    ) = _graph(case)
    before = {name: _bits(root) for name, root in roots.items()}
    expected_before = {name: _bits(root) for name, root in expected_roots.items()}
    original = original_compiled._normalize_triangle_orientation
    expected_result = original(expected_triangles, expected_centers, expected_offsets)
    result, returned_triangles, returned_centers, returned_offsets = compiled_current(
        triangles, centers, offsets
    )
    assert result is expected_result is None
    assert returned_triangles is triangles
    assert returned_centers is centers
    assert returned_offsets is offsets
    assert type(triangles) is type(centers) is type(offsets) is np.ndarray
    np.testing.assert_array_equal(triangles, expected_triangles)
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
    arrays = (triangles, centers, offsets)
    expected_arrays = (expected_triangles, expected_centers, expected_offsets)
    for array, expected_array in zip(arrays, expected_arrays, strict=True):
        assert _metadata(array) == _metadata(expected_array)
    for left in range(3):
        for right in range(left + 1, 3):
            assert np.shares_memory(arrays[left], arrays[right]) == np.shares_memory(
                expected_arrays[left], expected_arrays[right]
            )
            assert (arrays[left] is arrays[right]) == (
                expected_arrays[left] is expected_arrays[right]
            )
    signature = tuple(typeof(array) for array in arrays)
    expected_signature = tuple(typeof(array) for array in expected_arrays)
    assert signature in compiled_current.signatures
    assert expected_signature in original.signatures
    actual_return_types = compiled_current.overloads[
        signature
    ].signature.return_type.types
    original_return_type = original.overloads[expected_signature].signature.return_type
    assert actual_return_types[0] == original_return_type
    assert actual_return_types[1:] == signature
    assert compiled_current.nopython_signatures and original.nopython_signatures
    assert issubclass(actual.Orientation, Enum)
    assert [(member.name, member.value) for member in actual.Orientation] == [
        (member.name, member.value) for member in original_compiled.Orientation
    ]
    for member in actual.Orientation:
        assert type(member) is actual.Orientation
        assert member is actual.Orientation[member.name]
