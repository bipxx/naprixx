"""Pinned-source event traces for the native model change journal."""

import copy
import gc
import importlib.util
import operator
import sys
import weakref
from pathlib import Path

import numpy as np
import pytest
from pydantic import BaseModel, ConfigDict, Field, ValidationError

from napari.utils.events import EventedModel
from napari.utils.events.evented_model import ComparisonDelayer
from napari.utils.misc import _quiet_array_equal, pick_equality_operator
from naparixx import native


@pytest.fixture(scope="module")
def baseline():
    path = (
        Path(__file__).resolve().parents[1]
        / ".upstream/napari/src/napari/utils/events/evented_model.py"
    )
    if not path.exists():
        pytest.skip("Run tools/fetch_upstream.py for differential model tests")
    name = "_naparixx_baseline_evented_model"
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    yield module
    sys.modules.pop(name, None)


def model_type(base):
    class Model(base):
        x: int = Field(0, ge=0)
        y: int = 0
        model_config = ConfigDict(
            dependencies={"pair": ["x", "y"], "total": ["x", "y"]}
        )

        @property
        def pair(self) -> tuple[int, int]:
            return self.x, self.y

        @pair.setter
        def pair(self, value):
            self.x, self.y = value

        @property
        def total(self) -> int:
            return self.x + self.y

    return Model


def observe(model, reenter=False):
    trace = []
    callbacks = []
    for name in ("x", "y", "pair", "total"):

        def callback(event, name=name):
            trace.append((name, event.value, model.x, model.y))
            if reenter and name == "x" and event.value == 7:
                model.y = 17

        callbacks.append(callback)
        getattr(model.events, name).connect(callback)
    callback = lambda event: trace.append(("group", event.type, event.value))  # noqa: E731
    callbacks.append(callback)
    model.events.connect(callback)
    return trace, callbacks


@pytest.mark.parametrize("seed", range(5))
def test_nested_model_transitions_match_baseline(baseline, seed):
    reference = model_type(baseline.EventedModel)()
    actual = model_type(EventedModel)()
    traces = [observe(model) for model in (reference, actual)]
    rng = np.random.default_rng(seed)
    for _ in range(40):
        field = rng.choice(["x", "y", "pair"])
        value = tuple(int(v) for v in rng.integers(0, 8, 2))
        if field != "pair":
            value = value[0]
        for model in (reference, actual):
            setattr(model, field, value)
        assert actual.model_dump() == reference.model_dump()
        assert traces[0][0] == traces[1][0]
        assert actual._changes_queue == reference._changes_queue == {}
        assert actual._delay_check_semaphore == reference._delay_check_semaphore == 0
        assert actual._primary_changes == reference._primary_changes == {}


def test_callback_reentry_matches_baseline(baseline):
    models = (model_type(baseline.EventedModel)(), model_type(EventedModel)())
    traces = [observe(model, reenter=True) for model in models]
    for model in models:
        model.pair = (7, 2)
    assert traces[0][0] == traces[1][0]
    assert models[0].model_dump() == models[1].model_dump() == {"x": 7, "y": 17}


def test_partial_setter_failure_retains_events_and_recovers(baseline):
    models = (model_type(baseline.EventedModel)(), model_type(EventedModel)())
    traces = [observe(model) for model in models]
    for model in models:
        with pytest.raises(ValidationError):
            model.pair = (3, "invalid")
        assert model.pair == (3, 0)
        assert model._changes_queue == {}
        model.pair = (4, 2)
    assert traces[0][0] == traces[1][0]


def test_failed_equality_after_primary_comparison_recovers(baseline):
    snapshots = []
    for base, delayer in (
        (baseline.EventedModel, baseline.ComparisonDelayer),
        (EventedModel, ComparisonDelayer),
    ):
        model = model_type(base)()
        trace, callbacks = observe(model)
        original = model.__eq_operators__["y"]

        def bad_comparison(*args):
            raise RuntimeError("comparison failed")

        model.__eq_operators__["y"] = bad_comparison
        with pytest.raises(RuntimeError, match="comparison failed"):
            with delayer(model):
                model.x = 2
                model.y = 3
        snapshots.append((dict(model._changes_queue), dict(model._primary_changes)))
        model.__eq_operators__["y"] = original
        model.y = 4
        snapshots.append((model.model_dump(), trace, model._changes_queue))
        assert callbacks
    assert snapshots[0] == snapshots[2]
    assert snapshots[1] == snapshots[3]


