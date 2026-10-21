"""Dictionary behavior against independent, unmodified pinned implementations.

The oracle loads both original mapping modules, so its evented and namespace
classes inherit the original typed mapping. The event engine and third-party
services are shared; these are subsystem comparisons, not application parity.
"""

import gc
import importlib.util
import inspect
import sys
import types
import weakref
from collections.abc import Mapping
from pathlib import Path

import numpy as np
import pytest
from psygnal import EventedModel as PsygnalModel

from napari.utils.events import EmitterGroup, Event
from napari.utils.events.containers import _dict, _evented_dict


@pytest.fixture(scope="module")
def reference():
    directory = (
        Path(__file__).resolve().parents[1]
        / ".upstream/napari/src/napari/utils/events/containers"
    )
    loaded, originals = {}, {}
    try:
        for name in ("_dict", "_evented_dict"):
            production = "napari.utils.events.containers." + name
            identifier = "_original_dictionary" + name
            spec = importlib.util.spec_from_file_location(
                identifier, directory / (name + ".py")
            )
            module = importlib.util.module_from_spec(spec)
            sys.modules[identifier] = module
            spec.loader.exec_module(module)
            for value in vars(module).values():
                if isinstance(value, type) and value.__module__ == identifier:
                    value.__module__ = production
                    for method in vars(value).values():
                        if isinstance(method, (staticmethod, classmethod)):
                            method = method.__func__
                        if isinstance(method, types.FunctionType):
                            method.__module__ = production
            loaded[name] = module
            originals[production] = sys.modules[production]
            sys.modules[production] = module
    finally:
        sys.modules.update(originals)
    yield loaded
    for name in loaded:
        sys.modules.pop("_original_dictionary" + name, None)


KINDS = ("typed", "evented", "namespace")


def mapping_class(modules, kind):
    if kind == "typed":
        return modules["_dict"].TypedMutableMapping
    return getattr(
        modules["_evented_dict"],
        "EventedDictNamespace" if kind == "namespace" else "EventedDict",
    )


def compare(reference, scenario):
    expected = scenario(reference)
    actual = scenario({"_dict": _dict, "_evented_dict": _evented_dict})
    assert actual == expected
    return actual


class Token:
    def __init__(self, tag):
        self.tag = tag

    def __repr__(self):
        return f"Token({self.tag!r})"


def stable(value):
    if isinstance(value, Token):
        return "token", value.tag
    if isinstance(value, np.ndarray):
        return "array", str(value.dtype), value.shape, value.tolist()
    if isinstance(value, Mapping):
        return tuple((stable(k), stable(v)) for k, v in value.items())
    if isinstance(value, (tuple, list)):
        return tuple(stable(v) for v in value)
    return value


def caught(call):
    try:
        return "value", stable(call())
    except Exception as error:

        def exception(value):
            return None if value is None else (type(value).__name__, str(value))

        return (
            "error",
            type(error).__name__,
            str(error),
            exception(error.__cause__),
            exception(error.__context__),
            error.__suppress_context__,
        )


def observe(mapping):
    log = []
    if not hasattr(mapping, "events"):
        return log

    def receive(event):
        fields = tuple(
            (name, stable(getattr(event, name)))
            for name in ("key", "old_value", "value")
            if hasattr(event, name)
        )
        log.append((event.type, fields, stable(mapping)))

    mapping.events.connect(receive)
    return log


@pytest.mark.parametrize("kind", KINDS)
@pytest.mark.parametrize("base", ((), int, (int, str)))
@pytest.mark.parametrize(
    "data_case",
    ("none", "mapping", "pairs", "generator", "empty", "bad_pair", "scalar", "mixed"),
)
def test_construction_partial_failures(reference, kind, base, data_case):
    def scenario(modules):
        data = {
            "none": lambda: None,
            "mapping": lambda: {"a": 1, False: 2, (4,): 3},
            "pairs": lambda: [("a", 1), ("b", 2)],
            "generator": lambda: iter([("a", 1), ("b", 2)]),
            "empty": lambda: [],
            "bad_pair": lambda: [("a", 1), ("broken",)],
            "scalar": lambda: 42,
            "mixed": lambda: {"a": 1, "b": "value", "c": 2.5},
        }[data_case]()
        cls = mapping_class(modules, kind)
        instance = cls.__new__(cls)
        result = caught(lambda: cls.__init__(instance, data, base))
        state = object.__getattribute__(instance, "__dict__")
        return result, stable(state.get("_dict")), state.get("_basetypes")

    compare(reference, scenario)


