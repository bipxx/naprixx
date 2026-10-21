"""Focus notifications compared with the unchanged pinned Python function."""

import ast
import gc
import os
import subprocess
import sys
import textwrap
import weakref
from pathlib import Path
from types import ModuleType

import pytest

from naparixx import _native


@pytest.fixture(scope="module")
def reference_focus():
    path = (
        Path(__file__).resolve().parents[1]
        / ".upstream/napari/src/napari/_qt/qt_event_loop.py"
    )
    source = path.read_text(encoding="utf-8")
    function = next(
        node
        for node in ast.parse(source).body
        if isinstance(node, ast.FunctionDef) and node.name == "_focus_changed"
    )
    code = compile(
        "from __future__ import annotations\n"
        + ast.get_source_segment(source, function),
        str(path),
        "exec",
    )

    def make(namespace):
        exec(code, namespace)
        return namespace["_focus_changed"]

    return make


class _Timer:
    def __init__(self, name, trace, action=None):
        self.name = name
        self.trace = trace
        self.action = action

    def timer_start(self):
        self.trace.append(("start", self.name))

    def timer_stop(self):
        self.trace.append(("stop", self.name))
        if self.action is not None:
            self.action()


def _services(patch, *, count=1, accept_none=False):
    trace = []

    class MainWindow:
        pass

    class Widget:
        def __init__(self, window=None):
            self.owner = MainWindow() if window is None else window

        def window(self):
            trace.append("window")
            return self.owner

    class Notifications(list):
        def __bool__(self):
            trace.append("notifications truth")
            return len(self) != 0

        def __iter__(self):
            trace.append("notifications iter")
            return super().__iter__()

    notifications = Notifications(_Timer(str(index), trace) for index in range(count))

    class NotificationService:
        @property
        def _instances(self):
            trace.append("instances")
            return notifications

    def instancecheck(value, kind):
        trace.append(("instancecheck", "widget" if kind is Widget else "window"))
        return (accept_none and value is None and kind is Widget) or isinstance(
            value, kind
        )

    module = ModuleType("napari._qt.qt_main_window")
    module._QtMainWindow = MainWindow
    patch.setitem(sys.modules, module.__name__, module)
    namespace = {
        "QWidget": Widget,
        "NapariQtNotification": NotificationService(),
        "isinstance": instancecheck,
    }
    return namespace, Widget, notifications, trace


def _compare(reference_focus, monkeypatch, scenario):
    def original(old, new, namespace):
        return reference_focus(namespace)(old, new)

    observations = []
    for invoke in (original, _native.qt_focus_changed):
        with monkeypatch.context() as patch:
            observations.append(scenario(invoke, patch))
    assert observations[0] == observations[1]
    return observations[1]


@pytest.mark.parametrize(
    "transition", ["both_none", "both_widgets", "gained", "invalid_old", "foreign"]
)
def test_focus_guards_preserve_original_lookup_order(
    reference_focus, monkeypatch, transition
):
    def scenario(invoke, patch):
        namespace, widget, notifications, trace = _services(patch)
        transitions = {
            "both_none": (None, None),
            "both_widgets": (widget(), widget()),
            "gained": (None, widget()),
            "invalid_old": (object(), None),
            "foreign": (widget(object()), None),
        }
        if transition != "foreign":
            # These guards must return without importing the window module.
            patch.setitem(sys.modules, "napari._qt.qt_main_window", None)
        assert invoke(*transitions[transition], namespace) is None
        assert not any(
            isinstance(event, tuple) and event[0] == "stop" for event in trace
        )
        assert "instances" not in trace
        assert len(notifications) == 1
        return trace

    observed = _compare(reference_focus, monkeypatch, scenario)
    if transition in ("both_none", "both_widgets"):
        assert observed == []
    elif transition in ("gained", "invalid_old"):
        assert observed == [("instancecheck", "widget")]


