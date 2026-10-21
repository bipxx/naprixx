"""Original-first settings event wiring and canonical Canvas publication."""

# Every protocol callback completes within its source-family iteration.
# ruff: noqa: B023
from __future__ import annotations

import ast
import builtins
import hashlib
import json
import sys
from pathlib import Path
from types import ModuleType, SimpleNamespace

import pytest

import napari.settings as settings_module
from napari.components import canvas as canvas_module
from napari.layers.labels import _labels_mouse_bindings as labels_module
from napari.settings import _application as application_module
from napari.settings import _napari_settings as napari_settings_module
from napari.settings._constants import BrushSizeOnMouseModifiers
from napari.utils import status_messages
from napari.utils.events import EventedModel

ROOT = Path(__file__).resolve().parents[1]
BEFORE = ROOT / ".artifacts/settings-grid-publication-before"
MANIFEST = ROOT / ".artifacts/settings-grid-publication-before-snapshots.json"
SELECTION = ROOT / ".artifacts/viewer-next-owner-after-lifecycle-selection.json"
SELECTION_SHA = "380c134959f2d154afc36e95655caf30a30c865e5c37e0d420c011510946f06c"


def _function(tree, qualified):
    owner, _, name = qualified.rpartition(".")
    body = tree.body
    if owner:
        body = next(
            n.body for n in body if isinstance(n, ast.ClassDef) and n.name == owner
        )
    return next(n for n in body if isinstance(n, ast.FunctionDef) and n.name == name)


def _original_module(name, relative, namespace, names, overrides=None):
    path = BEFORE / relative
    tree = ast.parse(path.read_bytes())
    nodes = [
        node
        for node in tree.body
        if isinstance(node, ast.ImportFrom) and node.module == "__future__"
    ] + [
        node
        for node in tree.body
        if isinstance(node, (ast.ClassDef, ast.FunctionDef)) and node.name in names
    ]
    module = ModuleType(name)
    module.__file__ = str(path)
    module.__dict__.update(
        {
            key: value
            for key, value in namespace.items()
            if key not in ("__name__", "__file__", "__spec__", "__loader__")
        }
    )
    module.__dict__.update(overrides or {})
    assert name not in sys.modules
    sys.modules[name] = module
    try:
        exec(
            compile(ast.Module(nodes, []), str(path), "exec", dont_inherit=True),
            module.__dict__,
        )
    except BaseException:
        sys.modules.pop(name, None)
        raise
    return module


class Family:
    def __init__(self, name, canvas, application, napari, labels):
        self.name = name
        self.Canvas = canvas.Canvas
        self.Application = application.ApplicationSettings
        self.Settings = napari.NapariSettings
        self.canvas_namespace = canvas.__dict__
        self.application_namespace = application.__dict__
        self.labels_namespace = labels.__dict__
        self.canvas_init = canvas.Canvas.__init__
        self.grid_update = canvas.Canvas._update_viewer_grid
        self.settings_connect = napari.NapariSettings._connect_events
        self.application_connect = application.ApplicationSettings._connect_events
        self.brush_callback = application.brush_size_on_mouse_move_modifiers_callback
        self.precision_callback = application.float_display_precision_callback
        self.brush_store = labels.change_brush_size_on_mouse_move_modifiers


@pytest.fixture(scope="module")
def families():
    assert hashlib.sha256(SELECTION.read_bytes()).hexdigest() == SELECTION_SHA
    selection = json.loads(SELECTION.read_bytes())
    for source in json.loads(MANIFEST.read_bytes())["sources"]:
        raw = (ROOT / source["path"]).read_bytes()
        assert len(raw) == source["bytes"]
        assert hashlib.sha256(raw).hexdigest() == source["sha256"]
    for row in selection["selected_bodies"]:
        current = _function(
            ast.parse((BEFORE / row["current_source"]).read_bytes()),
            row["qualified_name"],
        )
        pinned = _function(
            ast.parse((BEFORE / row["pinned_source"]).read_bytes()),
            row["qualified_name"],
        )
        assert ast.dump(current, include_attributes=False) == ast.dump(
            pinned, include_attributes=False
        )
    modules = []
    try:
        labels = _original_module(
            "_naparixx_publication_labels_original",
            "src/napari/layers/labels/_labels_mouse_bindings.py",
            labels_module.__dict__,
            ("change_brush_size_on_mouse_move_modifiers",),
        )
        modules.append(labels)
        application = _original_module(
            "_naparixx_publication_application_original",
            "src/napari/settings/_application.py",
            application_module.__dict__,
            (
                "ApplicationSettings",
                "brush_size_on_mouse_move_modifiers_callback",
                "float_display_precision_callback",
            ),
        )
        modules.append(application)
        napari = _original_module(
            "_naparixx_publication_napari_original",
            "src/napari/settings/_napari_settings.py",
            napari_settings_module.__dict__,
            ("NapariSettings",),
            {"ApplicationSettings": application.ApplicationSettings},
        )
        modules.append(napari)
        canvas = _original_module(
            "_naparixx_publication_canvas_original",
            "src/napari/components/canvas.py",
            canvas_module.__dict__,
            ("Canvas",),
        )
        modules.append(canvas)
        yield (
            Family("original", canvas, application, napari, labels),
            Family(
                "current",
                canvas_module,
                application_module,
                napari_settings_module,
                labels_module,
            ),
        )
    finally:
        for module in modules:
            sys.modules.pop(module.__name__, None)


