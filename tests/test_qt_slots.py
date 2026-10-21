"""Verify Qt metadata calls and handler ownership in a fresh interpreter."""

import os
import subprocess
import sys
import textwrap

import pytest


@pytest.mark.gui
def test_slot_metadata_dispatch_and_shutdown_ownership():
    pytest.importorskip("qtpy.QtCore")
    code = textwrap.dedent(
        '''
        import gc
        import inspect
        import weakref
        from qtpy import API_NAME
        from qtpy.QtCore import (
            Q_ARG, Q_RETURN_ARG, QCoreApplication, QEvent, QMetaObject,
            QObject, Qt, Signal,
        )
        from napari.utils.events import EventedModel
        from naparixx.qt_slot import Slot, retain_slot_implementations

        class Model(EventedModel):
            value: int = 0

        model = Model()

        @retain_slot_implementations
        class Receiver(QObject):
            changed = Signal(int)

            def __init__(self):
                super().__init__()
                self.value = 0
                self.changed.connect(
                    self.set_value, Qt.ConnectionType.QueuedConnection
                )

            @Slot(int)
            def set_value(self, value: int) -> None:
                """A typed handler."""
                self.value = value

            @Slot()
            def work(self):
                self.value = 10

            @staticmethod
            @Slot(int, result=int)
            def static_doubled(value: int) -> int:
                return value * 2

            @Slot(int, result=int)
            def doubled(self, value: int) -> int:
                return value * 2

            @Slot(int)
            @Slot(str)
            def overloaded(self, value):
                self.value = value

            @Slot(name="renamed")
            def aliased(self):
                self.value = 20

        class Child(Receiver):
            pass

        app = QCoreApplication.instance() or QCoreApplication([])
        receiver = Child()
        assert type(Receiver) is type(QObject)
        signature = str(inspect.signature(Receiver.set_value))
        assert signature == '(self, value: int) -> None'
        assert Receiver.set_value.__doc__ == 'A typed handler.'
        assert Receiver.set_value.__name__ == 'set_value'
        metadata = receiver.metaObject()
        for signature in ('set_value(int)', 'work()', 'overloaded(int)',
                          'overloaded(QString)', 'renamed()', 'doubled(int)'):
            assert metadata.indexOfSlot(signature) >= 0, signature
        receiver.changed.emit(3)
        assert receiver.value == 0
        app.processEvents()
        assert receiver.value == 3
        direct = Qt.ConnectionType.DirectConnection
        QMetaObject.invokeMethod(receiver, 'work', direct)
        assert receiver.value == 10
        # PySide6 6.11.2 also crashes here with an unmodified qtpy Slot:
        # its meta dispatcher resolves the renamed Python attribute recursively.
        # Check registered alias metadata and the callable on that binding.
        if API_NAME == 'PySide6':
            receiver.aliased()
        else:
            QMetaObject.invokeMethod(receiver, 'renamed', direct)
        assert receiver.value == 20
        returned = QMetaObject.invokeMethod(
            receiver, 'doubled', direct, Q_RETURN_ARG(int), Q_ARG(int, 7)
        )
        assert returned == 14
        assert receiver.doubled(7) == 14
        assert Receiver.static_doubled(7) == receiver.static_doubled(7) == 14
        receiver.overloaded('text')
        assert receiver.value == 'text'

        class_refs = [weakref.ref(cls) for cls in (Child, Receiver)]
        implementations = [weakref.ref(fn) for fn in Receiver._qt_slot_implementations]
        receiver.deleteLater()
        QCoreApplication.sendPostedEvents(None, QEvent.Type.DeferredDelete)
        del receiver, metadata, Child, Receiver
        gc.collect()
        assert all(reference() is None for reference in class_refs)
        assert all(reference() is None for reference in implementations)
        print('Qt metadata and handlers released')
        '''
    )
    result = subprocess.run(
        [sys.executable, "-c", code],
        capture_output=True,
        text=True,
        timeout=45,
        env=os.environ.copy(),
    )
    assert result.returncode == 0, result.stdout + result.stderr
    assert "Qt metadata and handlers released" in result.stdout
    assert "nanobind: leaked" not in result.stderr, result.stderr
