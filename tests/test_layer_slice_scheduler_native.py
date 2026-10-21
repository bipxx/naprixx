"""Finite scheduler9 before/current comparisons; staged, not executed.

Exact pinned and current complete classes are compiled only by the test runner.
The actual installed native class is mandatory after its nine bodies change.
The pinned one-shot-iterator behavior remains unmodified: the fork's deliberate
materialization contract is tested separately. Real Future and RLock objects
remain authoritative; a deterministic executor controls publication boundaries.
Original worker/Viewer tests provide actual threads, Qt delivery and stale close
behavior. Private frames, arbitrary providers, platforms and full teardown remain
outside this finite cohort. No original application body is a production fallback.
"""

from __future__ import annotations

import ast
import builtins
import copy
import gc
import hashlib
import importlib
import pickle
import sys
import weakref
from concurrent.futures import Future, ThreadPoolExecutor
from pathlib import Path
from threading import Event, RLock, Thread
from types import ModuleType, SimpleNamespace

import pytest

ROOT = Path(__file__).resolve().parents[1]
PINNED = ROOT / ".upstream/napari/src/napari/components/_layer_slicer.py"
CURRENT = ROOT / "src/napari/components/_layer_slicer.py"
PINNED_SHA = "e554e1e2024040f74cd0dec15d865e027370cb742477b62c64626bcc1a06fb37"
BEFORE_CURRENT_SHA = "073cf6e4e98b94210dc94c2b74e17ee8f9e5edde792554fcd76c7a0e85c2f711"
METHODS = (
    "__init__", "force_sync", "wait_until_idle", "submit", "shutdown",
    "_slice_layers", "_on_slice_done", "_try_to_remove_task", "_find_existing_task",
)
EXPORTS = (
    "scheduler_init", "scheduler_force_sync", "scheduler_wait_idle",
    "scheduler_submit", "scheduler_shutdown", "scheduler_slice_layers",
    "scheduler_done", "scheduler_remove_task", "scheduler_find_task",
)


class ProbeError(RuntimeError):
    pass


class QuietLogger:
    def debug(self, *args, **kwargs):
        pass


class Request:
    def __init__(self, name, identifier, trace):
        self.name, self.id, self.trace = name, identifier, trace

    def __call__(self):
        self.trace.append(("request", self.name, self.id))
        return self.name, self.id


class AsyncLayer:
    def __init__(self, name, trace, *, visible=True, fail=False):
        self.name, self.trace, self.visible, self.fail = name, trace, visible, fail
        self.last_id, self.loaded = 0, True
        self.issued = []

    @property
    def _slicing_state(self):
        return self

    def _make_slice_request(self, dims):
        self.trace.append(("make", self.name, dims))
        if self.fail:
            raise ProbeError("request creation")
        request = Request(self.name, self.last_id + 1, self.trace)
        self.issued.append(weakref.ref(request))
        return request

    def _set_unloaded_slice_id(self, identifier):
        self.trace.append(("unloaded", self.name, identifier))
        self.last_id, self.loaded = identifier, False

    def _update_loaded_slice_id(self, identifier):
        self.trace.append(("loaded", self.name, identifier))
        if identifier == self.last_id:
            self.loaded = True

    def _update_slice_response(self, response):
        self.trace.append(("response", self.name, response))

    def _slice_dims(self, *, dims, force):
        self.trace.append(("sync", self.name, dims, force))


class SyncLayer:
    def __init__(self, name, trace):
        self.name, self.trace, self.visible = name, trace, True

    @property
    def _slicing_state(self):
        return self

    def _slice_dims(self, *, dims, force):
        self.trace.append(("sync", self.name, dims, force))


class ObservedFuture(Future):
    def __init__(self, owner, trace, *, callback_error=False):
        super().__init__()
        self.owner, self.trace = weakref.ref(owner), trace
        self.callback_error = callback_error

    def add_done_callback(self, callback):
        owner = self.owner()
        self.trace.append(("add_callback", self in owner._task_to_requests,
                           self in owner._layers_to_task.values()))
        if self.callback_error:
            raise ProbeError("callback registration")
        return super().add_done_callback(callback)


