from __future__ import annotations

from naparixx import _native
from naparixx._backend import dispatch as _native_dispatch

import functools
import inspect
import operator
import warnings
from collections.abc import Callable, Sequence
from importlib import import_module
from typing import (
    TYPE_CHECKING,
    Any,
    NamedTuple,
    TypeVar,
)

import dask
import numpy as np

from napari.utils.action_manager import action_manager
from napari.utils.events.custom_types import Array
from napari.utils.transforms import Affine

if TYPE_CHECKING:
    from collections.abc import Mapping

    import numpy.typing as npt
    import pandas as pd
    import pint

    from napari.layers._data_protocols import LayerDataProtocol
else:

    class _LazyPandas:
        def __getattr__(self, attr: str) -> Any:
            return getattr(import_module('pandas'), attr)

    pd = _LazyPandas()


class Extent(NamedTuple):
    """Extent of coordinates in a local data space and world space.

    Each extent is a (2, D) array that stores the minimum and maximum coordinate
    values in each of D dimensions. Both the minimum and maximum coordinates are
    inclusive so form an axis-aligned, closed interval or a D-dimensional box
    around all the coordinates.

    Attributes
    ----------
    data : (2, D) array of floats
        The minimum and maximum raw data coordinates ignoring any transforms like
        translation or scale.
    world : (2, D) array of floats
        The minimum and maximum world coordinates after applying a transform to the
        raw data coordinates that brings them into a potentially shared world space.
    step : (D,) array of floats
        The step in each dimension that when taken from the minimum world coordinate,
        should form a regular grid that eventually hits the maximum world coordinate.
    """

    data: np.ndarray
    world: np.ndarray
    step: np.ndarray
    units: tuple[pint.Unit, ...]


class LayerListExtent(NamedTuple):
    """Extent of coordinates in a local data space and world space.

    Each extent is a (2, D) array that stores the minimum and maximum coordinate
    values in each of D dimensions. Both the minimum and maximum coordinates are
    inclusive so form an axis-aligned, closed interval or a D-dimensional box
    around all the coordinates.

    Attributes
    ----------
    data : (2, D) array of floats
        The minimum and maximum raw data coordinates ignoring any transforms like
        translation or scale.
    world : (2, D) array of floats
        The minimum and maximum world coordinates after applying a transform to the
        raw data coordinates that brings them into a potentially shared world space.
    step : (D,) array of floats
        The step in each dimension that when taken from the minimum world coordinate,
        should form a regular grid that eventually hits the maximum world coordinate.
    """

    data: np.ndarray | None
    world: np.ndarray
    step: np.ndarray
    units: tuple[pint.Unit, ...] | None


TFunc = TypeVar('TFunc', bound=Callable)


def register_layer_action(
    keymapprovider,
    description: str,
    repeatable: bool = False,
    shortcuts: str | list[str] | None = None,
) -> Callable[[TFunc], TFunc]:
    """
    Convenient decorator to register an action with the current Layers

    It will use the function name as the action name. We force the description
    to be given instead of function docstring for translation purpose.


    Parameters
    ----------
    keymapprovider : KeymapProvider
        class on which to register the keybindings - this will typically be
        the instance in focus that will handle the keyboard shortcut.
    description : str
        The description of the action, this will typically be translated and
        will be what will be used in tooltips.
    repeatable : bool
        A flag indicating whether the action autorepeats when key is held
    shortcuts : str | List[str]
        Shortcut to bind by default to the action we are registering.

    Returns
    -------
    function:
        Actual decorator to apply to a function. Given decorator returns the
        function unmodified to allow decorator stacking.

    """

    def _inner(func: TFunc) -> TFunc:
        nonlocal shortcuts
        name = 'napari:' + func.__name__

        action_manager.register_action(
            name=name,
            command=func,
            description=description,
            keymapprovider=keymapprovider,
            repeatable=repeatable,
        )
        if shortcuts:
            if isinstance(shortcuts, str):
                shortcuts = [shortcuts]

            for shortcut in shortcuts:
                action_manager.bind_shortcut(name, shortcut)
        return func

    return _inner


