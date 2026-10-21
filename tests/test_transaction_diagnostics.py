"""Legacy journal diagnostics must not own application transaction payloads."""

import copy
import gc
import types
import weakref

import pytest

from napari.utils.events import EventedModel
from naparixx import native


class Model(EventedModel):
    value: object = None


class Payload:
    pass


def _legacy_view(journal):
    provider = types.SimpleNamespace(
        _primary_view=None,
        _change_state=journal,
        __pydantic_private__={},
    )
    return native.model_primary_changes(provider)


@pytest.mark.parametrize("method", ["copy", "model_copy"])
@pytest.mark.parametrize("edit", ["entries", "private_map"])
def test_saved_source_and_shallow_journals_follow_separate_owners(method, edit):
    source = Model()
    source._primary_changes["shared"] = None
    source._delay_check_semaphore = 2
    source_journal = source._change_state
    shallow = copy.copy(source) if method == "copy" else source.model_copy()
    shallow_journal = shallow._change_state

    assert shallow_journal is not source_journal
    assert shallow._primary_changes is source._primary_changes
    assert source_journal.primary_names == shallow_journal.primary_names == ["shared"]
    assert source_journal.depth == shallow_journal.depth == 2

    source_depth, shallow_depth = object(), object()
    for model, name, depth in (
        (source, "source", source_depth),
        (shallow, "shallow", shallow_depth),
    ):
        mapping = model.__pydantic_private__
        if edit == "private_map":
            mapping = dict(mapping)
        mapping["_primary_changes"] = {name: None}
        mapping["_delay_check_semaphore"] = depth
        if edit == "private_map":
            model.__pydantic_private__ = mapping

    # Read both facades again: attaching the shallow diagnostic must never
    # redirect a journal saved from the source to the shallow private map.
    assert source._change_state is source_journal
    assert shallow._change_state is shallow_journal
    assert source_journal.primary_names == ["source"]
    assert shallow_journal.primary_names == ["shallow"]
    assert source_journal.depth is source_depth
    assert shallow_journal.depth is shallow_depth
    assert source._delay_check_semaphore is source_depth
    assert shallow._delay_check_semaphore is shallow_depth


def test_deep_copy_legacy_view_memo_cannot_retain_application_primary_payload():
    source = Model()
    journal = source._change_state
    legacy_view = _legacy_view(journal)
    legacy_view["core"] = None
    payload = Payload()
    payload_reference = weakref.ref(payload)
    primary = {"application": payload}
    source._primary_changes = primary
    memo = {id(legacy_view): primary}

    deep = copy.deepcopy(source, memo)
    assert deep._primary_changes is not primary
    assert deep._primary_changes["application"] is not payload
    assert memo[id(primary)] is deep._primary_changes
    assert memo[id(payload)] is deep._primary_changes["application"]
    assert memo[id(legacy_view)] is primary

    # Drop the caller's deliberate alias and the original application owner.
    # Keeping the copied model/journal alive must not keep that old dict alive.
    del memo, primary, payload
    source._primary_changes = {}
    gc.collect()
    assert payload_reference() is None
    assert deep._change_state.primary_names == ["application"]
    assert list(_legacy_view(deep._change_state)) == ["core"]


def test_saved_journal_weak_edge_does_not_retain_model_or_native_owner():
    def scenario():
        model = Model()
        model.value = model
        journal = model._change_state
        owner = native.model_transaction_owner(model)
        return journal, weakref.ref(model), weakref.ref(owner)

    journal, model_reference, owner_reference = scenario()
    gc.collect()
    assert model_reference() is None
    assert owner_reference() is None
    assert journal.primary_names == []


@pytest.mark.parametrize("edit", ["attribute", "private_entry", "private_map"])
def test_saved_journal_does_not_retain_replaced_primary_payload(edit):
    model = Model()
    journal = model._change_state
    owner = native.model_transaction_owner(model)
    payload = Payload()
    payload_reference = weakref.ref(payload)
    model._primary_changes = {"old": payload}
    assert journal.primary_names == ["old"]
    del payload

    replacement = {"new": None}
    if edit == "attribute":
        model._primary_changes = replacement
    elif edit == "private_entry":
        model.__pydantic_private__["_primary_changes"] = replacement
    else:
        mapping = dict(model.__pydantic_private__)
        mapping["_primary_changes"] = replacement
        model.__pydantic_private__ = mapping

    assert payload_reference() is None
    assert owner.private_mapping is model.__pydantic_private__
    assert model._primary_changes is replacement
    assert model._change_state is journal
    assert journal.primary_names == ["new"]