class Probe:
    def __init__(self, tag, values, trace, read=None, write=None):
        object.__setattr__(self, "_tag", tag)
        object.__setattr__(self, "_values", values)
        object.__setattr__(self, "_trace", trace)
        object.__setattr__(self, "_read", read)
        object.__setattr__(self, "_write", write)

    def __getattr__(self, name):
        self._trace.append(("read", self._tag, name))
        if self._read is not None:
            self._read(name)
        return self._values[name]

    def __setattr__(self, name, value):
        self._trace.append(("write", self._tag, name))
        if self._write is not None:
            self._write(name, value)
        self._values[name] = value


def test_original_real_settings_canvas_precision_and_brush_workflow(
    families, monkeypatch
):
    outputs = []
    for family in families:
        settings = family.Settings(config_path=None)
        canvas = None
        with monkeypatch.context() as patch:
            patch.setitem(settings_module.__dict__, "_SETTINGS", settings)
            patch.setattr(
                labels_module,
                "change_brush_size_on_mouse_move_modifiers",
                family.brush_store,
            )
            patch.setitem(
                family.labels_namespace,
                "BRUSH_SIZE_ON_MOUSE_MOVE_MODIFIERS_PARTS",
                ("initial",),
            )
            patch.setattr(
                status_messages, "DEFAULT_PRECISION", status_messages.DEFAULT_PRECISION
            )
            try:
                assert settings_module.get_settings() is settings
                assert (
                    settings.application._connect_events.__func__
                    is family.application_connect
                )
                settings._connect_events()
                canvas = family.Canvas(size=(321, 654))
                assert canvas.size == (321, 654)
                assert canvas._viewer_theme is None
                assert "_overlay_positions_changed" in canvas.events.emitters
                original_grid = canvas.grid
                application = settings.application
                application.grid_stride = 2
                application.grid_height = 3
                application.grid_width = 4
                application.grid_spacing = 0.25
                assert canvas.grid is original_grid
                assert (canvas.grid.stride, canvas.grid.shape, canvas.grid.spacing) == (
                    2,
                    (3, 4),
                    0.25,
                )
                application.float_display_precision = 7
                assert status_messages.DEFAULT_PRECISION == 7
                application.brush_size_on_mouse_move_modifiers = (
                    BrushSizeOnMouseModifiers.CTRL_SHIFT
                )
                published = family.labels_namespace[
                    "BRUSH_SIZE_ON_MOUSE_MOVE_MODIFIERS_PARTS"
                ]
                assert type(published) is list and published == ["Control", "Shift"]
                notifications = []

                def background(event):
                    notifications.append(event.type)

                canvas.events.background_color.connect(background)
                other_theme = (
                    "light" if settings.appearance.theme != "light" else "dark"
                )
                settings.appearance.theme = other_theme
                assert notifications == ["theme"]
                canvas.events.background_color.disconnect(background)
                outputs.append(
                    (
                        canvas.grid.stride,
                        canvas.grid.shape,
                        canvas.grid.spacing,
                        status_messages.DEFAULT_PRECISION,
                        published,
                        notifications,
                    )
                )
            finally:
                if canvas is not None:
                    for field in (
                        "grid_stride",
                        "grid_width",
                        "grid_height",
                        "grid_spacing",
                    ):
                        getattr(settings.application.events, field).disconnect(
                            canvas._update_viewer_grid
                        )
                    settings.appearance.events.theme.disconnect(
                        canvas.events.background_color
                    )
    assert outputs[1] == outputs[0]


