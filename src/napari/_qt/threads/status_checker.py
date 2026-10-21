"""A performant, dedicated thread to compute cursor status and signal updates to a viewer."""

from __future__ import annotations
from naparixx import _native

import os
from threading import Event
from typing import TYPE_CHECKING
from weakref import ref

from qtpy.QtCore import QObject, QThread, Signal

from napari.utils.notifications import Notification, notification_manager

if TYPE_CHECKING:
    from napari.components import ViewerModel


class StatusChecker(QThread):
    """A dedicated thread for performant updating of the status bar.

    This class offloads the job of computing the cursor status into a separate thread,
    Qt Signals are used to update the main viewer with the status string.

    Prior to https://github.com/napari/napari/pull/7146, status bar updates
    happened on the main thread in the viewer model, which could be
    expensive in some layers and resulted in bad performance when some
    layers were selected.

    Because the thread runs a single infinite while loop, the updates are
    naturally throttled since they can only be sent at the rate which updates
    can be computed, but no faster.

    Attributes
    ----------
    _need_status_update : threading.Event
        An Event (fancy thread-safe bool-like to synchronize threads)
        for keeping track of when the status needs updating
        (because the cursor has moved).
    _terminate : bool
        If set to True, the status checker thread needs to be terminated.
        When the QtViewer is being closed, it sets this flag to terminate
        the status checker thread.
        After _terminate is set to True, no more status updates are sent.
        Default: False.
    viewer_ref : weakref.ref[napari.viewer.ViewerModel]
        A weak reference to the viewer which is providing status updates.
        We keep a weak reference to the viewer so the status checker thread
        will not prevent the viewer from being garbage collected.
        We proactively check the viewer to determine if a new status update
        needs to be computed and emitted.
    """

    # Create a Signal to establish a lightweight communication mechanism between the
    # viewer and the status checker thread for cursor events and related status
    status_and_tooltip_changed = Signal(object)

    def __init__(self, viewer: ViewerModel, parent: QObject | None = None):
        _native.viewer_status_init(self, viewer, parent, globals(), lambda self: super(), 'StatusChecker')

    def trigger_status_update(self) -> None:
        """Trigger a status update computation.

        When the cursor moves, the viewer will call this to instruct
        the status checker to update the viewer with the present status.
        """
        _native.viewer_status_trigger(self, globals())

    def close_terminate(self) -> None:
        """Close the status checker thread.

        This method is called when the viewer is closed.
        """
        _native.viewer_close_status(self, globals())

    def terminate(self) -> None:
        """Terminate the status checker thread.

        For proper cleanup,it's important to set _terminate to True before
        calling _needs_status_update.set.
        """
        _native.viewer_close_terminate_status(self, globals())

    def start(
        self, priority: QThread.Priority = QThread.Priority.InheritPriority
    ) -> None:
        """Start the status checker thread.

        Make sure to set the _terminate attribute to False prior to start.
        """
        _native.viewer_close_start_status(self, priority, globals(), lambda self: super())

    def run(self) -> None:
        _native.viewer_status_run(self, globals())

    def calculate_status(self) -> None:
        """Calculate the status and emit the signal.

        If the viewer is not available, do nothing. Otherwise,
        emit the signal that the status has changed.
        """
        locals_owner = _native.viewer_status_locals()
        _native.viewer_status_calculate(self, globals(), locals_owner)


def _viewer_status_asv_factory():
    return lambda self, priority=0: None

_native.viewer_status_asv(globals(), _viewer_status_asv_factory, 'ASV', 'true')