def test_journal_copying_preserves_queue_sharing_and_independent_depth(baseline):
    for base, delayer in (
        (baseline.EventedModel, baseline.ComparisonDelayer),
        (EventedModel, ComparisonDelayer),
    ):
        model = model_type(base)()
        trace, callbacks = observe(model)
        with delayer(model):
            model.x = 2
            primary = model._primary_changes
            shallow = copy.copy(model)
            deep = copy.deepcopy(model)
            assert shallow._delay_check_semaphore == deep._delay_check_semaphore == 1
            assert shallow._changes_queue is model._changes_queue
            assert deep._changes_queue is not model._changes_queue
            assert shallow._primary_changes is primary
            assert deep._primary_changes is not primary
            assert dict(deep._primary_changes) == dict(primary)
            with delayer(shallow):
                assert shallow._delay_check_semaphore == 2
                assert model._delay_check_semaphore == 1
            assert callbacks
        assert trace and not shallow._changes_queue and deep._changes_queue
        assert shallow._delay_check_semaphore == deep._delay_check_semaphore == 1


def test_private_primary_map_mutations_reach_native_state():
    model = model_type(EventedModel)()
    mapping = model._primary_changes
    mapping["x"] = None
    mapping["y"] = None
    assert list(mapping) == ["x", "y"]
    assert list(mapping.items()) == [("x", None), ("y", None)]
    assert mapping.get("x", 123) is None
    assert mapping.get("unknown", 123) == 123
    assert model._change_state.primary_names == ["x", "y"]
    assert mapping.pop("x") is None
    assert mapping.pop("unknown", 123) == 123
    del mapping["y"]
    with pytest.raises(KeyError):
        mapping.pop("unknown")
    mapping["x"] = None
    mapping.clear()
    assert mapping == {} and model._change_state.primary_names == []


def test_plugin_protected_method_overrides_are_called():
    calls = []

    class PluginModel(EventedModel):
        value: int = 0

        def _setattr_impl(self, name, value):
            calls.append(("set", name, value))
            super()._setattr_impl(name, value)

        def _super_setattr_(self, name, value):
            calls.append(("super", name, value))
            super()._super_setattr_(name, value)

        def _check_if_differ(self, name, old_value):
            calls.append(("compare", name, old_value))
            return super()._check_if_differ(name, old_value)

    model = PluginModel()
    callback = lambda event: None  # noqa: E731
    model.events.value.connect(callback)
    model.value = 2
    assert calls == [
        ("set", "value", 2),
        ("super", "value", 2),
        ("compare", "value", 0),
    ]


def test_plugin_mixin_assignment_hook_is_preserved(baseline):
    def scenario(base):
        calls = []

        class Mixin(BaseModel):
            def __setattr__(self, name, value):
                calls.append((name, value))
                super().__setattr__(name, value)

        class PluginModel(base, Mixin):
            value: int = 0

        model = PluginModel()
        model.value = 2
        return calls

    expected = scenario(baseline.EventedModel)
    assert expected == [
        ("_delay_check_semaphore", 1),
        ("value", 2),
        ("_delay_check_semaphore", 0),
    ]
    assert scenario(EventedModel) == expected


def test_native_journal_type_cycles_are_collected():
    class CustomJournal(native.ChangeJournal):
        pass

    state = CustomJournal()
    CustomJournal.default = state
    state_ref = weakref.ref(state)
    del state, CustomJournal
    gc.collect()
    assert state_ref() is None


def test_unobserved_values_skip_comparison():
    class Model(EventedModel):
        value: int = 0

        def _check_if_differ(self, name, old_value):
            raise AssertionError("unobserved values must not be compared")

    model = Model()
    model.value = 1
    assert model._primary_changes == {} and model._change_state.pending_names == []


def test_nullable_fields_and_values_use_normal_comparison():
    class Model(EventedModel):
        value: object = None

    model = Model()
    changes = []
    callback = lambda event: changes.append(event.value)  # noqa: E731
    model.events.value.connect(callback)
    model.value = 3
    model.value = None
    model.value = None
    assert changes == [3, None]
    assert pick_equality_operator(None) is operator.eq


def test_queued_python_values_are_collected_after_callbacks():
    class Value:
        pass

    class Model(EventedModel):
        value: object

    old = Value()
    model = Model(value=old)
    callback = lambda event: None  # noqa: E731
    model.events.value.connect(callback)
    old_ref = weakref.ref(old)
    journal_ref = weakref.ref(model._change_state)
    del old
    with ComparisonDelayer(model):
        model.value = Value()
        assert old_ref() is not None
    gc.collect()
    assert old_ref() is None
    del model, callback
    gc.collect()
    assert journal_ref() is None


def test_array_comparator_type_identity_and_keyword_options():
    class ArraySubclass(np.ndarray):
        pass

    assert pick_equality_operator(int) is operator.eq
    assert pick_equality_operator(np.ndarray) is _quiet_array_equal
    assert pick_equality_operator(np.ones(2).view(ArraySubclass)) is _quiet_array_equal
    assert native.quiet_array_equal([np.nan], [np.nan], equal_nan=True)
    assert not native.quiet_array_equal([np.nan], [np.nan])
