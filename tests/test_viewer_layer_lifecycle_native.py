"""Original-first lifecycle connections, callback policy, help and singleton state."""

# Protocol closures run synchronously within one source-family iteration.
# ruff: noqa: B023
from __future__ import annotations

import ast
import hashlib
import sys
from pathlib import Path
from types import ModuleType, SimpleNamespace

import numpy as np
import pytest

import napari.settings as settings_module
from napari.components import ViewerModel as ActualViewer
from napari.components import viewer_model as viewer_module
from napari.layers import Image, Labels

ROOT = Path(__file__).resolve().parents[1]
BEFORE = ROOT / ".artifacts/viewer-layer-lifecycle-before"
VIEWER_SHA = "95b63b133ee64a87dbbe384a2ea82d5d910efe014c867afa02e7cbbf437abc96"
SETTINGS_SHA = "2930d00b01ef6781b837df24943e98faf1b420d4c00767826d770b7cd0120c8f"
NAMES = (
    "_on_add_layer",
    "_on_remove_layer",
    "_update_mouse_pan",
    "_update_mouse_zoom",
    "_update_cursor",
    "_update_cursor_size",
    "_layer_help_from_mode",
    "_on_layer_mode_change",
)
SETTINGS_NAMES = ("get_settings", "_raise_path_set_twice")


def _selected(raw, classname, names):
    tree = ast.parse(raw)
    nodes = (
        tree.body
        if classname is None
        else next(
            node.body
            for node in tree.body
            if isinstance(node, ast.ClassDef) and node.name == classname
        )
    )
    return [
        node
        for node in nodes
        if isinstance(node, ast.FunctionDef) and node.name in names
    ]


def _original_module(name, raw, classname, names, namespace, filename):
    selected = _selected(raw, classname, names)
    future = [
        node
        for node in ast.parse(raw).body
        if isinstance(node, ast.ImportFrom) and node.module == "__future__"
    ]
    if classname is not None:
        cls = ast.ClassDef(
            name=classname, bases=[], keywords=[], body=selected, decorator_list=[]
        )
        ast.copy_location(cls, selected[0])
        selected = [cls]
    tree = ast.fix_missing_locations(
        ast.Module(body=[*future, *selected], type_ignores=[])
    )
    module = ModuleType(name)
    module.__file__ = str(filename)
    module.__dict__.update(
        {
            key: value
            for key, value in namespace.items()
            if key not in ("__name__", "__file__", "__spec__", "__loader__")
        }
    )
    assert name not in sys.modules
    sys.modules[name] = module
    try:
        exec(compile(tree, str(filename), "exec", dont_inherit=True), module.__dict__)
    except BaseException:
        sys.modules.pop(name, None)
        raise
    return module


class Family:
    def __init__(self, name, cls, viewer_namespace, settings):
        self.name = name
        self.methods = {key: getattr(cls, key) for key in NAMES}
        self.viewer_namespace = viewer_namespace
        self.settings_namespace = settings.__dict__
        self.get_settings = settings.get_settings
        self.raise_path = settings._raise_path_set_twice
        self.Viewer = (
            type(
                "OriginalLifecycleViewer",
                (ActualViewer,),
                {key: cls.__dict__[key] for key in NAMES},
            )
            if name == "original"
            else ActualViewer
        )


