"""Original-current hybrid reset/fit and Canvas policy, before and after adoption."""

# All protocol callbacks run synchronously within one family; none escapes it.
# ruff: noqa: B023
from __future__ import annotations

import ast
import hashlib
import sys
from pathlib import Path
from types import ModuleType, SimpleNamespace

import numpy as np
import pytest
from pydantic import Field

from napari.components import ViewerModel as ActualViewer
from napari.components import viewer_model as viewer_module
from napari.components.canvas import Canvas as ActualCanvas

ROOT = Path(__file__).resolve().parents[1]
BEFORE = ROOT / ".artifacts/viewer-reset-fit-before/current/src/napari/components"
PINNED = (
    ROOT
    / ".artifacts/viewer-reset-fit-before/pinned/.upstream/napari/src/napari/components"
)
VIEWER_SHA = "a5862ac20c787d5bee9d4d31c5b73680b0a785de18499487e14b694230768c8e"
CANVAS_SHA = "ed6e005552a0cd3b222b49a6e6e973e9e94765ef4ace54659b6c130668189131"
PINNED_VIEWER_SHA = "77559e4b2653428cb8318e8ef858d4aa4f57c74e3abd40f2a0ad4f58106132c9"
PINNED_CANVAS_SHA = "212475f24d9af915b2c28b10dfcee61d858d92517560da2caa1f522b3d2ca95d"
NAMES = ("reset_view", "fit_to_view", "_get_scene_parameters")


def _methods(raw, classname, names):
    cls = next(
        node
        for node in ast.parse(raw).body
        if isinstance(node, ast.ClassDef) and node.name == classname
    )
    return [
        node
        for node in cls.body
        if isinstance(node, ast.FunctionDef) and node.name in names
    ]


def _original_module(name, raw, classname, names, bindings, filename):
    methods = _methods(raw, classname, names)
    future = [
        node
        for node in ast.parse(raw).body
        if isinstance(node, ast.ImportFrom) and node.module == "__future__"
    ]
    cls = ast.ClassDef(
        name=classname, bases=[], keywords=[], body=methods, decorator_list=[]
    )
    ast.copy_location(cls, methods[0])
    tree = ast.fix_missing_locations(ast.Module(body=[*future, cls], type_ignores=[]))
    module = ModuleType(name)
    module.__file__ = str(filename)
    module.__dict__.update(bindings)
    assert name not in sys.modules
    sys.modules[name] = module
    try:
        exec(compile(tree, str(filename), "exec", dont_inherit=True), module.__dict__)
    except BaseException:
        sys.modules.pop(name, None)
        raise
    return module


class Family:
    def __init__(self, name, viewer_class, canvas_class, namespace):
        self.name = name
        self.namespace = namespace
        self.methods = {key: getattr(viewer_class, key) for key in NAMES}
        self.canvas_method = canvas_class.viewbox_size
        if name == "original":
            canvas_method = self.canvas_method

            class OriginalCanvas(ActualCanvas):
                viewbox_size = canvas_method

            methods = self.methods

            class OriginalViewer(ActualViewer):
                canvas: ActualCanvas = Field(
                    default_factory=OriginalCanvas, frozen=True
                )
                reset_view = methods["reset_view"]
                fit_to_view = methods["fit_to_view"]
                _get_scene_parameters = methods["_get_scene_parameters"]

            self.Viewer, self.Canvas = OriginalViewer, OriginalCanvas
        else:
            self.Viewer, self.Canvas = ActualViewer, ActualCanvas


