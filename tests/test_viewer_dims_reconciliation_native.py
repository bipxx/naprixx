"""Original-first Viewer/Layer dimension reconciliation and ordered callbacks."""

# All witnesses run synchronously and event callbacks disconnect before the
# next source-family iteration; none of these closures escape that iteration.
# ruff: noqa: B023
from __future__ import annotations

import ast
import hashlib
import sys
from pathlib import Path
from types import ModuleType, SimpleNamespace

import numpy as np
import pytest

from napari.components import ViewerModel as ActualViewer
from napari.components import viewer_model as viewer_module
from napari.layers import Image as ActualImage
from napari.layers.base import base as layer_module

ROOT = Path(__file__).resolve().parents[1]
BEFORE = ROOT / ".artifacts/viewer-dims-reconciliation-before"
VIEWER_SHA = "d6c9a7da12b93a14e95b5f4d9b43aabbabeb01bdf3448033b8e31869fe0c41bd"
LAYER_SHA = "2b33896223562e83cc2e64f03e26880d607a9759bd5b7176d2ba43588b248108"
VIEWER_NAMES = (
    "_sliced_extent_world_augmented",
    "_on_layer_reload",
    "_update_layers",
    "_merge_dims_and_layers_axis_labels",
    "_on_layers_change",
)
LAYER_NAMES = ("axis_labels", "_has_default_axis_labels")


def _selected(raw, class_name, names):
    tree = ast.parse(raw)
    cls = next(
        node
        for node in tree.body
        if isinstance(node, ast.ClassDef) and node.name == class_name
    )
    return [
        node
        for node in cls.body
        if isinstance(node, ast.FunctionDef) and node.name in names
    ]


def _oracle_module(name, class_name, raw, names, bindings):
    selected = _selected(raw, class_name, names)
    future = next(
        node
        for node in ast.parse(raw).body
        if isinstance(node, ast.ImportFrom) and node.module == "__future__"
    )
    cls = ast.ClassDef(
        name=class_name, bases=[], keywords=[], body=selected, decorator_list=[]
    )
    ast.copy_location(cls, selected[0])
    tree = ast.fix_missing_locations(ast.Module(body=[future, cls], type_ignores=[]))
    module = ModuleType(name)
    module.__file__ = str(
        BEFORE / ("viewer_model.py" if class_name == "ViewerModel" else "base.py")
    )
    module.__dict__.update(bindings)
    assert name not in sys.modules
    sys.modules[name] = module
    exec(compile(tree, module.__file__, "exec", dont_inherit=True), module.__dict__)
    return module


class Family:
    def __init__(self, name, viewer_cls, layer_cls, viewer_namespace):
        self.name = name
        self.viewer_methods = {name: getattr(viewer_cls, name) for name in VIEWER_NAMES}
        self.axis_property = layer_cls.axis_labels
        self.default = layer_cls._has_default_axis_labels
        self.namespace = viewer_namespace
        if name == "original":
            self.Viewer = type("OriginalViewer", (ActualViewer,), self.viewer_methods)
            self.Image = type(
                "OriginalImage",
                (ActualImage,),
                {
                    "axis_labels": self.axis_property,
                    "_has_default_axis_labels": self.default,
                },
            )
        else:
            self.Viewer, self.Image = ActualViewer, ActualImage

    def call(self, name, obj, *args, **kwargs):
        return self.viewer_methods[name](obj, *args, **kwargs)


