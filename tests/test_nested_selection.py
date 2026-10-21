"""Nested/selection decisions against independent, unmodified pinned sources."""

import gc
import importlib.util
import inspect
import sys
import types
import warnings
import weakref
from pathlib import Path

import pytest
from pydantic import BaseModel, TypeAdapter

from napari.utils.events.containers import (
    _nested_list,
    _selectable_list,
    _selection,
    _set,
)


@pytest.fixture(scope="module")
def reference():
    root = (
        Path(__file__).resolve().parents[1]
        / ".upstream/napari/src/napari/utils/events/containers"
    )
    loaded, originals = {}, {}
    try:
        for name in (
            "_typed",
            "_evented_list",
            "_set",
            "_selection",
            "_nested_list",
            "_selectable_list",
        ):
            production_name = "napari.utils.events.containers." + name
            identifier = "_original_nested_selection" + name
            spec = importlib.util.spec_from_file_location(
                identifier, root / (name + ".py")
            )
            module = importlib.util.module_from_spec(spec)
            sys.modules[identifier] = module
            spec.loader.exec_module(module)
            for value in vars(module).values():
                if isinstance(value, type) and value.__module__ == identifier:
                    value.__module__ = production_name
                    for method in vars(value).values():
                        if isinstance(method, (classmethod, staticmethod)):
                            method = method.__func__
                        if isinstance(method, types.FunctionType):
                            method.__module__ = production_name
                        elif isinstance(method, property):
                            for accessor in (method.fget, method.fset):
                                if accessor:
                                    accessor.__module__ = production_name
            loaded[name] = module
            originals[production_name] = sys.modules[production_name]
            sys.modules[production_name] = module
    finally:
        sys.modules.update(originals)
    yield loaded
    for name in loaded:
        sys.modules.pop("_original_nested_selection" + name, None)


actual = {
    "_nested_list": _nested_list,
    "_selection": _selection,
    "_selectable_list": _selectable_list,
    "_set": _set,
}


def caught(call):
    try:
        return "value", call()
    except Exception as error:
        cause = error.__cause__
        return (
            "error",
            type(error).__name__,
            str(error),
            (None if cause is None else (type(cause).__name__, str(cause))),
        )


def tree(value, ancestors=None):
    if hasattr(value, "_list"):
        ancestors = set() if ancestors is None else ancestors
        key = id(value)
        if key in ancestors:
            return "recursive", type(value).__name__
        ancestors.add(key)
        try:
            return [tree(child, ancestors) for child in value]
        finally:
            ancestors.remove(key)
    return value


def compare(reference, module, scenario):
    expected = scenario(reference[module])
    observed = scenario(actual[module])
    assert observed == expected
    return observed


def event_log(sequence):
    result = []

    def receive(event):
        data = []
        for name in ("index", "new_index", "old_value", "value", "added", "removed"):
            if hasattr(event, name):
                value = getattr(event, name)
                data.append((name, tree(value)))
        result.append((event.type, data))

    sequence.events.connect(receive)
    return result


@pytest.mark.parametrize(
    "index",
    [
        0,
        -1,
        True,
        (),
        (1,),
        (1, 2),
        (slice(None),),
        slice(1),
        None,
        "bad",
        (1, slice(None), 2),
        (1, "bad", 2),
    ],
)
def test_index_helpers(reference, index):
    compare(
        reference,
        "_nested_list",
        lambda m: (
            caught(lambda: m.ensure_tuple_index(index)),
            caught(lambda: m.split_nested_index(index)),
        ),
    )


@pytest.mark.parametrize(
    "key",
    [
        0,
        (),
        (0,),
        (0, 1),
        (0, 0, 0),
        (2, 0),
        (9,),
        slice(None),
        (0, slice(None)),
        (slice(None), 0),
        None,
    ],
)
def test_nested_read_delete_and_validity(reference, key):
    def scenario(m):
        sequence = m.NestableEventedList([[1, 2, 3], [4, 5], 6])
        log = event_log(sequence)
        return (
            caught(lambda: tree(sequence[key])),
            caught(lambda: sequence.has_index(key)),
            caught(lambda: sequence.__delitem__(key)),
            tree(sequence),
            log,
        )

    compare(reference, "_nested_list", scenario)


