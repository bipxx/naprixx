"""Exercise the actual ShapeList/Mesh owner through a Qt/Vispy Viewer."""

import os

import numpy as np
import pytest

from naparixx import _native


@pytest.mark.gui
def test_shape_mesh_cache_and_canvas_survive_edits_and_display_change(
    make_napari_viewer, qtbot
):
    viewer = make_napari_viewer(show=True)
    qtbot.wait_exposed(viewer.window._qt_window)
    first = np.array([[0, 0, 0], [0, 0, 8], [0, 8, 8], [0, 8, 0]], dtype=float)
    first.flags.writeable = False
    second = first + np.array([0, 12, 12])
    layer = viewer.add_shapes([first], shape_type="polygon", face_color="red")
    owner = layer._data_view
    mesh = owner._mesh
    if os.environ.get("NAPARIXX_SHAPE_STATE_BASELINE") != "1":
        assert isinstance(owner, _native.ShapeListNamespaceBase)
        assert isinstance(mesh, _native.ShapeMeshNamespaceBase)
    storage = vars(owner)
    assert storage is owner.__dict__
    np.testing.assert_array_equal(owner._visible_shapes_indices, [0])
    warm = owner._visible_shapes
    assert len(warm) == 1

    layer.add([second], shape_type="polygon", face_color="blue")
    assert layer._data_view is owner and owner._mesh is mesh
    assert vars(owner) is storage
    assert len(owner.shapes) == 2
    np.testing.assert_array_equal(owner._visible_shapes_indices, [0, 1])
    assert owner._visible_shapes is not warm
    assert len(mesh.displayed_triangles) > 0

    viewer.dims.ndisplay = 3
    assert owner.ndisplay == mesh.ndisplay == 3
    assert owner._mesh is mesh
    assert mesh.vertices.shape[1] == 3
    screenshot = viewer.screenshot(canvas_only=True, flash=False)
    assert screenshot.ndim == 3 and screenshot.shape[2] == 4
    assert screenshot.shape[0] > 0 and screenshot.shape[1] > 0

    viewer.dims.ndisplay = 2
    assert owner.ndisplay == mesh.ndisplay == 2
    assert owner._mesh is mesh
    assert mesh.vertices.shape[1] == 2
    layer.selected_data = {1}
    layer.remove_selected()
    assert layer._data_view is owner and owner._mesh is mesh
    assert vars(owner) is storage
    assert len(owner.shapes) == 1
    np.testing.assert_array_equal(owner._visible_shapes_indices, [0])
    np.testing.assert_array_equal(first, [[0, 0, 0], [0, 0, 8], [0, 8, 8], [0, 8, 0]])
    assert not first.flags.writeable
    screenshot = viewer.screenshot(canvas_only=True, flash=False)
    assert screenshot.ndim == 3 and screenshot.shape[2] == 4
    viewer.layers.remove(layer)
    assert len(viewer.layers) == 0
