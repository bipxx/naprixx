"""Observable original evaluation order and leases; root executes this packet."""
from __future__ import annotations

import gc
import importlib
import weakref
from pathlib import Path
from types import SimpleNamespace

import numpy as np
import pytest

ROOT = Path(__file__).resolve().parents[1]


@pytest.fixture
def triangulate():
    module = importlib.import_module(
        "napari.layers.shapes._accelerated_triangulate_python"
    )
    assert Path(module.__file__).resolve() == (
        ROOT / "src/napari/layers/shapes/_accelerated_triangulate_python.py"
    ).resolve()
    return module


@pytest.fixture
def adapters():
    module = importlib.import_module("naparixx._adapters")
    assert Path(module.__file__).resolve() == (
        ROOT / "src/naparixx/_adapters.py"
    ).resolve()
    return module


class _NumPyHooks:
    def __init__(self, **hooks):
        self.hooks = hooks

    def __getattr__(self, name):
        if name in self.hooks:
            return self.hooks[name]
        return getattr(np, name)


class _ShapeObservation(np.ndarray):
    def __array_finalize__(self, original):
        self.observation = None

    @property
    def shape(self):
        if self.observation is not None:
            events, label = self.observation
            events.append(label)
        return np.ndarray.shape.__get__(self)


@pytest.mark.parametrize("dimension", [2, 3])
def test_sign_cross_reads_each_chained_middle_shape_once(
    triangulate, monkeypatch, dimension
):
    events = []
    first = np.array([[1, 0], [0, 1]], dtype=float)
    second = np.array([[0, 1], [1, 0]], dtype=float)
    if dimension == 3:
        first = np.column_stack((first, np.zeros(2)))
        second = np.column_stack((second, np.zeros(2)))
    x, y = first.view(_ShapeObservation), second.view(_ShapeObservation)
    x.observation, y.observation = (events, "x"), (events, "y")
    crossed = []

    def cross(a, b):
        crossed.append((a, b))
        return np.cross(np.asarray(a), np.asarray(b))

    monkeypatch.setattr(triangulate, "np", _NumPyHooks(cross=cross))
    result = triangulate._sign_cross(x, y)
    assert events == (["x", "y"] if dimension == 2 else ["x", "y", "x", "y"])
    assert result.dtype == np.int32
    np.testing.assert_array_equal(
        result,
        [1, -1] if dimension == 2 else [[1, 1, 1], [1, 1, -1]],
    )
    if dimension == 2:
        assert crossed == []
    else:
        assert len(crossed) == 1
        assert crossed[0][0] is x
        assert crossed[0][1] is y


class _AugmentedObservation(np.ndarray):
    def __array_finalize__(self, original):
        self.augmented = None
        self.role = None

    def __lt__(self, other):
        state = self.augmented
        if state is not None and self.role == "target" and state.kind == "add":
            state.events.append(("mask", None))
            return np.less(np.asarray(self), other)
        return np.ndarray.__lt__(self, other)

    def __getitem__(self, key):
        value = np.ndarray.__getitem__(self, key)
        state = self.augmented
        if state is not None and self.role == "target" and state.selects(key):
            state.events.append(("get", key))
            state.active_key = key
            value.augmented, value.role = state, "view"
        return value

    def __setitem__(self, key, value):
        state = self.augmented
        if (
            state is not None
            and self.role == "target"
            and state.active_key is not None
            and state.selects(key)
        ):
            state.events.append(("set", key))
            state.active_key = None
        # ndarray's implementation may index a subclass internally. Observe
        # the public store boundary once, independently of those internals.
        original_role = self.role
        self.role = None
        try:
            return np.ndarray.__setitem__(self, key, value)
        finally:
            self.role = original_role

    def __iadd__(self, other):
        if self.augmented is not None and self.role == "view":
            self.augmented.events.append(("iadd", other))
        return np.ndarray.__iadd__(self, other)

    def __imul__(self, other):
        if self.augmented is not None and self.role == "view":
            self.augmented.events.append(("imul", other))
        return np.ndarray.__imul__(self, other)

    def __itruediv__(self, other):
        if self.augmented is not None and self.role == "view":
            self.augmented.events.append(("itruediv", other))
        return np.ndarray.__itruediv__(self, other)


def _observed_array(value, state):
    result = np.asarray(value).view(_AugmentedObservation)
    result.augmented, result.role = state, "target"
    return result


class _PolarAngles(np.ndarray):
    def __array_finalize__(self, original):
        self.augmented = None

    def __sub__(self, other):
        return _observed_array(np.subtract(np.asarray(self), other), self.augmented)