@pytest.mark.parametrize("kind", KINDS)
@pytest.mark.parametrize(
    "operation",
    (
        "get",
        "set",
        "same",
        "add",
        "delete",
        "missing_delete",
        "copy",
        "newlike",
        "pop",
        "missing_pop",
        "default_pop",
        "setdefault",
        "clear",
        "partial_update",
    ),
)
def test_mapping_interface_and_event_snapshots(reference, kind, operation):
    def scenario(modules):
        mapping = mapping_class(modules, kind)({"a": 1, "b": 2, "c": 3}, int)
        log = observe(mapping)
        before = (len(mapping), repr(mapping), tuple(mapping), stable(mapping.values()))
        calls = {
            "get": lambda: mapping["b"],
            "set": lambda: mapping.__setitem__("a", 4),
            "same": lambda: mapping.__setitem__("a", 1),
            "add": lambda: mapping.__setitem__("d", 4),
            "delete": lambda: mapping.__delitem__("a"),
            "missing_delete": lambda: mapping.__delitem__("absent"),
            "copy": mapping.copy,
            "newlike": lambda: mapping.__newlike__({"x": 5, "y": 6}),
            "pop": lambda: mapping.pop("a"),
            "missing_pop": lambda: mapping.pop("absent"),
            "default_pop": lambda: mapping.pop("absent", 9),
            "setdefault": lambda: mapping.setdefault("d", 8),
            "clear": mapping.clear,
            "partial_update": lambda: mapping.update([("d", 7), ("e", "bad")]),
        }
        result = caught(calls[operation])
        return before[:3], tuple(before[3]), result, stable(mapping), log

    compare(reference, scenario)


@pytest.mark.parametrize("kind", KINDS)
def test_live_views_iteration_and_shallow_copy(reference, kind):
    def scenario(modules):
        child = Token("shared")
        mapping = mapping_class(modules, kind)({"a": child, "b": 2})
        keys, values, items = mapping.keys(), mapping.values(), mapping.items()
        iterator = iter(mapping)
        first = next(iterator)
        copy = mapping.copy()
        mapping["c"] = 3
        iteration = caught(lambda: next(iterator))
        copy["new"] = 4
        return (
            first,
            iteration,
            tuple(keys),
            stable(tuple(values)),
            stable(tuple(items)),
            copy["a"] is child,
            copy._dict is mapping._dict,
            copy._basetypes is mapping._basetypes,
            stable(copy),
            stable(mapping),
        )

    compare(reference, scenario)


@pytest.mark.parametrize("kind", KINDS)
@pytest.mark.parametrize("base", ((), int, [int, str], (int, 42), None, "bad"))
def test_type_checks_and_replacement_hook(reference, kind, base):
    def scenario(modules):
        cls = mapping_class(modules, kind)
        mapping = cls(basetype=base)
        log = observe(mapping)
        results = [
            caught(lambda value=v: mapping._type_check(value)) for v in (1, "s", None)
        ]

        class Converted(cls):
            def _type_check(self, value):
                return value + 100

        converted = Converted()
        converted_log = observe(converted)
        converted["a"] = 1
        return results, log, stable(converted), converted_log

    compare(reference, scenario)