@pytest.mark.parametrize(
    "key,value",
    [
        (0, [7, 8]),
        ((0, 1), 9),
        ((1,), [7]),
        ((0, slice(None)), [8, 9]),
        (slice(1, 2), [7, 8]),
        ((2, 0), 9),
        ((slice(None), 0), 9),
    ],
)
def test_nested_assignment_subscription_and_events(reference, key, value):
    def scenario(m):
        sequence = m.NestableEventedList([[1, 2], [3, 4], 5])
        log = event_log(sequence)
        outcome = caught(lambda: sequence.__setitem__(key, value))
        return outcome, tree(sequence), log

    compare(reference, "_nested_list", scenario)


@pytest.mark.parametrize(
    "source", [0, (0, 0), (0, 2), (1, 1), -1, (), (9,), (0, slice(None))]
)
@pytest.mark.parametrize(
    "destination", [0, (0, 1), (1, 0), (1, -1), (), (slice(None),)]
)
def test_nested_single_move_partial_mutation(reference, source, destination):
    def scenario(m):
        sequence = m.NestableEventedList([[1, 2, 3], [4, 5], 6])
        log = event_log(sequence)
        outcome = caught(lambda: sequence.move(source, destination))
        return outcome, tree(sequence), log

    compare(reference, "_nested_list", scenario)


@pytest.mark.parametrize(
    "sources",
    [
        [],
        [0, 1],
        [(0, 0), (0, 2)],
        [(0, 1), (1, 0)],
        [(1, 1), (0, 0)],
        [(0, -1), (1, -1)],
        [()],
        [(0, slice(None))],
        [(slice(None), 1)],
        [(2**80,)],
    ],
)
@pytest.mark.parametrize("destination", [(0,), (0, 0), (1, -1), (), (slice(None),)])
def test_nested_move_plan_and_multiple(reference, sources, destination):
    def scenario(m):
        sequence = m.NestableEventedList([[1, 2, 3], [4, 5], 6])
        plan = caught(lambda: list(sequence._move_plan(iter(sources), destination)))
        log = event_log(sequence)
        outcome = caught(lambda: sequence.move_multiple(iter(sources), destination))
        return plan, outcome, tree(sequence), log

    compare(reference, "_nested_list", scenario)


@pytest.mark.parametrize(
    "start,stop,root",
    [
        (0, None, ()),
        (1, None, ()),
        (0, 1, (9,)),
        (-2, None, ()),
        (None, None, ()),
        (0, None, None),
    ],
)
def test_depth_first_index_generator(reference, start, stop, root):
    def scenario(m):
        generator = m.NestableEventedList([[1, 2], 3, [4]])._iter_indices(
            start, stop, root
        )
        assert isinstance(generator, types.GeneratorType)
        return caught(lambda: list(generator))

    compare(reference, "_nested_list", scenario)


@pytest.mark.parametrize("operation", ["send", "throw", "stop", "close"])
def test_recursive_generator_protocol_and_cleanup(reference, operation):
    def scenario(m):
        log = []

        class Child(m.NestableEventedList):
            def _iter_indices(self, *args, **kwargs):
                try:
                    sent = yield (7,)
                    log.append(("sent", sent))
                    yield (8,)
                except ValueError:
                    log.append(("caught",))
                    yield (9,)
                finally:
                    log.append(("closed",))

        sequence = m.NestableEventedList([Child([1]), 2])
        generator = sequence._iter_indices()
        prefix = next(generator), next(generator)

        def action():
            if operation == "send":
                return generator.send(3)
            if operation == "throw":
                return generator.throw(ValueError("sent"))
            if operation == "stop":
                return generator.throw(StopIteration("sent"))
            return generator.close()

        outcome = caught(action)
        rest = caught(lambda: list(generator))
        return prefix, outcome, rest, log

    compare(reference, "_nested_list", scenario)


