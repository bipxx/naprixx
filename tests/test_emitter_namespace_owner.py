"""Emitter namespace ownership with unchanged pinned production contracts.

Native-base cases intentionally fail before the namespace ownership port.
Contract cases compare the independently loaded pinned implementation.
"""

import copy
import gc
import importlib.util
import pickle
import re
import sys
import weakref
from contextlib import contextmanager
from pathlib import Path
from types import MappingProxyType

import pytest

from napari.utils.events import event as actual
from naparixx import _native

TRIPLE = ("_callbacks", "_callback_refs", "_callback_pass_event")


@pytest.fixture(scope="module")
def reference():
    path = (
        Path(__file__).resolve().parents[1]
        / ".upstream/napari/src/napari/utils/events/event.py"
    )
    name = "_original_emitter_namespace_owner_event"
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    for value in vars(module).values():
        if isinstance(value, type) and value.__module__ == name:
            value.__module__ = actual.__name__
    yield module
    sys.modules.pop(name, None)


def _error(function):
    try:
        function()
    except Exception as error:
        message = re.sub(r"0x[0-9a-fA-F]+", "0xADDRESS", str(error))
        return type(error).__name__, message
    return None


def _compare(reference, scenario):
    expected = scenario(reference)
    observed = scenario(actual)
    assert observed == expected
    return observed


@contextmanager
def _pickle_classes(module):
    """Resolve pinned class globals without altering their implementations."""
    previous = {}
    if module is not actual:
        for name, value in vars(module).items():
            if isinstance(value, type) and value.__module__ == actual.__name__:
                previous[name] = getattr(actual, name)
                setattr(actual, name, value)
    try:
        yield
    finally:
        for name, value in previous.items():
            setattr(actual, name, value)


def _pickle_callback(event):
    event.pickle_callback_seen = True


def test_native_base_dictionary_replacement_controls_subscription_and_emission():
    base = getattr(_native, "EventEmitterBase", None)
    assert base is not None, "EventEmitter still lacks a native namespace owner"
    emitter = actual.EventEmitter(type_name="change")
    assert isinstance(emitter, base)
    assert isinstance(actual.EmitterGroup(change=None), base)
    assert "__dict__" not in actual.EventEmitter.__dict__
    descriptor = base.__dict__["__dict__"]
    original = vars(emitter)
    output = []

    def first(event):
        output.append(("first", event.value))

    def second(event):
        output.append(("second", event.value))

    supplied = dict(original)
    supplied.update(
        _callbacks=[first],
        _callback_refs=["native-dict"],
        _callback_pass_event=[True],
    )
    descriptor.__set__(emitter, supplied)
    assert descriptor.__get__(emitter) is supplied
    assert vars(emitter) is supplied
    assert object.__getattribute__(emitter, "__dict__") is supplied
    emitter.connect(second, ref="second")
    assert supplied["_callbacks"] == [first, second]
    assert original["_callbacks"] == []
    emitter(value=7)
    assert output == [("first", 7), ("second", 7)]
    supplied["_callback_pass_event"][:] = []
    emitter(value=8)
    assert output == [("first", 7), ("second", 7)]
    descriptor.__delete__(emitter)
    assert vars(emitter) == {}
    assert supplied["_callbacks"] == [first, second]


@pytest.mark.parametrize("kind", ["initialized", "uninitialized", "slotted", "group"])
def test_native_namespace_gc_visits_dictionary_and_type_once_without_weak_edge(kind):
    base = getattr(_native, "EventEmitterBase", None)
    assert base is not None, "EventEmitter still lacks a native namespace owner"

    class Slotted(actual.EventEmitter):
        __slots__ = ("token",)

    if kind == "slotted":
        emitter = Slotted(type_name="change")
        token = object()
        emitter.token = token
    elif kind == "group":
        emitter = actual.EmitterGroup(change=None)
    elif kind == "uninitialized":
        emitter = object.__new__(actual.EventEmitter)
    else:
        emitter = actual.EventEmitter(type_name="change")
    assert isinstance(emitter, base)
    saved = vars(emitter)
    supplied = dict(saved)
    emitter.__dict__ = supplied
    emitter_ref = weakref.ref(emitter)
    assert emitter.__weakref__ is emitter_ref
    referents = gc.get_referents(emitter)
    assert sum(value is supplied for value in referents) == 1
    assert sum(value is type(emitter) for value in referents) == 1
    assert not any(value is saved or value is emitter_ref for value in referents)
    if kind == "slotted":
        assert sum(value is token for value in referents) == 1
    assert len(referents) == (3 if kind == "slotted" else 2)


