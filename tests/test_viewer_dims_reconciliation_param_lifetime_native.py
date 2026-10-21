"""Reassigned Viewer layers parameters release before later source callbacks."""

# Witness classes and callbacks execute synchronously within one family iteration.
# ruff: noqa: B023
from __future__ import annotations

import hashlib
import importlib.util
import sys
import weakref
from pathlib import Path
from types import SimpleNamespace

import pytest

from napari._app_model import context as context_module

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / ".artifacts/test_viewer_dims_reconciliation_next.py"
BASE_SHA = "2a828a581fa65b61c2ec31491934e86846c2c4faee61dfbb8e2386ce13cbc82f"


@pytest.fixture(scope="module")
def lifetime_families():
    """Reuse the frozen family oracle with exclusive declaration module names."""
    assert hashlib.sha256(BASE.read_bytes()).hexdigest() == BASE_SHA
    name = "_naparixx_viewer_dims_lifetime_base"
    assert name not in sys.modules
    spec = importlib.util.spec_from_file_location(name, BASE)
    assert spec is not None and spec.loader is not None
    base = importlib.util.module_from_spec(spec)
    sys.modules[name] = base
    try:
        spec.loader.exec_module(base)
    except BaseException:
        sys.modules.pop(name, None)
        raise
    original_builder = base._oracle_module

    def isolated_builder(name, *args, **kwargs):
        return original_builder(name + "_param_lifetime", *args, **kwargs)

    # Declaration namespace isolation only; the selected original ASTs and the
    # generator's hashes, setup, assertions, and cleanup execute unchanged.
    base._oracle_module = isolated_builder
    generator = base.families.__wrapped__()
    try:
        yield base, next(generator)
    finally:
        generator.close()
        sys.modules.pop(name, None)


def test_update_falsey_temporary_released_before_units_error(lifetime_families):
    _base, families = lifetime_families
    results = []
    for family in families:
        trace = []
        references = []

        class LaterError(RuntimeError):
            pass

        class TemporaryFalsey:
            def __init__(self):
                references.append(weakref.ref(self))

            def __bool__(self):
                trace.append(("input", "bool"))
                return False

            def __del__(self):
                trace.append(("input", "release"))

        class Extent:
            @property
            def units(self):
                trace.append(("extent", "units", references[0]() is None))
                raise LaterError("units after layers replacement")

        def forbidden_submit(**kwargs):
            raise AssertionError("submit must not run after the units error")

        viewer = SimpleNamespace(
            layers=SimpleNamespace(extent=Extent()),
            dims=SimpleNamespace(units=("old",)),
            cursor=SimpleNamespace(position=(4, 5)),
            _layer_slicer=SimpleNamespace(submit=forbidden_submit),
        )
        saved = None
        try:
            # Family.call would retain its args/kwargs. This direct source call
            # leaves the temporary parameter as the only owning input binding.
            family.viewer_methods["_update_layers"](
                viewer, layers=TemporaryFalsey()
            )
        except LaterError as error:
            saved = error
        assert saved is not None
        assert type(saved) is LaterError
        assert saved.args == ("units after layers replacement",)
        assert saved.__traceback__ is not None
        assert references[0]() is None
        assert trace == [
            ("input", "bool"),
            ("input", "release"),
            ("extent", "units", True),
        ]
        assert viewer.dims.units == ("old",)
        assert viewer.cursor.position == (4, 5)
        results.append((saved.args, list(trace)))
        saved.__traceback__ = None
        saved = None
    assert results[1] == results[0]


def test_extent_converted_temporary_released_before_foreign_error(
    lifetime_families, monkeypatch
):
    base, families = lifetime_families
    results = []
    for family in families:
        trace = []
        references = []
        converted = []
        context_targets = []
        context_callback_before = context_module.create_context
        foreign = base.ActualImage(base.np.zeros((2, 2)), name="foreign")
        constructor = family.namespace["LayerList"]
        contexts_method_before = constructor._create_contexts

        class TemporaryIterable:
            def __init__(self):
                references.append(weakref.ref(self))

            def __iter__(self):
                # The returned iterator retains the layer, not this argument.
                return iter((foreign,))

            def __del__(self):
                trace.append(("input", "release"))

        def layer_list(value):
            trace.append(("LayerList", "enter", references[0]() is value))
            result = constructor(value)
            assert isinstance(result, constructor)
            assert result is not value
            assert len(result) == 1 and result[0] is foreign
            converted.append(result)
            trace.append(("LayerList", "return", references[0]() is value))
            return result

        def no_frame_context_lookup(obj):
            # Isolate only the proven external frame-inspection effect.
            # Genuine LayerList construction and _create_contexts run;
            # the existing None-context branch publishes _ctx = None.
            context_targets.append(obj)
            return None

        class Owned:
            def __contains__(self, layer):
                assert layer is foreign
                trace.append(("owned", "contains", references[0]() is None))
                return False

        viewer = SimpleNamespace(layers=Owned())
        saved = None
        with monkeypatch.context() as patch:
            patch.setattr(
                context_module, "create_context", no_frame_context_lookup
            )
            patch.setitem(family.namespace, "LayerList", layer_list)
            try:
                family.viewer_methods["_sliced_extent_world_augmented"](
                    viewer, layers=TemporaryIterable()
                )
            except ValueError as error:
                saved = error
        assert context_module.create_context is context_callback_before
        assert constructor._create_contexts is contexts_method_before
        assert len(context_targets) == 1
        assert context_targets[0] is converted[0]
        assert converted[0]._ctx is None
        assert saved is not None
        assert type(saved) is ValueError
        assert saved.args == ('layer "foreign" is not part of this viewer.',)
        assert saved.__traceback__ is not None
        assert references[0]() is None
        assert trace == [
            ("LayerList", "enter", True),
            ("LayerList", "return", True),
            ("input", "release"),
            ("owned", "contains", True),
        ]
        assert len(converted) == 1
        assert converted[0][0] is foreign
        results.append((saved.args, list(trace)))
        saved.__traceback__ = None
        saved = None
        converted[0].clear()
    assert results[1] == results[0]
