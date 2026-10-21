"""Finite compiled scalar contracts for the two Shape triangulation helpers.

The original formulas below are test-only independent cached Numba oracles.
Actual calls always reach the normal imported current module.
"""

from __future__ import annotations

import ast
import hashlib
from pathlib import Path

import numpy as np
import pytest
from numba import njit, typeof
from numba.core import config, types
from numba.core.registry import CPUDispatcher

from napari.layers.shapes import _accelerated_triangulate_numba as accelerated

ROOT = Path(__file__).resolve().parents[1]
ORIGINAL_SHA256 = "50dd996c7ebf92b84a6bb0944884eb6e4b2e8a7a9eea25571d0d466b1bfdec90"
ORIGINAL_PATHS = (
    ".artifacts/shape-helper-closure-before/current/napari/layers/shapes/"
    "_accelerated_triangulate_numba.py",
    ".artifacts/shape-helper-closure-before/pinned/napari/layers/shapes/"
    "_accelerated_triangulate_numba.py",
)

@njit(cache=True, inline='always')
def _dot(v0: np.ndarray, v1: np.ndarray) -> float:
    """Return the dot product of two 2D vectors.

    If the vectors have norm 1, this is the cosine of the angle between them.
    """
    return v0[0] * v1[0] + v0[1] * v1[1]

@njit(cache=True, inline='always')
def _cross_z(v0: np.ndarray, v1: np.ndarray) -> float:
    """Return the z-magnitude of the cross-product between two vectors.

    If the vectors have norm 1, this is the sine of the angle between them.
    """
    return v0[0] * v1[1] - v0[1] * v1[0]


@njit(cache=True)
def _current_dot(v0, v1):
    return accelerated._dot(v0, v1)


@njit(cache=True)
def _current_cross_z(v0, v1):
    return accelerated._cross_z(v0, v1)


@pytest.fixture(scope="module", autouse=True)
def _genuine_original_oracles_and_profile():
    own = ast.parse(Path(__file__).read_text(encoding="utf-8"))
    own_functions = {
        node.name: node for node in own.body if isinstance(node, ast.FunctionDef)
    }
    for relative_path in ORIGINAL_PATHS:
        source = (ROOT / relative_path).read_bytes()
        assert hashlib.sha256(source).hexdigest() == ORIGINAL_SHA256
        frozen = ast.parse(source.decode("utf-8"))
        originals = {
            node.name: node
            for node in frozen.body
            if isinstance(node, ast.FunctionDef)
        }
        for name in ("_dot", "_cross_z"):
            assert ast.dump(own_functions[name]) == ast.dump(originals[name])
    assert not config.DISABLE_JIT
    assert Path(accelerated.__file__).resolve() == (
        ROOT / "src/napari/layers/shapes/_accelerated_triangulate_numba.py"
    ).resolve()
    for name in ("_dot", "_cross_z"):
        assert isinstance(getattr(accelerated, name), CPUDispatcher)


def _readonly_vector(values, dtype, direction):
    storage = np.full(4, np.nan, dtype=dtype)
    vector = storage[::2] if direction == "positive" else storage[::-2]
    vector[:] = values
    vector.flags.writeable = False
    return vector


def _rounded_expected(operation, first, second, dtype):
    # The two explicit casts preserve each original typed multiply boundary.
    if operation == "_dot":
        first_product = dtype(dtype(first[0]) * dtype(second[0]))
        second_product = dtype(dtype(first[1]) * dtype(second[1]))
        return dtype(first_product + second_product)
    first_product = dtype(dtype(first[0]) * dtype(second[1]))
    second_product = dtype(dtype(first[1]) * dtype(second[0]))
    return dtype(first_product - second_product)


def _assert_scalar_contract(operation, first, second, expected, numba_type):
    oracle = _dot if operation == "_dot" else _cross_z
    current = _current_dot if operation == "_dot" else _current_cross_z
    first_bytes, second_bytes = first.tobytes(), second.tobytes()
    original_result = oracle(first, second)
    current_result = current(first, second)
    assert original_result == expected
    assert current_result == expected
    assert bool(np.signbit(original_result)) == bool(np.signbit(expected))
    assert bool(np.signbit(current_result)) == bool(np.signbit(expected))
    signature = (typeof(first), typeof(second))
    assert oracle.overloads[signature].signature.return_type == numba_type
    assert current.overloads[signature].signature.return_type == numba_type
    assert first.tobytes() == first_bytes
    assert second.tobytes() == second_bytes
    assert not first.flags.writeable and not second.flags.writeable


@pytest.mark.parametrize("operation", ["_dot", "_cross_z"])
@pytest.mark.parametrize(
    "pair",
    ["float32", "float64", "float32-float64", "float64-float32"],
)
def test_compiled_products_round_before_cancellation(operation, pair):
    definitions = {
        "float32": (np.float32, np.float32, -13, "positive", "positive"),
        "float64": (np.float64, np.float64, -27, "negative", "negative"),
        "float32-float64": (np.float32, np.float64, -13, "positive", "negative"),
        "float64-float32": (np.float64, np.float32, -13, "negative", "positive"),
    }
    first_dtype, second_dtype, exponent, first_direction, second_direction = (
        definitions[pair]
    )
    delta = 2.0**exponent
    first = _readonly_vector([1.0 + delta, 1.0], first_dtype, first_direction)
    second_values = (
        [1.0 - delta, -1.0] if operation == "_dot" else [1.0, 1.0 - delta]
    )
    second = _readonly_vector(second_values, second_dtype, second_direction)
    assert first.strides[0] == (
        2 if first_direction == "positive" else -2
    ) * first.dtype.itemsize
    assert second.strides[0] == (
        2 if second_direction == "positive" else -2
    ) * second.dtype.itemsize
    promoted = np.float32 if pair == "float32" else np.float64
    expected = _rounded_expected(operation, first, second, promoted)
    if first_dtype == second_dtype:
        # Rounded first product is 1, so the separately rounded reduction is +0.
        # An incorrectly contracted/widened product instead preserves -delta**2.
        assert expected == promoted(0.0)
        assert not np.signbit(expected)
        assert expected != -(delta * delta)
    else:
        # The float32 inputs are exact at delta=2**-13; promotion preserves the
        # product decrement in float64, including both operand orders.
        assert expected == np.float64(-(delta * delta))
    expected_type = types.float32 if promoted == np.float32 else types.float64
    _assert_scalar_contract(operation, first, second, expected, expected_type)


@pytest.mark.parametrize("operation", ["_dot", "_cross_z"])
@pytest.mark.parametrize("dtype", [np.float32, np.float64], ids=["float32", "float64"])
def test_compiled_scalar_preserves_source_negative_zero(operation, dtype):
    first_values = [-0.0, -0.0] if operation == "_dot" else [-0.0, 0.0]
    first = _readonly_vector(first_values, dtype, "negative")
    second = _readonly_vector([1.0, 1.0], dtype, "negative")
    expected = _rounded_expected(operation, first, second, dtype)
    assert expected == dtype(0.0)
    assert np.signbit(expected)
    expected_type = types.float32 if dtype == np.float32 else types.float64
    _assert_scalar_contract(operation, first, second, expected, expected_type)