def register_layer_attr_action(
    keymapprovider,
    description: str,
    attribute_name: str,
    shortcuts=None,
) -> Callable[[TFunc], TFunc]:
    """
    Convenient decorator to register an action with the current Layers.
    This will get and restore attribute from function first argument.

    It will use the function name as the action name. We force the description
    to be given instead of function docstring for translation purpose.

    Parameters
    ----------
    keymapprovider : KeymapProvider
        class on which to register the keybindings - this will typically be
        the instance in focus that will handle the keyboard shortcut.
    description : str
        The description of the action, this will typically be translated and
        will be what will be used in tooltips.
    attribute_name : str
        The name of the attribute to be restored if key is hold over `get_settings().get_settings().application.hold_button_delay.
    shortcuts : str | List[str]
        Shortcut to bind by default to the action we are registering.

    Returns
    -------
    function:
        Actual decorator to apply to a function. Given decorator returns the
        function unmodified to allow decorator stacking.

    """

    def _handle(func: TFunc) -> TFunc:
        sig = inspect.signature(func)
        try:
            first_variable_name = next(iter(sig.parameters))
        except StopIteration as e:
            raise RuntimeError(
                'If actions has no arguments there is no way to know what to set the attribute to.',
            ) from e

        @functools.wraps(func)
        def _wrapper(*args, **kwargs):
            obj = args[0] if args else kwargs[first_variable_name]
            prev_mode = getattr(obj, attribute_name)
            func(*args, **kwargs)

            def _callback():
                setattr(obj, attribute_name, prev_mode)

            return _callback

        repeatable = False  # attribute actions are always non-repeatable
        register_layer_action(
            keymapprovider, description, repeatable, shortcuts
        )(_wrapper)
        return func

    return _handle


def _nanmin(array):
    """
    call np.min but fall back to avoid nan and inf if necessary
    """
    return _native.layer_nanmin(array, globals())


def _nanmax(array):
    """
    call np.max but fall back to avoid nan and inf if necessary
    """
    return _native.layer_nanmax(array, globals())


@_native_dispatch("range_adapter")
def calc_data_range(
    data: LayerDataProtocol, rgb: bool = False, dtype: np.dtype | None = None
) -> tuple[float, float]:
    """Calculate range of data values. If all values are equal return [0, 1].

    Parameters
    ----------
    data : array
        Data to calculate range of values over.
    rgb : bool
        Flag if data is rgb.
    dtype : np.dtype, optional
        Dtype of the layer data. If None, the dtype of the data is used.

    Returns
    -------
    values : pair of floats
        Minimum and maximum values in that order.

    Notes
    -----
    If the data type is uint8, no calculation is performed, and 0-255 is
    returned.
    """
    return _native.layer_calc_data_range(
        data, globals(), rgb=rgb, dtype=dtype
    )


@_native_dispatch("normal_adapter")
def segment_normal(a, b, p=(0, 0, 1)) -> np.ndarray:
    """Determines the unit normal of the vector from a to b.

    Parameters
    ----------
    a : np.ndarray
        Length 2 array of first point or Nx2 array of points
    b : np.ndarray
        Length 2 array of second point or Nx2 array of points
    p : 3-tuple, optional
        orthogonal vector for segment calculation in 3D.

    Returns
    -------
    unit_norm : np.ndarray
        Length the unit normal of the vector from a to b. If a == b,
        then returns [0, 0] or Nx2 array of vectors
    """
    locals_owner = _native.shape_accel_locals(('a', 'b', 'p', 'd', 'normal', 'norm', 'ind'), {'a': a, 'b': b, 'p': p}, {})
    del a, b, p
    norm: Any
    return _native.shape_accel_segment_normal(_native.shape_accel_namespace(), ('Determines the unit normal of the vector from a to b.\n\n    Parameters\n    ----------\n    a : np.ndarray\n        Length 2 array of first point or Nx2 array of points\n    b : np.ndarray\n        Length 2 array of second point or Nx2 array of points\n    p : 3-tuple, optional\n        orthogonal vector for segment calculation in 3D.\n\n    Returns\n    -------\n    unit_norm : np.ndarray\n        Length the unit normal of the vector from a to b. If a == b,\n        then returns [0, 0] or Nx2 array of vectors\n    ', 1, 2, 0, None, ('axis',), True, ('axis', 'keepdims')), locals_owner)


