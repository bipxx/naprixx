from __future__ import annotations

import warnings
from dataclasses import dataclass
from typing import TYPE_CHECKING, Generic, TypeVar

import numpy as np
import pint

from functools import cached_property
from naparixx import native as _native
from napari.utils.misc import reorder_after_dim_reduction
from napari.utils.transforms import Affine

if TYPE_CHECKING:
    from collections.abc import Sequence
    from typing import Self

    import numpy.typing as npt

    from napari.components.dims import Dims

_T = TypeVar('_T')


def _slice_dataclass_thick_init_template():
    _object_capture = None

    def __init__(self, point, margin_left, margin_right):
        return _native.slice_dataclass_thick_init(
            self, point, margin_left, margin_right, lambda: _object_capture
        )

    return __init__


def _slice_dataclass_input_init_template():
    _object_capture = None

    def __init__(self, ndisplay, world_slice, order):
        return _native.slice_dataclass_input_init(
            self, ndisplay, world_slice, order, lambda: _object_capture
        )

    return __init__


@dataclass(frozen=True)
class _ThickNDSlice(_native.SliceDataclassNamespaceBase, Generic[_T]):
    """Holds the point and the left and right margins of a thick nD slice."""

    point: tuple[_T, ...]
    margin_left: tuple[_T, ...]
    margin_right: tuple[_T, ...]

    @property
    def ndim(self):
        return _native.slice_dataclass_thick_ndim(self, globals())

    @classmethod
    def make_full(
        cls,
        point=None,
        margin_left=None,
        margin_right=None,
        ndim=None,
    ):
        """
        Make a full slice based on minimal input.

        If ndim is provided, it will be used to crop or prepend zeros to the given values.
        Values not provided will be filled zeros.
        """
        locals_owner = _native.slice_dataclass_make_full_locals(
            point, margin_left, margin_right, ndim
        )
        del point, margin_left, margin_right, ndim
        return _native.slice_dataclass_thick_make_full(
            cls, globals(), locals_owner, (0,)
        )

    @classmethod
    def from_dims(cls, dims: Dims) -> Self:
        """Generate from a Dims object's point and margins."""
        return _native.slice_dataclass_thick_from_dims(cls, dims)

    def copy_with(
        self,
        point=None,
        margin_left=None,
        margin_right=None,
        ndim=None,
    ):
        """Create a copy, but modifying the given fields."""
        return _native.slice_dataclass_thick_copy_with(
            self, point, margin_left, margin_right, ndim
        )

    def as_array(self) -> npt.NDArray:
        """Return point and left and right margin as a (3, D) array."""
        return _native.thick_slice_as_array(self, globals())

    @classmethod
    def from_array(cls, arr: npt.NDArray) -> _ThickNDSlice:
        """Construct from a (3, D) array of point, left margin and right margin."""
        return _native.thick_slice_from_array(cls, arr, globals())

    def __getitem__(self, key):
        # this allows to use numpy-like slicing on the whole object
        return _native.slice_dataclass_thick_getitem(self, key, globals())

    def __iter__(self):
        # iterate all three fields dimension per dimension
        yield from _native.slice_dataclass_thick_iter(self, globals())


_native.slice_dataclass_install_init(
    _ThickNDSlice, _ThickNDSlice.__init__, _slice_dataclass_thick_init_template()
)


@dataclass(frozen=True)
class _SliceInput(_native.SliceDataclassNamespaceBase):
    """Encapsulates the input needed for slicing a layer.

    An instance of this should be associated with a layer and some of the values
    in ``Viewer.dims`` when slicing a layer.
    """

    # The number of dimensions to be displayed in the slice.
    ndisplay: int
    # The thick slice in world coordinates.
    # Only the elements in the non-displayed dimensions have meaningful values.
    world_slice: _ThickNDSlice[float]
    # The layer dimension indices in the order they are displayed.
    # A permutation of the ``range(self.ndim)``.
    # The last ``self.ndisplay`` dimensions are displayed in the canvas.
    order: tuple[int, ...]

    @cached_property
    def _native_plan(self):
        return _native.SlicePlan(self.ndisplay, self.order)

    @property
    def ndim(self) -> int:
        """The dimensionality of the associated layer."""
        return _native.slice_dataclass_input_ndim(self, globals())

    @property
    def displayed(self) -> list[int]:
        """The layer dimension indices displayed in this slice."""
        return _native.slice_dataclass_input_displayed(self, globals())

    @property
    def not_displayed(self) -> list[int]:
        """The layer dimension indices not displayed in this slice."""
        return _native.slice_dataclass_input_not_displayed(self, globals())

    def with_ndim(self, ndim: int) -> _SliceInput:
        """Returns a new instance with the given number of layer dimensions."""
        old_ndim: int

        def _generator(native_iterator):
            yield from native_iterator

        locals_owner = _native.slice_dataclass_resize_locals()
        return _native.slice_dataclass_input_with_ndim(
            self,
            globals(),
            lambda: ndim,
            lambda: old_ndim,
            _generator,
            locals_owner,
        )

    def data_slice(
        self,
        world_to_data: Affine,
    ) -> _ThickNDSlice[float | int]:
        """Transforms this thick_slice into data coordinates with only relevant dimensions.

        The elements in non-displayed dimensions will be real numbers.
        The elements in displayed dimensions will be ``slice(None)``.
        """
        locals_owner = _native.slice_dataclass_slice_locals()
        return _native.slice_dataclass_data_slice(
            self,
            world_to_data,
            globals(),
            'Non-orthogonal slicing is being requested, but is not fully supported. '
            'Data is displayed without applying an out-of-slice rotation or shear component.',
            locals_owner,
        )

    def is_orthogonal(self, world_to_data: Affine) -> bool:
        """Returns True if this slice represents an orthogonal slice through a layer's data, False otherwise."""
        mapped_nd_subspace: np.ndarray

        def _generator(native_iterator):
            yield from native_iterator

        locals_owner = _native.slice_dataclass_orthogonal_locals(world_to_data)
        del world_to_data
        return _native.slice_dataclass_input_is_orthogonal(
            self,
            globals(),
            lambda: mapped_nd_subspace,
            _generator,
            locals_owner,
            1e-8,
        )


_native.slice_dataclass_install_init(
    _SliceInput, _SliceInput.__init__, _slice_dataclass_input_init_template()
)


def apply_units_to_transform(
    data_to_world: Affine, world_units: Sequence[str] | None
) -> Affine:
    """Applies unit scaling to a data_to_world transform.

    Parameters
    ----------
    data_to_world : Affine
        The original data to world transform.
    world_units : Sequence[str] | None
        The units for each dimension of the layer.

    Returns
    -------
    Affine
        The new data to world transform with unit scaling applied.
    """
    return _native.apply_slice_units(data_to_world, world_units)
