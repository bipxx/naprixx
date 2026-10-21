"""Bounded publication checks through actual Viewer, Qt and Vispy objects.

Run with ``-p napari.utils._testsupport`` to use the original Viewer fixture
and its cleanup hooks. Cases compare the current callback with the exact
pinned callback body and its main-thread decorator, installed before Viewer
creation. Models, slicing implementations, providers and external dependencies
are shared fork services. Two unchanged pinned test helpers are loaded alone;
the upstream application package is never imported. These cases establish
callback publication behavior, not complete native Image/Labels conversion,
rendering parity, external-plugin coverage or wheel/platform acceptance.
"""

import ast
import hashlib
import importlib
import threading
import weakref
from contextlib import contextmanager
from pathlib import Path

import numpy as np
import pytest
from qtpy.QtCore import QCoreApplication, QThread
from qtpy.QtWidgets import QPushButton
from superqt import ensure_main_thread

from napari._vispy.layers.labels import (
    DirectLabelVispyColormap,
    LabelVispyColormap,
)
from napari.components import Dims
from napari.layers import Image, Labels
from napari.utils._proxies import PublicOnlyProxy
from napari.utils.colormaps import DirectLabelColormap
from napari.utils.events import Event

ROOT = Path(__file__).resolve().parents[1]
UPSTREAM = ROOT / ".upstream/napari/src/napari"
PINNED_TEST_HELPERS = {
    "_tests/utils.py":
        "1d20c223a996bbc99bd420c62003025c9e0716fe31b818727303c739da6c4b80",
    "_qt/_tests/test_async_slicing.py":
        "243454688a73fbc34dd9c2e3cf4bf666096e32729c999f8b5bf8e3f8d0fd744d",
}
PINNED_CALLBACK_SHA256 = (
    "ade606601e6a349d8903b132ba887a352663a2b0e4929dc7acfcc85615e27d5a"
)
TIMEOUT_MS = 5000
pytestmark = [pytest.mark.gui, pytest.mark.usefixtures("_enable_async")]


def _pinned_helper(relative, name, namespace):
    raw = (UPSTREAM / relative).read_bytes()
    assert hashlib.sha256(raw).hexdigest() == PINNED_TEST_HELPERS[relative]
    original = next(
        node for node in ast.parse(raw).body
        if isinstance(node, (ast.ClassDef, ast.FunctionDef)) and node.name == name
    )
    module = ast.Module(
        body=[
            ast.ImportFrom(
                module="__future__",
                names=[ast.alias(name="annotations")],
                level=0,
            ),
            original,
        ],
        type_ignores=[],
    )
    ast.fix_missing_locations(module)
    assert ast.dump(module.body[1]) == ast.dump(original)
    exec(compile(module, str(UPSTREAM / relative), "exec"), namespace)
    return namespace[name]


@pytest.fixture(scope="module")
def pinned_helpers():
    lockable_data = _pinned_helper(
        "_tests/utils.py", "LockableData", {"RLock": threading.RLock}
    )
    setup = _pinned_helper(
        "_qt/_tests/test_async_slicing.py",
        "setup_viewer_for_async_slicing",
        {},
    )
    return lockable_data, setup


@pytest.fixture(autouse=True)
def _fork_application_modules():
    for name in (
        "napari.viewer",
        "napari._qt.qt_viewer",
        "napari.layers._scalar_field._slice",
        "napari.layers._scalar_field.scalar_field",
        "napari.layers.image.image",
        "napari.layers.labels.labels",
        "napari._vispy.layers.scalar_field",
        "napari._vispy.layers.labels",
    ):
        module = importlib.import_module(name)
        expected = ROOT / "src" / Path(*name.split(".")).with_suffix(".py")
        assert Path(module.__file__).resolve() == expected.resolve()


