"""Native component state/view consistency against pinned camera/grid sources."""

import copy
import gc
import importlib.util
import inspect
import json
import pickle
import subprocess
import sys
import sysconfig
import weakref
from pathlib import Path
from types import SimpleNamespace
from typing import get_type_hints

import numpy as np
import pytest

from napari.components import Camera
from napari.components.grid import GridCanvas

ROOT = Path(__file__).resolve().parents[1]
LAYERS = [SimpleNamespace(visible=value) for value in (True, False, True, True, True)]


@pytest.fixture(scope="module")
def originals():
    result = {}
    for name in ("camera", "grid"):
        module_name = "_component_state_original_" + name
        path = ROOT / ".upstream/napari/src/napari/components" / (name + ".py")
        spec = importlib.util.spec_from_file_location(module_name, path)
        module = importlib.util.module_from_spec(spec)
        sys.modules[module_name] = module
        spec.loader.exec_module(module)
        result[name] = module.Camera if name == "camera" else module.GridCanvas
    yield result
    for name in result:
        sys.modules.pop("_component_state_original_" + name, None)


def payload(kind):
    if kind == "camera":
        return {
            "center": (3.0, 4.0, 5.0),
            "zoom": 2.5,
            "angles": (10.0, 70.0, 35.0),
            "perspective": 45.0,
            "mouse_pan": False,
            "mouse_zoom": False,
            "orientation": ("away", "up", "left"),
            "synced": False,
        }
    return {"stride": -2, "shape": (1, 3), "enabled": True, "spacing": 0.1}


def assert_behavior(actual, reference, kind):
    assert actual.model_dump() == reference.model_dump()
    if kind == "camera":
        np.testing.assert_allclose(
            actual.view_direction, reference.view_direction, atol=1e-14
        )
        np.testing.assert_allclose(
            actual.up_direction, reference.up_direction, atol=1e-14
        )
        np.testing.assert_allclose(
            actual.calculate_nd_view_direction(5, (1, 3, 4)),
            reference.calculate_nd_view_direction(5, (1, 3, 4)),
            atol=1e-14,
        )
        assert actual.handedness == reference.handedness
        for model in (actual, reference):
            model._cache_state(2)
        first, second = actual._pop_cached_state(2), reference._pop_cached_state(2)
        assert (first.center, first.zoom, first.angles) == (
            second.center,
            second.zoom,
            second.angles,
        )
        assert actual._native_state.center == actual.center
        assert actual._native_state.angles == actual.angles
        assert actual._native_state.zoom == actual.zoom
    else:
        assert actual.actual_shape(LAYERS) == reference.actual_shape(LAYERS)
        assert [actual.position(i, LAYERS) for i in range(5)] == [
            reference.position(i, LAYERS) for i in range(5)
        ]
        assert actual.contents_at((0, 0), LAYERS) == reference.contents_at(
            (0, 0), LAYERS
        )
        assert list(actual.iter_viewboxes(LAYERS)) == list(
            reference.iter_viewboxes(LAYERS)
        )
        assert actual._compute_canvas_spacing(
            (500, 400), LAYERS
        ) == reference._compute_canvas_spacing((500, 400), LAYERS)


def restore_model_state(model):
    """Exercise supported model-state restore and native-state pickle together."""
    saved = model.__getstate__()
    saved["__dict__"] = dict(saved["__dict__"])
    private = saved["__pydantic_private__"] = dict(saved["__pydantic_private__"])
    private["_change_state"] = copy.copy(private["_change_state"])
    if "_native_state" in private:
        private["_native_state"] = pickle.loads(pickle.dumps(private["_native_state"]))
    result = type(model).__new__(type(model))
    result.__setstate__(saved)
    return result


@pytest.mark.parametrize("kind", ("camera", "grid"))
@pytest.mark.parametrize(
    "operation",
    (
        "item",
        "update",
        "dict_update",
        "ior",
        "replace",
        "generic_replace",
        "clear_clone",
    ),
)
def test_live_dictionary_mutation_matches_pinned_calculations(
    originals, kind, operation
):
    actual = Camera() if kind == "camera" else GridCanvas()
    reference = originals[kind]()
    traces = [[], []]
    data = payload(kind)
    for model, trace in zip((actual, reference), traces, strict=True):
        model.events.connect(lambda event, trace=trace: trace.append(event.type))
        fields = vars(model)
        if operation == "item":
            for name, value in data.items():
                fields[name] = value
        elif operation == "update":
            fields.update(data)
        elif operation == "dict_update":
            dict.update(fields, data)
        elif operation == "ior":
            fields |= data
        elif operation == "replace":
            model.__dict__ = dict(fields, **data)
        elif operation == "generic_replace":
            object.__setattr__(model, "__dict__", dict(fields, **data))
        else:
            saved = dict(fields, **data)
            fields.clear()
            dict.update(fields, saved)
        if operation not in {"replace", "generic_replace"}:
            assert vars(model) is fields
        assert type(vars(model)) is dict
        assert all(vars(model)[name] is value for name, value in data.items())
    assert_behavior(actual, reference, kind)
    assert traces == [[], []]
    assert actual._native_state._model_dictionary_record.dictionary is vars(actual)


