"""Small public/provider contracts for MC4-MC8; root executes this packet."""
from __future__ import annotations

import importlib
import inspect
import weakref
from pathlib import Path

import numpy as np
import pytest

ROOT = Path(__file__).resolve().parents[1]


def _module(name, relative_path):
    module = importlib.import_module(name)
    assert Path(module.__file__).resolve() == (ROOT / relative_path).resolve()
    return module


@pytest.fixture
def triangulate():
    return _module(
        "napari.layers.shapes._accelerated_triangulate_python",
        "src/napari/layers/shapes/_accelerated_triangulate_python.py",
    )


@pytest.fixture
def adapters():
    return _module("naparixx._adapters", "src/naparixx/_adapters.py")


def _numba_module():
    return _module(
        "napari.layers.shapes._accelerated_triangulate_numba",
        "src/napari/layers/shapes/_accelerated_triangulate_numba.py",
    )


class _IndexObservation(np.ndarray):
    def __array_finalize__(self, original):
        self.indices = None

    def __getitem__(self, key):
        if self.indices is not None and isinstance(key, tuple):
            self.indices.append(key)
        return np.ndarray.__getitem__(self, key)


def test_box_provider_receives_same_folded_index_tuple_objects_across_calls(
    triangulate,
):
    box = np.array([[0.0, 0.0], [2.0, 4.0]]).view(_IndexObservation)
    observed = []
    box.indices = observed
    first = triangulate.create_box_from_bounding_py(box)
    second = triangulate.create_box_from_bounding_py(box)
    expected = [
        ((0, 0), (0, 1)),
        ((1, 1), (0, 1)),
        ((1, 0), (0, 1)),
        ((0, 1), (0, 1)),
    ]
    assert observed == expected + expected
    assert all(observed[index] is observed[index + 4] for index in range(4))
    np.testing.assert_array_equal(first, second)
    assert first.shape == (9, 2)


@pytest.mark.parametrize("backend", ["python", "numba_py_func"])
def test_normalizer_provider_receives_same_zero_edge_constant_across_calls(
    triangulate, monkeypatch, backend
):
    module = triangulate if backend == "python" else _numba_module()
    function = (
        module.normalize_vertices_and_edges_py
        if backend == "python"
        else module.normalize_vertices_and_edges.py_func
    )
    removed = []

    class ObservedSet(set):
        def remove(self, edge):
            if edge == (0, 0):
                removed.append(edge)
            return super().remove(edge)

    monkeypatch.setattr(module, "set", ObservedSet, raising=False)
    vertices = np.array(
        [[0.0, 0.0], [2.0, 0.0], [2.0, 2.0], [0.0, 0.0]]
    ).view(_IndexObservation)
    observed_indices = []
    vertices.indices = observed_indices
    first = function(vertices)
    second = function(vertices)
    assert len(removed) == 2
    assert removed[0] is removed[1]
    assert removed[0] == (0, 0)
    np.testing.assert_array_equal(first[0], second[0])
    assert set(map(tuple, first[1])) == set(map(tuple, second[1]))
    if backend == "numba_py_func":
        expected = [(0, 0), (-1, 0), (0, 1), (-1, 1)]
        assert observed_indices == expected + expected
        assert all(
            observed_indices[index] is observed_indices[index + 4]
            for index in range(4)
        )


@pytest.mark.parametrize("constructor_result", ["exception", "invalid_object"])
def test_shadowed_raise_factory_exception_class_is_constructed_and_validated(
    triangulate, monkeypatch, constructor_result
):
    events = []

    class ReturnedError(Exception):
        def __new__(cls, *args):
            events.append(("construct", args))
            if constructor_result == "invalid_object":
                return object()
            return super().__new__(cls)

    def factory(*args):
        events.append(("factory", args))
        return ReturnedError

    monkeypatch.setattr(triangulate, "ValueError", factory, raising=False)
    expected_type = ReturnedError if constructor_result == "exception" else TypeError
    try:
        with pytest.raises(expected_type) as caught:
            triangulate._sign_cross(np.zeros((1, 4)), np.zeros((1, 4)))
        assert events == [("factory", (4, 4)), ("construct", ())]
        if constructor_result == "exception":
            assert type(caught.value) is ReturnedError
            assert caught.value.args == ()
        else:
            assert str(caught.value) == (
                f"calling {ReturnedError!r} should have returned an instance of "
                f"BaseException, not {object!r}"
            )
    finally:
        if "caught" in locals():
            caught.value.__traceback__ = None


class _ComprehensionFailure(Exception):
    pass