@pytest.fixture(autouse=True, params=["native", "pinned"])
def _slice_ready_implementation(request, monkeypatch, _fork_application_modules):
    if request.param == "native":
        return
    module = importlib.import_module("napari._qt.qt_viewer")
    path = UPSTREAM / "_qt/qt_viewer.py"
    raw = path.read_bytes()
    assert hashlib.sha256(raw).hexdigest() == PINNED_CALLBACK_SHA256
    owner = next(
        node for node in ast.parse(raw).body
        if isinstance(node, ast.ClassDef) and node.name == "QtViewer"
    )
    method = next(
        node for node in owner.body
        if isinstance(node, ast.FunctionDef) and node.name == "_on_slice_ready"
    )
    assert [ast.dump(node) for node in method.decorator_list] == [
        "Name(id='ensure_main_thread', ctx=Load())"
    ]
    oracle = ast.Module(body=[
        ast.ImportFrom(
            module="__future__", names=[ast.alias(name="annotations")], level=0
        ),
        ast.ClassDef(
            name=owner.name, bases=[], keywords=[], body=[method], decorator_list=[]
        ),
    ], type_ignores=[])
    ast.fix_missing_locations(oracle)
    assert ast.dump(oracle.body[1].body[0]) == ast.dump(method)
    namespace = dict(vars(module))
    exec(compile(oracle, str(path), "exec"), namespace)
    monkeypatch.setattr(
        module.QtViewer, "_on_slice_ready", namespace["QtViewer"]._on_slice_ready
    )


@pytest.fixture(autouse=True)
def _isolated_plugin_manager(npe2pm):
    # The pinned napari conftest activates the same original npe2 fixture.
    return npe2pm


@pytest.fixture(autouse=True)
def _fresh_gui_settings(monkeypatch):
    # Match the upstream settings isolation while restoring the prior singleton.
    from napari import settings
    from napari.settings import NapariSettings

    monkeypatch.setattr(NapariSettings.model_fields["config_path"], "default", None)
    original_save = NapariSettings.save

    def isolated_save(self, path=None, **kwargs):
        if path or self.config_path:
            return original_save(self, path, **kwargs)
        return None

    monkeypatch.setattr(NapariSettings, "save", isolated_save)
    monkeypatch.setattr(settings, "_SETTINGS", None)


@pytest.fixture
def _enable_async(_fresh_gui_settings, make_napari_viewer):
    # The original Viewer fixture resets settings during its own setup.
    from napari.settings import get_settings

    get_settings().experimental.async_ = True


def _wait_for_slice(qtbot, layer, visual, expected):
    def published():
        assert layer.loaded
        np.testing.assert_array_equal(layer._slice.image.raw, expected)
        np.testing.assert_array_equal(visual.node._data, layer._slice.image.view)

    qtbot.waitUntil(published, timeout=TIMEOUT_MS)


def _queue_from_worker(callback):
    results, errors = [], []

    def run():
        try:
            results.append(callback())
        except BaseException as error:
            errors.append(error)

    worker = threading.Thread(target=run, daemon=True)
    worker.start()
    worker.join(timeout=TIMEOUT_MS / 1000)
    assert not worker.is_alive()
    assert not errors and len(results) == 1
    return results[0]


def _settle_startup(qtbot, viewer, layer, visual, data):
    @ensure_main_thread
    def barrier():
        return None

    previous = None
    for _ in range(4):
        viewer._layer_slicer.wait_until_idle(timeout=TIMEOUT_MS / 1000)
        # Posting from another thread places this after already posted ready
        # callbacks. Repeat until two barriers observe the same idle state.
        future = _queue_from_worker(barrier)
        qtbot.waitUntil(future.done, timeout=TIMEOUT_MS)
        assert future.result(timeout=0) is None
        state = layer._slicing_state
        current = (state._last_slice_id, layer._slice.request_id)
        idle = (
            layer.loaded and not viewer._layer_slicer._layers_to_task
            and not viewer._layer_slicer._task_to_requests
        )
        if idle and current == previous:
            _wait_for_slice(
                qtbot, layer, visual, data[viewer.dims.current_step[0]]
            )
            return
        previous = current if idle else None
    pytest.fail("Viewer startup did not reach a stable idle publication state")


