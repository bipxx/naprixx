"""Grid application control compared with unchanged pinned Grid method bodies.

The imported EventedModel/dependency services are shared with the fork. These
comparisons establish Grid behavior, not an independent whole-app oracle.
"""

import copy
import gc
import importlib.util
import inspect
import pickle
import sys
import weakref
from pathlib import Path
from types import SimpleNamespace

import numpy as np
import pytest

from napari.components import grid as current_module
from napari.components.grid import GridCanvas
from naparixx import _native

ROOT = Path(__file__).resolve().parents[1]


@pytest.fixture(scope="module")
def original_module():
    name = "_grid_controller_pinned"
    source = ROOT / ".upstream/napari/src/napari/components/grid.py"
    spec = importlib.util.spec_from_file_location(name, source)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    yield module
    sys.modules.pop(name, None)


def layers(visibility):
    return [SimpleNamespace(visible=value) for value in visibility]


def outcome(function):
    try:
        return ("value", function())
    except BaseException as error:
        cause = type(error.__cause__).__name__ if error.__cause__ else None
        return (type(error).__name__, str(error), cause)


@pytest.mark.parametrize("enabled", [False, True])
@pytest.mark.parametrize("shape", [(-1, -1), (1, 2), (-1, 2), (2, -1)])
@pytest.mark.parametrize("stride", [-3, -1, 1, 2, -(2**63), 2**80, -(2**80)])
@pytest.mark.parametrize("visibility", [(), (False, False), (True, False, True, True)])
def test_grid_calculations(original_module, enabled, shape, stride, visibility):
    models = [
        kind(enabled=enabled, shape=shape, stride=stride, spacing=0.2)
        for kind in (GridCanvas, original_module.GridCanvas)
    ]
    source = layers(visibility)
    for name in ("actual_shape", "_effective_indices", "_viewbox_groups"):
        assert outcome(lambda name=name: getattr(models[0], name)(source)) == outcome(
            lambda name=name: getattr(models[1], name)(source)
        )
    for index in (-1, 0, len(source) - 1, len(source), 2**100):
        assert outcome(
            lambda index=index: models[0].position(index, source)
        ) == outcome(lambda index=index: models[1].position(index, source))
    for location in ((0, 0), (0, 1), (-1, -1), [0, 0]):
        assert models[0].contents_at(location, source) == models[1].contents_at(
            location, source
        )
    assert list(models[0].iter_viewboxes(source)) == list(
        models[1].iter_viewboxes(source)
    )
    for canvas in ((15, 11), np.array([301, 203])):
        for name in ("_compute_canvas_spacing", "_compute_canvas_spacing_raw"):
            assert outcome(
                lambda name=name, canvas=canvas: getattr(models[0], name)(
                    canvas, source
                )
            ) == outcome(
                lambda name=name, canvas=canvas: getattr(models[1], name)(
                    canvas, source
                )
            )


@pytest.mark.parametrize(
    "enabled,visibility", [(False, (True,)), (True, (False,)), (True, ())]
)
def test_trusted_zero_stride_short_circuit(original_module, enabled, visibility):
    source = layers(visibility)
    actual = GridCanvas.model_construct(enabled=enabled, stride=0)
    baseline = original_module.GridCanvas.model_construct(enabled=enabled, stride=0)
    assert actual.actual_shape(source) == baseline.actual_shape(source) == (1, 1)
    assert outcome(lambda: actual._viewbox_groups(source)) == outcome(
        lambda: baseline._viewbox_groups(source)
    )


def test_disabled_does_not_consult_visibility(original_module):
    class Layer:
        @property
        def visible(self):
            raise AssertionError("visibility should not be read")

    for kind in (GridCanvas, original_module.GridCanvas):
        model = kind(enabled=False)
        assert model.actual_shape([Layer()]) == (1, 1)
        assert model.position(2**80, [Layer()]) == (0, 0)
        assert model.contents_at((0, 0), [Layer()]) == (0,)