@pytest.fixture(scope="module")
def families():
    raw_viewer = (BEFORE / "src/napari/components/viewer_model.py").read_bytes()
    raw_settings = (BEFORE / "src/napari/settings/__init__.py").read_bytes()
    assert hashlib.sha256(raw_viewer).hexdigest() == VIEWER_SHA
    assert hashlib.sha256(raw_settings).hexdigest() == SETTINGS_SHA
    for raw, rel, cls, names in (
        (raw_viewer, "napari/components/viewer_model.py", "ViewerModel", NAMES),
        (raw_settings, "napari/settings/__init__.py", None, SETTINGS_NAMES),
    ):
        pinned = (BEFORE / ".upstream/napari/src" / rel).read_bytes()
        assert [
            ast.dump(node, include_attributes=False)
            for node in _selected(raw, cls, names)
        ] == [
            ast.dump(node, include_attributes=False)
            for node in _selected(pinned, cls, names)
        ]
    canonical = settings_module.get_settings()
    modules = []
    try:
        settings = _original_module(
            "_naparixx_lifecycle_settings_original",
            raw_settings,
            None,
            SETTINGS_NAMES,
            settings_module.__dict__,
            BEFORE / "src/napari/settings/__init__.py",
        )
        modules.append(settings)
        settings._SETTINGS = canonical
        viewer = _original_module(
            "_naparixx_lifecycle_viewer_original",
            raw_viewer,
            "ViewerModel",
            NAMES,
            viewer_module.__dict__,
            BEFORE / "src/napari/components/viewer_model.py",
        )
        modules.append(viewer)
        viewer.get_settings = settings.get_settings
        yield (
            Family("original", viewer.ViewerModel, viewer.__dict__, settings),
            Family("current", ActualViewer, viewer_module.__dict__, settings_module),
        )
    finally:
        for module in modules:
            sys.modules.pop(module.__name__, None)


class Probe:
    def __init__(self, tag, values, trace, read=None):
        object.__setattr__(self, "_tag", tag)
        object.__setattr__(self, "_values", values)
        object.__setattr__(self, "_trace", trace)
        object.__setattr__(self, "_read", read)

    def __getattr__(self, name):
        self._trace.append(("read", self._tag, name))
        if self._read is not None:
            self._read(name)
        if name not in self._values:
            raise AttributeError(name)
        return self._values[name]

    def __setattr__(self, name, value):
        self._trace.append(("write", self._tag, name))
        self._values[name] = value


@pytest.mark.parametrize("kind", ["image", "labels"])
def test_original_real_layer_connect_remove_keeps_external_callback(families, kind):
    results = []
    for family in families:
        viewer = family.Viewer()
        viewer._layer_slicer._force_sync = True
        layer = (
            Image(np.zeros((4, 5)))
            if kind == "image"
            else Labels(np.zeros((4, 5), dtype=int))
        )
        external_values = []

        def external(event):
            external_values.append(event.cursor)

        layer.events.cursor.connect(external)
        try:
            for name in NAMES:
                bound = getattr(viewer, name)
                assert (
                    (bound is family.methods[name])
                    if name == "_layer_help_from_mode"
                    else (bound.__func__ is family.methods[name])
                )
            viewer.layers.append(layer)
            assert viewer.layers.selection.active is layer
            layer.events.cursor(cursor="cross")
            assert viewer.cursor.style == "cross"
            layer.events.cursor_size(cursor_size=7)
            assert viewer.cursor.size == 7
            layer.events.mouse_pan(mouse_pan=False)
            layer.events.mouse_zoom(mouse_zoom=False)
            assert viewer.scene.camera.mouse_pan is False
            assert viewer.scene.camera.mouse_zoom is False
            viewer.layers.remove(layer)
            before = viewer.cursor.style
            layer.events.cursor(cursor="standard")
            assert viewer.cursor.style == before
            assert external_values == ["cross", "standard"]
            assert len(viewer.layers) == 0
            results.append((external_values, viewer.dims.ndim, viewer.cursor.style))
        finally:
            layer.events.cursor.disconnect(external)
            viewer.layers.clear()
            viewer._layer_slicer.shutdown()
    assert results[1] == results[0]