class ManualExecutor:
    """Test service: real futures, explicitly controlled completion."""

    def __init__(self, trace, *, immediate=False, callback_error=False):
        self.trace, self.immediate, self.callback_error = trace, immediate, callback_error
        self.pending = []

    def submit(self, callback, requests):
        self.trace.append(("submit",))
        future = ObservedFuture(callback.__self__, self.trace,
                                callback_error=self.callback_error)
        self.pending.append((future, callback, requests))
        if self.immediate:
            future.set_result(callback(requests))
        return future

    def shutdown(self, *, wait, cancel_futures):
        self.trace.append(("shutdown", wait, cancel_futures))
        if cancel_futures:
            for future, _, _ in self.pending:
                future.cancel()
        self.pending.clear()


class TraceLock:
    """The lock protocol probe still owns a genuine RLock."""

    def __init__(self, trace, *, suppress=False):
        self.lock, self.trace, self.suppress = RLock(), trace, suppress

    def __enter__(self):
        self.trace.append(("enter",))
        return self.lock.__enter__()

    def __exit__(self, kind, value, traceback):
        self.trace.append(("exit", None if kind is None else kind.__name__,
                           value is None, traceback is None))
        self.lock.__exit__(kind, value, traceback)
        return self.suppress


class RejectMap(dict):
    def __setitem__(self, key, value):
        raise ProbeError("second map publication")


def _class(source):
    return next(node for node in ast.parse(source).body
                if isinstance(node, ast.ClassDef) and node.name == "_LayerSlicer")


def _metadata(node):
    return (ast.dump(node.args), ast.dump(node.returns) if node.returns else None,
            [ast.dump(d) for d in node.decorator_list], ast.get_docstring(node, clean=False))


def _execute(source, namespace, path, *, candidate=False):
    parsed = ast.parse(source)
    selected = [node for node in parsed.body if (
        isinstance(node, ast.ImportFrom) and node.module == "__future__"
    ) or (isinstance(node, ast.ClassDef) and node.name in {
        "_SliceRequest", "_AsyncSliceable", "_LayerSlicer"
    })]
    tree = ast.Module(body=copy.deepcopy(selected), type_ignores=[])
    if candidate:
        owner = next(n for n in tree.body if isinstance(n, ast.ClassDef)
                     and n.name == "_LayerSlicer")
        owner.bases = [ast.Attribute(value=ast.Name(id="_native", ctx=ast.Load()),
                                     attr="LayerNamespaceBase", ctx=ast.Load())]
    ast.fix_missing_locations(tree)
    exec(compile(tree, str(path), "exec"), namespace)
    return namespace["_LayerSlicer"]


@pytest.fixture
def implementations():
    installed = importlib.import_module("napari.components._layer_slicer")
    before = PINNED.read_bytes()
    assert hashlib.sha256(before).hexdigest() == PINNED_SHA
    sources = {"original": before.decode("utf-8"),
               "current": CURRENT.read_text(encoding="utf-8")}
    modules = {}
    for name, source in sources.items():
        module_name = f"{__name__}_{name}"
        module = ModuleType(module_name)
        module.__dict__.update(installed.__dict__)
        module.__name__ = module_name
        module.__dict__["__builtins__"] = dict(vars(builtins))
        module.trace = []
        module.logger = QuietLogger()
        module.ThreadPoolExecutor = lambda *, max_workers, m=module: ManualExecutor(m.trace)
        module.get_settings = lambda: SimpleNamespace(experimental=SimpleNamespace(async_=True))
        module.RLock = RLock
        sys.modules[module_name] = module
        _execute(source, module.__dict__, PINNED if name == "original" else CURRENT)
        modules[name] = module
    try:
        yield modules
    finally:
        for module in modules.values():
            sys.modules.pop(module.__name__, None)


