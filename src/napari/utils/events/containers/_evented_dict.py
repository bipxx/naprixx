"""MutableMapping that emits events when altered."""

from typing import TYPE_CHECKING

from psygnal import EmissionInfo, EventedModel as PsygnalModel

from napari.utils.events.containers._dict import _K, _T, TypedMutableMapping
from napari.utils.events.event import EmitterGroup, Event
from napari.utils.events.types import SupportsEvents
from naparixx import _native

if TYPE_CHECKING:
    from collections.abc import Mapping, Sequence


class EventedDict(TypedMutableMapping[_K, _T]):
    """Mutable dictionary that emits events when altered.

    This class is designed to behave exactly like builtin ``dict``, but
    will emit events before and after all mutations (addition, removal, and
    changing).

    Parameters
    ----------
    data : Mapping, optional
        Dictionary to initialize the class with.
    basetype : type of sequence of types, optional
        Type of the element in the dictionary.

    Events
    ------
    changing (key: K)
        emitted before an item at ``key`` is changed
    changed (key: K, old_value: T, value: T)
        emitted when item at ``key`` is changed from ``old_value`` to ``value``
    adding (key: K)
        emitted before an item is added to the dictionary with ``key``
    added (key: K, value: T)
        emitted after ``value`` was added to the dictionary with ``key``
    removing (key: K)
        emitted before ``key`` is removed from the dictionary
    removed (key: K, value: T)
        emitted after ``key`` was removed from the dictionary
    updated (key, K, value: T)
        emitted after ``value`` of ``key`` was changed. Only implemented by
        subclasses to give them an option to trigger some update after ``value``
        was changed and this class did not register it. This can be useful if
        the ``basetype`` is not an evented object.
    """

    events: EmitterGroup

    def __init__(
        self,
        data: "Mapping[_K, _T] | None" = None,
        basetype: "type[_T] | Sequence[type[_T]]" = (),
    ) -> None:
        _native.evented_mapping_init(self, data, basetype, super())

    def first_callback_connect(self) -> None:
        """When the first callback is connected to `self.events`,
        connect to all child emitters.
        """
        _native.evented_mapping_first_callback(self)

    def last_callback_disconnect(self) -> None:
        """When the last callback is disconnected from `self.events`, disconnect
        from all child emitters.
        """
        _native.evented_mapping_last_callback(self)

    def __setitem__(self, key: _K, value: _T) -> None:
        _native.evented_mapping_setitem(self, key, value, super())

    def __delitem__(self, key: _K) -> None:
        _native.evented_mapping_delitem(self, key)

    def _reemit_child_event(self, event: Event) -> None:
        """An item in the dict emitted an event.  Re-emit with key"""
        _native.evented_mapping_reemit(self, event)

    def _reemit_child_event_psygnal(self, event: EmissionInfo) -> None:
        _native.evented_mapping_reemit_psygnal(self, event)

    def _disconnect_child_emitters(self, child: _T) -> None:
        """Disconnect all events from the child from the re-emitter."""
        _native.evented_mapping_disconnect(self, child)

    def _connect_child_emitters(self, child: _T) -> None:
        """Connect all events from the child to be re-emitted."""
        _native.evented_mapping_connect(self, child)

    def key(self, value: _T) -> _K | None:
        """Return first instance of value."""
        return _native.evented_mapping_key(self, value)


class EventedDictNamespace(EventedDict[str, _T]):
    """An evented dict that also exposes its elements as a simple namespace.

    Needs keys to be str.
    """

    def __getattr__(self, name: str) -> _T:
        return _native.evented_namespace_getattr(self, name)

    def __setattr__(self, name: str, value: _T) -> None:
        _native.evented_namespace_setattr(self, name, value, super())

    def __dir__(self) -> list[str]:
        return _native.evented_namespace_dir(self, super())
