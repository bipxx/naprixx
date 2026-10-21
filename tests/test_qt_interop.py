"""Exercise foreign Qt wrappers around real C++-owned widgets."""

import gc
import weakref
from concurrent.futures import ThreadPoolExecutor

import numpy as np
import pytest

qtpy = pytest.importorskip("qtpy")
# Load the chosen Qt runtime before the native extension.
widgets = pytest.importorskip("qtpy.QtWidgets")
native = pytest.importorskip(
    "naparixx._qt5_native" if qtpy.API_NAME == "PyQt5" else "naparixx._qt_native",
    reason="opt-in native Qt bridge for the active binding is not built",
)

from qtpy.QtCore import QCoreApplication, QEvent, QObject  # noqa: E402

from naparixx.qt_host import NativeWidgetHost, _valid, _wrapper_api  # noqa: E402

pytestmark = [pytest.mark.gui, pytest.mark.no_qt_log]


@pytest.fixture(scope="module")
def application():
    app = widgets.QApplication.instance() or widgets.QApplication([])
    yield app
    QCoreApplication.sendPostedEvents(None, QEvent.Type.DeferredDelete)
    app.processEvents()


def test_plugin_widget_identity_callbacks_and_native_close(application):
    class PluginButton(widgets.QPushButton):
        def __init__(self):
            super().__init__("Plugin")
            self.clicks = 0
            self.clicked.connect(self.record_click)

        def record_click(self):
            self.clicks += 1

    for _ in range(10):
        host = NativeWidgetHost()
        button = PluginButton()
        token, dock = host.add_plugin_widget(button, "Plugin Ω", "left")
        assert dock.widget() is button
        assert button.parentWidget() is dock
        assert dock.parentWidget() is host.window
        assert dock.windowTitle() == "Plugin Ω"
        assert host.dock_count == 1
        button.click()
        assert button.clicks == 1
        window = host.window
        references = [weakref.ref(obj) for obj in (host, host._host, button, dock)]
        assert host.close()
        assert host.closed
        assert not _valid(window)
        assert not _valid(button)
        assert not _valid(dock)
        assert host.close()
        del host, button, dock, window, token
        gc.collect()
        assert all(reference() is None for reference in references)


def test_detached_plugin_widget_survives_native_window(application):
    button = widgets.QPushButton("Detached plugin")
    with NativeWidgetHost() as host:
        token, dock = host.add_plugin_widget(button)
        assert host.remove_plugin_widget(token) is button
        assert host.dock_count == 0
        assert button.parentWidget() is None
        assert not _valid(dock)
    assert _valid(button)
    if qtpy.API_NAME in {"PyQt5", "PyQt6"}:
        assert _wrapper_api().ispyowned(button)
    else:
        assert _wrapper_api().ownedByPython(button)
    button.click()
    reference = weakref.ref(button)
    del button
    gc.collect()
    assert reference() is None


def test_native_canvas_transfer_and_invalid_inputs(application):
    canvas = widgets.QLabel("canvas")
    with NativeWidgetHost() as host:
        host.attach_canvas(canvas)
        assert host.window.centralWidget() is canvas
        assert host.take_canvas() is canvas
        assert canvas.parentWidget() is None
        host.attach_canvas(canvas)
        with pytest.raises(ValueError, match="already the canvas"):
            host.add_plugin_widget(canvas)
        with pytest.raises(TypeError, match="QWidget"):
            host.add_plugin_widget(object())
        invalid_area = widgets.QLabel("invalid area")
        with pytest.raises(ValueError, match="dock area"):
            host.add_plugin_widget(invalid_area, area="center")
        assert invalid_area.parentWidget() is None
        with pytest.raises(ValueError, match="positive"):
            host.resize(0, 40)
    assert not _valid(canvas)


def test_worker_thread_cannot_mutate_gui(application):
    with NativeWidgetHost() as host, ThreadPoolExecutor(max_workers=1) as worker:
        task = worker.submit(host.resize, 128, 128)
        with pytest.raises(RuntimeError, match="GUI thread"):
            task.result(timeout=5)


def test_destroyed_callback_can_reenter_close(application):
    host = NativeWidgetHost()
    window = host.window
    results = []
    window.destroyed.connect(lambda: results.append(host.close()))
    assert host.close()
    assert results == [True]
    assert host.closed


def test_close_event_can_reenter_without_releasing_widget_leases(application):
    host = NativeWidgetHost()
    button = widgets.QPushButton("plugin")
    host.add_plugin_widget(button)
    results = []

    class CloseFilter(QObject):
        def eventFilter(self, _watched, event):
            if event.type() == QEvent.Type.Close:
                results.append(host.close())
                assert host.dock_count == 1
                assert _valid(button)
            return False

    event_filter = CloseFilter(host.window)
    host.window.installEventFilter(event_filter)
    assert host.close()
    assert results == [False]
    assert not _valid(button)
    assert host.closed


def test_ignored_close_keeps_native_window_and_plugin_widgets(application):
    host = NativeWidgetHost()
    button = widgets.QPushButton("plugin")
    host.add_plugin_widget(button)

    class IgnoreClose(QObject):
        def eventFilter(self, _watched, event):
            if event.type() == QEvent.Type.Close:
                event.ignore()
                return True
            return False

    event_filter = IgnoreClose(host.window)
    host.window.installEventFilter(event_filter)
    assert not host.close()
    assert not host.closed
    assert host.dock_count == 1
    assert _valid(button)
    host.window.removeEventFilter(event_filter)
    assert host.close()
    assert not _valid(button)


def test_vispy_canvas_rendering_in_native_window(application):
    from vispy import app, scene

    app.use_app(qtpy.API_NAME.lower())
    canvas = scene.SceneCanvas(size=(96, 96), show=False)
    with NativeWidgetHost() as host:
        host.attach_canvas(canvas.native)
        view = canvas.central_widget.add_view()
        image = scene.visuals.Image(
            np.arange(256, dtype=np.float32).reshape(16, 16),
            cmap="grays",
            parent=view.scene,
        )
        view.camera = "panzoom"
        view.camera.set_range()
        frame = canvas.render(size=(96, 96))
        assert frame.shape == (96, 96, 4)
        assert frame[..., :3].max() > frame[..., :3].min()
        assert image.parent is view.scene
        assert host.take_canvas() is canvas.native
        canvas.close()
    del image, view, canvas
    gc.collect()