@pytest.fixture(scope="module")
def families():
    raw_viewer = (BEFORE / "viewer_model.py").read_bytes()
    raw_canvas = (BEFORE / "canvas.py").read_bytes()
    assert hashlib.sha256(raw_viewer).hexdigest() == VIEWER_SHA
    assert hashlib.sha256(raw_canvas).hexdigest() == CANVAS_SHA
    pinned_viewer = (PINNED / "viewer_model.py").read_bytes()
    pinned_canvas = (PINNED / "canvas.py").read_bytes()
    assert hashlib.sha256(pinned_viewer).hexdigest() == PINNED_VIEWER_SHA
    assert hashlib.sha256(pinned_canvas).hexdigest() == PINNED_CANVAS_SHA
    current_methods = _methods(raw_viewer, "ViewerModel", NAMES)
    pinned_methods = _methods(pinned_viewer, "ViewerModel", NAMES)
    assert [
        ast.dump(current, include_attributes=False)
        == ast.dump(pinned, include_attributes=False)
        for current, pinned in zip(current_methods, pinned_methods, strict=True)
    ] == [True, False, True]
    assert ast.dump(
        _methods(raw_canvas, "Canvas", ("viewbox_size",))[0], include_attributes=False
    ) != ast.dump(
        _methods(pinned_canvas, "Canvas", ("viewbox_size",))[0],
        include_attributes=False,
    )
    modules = []
    try:
        viewer = _original_module(
            "_naparixx_reset_fit_current_original",
            raw_viewer,
            "ViewerModel",
            NAMES,
            {"_native": viewer_module._native, "np": np},
            BEFORE / "viewer_model.py",
        )
        modules.append(viewer)
        canvas = _original_module(
            "_naparixx_canvas_fit_current_original",
            raw_canvas,
            "Canvas",
            ("viewbox_size",),
            {},
            BEFORE / "canvas.py",
        )
        modules.append(canvas)
        yield (
            Family("original", viewer.ViewerModel, canvas.Canvas, viewer.__dict__),
            Family("current", ActualViewer, ActualCanvas, viewer_module.__dict__),
        )
    finally:
        for module in modules:
            sys.modules.pop(module.__name__, None)


@pytest.mark.parametrize("ndisplay", [2, 3], ids=["two", "three"])
@pytest.mark.parametrize("grid_enabled", [False, True], ids=["single", "grid"])
def test_original_real_reset_fit_grid_subset_margin_and_camera_events(
    families, ndisplay, grid_enabled
):
    results = []
    for family in families:
        viewer = family.Viewer(ndisplay=ndisplay)
        viewer._layer_slicer._force_sync = True
        authorities = (
            viewer.canvas,
            viewer.canvas.grid,
            viewer.scene,
            viewer.scene.camera,
        )
        events = []

        def on_reset(event):
            assert event.center == viewer.scene.camera.center
            assert event.zoom == viewer.scene.camera.zoom
            assert event.angles == viewer.scene.camera.angles
            events.append((tuple(event.center), event.zoom, tuple(event.angles)))

        try:
            assert isinstance(viewer.canvas, family.Canvas)
            assert viewer.reset_view.__func__ is family.methods["reset_view"]
            assert viewer.fit_to_view.__func__ is family.methods["fit_to_view"]
            assert (
                viewer._get_scene_parameters.__func__
                is family.methods["_get_scene_parameters"]
            )
            assert viewer.canvas.viewbox_size.__func__ is family.canvas_method
            viewer.canvas.size = (900, 600)
            first = viewer.add_image(
                np.arange(192).reshape(4, 6, 8), scale=(2, 3, 4), translate=(1, 2, 3)
            )
            viewer.add_image(np.zeros((4, 6, 8)), translate=(20, 30, 40))
            viewer.canvas.grid.enabled = grid_enabled
            viewer.canvas.grid.shape = (1, -1)
            viewer.canvas.grid.spacing = 0.1
            viewer.events.reset_view.connect(on_reset)
            viewer.scene.camera.angles = (20, 30, 40)
            viewer.reset_view(layers=[first], margin=0.1, reset_camera_angle=False)
            assert viewer.scene.camera.angles == (20, 30, 40)
            subset = (tuple(viewer.scene.camera.center), viewer.scene.camera.zoom)
            viewer.fit_to_view(layers=[first], margin=0.2)
            assert viewer.scene.camera.zoom < subset[1]
            larger_margin = viewer.scene.camera.zoom
            viewer.fit_to_view(layers=[first], margin=0)
            assert viewer.scene.camera.zoom > subset[1] > larger_margin
            viewer.reset_view(layers=[first], margin=0.1)
            assert viewer.scene.camera.angles == (
                (0, 0, 0) if ndisplay == 3 else (20, 30, 40)
            )
            before_error = (viewer.scene.camera.center, viewer.scene.camera.zoom)
            with pytest.raises(ValueError, match="margin must be between 0 and 1"):
                viewer.fit_to_view(layers=[first], margin=1)
            assert (
                viewer.scene.camera.center,
                viewer.scene.camera.zoom,
            ) == before_error
            assert len(events) == 4
            assert all(
                a is b
                for a, b in zip(
                    authorities,
                    (
                        viewer.canvas,
                        viewer.canvas.grid,
                        viewer.scene,
                        viewer.scene.camera,
                    ),
                    strict=True,
                )
            )
            results.append((events, family.canvas_method(viewer.canvas, viewer.layers)))
        finally:
            viewer.events.reset_view.disconnect(on_reset)
            viewer.layers.clear()
            viewer._layer_slicer.shutdown()
    assert results[1] == results[0]


