# Copyright (c) Vispy Development Team. All Rights Reserved.
# Distributed under the (new) BSD License. See LICENSE.txt for more info.

# # LICENSE.txt
# Vispy licensing terms
# ---------------------

# Vispy is licensed under the terms of the (new) BSD license:
#
# Copyright (c) 2013-2017, Vispy Development Team. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
# * Redistributions of source code must retain the above copyright
#   notice, this list of conditions and the following disclaimer.
# * Redistributions in binary form must reproduce the above copyright
#   notice, this list of conditions and the following disclaimer in the
#   documentation and/or other materials provided with the distribution.
# * Neither the name of Vispy Development Team nor the names of its
#   contributors may be used to endorse or promote products
#   derived from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
# IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
# TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
# PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
# OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
# PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
# LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
# NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
#
# Exceptions
# ----------
#
# The examples code in the examples directory can be considered public
# domain, unless otherwise indicated in the corresponding source file.

"""
The event module implements the classes that make up the event system.
The Event class and its subclasses are used to represent "stuff that happens".
The EventEmitter class provides an interface to connect to events and
to emit events. The EmitterGroup groups EventEmitter objects.

For more information see http://github.com/vispy/vispy/wiki/API_Events

"""

import inspect
import os
import warnings
import weakref
from collections.abc import Callable, Iterable, Iterator, Sequence
from functools import partial
from typing import (
    Any,
    Generic,
    Literal,
    Optional,
    TypeVar,
    Union,
    cast,
)

from vispy.util.logs import _handle_exception

from naparixx import _native


class Event:
    """Class describing events that occur and can be reacted to with callbacks.
    Each event instance contains information about a single event that has
    occurred such as a key press, mouse motion, timer activation, etc.

    Subclasses: :class:`KeyEvent`, :class:`MouseEvent`, :class:`TouchEvent`,
    :class:`StylusEvent`

    The creation of events and passing of events to the appropriate callback
    functions is the responsibility of :class:`EventEmitter` instances.

    Note that each event object has an attribute for each of the input
    arguments listed below.

    Parameters
    ----------
    type : str
       String indicating the event type (e.g. mouse_press, key_release)
    native : object (optional)
       The native GUI event object
    **kwargs : keyword arguments
        All extra keyword arguments become attributes of the event object.
    """

    def __init__(
        self, type_name: str, native: Any = None, **kwargs: Any
    ) -> None:
        # stack of all sources this event has been emitted through
        _native.event_initialize(self, type_name, native, kwargs)

    @property
    def source(self) -> Any:
        """The object that the event applies to (i.e. the source of the event)."""
        return _native.event_get(self, "source")

    @property
    def sources(self) -> list[Any]:
        """List of objects that the event applies to (i.e. are or have
        been a source of the event). Can contain multiple objects in case
        the event traverses a hierarchy of objects.
        """
        return _native.event_get(self, "sources")

    def _push_source(self, source: Any) -> None:
        _native.event_push_source(self, source)

    def _pop_source(self):
        return _native.event_pop_source(self)

    @property
    def type(self) -> str:
        # No docstring; documented in class docstring
        return _native.event_get(self, "type")

    @property
    def native(self) -> Any:
        # No docstring; documented in class docstring
        return _native.event_get(self, "native")

    @property
    def handled(self) -> bool:
        """This boolean property indicates whether the event has already been
        acted on by an event handler. Since many handlers may have access to
        the same events, it is recommended that each check whether the event
        has already been handled as well as set handled=True if it decides to
        act on the event.
        """
        return _native.event_get(self, "handled")

    @handled.setter
    def handled(self, val) -> None:
        _native.event_set_flag(self, "handled", val)

    @property
    def blocked(self) -> bool:
        """This boolean property indicates whether the event will be delivered
        to event callbacks. If it is set to True, then no further callbacks
        will receive the event. When possible, it is recommended to use
        Event.handled rather than Event.blocked.
        """
        return _native.event_get(self, "blocked")

    @blocked.setter
    def blocked(self, val) -> None:
        _native.event_set_flag(self, "blocked", val)

    def __repr__(self) -> str:
        # Try to generate a nice string representation of the event that
        # includes the interesting properties.
        # need to keep track of depth because it is
        # very difficult to avoid excessive recursion.
        return _native.event_repr(self)

    def __str__(self) -> str:
        """Shorter string representation"""
        return _native.event_object_string(self)

    # pyrefly fix for dynamic attribute access
    def __getattr__(self, name: str) -> Any:
        return _native.event_object_getattribute(self, name, globals())


