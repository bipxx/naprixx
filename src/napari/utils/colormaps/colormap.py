from collections import defaultdict
from collections.abc import MutableMapping, Sequence
from enum import StrEnum
from functools import cached_property
from typing import (
    TYPE_CHECKING,
    Annotated,
    Any,
    Literal,
    Self,
    cast,
    overload,
)
from warnings import warn

import numpy as np
from pydantic import (
    ConfigDict,
    Field,
    PrivateAttr,
    ValidationInfo,
    field_validator,
)

from napari.utils.color import ColorArray, ColorValue
from napari.utils.colormaps import _accelerated_cmap as _accel_cmap
from napari.utils.colormaps.colorbars import make_colorbar
from napari.utils.colormaps.standardize_color import transform_color
from napari.utils.events import EventedModel
from napari.utils.events.custom_types import Array
from napari.utils.migrations import deprecated_class_name

if TYPE_CHECKING:
    from numba import typed


__all__ = (
    'Colormap',
    'ColormapInterpolationMode',
    'CyclicLabelColormap',
    'DirectLabelColormap',
)


class ColormapInterpolationMode(StrEnum):
    """INTERPOLATION: Interpolation mode for colormaps.

    Selects an interpolation mode for the colormap.
            * linear: colors are defined by linear interpolation between
              colors of neighboring controls points.
            * zero: colors are defined by the value of the color in the
              bin between by neighboring controls points.
    """

    LINEAR = 'linear'
    ZERO = 'zero'


class Colormap(EventedModel):
    """Colormap that relates intensity values to colors.

    Attributes
    ----------
    colors : array, shape (N, 4)
        Data used in the colormap.
    name : str
        Name of the colormap.
    _display_name : str
        Display name of the colormap.
    controls : array, shape (N,) or (N+1,)
        Control points of the colormap.
    interpolation : str
        Colormap interpolation mode, either 'linear' or
        'zero'. If 'linear', ncontrols = ncolors (one
        color per control point). If 'zero', ncontrols
        = ncolors+1 (one color per bin).
    nan_color : ColorValue
        Mapping for NaN values. Equivalent to matplotlib's `bad_color`
    high_color : ColorValue
        Mapping for values equal to or greater than 1.
    low_color : ColorValue
        Mapping for values equal to or less than 0.
    """

    # fields
    colors: ColorArray
    name: str = 'custom'
    _display_name: str | None = PrivateAttr(None)
    interpolation: ColormapInterpolationMode = ColormapInterpolationMode.LINEAR
    controls: Array = Field(
        default_factory=lambda: cast(Array, []), validate_default=True
    )
    nan_color: ColorValue = ColorValue('transparent')
    high_color: ColorValue | None = None
    low_color: ColorValue | None = None

    def __init__(
        self, colors, display_name: str | None = None, **data
    ) -> None:
        if display_name is None:
            display_name = data.get('name', 'custom')

        super().__init__(colors=colors, **data)
        self._display_name = display_name

    # controls validator must be called even if None for correct initialization
    @field_validator('controls', mode='after')
    @classmethod
    def _check_controls(cls, v, info):
        # If no control points provided generate defaults
        if v is None or len(v) == 0:
            n_controls = len(info.data['colors']) + int(
                info.data['interpolation'] == ColormapInterpolationMode.ZERO
            )
            return np.linspace(0, 1, n_controls, dtype=np.float32)

        # Check control end points are correct
        if v[0] != 0 or (len(v) > 1 and v[-1] != 1):
            raise ValueError(
                'Control points must start with 0.0 and end with 1.0. '
                f'Got {v[0]} and {v[-1]}'
            )

        # Check control points are sorted correctly
        if not np.array_equal(v, sorted(v)):
            raise ValueError(
                'Control points need to be sorted in ascending order'
            )

        # Check number of control points is correct
        n_controls_target = len(info.data.get('colors', [])) + int(
            info.data['interpolation'] == ColormapInterpolationMode.ZERO
        )
        n_controls = len(v)
        if n_controls != n_controls_target:
            raise ValueError(
                f'Wrong number of control points provided. Expected {n_controls_target}, got {n_controls}'
            )

        return v

    def __len__(self):
        return len(self.colors)

    def map(self, values):
        from naparixx import _native

        locals_owner = _native.layer_colormap_locals(values)
        del values
        return _native.layer_colormap_map(self, globals(), locals_owner)

    @property
    def colorbar(self):
        return make_colorbar(self)

    def same_colors(self, other: 'Colormap') -> bool:
        return bool(
            np.array_equal(self.controls, other.controls)
            and np.array_equal(self.colors, other.colors)
            and self.interpolation == other.interpolation
            and np.all(self.nan_color == other.nan_color)
            and np.all(self.high_color == other.high_color)
            and np.all(self.low_color == other.low_color)
        )