@pytest.mark.parametrize(
    "class_name,operation",
    [
        (name, operation)
        for name in ("EventedSet", "Selection")
        for operation in (
            "add",
            "discard",
            "update",
            "clear",
            "difference_update",
            "intersection_update",
            "symmetric_difference_update",
            "toggle",
            "select_only",
            "_add_and_remove",
        )
        if name == "Selection"
        or operation not in {"toggle", "select_only", "_add_and_remove"}
    ],
)
@pytest.mark.parametrize("generator", [False, True])
def test_set_and_selection_mutations_events(
    reference, class_name, operation, generator
):
    module = "_set" if class_name == "EventedSet" else "_selection"

    def scenario(m):
        sequence = getattr(m, class_name)([1, 2, 3])
        log = event_log(sequence)
        other = iter([2, 4]) if generator else [2, 4]

        def action():
            if operation in {"add", "discard", "toggle", "select_only"}:
                return getattr(sequence, operation)(2)
            if operation == "clear":
                return sequence.clear()
            if operation == "_add_and_remove":
                return sequence._add_and_remove(other, iter([1]) if generator else [1])
            return getattr(sequence, operation)(other)

        return (
            caught(action),
            set(sequence),
            getattr(sequence, "active", None),
            getattr(sequence, "_current", None),
            log,
        )

    compare(reference, module, scenario)


@pytest.mark.parametrize(
    "operation",
    [
        "copy",
        "difference",
        "intersection",
        "symmetric_difference",
        "union",
        "issubset",
        "issuperset",
    ],
)
def test_set_results_and_constructor_hooks(reference, operation):
    def scenario(m):
        sequence = m.EventedSet([1, 2, 3])
        outcome = (
            getattr(sequence, operation)()
            if operation == "copy"
            else getattr(sequence, operation)([2, 4])
        )
        return (
            outcome
            if isinstance(outcome, bool)
            else (type(outcome).__name__, set(outcome))
        )

    compare(reference, "_set", scenario)


@pytest.mark.parametrize("value", [None, 0, 1, 4, [1]])
@pytest.mark.parametrize("attribute", ["active", "_current"])
def test_active_current_transitions_failures_and_events(reference, value, attribute):
    def scenario(m):
        sequence = m.Selection([1, 2])
        log = event_log(sequence)
        outcome = caught(lambda: setattr(sequence, attribute, value))
        return outcome, set(sequence), sequence.active, sequence._current, log

    compare(reference, "_selection", scenario)


@pytest.mark.parametrize(
    "class_name", ["SelectableEventedList", "SelectableNestableEventedList"]
)
@pytest.mark.parametrize(
    "operation",
    [
        "insert",
        "remove_selected",
        "select_all",
        "select_next",
        "select_previous",
        "move_selected",
        "selection",
        "invalid_selection",
    ],
)
def test_selectable_list_workflows(reference, class_name, operation):
    def scenario(m):
        sequence = getattr(m, class_name)(["a", "b", "c"])
        sequence.selection = ["b"]
        log, selection_log = event_log(sequence), event_log(sequence.selection)

        def action():
            if operation == "insert":
                return sequence.insert(1, "d")
            if operation == "selection":
                sequence.selection = iter(["a", "c"])
                return None
            if operation == "invalid_selection":
                sequence.selection = ["missing"]
                return None
            if operation == "select_next":
                return sequence.select_next(1, shift=True)
            if operation == "select_previous":
                return sequence.select_previous(shift=True)
            if operation == "move_selected":
                return sequence.move_selected(1, 0)
            return getattr(sequence, operation)()

        with warnings.catch_warnings(record=True) as warned:
            warnings.simplefilter("always")
            outcome = caught(action)
        return (
            outcome,
            tree(sequence),
            set(sequence.selection),
            sequence.selection.active,
            sequence.selection._current,
            log,
            selection_log,
            [(w.category.__name__, str(w.message)) for w in warned],
        )

    compare(reference, "_selectable_list", scenario)