class Trace:
    def __init__(self, label, values, events, *, read=None, write=None):
        object.__setattr__(self, "label", label)
        object.__setattr__(self, "values", values)
        object.__setattr__(self, "_trace", events)
        object.__setattr__(self, "read", read)
        object.__setattr__(self, "write", write)

    def __getattr__(self, name):
        self._trace.append(("get", self.label, name))
        if self.read is not None:
            self.read(name)
        return self.values[name]

    def __setattr__(self, name, value):
        self._trace.append(("set", self.label, name))
        self.values[name] = value
        if self.write is not None:
            self.write(name, value)


@pytest.mark.parametrize("mode", ["two", "three_false", "three_reentry", "angle_error"])
def test_original_reset_angle_short_circuit_and_fresh_fit(families, mode):
    results = []
    for family in families:
        trace = []
        layers, margin = object(), object()

        class Flag:
            def __bool__(self):
                trace.append(("flag", "bool"))
                return mode != "three_false"

        def fit(**kwargs):
            trace.append(
                ("fit", kwargs["layers"] is layers, kwargs["margin"] is margin)
            )
            return object()

        def changed_fit(**kwargs):
            trace.append(
                ("changed_fit", kwargs["layers"] is layers, kwargs["margin"] is margin)
            )

        def camera_write(name, value):
            assert name == "angles" and value == (0, 0, 0)
            if mode == "angle_error":
                raise RuntimeError("angle publication failed")
            viewer.values["fit_to_view"] = changed_fit

        camera = Trace("camera", {"angles": (1, 2, 3)}, trace, write=camera_write)
        viewer = Trace(
            "viewer",
            {
                "dims": Trace("dims", {"ndisplay": 2 if mode == "two" else 3}, trace),
                "scene": Trace("scene", {"camera": camera}, trace),
                "fit_to_view": fit,
            },
            trace,
        )
        if mode == "angle_error":
            with pytest.raises(RuntimeError, match="angle publication failed"):
                family.methods["reset_view"](
                    viewer, layers=layers, margin=margin, reset_camera_angle=Flag()
                )
        else:
            assert (
                family.methods["reset_view"](
                    viewer, layers=layers, margin=margin, reset_camera_angle=Flag()
                )
                is None
            )
        expected = [("get", "viewer", "dims"), ("get", "dims", "ndisplay")]
        if mode != "two":
            expected.append(("flag", "bool"))
        if mode in ("three_reentry", "angle_error"):
            expected += [
                ("get", "viewer", "scene"),
                ("get", "scene", "camera"),
                ("set", "camera", "angles"),
            ]
            assert camera.values["angles"] == (0, 0, 0)
        if mode != "angle_error":
            expected += [
                ("get", "viewer", "fit_to_view"),
                ("changed_fit" if mode == "three_reentry" else "fit", True, True),
            ]
        assert trace == expected
        results.append(trace)
    assert results[1] == results[0]