class _RebuildableCache(dict):
    """Cache dict that is emptied on (deep)copy and rebuilt lazily.

    ``DirectLabelColormap`` stores a numba ``typed.Dict`` here after a layer
    has been rendered; that object holds an unpicklable
    ``_nrt_python._MemInfo`` that breaks ``copy.deepcopy`` of the colormap.
    The cache is a pure performance memoization, so emptying it on copy is
    always safe -- it is recomputed on the next ``map`` call. Defending the
    value itself keeps this independent of where pydantic stores private
    attributes or which hook a copy routes through.
    """

    def __deepcopy__(self, memo):
        from naparixx import _native

        return _native.vispy_labels_cache_deepcopy(self, globals())


class LabelColormapBase(Colormap):
    use_selection: bool = False
    selection: int = 0
    background_value: int = 0
    interpolation: Literal[ColormapInterpolationMode.ZERO] = Field(  # pyrefly: ignore [bad-override]
        ColormapInterpolationMode.ZERO, frozen=True
    )
    _cache_mapping: dict[tuple[np.dtype, np.dtype], np.ndarray] = PrivateAttr(
        default_factory=_RebuildableCache
    )
    _cache_other: dict[str, Any] = PrivateAttr(
        default_factory=_RebuildableCache
    )

    model_config = EventedModel.model_config | ConfigDict(
        # this config is to avoid deepcopy of cached_property
        # see https://github.com/pydantic/pydantic/issues/2763
        # it is required until we drop Pydantic 1 or Python 3.11 and older
        # need to validate after drop pydantic 1
        ignored_types=(cached_property,)
        # TODO: ensure that this is actually needed
    )

    @overload
    def _data_to_texture(self, values: np.ndarray) -> np.ndarray: ...

    @overload
    def _data_to_texture(self, values: np.integer) -> np.integer: ...

    def _data_to_texture(
        self, values: np.ndarray | np.integer
    ) -> np.ndarray | np.integer:
        """Map input values to values for send to GPU."""
        raise NotImplementedError

    def _cmap_without_selection(self) -> Self:
        from naparixx import _native

        if _native.label_auto_texture_use_selection(self):
            cmap = _native.labelmap_clone(self)
            _native.labelmap_disable_selection(cmap)
            return cmap
        return self

    def _get_mapping_from_cache(
        self, data_dtype: np.dtype
    ) -> np.ndarray | None:
        """For given dtype, return precomputed array mapping values to colors.

        Returns None if the dtype itemsize is greater than 2.
        """
        from naparixx import _native

        target_dtype = _native.labelmap_cache_target(self, data_dtype, globals())
        key = _native.labelmap_cache_key(data_dtype, target_dtype)
        if _native.labelmap_cache_needs_fill(self, key, data_dtype):
            data = _native.labelmap_cache_data(data_dtype, target_dtype, globals())
            _native.labelmap_cache_publish(self, key, data)
        return _native.labelmap_cache_get(self, key)

    def _clear_cache(self):
        """Mechanism to clean cached properties"""
        from naparixx import _native

        _native.direct_cache_base_clear(self, globals())

    @property
    def _num_unique_colors(self) -> int:
        """Number of unique colors, not counting transparent black."""
        from naparixx import _native

        return _native.label_auto_texture_num_colors(self, globals())

    def _map_without_cache(self, values: np.ndarray) -> np.ndarray:
        """Function that maps values to colors without selection or cache"""
        raise NotImplementedError

    def _selection_as_minimum_dtype(self, dtype: np.dtype) -> int:
        """Treat selection as given dtype and calculate value with min dtype.

        Parameters
        ----------
        dtype : np.dtype
            The dtype to convert the selection to.

        Returns
        -------
        int
            The selection converted.
        """
        from naparixx import _native

        return _native.vispy_labels_selection_minimum(self, dtype, globals())


