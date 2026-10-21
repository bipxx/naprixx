"""Independent compiled orientation rounding, Enum and indexable contracts."""

from __future__ import annotations

import ast
import hashlib
from enum import Enum
from pathlib import Path

import numpy as np
import pytest
from numba import njit, typeof
from numba.core import config, types
from numba.core.registry import CPUDispatcher
from numba.typed import List

from napari.layers.shapes import _accelerated_triangulate_numba as accelerated

ROOT = Path(__file__).resolve().parents[1]
ORIGINAL_SHA256 = "50dd996c7ebf92b84a6bb0944884eb6e4b2e8a7a9eea25571d0d466b1bfdec90"
ORIGINAL_PATHS = (
    ".artifacts/shape-helper-closure-before/current/napari/layers/shapes/"
    "_accelerated_triangulate_numba.py",
    ".artifacts/shape-helper-closure-before/pinned/napari/layers/shapes/"
    "_accelerated_triangulate_numba.py",
)

class Orientation(Enum):
    """Orientation of a triangle.

    The terms assume napari's preferred coordinate frame, in which
    the 0th axis, y, is pointing down, and the 1st axis, x, is pointing
    right. If one of the axes is flipped, the observed orientation
    would also flip.
    """
    clockwise = -1
    collinear = 0
    anticlockwise = 1


@njit(cache=True, inline='always')
def _orientation(p1: np.ndarray, p2: np.ndarray, p3: np.ndarray) -> Orientation:
    """Compute the orientation of three points.

    In terms of napari's preferred coordinate frame (axis 0, y, is pointing
    down, axis 1, x is pointing right), this orientation is positive for
    anti-clockwise and negative for clockwise.

    Parameters
    ----------
    p1, p2, p3: np.ndarray, shape (2,)
        The three points to check.

    Returns
    -------
    float
        Positive if anti-clockwise, negative if clockwise, 0 if collinear.
    """
    val = (p2[0] - p1[0]) * (p3[1] - p1[1]) - (p2[1] - p1[1]) * (p3[0] - p1[0])
    if val < 0:
        return Orientation.clockwise
    if val > 0:
        return Orientation.anticlockwise
    return Orientation.collinear


@njit(cache=True)
def _current_orientation(p1, p2, p3):
    return accelerated._orientation(p1, p2, p3)


@pytest.fixture(scope="module", autouse=True)
def _genuine_orientation_oracles_and_profile():
    own = ast.parse(Path(__file__).read_text(encoding="utf-8"))
    own_nodes = {
        node.name: node
        for node in own.body
        if isinstance(node, (ast.FunctionDef, ast.ClassDef))
    }
    actual_source = ast.parse(Path(accelerated.__file__).read_text(encoding="utf-8"))
    actual_enum = next(
        node
        for node in actual_source.body
        if isinstance(node, ast.ClassDef) and node.name == "Orientation"
    )
    for relative in ORIGINAL_PATHS:
        raw = (ROOT / relative).read_bytes()
        assert hashlib.sha256(raw).hexdigest() == ORIGINAL_SHA256
        frozen = ast.parse(raw.decode("utf-8"))
        for name in ("Orientation", "_orientation"):
            original_node = next(
                node
                for node in frozen.body
                if isinstance(node, (ast.FunctionDef, ast.ClassDef))
                and node.name == name
            )
            assert ast.dump(own_nodes[name]) == ast.dump(original_node)
        assert ast.dump(actual_enum) == ast.dump(own_nodes["Orientation"])
    assert not config.DISABLE_JIT
    assert Path(accelerated.__file__).resolve() == (
        ROOT / "src/napari/layers/shapes/_accelerated_triangulate_numba.py"
    ).resolve()
    assert isinstance(accelerated._orientation, CPUDispatcher)
    assert [(member.name, member.value) for member in Orientation] == [
        ("clockwise", -1), ("collinear", 0), ("anticlockwise", 1)
    ]
    assert [(member.name, member.value) for member in accelerated.Orientation] == [
        ("clockwise", -1), ("collinear", 0), ("anticlockwise", 1)
    ]


def _readonly_point(values, dtype, direction):
    storage = np.full(4, np.nan, dtype=dtype)
    point = storage[::2] if direction == "positive" else storage[::-2]
    point[:] = values
    point.flags.writeable = False
    return point


def _rounded_determinant(p1, p2, p3):
    # Source evaluation order: first two differences, first product, then
    # second two differences, second product, and final subtraction.
    left_type = np.result_type(p2.dtype, p1.dtype).type
    right_type = np.result_type(p3.dtype, p1.dtype).type
    product_type = np.result_type(left_type, right_type).type
    first_left = left_type(left_type(p2[0]) - left_type(p1[0]))
    first_right = right_type(right_type(p3[1]) - right_type(p1[1]))
    first = product_type(product_type(first_left) * product_type(first_right))
    second_left = left_type(left_type(p2[1]) - left_type(p1[1]))
    second_right = right_type(right_type(p3[0]) - right_type(p1[0]))
    second = product_type(product_type(second_left) * product_type(second_right))
    return product_type(first - second)


