"""Missing close/allocation contracts; immutable original bodies are test-only.

Pinned behavior and deliberate fork cleanup are compared separately. The one
actual GUI case proves identities/cancel-stop-wait, not complete Viewer teardown.
"""
from __future__ import annotations

import ast
import builtins
import gc
import hashlib
import importlib
import json
import types
import weakref
from pathlib import Path
from types import SimpleNamespace

import pytest

ROOT = Path(__file__).resolve().parents[1]
BEFORE_INDEX = ROOT / ".artifacts/viewer-close-before/index.json"
BEFORE_SHA256 = "fba037056d49fc6195bbe98c0da4ce2d841f1a39057b4185a277dfc7aced450d"
assert hashlib.sha256(BEFORE_INDEX.read_bytes()).hexdigest() == BEFORE_SHA256
_INDEX = json.loads(BEFORE_INDEX.read_bytes())
GUI_HELPER = ROOT / "tests/test_viewer_slice_gui.py"
GUI_HELPER_SHA256 = "5c7d5277c22232a3a0be0e5b721e330110448bc7973e030b814c88360b8d688b"
assert hashlib.sha256(GUI_HELPER.read_bytes()).hexdigest() == GUI_HELPER_SHA256

# Reuse the exact accepted GUI10 settings fixture alone. This does not import
# its application modules, register its other fixtures or collect ten tests.
_settings_tree = ast.parse(GUI_HELPER.read_bytes())
_settings_node = next(
    n for n in _settings_tree.body
    if isinstance(n, ast.FunctionDef) and n.name == "_fresh_gui_settings"
)
_settings_module = ast.fix_missing_locations(
    ast.Module(body=[_settings_node], type_ignores=[])
)
_settings_namespace = {"pytest": pytest}
exec(compile(_settings_module, str(GUI_HELPER), "exec"), _settings_namespace)
_fresh_gui_settings = _settings_namespace["_fresh_gui_settings"]

TARGETS = {
    "Window": ("_qt/qt_main_window.py", ("close", "_connect_theme", "_disconnect_theme")),
    "MainWindow": ("_qt/qt_main_window.py", ("_stop_worker_threads",)),
    "QtViewer": ("_qt/qt_viewer.py", ("closeEvent",)),
    "QtDims": ("_qt/widgets/qt_dims.py", ("stop", "closeEvent")),
    "StatusChecker": ("_qt/threads/status_checker.py", ("close_terminate", "terminate")),
    "AnimationThread": ("_qt/widgets/qt_dims_slider.py", ("_stop",)),
}
CLASS_NAMES = {"MainWindow": "_QtMainWindow"}


def _frozen_source(kind, relative):
    directory = "current" if kind == "current_fork" else kind
    expected_path = ".artifacts/viewer-close-before/" + directory + "/" + relative
    row = next(item for item in _INDEX["files"] if item["before"] == expected_path)
    path = ROOT / row["before"]
    raw = path.read_bytes()
    assert hashlib.sha256(raw).hexdigest() == row["before_sha256"]
    return path, raw


def _policy_owner(relative, name, methods, kind):
    module_name = "napari." + relative[:-3].replace("/", ".")
    loaded = importlib.import_module(module_name)
    assert Path(loaded.__file__).resolve() == (ROOT / "src/napari" / relative).resolve()
    if kind == "current":
        path = ROOT / "src/napari" / relative
        raw = path.read_bytes()
    else:
        path, raw = _frozen_source(kind, relative)
    original = next(n for n in ast.parse(raw).body if isinstance(n, ast.ClassDef) and n.name == name)
    selected = [n for n in original.body if isinstance(n, ast.FunctionDef) and n.name in methods]
    assert len(selected) == len(methods)
    namespace = dict(vars(loaded))
    namespace["__name__"] = "_naparixx_close_" + kind + "_" + name
    namespace["__builtins__"] = dict(vars(builtins))
    namespace["_actual_owner"] = getattr(loaded, name)
    # Actual parent supplies unchanged callback descriptors. Selected methods
    # retain exact source bodies/decorators and a genuine lexical class.
    owner = ast.ClassDef(
        name=name, bases=[ast.Name(id="_actual_owner", ctx=ast.Load())],
        keywords=[], body=selected, decorator_list=[]
    )
    tree = ast.fix_missing_locations(ast.Module(body=[
        ast.ImportFrom(module="__future__", names=[ast.alias(name="annotations")], level=0),
        owner,
    ], type_ignores=[]))
    assert [ast.dump(n) for n in owner.body] == [ast.dump(n) for n in selected]
    exec(compile(tree, str(path), "exec", dont_inherit=True), namespace)
    cls = namespace[name]
    # Existing weak-slot metadata is binding glue. The test-only class owns
    # its implementations; no separate QObject receiver is constructed here.
    retain = namespace.get("retain_slot_implementations")
    if retain is not None:
        cls = retain(cls)
    return cls, namespace