class CyclicLabelColormap(LabelColormapBase):
    """Color cycle with a background value.

    Attributes
    ----------
    colors : ColorArray
        Colors to be used for mapping.
        For values above the number of colors,
        the colors will be cycled.
    use_selection : bool
        Whether map only selected label.
        If `True` only selected label will be mapped to not transparent color.
    selection : int
        The selected label.
    background_value : int
        Which value should be treated as a background
        and mapped to transparent color.
    interpolation : Literal['zero']
        required by implementation, please do not set value
    seed : float
        seed used for random color generation. Used for reproducibility.
        It will be removed in the future release.
    """

    seed: float = 0.5

    @field_validator('colors', mode='after')
    @classmethod
    def _validate_color(cls, v):
        if len(v) > 2**16:
            raise ValueError(
                'Only up to 2**16=65535 colors are supported for LabelColormap'
            )
        return v

    def _background_as_minimum_dtype(self, dtype: np.dtype) -> int:
        """Treat background as given dtype and calculate value with min dtype.

        Parameters
        ----------
        dtype : np.dtype
            The dtype to convert the background to.

        Returns
        -------
        int
            The background converted.
        """
        from naparixx import _native

        return _native.vispy_labels_background_minimum(self, dtype, globals())

    @overload
    def _data_to_texture(self, values: np.ndarray) -> np.ndarray: ...

    @overload
    def _data_to_texture(self, values: np.integer) -> np.integer: ...

    def _data_to_texture(
        self, values: np.ndarray | np.integer
    ) -> np.ndarray | np.integer:
        """Map input values to values for send to GPU."""
        from naparixx import _native

        return _native.label_cyclic_data_to_texture(self, values, globals())

    def _map_without_cache(self, values) -> np.ndarray:
        from naparixx import _native

        texture_dtype_values = _native.labelmap_cyclic_texture(self, values, globals())
        mapped = _native.labelmap_cyclic_lookup(self, texture_dtype_values)
        _native.labelmap_cyclic_zero(mapped, texture_dtype_values)
        return mapped

    def map(self, values: np.ndarray | np.integer | int) -> np.ndarray:
        """Map values to colors.

        Parameters
        ----------
        values : np.ndarray or int
            Values to be mapped.

        Returns
        -------
        np.ndarray of the same shape as values,
            but with the last dimension of size 4
            Mapped colors.
        """
        from naparixx import _native

        original_shape = _native.label_texture_shape(values, globals())
        values = _native.label_texture_array(values, globals())
        if _native.labelmap_is_float(values):
            values = _native.labelmap_to_int64(values, globals())
        mapper = _native.labelmap_cache(self, values)
        if _native.labelmap_cache_present(mapper):
            mapped = _native.labelmap_index(mapper, values)
        else:
            mapped = _native.labelmap_uncached(self, values)
        if _native.label_auto_texture_use_selection(self):
            _native.labelmap_mask(self, values, mapped)
        return _native.labelmap_reshape(mapped, original_shape, globals())

    def shuffle(self, seed: int):
        """Shuffle the colormap colors.

        Parameters
        ----------
        seed : int
            Seed for the random number generator.
        """
        np.random.default_rng(seed).shuffle(self.colors[1:])
        self.events.colors(value=self.colors)


LabelColormap = deprecated_class_name(
    CyclicLabelColormap,
    'LabelColormap',
    version='0.5.0',
    since_version='0.4.19',
)


