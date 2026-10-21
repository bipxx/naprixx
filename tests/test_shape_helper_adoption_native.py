"""Installed-only native adoption and the corrected publication lifetime ABI."""

import gc
import weakref

import pytest

from napari.layers.shapes import _accelerated_triangulate_dispatch as dispatch
from napari.layers.shapes import _shapes_utils as geometry
from napari.utils import misc
from naparixx import _native


def test_corrected_publication_owner_has_weakrefs_and_collects_real_cycle():
    class Payload:
        pass

    payload = Payload()
    owner = _native.shape_publication_locals(("values",), {"values": payload})
    assert type(owner) is _native.ShapePublicationLocals
    assert owner.values is payload
    assert gc.is_tracked(owner)
    with pytest.raises(AttributeError):
        owner.values = None
    with pytest.raises(TypeError):
        _native.ShapePublicationLocals()
    payload.owner = owner
    owner_ref, payload_ref = weakref.ref(owner), weakref.ref(payload)
    del owner, payload
    gc.collect()
    assert owner_ref() is None
    assert payload_ref() is None


def test_publication_binding_uses_actual_executing_module_dictionary():
    assert _native.shape_publication_globals() is globals()


@pytest.mark.parametrize("function, operation, field", [(misc.argsort, "shape_publication_argsort", "values"), (dispatch._set_numba, "shape_publication_set_numba", "value"), (geometry._save_failed_triangulation, "shape_geometry_save_failed_triangulation", "data")])
def test_real_facades_dispatch_to_actual_native_exports(monkeypatch, function, operation, field):
    assert operation in function.__code__.co_names
    native = getattr(_native, operation)
    assert callable(native)
    assert native.__module__ == "naparixx._native"
    assert not hasattr(native, "__code__")
    argument = [2, 1] if field == "values" else False if field == "value" else object()
    result = object()
    calls = []

    def observe(namespace, literals, owner):
        calls.append((namespace, literals, owner))
        return result

    monkeypatch.setattr(_native, operation, observe)
    assert function(argument) is result
    assert len(calls) == 1
    namespace, literals, owner = calls[0]
    assert namespace is function.__globals__
    assert isinstance(literals, tuple)
    assert getattr(owner, field) is argument
    assert type(owner) is (_native.ShapeGeometryLocals if field == "data" else _native.ShapePublicationLocals)
