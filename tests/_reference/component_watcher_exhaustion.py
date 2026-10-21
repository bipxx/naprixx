"""Reserve dict-watcher IDs before constructing native component models."""

import ctypes
import gc


def main():
    api = ctypes.pythonapi
    ids, callbacks = [], []

    @ctypes.PYFUNCTYPE(
        ctypes.c_int, ctypes.c_int, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p
    )
    def unused(*args):
        callbacks.append(args)
        return 0

    if hasattr(api, "PyDict_AddWatcher"):
        api.PyDict_AddWatcher.argtypes = [ctypes.c_void_p]
        api.PyDict_AddWatcher.restype = ctypes.c_int
        api.PyDict_ClearWatcher.argtypes = [ctypes.c_int]
        api.PyDict_ClearWatcher.restype = ctypes.c_int
        while True:
            try:
                ids.append(api.PyDict_AddWatcher(ctypes.cast(unused, ctypes.c_void_p)))
            except RuntimeError:
                break
    try:
        from napari.components import Camera
        from napari.components.grid import GridCanvas

        camera, grid = Camera(), GridCanvas()
        assert not camera._native_state._model_dictionary_record.watched
        assert not grid._native_state._model_dictionary_record.watched
        vars(camera)["angles"] = (0.0, 90.0, 0.0)
        vars(grid)["enabled"] = True
        assert camera._native_state.angles == (0.0, 90.0, 0.0)
        assert grid._native_state.layout((True, True, True)).shape == (2, 2)
        camera._native_state.update(
            (4.0, 5.0, 6.0),
            3.0,
            (0.0, 90.0, 0.0),
            40.0,
            False,
            False,
            ("away", "up", "left"),
            False,
        )
        assert camera.center == (4.0, 5.0, 6.0)
        del camera, grid
        gc.collect()
        assert not callbacks
        print("Native component boundaries remain consistent without dict watchers.")
    finally:
        for watcher in ids:
            assert api.PyDict_ClearWatcher(watcher) == 0


if __name__ == "__main__":
    main()
