"""Native ownership, immutable adapter handles and GC-visible record edges."""

import gc
import weakref

import pytest

from napari.components import Camera
from napari.components.grid import GridCanvas
from naparixx import _native


def component(kind):
    return Camera() if kind == "camera" else GridCanvas()


@pytest.mark.parametrize("kind", ("camera", "grid"))
@pytest.mark.parametrize("field", ("owner", "dictionary", "model_ref"))
@pytest.mark.parametrize("mutation", ("setattr", "generic_setattr", "delete"))
def test_record_handles_cannot_be_replaced(kind, field, mutation):
    model = component(kind)
    record = model._native_state._model_dictionary_record
    value = getattr(record, field)
    with pytest.raises(AttributeError):
        if mutation == "setattr":
            setattr(record, field, object())
        elif mutation == "generic_setattr":
            object.__setattr__(record, field, object())
        else:
            delattr(record, field)
    assert getattr(record, field) is value
    assert record.owner is model._native_state
    assert record.dictionary is vars(model)
    assert record.model_ref() is model


@pytest.mark.parametrize("kind", ("camera", "grid"))
def test_record_has_no_mutable_attribute_shadow(kind):
    model = component(kind)
    record = model._native_state._model_dictionary_record
    with pytest.raises(AttributeError):
        _ = record.__dict__
    with pytest.raises(AttributeError):
        record.unrelated_reference = model


@pytest.mark.parametrize("kind", ("camera", "grid"))
def test_native_record_edges_are_directly_traversed(kind):
    model = component(kind)
    state = model._native_state
    record = state._model_dictionary_record
    assert gc.is_tracked(record)
    references = gc.get_referents(record)
    for expected in (type(record), state, vars(model), record.model_ref):
        assert any(value is expected for value in references)


@pytest.mark.parametrize("kind", ("camera", "grid"))
def test_retained_record_keeps_actual_native_owner_alive(kind):
    model = component(kind)
    state = model._native_state
    record = state._model_dictionary_record
    model_ref, state_ref = weakref.ref(model), weakref.ref(state)
    record_ref = weakref.ref(record)
    del model, state
    gc.collect()
    assert model_ref() is None
    assert state_ref() is record.owner
    assert record.model_ref() is None
    field, value = ("zoom", 6.0) if kind == "camera" else ("stride", 2**80)
    record.dictionary[field] = value
    assert getattr(record.owner, field) == value
    del record
    gc.collect()
    assert state_ref() is None
    assert record_ref() is None


@pytest.mark.parametrize("kind", ("camera", "grid"))
def test_model_state_record_and_dictionary_cycles_collect(kind):
    references = []
    for _ in range(25):
        model = component(kind)
        state = model._native_state
        record = state._model_dictionary_record
        vars(model)["cycle"] = (model, state, record)
        references.extend(weakref.ref(value) for value in (model, state, record))
        del model, state, record
    gc.collect()
    assert all(reference() is None for reference in references)


def test_gc_can_traverse_uninitialized_private_record():
    record = _native.ComponentStateRecord.__new__(_native.ComponentStateRecord)
    assert any(value is type(record) for value in gc.get_referents(record))
    reference = weakref.ref(record)
    del record
    gc.collect()
    assert reference() is None


@pytest.mark.parametrize("kind", ("camera", "grid"))
def test_dictionary_finalizer_reentry_sees_new_owner_view(kind):
    model = component(kind)
    state = model._native_state
    record = state._model_dictionary_record
    observations = []
    field, value = ("zoom", 8.0) if kind == "camera" else ("stride", 3)

    if kind == "camera":
        old_map = vars(model)

        class Payload:
            def __del__(self):
                current_record = state._model_dictionary_record
                observations.append(
                    (
                        current_record.owner is state,
                        current_record.dictionary is vars(model),
                        getattr(state, field),
                    )
                )
    else:
        old_map = vars(model)

        class Payload:
            def __del__(self):
                current_record = state._model_dictionary_record
                observations.append(
                    (
                        current_record.owner is state,
                        current_record.dictionary is vars(model),
                        getattr(state, field),
                    )
                )

    model.__dict__["payload"] = Payload()
    replacement = dict(model.__dict__)
    del replacement["payload"]
    replacement[field] = value
    model.__dict__ = replacement
    assert observations == []
    if kind == "camera":
        # This fork-only diagnostic formerly retargeted the saved record lazily.
        # Camera now publishes a successor; a caller-held record intentionally
        # retains its old map as a passive alias until that caller drops it.
        current_record = state._model_dictionary_record
        assert current_record is not record
        assert current_record.owner is state
        assert current_record.dictionary is replacement is vars(model)
        assert record.dictionary is old_map
        assert record.watched is False
        assert getattr(state, field) == value
        assert observations == []
        del record, old_map
        assert observations == [(True, True, value)]
    else:
        # Grid now publishes a successor; a saved diagnostic remains an owning
        # caller alias for its retired map without retaining application authority.
        current_record = state._model_dictionary_record
        assert current_record is not record
        assert current_record.owner is state
        assert current_record.dictionary is replacement is vars(model)
        assert record.dictionary is old_map
        assert record.watched is False
        assert getattr(state, field) == value
        assert observations == []
        del record, old_map
        assert observations == [(True, True, value)]