@pytest.mark.parametrize("edit", ["replacement", "deletion"])
def test_dictionary_release_publishes_new_namespace_before_payload_finalizer(
    reference, edit
):
    def scenario(module):
        emitter = module.EventEmitter(type_name="change")
        emitter.marker = "old"
        emitter_ref = weakref.ref(emitter)
        output = []
        replacement = {"marker": "new"}

        class Payload:
            def __del__(self):
                owner = emitter_ref()
                namespace = vars(owner)
                output.append(
                    (
                        namespace.get("marker"),
                        id(namespace) != old_identity,
                        (
                            namespace is replacement
                            if edit == "replacement"
                            else not namespace
                        ),
                    )
                )
                owner.observed = "payload-finalizer"

        emitter.release = Payload()
        old_identity = id(vars(emitter))
        if edit == "replacement":
            emitter.__dict__ = replacement
            assert vars(emitter) is replacement
        else:
            del emitter.__dict__
        return output, emitter.observed

    marker = "new" if edit == "replacement" else None
    assert _compare(reference, scenario) == (
        [(marker, True, True)],
        "payload-finalizer",
    )


def test_subtype_finalizer_resurrection_preserves_namespace_then_releases_weak_first(
    reference,
):
    def scenario(module):
        output = []
        resurrected = []

        class Resurrecting(module.EventEmitter):
            def __del__(self):
                output.append(("finalize", self.marker))
                resurrected.append(self)

        class Payload:
            def __del__(self):
                output.append(("payload", emitter_ref() is None))

        emitter = Resurrecting(type_name="change")
        emitter.marker = "alive"
        emitter.payload = Payload()
        namespace_identity = id(vars(emitter))
        emitter_ref = weakref.ref(
            emitter, lambda ref: output.append(("weak", ref() is None))
        )
        del emitter
        assert output == [("finalize", "alive")]
        recovered = resurrected.pop()
        assert emitter_ref() is recovered
        assert id(vars(recovered)) == namespace_identity
        assert isinstance(recovered.payload, Payload)
        del recovered
        return output, emitter_ref() is None, resurrected

    assert _compare(reference, scenario) == (
        [("finalize", "alive"), ("weak", True), ("payload", True)],
        True,
        [],
    )


def test_emitter_dynamic_subtype_cycle_collects_instance_and_heap_type(reference):
    def scenario(module):
        cls = type("OwnedEmitter", (module.EventEmitter,), {})
        emitter = cls(type_name="change")
        cls.owned_instance = emitter
        emitter_ref, type_ref = weakref.ref(emitter), weakref.ref(cls)
        del emitter, cls
        gc.collect()
        return emitter_ref() is None, type_ref() is None

    assert _compare(reference, scenario) == (True, True)


def test_exact_initial_namespace_and_group_inheritance(reference):
    def scenario(module):
        emitter = module.EventEmitter(type_name="change")
        namespace = vars(emitter)
        assert namespace is emitter.__dict__
        assert namespace is object.__getattribute__(emitter, "__dict__")
        assert type(namespace) is dict
        assert all(namespace[name] is getattr(emitter, name) for name in TRIPLE)
        assert all(type(namespace[name]) is list for name in TRIPLE)
        group = module.EmitterGroup(change=None)
        assert isinstance(group, module.EventEmitter)
        assert vars(group)["_emitters"]["change"] is group.change
        assert all(vars(group)[name] is getattr(group, name) for name in TRIPLE)
        return tuple(namespace), tuple(vars(group)), tuple(group)

    result = _compare(reference, scenario)
    assert result[0] == (
        "_callbacks",
        "_callback_refs",
        "_callback_pass_event",
        "_blocked",
        "_block_counter",
        "_emitting",
        "_source",
        "default_args",
        "event_class",
        "_ignore_callback_errors",
        "_print_callback_errors",
    )