def _run_both(modules, exercise):
    outcomes = [exercise(modules[name]) for name in ("original", "current")]
    assert outcomes[1] == outcomes[0]
    return outcomes[0]


def test_whole_nine_body_metadata_and_actual_native_adoption(implementations):
    original = _class(PINNED.read_text(encoding="utf-8"))
    current = _class(CURRENT.read_text(encoding="utf-8"))
    for name in METHODS:
        before = next(n for n in original.body if isinstance(n, ast.FunctionDef) and n.name == name)
        after = next(n for n in current.body if isinstance(n, ast.FunctionDef) and n.name == name)
        assert _metadata(after) == _metadata(before), name
    native_calls = {node.func.attr for node in ast.walk(current)
                    if isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute)
                    and isinstance(node.func.value, ast.Name) and node.func.value.id == "_native"}
    if "scheduler_init" in native_calls:
        assert set(EXPORTS) <= native_calls
        cls = implementations["current"]._LayerSlicer
        assert implementations["current"]._native.SliceSchedulerNamespaceBase in cls.__mro__
        assert cls.__init__.__globals__ is implementations["current"].__dict__
    else:
        # A pre-port baseline is required to be the actual held source.
        assert hashlib.sha256(CURRENT.read_bytes()).hexdigest() == BEFORE_CURRENT_SHA


def test_physical_namespace_live_maps_restore_and_collection(implementations):
    for name, module in implementations.items():
        if name == "original":
            cls = _execute(PINNED.read_text(encoding="utf-8"), module.__dict__, PINNED,
                           candidate=True)
        else:
            cls = module._LayerSlicer
        instance = object.__new__(cls)
        first_map, second_map = {}, {}
        payload = {"_layers_to_task": first_map, "_task_to_requests": second_map,
                   "_force_sync": "marker"}
        instance.__dict__ = payload
        assert vars(instance) is payload
        assert instance._layers_to_task is first_map and instance._task_to_requests is second_map
        layer, task = SyncLayer("live", []), Future()
        first_map[(weakref.ref(layer),)] = task
        instance._lock_futures_dicts = RLock()
        assert instance._find_existing_task([layer]) is task
        assert instance._try_to_remove_task(task) and not first_map
        replacement = {"_force_sync": "replacement", "_layers_to_task": {},
                       "_task_to_requests": {}}
        instance.__dict__ = replacement
        assert vars(instance) is replacement and payload["_force_sync"] == "marker"
        # RLock/Future are intentionally not pickleable; restore the ordinary
        # canonical graph after exercising the real service objects.
        shared = []
        instance.__dict__ = {"left": shared, "right": shared, "_force_sync": False}
        for clone in (copy.copy(instance), copy.deepcopy(instance),
                      pickle.loads(pickle.dumps(instance))):
            assert clone.left is clone.right
            assert clone.__dict__ is not instance.__dict__
        reference = weakref.ref(instance)
        instance.loop = instance
        del instance, clone
        gc.collect()
        assert reference() is None


@pytest.mark.parametrize("stage", ["events", "executor", "settings", "lock"])
def test_constructor_failure_publishes_only_completed_original_fields(implementations, stage):
    def exercise(module):
        trace = []
        def fail(label, result):
            trace.append(label)
            if label == stage:
                raise ProbeError(label)
            return result
        module.EmitterGroup = lambda **kw: fail("events", SimpleNamespace())
        module.ThreadPoolExecutor = lambda **kw: fail("executor", ManualExecutor(trace))
        module.get_settings = lambda: fail("settings", SimpleNamespace(
            experimental=SimpleNamespace(async_=True)))
        module.RLock = lambda: fail("lock", RLock())
        instance = object.__new__(module._LayerSlicer)
        with pytest.raises(ProbeError, match=stage):
            module._LayerSlicer.__init__(instance)
        fields = tuple(vars(instance))
        expected = {"events": (), "executor": ("events",),
                    "settings": ("events", "_executor"),
                    "lock": ("events", "_executor", "_force_sync", "_layers_to_task",
                             "_task_to_requests")}
        assert fields == expected[stage]
        return trace, fields
    _run_both(implementations, exercise)


