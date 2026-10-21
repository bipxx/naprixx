"""Retain genuine typed-list scalar support while adding Array fast paths."""
from __future__ import annotations

import importlib.util
from pathlib import Path
import sys

import numpy as np
import pytest
from numba import typeof, types
from numba.typed import List


ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    'shape_scalar_indexable_oracles',
    ROOT / '.artifacts/test_shape_scalar_typing_contracts_next.py',
)
assert spec is not None and spec.loader is not None
oracles = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = oracles
spec.loader.exec_module(oracles)
# Reuse the source-hashed original-formula/profile fixture as an autouse fixture.
_genuine_original_oracles_and_profile = oracles._genuine_original_oracles_and_profile


@pytest.mark.parametrize('operation', ['_dot', '_cross_z'])
def test_compiled_scalar_retains_genuine_float64_typed_lists(operation):
    first = List([np.float64(3), np.float64(4)])
    second = List([np.float64(5), np.float64(6)])
    signature = (typeof(first), typeof(second))
    assert signature[0].dtype == signature[1].dtype == types.float64
    assert not hasattr(signature[0], 'ndim') and not hasattr(signature[1], 'ndim')
    original = oracles._dot if operation == '_dot' else oracles._cross_z
    current = oracles._current_dot if operation == '_dot' else oracles._current_cross_z
    expected = 39.0 if operation == '_dot' else -2.0
    assert original(first, second) == expected
    assert current(first, second) == expected
    assert original.overloads[signature].signature.return_type == types.float64
    assert current.overloads[signature].signature.return_type == types.float64
    assert list(first) == [3.0, 4.0] and list(second) == [5.0, 6.0]
