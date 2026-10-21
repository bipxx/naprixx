"""Event contracts compared with the independent, unmodified pinned module."""

import gc
import importlib.util
import inspect
import re
import sys
import warnings
import weakref
from functools import partial
from pathlib import Path
from types import SimpleNamespace

import pytest

from napari.utils.events import debugging as actual_debug
from napari.utils.events import event as actual


@pytest.fixture(scope="module")
def reference():
    path = (
        Path(__file__).resolve().parents[1]
        / ".upstream/napari/src/napari/utils/events/event.py"
    )
    name = "_original_native_event_engine"
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    # Load independent implementations while preserving the production module
    # identity used by the event engine's core-callback priority contract.
    for value in vars(module).values():
        if isinstance(value, type) and value.__module__ == name:
            value.__module__ = actual.__name__
    yield module
    sys.modules.pop(name, None)


@pytest.fixture(scope="module")
def reference_debug():
    path = (
        Path(__file__).resolve().parents[1]
        / ".upstream/napari/src/napari/utils/events/debugging.py"
    )
    name = "_original_native_event_debugging"
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    yield module
    sys.modules.pop(name, None)


def normalized(value):
    text = (
        str(value)
        .replace(actual.__name__ + ".", "")
        .replace("_original_native_event_engine.", "")
    )
    return re.sub(r"0x[0-9a-fA-F]+", "0xADDRESS", text)


def error(call):
    try:
        call()
    except BaseException as exc:
        return type(exc).__name__, normalized(exc)
    return None


def compare(reference, scenario):
    results = [scenario(module) for module in (reference, actual)]
    assert results[1] == results[0]
    return results[1]


@pytest.mark.parametrize("position", ["first", "last", "invalid", None, 7])
@pytest.mark.parametrize("ref", [False, True, "custom", None, 3])
def test_subscription_options(reference, position, ref):
    def scenario(module):
        emitter = module.EventEmitter(type_name="change")
        output = []

        def first(event):
            output.append("first")

        def second(event):
            output.append("second")

        emitter.connect(first, ref="existing")
        failure = error(lambda: emitter.connect(second, ref=ref, position=position))
        duplicate = emitter.connect(first) is None
        emitter()
        return failure, duplicate, emitter.callback_refs, output

    compare(reference, scenario)


@pytest.mark.parametrize(
    "before,after",
    [
        (None, None),
        ("a", None),
        (None, "a"),
        ("a", "b"),
        (["a", "b"], None),
        ("missing", None),
        ([], []),
    ],
)
def test_reference_ordering_and_failed_insertion(reference, before, after):
    def scenario(module):
        emitter = module.EventEmitter(type_name="change")
        output = []
        callbacks = [
            lambda event, name=name: output.append(name) for name in ("a", "b", "c")
        ]
        for name, callback in zip(("a", "b"), callbacks, strict=False):
            emitter.connect(callback, ref=name)
        failure = error(
            lambda: emitter.connect(callbacks[2], ref="c", before=before, after=after)
        )
        emitter()
        return failure, emitter.callback_refs, output

    compare(reference, scenario)


def test_core_partial_priority_and_until(reference):
    def scenario(module):
        emitter = module.EventEmitter(type_name="change")
        until = module.EventEmitter(type_name="done")
        output = []

        def external(event):
            output.append("external")

        def core(event):
            output.append("core")

        core.__module__ = "napari.some_plugin_contract"
        emitter.connect(external, until=until)
        emitter.connect(partial(core))
        emitter()
        until()
        emitter()
        return output, len(emitter.callbacks)

    assert compare(reference, scenario) == (["core", "external", "core"], 1)


@pytest.mark.parametrize("mode", ["mutate", "reenter", "block", "stop"])
def test_callback_snapshot_reentry_and_blocking(reference, mode):
    def scenario(module):
        class Owner:
            pass

        source = Owner()
        emitter = module.EventEmitter(source=source, type_name="change")
        output = []

        def added(event):
            output.append(("added", event.value, len(event.sources)))

        def second(event):
            output.append(("second", event.value, len(event.sources)))

        def first(event):
            output.append(("first", event.value, len(event.sources)))
            if event.value == 0:
                if mode == "mutate":
                    emitter.disconnect(second)
                    emitter.connect(added)
                elif mode == "reenter":
                    emitter(value=1)
                elif mode == "block":
                    emitter.block(second)
                else:
                    event.blocked = True

        emitter.connect(first)
        emitter.connect(second)
        event = emitter(value=0)
        snapshot = list(output)
        emitter(value=2)
        return snapshot, output, event.sources, emitter._emitting

    compare(reference, scenario)