@pytest.mark.parametrize("transition", ["normal", "exception", "close", "abandon", "cycle"])
def test_force_sync_lazy_previous_identity_and_finally_cleanup(implementations, transition):
    def exercise(module):
        trace, previous = [], object()
        class Watched(module._LayerSlicer):
            def __setattr__(self, name, value):
                if name == "_force_sync":
                    trace.append("previous" if value is previous else value)
                return super().__setattr__(name, value)
        instance = object.__new__(Watched)
        instance._force_sync = previous
        trace.clear()
        generator = module._LayerSlicer.force_sync.__wrapped__(instance)
        assert instance._force_sync is previous and trace == []
        assert next(generator) is None and instance._force_sync is True
        if transition == "normal":
            with pytest.raises(StopIteration):
                next(generator)
        elif transition == "exception":
            failure = ProbeError("context body")
            with pytest.raises(ProbeError) as raised:
                generator.throw(failure)
            assert raised.value is failure
            del raised, failure
        elif transition == "close":
            assert generator.close() is None
        elif transition == "abandon":
            reference = weakref.ref(generator)
            del generator
            gc.collect()
            assert reference() is None
        else:
            instance.pending = generator
            references = weakref.ref(instance), weakref.ref(generator)
            del instance, generator
            gc.collect()
            assert all(reference() is None for reference in references)
            return trace
        assert instance._force_sync is previous
        return trace
    assert _run_both(implementations, exercise) == [True, "previous"]


@pytest.mark.parametrize("immediate", [False, True])
def test_submit_publishes_actual_future_maps_before_done_callback(implementations, immediate):
    def exercise(module):
        trace, dims = module.trace, object()
        module.ThreadPoolExecutor = lambda **kw: ManualExecutor(trace, immediate=immediate)
        instance = module._LayerSlicer()
        async_layer, sync_layer = AsyncLayer("async", trace), SyncLayer("sync", trace)
        task = instance.submit(layers=[async_layer, sync_layer], dims=dims, force=True)
        assert isinstance(task, Future)
        assert ("add_callback", True, True) in trace
        if immediate:
            assert task.done() and not instance._layers_to_task and not instance._task_to_requests
        else:
            key = next(iter(instance._layers_to_task))
            assert key[0]() is async_layer and instance._layers_to_task[key] is task
            assert instance._task_to_requests[task] is instance._executor.pending[0][2]
            assert next(iter(instance._task_to_requests[task])) is key[0]
        assert trace[-1] == ("sync", "sync", dims, True)
        assert trace.index(("submit",)) > trace.index(("unloaded", "async", 1))
        # Strip only the run-specific dims identity for paired comparison.
        return [tuple("dims" if value is dims else value for value in row) for row in trace]
    _run_both(implementations, exercise)


@pytest.mark.parametrize("visible,force_sync", [(True, False), (False, False), (True, True)])
def test_submit_visibility_and_force_sync_keep_original_paths(implementations, visible, force_sync):
    def exercise(module):
        instance = module._LayerSlicer()
        instance._force_sync = force_sync
        layer = AsyncLayer("layer", module.trace, visible=visible)
        result = instance.submit(layers=[layer], dims=None, force=False)
        asynchronous = visible and not force_sync
        assert (result is not None) == asynchronous
        assert layer.loaded is not asynchronous
        assert any(row[0] == "sync" for row in module.trace) is (not asynchronous)
        return module.trace
    _run_both(implementations, exercise)