def convert_to_uint8(data: np.ndarray) -> np.ndarray:
    """
    Convert array content to uint8, always returning a copy.

    Based on skimage.util.dtype._convert but limited to an output type uint8,
    so should be equivalent to skimage.util.dtype.img_as_ubyte.

    If all negative, values are clipped to 0.

    If values are integers and below 256, this simply casts.
    Otherwise the maximum value for the input data type is determined and
    output values are proportionally scaled by this value.

    Binary images are converted so that False -> 0, True -> 255.

    Float images are multiplied by 255 and then cast to uint8.
    """
    out_dtype: np.dtype
    locals_owner = _native.layer_thumbnail_locals()
    return _native.layer_convert_to_uint8(data, globals(), locals_owner)


def get_current_properties(
    properties: dict[str, np.ndarray],
    choices: dict[str, np.ndarray],
    num_data: int = 0,
) -> dict[str, Any]:
    """Get the current property values from the properties or choices.

    Parameters
    ----------
    properties : dict[str, np.ndarray]
        The property values.
    choices : dict[str, np.ndarray]
        The property value choices.
    num_data : int
        The length of data that the properties represent (e.g. number of points).

    Returns
    -------
    dict[str, Any]
        A dictionary where the key is the property name and the value is the current
        value of that property.
    """
    current_properties = {}
    if num_data > 0:
        current_properties = {
            k: np.asarray([v[-1]]) for k, v in properties.items()
        }
    elif num_data == 0 and len(choices) > 0:
        current_properties = {
            k: np.asarray([v[0]]) for k, v in choices.items()
        }
    return current_properties


def dataframe_to_properties(
    dataframe: pd.DataFrame,
) -> dict[str, np.ndarray]:
    """Convert a dataframe to a properties dictionary.
    Parameters
    ----------
    dataframe : DataFrame
        The dataframe object to be converted to a properties dictionary
    Returns
    -------
    dict[str, np.ndarray]
        A properties dictionary where the key is the property name and the value
        is an ndarray with the property value for each point.
    """
    return {col: np.asarray(dataframe[col]) for col in dataframe}


def validate_properties(
    properties: dict[str, Array] | pd.DataFrame | None,
    expected_len: int | None = None,
) -> dict[str, np.ndarray]:
    """Validate the type and size of properties and coerce values to numpy arrays.
    Parameters
    ----------
    properties : dict[str, Array] or DataFrame
        The property values.
    expected_len : int
        The expected length of each property value array.
    Returns
    -------
    Dict[str, np.ndarray]
        The property values.
    """
    if properties is None or len(properties) == 0:
        return {}

    if not isinstance(properties, dict):
        properties = dataframe_to_properties(properties)

    lens = [len(v) for v in properties.values()]
    if expected_len is None:
        expected_len = lens[0]
    if any(v != expected_len for v in lens):
        raise ValueError(
            'the number of items must be equal for all properties'
        )

    return {k: np.asarray(v) for k, v in properties.items()}


def _validate_property_choices(property_choices):
    if property_choices is None:
        property_choices = {}
    return {k: np.unique(v) for k, v in property_choices.items()}


def _coerce_current_properties_value(
    value: float | str | bool | list | tuple | np.ndarray,
) -> np.ndarray:
    """Coerce a value in a current_properties dictionary into the correct type.

    Parameters
    ----------
    value : Union[float, str, int, bool, list, tuple, np.ndarray]
        The value to be coerced.

    Returns
    -------
    coerced_value : np.ndarray
        The value in a 1D numpy array with length 1.
    """
    if isinstance(value, np.ndarray | list | tuple):
        if len(value) != 1:
            raise ValueError('current_properties values should have length 1.')
        coerced_value = np.asarray(value)
    else:
        coerced_value = np.array([value])

    return coerced_value


