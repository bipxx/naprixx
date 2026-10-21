"""Native worker/menu shutdown contracts and real interpreter-exit regressions."""

import os
import subprocess
import sys
import textwrap
from types import SimpleNamespace

import pytest

from naparixx import _native


def test_worker_shutdown_snapshots_windows_before_reentry():
    calls = []
    windows = []

    class Window:
        def __init__(self, name):
            self.name = name

        def _stop_worker_threads(self):
            calls.append(self.name)
            windows.clear()

    windows.extend((Window("first"), Window("second")))
    assert _native.qt_stop_window_workers(windows) is None
    assert calls == ["first", "second"]


def test_worker_shutdown_continues_after_deleted_qt_wrapper():
    calls = []

    def deleted():
        raise RuntimeError("wrapped C/C++ object has been deleted")

    windows = [
        SimpleNamespace(_stop_worker_threads=deleted),
        SimpleNamespace(_stop_worker_threads=lambda: calls.append("alive")),
    ]
    _native.qt_stop_window_workers(windows)
    assert calls == ["alive"]


@pytest.mark.parametrize("failure", [ValueError, TypeError, AttributeError])
def test_worker_shutdown_preserves_other_failures(failure):
    error = failure("worker failure")
    calls = []

    def stop():
        raise error

    with pytest.raises(failure) as caught:
        _native.qt_stop_window_workers(
            [
                SimpleNamespace(_stop_worker_threads=stop),
                SimpleNamespace(_stop_worker_threads=lambda: calls.append("later")),
            ]
        )
    assert caught.value is error
    assert not calls


def test_disposal_queues_owned_actions_before_processing_callbacks():
    events = []
    action_type, deferred, flags = object(), object(), object()

    class Action:
        def __init__(self, name, owner):
            self.name = name
            self._app = SimpleNamespace(name=owner)

        def deleteLater(self):
            events.append(("delete", self.name))

    actions = [
        Action("first", "napari"),
        Action("foreign", "another_application"),
        Action("second", "napari"),
    ]

    def find_children(kind):
        assert kind is action_type
        return list(actions)

    def post(receiver, kind):
        assert receiver is None and kind is deferred
        events.append(("post",))

    def process(kind):
        assert kind is flags
        events.append(("process",))

    app = SimpleNamespace(findChildren=find_children, processEvents=process)
    core = SimpleNamespace(sendPostedEvents=post)
    assert _native.qt_dispose_menu_actions(app, action_type, core, deferred, flags) == 2
    assert events[:2] == [
        ("delete", "first"),
        ("delete", "second"),
    ]
    assert events[2:] == [("post",), ("process",), ("post",), ("process",)]


@pytest.mark.parametrize("absent", [True, False])
def test_disposal_does_not_pump_without_owned_actions(absent):
    def unexpected(*args):
        raise AssertionError("No Qt event processing required")

    app = (
        None
        if absent
        else SimpleNamespace(findChildren=lambda kind: [], processEvents=unexpected)
    )
    core = SimpleNamespace(sendPostedEvents=unexpected)
    assert _native.qt_dispose_menu_actions(app, None, core, None, None) == 0


def test_disposal_preserves_deletion_error_without_processing_events():
    error = ValueError("delete failed")
    processed = []

    def delete():
        raise error

    action = SimpleNamespace(
        _app=SimpleNamespace(name="napari"),
        deleteLater=delete,
    )
    app = SimpleNamespace(
        findChildren=lambda kind: [action],
        processEvents=lambda *args: processed.append(True),
    )
    with pytest.raises(ValueError) as caught:
        _native.qt_dispose_menu_actions(
            app, None, SimpleNamespace(sendPostedEvents=lambda *args: None), None, None
        )
    assert caught.value is error
    assert not processed


def test_focus_teardown_disconnects_only_the_supplied_callback():
    own, foreign = object(), object()
    callbacks = [own, foreign]
    signal = SimpleNamespace(disconnect=callbacks.remove)
    assert _native.qt_disconnect_focus_callback(
        SimpleNamespace(focusChanged=signal), own
    )
    assert callbacks == [foreign]


def test_focus_teardown_honors_false_result_from_qt_binding():
    calls = []
    callback = object()

    def disconnected(value):
        calls.append(value)
        return False

    application = SimpleNamespace(focusChanged=SimpleNamespace(disconnect=disconnected))
    assert not _native.qt_disconnect_focus_callback(application, callback)
    assert calls == [callback]


