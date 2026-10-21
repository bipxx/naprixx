"""Finite public range-hook context checks; staged, never an app fallback.

The oracle compiles only the three pinned range functions in a private test
namespace. NumPy, Dask, and mutable source-global reduction providers are
explicit dependencies. This checks the new public delegation/context boundary,
not all range sampling, dtype kernels, or arbitrary provider protocols.
"""

from __future__ import annotations

import ast
import builtins
import hashlib
from functools import wraps
from pathlib import Path

import dask
import dask.array as da
import numpy as np
import pytest

PINNED_PATH = (
    Path(__file__).resolve().parents[1]
    / ".upstream/napari/src/napari/layers/utils/layer_utils.py"
)
PINNED_SHA256 = "6d3ed1151813da2cc39a1d476fb0b13cd712e1b741c0a42fb02d7b7f6a244d6d"
OWNED_ORACLE_NAMES = {"_nanmin", "_nanmax", "calc_data_range"}


def _original(*, builtin_updates=None, global_updates=None):
    raw = PINNED_PATH.read_bytes()
    assert hashlib.sha256(raw).hexdigest() == PINNED_SHA256
    parsed = ast.parse(raw, filename=str(PINNED_PATH))
    selected = [
        node
        for node in parsed.body
        if isinstance(node, ast.FunctionDef) and node.name in OWNED_ORACLE_NAMES
    ]
    assert {node.name for node in selected} == OWNED_ORACLE_NAMES
    module = ast.Module(
        body=[
            ast.ImportFrom(
                module="__future__",
                names=[ast.alias(name="annotations")],
                level=0,
            ),
            *selected,
        ],
        type_ignores=[],
    )
    source_builtins = dict(vars(builtins))
    source_builtins.update(builtin_updates or {})
    namespace = {
        "__name__": "range_dispatch_test_only_oracle",
        "__builtins__": source_builtins,
        "np": np,
        "dask": dask,
    }
    namespace.update(global_updates or {})
    exec(
        compile(ast.fix_missing_locations(module), str(PINNED_PATH), "exec"), namespace
    )
    assert "_native" not in namespace
    return namespace["calc_data_range"]


def _wrapped(original):
    @wraps(original)
    def inner(*args, **kwargs):
        return original(*args, **kwargs)

    @wraps(inner)
    def outer(*args, **kwargs):
        return inner(*args, **kwargs)

    return outer


@pytest.fixture
def bindings():
    from naparixx import _adapters, native

    return _adapters, native


def test_fresh_public_hook_gets_original_context_without_native_global(
    monkeypatch, bindings
):
    adapters, native = bindings
    original = _original()
    source_builtins = original.__builtins__
    # Function builtins remain authoritative even if the globals entry changes.
    original.__globals__["__builtins__"] = {"float": object()}
    data, rgb, dtype = object(), object(), object()
    first_result, second_result = (object(), object()), (object(), object())
    seen = []

    def first(*args):
        seen.append(("first", args))
        return first_result

    def second(*args):
        seen.append(("second", args))
        return second_result

    adapted = adapters.range_adapter(_wrapped(original))
    monkeypatch.setattr(native, "calc_data_range", first)
    assert adapted(data, rgb=rgb, dtype=dtype) is first_result
    monkeypatch.setattr(native, "calc_data_range", second)
    assert adapted(data, rgb=None, dtype=None) is second_result
    assert [name for name, _ in seen] == ["first", "second"]
    for (_, args), expected_rgb, expected_dtype in zip(
        seen, (rgb, None), (dtype, None), strict=True
    ):
        assert len(args) == 5
        assert args[0] is data
        assert args[1] is original.__globals__
        assert "_native" not in args[1]
        assert args[2] is source_builtins
        assert args[2] is not args[1]["__builtins__"]
        assert args[3] is expected_rgb
        assert args[4] is expected_dtype


