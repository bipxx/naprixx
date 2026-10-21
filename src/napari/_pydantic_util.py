from collections.abc import Collection
from types import NoneType, UnionType
from typing import (  # noqa
    Annotated,
    Any,
    Dict,
    List,
    Union,
    get_args,
    get_origin,
)

from pydantic import ConfigDict
from naparixx import _native


class NapariConfigDict(ConfigDict):
    """Custom ConfigDict for napari models.

    add dependencies attribute to track field dependencies
    """

    # FIXME: make it part of public api?
    dependencies: dict[str, Collection[str]] | None


def iter_inner_types(type_: Any) -> Any:
    yield from _native.iter_inner_types(type_)


def get_inner_type(type_: Any) -> Any:
    """Roughly replacing pydantic.v1 Field.type_"""
    return _native.get_inner_type(type_)


def get_outer_type(type_: Any) -> Any:
    """Roughly replacing pydantic.v1 Field.outer_type_"""
    return _native.get_outer_type(type_)


def is_list_type(type_: Any) -> bool:
    """Roughly replacing pydantic.v1 comparison to SHAPE_LIST"""
    return _native.is_list_type(type_)


__all__ = (
    'get_inner_type',
    'get_outer_type',
    'is_list_type',
    'iter_inner_types',
)