def test_weak_bound_owner_and_callback_cycles_collect(reference):
    def scenario(module):
        emitter = module.EventEmitter(type_name="change")

        class Owner:
            def callback(self, event):
                pass

        owner = Owner()
        owner_ref = weakref.ref(owner)
        emitter.connect(owner.callback)
        del owner
        gc.collect()
        dead_before_emit = owner_ref() is None
        emitter()
        count = len(emitter.callbacks)

        class Cycle:
            def __init__(self):
                self.events = module.EventEmitter(type_name="change")
                self.events.connect(lambda event: self)

        cycle = Cycle()
        cycle_ref = weakref.ref(cycle)
        emitter_ref = weakref.ref(cycle.events)
        del cycle
        gc.collect()
        return dead_before_emit, count, cycle_ref() is None, emitter_ref() is None

    assert compare(reference, scenario) == (True, 0, True, True)


def test_missing_method_warns_and_disconnect_owner_replaces_lists(reference):
    def scenario(module):
        class Owner:
            def callback(self, event):
                pass

        owner = Owner()
        emitter = module.EventEmitter(type_name="change")
        emitter.connect(owner.callback)
        owner.callback = None
        with warnings.catch_warnings(record=True) as caught:
            warnings.simplefilter("always")
            emitter()
        saved = emitter._callbacks
        messages = [(type(w.message).__name__, normalized(w.message)) for w in caught]
        emitter.disconnect(owner)
        emitter.connect(lambda event: None)
        saved_all = emitter._callbacks
        emitter.disconnect()
        return messages, len(saved), len(saved_all), saved_all is emitter._callbacks

    compare(reference, scenario)


@pytest.mark.parametrize("kind", [ValueError, KeyboardInterrupt, SystemExit])
@pytest.mark.parametrize("cleanup_fails", [False, True])
def test_emission_cleanup_preserves_active_exception(reference, kind, cleanup_fails):
    def scenario(module):
        output = []
        failure = kind("callback failed")

        class TracedEvent(module.Event):
            def _pop_source(self):
                output.append(
                    (type(sys.exception()).__name__, sys.exception() is failure)
                )
                value = super()._pop_source()
                if cleanup_fails:
                    raise OSError("cleanup failed")
                return value

        emitter = module.EventEmitter(type_name="change", event_class=TracedEvent)

        def callback(event):
            raise failure

        emitter.connect(callback)
        event = TracedEvent("change")
        try:
            emitter(event)
        except BaseException as exc:
            output.append(
                (type(exc).__name__, str(exc), type(exc.__context__).__name__)
            )
        return output, event.sources, emitter._emitting

    compare(reference, scenario)


@pytest.mark.parametrize("ignore", [False, True])
def test_callback_handler_receives_original_exception(reference, monkeypatch, ignore):
    def scenario(module):
        output = []
        failure = LookupError("boom")

        def handler(received_ignore, printing, emitter, cb_event):
            exception, value, traceback = sys.exc_info()
            output.append(
                (
                    received_ignore,
                    printing,
                    exception.__name__,
                    value is failure,
                    traceback is failure.__traceback__,
                    cb_event[1].type,
                )
            )
            if not received_ignore:
                raise

        with monkeypatch.context() as patch:
            patch.setattr(module, "_handle_exception", handler)
            emitter = module.EventEmitter(type_name="change")
            emitter.ignore_callback_errors = ignore

            def callback(event):
                raise failure

            emitter.connect(callback)
            emitter.connect(lambda event: output.append("continued"))
            result = error(emitter)
        return output, result, emitter._emitting

    compare(reference, scenario)


@pytest.mark.parametrize(
    "message",
    [
        "wrapped C++ object has been deleted",
        "C++ object already deleted.",
        "some unrelated error",
    ],
)
def test_deleted_qt_callback_cleanup(reference, message):
    def scenario(module):
        emitter = module.EventEmitter(type_name="change")

        def callback(event):
            raise RuntimeError(message)

        emitter.connect(callback)
        result = error(emitter)
        return result, len(emitter.callbacks)

    compare(reference, scenario)


