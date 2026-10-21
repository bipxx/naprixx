import warnings
from typing import Any, TypeVar

from napari.utils.events.containers._evented_list import EventedList
from napari.utils.events.containers._nested_list import NestableEventedList
from napari.utils.events.containers._selection import Selectable

from naparixx import _native

_T = TypeVar("_T")


class SelectableEventedList(Selectable[_T], EventedList[_T]):
    """List model that also supports selection.

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

    selection.changed (added: Set[_T], removed: Set[_T])
        Emitted when the set changes, includes item(s) that have been added
        and/or removed from the set.
    selection.active (value: _T)
        emitted when the current item has changed.
    selection._current (value: _T)
        emitted when the current item has changed. (Private event)
    """

    def __init__(self, *args: Any, **kwargs: Any) -> None:
        _native.selectable_list_init(self, args, kwargs, super())

    def _preselect_hook(self, value: _T) -> _T:
        """Called before adding an item to the selection."""
        return _native.selectable_list_preselect(self, value)

    def _process_delete_item(self, item: _T) -> None:
        _native.selectable_list_process_delete(self, item)

    def insert(self, index: int, value: _T) -> None:
        _native.selectable_list_insert(self, index, value, super())

    def select_all(self) -> None:
        """Select all items in the list."""
        _native.selectable_list_select_all(self)

    def remove_selected(self) -> None:
        """Remove selected items from list."""
        _native.selectable_list_remove_selected(self)

    def move_selected(self, index: int, insert: int) -> None:
        """Reorder list by moving the item at index and inserting it
        at the insert index. If additional items are selected these will
        get inserted at the insert index too. This allows for rearranging
        the list based on dragging and dropping a selection of items, where
        index is the index of the primary item being dragged, and insert is
        the index of the drop location, and the selection indicates if
        multiple items are being dragged. If the moved layer is not selected
        select it.

        This method is deprecated. Please use layers.move_multiple
        with layers.selection instead.

        Parameters
        ----------
        index : int
            Index of primary item to be moved
        insert : int
            Index that item(s) will be inserted at
        """
        # this is just here for now to support the old layerlist API
        _native.selectable_list_move_selected(self, index, insert)

    def select_next(self, step: int = 1, shift: bool = False) -> None:
        """Selects next item from list."""
        _native.selectable_list_select_next(self, step, shift)

    def select_previous(self, shift: bool = False) -> None:
        """Selects previous item from list."""
        _native.selectable_list_select_previous(self, shift)


class SelectableNestableEventedList(SelectableEventedList[_T], NestableEventedList[_T]):
    pass