@pytest.mark.parametrize(
    "mode", ["success_reentry", "super_error", "third_connect_error"]
)
def test_original_canvas_ctor_lexical_super_kwargs_and_connection_prefix(
    families, monkeypatch, mode
):
    for family in families:
        trace = []
        callbacks = []
        value = object()
        failure = RuntimeError("constructor boundary failed")
        background = object()

        class Emitter:
            def __init__(self, field):
                self.field = field

            def connect(self, callback):
                trace.append(("connect", self.field))
                if self.field == "theme":
                    assert callback is background
                    return
                assert callback is callbacks[-1]
                if mode == "third_connect_error" and self.field == "grid_height":
                    raise failure
                if mode == "success_reentry" and self.field == "grid_stride":
                    callback()

        settings = SimpleNamespace(
            application=SimpleNamespace(
                events=SimpleNamespace(
                    **{
                        field: Emitter(field)
                        for field in (
                            "grid_stride",
                            "grid_width",
                            "grid_height",
                            "grid_spacing",
                        )
                    }
                )
            ),
            appearance=SimpleNamespace(events=SimpleNamespace(theme=Emitter("theme"))),
        )

        def get_settings():
            trace.append(("settings",))
            return settings

        def add(**kwargs):
            trace.append(("add", tuple(kwargs)))
            assert (
                kwargs["_overlay_positions_changed"] is family.canvas_namespace["Event"]
            )

        events = SimpleNamespace(add=add, background_color=background)

        def base_init(self, **kwargs):
            trace.append(("super", tuple(kwargs), kwargs["token"] is value))
            if mode == "super_error":
                raise failure

        class ProtocolCanvas(family.Canvas):
            def __getattribute__(self, name):
                if name == "events":
                    return events
                if name == "_update_viewer_grid":
                    index = len(callbacks)

                    def callback():
                        trace.append(("update", index))

                    callbacks.append(callback)
                    return callback
                return super().__getattribute__(name)

            def __setattr__(self, name, supplied):
                if name == "_viewer_theme":
                    trace.append(("theme_store", supplied is None))
                    return
                super().__setattr__(name, supplied)

        with monkeypatch.context() as patch:
            patch.setattr(EventedModel, "__init__", base_init)
            patch.setitem(family.canvas_namespace, "get_settings", get_settings)
            if mode == "success_reentry":
                instance = ProtocolCanvas(token=value, extra=17)
                assert isinstance(instance, family.Canvas)
            else:
                with pytest.raises(RuntimeError) as caught:
                    ProtocolCanvas(token=value, extra=17)
                assert caught.value is failure
        expected = [("super", ("token", "extra"), True)]
        if mode != "super_error":
            expected += [
                ("add", ("_overlay_positions_changed",)),
                ("theme_store", True),
                ("update", 0),
                ("settings",),
                ("connect", "grid_stride"),
            ]
            if mode == "success_reentry":
                expected += [("update", 1)]
            expected += [("connect", "grid_width"), ("connect", "grid_height")]
            if mode == "success_reentry":
                expected += [("connect", "grid_spacing"), ("connect", "theme")]
        assert trace == expected


