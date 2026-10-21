from __future__ import annotations

from naparixx import native as _native

from naparixx._backend import dispatch as _native_dispatch

from typing import TYPE_CHECKING, Generic, TypeVar, overload

import numpy as np
import numpy.typing as npt
import pint
import toolz as tz
from psygnal import Signal

from napari.utils.events import EventedList
from napari.utils.transforms._units import get_units_from_name
from napari.utils.transforms.transform_utils import (
    compose_linear_matrix,
    decompose_linear_matrix,
    embed_in_identity_matrix,
    infer_ndim,
    is_diagonal,
    is_matrix_triangular,
    is_matrix_upper_triangular,
    rotate_to_matrix,
    scale_to_vector,
    shear_to_matrix,
    translate_to_vector,
)

if TYPE_CHECKING:
    from collections.abc import Iterable, Sequence


class Transform(_native.TransformNamespaceBase):
    """Base transform class.

    Defaults to the identity transform.

    Parameters
    ----------
    func : callable, Coords -> Coords
        A function converting an NxD array of coordinates to NxD'.
    name : string
        A string name for the transform.
    """

    changed = Signal()

    def __init__(self, func=tz.identity, inverse=None, name=None) -> None:
        _native.transform_initialize(self, func, inverse, name, globals())

    def __call__(self, coords):
        """Transform input coordinates to output."""
        return _native.transform_base_call(self, coords, globals())

    @property
    def inverse(self) -> Transform:
        return _native.transform_base_inverse(self, globals())

    def compose(self, transform: Transform) -> Transform:
        """Return the composite of this transform and the provided one."""
        return _native.transform_base_compose(self, transform, globals())

    def set_slice(self, axes: Sequence[int]) -> Transform:
        """Return a transform subset to the visible dimensions.

        Parameters
        ----------
        axes : Sequence[int]
            Axes to subset the current transform with.

        Returns
        -------
        Transform
            Resulting transform.
        """
        return _native.transform_subset_error(globals())

    def expand_dims(self, axes: Sequence[int]) -> Transform:
        """Return a transform with added axes for non-visible dimensions.

        Parameters
        ----------
        axes : Sequence[int]
            Location of axes to expand the current transform with. Passing a
            list allows expansion to occur at specific locations and for
            expand_dims to be like an inverse to the set_slice method.

        Returns
        -------
        Transform
            Resulting transform.
        """
        return _native.transform_subset_error(globals())

    @property
    def _is_diagonal(self):
        """Indicate when a transform does not mix or permute dimensions.

        Can be overridden in subclasses to enable performance optimizations
        that are specific to this case.
        """
        return _native.transform_base_diagonal(self, globals())

    def _clean_cache(self):
        _native.transform_clean_cache(self, globals())


_T = TypeVar("_T", bound=Transform)


class TransformChain(EventedList[_T], Transform, Generic[_T]):
    def __init__(self, transforms: Iterable[Transform] | None = None) -> None:
        locals_owner = _native.transform_object_locals("chain_init", (transforms,))
        del transforms

        def lookup_factory():
            return lambda x: _native.transform_chain_lookup(x, globals())

        _native.transform_chain_initialize(
            self,
            locals_owner,
            lambda self: super(),
            lookup_factory,
            globals(),
        )

    def __call__(self, coords):
        return _native.transform_chain_call(self, coords, globals())

    def __newlike__(self, iterable):
        return _native.transform_chain_newlike(iterable, globals())

    @overload
    def __getitem__(self, key: int) -> _T: ...

    @overload
    def __getitem__(self, key: str) -> _T: ...

    @overload
    def __getitem__(self, key: slice) -> TransformChain[_T]: ...

    def __getitem__(self, key):
        return _native.transform_chain_get(self, key, lambda self: super(), globals())

    def __setitem__(self, key, value):
        _native.transform_chain_set(self, key, value, lambda self: super(), globals())

    def __delitem__(self, key):
        locals_owner = _native.transform_object_locals("chain_delete", ())
        _native.transform_chain_delete(
            self,
            key,
            locals_owner,
            lambda self: super(),
            globals(),
        )

    @property
    def inverse(self) -> TransformChain:
        """Return the inverse transform chain."""
        return _native.transform_chain_inverse(self, globals())

    @property
    def _is_diagonal(self):
        def generator_factory(iterator):
            yield from iterator

        return _native.transform_chain_diagonal(self, generator_factory, globals())

    @property
    def simplified(self) -> _T:
        """
        Return the composite of the transforms inside the transform chain.

        Raises
        ------
        ValueError
            If the transform chain is empty.
        """
        return _native.transform_chain_simplified(self, globals())

    def set_slice(self, axes: Sequence[int]) -> TransformChain:
        """Return a transform chain subset to the visible dimensions.

        Parameters
        ----------
        axes : Sequence[int]
            Axes to subset the current transform chain with.

        Returns
        -------
        TransformChain
            Resulting transform chain.
        """
        return _native.transform_chain_slice(self, axes, globals())

    def expand_dims(self, axes: Sequence[int]) -> TransformChain:
        """Return a transform chain with added axes for non-visible dimensions.

        Parameters
        ----------
        axes : Sequence[int]
            Location of axes to expand the current transform with. Passing a
            list allows expansion to occur at specific locations and for
            expand_dims to be like an inverse to the set_slice method.

        Returns
        -------
        TransformChain
            Resulting transform chain.
        """
        return _native.transform_chain_expand(self, axes, globals())