@pytest.mark.parametrize("mode", ["first", "later_no_mode", "connect_error"])
def test_original_add_connection_order_live_callbacks_and_first_layer(families, mode):
    fields = [
        "mouse_pan",
        "mouse_zoom",
        "cursor",
        "cursor_size",
        "data",
        "scale",
        "units",
        "translate",
        "rotate",
        "shear",
        "affine",
        "axis_labels",
        "name",
        "reload",
    ]
    results = []
    for family in families:
        trace = []
        callbacks = {name: object() for name in NAMES}
        callbacks.pop("_layer_help_from_mode")
        callbacks.update(_on_layers_change=object(), _on_layer_reload=object())
        name_callback = object()

        class LayerList:
            _update_name = name_callback

            def __len__(self):
                trace.append(("len",))
                return 1 if mode == "first" else 2

        def dims_changed():
            trace.append(("dims",))

        def helper(layer):
            trace.append(("help", layer is supplied))
            viewer._on_layers_change = dims_changed

        def update(**kwargs):
            trace.append(
                (
                    "slice",
                    len(kwargs["layers"]) == 1 and kwargs["layers"][0] is supplied,
                )
            )

        viewer = SimpleNamespace(
            **callbacks,
            layers=LayerList(),
            _layer_help_from_mode=helper,
            _update_layers=update,
            reset_view=lambda: trace.append(("reset",)),
            dims=SimpleNamespace(_go_to_center_step=lambda: trace.append(("center",))),
        )
        connection_names = {
            "mouse_pan": "_update_mouse_pan",
            "mouse_zoom": "_update_mouse_zoom",
            "cursor": "_update_cursor",
            "cursor_size": "_update_cursor_size",
            "name": "name",
            "reload": "_on_layer_reload",
            "mode": "_on_layer_mode_change",
        }

        class Emitter:
            def __init__(self, field):
                self.field = field

            def connect(self, callback):
                key = connection_names.get(self.field, "_on_layers_change")
                expected = name_callback if key == "name" else callbacks[key]
                trace.append(("connect", self.field, callback is expected))
                if self.field == "units" and mode == "connect_error":
                    raise RuntimeError("units connect failed")

        events = {name: Emitter(name) for name in fields}
        if mode != "later_no_mode":
            events["mode"] = Emitter("mode")
        supplied = SimpleNamespace(events=SimpleNamespace(**events))
        if mode == "connect_error":
            with pytest.raises(RuntimeError, match="units connect failed"):
                family.methods["_on_add_layer"](viewer, SimpleNamespace(value=supplied))
        else:
            assert (
                family.methods["_on_add_layer"](viewer, SimpleNamespace(value=supplied))
                is None
            )
        expected = [
            ("connect", name, True) for name in fields[:7] if mode == "connect_error"
        ]
        if mode != "connect_error":
            expected = [("connect", name, True) for name in fields]
            if mode == "first":
                expected.append(("connect", "mode", True))
            expected += [("help", True), ("dims",), ("slice", True), ("len",)]
            if mode == "first":
                expected += [("reset",), ("center",)]
        assert trace == expected
        results.append(trace)
    assert results[1] == results[0]


@pytest.mark.parametrize("mode", ["success", "second_disconnect_error"])
def test_original_remove_live_owner_order_and_error_prefix(families, monkeypatch, mode):
    for family in families:
        trace = []
        event_group, new_layers = object(), object()
        viewer = SimpleNamespace(
            layers=object(), _on_layers_change=lambda: trace.append("dims")
        )

        def disconnect(group, owner):
            assert group is event_group
            trace.append(
                "viewer"
                if owner is viewer
                else "new_layers"
                if owner is new_layers
                else "wrong"
            )
            if owner is viewer:
                viewer.layers = new_layers
            elif mode == "second_disconnect_error":
                raise RuntimeError("second disconnect failed")

        with monkeypatch.context() as patch:
            patch.setitem(family.viewer_namespace, "disconnect_events", disconnect)
            event = SimpleNamespace(value=SimpleNamespace(events=event_group))
            if mode == "success":
                assert family.methods["_on_remove_layer"](viewer, event) is None
            else:
                with pytest.raises(RuntimeError, match="second disconnect failed"):
                    family.methods["_on_remove_layer"](viewer, event)
        assert trace == ["viewer", "new_layers"] + (
            ["dims"] if mode == "success" else []
        )


