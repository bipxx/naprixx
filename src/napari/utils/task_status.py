from __future__ import annotations
from naparixx import _native

import datetime
import uuid
from enum import auto
from typing import TYPE_CHECKING, Optional

from napari.utils.misc import StringEnum

if TYPE_CHECKING:
    from collections.abc import Callable


class Status(StringEnum):
    PENDING = auto()
    BUSY = auto()
    COMPLETED = auto()
    CANCELLED = auto()
    FAILED = auto()
    START_FAILED = auto()


class TaskStatusItem:
    def __init__(
        self,
        provider: str,
        status: Status,
        description: str,
        cancel_callback: Optional[Callable] = None,
    ) -> None:
        self.id: uuid.UUID = uuid.uuid4()
        self._provider = provider
        self._timestamp = [self._timestap()]
        self._status = [status]
        self._description = [description]
        self._cancel_callback = cancel_callback

    def _timestap(self) -> str:
        return _native.viewer_close_task_timestamp(self, globals())

    def __str__(self) -> str:
        return f'TaskStatusItem: ({self._provider}, {self.id}, {self._timestamp[-1]}, {self._status[-1]}, {self._description[-1]})'

    def update(self, status: Status, description: str) -> None:
        _native.viewer_close_task_update(self, status, description, globals())

    def cancel(self) -> bool:
        return _native.viewer_close_task_cancel(self, globals(), '')

    def state(self) -> tuple[str, str, Status, str]:
        return _native.viewer_close_task_state(self, globals())


class TaskStatusManager:
    """
    A task status manager, to store status of long running processes/tasks.

    Only one instance is in general available through napari.

    napari methods and plugins can use it to register and update
    long running tasks.
    """

    _tasks: dict[uuid.UUID, TaskStatusItem]

    def __init__(self) -> None:
        # Note: we are using a dict here that may not be thread-safe; however
        # given the that the values from it are added/updated using an UUID
        # collision chances are low and it should be ok as long as operations
        # that require its iteration (`is_busy`, `get_status`, `cancel_all`)
        # are done when no task status additions are scheduled (i.e when
        # closing the application).
        self._tasks: dict[uuid.UUID, TaskStatusItem] = {}

    def register_task_status(
        self,
        provider: str,
        task_status: Status,
        description: str,
        cancel_callback: Optional[Callable] = None,
    ) -> uuid.UUID:
        item = TaskStatusItem(
            provider, task_status, description, cancel_callback
        )
        self._tasks[item.id] = item
        return item.id

    def update_task_status(
        self,
        status_id: uuid.UUID,
        task_status: Status,
        description: str = '',
    ) -> bool:
        if status_id in self._tasks:
            item = self._tasks[status_id]
            item.update(task_status, description)
            return True

        return False

    def is_busy(self) -> bool:
        locals_owner = _native.viewer_close_locals('task_busy', None)
        return _native.viewer_close_tasks_busy(self, globals(), locals_owner)

    def get_status(self) -> list[str]:
        locals_owner = _native.viewer_close_locals('task_status', None)
        return _native.viewer_close_tasks_status(self, globals(), locals_owner)

    def cancel_all(self) -> None:
        locals_owner = _native.viewer_close_locals('task_cancel', None)
        _native.viewer_close_tasks_cancel(self, globals(), locals_owner)
