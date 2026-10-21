"""Qt wrapper adapters for the opt-in C++ widget ownership prototype.

This host proves native/Python-widget interoperability. The napari Viewer
continues to use its existing controllers while those controllers are ported.
"""

import importlib
import weakref

from qtpy import API_NAME
from qtpy.QtCore import qVersion
from qtpy.QtWidgets import QApplication, QDockWidget, QMainWindow, QWidget

_SIP_MODULES = {"PyQt5": "PyQt5.sip", "PyQt6": "PyQt6.sip"}


def _wrapper_api():
    if API_NAME in _SIP_MODULES:
        return importlib.import_module(_SIP_MODULES[API_NAME])
    if API_NAME == "PySide6":
        return importlib.import_module("shiboken6")
    raise RuntimeError("The native Qt bridge supports PyQt5, PyQt6 and PySide6")


def _valid(widget):
    api = _wrapper_api()
    return (
        not api.isdeleted(widget) if API_NAME in _SIP_MODULES else api.isValid(widget)
    )


def _pointer(widget):
    if not isinstance(widget, QWidget):
        raise TypeError("a QWidget from the active Qt binding is required")
    if not _valid(widget):
        raise RuntimeError("the QWidget has already been deleted")
    api = _wrapper_api()
    if API_NAME in _SIP_MODULES:
        return int(api.unwrapinstance(widget))
    return int(api.getCppPointer(widget)[0])


def _wrap(pointer, wrapper_type):
    api = _wrapper_api()
    if API_NAME in _SIP_MODULES:
        wrapper = api.wrapinstance(pointer, wrapper_type)
        api.transferto(wrapper, None)
        return wrapper
    wrapper = api.wrapInstance(pointer, wrapper_type)
    reference = weakref.ref(wrapper)

    def invalidate_wrapper(*_arguments):
        instance = reference()
        if instance is not None:
            api.invalidate(instance)

    # Native Qt objects lack Shiboken-generated destructor hooks.
    wrapper.destroyed.connect(invalidate_wrapper)
    return wrapper


def _parent_ownership(widget, parent):
    """Update foreign binding metadata after a native parenting operation."""
    api = _wrapper_api()
    if API_NAME in _SIP_MODULES:
        if parent is None:
            api.transferback(widget)
        else:
            api.transferto(widget, parent)
    else:
        # Shiboken updates ownership through the generated Qt parent adapter.
        # On attachment, C++ has already set this same Qt parent.
        widget.setParent(parent)


class NativeWidgetHost:
    """C++ window hosting Python plugin QWidgets and a rendering canvas.

    The extension must be built with NAPARIXX_BUILD_QT/BUILD_QT5 and a Qt SDK
    matching the active binding's runtime. Attached widgets are owned by the
    native window; removing a widget transfers its ownership back to Python.
    """

    def __init__(self):
        _wrapper_api()
        # Import the Python Qt binding first so it selects the runtime DLLs.
        try:
            module_name = "._qt5_native" if API_NAME == "PyQt5" else "._qt_native"
            _qt_native = importlib.import_module(module_name, package=__package__)
        except ImportError as error:
            raise RuntimeError(
                "Build the native Qt bridge with NAPARIXX_BUILD_QT=ON (Qt6) "
                "or NAPARIXX_BUILD_QT5=ON (Qt5) and a matching Qt SDK"
            ) from error
        if qVersion() != _qt_native.qt_build_version:
            raise RuntimeError(
                f"Native Qt SDK {_qt_native.qt_build_version} differs from "
                f"the active Qt runtime {qVersion()}"
            )
        self._application = QApplication.instance()
        self._host = _qt_native.WidgetHost()
        self._window = _wrap(self._host.window_address, QMainWindow)
        self._docks = {}
        self._canvas = None

    @property
    def window(self):
        """Return the active binding's wrapper for the native QMainWindow."""
        if not self._host.window_address:
            raise RuntimeError("the native Qt host is closed")
        return self._window

    @property
    def dock_count(self):
        return self._host.dock_count

    @property
    def closed(self):
        return not self._host.window_address

    def add_plugin_widget(self, widget, title="Plugin", area="right"):
        token = self._host.add_dock(_pointer(widget), title, area)
        dock = _wrap(self._host.dock_address(token), QDockWidget)
        self._docks[token] = (widget, dock)
        _parent_ownership(widget, dock)
        return token, dock

    def remove_plugin_widget(self, token):
        widget, _dock = self._docks[token]
        # Release foreign ownership association before deleting its old owner.
        if _valid(widget):
            _parent_ownership(widget, None)
        pointer = self._host.detach_dock(token)
        del self._docks[token]
        return widget if pointer else None

    def attach_canvas(self, widget):
        self._host.set_canvas(_pointer(widget))
        self._canvas = widget
        _parent_ownership(widget, self._window)

    def take_canvas(self):
        widget = self._canvas
        if widget is not None and _valid(widget):
            _parent_ownership(widget, None)
        pointer = self._host.take_canvas()
        self._canvas = None
        return widget if pointer else None

    def resize(self, width, height):
        self._host.resize(width, height)

    def show(self):
        self._host.show()

    def close(self):
        if not self._host.close():
            return False
        self._docks.clear()
        self._canvas = None
        return True

    def __enter__(self):
        return self

    def __exit__(self, *_error):
        self.close()
