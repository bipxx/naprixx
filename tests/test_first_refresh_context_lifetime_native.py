"""One source-grounded exceptional-with lifetime comparison.

The explicit baseline mode reconstructs pinned and frozen-before methods only.
Default installed mode compares the independent pinned method with the actual
current facade and actual native extension. Neither mode mounts application
modules or provides an application/native-missing fallback. Controlled state,
manager and body callbacks isolate the saved-exit lifetime; complete controller
allocation, private frame identity and wider workflow closure remain outside it.
"""

from __future__ import annotations

import ast
import copy
import gc
import hashlib
import importlib
import inspect
import os
import weakref
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RELATIVE = "layers/base/base.py"
SOURCES = {
    "pinned": (
        ROOT / ".upstream/napari/src/napari" / RELATIVE,
        "2c9e08f2cf0508fdf2399abf49eea42ecb545adb81ac4c8422b57103b57eefa4",
    ),
    "before": (
        ROOT / ".artifacts/first-refresh-control-before/files/src/napari" / RELATIVE,
        "a302ad34b0a586d2ce2cdfda21e336d04d13503d8d4c0d67b33fb11c5ffa592a",
    ),
    "current": (
        ROOT / "src/napari" / RELATIVE,
        "324b01830b1607d4fbf25ffe28a3b2483a8c338b6135166f5ee5c87afd1dce89",
    ),
}


def _definition(key):
    path, expected = SOURCES[key]
    raw = path.read_bytes()
    assert hashlib.sha256(raw).hexdigest() == expected
    module = ast.parse(raw.decode("utf-8"), filename=str(path))
    owner = next(
        node
        for node in module.body
        if isinstance(node, ast.ClassDef) and node.name == "_LayerSlicingState"
    )
    method = next(
        node
        for node in owner.body
        if isinstance(node, ast.FunctionDef) and node.name == "set_view_slice"
    )
    futures = [
        copy.deepcopy(node)
        for node in module.body
        if isinstance(node, ast.ImportFrom) and node.module == "__future__"
    ]
    return path, method, futures


def _controller(definition, native=None):
    path, method, futures = definition
    owner = ast.ClassDef(
        name="_LayerSlicingState",
        bases=[],
        keywords=[],
        body=[copy.deepcopy(method)],
        decorator_list=[],
    )
    module = ast.fix_missing_locations(
        ast.Module(body=futures + [owner], type_ignores=[])
    )
    namespace = {"__name__": "napari.layers.base.base"}
    if native is not None:
        namespace["_native"] = native
    exec(compile(module, str(path), "exec", dont_inherit=True), namespace)
    return namespace["_LayerSlicingState"]


def _run(controller):
    trace, manager_refs = [], []

    class ExitTruth:
        def __init__(self, manager_ref):
            self.manager_ref = manager_ref

        def __bool__(self):
            alive = self.manager_ref() is not None
            trace.append(("truth-manager-alive", alive))
            return alive

        def __del__(self):
            trace.append("result-released")

    class Manager:
        def __enter__(self):
            trace.append("enter")
            return None

        def __exit__(self, kind, value, traceback):
            trace.append(("exit", kind.__name__, str(value), traceback is not None))
            return ExitTruth(weakref.ref(self))

        def __del__(self):
            trace.append("manager-released")

    def manager_factory():
        manager = Manager()
        manager_refs.append(weakref.ref(manager))
        return manager

    def body():
        trace.append("body")
        raise RuntimeError("slice body")

    state = controller()
    state.dask_optimized_slicing = manager_factory
    state._set_view_slice = body
    state.set_view_slice()
    trace.append("suppressed")
    gc.collect()
    return tuple(trace), len(manager_refs), manager_refs[0]() is None


def test_saved_exit_keeps_ephemeral_manager_alive_through_result_truth():
    definitions = {key: _definition(key) for key in SOURCES}
    assert ast.dump(definitions["pinned"][1]) == ast.dump(definitions["before"][1])
    assert ast.dump(definitions["pinned"][1]) != ast.dump(definitions["current"][1])
    baseline = os.environ.get("NAPARIXX_CONTEXT_LIFETIME_BASELINE")
    assert baseline in (None, "1"), "Use explicit baseline=1 or unset installed mode"
    native = None
    selected = "before"
    if baseline is None:
        native = importlib.import_module("naparixx._native")
        assert callable(native.layer_slicing_set_view_slice)
        selected = "current"
    original = _controller(definitions["pinned"])
    actual = _controller(definitions[selected], native)
    assert str(inspect.signature(original.set_view_slice)) == str(
        inspect.signature(actual.set_view_slice)
    )
    assert original.set_view_slice.__doc__ == actual.set_view_slice.__doc__
    observed = _run(original)
    assert _run(actual) == observed
    assert observed == (
        (
            "enter",
            "body",
            ("exit", "RuntimeError", "slice body", True),
            ("truth-manager-alive", True),
            "result-released",
            "manager-released",
            "suppressed",
        ),
        1,
        True,
    )