@pytest.fixture(scope="module")
def oracles():
    result = {}
    for kind in ("pinned", "current_fork", "current"):
        owners = {}
        for key, (relative, methods) in TARGETS.items():
            name = CLASS_NAMES.get(key, key)
            cls, namespace = _policy_owner(relative, name, methods, kind)
            owners[key] = cls
            owners[key + "_namespace"] = namespace
        result[kind] = SimpleNamespace(**owners)
    return result


def _outcome(call):
    try:
        return "value", call()
    except Exception as error:
        return "error", type(error).__name__, str(error)


def _common(oracles, scenario, contract):
    for kind in ("pinned", "current_fork", "current"):
        assert scenario(oracles[kind]) == contract, kind


@pytest.mark.parametrize(
    ("relative", "name"),
    [("_qt/qt_main_window.py", "Window"), ("_vispy/canvas.py", "VispyCanvas")],
)
def test_source_identical_plain_native_namespace_candidate(relative, name):
    loaded = importlib.import_module("napari." + relative[:-3].replace("/", "."))
    from naparixx import _native

    path, raw = _frozen_source("current_fork", relative)
    tree = ast.parse(raw)
    owner = next(n for n in tree.body if isinstance(n, ast.ClassDef) and n.name == name)
    assert owner.bases == []
    unchanged_body = [ast.dump(n) for n in owner.body]
    owner.bases = [ast.Attribute(value=ast.Name(id="_native", ctx=ast.Load()),
                                 attr="VispyNamespaceBase", ctx=ast.Load())]
    namespace = dict(vars(loaded))
    namespace.update(_native=_native, __name__="_naparixx_close_candidate_" + name)
    namespace["__builtins__"] = dict(vars(builtins))
    candidate_tree = ast.fix_missing_locations(ast.Module(body=[
        ast.ImportFrom(module="__future__", names=[ast.alias(name="annotations")], level=0),
        owner,
    ], type_ignores=[]))
    assert [ast.dump(n) for n in owner.body] == unchanged_body
    exec(compile(candidate_tree, str(path), "exec", dont_inherit=True), namespace)
    cls = namespace[name]
    assert _native.VispyNamespaceBase in cls.__mro__
    assert cls.__doc__ == getattr(loaded, name).__doc__
    instance = object.__new__(cls)
    payload = object()
    instance.payload = payload
    saved = vars(instance)
    assert saved is instance.__dict__ and saved["payload"] is payload
    replacement = {"payload": payload}
    instance.__dict__ = replacement
    assert vars(instance) is replacement and saved["payload"] is payload
    del instance.__dict__
    assert vars(instance) == {} and saved["payload"] is payload
    instance.__dict__ = replacement

    class Derived(cls):
        __slots__ = ("extra",)

        @property
        def marker(self):
            return self.__dict__["payload"]

    derived = object.__new__(Derived)
    derived.payload = payload
    derived.extra = 3
    assert derived.marker is payload and derived.extra == 3
    reference, derived_reference = weakref.ref(instance), weakref.ref(derived)
    del instance, derived
    gc.collect()
    assert reference() is None and derived_reference() is None
    assert saved["payload"] is payload and replacement["payload"] is payload
    cyclic = object.__new__(cls)
    cyclic.cycle = cyclic
    cyclic_reference = weakref.ref(cyclic)
    del cyclic
    gc.collect()
    assert cyclic_reference() is None

    actual = getattr(loaded, name)
    actual_tree = ast.parse((ROOT / "src/napari" / relative).read_bytes())
    declaration = next(n for n in actual_tree.body if isinstance(n, ast.ClassDef) and n.name == name)
    if any(isinstance(base, ast.Attribute) and base.attr == "VispyNamespaceBase"
           for base in declaration.bases):
        assert _native.VispyNamespaceBase in actual.__mro__