@pytest.mark.parametrize(
    ("method", "value_key", "active"),
    [
        ("_update_mouse_pan", "mouse_pan", True),
        ("_update_mouse_pan", "mouse_pan", False),
        ("_update_mouse_zoom", "mouse_zoom", True),
        ("_update_mouse_zoom", "mouse_zoom", False),
        ("_update_cursor", "cursor", None),
        ("_update_cursor_size", "cursor_size", None),
    ],
)
def test_original_mouse_cursor_identity_and_rhs_read_order(
    families, method, value_key, active
):
    for family in families:
        trace = []
        source, value = object(), object()
        selection = Probe("selection", {"active": object()}, trace)

        def event_read(name):
            if name == "source" and active:
                selection._values["active"] = source

        event = Probe(
            "event", {"source": source, value_key: value}, trace, read=event_read
        )
        camera = Probe("camera", {}, trace)
        cursor = Probe("cursor", {}, trace)
        viewer = Probe(
            "viewer",
            {
                "layers": Probe("layers", {"selection": selection}, trace),
                "scene": Probe("scene", {"camera": camera}, trace),
                "cursor": cursor,
            },
            trace,
        )
        family.methods[method](viewer, event)
        if active is None:
            target = "style" if method == "_update_cursor" else "size"
            assert cursor._values[target] is value
            expected = [
                ("read", "event", value_key),
                ("read", "viewer", "cursor"),
                ("write", "cursor", target),
            ]
        else:
            expected = [
                ("read", "event", "source"),
                ("read", "viewer", "layers"),
                ("read", "layers", "selection"),
                ("read", "selection", "active"),
            ]
            if active:
                assert camera._values[value_key] is value
                expected += [
                    ("read", "event", value_key),
                    ("read", "viewer", "scene"),
                    ("read", "scene", "camera"),
                    ("write", "camera", value_key),
                ]
            else:
                assert camera._values == {}
        assert trace == expected


@pytest.mark.parametrize("mode", ["success", "description_error", "no_class_entry"])
def test_original_mode_help_description_before_shortcuts(families, monkeypatch, mode):
    for family in families:
        trace = []

        class LayerProbe:
            help = "old"

            @property
            def mode(self):
                trace.append("mode")
                return "already"

        layer = LayerProbe()

        def already():
            raise AssertionError("name metadata functions are not invoked")

        def unavailable():
            raise AssertionError("name metadata functions are not invoked")

        def available():
            raise AssertionError("name metadata functions are not invoked")

        class Description:
            def __init__(self, name):
                self.name = name

            def lower(self):
                trace.append(("lower", self.name))
                if mode == "description_error":
                    raise RuntimeError("description failed")
                return "draw points"

        class Shortcuts(dict):
            def get(self, key, default):
                trace.append(("shortcut_get", key))
                return super().get(key, default)

            def __getitem__(self, key):
                trace.append(("shortcut_index", key))
                return super().__getitem__(key)

        shortcuts = Shortcuts({"napari:available": ["A", "B"]})

        def get_settings():
            trace.append("settings")
            return SimpleNamespace(shortcuts=SimpleNamespace(shortcuts=shortcuts))

        with monkeypatch.context() as patch:
            patch.setitem(
                family.viewer_namespace,
                "Points",
                LayerProbe if mode != "no_class_entry" else type("Other", (), {}),
            )
            patch.setitem(
                family.viewer_namespace,
                "points_fun_to_mode",
                [
                    (already, "already"),
                    (unavailable, "other"),
                    (available, "draw"),
                ],
            )
            patch.setitem(family.viewer_namespace, "get_settings", get_settings)
            patch.setitem(
                family.viewer_namespace,
                "action_manager",
                SimpleNamespace(
                    _actions={
                        "napari:unavailable": SimpleNamespace(
                            description=Description("unavailable")
                        ),
                        "napari:available": SimpleNamespace(
                            description=Description("available")
                        ),
                    }
                ),
            )
            if mode == "description_error":
                with pytest.raises(RuntimeError, match="description failed"):
                    family.methods["_layer_help_from_mode"](layer)
                assert layer.help == "old"
            else:
                assert family.methods["_layer_help_from_mode"](layer) is None
                assert layer.help == (
                    "" if mode == "no_class_entry" else "use <A> for draw points"
                )
        expected = ["settings"]
        if mode != "no_class_entry":
            expected += ["mode", "mode", ("lower", "unavailable")]
            if mode == "success":
                expected += [
                    ("shortcut_get", "napari:unavailable"),
                    "mode",
                    ("lower", "available"),
                    ("shortcut_get", "napari:available"),
                    ("shortcut_index", "napari:available"),
                ]
        assert trace == expected


