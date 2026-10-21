"""Public annotation/generator contracts against the unmodified pinned module."""

import gc
import importlib.util
import subprocess
import sys
import types
import weakref
from pathlib import Path
from typing import Annotated, Any, Dict, Iterable, List, Union

import pytest

from napari import _pydantic_util as actual
from naparixx import _native


@pytest.fixture(scope="module")
def reference():
    path = (
        Path(__file__).resolve().parents[1]
        / ".upstream/napari/src/napari/_pydantic_util.py"
    )
    spec = importlib.util.spec_from_file_location("_original_annotation_helpers", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def outcome(function, annotation):
    try:
        value = function(annotation)
        if function.__name__ == "iter_inner_types":
            value = list(value)
        return "result", value
    except Exception as error:
        return "error", type(error), str(error)


@pytest.mark.parametrize(
    "annotation",
    [
        int,
        None,
        types.NoneType,
        Any,
        list,
        dict,
        List,
        Dict,
        list[list[int]],
        dict[str, list[int | None]],
        Annotated[list[int | str], "metadata"],
        Iterable[int],
        tuple[int, ...],
        Union[list[int], dict[str, float], None],
        int | str | None,
    ],
)
@pytest.mark.parametrize(
    "name", ["iter_inner_types", "get_inner_type", "get_outer_type", "is_list_type"]
)
def test_annotation_classification_matches_pinned_source(reference, annotation, name):
    assert outcome(getattr(actual, name), annotation) == outcome(
        getattr(reference, name), annotation
    )


@pytest.mark.parametrize(
    "annotation", [list[int], dict[str, int], Annotated[int, "meta"], int | str | None]
)
def test_cached_public_iterator_reenters_recursive_override(
    reference, monkeypatch, annotation
):
    observations = []

    def scenario(module):
        saved = module.iter_inner_types
        calls = []

        def replacement(child):
            calls.append(child)
            return iter((float,))

        with monkeypatch.context() as patch:
            patch.setattr(module, "iter_inner_types", replacement)
            observations.append((list(saved(annotation)), calls))

    scenario(reference)
    scenario(actual)
    assert observations[0] == observations[1]
    assert observations[0][1]


def test_metadata_alias_hooks_and_iteration_laziness(reference, monkeypatch):
    observations = []
    annotation = object()

    def scenario(module):
        calls = []

        def origin(value):
            calls.append(("origin", value))
            return list if value is annotation else None

        def args(value):
            calls.append(("args", value))
            return (int,) if value is annotation else ()

        with monkeypatch.context() as patch:
            patch.setattr(module, "get_origin", origin)
            patch.setattr(module, "get_args", args)
            generator = module.iter_inner_types(annotation)
            assert isinstance(generator, types.GeneratorType)
            assert not calls
            observations.append((list(generator), calls))

    scenario(reference)
    scenario(actual)
    assert observations[0] == observations[1]


def test_helper_calls_preserve_public_overrides(reference, monkeypatch):
    def scenario(module):
        calls = []

        def inner(value):
            calls.append(("inner", value))
            return iter((float, str))

        def outer(value):
            calls.append(("outer", value))
            return list

        saved_outer = module.get_outer_type
        with monkeypatch.context() as patch:
            patch.setattr(module, "iter_inner_types", inner)
            assert module.get_inner_type(int) == Union[float, str]
            patch.setattr(module, "get_outer_type", outer)
            assert module.is_list_type(int)
            assert saved_outer(int | None) is list
        assert calls == [("inner", int), ("outer", int), ("outer", int)]

    scenario(reference)
    scenario(actual)


def test_delegated_generator_send_throw_and_close(reference, monkeypatch):
    observations = []
    problem = ValueError("delegated")

    def scenario(module):
        saved = module.iter_inner_types
        trace = []

        def child(annotation):
            trace.append(("enter", annotation))
            try:
                sent = yield annotation
                trace.append(("send", sent))
                yield str
            except ValueError as error:
                trace.append(("throw", error is problem, sys.exception() is error))
                yield float
            finally:
                trace.append(("finally", type(sys.exception()).__name__))

        with monkeypatch.context() as patch:
            patch.setattr(module, "iter_inner_types", child)
            generator = saved(list[int] | set[str])
            yielded = [
                next(generator),
                generator.send("sent"),
                generator.throw(problem),
            ]
            yielded.append(next(generator))
            generator.close()
            observations.append((yielded, trace))

    scenario(reference)
    scenario(actual)
    assert observations[0] == observations[1]


def test_custom_throw_return_continues_union_and_restores_exception(
    reference, monkeypatch
):
    observations = []
    problem = ValueError("handled")

    def scenario(module):
        saved = module.iter_inner_types
        trace = []

        class Child:
            def __init__(self, annotation):
                self.annotation = annotation
                trace.append(("create", annotation, sys.exception()))

            def __iter__(self):
                return self

            def __next__(self):
                return self.annotation

            def throw(self, *args):
                error = args[0]
                trace.append(
                    ("throw", len(args), error is problem, sys.exception() is error)
                )
                raise StopIteration("ignored return")

            def close(self):
                trace.append(("close", type(sys.exception()).__name__))

        with monkeypatch.context() as patch:
            patch.setattr(module, "iter_inner_types", Child)
            generator = saved(int | str)
            yielded = [next(generator), generator.throw(problem)]
            generator.close()
            observations.append((yielded, trace))

    scenario(reference)
    scenario(actual)
    assert observations[0] == observations[1]


@pytest.mark.parametrize("signature", ["instance", "class", "raw", "traceback"])
def test_delegate_receives_original_throw_arguments_before_normalization(
    reference, monkeypatch, signature
):
    observations = []
    constructor_calls = []

    class Problem(Exception):
        def __init__(self, *args):
            constructor_calls.append(args)
            super().__init__(*args)

    supplied = ValueError("original")
    arguments = {
        "instance": (supplied,),
        "class": (Problem, "value"),
        "raw": ("provider token",),
        "traceback": (Problem, "value", 17),
    }[signature]

    def scenario(module):
        saved = module.iter_inner_types
        calls = []
        constructor_calls.clear()

        class Child:
            def __iter__(self):
                return self

            def __next__(self):
                return int

            def throw(self, *args):
                calls.append((args, sys.exception()))
                return str

        with monkeypatch.context() as patch:
            patch.setattr(module, "iter_inner_types", lambda annotation: Child())
            generator = saved(list[int])
            assert next(generator) is int
            try:
                result = generator.throw(*arguments)
            except Exception as error:
                result = type(error), str(error)
            generator.close()
            observations.append((result, calls, list(constructor_calls)))

    scenario(reference)
    scenario(actual)
    assert observations[0] == observations[1]


def test_delegate_close_failure_preserves_caller_exception(reference, monkeypatch):
    observations = []

    def scenario(module):
        saved = module.iter_inner_types
        trace = []
        caller = ValueError("caller")
        failure = RuntimeError("close")

        class Child:
            def __iter__(self):
                return self

            def __next__(self):
                return int

            def close(self):
                trace.append(("close", sys.exception() is caller))
                raise failure

        with monkeypatch.context() as patch:
            patch.setattr(module, "iter_inner_types", lambda annotation: Child())
            generator = saved(list[int])
            assert next(generator) is int
            try:
                raise caller
            except ValueError:
                with pytest.raises(RuntimeError) as caught:
                    generator.close()
                trace.append((caught.value is failure, failure.__context__ is caller))
                assert sys.exception() is caller
            with pytest.raises(StopIteration):
                next(generator)
            observations.append(trace)

    scenario(reference)
    scenario(actual)
    assert observations[0] == observations[1]


@pytest.mark.parametrize("operation", ["send", "throw"])
def test_delegate_missing_protocol_closes_parent(reference, monkeypatch, operation):
    observations = []
    problem = ValueError("missing protocol")

    def scenario(module):
        saved = module.iter_inner_types
        with monkeypatch.context() as patch:
            patch.setattr(
                module, "iter_inner_types", lambda annotation: iter((int, str))
            )
            generator = saved(list[int])
            assert next(generator) is int
            try:
                if operation == "send":
                    generator.send("value")
                else:
                    generator.throw(problem)
            except Exception as error:
                observations.append((type(error), str(error), error is problem))
            else:
                pytest.fail("The missing delegated protocol should raise")
            with pytest.raises(StopIteration):
                next(generator)

    scenario(reference)
    scenario(actual)
    assert observations[0] == observations[1]


@pytest.mark.parametrize("callback", ["get_origin", "get_args"])
def test_body_stop_iteration_is_wrapped_with_original_cause(
    reference, monkeypatch, callback
):
    def scenario(module):
        problem = StopIteration("provider failure")

        def fail(annotation):
            raise problem

        with monkeypatch.context() as patch:
            patch.setattr(module, callback, fail)
            generator = module.iter_inner_types(int)
            with pytest.raises(
                RuntimeError, match="generator raised StopIteration"
            ) as caught:
                next(generator)
            assert caught.value.__cause__ is caught.value.__context__ is problem
            with pytest.raises(StopIteration):
                next(generator)

    scenario(reference)
    scenario(actual)


def test_generator_start_close_and_invalid_throw_protocol(reference):
    def scenario(module):
        generator = module.iter_inner_types(int)
        with pytest.raises(TypeError, match="just-started"):
            generator.send("value")
        assert next(generator) is int
        for args in ((), (1,), (ValueError("x"), "separate"), (ValueError, None, 3)):
            with pytest.raises(TypeError):
                generator.throw(*args)
        generator.close()
        with pytest.raises(StopIteration):
            generator.send("value")
        problem = StopIteration("closed")
        with pytest.raises(StopIteration) as caught:
            generator.throw(problem)
        assert caught.value is problem

    scenario(reference)
    scenario(actual)


def test_native_iterator_reentry_and_python_cycles_are_visible_to_gc(monkeypatch):
    iterator = _native.iter_inner_types(int)
    iterator_ref = weakref.ref(iterator)

    def origin(annotation):
        with pytest.raises(ValueError, match="generator already executing"):
            next(iterator_ref())
        return None

    with monkeypatch.context() as patch:
        patch.setattr(actual, "get_origin", origin)
        assert next(iterator) is int
    iterator.frame["cycle"] = iterator
    reference = weakref.ref(iterator)
    del iterator
    gc.collect()
    assert reference() is None


def test_nested_provider_cycle_and_finally_are_collected(monkeypatch):
    calls = []
    saved = actual.iter_inner_types

    class Child:
        def __iter__(self):
            return self

        def __next__(self):
            return int

        def close(self):
            calls.append("closed")

    child = Child()
    child_ref = weakref.ref(child)
    with monkeypatch.context() as patch:
        patch.setattr(actual, "iter_inner_types", lambda annotation: child_ref())
        generator = saved(list[int])
        assert next(generator) is int
    native_iterator = generator.gi_yieldfrom
    child.owner = generator
    references = [weakref.ref(value) for value in (child, generator, native_iterator)]
    del child, generator, native_iterator
    gc.collect()
    assert all(reference() is None for reference in references)
    assert calls == ["closed"]


def test_recursive_provider_annotations_respect_interpreter_limit(reference):
    # Keep the recursion limit and deep generator finalization out of pytest's
    # own interpreter. Both modules must fail as generators and release owners.
    program = """
import gc, importlib.util, sys, weakref
from napari import _pydantic_util as actual
spec = importlib.util.spec_from_file_location('_recursive_reference', sys.argv[1])
reference = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reference)
class Node:
    def __init__(self, child): self.child = child
annotation = int
for index in range(1500): annotation = Node(annotation)
sys.setrecursionlimit(400)
for module in (reference, actual):
    module.get_origin = lambda value: list if isinstance(value, Node) else None
    module.get_args = lambda value: (value.child,) if isinstance(value, Node) else ()
    generator = module.iter_inner_types(annotation)
    owner = weakref.ref(generator)
    try:
        next(generator)
    except RecursionError:
        pass
    else:
        raise AssertionError('Recursive annotations bypassed the interpreter limit')
    del generator
    gc.collect()
    assert owner() is None
"""
    result = subprocess.run(
        [sys.executable, "-c", program, reference.__file__],
        capture_output=True,
        text=True,
        timeout=30,
        check=False,
    )
    assert result.returncode == 0, result.stdout + result.stderr
    assert not result.stderr, result.stderr
