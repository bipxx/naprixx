"""Finite whole-worker source/native contracts; root executes all Qt/runtime checks."""
# Each family drains/releases its callbacks before the next loop iteration;
# intentional live closure cells and full forensic trace names stay explicit.
# ruff: noqa: B023, E501
from __future__ import annotations

import ast
import builtins
import gc
import hashlib
import importlib
import inspect
import json
import threading
import weakref
from pathlib import Path
from types import SimpleNamespace

import pytest

ROOT = Path(__file__).resolve().parents[1]
BEFORE = ROOT / ".artifacts/viewer-workers-before/index.json"
BEFORE_SHA = "9c63ead37cd426a5cd9498f3cf88ca16c36aa8c6f420076845a7ae030bf7c25e"
SCOPE = ROOT / ".artifacts/viewer-workers-scope-next.json"
SCOPE_SHA = "34393347f7ce5c4c257bba1c7cd0f66c5642c1af1a080fc9d8ba68fafeaca3fd"
MODULES = {
    "status": ("napari._qt.threads.status_checker", "_qt/threads/status_checker.py", ("StatusChecker",)),
    "sliders": ("napari._qt.widgets.qt_dims_slider", "_qt/widgets/qt_dims_slider.py", ("QtDimSliderWidget", "QtPlayButton", "AnimationThread")),
    "dims": ("napari._qt.widgets.qt_dims", "_qt/widgets/qt_dims.py", ("QtDims",)),
    "main": ("napari._qt.qt_main_window", "_qt/qt_main_window.py", ("_QtMainWindow",)),
    "canvas": ("napari._vispy.canvas", "_vispy/canvas.py", ("VispyCanvas",)),
    "utils": ("napari._qt.utils", "_qt/utils.py", ()),
}


def _sha(raw):
    return hashlib.sha256(raw).hexdigest()


def _source(mode, relative):
    index = json.loads(BEFORE.read_bytes())
    assert _sha(BEFORE.read_bytes()) == BEFORE_SHA
    if mode == "actual_source":
        path = ROOT / "src/napari" / relative
    else:
        directory = "current" if mode == "current_fork" else mode
        name = ".artifacts/viewer-workers-before/" + directory + "/" + relative
        row = next(r for r in index["files"] if r["before"] == name)
        path = ROOT / name
        assert _sha(path.read_bytes()) == row["before_sha256"]
    return path, ast.parse(path.read_bytes())


def _compile_nodes(nodes, namespace, path):
    future_imports = [
        node for node in ast.parse(path.read_bytes()).body
        if isinstance(node, ast.ImportFrom) and node.module == "__future__"
    ]
    tree = ast.fix_missing_locations(ast.Module(body=[
        *future_imports,
        *nodes,
    ], type_ignores=[]))
    # Complete original bodies, including property/slot declarations, are the
    # test-only oracle. Production never receives any source-body fallback.
    assert all(ast.dump(a) == ast.dump(b) for a, b in zip(nodes, tree.body[len(future_imports):], strict=True))
    exec(compile(tree, str(path), "exec", dont_inherit=True), namespace)


# Reuse only the unchanged accepted GUI settings fixture, not its ten workflows.
_gui_helper = ROOT / "tests/test_viewer_slice_gui.py"
assert _sha(_gui_helper.read_bytes()) == "5c7d5277c22232a3a0be0e5b721e330110448bc7973e030b814c88360b8d688b"
_fixture_node = next(n for n in ast.parse(_gui_helper.read_bytes()).body if isinstance(n, ast.FunctionDef) and n.name == "_fresh_gui_settings")
_fixture_namespace = {"__name__": __name__, "pytest": pytest}
_compile_nodes([_fixture_node], _fixture_namespace, _gui_helper)
_fresh_gui_settings = _fixture_namespace["_fresh_gui_settings"]


