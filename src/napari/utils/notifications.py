from __future__ import annotations

from naparixx import _native

import logging
import os
import sys
import threading
import warnings
from collections.abc import Callable, Sequence
from datetime import datetime
from enum import auto
from typing import TYPE_CHECKING

from napari.utils.events import Event, EventEmitter
from napari.utils.misc import StringEnum

if TYPE_CHECKING:
    from types import TracebackType

name2num = {
    'error': 40,
    'warning': 30,
    'info': 20,
    'debug': 10,
    'none': 0,
}

__all__ = [
    'ErrorNotification',
    'Notification',
    'NotificationManager',
    'NotificationSeverity',
    'WarningNotification',
    'show_console_notification',
    'show_debug',
    'show_error',
    'show_info',
    'show_warning',
]


class NotificationSeverity(StringEnum):
    """Severity levels for the notification dialog.  Along with icons for each."""

    ERROR = auto()
    WARNING = auto()
    INFO = auto()
    DEBUG = auto()
    NONE = auto()

    def as_icon(self):
        return _native.notification_severity_icon(self, ('ⓧ', '⚠️', 'ⓘ', '🐛', ''))

    def __lt__(self, other):
        return _native.notification_severity_lt(self, other, globals())

    def __le__(self, other):
        return _native.notification_severity_le(self, other, globals())

    def __gt__(self, other):
        return _native.notification_severity_gt(self, other, globals())

    def __ge__(self, other):
        return _native.notification_severity_ge(self, other, globals())

    def __eq__(self, other):
        return _native.notification_severity_eq(self, other, globals())

    def __hash__(self):
        return _native.notification_severity_hash(self, globals())


ActionSequence = Sequence[tuple[str, Callable[[], None]]]


class Notification(_native.NotificationNamespaceBase, Event):
    """A Notifcation event.  Usually created by :class:`NotificationManager`.

    Parameters
    ----------
    message : str
        The main message/payload of the notification.
    severity : str or NotificationSeverity, optional
        The severity of the notification, by default
        `NotificationSeverity.WARNING`.
    actions : sequence of tuple, optional
        Where each tuple is a `(str, callable)` 2-tuple where the first item
        is a name for the action (which may, for example, be put on a button),
        and the callable is a callback to perform when the action is triggered.
        (for example, one might show a traceback dialog). by default ()
    """

    def __init__(
        self,
        message: str,
        severity: str | NotificationSeverity = NotificationSeverity.WARNING,
        actions: ActionSequence = (),
        **kwargs,
    ) -> None:
        _native.notification_init(self, message, severity, actions, kwargs, globals(), lambda self: super().__init__)

    @property
    def message(self):
        return _native.notification_message(self)

    @message.setter
    def message(self, value):
        _native.notification_set_message(self, value)

    @classmethod
    def from_exception(cls, exc: BaseException, **kwargs) -> Notification:
        return _native.notification_from_exception(exc, kwargs, globals())

    @classmethod
    def from_warning(cls, warning: Warning, **kwargs) -> Notification:
        return _native.notification_from_warning(warning, kwargs, globals())

    def __str__(self):
        return _native.notification_str(self, globals(), ': ')


class ErrorNotification(Notification):
    """
    Notification at an Error severity level.
    """

    exception: BaseException

    def __init__(self, exception: BaseException, *args, **kwargs) -> None:
        locals_owner = _native.notification_locals()
        _native.notification_error_init(self, exception, globals(), lambda self: super().__init__, ('message', 'actions', ()), locals_owner)

    def as_html(self):
        locals_owner = _native.notification_locals()
        return _native.notification_error_html(self, globals(), ('napari.utils._tracebacks', ('get_tb_formatter',)), locals_owner)

    def as_text(self):
        locals_owner = _native.notification_locals()
        return _native.notification_error_text(self, globals(), ('napari.utils._tracebacks', ('get_tb_formatter',), 'NoColor'), locals_owner)

    def __str__(self):
        locals_owner = _native.notification_locals()
        return _native.notification_error_str(self, globals(), ('napari.utils._tracebacks', ('get_tb_formatter',)), locals_owner)