def coerce_current_properties(
    current_properties: Mapping[
        str, float | str | int | bool | list | tuple | npt.NDArray
    ],
) -> dict[str, np.ndarray]:
    """Coerce a current_properties dictionary into the correct type.


    Parameters
    ----------
    current_properties : Dict[str, Union[float, str, int, bool, list, tuple, np.ndarray]]
        The current_properties dictionary to be coerced.

    Returns
    -------
    coerced_current_properties : Dict[str, np.ndarray]
        The current_properties dictionary with string keys and 1D numpy array with length 1 values.
    """
    coerced_current_properties = {
        k: _coerce_current_properties_value(v)
        for k, v in current_properties.items()
    }

    return coerced_current_properties


def compute_multiscale_level(
    requested_shape, shape_threshold, downsample_factors
):
    """Computed desired level of the multiscale given requested field of view.

    The level of the multiscale should be the lowest resolution such that
    the requested shape exceeds the shape threshold in the dimensions that
    contribute to level selection. Dimensions that already fit within the
    canvas or are not downsampled across pyramid levels are ignored. By
    passing a shape threshold corresponding to the size of the canvas, this
    ensures that we have at least one data pixel per screen pixel, but no
    more than necessary.

    Parameters
    ----------
    requested_shape : tuple
        Requested shape of field of view in data coordinates
    shape_threshold : tuple
        Maximum size of a displayed tile in pixels.
    downsample_factors : list of tuple
        Downsampling factors for each level of the multiscale. Must be increasing
        for each level of the multiscale.

    Returns
    -------
    level : int
        Level of the multiscale to be viewing.
    """

    # Scale shape by downsample factors
    locals_owner = _native.scalar_viewport_policy_locals()
    return _native.scalar_viewport_level(
        requested_shape, shape_threshold, downsample_factors, globals(), locals_owner
    )


def compute_multiscale_level_and_corners(
    corner_pixels, shape_threshold, downsample_factors
):
    """Computed desired level and corners of a multiscale view.

    The level of the multiscale should be the lowest resolution such that
    the requested shape is above the shape threshold. By passing a shape
    threshold corresponding to the shape of the canvas on the screen this
    ensures that we have at least one data pixel per screen pixel, but no
    more than we need.

    Parameters
    ----------
    corner_pixels : array (2, D)
        Requested corner pixels at full resolution.
    shape_threshold : tuple
        Maximum size of a displayed tile in pixels.
    downsample_factors : list of tuple
        Downsampling factors for each level of the multiscale. Must be increasing
        for each level of the multiscale.

    Returns
    -------
    level : int
        Level of the multiscale to be viewing.
    corners : array (2, D)
        Needed corner pixels at target resolution.
    """
    locals_owner = _native.scalar_viewport_policy_locals()
    return _native.scalar_viewport_level_corners(
        corner_pixels, shape_threshold, downsample_factors, globals(), locals_owner
    )


def _chunk_boundaries(axis_chunks: Any, axis_size: int) -> np.ndarray | None:
    """Return chunk boundary coordinates for one axis.

    This accepts the common chunk metadata forms:
    - zarr/tensorstore: one regular chunk size for the axis, e.g. ``64``
    - dask: explicit chunk sizes for the axis, e.g. ``(64, 64, 32)``

    The returned boundaries include both ``0`` and ``axis_size``.  For example,
    chunks ``(4, 4, 2)`` for an axis of size ``10`` return ``[0, 4, 8, 10]``.
    """
    def generator_factory(native_iterator):
        yield from native_iterator

    locals_owner = _native.scalar_viewport_policy_locals()
    return _native.scalar_viewport_chunk_boundaries(
        axis_chunks, axis_size, globals(), generator_factory, locals_owner
    )