@pytest.fixture(scope="module")
def families(qapp):
    assert _sha(SCOPE.read_bytes()) == SCOPE_SHA
    modules = {key: importlib.import_module(spec[0]) for key, spec in MODULES.items()}
    result = []
    for mode in ("pinned", "current_fork", "actual"):
        namespaces, classes = {}, {}
        for key, (_module_name, relative, names) in MODULES.items():
            module = modules[key]
            assert Path(module.__file__).resolve() == (ROOT / "src/napari" / relative).resolve()
            if mode == "actual":
                namespace = vars(module)
            else:
                path, tree = _source(mode, relative)
                namespace = dict(vars(module))
                namespace["__builtins__"] = dict(vars(builtins))
                if key == "dims":
                    namespace.update({name: classes[name] for name in MODULES["sliders"][2]})
                nodes = [n for n in tree.body if isinstance(n, ast.ClassDef) and n.name in names]
                if key == "utils":
                    nodes = [n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == "qt_signals_blocked"]
                assert len(nodes) == (len(names) or 1)
                _compile_nodes(nodes, namespace, path)
                # The established weak-slot bridge owns original implementations
                # on their genuine class; it adds no controller/instance root.
                retain = namespace.get("retain_slot_implementations")
                if retain is not None:
                    for name in names:
                        if "_qt_slot_implementations" not in vars(namespace[name]):
                            retain(namespace[name])
            namespaces[key] = namespace
            classes.update({name: namespace[name] for name in names})
        if mode != "actual":
            namespaces["sliders"]["qt_signals_blocked"] = namespaces["utils"]["qt_signals_blocked"]
        result.append(SimpleNamespace(mode=mode, classes=classes, namespaces=namespaces))
    return result


def _member(family, qualified):
    parts = qualified.split(".")
    key = next(key for key, spec in MODULES.items() if parts[0] in spec[2]) if len(parts) > 1 else "utils"
    if len(parts) == 1:
        return family.namespaces[key][parts[0]]
    value = getattr(family.classes[parts[0]], parts[1])
    if len(parts) == 3:
        value = getattr(value, parts[2])
    return value


class Box:
    pass


class Hook:
    """A source callback primitive, logging labels without retaining receivers."""
    def __init__(self, tag, trace):
        self.tag, self.trace = tag, trace

    def connect(self, callback):
        self.trace.append((self.tag, "connect", callback.__name__))

    def disconnect(self, callback):
        self.trace.append((self.tag, "disconnect", callback.__name__))

    def emit(self, *values):
        self.trace.append((self.tag, "emit", *values))

    def blocker(self, callback):
        from contextlib import contextmanager

        @contextmanager
        def blocked():
            self.trace.append((self.tag, "enter", callback.__name__))
            try:
                yield
            finally:
                self.trace.append((self.tag, "exit", callback.__name__))
        return blocked()


def _slider(tag, trace):
    slider = Box()
    slider.fps, slider.frame_range = 20, (0, 0)
    slider.axis, slider.loop_mode = 0, "loop"
    slider.dims = SimpleNamespace(nsteps=(8,), current_step=(2,), events=SimpleNamespace(current_step=Hook(tag + ".step", trace)))
    slider.fps_changed, slider.range_changed = Hook(tag + ".fps", trace), Hook(tag + ".range", trace)

    class Button:
        def _handle_start(self):
            trace.append((tag, "start"))

        def _handle_stop(self):
            trace.append((tag, "stop"))

    slider.play_button = Button()
    return slider


def test_all35_source_metadata_actual_qthread_and_live_weak_state(families, qapp):
    from qtpy.QtCore import QObject, QThread

    from napari.components import ViewerModel

    rows = json.loads(SCOPE.read_bytes())["owned_definitions"]
    assert len(rows) == 35
    for family in families:
        for row in rows:
            reference = _member(families[1], row["qualified_name"])
            actual = _member(family, row["qualified_name"])
            assert str(inspect.signature(actual)) == str(inspect.signature(reference)), row["qualified_name"]
            assert actual.__name__ == reference.__name__
            assert actual.__qualname__ == reference.__qualname__
            assert actual.__doc__ == reference.__doc__
            assert actual.__annotations__ == reference.__annotations__
        model, parent = ViewerModel(), QObject()
        status = family.classes["StatusChecker"](model, parent)
        animation = family.classes["AnimationThread"](parent)
        assert isinstance(status, QThread) and isinstance(animation, QThread)
        assert type(type(status)) is type(QThread)
        assert status.parent() is parent and animation.parent() is parent
        assert status.viewer_ref() is model and animation.slider is None
        assert animation.axis is None and animation.dims is None and animation.loop_mode is None
        state = vars(animation)
        state["_interval"] = 17
        assert animation.interval == 17
        animation.interval = 19
        assert state["_interval"] == 19 and vars(animation) is state
        reference = weakref.ref(model)
        del model
        gc.collect()
        assert reference() is None and status.viewer_ref() is None
        assert status.run() is None and not status._terminate
        status.deleteLater()
        animation.deleteLater()
        parent.deleteLater()
    qapp.processEvents()