@pytest.mark.parametrize("mode", ["success_reentry", "width_error", "shape_error"])
def test_original_grid_rhs_tuple_capture_and_partial_publication(
    families, monkeypatch, mode
):
    for family in families:
        trace = []
        stride, height, width_before, width_after, spacing = (
            object() for _ in range(5)
        )
        failure = RuntimeError("grid publication failed")

        def read(name):
            if mode == "width_error" and name == "grid_width":
                raise failure

        application = Probe(
            "application",
            {
                "grid_stride": stride,
                "grid_height": height,
                "grid_width": width_before,
                "grid_spacing": spacing,
            },
            trace,
            read=read,
        )

        def write(name, value):
            if name == "stride":
                assert value is stride
                application._values["grid_width"] = width_after
            if mode == "shape_error" and name == "shape":
                raise failure

        grid = Probe("grid", {}, trace, write=write)
        viewer = Probe("canvas", {"grid": grid}, trace)

        def settings():
            trace.append(("settings",))
            return SimpleNamespace(application=application)

        with monkeypatch.context() as patch:
            patch.setitem(family.canvas_namespace, "get_settings", settings)
            if mode == "success_reentry":
                assert family.grid_update(viewer) is None
            else:
                with pytest.raises(RuntimeError) as caught:
                    family.grid_update(viewer)
                assert caught.value is failure
        expected = [
            ("settings",),
            ("read", "application", "grid_stride"),
            ("read", "canvas", "grid"),
            ("write", "grid", "stride"),
            ("read", "application", "grid_height"),
            ("read", "application", "grid_width"),
        ]
        if mode != "width_error":
            expected += [("read", "canvas", "grid"), ("write", "grid", "shape")]
        if mode == "success_reentry":
            expected += [
                ("read", "application", "grid_spacing"),
                ("read", "canvas", "grid"),
                ("write", "grid", "spacing"),
            ]
            assert grid._values["shape"][0] is height
            assert grid._values["shape"][1] is width_after
            assert grid._values["spacing"] is spacing
        else:
            assert "shape" not in grid._values and "spacing" not in grid._values
        assert grid._values["stride"] is stride
        assert trace == expected


@pytest.mark.parametrize("mode", ["live_experimental", "application_error"])
def test_original_settings_application_before_fresh_experimental(families, mode):
    for family in families:
        trace = []
        failure = RuntimeError("application connection failed")
        new_experimental = SimpleNamespace(
            _connect_events=lambda: trace.append("new_experimental")
        )
        owner = SimpleNamespace(
            experimental=SimpleNamespace(
                _connect_events=lambda: pytest.fail("stale experimental receiver")
            )
        )

        def application():
            trace.append("application")
            owner.experimental = new_experimental
            if mode == "application_error":
                raise failure

        owner.application = SimpleNamespace(_connect_events=application)
        if mode == "application_error":
            with pytest.raises(RuntimeError) as caught:
                family.settings_connect(owner)
            assert caught.value is failure
        else:
            assert family.settings_connect(owner) is None
        assert trace == ["application"] + (
            ["new_experimental"] if mode == "live_experimental" else []
        )


@pytest.mark.parametrize(
    "mode", ["success_reentry", "brush_emit_error", "precision_connect_error"]
)
def test_original_application_connect_emit_live_callback_and_value(
    families, monkeypatch, mode
):
    for family in families:
        trace = []
        brush_value, old_precision, new_precision = object(), object(), object()
        failure = RuntimeError("application event failed")

        def precision(event):
            trace.append(("precision_callback", event.value is new_precision))

        def brush(event):
            trace.append(("brush_callback", event.value is brush_value))
            family.application_namespace["float_display_precision_callback"] = precision
            owner.float_display_precision = new_precision
            if mode == "brush_emit_error":
                raise failure

        class Emitter:
            def __init__(self, field):
                self.field = field
                self.callback = None

            def connect(self, callback):
                trace.append(("connect", self.field))
                assert callback is (brush if self.field == "brush" else precision)
                if mode == "precision_connect_error" and self.field == "precision":
                    raise failure
                self.callback = callback

            def __call__(self, **kwargs):
                trace.append(("emit", self.field, tuple(kwargs)))
                self.callback(SimpleNamespace(value=kwargs["value"]))

        owner = SimpleNamespace(
            events=SimpleNamespace(
                brush_size_on_mouse_move_modifiers=Emitter("brush"),
                float_display_precision=Emitter("precision"),
            ),
            brush_size_on_mouse_move_modifiers=brush_value,
            float_display_precision=old_precision,
        )
        with monkeypatch.context() as patch:
            patch.setitem(
                family.application_namespace,
                "brush_size_on_mouse_move_modifiers_callback",
                brush,
            )
            patch.setitem(
                family.application_namespace,
                "float_display_precision_callback",
                lambda event: pytest.fail("stale precision callback"),
            )
            if mode == "success_reentry":
                assert family.application_connect(owner) is None
            else:
                with pytest.raises(RuntimeError) as caught:
                    family.application_connect(owner)
                assert caught.value is failure
        expected = [
            ("connect", "brush"),
            ("emit", "brush", ("value",)),
            ("brush_callback", True),
        ]
        if mode != "brush_emit_error":
            expected += [("connect", "precision")]
        if mode == "success_reentry":
            expected += [
                ("emit", "precision", ("value",)),
                ("precision_callback", True),
            ]
        assert trace == expected