def _assert_enum_contract(points, expected_name, expected_value):
    signature = tuple(typeof(point) for point in points)
    original = _orientation(*points)
    current = _current_orientation(*points)
    original_member = getattr(Orientation, expected_name)
    current_member = getattr(accelerated.Orientation, expected_name)
    assert type(original) is Orientation and original is original_member
    assert type(current) is accelerated.Orientation and current is current_member
    assert original.name == current.name == expected_name
    assert original.value == current.value == expected_value
    original_type = _orientation.overloads[signature].signature.return_type
    current_type = _current_orientation.overloads[signature].signature.return_type
    assert original_type == typeof(original_member)
    assert current_type == typeof(current_member)
    assert isinstance(original_type, types.EnumMember)
    assert isinstance(current_type, types.EnumMember)
    assert original_type.instance_class is Orientation
    assert current_type.instance_class is accelerated.Orientation
    assert original_type.dtype == current_type.dtype == types.int64


@pytest.mark.parametrize(
    "case",
    [
        "float32-round-collinear",
        "float64-round-collinear",
        "mixed-first64-clockwise",
        "mixed-second64-anticlockwise",
        "float32-clockwise",
        "float64-anticlockwise",
    ],
)
def test_compiled_orientation_preserves_typed_rounding_and_Enum(case):
    if case == "float32-round-collinear":
        dtypes, exponent, reflected = (np.float32,) * 3, -13, False
        expected_name, expected_value = "collinear", 0
    elif case == "float64-round-collinear":
        dtypes, exponent, reflected = (np.float64,) * 3, -27, False
        expected_name, expected_value = "collinear", 0
    elif case == "mixed-first64-clockwise":
        dtypes, exponent, reflected = (np.float64, np.float32, np.float32), -13, False
        expected_name, expected_value = "clockwise", -1
    elif case == "mixed-second64-anticlockwise":
        dtypes, exponent, reflected = (np.float32, np.float64, np.float32), -13, True
        expected_name, expected_value = "anticlockwise", 1
    else:
        dtype = np.float32 if case == "float32-clockwise" else np.float64
        dtypes, exponent, reflected = (dtype,) * 3, None, False
        expected_name = "clockwise" if dtype == np.float32 else "anticlockwise"
        expected_value = -1 if dtype == np.float32 else 1
    if exponent is not None:
        delta = 2.0**exponent
        values = (
            ([0.0, 0.0], [1.0, 1.0 + delta], [1.0 - delta, 1.0])
            if reflected
            else ([0.0, 0.0], [1.0 + delta, 1.0], [1.0, 1.0 - delta])
        )
    else:
        values = ([2.0, 3.0], [3.0, 3.0], [2.0, 3.0 + expected_value])
    directions = ("negative", "positive", "negative")
    points = tuple(
        _readonly_point(value, dtype, direction)
        for value, dtype, direction in zip(values, dtypes, directions, strict=True)
    )
    before = tuple(point.tobytes() for point in points)
    determinant = _rounded_determinant(*points)
    if expected_name == "collinear":
        assert determinant == 0.0 and not np.signbit(determinant)
        assert determinant != -(delta * delta)
    elif exponent is not None:
        assert determinant == expected_value * (delta * delta)
    else:
        assert determinant == expected_value
    _assert_enum_contract(points, expected_name, expected_value)
    assert tuple(point.tobytes() for point in points) == before
    assert all(not point.flags.writeable for point in points)
    assert [point.strides[0] for point in points] == [
        -2 * points[0].dtype.itemsize,
        2 * points[1].dtype.itemsize,
        -2 * points[2].dtype.itemsize,
    ]


@pytest.mark.parametrize("expected_value", [-1, 1], ids=["clockwise", "anticlockwise"])
def test_compiled_orientation_retains_genuine_float64_typed_lists(expected_value):
    points = (
        List([np.float64(0.0), np.float64(0.0)]),
        List([np.float64(1.0), np.float64(0.0)]),
        List([np.float64(0.0), np.float64(expected_value)]),
    )
    assert all(typeof(point).dtype == types.float64 for point in points)
    assert all(not hasattr(typeof(point), "ndim") for point in points)
    before = tuple(tuple(point) for point in points)
    name = "clockwise" if expected_value < 0 else "anticlockwise"
    _assert_enum_contract(points, name, expected_value)
    assert tuple(tuple(point) for point in points) == before