@pytest.mark.parametrize("mode", ["reentry", "helper_error"])
def test_original_mode_change_helper_before_live_active(families, mode):
    for family in families:
        trace = []
        source = object()
        active = SimpleNamespace(help="new active help")
        selection = Probe("selection", {"active": None}, trace)

        def helper(value):
            trace.append(("helper", value is source))
            if mode == "helper_error":
                raise RuntimeError("mode helper failed")
            selection._values["active"] = active

        viewer = Probe(
            "viewer",
            {
                "_layer_help_from_mode": helper,
                "layers": Probe("layers", {"selection": selection}, trace),
                "help": "old",
            },
            trace,
        )
        if mode == "helper_error":
            with pytest.raises(RuntimeError, match="mode helper failed"):
                family.methods["_on_layer_mode_change"](
                    viewer, SimpleNamespace(source=source)
                )
            assert viewer._values["help"] == "old"
        else:
            family.methods["_on_layer_mode_change"](
                viewer, SimpleNamespace(source=source)
            )
            assert viewer._values["help"] == active.help
        expected = [("read", "viewer", "_layer_help_from_mode"), ("helper", True)]
        if mode == "reentry":
            expected += [
                ("read", "viewer", "layers"),
                ("read", "layers", "selection"),
                ("read", "selection", "active"),
                ("write", "viewer", "help"),
            ]
        assert trace == expected


@pytest.mark.parametrize(
    "mode",
    [
        "cold_unset",
        "cold_none",
        "cold_path",
        "ctor_error",
        "connect_error",
        "connect_reentry",
    ],
)
def test_original_settings_single_authority_and_partial_publication(
    families, monkeypatch, mode
):
    for family in families:
        ns = family.settings_namespace
        trace = []
        unset = ns["_NOT_SET"]
        supplied_path = (
            Path("relative-lifecycle-settings.yaml")
            if mode == "cold_path"
            else (None if mode == "cold_none" else unset)
        )

        class Settings:
            def __bool__(self):
                raise AssertionError("singleton authority is a None identity test")

            def _connect_events(self):
                trace.append(("connect", ns["_SETTINGS"] is self))
                if mode == "connect_reentry":
                    assert family.get_settings() is self
                    trace.append(("reentry",))
                if mode == "connect_error":
                    raise RuntimeError("connect failed")

        instance = Settings()

        def constructor(**kwargs):
            trace.append(("ctor", ns["_SETTINGS"] is None))
            value = kwargs["config_path"]
            assert (
                value is (None if mode == "cold_none" else unset)
                if mode != "cold_path"
                else value == supplied_path.resolve()
            )
            if mode == "ctor_error":
                raise RuntimeError("ctor failed")
            return instance

        original = ns["_SETTINGS"]
        with monkeypatch.context() as patch:
            patch.setitem(ns, "_SETTINGS", None)
            patch.setitem(ns, "NapariSettings", constructor)
            if mode in ("ctor_error", "connect_error"):
                with pytest.raises(RuntimeError, match="ctor failed|connect failed"):
                    family.get_settings(supplied_path)
            else:
                assert family.get_settings(supplied_path) is instance
            assert ns["_SETTINGS"] is (None if mode == "ctor_error" else instance)
            if mode != "ctor_error":
                assert family.get_settings() is instance
        assert ns["_SETTINGS"] is original
        assert trace == [("ctor", True)] + (
            [] if mode == "ctor_error" else [("connect", True)]
        ) + ([("reentry",)] if mode == "connect_reentry" else [])


def _named_settings_caller(function, path):
    return function(path)


def _named_plugin_caller():
    return settings_module.get_plugin_settings("demo", path_dir=None)


def test_original_shared_path_error_keeps_public_settings_and_plugin_caller(
    families, monkeypatch
):
    for family in families:
        ns = family.settings_namespace
        canonical = settings_module.get_settings()
        with monkeypatch.context() as patch:
            patch.setitem(ns, "_SETTINGS", canonical)
            assert family.get_settings() is canonical
            with pytest.raises(
                RuntimeError, match="Settings called from _named_settings_caller"
            ):
                _named_settings_caller(family.get_settings, None)
            patch.setattr(settings_module, "_raise_path_set_twice", family.raise_path)
            patch.setattr(settings_module, "_PLUGIN_SETTINGS", {"demo": object()})
            with pytest.raises(
                RuntimeError, match="Settings called from _named_plugin_caller"
            ):
                _named_plugin_caller()