@contextmanager
def _slice_events(viewer, layer):
    phase = ["startup"]
    app_thread = QCoreApplication.instance().thread()

    def snapshot(kind):
        state = layer._slicing_state
        return {
            "kind": kind, "phase": phase[0],
            "request_id": layer._slice.request_id,
            "pending_id": state._last_slice_id, "loaded": layer.loaded,
            "thread": threading.get_ident(),
            "on_qt_main": QThread.currentThread() is app_thread,
        }

    trace = [snapshot("initial")]

    def ready(event):
        responses = {
            response.request_id: response.image.raw.copy()
            for weak_layer, response in event.value.items()
            if weak_layer() is layer
        }
        trace.append({
            "kind": "ready", "phase": phase[0], "responses": responses,
            "thread": threading.get_ident(),
        })

    def loaded():
        trace.append(snapshot("loaded"))

    def set_data(event):
        item = snapshot("set_data")
        item.update({
            "raw": layer._slice.image.raw.copy(),
            "removed": layer not in viewer.layers,
            "window_closed": not hasattr(viewer.window, "_qt_window"),
        })
        trace.append(item)

    loaded_signal = layer._slicing_state.loaded_data
    viewer._layer_slicer.events.ready.connect(ready)
    loaded_signal.connect(loaded)
    layer.events.set_data.connect(set_data)
    try:
        yield trace, phase
    finally:
        viewer._layer_slicer.events.ready.disconnect(ready)
        loaded_signal.disconnect(loaded)
        layer.events.set_data.disconnect(set_data)


def _assert_slice_events(trace):
    main_thread = trace[0]["thread"]
    known_loaded = trace[0]["loaded"]
    for item in trace[1:]:
        if item["kind"] == "ready":
            assert item["thread"] != main_thread, trace
            continue
        assert item["thread"] == main_thread and item["on_qt_main"], trace
        if item["kind"] == "loaded":
            if item["loaded"]:
                assert item["request_id"] == item["pending_id"], trace
            known_loaded = item["loaded"]
        else:
            assert item["loaded"] == known_loaded, trace
            if item["request_id"] == item["pending_id"]:
                assert item["loaded"], trace


def test_actual_async_labels_rebuild_colormap_and_publish_texture(
    make_napari_viewer, qtbot, pinned_helpers
):
    lockable_data, _ = pinned_helpers
    viewer = make_napari_viewer()
    data = np.zeros((3, 4, 5), dtype=np.uint32)
    data[1, 1, 2] = 42
    data[2, 2, 3] = 1
    provider = lockable_data(data)
    layer = Labels(provider, multiscale=False)
    layer.colormap = DirectLabelColormap(color_dict={
        None: [0, 0, 0, 0],
        0: [0, 0, 0, 0],
        1: [0, 0.25, 1, 1],
        42: [1, 0, 0, 1],
    })
    assert layer._slice.empty

    # Hold the actual provider read so the real Viewer visual starts with the
    # asynchronous placeholder, then receives the completed uint32 response.
    with provider.lock:
        viewer.add_layer(layer)
        visual = viewer.window._qt_viewer.layer_to_visual[layer]
        assert layer._slice.empty
        assert visual._colormap_dtypes == (np.dtype("uint8"),) * 2
        assert isinstance(visual.node.cmap, LabelVispyColormap)

    expected = data[viewer.dims.current_step[0]]
    _wait_for_slice(qtbot, layer, visual, expected)
    assert visual._colormap_dtypes == (np.dtype("uint32"), np.dtype("uint8"))
    assert isinstance(visual.node.cmap, DirectLabelVispyColormap)
    assert visual.node._data.dtype == np.dtype("uint8")
    colormap = visual.node.cmap
    assert layer.data is provider

    viewer.dims.current_step = (2, 0, 0)
    _wait_for_slice(qtbot, layer, visual, data[2])
    assert visual.node.cmap is colormap
    assert visual._colormap_dtypes == (np.dtype("uint32"), np.dtype("uint8"))
    assert visual.node._data.dtype == np.dtype("uint8")
    assert viewer.window._qt_viewer.layer_to_visual[layer] is visual
    assert layer.data is provider


