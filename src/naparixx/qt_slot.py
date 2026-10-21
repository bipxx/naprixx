"""Qt slot metadata with Python-visible ownership of application handlers.

Qt can retain metadata for a QObject class after its last instance is deleted.
The metadata trampoline holds only a weak reference to the implementation;
the Python class keeps the implementation alive while instances can use it.
This module deliberately imports no napari models or native state owners.
"""

import inspect
import weakref

from qtpy.QtCore import Slot as _QtSlot


def Slot(*types, **options):
    """Apply the binding's slot declaration to a weak metadata trampoline."""
    declare = _QtSlot(*types, **options)

    def decorate(function):
        if not hasattr(function, "_qt_slot_implementation"):
            reference = weakref.ref(function)

            def trampoline(*args, **kwargs):
                implementation = reference()
                if implementation is None:
                    raise RuntimeError("The Qt slot's Python class has been released")
                return implementation(*args, **kwargs)

            trampoline.__name__ = function.__name__
            trampoline.__qualname__ = function.__qualname__
            trampoline.__module__ = function.__module__
            trampoline.__doc__ = function.__doc__
            trampoline.__annotations__ = function.__annotations__.copy()
            trampoline.__signature__ = inspect.signature(function)
            trampoline._qt_slot_implementation = function
            function = trampoline
        return declare(function)

    return decorate


def retain_slot_implementations(cls):
    """Keep handlers alive on their existing class after Qt consumes metadata.

    Use this class decorator with :func:`Slot`. It leaves Qt's metaclass and
    the class identity unchanged, including for plugin-defined subclasses.
    """
    implementations = []
    for value in vars(cls).values():
        if isinstance(value, (staticmethod, classmethod)):
            value = value.__func__
        implementation = getattr(value, "_qt_slot_implementation", None)
        if implementation is not None:
            implementations.append(implementation)
            del value._qt_slot_implementation
    cls._qt_slot_implementations = tuple(implementations)
    return cls