class ScaleTranslate(Transform):
    """n-dimensional scale and translation (shift) class.

    Scaling is always applied before translation.

    Parameters
    ----------
    scale : 1-D array
        A 1-D array of factors to scale each axis by. Scale is broadcast to 1
        in leading dimensions, so that, for example, a scale of [4, 18, 34] in
        3D can be used as a scale of [1, 4, 18, 34] in 4D without modification.
        An empty translation vector implies no scaling.
    translate : 1-D array
        A 1-D array of factors to shift each axis by. Translation is broadcast
        to 0 in leading dimensions, so that, for example, a translation of
        [4, 18, 34] in 3D can be used as a translation of [0, 4, 18, 34] in 4D
        without modification. An empty translation vector implies no
        translation.
    name : string
        A string name for the transform.
    """

    def __init__(self, scale=(1.0,), translate=(0.0,), *, name=None) -> None:
        locals_owner = _native.transform_object_locals("scale_init", (scale, translate))
        del scale, translate
        _native.transform_scale_initialize(
            self,
            name,
            locals_owner,
            lambda self: super(),
            globals(),
        )

    @_native_dispatch("scale_translate_adapter")
    def __call__(self, coords):
        locals_owner = _native.transform_call_locals(coords)
        del coords
        return _native.scale_translate_call(
            self,
            locals_owner.coords,
            globals(),
            locals_owner,
        )

    @property
    def inverse(self) -> ScaleTranslate:
        """Return the inverse transform."""
        return _native.transform_scale_inverse(self, globals())

    def compose(self, transform: Transform) -> Transform:
        """Return the composite of this transform and the provided one."""
        locals_owner = _native.transform_object_locals("scale_compose", ())
        return _native.transform_scale_compose(
            self,
            transform,
            locals_owner,
            lambda self: super(),
            globals(),
        )

    def set_slice(self, axes: Sequence[int]) -> ScaleTranslate:
        """Return a transform subset to the visible dimensions.

        Parameters
        ----------
        axes : Sequence[int]
            Axes to subset the current transform with.

        Returns
        -------
        Transform
            Resulting transform.
        """
        return _native.transform_scale_slice(self, axes, globals())

    def expand_dims(self, axes: Sequence[int]) -> ScaleTranslate:
        """Return a transform with added axes for non-visible dimensions.

        Parameters
        ----------
        axes : Sequence[int]
            Location of axes to expand the current transform with. Passing a
            list allows expansion to occur at specific locations and for
            expand_dims to be like an inverse to the set_slice method.

        Returns
        -------
        Transform
            Resulting transform.
        """
        locals_owner = _native.transform_object_locals("scale_expand", ())
        return _native.transform_scale_expand(self, axes, locals_owner, globals())

    @property
    def _is_diagonal(self):
        """Indicate that this transform does not mix or permute dimensions."""
        return _native.transform_scale_diagonal(self, globals())