@pytest.mark.parametrize("edit", ["raw_dict", "object_setattr"])
def test_raw_and_object_setattr_callback_triples_are_authoritative(reference, edit):
    def scenario(module):
        emitter = module.EventEmitter(type_name="change")
        namespace = vars(emitter)
        saved = tuple(namespace[name] for name in TRIPLE)
        output = []

        def callback(event):
            output.append(event.value)

        replacements = ([callback], ["raw"], [True])
        for name, value in zip(TRIPLE, replacements, strict=True):
            if edit == "raw_dict":
                namespace[name] = value
            else:
                object.__setattr__(emitter, name, value)
            assert namespace[name] is value
            assert getattr(emitter, name) is value
        emitter(value=2)
        namespace["_callback_refs"][:] = ["changed"]
        namespace["_callback_pass_event"][:] = []
        emitter(value=3)
        return output, emitter.callback_refs, tuple(map(len, saved))

    assert _compare(reference, scenario) == ([2], ("changed",), (0, 0, 0))


@pytest.mark.parametrize("subclass", [False, True])
def test_dictionary_replacement_preserves_exact_map_and_saved_aliases(
    reference, subclass
):
    def scenario(module):
        class Namespace(dict):
            pass

        emitter = module.EventEmitter(type_name="change")
        old = vars(emitter)
        supplied = Namespace(old) if subclass else dict(old)
        marker = object()
        supplied["marker"] = marker
        emitter.__dict__ = supplied
        assert vars(emitter) is supplied
        assert emitter.marker is marker
        assert all(supplied[name] is old[name] for name in TRIPLE)
        emitter.disconnect(None)
        assert all(getattr(emitter, name) is supplied[name] for name in TRIPLE)
        return (
            type(vars(emitter)).__name__,
            tuple(supplied),
            tuple(old),
            all(supplied[name] is not old[name] for name in TRIPLE),
        )

    _compare(reference, scenario)


def test_dictionary_deletion_keeps_saved_map_and_can_restore_emission(reference):
    def scenario(module):
        emitter = module.EventEmitter(type_name="change")
        output = []

        def callback(event):
            output.append(event.value)

        emitter.connect(callback, ref="saved")
        saved = vars(emitter)
        emitter_ref = weakref.ref(emitter)
        del emitter.__dict__
        fresh = vars(emitter)
        assert fresh == {} and fresh is not saved
        failure = _error(lambda: emitter.callbacks)
        assert len(saved["_callbacks"]) == 1
        emitter.__dict__ = saved
        emitter(value=9)
        return failure, output, vars(emitter) is saved, emitter_ref() is emitter

    _compare(reference, scenario)


@pytest.mark.parametrize("kind", ["none", "list", "mapping_proxy"])
def test_invalid_dictionary_assignment_keeps_namespace_and_pinned_error(
    reference, kind
):
    def scenario(module):
        emitter = module.EventEmitter(type_name="change")
        saved = vars(emitter)
        supplied = {"none": None, "list": [], "mapping_proxy": MappingProxyType({})}[
            kind
        ]
        failure = _error(lambda: object.__setattr__(emitter, "__dict__", supplied))
        return failure, vars(emitter) is saved, len(emitter.callbacks)

    observed = _compare(reference, scenario)
    assert observed[0][0] == "TypeError"
    assert observed[1:] == (True, 0)


def test_dictionary_subclass_attribute_access_bypasses_mapping_hooks(reference):
    def scenario(module):
        calls = []

        class Namespace(dict):
            def __getitem__(self, key):
                calls.append(("get", key))
                raise AssertionError("attribute access called __getitem__")

            def __setitem__(self, key, value):
                calls.append(("set", key))
                raise AssertionError("attribute access called __setitem__")

            def __delitem__(self, key):
                calls.append(("del", key))
                raise AssertionError("attribute access called __delitem__")

        emitter = module.EventEmitter(type_name="change")
        namespace = Namespace(vars(emitter))
        emitter.__dict__ = namespace
        emitter.extra = 7
        assert emitter.extra == 7
        del emitter.extra
        assert emitter.callbacks == ()
        emitter.connect(_pickle_callback, ref="callback")
        assert emitter().pickle_callback_seen
        return calls, vars(emitter) is namespace, emitter.callback_refs

    assert _compare(reference, scenario) == ([], True, ("callback",))


