"""Native Dims dictionary-reference ownership, GC and teardown behavior."""

import gc
import sys
import weakref

import pytest

from napari.components import Dims
from naparixx import _native


def record_for(model, kind):
    return (
        model._native_dictionary_record
        if kind == "fields"
        else model._native_flag_record
    )


def owner_for(model, kind):
    return model._native_state if kind == "fields" else model._native_flags


@pytest.mark.parametrize("kind", ("fields", "flags"))
@pytest.mark.parametrize("mutation", ("setattr", "generic_setattr", "delete"))
def test_dictionary_reference_cannot_be_replaced(kind, mutation):
    model = Dims(ndim=3)
    record = record_for(model, kind)
    dictionary = record.dictionary
    with pytest.raises(AttributeError):
        if mutation == "setattr":
            record.dictionary = {}
        elif mutation == "generic_setattr":
            object.__setattr__(record, "dictionary", {})
        else:
            del record.dictionary
    assert record.dictionary is dictionary
    assert dictionary is (
        vars(model) if kind == "fields" else model.__pydantic_private__
    )


@pytest.mark.parametrize("kind", ("fields", "flags"))
def test_record_has_no_mutable_attribute_shadow(kind):
    record = record_for(Dims(), kind)
    with pytest.raises(AttributeError):
        _ = record.__dict__
    with pytest.raises(AttributeError):
        record.extra_reference = object()


@pytest.mark.parametrize("kind", ("fields", "flags"))
def test_native_dictionary_edge_is_directly_traversed(kind):
    model = Dims()
    record = record_for(model, kind)
    assert gc.is_tracked(record)
    references = gc.get_referents(record)
    assert any(value is type(record) for value in references)
    assert any(value is record.dictionary for value in references)


@pytest.mark.parametrize("kind", ("fields", "flags"))
def test_retained_record_preserves_dictionary_after_model_collection(kind):
    class Payload:
        pass

    model = Dims(ndim=3)
    owner = owner_for(model, kind)
    record = record_for(model, kind)
    record.dictionary["payload"] = Payload()
    model_ref, record_ref = weakref.ref(model), weakref.ref(record)
    payload_ref = weakref.ref(record.dictionary["payload"])
    del model
    gc.collect()
    assert model_ref() is None
    assert payload_ref() is not None
    if kind == "fields":
        record.dictionary["point"] = (2.0, 1.0, 0.0)
        assert owner.point == (2.0, 1.0, 0.0)
    else:
        record.dictionary["_scroll_progress"] = 0.75
        assert owner.scroll_progress == 0.75
    del record
    gc.collect()
    assert record_ref() is payload_ref() is None
    # Retaining the primitive C++ owner must not retain an observer's dictionary.
    if kind == "fields":
        assert owner.point == (2.0, 1.0, 0.0)
    else:
        assert owner.scroll_progress == 0.75


@pytest.mark.parametrize("kind", ("fields", "flags"))
def test_model_record_dictionary_and_payload_cycles_collect(kind):
    class Payload:
        pass

    references = []
    for _ in range(25):
        model = Dims(ndim=3)
        owner = owner_for(model, kind)
        record = record_for(model, kind)
        payload = Payload()
        payload.cycle = model, owner, record
        record.dictionary["payload"] = payload
        references.extend(
            weakref.ref(value) for value in (model, owner, record, payload)
        )
        del model, owner, record, payload
    gc.collect()
    assert all(reference() is None for reference in references)


@pytest.mark.parametrize("record_name", ("DimsDictionaryRecord", "DimsFlagRecord"))
def test_uninitialized_record_is_safe_to_traverse_and_collect(record_name):
    record_type = getattr(_native, record_name)
    record = record_type.__new__(record_type)
    assert any(value is record_type for value in gc.get_referents(record))
    reference = weakref.ref(record)
    del record
    gc.collect()
    assert reference() is None