class Affine(Transform):
    """n-dimensional affine transformation class.

    The affine transform can be represented as a n+1 dimensional
    transformation matrix in homogeneous coordinates [1]_, an n
    dimensional matrix and a length n translation vector, or be
    composed and decomposed from scale, rotate, and shear
    transformations defined in the following order:

    rotate * shear * scale + translate

    The affine_matrix representation can be used for easy compatibility
    with other libraries that can generate affine transformations.

    Parameters
    ----------
    rotate : float, 3-tuple of float, or n-D array.
        If a float convert into a 2D rotation matrix using that value as an
        angle. If 3-tuple convert into a 3D rotation matrix, using a yaw,
        pitch, roll convention. Otherwise assume an nD rotation. Angles are
        assumed to be in degrees. They can be converted from radians with
        np.degrees if needed.
    scale : 1-D array
        A 1-D array of factors to scale each axis by. Scale is broadcast to 1
        in leading dimensions, so that, for example, a scale of [4, 18, 34] in
        3D can be used as a scale of [1, 4, 18, 34] in 4D without modification.
        An empty translation vector implies no scaling.
    shear : 1-D array or n-D array
        Either a vector of upper triangular values, or an nD shear matrix with
        ones along the main diagonal.
    translate : 1-D array
        A 1-D array of factors to shift each axis by. Translation is broadcast
        to 0 in leading dimensions, so that, for example, a translation of
        [4, 18, 34] in 3D can be used as a translation of [0, 4, 18, 34] in 4D
        without modification. An empty translation vector implies no
        translation.
    linear_matrix : n-D array, optional
        (N, N) matrix with linear transform. If provided then scale, rotate,
        and shear values are ignored.
    affine_matrix : n-D array, optional
        (N+1, N+1) affine transformation matrix in homogeneous coordinates [1]_.
        The first (N, N) entries correspond to a linear transform and
        the final column is a length N translation vector and a 1 or a napari
        AffineTransform object. If provided then translate, scale, rotate, and
        shear values are ignored.
    ndim : int
        The dimensionality of the transform. If None, this is inferred from the
        other parameters.
    name : string
        A string name for the transform.

    References
    ----------
    .. [1] https://en.wikipedia.org/wiki/Homogeneous_coordinates.
    """

    def __init__(
        self,
        scale=(1.0, 1.0),
        translate=(
            0.0,
            0.0,
        ),
        *,
        affine_matrix=None,
        axis_labels: Sequence[str] | None = None,
        linear_matrix=None,
        name=None,
        ndim=None,
        rotate=None,
        shear=None,
        units: Sequence[str | pint.Unit] | None = None,
    ) -> None:
        locals_owner = _native.transform_object_locals(
            'affine_init',
            (translate, linear_matrix, ndim, rotate, shear),
        )
        del translate, linear_matrix, ndim, rotate, shear

        def generator_factory(iterator):
            yield from iterator

        _native.transform_affine_initialize(
            self,
            scale,
            affine_matrix,
            axis_labels,
            name,
            units,
            locals_owner,
            lambda self: super(),
            generator_factory,
            globals(),
        )

    @_native_dispatch("affine_adapter")
    def __call__(self, coords):
        locals_owner = _native.transform_call_locals(coords)
        del coords
        return _native.affine_call(self, locals_owner.coords, globals(), locals_owner)

    @property
    def ndim(self) -> int:
        """Dimensionality of the transform."""
        return _native.transform_affine_ndim(self, globals())

    @property
    def axis_labels(self) -> tuple[str, ...]:
        """tuple of axis labels for the layer."""
        return _native.transform_affine_labels_get(self, globals())

    @axis_labels.setter
    def axis_labels(self, axis_labels: Sequence[str] | None) -> None:
        locals_owner = _native.transform_object_locals("affine_labels", (axis_labels,))
        del axis_labels

        def generator_factory(iterator):
            yield from iterator

        _native.transform_affine_labels_set(
            self,
            locals_owner,
            generator_factory,
            globals(),
        )

    @property
    def units(self) -> tuple[pint.Unit, ...]:
        """List of units for the layer."""
        return _native.transform_affine_units_get(self, globals())

    @units.setter
    def units(self, units: Sequence[pint.Unit] | None) -> None:
        locals_owner = _native.transform_object_locals("affine_units", (units,))
        del units
        _native.transform_affine_units_set(self, locals_owner, globals())

    @property
    def scale(self) -> npt.NDArray:
        """Return the scale of the transform."""
        return _native.transform_affine_scale_get(self, globals())

    @scale.setter
    def scale(self, scale):
        """Set the scale of the transform."""
        locals_owner = _native.transform_object_locals("affine_scale", (scale,))
        del scale
        _native.transform_affine_scale_set(self, locals_owner, globals())

    @property
    def physical_scale(self) -> tuple[pint.Quantity, ...]:
        """Return the scale of the transform, with units."""
        return _native.transform_affine_physical_scale(self, globals())

    @property
    def translate(self) -> npt.NDArray:
        """Return the translation of the transform."""
        return _native.transform_affine_translate_get(self, globals())

    @translate.setter
    def translate(self, translate):
        """Set the translation of the transform."""
        _native.transform_affine_translate_set(self, translate, globals())

    def _setup_decompose_linear_matrix_cache(self):
        _native.transform_affine_decompose(self, globals())

    @property
    def rotate(self) -> np.ndarray[tuple[int, int], np.dtype[np.float64]]:
        """Return the rotation of the transform."""
        return _native.transform_affine_rotate_get(self, globals())

    @rotate.setter
    def rotate(self, rotate):
        """Set the rotation of the transform."""
        _native.transform_affine_rotate_set(self, rotate, globals())

    @property
    def shear(self) -> npt.NDArray:
        """Return the shear of the transform."""
        return _native.transform_affine_shear_get(self, globals())

    @shear.setter
    def shear(self, shear):
        """Set the shear of the transform."""
        locals_owner = _native.transform_object_locals("affine_shear", (shear,))
        del shear
        _native.transform_affine_shear_set(self, locals_owner, globals())

    @property
    def _shear_cache(self):
        return _native.transform_affine_shear_cache(self, globals())

    @property
    def linear_matrix(self) -> npt.NDArray:
        """Return the linear matrix of the transform."""
        return _native.transform_affine_linear_get(self, globals())

    @linear_matrix.setter
    def linear_matrix(self, linear_matrix):
        """Set the linear matrix of the transform."""
        _native.transform_affine_linear_set(self, linear_matrix, globals())

    @property
    def affine_matrix(self) -> npt.NDArray:
        """Return the affine matrix for the transform."""
        locals_owner = _native.transform_object_locals("affine_matrix", ())
        return _native.transform_affine_matrix_get(self, locals_owner, globals())

    @affine_matrix.setter
    def affine_matrix(self, affine_matrix):
        """Set the affine matrix for the transform."""
        _native.transform_affine_matrix_set(self, affine_matrix, globals())

    def __array__(self, *args, **kwargs):
        """NumPy __array__ protocol to get the affine transform matrix."""
        return _native.transform_affine_array(self, globals())

    @property
    def inverse(self) -> Affine:
        """Return the inverse transform."""
        return _native.transform_affine_inverse(self, globals())

    @overload
    def compose(self, transform: Affine) -> Affine: ...

    @overload
    def compose(self, transform: Transform) -> Transform: ...

    def compose(self, transform):
        """Return the composite of this transform and the provided one."""
        locals_owner = _native.transform_object_locals("affine_compose", ())
        return _native.transform_affine_compose(
            self,
            transform,
            locals_owner,
            lambda self: super(),
            globals(),
        )

    def set_slice(self, axes: Sequence[int]) -> Affine:
        """Return a transform subset to the visible dimensions.

        Parameters
        ----------
        axes : Sequence[int]
            Axes to subset the current transform with.

        Returns
        -------
        Affine
            Resulting transform.
        """
        locals_owner = _native.transform_object_locals("affine_slice", (axes,))
        del axes
        return _native.transform_affine_slice(self, locals_owner, globals())

    def replace_slice(self, axes: Sequence[int], transform: Affine) -> Affine:
        """Returns a transform where the transform at the indicated n dimensions is replaced with another n-dimensional transform

        Parameters
        ----------
        axes : Sequence[int]
            Axes where the transform will be replaced
        transform : Affine
            The transform that will be inserted. Must have as many dimension as len(axes)

        Returns
        -------
        Affine
            Resulting transform.
        """

        locals_owner = _native.transform_object_locals("affine_replace", ())
        return _native.transform_affine_replace(
            self,
            axes,
            transform,
            locals_owner,
            globals(),
        )

    def expand_dims(self, axes: Sequence[int]) -> Affine:
        """Return a transform with added axes for non-visible dimensions.

        Parameters
        ----------
        axes : Sequence[int]
            Location of axes to expand the current transform with. Passing a
            list allows expansion to occur at specific locations and for
            expand_dims to be like an inverse to the set_slice method.

        Returns
        -------
        Transform
            Resulting transform.
        """
        locals_owner = _native.transform_object_locals("affine_expand", ())
        return _native.transform_affine_expand(self, axes, locals_owner, globals())

    @property
    def _is_diagonal(self):
        """Determine whether linear_matrix is diagonal up to some tolerance.

        Since only `self.linear_matrix` is checked, affines with a translation
        component can still be considered diagonal.
        """
        return _native.transform_affine_diagonal(self, globals())