def test_visibility_changes_between_yields(original_module):
    observed = []
    for kind in (GridCanvas, original_module.GridCanvas):
        source = layers((True, True))
        iterator = kind(enabled=True, shape=(1, 2)).iter_viewboxes(source)
        first = next(iterator)
        source[1].visible = False
        second = iterator.send("ignored by a for-loop generator")
        observed.append(
            (first, second, outcome(lambda iterator=iterator: next(iterator)))
        )
    assert observed[0] == observed[1]
    assert observed[0][1] == ((0, 1), ())


@pytest.mark.parametrize(
    "hook",
    [
        "position",
        "actual_shape",
        "_effective_indices",
        "_viewbox_groups",
        "_compute_canvas_spacing_raw",
    ],
)
def test_subclass_hooks(original_module, hook):
    results = []
    for kind in (GridCanvas, original_module.GridCanvas):
        calls = []

        def override(self, *args, calls=calls):
            calls.append(hook)
            values = {
                "position": (0, 0),
                "actual_shape": (1, 2),
                "_effective_indices": [0],
                "_viewbox_groups": ({0: 0, 1: 0}, [0]),
                "_compute_canvas_spacing_raw": 7,
            }
            return values[hook]

        subclass = type("GridOverride", (kind,), {hook: override})
        model = subclass(enabled=True, shape=(1, 2))
        source = layers((True, True))
        results.append(
            (
                model.contents_at((0, 0), source),
                list(model.iter_viewboxes(source)),
                model._compute_canvas_spacing((300, 200), source),
                calls,
            )
        )
    assert results[0] == results[1]


@pytest.mark.parametrize(
    "stride",
    [2**80, -(2**80), 2**16000],
    ids=["wide-positive", "wide-negative", "unlimited-digits"],
)
def test_wide_native_state_copies_and_updates(stride):
    model = GridCanvas(enabled=True, stride=stride, shape=(2**80, 1))
    state = model._native_state
    assert state.stride == stride
    assert state.shape == (2**80, 1)
    assert model.actual_shape(layers((True, True))) == (2**80, 1)
    for cloned in (
        copy.copy(model),
        copy.deepcopy(model),
        model.model_copy(),
        model.model_copy(deep=True),
    ):
        assert cloned._native_state is not state
        assert cloned._native_state.stride == stride
        assert cloned._native_state.shape == (2**80, 1)
    native_copy = pickle.loads(pickle.dumps(state))
    assert native_copy.stride == stride
    assert native_copy.shape == state.shape
    model.__dict__["stride"] = -(2**100)
    assert state.stride == -(2**100)
    state.update(2, (1, 2), True, 0.0)
    assert model.stride == state.stride == 2
    assert model.shape == state.shape == (1, 2)


@pytest.mark.parametrize("started", [False, True])
@pytest.mark.parametrize("exception_type", [ValueError, StopIteration, GeneratorExit])
def test_viewboxes_throw(original_module, started, exception_type):
    values = []
    for kind in (GridCanvas, original_module.GridCanvas):
        # Throw mutates exception tracebacks. Each oracle must receive its own
        # instance; keeping one in pytest's module marker retains Grid frames.
        exception = (
            exception_type()
            if exception_type is GeneratorExit
            else exception_type("sentinel")
        )
        iterator = kind(enabled=True, shape=(1, 2)).iter_viewboxes(layers((True, True)))
        if started:
            next(iterator)
        values.append(
            (
                outcome(
                    lambda iterator=iterator, exception=exception: iterator.throw(
                        exception
                    )
                ),
                outcome(lambda iterator=iterator: next(iterator)),
            )
        )
    assert values[0] == values[1]


def test_generator_laziness_and_send(original_module):
    observations = []
    for kind in (GridCanvas, original_module.GridCanvas):
        calls = []

        class Override(kind):
            def actual_shape(self, source, calls=calls):
                calls.append("shape")
                return (1, 1)

        iterator = Override().iter_viewboxes([])
        before = list(calls)
        rejected = outcome(lambda iterator=iterator: iterator.send(3))
        after_rejection = list(calls)
        result = iterator.send(None)
        iterator.close()
        observations.append((before, rejected, after_rejection, result, calls))
    assert observations[0] == observations[1]
    assert observations[0][0] == observations[0][2] == []