@pytest.mark.parametrize("mode", ["success", "split_error", "import_error"])
def test_original_brush_import_split_and_exact_helper_alias(
    families, monkeypatch, mode
):
    for family in families:
        trace = []
        result = ["Control", "Alt"]
        failure = RuntimeError("brush callback failed")
        original_import = builtins.__import__
        previous = family.labels_namespace["BRUSH_SIZE_ON_MOUSE_MOVE_MODIFIERS_PARTS"]

        class Value:
            def split(self, separator):
                trace.append(("split", separator))
                if mode == "split_error":
                    raise failure
                return result

        event = Probe("event", {"value": Value()}, trace)

        def importer(name, globals=None, locals=None, fromlist=(), level=0):
            if name == "napari.layers.labels._labels_mouse_bindings":
                trace.append(
                    (
                        "import",
                        globals is family.application_namespace,
                        locals is None,
                        fromlist,
                        level,
                    )
                )
                if mode == "import_error":
                    raise failure
            return original_import(name, globals, locals, fromlist, level)

        def helper(value):
            trace.append(("helper", value is result))
            family.brush_store(value)

        with monkeypatch.context() as patch:
            patch.setitem(
                family.labels_namespace,
                "BRUSH_SIZE_ON_MOUSE_MOVE_MODIFIERS_PARTS",
                previous,
            )
            patch.setattr(
                labels_module, "change_brush_size_on_mouse_move_modifiers", helper
            )
            patch.setattr(builtins, "__import__", importer)
            if mode == "success":
                assert family.brush_callback(event) is None
                assert (
                    family.labels_namespace["BRUSH_SIZE_ON_MOUSE_MOVE_MODIFIERS_PARTS"]
                    is result
                )
            else:
                with pytest.raises(RuntimeError) as caught:
                    family.brush_callback(event)
                assert caught.value is failure
                assert (
                    family.labels_namespace["BRUSH_SIZE_ON_MOUSE_MOVE_MODIFIERS_PARTS"]
                    is previous
                )
        expected = [
            ("import", True, True, ("change_brush_size_on_mouse_move_modifiers",), 0)
        ]
        if mode != "import_error":
            expected += [("read", "event", "value"), ("split", "+")]
        if mode == "success":
            expected += [("helper", True)]
        assert trace == expected
        assert builtins.__import__ is original_import


@pytest.mark.parametrize("mode", ["success", "import_error"])
def test_original_precision_import_before_value_and_publication(
    families, monkeypatch, mode
):
    for family in families:
        trace = []
        value = object()
        failure = RuntimeError("precision import failed")
        original_import = builtins.__import__
        previous = status_messages.DEFAULT_PRECISION
        event = Probe("event", {"value": value}, trace)

        def importer(name, globals=None, locals=None, fromlist=(), level=0):
            if name == "napari.utils" and fromlist == ("status_messages",):
                trace.append(
                    (
                        "import",
                        globals is family.application_namespace,
                        locals is None,
                        fromlist,
                        level,
                    )
                )
                if mode == "import_error":
                    raise failure
            return original_import(name, globals, locals, fromlist, level)

        with monkeypatch.context() as patch:
            patch.setattr(status_messages, "DEFAULT_PRECISION", previous)
            patch.setattr(builtins, "__import__", importer)
            if mode == "success":
                assert family.precision_callback(event) is None
                assert status_messages.DEFAULT_PRECISION is value
            else:
                with pytest.raises(RuntimeError) as caught:
                    family.precision_callback(event)
                assert caught.value is failure
                assert status_messages.DEFAULT_PRECISION is previous
        expected = [("import", True, True, ("status_messages",), 0)]
        if mode == "success":
            expected += [("read", "event", "value")]
        assert trace == expected
        assert status_messages.DEFAULT_PRECISION is previous
        assert builtins.__import__ is original_import


def test_original_brush_helper_publishes_arbitrary_value_identity(
    families, monkeypatch
):
    for family in families:
        value = object()
        with monkeypatch.context() as patch:
            patch.setitem(
                family.labels_namespace,
                "BRUSH_SIZE_ON_MOUSE_MOVE_MODIFIERS_PARTS",
                None,
            )
            assert family.brush_store(value) is None
            assert (
                family.labels_namespace["BRUSH_SIZE_ON_MOUSE_MOVE_MODIFIERS_PARTS"]
                is value
            )