@pytest.mark.parametrize(
    "case",
    (
        "absent_none",
        "stored_none",
        "stored_none_to_int",
        "identity",
        "equal",
        "false",
        "comparison_error",
        "truth_error",
        "array",
    ),
)
def test_evented_identity_none_and_equality(reference, case):
    def scenario(modules):
        log = []

        class Equality(Token):
            def __eq__(self, other):
                log.append(("eq", self.tag, stable(other)))
                if case == "comparison_error":
                    raise LookupError("equality failed")
                if case == "truth_error":

                    class Truth:
                        def __bool__(self):
                            raise RuntimeError("truth failed")

                    return Truth()
                return case == "equal"

        old = None if "none" in case else Equality("old")
        value = (
            None
            if case in ("absent_none", "stored_none")
            else 2
            if case == "stored_none_to_int"
            else old
            if case == "identity"
            else np.array([1, 2])
            if case == "array"
            else Equality("new")
        )
        mapping = modules["_evented_dict"].EventedDict()
        if case != "absent_none":
            mapping._dict["a"] = old
        events = observe(mapping)
        result = caught(lambda: mapping.__setitem__("a", value))
        return result, stable(mapping), log, events, mapping._dict.get("a") is value

    compare(reference, scenario)


@pytest.mark.parametrize(
    "phase", ("adding", "added", "changing", "changed", "removing", "removed")
)
@pytest.mark.parametrize("effect", ("raise", "reenter", "swap_store"))
def test_mutation_reentry_error_and_store_replacement(reference, phase, effect):
    def scenario(modules):
        mapping = modules["_evented_dict"].EventedDict({"a": 1})
        log = observe(mapping)
        entered = False

        def callback(event):
            nonlocal entered
            if entered:
                return
            entered = True
            if effect == "raise":
                raise RuntimeError("mutation callback failed")
            if effect == "reenter":
                mapping["nested"] = 7
            else:
                mapping._dict = {"a": 30}

        emitter = getattr(mapping.events, phase)
        emitter.ignore_callback_errors = False
        emitter.connect(callback)

        def operation():
            if phase in ("adding", "added"):
                return mapping.__setitem__("b", 2)
            if phase in ("changing", "changed"):
                return mapping.__setitem__("a", 2)
            return mapping.__delitem__("a")

        return caught(operation), stable(mapping), log

    compare(reference, scenario)


class Child(Token):
    def __init__(self, tag):
        super().__init__(tag)
        self.events = EmitterGroup(changed=None)


@pytest.mark.parametrize("kind", ("evented", "namespace"))
@pytest.mark.parametrize("workflow", ("connect", "replace", "duplicate", "remove"))
def test_child_subscriptions_and_first_key(reference, kind, workflow):
    def scenario(modules):
        first, second = Child("first"), Child("second")
        mapping = mapping_class(modules, kind)({"a": first})
        initial = (first.events.source is None, len(first.events.changed.callbacks))
        log = observe(mapping)

        def extra(event):
            return None

        mapping.events.connect(extra)
        first.events.changed(value=1)
        if workflow == "replace":
            mapping["a"] = second
            first.events.changed(value=2)
            second.events.changed(value=3)
        elif workflow == "duplicate":
            mapping["b"] = first
            del mapping["a"]
            first.events.changed(value=2)
        elif workflow == "remove":
            del mapping["a"]
            first.events.changed(value=2)
        callbacks = (
            len(first.events.changed.callbacks),
            len(second.events.changed.callbacks),
        )
        mapping.events.disconnect(extra)
        remaining = len(first.events.changed.callbacks)
        mapping.events.disconnect()
        after = (
            len(first.events.changed.callbacks),
            len(second.events.changed.callbacks),
        )
        return initial, log, callbacks, remaining, after, first.events.source is first

    compare(reference, scenario)


class PsyChild(PsygnalModel):
    value: int = 0


class PsyParent(PsygnalModel):
    child: PsyChild


@pytest.mark.parametrize("nested", (False, True))
def test_real_psygnal_children(reference, nested):
    def scenario(modules):
        child = PsyChild()
        source = PsyParent(child=child) if nested else child
        mapping = modules["_evented_dict"].EventedDict()
        mapping._dict["a"] = source
        received = []
        mapping.events.connect(
            lambda event: received.append((event.key, stable(event.value)))
        )
        if nested:
            source.child.value = 3
        else:
            source.value = 3
        mapping.events.disconnect()
        if nested:
            source.child.value = 4
        else:
            source.value = 4
        return received

    compare(reference, scenario)