def test_group_hooks_blockers_and_live_iteration(reference):
    def scenario(module):
        output = []

        class Owner:
            def first_callback_connect(self):
                output.append("first")

            def last_callback_disconnect(self):
                output.append("last")

            def on_change(self, event):
                output.append(("auto", event.type))

        owner = Owner()
        group = module.EmitterGroup(
            source=owner, auto_connect=True, change=module.Event
        )

        def callback(event):
            output.append(("group", event.type, len(event.sources)))

        group.connect(callback)
        group.change()
        with group.blocker_all() as value:
            assert value is None
            group.change()
        group.disconnect(callback)
        group.change()
        iterator = iter(group)
        generator = inspect.isgenerator(iterator)
        group.add(extra=None)
        names = list(iterator)
        blocker = group.change.blocker()
        with blocker as entered:
            assert entered is blocker
            group.change()
            group.change()
        return output, names, generator, blocker.count, group.ignore_callback_errors

    compare(reference, scenario)


@pytest.mark.parametrize("names", [("new", "source"), ("new", "old"), ("new", "bad")])
def test_group_name_validation_and_partial_bad_value(reference, names):
    def scenario(module):
        group = module.EmitterGroup(old=module.Event)
        kwargs = {names[0]: None, names[1]: 5 if names[1] == "bad" else None}
        failure = error(lambda: group.add(**kwargs))
        return failure, tuple(group)

    compare(reference, scenario)


def test_event_repr_flags_and_failure_depth_cleanup(reference):
    def scenario(module):
        class Recursive(module.Event):
            @property
            def recurse(self):
                return self

        class Broken(module.Event):
            @property
            def broken(self):
                raise ValueError("bad property")

        recursive = Recursive("change", _native="overridden")
        recursive.handled = [1]
        recursive.blocked = []
        representation = repr(recursive)
        failure = error(lambda: repr(Broken("change")))
        return representation, recursive.native, failure, module._event_repr_depth

    compare(reference, scenario)


@pytest.mark.parametrize(
    "mode", ["normal", "nonmapping", "duplicate", "bad_keys", "nonstring"]
)
def test_default_mapping_keyword_expansion(reference, mode):
    def scenario(module):
        class Mapping:
            def copy(self):
                return self

            def update(self, kwargs):
                pass

            def keys(self):
                if mode == "bad_keys":
                    raise AttributeError("keys unavailable")
                if mode == "duplicate":
                    return ["type_name", "type_name"]
                return [3] if mode == "nonstring" else ["type_name"]

            def __getitem__(self, name):
                return "change"

        class NonMapping:
            def copy(self):
                return self

            def update(self, kwargs):
                pass

        emitter = module.EventEmitter(type_name="change")
        emitter.default_args = NonMapping() if mode == "nonmapping" else Mapping()
        return error(emitter), emitter._emitting

    compare(reference, scenario)


def test_public_override_boundaries_and_source_stack(reference):
    def scenario(module):
        output = []

        class Owner:
            pass

        class Custom(module.EventEmitter):
            def _normalize_cb(self, callback):
                output.append("normalize")
                return super()._normalize_cb(callback)

            def _is_core_callback(self, callback, core):
                output.append("core")
                return super()._is_core_callback(callback, core)

            def _prepare_event(self, *args, **kwargs):
                output.append("prepare")
                return super()._prepare_event(*args, **kwargs)

            def _invoke_callback(self, callback, event):
                output.append("invoke")
                return super()._invoke_callback(callback, event)

        first_owner, second_owner = Owner(), Owner()
        first = Custom(source=first_owner, type_name="change")
        second = module.EventEmitter(source=second_owner, type_name="change")
        first.connect(second)
        second.connect(
            lambda event: output.append(
                (
                    event.source is second_owner,
                    event.sources == [first_owner, second_owner],
                )
            )
        )
        event = first()
        return output, event.sources

    compare(reference, scenario)