@pytest.mark.parametrize("mode", ["success", "subtraction_error"])
def test_original_scene_parameters_read_order_and_aliases(families, mode):
    results = []
    for family in families:
        trace = []
        layers, low, corner, scene_size = object(), object(), object(), object()
        reads = 0

        class High:
            def __sub__(self, other):
                trace.append(("subtract", other is low))
                if mode == "subtraction_error":
                    raise RuntimeError("extent subtraction failed")
                return scene_size

        high = High()

        class Extent:
            def __getitem__(self, key):
                nonlocal reads
                trace.append(("getitem", key))
                if key == 1:
                    return high
                reads += 1
                return low if reads == 1 else corner

        extent = Extent()

        def sliced(**kwargs):
            trace.append(("sliced", kwargs["layers"] is layers))
            return extent

        viewer = SimpleNamespace(_sliced_extent_world_augmented=sliced)
        if mode == "subtraction_error":
            with pytest.raises(RuntimeError, match="extent subtraction failed"):
                family.methods["_get_scene_parameters"](viewer, layers=layers)
        else:
            result = family.methods["_get_scene_parameters"](viewer, layers=layers)
            assert (
                result[0] is extent and result[1] is scene_size and result[2] is corner
            )
        expected = [
            ("sliced", True),
            ("getitem", 1),
            ("getitem", 0),
            ("subtract", True),
        ]
        if mode == "success":
            expected.append(("getitem", 0))
        assert trace == expected
        results.append(trace)
    assert results[1] == results[0]


@pytest.mark.parametrize(
    "mode",
    ["success_reentry", "unpack_error", "center_error", "zoom_error", "event_error"],
)
def test_original_fit_unpack_live_result_publication_and_event_prefix(
    families, monkeypatch, mode
):
    results = []
    for family in families:
        trace = []
        layers, margin, corner, scene_size = object(), object(), object(), object()
        center, zoom, later_zoom, later_center = object(), object(), object(), object()
        angles, direction, up, viewbox, size = (
            object(),
            object(),
            object(),
            object(),
            object(),
        )
        camera2 = Trace(
            "camera2", {"center": later_center, "zoom": zoom, "angles": angles}, trace
        )
        scene2 = Trace("scene2", {"camera": camera2}, trace)
        result = Trace("result", {"center": center, "zoom": zoom}, trace)

        def camera_write(name, value):
            if name == "center" and mode == "center_error":
                raise RuntimeError("center publication failed")
            if name == "zoom" and mode == "zoom_error":
                raise RuntimeError("zoom publication failed")
            if name == "center" and mode == "success_reentry":
                result.values["zoom"] = later_zoom
                viewer.values["scene"] = scene2

        camera = Trace(
            "camera",
            {
                "center": None,
                "zoom": None,
                "angles": angles,
                "view_direction": direction,
                "up_direction": up,
            },
            trace,
            write=camera_write,
        )

        def get_scene(**kwargs):
            trace.append(("scene_parameters", kwargs["layers"] is layers))
            return (
                (None, scene_size)
                if mode == "unpack_error"
                else (None, scene_size, corner)
            )

        def viewbox_size(value):
            trace.append(("viewbox_call", value is layers))
            return viewbox

        def fit_view(*args):
            trace.append(
                (
                    "native_call",
                    all(
                        a is b
                        for a, b in zip(
                            args,
                            (
                                corner,
                                scene_size,
                                3,
                                direction,
                                up,
                                viewbox,
                                size,
                                margin,
                            ),
                            strict=True,
                        )
                    ),
                )
            )
            return result

        def event(**kwargs):
            trace.append(
                (
                    "event_call",
                    kwargs["center"]
                    is (later_center if mode == "success_reentry" else center),
                    kwargs["zoom"]
                    is (later_zoom if mode == "success_reentry" else zoom),
                    kwargs["angles"] is angles,
                )
            )
            if mode == "event_error":
                raise RuntimeError("reset event failed")

        viewer = Trace(
            "viewer",
            {
                "_get_scene_parameters": get_scene,
                "dims": Trace("dims", {"ndisplay": 3}, trace),
                "scene": Trace("scene", {"camera": camera}, trace),
                "canvas": Trace(
                    "canvas", {"viewbox_size": viewbox_size, "size": size}, trace
                ),
                "layers": layers,
                "events": Trace("events", {"reset_view": event}, trace),
            },
            trace,
        )
        actual_native = family.namespace["_native"]
        controlled_native = Trace("native", {"fit_view": fit_view}, trace)

        class NativeProxy:
            def __getattr__(self, name):
                if name in (
                    "viewer_reset_fit_locals", "viewer_fit_to_view"
                ):
                    return getattr(actual_native, name)
                return getattr(controlled_native, name)

        native = NativeProxy()
        with monkeypatch.context() as patch:
            patch.setitem(family.namespace, "_native", native)
            if mode == "unpack_error":
                with pytest.raises(ValueError, match="not enough values to unpack"):
                    family.methods["fit_to_view"](viewer, layers=layers, margin=margin)
            elif mode.endswith("error"):
                expected_error = {
                    "center_error": "center publication failed",
                    "zoom_error": "zoom publication failed",
                    "event_error": "reset event failed",
                }[mode]
                with pytest.raises(RuntimeError, match=expected_error):
                    family.methods["fit_to_view"](viewer, layers=layers, margin=margin)
            else:
                assert (
                    family.methods["fit_to_view"](viewer, layers=layers, margin=margin)
                    is None
                )
        prefix = [
            ("get", "viewer", "_get_scene_parameters"),
            ("scene_parameters", True),
        ]
        if mode == "unpack_error":
            assert trace == prefix
            assert camera.values["center"] is None and camera.values["zoom"] is None
        else:
            prefix += [
                ("get", "native", "fit_view"),
                ("get", "viewer", "dims"),
                ("get", "dims", "ndisplay"),
                ("get", "viewer", "scene"),
                ("get", "scene", "camera"),
                ("get", "camera", "view_direction"),
                ("get", "viewer", "scene"),
                ("get", "scene", "camera"),
                ("get", "camera", "up_direction"),
                ("get", "viewer", "canvas"),
                ("get", "canvas", "viewbox_size"),
                ("get", "viewer", "layers"),
                ("viewbox_call", True),
                ("get", "viewer", "canvas"),
                ("get", "canvas", "size"),
                ("native_call", True),
                ("get", "result", "center"),
                ("get", "viewer", "scene"),
                ("get", "scene", "camera"),
                ("set", "camera", "center"),
            ]
            assert camera.values["center"] is center
            if mode != "center_error":
                scene_label = "scene2" if mode == "success_reentry" else "scene"
                camera_label = "camera2" if mode == "success_reentry" else "camera"
                prefix += [
                    ("get", "result", "zoom"),
                    ("get", "viewer", "scene"),
                    ("get", scene_label, "camera"),
                    ("set", camera_label, "zoom"),
                ]
                if mode != "zoom_error":
                    prefix += [
                        ("get", "viewer", "events"),
                        ("get", "events", "reset_view"),
                    ]
                    for key in ("center", "zoom", "angles"):
                        prefix += [
                            ("get", "viewer", "scene"),
                            ("get", scene_label, "camera"),
                            ("get", camera_label, key),
                        ]
                    prefix += [("event_call", True, True, True)]
            assert trace == prefix
        results.append(trace)
    assert results[1] == results[0]