class WarningNotification(Notification):
    """
    Notification at a Warning severity level.
    """

    warning: Warning

    def __init__(
        self, warning: Warning, filename=None, lineno=None, *args, **kwargs
    ) -> None:
        locals_owner = _native.notification_locals()
        _native.notification_warning_init(self, warning, filename, lineno, globals(), lambda self: super().__init__, ('message', 'actions', ()), locals_owner)

    def __str__(self):
        locals_owner = _native.notification_locals()
        return _native.notification_warning_str(self, globals(), (':', ': ', '!'), locals_owner)


class NotificationManager(_native.NotificationNamespaceBase):
    """
    A notification manager, to route all notifications through.

    Only one instance is in general available through napari; as we need
    notification to all flow to a single location that is registered with the
    sys.except_hook  and showwarning hook.

    This can and should be used a context manager; the context manager will
    properly re-entered, and install/remove hooks and keep them in a stack to
    restore them.

    While it might seem unnecessary to make it re-entrant; or to make the
    re-entrancy no-op; one need to consider that this could be used inside
    another context manager that modify except_hook and showwarning.

    Currently the original except and show warnings hooks are not called; but
    this could be changed in the future; this poses some questions with the
    re-entrency of the hooks themselves.
    """

    records: list[Notification]
    _instance: NotificationManager | None = None

    def __init__(self) -> None:
        self.records: list[Notification]
        self._originals_except_hooks: list[Callable]
        self._original_showwarnings_hooks: list[Callable]
        self._originals_thread_except_hooks: list[Callable]
        self._seen_warnings: set[tuple[str, type, str, int]]
        _native.notification_manager_init(self, globals(), ('NAPARI_EXIT_ON_ERROR', ('1', 'True'), 'NAPARI_CATCH_ERRORS', ('0', 'False')))

    def __enter__(self):
        return _native.notification_manager_enter(self)

    def __exit__(self, *args, **kwargs):
        _native.notification_manager_exit(self)

    def install_hooks(self):
        """
        Install a `sys.excepthook`, a `showwarning` hook and a
        threading.excepthook to display any message in the UI,
        storing the previous hooks to be restored if necessary.
        """
        # TODO: we might want to display the additional thread information
        _native.notification_install_hooks(self, globals())

    def restore_hooks(self):
        """
        Remove hooks installed by `install_hooks` and restore previous hooks.
        """
        _native.notification_restore_hooks(self, globals())

    def dispatch(self, notification: Notification):
        _native.notification_dispatch(self, notification)

    def receive_thread_error(
        self,
        args: tuple[
            type[BaseException],
            BaseException,
            TracebackType | None,
            threading.Thread | None,
        ],
    ):
        _native.notification_thread_error(self, args)

    def receive_error(
        self,
        exctype: type[BaseException],
        value: BaseException,
        traceback: TracebackType | None = None,
        thread: threading.Thread | None = None,
    ):
        _native.notification_receive_error(self, exctype, value, traceback, globals(), ('Closed by KeyboardInterrupt', 'Exit on error'))

    def receive_warning(
        self,
        message: Warning,
        category: type[Warning],
        filename: str,
        lineno: int,
        file=None,
        line=None,
    ):
        locals_owner = _native.notification_locals()
        _native.notification_receive_warning(self, message, category, filename, lineno, globals(), locals_owner)

    def receive_info(self, message: str):
        _native.notification_receive_info(self, message, globals(), 'INFO')


notification_manager = NotificationManager()


def show_debug(message: str):
    """
    Show a debug message in the notification manager.
    """
    _native.notification_show_debug(message, globals())


def show_info(message: str):
    """
    Show an info message in the notification manager.
    """
    _native.notification_show_info(message, globals())


def show_warning(message: str):
    """
    Show a warning in the notification manager.
    """
    _native.notification_show_warning(message, globals())


def show_error(message: str):
    """
    Show an error in the notification manager.
    """
    _native.notification_show_error(message, globals())


def show_console_notification(notification: Notification):
    """
    Show a notification in the console.
    """
    locals_owner = _native.notification_locals()
    _native.notification_console(notification, globals(), ('napari.settings', ('get_settings',), 'napari', 'An error occurred while trying to format an error and show it in console.\nYou can try to uninstall IPython to disable rich traceback formatting\nAnd/or report a bug to napari'), locals_owner)
