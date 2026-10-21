"""Application contracts: native state, events, copying and upstream parity."""

import copy
import importlib.util
import pickle
import sys
from pathlib import Path
from types import SimpleNamespace

import numpy as np
import pytest
from pydantic import ValidationError

import naparixx
from napari.components import Camera, Dims, ViewerModel
from napari.components.canvas import Canvas
from napari.components.grid import GridCanvas
from naparixx import native

ROOT = Path(__file__).resolve().parents[1]


@pytest.fixture(scope="module")
def baseline():
    result = {}
    for name in ("dims", "camera", "grid", "canvas"):
        path = ROOT / f".upstream/napari/src/napari/components/{name}.py"
        if not path.exists():
            pytest.skip("Run tools/fetch_upstream.py for differential model tests")
        module_name = f"_naparixx_baseline_{name}"
        spec = importlib.util.spec_from_file_location(module_name, path)
        module = importlib.util.module_from_spec(spec)
        sys.modules[module_name] = module
        spec.loader.exec_module(module)
        result[name] = module
    yield result
    for name in result:
        sys.modules.pop(f"_naparixx_baseline_{name}", None)


def test_plain_napari_uses_native_application_models():
    # The numerical verification switch does not change application ownership.
    assert not naparixx.backend_status()["enabled"]
    assert isinstance(Dims()._native_state, native.DimsState)
    assert isinstance(Camera()._native_state, native.CameraState)
    assert isinstance(GridCanvas()._native_state, native.GridState)
    import napari

    assert napari.__version__ == "0.9.2+naparixx.2"


@pytest.mark.parametrize("cls", [Dims, Camera, GridCanvas])
def test_model_serialization_and_copy(cls):
    model = cls()
    for clone in (
        copy.copy(model),
        copy.deepcopy(model),
        model.model_copy(),
        model.model_copy(deep=True),
        cls.model_validate_json(model.model_dump_json()),
    ):
        assert clone.model_dump() == model.model_dump()
        assert clone._native_state is not model._native_state
        assert "_native_state" not in clone.model_dump()
        if cls is Dims:
            clone.set_point(0, 1)
            assert clone.current_step == (1, 0)
            assert model.current_step == (0, 0)
        elif cls is Camera:
            clone.angles = (0, 90, 0)
            np.testing.assert_allclose(clone.view_direction, (0, 0, -1), atol=1e-15)
            assert model.view_direction == (-1, 0, 0)
        else:
            clone.enabled = True
            layers = [SimpleNamespace(visible=True)] * 3
            assert clone.actual_shape(layers) == (2, 2)
            assert model.actual_shape(layers) == (1, 1)


def test_model_copy_updates_are_trusted_without_assignment_validation():
    dims = Dims().model_copy(update={"point": (99.0, -99.0)})
    assert dims.point == (99.0, -99.0)
    assert dims.current_step == (99, -99)
    camera = Camera().model_copy(update={"angles": (0.0, 90.0, 0.0)})
    np.testing.assert_allclose(camera.view_direction, (0, 0, -1), atol=1e-15)
    grid = GridCanvas().model_copy(update={"enabled": True})
    assert grid.actual_shape([SimpleNamespace(visible=True)] * 3) == (2, 2)


@pytest.mark.parametrize("nsteps", [1, 0, -1])
def test_invalid_nsteps_retains_upstream_exception_type(baseline, nsteps):
    reference = baseline["dims"].Dims()
    actual = Dims()
    with pytest.raises(Exception) as expected:
        reference.nsteps = (nsteps, 3)
    with pytest.raises(type(expected.value)):
        actual.nsteps = (nsteps, 3)
    assert actual.range == reference.range
    # ExceptionInfo retains the traceback and this frame's native models.
    del expected


def test_camera_cache_survives_copy_and_native_state_pickle():
    camera = Camera(center=(1, 2, 3), zoom=2, angles=(10, 20, 30))
    camera._cache_state(2)
    camera.center = (4, 5, 6)
    camera.zoom = 4
    camera._cache_state(3)
    clones = (
        (copy.copy(camera), "_pop_cached_state"),
        (copy.deepcopy(camera), "_pop_cached_state"),
        (pickle.loads(pickle.dumps(camera._native_state)), "pop_cache"),
    )
    for clone, method in clones:
        first = getattr(clone, method)(2)
        second = getattr(clone, method)(3)
        assert first.center == (1, 2, 3) and first.zoom == 2
        assert second.center == (4, 5, 6) and second.zoom == 4
        assert getattr(clone, method)(2) is None
    assert camera._pop_cached_state(2).center == (1, 2, 3)