class DirectLabelColormap(LabelColormapBase):
    """Colormap using a direct mapping from labels to color using a dict.

    Attributes
    ----------
    color_dict: dict from int to (3,) or (4,) array
        The dictionary mapping labels to colors.

    use_selection : bool
        Whether to map only the selected label to a color.
        If `True` only selected label will be not transparent.
    selection : int
        The selected label.
    colors : ColorArray
        Exist because of implementation details. Please do not use it.
    """

    color_dict: defaultdict[
        int | None,
        Annotated[np.ndarray, Field(default_factory=lambda: np.zeros(4))],
    ] = Field(default_factory=lambda: defaultdict(lambda: np.zeros(4)))
    use_selection: bool = False
    selection: int = 0

    def __init__(self, *args, **kwargs) -> None:
        if 'colors' not in kwargs and not args:
            kwargs['colors'] = np.zeros(3)
        super().__init__(*args, **kwargs)

    def __len__(self):
        """Overwrite from base class because .color is a dummy array.

        This returns the number of colors in the colormap, including
        background and unmapped labels.
        """
        return self._num_unique_colors + 2

    @field_validator('color_dict', mode='before')
    @classmethod
    def _validate_color_dict(cls, v: MutableMapping, values: ValidationInfo):
        """Ensure colors are RGBA arrays, not strings.

        Parameters
        ----------
        cls : type
            The class of the object being instantiated.
        v : MutableMapping
            A mapping from integers to colors. It *may* have None as a key,
            which indicates the color to map items not in the dictionary.
            Alternatively, it could be a defaultdict. If neither is provided,
            missing colors are not rendered (rendered as fully transparent).
        values : ValidationInfo
            A structure with information about previously validated attributes

        Returns
        -------
        res : (default)dict[int, np.ndarray[float]]
            A properly-formatted dictionary mapping labels to RGBA arrays.
        """
        if not isinstance(v, defaultdict) and None not in v:
            warn(
                'color_dict did not provide a default color. '
                'Missing keys will be transparent. '
                'To provide a default color, use the key `None`, '
                'or provide a defaultdict instance.'
            )
            v = {**v, None: 'transparent'}
        res = {
            label: transform_color(color_str)[0]
            for label, color_str in v.items()
        }
        if (
            'background_value' in values.data
            and (bg := values.data['background_value']) not in res
        ):
            res[bg] = transform_color('transparent')[0]
        if isinstance(v, defaultdict):
            res = defaultdict(v.default_factory, res)
        return res

    def _selection_as_minimum_dtype(self, dtype: np.dtype) -> int:
        from naparixx import _native

        return _native.vispy_labels_direct_selection_minimum(self, dtype, globals())

    @overload
    def _data_to_texture(self, values: np.ndarray) -> np.ndarray: ...

    @overload
    def _data_to_texture(self, values: np.integer) -> np.integer: ...

    def _data_to_texture(
        self, values: np.ndarray | np.integer
    ) -> np.ndarray | np.integer:
        """Map input values to values for send to GPU."""
        from naparixx import _native

        return _native.label_direct_data_to_texture(self, values, globals())

    def map(self, values: np.ndarray | np.integer | int) -> np.ndarray:
        """Map values to colors.

        Parameters
        ----------
        values : np.ndarray or int
            Values to be mapped.

        Returns
        -------
        np.ndarray of same shape as values, but with last dimension of size 4
            Mapped colors.
        """
        from naparixx import _native

        if _native.label_texture_is_integer(values, globals()):
            values = _native.labelmap_to_int(values, globals())
        if _native.labelmap_is_int(values, globals()):
            if _native.labelmap_scalar_hidden(self, values):
                return _native.labelmap_transparent(globals())
            return _native.labelmap_scalar_color(self, values)
        if _native.labelmap_is_sequence(values, globals()):
            values = _native.labelmap_to_array(values, globals())
        _native.labelmap_direct_validate(values, globals())
        mapper = _native.labelmap_cache(self, values)
        if _native.labelmap_cache_present(mapper):
            mapped = _native.labelmap_index(mapper, values)
        else:
            values_cast = _native.labelmap_direct_texture(values, self, globals())
            mapped = _native.labelmap_precast(self, values_cast, True)
        if _native.label_auto_texture_use_selection(self):
            _native.labelmap_mask(self, values, mapped)
        return mapped

    def _map_without_cache(self, values: np.ndarray) -> np.ndarray:
        from naparixx import _native

        cmap = _native.labelmap_direct_clone(self)
        cast = _native.labelmap_direct_texture(values, cmap, globals())
        return _native.labelmap_precast(self, cast, False)

    def _map_precast(self, values, apply_selection) -> np.ndarray:
        """Map values to colors.

        Parameters
        ----------
        values : np.ndarray
            Values to be mapped. It need to be already cast using
            cast_labels_to_minimum_type_auto

        Returns
        -------
        np.ndarray of shape (N, M, 4)
            Mapped colors.

        Notes
        -----
        it is implemented for thumbnail labels,
        where we already have cast values
        """
        from naparixx import _native

        mapped = _native.labelmap_precast_zero(values, globals())
        colors = _native.labelmap_precast_colors(self, apply_selection)
        locals_owner = _native.labelmap_loop_locals()
        _native.labelmap_precast_fill(values, mapped, colors, globals(), locals_owner)
        return mapped

    @cached_property
    def _num_unique_colors(self) -> int:
        """Count the number of unique colors in the colormap.

        This number does not include background or the default color for
        unmapped labels.
        """
        from naparixx import _native

        return _native.direct_cache_unique_count(self, globals())

    def _clear_cache(self):
        from naparixx import _native

        _native.direct_cache_call_base(super()._clear_cache)
        _native.direct_cache_clear_cached_properties(self)

    def _values_mapping_to_minimum_values_set(
        self, apply_selection=True
    ) -> tuple[dict[int | None, int], dict[int, np.ndarray]]:
        """Create mapping from original values to minimum values set.
        To use minimum possible dtype for labels.

        Returns
        -------
        Dict[Optional[int], int]
            Mapping from original values to minimum values set.
        Dict[int, np.ndarray]
            Mapping from new values to colors.

        """
        from naparixx import _native

        if _native.direct_cache_selection(self, apply_selection):
            return _native.direct_cache_selected_mapping(self, globals())
        return _native.direct_cache_canonical_mapping(self)

    @cached_property
    def _label_mapping_and_color_dict(
        self,
    ) -> tuple[dict[int | None, int], dict[int, np.ndarray]]:
        from naparixx import _native

        color_to_labels: dict[tuple[int, ...], list[int | None]] = (
            _native.direct_cache_empty_colors()
        )
        labels_to_new_labels: dict[int | None, int] = (
            _native.direct_cache_reserved_labels(globals())
        )
        new_color_dict: dict[int, np.ndarray] = (
            _native.direct_cache_reserved_colors(self, globals())
        )
        locals_owner = _native.direct_colormap_cache_locals()
        _native.direct_cache_fill_canonical(
            self,
            color_to_labels,
            labels_to_new_labels,
            new_color_dict,
            globals(),
            locals_owner,
        )
        return _native.direct_cache_mapping_result(
            labels_to_new_labels, new_color_dict
        )

    def _get_typed_dict_mapping(self, data_dtype: np.dtype) -> 'typed.Dict':
        """Create mapping from label values to texture values of smaller dtype.

        In https://github.com/napari/napari/issues/6397, we noticed that using
        float32 textures was much slower than uint8 or uint16 textures. When
        labels data is (u)int(8,16), we simply use the labels data directly.
        But when it is higher-precision, we need to compress the labels into
        the smallest dtype that can still achieve the goal of the
        visualisation. This corresponds to the smallest dtype that can map to
        the number of unique colors in the colormap. Even if we have a
        million labels, if they map to one of two colors, we can map them to
        a uint8 array with values 1 and 2; then, the texture can map those
        two values to each of the two possible colors.

        Returns
        -------
        Dict[Optional[int], int]
            Mapping from original values to minimal texture value set.
        """

        # we cache the result to avoid recomputing it on each slice;
        # check first if it's already in the cache.
        from naparixx import _native

        key = _native.direct_cache_typed_key(data_dtype)
        if _native.direct_cache_typed_hit(self, key):
            return _native.direct_cache_typed_get(self, key)

        from numba import typed, types

        target_type = _native.direct_cache_target_type(self, globals())
        dkt = _native.direct_cache_typed_new(
            typed, types, data_dtype, target_type, globals()
        )
        iinfo = _native.direct_cache_iinfo(data_dtype, globals())
        locals_owner = _native.direct_colormap_cache_locals()
        _native.direct_cache_typed_fill(
            self, data_dtype, target_type, dkt, iinfo, locals_owner
        )
        _native.direct_cache_typed_publish(self, key, dkt)
        return dkt

    @cached_property
    def _array_map(self):
        """Create an array to map labels to texture values of smaller dtype."""

        from naparixx import _native

        max_value = _native.direct_cache_array_max(self, globals())
        max_value = _native.direct_cache_array_extent(
            self, max_value, globals()
        )
        _native.direct_cache_array_check(max_value, globals())
        dtype = _native.direct_cache_target_type(self, globals())
        label_mapping = _native.direct_cache_array_mapping(self)
        mapper = _native.direct_cache_array_new(max_value, dtype, globals())
        locals_owner = _native.direct_colormap_cache_locals()
        _native.direct_cache_array_fill(label_mapping, mapper, locals_owner)
        return mapper

    @property
    def default_color(self) -> np.ndarray:
        from naparixx import _native

        return _native.direct_cache_default_color(self, globals())
        # we provided here default color for backward compatibility
        # if someone is using DirectLabelColormap directly, not through Label layer