@pytest.mark.parametrize("typed", [False, True])
@pytest.mark.parametrize(
    "data",
    [
        [1, 2],
        {"selection": [1], "_current": 2},
        {"selection": [], "_current": None},
        {"selection": ["bad"], "_current": 1},
        ["bad"],
    ],
)
def test_pydantic_validation_and_json(reference, typed, data):
    def scenario(m):
        selection_type = m.Selection[int] if typed else m.Selection

        class Model(BaseModel):
            selection: selection_type

        def action():
            model = Model(selection=data)
            adapter = TypeAdapter(selection_type)
            return (
                set(model.selection),
                model.selection._current,
                model.model_dump(mode="json"),
                adapter.dump_python(model.selection, mode="json"),
            )

        return caught(action)

    compare(reference, "_selection", scenario)


@pytest.mark.parametrize(
    "module_name", ["_set", "_selection", "_nested_list", "_selectable_list"]
)
def test_public_metadata(reference, module_name):
    module, original = actual[module_name], reference[module_name]
    assert module.__doc__ == original.__doc__
    for name, value in vars(original).items():
        if isinstance(value, type) and value.__module__ == module.__name__:
            converted = getattr(module, name)
            assert converted.__doc__ == value.__doc__
            for method_name, method in vars(value).items():
                other = vars(converted).get(method_name)
                if isinstance(method, property):
                    methods = [(method.fget, other.fget), (method.fset, other.fset)]
                else:
                    if isinstance(method, (classmethod, staticmethod)):
                        method, other = method.__func__, other.__func__
                    methods = [(method, other)]
                for before, after in methods:
                    if isinstance(before, types.FunctionType):
                        assert str(inspect.signature(after)) == str(
                            inspect.signature(before)
                        )
                        assert after.__doc__ == before.__doc__
                        assert inspect.isgeneratorfunction(
                            after
                        ) == inspect.isgeneratorfunction(before)


def test_generator_and_selection_cycles_collect(reference):
    def scenario(m):
        sequence = m.SelectableNestableEventedList(["a", "b"])
        generator = sequence._iter_indices()
        next(generator)
        sequence.generator = generator
        refs = (
            weakref.ref(sequence),
            weakref.ref(sequence.selection),
            weakref.ref(generator),
        )
        del generator, sequence
        gc.collect()
        return tuple(ref() is None for ref in refs)

    assert compare(reference, "_selectable_list", scenario) == (True, True, True)


@pytest.mark.parametrize(
    "kind",
    [
        "none",
        "zero",
        "integer",
        "iteration_error",
        "length_error",
        "truth_stop",
        "sequence",
    ],
)
def test_index_roots_preserve_provider_protocols(reference, kind):
    def scenario(m):
        log = []

        class Root:
            def __bool__(self):
                log.append("truth")
                if kind == "truth_stop":
                    raise StopIteration("truth failed")
                return True

            def __iter__(self):
                log.append("iter")
                if kind == "iteration_error":
                    raise TypeError("provider iteration failed")
                return iter([9])

            def __len__(self):
                log.append("len")
                if kind == "length_error":
                    raise ValueError("provider length failed")
                return 1

        class Sequence:
            def __getitem__(self, index):
                log.append(("getitem", index))
                if index:
                    raise IndexError
                return 9

        root = {"none": None, "zero": 0, "integer": 2, "sequence": Sequence()}.get(
            kind, Root()
        )
        generator = m.NestableEventedList([[1], 2])._iter_indices(root=root)
        outcome = caught(lambda: list(generator))
        return outcome, caught(lambda: next(generator)), log

    compare(reference, "_nested_list", scenario)


@pytest.mark.parametrize(
    "row", [6, 2**63 - 1, 2**63, 2**63 - 2, -(2**63), -(2**63) - 1]
)
@pytest.mark.parametrize("parent_kind", ["plain", "subclass", "list"])
def test_move_plan_public_split_changes_after_resume(
    reference, monkeypatch, row, parent_kind
):
    def scenario(m):
        log = []
        original = m.split_nested_index

        class Parent(tuple):
            pass

        def split(index):
            log.append(index)
            parent, result = original(index)
            if len(log) >= 3:
                parent = (
                    Parent(parent)
                    if parent_kind == "subclass"
                    else list(parent)
                    if parent_kind == "list"
                    else parent
                )
                result = row
            return parent, result

        with monkeypatch.context() as patch:
            patch.setattr(m, "split_nested_index", split)
            generator = m.NestableEventedList([0, 1, 2, 3])._move_plan([0, 1, 2], (0,))
            first = caught(lambda: next(generator))
            rest = caught(lambda: list(generator))
        return first, rest, log

    compare(reference, "_nested_list", scenario)