@pytest.mark.parametrize("comprehension", ["first", "second"])
def test_set_comprehension_retains_current_edge_through_iterator_set_unwind(
    triangulate, comprehension
):
    events, references = [], []
    failure = _ComprehensionFailure(comprehension)
    current = [None]
    target_pass = 2 if comprehension == "first" else 3

    def current_live():
        return current[0] is not None and current[0]() is not None

    class Endpoint:
        def __init__(self, value, failing=False):
            self.value, self.failing = value, failing

        def __hash__(self):
            if self.failing:
                raise failure
            return hash(self.value)

        def __eq__(self, other):
            return self.value == (other.value if isinstance(other, Endpoint) else other)

    class Edge:
        def __init__(self, sequence):
            self.endpoints = (
                Endpoint(0), Endpoint(1)
            ) if sequence == 1 else (Endpoint(1), Endpoint(2, failing=True))
            if sequence == 1:
                references.extend(
                    weakref.ref(
                        endpoint,
                        lambda reference: events.append(
                            ("partial-set", current_live())
                        ),
                    )
                    for endpoint in self.endpoints
                )

        def __getitem__(self, index):
            return self.endpoints[index]

    class TargetIterator:
        def __init__(self):
            self.sequence = 0
            self.previous = None

        def __iter__(self):
            return self

        def __next__(self):
            if self.previous is not None:
                events.append(("following-next", self.previous() is not None))
            self.sequence += 1
            edge = Edge(self.sequence)
            self.previous = weakref.ref(edge)
            if self.sequence == 2:
                current[0] = weakref.ref(
                    edge, lambda reference: events.append(("current-edge", False))
                )
                references.append(current[0])
            return edge

        def __del__(self):
            events.append(("iterator", current_live()))

    class Edges:
        def __init__(self):
            self.passes = 0

        def __iter__(self):
            self.passes += 1
            return TargetIterator() if self.passes == target_pass else iter([(0, 1)])

    edges = Edges()
    try:
        with pytest.raises(_ComprehensionFailure) as caught:
            triangulate.reconstruct_polygons_from_edges_py(
                np.array([[0.0, 0.0], [1.0, 0.0]]), edges
            )
        assert caught.value is failure
        assert edges.passes == target_pass
        assert [event for event in events if event[0] == "following-next"] == [
            ("following-next", True)
        ]
        cleanup = [
            event for event in events
            if event[0] in ("iterator", "partial-set", "current-edge")
        ]
        assert cleanup == [
            ("iterator", True),
            ("partial-set", True),
            ("partial-set", True),
            ("current-edge", False),
        ]
        assert current[0]() is None
        assert caught.value.__traceback__ is not None
    finally:
        failure.__traceback__ = None
        references.clear()


@pytest.mark.parametrize("surface", ["mirror", "adapter", "numba_py_func"])
def test_source_absent_shadowed_globals_callback_is_never_added(
    triangulate, adapters, monkeypatch, surface
):
    module = (
        triangulate if surface == "mirror"
        else adapters if surface == "adapter"
        else _numba_module()
    )
    events = []

    def forbidden():
        events.append("globals callback")
        pytest.fail("the original algorithm has no globals lookup at this point")

    monkeypatch.setattr(module, "globals", forbidden, raising=False)
    if surface == "mirror":
        np.testing.assert_array_equal(
            triangulate._mirror_point(np.array([1, 2]), np.array([4, 7])), [7, 12]
        )
    elif surface == "adapter":
        marker = object()

        def original(a, b, p=(0, 0, 1)):
            return marker

        wrapped = adapters.normal_adapter(original)
        assert wrapped(
            np.zeros(2, dtype=np.float32), np.ones(2, dtype=np.float32)
        ) is marker
    else:
        assert module._dot.py_func(np.array([2, 3]), np.array([5, 7])) == 31
    assert events == []


def test_normal_adapter_uses_one_wraps_and_original_raw_declaration_metadata(
    adapters, monkeypatch
):
    events = []
    observed = []
    decorated = object()

    def original(a, b, p=(0, 0, 1)):
        return None

    def wraps(callback):
        events.append(("wraps", callback))

        def decorate(raw):
            events.append(("decorate", raw))
            observed.append(raw)
            return decorated

        return decorate

    monkeypatch.setattr(adapters, "wraps", wraps)
    result = adapters.normal_adapter(original)
    assert result is decorated
    assert len(observed) == 1
    raw = observed[0]
    assert inspect.isfunction(raw)
    assert events == [("wraps", original), ("decorate", raw)]
    assert raw.__name__ == "call"
    assert raw.__qualname__ == "normal_adapter.<locals>.call"
    assert raw.__module__ == "naparixx._adapters"
    assert raw.__defaults__ == ((0, 0, 1),)
    assert raw.__doc__ is None
    assert raw.__annotations__ == {}
    signature = inspect.signature(raw, follow_wrapped=False)
    assert list(signature.parameters) == ["a", "b", "p"]
    assert all(
        parameter.kind is inspect.Parameter.POSITIONAL_OR_KEYWORD
        for parameter in signature.parameters.values()
    )