@pytest.mark.parametrize("branch", ["success", "teardown", "qt_viewer", "qt_window", "reenter"])
def test_window_close_preserves_completed_prefix_and_reentry(oracles, branch):
    def scenario(impl):
        trace = []
        owner = object.__new__(impl.Window)
        nested = [False]

        def teardown():
            trace.append("teardown")
            if branch == "teardown":
                raise RuntimeError("teardown failed")

        def viewer_close():
            trace.append("qt-viewer")
            if branch == "qt_viewer":
                raise RuntimeError("qt_viewer failed")
            if branch == "reenter" and not nested[0]:
                nested[0] = True
                owner.close()

        def window_close():
            trace.append("qt-window")
            if branch == "qt_window":
                raise RuntimeError("qt_window failed")

        owner._teardown = teardown
        owner._qt_window = SimpleNamespace(
            close=window_close, _qt_viewer=SimpleNamespace(close=viewer_close)
        )
        outcome = _outcome(owner.close)
        present = hasattr(owner, "_qt_window")
        if branch == "success":
            assert owner.close() is None
        return tuple(trace), outcome, present

    prefix = {
        "success": ("teardown", "qt-viewer", "qt-window"),
        "teardown": ("teardown",),
        "qt_viewer": ("teardown", "qt-viewer"),
        "qt_window": ("teardown", "qt-viewer", "qt-window"),
        "reenter": ("teardown", "qt-viewer", "teardown", "qt-viewer", "qt-window"),
    }[branch]
    outcome = {
        "success": ("value", None),
        "teardown": ("error", "RuntimeError", "teardown failed"),
        "qt_viewer": ("error", "RuntimeError", "qt_viewer failed"),
        "qt_window": ("error", "RuntimeError", "qt_window failed"),
        "reenter": ("error", "AttributeError", "'Window' object has no attribute '_qt_window'"),
    }[branch]
    _common(oracles, scenario, (prefix, outcome, branch not in ("success", "reenter")))


def test_matched_fork_theme_disconnect_releases_only_registered_callback_roots(oracles):
    def scenario(impl):
        class Signal:
            def __init__(self):
                self.callbacks = []

            def connect(self, callback):
                self.callbacks.append(callback)

            def disconnect(self, callback):
                self.callbacks.remove(callback)

        names = ("background", "foreground", "primary", "secondary", "highlight",
                 "text", "warning", "current", "icon", "font_size", "canvas")
        events = SimpleNamespace(**{name: Signal() for name in names})
        owner = object.__new__(impl.Window)
        owner._qt_window = SimpleNamespace(
            _qt_viewer=SimpleNamespace(_console=None)
        )
        owner._connect_theme(SimpleNamespace(events=events))
        saved = events.canvas.callbacks[0]
        registered_bound = isinstance(saved, types.MethodType) and saved.__self__ is owner
        outcome = _outcome(lambda: owner._disconnect_theme(SimpleNamespace(events=events)))
        remaining = tuple(name for name in names if getattr(events, name).callbacks)
        reference = weakref.ref(owner)
        # Record the deliberate pinned mismatch before fixture cleanup.
        events.canvas.callbacks.clear()
        del owner
        gc.collect()
        held_by_saved = reference() is not None
        del saved
        gc.collect()
        released = reference() is None
        return registered_bound, outcome[0:2], remaining, held_by_saved, released

    assert scenario(oracles["pinned"]) == (
        False, ("error", "ValueError"), ("canvas",), True, True
    )
    fork_contract = (True, ("value", None), (), True, True)
    assert scenario(oracles["current_fork"]) == fork_contract
    assert scenario(oracles["current"]) == fork_contract


def test_stop_requests_precede_status_and_animation_waits(oracles):
    def scenario(impl):
        trace = []

        class Status:
            def __setattr__(self, name, value):
                if name in ("_allow_start", "_terminate"):
                    trace.append((name, value))
                object.__setattr__(self, name, value)

        status = Status()
        vars(status).update(_allow_start=True, _terminate=False)
        status._need_status_update = SimpleNamespace(
            set=lambda: trace.append(("status-event", status._terminate))
        )
        status.terminate = types.MethodType(impl.StatusChecker.terminate, status)
        status.close_terminate = types.MethodType(impl.StatusChecker.close_terminate, status)
        status.wait = lambda: trace.append("status-wait")
        animation = SimpleNamespace(
            _waiter=SimpleNamespace(set=lambda: trace.append("animation-event"))
        )
        animation._stop = types.MethodType(impl.AnimationThread._stop, animation)
        animation.wait = lambda: trace.append("animation-wait")
        dims = SimpleNamespace(_animation_thread=animation)
        dims.stop = types.MethodType(impl.QtDims.stop, dims)
        owner = SimpleNamespace(status_thread=status, _qt_viewer=SimpleNamespace(dims=dims))
        assert impl.MainWindow._stop_worker_threads(owner) is None
        return tuple(trace)

    _common(oracles, scenario, (
        ("_allow_start", False), ("_terminate", True), ("status-event", True),
        "status-wait", "animation-event", "animation-wait",
    ))


