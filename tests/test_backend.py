import gc
import weakref
from concurrent.futures import ThreadPoolExecutor

import numpy as np
import pytest
from npe2 import DynamicPlugin

import naparixx
from napari.components import ViewerModel
from napari.layers import Image, Labels, Points
from napari.layers.labels import labels as labels_module
from napari.layers.points import _points_utils
from napari.utils import geometry
from napari.utils.transforms import Affine
from naparixx import native


def test_dispatch_switch_preserves_existing_napari_aliases():
    original_projection = geometry.project_points_onto_plane
    original_interpolate = labels_module.interpolate_coordinates
    original_affine = Affine.__call__
    assert _points_utils.project_points_onto_plane is original_projection
    assert not naparixx.backend_status()["enabled"]
    first = naparixx.enable()
    assert first["enabled"] and len(first["operations"]) == 9
    assert first["application_core"] == "C++"
    assert first["integration"] == "source fork; explicit calls"
    assert geometry.project_points_onto_plane is original_projection
    assert _points_utils.project_points_onto_plane is geometry.project_points_onto_plane
    assert labels_module.interpolate_coordinates is original_interpolate
    assert Affine.__call__ is original_affine
    assert naparixx.enable() == first
    naparixx.disable()
    assert geometry.project_points_onto_plane is original_projection
    assert _points_utils.project_points_onto_plane is original_projection
    assert labels_module.interpolate_coordinates is original_interpolate
    assert Affine.__call__ is original_affine


def test_unsupported_napari_version_fails_before_activation(monkeypatch):
    import napari

    monkeypatch.setattr(napari, "__version__", "0.0.0")
    with pytest.raises(RuntimeError, match="requires napari==0.9.2"):
        naparixx.enable()
    assert not naparixx.backend_status()["enabled"]


def test_facade_preserves_class_identity():
    import napari

    assert naparixx.layers is napari.layers
    assert naparixx.layers.Image is Image
    assert naparixx.layers.Points is Points
    assert naparixx.backend_status()["enabled"]
    with pytest.raises(AttributeError):
        _ = naparixx.this_name_does_not_exist


def test_layer_operations_actually_call_native_kernels(monkeypatch):
    calls = {
        name: 0
        for name in (
            "affine",
            "calc_data_range",
            "slice_points",
            "interpolate_coordinates",
        )
    }
    for name in calls:
        original = getattr(native, name)

        def record(*args, _name=name, _original=original, **kwargs):
            calls[_name] += 1
            return _original(*args, **kwargs)

        monkeypatch.setattr(native, name, record)
    naparixx.enable()
    image = Image(np.arange(80.0).reshape(8, 10), scale=(2.0, 3.0))
    assert image.contrast_limits == [0.0, 79.0]
    np.testing.assert_allclose(image.world_to_data((4.0, 6.0)), [2.0, 2.0])
    points = Points(np.array([[0.0, 1.0, 2.0], [1.0, 3.0, 4.0]]))
    assert points.data.shape == (2, 3)
    labels = Labels(np.zeros((16, 16), dtype=np.int32))
    labels.brush_size = 3
    labels.mode = "paint"
    labels._draw(7, (4.0, 4.0), (4.0, 10.0))
    assert np.all(labels.data[4, 4:11] == 7)
    assert all(count > 0 for count in calls.values()), calls


def test_viewer_model_retains_layers_and_events():
    naparixx.enable()
    viewer = ViewerModel()
    events = []
    viewer.layers.events.inserted.connect(lambda event: events.append(event.value))
    image = viewer.add_image(np.zeros((4, 8, 8)), name="volume", scale=(2, 3, 4))
    labels = viewer.add_labels(np.zeros((4, 8, 8), dtype=np.int32))
    points = viewer.add_points(np.array([[0.0, 1.0, 2.0]]))
    viewer.add_shapes(
        [np.array([[1.0, 1.0], [2.0, 1.0], [2.0, 2.0]])], shape_type="polygon"
    )
    viewer.add_vectors(np.array([[[0.0, 0.0], [1.0, 2.0]]]))
    viewer.add_surface(
        (
            np.array([[0.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]]),
            np.array([[0, 1, 2]]),
        )
    )
    viewer.add_tracks(np.array([[0, 0, 1, 1], [0, 1, 2, 2]], dtype=float))
    assert isinstance(image, Image) and isinstance(labels, Labels)
    assert isinstance(points, Points)
    assert len(viewer.layers) == len(events) == 7
    np.testing.assert_allclose(image.data_to_world((1, 2, 3)), [2, 6, 12])