@pytest.mark.parametrize("asynchronous", [False, True])
def test_deliberate_fork_one_shot_contract_has_unmodified_pinned_control(implementations, asynchronous):
    for name, module in implementations.items():
        instance = module._LayerSlicer()
        instance._force_sync = not asynchronous
        layers = [AsyncLayer(str(i), module.trace) for i in range(2)]
        result = instance.submit(layers=(layer for layer in layers), dims=None)
        if name == "original":
            # Exact pinned _find_existing_task consumes the one-shot input.
            assert result is None and module.trace == []
        elif asynchronous:
            assert isinstance(result, Future)
            assert len(instance._task_to_requests[result]) == 2
            assert all(not layer.loaded for layer in layers)
        else:
            assert result is None
            assert [row[1] for row in module.trace if row[0] == "sync"] == ["0", "1"]


def test_submit_creation_error_keeps_partial_loading_and_source_locals(implementations):
    def exercise(module):
        instance = module._LayerSlicer()
        first = AsyncLayer("first", module.trace)
        second = AsyncLayer("second", module.trace, fail=True)
        try:
            instance.submit(layers=[first, second], dims=None)
        except ProbeError as error:
            saved = error
        else:
            raise AssertionError("request creation did not fail")
        assert not first.loaded and second.loaded
        assert not instance._layers_to_task and not instance._task_to_requests
        reference = first.issued[0]
        assert reference() is not None
        saved.__traceback__ = None
        del saved
        gc.collect()
        assert reference() is None
        return module.trace
    _run_both(implementations, exercise)


@pytest.mark.parametrize("suppress", [False, True])
def test_submit_partial_map_failure_uses_real_lock_exception_protocol(implementations, suppress):
    def exercise(module):
        instance = module._LayerSlicer()
        trace = module.trace
        instance._lock_futures_dicts = TraceLock(trace, suppress=suppress)
        instance._task_to_requests = RejectMap()
        layer = AsyncLayer("layer", trace)
        if suppress:
            result = instance.submit(layers=[layer], dims=None)
            assert isinstance(result, Future)
            assert ("add_callback", False, True) in trace
        else:
            with pytest.raises(ProbeError, match="second map publication"):
                instance.submit(layers=[layer], dims=None)
            assert not any(row[0] == "add_callback" for row in trace)
        assert len(instance._layers_to_task) == 1 and not instance._task_to_requests
        assert ("exit", "ProbeError", False, False) in trace
        return trace
    _run_both(implementations, exercise)


def test_submit_callback_failure_keeps_both_published_maps(implementations):
    def exercise(module):
        trace = module.trace
        module.ThreadPoolExecutor = lambda **kw: ManualExecutor(trace, callback_error=True)
        instance = module._LayerSlicer()
        with pytest.raises(ProbeError, match="callback registration"):
            instance.submit(layers=[AsyncLayer("async", trace), SyncLayer("sync", trace)], dims=None)
        task = next(iter(instance._task_to_requests))
        assert task in instance._layers_to_task.values()
        assert not any(row[0] == "sync" for row in trace)
        return trace
    _run_both(implementations, exercise)


@pytest.mark.parametrize("broken", [None, 0, 1])
def test_slice_layers_publishes_exact_result_only_after_all_requests(implementations, broken):
    def exercise(module):
        instance, seen = module._LayerSlicer(), []
        instance.events.ready.connect(lambda event: seen.append(event.value))
        def request(index):
            def call():
                module.trace.append(("request", index))
                if index == broken:
                    raise ProbeError("slice execution")
                return object()
            return call
        requests = {"first": request(0), "second": request(1)}
        if broken is None:
            result = instance._slice_layers(requests)
            assert seen == [result] and seen[0] is result
            assert tuple(result) == ("first", "second")
        else:
            with pytest.raises(ProbeError, match="slice execution"):
                instance._slice_layers(requests)
            assert seen == []
        return module.trace
    _run_both(implementations, exercise)


