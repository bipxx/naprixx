"""Source-reachable RF1/RF2 cleanup and RF3 compiled keyword-key identity."""

# Every callback completes within one source-family iteration.
# ruff: noqa: B023
from __future__ import annotations

import hashlib
import importlib.util
import sys
import weakref
from functools import partial
from pathlib import Path
from types import SimpleNamespace

import pytest

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / ".artifacts/test_viewer_reset_fit_trace_storage_correction.py"
BASE_SHA = "399eb3e570f173b8086527d09383c5d0973d90cf35aae71c63c5a38e6ef15c67"


@pytest.fixture(scope="module")
def regression_families():
    assert hashlib.sha256(BASE.read_bytes()).hexdigest() == BASE_SHA
    name = "_naparixx_reset_fit_regression_base"
    assert name not in sys.modules
    spec = importlib.util.spec_from_file_location(name, BASE)
    assert spec is not None and spec.loader is not None
    base = importlib.util.module_from_spec(spec)
    sys.modules[name] = base
    generator = None
    try:
        spec.loader.exec_module(base)
        builder = base._original_module

        def isolated_builder(name, *args, **kwargs):
            return builder(name + "_review_regressions", *args, **kwargs)

        base._original_module = isolated_builder
        generator = base.families.__wrapped__()
        yield base, next(generator)
    finally:
        if generator is not None:
            generator.close()
        sys.modules.pop(name, None)


@pytest.mark.parametrize("mode", ["return", "raise"])
def test_source_non_python_call_releases_callable_before_arguments(
    regression_families, mode
):
    _base, families = regression_families
    for family in families:
        released = []
        references = {}
        calls = []
        output = object()

        class Operand:
            def __init__(self, label):
                self.label = label
                references[label] = weakref.ref(self)

            def __del__(self):
                released.append(self.label)

        def consume(size, layout):
            calls.append(
                (size is references["size"](), layout is references["layout"]())
            )
            # Remove the Python callback frame's parameter roots. The genuine
            # C partial and source CALL operands remain their owning authorities.
            del size, layout
            if mode == "raise":
                raise RuntimeError("completed partial call failed")
            return output

        class State:
            @property
            def viewbox_size(self):
                function = partial(consume)
                references["callable"] = weakref.ref(
                    function, lambda _: released.append("callable")
                )
                return function

        class Grid:
            _native_state = State()

            def _layout(self, layers):
                return Operand("layout")

        class Canvas:
            grid = Grid()

            @property
            def size(self):
                return Operand("size")

        if mode == "raise":
            with pytest.raises(
                RuntimeError, match="completed partial call failed"
            ) as error:
                family.canvas_method(Canvas(), None)
            assert error.value.__traceback__ is not None
        else:
            assert family.canvas_method(Canvas(), None) is output
        assert calls == [(True, True)]
        assert released == ["callable", "size", "layout"]
        assert all(reference() is None for reference in references.values())


class NativeProxy:
    def __init__(self, actual, fit_view):
        self.actual = actual
        self.controlled_fit = fit_view

    @property
    def fit_view(self):
        return self.controlled_fit

    def __getattr__(self, name):
        # Only genuine installed exports are forwarded; the protocol overrides
        # the same existing numerical primitive as the frozen main cohort.
        return getattr(self.actual, name)