def test_polar_mask_key_precedes_rhs_and_is_reused_for_augmented_store(
    triangulate, monkeypatch
):
    state = SimpleNamespace(
        kind="add",
        events=[],
        active_key=None,
        selects=lambda key: isinstance(key, np.ndarray) and key.dtype == np.bool_,
    )
    angles = np.array([0.0, -2.0, 1.0]).view(_PolarAngles)
    angles.augmented = state

    class PolarNumPy(_NumPyHooks):
        @property
        def pi(self):
            state.events.append(("rhs", None))
            return np.pi

    def arctan2(y, x):
        assert y.shape == x.shape == (3,)
        return angles

    monkeypatch.setattr(triangulate, "np", PolarNumPy(arctan2=arctan2))
    try:
        result = triangulate._are_polar_angles_monotonic(
            np.array([[0.0, 0.0], [1.0, 0.0], [0.0, 1.0]]), -1
        )
        assert result is False
        assert [event[0] for event in state.events] == [
            "mask", "get", "rhs", "iadd", "set"
        ]
        assert state.events[1][1] is state.events[4][1]
        assert state.events[3][1] == 2 * np.pi
    finally:
        state.events.clear()
        state.active_key = None


@pytest.mark.parametrize("site", ["multiply", "divide"])
def test_mesh_offset_augmented_get_rhs_operator_store_order(
    triangulate, monkeypatch, site
):
    def selects(key):
        if site == "multiply":
            return (
                isinstance(key, slice)
                and key.start is None
                and key.stop is None
                and key.step == 2
            )
        return isinstance(key, np.ndarray) and np.issubdtype(key.dtype, np.integer)

    state = SimpleNamespace(kind=site, events=[], active_key=None, selects=selects)
    repeat_calls = []

    def repeat(*args, **kwargs):
        result = np.repeat(*args, **kwargs)
        repeat_calls.append(None)
        return _observed_array(result, state) if len(repeat_calls) == 2 else result

    def sqrt(value):
        state.events.append(("rhs", None))
        return np.sqrt(value)

    hooks = {"repeat": repeat}
    if site == "divide":
        hooks["sqrt"] = sqrt
    monkeypatch.setattr(triangulate, "np", _NumPyHooks(**hooks))
    try:
        centers, offsets, triangles = triangulate.generate_2D_edge_meshes_py(
            np.array([[0.0, 0.0], [3.0, 0.0], [3.0, 3.0]]),
            bevel=site == "divide",
        )
        expected = (
            ["get", "imul", "set"]
            if site == "multiply"
            else ["get", "rhs", "itruediv", "set"]
        )
        assert [event[0] for event in state.events] == expected
        assert state.events[0][1] is state.events[-1][1]
        if site == "multiply":
            assert state.events[1][1] == -1
        assert centers.shape == offsets.shape
        assert centers.shape[1] == 2
        assert triangles.shape[1] == 3
        assert np.isfinite(centers).all()
        assert np.isfinite(offsets).all()
    finally:
        state.events.clear()
        state.active_key = None


class _ConversionFailure(Exception):
    pass


@pytest.mark.parametrize("failed_argument", ["second", "reference"])
def test_normal_adapter_later_rhs_failure_releases_unassigned_array_temporaries(
    adapters, monkeypatch, failed_argument
):
    markers = {name: object() for name in ("first", "second", "reference")}
    events, leases = [], []
    failure = _ConversionFailure(failed_argument)

    def asarray(value):
        name = next(name for name, marker in markers.items() if marker is value)
        events.append(name)
        if name == failed_argument:
            raise failure
        array = np.array([0.0, 1.0])
        leases.append(weakref.ref(array))
        return array

    def original(a, b, p=(0, 0, 1)):
        pytest.fail("source selection cannot occur after a conversion failure")

    monkeypatch.setattr(adapters, "np", _NumPyHooks(asarray=asarray))
    wrapped = adapters.normal_adapter(original)
    try:
        with pytest.raises(_ConversionFailure) as caught:
            wrapped(markers["first"], markers["second"], markers["reference"])
        assert caught.value is failure
        assert caught.value.__traceback__ is not None
        assert wrapped.__wrapped__ is original
        assert events == (
            ["first", "second"]
            if failed_argument == "second"
            else ["first", "second", "reference"]
        )
        assert len(leases) == (1 if failed_argument == "second" else 2)
        gc.collect()
        assert all(lease() is None for lease in leases)
        assert caught.value.__traceback__ is not None
    finally:
        failure.__traceback__ = None
        leases.clear()
