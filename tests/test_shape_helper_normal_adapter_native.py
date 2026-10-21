"""Canonical closure and native-selection contracts for normal_adapter."""

import inspect

import numpy as np
import pytest

from naparixx import _adapters


@pytest.mark.parametrize("dtype, native_selected", [(np.float64, True), (np.float32, False)])
def test_adapter_selects_native_or_current_original_without_copying_inputs(monkeypatch, dtype, native_selected):
    calls = []
    native_result, original_result = object(), object()
    first = np.array([0, 0], dtype=dtype)
    second = np.array([4, 0], dtype=dtype)
    reference = np.array([0, 0, 1], dtype=np.float64)

    def original(a, b, p=(0, 0, 1)):
        calls.append(("original", a, b, p))
        return original_result

    def native(a, b, p):
        calls.append(("native", a, b, p))
        return native_result

    monkeypatch.setattr(_adapters._native, "segment_normal", native)
    wrapped = _adapters.normal_adapter(original)
    result = wrapped(first, second, reference)
    assert wrapped.__wrapped__ is original
    assert inspect.signature(wrapped) == inspect.signature(original)
    assert result is (native_result if native_selected else original_result)
    assert len(calls) == 1
    assert calls[0][0] == ("native" if native_selected else "original")
    assert calls[0][1] is first
    assert calls[0][2] is second
    assert calls[0][3] is reference


def test_adapter_uses_genuine_original_cell_after_replacement():
    first = np.array([0, 0], dtype=np.float32)
    second = np.array([4, 0], dtype=np.float32)
    previous, current = object(), object()

    def original(a, b, p=(0, 0, 1)):
        return previous

    def replacement(a, b, p=(0, 0, 1)):
        return current

    wrapped = _adapters.normal_adapter(original)
    cells = dict(zip(wrapped.__code__.co_freevars, wrapped.__closure__, strict=True))
    assert "original" in cells
    cells["original"].cell_contents = replacement
    assert wrapped(first, second) is current
    assert wrapped.__wrapped__ is original


def test_adapter_conversion_failure_preserves_exception_identity_before_selection(monkeypatch):
    failure = RuntimeError("array conversion")
    trace = []

    class BadArray:
        def __array__(self, dtype=None, copy=None):
            trace.append("array")
            raise failure

    def original(a, b, p=(0, 0, 1)):
        trace.append("original")

    wrapped = _adapters.normal_adapter(original)
    with pytest.raises(RuntimeError) as caught:
        wrapped(BadArray(), [0, 0])
    assert caught.value is failure
    assert trace == ["array"]