@pytest.mark.parametrize("kind", ["status", "animation"])
def test_constructor_failure_keeps_exact_source_partial_publication(families, monkeypatch, qapp, kind):
    from napari.components import ViewerModel

    for family in families:
        cls = family.classes["StatusChecker" if kind == "status" else "AnimationThread"]
        owners = []

        class Broken(cls):
            def __init__(self, *args):
                owners.append(self)
                super().__init__(*args)

        def event_failure():
            raise RuntimeError("event construction failed")

        model = ViewerModel()
        namespace = family.namespaces["status" if kind == "status" else "sliders"]
        with monkeypatch.context() as patch:
            patch.setitem(namespace, "Event" if kind == "status" else "threading", event_failure if kind == "status" else SimpleNamespace(Event=event_failure))
            with pytest.raises(RuntimeError, match="event construction failed"):
                Broken(model) if kind == "status" else Broken()
        owner = owners.pop()
        if kind == "status":
            assert owner.viewer_ref() is model
            assert "_need_status_update" not in vars(owner) and "_terminate" not in vars(owner)
        else:
            assert owner.interval == 1 and owner.slider is None
            assert "_waiter" not in vars(owner) and "frame_range" not in vars(owner)
        owner.deleteLater()
    qapp.processEvents()


@pytest.mark.parametrize("failure", [None, "fps", "range"], ids=["success", "fps_error", "range_error"])
def test_slider_transfer_order_new_weak_selection_and_saved_old_lease(families, failure):
    for family in families:
        trace = []
        previous, new = _slider("old", trace), _slider("new", trace)
        old_reference = weakref.ref(previous)
        cls = family.classes["AnimationThread"]

        class State:
            slider = cls.slider

            def set_fps(self, value):
                trace.append(("fps", value, self.slider is new))
                if failure == "fps":
                    raise RuntimeError("fps failed")

            def set_frame_range(self, value):
                trace.append(("range", value, self.slider is new))
                if failure == "range":
                    raise RuntimeError("range failed")

            def _on_axis_changed(self):
                pass

        owner = State()
        owner._slider = weakref.ref(previous)
        owner.min_point, owner.max_point = 1, 5
        owner.finished, owner.started = Hook("finished", trace), Hook("started", trace)
        saved_error = None
        try:
            cls.set_slider(owner, new)
        except RuntimeError as error:
            saved_error = error
        assert owner.slider is new
        prefix = [("fps", 20, True)]
        if failure != "fps":
            prefix.append(("range", (0, 0), True))
        if failure:
            assert trace == prefix
            assert str(saved_error) == failure + " failed"
            del previous
            gc.collect()
            assert old_reference() is not None
            saved_error = None
            gc.collect()
            assert old_reference() is None
        else:
            assert saved_error is None and owner.current == 2
            assert trace == prefix + [
                ("old.fps", "disconnect", "set_fps"),
                ("old.range", "disconnect", "set_frame_range"),
                ("old.step", "disconnect", "_on_axis_changed"),
                ("finished", "disconnect", "_handle_stop"),
                ("started", "disconnect", "_handle_start"),
                ("new.fps", "connect", "set_fps"),
                ("new.range", "connect", "set_frame_range"),
                ("new.step", "connect", "_on_axis_changed"),
                ("finished", "connect", "_handle_stop"),
                ("started", "connect", "_handle_start"),
            ]
            del previous
            gc.collect()
            assert old_reference() is None
        new = None
        gc.collect()
        assert owner.slider is None


