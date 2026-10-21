"""MutableSequence that emits events when altered.

Note For Developers
===================

Be cautious when re-implementing typical list-like methods here (e.g. extend,
pop, clear, etc...).  By not re-implementing those methods, we force ALL "CRUD"
(create, read, update, delete) operations to go through a few key methods
defined by the abc.MutableSequence interface, where we can emit the necessary
events.

Specifically:

- ``insert`` = "create" : add a new item/index to the list
- ``__getitem__`` = "read" : get the value of an existing index
- ``__setitem__`` = "update" : update the value of an existing index
- ``__delitem__`` = "delete" : remove an existing index from the list

All of the additional list-like methods are provided by the MutableSequence
interface, and call one of those 4 methods.  So if you override a method, you
MUST make sure that all the appropriate events are emitted.  (Tests should
cover this in test_evented_list.py)
"""

import contextlib  # noqa: F401 - external context service used by C++
from naparixx import _native
import logging
from collections.abc import Callable, Generator, Iterable, Sequence

from napari.utils.events.containers._typed import (
    _L,
    _T,
    Index,
    TypedMutableSequence,
)
from napari.utils.events.event import EmitterGroup, Event
from napari.utils.events.types import SupportsEvents

logger = logging.getLogger(__name__)


class EventedList(TypedMutableSequence[_T]):
    """Mutable Sequence that emits events when altered.

    This class is designed to behave exactly like the builtin ``list``, but
    will emit events before and after all mutations (insertion, removal,
    setting, and moving).

    Parameters
    ----------
    data : iterable, optional
        Elements to initialize the list with.
    basetype : type or sequence of types, optional
        Type of the elements in the list.
    lookup : dict of Type[L] : function(object) -> L
        Mapping between a type, and a function that converts items in the list
        to that type.

    Events
    ------
    inserting (index: int)
        emitted before an item is inserted at ``index``
    inserted (index: int, value: T)
        emitted after ``value`` is inserted at ``index``
    removing (index: int)
        emitted before an item is removed at ``index``
    removed (index: int, value: T)
        emitted after ``value`` is removed at ``index``
    moving (index: int, new_index: int)
        emitted before an item is moved from ``index`` to ``new_index``
    moved (index: int, new_index: int, value: T)
        emitted after ``value`` is moved from ``index`` to ``new_index``
    changed (index: int, old_value: T, value: T)
        emitted when item at ``index`` is changed from ``old_value`` to ``value``
    changed <OVERLOAD> (index: slice, old_value: List[_T], value: List[_T])
        emitted when item at ``index`` is changed from ``old_value`` to ``value``
    reordered (value: self)
        emitted when the list is reordered (eg. moved/reversed).
    """

    events: EmitterGroup

    def __init__(
        self,
        data: Iterable[_T] = (),
        *,
        basetype: type[_T] | Sequence[type[_T]] = (),
        lookup: dict[type[_L], Callable[[_T], _T | _L]] | None = None,
    ) -> None:
        locals_owner = _native.evented_container_locals('init', lookup, None, None)
        del lookup
        _native.evented_sequence_init(
            self, data, basetype, globals(), lambda self: super(),
            ('inserting', 'inserted', 'removing', 'removed', 'moving',
             'moved', 'changed', 'reordered'), locals_owner,
        )

    # WAIT!! ... Read the module docstring before reimplement these methods
    # def append(self, item): ...
    # def clear(self): ...
    # def pop(self, index=-1): ...
    # def extend(self, value: Iterable[_T]): ...
    # def remove(self, value: T): ...

    def __setitem__(self, key, value: _T) -> None:
        locals_owner = _native.evented_container_locals('setitem', None, value, None)
        del value
        _native.evented_sequence_setitem(
            self, key, globals(), lambda self: super(),
            'Can only assign an iterable to slice', locals_owner,
        )

    def _delitem_indices(self, key: Index) -> Iterable[tuple["EventedList[_T]", int]]:
        # returning List[(self, int)] allows subclasses to pass nested members
        locals_owner = _native.evented_container_locals('indices', None, None, None)
        return _native.evented_sequence_delitem_indices(
            self, key, globals(), locals_owner
        )

    def __delitem__(self, key: Index) -> None:
        # delete from the end
        locals_owner = _native.evented_container_locals('delete', None, None, None)
        _native.evented_sequence_delitem(self, key, globals(), locals_owner)

    def _process_delete_item(self, item: _T) -> None:
        """Allow process item in inherited class before event was emitted"""
        _native.evented_sequence_process_delete(self, item)

    def insert(self, index: int, value: _T) -> None:
        """Insert ``value`` before index."""
        _native.evented_sequence_insert(
            self, index, value, globals(), lambda self: super()
        )

    def _reemit_child_event(self, event: Event) -> None:
        """An item in the list emitted an event.  Re-emit with index"""
        _native.evented_sequence_reemit(self, event, globals())

    def _disconnect_child_emitters(self, child: _T) -> None:
        """Disconnect all events from the child from the reemitter."""
        _native.evented_sequence_disconnect(self, child, globals())

    def _connect_child_emitters(self, child: _T) -> None:
        """Connect all events from the child to be reemitted."""
        _native.evented_sequence_connect(self, child, globals())

    def move(self, src_index: int, dest_index: int = 0) -> bool:
        """Insert object at ``src_index`` before ``dest_index``.

        Both indices refer to the list prior to any object removal
        (pre-move space).
        """
        locals_owner = _native.evented_container_locals('move', None, None, dest_index)
        del dest_index
        return _native.evented_sequence_move(self, src_index, globals(), locals_owner)

    def move_multiple(self, sources: Iterable[Index], dest_index: int = 0) -> int:
        """Move a batch of `sources` indices, to a single destination.

        Note, if `dest_index` is higher than any of the `sources`, then
        the resulting position of the moved objects after the move operation
        is complete will be lower than `dest_index`.

        Parameters
        ----------
        sources : Sequence[int or slice]
            A sequence of indices
        dest_index : int, optional
            The destination index.  All sources will be inserted before this
            index (in pre-move space), by default 0... which has the effect of
            "bringing to front" everything in ``sources``, or acting as a
            "reorder" method if ``sources`` contains all indices.

        Returns
        -------
        int
            The number of successful move operations completed.

        Raises
        ------
        TypeError
            If the destination index is a slice, or any of the source indices
            are not ``int`` or ``slice``.
        """
        locals_owner = _native.evented_container_locals('multiple', None, None, None)
        return _native.evented_sequence_move_multiple(
            self, sources, dest_index, globals(),
            'move_multiple(sources={sources}, dest_index={dest_index})',
            locals_owner,
        )

    def _move_plan(
        self, sources: Iterable[Index], dest_index: int
    ) -> Generator[tuple[int, int], None, None]:
        """Prepared indices for a multi-move.

        Given a set of ``sources`` from anywhere in the list,
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
        dest_index : Tuple[int]
            The destination for sources.
        """
        src: object

        def generator_factory(iterator):
            yield from iterator

        native_iterator = _native.evented_sequence_move_iterator(
            self, sources, dest_index, globals(), lambda: src,
            generator_factory, 'Destination index may not be a slice',
        )
        del dest_index
        yield from native_iterator

    def reverse(self) -> None:
        """Reverse list *IN PLACE*."""
        # reimplementing this method to emit a change event
        # If this method were removed, .reverse() would still be available,
        # it would just emit a "changed" event for each moved index in the list
        _native.evented_sequence_reverse(self, globals())
