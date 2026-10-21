from typing import TYPE_CHECKING, Literal

import numpy as np

from naparixx import native as _native

from napari.utils._dtype import normalize_dtype
from napari.utils.colormaps import AVAILABLE_COLORMAPS, ensure_colormap
from napari.utils.events import Event
from napari.utils.status_messages import format_float
from napari.utils.validators import _validate_increasing, validate_n_seq

validate_2_tuple = validate_n_seq(2)

if TYPE_CHECKING:
    from napari.layers._scalar_field.scalar_field import ScalarFieldBase
    from napari.layers.surface import Surface


class IntensityVisualizationMixin:
    """A mixin that adds gamma, colormap, and contrast limits logic to Layers.

    When used, this should come before the Layer in the inheritance, e.g.:

        class Image(IntensityVisualizationMixin, Layer):
            def __init__(self):
                ...

    Note: `contrast_limits_range` is range extent available on the widget,
    and `contrast_limits` is the visible range (the set values on the widget)
    """

    def __init__(self, *args, **kwargs) -> None:
        locals_owner = _native.intensity_init_locals()
        self._contrast_limits: tuple[float | None, float | None]
        self._contrast_limits_range: tuple[float | None, float | None]
        _native.intensity_init(
            self,
            args,
            kwargs,
            (None, None),
            globals(),
            locals_owner,
            lambda self: super().__init__,
        )

    def reset_contrast_limits(self: 'ScalarFieldBase | Surface', mode=None):
        """Scale contrast limits to data range"""
        mode = _native.intensity_select_contrast_mode(self, mode)
        _native.intensity_reset_contrast_limits(self, mode)

    def _calc_data_range(self, mode):
        raise NotImplementedError

    def reset_contrast_limits_range(
        self, mode: Literal['data', 'slice'] | None = None
    ) -> None:
        """Scale contrast limits range to data type if dtype is an integer,
        or use the current maximum data range otherwise.

        Parameters
        ----------
        mode: 'data' or 'slice'
           If range should be calculated on whole data, or only visible slice.
        """
        dtype = _native.intensity_contrast_dtype(self, globals())
        mode, info, is_integer = _native.intensity_prepare_contrast_range(
            self, dtype, mode, globals()
        )
        _native.intensity_reset_contrast_limits_range(
            self, mode, info, is_integer
        )

    @property
    def colormap(self):
        """napari.utils.Colormap: colormap for luminance images."""
        return self._colormap

    @colormap.setter
    def colormap(self, colormap):
        self._set_colormap(colormap)

    def _set_colormap(self, colormap):
        self._colormap = ensure_colormap(colormap)
        self._update_thumbnail()
        self.events.colormap()

    @property
    def colormaps(self):
        """tuple of str: names of available colormaps."""
        return tuple(AVAILABLE_COLORMAPS.keys())

    @property
    def colorbar(self):
        """Color Bar overlay."""
        return self._overlays['colorbar']

    @property
    def contrast_limits(self):
        """list of float: Limits to use for the colormap."""
        return _native.intensity_get_contrast_limits(self, globals())

    @contrast_limits.setter
    def contrast_limits(self, contrast_limits):
        locals_owner = _native.intensity_contrast_locals()
        _native.intensity_set_contrast_limits(
            self, contrast_limits, globals(), locals_owner
        )

    @property
    def contrast_limits_range(self):
        """The current valid range of the contrast limits."""
        return _native.intensity_get_contrast_limits_range(self, globals())

    @contrast_limits_range.setter
    def contrast_limits_range(self, value):
        """Set the valid range of the contrast limits.
        If either value is "None", the current range will be preserved.
        If the range overlaps the current contrast limits, the range will be set
        requested and there will be no change the contrast limits.
        If the requested contrast range limits are completely outside the
        current contrast limits, the range will be set as requested and the
        contrast limits will be reset to the new range.
        """
        locals_owner = _native.intensity_contrast_range_locals(value)
        del value
        _native.intensity_set_contrast_limits_range(
            self, globals(), locals_owner
        )

    @property
    def gamma(self):
        return self._gamma

    @gamma.setter
    def gamma(self, value):
        self._gamma = float(value)
        self._update_thumbnail()
        self.events.gamma()

    @property
    def auto_contrast(self):
        """bool: Whether to automatically set contrast limits to the data range of each slice."""
        return self._auto_contrast

    @auto_contrast.setter
    def auto_contrast(self, value):
        self._auto_contrast = bool(value)
        self.events.auto_contrast()
        if self._auto_contrast:
            self.reset_contrast_limits()