@pytest.mark.parametrize("cls", [Dims, Camera, GridCanvas])
def test_native_state_pickle_roundtrip(cls):
    state = cls()._native_state
    clone = pickle.loads(pickle.dumps(state))
    assert type(clone) is type(state) and clone is not state


@pytest.mark.parametrize("seed", range(5))
def test_dims_transitions_and_events_match_baseline(baseline, seed):
    reference = baseline["dims"].Dims(ndim=5)
    model = Dims(ndim=5)
    traces = [[], []]
    for obj, trace in zip((reference, model), traces, strict=True):
        obj.events.connect(lambda event, _trace=trace: _trace.append(event.type))
    rng = np.random.default_rng(seed)
    properties = (
        "nsteps",
        "current_step",
        "displayed",
        "not_displayed",
        "displayed_order",
        "margin_left_step",
        "margin_right_step",
        "thickness",
        "thickness_step",
    )
    operations = [
        ("range", ((-10, 10, 0.5),) * 5),
        ("point", tuple(rng.uniform(-20, 20, 5))),
        ("order", tuple(map(int, rng.permutation(5)))),
        ("margin_left", tuple(rng.uniform(0, 5, 5))),
        ("margin_right", tuple(rng.uniform(0, 5, 5))),
        ("ndisplay", 3),
        ("ndim", 7),
        ("ndim", 4),
    ]
    for name, value in operations:
        for obj in (reference, model):
            setattr(obj, name, value)
        assert model.model_dump() == reference.model_dump()
        for prop in properties:
            assert getattr(model, prop) == getattr(reference, prop), prop
        assert traces[0] == traces[1], (name, traces)
    for name, args in (
        ("set_point", ((-1, 0), (9, -3))),
        ("set_current_step", ((-1, 0), (2, 4))),
        ("roll", ()),
        ("transpose", ()),
        ("_focus_down", ()),
        ("_focus_up", ()),
        ("_increment_dims_right", ()),
        ("_increment_dims_left", ()),
        ("_go_to_center_step", ()),
        ("reset", ()),
    ):
        for obj in (reference, model):
            getattr(obj, name)(*args)
        assert model.model_dump() == reference.model_dump(), name
        assert traces[0] == traces[1], (name, traces)


def test_dims_invalid_assignment_recovers_native_state(baseline):
    for cls in (baseline["dims"].Dims, Dims):
        dims = cls(ndim=4)
        with pytest.raises(ValidationError, match="Invalid ordering"):
            dims.order = (0, 0, 1, 2)
        dims.order = (3, 2, 1, 0)
        assert dims.displayed == (1, 0)
        dims.set_current_step(-1, 1)
        assert dims.current_step == (0, 0, 0, 1)


@pytest.mark.parametrize(
    "orientation",
    [
        (depth, vertical, horizontal)
        for depth in ("towards", "away")
        for vertical in ("down", "up")
        for horizontal in ("right", "left")
    ],
)
def test_camera_rotation_and_inverse_match_baseline(baseline, orientation):
    reference = baseline["camera"].Camera(orientation=orientation)
    model = Camera(orientation=orientation)
    angles = np.random.default_rng(12).uniform(-180, 180, (40, 3))
    angles = np.concatenate([angles, [(20, 90, 30), (20, -90, 30), (180, 0, 180)]])
    for angle in angles:
        reference.angles = model.angles = tuple(angle)
        np.testing.assert_allclose(
            model.view_direction, reference.view_direction, atol=1e-14
        )
        np.testing.assert_allclose(
            model.up_direction, reference.up_direction, atol=1e-14
        )
        model.set_view_direction(reference.view_direction, reference.up_direction)
        np.testing.assert_allclose(
            model.view_direction, reference.view_direction, atol=1e-7
        )
        np.testing.assert_allclose(
            model.up_direction, reference.up_direction, atol=1e-7
        )