@pytest.mark.parametrize("large", [False, True])
def test_move_plan_integer_subclass_operators(reference, large):
    def scenario(m):
        log = []

        class Index(int):
            def __lt__(self, other):
                log.append(("lt", int(self), int(other)))
                return int(self) < other

            def __le__(self, other):
                log.append(("le", int(self), int(other)))
                return int(self) <= other

            def __add__(self, other):
                log.append(("add", int(self), int(other)))
                return Index(int(self) + other)

            def __sub__(self, other):
                log.append(("sub", int(self), int(other)))
                return Index(int(self) - other)

        offset = 2**80 if large else 0
        sequence = m.NestableEventedList([0, 1, 2, 3])
        result = caught(
            lambda: list(
                sequence._move_plan(
                    [Index(offset + 1), Index(offset + 2)], (Index(offset),)
                )
            )
        )
        return result, log

    compare(reference, "_nested_list", scenario)


@pytest.mark.parametrize("kind", ["iterator", "list_subclass"])
def test_move_plan_sort_provider_is_not_sized(reference, monkeypatch, kind):
    def scenario(m):
        log = []

        class Ordered(list):
            def __len__(self):
                log.append("unexpected length read")
                raise ValueError("sorting provider must not be sized")

            def __iter__(self):
                log.append("iterate")
                return super().__iter__()

        def order(sources, reverse):
            log.append(("sort", tuple(sources), reverse))
            return iter([2, 1, 0]) if kind == "iterator" else Ordered([2, 1, 0])

        with monkeypatch.context() as patch:
            patch.setattr(m, "sorted", order, raising=False)
            sequence = m.NestableEventedList([0, 1, 2, 3])
            result = caught(lambda: list(sequence._move_plan(iter([0, 1, 2]), (3,))))
        return result, log

    compare(reference, "_nested_list", scenario)


def test_move_plan_sort_provider_can_mutate_remaining_sources(reference, monkeypatch):
    def scenario(m):
        ordered = [2, 1, 0]
        original = m.split_nested_index
        calls = 0

        def split(index):
            nonlocal calls
            calls += 1
            if calls == 2:
                ordered[-1] = (2**63 - 1, 0)
            return original(index)

        with monkeypatch.context() as patch:
            patch.setattr(m, "sorted", lambda sources, reverse: ordered, raising=False)
            patch.setattr(m, "split_nested_index", split)
            sequence = m.NestableEventedList([0, 1, 2, 3])
            result = caught(lambda: list(sequence._move_plan([0, 1, 2], (0,))))
        return result, calls

    compare(reference, "_nested_list", scenario)


@pytest.mark.parametrize(
    "operation",
    ["send", "throw", "invalid_throw", "stop_throw", "close", "close_error", "reentry"],
)
def test_custom_iterator_delegation_completion_and_errors(reference, operation):
    def scenario(m):
        log = []
        holder = {}

        class Delegate:
            def __iter__(self):
                log.append("iter")
                return self

            def __next__(self):
                log.append("next")
                return (7,)

            def send(self, value):
                log.append(("send", value))
                raise StopIteration("finished")

            def throw(self, *args):
                log.append(
                    (
                        "throw",
                        tuple(
                            type(arg).__name__
                            if isinstance(arg, BaseException)
                            else arg.__name__
                            if isinstance(arg, type)
                            else arg
                            for arg in args
                        ),
                    )
                )
                if operation == "stop_throw":
                    raise StopIteration("finished")
                return (8,)

            def close(self):
                log.append("close")
                if operation == "close_error":
                    raise ValueError("close failed")
                if operation == "reentry":
                    log.append(caught(lambda: next(holder["generator"])))

        class Child(m.NestableEventedList):
            def _iter_indices(self, **kwargs):
                log.append(("child", kwargs))
                return Delegate()

        sequence = m.NestableEventedList([Child([1]), 2])
        generator = holder["generator"] = sequence._iter_indices()
        prefix = next(generator), next(generator)
        if operation == "send":
            result = caught(lambda: generator.send(3))
        elif operation == "throw":
            result = caught(lambda: generator.throw(ValueError, "sent"))
        elif operation == "invalid_throw":
            result = caught(lambda: generator.throw(17))
        elif operation == "stop_throw":
            result = caught(lambda: generator.throw(ValueError("sent")))
        else:
            result = caught(generator.close)
        following = caught(lambda: next(generator))
        generator.close()
        return prefix, result, following, log

    compare(reference, "_nested_list", scenario)