def test_actual_async_ready_updates_layer_on_qt_main_thread(
    make_napari_viewer, qtbot, pinned_helpers
):
    lockable_data, setup = pinned_helpers
    viewer = make_napari_viewer()
    data = np.arange(60, dtype=np.float32).reshape(3, 4, 5)
    provider = lockable_data(data)
    layer = Image(provider, multiscale=False, contrast_limits=(0, 59))
    visual = setup(viewer, layer)
    with _slice_events(viewer, layer) as (trace, phase):
        _settle_startup(qtbot, viewer, layer, visual, data)
        phase[0] = "target"
        with provider.lock:
            viewer.dims.current_step = (2, 0, 0)
            assert not layer.loaded
        _wait_for_slice(qtbot, layer, visual, data[2])
        _assert_slice_events(trace)
        target = [item for item in trace if item["phase"] == "target"]
        responses = {
            request_id: raw for item in target if item["kind"] == "ready"
            for request_id, raw in item["responses"].items()
        }
        publications = [item for item in target if item["kind"] == "set_data"]
        assert responses and publications, trace
        publication_ids = [item["request_id"] for item in publications]
        assert len(publication_ids) == len(set(publication_ids)), trace
        assert set(publication_ids) == set(responses), trace
        latest = layer._slicing_state._last_slice_id
        assert any(
            item["request_id"] == latest and item["loaded"]
            for item in publications
        ), trace
        for item in publications:
            assert item["request_id"] in responses, trace
            np.testing.assert_array_equal(item["raw"], responses[item["request_id"]])
            np.testing.assert_array_equal(item["raw"], data[2])
        assert layer.data is provider


def test_actual_plugin_widget_keeps_identity_during_async_publication(
    make_napari_viewer, qtbot, pinned_helpers, npe2pm
):
    _, setup = pinned_helpers
    viewer = make_napari_viewer()
    data = np.arange(60, dtype=np.float32).reshape(3, 4, 5)
    layer = Image(data, contrast_limits=(0, 59))
    visual = setup(viewer, layer)
    constructed = []

    class SliceWidget(QPushButton):
        def __init__(self, napari_viewer):
            super().__init__("Slice plugin")
            self.viewer_proxy = napari_viewer
            self.publications = []
            self.clicks = []
            constructed.append(self)

        def observe_slice(self, event):
            self.publications.append((
                self, self.layer, self.layer._slice.image.raw.copy(),
                QThread.currentThread() is self.thread(),
            ))

        def click_callback(self, checked=False):
            self.clicks.append((self, self.viewer_proxy))

    # Use the exact manager installed by the original npe2 fixture. The neutral
    # name also avoids Executable.plugin_name's installed-distribution prefix
    # heuristic for contributions added after manifest construction.
    with npe2pm.tmp_plugin(name="zzviewer-slice-fixture") as plugin:
        from napari.plugins import _npe2

        assert plugin.plugin_manager is npe2pm
        assert _npe2.pm.instance() is npe2pm
        plugin.contribute.widget(display_name="Slice widget")(SliceWidget)
        contribution, = plugin.manifest.contributions.widgets
        assert contribution.plugin_name == plugin.name
        assert _npe2.get_widget_contribution(plugin.name, "Slice widget") == (
            SliceWidget, "Slice widget"
        )
        with _slice_events(viewer, layer) as (trace, phase):
            _settle_startup(qtbot, viewer, layer, visual, data)
            _assert_slice_events(trace)
        dock, widget = viewer.window.add_plugin_dock_widget(
            plugin.name, "Slice widget"
        )
        assert constructed == [widget]
        assert isinstance(widget.viewer_proxy, PublicOnlyProxy)
        assert widget.viewer_proxy.__wrapped__ is viewer
        proxy = widget.viewer_proxy
        widget.layer = layer
        callback = widget.observe_slice
        click_callback = widget.click_callback
        layer.events.set_data.connect(callback)
        widget.clicked.connect(click_callback)
        try:
            with _slice_events(viewer, layer) as (trace, phase):
                phase[0] = "target"
                widget.click()
                viewer.dims.current_step = (2, 0, 0)
                _wait_for_slice(qtbot, layer, visual, data[2])
                widget.click()
                _assert_slice_events(trace)
                publications = [
                    item for item in trace if item["kind"] == "set_data"
                ]
                assert publications, trace
                latest = layer._slicing_state._last_slice_id
                assert any(
                    item["request_id"] == latest and item["loaded"]
                    for item in publications
                ), trace
                assert len(widget.publications) == len(publications), trace
            assert widget.viewer_proxy is proxy
            assert dock.inner_widget() is widget
            assert viewer.window.dock_widgets[dock.name] is widget
            same_dock, same_widget = viewer.window.add_plugin_dock_widget(
                plugin.name, "Slice widget"
            )
            assert same_dock is dock and same_widget is widget
            assert callback.__self__ is widget
            assert widget.publications
            for owner, published_layer, raw, on_owner_thread in widget.publications:
                assert owner is widget and published_layer is layer and on_owner_thread
                np.testing.assert_array_equal(raw, data[2])
            assert widget.clicks == [(widget, proxy), (widget, proxy)]
            assert viewer.window._qt_viewer.layer_to_visual[layer] is visual
        finally:
            layer.events.set_data.disconnect(callback)
            widget.clicked.disconnect(click_callback)
            viewer.window.remove_dock_widget(dock)
            with qtbot.waitSignal(widget.destroyed, timeout=TIMEOUT_MS):
                widget.deleteLater()


