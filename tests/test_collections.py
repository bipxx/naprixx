"""Collection control compared with independent, unmodified pinned sources."""

import gc
import importlib.util
import inspect
import sys
import types
import weakref
from pathlib import Path

import numpy as np
import pytest

from napari.utils.events import EmitterGroup
from napari.utils.events.containers import _evented_list, _typed


@pytest.fixture(scope="module")
def reference():
    root = (
        Path(__file__).resolve().parents[1]
        / ".upstream/napari/src/napari/utils/events/containers"
    )
    loaded = []

    def load(filename, production_name):
        name = "_original_collection_" + filename[:-3]
        spec = importlib.util.spec_from_file_location(name, root / filename)
        module = importlib.util.module_from_spec(spec)
        sys.modules[name] = module
        loaded.append(name)
        spec.loader.exec_module(module)
        # Event core-priority uses callback module names. Align that metadata
        # while keeping the reference classes and implementations independent.
        for value in vars(module).values():
            if isinstance(value, type) and value.__module__ == name:
                value.__module__ = production_name
                for method in vars(value).values():
                    if isinstance(method, types.FunctionType):
                        method.__module__ = production_name
        return module

    typed = load("_typed.py", _typed.__name__)
    previous = sys.modules[_typed.__name__]
    try:
        sys.modules[_typed.__name__] = typed
        evented = load("_evented_list.py", _evented_list.__name__)
    finally:
        sys.modules[_typed.__name__] = previous
    yield typed, evented
    for name in loaded:
        sys.modules.pop(name, None)


def result(call):
    try:
        value = call()
    except Exception as error:
        cause = error.__cause__
        return (
            "error",
            type(error).__name__,
            str(error),
            (None if cause is None else (type(cause).__name__, str(cause))),
        )
    if isinstance(value, np.ndarray):
        return "array", value.dtype.str, value.tolist()
    return "value", value


def compare(reference, kind, scenario):
    original = getattr(reference[kind], ("TypedMutableSequence", "EventedList")[kind])
    actual = (_typed.TypedMutableSequence, _evented_list.EventedList)[kind]
    expected = scenario(original)
    observed = scenario(actual)
    assert observed == expected
    return observed


def events(sequence):
    log = []

    def receive(event):
        fields = []
        for name in ("index", "new_index", "old_value", "value"):
            if hasattr(event, name):
                value = getattr(event, name)
                if value is sequence:
                    value = list(sequence)
                fields.append((name, value))
        log.append((event.type, fields))

    sequence.events.connect(receive)
    return log


@pytest.mark.parametrize("kind", [0, 1])
@pytest.mark.parametrize("basetype", [(), int, (int, float), [int, float], None, "bad"])
@pytest.mark.parametrize("data", [(), (1, 2), (1, "bad")])
def test_construction_partial_state(reference, kind, basetype, data):
    def scenario(cls):
        sequence = cls.__new__(cls)
        error = result(lambda: sequence.__init__(iter(data), basetype=basetype))
        return (
            error,
            getattr(sequence, "_list", None),
            repr(getattr(sequence, "_basetypes", None)),
        )

    compare(reference, kind, scenario)


@pytest.mark.parametrize("kind", [0, 1])
@pytest.mark.parametrize(
    "key,values",
    [
        (0, 9),
        (-1, 9),
        (3, 9),
        (slice(None), [3, 4]),
        (slice(1, 2), [7, 8, 9]),
        (slice(-3, -1), [7, 8]),
        (slice(None, None, -1), [3, 2, 1]),
        (slice(None, None, 2), [7, 8]),
        (slice(None, None, 2), [7]),
        (slice(None), [1, "bad"]),
        (slice(None), 8),
        (slice(None, None, 0), [9]),
        (slice(1, 1), []),
    ],
)
def test_assignment_events_validation_and_partial_state(reference, kind, key, values):
    def scenario(cls):
        sequence = cls([1, 2, 3], basetype=int)
        log = events(sequence) if kind else []
        error = result(
            lambda: sequence.__setitem__(
                key, iter(values) if isinstance(values, list) else values
            )
        )
        return error, list(sequence), log

    compare(reference, kind, scenario)