@pytest.fixture(scope="module")
def families():
    viewer_raw = (BEFORE / "viewer_model.py").read_bytes()
    layer_raw = (BEFORE / "base.py").read_bytes()
    assert hashlib.sha256(viewer_raw).hexdigest() == VIEWER_SHA
    assert hashlib.sha256(layer_raw).hexdigest() == LAYER_SHA
    for raw, path, cls, names in (
        (viewer_raw, "napari/components/viewer_model.py", "ViewerModel", VIEWER_NAMES),
        (layer_raw, "napari/layers/base/base.py", "Layer", LAYER_NAMES),
    ):
        pinned = (ROOT / ".upstream/napari/src" / path).read_bytes()
        assert [
            ast.dump(node, include_attributes=False)
            for node in _selected(raw, cls, names)
        ] == [
            ast.dump(node, include_attributes=False)
            for node in _selected(pinned, cls, names)
        ]
    modules = []
    try:
        vm = _oracle_module(
            "_naparixx_viewer_dims_original",
            "ViewerModel",
            viewer_raw,
            VIEWER_NAMES,
            {"np": np, "LayerList": viewer_module.LayerList},
        )
        modules.append(vm)
        lm = _oracle_module(
            "_naparixx_layer_axes_original", "Layer", layer_raw, LAYER_NAMES, {}
        )
        modules.append(lm)
        yield (
            Family("original", vm.ViewerModel, lm.Layer, vm.__dict__),
            Family("current", ActualViewer, layer_module.Layer, viewer_module.__dict__),
        )
    finally:
        for module in modules:
            sys.modules.pop(module.__name__, None)


def _snapshot(viewer):
    return (
        viewer.dims.ndim,
        viewer.dims.ndisplay,
        tuple(viewer.dims.order),
        tuple(tuple(row) for row in viewer.dims.range),
        tuple(viewer.dims.axis_labels),
        (
            None
            if (units := viewer.dims.units) is None
            else tuple(str(unit) for unit in units)
        ),
        tuple(viewer.cursor.position),
        viewer._layers_had_custom_axis_labels,
        len(viewer.layers),
    )


def test_original_real_viewer_data_layer_cursor_and_label_history(families):
    results = []
    for family in families:
        viewer = family.Viewer()
        viewer._layer_slicer._force_sync = True
        authorities = (viewer.layers, viewer.dims, viewer.cursor, viewer._layer_slicer)
        states = []
        try:
            first = family.Image(
                np.arange(24).reshape(2, 3, 4),
                axis_labels=("z", "y", "x"),
                name="first",
            )
            second = family.Image(
                np.arange(48).reshape(2, 2, 3, 4),
                axis_labels=("t", "z", "y", "x"),
                name="second",
            )
            viewer.add_layer(first)
            viewer.cursor.position = (1, 2)
            family.call("_on_layers_change", viewer)
            assert viewer.cursor.position == (1.0, 2.0, 0.0)
            states.append(_snapshot(viewer))
            viewer.add_layer(second)
            states.append(_snapshot(viewer))
            first.axis_labels = None
            assert viewer._layers_had_custom_axis_labels
            second.axis_labels = None
            assert not viewer._layers_had_custom_axis_labels
            assert viewer.dims.axis_labels == viewer.layers.axis_labels
            states.append(_snapshot(viewer))
            viewer.dims.axis_labels = ("manual-t", "manual-z", "manual-y", "manual-x")
            family.call("_on_layers_change", viewer)
            assert viewer.dims.axis_labels == (
                "manual-t",
                "manual-z",
                "manual-y",
                "manual-x",
            )
            states.append(_snapshot(viewer))
            second.data = np.arange(12).reshape(3, 4)
            assert viewer.dims.ndim == len(viewer.cursor.position) == 3
            states.append(_snapshot(viewer))
            viewer.layers.remove(first)
            assert viewer.dims.ndim == len(viewer.cursor.position) == 2
            states.append(_snapshot(viewer))
            viewer.layers.remove(second)
            assert viewer.dims.ndim == 2 and not viewer._layers_had_custom_axis_labels
            states.append(_snapshot(viewer))
            assert all(
                before is after
                for before, after in zip(
                    authorities,
                    (viewer.layers, viewer.dims, viewer.cursor, viewer._layer_slicer),
                    strict=True,
                )
            )
            results.append(states)
        finally:
            viewer.layers.clear()
            viewer._layer_slicer.shutdown()
    assert results[1] == results[0]


