"""Only the two source contracts identified by the coherent shape-state review.

The existing 25-case source/candidate/actual Family oracle stays byte exact.
Root owns all application imports, collection, baseline and installed execution.
"""

# Every callback is consumed before the next source family is selected.
# ruff: noqa: B023
from __future__ import annotations

import gc
import hashlib
import importlib.util
import inspect
import sys
import types
import weakref
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / ".artifacts/test_viewer_shape_state_next.py"
assert hashlib.sha256(FIXTURE.read_bytes()).hexdigest() == (
    "efa3f8b802a2e7956659c874ba29305e72937517fc6a84bd5d0ab08193bbe36a"
)
SPEC = importlib.util.spec_from_file_location(
    "_naparixx_shape_state_delta_fixture", FIXTURE
)
assert SPEC is not None and SPEC.loader is not None
ORACLE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = ORACLE
SPEC.loader.exec_module(ORACLE)
families = ORACLE.families


def _literal_tuples(value):
    if isinstance(value, types.CodeType):
        for constant in value.co_consts:
            yield from _literal_tuples(constant)
    elif isinstance(value, tuple):
        yield value
        for constant in value:
            yield from _literal_tuples(constant)


@pytest.mark.parametrize("site", ["mesh_clear", "remove_all", "shape_init"])
def test_mesh_and_shape_state_reuse_original_constant_tuple_arguments(
    families, monkeypatch, site
):
    for family in families:
        kind = "Mesh" if site == "mesh_clear" else "ShapeList"
        name = {"mesh_clear": "clear", "remove_all": "remove_all"}.get(
            site, "__init__"
        )
        owner = family.cls(kind)()
        function = inspect.unwrap(ORACLE._function(family.cls(kind), name))
        expected_keys = {(0, 3), (0, 4)} if site == "mesh_clear" else {(0, 4)}
        constants = {
            value: value
            for value in _literal_tuples(function.__code__)
            if value in expected_keys
        }
        assert set(constants) == expected_keys
        observed = {key: [] for key in expected_keys}

        def empty(shape, *args, **kwargs):
            if isinstance(shape, tuple) and shape in observed:
                observed[shape].append(shape)
            return ORACLE.np.empty(shape, *args, **kwargs)

        with ORACLE._namespace_patch(
            monkeypatch,
            family.namespace(kind),
            "np",
            ORACLE.NumpyProxy(empty=empty),
        ):
            getattr(owner, name)()
            getattr(owner, name)()
        assert {key: len(values) for key, values in observed.items()} == dict.fromkeys(
            expected_keys, 4
        )
        assert all(
            value is constants[key]
            for key, values in observed.items()
            for value in values
        )


@pytest.mark.parametrize("consumer", ["sum", "all"])
def test_saved_genexpr_error_traceback_keeps_iterator_and_current_until_release(
    families, monkeypatch, consumer
):
    for family in families:
        references, drops, generators, trace = {}, [], [], []
        marker = RuntimeError("source genexpr callback")

        class BadOperand:
            def __rsub__(self, other):
                trace.append("callback")
                raise marker

        class Current:
            stop = 1

            def __init__(self):
                self.start = BadOperand()

            def __del__(self):
                drops.append("current")

        class Cursor:
            def __iter__(self):
                return self

            def __next__(self):
                value = Current()
                references["current"] = weakref.ref(value)
                return value

            def __del__(self):
                drops.append("iterator")

        class Source:
            def __len__(self):
                return 1

            def __iter__(self):
                iterator = Cursor()
                references["iterator"] = weakref.ref(iterator)
                return iterator

        def consume(generator):
            generators.append(generator)
            return next(generator)

        def failing_issubclass(subclass, base):
            trace.append("callback")
            raise marker

        owner = family.ShapeList()
        source = Source()
        with monkeypatch.context() as patch:
            patch.setitem(family.shape_namespace, consumer, consume)
            if consumer == "all":
                patch.setitem(
                    family.shape_namespace, "issubclass", failing_issubclass
                )
            with pytest.raises(RuntimeError) as caught:
                if consumer == "sum":
                    family.ShapeList._slicess_to_array(source)
                else:
                    owner._add_multiple_shapes(source)
        assert caught.value is marker
        assert trace == ["callback"]
        assert set(references) == {"iterator", "current"}
        assert all(reference() is not None for reference in references.values())
        closed = generators.pop()
        assert isinstance(closed, types.GeneratorType)
        closed.close()
        assert all(reference() is not None for reference in references.values())
        replacement = LookupError("already closed")
        with pytest.raises(LookupError) as thrown:
            closed.throw(replacement)
        assert thrown.value is replacement
        del thrown, replacement
        gc.collect()
        assert drops == []
        assert all(reference() is not None for reference in references.values())
        # Drop both pytest's separately held traceback and the exception's
        # actual traceback, together with the intentionally captured generator.
        del caught, closed
        generators.clear()
        marker.__traceback__ = None
        gc.collect()
        assert all(reference() is None for reference in references.values())
        assert sorted(drops) == ["current", "iterator"]
