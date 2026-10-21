"""Nestable MutableSequence that emits events when altered.

see module docstring of evented_list.py for more details
"""

from __future__ import annotations

import contextlib
import logging
from collections import defaultdict
from collections.abc import Generator, Iterable, MutableSequence
from typing import (
    NewType,
    TypeVar,
    Union,
    cast,
    overload,
)

from napari.utils.events.containers._evented_list import EventedList, Index
from napari.utils.events.event import Event

from naparixx import _native

logger = logging.getLogger(__name__)

NestedIndex = tuple[Index, ...]
MaybeNestedIndex = Union[Index, NestedIndex]
ParentIndex = NewType("ParentIndex", tuple[int, ...])
_T = TypeVar("_T")


def ensure_tuple_index(index: MaybeNestedIndex) -> NestedIndex:
    """Return index as a tuple of ints or slices.

    Parameters
    ----------
    index : Tuple[Union[int, slice], ...] or int or slice
        An index as an int, tuple, or slice

    Returns
    -------
    NestedIndex
        The index, guaranteed to be a tuple.

    Raises
    ------
    TypeError
        If the input ``index`` is not an ``int``, ``slice``, or ``tuple``.
    """
    return _native.nested_ensure_index(index)


def split_nested_index(index: MaybeNestedIndex) -> tuple[ParentIndex, Index]:
    """Given a nested index, return (nested_parent_index, row).

    Parameters
    ----------
    index : MaybeNestedIndex
        An index as an int, tuple, or slice

    Returns
    -------
    Tuple[NestedIndex, Index]
        A tuple of ``parent_index``, ``row``

    Raises
    ------
    ValueError
        If any of the items in the returned ParentIndex tuple are not ``int``.

    Examples
    --------
    >>> split_nested_index((1, 2, 3, 4))
    ((1, 2, 3), 4)
    >>> split_nested_index(1)
    ((), 1)
    >>> split_nested_index(())
    ((), -1)
    """
    return _native.nested_split_index(index)