@pytest.mark.parametrize("missing", ["application", "callback"])
def test_focus_teardown_ignores_absent_application_or_callback(missing):
    def unexpected(*args):
        raise AssertionError("No connection to remove")

    application = SimpleNamespace(focusChanged=SimpleNamespace(disconnect=unexpected))
    callback = object()
    assert not _native.qt_disconnect_focus_callback(
        None if missing == "application" else application,
        None if missing == "callback" else callback,
    )


@pytest.mark.parametrize(
    "failure", [TypeError, RuntimeError, ValueError, AttributeError]
)
def test_focus_teardown_preserves_unexpected_provider_failures(failure):
    error = failure("connection failure")

    def disconnect(callback):
        raise error

    application = SimpleNamespace(focusChanged=SimpleNamespace(disconnect=disconnect))
    if failure in (TypeError, RuntimeError):
        assert not _native.qt_disconnect_focus_callback(application, object())
    else:
        with pytest.raises(failure) as caught:
            _native.qt_disconnect_focus_callback(application, object())
        assert caught.value is error


def run_process(code):
    environment = os.environ.copy()
    if os.name != "nt":
        environment.setdefault("QT_QPA_PLATFORM", "offscreen")
    result = subprocess.run(
        [sys.executable, "-c", textwrap.dedent(code)],
        capture_output=True,
        text=True,
        env=environment,
        timeout=90,
    )
    output = result.stdout + result.stderr
    assert result.returncode == 0, output
    assert "nanobind: leaked" not in output, output
    assert "Exception ignored" not in output, output
    assert "Traceback" not in output, output
    return output


@pytest.mark.gui
def test_focus_teardown_preserves_foreign_qt_signal_connection():
    pytest.importorskip("qtpy.QtWidgets")
    output = run_process(
        """
        from napari._qt.qt_event_loop import get_qapp, _focus_changed
        from naparixx import _native
        app = get_qapp()
        calls = []
        def foreign(old, new):
            calls.append((old, new))
        app.focusChanged.connect(foreign)
        assert _native.qt_disconnect_focus_callback(app, _focus_changed)
        app.focusChanged.emit(None, None)
        assert calls == [(None, None)]
        assert not _native.qt_disconnect_focus_callback(app, _focus_changed)
        app.focusChanged.disconnect(foreign)
        print('FOREIGN_FOCUS_PRESERVED')
        """
    )
    assert "FOREIGN_FOCUS_PRESERVED" in output


@pytest.mark.gui
def test_shutdown_releases_registered_callback_after_module_replacement():
    pytest.importorskip("qtpy.QtWidgets")
    output = run_process(
        """
        import weakref
        from napari._qt.qt_event_loop import get_qapp
        from naparixx import _native
        app = get_qapp()
        record = app._naparixx_focus_connection
        registered = weakref.ref(record.callback)
        replacements = []
        get_qapp.__globals__['_focus_changed'] = lambda *args: replacements.append(args)
        _native.qt_shutdown_open_windows(())
        assert record.callback is None and record.connection is None
        assert not hasattr(app, '_naparixx_focus_connection')
        assert registered() is None
        app.focusChanged.emit(None, None)
        assert not replacements
        print('REGISTERED_CALLBACK_RELEASED')
        """
    )
    assert "REGISTERED_CALLBACK_RELEASED" in output