@pytest.mark.parametrize("authority", ["builtins", "globals"])
def test_context_preserves_builtin_authority_and_global_precedence(
    authority, monkeypatch, bindings
):
    adapters, native = bindings
    trace = []

    def builtin_float(value):
        trace.append(("builtins", float(value)))
        return float(value) + 10

    def global_float(value):
        trace.append(("globals", float(value)))
        return float(value) + 20

    original = _original(builtin_updates={"float": builtin_float})
    if authority == "globals":
        original.__globals__["float"] = global_float
    original.__globals__["__builtins__"] = dict(vars(builtins))
    data = np.array([[-2, 5]], dtype=np.float16)
    expected = original(data)
    expected_trace = list(trace)
    trace.clear()
    public = native.calc_data_range
    hooks = []

    def record(data_arg, namespace, source_builtins, rgb, dtype):
        hooks.append((namespace, source_builtins))
        return public(data_arg, namespace, source_builtins, rgb, dtype)

    monkeypatch.setattr(native, "calc_data_range", record)
    actual = adapters.range_adapter(original)(data)
    assert actual == expected
    assert actual == ((18.0, 25.0) if authority == "globals" else (8.0, 15.0))
    assert trace == expected_trace == [(authority, -2.0), (authority, 5.0)]
    assert len(hooks) == 1
    assert hooks[0][0] is original.__globals__
    assert hooks[0][1] is original.__builtins__


class ArrayProvider:
    def __init__(self):
        self.payload = np.arange(24.0).reshape(2, 3, 4)
        self.trace = []

    @property
    def dtype(self):
        self.trace.append("dtype")
        return self.payload.dtype

    @property
    def size(self):
        self.trace.append("size")
        return self.payload.size

    def __array__(self, dtype=None, copy=None):
        self.trace.append(("array", dtype, copy))
        return np.asarray(self.payload, dtype=dtype, copy=copy)


@pytest.mark.parametrize("kind", ["float16", "lazy", "provider"])
def test_generic_inputs_use_public_context_hook(kind, monkeypatch, bindings):
    adapters, native = bindings
    original = _original()
    if kind == "float16":
        data = np.array([-2, 5, 1], dtype=np.float16)
    elif kind == "lazy":
        data = da.from_array(np.arange(24.0).reshape(2, 3, 4), chunks=(1, 3, 2))
    else:
        data = ArrayProvider()
    expected = original(data)
    expected_trace = list(data.trace) if kind == "provider" else None
    if kind == "provider":
        data.trace.clear()
    public = native.calc_data_range
    calls = []

    def record(data_arg, namespace, source_builtins, rgb, dtype):
        calls.append((data_arg, namespace, source_builtins, rgb, dtype))
        return public(data_arg, namespace, source_builtins, rgb, dtype)

    monkeypatch.setattr(native, "calc_data_range", record)
    assert adapters.range_adapter(original)(data) == expected
    assert len(calls) == 1
    assert calls[0][0] is data
    assert calls[0][1] is original.__globals__
    assert calls[0][2] is original.__builtins__
    assert calls[0][3:] == (False, None)
    if kind == "provider":
        assert data.trace == expected_trace
        assert "dtype" in expected_trace and "size" in expected_trace
        assert any(isinstance(item, tuple) for item in expected_trace)


@pytest.mark.parametrize("failure", ["public_hook", "source_helper"])
def test_public_and_source_helper_errors_propagate_unchanged(
    failure, monkeypatch, bindings
):
    adapters, native = bindings
    marker = LookupError(failure)
    original = _original()
    data = np.array([-2, 5], dtype=np.float16)
    seen = []

    def fail(*args):
        seen.append(args)
        raise marker

    if failure == "public_hook":
        monkeypatch.setattr(native, "calc_data_range", fail)
    else:
        original.__globals__["_nanmin"] = fail
        with pytest.raises(LookupError) as expected:
            original(data)
        assert expected.value is marker
        seen.clear()
        public = native.calc_data_range

        def record(*args):
            return public(*args)

        monkeypatch.setattr(native, "calc_data_range", record)
    with pytest.raises(LookupError) as actual:
        adapters.range_adapter(original)(data)
    assert actual.value is marker
    assert len(seen) == 1
    assert seen[0][0] is data
    if failure == "public_hook":
        assert len(seen[0]) == 5
        assert seen[0][1] is original.__globals__
        assert seen[0][2] is original.__builtins__
    else:
        assert len(seen[0]) == 1