@pytest.mark.parametrize("kind", ["viewer", "dims"])
def test_fork_qt_cleanup_differences_remain_explicit(oracles, monkeypatch, kind):
    def scenario(impl):
        trace = []
        event = SimpleNamespace(accept=lambda: trace.append("accept"))
        if kind == "viewer":
            owner = SimpleNamespace(
                _layers=None,
                dims=SimpleNamespace(stop=lambda: trace.append("stop"),
                                     close=lambda: trace.append("dims-close")),
                canvas=SimpleNamespace(delete=lambda: trace.append("canvas-delete")),
                _console=None,
                dockConsole=SimpleNamespace(deleteLater=lambda: trace.append("dock-delete")),
            )
            assert impl.QtViewer.closeEvent(owner, event) is None
            return tuple(trace)
        marker = object()
        widget = SimpleNamespace(dims=marker)
        owner = SimpleNamespace(
            dims=SimpleNamespace(events=marker), _animation_thread=object(),
            slider_widgets=[widget], _displayed_sliders=[True],
            stop=lambda: trace.append("stop"),
            deleteLater=lambda: trace.append("self-delete"),
        )
        widget.qt_dims = owner
        widget._set_slice_from_label = lambda: None
        widget._clear_label_focus = lambda: None
        widget.curslice_label = SimpleNamespace(editingFinished=SimpleNamespace(
            disconnect=lambda callback: trace.append(
                ("disconnect-slice", callback is widget._set_slice_from_label)
            )
        ))
        widget.axis_label = SimpleNamespace(editingFinished=SimpleNamespace(
            disconnect=lambda callback: trace.append(
                ("disconnect-axis", callback is widget._clear_label_focus)
            )
        ))
        widget.deleteLater = lambda: trace.append("widget-delete")
        sliders, displayed = owner.slider_widgets, owner._displayed_sliders

        def disconnect(events, target):
            assert events is marker
            trace.append(
                "disconnect-self" if target is owner else
                "disconnect-thread" if target is owner._animation_thread else
                "disconnect-widget"
            )

        with monkeypatch.context() as patch:
            patch.setitem(impl.QtDims_namespace, "disconnect_events", disconnect)
            assert impl.QtDims.closeEvent(owner, event) is None
        return (tuple(trace), len(sliders), len(displayed),
                owner.dims is None, widget.dims is None, widget.qt_dims is None)

    if kind == "viewer":
        pinned = ("stop", "canvas-delete", "dock-delete", "accept")
        fork = ("stop", "dims-close", "canvas-delete", "dock-delete", "accept")
    else:
        pinned = (("widget-delete", "self-delete", "accept"), 1, 1, False, False, False)
        fork = (("stop", "disconnect-self", "disconnect-thread", "disconnect-widget",
                 ("disconnect-slice", True), ("disconnect-axis", True),
                 "widget-delete", "self-delete", "accept"), 0, 0, True, True, True)
    assert scenario(oracles["pinned"]) == pinned
    assert scenario(oracles["current_fork"]) == fork
    assert scenario(oracles["current"]) == fork