@pytest.mark.gui
def test_shared_menu_actions_survive_until_explicit_shutdown():
    pytest.importorskip("qtpy.QtWidgets")
    output = run_process(
        """
        import gc
        import weakref
        from app_model.backends.qt import QModelMenu
        from app_model.backends.qt._qaction import QMenuItemAction
        from app_model import Application
        from app_model.types import CommandRule, MenuItem
        from napari._app_model import get_app_model
        from napari._app_model.constants import MenuId
        from napari._qt._qapp_model.qactions import init_qactions
        from napari._qt.qt_event_loop import get_qapp
        from qtpy.QtCore import QCoreApplication, QEvent, QEventLoop
        from qtpy.QtWidgets import QAction
        from naparixx import _native

        app = get_qapp()
        init_qactions()
        first = QModelMenu(MenuId.MENUBAR_FILE, get_app_model())
        second = QModelMenu(MenuId.MENUBAR_FILE, get_app_model())
        actions = app.findChildren(QMenuItemAction)
        assert len(actions) == 20
        first_ids = {id(action) for action in first.actions()}
        assert first_ids & {id(action) for action in second.actions()}
        references = [weakref.ref(action) for action in actions]
        del actions
        first.deleteLater()
        second.deleteLater()
        del first, second
        QCoreApplication.sendPostedEvents(None, QEvent.Type.DeferredDelete)
        app.processEvents()
        assert len(app.findChildren(QMenuItemAction)) == 20
        foreign = QAction('foreign widget action', app)
        foreign_model = Application('naparixx-shutdown-foreign')
        foreign_item = MenuItem(command=CommandRule(
            id='foreign.action', title='foreign'))
        foreign_owned = QMenuItemAction.create(foreign_item, foreign_model, app)
        queued = _native.qt_dispose_menu_actions(
            app, QMenuItemAction, QCoreApplication, QEvent.Type.DeferredDelete,
            QEventLoop.ProcessEventsFlag.ExcludeUserInputEvents
        )
        assert queued == 20
        assert app.findChildren(QMenuItemAction) == [foreign_owned]
        gc.collect()
        assert all(reference() is None for reference in references)
        assert foreign.text() == 'foreign widget action'
        assert foreign_owned.text() == 'foreign'
        assert _native.qt_dispose_menu_actions(
            app, QMenuItemAction, QCoreApplication, QEvent.Type.DeferredDelete,
            QEventLoop.ProcessEventsFlag.ExcludeUserInputEvents
        ) == 0
        foreign.deleteLater()
        foreign_owned.deleteLater()
        del foreign, foreign_owned
        for _ in range(2):
            QCoreApplication.sendPostedEvents(None, QEvent.Type.DeferredDelete)
            app.processEvents()
        print('SHARED_ACTIONS_RELEASED')
        """
    )
    assert "SHARED_ACTIONS_RELEASED" in output


@pytest.mark.gui
def test_real_menu_interpreter_shutdown_releases_native_owners():
    pytest.importorskip("qtpy.QtWidgets")
    output = run_process(
        """
        from app_model.backends.qt._qaction import QMenuItemAction
        from napari._app_model.constants import MenuId
        from napari._qt._qapp_model import build_qmodel_menu
        from napari._qt._qapp_model.qactions import init_qactions
        from napari._qt.qt_event_loop import get_qapp
        from qtpy.QtCore import QCoreApplication, QEvent

        app = get_qapp()
        init_qactions()
        menu = build_qmodel_menu(MenuId.MENUBAR_FILE, title='shutdown regression')
        menu.close()
        menu.deleteLater()
        del menu
        for _ in range(5):
            QCoreApplication.sendPostedEvents(None, QEvent.Type.DeferredDelete)
            app.processEvents()
        assert len(app.findChildren(QMenuItemAction)) == 20
        print('MENU_EXIT_READY')
        """
    )
    assert "MENU_EXIT_READY" in output


@pytest.mark.gui
def test_repeated_viewer_close_keeps_actions_reusable_and_exit_clean():
    pytest.importorskip("qtpy.QtWidgets")
    output = run_process(
        """
        import gc
        import weakref
        import numpy as np
        import napari
        from app_model.backends.qt._qaction import QMenuItemAction
        from napari._qt.qt_event_loop import get_qapp
        from qtpy.QtCore import QCoreApplication, QEvent

        app = get_qapp()
        prior_ids = None
        references = []
        for _ in range(3):
            viewer = napari.Viewer(show=False)
            viewer.add_image(np.array([[0, 1], [2, 3]]))
            references.append(weakref.ref(viewer))
            action_ids = {id(action) for action in app.findChildren(QMenuItemAction)}
            assert action_ids
            if prior_ids is not None:
                assert action_ids == prior_ids
            prior_ids = action_ids
            viewer.close()
            del viewer
            for _ in range(5):
                QCoreApplication.sendPostedEvents(None, QEvent.Type.DeferredDelete)
                app.processEvents()
            gc.collect()
            assert len(app.findChildren(QMenuItemAction)) == len(action_ids)
        assert all(reference() is None for reference in references)
        print('REPEATED_VIEWER_EXIT_READY')
        """
    )
    assert "REPEATED_VIEWER_EXIT_READY" in output