@overload
def _convert_small_ints_to_unsigned(
    data: np.ndarray,
) -> np.ndarray: ...


@overload
def _convert_small_ints_to_unsigned(
    data: np.integer,
) -> np.integer: ...


def _convert_small_ints_to_unsigned(
    data: np.ndarray | np.integer,
) -> np.ndarray | np.integer:
    """Convert (u)int8 to uint8 and (u)int16 to uint16.

    Otherwise, return the original array.

    Parameters
    ----------
    data : np.ndarray | np.integer
        Data to be converted.

    Returns
    -------
    np.ndarray | np.integer
        Converted data.
    """
    from naparixx import _native

    return _native.label_convert_small_ints_unsigned(data, globals())


@overload
def _cast_labels_data_to_texture_dtype_auto(
    data: np.ndarray,
    colormap: CyclicLabelColormap,
) -> np.ndarray: ...


@overload
def _cast_labels_data_to_texture_dtype_auto(
    data: np.integer,
    colormap: CyclicLabelColormap,
) -> np.integer: ...


def _cast_labels_data_to_texture_dtype_auto(
    data: np.ndarray | np.integer,
    colormap: CyclicLabelColormap,
) -> np.ndarray | np.integer:
    """Convert labels data to the data type used in the texture.

    In https://github.com/napari/napari/issues/6397, we noticed that using
    float32 textures was much slower than uint8 or uint16 textures. Here we
    convert the labels data to uint8 or uint16, based on the following rules:

    - uint8 and uint16 labels data are unchanged. (No copy of the arrays.)
    - int8 and int16 data are converted with a *view* to uint8 and uint16.
      (This again does not involve a copy so is fast, and lossless.)
    - higher precision integer data (u)int{32,64} are hashed to uint8, uint16,
      or float32, depending on the number of colors in the input colormap. (See
      `minimum_dtype_for_labels`.) Since the hashing can result in collisions,
      this conversion *has* to happen in the CPU to correctly map the
      background and selection values.

    Parameters
    ----------
    data : np.ndarray
        Labels data to be converted.
    colormap : CyclicLabelColormap
        Colormap used to display the labels data.

    Returns
    -------
    np.ndarray | np.integer
        Converted labels data.
    """
    from naparixx import _native

    original_shape = _native.label_texture_shape(data, globals())
    if _native.label_texture_small(data):
        return _native.label_auto_texture_small(data, globals())

    data_arr = _native.label_texture_array(data, globals())
    num_colors = _native.label_auto_texture_num_colors(colormap, globals())
    zero_preserving_modulo_func = _native.label_auto_texture_modulo(globals())
    zero_preserving_modulo_func = _native.label_auto_texture_choose_modulo(
        data, zero_preserving_modulo_func, globals()
    )
    dtype = _native.label_auto_texture_dtype(num_colors, globals())

    if _native.label_auto_texture_use_selection(colormap):
        selection_in_texture = _native.label_auto_texture_selection(
            colormap, num_colors, dtype, globals()
        )
        converted = _native.label_auto_texture_selected(
            data_arr, colormap, selection_in_texture, dtype, globals()
        )
    else:
        converted = _native.label_auto_texture_unselected(
            zero_preserving_modulo_func, data_arr, num_colors, dtype, colormap
        )
    return _native.label_auto_texture_result(
        data, dtype, converted, original_shape, globals()
    )


