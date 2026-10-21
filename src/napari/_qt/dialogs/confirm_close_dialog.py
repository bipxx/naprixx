from naparixx import _native
from qtpy.QtCore import Qt
from qtpy.QtGui import QKeySequence
from qtpy.QtWidgets import (
    QCheckBox,
    QDialog,
    QHBoxLayout,
    QLabel,
    QPushButton,
    QVBoxLayout,
    QWidget,
)

from napari.settings import get_settings


class ConfirmCloseDialog(QDialog):
    def __init__(
        self,
        parent: QWidget | None,
        close_app: bool = False,
        display_checkbox: bool = True,
        extra_info: str = '',
    ) -> None:
        locals_owner = _native.viewer_close_locals('confirm_init', extra_info)
        del extra_info
        _native.viewer_close_confirm_init(
            self, parent, close_app, display_checkbox, globals(),
            lambda self: super(),
            ('\n\n', '', 'Cancel', 'Close', 'warning_icon_btn', 'Do not ask in future', 'Close Application?', "Do you want to close the application? ('", 'Ctrl+Q', "' to confirm). This will close all Qt Windows in this process", 'error_icon_btn', 'error_icon_element', 'Close Window?', "Confirm to close window (or press '", 'Ctrl+W', "')", 'warning_icon_element'), locals_owner
        )

    def accept(self) -> None:
        _native.viewer_close_confirm_accept(self, globals(), lambda self: super())