@pytest.mark.parametrize("operation", ["fps", "loop_mode", "frame_range"])
def test_slider_properties_publish_before_source_callbacks_and_restore_blocking(families, operation):
    for family in families:
        trace = []
        cls = family.classes["QtDimSliderWidget"]
        mode = family.namespaces["sliders"]["LoopMode"]
        owner = Box()
        owner._fps, owner._loop_mode = 10, mode.LOOP
        owner._minframe, owner._maxframe = 0, 0

        class Combo:
            blocked = False

            def blockSignals(self, value):
                trace.append(("block", value))
                previous, self.blocked = self.blocked, value
                return previous

            def setCurrentText(self, value):
                trace.append(("text", value, self.blocked, str(owner._loop_mode)))

        owner.play_button = SimpleNamespace(
            fpsspin=SimpleNamespace(setValue=lambda value: trace.append(("spin", value, owner._fps))),
            reverse_check=SimpleNamespace(setChecked=lambda value: trace.append(("reverse", value, owner._fps))),
            mode_combo=Combo(),
        )
        owner.fps_changed, owner.mode_changed, owner.range_changed = Hook("fps", trace), Hook("mode", trace), Hook("range", trace)
        if operation == "fps":
            cls.fps.fset(owner, -20)
            assert cls.fps.fget(owner) == -20
            cls.fps.fset(owner, -20)
            assert trace == [("spin", 20, -20), ("reverse", True, -20), ("fps", "emit", -20)]
        elif operation == "loop_mode":
            cls.loop_mode.fset(owner, "back_and_forth")
            assert cls.loop_mode.fget(owner) is mode.BACK_AND_FORTH
            cls.loop_mode.fset(owner, mode.BACK_AND_FORTH)
            assert trace == [("block", True), ("text", "back and forth", True, "back_and_forth"), ("block", False), ("mode", "emit", "back_and_forth")]
        else:
            value = [1, 4]
            cls.frame_range.fset(owner, value)
            assert cls.frame_range.fget(owner) == (1, 4)
            cls.frame_range.fset(owner, None)
            assert cls.frame_range.fget(owner) == (0, 0)
            assert trace == [("range", "emit", (1, 4)), ("range", "emit", (0, 0))]
            with pytest.raises(ValueError, match="length of 2"):
                cls.frame_range.fset(owner, (1,))
            assert cls.frame_range.fget(owner) == (0, 0)


def test_frame_debounce_and_canvas_ack_use_live_model_fields(families):
    for family in families:
        trace = []
        dims = SimpleNamespace(_play_ready=True, point=(0,))

        def move(axis, value):
            trace.append((axis, value, dims._play_ready))
            dims.point = (value,)

        dims.set_current_step = move
        owner = SimpleNamespace(dims=dims)
        family.classes["QtDims"]._set_frame(owner, 0, 1)
        assert trace == [(0, 1, False)] and not dims._play_ready
        family.classes["QtDims"]._set_frame(owner, 0, 2)
        assert len(trace) == 1
        canvas = SimpleNamespace(viewer=SimpleNamespace(dims=dims))
        assert family.classes["VispyCanvas"].enable_dims_play(canvas) is None
        family.classes["QtDims"]._set_frame(owner, 0, 3)
        assert trace == [(0, 1, False), (0, 3, False)]


def test_status_receiver_and_toggle_preserve_dynamic_override_order(families):
    for family in families:
        trace = []
        owner = Box()
        owner.status_thread = SimpleNamespace(start=lambda: trace.append("start"), terminate=lambda: trace.append("terminate"))
        cls = family.classes["_QtMainWindow"]
        assert cls.set_status_and_tooltip(owner, None) is None
        active = SimpleNamespace(help="active help")
        viewer = SimpleNamespace(tooltip=SimpleNamespace(text=""), layers=SimpleNamespace(selection=SimpleNamespace(active=active)))
        owner._qt_viewer = SimpleNamespace(viewer=viewer)
        cls.set_status_and_tooltip(owner, ("ready", "tip"))
        assert (viewer.status, viewer.tooltip.text, viewer.help) == ("ready", "tip", "active help")
        cls._toggle_status_thread(owner, SimpleNamespace(value=True))
        cls._toggle_status_thread(owner, SimpleNamespace(value=False))
        assert trace == ["start", "terminate"]