@overload
def _cast_labels_data_to_texture_dtype_direct(
    data: np.ndarray, direct_colormap: DirectLabelColormap
) -> np.ndarray: ...


@overload
def _cast_labels_data_to_texture_dtype_direct(
    data: np.integer, direct_colormap: DirectLabelColormap
) -> np.integer: ...


def _cast_labels_data_to_texture_dtype_direct(
    data: np.ndarray | np.integer, direct_colormap: DirectLabelColormap
) -> np.ndarray | np.integer:
    """Convert labels data to the data type used in the texture.

    In https://github.com/napari/napari/issues/6397, we noticed that using
    float32 textures was much slower than uint8 or uint16 textures. Here we
    convert the labels data to uint8 or uint16, based on the following rules:

    - uint8 and uint16 labels data are unchanged. (No copy of the arrays.)
    - int8 and int16 data are converted with a *view* to uint8 and uint16.
      (This again does not involve a copy so is fast, and lossless.)
    - higher precision integer data (u)int{32,64} are mapped to an intermediate
      space of sequential values based on the colors they map to. As an
      example, if the values are [1, 2**25, and 2**50], and the direct
      colormap maps them to ['red', 'green', 'red'], then the intermediate map
      is {1: 1, 2**25: 2, 2**50: 1}. The labels can then be converted to a
      uint8 texture and a smaller direct colormap with only two values.

    This function calls `_labels_raw_to_texture_direct`, but makes sure that
    signed ints are first viewed as their unsigned counterparts.

    Parameters
    ----------
    data : np.ndarray | np.integer
        Labels data to be converted.
    direct_colormap : CyclicLabelColormap
        Colormap used to display the labels data.

    Returns
    -------
    np.ndarray | np.integer
        Converted labels data.
    """
    from naparixx import _native

    data = _native.label_cast_texture_direct_prepare(data, globals())

    if _native.label_texture_small(data):
        return data
    if _native.label_texture_is_integer(data, globals()):
        mapper = _native.label_direct_texture_mapper(direct_colormap)
        target_dtype = _native.label_direct_texture_dtype(
            direct_colormap, globals()
        )
        return _native.label_direct_texture_scalar(
            data, mapper, target_dtype, globals()
        )
    original_shape = _native.label_texture_shape(data, globals())
    array_data = _native.label_texture_array(data, globals())
    return _native.label_direct_texture_array(
        array_data, direct_colormap, original_shape, globals()
    )