def _chunks_metadata(
    data: LayerDataProtocol,
) -> Sequence[int | Sequence[int] | None] | None:
    """Return zarr/dask-style per-axis ``chunks`` metadata, or ``None``.

    dask and zarr expose this directly as ``chunks``. tensorstore has no
    ``chunks`` attribute; its read granularity lives on
    ``chunk_layout.read_chunk.shape``, a per-axis tuple matching the zarr form
    (with ``None`` for axes that are not chunked). ``read_chunk`` is the
    smallest independently-readable unit.

    Returns
    -------
    Sequence[int | Sequence[int] | None] | None
        A sequence with one entry per axis, each entry either a single chunk
        size (regular grid, as zarr and tensorstore expose) or a sequence of
        explicit chunk sizes (as dask exposes).
        Returns ``None`` when the array exposes no usable chunk metadata.
    """
    # zarr/dask-like arrays with ``chunks`` attribute
    def generator_factory(native_iterator):
        yield from native_iterator

    locals_owner = _native.scalar_viewport_policy_locals()
    return _native.scalar_viewport_chunks_metadata(
        data, globals(), generator_factory, locals_owner
    )


def expand_corners_to_chunk_boundaries(
    corners: npt.NDArray,
    data: LayerDataProtocol,
    axes: Sequence[int],
) -> npt.NDArray:
    """Expand inclusive corner bounds to include complete data chunks.

    For chunked arrays, reading any part of a chunk loads the whole chunk
    from the backing store, so this expands the requested bounds outward to chunk
    boundaries.

    Parameters
    ----------
    corners : array (2, D)
        Inclusive lower (``corners[0]``) and upper (``corners[1]``) corner
        pixel bounds in the coordinate space of ``data``. The bounds must be
        ordered per axis (``corners[0] <= corners[1]``).
    data : LayerDataProtocol
        Array whose chunk metadata drives the expansion. Supported forms are
        dask arrays (``chunks`` is a tuple of per-axis chunk-size tuples), zarr
        arrays (``chunks`` is the regular chunk shape), and tensorstore arrays
        (read granularity read from ``chunk_layout.read_chunk``).
    axes : sequence of int
        Axes to expand, typically the displayed axes. Other axes (e.g. sliced
        ones) are left untouched, so they keep their single-plane bounds.

    Returns
    -------
    array (2, D)
        Corner bounds expanded outward to chunk boundaries along ``axes``. A
        new array is returned when expansion happens; ``corners`` is returned
        unchanged (and unmodified) for arrays without usable chunk metadata.
    """
    locals_owner = _native.scalar_viewport_policy_locals()
    return _native.scalar_viewport_expand_chunks(
        corners, data, axes, globals(), locals_owner
    )


def coerce_affine(
    affine: npt.ArrayLike | Affine,
    *,
    ndim: int,
    name: str | None = None,
) -> Affine:
    """Coerce a user input into an affine transform object.

    If the input is already an affine transform object, that same object is returned
    with a name change if the given name is not None. If the input is None, an identity
    affine transform object of the given dimensionality is returned.

    Parameters
    ----------
    affine : array-like or napari.utils.transforms.Affine
        An existing affine transform object or an array-like that is its transform matrix.
    ndim : int
        The desired dimensionality of the transform. Ignored is affine is an Affine transform object.
    name : str
        The desired name of the transform.

    Returns
    -------
    napari.utils.transforms.Affine
        The input coerced into an affine transform object.
    """
    locals_owner = _native.policy_transform_coerce_locals(affine)
    del affine
    return _native.policy_transform_coerce(ndim, name, globals(), locals_owner)


def dims_displayed_world_to_layer(
    dims_displayed_world: list[int],
    ndim_world: int,
    ndim_layer: int,
) -> list[int]:
    """Convert the dims_displayed from world dims to the layer dims.

    This accounts differences in the number of dimensions in the world
    dims versus the layer and for transpose and rolls.

    Parameters
    ----------
    dims_displayed_world : List[int]
        The dims_displayed in world coordinates (i.e., from viewer.dims.displayed).
    ndim_world : int
        The number of dimensions in the world coordinates (i.e., viewer.dims.ndim)
    ndim_layer : int
        The number of dimensions in layer the layer (i.e., layer.ndim).
    """
    owner = _native.viewer_geometry_locals()
    return _native.viewer_geometry_world_dims(dims_displayed_world, ndim_world, ndim_layer, globals(), owner)