class NestableEventedList(EventedList[_T]):
    """Nestable Mutable Sequence that emits recursive events when altered.

    ``NestableEventedList`` instances can be indexed with a ``tuple`` of
    ``int`` (e.g. ``mylist[0, 2, 1]``) to retrieve nested child objects.

    A key property of this class is that when new mutable sequences are added
    to the list, they are themselves converted to a ``NestableEventedList``,
    and all of the ``EventEmitter`` objects in the child are connect to the
    parent object's ``_reemit_child_event`` method (assuming the child has
    an attribute called ``events`` that is an instance of ``EmitterGroup``).
    When ``_reemit_child_event`` receives an event from a child object, it
    remits the event, but changes any ``index`` keys in the event to a
    ``NestedIndex`` (a tuple of ``int``) such that indices emitted by any given
    ``NestableEventedList`` are always relative to itself.


    Parameters
    ----------
    data : iterable, optional
        Elements to initialize the list with. by default None.
    basetype : type or sequence of types, optional
        Type of the elements in the list.
    lookup : dict of Type[L] : function(object) -> L
        Mapping between a type, and a function that converts items in the list
        to that type.

    Events
    ------
    types used:
        Index = Union[int, Tuple[int, ...]]

    inserting (index: Index)
        emitted before an item is inserted at ``index``
    inserted (index: Index, value: T)
        emitted after ``value`` is inserted at ``index``
    removing (index: Index)
        emitted before an item is removed at ``index``
    removed (index: Index, value: T)
        emitted after ``value`` is removed at ``index``
    moving (index: Index, new_index: Index)
        emitted before an item is moved from ``index`` to ``new_index``
    moved (index: Index, new_index: Index, value: T)
        emitted after ``value`` is moved from ``index`` to ``new_index``
    changed (index: Index, old_value: T, value: T)
        emitted when item at ``index`` is changed from ``old_value`` to ``value``
    changed <OVERLOAD> (index: slice, old_value: list[_T], value: list[_T])
        emitted when slice at ``index`` is changed from ``old_value`` to ``value``
    reordered (value: self)
        emitted when the list is reordered (eg. moved/reversed).
    """

    # WAIT!! ... Read the ._list module docs before reimplement these classes
    # def append(self, item): ...
    # def clear(self): ...
    # def pop(self, index=-1): ...
    # def extend(self, value: Iterable[_T]): ...
    # def remove(self, value: T): ...

    @overload
    def __getitem__(
        self, key: int
    ) -> _T | NestableEventedList[_T]: ...  # pragma: no cover

    @overload
    def __getitem__(
        self, key: ParentIndex
    ) -> NestableEventedList[_T]: ...  # pragma: no cover

    @overload
    def __getitem__(
        self, key: slice
    ) -> NestableEventedList[_T]: ...  # pragma: no cover

    @overload
    def __getitem__(
        self, key: NestedIndex
    ) -> _T | NestableEventedList[_T]: ...  # pragma: no cover

    def __getitem__(self, key: MaybeNestedIndex):
        return _native.nested_getitem(self, key, super())

    @overload
    def __setitem__(
        self, key: int | NestedIndex, value: _T
    ) -> None: ...  # pragma: no cover

    @overload
    def __setitem__(
        self, key: slice, value: Iterable[_T]
    ) -> None: ...  # pragma: no cover

    def __setitem__(self, key, value):
        # NOTE: if we check isinstance(..., MutableList), then we'll actually
        # clobber object of specialized classes being inserted into the list
        # (for instance, subclasses of NestableEventedList)
        # this check is more conservative, but will miss some "nestable" things
        _native.nested_setitem(self, key, value, super())

    def _delitem_indices(
        self, key: MaybeNestedIndex
    ) -> Iterable[tuple[EventedList[_T], int]]:
        return _native.nested_delitem_indices(self, key, super())

    def insert(self, index: int, value: _T) -> None:
        """Insert object before index."""
        # this is delicate, we want to preserve the evented list when nesting
        # but there is a high risk here of clobbering attributes of a special
        # child class
        _native.nested_insert(self, index, value, super())

    def _reemit_child_event(self, event: Event) -> None:
        """An item in the list emitted an event.  Re-emit with index"""
        _native.nested_reemit(self, event)

    def _non_negative_index(
        self, parent_index: ParentIndex, dest_index: Index
    ) -> Index:
        """Make sure dest_index is a positive index inside parent_index."""
        return _native.nested_non_negative_index(self, parent_index, dest_index)

    def _move_plan(
        self, sources: Iterable[MaybeNestedIndex], dest_index: NestedIndex
    ) -> Generator[tuple[NestedIndex, NestedIndex], None, None]:
        """Prepared indices for a complicated nested multi-move.

        Given a set of possibly-nested ``sources`` from anywhere in the tree,
        and a single ``dest_index``, this function computes and yields
        ``(from_index, to_index)`` tuples that can be used sequentially in
        single move operations.  It keeps track of what has moved where and
        updates the source and destination indices to reflect the model at each
        point in the process.

        This is useful for a drag-drop operation with a QtModel/View.

        Parameters
        ----------
        sources : Iterable[tuple[int, ...]]
            An iterable of tuple[int] that should be moved to ``dest_index``.
            (Note: currently, the order of ``sources`` will NOT be maintained.)
        dest_index : Tuple[int]
            The destination for sources.

        Yields
        ------
        Generator[tuple[int, ...], None, None]
            [description]

        Raises
        ------
        ValueError
            If any source terminal or the destination terminal index is a slice
        IndexError
            If any of the sources are the root object: ``()``.
        NotImplementedError
            If a slice is provided in the middle of a source index.
        """

        yield from _native.nested_move_iterator(self, sources, dest_index)

    def move(
        self,
        src_index: int | NestedIndex,
        dest_index: int | NestedIndex = (0,),
    ) -> bool:
        """Move a single item from ``src_index`` to ``dest_index``.

        Parameters
        ----------
        src_index : Union[int, NestedIndex]
            The index of the object to move
        dest_index : Union[int, NestedIndex], optional
            The destination.  Object will be inserted before ``dest_index.``,
            by default, will insert at the front of the root list.

        Returns
        -------
        bool
            Whether the operation completed successfully

        Raises
        ------
        ValueError
            If the terminal source is a slice, or if the source is this root
            object
        """
        return _native.nested_move(self, src_index, dest_index)

    def _type_check(self, e) -> _T:
        return _native.nested_type_check(self, e)

    def _iter_indices(
        self,
        start: int = 0,
        stop: int | None = None,
        root: tuple[int, ...] = (),
    ) -> Generator[int | tuple[int]]:
        """Iter indices from start to stop.

        Depth first traversal of the tree
        """
        yield from _native.nested_index_iterator(self, start, stop, root)

    def has_index(self, index: int | tuple[int, ...]) -> bool:
        """Return true if `index` is valid for this nestable list."""
        return _native.nested_has_index(self, index)