def test_original_real_extent_none_empty_displayed_axes_and_foreign(families):
    results = []
    for family in families:
        viewer = family.Viewer()
        viewer._layer_slicer._force_sync = True
        try:
            viewer.dims.ndim = 4
            viewer.dims.order = (2, 0, 3, 1)
            empty = family.call("_sliced_extent_world_augmented", viewer, layers=[])
            assert empty.shape == (2, 4)
            np.testing.assert_array_equal(empty[0], np.full(4, -0.5))
            np.testing.assert_array_equal(empty[1], np.full(4, 511.5))
            layer = family.Image(
                np.zeros((2, 3, 4)),
                scale=(3, 2, 1),
                translate=(10, 20, 5),
                name="owned",
            )
            viewer.add_layer(layer)
            viewer.dims.order = (1, 2, 0)
            whole = family.call("_sliced_extent_world_augmented", viewer)
            np.testing.assert_array_equal(
                whole, viewer.layers._extent_world_augmented[:, viewer.dims.displayed]
            )
            explicit_empty = family.call(
                "_sliced_extent_world_augmented", viewer, layers=[]
            )
            assert explicit_empty.shape == (2, 3)
            foreign = family.Image(np.zeros((2, 2)), name='foreign "quoted"')
            with pytest.raises(ValueError) as error:
                family.call("_sliced_extent_world_augmented", viewer, layers=[foreign])
            assert error.value.args == (
                'layer "foreign "quoted"" is not part of this viewer.',
            )
            results.append((whole.tolist(), explicit_empty.tolist(), error.value.args))
        finally:
            viewer.layers.clear()
            viewer._layer_slicer.shutdown()
    assert results[1] == results[0]


def test_original_real_layer_axis_live_tuple_events_and_reentry(families):
    results = []
    for family in families:
        layer = family.Image(np.zeros((2, 3, 4)), name="axes")
        events = []
        reentered = False

        def changed(event):
            nonlocal reentered
            assert event.source is layer
            events.append(tuple(layer.axis_labels))
            if not reentered:
                reentered = True
                layer.axis_labels = ("inner-z", "inner-y", "inner-x")

        getter = family.axis_property.fget
        assert getter(layer) is layer._transforms["data2physical"].axis_labels
        layer.events.axis_labels.connect(changed)
        try:
            layer.axis_labels = ("outer-z", "outer-y", "outer-x")
            assert layer.axis_labels == ("inner-z", "inner-y", "inner-x")
            assert len(events) == 2
            layer.axis_labels = layer.axis_labels
            assert len(events) == 2
            assert not family.default(layer)
            layer.axis_labels = None
            assert family.default(layer)
            assert getter(layer) is layer._transforms["data2physical"].axis_labels
            results.append((events, tuple(layer.axis_labels)))
        finally:
            layer.events.axis_labels.disconnect(changed)
    assert results[1] == results[0]


class TraceObject:
    def __init__(self, trace, name, values, fail=None):
        object.__setattr__(self, "_trace", trace)
        object.__setattr__(self, "_name", name)
        object.__setattr__(self, "_values", values)
        object.__setattr__(self, "_fail", fail)
        object.__setattr__(self, "_on_set", None)

    def __getattr__(self, name):
        self._trace.append((self._name, "get", name))
        return self._values[name]

    def __setattr__(self, name, value):
        if name.startswith("_"):
            object.__setattr__(self, name, value)
            return
        self._trace.append((self._name, "set", name, value))
        self._values[name] = value
        if self._on_set is not None:
            self._on_set(name, value)
        if self._fail == name:
            raise RuntimeError(f"{self._name}.{name} setter failed")


class TraceLayers:
    def __init__(self, trace, values, members=()):
        self.trace, self.values, self.members = trace, values, list(members)

    def __len__(self):
        self.trace.append(("layers", "len"))
        return len(self.members)

    def __iter__(self):
        self.trace.append(("layers", "iter"))
        return iter(self.members)

    def __getattr__(self, name):
        self.trace.append(("layers", "get", name))
        if name == "_ranges" and self.values.get("fail_ranges"):
            raise RuntimeError("ranges failed")
        return self.values[name]


