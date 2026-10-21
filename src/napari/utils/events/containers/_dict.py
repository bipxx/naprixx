"""Evented dictionary"""

from collections.abc import Iterator, Mapping, MutableMapping, Sequence
from typing import (
    Any,
    TypeVar,
)

from naparixx import _native

_K = TypeVar("_K")
_T = TypeVar("_T")


class TypedMutableMapping(MutableMapping[_K, _T]):
    """Dictionary mixin that enforces item type."""

    def __init__(
        self,
        data: Mapping[_K, _T] | None = None,
        basetype: type[_T] | Sequence[type[_T]] = (),
    ) -> None:
        _native.typed_mapping_init(self, data, basetype)

    # #### START Required Abstract Methods

    def __setitem__(self, key: _K, value: _T) -> None:
        _native.typed_mapping_setitem(self, key, value)

    def __delitem__(self, key: _K) -> None:
        _native.typed_mapping_delitem(self, key)

    def __getitem__(self, key: _K) -> _T:
        return _native.typed_mapping_getitem(self, key)

    def __len__(self) -> int:
        return _native.typed_mapping_len(self)

    def __iter__(self) -> Iterator[_K]:
        return _native.typed_mapping_iter(self)

    def __repr__(self) -> str:
        return _native.typed_mapping_repr(self)

    def _type_check(self, e: Any) -> _T:
        return _native.typed_mapping_check(self, e)

    def __newlike__(
        self, iterable: MutableMapping[_K, _T]
    ) -> "TypedMutableMapping[_K, _T]":
        return _native.typed_mapping_newlike(self, iterable)

    def copy(self) -> "TypedMutableMapping[_K, _T]":
        """Return a shallow copy of the dictionary."""
        return _native.typed_mapping_copy(self)
