"""Native suspended-reference ownership and close/GC behavior."""

import gc
import weakref
from types import SimpleNamespace

import pytest

from napari.components.grid import GridCanvas
from napari.utils.events.containers import (
    EventedList,
    NestableEventedList,
    TypedMutableSequence,
)
from naparixx import _native

KINDS = ("list_move", "completion", "nested_move", "nested_index", "grid")
CLASSES = (
    "ListMoveIterator",
    "ListCompletionIterator",
    "NestedMoveIterator",
    "NestedIndexIterator",
    "GridIterator",
)


def make_iterator(kind):
    if kind == "list_move":
        owner = EventedList([0, 1, 2, 3])
        return owner, _native.evented_sequence_move_iterator(owner, [0, 2], 4)
    if kind == "completion":
        owner = TypedMutableSequence(["first", "second"], lookup={str: str})
        return owner, _native.sequence_completion_iterator(owner, iter(owner))
    if kind == "nested_move":
        owner = NestableEventedList([0, [1, 2], 3])
        return owner, _native.nested_move_iterator(owner, [(1, 0)], (3,))
    if kind == "nested_index":
        owner = NestableEventedList([0, [1, 2], 3])
        return owner, _native.nested_index_iterator(owner, 0, None, ())
    if kind == "grid":
        owner = GridCanvas(enabled=True, shape=(1, -1))
        layers = [SimpleNamespace(visible=True) for _ in range(3)]
        return owner, _native.grid_viewboxes(owner, layers)
    raise AssertionError(kind)


@pytest.mark.parametrize("kind", KINDS)
def test_frame_is_a_native_gc_edge_and_readonly_reference(kind):
    owner, iterator = make_iterator(kind)
    frame = iterator.frame
    assert type(frame) is dict
    assert not hasattr(iterator, "__dict__")
    assert any(value is frame for value in gc.get_referents(iterator))
    assert any(value is type(iterator) for value in gc.get_referents(iterator))
    for replacement in ({}, None, object()):
        with pytest.raises(AttributeError):
            iterator.frame = replacement
        assert iterator.frame is frame
    with pytest.raises(AttributeError):
        del iterator.frame
    with pytest.raises(AttributeError):
        iterator.injected_reference = owner
    next(iterator)
    assert iterator.frame is frame
    iterator.close()
    assert iterator.frame is frame and not frame


@pytest.mark.parametrize("kind", KINDS)
@pytest.mark.parametrize("started", (False, True))
def test_native_frame_cycles_collect(kind, started):
    owner, iterator = make_iterator(kind)
    if started:
        next(iterator)
    references = weakref.ref(owner), weakref.ref(iterator)
    iterator.frame["cycle"] = iterator
    vars(owner)["iterator_cycle"] = iterator
    del owner, iterator
    gc.collect()
    assert all(reference() is None for reference in references)


@pytest.mark.parametrize("kind", KINDS)
@pytest.mark.parametrize("finish", ("close", "exhaust", "throw"))
def test_finishing_releases_payloads_and_empties_saved_frame(kind, finish):
    owner, iterator = make_iterator(kind)
    reference = weakref.ref(owner)
    frame = iterator.frame
    del owner
    gc.collect()
    assert reference() is not None
    if finish == "close":
        iterator.close()
    elif finish == "exhaust":
        list(iterator)
    else:
        with pytest.raises(RuntimeError, match="injected"):
            iterator.throw(RuntimeError("injected"))
    assert not frame and iterator.frame is frame
    gc.collect()
    assert reference() is None
    with pytest.raises(StopIteration):
        next(iterator)
    assert iterator.close() is None


@pytest.mark.parametrize("kind", KINDS)
def test_payload_finalizers_observe_exhausted_iterator(kind):
    owner, iterator = make_iterator(kind)
    reference = weakref.ref(iterator)
    observations = []

    class Payload:
        def __del__(self):
            current = reference()
            if current is None:
                return
            observations.append((bool(current.frame), current.close()))
            try:
                next(current)
            except StopIteration:
                observations.append("exhausted")

    iterator.frame["payload"] = Payload()
    iterator.close()
    assert observations == [(False, None), "exhausted"]
    del owner, iterator


@pytest.mark.parametrize("class_name", CLASSES)
def test_uninitialized_native_frame_objects_collect(class_name):
    cls = getattr(_native, class_name)
    iterator = cls.__new__(cls)
    assert any(value is cls for value in gc.get_referents(iterator))
    reference = weakref.ref(iterator)
    del iterator
    gc.collect()
    assert reference() is None