@pytest.mark.parametrize("path", ("", "nested.value", None))
def test_psygnal_reemission_payload_and_source(reference, path):
    def scenario(modules):
        source = Token("source")
        mapping = modules["_evented_dict"].EventedDict({"a": source})
        received = []
        mapping.events.connect(
            lambda event: received.append(
                (
                    event.type,
                    event.key,
                    event.value,
                    tuple(stable(v) for v in event.sources),
                )
            )
        )
        emission = types.SimpleNamespace(
            signal=types.SimpleNamespace(instance=source), path=path, args=(2, 1)
        )
        result = caught(lambda: mapping._reemit_child_event_psygnal(emission))
        return result, received

    compare(reference, scenario)


@pytest.mark.parametrize("name", ("a", "missing", "events", "_private", "copy"))
def test_namespace_lookup_and_exception_chaining(reference, name):
    def scenario(modules):
        cls = modules["_evented_dict"].EventedDictNamespace
        mapping = cls({"a": 1, "_private": 2, "copy": 3})
        return caught(lambda: cls.__getattr__(mapping, name))

    compare(reference, scenario)


def test_namespace_preserves_the_original_key_error(reference):
    def scenario(modules):
        failure = KeyError("original missing key")

        class Namespace(modules["_evented_dict"].EventedDictNamespace):
            def __getitem__(self, name):
                raise failure

        instance = Namespace()
        try:
            Namespace.__getattr__(instance, "missing")
        except AttributeError as error:
            return (
                error.args,
                error.__cause__ is failure,
                error.__context__ is failure,
                error.__suppress_context__,
            )
        raise AssertionError("missing namespace attribute did not raise")

    assert compare(reference, scenario) == (("missing",), True, True, True)


@pytest.mark.parametrize("name", ("public", "_private", "copy", "_dict", "_basetypes"))
def test_namespace_assignment_and_directory(reference, name):
    def scenario(modules):
        mapping = modules["_evented_dict"].EventedDictNamespace({"a": 1, "_hidden": 2})
        log = observe(mapping)
        value = {"z": 9} if name == "_dict" else (int,) if name == "_basetypes" else 3
        result = caught(lambda: setattr(mapping, name, value))
        directory = mapping.__dir__()
        state = object.__getattribute__(mapping, "__dict__")
        return result, stable(mapping), log, directory, name in state

    compare(reference, scenario)


@pytest.mark.parametrize("kind", KINDS)
def test_public_metadata(reference, kind):
    original = mapping_class(reference, kind)
    actual = mapping_class({"_dict": _dict, "_evented_dict": _evented_dict}, kind)
    assert actual.__name__ == original.__name__
    assert actual.__module__ == original.__module__
    assert actual.__doc__ == original.__doc__
    for name, method in vars(original).items():
        if inspect.isfunction(method):
            current = vars(actual)[name]
            assert str(inspect.signature(current)) == str(inspect.signature(method))
            assert current.__doc__ == method.__doc__
            assert current.__module__ == method.__module__


@pytest.mark.parametrize("kind", KINDS)
def test_self_cycles_and_callback_owners_collect(reference, kind):
    def scenario(modules):
        cls = mapping_class(modules, kind)
        mapping = cls()
        mapping._dict["cycle"] = mapping
        mapping_ref = weakref.ref(mapping)
        if kind != "typed":

            class Owner:
                def __init__(self, target):
                    self.target = target

                def callback(self, event):
                    return self.target

            owner = Owner(mapping)
            owner_ref = weakref.ref(owner)
            mapping.events.connect(owner.callback)
            del owner
        else:

            def owner_ref():
                return None

        del mapping
        gc.collect()
        return mapping_ref() is None, owner_ref() is None

    assert compare(reference, scenario) == (True, True)