@pytest.mark.parametrize("mode", ["success", "size_error", "layout_error"])
def test_original_canvas_bound_receiver_before_size_and_live_layout(families, mode):
    results = []
    for family in families:
        trace = []
        layers, size, layout, output = object(), object(), object(), object()

        def layout_call(value):
            trace.append(("layout_call", value is layers))
            if mode == "layout_error":
                raise RuntimeError("layout failed")
            return layout

        def bound_call(first, second):
            trace.append(("bound_call", first is size, second is layout))
            return output

        grid2 = Trace("grid2", {"_layout": layout_call}, trace)
        grid1 = Trace(
            "grid1",
            {"_native_state": Trace("state1", {"viewbox_size": bound_call}, trace)},
            trace,
        )

        def canvas_read(name):
            if name == "size":
                if mode == "size_error":
                    raise RuntimeError("size failed")
                canvas.values["grid"] = grid2

        canvas = Trace("canvas", {"grid": grid1, "size": size}, trace, read=canvas_read)
        if mode.endswith("error"):
            with pytest.raises(RuntimeError, match="size failed|layout failed"):
                family.canvas_method(canvas, layers)
        else:
            assert family.canvas_method(canvas, layers) is output
        expected = [
            ("get", "canvas", "grid"),
            ("get", "grid1", "_native_state"),
            ("get", "state1", "viewbox_size"),
            ("get", "canvas", "size"),
        ]
        if mode != "size_error":
            expected += [
                ("get", "canvas", "grid"),
                ("get", "grid2", "_layout"),
                ("layout_call", True),
            ]
            if mode == "success":
                expected += [("bound_call", True, True)]
        assert trace == expected
        results.append(trace)
    assert results[1] == results[0]
