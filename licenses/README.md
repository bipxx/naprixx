The bundled application sources and native implementations derive from napari 0.9.2,
commit `595639295a153c84bcb127a9e07d801355440a35`.
The original BSD 3-Clause notice is retained in `napari-BSD-3-Clause.txt`.

The experimental handwritten grid policies in
`experiments/labels_frontend/grid_provider.inc` derive from NumPy 2.5.3's
`numpy/lib/_index_tricks_impl.py` and `numpy/_core/numeric.py`.
The NumPy BSD 3-Clause notice is retained in `NumPy-BSD-3-Clause.txt`.

The event implementation in `src/napari/utils/events/event.py` derives from
Vispy's event system. Its BSD 3-Clause notice is retained in that Python source
and `VisPy-BSD-3-Clause.txt`, including for the handwritten native event
adaptations in `cpp/event_objects.cpp`, `cpp/event_subscriptions.cpp`,
`cpp/event_delivery.cpp`, `cpp/event_groups.cpp` and their headers. The tracing
adaptation in `cpp/event_tracing.cpp` derives from napari's BSD-licensed
`src/napari/utils/events/debugging.py`.

Upstream files: `src/napari/utils/transforms/transforms.py`,
`src/napari/layers/points/_slice.py`,
`src/napari/layers/points/_points_utils.py`,
`src/napari/layers/labels/_labels_utils.py`,
`src/napari/layers/utils/layer_utils.py`, and
`src/napari/utils/geometry.py`.

The application model port additionally derives behavior from
`src/napari/components/dims.py`, `camera.py`, `grid.py`, `canvas.py`,
`viewer_model.py`, and `src/napari/utils/camera_orientations.py`.
The full source baseline remains under `src/napari` and `src/napari_builtins`
with its bundled vendor notices intact. This is an independent source fork;
the local distribution version identifies the native implementation.