def test_play_button_style_uses_actual_receiver_at_each_source_callback(families):
    for family in families:
        trace = []
        owner = Box()
        owner.setProperty = lambda key, value: trace.append((key, value))

        def style():
            trace.append("style")
            return SimpleNamespace(unpolish=lambda value: trace.append(("unpolish", value is owner)), polish=lambda value: trace.append(("polish", value is owner)))

        owner.style = style
        cls = family.classes["QtPlayButton"]
        cls._handle_start(owner)
        cls._handle_stop(owner)
        assert trace == [("playing", "True"), "style", ("unpolish", True), "style", ("polish", True), ("playing", "False"), "style", ("unpolish", True), "style", ("polish", True)]


@pytest.mark.parametrize("exit_kind", ["normal", "body_error", "generator_close"])
def test_signal_block_context_restores_exact_previous_value(families, exit_kind):
    for family in families:
        trace = []
        previous = object()

        class State:
            def blockSignals(self, value):
                trace.append(value)
                return previous

        manager = family.namespaces["utils"]["qt_signals_blocked"](State())
        if exit_kind == "generator_close":
            assert manager.__enter__() is None
            manager.gen.close()
        elif exit_kind == "body_error":
            with pytest.raises(RuntimeError, match="body failed"):
                with manager:
                    raise RuntimeError("body failed")
        else:
            with manager:
                assert trace == [True]
        assert len(trace) == 2 and trace[0] is True and trace[1] is previous


@pytest.mark.parametrize("enabled", [False, True], ids=["normal", "asv"])
def test_asv_configuration_publishes_only_source_start_override(families, enabled):
    for family in families:
        mode = "actual_source" if family.mode == "actual" else family.mode
        path, tree = _source(mode, MODULES["status"][1])
        namespace = dict(family.namespaces["status"])
        namespace["__builtins__"] = dict(vars(builtins))
        namespace["os"] = SimpleNamespace(environ={"ASV": "true"} if enabled else {})
        cls = next(n for n in tree.body if isinstance(n, ast.ClassDef) and n.name == "StatusChecker")
        _compile_nodes([cls], namespace, path)
        original = namespace["StatusChecker"].start
        policies = [n for n in tree.body if isinstance(n, (ast.If, ast.Expr)) and ("ASV" in ast.unparse(n) or "viewer_status_asv" in ast.unparse(n))]
        assert len(policies) == 1
        _compile_nodes(policies, namespace, path)
        selected = namespace["StatusChecker"].start
        if enabled:
            assert selected is not original
            assert str(inspect.signature(selected)) == "(self, priority=0)"
            assert selected(object()) is None
        else:
            assert selected is original


def test_actual_notification_record_holds_and_releases_source_worker_model_leases(families, monkeypatch, qapp):
    from napari.components import ViewerModel
    from napari.utils.notifications import ErrorNotification, NotificationManager

    for family in families:
        class Model(ViewerModel):
            def _calc_status_from_cursor(self):
                raise RuntimeError("controlled worker calculation")

        model = Model()
        worker = family.classes["StatusChecker"](model)
        model_ref, worker_ref = weakref.ref(model), weakref.ref(worker)
        manager = NotificationManager()
        with monkeypatch.context() as patch:
            patch.setitem(family.namespaces["status"], "notification_manager", manager)
            assert worker.calculate_status() is None
        assert len(manager.records) == 1 and type(manager.records[0]) is ErrorNotification
        assert str(manager.records[0].exception) == "controlled worker calculation"
        del worker, model
        gc.collect()
        assert worker_ref() is not None and model_ref() is not None
        # Removing only OUR record leaves an explicit external alias until the
        # next deletion; no production notification history/global clearing.
        record = manager.records.pop()
        gc.collect()
        assert worker_ref() is not None and model_ref() is not None
        del record
        gc.collect()
        assert worker_ref() is None and model_ref() is None
    qapp.processEvents()