@pytest.mark.parametrize("mode", ("accept", "reject", "truth_error", "iterator_error"))
def test_type_provider_reads_and_instance_hooks(reference, mode):
    def scenario(modules):
        trace = []

        class InstanceMeta(type):
            def __instancecheck__(cls, value):
                trace.append(("instance", value))
                return mode == "accept"

        class Accepted(metaclass=InstanceMeta):
            pass

        class Types:
            def __bool__(self):
                trace.append("truth")
                if mode == "truth_error":
                    raise RuntimeError("type truth failed")
                return True

            def __iter__(self):
                trace.append("iterate")
                if mode == "iterator_error":
                    raise LookupError("type iteration failed")
                return iter((Accepted,))

            def __repr__(self):
                trace.append("repr")
                return "ExpectedTypes"

        class Mapping(modules["_dict"].TypedMutableMapping):
            def __getattribute__(self, name):
                if name == "_basetypes":
                    trace.append("read-types")
                return super().__getattribute__(name)

        mapping = Mapping()
        mapping._basetypes = Types()
        trace.clear()
        return caught(lambda: mapping._type_check(1)), trace

    compare(reference, scenario)


@pytest.mark.parametrize("kind", KINDS)
def test_validation_before_live_store_lookup(reference, kind):
    def scenario(modules):
        trace = []

        class Mapping(mapping_class(modules, kind)):
            def _type_check(self, value):
                trace.append(("validate", value))
                object.__setattr__(self, "_dict", replacement)
                return value + 10

            def __getattribute__(self, name):
                if name == "_dict":
                    trace.append("read-store")
                return super().__getattribute__(name)

        mapping = Mapping()
        original = mapping._dict
        replacement = {}
        trace.clear()
        mapping["a"] = 1
        return trace[:], original, replacement

    compare(reference, scenario)


@pytest.mark.parametrize("kind", KINDS)
def test_copy_factory_precedes_live_basetype_lookup(reference, kind):
    def scenario(modules):
        trace, existing = [], []

        class Mapping(mapping_class(modules, kind)):
            def __init__(self):
                trace.append("initialize")
                super().__init__()
                if existing:
                    object.__setattr__(existing[0], "_basetypes", (str,))

        original = Mapping()
        original._dict["a"] = "valid"
        original._basetypes = (int,)
        existing.append(original)
        trace.clear()
        result = caught(original.copy)
        return trace, result, original._basetypes

    compare(reference, scenario)


@pytest.mark.parametrize("replace_type", (False, True))
def test_existing_emitter_getter_precedes_group_type_lookup(reference, replace_type):
    def scenario(modules):
        module = modules["_evented_dict"]
        trace = []
        group = EmitterGroup(earlier=None)
        reads = 0

        class Mapping(module.EventedDict):
            @property
            def events(self):
                nonlocal reads
                reads += 1
                trace.append(("events", reads))
                if reads == 2 and replace_type:
                    module.EmitterGroup = int
                return object.__getattribute__(self, "group")

            @events.setter
            def events(self, value):
                trace.append("assign-events")
                object.__setattr__(self, "group", value)

        original = module.EmitterGroup
        mapping = Mapping.__new__(Mapping)
        object.__setattr__(mapping, "group", group)
        try:
            result = caught(lambda: Mapping.__init__(mapping))
            return result, trace, tuple(group.emitters), hasattr(mapping, "_dict")
        finally:
            module.EmitterGroup = original

    compare(reference, scenario)


@pytest.mark.parametrize(
    "operation", ("first_callback_connect", "last_callback_disconnect")
)
@pytest.mark.parametrize("mutate", (False, True))
def test_child_hook_iteration_uses_live_dictionary(reference, operation, mutate):
    def scenario(modules):
        trace = []

        class Mapping(modules["_evented_dict"].EventedDict):
            def _connect_child_emitters(self, value):
                trace.append(("connect", value))
                if mutate:
                    self._dict["new"] = 3

            def _disconnect_child_emitters(self, value):
                trace.append(("disconnect", value))
                if mutate:
                    del self._dict["b"]

        mapping = Mapping({"a": 1, "b": 2})
        trace.clear()
        result = caught(lambda: getattr(mapping, operation)())
        return result, stable(mapping), trace

    compare(reference, scenario)