@pytest.mark.parametrize("count", [0, 1, 3])
def test_focus_loss_stops_each_live_notification(reference_focus, monkeypatch, count):
    def scenario(invoke, patch):
        namespace, widget, notifications, trace = _services(patch, count=count)
        assert invoke(widget(), None, namespace) is None
        assert [
            event for event in trace if isinstance(event, tuple) and event[0] == "stop"
        ] == [("stop", str(index)) for index in range(count)]
        assert trace.count("notifications truth") == 1
        assert trace.count("notifications iter") == bool(count)
        return trace, len(notifications)

    _compare(reference_focus, monkeypatch, scenario)


def test_existing_start_branch_uses_last_notification(reference_focus, monkeypatch):
    def scenario(invoke, patch):
        namespace, widget, _, trace = _services(patch, count=3, accept_none=True)
        assert invoke(None, widget(), namespace) is None
        assert ("start", "2") in trace
        assert not any(
            isinstance(event, tuple) and event[0] == "stop" for event in trace
        )
        assert "notifications iter" not in trace
        return trace

    _compare(reference_focus, monkeypatch, scenario)


@pytest.mark.parametrize(
    "stage",
    ["widget_check", "window", "window_check", "instances", "truth", "iter", "timer"],
)
def test_provider_error_identity_and_short_circuit(reference_focus, monkeypatch, stage):
    def scenario(invoke, patch):
        namespace, widget, notifications, trace = _services(patch, count=2)
        error = LookupError(stage)

        def fail(*args):
            trace.append(("fail", stage))
            raise error

        if stage == "widget_check":
            namespace["isinstance"] = fail
        elif stage == "window":
            patch.setattr(widget, "window", fail)
            patch.setitem(sys.modules, "napari._qt.qt_main_window", None)
        elif stage == "window_check":
            check = namespace["isinstance"]

            def instancecheck(value, kind):
                return check(value, kind) if kind is widget else fail()

            namespace["isinstance"] = instancecheck
        elif stage == "instances":

            class Service:
                _instances = property(fail)

            namespace["NapariQtNotification"] = Service()
        elif stage == "truth":
            patch.setattr(type(notifications), "__bool__", fail)
        elif stage == "iter":
            patch.setattr(type(notifications), "__iter__", fail)
        else:
            notifications[0].action = fail
        with pytest.raises(LookupError) as caught:
            invoke(widget(), None, namespace)
        assert caught.value is error
        assert ("stop", "1") not in trace
        return trace

    _compare(reference_focus, monkeypatch, scenario)


@pytest.mark.parametrize("mutation", ["clear", "append", "remove_next", "replace"])
def test_notification_callbacks_mutate_the_live_collection(
    reference_focus, monkeypatch, mutation
):
    def scenario(invoke, patch):
        namespace, widget, notifications, trace = _services(patch, count=2)

        def mutate():
            if mutation == "clear":
                notifications.clear()
            elif mutation == "append":
                notifications.append(_Timer("appended", trace))
            elif mutation == "remove_next":
                notifications.pop(1)
            else:

                class Replacement:
                    _instances = [_Timer("replacement", trace)]

                namespace["NapariQtNotification"] = Replacement

        notifications[0].action = mutate
        assert invoke(widget(), None, namespace) is None
        stopped = [
            event[1]
            for event in trace
            if isinstance(event, tuple) and event[0] == "stop"
        ]
        expected = {
            "clear": ["0"],
            "append": ["0", "1", "appended"],
            "remove_next": ["0"],
            "replace": ["0", "1"],
        }
        assert stopped == expected[mutation]
        return trace

    _compare(reference_focus, monkeypatch, scenario)


def test_nested_focus_callback_has_independent_iterator(reference_focus, monkeypatch):
    def scenario(invoke, patch):
        namespace, widget, notifications, trace = _services(patch, count=2)

        def reenter():
            notifications[0].action = None
            assert invoke(widget(), None, namespace) is None

        notifications[0].action = reenter
        assert invoke(widget(), None, namespace) is None
        stopped = [
            event[1]
            for event in trace
            if isinstance(event, tuple) and event[0] == "stop"
        ]
        assert stopped == ["0", "0", "1", "1"]
        return trace

    _compare(reference_focus, monkeypatch, scenario)