@pytest.mark.parametrize("completion", ["cancelled", "matching", "stale", "collected"])
def test_done_releases_maps_and_preserves_loading_notification_order(implementations, completion):
    def exercise(module):
        instance, trace = module._LayerSlicer(), module.trace
        layer = AsyncLayer("layer", trace)
        layer.last_id, layer.loaded = 4, False
        request, task = Request("layer", 4, trace), Future()
        reference = weakref.ref(layer)
        instance._layers_to_task[(reference,)] = task
        instance._task_to_requests[task] = {reference: request}
        failure = ProbeError("worker failure")
        notifications = []
        def receive_error(kind, value, traceback):
            assert not instance._layers_to_task and not instance._task_to_requests
            assert kind is ProbeError and value is failure
            notifications.append(("receive_error",))
        manager = SimpleNamespace(receive_error=receive_error)
        original_import = builtins.__import__
        def importing(name, globals=None, locals=None, fromlist=(), level=0):
            if name == "napari.utils.notifications":
                assert globals is module.__dict__
                assert fromlist == ("notification_manager",) and level == 0
                trace.append(("notification_import",))
                return SimpleNamespace(notification_manager=manager)
            return original_import(name, globals, locals, fromlist, level)
        module.__dict__["__builtins__"]["__import__"] = importing
        if completion == "cancelled":
            task.cancel()
        else:
            task.set_exception(failure)
        if completion == "stale":
            layer.last_id = 5
        elif completion == "collected":
            del layer
            gc.collect()
            assert reference() is None
        instance._on_slice_done(task)
        assert not instance._layers_to_task and not instance._task_to_requests
        if completion == "cancelled":
            assert not notifications and layer.loaded is False and trace == []
        else:
            assert notifications == [("receive_error",)]
            if completion != "collected":
                assert layer.loaded is (completion == "matching")
                assert trace.index(("loaded", "layer", 4)) < trace.index(("notification_import",))
        return trace, notifications
    _run_both(implementations, exercise)


def test_find_and_remove_use_live_map_aliases_and_ignore_dead_layers(implementations):
    def exercise(module):
        instance = module._LayerSlicer()
        first, second, dead = (SyncLayer(name, []) for name in ("a", "b", "dead"))
        dead_ref = weakref.ref(dead)
        hash(dead_ref)  # Real map keys are hashed while their layer is alive.
        del dead
        gc.collect()
        first_task, second_task = Future(), Future()
        live = {(dead_ref, weakref.ref(first)): first_task,
                (weakref.ref(second),): second_task}
        instance.__dict__["_layers_to_task"] = live
        assert instance._find_existing_task([first, second]) is first_task
        assert instance._try_to_remove_task(first_task)
        assert len(live) == 1 and instance._find_existing_task([first]) is None
        replacement = dict(vars(instance))
        replacement["_layers_to_task"] = {(weakref.ref(first),): second_task}
        instance.__dict__ = replacement
        assert vars(instance) is replacement
        assert instance._find_existing_task([first]) is second_task
        assert not instance._try_to_remove_task(first_task)
        return len(live), len(instance._layers_to_task)
    assert _run_both(implementations, exercise) == (1, 1)


@pytest.mark.parametrize("remaining", [False, True])
def test_wait_uses_fresh_global_and_exact_live_values_view(implementations, remaining):
    def exercise(module):
        instance = module._LayerSlicer()
        task, timeout, calls = Future(), 0, []
        instance._layers_to_task[(None,)] = task
        def waiting(futures, *, timeout):
            assert type(futures) is type({}.values())
            assert list(futures) == [task]
            instance._layers_to_task[("later",)] = task
            assert len(futures) == 2
            calls.append(("wait", timeout))
            return set(), {task} if remaining else set()
        module.wait = waiting
        if remaining:
            with pytest.raises(TimeoutError) as raised:
                instance.wait_until_idle(timeout=timeout)
            assert str(raised.value) == "Slicing 1 tasks did not complete within timeout (0s)."
        else:
            assert instance.wait_until_idle(timeout=timeout) is None
        return calls
    assert _run_both(implementations, exercise) == [("wait", 0)]