def _protocol_viewer(family, trace, fail=None, position=(7, 8)):
    units = ("unit-0", "unit-1", "unit-2")
    dims = TraceObject(
        trace,
        "dims",
        {
            "ndim": 3,
            "ndisplay": 2,
            "order": (2, 0, 1),
            "point": (10, 20, 30),
            "axis_labels": ("manual-0", "manual-1", "manual-2"),
            "units": ("old",),
        },
        fail=fail,
    )
    cursor = TraceObject(trace, "cursor", {"position": position})
    layers = TraceLayers(
        trace,
        {
            "extent": TraceObject(trace, "extent", {"units": units}),
            "units": units,
            "_ranges": ((0, 2, 1), (0, 3, 1), (0, 4, 1)),
            "axis_labels": ("-3", "-2", "-1"),
        },
    )
    viewer = SimpleNamespace(
        dims=dims, cursor=cursor, layers=layers, _layers_had_custom_axis_labels=False
    )
    viewer._merge_dims_and_layers_axis_labels = lambda: family.call(
        "_merge_dims_and_layers_axis_labels", viewer
    )

    def submit(**kwargs):
        trace.append(
            (
                "slicer",
                "submit",
                tuple(kwargs),
                kwargs.get("force", False),
                kwargs["layers"] is viewer.layers,
                kwargs["dims"] is viewer.dims,
            )
        )
        if fail == "submit":
            raise RuntimeError("submit failed")

    viewer._layer_slicer = SimpleNamespace(submit=submit)
    return viewer


def _outcome(fn):
    try:
        value = fn()
    except Exception as error:
        return "error", type(error), error.args
    return "return", value


@pytest.mark.parametrize("kind", ("none_short", "falsey_rebind", "truthy_long"))
def test_original_update_units_submit_cursor_live_fallback(families, kind):
    results = []
    for family in families:
        trace = []
        viewer = _protocol_viewer(
            family, trace, position=(7, 8, 9, 10) if kind == "truthy_long" else (7, 8)
        )
        layers = None
        if kind == "falsey_rebind":
            replacement = TraceLayers(
                trace,
                {
                    "extent": TraceObject(
                        trace, "replacement-extent", {"units": ("replacement",)}
                    )
                },
            )

            class Falsey:
                def __bool__(self):
                    trace.append(("argument", "bool"))
                    viewer.layers = replacement
                    return False

            layers = Falsey()
        elif kind == "truthy_long":
            layers = ["selected-layer"]
        result = family.call("_update_layers", viewer, layers=layers)
        assert result is None
        assert viewer.cursor._values["position"] == (
            (7, 8, 30, 10) if kind == "truthy_long" else (0.0, 0.0, 30)
        )
        assert tuple(type(value) for value in viewer.cursor._values["position"]) == (
            (int, int, int, int) if kind == "truthy_long" else (float, float, int)
        )
        units_set = next(
            i for i, row in enumerate(trace) if row[:3] == ("dims", "set", "units")
        )
        submit = next(
            i for i, row in enumerate(trace) if row[:2] == ("slicer", "submit")
        )
        cursor_read = next(
            i for i, row in enumerate(trace) if row == ("cursor", "get", "position")
        )
        assert units_set < submit < cursor_read
        assert trace[submit][4] is (kind != "truthy_long")
        results.append((trace, viewer.cursor._values["position"]))
    assert results[1] == results[0]


@pytest.mark.parametrize("failure", ("units", "submit"))
def test_original_update_failure_prefix_stops_before_cursor(families, failure):
    results = []
    for family in families:
        trace = []
        viewer = _protocol_viewer(family, trace, fail=failure)
        outcome = _outcome(lambda: family.call("_update_layers", viewer, layers=[]))
        assert outcome[0] == "error" and outcome[1] is RuntimeError
        assert ("cursor", "get", "position") not in trace
        assert viewer.cursor._values["position"] == (7, 8)
        results.append((outcome, trace, viewer.dims._values["units"]))
    assert results[1] == results[0]