@pytest.mark.parametrize("kind", [0, 1])
@pytest.mark.parametrize(
    "key", [0, -1, 9, slice(None), slice(None, None, -1), "two", "missing", None]
)
def test_get_delete_contains_lookup(reference, kind, key):
    def scenario(cls):
        sequence = cls(["one", "two", "three"], lookup={str: str})
        log = events(sequence) if kind else []
        read = result(lambda: sequence[key])
        # Slicing constructs the original class, whose equality is list-like.
        if read[0] == "value" and hasattr(read[1], "_list"):
            read = "value", list(read[1])
        membership = result(lambda: key in sequence)
        deletion = result(lambda: sequence.__delitem__(key))
        return read, membership, deletion, list(sequence), log

    compare(reference, kind, scenario)


@pytest.mark.parametrize("kind", [0, 1])
@pytest.mark.parametrize(
    "start,stop",
    [(0, None), (-2, None), (0, -1), (None, None), (0, 12), (4, 9), (-20, -20)],
)
@pytest.mark.parametrize("value", [1, 3, 8])
def test_index_ranges_and_errors(reference, kind, start, stop, value):
    compare(
        reference,
        kind,
        lambda cls: result(lambda: cls([1, 2, 3]).index(value, start, stop)),
    )


@pytest.mark.parametrize("kind", [0, 1])
def test_copy_add_identity_and_lookup_independence(reference, kind):
    def scenario(cls):
        sequence = cls([1, 2], basetype=int, lookup={str: str})
        copied = sequence.copy()
        added = sequence + [3]
        prepended = [0] + sequence
        same = sequence.__iadd__([4])
        return (
            list(copied),
            list(added),
            prepended,
            list(sequence),
            same is sequence,
            copied._lookup == sequence._lookup,
            copied._lookup is sequence._lookup,
            copied._basetypes is sequence._basetypes,
            repr(sequence),
            hash(sequence) == id(sequence),
        )

    compare(reference, kind, scenario)


@pytest.mark.parametrize(
    "sources",
    [
        [],
        [0],
        [1, 3, 1],
        [4, 0, 2],
        [slice(None, None, -1)],
        [slice(1, 5, 2), 0],
        [-1, 0],
        [2**80, 0],
    ],
)
@pytest.mark.parametrize("destination", [-7, -1, 0, 1, 5, 6, 2**80])
def test_move_plan_and_multiple_mutations(reference, sources, destination):
    def scenario(cls):
        sequence = cls(range(5))
        plan = result(lambda: list(sequence._move_plan(iter(sources), destination)))
        log = events(sequence)
        moved = result(lambda: sequence.move_multiple(iter(sources), destination))
        return plan, moved, list(sequence), log

    compare(reference, 1, scenario)


@pytest.mark.parametrize("source", [-7, -1, 0, 1, 4, 5, 2**80])
@pytest.mark.parametrize("destination", [-7, -1, 0, 1, 5, 9])
def test_single_move_partial_failures_and_events(reference, source, destination):
    def scenario(cls):
        sequence = cls(range(5))
        log = events(sequence)
        moved = result(lambda: sequence.move(source, destination))
        return moved, list(sequence), log

    compare(reference, 1, scenario)


@pytest.mark.parametrize(
    "sources,destination",
    [
        ([None], 0),
        (["bad"], 0),
        ([slice(None, None, 0)], 0),
        ([1], slice(None)),
        (None, 0),
        ([1], None),
    ],
)
def test_move_generator_deferred_validation(reference, sources, destination):
    def scenario(cls):
        sequence = cls(range(5))
        generator = sequence._move_plan(sources, destination)
        assert isinstance(generator, types.GeneratorType)
        first = result(lambda: next(generator))
        return first, result(lambda: next(generator))

    compare(reference, 1, scenario)


@pytest.mark.parametrize("method", ["close", "throw", "stop", "send"])
@pytest.mark.parametrize("started", [False, True])
def test_move_generator_protocol(reference, method, started):
    def scenario(cls):
        generator = cls(range(5))._move_plan([4, 3, 2], 0)
        before = result(lambda: next(generator)) if started else None

        def action():
            if method == "throw":
                return generator.throw(ValueError("sent"))
            if method == "stop":
                return generator.throw(StopIteration("sent"))
            if method == "send":
                return generator.send(9)
            return generator.close()

        return before, result(action), result(lambda: next(generator))

    compare(reference, 1, scenario)


