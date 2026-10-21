from collections import deque
from collections.abc import Iterable, MutableSet
from types import GeneratorType
from typing import TYPE_CHECKING, Any, Generic, TypeVar, Union, get_args

from pydantic import GetCoreSchemaHandler
from pydantic_core import core_schema

from napari.utils.events.containers._set import EventedSet
from napari.utils.events.event import EmitterGroup

from naparixx import _native

if TYPE_CHECKING:
    from pydantic_core.core_schema import DictSchema, TypedDictSchema

_T = TypeVar("_T")
_S = TypeVar("_S")


def sequence_like(v: Any) -> bool:
    """Check whether an object is sequence-like."""
    return _native.selection_sequence_like(v)


class Selection(EventedSet[_T]):
    """A model of selected items, with ``active`` and ``current`` item.

    There can only be one ``active`` and one ``current`` item, but there can be
    multiple selected items.  An "active" item is defined as a single selected
    item (if multiple items are selected, there is no active item).  The
    "current" item is mostly useful for (e.g.) keyboard actions: even with
    multiple items selected, you may only have one current item, and keyboard
    events (like up and down) can modify that current item.  It's possible to
    have a current item without an active item, but an active item will always
    be the current item.

    An item can be the current item and selected at the same time. Qt views
    will ensure that there is always a current item as keyboard navigation,
    for example, requires a current item.

    This pattern mimics current/selected items from Qt:
    https://doc.qt.io/qt-5/model-view-programming.html#current-item-and-selected-items

    Parameters
    ----------
    data : iterable, optional
        Elements to initialize the set with.

    Attributes
    ----------
    active : Any, optional
        The active item, if any.  An active item is the one being edited.
    _current : Any, optional
        The current item, if any.  This is used primarily by GUI views when
        handling mouse/key events.

    Events
    ------
    changed (added: Set[_T], removed: Set[_T])
        Emitted when the set changes, includes item(s) that have been added and/or removed from the set.
    active (value: _T)
        emitted when the current item has changed.
    _current (value: _T)
        emitted when the current item has changed. (Private event)
    """

    def __init__(self, data: Iterable[_T] = ()) -> None:
        _native.selection_init(self, data, super())

    def _emit_change(
        self,
        added: set[_T] | None = None,
        removed: set[_T] | None = None,
    ) -> None:
        return _native.selection_emit_change(self, added, removed, super())

    def __repr__(self) -> str:
        return _native.selection_repr(self)

    def __hash__(self) -> int:
        """Make selection hashable."""
        return _native.selection_hash(self)

    @property
    def _current(self) -> _T | None:
        """Get current item."""
        return _native.selection_current_get(self)

    @_current.setter
    def _current(self, index: _T | None) -> None:
        """Set current item."""
        _native.selection_current_set(self, index)

    @property
    def active(self) -> _T | None:
        """Return the currently active item or None."""
        return _native.selection_active_get(self)

    @active.setter
    def active(self, value: _T | None) -> None:
        """Set the active item.

        This make `value` the only selected item, and make it current.
        """
        _native.selection_active_set(self, value)

    def _update_active(self) -> None:
        """On a selection event, update the active item based on selection.

        (An active item is a single selected item).
        """
        _native.selection_update_active(self)

    def clear(self, keep_current: bool = False) -> None:
        """Clear the selection."""
        _native.selection_clear(self, keep_current, super())

    def _add_and_remove(self, add: Iterable[_T], remove: Iterable[_T]) -> None:
        """Add and remove obj from selection."""
        _native.selection_add_remove(self, add, remove)

    def toggle(self, obj: _T) -> None:
        """Toggle selection state of obj."""
        _native.selection_toggle(self, obj)

    def select_only(self, obj: _T) -> None:
        """Unselect everything but `obj`. Add to selection if not present."""
        _native.selection_only(self, obj)

    @classmethod
    def __get_pydantic_core_schema__(
        cls, source: Any, handler: GetCoreSchemaHandler
    ) -> core_schema.CoreSchema:
        def _serialize(v: Selection) -> dict:
            return _native.selection_serialize(v)

        return _native.selection_schema(cls, source, handler, _serialize)

    @classmethod
    def _validate_selection(
        cls,
        v: Union["Selection", dict],
    ) -> "Selection":
        """Pydantic validator."""
        return _native.selection_validate(cls, v)

    def _json_encode(self) -> dict:  # pyrefly: ignore [bad-override]
        """Return an object that can be used by json.dumps."""
        # we don't serialize active, as it's gleaned from the selection.
        return _native.selection_json(self, super())


class Selectable(Generic[_S]):
    """Mixin that adds a selection model to an object."""

    def __init__(self, *args: Any, **kwargs: Any) -> None:
        _native.selectable_init(self, args, kwargs, super())

    @property
    def selection(self) -> Selection[_S]:
        """Get current selection."""
        return _native.selectable_get(self)

    @selection.setter
    def selection(self, new_selection: Iterable[_S]) -> None:
        """Set selection, without deleting selection model object."""
        _native.selectable_set(self, new_selection)