def test_generator_provider_stop_iteration(original_module):
    observations = []
    for kind in (GridCanvas, original_module.GridCanvas):

        class Override(kind):
            def contents_at(self, location, source):
                raise StopIteration("provider")

        iterator = Override().iter_viewboxes([])
        observations.append(outcome(lambda iterator=iterator: next(iterator)))
    assert (
        observations[0]
        == observations[1]
        == ("RuntimeError", "generator raised StopIteration", "StopIteration")
    )


def test_generator_reentry(original_module):
    results = []
    for kind in (GridCanvas, original_module.GridCanvas):
        holder = {}

        class Override(kind):
            def actual_shape(self, source, holder=holder):
                next(holder["iterator"])

        holder["iterator"] = Override().iter_viewboxes([])
        results.append(outcome(lambda holder=holder: next(holder["iterator"])))
    assert results[0] == results[1]
    assert results[0][0] == "ValueError"


def test_generator_frame_gc_and_close():
    model = GridCanvas(enabled=True, shape=(1, 1))
    native = _native.grid_viewboxes(model, [])
    model_reference = weakref.ref(model)
    native_reference = weakref.ref(native)
    model.__dict__["iterator_cycle"] = native
    del model
    native.close()
    assert native.frame == {}
    gc.collect()
    assert model_reference() is None
    del native
    gc.collect()
    assert native_reference() is None


def test_failed_native_update_preserves_owner_and_views():
    model = GridCanvas(enabled=True, stride=2**80, shape=(1, 2), spacing=0.5)
    state = model._native_state
    previous = (state.stride, state.shape, state.enabled, state.spacing)
    with pytest.raises((TypeError, RuntimeError)):
        state.update(99, (None, None), False, 1.0)
    assert (state.stride, state.shape, state.enabled, state.spacing) == previous
    assert (model.stride, model.shape, model.enabled, model.spacing) == previous


def test_enumerate_provider(original_module, monkeypatch):
    results = []
    for module in (current_module, original_module):
        calls = []

        def enumerate_provider(source, calls=calls):
            calls.append(source)
            return [
                (11, SimpleNamespace(visible=True)),
                (12, SimpleNamespace(visible=False)),
            ]

        monkeypatch.setattr(module, "enumerate", enumerate_provider, raising=False)
        source = object()
        results.append(
            (
                module.GridCanvas()._effective_indices(source),
                len(calls),
                calls[0] is source,
            )
        )
    assert results[0] == results[1] == ([11], 1, True)


def test_ceil_capture_before_sqrt(original_module, monkeypatch):
    results = []
    for module in (current_module, original_module):
        calls = []
        provider = SimpleNamespace()

        def first_ceil(value, calls=calls):
            calls.append("captured ceil")
            return np.ceil(value)

        def later_ceil(value, calls=calls):
            calls.append("replacement ceil")
            return np.ceil(value)

        def sqrt(value, calls=calls, provider=provider):
            calls.append("sqrt")
            provider.ceil = later_ceil
            return np.sqrt(value)

        provider.ceil = first_ceil
        provider.sqrt = sqrt
        monkeypatch.setattr(module, "np", provider)
        results.append(
            (module.GridCanvas(enabled=True).actual_shape(layers((True, True))), calls)
        )
    assert (
        results[0]
        == results[1]
        == ((1, 2), ["sqrt", "captured ceil", "replacement ceil"])
    )


@pytest.mark.parametrize(
    "name",
    [
        "actual_shape",
        "position",
        "contents_at",
        "iter_viewboxes",
        "_compute_canvas_spacing",
        "_compute_canvas_spacing_raw",
        "_effective_indices",
        "_viewbox_groups",
    ],
)
def test_public_method_metadata(original_module, name):
    current = getattr(GridCanvas, name)
    original = getattr(original_module.GridCanvas, name)
    assert inspect.signature(current) == inspect.signature(original)
    assert current.__doc__ == original.__doc__
    assert current.__annotations__ == original.__annotations__