def test_camera_computed_events_match_baseline(baseline):
    reference, model = baseline["camera"].Camera(), Camera()
    assert model.__field_dependents__ == reference.__field_dependents__
    traces = [[], []]
    for obj, trace in zip((reference, model), traces, strict=True):
        for event in ("view_direction", "up_direction", "orientation2d", "handedness"):
            getattr(obj.events, event).connect(
                lambda e, _trace=trace: _trace.append(e.type)
            )
    for key, value in (
        ("angles", (10, 20, 30)),
        ("orientation", ("away", "up", "left")),
    ):
        for obj in (reference, model):
            setattr(obj, key, value)
        assert traces[0] == traces[1]


@pytest.mark.parametrize("stride", [-4, -2, -1, 1, 2, 4])
@pytest.mark.parametrize("shape", [(-1, -1), (2, -1), (-1, 3), (1, 2)])
def test_grid_layout_and_spacing_match_baseline(baseline, stride, shape):
    reference = baseline["grid"].GridCanvas(enabled=True, stride=stride, shape=shape)
    model = GridCanvas(enabled=True, stride=stride, shape=shape)
    rng = np.random.default_rng(1)
    for count in range(12):
        layers = [SimpleNamespace(visible=bool(v)) for v in rng.integers(0, 2, count)]
        assert model.actual_shape(layers) == reference.actual_shape(layers)
        assert model._viewbox_groups(layers) == reference._viewbox_groups(layers)
        assert list(model.iter_viewboxes(layers)) == list(
            reference.iter_viewboxes(layers)
        )
        for spacing in (0, 0.2, 1, 1000):
            reference.spacing = model.spacing = spacing
            for canvas in ((100, 250), (5, 10)):
                assert model._compute_canvas_spacing(
                    canvas, layers
                ) == reference._compute_canvas_spacing(canvas, layers)
                assert model._compute_canvas_spacing_raw(
                    canvas, layers
                ) == reference._compute_canvas_spacing_raw(canvas, layers)


@pytest.mark.parametrize("enabled", [False, True])
@pytest.mark.parametrize("shape", [(-1, -1), (1, 2), (2, 3)])
def test_canvas_viewbox_geometry_matches_baseline(baseline, enabled, shape):
    reference, model = baseline["canvas"].Canvas(), Canvas()
    layers = [SimpleNamespace(visible=True), SimpleNamespace(visible=False)] * 3
    for obj in (reference, model):
        obj.grid.enabled = enabled
        obj.grid.shape = shape
        obj.grid.spacing = 0.2
    for size in ((600, 800), (5, 10)):
        reference.size = model.size = size
        assert model.viewbox_size(layers) == reference.viewbox_size(layers)


def test_view_navigation_is_native_and_emits_reset_event(monkeypatch):
    calls = []
    original = native.fit_view

    def record(*args):
        calls.append(args)
        return original(*args)

    monkeypatch.setattr(native, "fit_view", record)
    viewer = ViewerModel()
    try:
        viewer.add_image(np.zeros((5, 10, 20)))
        events = []
        viewer.events.reset_view.connect(events.append)
        viewer.fit_to_view(margin=0.2)
        assert calls and len(events) == 1
        assert events[0].center == viewer.scene.camera.center
        assert events[0].zoom == viewer.scene.camera.zoom
        viewer.dims.set_point(0, 2)
        viewer.dims.ndisplay = 3
        assert viewer.scene.camera.center[0] == 2
    finally:
        viewer._layer_slicer.shutdown()


def test_cached_alias_dispatches_without_rebinding(monkeypatch):
    from napari.utils.geometry import project_points_onto_plane as cached

    calls = []
    original = native.project_points_onto_plane

    def record(*args):
        calls.append(True)
        return original(*args)

    monkeypatch.setattr(native, "project_points_onto_plane", record)
    args = (np.array([[1.0, 2.0, 3.0]]), np.zeros(3), np.array([0.0, 0.0, 1.0]))
    cached(*args)
    assert not calls
    naparixx.enable()
    cached(*args)
    assert len(calls) == 1
    naparixx.disable()
    cached(*args)
    assert len(calls) == 1