@pytest.mark.parametrize("kind", ("camera", "grid"))
@pytest.mark.parametrize(
    "clone",
    (
        copy.copy,
        copy.deepcopy,
        lambda model: model.model_copy(),
        lambda model: model.model_copy(deep=True),
        restore_model_state,
    ),
)
def test_copied_and_restored_models_have_independent_native_owners(
    originals, kind, clone
):
    cls = Camera if kind == "camera" else GridCanvas
    source, reference = cls(), originals[kind]()
    vars(source).update(payload(kind))
    vars(reference).update(payload(kind))
    copied, expected = clone(source), clone(reference)
    assert copied._native_state is not source._native_state
    assert (
        copied._native_state._model_dictionary_record
        is not source._native_state._model_dictionary_record
    )
    assert copied._native_state._model_dictionary_record.dictionary is vars(copied)
    assert_behavior(copied, expected, kind)
    key, changed = (
        ("angles", (0.0, 90.0, 0.0)) if kind == "camera" else ("shape", (3, 1))
    )
    vars(copied)[key] = changed
    vars(expected)[key] = changed
    assert_behavior(copied, expected, kind)
    assert vars(source)[key] != changed


@pytest.mark.parametrize("kind", ("camera", "grid"))
def test_trusted_construction_attaches_current_fields_without_validation(
    originals, kind
):
    cls = Camera if kind == "camera" else GridCanvas
    data = payload(kind)
    actual, reference = (
        cls.model_construct(**data),
        originals[kind].model_construct(**data),
    )
    assert all(vars(actual)[name] is value for name, value in data.items())
    assert_behavior(actual, reference, kind)


@pytest.mark.parametrize("kind", ("camera", "grid"))
def test_native_update_changes_public_views_without_emitting_events(kind):
    model = Camera() if kind == "camera" else GridCanvas()
    fields, state = vars(model), model._native_state
    trace = []
    model.events.connect(lambda event: trace.append(event.type))
    if kind == "camera":
        state.update(
            (4.0, 5.0, 6.0),
            3.0,
            (0.0, 90.0, 0.0),
            40.0,
            False,
            False,
            ("away", "up", "left"),
            False,
        )
        assert model.center == state.center == (4.0, 5.0, 6.0)
        assert model.zoom == state.zoom == 3.0
        assert tuple(map(str, model.orientation)) == ("away", "up", "left")
        assert all(hasattr(value, "value") for value in model.orientation)
    else:
        state.update(-2, (1, 3), True, 0.1)
        assert model.model_dump() == payload("grid")
        assert model.actual_shape(LAYERS) == (1, 3)
    assert fields is vars(model)
    assert trace == []


@pytest.mark.parametrize("kind", ("camera", "grid"))
def test_restored_dictionary_aliases_update_separate_native_owners(originals, kind):
    cls = Camera if kind == "camera" else GridCanvas
    source, reference = cls(), originals[kind]()
    saved, expected_state = source.__getstate__(), reference.__getstate__()
    restored, expected = cls(), originals[kind]()
    # Restore an independent native owner while retaining the real dict alias.
    saved["__pydantic_private__"] = copy.deepcopy(saved["__pydantic_private__"])
    restored.__setstate__(saved)
    expected.__setstate__(expected_state)
    assert vars(source) is vars(restored)
    assert source._native_state is not restored._native_state
    vars(source).update(payload(kind))
    vars(reference).update(payload(kind))
    assert_behavior(source, reference, kind)
    assert_behavior(restored, expected, kind)


@pytest.mark.parametrize("kind", ("camera", "grid"))
def test_native_owner_adapter_and_provider_cycles_are_visible_to_gc(kind):
    model = Camera() if kind == "camera" else GridCanvas()
    state = model._native_state
    record = state._model_dictionary_record
    vars(model)["provider_cycle"] = model
    refs = [weakref.ref(value) for value in (model, state, record)]
    del model, state, record
    gc.collect()
    assert all(ref() is None for ref in refs)