def get_extent_world(
    data_extent: npt.NDArray,
    data_to_world: Affine,
    centered: Any | None = None,
) -> npt.NDArray:
    """Range of layer in world coordinates base on provided data_extent

    Parameters
    ----------
    data_extent : array, shape (2, D)
        Extent of layer in data coordinates.
    data_to_world : napari.utils.transforms.Affine
        The transform from data to world coordinates.

    Returns
    -------
    extent_world : array, shape (2, D)
    """
    locals_owner = _native.layer_geometry_locals()
    return _native.layer_geometry_get_extent_world(
        data_extent,
        data_to_world,
        centered,
        globals(),
        locals_owner,
        'The `centered` argument is deprecated. '
        'Extents are now always centered on data points.',
    )


def features_to_pandas_dataframe(features: Any) -> pd.DataFrame:
    """Coerces a layer's features property to a pandas DataFrame.

    In general, this may copy the data from features into the returned
    DataFrame so there is no guarantee that changing element values in the
    returned DataFrame will also change values in the features property.

    Parameters
    ----------
    features
        The features property of a layer.

    Returns
    -------
    pd.DataFrame
        A pandas DataFrame that stores the given features.
    """
    return features


class _FeatureTable:
    """Stores feature values and their defaults.

    Parameters
    ----------
    values : Optional[Union[Dict[str, np.ndarray], pd.DataFrame]]
        The features values, which will be passed to the pandas DataFrame initializer.
        If this is a pandas DataFrame with a non-default index, that index
        (except its length) will be ignored.
    num_data : Optional[int]
        The number of the elements in the layer calling this, such as
        the number of points, which is used to check that the features
        table has the expected number of rows. If None, then the default
        DataFrame index is used.
    defaults: Optional[Union[Dict[str, Any], pd.DataFrame]]
        The default feature values, which if specified should have the same keys
        as the values provided. If None, will be inferred from the values.
    """

    def __init__(
        self,
        values: dict[str, np.ndarray] | pd.DataFrame | None = None,
        *,
        num_data: int | None = None,
        defaults: dict[str, Any] | pd.DataFrame | None = None,
    ) -> None:
        self._values = _validate_features(values, num_data=num_data)
        self._defaults = _validate_feature_defaults(defaults, self._values)

    @property
    def values(self) -> pd.DataFrame:
        """The feature values table."""
        return self._values

    def set_values(self, values, *, num_data=None) -> None:
        """Sets the feature values table."""
        self._values = _validate_features(values, num_data=num_data)
        self._defaults = _validate_feature_defaults(None, self._values)

    @property
    def defaults(self) -> pd.DataFrame:
        """The default values one-row table."""
        return self._defaults

    def set_defaults(self, defaults: dict[str, Any] | pd.DataFrame) -> None:
        """Sets the feature default values."""
        self._defaults = _validate_feature_defaults(defaults, self._values)

    def properties(self) -> dict[str, np.ndarray]:
        """Converts this to a deprecated properties dictionary.

        This will reference the features data when possible, but in general the
        returned dictionary may contain copies of those data.

        Returns
        -------
        Dict[str, np.ndarray]
            The properties dictionary equivalent to the given features.
        """
        return _features_to_properties(self._values)

    def choices(self) -> dict[str, np.ndarray]:
        """Converts this to a deprecated property choices dictionary.

        Only categorical features will have corresponding entries in the dictionary.

        Returns
        -------
        Dict[str, np.ndarray]
            The property choices dictionary equivalent to this.
        """
        return {
            name: series.dtype.categories.to_numpy()
            for name, series in self._values.items()
            if isinstance(series.dtype, pd.CategoricalDtype)
        }

    def currents(self) -> dict[str, np.ndarray]:
        """Converts the defaults table to a deprecated current properties dictionary."""
        return _features_to_properties(self._defaults)

    def set_currents(
        self,
        currents: dict[str, npt.NDArray],
        *,
        update_indices: list[int] | None = None,
    ) -> None:
        """Sets the default values using the deprecated current properties dictionary.

        May also update some of the feature values to be equal to the new default values.

        Parameters
        ----------
        currents : Dict[str, np.ndarray]
            The new current property values.
        update_indices : Optional[List[int]]
            If not None, the all features values at the given row indices will be set to
            the corresponding new current/default feature values.
        """
        currents = coerce_current_properties(currents)
        self._defaults = _validate_features(currents, num_data=1)
        if update_indices is not None:
            for k in self._defaults:
                self._values.loc[update_indices, k] = self._defaults[k][0]

    def resize(
        self,
        size: int,
    ) -> None:
        """Resize this padding with default values if required.

        Parameters
        ----------
        size : int
            The new size (number of rows) of the features table.
        """
        current_size = self._values.shape[0]
        if size < current_size:
            self.remove(range(size, current_size))
        elif size > current_size:
            to_append = self._defaults.iloc[np.zeros(size - current_size)]
            self.append(to_append)

    def append(self, to_append: pd.DataFrame) -> None:
        """Append new feature rows to this.

        Parameters
        ----------
        to_append : pd.DataFrame
            The features to append.
        """
        self._values = pd.concat([self._values, to_append], ignore_index=True)

    def remove(self, indices: Any) -> None:
        """Remove rows from this by index.

        Parameters
        ----------
        indices : Any
            The indices of the rows to remove. Must be usable as the labels parameter
            to pandas.DataFrame.drop.
        """
        self._values = self._values.drop(labels=indices, axis=0).reset_index(
            drop=True
        )

    def reorder(self, order: Sequence[int]) -> None:
        """Reorders the rows of the feature values table."""
        self._values = self._values.iloc[order].reset_index(drop=True)

    @classmethod
    def from_layer(
        cls,
        *,
        features: dict[str, np.ndarray] | pd.DataFrame | None = None,
        feature_defaults: dict[str, Any] | pd.DataFrame | None = None,
        properties: dict[str, np.ndarray] | pd.DataFrame | None = None,
        property_choices: dict[str, np.ndarray] | None = None,
        num_data: int | None = None,
    ) -> _FeatureTable:
        """Coerces a layer's keyword arguments to a feature manager.

        Parameters
        ----------
        features : Optional[Union[Dict[str, np.ndarray], pd.DataFrame]]
            The features input to a layer.
        properties : Optional[Union[Dict[str, np.ndarray], pd.DataFrame]]
            The properties input to a layer.
        property_choices : Optional[Dict[str, np.ndarray]]
            The property choices input to a layer.
        num_data : Optional[int]
            The number of the elements in the layer calling this, such as
            the number of points.

        Returns
        -------
        _FeatureTable
            The feature manager created from the given layer keyword arguments.

        Raises
        ------
        ValueError
            If the input property columns are not all the same length, or if
            that length is not equal to the given num_data.
        """
        if properties is not None or property_choices is not None:
            features = _features_from_properties(
                properties=properties,
                property_choices=property_choices,
                num_data=num_data,
            )
        return cls(features, defaults=feature_defaults, num_data=num_data)