@pytest.mark.parametrize("failing_field", ["_callback_refs", "_callback_pass_event"])
def test_connect_publishes_triples_in_pinned_order_even_when_insert_raises(
    reference, failing_field
):
    def scenario(module):
        emitter = module.EventEmitter(type_name="change")
        output = []

        class FailingInsert(list):
            def insert(self, index, value):
                output.append(tuple(len(getattr(emitter, name)) for name in TRIPLE))
                super().insert(index, value)
                raise ValueError("insert failed after publication")

        installed = FailingInsert()
        vars(emitter)[failing_field] = installed
        failure = _error(lambda: emitter.connect(_pickle_callback, ref="partial"))
        assert getattr(emitter, failing_field) is installed
        event = emitter()
        return (
            failure,
            output,
            tuple(len(getattr(emitter, name)) for name in TRIPLE),
            emitter.callback_refs,
            getattr(event, "pickle_callback_seen", False),
        )

    _compare(reference, scenario)


def test_disconnect_all_replaces_each_list_without_clearing_saved_aliases(reference):
    def scenario(module):
        emitter = module.EventEmitter(type_name="change")
        emitter.connect(_pickle_callback, ref="original")
        namespace = vars(emitter)
        saved = tuple(namespace[name] for name in TRIPLE)
        assert emitter.disconnect(None) is None
        assert vars(emitter) is namespace
        assert all(
            namespace[name] is not value
            for name, value in zip(TRIPLE, saved, strict=True)
        )
        emitter.connect(_pickle_callback, ref="new")
        return tuple(map(len, saved)), saved[1], emitter.callback_refs

    assert _compare(reference, scenario) == ((1, 1, 1), ["original"], ("new",))


def test_disconnect_all_finalizer_reentry_observes_partial_list_replacement(reference):
    def scenario(module):
        emitter = module.EventEmitter(type_name="change")
        emitter_ref = weakref.ref(emitter)
        output = []

        class Callback:
            def __call__(self, event):
                pass

            def __del__(self):
                owner = emitter_ref()
                output.append(tuple(len(getattr(owner, name)) for name in TRIPLE))
                owner.connect(_pickle_callback, ref="reentered")
                output.append(tuple(len(getattr(owner, name)) for name in TRIPLE))

        emitter.connect(Callback(), ref="original")
        emitter.disconnect(None)
        event = emitter()
        return (
            output,
            tuple(len(getattr(emitter, name)) for name in TRIPLE),
            getattr(event, "pickle_callback_seen", False),
        )

    assert _compare(reference, scenario) == ([(0, 1, 1), (1, 2, 2)], (1, 0, 0), False)


def test_shallow_copy_has_distinct_namespace_shared_triples_then_replaces(reference):
    def scenario(module):
        emitter = module.EventEmitter(type_name="change")
        emitter.connect(_pickle_callback, ref="shared")
        alias = copy.copy(emitter)
        assert vars(alias) is not vars(emitter)
        assert all(getattr(alias, name) is getattr(emitter, name) for name in TRIPLE)
        alias.extra = "alias-only"
        assert "extra" not in vars(emitter)
        alias.disconnect(_pickle_callback)
        shared_lengths = tuple(len(getattr(emitter, name)) for name in TRIPLE)
        emitter.connect(_pickle_callback, ref="original")
        alias.disconnect(None)
        assert all(
            getattr(alias, name) is not getattr(emitter, name) for name in TRIPLE
        )
        return shared_lengths, emitter.callback_refs, alias.callback_refs

    assert _compare(reference, scenario) == ((0, 0, 0), ("original",), ())


def test_deepcopy_honors_memo_and_canonical_namespace_aliases(reference):
    def scenario(module):
        emitter = module.EventEmitter(type_name="change")
        emitter.connect(_pickle_callback, ref="deep")
        payload = []
        replacement = object()
        emitter.alias = {
            "callbacks": emitter._callbacks,
            "self": emitter,
            "data": payload,
        }
        memo = {id(payload): replacement}
        cloned = copy.deepcopy(emitter, memo)
        assert memo[id(emitter)] is cloned
        assert vars(cloned) is not vars(emitter)
        assert all(
            getattr(cloned, name) is not getattr(emitter, name) for name in TRIPLE
        )
        assert cloned.alias["callbacks"] is cloned._callbacks
        assert cloned.alias["self"] is cloned
        assert cloned.alias["data"] is replacement
        assert cloned._block_counter is not emitter._block_counter
        assert cloned.event_class is emitter.event_class
        cloned.disconnect(_pickle_callback)
        return emitter.callback_refs, cloned.callback_refs, cloned.source

    assert _compare(reference, scenario) == (("deep",), (), None)


