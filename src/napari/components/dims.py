import contextlib
from collections.abc import Sequence
from typing import (
    Any,
    Literal,
    NamedTuple,
)

import pint
from pydantic import ConfigDict, PrivateAttr, field_validator, model_validator

from naparixx import _native

from napari.utils.events import EventedModel
from napari.utils.misc import reorder_after_dim_reduction


class RangeTuple(NamedTuple):
    start: float
    stop: float
    step: float


class Dims(EventedModel):
    """Dimensions object modeling slicing and displaying.

    Parameters
    ----------
    ndim : int
        Number of dimensions.
    ndisplay : int
        Number of displayed dimensions.
    range : tuple of 3-tuple of float
        List of tuples (min, max, step), one for each dimension in world
        coordinates space. Lower and upper bounds are inclusive.
    point : tuple of floats
        Dims position in world coordinates for each dimension.
    margin_left : tuple of floats
        Left margin in world pixels of the slice for each dimension.
    margin_right : tuple of floats
        Right margin in world pixels of the slice for each dimension.
    order : tuple of int
        Tuple of ordering the dimensions, where the last dimensions are rendered.
    axis_labels : tuple of str
        Tuple of labels for each dimension.
    units : tuple of pint.Unit, optional
        Shared world units for each dimension.
        If ``None``, no additional unit conversion is applied.
    last_used : int
        Dimension which was last interacted with.

    Attributes
    ----------
    ndim : int
        Number of dimensions.
    ndisplay : int
        Number of displayed dimensions.
    range : tuple of 3-tuple of float
        List of tuples (min, max, step), one for each dimension in world
        coordinates space. Lower and upper bounds are inclusive.
    point : tuple of floats
        Dims position in world coordinates for each dimension.
    margin_left : tuple of floats
        Left margin (=thickness) in world pixels of the slice for each dimension.
    margin_right : tuple of floats
        Right margin (=thickness) in world pixels of the slice for each dimension.
    order : tuple of int
        Tuple of ordering the dimensions, where the last dimensions are rendered.
    axis_labels : tuple of str
        Tuple of labels for each dimension.
    units : tuple of pint.Unit or None
        Shared world units for each dimension.
        If ``None``, no additional unit conversion is applied.
    last_used : int
        Dimension which was last used.
        Tuple the slider position for each dims slider, in world coordinates.
    current_step : tuple of int
        Current step for each dimension (same as point, but in slider coordinates).
    nsteps : tuple of int
        Number of steps available to each slider. These are calculated from
        the ``range``.
    thickness : tuple of floats
        Thickness of the slice (sum of both margins) for each dimension in world coordinates.
    displayed : tuple of int
        List of dimensions that are displayed. These are calculated from the
        ``order`` and ``ndisplay``.
    not_displayed : tuple of int
        List of dimensions that are not displayed. These are calculated from the
        ``order`` and ``ndisplay``.
    displayed_order : tuple of int
        Order of only displayed dimensions. These are calculated from the
        ``displayed`` dimensions.
    rollable :  tuple of bool
        Tuple of axis roll state. If True the axis is rollable.
    """

    # A GC-visible slot keeps each native owner independent of the
    # Pydantic private dictionaries that copy/restoration can intentionally share.
    __slots__ = ('_naparixx_dims_owner',)

    # fields
    ndim: int = 2
    ndisplay: Literal[2, 3] = 2

    order: tuple[int, ...] = ()
    axis_labels: tuple[str, ...] = ()
    rollable: tuple[bool, ...] = ()

    range: tuple[RangeTuple, ...] = ()
    margin_left: tuple[float, ...] = ()
    margin_right: tuple[float, ...] = ()
    point: tuple[float, ...] = ()
    units: tuple[pint.Unit, ...] | None = None

    last_used: int = 0

    # Schemas remain public; native descriptors own persistent numeric state.
    _native_state: _native.DimsState | None = PrivateAttr(None)
    _native_candidate: _native.DimsState | None = PrivateAttr(None)
    _native_boundary_depth: int = PrivateAttr(0)
    _native_overrides: dict = PrivateAttr(default_factory=dict)
    model_config = ConfigDict(dependencies={
        "nsteps": ["range"],
        "current_step": ["point", "range"],
        "margin_left_step": ["margin_left", "range"],
        "margin_right_step": ["margin_right", "range"],
        "thickness": ["margin_left", "margin_right"],
        "thickness_step": ["margin_left", "margin_right", "range"],
        "displayed": ["order", "ndisplay"],
        "not_displayed": ["order", "ndisplay"],
        "displayed_order": ["order", "ndisplay"],
    })

    # private vars
    _play_ready: bool = True  # False if currently awaiting a draw event
    _scroll_progress: int = 0
    _validating: bool = False

    def __init__(self, **kwargs):
        _native.dims_initialize(self, kwargs)

    def __getattribute__(self, name):
        return _native.dims_getattribute(self, name)

    def __setattr__(self, name, value):
        _native.dims_setattr(self, name, value)

    def _super_setattr_(self, name, value):
        _native.dims_super_setattr(self, name, value)

    def __copy__(self):
        return _native.dims_copy(self)

    def __deepcopy__(self, memo=None):
        return _native.dims_copy(self, True, memo)

    def __setstate__(self, state):
        _native.dims_setstate(self, state)

    def model_copy(self, *, update=None, deep=False):
        return _native.dims_model_copy(self, update, deep)

    @classmethod
    def model_construct(cls, _fields_set=None, **values):
        return _native.dims_construct(cls, _fields_set, values)

    # validators
    # check fields is false to allow private fields to work
    @field_validator(
        'order',
        'axis_labels',
        'rollable',
        'point',
        'margin_left',
        'margin_right',
        mode='before',
    )
    def _as_tuple(v):
        return tuple(v)

    @field_validator('range', mode='before')
    def _check_ranges(ranges):
        """
        Ensure the range values are sane.

        - start < stop
        - step > 0
        """
        _native.validate_dims_ranges(ranges)
        return ranges

    @model_validator(mode='after')
    def _check_dims(self):
        """Check the consistency of dimensionality for all attributes.

        Parameters
        ----------
        values : dict
            Values dictionary to update dims model with.
        """
        return _native.dims_validate_model(self)

    @staticmethod
    def _nsteps_from_range(dims_range) -> tuple[float, ...]:
        return _native.dims_range_nsteps(dims_range)

    @property
    def nsteps(self) -> tuple[float, ...]:
        return _native.dims_computed(self, 'nsteps')

    @nsteps.setter
    def nsteps(self, value):
        _native.dims_set_computed(self, 'nsteps', value)

    @property
    def current_step(self):
        return _native.dims_computed(self, 'current_step')

    @current_step.setter
    def current_step(self, value):
        _native.dims_set_computed(self, 'current_step', value)

    @property
    def margin_left_step(self) -> tuple[int, ...]:
        return _native.dims_computed(self, 'margin_left_step')

    @margin_left_step.setter
    def margin_left_step(self, value):
        _native.dims_set_computed(self, 'margin_left_step', value)

    @property
    def margin_right_step(self) -> tuple[int, ...]:
        return _native.dims_computed(self, 'margin_right_step')

    @margin_right_step.setter
    def margin_right_step(self, value):
        _native.dims_set_computed(self, 'margin_right_step', value)

    @property
    def thickness(self) -> tuple[float, ...]:
        return _native.dims_computed(self, 'thickness')

    @thickness.setter
    def thickness(self, value):
        _native.dims_set_computed(self, 'thickness', value)

    @property
    def thickness_step(self) -> tuple[int, ...]:
        return _native.dims_computed(self, 'thickness_step')

    @thickness_step.setter
    def thickness_step(self, value):
        _native.dims_set_computed(self, 'thickness_step', value)

    @property
    def displayed(self) -> tuple[int, ...]:
        """Tuple: Dimensions that are displayed."""
        return _native.dims_computed(self, 'displayed')

    @property
    def not_displayed(self) -> tuple[int, ...]:
        """Tuple: Dimensions that are not displayed."""
        return _native.dims_computed(self, 'not_displayed')

    @property
    def displayed_order(self) -> tuple[int, ...]:
        return _native.dims_computed(self, 'displayed_order')

    def set_range(
        self,
        axis: int | Sequence[int],
        _range: Sequence[int | float] | Sequence[Sequence[int | float]],
    ):
        """Sets ranges (min, max, step) for the given dimensions.

        Parameters
        ----------
        axis : int or sequence of int
            Dimension index or a sequence of axes whos range will be set.
        _range : tuple or sequence of tuple
            Range specified as (min, max, step) or a sequence of these range
            tuples.
        """
        _native.dims_set_on_axes(self, 'range', axis, _range)

    def set_point(
        self,
        axis: int | Sequence[int],
        value: float | Sequence[float],
    ):
        """Sets point to slice dimension in world coordinates.

        Parameters
        ----------
        axis : int or sequence of int
            Dimension index or a sequence of axes whos point will be set.
        value : scalar or sequence of scalars
            Value of the point for each axis.
        """
        _native.dims_set_on_axes(self, 'point', axis, value)

    def set_current_step(
        self,
        axis: int | Sequence[int],
        value: int | Sequence[int],
    ):
        _native.dims_set_on_axes(self, 'current_step', axis, value)

    def set_axis_label(
        self,
        axis: int | Sequence[int],
        label: str | Sequence[str],
    ):
        """Sets new axis labels for the given axes.

        Parameters
        ----------
        axis : int or sequence of int
            Dimension index or a sequence of axes whos labels will be set.
        label : str or sequence of str
            Given labels for the specified axes.
        """
        _native.dims_set_on_axes(self, 'axis_labels', axis, label)

    def reset(self):
        """Reset dims values to initial states."""
        _native.dims_execute(self, 'reset')

    def transpose(self):
        """Transpose displayed dimensions.

        This swaps the order of the last two displayed dimensions.
        The order of the displayed is taken from Dims.order.
        """
        _native.dims_execute(self, 'transpose')

    def _increment_dims_right(self, axis: int | None = None):
        """Increment dimensions to the right along given axis, or last used axis if None

        Parameters
        ----------
        axis : int, optional
            Axis along which to increment dims, by default None
        """
        _native.dims_execute(self, 'increment', axis, 1)

    def _increment_dims_left(self, axis: int | None = None):
        """Increment dimensions to the left along given axis, or last used axis if None

        Parameters
        ----------
        axis : int, optional
            Axis along which to increment dims, by default None
        """
        _native.dims_execute(self, 'increment', axis, -1)

    def _focus_up(self):
        """Shift focused dimension slider to be the next slider above."""
        _native.dims_execute(self, 'focus', direction=1)

    def _focus_down(self):
        """Shift focused dimension slider to be the next slider bellow."""
        _native.dims_execute(self, 'focus', direction=-1)

    def roll(self):
        """Roll order of dimensions for display."""
        _native.dims_execute(self, 'roll')

    def _go_to_center_step(self):
        _native.dims_execute(self, 'center')

    def _sanitize_input(
        self, axis, value, value_is_sequence=False
    ) -> tuple[list[int], list]:
        """
        Ensure that axis and value are the same length, that axes are not
        out of bounds, and coerces to lists for easier processing.
        """
        return _native.sanitize_dims_input(axis, value, value_is_sequence, self.ndim)

    @contextlib.contextmanager
    def _validating_ctx(self):
        # Retain the Python generator/context protocol; flag transitions,
        # exception handling and cleanup are implemented by the native iterator.
        yield from _native.dims_validation_generator(self)