def _get_default_column(column: pd.Series) -> pd.Series:
    """Get the default column of length 1 from a data column."""
    value = None
    if column.size > 0:
        value = column.iloc[-1]
    elif isinstance(column.dtype, pd.CategoricalDtype):
        choices = column.dtype.categories
        if choices.size > 0:
            value = choices[0]
    elif isinstance(column.dtype, np.dtype) and np.issubdtype(
        column.dtype, np.integer
    ):
        # For numpy backed columns that store integers there's no way to
        # store missing values, so passing None creates an np.float64 series
        # containing NaN. Therefore, use a default of 0 instead.
        value = 0
    return pd.Series(data=[value], dtype=column.dtype, index=range(1))


def _validate_features(
    features: dict[str, np.ndarray] | pd.DataFrame | None,
    *,
    num_data: int | None = None,
) -> pd.DataFrame:
    """Validates and coerces feature values into a pandas DataFrame.

    See Also
    --------
    :class:`_FeatureTable` : See initialization for parameter descriptions.
    """
    if isinstance(features, pd.DataFrame):
        features = features.reset_index(drop=True)
    elif isinstance(features, dict):
        # Convert all array-like objects into a numpy array.
        # This section was introduced due to an unexpected behavior when using
        # a pandas Series with mixed indices as input.
        # This way should handle all array-like objects correctly.
        # See https://github.com/napari/napari/pull/4755 for more details.
        features = {key: np.asarray(value) for key, value in features.items()}
    index = None if num_data is None else range(num_data)
    return pd.DataFrame(data=features, index=index)