def test_int_subclass_operators_and_resume_order(reference):
    def scenario(cls):
        log = []

        class Index(int):
            def __hash__(self):
                log.append(("hash", int(self)))
                return int.__hash__(self)

            def __ne__(self, other):
                log.append(("ne", int(self), int(other)))
                return int.__ne__(self, other)

            def __le__(self, other):
                log.append(("le", int(self), int(other)))
                return int.__le__(self, other)

            def __isub__(self, other):
                log.append(("isub", int(self), int(other)))
                return Index(int(self) - other)

            def __iadd__(self, other):
                log.append(("iadd", int(self), int(other)))
                return Index(int(self) + other)

        generator = cls(range(5))._move_plan([Index(4), Index(2)], Index(0))
        first = next(generator)
        log.append(("yield", tuple(map(int, first))))
        second = generator.send(None)
        log.append(("yield", tuple(map(int, second))))
        done = result(lambda: next(generator))
        return log, done

    compare(reference, 1, scenario)


@pytest.mark.parametrize("kind", [0, 1])
def test_completion_snapshot_and_dynamic_converter(reference, kind):
    def scenario(cls):
        log = []

        class Sequence(cls):
            def __iter__(self):
                log.append("iter")
                return iter(self._list)

        sequence = Sequence([1, 2, 3], lookup={str: lambda value: f"a{value}"})
        generator = sequence._ipython_key_completions_()
        assert isinstance(generator, types.GeneratorType)
        captured = log.copy()
        first = next(generator)
        sequence._lookup[str] = lambda value: f"b{value}"
        sequence.append(4)
        return captured, first, list(generator), cls()._ipython_key_completions_()

    compare(reference, kind, scenario)


@pytest.mark.parametrize("kind", [0, 1])
@pytest.mark.parametrize("exception", [ValueError, StopIteration])
def test_completion_callback_errors_and_cleanup(reference, kind, exception):
    def scenario(cls):
        def convert(value):
            raise exception("callback")

        sequence = cls([1], lookup={str: convert})
        generator = sequence._ipython_key_completions_()
        return result(lambda: next(generator)), result(lambda: next(generator))

    compare(reference, kind, scenario)


def test_subclass_delete_check_hooks_and_child_events(reference):
    def scenario(cls):
        log = []

        class Sequence(cls):
            def _type_check(self, value):
                log.append(("check", value))
                return super()._type_check(value)

            def _process_delete_item(self, value):
                log.append(("deleted", value))

        sequence = Sequence([1, 2, 3], basetype=int)
        emitted = events(sequence)
        sequence[1] = 8
        del sequence[::2]
        sequence.reverse()

        class Child:
            def __init__(self):
                self.events = EmitterGroup(auto_connect=False, changed=None)

        child = Child()
        children = cls([child])
        child_log = []
        children.events.connect(
            lambda event: child_log.append((event.type, getattr(event, "index", None)))
        )
        child.events.changed()
        del children[0]
        child.events.changed()
        return log, emitted, list(sequence), child_log, child.events.source is child

    compare(reference, 1, scenario)


@pytest.mark.parametrize("kind", [0, 1])
def test_checked_values_are_retained_through_comprehension(reference, kind):
    def scenario(cls):
        log = []

        class Checked:
            def __init__(self, value):
                self.value = value

            def __del__(self):
                log.append(("release", self.value))

        class Sequence(cls):
            def _type_check(self, value):
                log.append(("check", value))
                return Checked(value)

        sequence = Sequence()
        sequence[:] = [1, 2]
        # Both implementations discard/retain different values by design:
        # this compares the actual reference sequence of hook/destructor effects.
        del sequence
        gc.collect()
        return log

    compare(reference, kind, scenario)


@pytest.mark.parametrize("kind", [0, 1])
def test_public_signatures_docstrings_and_generator_metadata(reference, kind):
    actual = (_typed.TypedMutableSequence, _evented_list.EventedList)[kind]
    original = getattr(reference[kind], ("TypedMutableSequence", "EventedList")[kind])
    for name, method in vars(original).items():
        if isinstance(method, types.FunctionType):
            forwarded = getattr(actual, name)
            assert str(inspect.signature(forwarded)) == str(inspect.signature(method))
            assert forwarded.__doc__ == method.__doc__
            assert inspect.isgeneratorfunction(
                forwarded
            ) == inspect.isgeneratorfunction(method)
    assert actual.__doc__ == original.__doc__
    assert _evented_list.__doc__ == reference[1].__doc__