@pytest.mark.parametrize(
    "operation", ("_connect_child_emitters", "_disconnect_child_emitters")
)
@pytest.mark.parametrize("psygnal", (False, True))
def test_child_external_callable_captured_before_callback_getter(
    reference, operation, psygnal
):
    def scenario(modules):
        module = modules["_evented_dict"]
        trace = []

        class Meta(type):
            def __instancecheck__(cls, child):
                trace.append(cls.__name__)
                return cls.__name__ == "Supports" or psygnal

        class Psy(metaclass=Meta):
            pass

        class Supports(metaclass=Meta):
            pass

        class Group:
            source = None

            @property
            def all(self):
                trace.append("all")
                return self

            def connect(self, callback, **kwargs):
                trace.append(("connect-original", kwargs))

            def disconnect(self, callback):
                trace.append("disconnect-original")

        group = Group()

        class Target:
            @property
            def _reemit_child_event(self):
                trace.append("callback")
                group.connect = lambda *args, **kwargs: trace.append("connect-replaced")
                group.disconnect = lambda *args: trace.append("disconnect-replaced")
                return lambda event: None

            @property
            def _reemit_child_event_psygnal(self):
                trace.append("psygnal-callback")
                group.connect = lambda *args, **kwargs: trace.append("connect-replaced")
                group.disconnect = lambda *args: trace.append("disconnect-replaced")
                return lambda event: None

        child = types.SimpleNamespace(events=group)
        original = module.PsygnalModel, module.SupportsEvents
        module.PsygnalModel, module.SupportsEvents = Psy, Supports
        try:
            result = caught(
                lambda: getattr(module.EventedDict, operation)(Target(), child)
            )
            return result, trace, group.source is child
        finally:
            module.PsygnalModel, module.SupportsEvents = original

    compare(reference, scenario)


def test_child_key_callable_captured_before_source_getter(reference):
    def scenario(modules):
        trace = []
        root = types.SimpleNamespace()

        def key(source):
            trace.append(("original-key", source))
            return "a"

        root.key = key
        root.events = lambda event: trace.append(("emit", event.key))

        class Payload:
            @property
            def source(self):
                root.key = lambda source: "replaced"
                trace.append("source")
                return 1

        result = caught(
            lambda: modules["_evented_dict"].EventedDict._reemit_child_event(
                root, Payload()
            )
        )
        return result, trace

    compare(reference, scenario)


def test_psygnal_event_factory_and_path_read_order(reference):
    def scenario(modules):
        module = modules["_evented_dict"]
        trace = []

        class Key:
            def __format__(self, specification):
                trace.append("format-key")
                return "key"

        class Payload:
            signal = types.SimpleNamespace(instance=1)

            @property
            def path(self):
                trace.append("path")
                return "nested"

            @property
            def args(self):
                trace.append("args")
                module.Event = lambda **kwargs: trace.append("replaced-event")
                return (3,)

        def event_factory(**kwargs):
            trace.append(("event-original", kwargs))
            event = Event(**kwargs)
            return event

        root = types.SimpleNamespace(
            key=lambda source: Key(),
            events=lambda event: trace.append(
                ("emit", event.key, event.value, event.source)
            ),
        )
        original = module.Event
        module.Event = event_factory
        try:
            result = caught(
                lambda: module.EventedDict._reemit_child_event_psygnal(root, Payload())
            )
            return result, trace
        finally:
            module.Event = original

    compare(reference, scenario)


@pytest.mark.parametrize("key", ("public", "_private", 4))
def test_namespace_directory_captures_external_factories(reference, key):
    def scenario(modules):
        module = modules["_evented_dict"]
        trace = []

        def sort(values):
            trace.append("sort-original")
            return sorted(values)

        def make_set(values):
            trace.append("set-original")
            return set(values)

        class Namespace(module.EventedDictNamespace):
            def keys(self):
                trace.append("keys")
                module.sorted = lambda values: ["sort-replaced"]
                module.set = lambda values: {"set-replaced"}
                return iter((key,))

        mapping = Namespace()
        sentinel = object()
        previous = {
            name: vars(module).get(name, sentinel) for name in ("sorted", "set")
        }
        module.sorted, module.set = sort, make_set
        try:
            result = caught(mapping.__dir__)
            return result, trace
        finally:
            for name, value in previous.items():
                if value is sentinel:
                    delattr(module, name)
                else:
                    setattr(module, name, value)

    compare(reference, scenario)