def _validate_feature_defaults(
    defaults: dict[str, Any] | pd.DataFrame | None,
    values: pd.DataFrame,
) -> pd.DataFrame:
    """Validates and coerces feature default values into a pandas DataFrame.

    See Also
    --------
    :class:`_FeatureTable` : See initialization for parameter descriptions.
    """
    if defaults is None:
        defaults = {c: _get_default_column(values[c]) for c in values.columns}
    else:
        default_columns = set(defaults.keys())
        value_columns = set(values.keys())
        extra_defaults = default_columns - value_columns
        if len(extra_defaults) > 0:
            raise ValueError(
                f'Feature defaults contain some extra columns not in feature values: {extra_defaults}'
            )
        missing_defaults = value_columns - default_columns
        if len(missing_defaults) > 0:
            raise ValueError(
                f'Feature defaults is missing some columns in feature values: {missing_defaults}'
            )
        # Convert to series first to capture the per-column dtype from values,
        # since the DataFrame initializer does not support passing multiple dtypes.
        defaults = {
            c: pd.Series(
                defaults[c],
                dtype=values.dtypes[c],
                index=range(1),
            )
            for c in defaults
        }

    return pd.DataFrame(defaults, index=range(1))


def _features_from_properties(
    *,
    properties: dict[str, np.ndarray] | pd.DataFrame | None = None,
    property_choices: dict[str, np.ndarray] | None = None,
    num_data: int | None = None,
) -> pd.DataFrame:
    """Validates and coerces deprecated properties input into a features DataFrame.

    See Also
    --------
    :meth:`_FeatureTable.from_layer`
    """
    # Create categorical series for any choices provided.
    if property_choices is not None:
        properties_df = pd.DataFrame(data=properties)
        for name, choices in property_choices.items():
            dtype = pd.CategoricalDtype(categories=choices)
            num_values = (
                properties_df.shape[0] if num_data is None else num_data
            )
            values = (
                properties_df[name]
                if name in properties_df
                else [None] * num_values
            )
            properties_df[name] = pd.Series(values, dtype=dtype)
        properties = properties_df
    return _validate_features(properties, num_data=num_data)


def _features_to_properties(features: pd.DataFrame) -> dict[str, np.ndarray]:
    """Converts a features DataFrame to a deprecated properties dictionary.

    See Also
    --------
    :meth:`_FeatureTable.properties`
    """
    return {name: series.to_numpy() for name, series in features.items()}


def _unique_element(array: Array) -> Any | None:
    """
    Returns the unique element along the 0th axis, if it exists; otherwise, returns None.

    This is faster than np.unique, does not require extra tricks for nD arrays, and
    does not fail for non-sortable elements.
    """
    if len(array) == 0:
        return None
    el = array[0]
    if len(array) == 1:
        return el
    if isinstance(el, (list, tuple, np.ndarray)):
        # Container-valued elements (e.g. list-valued features, or a 2D
        # array of per-point RGBA colors) can't be safely compared via
        # numpy broadcasting: `array[1:] != el` raises on ragged/differently
        # shaped elements, and `np.equal(array[1:], array[:1])` raises
        # "ambiguous truth value" once any element has more than one entry.
        # Compare them individually with np.array_equal instead, which
        # returns False rather than raising on a shape mismatch.
        if all(np.array_equal(x, el) for x in array[1:]):
            return el
        return None
    if np.any(array[1:] != el):
        return None
    return el