@pytest.mark.parametrize("kind", [0, 1])
def test_completion_generator_cycles_are_visible_to_gc(reference, kind):
    def scenario(cls):
        sequence = cls([1, 2], lookup={str: str})
        generator = sequence._ipython_key_completions_()
        next(generator)
        sequence.generator = generator
        sequence_ref, generator_ref = weakref.ref(sequence), weakref.ref(generator)
        del generator, sequence
        gc.collect()
        return sequence_ref() is None, generator_ref() is None

    assert compare(reference, kind, scenario) == (True, True)


@pytest.mark.parametrize("kind", [0, 1])
@pytest.mark.parametrize(
    "operation",
    ["insert", "set", "slice", "get", "index", "delete", "copy", "contains"],
)
def test_provider_attribute_and_callback_order(reference, kind, operation):
    def scenario(cls):
        log = []

        class Sequence(cls):
            def __getattribute__(self, name):
                if name in {
                    "_list",
                    "_lookup",
                    "_basetypes",
                    "_type_check",
                    "index",
                    "_iter_indices",
                    "__getitem__",
                    "__setitem__",
                    "__newlike__",
                    "extend",
                    "insert",
                    "events",
                    "_delitem_indices",
                    "_disconnect_child_emitters",
                    "_process_delete_item",
                }:
                    log.append(("read", name))
                return super().__getattribute__(name)

            def _type_check(self, value):
                log.append(("check", value))
                return super()._type_check(value)

        sequence = Sequence([1, 2, 3], basetype=int, lookup={str: str})
        log.clear()
        actions = {
            "insert": lambda: sequence.insert(1, 7),
            "set": lambda: sequence.__setitem__(1, 7),
            "slice": lambda: sequence.__setitem__(slice(1, 2), [7, 8]),
            "get": lambda: sequence["2"],
            "index": lambda: sequence.index("2", -2, -1),
            "delete": lambda: sequence.__delitem__(slice(0, 3, 2)),
            "copy": sequence.copy,
            "contains": lambda: "2" in sequence,
        }
        outcome = result(actions[operation])
        if outcome[0] == "value" and hasattr(outcome[1], "_list"):
            outcome = "value", list(outcome[1])
        return outcome, log

    compare(reference, kind, scenario)


@pytest.mark.parametrize("suppress", [False, True])
def test_move_multiple_context_capture_exception_and_suppression(reference, suppress):
    def scenario(cls):
        log = []
        failure = ValueError("move failed")

        class Method:
            def __init__(self, name):
                self.name = name

            def __get__(self, instance, owner):
                log.append(("bind", self.name))
                if self.name == "enter":
                    return lambda: log.append(("enter",))

                def exit(type_, value, traceback):
                    log.append(
                        (
                            "exit",
                            type_.__name__,
                            value is failure,
                            sys.exception() is failure,
                            traceback is not None,
                        )
                    )
                    return suppress

                return exit

        class Context:
            __enter__ = Method("enter")
            __exit__ = Method("exit")

        class Reordered:
            def blocker(self):
                log.append(("blocker",))
                return Context()

            def __call__(self, **kwargs):
                log.append(("reordered",))

        class Sequence(cls):
            def move(self, source, destination=0):
                log.append(("move", source, destination))
                raise failure

        sequence = Sequence(range(5))
        sequence.events = types.SimpleNamespace(reordered=Reordered())
        outcome = result(lambda: sequence.move_multiple([4, 3], 0))
        return outcome, log

    compare(reference, 1, scenario)


def test_reentry_and_provider_stop_iteration(reference):
    def scenario(cls):
        calls = []

        class Sources:
            def __iter__(self):
                calls.append(result(lambda: next(generator)))
                return iter([4, 3])

        generator = cls(range(5))._move_plan(Sources(), 0)
        plan = list(generator)

        class BadSources:
            def __iter__(self):
                raise StopIteration("provider")

        failure = result(lambda: next(cls(range(5))._move_plan(BadSources(), 0)))
        return calls, plan, failure

    compare(reference, 1, scenario)


@pytest.mark.parametrize("kind", [0, 1])
def test_equality_preserves_non_boolean_dependency_results(reference, kind):
    def scenario(cls):
        sequence = cls()
        sequence._list = np.array([1, 2, 3])
        return result(lambda: sequence.__eq__(2))

    compare(reference, kind, scenario)