class CompositeAffine(Affine):
    """n-dimensional affine transformation composed from more basic components.

    Composition is in the following order

    rotate * shear * scale + translate

    Parameters
    ----------
    rotate : float, 3-tuple of float, or n-D array.
        If a float convert into a 2D rotation matrix using that value as an
        angle. If 3-tuple convert into a 3D rotation matrix, using a yaw,
        pitch, roll convention. Otherwise assume an nD rotation. Angles are
        assumed to be in degrees. They can be converted from radians with
        np.degrees if needed.
    scale : 1-D array
        A 1-D array of factors to scale each axis by. Scale is broadcast to 1
        in leading dimensions, so that, for example, a scale of [4, 18, 34] in
        3D can be used as a scale of [1, 4, 18, 34] in 4D without modification.
        An empty translation vector implies no scaling.
    shear : 1-D array or n-D array
        Either a vector of upper triangular values, or an nD shear matrix with
        ones along the main diagonal.
    translate : 1-D array
        A 1-D array of factors to shift each axis by. Translation is broadcast
        to 0 in leading dimensions, so that, for example, a translation of
        [4, 18, 34] in 3D can be used as a translation of [0, 4, 18, 34] in 4D
        without modification. An empty translation vector implies no
        translation.
    ndim : int
        The dimensionality of the transform. If None, this is inferred from the
        other parameters.
    name : string
        A string name for the transform.
    """

    def __init__(
        self,
        scale=(1, 1),
        translate=(0, 0),
        *,
        axis_labels=None,
        rotate=None,
        shear=None,
        ndim=None,
        name=None,
        units=None,
    ) -> None:
        locals_owner = _native.transform_object_locals("composite_init", (ndim,))
        del ndim
        _native.transform_composite_initialize(
            self,
            scale,
            translate,
            axis_labels,
            rotate,
            shear,
            name,
            units,
            locals_owner,
            lambda self: super(),
            globals(),
        )

    @property
    def scale(self) -> npt.NDArray:
        """Return the scale of the transform."""
        return _native.transform_composite_scale_get(self, globals())

    @scale.setter
    def scale(self, scale):
        """Set the scale of the transform."""
        _native.transform_composite_scale_set(self, scale, globals())

    @property
    def rotate(self) -> npt.NDArray:
        """Return the rotation of the transform."""
        return _native.transform_composite_rotate_get(self, globals())

    @rotate.setter
    def rotate(self, rotate):
        """Set the rotation of the transform."""
        _native.transform_composite_rotate_set(self, rotate, globals())

    @property
    def shear(self) -> npt.NDArray:
        """Return the shear of the transform."""
        return _native.transform_composite_shear_get(self, globals())

    @shear.setter
    def shear(self, shear):
        """Set the shear of the transform."""
        _native.transform_composite_shear_set(self, shear, globals())

    @property
    def linear_matrix(self):
        return _native.transform_composite_super_get(
            self,
            lambda self: super(),
            'linear_matrix',
            globals(),
        )

    @linear_matrix.setter
    def linear_matrix(self, linear_matrix):
        """Setting the linear matrix of a CompositeAffine transform is not supported."""
        return _native.transform_composite_matrix_error('linear_matrix', globals())

    @property
    def affine_matrix(self):
        return _native.transform_composite_super_get(
            self,
            lambda self: super(),
            'affine_matrix',
            globals(),
        )

    @affine_matrix.setter
    def affine_matrix(self, affine_matrix):
        """Setting the affine matrix of a CompositeAffine transform is not supported."""
        return _native.transform_composite_matrix_error('affine_matrix', globals())

    def set_slice(self, axes: Sequence[int]) -> CompositeAffine:
        return _native.transform_composite_slice(self, axes, globals())

    def expand_dims(self, axes: Sequence[int]) -> CompositeAffine:
        locals_owner = _native.transform_object_locals("composite_expand", ())
        return _native.transform_composite_expand(self, axes, locals_owner, globals())

    def _make_linear_matrix(self):
        return _native.transform_composite_make_linear(self, globals())
