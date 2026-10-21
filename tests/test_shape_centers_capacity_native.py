"""Original compiled miter contract with exactly two writable rows remaining."""

from __future__ import annotations

import hashlib
import importlib.util
import sys
from pathlib import Path

import numpy as np
from numba.core import types

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / ".artifacts/test_shape_centers_typing_contracts_next.py"
BASE_SHA = "ca744d4748ac17f25748f6c644df4ebd6d666ee3adbae3592dcc86812e5a7e9a"
assert hashlib.sha256(BASE.read_bytes()).hexdigest() == BASE_SHA
NAME = "_naparixx_test_centers_capacity_frozen16"
if NAME in sys.modules:
    base = sys.modules[NAME]
else:
    spec = importlib.util.spec_from_file_location(NAME, BASE)
    assert spec is not None and spec.loader is not None
    base = importlib.util.module_from_spec(spec)
    sys.modules[NAME] = base
    spec.loader.exec_module(base)

# Reuse the genuine frozen fixture and source-defined cached consumer unchanged.
original_compiled = base.original_compiled


def test_original_compiled_miter_with_exactly_two_output_rows(original_compiled):
    case = next(row for row in base.CASES if row[0] == "miter_f64")
    actual_args, actual_roots = base._inputs(case)
    expected_args, expected_roots = base._inputs(case)
    remaining_capacity = base.J + 2
    actual_args = (
        tuple(array[:remaining_capacity] for array in actual_args[:3]) + actual_args[3:]
    )
    expected_args = (
        tuple(array[:remaining_capacity] for array in expected_args[:3])
        + expected_args[3:]
    )
    actual_before = {name: array.copy() for name, array in actual_roots.items()}
    assert all(array.shape[0] - base.J == 2 for array in actual_args[:3])
    assert actual_args[-1] is False

    expected = original_compiled(*expected_args)
    observed = base.compiled_current(*actual_args)
    assert observed[0] == expected[0] == 2
    assert base.compiled_current.nopython_signatures
    assert original_compiled.nopython_signatures
    current_type = base.compiled_current.overloads[
        base.compiled_current.signatures[-1]
    ].signature.return_type
    original_type = original_compiled.overloads[
        original_compiled.signatures[-1]
    ].signature.return_type
    assert current_type.types[0] == original_type.types[0] == types.int64
    for index in range(3):
        assert observed[index + 1] is actual_args[index]
        assert expected[index + 1] is expected_args[index]
    assert actual_roots.keys() == expected_roots.keys()
    for name, storage in actual_roots.items():
        base._exact_storage(storage, expected_roots[name])
        untouched = expected_roots[name] == actual_before[name]
        np.testing.assert_array_equal(
            storage[untouched], actual_before[name][untouched]
        )
    # Optimistic indices are integer writes; no referenced vertex is read here.
    np.testing.assert_array_equal(
        actual_args[2][base.J : base.J + 2], expected_args[2][base.J : base.J + 2]
    )