@pytest.mark.gui
def test_actual_qt_model_identity_pending_cancel_and_stop_wait(qapp, qtbot, monkeypatch):
    import napari
    from napari._qt.qt_main_window import _QtMainWindow
    from napari._qt.qt_viewer import QtViewer
    from napari._qt.threads.status_checker import StatusChecker
    from napari._qt.widgets.qt_dims import QtDims
    from napari._qt.widgets.qt_dims_slider import AnimationThread
    from napari.components.viewer_model import ViewerModel
    from napari.utils.events import EventedModel
    from napari.utils.key_bindings import KeymapProvider
    from napari.utils.mouse_bindings import MousemapProviderPydantic
    from naparixx import _native
    from naparixx.qt_host import _pointer
    from qtpy.QtCore import QCoreApplication, QEvent, QThread
    from qtpy.QtWidgets import QMainWindow, QSplitter, QWidget

    viewer = napari.Viewer(show=False)
    window = viewer.window
    qt_window, qt_viewer = window._qt_window, window._qt_viewer
    dims, canvas = qt_viewer.dims, qt_viewer.canvas
    status, animation = qt_window.status_thread, dims._animation_thread
    sliders = list(dims.slider_widgets)
    trace = []
    try:
        assert type(qt_window) is _QtMainWindow and type(qt_viewer) is QtViewer
        assert type(dims) is QtDims and type(status) is StatusChecker
        assert type(animation) is AnimationThread
        assert type(_QtMainWindow) is type(QMainWindow)
        assert type(QtViewer) is type(QSplitter) and type(QtDims) is type(QWidget)
        assert type(StatusChecker) is type(QThread) and type(AnimationThread) is type(QThread)
        assert ViewerModel.__bases__ == (KeymapProvider, MousemapProviderPydantic, EventedModel)
        assert napari.Viewer.__bases__ == (ViewerModel,)
        for cls in (QtViewer, QtDims, _QtMainWindow, StatusChecker, AnimationThread,
                    napari.Viewer, ViewerModel):
            assert _native.VispyNamespaceBase not in cls.__mro__
        private = viewer.__pydantic_private__
        assert _native.model_transaction_owner(viewer).private_mapping is private
        assert private["_layer_slicer"] is viewer._layer_slicer
        pointers = tuple(_pointer(obj) for obj in (qt_window, qt_viewer, dims, canvas.native))
        assert qt_window._qt_viewer is qt_viewer and qt_viewer.dims is dims
        assert qt_window.status_thread is status and dims._animation_thread is animation
        metadata = dims.metaObject()
        assert metadata.indexOfSlot("stop()") >= 0
        assert QThread.currentThread() is qapp.thread()

        if not status.isRunning():
            status.start()
        qtbot.waitUntil(status.isRunning, timeout=5000)
        status_close, status_wait = status.close_terminate, status.wait
        dims_stop, animation_stop, animation_wait = dims.stop, animation._stop, animation.wait

        def close_status():
            trace.append("status-close")
            return status_close()

        def wait_status(*args, **kwargs):
            # The running worker may consume its event before wait is entered;
            # the source's cooperative flags are stable on this close path.
            trace.append(("status-wait", status._terminate, status._allow_start))
            return status_wait(*args, **kwargs)

        def stop_dims():
            trace.append("dims-stop")
            return dims_stop()

        def stop_animation():
            trace.append("animation-stop")
            return animation_stop()

        def wait_animation(*args, **kwargs):
            trace.append(("animation-wait", animation._waiter.is_set()))
            return animation_wait(*args, **kwargs)

        manager = SimpleNamespace(
            get_status=lambda: trace.append("task-status") or ["pending task"],
            is_busy=lambda: trace.append("task-busy") or True,
            cancel_all=lambda: trace.append("task-cancel"),
        )
        with monkeypatch.context() as patch:
            patch.setattr(window, "_task_status_manager", manager)
            patch.setattr(qt_window, "_ev", None)
            patch.setattr(qt_window, "_quit_app", False)
            patch.setattr(status, "close_terminate", close_status)
            patch.setattr(status, "wait", wait_status)
            patch.setattr(dims, "stop", stop_dims)
            patch.setattr(animation, "_stop", stop_animation)
            patch.setattr(animation, "wait", wait_animation)
            # Real Qt close() dispatches the real virtual closeEvent. No future
            # native operation is fabricated or invoked directly.
            assert qt_window.close()
            assert trace == [
                "task-status", "task-busy", "task-cancel", "status-close",
                ("status-wait", True, False), "dims-stop", "animation-stop",
                ("animation-wait", True),
            ]
            assert not status.isRunning()
            assert tuple(_pointer(obj) for obj in (qt_window, qt_viewer, dims, canvas.native)) == pointers
        assert viewer.__pydantic_private__ is private
        assert window._qt_viewer is qt_viewer and qt_viewer.canvas is canvas
        viewer.close()
        assert not hasattr(window, "_qt_window")
        assert dims.dims is None and dims.slider_widgets == [] and dims._displayed_sliders == []
        assert all(widget.dims is None and widget.qt_dims is None for widget in sliders)
    finally:
        viewer._layer_slicer.shutdown()
        if hasattr(window, "_qt_window"):
            viewer.close()
        # Existing bounded deferred-delete fixture pattern; not full teardown.
        for _ in range(2):
            QCoreApplication.sendPostedEvents(None, QEvent.Type.DeferredDelete)
            qapp.processEvents()