def test_field_dictionary_adoption_finalizer_sees_consistent_new_view():
    model = Dims(ndim=3)
    owner = model._native_state
    record = model._native_dictionary_record
    observations = []

    class Payload:
        def __del__(self):
            observations.append(
                (
                    record.dictionary is vars(model),
                    model.point,
                    owner.point,
                    record.paused,
                )
            )

    vars(model)["payload"] = Payload()
    replacement = dict(vars(model))
    del replacement["payload"]
    replacement["point"] = (2.0, 1.0, 0.0)
    model.__dict__ = replacement
    assert model.point == (2.0, 1.0, 0.0)
    assert owner.point == (2.0, 1.0, 0.0)
    assert record.dictionary is replacement
    assert observations == [(True, (2.0, 1.0, 0.0), (2.0, 1.0, 0.0), 0)]


@pytest.mark.parametrize("read_first", ("projection", "field", "native_owner"))
def test_current_projection_adopts_replacement_and_saved_view_stays_valid(read_first):
    class Payload:
        pass

    model = Dims(ndim=3)
    old_view = model._native_projection
    vars(model)["payload"] = Payload()
    payload_ref = weakref.ref(old_view["payload"])
    replacement = dict(vars(model))
    del replacement["payload"]
    replacement["point"] = (2.0, 1.0, 0.0)
    model.__dict__ = replacement
    if read_first == "projection":
        assert model._native_projection["point"] == (2.0, 1.0, 0.0)
    elif read_first == "field":
        assert model.point == (2.0, 1.0, 0.0)
    else:
        assert model._native_state.point == (2.0, 1.0, 0.0)
    assert model._native_projection is not old_view
    assert model._native_projection["point"] == (2.0, 1.0, 0.0)
    assert old_view["point"] == (0.0, 0.0, 0.0)
    assert payload_ref() is not None
    del old_view
    gc.collect()
    assert payload_ref() is None


def test_private_dictionary_replacement_finalizer_sees_new_flag_record():
    model = Dims()
    model._scroll_progress = 0.25
    previous_owner = model._native_flags
    previous_record = model._native_flag_record
    previous_ref = weakref.ref(previous_record)
    observations = []

    class Payload:
        def __del__(self):
            observations.append(
                (
                    model._native_flag_record.dictionary is model.__pydantic_private__,
                    model._native_flags.scroll_progress,
                    previous_owner.scroll_progress,
                )
            )

    model.__pydantic_private__["payload"] = Payload()
    replacement = dict(model.__pydantic_private__)
    del replacement["payload"]
    replacement["_scroll_progress"] = 0.75
    model.__pydantic_private__ = replacement
    assert model._native_flags is not previous_owner
    assert observations == []
    del previous_record
    gc.collect()
    assert previous_ref() is None
    assert observations == [(True, 0.75, 0.25)]


@pytest.mark.parametrize("kind", ("fields", "flags"))
def test_restored_dictionary_aliases_keep_native_observers(kind):
    source = Dims(ndim=3)
    saved = source.__getstate__()
    first, second = Dims.__new__(Dims), Dims.__new__(Dims)
    first.__setstate__(saved)
    second.__setstate__(saved)
    record_a, record_b = record_for(first, kind), record_for(second, kind)
    assert record_a.dictionary is record_b.dictionary
    owner_a, owner_b = owner_for(first, kind), owner_for(second, kind)
    if kind == "fields":
        assert record_a is not record_b
        record_a.dictionary["point"] = (2.0, 1.0, 0.0)
        assert owner_a.point == owner_b.point == (2.0, 1.0, 0.0)
    else:
        assert record_a is record_b
        record_a.dictionary["_scroll_progress"] = 0.75
        assert owner_a is owner_b
        assert owner_a.scroll_progress == 0.75
    first_ref, second_ref = weakref.ref(first), weakref.ref(second)
    del source, saved, first, second
    gc.collect()
    assert first_ref() is second_ref() is None
    if kind == "fields":
        record_b.dictionary["point"] = (0.0, 2.0, 1.0)
        assert owner_a.point == owner_b.point == (0.0, 2.0, 1.0)
    else:
        record_b.dictionary["_scroll_progress"] = 0.5
        assert owner_a.scroll_progress == 0.5


@pytest.mark.parametrize("kind", ("fields", "flags"))
def test_record_teardown_preserves_active_python_exception(kind):
    model = Dims()
    record = record_for(model, kind)
    reference = weakref.ref(record)
    try:
        raise ValueError("preserve active exception")
    except ValueError as error:
        del model, record
        gc.collect()
        assert reference() is None
        assert sys.exception() is error