# Install descriptors after Pydantic has collected the original field schemas.
# The class, signatures and plugin subclass entry points retain their identity.
class _NativeDimsField:
    """Python descriptor protocol adapter; values and decisions live in C++."""

    __slots__ = ('_field',)

    def __init__(self, field):
        self._field = field

    def __get__(self, instance, owner=None):
        return _native.dims_field_get(instance, self._field)

    def __set__(self, instance, value):
        _native.dims_field_set(instance, self._field, value)

    def __delete__(self, instance):
        _native.dims_field_delete(instance, self._field)


_native.install_dims_fields(Dims, _NativeDimsField)


def ensure_len(value: tuple, length: int, pad_width: Any):
    """
    Ensure that the value has the required number of elements.

    Right-crop if value is too long; left-pad with default if too short.

    Parameters
    ----------
    value : Tuple
        A tuple of values to be resized.
    ndim : int
        Number of desired values.
    default : Tuple
        Default element for left-padding.
    """
    return _native.ensure_len(value, length, pad_width)


def ensure_axis_in_bounds(axis: int, ndim: int) -> int:
    """Ensure a given value is inside the existing axes of the image.

    Returns
    -------
    axis : int
        The axis which was checked for validity.
    ndim : int
        The dimensionality of the layer.

    Raises
    ------
    ValueError
        The given axis index is out of bounds.
    """
    return _native.ensure_axis_in_bounds(axis, ndim)
