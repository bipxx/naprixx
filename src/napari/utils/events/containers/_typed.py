import logging
from collections.abc import Callable, Iterable, MutableSequence, Sequence
from typing import (
    Any,
    Self,
    TypeVar,
    Union,
    overload,
)

from naparixx import _native

logger = logging.getLogger(__name__)


Index = Union[int, slice]

_T = TypeVar("_T")
_L = TypeVar("_L", bound=Any)


class TypedMutableSequence(_native.TransformNamespaceBase, MutableSequence[_T]):
    """List mixin that enforces item type, and enables custom indexing.

    Parameters
    ----------
    data : iterable, optional
        Elements to initialize the list with.
    basetype : type or sequence of types, optional
        Type of the elements in the list.  If a basetype (or multiple) is
        provided, then a TypeError will be raised when attempting to add an
        item to this sequence if it is not an instance of one of the types in
        ``basetype``.
    lookup : dict of Type[L] : function(object) -> L
        Mapping between a type, and a function that converts items in the list
        to that type.  This is used for custom indexing.  For example, if a
        ``lookup`` of {str: lambda x: x.name} is provided, then you can index
        into the list using ``list['frank']`` and it will search for an object
        whos attribute ``.name`` equals ``'frank'``.
    """

    def __init__(
        self,
        data: Iterable[_T] = (),
        *,
        basetype: type[_T] | Sequence[type[_T]] = (),
        lookup: dict[type[_L], Callable[[_T], _T | _L]] | None = None,
    ) -> None:
        self._list: list[_T]
        self._basetypes: tuple[type[_T], ...]
        locals_owner = _native.typed_container_locals('init', lookup, None, None)
        del lookup
        _native.sequence_init(self, data, basetype, globals(), locals_owner)

    def __len__(self) -> int:
        return _native.sequence_len(self, globals())

    def __repr__(self) -> str:
        return _native.sequence_repr(self, globals())

    def __eq__(self, other: object) -> bool:
        return _native.sequence_equal(self, other, globals())

    def __hash__(self) -> int:
        # it's important to add this to allow this object to be hashable
        # given that we've also reimplemented __eq__
        return _native.sequence_hash(self, globals())

    @overload
    def __setitem__(self, key: int, value: _T): ...  # pragma: no cover

    @overload
    def __setitem__(self, key: slice, value: Iterable[_T]): ...  # pragma: no cover

    def __setitem__(self, key, value):
        _native.sequence_setitem(
            self, key, value, globals(),
            'Can only assign an iterable to slice',
        )

    def insert(self, index: int, value: _T) -> None:
        _native.sequence_insert(self, index, value, globals())

    def __contains__(self, key: Any) -> bool:
        return _native.sequence_contains(
            self, key, globals(), lambda self: super()
        )

    @overload
    def __getitem__(self, key: str) -> _T: ...  # pragma: no cover

    @overload
    def __getitem__(self, key: int) -> _T: ...  # pragma: no cover

    @overload
    def __getitem__(
        self, key: slice
    ) -> "TypedMutableSequence[_T]": ...  # pragma: no cover

    def __getitem__(self, key):
        """Get an item from the list

        Parameters
        ----------
        key : int, slice, or any type in self._lookup
            The key to get.

        Returns
        -------
        The value at `key`

        Raises
        ------
        IndexError:
            If ``type(key)`` is not in ``self._lookup`` (usually an int, like a regular
            list), and the index is out of range.
        KeyError:
            If type(key) is in self._lookup and the key is not in the list (after)
            applying the self._lookup[key] function to each item in the list
        """
        locals_owner = _native.typed_container_locals('getitem', None, None, None)
        return _native.sequence_getitem(self, key, globals(), locals_owner)

    def __delitem__(self, key) -> None:
        locals_owner = _native.typed_container_locals('delete', None, None, None)
        _native.sequence_delitem(self, key, globals(), locals_owner)

    def _type_check(self, e: Any) -> _T:
        def generator_factory(iterator):
            yield from iterator

        return _native.sequence_type_check(
            self, globals(), lambda: e, generator_factory
        )

    def __newlike__(self, iterable: Iterable[_T]) -> "TypedMutableSequence[_T]":
        locals_owner = _native.typed_container_locals('newlike', None, None, None)
        return _native.sequence_newlike(self, iterable, globals(), locals_owner)

    def copy(self) -> "TypedMutableSequence[_T]":
        """Return a shallow copy of the list."""
        return _native.sequence_copy(self, globals())

    def __add__(self, other: Iterable[_T]) -> "TypedMutableSequence[_T]":
        """Add other to self, return new object."""
        locals_owner = _native.typed_container_locals('add', None, None, None)
        return _native.sequence_add(self, other, globals(), locals_owner)

    def __iadd__(self, other: Iterable[_T]) -> Self:
        """Add other to self in place (self += other)."""
        return _native.sequence_iadd(self, other, globals())

    def __radd__(self, other: list) -> list:
        """Add other to self in place (self += other)."""
        return _native.sequence_radd(self, other, globals())

    def index(self, value: _L, start: int = 0, stop: int | None = None) -> int:
        """Return first index of value.

        Parameters
        ----------
        value : Any
            A value to lookup.  If `type(value)` is in the lookups functions
            provided for this class, then values in the list will be searched
            using the corresponding lookup converter function.
        start : int, optional
            The starting index to search, by default 0
        stop : int, optional
            The ending index to search, by default None

        Returns
        -------
        int
            The index of the value

        Raises
        ------
        ValueError
            If the value is not present
        """
        locals_owner = _native.typed_container_locals('index', None, start, stop)
        del start, stop
        return _native.sequence_index(self, value, globals(), locals_owner)

    def _iter_indices(self, start: int = 0, stop: int | None = None) -> Iterable[int]:
        """Iter indices from start to stop.

        While this is trivial for this basic sequence type, this method lets
        subclasses (like NestableEventedList modify how they are traversed).
        """
        yield from _native.sequence_indices(self, start, stop, globals())

    def _ipython_key_completions_(self):
        def generator_factory(iterator):
            yield from iterator

        return _native.sequence_completions(
            globals(), lambda: self, generator_factory
        )


def _noop(x: _T) -> _T:
    return _native.sequence_noop(x)


def _make_key_completions(sequence, iterator):
    """Keep public generator protocol and references visible to Python GC."""
    yield from _native.sequence_completion_iterator(sequence, iterator)