_event_repr_depth = 0


Callback = Union[Callable[[Event], None], Callable[[], None]]
CallbackRef = tuple['weakref.ReferenceType[Any]', str]  # dereferenced method
CallbackStr = tuple[
    Union['weakref.ReferenceType[Any]', object], str
]  # dereferenced method


_T = TypeVar('_T')


class _WeakCounter(Generic[_T]):
    """
    Similar to collection counter but has weak keys.

    It will only implement the methods we use here.
    """

    def __init__(self) -> None:
        _native.event_counter_initialize(self)

    def update(self, iterable: Iterable[_T]):
        _native.event_counter_update(self, iterable)

    def get(self, key: _T, default: int) -> int:
        return _native.event_counter_get(self, key, default)


class EventEmitter(_native.EventEmitterBase):
    """Encapsulates a list of event callbacks.

    Each instance of EventEmitter represents the source of a stream of similar
    events, such as mouse click events or timer activation events. For
    example, the following diagram shows the propagation of a mouse click
    event to the list of callbacks that are registered to listen for that
    event::

       User clicks    |Canvas creates
       mouse on       |MouseEvent:                |'mouse_press' EventEmitter:         |callbacks in sequence: # noqa
       Canvas         |                           |                                    |  # noqa
                   -->|event = MouseEvent(...) -->|Canvas.events.mouse_press(event) -->|callback1(event)  # noqa
                      |                           |                                 -->|callback2(event)  # noqa
                      |                           |                                 -->|callback3(event)  # noqa

    Callback functions may be added or removed from an EventEmitter using
    :func:`connect() <vispy.event.EventEmitter.connect>` or
    :func:`disconnect() <vispy.event.EventEmitter.disconnect>`.

    Calling an instance of EventEmitter will cause each of its callbacks
    to be invoked in sequence. All callbacks are invoked with a single
    argument which will be an instance of :class:`Event <vispy.event.Event>`.

    EventEmitters are generally created by an EmitterGroup instance.

    Parameters
    ----------
    source : object
        The object that the generated events apply to. All emitted Events will
        have their .source property set to this value.
    type_name: str or None
        String indicating the event type (e.g. mouse_press, key_release)
    event_class : subclass of Event
        The class of events that this emitter will generate.
    """

    def __init__(
        self,
        source: Any = None,
        type_name: str | None = None,
        event_class: type[Event] = Event,
    ) -> None:
        # connected callbacks
        _native.event_emitter_initialize(self, source, type_name, event_class)

    @property
    def ignore_callback_errors(self) -> bool:
        """Whether exceptions during callbacks will be caught by the emitter

        This allows it to continue invoking other callbacks if an error
        occurs.
        """
        return _native.event_emitter_get(self, "ignore_callback_errors")

    @ignore_callback_errors.setter
    def ignore_callback_errors(self, val: bool):
        _native.event_emitter_set(self, "ignore_callback_errors", val)

    @property
    def print_callback_errors(self) -> str:
        """Print a message and stack trace if a callback raises an exception

        Valid values are "first" (only show first instance), "reminders" (show
        complete first instance, then counts), "always" (always show full
        traceback), or "never".

        This assumes ignore_callback_errors=True. These will be raised as
        warnings, so ensure that the vispy logging level is set to at
        least "warning".
        """
        return _native.event_emitter_get(self, "print_callback_errors")

    @print_callback_errors.setter
    def print_callback_errors(
        self,
        val: Literal['first', 'reminders', 'always', 'never'],
    ):
        _native.event_emitter_set(self, "print_callback_errors", val)

    @property
    def callback_refs(self) -> tuple[str | None, ...]:
        """The set of callback references"""
        return _native.event_emitter_get(self, "callback_refs")

    @property
    def callbacks(self) -> tuple[Callback | CallbackRef, ...]:
        """The set of callbacks"""
        return _native.event_emitter_get(self, "callbacks")

    @property
    def source(self) -> Any:
        """The object that events generated by this emitter apply to"""
        return _native.event_emitter_get(self, "source")

    @source.setter
    def source(self, s):
        _native.event_emitter_set(self, "source", s)

    def _is_core_callback(self, callback: CallbackRef | Callback, core: str):
        """
        Check if the callback is a core callback

        Parameters
        ----------
        callback : Union[CallbackRef, Callback]
            The callback to check. Callback could be function or
            weak reference to object method coded using weakreference
            to object and method name stored in tuple.
        core : str
            Name of core module, for example 'napari'.
        """
        return _native.event_is_core_callback(callback, core)

    def connect(
        self,
        callback: Union[Callback, CallbackRef, CallbackStr, 'EventEmitter'],
        ref: bool | str = False,
        position: Literal['first', 'last'] = 'last',
        before: str | Callback | list[str | Callback] | None = None,
        after: str | Callback | list[str | Callback] | None = None,
        until: Optional['EventEmitter'] = None,
    ):
        """Connect this emitter to a new callback.

        Parameters
        ----------
        callback : function | tuple
            *callback* may be either a callable object or a tuple
            (object, attr_name) where object.attr_name will point to a
            callable object. Note that only a weak reference to ``object``
            will be kept.
        ref : bool | str
            Reference used to identify the callback in ``before``/``after``.
            If True, the callback ref will automatically determined (see
            Notes). If False, the callback cannot be referred to by a string.
            If str, the given string will be used. Note that if ``ref``
            is not unique in ``callback_refs``, an error will be thrown.
        position : str
            If ``'first'``, the first eligible position is used (that
            meets the before and after criteria), ``'last'`` will use
            the last position.
        before : str | callback | list of str or callback | None
            List of callbacks that the current callback should precede.
            Can be None if no before-criteria should be used.
        after : str | callback | list of str or callback | None
            List of callbacks that the current callback should follow.
            Can be None if no after-criteria should be used.
        until : optional eventEmitter
            if provided, when the event `until` is emitted, `callback`
            will be disconnected from this emitter.

        Notes
        -----
        If ``ref=True``, the callback reference will be determined from:

            1. If ``callback`` is ``tuple``, the second element in the tuple.
            2. The ``__name__`` attribute.
            3. The ``__class__.__name__`` attribute.

        The current list of callback refs can be obtained using
        ``event.callback_refs``. Callbacks can be referred to by either
        their string reference (if given), or by the actual callback that
        was attached (e.g., ``(canvas, 'swap_buffers')``).

        If the specified callback is already connected, then the request is
        ignored.

        If before is None and after is None (default), the new callback will
        be added to the beginning of the callback list. Thus the
        callback that is connected _last_ will be the _first_ to receive
        events from the emitter.
        """
        return _native.event_connect(self, callback, ref, position, before, after, until)

    def disconnect(
        self, callback: Callback | CallbackRef | object | None = None
    ):
        """Disconnect a callback from this emitter.

        If no callback is specified, then *all* callbacks are removed.
        If the callback was not already connected, then the call does nothing.
        """
        _native.event_disconnect(self, callback)

    @staticmethod
    def _get_proper_name(callback):
        return _native.event_proper_callback_name(callback)

    @staticmethod
    def _check_signature(fun: Callable) -> bool:
        """
        Check if function will accept event parameter
        """
        return _native.event_check_signature(fun)

    def _normalize_cb(self, callback) -> tuple[CallbackRef | Callback, bool]:
        # dereference methods into a (self, method_name) pair so that we can
        # make the connection without making a strong reference to the
        # instance.
        return _native.event_normalize_callback(self, callback)

    def __call__(self, *args, **kwargs) -> Event:
        """__call__(**kwargs)
        Invoke all callbacks for this emitter.

        Emit a new event object, created with the given keyword
        arguments, which must match with the input arguments of the
        corresponding event class. Note that the 'type' argument is
        filled in by the emitter.

        Alternatively, the emitter can also be called with an Event
        instance as the only argument. In this case, the specified
        Event will be used rather than generating a new one. This allows
        customized Event instances to be emitted and also allows EventEmitters
        to be chained by connecting one directly to another.

        Note that the same Event instance is sent to all callbacks.
        This allows some level of communication between the callbacks
        (notably, via Event.handled) but also requires that callbacks
        be careful not to inadvertently modify the Event.
        """
        # This is a VERY highly used method; must be fast!
        return _native.event_emit(self, args, kwargs)

    def _invoke_callback(
        self, cb: Callback | Callable[[], None], event: Event | None
    ):
        _native.event_invoke_callback(self, cb, event)

    def _prepare_event(self, *args, **kwargs) -> Event:
        # When emitting, this method is called to create or otherwise alter
        # an event before it is sent to callbacks. Subclasses may extend
        # this method to make custom modifications to the event.
        return _native.event_prepare(self, args, kwargs)

    def blocked(self, callback: Callback | None = None) -> bool:
        """Return boolean indicating whether the emitter is blocked for
        the given callback.
        """
        return _native.event_is_blocked(self, callback)

    def block(self, callback: Callback | None = None):
        """Block this emitter. Any attempts to emit an event while blocked
        will be silently ignored. If *callback* is given, then the emitter
        is only blocked for that specific callback.

        Calls to block are cumulative; the emitter must be unblocked the same
        number of times as it is blocked.
        """
        _native.event_block(self, callback)

    def unblock(self, callback: Callback | None = None):
        """Unblock this emitter. See :func:`event.EventEmitter.block`.

        Note: Use of ``unblock(None)`` only reverses the effect of
        ``block(None)``; it does not unblock callbacks that were explicitly
        blocked using ``block(callback)``.
        """
        _native.event_unblock(self, callback)

    def blocker(self, callback: Callback | None = None):
        """Return an EventBlocker to be used in 'with' statements

        Notes
        -----
        For example, one could do::

            with emitter.blocker():
                pass  # ..do stuff; no events will be emitted..
        """
        return _native.event_make_blocker(self, callback)