def test_default_reduce_and_state_reconstruction_preserve_namespace_contract(reference):
    def scenario(module):
        emitter = module.EventEmitter(type_name="change")
        emitter.connect(_pickle_callback, ref="state")
        assert emitter.__getstate__() is vars(emitter)
        no_setstate = _error(lambda: emitter.__setstate__)
        results = []
        for protocol in range(pickle.HIGHEST_PROTOCOL + 1):
            reduction = emitter.__reduce_ex__(protocol)
            factory, args, state = reduction[:3]
            assert state is vars(emitter)
            assert all(state[name] is getattr(emitter, name) for name in TRIPLE)
            rebuilt = factory(*args)
            assert vars(rebuilt) == {}
            vars(rebuilt).update(state)
            assert rebuilt.__getstate__() is vars(rebuilt)
            assert vars(rebuilt) is not state
            assert all(getattr(rebuilt, name) is state[name] for name in TRIPLE)
            results.append(
                (protocol, len(reduction), factory.__name__, rebuilt.callback_refs)
            )
        return results, no_setstate

    _compare(reference, scenario)


def test_pickle_roundtrip_of_pickle_compatible_namespace_preserves_aliases(reference):
    def scenario(module):
        emitter = module.EventEmitter(type_name="change")
        # The pinned weak counter contains an unpickleable local remove function.
        # A raw namespace may replace it, and default serialization preserves it.
        emitter._block_counter = None
        emitter.connect(_pickle_callback, ref="pickle")
        emitter.callback_alias = emitter._callbacks
        emitter.self_alias = emitter
        with _pickle_classes(module):
            cloned = pickle.loads(pickle.dumps(emitter, protocol=5))
        assert type(cloned) is module.EventEmitter
        assert vars(cloned) is not vars(emitter)
        assert cloned.callback_alias is cloned._callbacks
        assert cloned.self_alias is cloned
        assert cloned._callbacks is not emitter._callbacks
        assert cloned._block_counter is None
        assert cloned._callbacks[0] is _pickle_callback
        return cloned.callback_refs, cloned().pickle_callback_seen

    assert _compare(reference, scenario) == (("pickle",), True)


@pytest.mark.parametrize("kind", ["source", "bound_callback"])
def test_pickle_weak_reference_failure_matches_pinned_source(reference, kind):
    def scenario(module):
        class Owner:
            def callback(self, event):
                pass

        owner = Owner()
        emitter = module.EventEmitter(type_name="change")
        emitter._block_counter = None
        if kind == "source":
            emitter.source = owner
        else:
            emitter.connect(owner.callback)
        with _pickle_classes(module):
            return _error(lambda: pickle.dumps(emitter, protocol=5))

    observed = _compare(reference, scenario)
    assert observed == ("TypeError", "cannot pickle 'weakref.ReferenceType' object")


@pytest.mark.parametrize("slotted", [False, True])
def test_object_new_uninitialized_and_slotted_subclass_contract(reference, slotted):
    def scenario(module):
        class Slotted(module.EventEmitter):
            __slots__ = ("tag",)

        cls = Slotted if slotted else module.EventEmitter
        emitter = object.__new__(cls)
        assert vars(emitter) == {}
        emitter_ref = weakref.ref(emitter)
        failure = _error(lambda: emitter.callbacks)
        object.__setattr__(emitter, "extra", "retained")
        if slotted:
            emitter.tag = "slot-value"
            assert "tag" not in vars(emitter)
        cls.__init__(emitter, type_name="change")
        assert emitter.extra == "retained"
        emitter.connect(_pickle_callback, ref="initialized")
        cloned = copy.copy(emitter)
        assert vars(cloned) is not vars(emitter)
        assert cloned._callbacks is emitter._callbacks
        state = emitter.__getstate__()
        if slotted:
            assert state[0] is vars(emitter)
            assert state[1] == {"tag": "slot-value"}
            assert cloned.tag == emitter.tag
        else:
            assert state is vars(emitter)
        return (
            failure,
            tuple(vars(emitter)),
            emitter_ref() is emitter,
            emitter.callback_refs,
        )

    _compare(reference, scenario)