@pytest.mark.parametrize("executor_error", [False, True])
def test_shutdown_order_and_failure_preserve_original_disconnect_boundary(implementations, executor_error):
    def exercise(module):
        instance, trace = module._LayerSlicer(), []
        def shutdown(**kwargs):
            trace.append(("executor", kwargs))
            if executor_error:
                raise ProbeError("shutdown failure")
        instance._executor = SimpleNamespace(shutdown=shutdown)
        instance.events = SimpleNamespace(
            disconnect=lambda: trace.append(("group",)),
            ready=SimpleNamespace(disconnect=lambda: trace.append(("ready",))),
        )
        if executor_error:
            with pytest.raises(ProbeError, match="shutdown failure"):
                instance.shutdown()
        else:
            assert instance.shutdown() is None
        expected = [("executor", {"wait": True, "cancel_futures": True})]
        if not executor_error:
            expected += [("group",), ("ready",)]
        assert trace == expected
        return trace
    _run_both(implementations, exercise)


def test_real_shutdown_waits_running_task_and_cancels_pending_task(implementations):
    def exercise(module):
        module.ThreadPoolExecutor = ThreadPoolExecutor
        instance = module._LayerSlicer()
        entered, release, cancelled = Event(), Event(), Event()
        errors, ready_results = [], []
        instance.events.ready.connect(lambda event: ready_results.append(event.value))
        running_layer, pending_layer = SyncLayer("running", []), SyncLayer("pending", [])
        running_ref, pending_ref = weakref.ref(running_layer), weakref.ref(pending_layer)
        def blocking():
            entered.set()
            if not release.wait(5):
                raise ProbeError("release timeout")
            return "running response"
        # Two real futures through the complete slicing operation; scheduling
        # policy itself is covered by paired submit and the original fixtures.
        running_requests = {running_ref: blocking}
        pending_requests = {pending_ref: lambda: "must not execute"}
        running = instance._executor.submit(instance._slice_layers, running_requests)
        assert entered.wait(5)
        pending = instance._executor.submit(instance._slice_layers, pending_requests)
        instance._layers_to_task[(running_ref,)] = running
        instance._layers_to_task[(pending_ref,)] = pending
        instance._task_to_requests[running] = running_requests
        instance._task_to_requests[pending] = pending_requests
        running.add_done_callback(instance._on_slice_done)
        pending.add_done_callback(instance._on_slice_done)
        pending.add_done_callback(lambda future: cancelled.set() if future.cancelled() else None)
        def releasing():
            try:
                assert cancelled.wait(5)
                assert running.running() and not running.done()
            except BaseException as error:
                errors.append(error)
            finally:
                release.set()
        observer = Thread(target=releasing)
        observer.start()
        try:
            instance.shutdown()
        finally:
            release.set()
            observer.join(5)
            instance._executor.shutdown(wait=True, cancel_futures=True)
        assert not observer.is_alive() and not errors
        result = running.result(timeout=0)
        assert pending.cancelled() and result == {running_ref: "running response"}
        assert not instance._layers_to_task and not instance._task_to_requests
        assert ready_results == [result] and ready_results[0] is result
        with pytest.raises(RuntimeError):
            instance._executor.submit(lambda: None)
        return pending.cancelled(), len(ready_results)
    assert _run_both(implementations, exercise) == (True, 1)


def test_pending_future_callback_cycle_has_no_persistent_scheduler_root(implementations):
    def exercise(module):
        instance = module._LayerSlicer()
        task = instance.submit(layers=[AsyncLayer("layer", module.trace)], dims=None)
        references = weakref.ref(instance), weakref.ref(task)
        del instance, task
        gc.collect()
        assert all(reference() is None for reference in references)
        return True
    assert _run_both(implementations, exercise)