@pytest.mark.parametrize(
    "options",
    [
        {},
        {"stack_depth": 0},
        {"stack_depth": 1},
        {"stack_depth": -1},
        {"include_events": {"change"}},
        {"include_events": {"other"}},
        {"exclude_events": {"change"}},
        {"include_emitters": {"Owner"}},
        {"include_emitters": {"other"}},
        {"exclude_emitters": {"Owner"}},
        {"nesting_allowance": 1},
    ],
)
@pytest.mark.parametrize("initial_depth", [0, 1])
def test_tracing_filters_formatting_and_depth_cleanup(
    reference_debug, monkeypatch, options, initial_depth
):
    results = []
    for module in (reference_debug, actual_debug):

        class Owner:
            pass

        owner = Owner()
        cfg = module.EventDebugSettings(**options)
        cfg._cur_depth = initial_depth
        event = actual.Event("change", value=3)
        event._push_source(owner)
        output = []

        class Producer:
            pass

        Producer.__module__ = "napari.probe"
        frames = [
            SimpleNamespace(
                filename="trace.py",
                lineno=1,
                function="log",
                frame=SimpleNamespace(f_locals={}),
            ),
            SimpleNamespace(
                filename="emit.py",
                lineno=2,
                function="emit",
                frame=SimpleNamespace(f_locals={}),
            ),
            SimpleNamespace(
                filename=module.ROOT_DIR + "/app.py",
                lineno=10,
                function="produce",
                frame=SimpleNamespace(f_locals={"self": Producer()}),
            ),
            SimpleNamespace(
                filename=module._SP + "/plugin.py",
                lineno=20,
                function="callback",
                frame=SimpleNamespace(f_locals={}),
            ),
        ]
        with monkeypatch.context() as patch:
            patch.setattr(module, "print", output.append, raising=False)
            patch.setattr(
                module.inspect, "stack", lambda context, frames=frames: frames
            )
            module.log_event_stack(event, cfg)
        during = cfg._cur_depth
        popped = event._pop_source() is owner
        results.append((output, during, cfg._cur_depth, popped, event.sources))
    assert results[1] == results[0]


@pytest.mark.parametrize("location", ["site", "stdlib", "napari", "unrelated"])
def test_trace_filename_shortening(reference_debug, location):
    results = []
    for module in (reference_debug, actual_debug):
        prefix = {
            "site": module._SP,
            "stdlib": module._STD_LIB,
            "napari": module.ROOT_DIR,
            "unrelated": "external",
        }[location]
        results.append(module._shorten_fname(prefix + "/module.py"))
    assert results[1] == results[0]


def test_trace_print_failure_leaves_cleanup_unmodified(reference_debug, monkeypatch):
    results = []
    for module in (reference_debug, actual_debug):
        cfg = module.EventDebugSettings(stack_depth=0)
        event = actual.Event("change")
        event._push_source(None)
        original = event._pop_source

        def fail(message):
            raise OSError("print failed")

        with monkeypatch.context() as patch:
            patch.setattr(module, "print", fail, raising=False)
            patch.setattr(module.inspect, "stack", lambda context: [])
            failure = error(
                lambda module=module, event=event, cfg=cfg: module.log_event_stack(
                    event, cfg
                )
            )
        results.append((failure, cfg._cur_depth, event._pop_source == original))
    assert results[1] == results[0]


def test_tracing_cleanup_cycle_is_visible_to_python_gc(monkeypatch):
    cfg = actual_debug.EventDebugSettings(stack_depth=0)
    event = actual.Event("change")
    event._push_source(None)
    with monkeypatch.context() as patch:
        patch.setattr(actual_debug, "print", lambda message: None, raising=False)
        patch.setattr(actual_debug.inspect, "stack", lambda context: [])
        actual_debug.log_event_stack(event, cfg)
    event_ref, cfg_ref = weakref.ref(event), weakref.ref(cfg)
    del event, cfg
    gc.collect()
    assert event_ref() is None and cfg_ref() is None


@pytest.mark.parametrize("configured", [False, True])
def test_tracing_activation_uses_independent_reference(
    reference, reference_debug, monkeypatch, configured
):
    results = []
    for event_module, debug_module in (
        (reference, reference_debug),
        (actual, actual_debug),
    ):
        cfg = debug_module.EventDebugSettings(stack_depth=0) if configured else None
        with monkeypatch.context() as patch:
            patch.setitem(sys.modules, "napari.utils.events.debugging", debug_module)
            patch.setattr(event_module, "_log_event_stack", event_module._noop)
            event_module.set_event_tracing_enabled(True, cfg)
            callback = event_module._log_event_stack
            function = callback.func if configured else callback
            assert function is debug_module.log_event_stack
            event_module.set_event_tracing_enabled(False)
            results.append(event_module._log_event_stack is event_module._noop)
    assert results == [True, True]