@pytest.mark.gui
def test_real_virtual_workers_release_gil_deliver_on_main_thread_and_close_inflight(
    families, qapp, qtbot, monkeypatch
):
    import numpy as np
    from qtpy.QtCore import QCoreApplication, QEvent, QObject, QThread, Slot

    import napari

    def scenario(family):
        viewer = napari.Viewer(show=False)
        viewer.add_image(np.zeros((4, 2, 2), dtype=np.uint8))
        window, qt_viewer = viewer.window, viewer.window._qt_viewer
        main, dims = window._qt_window, qt_viewer.dims
        # Stop the pre-existing real children before substituting test-only
        # source-oracle QThreads in the same actual receiver/parent graph.
        old_status = main.status_thread
        old_status.close_terminate()
        assert old_status.wait(5000)
        viewer.cursor.events.position.disconnect(old_status.trigger_status_update)
        old_animation = dims._animation_thread
        dims.stop()
        status = family.classes["StatusChecker"](viewer, main)
        animation = family.classes["AnimationThread"](dims)
        main.status_thread, dims._animation_thread = status, animation
        viewer.cursor.events.position.connect(status.trigger_status_update)
        status.status_and_tooltip_changed.connect(main.set_status_and_tooltip)
        trace = []
        entered, calculation_gate, release_gate = threading.Event(), threading.Event(), threading.Event()
        model_handler_completed = threading.Event()
        initial_wait_after_clear = threading.Event()

        class CloseHeldAnimationEvent(threading.Event):
            def wait(self, timeout=None):
                # The real source work() has already emitted its first frame
                # and called clear(). Hold only this test primitive's wait
                # until the ordinary original _stop() calls inherited set().
                trace.append(("animation-wait-after-clear", self.is_set(), QThread.currentThread() is qapp.thread()))
                initial_wait_after_clear.set()
                return super().wait()

        held_animation_waiter = CloseHeldAnimationEvent()
        animation._waiter = held_animation_waiter
        original_set_frame = type(dims)._set_frame

        def acknowledged_set_frame(self, axis, frame):
            # Call the actual original handler completely before acknowledgement;
            # an observer's earlier signal callback does not establish this.
            result = original_set_frame(self, axis, frame)
            trace.append(("frame-applied", axis, frame, QThread.currentThread() is qapp.thread()))
            model_handler_completed.set()
            return result

        class Receiver(QObject):
            @Slot(object)
            def status_result(self, result):
                trace.append(("status", result, QThread.currentThread() is qapp.thread()))

            @Slot(int, int)
            def frame_result(self, axis, frame):
                trace.append(("frame", axis, frame, QThread.currentThread() is qapp.thread()))

        receiver = Receiver()
        canvas, native_canvas = qt_viewer.canvas, qt_viewer.canvas.native
        destroyed = []
        watched = [
            ("main", main), ("qt_viewer", qt_viewer), ("dims", dims),
            ("status", status), ("animation", animation),
            ("old_status", old_status), ("old_animation", old_animation),
            ("native_canvas", native_canvas), ("receiver", receiver),
        ]
        references = {name: weakref.ref(value) for name, value in watched}
        references.update(viewer=weakref.ref(viewer), window=weakref.ref(window), canvas=weakref.ref(canvas))
        for name, value in watched:
            # The observer owns only a label and the label list; neither its
            # default nor its closure retains any observed QObject/model.
            def observed_destroyed(*_args, _name=name):
                destroyed.append(_name)

            value.destroyed.connect(observed_destroyed)
        del watched, value, observed_destroyed
        status.status_and_tooltip_changed.connect(receiver.status_result)
        animation.frame_requested.connect(receiver.frame_result)

        def calculate(self):
            trace.append(("calculate", QThread.currentThread() is qapp.thread()))
            entered.set()
            if not calculation_gate.wait(5):
                raise RuntimeError("fixture calculation gate timed out")
            return "worker ready", "worker tip"

        original_close = status.close_terminate

        def close_status():
            original_close()
            trace.append(("close-flags", status._terminate, status._allow_start))
            release_gate.set()

        def release_calculation():
            if release_gate.wait(5):
                calculation_gate.set()

        release_thread = threading.Thread(target=release_calculation)
        try:
            with monkeypatch.context() as patch:
                patch.setattr(type(viewer), "_calc_status_from_cursor", calculate)
                patch.setattr(status, "close_terminate", close_status)
                patch.setattr(type(dims), "_set_frame", acknowledged_set_frame)
                status.start()
                status.trigger_status_update()
                qtbot.waitUntil(entered.is_set, timeout=5000)
                # A second real QThread and the GUI run while Event.wait in the
                # status calculation is blocked: a concrete GIL-release exit.
                qt_viewer.canvas.enable_dims_play()
                dims.play(axis=0, fps=20, loop_mode="loop", frame_range=(0, 2))
                qtbot.waitUntil(lambda: any(row[0] == "frame" for row in trace), timeout=5000)
                qtbot.waitUntil(lambda: model_handler_completed.is_set() and initial_wait_after_clear.is_set(), timeout=5000)
                assert [row for row in trace if row[0] == "animation-wait-after-clear"] == [("animation-wait-after-clear", False, False)]
                assert all(row[-1] is True for row in trace if row[0] == "frame-applied")
                assert status.isRunning() and animation.isRunning()
                release_thread.start()
                viewer.close()
                release_thread.join(5)
                assert not release_thread.is_alive()
                assert not status.isRunning() and not animation.isRunning()
                assert not hasattr(window, "_qt_window") and dims.dims is None
                assert ("calculate", False) in trace and ("close-flags", True, False) in trace
                # Source has no post-calculate termination guard. This already
                # computed result remains a real queued receiver/payload lease.
                qtbot.waitUntil(lambda: any(row[0] == "status" for row in trace), timeout=5000)
                assert [row for row in trace if row[0] == "status"] == [("status", ("worker ready", "worker tip"), True)]
                assert all(row[-1] is True for row in trace if row[0] == "frame")
        finally:
            calculation_gate.set()
            release_gate.set()
            if release_thread.ident is not None:
                release_thread.join(5)
            # Only ordinary source stop/join and the accepted bounded fixture
            # DeferredDelete pattern. No new production teardown workaround.
            for worker, stop in ((status, "close_terminate"), (animation, "_stop")):
                try:
                    getattr(worker, stop)()
                    worker.wait(5000)
                except RuntimeError:
                    pass  # The actual QObject may already have been deleted.
            viewer._layer_slicer.shutdown()
            if hasattr(window, "_qt_window"):
                viewer.close()
            receiver.deleteLater()
            for _ in range(2):
                QCoreApplication.sendPostedEvents(None, QEvent.Type.DeferredDelete)
                qapp.processEvents()
        # Source schedules these exact real children/receivers for destruction;
        # this verifies delivered destruction, rather than deleteLater alone.
        assert {"main", "qt_viewer", "dims", "status", "animation", "old_status", "old_animation", "native_canvas", "receiver"} <= set(destroyed)
        assert references["status"]() is status and references["animation"]() is animation
        # Release every named subject and bound/callback alias after worker join
        # and ordinary deferred destruction. Keep only root-free observers,
        # immutable trace values and weakrefs for the source outcome comparison.
        del close_status, calculate, release_calculation, acknowledged_set_frame
        del held_animation_waiter, CloseHeldAnimationEvent
        original_set_frame = None
        model_handler_completed = None
        initial_wait_after_clear = None
        original_close = None
        del viewer, window, main, qt_viewer, dims, animation
        status = None
        del old_status, old_animation, canvas, native_canvas, receiver, worker, patch
        del release_thread
        gc.collect()
        return {
            "destroyed": tuple(sorted(set(destroyed))),
            "weak_alive_after_alias_release": {name: reference() is not None for name, reference in references.items()},
        }

    outcomes = {family.mode: scenario(family) for family in families}
    # Preserve the actual frozen-fork lifetime result rather than guessing all
    # models/wrappers must die. Pinned results are observed independently; its
    # existing widget-cleanup differences are not silently erased.
    assert outcomes["actual"] == outcomes["current_fork"]