@pytest.mark.parametrize("kind", ("camera", "grid"))
@pytest.mark.parametrize("required", (False, True))
def test_deleted_fields_are_required_only_by_consuming_operations(
    originals, kind, required
):
    cls = Camera if kind == "camera" else GridCanvas
    actual, reference = cls(), originals[kind]()
    field = (
        ("angles" if required else "zoom")
        if kind == "camera"
        else ("shape" if required else "spacing")
    )
    if kind == "grid":
        actual.enabled = reference.enabled = True
    for model in (actual, reference):
        del vars(model)[field]
    if required:
        for model in (actual, reference):
            with pytest.raises(AttributeError, match=field):
                if kind == "camera":
                    _ = model.view_direction
                else:
                    model.actual_shape(LAYERS)
    elif kind == "camera":
        np.testing.assert_allclose(
            actual.view_direction, reference.view_direction, atol=1e-14
        )
    else:
        assert actual.actual_shape(LAYERS) == reference.actual_shape(LAYERS)
    # State restoration preserves deleted keys without requiring their values.
    copied = restore_model_state(actual)
    assert field not in vars(copied)


@pytest.mark.parametrize("kind", ("camera", "grid"))
def test_retained_native_owner_remains_usable_after_facade_collection(kind):
    model = Camera() if kind == "camera" else GridCanvas()
    state, fields = model._native_state, vars(model)
    ref = weakref.ref(model)
    del model
    gc.collect()
    assert ref() is None
    if kind == "camera":
        fields["angles"] = (0.0, 90.0, 0.0)
        assert state.angles == (0.0, 90.0, 0.0)
    else:
        fields["enabled"] = True
        assert state.layout((True, True, True)).shape == (2, 2)


@pytest.mark.parametrize("kind", ("camera", "grid"))
def test_primitive_views_and_native_owner_agree_after_direct_writes(kind):
    model = Camera() if kind == "camera" else GridCanvas()
    values = payload(kind)
    vars(model).update(values)
    for key, expected in values.items():
        assert getattr(model._native_state, key) == expected


@pytest.mark.parametrize("kind", ("camera", "grid"))
def test_shared_dictionary_native_update_reaches_every_bound_owner(kind):
    cls = Camera if kind == "camera" else GridCanvas
    source, restored = cls(), cls()
    saved = source.__getstate__()
    saved["__pydantic_private__"] = copy.deepcopy(saved["__pydantic_private__"])
    restored.__setstate__(saved)
    assert vars(source) is vars(restored)
    assert source._native_state is not restored._native_state
    if kind == "camera":
        source._native_state.update(
            (4.0, 5.0, 6.0),
            3.0,
            (0.0, 90.0, 0.0),
            40.0,
            False,
            False,
            ("away", "up", "left"),
            False,
        )
        assert restored._native_state.center == source._native_state.center
        assert restored._native_state.angles == source._native_state.angles
    else:
        source._native_state.update(-2, (1, 3), True, 0.1)
        assert restored._native_state.shape == source._native_state.shape
        assert restored.actual_shape(LAYERS) == source.actual_shape(LAYERS)


@pytest.fixture(scope="module")
def original_pickle_errors():
    result = subprocess.run(
        [
            sys.executable,
            "-S",
            str(ROOT / "tests/_reference/component_pickle_probe.py"),
            sysconfig.get_paths()["purelib"],
        ],
        cwd=ROOT,
        capture_output=True,
        text=True,
    )
    assert result.returncode == 0, result.stderr
    return json.loads(result.stdout)


@pytest.mark.parametrize("kind", ("camera", "grid"))
def test_full_model_pickle_failure_matches_independent_upstream(
    original_pickle_errors, kind
):
    # Original event WeakKeyDictionary callbacks are not picklable. Confirm this
    # with the fully original hierarchy, rather than weakening a restore oracle.
    model = Camera() if kind == "camera" else GridCanvas()
    try:
        pickle.dumps(model)
    except Exception as error:
        actual = [type(error).__name__, str(error)]
    else:
        actual = None
    assert actual == original_pickle_errors[kind]


def test_native_boundary_consistency_when_dictionary_watchers_are_unavailable():
    result = subprocess.run(
        [
            sys.executable,
            str(ROOT / "tests/_reference/component_watcher_exhaustion.py"),
        ],
        cwd=ROOT,
        capture_output=True,
        text=True,
    )
    assert result.returncode == 0, result.stdout + result.stderr
    assert "without dict watchers" in result.stdout
    assert "nanobind: leaked" not in result.stderr


@pytest.mark.parametrize("kind", ("camera", "grid"))
def test_component_hook_signatures_docs_and_type_hints_match_pinned(originals, kind):
    def hints(function):
        try:
            return get_type_hints(function)
        except NameError as error:
            return type(error).__name__, str(error)

    cls = Camera if kind == "camera" else GridCanvas
    for name in ("model_post_init", "__deepcopy__", "__setstate__"):
        actual, expected = getattr(cls, name), getattr(originals[kind], name)
        assert inspect.signature(actual) == inspect.signature(expected)
        assert actual.__doc__ == expected.__doc__
        assert hints(actual) == hints(expected)