@pytest.mark.parametrize("class_name", ["EventedSet", "Selection"])
@pytest.mark.parametrize(
    "operation",
    [
        "add",
        "update",
        "discard",
        "clear",
        "difference_update",
        "intersection_update",
        "symmetric_difference_update",
        "copy",
    ],
)
def test_set_provider_attribute_order_and_preadd_hooks(
    reference, class_name, operation
):
    module_name = "_set" if class_name == "EventedSet" else "_selection"

    def scenario(m):
        log, recording = [], False

        class Watched(getattr(m, class_name)):
            def __getattribute__(self, name):
                if recording and name in {
                    "_set",
                    "_pre_add_hook",
                    "_emit_change",
                    "_update_active",
                    "difference_update",
                    "events",
                    "_active",
                    "_current_",
                }:
                    log.append(("get", name))
                return super().__getattribute__(name)

            def _pre_add_hook(self, value):
                if recording:
                    log.append(("preadd", value))
                return value * 10

        sequence = Watched([1, 2])
        recording = True

        def action():
            if operation == "add":
                return sequence.add(3)
            if operation == "discard":
                return sequence.discard(10)
            if operation == "clear":
                return sequence.clear()
            if operation == "copy":
                return set(sequence.copy())
            return getattr(sequence, operation)(iter([10, 3]))

        result = caught(action)
        recording = False
        return result, set(sequence), log

    compare(reference, module_name, scenario)


@pytest.mark.parametrize(
    "module_name,class_name", [("_set", "EventedSet"), ("_selection", "Selection")]
)
@pytest.mark.parametrize("typed", [False, True])
def test_schema_provider_method_capture_order(
    reference, monkeypatch, module_name, class_name, typed
):
    def scenario(m):
        log = []

        class Mutable:
            def __getitem__(self, value):
                log.append(("mutable", value.__name__))
                handler.changed = True
                return "mutable-item"

        class Handler:
            changed = False

            @property
            def generate_schema(self):
                before = self.changed
                log.append(("generate-read", before))

                def generate(value):
                    label = (
                        value.__name__
                        if isinstance(value, type)
                        else value
                        if isinstance(value, str)
                        else "Mutable"
                    )
                    log.append(("generate-call", before, label))
                    return {"type": "any"}

                return generate

        class Schema:
            def __getattr__(self, name):
                log.append(("schema-read", name))

                def build(*args, **kwargs):
                    log.append(("schema-call", name))
                    return {"type": "any"}

                return build

        handler = Handler()
        cls = getattr(m, class_name)
        with monkeypatch.context() as patch:
            patch.setattr(m, "MutableSet", Mutable())
            patch.setattr(m, "core_schema", Schema())
            result = cls.__get_pydantic_core_schema__(
                cls[int] if typed else cls, handler
            )
        return result, log

    compare(reference, module_name, scenario)


@pytest.mark.parametrize("step", [-1, 0, 1, 2])
@pytest.mark.parametrize("shift", [False, True])
@pytest.mark.parametrize("current", [None, 0, 1, 2])
def test_selectable_navigation_zero_and_range_edges(reference, step, shift, current):
    def scenario(m):
        sequence = m.SelectableEventedList([0, 1, 2])
        sequence.selection.clear()
        if current is not None:
            sequence.selection.add(current)
        log = event_log(sequence.selection)
        result = caught(lambda: sequence.select_next(step, shift))
        return (
            result,
            set(sequence.selection),
            sequence.selection.active,
            sequence.selection._current,
            log,
        )

    compare(reference, "_selectable_list", scenario)
