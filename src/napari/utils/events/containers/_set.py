from __future__ import annotations

from collections import deque
from collections.abc import Iterable, Iterator, MutableSet
from types import GeneratorType
from typing import (
    TYPE_CHECKING,
    Any,
    TypeVar,
    get_args,
)

from pydantic_core import core_schema

from napari.utils.events import EmitterGroup

from naparixx import _native

if TYPE_CHECKING:
    from pydantic import GetCoreSchemaHandler

_T = TypeVar("_T")


def sequence_like(v: Any) -> bool:
    return _native.evented_set_sequence_like(v)


class EventedSet(MutableSet[_T]):
    """An unordered collection of unique elements.

    Parameters
    ----------
    data : iterable, optional
        Elements to initialize the set with.

    Events
    ------
    changed (added: Set[_T], removed: Set[_T])
        Emitted when the set changes, includes item(s) that have been added
        and/or removed from the set.
    """

    events: EmitterGroup

    def __init__(self, data: Iterable[_T] = ()) -> None:
        _native.evented_set_init(self, data)

    # #### START Required Abstract Methods

    def __contains__(self, x: Any) -> bool:
        return _native.evented_set_contains(self, x)

    def __iter__(self) -> Iterator[_T]:
        return _native.evented_set_iter(self)

    def __len__(self) -> int:
        return _native.evented_set_len(self)

    def _pre_add_hook(self, value: _T) -> _T:
        # for subclasses to potentially check value before adding
        return _native.evented_set_pre_add(value)

    def _emit_change(
        self,
        added: set[_T] | None = None,
        removed: set[_T] | None = None,
    ) -> None:
        # provides a hook for subclasses to update internal state before emit
        _native.evented_set_emit_change(self, added, removed)

    def add(self, value: _T) -> None:
        """Add an element to the set, if not already present."""
        _native.evented_set_add(self, value)

    def discard(self, value: _T) -> None:
        """Remove an element from a set if it is a member.

        If the element is not a member, do nothing.
        """
        _native.evented_set_discard(self, value)

    # #### END Required Abstract Methods

    # methods inherited from Set:
    # __le__, __lt__, __eq__, __ne__, __gt__, __ge__, __and__, __or__,
    # __sub__, __xor__, and isdisjoint

    # methods inherited from MutableSet:
    # clear, pop, remove, __ior__, __iand__, __ixor__, and __isub__

    # The rest are for parity with builtins.set:

    def clear(self) -> None:
        _native.evented_set_clear(self)

    def __repr__(self) -> str:
        return _native.evented_set_repr(self)

    def update(self, others: Iterable[_T] = ()) -> None:
        """Update this set with the union of this set and others"""
        _native.evented_set_update(self, others)

    def copy(self) -> EventedSet[_T]:
        """Return a shallow copy of this set."""
        return _native.evented_set_copy(self)

    def difference(self, others: Iterable[_T] = ()) -> EventedSet[_T]:
        """Return set of all elements that are in this set but not other."""
        return _native.evented_set_difference(self, others)

    def difference_update(self, others: Iterable[_T] = ()) -> None:
        """Remove all elements of another set from this set."""
        _native.evented_set_difference_update(self, others)

    def intersection(self, others: Iterable[_T] = ()) -> EventedSet[_T]:
        """Return all elements that are in both sets as a new set."""
        return _native.evented_set_intersection(self, others)

    def intersection_update(self, others: Iterable[_T] = ()) -> None:
        """Remove all elements of in this set that are not present in other."""
        _native.evented_set_intersection_update(self, others)

    def issubset(self, others: Iterable[_T]) -> bool:
        """Returns whether another set contains this set or not"""
        return _native.evented_set_issubset(self, others)

    def issuperset(self, others: Iterable[_T]) -> bool:
        """Returns whether this set contains another set or not"""
        return _native.evented_set_issuperset(self, others)

    def symmetric_difference(self, others: Iterable[_T]) -> EventedSet[_T]:
        """Returns set of elements that are in exactly one of the sets"""
        return _native.evented_set_symmetric_difference(self, others)

    def symmetric_difference_update(self, others: Iterable[_T]) -> None:
        """Update set to the symmetric difference of itself and another.

        This will remove any items in this set that are also in `other`, and
        add any items in others that are not present in this set.
        """
        _native.evented_set_symmetric_difference_update(self, others)

    def union(self, others: Iterable[_T] = ()) -> EventedSet[_T]:
        """Return a set containing the union of sets"""
        return _native.evented_set_union(self, others)

    @classmethod
    def __get_pydantic_core_schema__(
        cls, source: Any, handler: GetCoreSchemaHandler
    ) -> core_schema.CoreSchema:
        return _native.evented_set_schema(cls, source, handler)

    def _json_encode(self) -> list:
        """Return an object that can be used by json.dumps."""
        return _native.evented_set_json(self)