def test_source_weak_reference_does_not_keep_owner_alive(reference):
    def scenario(module):
        class Owner:
            pass

        owner = Owner()
        owner_ref = weakref.ref(owner)
        emitter = module.EventEmitter(source=owner, type_name="change")
        namespace = vars(emitter)
        assert namespace["_source"]() is owner
        del owner
        gc.collect()
        event = emitter()
        return (
            owner_ref() is None,
            emitter.source,
            event.sources,
            namespace is vars(emitter),
        )

    assert _compare(reference, scenario) == (True, None, [], True)


def test_bound_callback_dead_owner_cleanup_updates_saved_triples_in_place(reference):
    def scenario(module):
        class Owner:
            def callback(self, event):
                pass

        owner = Owner()
        owner.peer = owner
        emitter = module.EventEmitter(type_name="change")
        owner.emitter = emitter
        emitter.connect(owner.callback, ref="weak")
        saved = tuple(vars(emitter)[name] for name in TRIPLE)
        owner_ref = weakref.ref(owner)
        del owner
        gc.collect()
        assert owner_ref() is None
        before = tuple(map(len, saved))
        emitter()
        return before, tuple(map(len, saved)), emitter.callback_refs

    assert _compare(reference, scenario) == ((1, 1, 1), (0, 0, 0), ())


def test_callable_callback_cycle_collects_emitter_and_callback(reference):
    def scenario(module):
        class Callback:
            def __init__(self, emitter):
                self.emitter = emitter

            def __call__(self, event):
                pass

        emitter = module.EventEmitter(type_name="change")
        callback = Callback(emitter)
        emitter.connect(callback)
        emitter_ref, callback_ref = weakref.ref(emitter), weakref.ref(callback)
        del emitter, callback
        gc.collect()
        return emitter_ref() is None, callback_ref() is None

    assert _compare(reference, scenario) == (True, True)


def test_until_partial_callback_cycle_collects_all_owners(reference):
    def scenario(module):
        class Callback:
            def __init__(self, until):
                self.until = until

            def __call__(self, event):
                pass

        emitter = module.EventEmitter(type_name="change")
        until = module.EventEmitter(type_name="done")
        callback = Callback(until)
        emitter.connect(callback, until=until)
        refs = tuple(weakref.ref(value) for value in (emitter, until, callback))
        del emitter, until, callback
        gc.collect()
        return tuple(ref() is None for ref in refs)

    assert _compare(reference, scenario) == (True, True, True)


def test_replaced_namespace_self_cycle_is_visible_to_python_gc(reference):
    def scenario(module):
        class Namespace(dict):
            pass

        emitter = module.EventEmitter(type_name="change")
        namespace = Namespace(vars(emitter))
        namespace["owner"] = emitter
        emitter.__dict__ = namespace
        emitter_ref, namespace_ref = weakref.ref(emitter), weakref.ref(namespace)
        del namespace, emitter
        gc.collect()
        return emitter_ref() is None, namespace_ref() is None

    assert _compare(reference, scenario) == (True, True)


def test_group_shallow_namespace_aliases_keep_child_behavior_independent(reference):
    def scenario(module):
        group = module.EmitterGroup(change=None)
        output = []

        def first(event):
            output.append(("first", event.value))

        def second(event):
            output.append(("second", event.value))

        group.connect(first, ref="first")
        alias = copy.copy(group)
        assert vars(alias) is not vars(group)
        assert alias.emitters is group.emitters
        assert alias.change is group.change
        alias.disconnect(None)
        assert group.callback_refs == ("first",)
        assert alias.callback_refs == ()
        group.change(value=1)
        alias.connect(second, ref="second")
        group.change(value=2)
        group.disconnect(None)
        group.change(value=3)
        vars(alias)["alias_marker"] = 4
        assert "alias_marker" not in vars(group)
        children = group.emitters
        extra = module.EventEmitter(type_name="extra")
        children["extra"] = extra
        assert alias["extra"] is extra
        assert group["extra"] is extra
        return output, group.callback_refs, alias.callback_refs, tuple(alias)

    assert _compare(reference, scenario) == (
        [("first", 1), ("first", 2), ("second", 2), ("second", 3)],
        (),
        ("second",),
        ("change", "extra"),
    )
