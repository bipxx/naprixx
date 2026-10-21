from __future__ import annotations

from naparixx import native as _native

from collections.abc import Sequence
from contextlib import suppress
from typing import (
    Union,
    overload,
)

import pint

UnitsLike = Union[None, str, pint.Unit, Sequence[str | pint.Unit]]
UnitsInfo = Union[None, pint.Unit, tuple[pint.Unit, ...]]


__all__ = (
    'UnitsInfo',
    'UnitsLike',
    'get_units_from_name',
)


def get_unit_from_name(unit: str | pint.Unit | None) -> pint.Unit:
    return _native.unit_from_name(unit)


@overload
def get_units_from_name(units: None) -> None: ...


@overload
def get_units_from_name(units: str | pint.Unit | None) -> pint.Unit: ...


@overload
def get_units_from_name(
    units: Sequence[str | pint.Unit | None],
) -> tuple[pint.Unit, ...]: ...


def get_units_from_name(units: UnitsLike) -> UnitsInfo:
    """Convert a string or sequence of strings to pint units."""
    return _native.units_from_name(units)