class WarningEmitter(EventEmitter):
    """
    EventEmitter subclass used to allow deprecated events to be used with a
    warning message.
    """

    def __init__(
        self,
        message: str,
        category: type[Warning] = FutureWarning,
        stacklevel: int = 3,
        *args,
        **kwargs,
    ) -> None:
        _native.event_warning_initialize(self, __class__, message, category, stacklevel, args, kwargs)

    def connect(self, cb, *args, **kwargs):
        return _native.event_warning_connect(self, cb, args, kwargs)

    def _invoke_callback(self, cb, event):
        """invoke callback with warn if not warned yet."""
        # check if this is needed. The _invoke_callback is not called if there is a callback.
        # but if there is a callback, then _warn was called in the connect.
        # And warn emmit a warning only once.
        return _native.event_warning_invoke(self, cb, event)

    def _warn(self, cb):
        _native.event_warning_warn(self, cb)


class RenamedWarningEmitter(WarningEmitter):
    """
    Warning emitter to be used when an attribute was renamed or moved to a composition object.
    It will connect to the new event once the callback is connected to the old one.
    It will also warn the user that the attribute was renamed.
    """

    def __init__(
        self,
        new_name: str,
        *args,
        **kwargs,
    ) -> None:
        _native.event_renamed_initialize(self, __class__, new_name, args, kwargs)

    def _get_new_emitter(self):
        return _native.event_renamed_target(self)

    def connect(self, cb, *args, **kwargs):
        _native.event_renamed_connect(self, __class__, cb, args, kwargs)

    def disconnect(
        self, callback: Callback | CallbackRef | object | None = None
    ):
        _native.event_renamed_disconnect(self, __class__, callback)