def _texture_dtype(num_colors: int, dtype: np.dtype) -> np.dtype:
    """Compute VisPy texture dtype given number of colors and raw data dtype.

    - for data of type int8 and uint8 we can use uint8 directly, with no copy.
    - for int16 and uint16 we can use uint16 with no copy.
    - for any other dtype, we fall back on `minimum_dtype_for_labels`, which
      will require on-CPU mapping between the raw data and the texture dtype.
    """
    from naparixx import _native

    return _native.label_texture_dtype(num_colors, dtype, globals())


def _normalize_label_colormap(
    any_colormap_like,
) -> CyclicLabelColormap | DirectLabelColormap:
    """Convenience function to convert color containers to LabelColormaps.

    A list of colors or 2D nparray of colors is interpreted as a color cycle
    (`CyclicLabelColormap`), and a mapping of colors is interpreted as a direct
    color map (`DirectLabelColormap`).

    Parameters
    ----------
    any_colormap_like : Sequence[color], MutableMapping[int, color], ...
        An object that can be interpreted as a LabelColormap, including a
        LabelColormap directly.

    Returns
    -------
    CyclicLabelColormap | DirectLabelColormap
        The computed LabelColormap object.
    """
    if isinstance(
        any_colormap_like, CyclicLabelColormap | DirectLabelColormap
    ):
        return any_colormap_like
    if isinstance(any_colormap_like, Sequence):
        return CyclicLabelColormap(colors=any_colormap_like)
    if isinstance(any_colormap_like, MutableMapping):
        return DirectLabelColormap(color_dict=any_colormap_like)
    if (
        isinstance(any_colormap_like, np.ndarray)
        and any_colormap_like.ndim == 2
        and any_colormap_like.shape[1] in (3, 4)
    ):
        return CyclicLabelColormap(colors=any_colormap_like)
    raise ValueError(
        f'Unable to interpret as labels colormap: {any_colormap_like}'
    )