def test_native_focus_does_not_retain_namespace_owners(monkeypatch):
    def scenario():
        namespace, widget, _, _ = _services(monkeypatch)
        payload = ModuleType("_focus_namespace_owner")
        payload.cycle = payload
        namespace["adapter_owner"] = payload
        reference = weakref.ref(payload)
        assert _native.qt_focus_changed(widget(), None, namespace) is None
        return reference

    reference = scenario()
    gc.collect()
    assert reference() is None


@pytest.mark.gui
def test_saved_public_callback_signature_and_qt_signal():
    pytest.importorskip("qtpy.QtWidgets")
    path = (
        Path(__file__).resolve().parents[1]
        / ".upstream/napari/src/napari/_qt/qt_event_loop.py"
    )
    environment = os.environ.copy()
    if os.name != "nt":
        environment.setdefault("QT_QPA_PLATFORM", "offscreen")
    code = """
        import ast
        import gc
        import inspect
        import sys
        import weakref
        from pathlib import Path
        from types import ModuleType
        from qtpy.QtCore import QCoreApplication, QEvent, QEventLoop, QObject, Signal
        from qtpy.QtWidgets import QWidget
        from napari._qt import qt_event_loop

        app = qt_event_loop.get_qapp()
        saved = qt_event_loop._focus_changed
        source = Path(sys.argv[1]).read_text(encoding='utf-8')
        function = next(
            node for node in ast.parse(source).body
            if isinstance(node, ast.FunctionDef) and node.name == '_focus_changed'
        )
        reference_namespace = {'__name__': qt_event_loop.__name__}
        exec(compile(
            'from __future__ import annotations\\n'
            + ast.get_source_segment(source, function), sys.argv[1], 'exec'
        ), reference_namespace)
        original = reference_namespace['_focus_changed']
        assert inspect.signature(saved) == inspect.signature(original)
        assert saved.__name__ == original.__name__
        assert saved.__module__ == original.__module__
        assert saved.__doc__ == original.__doc__
        del original, reference_namespace

        trace = []
        class Timer:
            def __init__(self, name):
                self.name = name
            def timer_start(self):
                trace.append(('start', self.name))
            def timer_stop(self):
                trace.append(('stop', self.name))
        class Service:
            _instances = [Timer('0'), Timer('1')]
        class FocusSignal(QObject):
            changed = Signal(object, object)

        module_name = 'napari._qt.qt_main_window'
        absent = object()
        previous_module = sys.modules.get(module_name, absent)
        previous_service = qt_event_loop.NapariQtNotification
        module = ModuleType(module_name)
        module._QtMainWindow = QWidget
        signal = FocusSignal(app)
        widget = QWidget()
        references = [weakref.ref(signal), weakref.ref(widget)]
        sys.modules[module_name] = module
        qt_event_loop.NapariQtNotification = Service
        signal.changed.connect(saved)
        try:
            signal.changed.emit(widget, None)
            signal.changed.emit(None, widget)
            assert trace == [('stop', '0'), ('stop', '1')]
        finally:
            signal.changed.disconnect(saved)
            signal.deleteLater()
            widget.deleteLater()
            qt_event_loop.NapariQtNotification = previous_service
            if previous_module is absent:
                sys.modules.pop(module_name)
            else:
                sys.modules[module_name] = previous_module
        del signal, widget
        for _ in range(2):
            QCoreApplication.sendPostedEvents(None, QEvent.Type.DeferredDelete)
            app.processEvents(QEventLoop.ProcessEventsFlag.ExcludeUserInputEvents)
        gc.collect()
        assert all(reference() is None for reference in references)
        print('FOCUS_CALLBACK_RELEASED')
    """
    result = subprocess.run(
        [sys.executable, "-c", textwrap.dedent(code), str(path)],
        capture_output=True,
        text=True,
        env=environment,
        timeout=90,
    )
    output = result.stdout + result.stderr
    assert result.returncode == 0, output
    assert "FOCUS_CALLBACK_RELEASED" in output, output
    assert "nanobind: leaked" not in output, output
    assert "Exception ignored" not in output, output
    assert "Traceback" not in output, output