class EmitterGroup(EventEmitter):
    """EmitterGroup instances manage a set of related
    :class:`EventEmitters <vispy.event.EventEmitter>`.
    Its primary purpose is to provide organization for objects
    that make use of multiple emitters and to reduce the boilerplate code
    needed to initialize those emitters with default connections.

    EmitterGroup instances are usually stored as an 'events' attribute on
    objects that use multiple emitters. For example::

         EmitterGroup  EventEmitter
                 |       |
        Canvas.events.mouse_press
        Canvas.events.resized
        Canvas.events.key_press

    EmitterGroup is also a subclass of :class:`EventEmitters`,
    allowing it to emit its own events.
    Any callback that connects directly to the EmitterGroup will
    receive *all* of the events generated by the group's emitters.

    When connecting a first callback to an EmitterGroup, the group will
    check if a source object has a ` first_callback_connect ` method, and if so,
    it will call it. When the last callback is disconnected, the group will
    check if a source object has the ` last_callback_disconnect ` method, and
    if so, it will call it.

    Parameters
    ----------
    source : object
        The object that the generated events apply to.
    auto_connect : bool
        If *auto_connect* is True, then one connection will
        be made for each emitter that looks like
        :func:`emitter.connect((source, 'on_' + event_name))
        <vispy.event.EventEmitter.connect>`.
        This provides a simple mechanism for automatically connecting a large
        group of emitters to default callbacks.  By default, false.
    emitters : keyword arguments
        See the :func:`add <vispy.event.EmitterGroup.add>` method.
    """

    def __init__(
        self,
        source: Any = None,
        auto_connect: bool = False,
        _connect_children: bool = True,
        **emitters: type[Event] | EventEmitter | None,
    ) -> None:
        _native.event_group_initialize(self, source, auto_connect, _connect_children, emitters)

    def __getattr__(self, name) -> EventEmitter:
        return object.__getattribute__(self, name)

    def __getitem__(self, name: str) -> EventEmitter:
        """
        Return the emitter assigned to the specified name.
        Note that emitters may also be retrieved as an attribute of the
        EmitterGroup.
        """
        return _native.event_group_get(self, name)

    def __setitem__(
        self, name: str, emitter: type[Event] | EventEmitter | None
    ):
        """
        Alias for EmitterGroup.add(name=emitter)
        """
        _native.event_group_set(self, name, emitter)

    def add(
        self,
        auto_connect: bool | None = None,
        **kwargs: type[Event] | EventEmitter | None,
    ):
        """Add one or more EventEmitter instances to this emitter group.
        Each keyword argument may be specified as either an EventEmitter
        instance or an Event subclass, in which case an EventEmitter will be
        generated automatically::

            # This statement:
            group.add(mouse_press=MouseEvent,
                      mouse_release=MouseEvent)

            # ..is equivalent to this statement:
            group.add(mouse_press=EventEmitter(group.source, 'mouse_press',
                                               MouseEvent),
                      mouse_release=EventEmitter(group.source, 'mouse_press',
                                                 MouseEvent))
        """
        _native.event_group_add(self, auto_connect, kwargs)

    @property
    def emitters(self) -> dict[str, EventEmitter]:
        """List of current emitters in this group."""
        return _native.event_group_emitters(self)

    def __iter__(self) -> Iterator[str]:
        """
        Iterates over the names of emitters in this group.
        """
        yield from _native.event_group_iter(self)

    def block_all(self):
        """
        Block all emitters in this group by increase counter of semaphores for each event emitter
        """
        _native.event_group_block_all(self)

    def unblock_all(self):
        """
        Unblock all emitters in this group, by decrease counter of semaphores for each event emitter.
        if block is called twice and unblock is called once, then events will be still blocked.
        See `Semaphore (programming) <https://en.wikipedia.org/wiki/Semaphore_(programming)>`__.
        """
        _native.event_group_unblock_all(self)

    def connect(
        self,
        callback: Union[Callback, CallbackRef, EventEmitter, 'EmitterGroup'],
        ref: bool | str = False,
        position: Literal['first', 'last'] = 'first',
        before: str | Callback | list[str | Callback] | None = None,
        after: str | Callback | list[str | Callback] | None = None,
    ):
        """Connect the callback to the event group. The callback will receive
        events from *all* of the emitters in the group.

        If this is the first callback to be connected to the group, then
        `EventEmitter` connects to all sub-emitters in the group.

        It also cheks if the source has a `first_callback_connect`
        method, and if so, it calls it.
        """
        return _native.event_group_connect(self, callback, ref, position, before, after)

    def disconnect(self, callback: Callback | None = None):
        """Disconnect the callback from this group. See
        :func:`connect()` and :func:`EventEmitter.connect()`` for
        more information.

        If this is the last callback to be disconnected from the group, then
        `EventEmitter` disconnects from all sub-emitters in the group.

        If the source has a `last_callback_disconnect` method,
        then it is called too.
        """
        return _native.event_group_disconnect(self, callback)

    def _connect_emitters(self, connect):
        # Connect/disconnect all sub-emitters from the group. This allows the
        # group to emit an event whenever _any_ of the sub-emitters emit,
        # while simultaneously eliminating the overhead if nobody is listening.
        _native.event_group_connect_children(self, connect)

    @property
    def ignore_callback_errors(self):
        return _native.event_group_get_errors(self, __class__)

    @ignore_callback_errors.setter
    def ignore_callback_errors(self, ignore):
        _native.event_group_set_errors(self, ignore)

    def blocker_all(self) -> 'EventBlockerAll':
        """Return an EventBlockerAll to be used in 'with' statements

        Notes
        -----
        For example, one could do::

            with emitter.blocker_all():
                pass  # ..do stuff; no events will be emitted..
        """
        return _native.event_make_blocker_all(self)


class EventBlocker:
    """Represents a block for an EventEmitter to be used in a context
    manager (i.e. 'with' statement).
    """

    def __init__(self, target: EventEmitter, callback=None) -> None:
        _native.event_blocker_initialize(self, target, callback)

    @property
    def count(self):
        return _native.event_blocker_count(self)

    def __enter__(self):
        return _native.event_blocker_enter(self)

    def __exit__(self, *args):
        _native.event_blocker_exit(self)


class EventBlockerAll:
    """Represents a block_all for an EmitterGroup to be used in a context
    manager (i.e. 'with' statement).
    """

    def __init__(self, target: EmitterGroup) -> None:
        _native.event_blocker_all_initialize(self, target)

    def __enter__(self):
        _native.event_blocker_all_enter(self)

    def __exit__(self, *args):
        _native.event_blocker_all_exit(self)


def _is_pos_arg(param: inspect.Parameter):
    """
    Check if param is positional or named and has no default parameter.
    """
    return _native.event_positional_parameter(param)


def _noop(*a, **k):
    pass


_log_event_stack = _noop


def set_event_tracing_enabled(enabled=True, cfg=None):
    _native.event_tracing_set(enabled, cfg)


_native.event_tracing_environment()