@pytest.mark.parametrize("transition", ["remove_layer", "close_viewer"])
def test_queued_stale_response_publishes_to_live_layer_after_detachment(
    make_napari_viewer, qtbot, pinned_helpers, transition
):
    _, setup = pinned_helpers
    viewer = make_napari_viewer()
    data = np.arange(60, dtype=np.float32).reshape(3, 4, 5)
    layer = Image(data, contrast_limits=(0, 59))
    visual = setup(viewer, layer)
    qt_viewer = viewer.window._qt_viewer
    with _slice_events(viewer, layer) as (trace, phase):
        _settle_startup(qtbot, viewer, layer, visual, data)
        _assert_slice_events(trace)
        phase[0] = "queued"
        # A new model carries public dimensions without copying the Viewer's
        # event subscribers into this independently generated response.
        response_dims = Dims(**viewer.dims.model_dump())
        response_dims.current_step = (2, 0, 0)
        response = layer._slicing_state._make_slice_request(response_dims)()
        queued_id = response.request_id
        layer._slicing_state._set_unloaded_slice_id(queued_id + 1)
        assert not layer.loaded

        future = _queue_from_worker(lambda: qt_viewer._on_slice_ready(Event(
            "ready", value={weakref.ref(layer): response}
        )))
        assert not future.done()
        assert not any(
            item["kind"] == "set_data" and item["request_id"] == queued_id
            for item in trace
        ), trace

        # The pinned callback follows live weakrefs even if the response is
        # stale and the layer has been detached. ID matching only controls
        # the loaded notification; introducing a discard would change behavior.
        if transition == "remove_layer":
            viewer.layers.remove(layer)
        else:
            viewer.close()
        assert layer not in viewer.layers
        if transition == "close_viewer":
            assert not hasattr(viewer.window, "_qt_window")

        qtbot.waitUntil(future.done, timeout=TIMEOUT_MS)
        assert future.result(timeout=0) is None
        _assert_slice_events(trace)
        publications = [
            item for item in trace if item["kind"] == "set_data"
            and item["request_id"] == queued_id
        ]
        assert len(publications) == 1, trace
        publication = publications[0]
        assert publication["phase"] == "queued" and publication["removed"], trace
        assert not publication["loaded"], trace
        if transition == "close_viewer":
            assert publication["window_closed"], trace
        np.testing.assert_array_equal(publication["raw"], data[2])
        np.testing.assert_array_equal(layer._slice.image.raw, data[2])
        assert not layer.loaded