@pytest.mark.parametrize("failure", ("none", "units", "event_layer"))
def test_original_reload_units_before_event_and_submission(families, failure):
    results = []
    for family in families:
        trace = []
        viewer = _protocol_viewer(family, trace, fail=failure)

        class Event:
            @property
            def layer(self):
                trace.append(("event", "get", "layer"))
                if failure == "event_layer":
                    raise RuntimeError("event layer failed")
                return "reloaded-layer"

        outcome = _outcome(lambda: family.call("_on_layer_reload", viewer, Event()))
        if failure == "none":
            assert outcome == ("return", None)
            submission = [row for row in trace if row[:2] == ("slicer", "submit")]
            assert len(submission) == 1 and submission[0][3] is True
        else:
            assert outcome[0] == "error" and outcome[1] is RuntimeError
            assert not any(row[:2] == ("slicer", "submit") for row in trace)
        assert (("event", "get", "layer") in trace) is (failure != "units")
        results.append((outcome, trace, viewer.dims._values["units"]))
    assert results[1] == results[0]


@pytest.mark.parametrize("mode", ("live_append_shortcircuit", "axis_reentry"))
def test_original_layers_all_live_iteration_history_and_reentry(families, mode):
    results = []
    for family in families:
        trace = []
        viewer = _protocol_viewer(family, trace)
        viewer._layers_had_custom_axis_labels = True

        class Layer:
            def __init__(self, name, default):
                self.name, self.default = name, default

            def _has_default_axis_labels(self):
                trace.append(("layer", self.name, "default"))
                if self.name == "first" and mode == "live_append_shortcircuit":
                    viewer.layers.members.append(Layer("appended", False))
                    viewer.layers.members.append(Layer("unread", None))
                if self.default is None:
                    raise AssertionError("all must short-circuit before this layer")
                return self.default

        viewer.layers.members.append(Layer("first", True))
        if mode == "axis_reentry":

            def reenter(name, value):
                if name == "axis_labels":
                    trace.append(("callback", "axis-reentry"))
                    viewer.cursor._values["position"] = (1, 2, 3, 4, 5)

            viewer.dims._on_set = reenter
        assert family.call("_on_layers_change", viewer) is None
        if mode == "live_append_shortcircuit":
            assert ("layer", "appended", "default") in trace
            assert ("layer", "unread", "default") not in trace
            assert viewer._layers_had_custom_axis_labels
            assert viewer.dims._values["axis_labels"] == (
                "manual-0",
                "manual-1",
                "manual-2",
            )
        else:
            assert not viewer._layers_had_custom_axis_labels
            assert viewer.cursor._values["position"] == (1, 2, 3)
            assert viewer.dims._values["axis_labels"] == ("-3", "-2", "-1")
        results.append(
            (
                trace,
                viewer.dims._values,
                viewer.cursor._values,
                viewer._layers_had_custom_axis_labels,
            )
        )
    assert results[1] == results[0]


@pytest.mark.parametrize("failure", ("ranges", "units", "axis_labels"))
def test_original_layers_change_partial_publication_error_prefix(families, failure):
    results = []
    for family in families:
        trace = []
        viewer = _protocol_viewer(
            family, trace, fail=None if failure == "ranges" else failure
        )
        viewer.layers.members.append(
            SimpleNamespace(_has_default_axis_labels=lambda: True)
        )
        viewer.layers.values["fail_ranges"] = failure == "ranges"
        outcome = _outcome(lambda: family.call("_on_layers_change", viewer))
        assert outcome[0] == "error" and outcome[1] is RuntimeError
        assert ("cursor", "get", "position") not in trace
        assert not viewer._layers_had_custom_axis_labels
        results.append((outcome, trace, viewer.dims._values, viewer.cursor._values))
    assert results[1] == results[0]


