"""Check a built installation, including optional real Qt/OpenGL viewers."""

import argparse
import gc
import json
import sysconfig
import tempfile
import weakref
from importlib.metadata import distribution
from importlib.resources import files
from pathlib import Path

import numpy as np
from npe2 import DynamicPlugin, PluginManager

import napari
import naparixx
from napari.components import Camera, Dims, ViewerModel
from napari.components.grid import GridCanvas
from napari.layers import Image, Points, Surface, Vectors
from napari.plugins import _initialize_plugins
from napari.plugins.io import read_data_with_plugins
from naparixx import native


def check_gui():
    from qtpy.QtCore import QCoreApplication, QEvent
    from qtpy.QtGui import QOffscreenSurface, QOpenGLContext

    from napari._qt.qt_event_loop import get_qapp

    app = get_qapp()
    surface = QOffscreenSurface()
    surface.create()
    context = QOpenGLContext()
    try:
        if not context.create() or not context.makeCurrent(surface):
            raise RuntimeError("Qt could not create an actual OpenGL context")
        context.doneCurrent()
    finally:
        surface.destroy()

    def exercise(mode):
        viewer = napari.Viewer(show=False, ndisplay=mode)
        models = (viewer.dims, viewer.scene.camera, viewer.canvas.grid)
        model_refs = [weakref.ref(model) for model in models]
        try:
            viewer.add_image(np.zeros((4, 8, 8)))
            viewer.add_labels(np.zeros((4, 8, 8), dtype=np.int32))
            viewer.add_points(np.array([[1.0, 2.0, 3.0]]))
            viewer.dims.ndisplay = 3 if mode == 2 else 2
            viewer.dims.ndisplay = mode
            viewer.canvas.grid.enabled = True
            viewer.fit_to_view()
        finally:
            layer_refs = [weakref.ref(layer) for layer in viewer.layers]
            load_refs = [
                weakref.ref(layer._slicing_state._load_state) for layer in viewer.layers
            ]
            viewer.close()
            state_refs = [weakref.ref(model._native_state) for model in models]
            QCoreApplication.sendPostedEvents(None, QEvent.Type.DeferredDelete)
            app.processEvents()
        return model_refs + state_refs + layer_refs + load_refs

    for mode in (2, 3):
        refs = exercise(mode)
        gc.collect()
        if any(ref() is not None for ref in refs):
            raise RuntimeError(
                "A closed viewer retained a model, layer or native state"
            )


def check_layer_slices():
    """Exercise the bound request implementations in an installed wheel."""
    dims = Dims(ndim=3, point=(1, 0, 0))
    values = np.arange(60).reshape(3, 4, 5)
    image = Image(values, units=("um",) * 3)
    request = image._slicing_state._make_slice_request(dims)
    assert isinstance(request.slice_input._native_plan, native.SlicePlan)
    assert isinstance(image._slicing_state._load_state, native.LayerLoadState)
    np.testing.assert_array_equal(request().image.raw, values[1])

    points = Points(np.array([[1, 2, 3], [0, 2, 3]], dtype=float))
    result = points._slicing_state._make_slice_request(dims)()
    np.testing.assert_array_equal(result.indices, [0])

    vectors = Vectors(np.array([[[1, 0, 0], [0, 1, 1]], [[0, 0, 0], [0, 1, 1]]]))
    result = vectors._slicing_state._make_slice_request(dims)()
    np.testing.assert_array_equal(result.indices, [0])

    vertices = np.array([[1, 0, 0], [1, 2, 0], [1, 0, 2], [0, 0, 0]], dtype=float)
    surface = Surface((vertices, np.array([[0, 1, 2], [0, 1, 3]]), np.arange(4)))
    result = surface._slicing_state._make_slice_request(dims)()
    np.testing.assert_array_equal(result.faces, [[0, 1, 2]])
    np.testing.assert_array_equal(result.vertices, vertices[:3, 1:])

    image._slicing_state._set_unloaded_slice_id(100)
    image._slicing_state._update_loaded_slice_id(99)
    assert not image.loaded
    image._slicing_state._update_loaded_slice_id(100)
    assert image.loaded


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--require-wheel", action="store_true")
    parser.add_argument("--gui", action="store_true")
    args = parser.parse_args()
    package = distribution("napari")
    assert package.version == napari.__version__ == "0.9.2+naparixx.2"
    assert native.__version__ == "0.3.0"
    paths = {
        "napari": str(Path(napari.__file__).resolve()),
        "naparixx": str(Path(naparixx.__file__).resolve()),
        "extension": str(Path(native.__file__).resolve()),
    }
    if args.require_wheel:
        roots = [
            Path(sysconfig.get_path(name)).resolve() for name in ("purelib", "platlib")
        ]
        for path in paths.values():
            assert any(Path(path).is_relative_to(root) for root in roots), path
        direct_url = package.read_text("direct_url.json")
        assert not direct_url or not json.loads(direct_url).get("dir_info", {}).get(
            "editable", False
        )

    for cls, state in (
        (Dims, native.DimsState),
        (Camera, native.CameraState),
        (GridCanvas, native.GridState),
    ):
        assert isinstance(cls()._native_state, state)
    viewer = ViewerModel()
    image = viewer.add_image(np.arange(256.0).reshape(4, 8, 8))
    viewer.dims.set_current_step(0, 2)
    assert viewer.dims.current_step[0] == 2
    assert isinstance(image, Image) and image.contrast_limits == [0, 255]
    check_layer_slices()

    _initialize_plugins()
    assert PluginManager.instance().get_manifest("napari").name == "napari"
    assert files("napari_builtins").joinpath("builtins.yaml").is_file()
    assert files("napari").joinpath("resources/multichannel_cells.png").is_file()
    assert files("napari").joinpath("_qt/qt_resources/styles/00_base.qss").is_file()
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "example.nativetest"
        path.write_text("test reader", encoding="utf-8")
        with DynamicPlugin("nativeportinstallfixture") as plugin:

            @plugin.contribute.reader(filename_patterns=["*.nativetest"])
            def get_reader(path):
                return lambda path: [(np.arange(20.0).reshape(4, 5), {}, "image")]

            data, reader = read_data_with_plugins([str(path)], plugin=plugin.name)
            assert reader and type(Image(data[0][0], **data[0][1])) is Image

    if args.gui:
        check_gui()
    print(
        json.dumps(
            {
                "napari": package.version,
                "native": native.__version__,
                "paths": paths,
                "builtin_manifest": "passed",
                "npe2_reader": "passed",
                "native_layer_requests_and_loading": "passed",
                "gui_2d_3d_and_release": "passed" if args.gui else "not run",
                "backend": naparixx.backend_status(),
            },
            indent=2,
        )
    )


if __name__ == "__main__":
    main()
