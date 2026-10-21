"""Python access to the C++ application core and the compatible napari API."""

from importlib import import_module

from . import _native as native  # noqa: F401 -- public re-export
from ._backend import NAPARI_VERSION as NAPARI_VERSION
from ._backend import backend_status as backend_status
from ._backend import disable as disable
from ._backend import enable as enable

__version__ = "0.3.0"
__all__ = sorted(
    set(import_module("napari").__all__)
    | {"NAPARI_VERSION", "backend_status", "disable", "enable", "native"}
)


def __getattr__(name):
    if name.startswith("_"):
        raise AttributeError(f"module {__name__!r} has no attribute {name!r}")
    napari = import_module("napari")
    # Resolve names before activation so misspelled attributes have no effects.
    value = getattr(napari, name)
    enable()
    return value


def __dir__():
    return sorted(set(globals()) | set(dir(import_module("napari"))))