@pytest.mark.parametrize("mode", ("reentry", "event_error", "validation_error"))
def test_original_layer_axis_setter_order_reentry_and_partial_failure(families, mode):
    results = []
    for family in families:
        trace = []
        labels = {"value": ("old", "labels")}

        class Transform:
            @property
            def axis_labels(self):
                trace.append(("transform", "get", labels["value"]))
                return labels["value"]

            @axis_labels.setter
            def axis_labels(self, value):
                trace.append(("transform", "set", value))
                if mode == "validation_error":
                    raise ValueError("axis labels validation failed")
                labels["value"] = value

        transform = Transform()

        class Mapping:
            def __getitem__(self, key):
                trace.append(("transforms", "getitem", key))
                return transform

        layer = SimpleNamespace(_transforms=Mapping())
        reentered = False

        def event():
            nonlocal reentered
            trace.append(("event", "axis_labels", labels["value"]))
            if mode == "event_error":
                raise RuntimeError("axis event failed")
            if not reentered:
                reentered = True
                family.axis_property.fset(layer, ("inner", "labels"))

        layer.events = SimpleNamespace(axis_labels=event)
        assert family.axis_property.fget(layer) is labels["value"]
        outcome = _outcome(lambda: family.axis_property.fset(layer, ("new", "labels")))
        if mode == "reentry":
            assert outcome == ("return", None)
            assert labels["value"] == ("inner", "labels")
        else:
            assert outcome[0] == "error"
            assert outcome[1] is (RuntimeError if mode == "event_error" else ValueError)
            assert labels["value"] == (
                ("new", "labels") if mode == "event_error" else ("old", "labels")
            )
        results.append((outcome, trace, labels["value"]))
    assert results[1] == results[0]


def test_original_layer_default_returns_comparison_object_after_ndim(families):
    results = []
    for family in families:
        trace = []
        marker = object()

        class Labels:
            def __eq__(self, other):
                trace.append(("labels", "eq", other))
                return marker

        class Layer:
            @property
            def ndim(self):
                trace.append(("layer", "ndim"))
                return 3

            @property
            def axis_labels(self):
                trace.append(("layer", "axis_labels"))
                return Labels()

        assert family.default(Layer()) is marker
        assert trace == [
            ("layer", "ndim"),
            ("layer", "axis_labels"),
            ("labels", "eq", ("-3", "-2", "-1")),
        ]
        results.append(trace)
    assert results[1] == results[0]


def test_original_extent_factory_membership_and_foreign_name_format_order(
    families, monkeypatch
):
    results = []
    for family in families:
        trace = []

        class Name:
            def __format__(self, spec):
                trace.append(("name", "format", spec))
                return 'foreign "name"'

        class Foreign:
            @property
            def name(self):
                trace.append(("foreign", "name"))
                return Name()

        foreign = Foreign()

        class Owned:
            def __contains__(self, item):
                trace.append(("owned", "contains", item is foreign))
                return False

        class Supplied:
            def __iter__(self):
                trace.append(("supplied", "iter"))
                return iter([foreign])

        supplied = Supplied()

        def factory(value):
            trace.append(("LayerList", "construct", value is supplied))
            return value

        viewer = SimpleNamespace(layers=Owned())
        with monkeypatch.context() as patch:
            patch.setitem(family.namespace, "LayerList", factory)
            outcome = _outcome(
                lambda: family.call(
                    "_sliced_extent_world_augmented", viewer, layers=supplied
                )
            )
        assert outcome == (
            "error",
            ValueError,
            ('layer "foreign "name"" is not part of this viewer.',),
        )
        assert trace == [
            ("LayerList", "construct", True),
            ("supplied", "iter"),
            ("owned", "contains", True),
            ("foreign", "name"),
            ("name", "format", ""),
        ]
        results.append((outcome, trace))
    assert results[1] == results[0]