@pytest.mark.parametrize("mode", ["store_ok_then_zoom_error", "store_error"])
def test_source_store_attr_releases_rhs_before_receiver(
    regression_families, monkeypatch, mode
):
    _base, families = regression_families
    for family in families:
        released = []
        references = {}
        setters = []
        input_camera = SimpleNamespace(view_direction=(0, 0, 1), up_direction=(0, 1, 0))

        class Value:
            def __init__(self):
                references["value"] = weakref.ref(self)

            def __del__(self):
                released.append("value")

        class Receiver:
            def __init__(self):
                references["receiver"] = weakref.ref(self)

            def __setattr__(self, name, value):
                setters.append((name, value is references["value"]()))
                # A non-owning setter; no saved error frame retains either
                # expression operand after the callback completes or raises.
                del value, self
                if mode == "store_error":
                    raise RuntimeError("non-owning center setter failed")

            def __del__(self):
                released.append("receiver")

        class Scene:
            target = False

            @property
            def camera(self):
                return Receiver() if self.target else input_camera

        scene = Scene()

        class Result:
            @property
            def center(self):
                scene.target = True
                return Value()

            @property
            def zoom(self):
                raise RuntimeError("after center store")

        result = Result()

        def fit_view(*args):
            return result

        viewer = SimpleNamespace(
            _get_scene_parameters=lambda **kwargs: (None, (4, 6), (0, 0)),
            dims=SimpleNamespace(ndisplay=2),
            scene=scene,
            layers=(),
            canvas=SimpleNamespace(
                viewbox_size=lambda layers: (800, 600), size=(800, 600)
            ),
        )
        expected_error = (
            "non-owning center setter failed"
            if mode == "store_error"
            else "after center store"
        )
        actual_native = family.namespace["_native"]
        with monkeypatch.context() as patch:
            patch.setitem(
                family.namespace, "_native", NativeProxy(actual_native, fit_view)
            )
            with pytest.raises(RuntimeError, match=expected_error) as error:
                family.methods["fit_to_view"](viewer)
        assert error.value.__traceback__ is not None
        assert setters == [("center", True)]
        assert released == ["value", "receiver"]
        assert all(reference() is None for reference in references.values())
        assert family.namespace["_native"] is actual_native


def test_source_repeated_compiled_keyword_keys_are_identical(
    regression_families, monkeypatch
):
    base, families = regression_families
    for family in families:
        observations = {
            "reset_fit": [],
            "scene_layers": [],
            "fit_scene": [],
            "event": [],
        }
        layers, margin = object(), object()

        def record(name, kwargs):
            observations[name].append(tuple(kwargs))

        def reset_fit(**kwargs):
            record("reset_fit", kwargs)
            assert kwargs["layers"] is layers and kwargs["margin"] is margin

        reset_viewer = SimpleNamespace(
            dims=SimpleNamespace(ndisplay=2), fit_to_view=reset_fit
        )
        extent = base.np.array([[0, 0], [4, 6]])

        def sliced(**kwargs):
            record("scene_layers", kwargs)
            assert kwargs["layers"] is layers
            return extent

        scene_viewer = SimpleNamespace(_sliced_extent_world_augmented=sliced)

        def get_scene(**kwargs):
            record("fit_scene", kwargs)
            assert kwargs["layers"] is layers
            return (extent, (4, 6), (0, 0))

        def on_reset(**kwargs):
            record("event", kwargs)

        camera = SimpleNamespace(
            view_direction=(0, 0, 1),
            up_direction=(0, 1, 0),
            center=None,
            zoom=None,
            angles=(1, 2, 3),
        )
        fit_viewer = SimpleNamespace(
            _get_scene_parameters=get_scene,
            dims=SimpleNamespace(ndisplay=2),
            scene=SimpleNamespace(camera=camera),
            layers=layers,
            canvas=SimpleNamespace(
                viewbox_size=lambda value: (800, 600), size=(800, 600)
            ),
            events=SimpleNamespace(reset_view=on_reset),
        )

        def fit_view(*args):
            return SimpleNamespace(center=(0, 2, 3), zoom=2)

        actual_native = family.namespace["_native"]
        with monkeypatch.context() as patch:
            patch.setitem(
                family.namespace, "_native", NativeProxy(actual_native, fit_view)
            )
            for _ in range(2):
                family.methods["reset_view"](reset_viewer, layers=layers, margin=margin)
                family.methods["_get_scene_parameters"](scene_viewer, layers=layers)
                family.methods["fit_to_view"](fit_viewer, layers=layers, margin=margin)
        names = {
            "reset_fit": ("layers", "margin"),
            "scene_layers": ("layers",),
            "fit_scene": ("layers",),
            "event": ("center", "zoom", "angles"),
        }
        for context, pairs in observations.items():
            assert len(pairs) == 2
            assert pairs[0] == pairs[1] == names[context]
            # Keep both tuples alive; this is object identity, not id recycling.
            assert all(a is b for a, b in zip(pairs[0], pairs[1], strict=True))
        assert family.namespace["_native"] is actual_native