def test_npe2_plugin_reader_still_creates_original_layer_class(tmp_path):
    from napari.plugins.io import read_data_with_plugins

    naparixx.enable()
    path = tmp_path / "example.nativetest"
    path.write_text("test reader", encoding="utf-8")
    expected = np.arange(20.0).reshape(4, 5)
    with DynamicPlugin("nativeportreaderfixture") as plugin:

        @plugin.contribute.reader(filename_patterns=["*.nativetest"])
        def get_reader(path):
            def read(path):
                return [(expected, {"name": "plugin image"}, "image")]

            return read

        data, reader = read_data_with_plugins([str(path)], plugin=plugin.name)
        assert data and reader
        layer = Image(data[0][0], **data[0][1])
        assert type(layer) is Image
        np.testing.assert_array_equal(layer.data, expected)
        assert layer.contrast_limits == [0.0, 19.0]


def test_native_calls_from_multiple_workers():
    coords = np.random.default_rng(0).normal(size=(7000, 3))
    coords.flags.writeable = False
    with ThreadPoolExecutor(max_workers=4) as pool:
        results = list(
            pool.map(lambda _: native.affine(coords, np.eye(3), np.zeros(3)), range(8))
        )
    for result in results:
        np.testing.assert_allclose(result, coords)


@pytest.mark.gui
@pytest.mark.no_qt_log
@pytest.mark.parametrize("ndisplay", [2, 3])
def test_real_viewer_can_add_layers_and_close(ndisplay):
    pytest.importorskip("qtpy.QtWidgets")
    from qtpy.QtCore import QCoreApplication, QEvent
    from qtpy.QtGui import QOffscreenSurface, QOpenGLContext

    from napari._qt.qt_event_loop import get_qapp
    from napari._qt.widgets.qt_viewer_buttons import QtViewerPushButton
    from napari._qt.widgets.qt_viewer_dock_widget import QtCustomTitleBar

    app = get_qapp()
    assert app is not None
    surface = QOffscreenSurface()
    surface.create()
    context = QOpenGLContext()
    try:
        if not context.create() or not context.makeCurrent(surface):
            pytest.skip("This Qt platform cannot create an OpenGL context")
        context.doneCurrent()
    finally:
        surface.destroy()

    def exercise_viewer():
        viewer = naparixx.Viewer(show=False, ndisplay=ndisplay)
        model_refs = [
            weakref.ref(model)
            for model in (viewer.dims, viewer.scene.camera, viewer.canvas.grid)
        ]
        model_refs.append(weakref.ref(viewer.window))
        try:
            assert isinstance(viewer.add_image(np.zeros((4, 8, 8))), Image)
            assert isinstance(
                viewer.add_labels(np.zeros((4, 8, 8), dtype=np.int32)), Labels
            )
            assert isinstance(viewer.add_points(np.array([[1.0, 2.0, 3.0]])), Points)
            viewer.canvas.grid.enabled = True
            layer_refs = [weakref.ref(layer) for layer in viewer.layers]
            layer_state_refs = [
                weakref.ref(layer._slicing_state._load_state) for layer in viewer.layers
            ]
            widget_refs = [
                weakref.ref(widget)
                for widget in (
                    *viewer.window._qt_viewer.dims.slider_widgets,
                    *viewer.window._qt_window.findChildren(QtCustomTitleBar),
                    *viewer.window._qt_window.findChildren(QtViewerPushButton),
                    viewer.window.main_menu,
                    viewer.window.file_menu,
                    viewer.window.view_menu,
                    viewer.window.layers_menu,
                    viewer.window.plugins_menu,
                    viewer.window.window_menu,
                    viewer.window.help_menu,
                )
            ]
        finally:
            viewer.close()
            state_refs = [
                weakref.ref(model._native_state)
                for model in (viewer.dims, viewer.scene.camera, viewer.canvas.grid)
            ]
            # Hidden viewers have no running Qt event loop to process deferred
            # widget destruction. Flush it before checking native lifetimes.
            # Deleting widgets can queue destruction of signal proxies as well.
            for _ in range(3):
                QCoreApplication.sendPostedEvents(None, QEvent.Type.DeferredDelete)
                app.processEvents()
        return model_refs, state_refs, layer_refs, layer_state_refs, widget_refs

    # Complete the frame first: pytest's rewritten assertions hold temporary
    # bound methods even after an assertion passes.
    model_refs, state_refs, layer_refs, layer_state_refs, widget_refs = (
        exercise_viewer()
    )
    gc.collect()
    assert all(ref() is None for ref in model_refs)
    assert all(ref() is None for ref in state_refs)
    assert all(ref() is None for ref in layer_refs)
    assert all(ref() is None for ref in layer_state_refs)
    assert all(ref() is None for ref in widget_refs)
