"""Image class."""

from __future__ import annotations

import typing
import warnings
from typing import Any, Literal, cast

import numpy as np

from naparixx import native as _native

from napari.layers._data_protocols import LayerDataProtocol
from napari.layers._multiscale_data import MultiScaleData
from napari.layers._scalar_field._slice import _ScalarFieldSliceResponse
from napari.layers._scalar_field.scalar_field import (
    ScalarFieldBase,
    ScalarFieldSlicingState,
)
from napari.layers.image._image_constants import (
    ImageProjectionMode,
    ImageRendering,
    Interpolation,
    InterpolationStr,
)
from napari.layers.image._image_utils import guess_rgb
from napari.layers.image._slice import _ImageSliceRequest
from napari.layers.intensity_mixin import IntensityVisualizationMixin
from napari.layers.utils.layer_utils import calc_data_range
from napari.types import LayerDataType
from napari.utils._dtype import get_dtype_limits, normalize_dtype
from napari.utils.color import rgb_to_luminance
from napari.utils.colormaps import ensure_colormap
from napari.utils.colormaps.colormap_utils import _coerce_contrast_limits

if typing.TYPE_CHECKING:
    from collections.abc import Sequence

    import numpy.typing as npt
    import pint

    from napari.components.histogram import HistogramModel
    from napari.types import ArrayLike
    from napari.utils.transforms import Affine

__all__ = ('Image',)


class Image(IntensityVisualizationMixin, ScalarFieldBase):
    """Image layer.

    Parameters
    ----------
    data : array or list of array
        Image data. Can be N >= 2 dimensional. If the last dimension has length
        3 or 4 can be interpreted as RGB or RGBA if rgb is `True`. If a
        list and arrays are decreasing in shape then the data is treated as
        a multiscale image. In 2D, the displayed resolution is chosen
        automatically based on the viewport. In 3D, the lowest resolution
        scale is displayed by default. The resolution level can be locked
        via ``locked_data_level`` or the resolution control in the layer
        controls UI.
    affine : n-D array or napari.utils.transforms.Affine
        (N+1, N+1) affine transformation matrix in homogeneous coordinates.
        The first (N, N) entries correspond to a linear transform and
        the final column is a length N translation vector and a 1 or a napari
        `Affine` transform object. Applied as an extra transform on top of the
        provided scale, rotate, and shear values.
    attenuation : float
        Attenuation rate for attenuated maximum intensity projection.
    auto_contrast : bool
        Wether to automatically set contrast limits to the min and max of the
        currently viewed slice. If True, contrast limits will be updated
        whenever the slice changes.
    axis_labels : tuple of str
        Dimension names of the layer data.
        If not provided, axis_labels will be set to (..., '-2', '-1').
    blending : str
        One of a list of preset blending modes that determines how RGB and
        alpha values of the layer visual get mixed. Allowed values are
        {'translucent', 'translucent_no_depth', 'additive', 'minimum', 'opaque'}.
    cache : bool
        Whether slices of out-of-core datasets should be cached upon retrieval.
        Currently, this only applies to dask arrays.
    colormap : str, napari.utils.Colormap, tuple, dict
        Colormaps to use for luminance images. If a string, it can be the name
        of a supported colormap from vispy or matplotlib or the name of
        a vispy color or a hexadecimal RGB color representation.
        If a tuple, the first value must be a string to assign as a name to a
        colormap and the second item must be a Colormap. If a dict, the key must
        be a string to assign as a name to a colormap and the value must be a
        Colormap.
    contrast_limits : list (2,)
        Intensity value limits to be used for determining the minimum and maximum colormap bounds for
        luminance images. If not passed, they will be calculated as the min and max intensity value of
        the image.
    custom_interpolation_kernel_2d : np.ndarray
        Convolution kernel used with the 'custom' interpolation mode in 2D rendering.
    depiction : str
        3D Depiction mode. Must be one of {'volume', 'plane'}.
        The default value is 'volume'.
    experimental_clipping_planes : list of dicts, list of ClippingPlane, or ClippingPlaneList
        Each dict defines a clipping plane in 3D in data coordinates.
        Valid dictionary keys are {'position', 'normal', and 'enabled'}.
        Values on the negative side of the normal are discarded if the plane is enabled.
    gamma : float
        Gamma correction for determining colormap linearity; defaults to 1.
    interpolation2d : str
        Interpolation mode used by vispy for rendering 2d data.
        Must be one of our supported modes.
        (for list of supported modes see Interpolation enum)
        'custom' is a special mode for 2D interpolation in which a regular grid
        of samples is taken from the texture around a position using 'linear'
        interpolation before being multiplied with a custom interpolation kernel
        (provided with 'custom_interpolation_kernel_2d').
    interpolation3d : str
        Same as 'interpolation2d' but for 3D rendering.
    iso_threshold : float
        Threshold for isosurface.
    locked_data_level : int, optional
        Lock the multiscale resolution level to a specific index. When set,
        forces rendering at the given multiscale level instead of automatic
        level selection based on the viewport. Set to ``None`` (default) to
        use automatic selection.
    metadata : dict
        Layer metadata.
    multiscale : bool
        Whether the data is a multiscale image or not. Multiscale data is
        represented by a list of array-like image data. If not specified by
        the user and if the data is a list of arrays that decrease in shape,
        then it will be taken to be multiscale. The first image in the list
        should be the largest. In 2D, the displayed resolution is chosen
        automatically based on the viewport. In 3D, the lowest resolution
        scale is displayed by default. The resolution level can be locked
        via ``locked_data_level`` or the resolution control in the layer
        controls UI.
    name : str
        Name of the layer.
    opacity : float
        Opacity of the layer visual, between 0.0 and 1.0.
    plane : dict or SlicingPlane
        Properties defining plane rendering in 3D. Properties are defined in
        data coordinates. Valid dictionary keys are
        {'position', 'normal', 'thickness', and 'enabled'}.
    projection_mode : str
        How data outside the viewed dimensions, but inside the thick Dims slice will
        be projected onto the viewed dimensions. Must fit to ImageProjectionMode
    rendering : str
        Rendering mode used by vispy. Must be one of our supported
        modes.
    rgb : bool, optional
        Whether the image is RGB or RGBA if rgb. If not
        specified by user, but the last dimension of the data has length 3 or 4,
        it will be set as `True`. If `False`, the image is interpreted as a
        luminance image.
    rotate : float, 3-tuple of float, or n-D array.
        If a float, convert into a 2D rotation matrix using that value as an
        angle. If 3-tuple, convert into a 3D rotation matrix, using a yaw,
        pitch, roll convention. Otherwise, assume an nD rotation. Angles are
        assumed to be in degrees. They can be converted from radians with
        'np.degrees' if needed.
    scale : tuple of float
        Scale factors for the layer.
    shear : 1-D array or n-D array
        Either a vector of upper triangular values, or an nD shear matrix with
        ones along the main diagonal.
    translate : tuple of float
        Translation values for the layer.
    units : tuple of str or pint.Unit, optional
        Units of the layer data in world coordinates.
        If not provided, the default units are assumed to be pixels.
    visible : bool
        Whether the layer visual is currently being displayed.

    Attributes
    ----------
    data : array or list of array
        Image data. Can be N dimensional. If the last dimension has length
        3 or 4 can be interpreted as RGB or RGBA if rgb is `True`. If a list
        and arrays are decreasing in shape then the data is treated as a
        multiscale image. In 2D, the displayed resolution is chosen
        automatically based on the viewport. In 3D, the lowest resolution
        scale is displayed by default. The resolution level can be locked
        via ``locked_data_level`` or the resolution control in the layer
        controls UI.
    axis_labels : tuple of str
        Dimension names of the layer data.
    metadata : dict
        Image metadata.
    rgb : bool
        Whether the image is rgb RGB or RGBA if rgb. If not
        specified by user and the last dimension of the data has length 3 or 4
        it will be set as `True`. If `False` the image is interpreted as a
        luminance image.
    multiscale : bool
        Whether the data is a multiscale image or not. Multiscale data is
        represented by a list of array like image data. The first image in the
        list should be the largest. In 2D, the displayed resolution is chosen
        automatically based on the viewport. In 3D, the lowest resolution
        scale is displayed by default. The resolution level can be locked
        via ``locked_data_level`` or the resolution control in the layer
        controls UI.
    mode : str
        Interactive mode. The normal, default mode is PAN_ZOOM, which
        allows for normal interactivity with the canvas.

        In TRANSFORM mode the image can be transformed interactively.
    colormap : 2-tuple of str, napari.utils.Colormap
        The first is the name of the current colormap, and the second value is
        the colormap. Colormaps are used for luminance images, if the image is
        rgb the colormap is ignored.
    colormaps : tuple of str
        Names of the available colormaps.
    contrast_limits : list (2,) of float
        Color limits to be used for determining the colormap bounds for
        luminance images. If the image is rgb the contrast_limits is ignored.
    contrast_limits_range : list (2,) of float
        Range for the color limits for luminance images. If the image is
        rgb the contrast_limits_range is ignored.
    auto_contrast : bool
        Wether to automatically set contrast limits to the min and max of the
        currently viewed slice. If True, contrast limits will be updated
        whenever the slice changes.
    gamma : float
        Gamma correction for determining colormap linearity.
    interpolation2d : str
        Interpolation mode used by vispy. Must be one of our supported modes.
        'custom' is a special mode for 2D interpolation in which a regular grid
        of samples are taken from the texture around a position using 'linear'
        interpolation before being multiplied with a custom interpolation kernel
        (provided with 'custom_interpolation_kernel_2d').
    interpolation3d : str
        Same as 'interpolation2d' but for 3D rendering.
    rendering : str
        Rendering mode used by vispy. Must be one of our supported
        modes.
    depiction : str
        3D Depiction mode used by vispy. Must be one of our supported modes.
    iso_threshold : float
        Threshold for isosurface.
        When in RGB, this threshold applies to the perceived luminance.
    attenuation : float
        Attenuation rate for attenuated maximum intensity projection.
    plane : SlicingPlane or dict
        Properties defining plane rendering in 3D. Valid dictionary keys are
        {'position', 'normal', 'thickness'}.
    projection_mode : str
        How data outside the viewed dimensions, but inside the thick Dims slice will
        be projected onto the viewed dimensions. Must fit to ImageProjectionMode
    experimental_clipping_planes : ClippingPlaneList
        Clipping planes defined in data coordinates, used to clip the volume.
    custom_interpolation_kernel_2d : np.ndarray
        Convolution kernel used with the 'custom' interpolation mode in 2D rendering.
    units: tuple of pint.Unit
        Units of the layer data in world coordinates.
    Notes
    -----
    _data_view : array (N, M), (N, M, 3), or (N, M, 4)
        Image data for the currently viewed slice. Must be 2D image data, but
        can be multidimensional for RGB or RGBA images if multidimensional is
        `True`.
    """

    _projectionclass = ImageProjectionMode

    def __init__(
        self,
        data: ArrayLike | Sequence[ArrayLike],
        *,
        affine: npt.ArrayLike | Affine | None = None,
        attenuation: float = 0.05,
        auto_contrast: bool = False,
        axis_labels: Sequence[str] | None = None,
        blending: str = 'translucent',
        cache: bool = True,
        colormap: str = 'gray',
        contrast_limits: tuple[float, float] | None = None,
        custom_interpolation_kernel_2d: npt.NDArray | None = None,
        depiction: str = 'volume',
        experimental_clipping_planes: dict | None = None,
        gamma: float = 1.0,
        interpolation2d: InterpolationStr = 'nearest',
        interpolation3d: InterpolationStr = 'linear',
        iso_threshold: float | None = None,
        locked_data_level: int | None = None,
        metadata: dict | None = None,
        multiscale: bool | None = None,
        name: str | None = None,
        opacity: float = 1.0,
        plane: dict | None = None,
        projection_mode: str = 'mean',
        rendering: str = 'mip',
        rgb: bool | None = None,
        rotate: float | Sequence[float] | npt.NDArray | None = None,
        scale: Sequence[float] | None = None,
        shear: Sequence[float] | npt.NDArray | None = None,
        translate: Sequence[float] | None = None,
        units: Sequence[str | pint.Unit] | None = None,
        visible: bool = True,
    ):
        # Determine if rgb
        locals_owner = _native.image_init_locals(rgb)
        del rgb
        self._contrast_limits: tuple[float, float]
        _native.image_init(
            self,
            data,
            affine,
            attenuation,
            auto_contrast,
            axis_labels,
            blending,
            cache,
            colormap,
            contrast_limits,
            custom_interpolation_kernel_2d,
            depiction,
            experimental_clipping_planes,
            gamma,
            interpolation2d,
            interpolation3d,
            iso_threshold,
            locked_data_level,
            metadata,
            multiscale,
            name,
            opacity,
            plane,
            projection_mode,
            rendering,
            rotate,
            scale,
            shear,
            translate,
            units,
            visible,
            globals(),
            locals_owner,
            lambda self: super().__init__,
        )

    @property
    def rendering(self) -> str:  # pyrefly: ignore [bad-override]
        """Return current rendering mode.

        Selects a preset rendering mode in vispy that determines how
        volume is displayed.  Options include:

        * ``translucent``: voxel colors are blended along the view ray until
            the result is opaque.
        * ``mip``: maximum intensity projection. Cast a ray and display the
            maximum value that was encountered.
        * ``minip``: minimum intensity projection. Cast a ray and display the
            minimum value that was encountered.
        * ``attenuated_mip``: attenuated maximum intensity projection. Cast a
            ray and attenuate values based on integral of encountered values,
            display the maximum value that was encountered after attenuation.
            This will make nearer objects appear more prominent.
        * ``additive``: voxel colors are added along the view ray until
            the result is saturated.
        * ``iso``: isosurface. Cast a ray until a certain threshold is
            encountered. At that location, lighning calculations are
            performed to give the visual appearance of a surface.
        * ``average``: average intensity projection. Cast a ray and display the
            average of values that were encountered.

        Returns
        -------
        str
            The current rendering mode
        """
        return str(self._rendering)

    @rendering.setter
    def rendering(self, rendering: str) -> None:
        self._rendering = ImageRendering(rendering)
        self.events.rendering()

    def _get_state(self) -> dict[str, Any]:
        """Get dictionary of layer state.

        Returns
        -------
        state : dict of str to Any
            Dictionary of layer state.
        """
        state = self._get_base_state()
        state.update(
            {
                'rgb': self.rgb,
                'multiscale': self.multiscale,
                'locked_data_level': self.locked_data_level,
                'auto_contrast': self.auto_contrast,
                'colormap': self.colormap.model_dump(),
                'contrast_limits': self.contrast_limits,
                'interpolation2d': self.interpolation2d,
                'interpolation3d': self.interpolation3d,
                'rendering': self.rendering,
                'depiction': self.depiction,
                'plane': self.plane.model_dump(),
                'iso_threshold': self.iso_threshold,
                'attenuation': self.attenuation,
                'gamma': self.gamma,
                'data': self.data,
                'custom_interpolation_kernel_2d': self.custom_interpolation_kernel_2d,
            }
        )
        return state

    @property
    def attenuation(self) -> float:
        """float: attenuation rate for attenuated_mip rendering."""
        return self._attenuation

    @attenuation.setter
    def attenuation(self, value: float) -> None:
        self._attenuation = value
        self._update_thumbnail()
        self.events.attenuation()

    @property
    def histogram(self) -> HistogramModel:
        """Histogram model for this layer, created lazily on first access.

        The histogram model computes and stores histogram data for the layer,
        responding to changes in layer data, contrast limits, and gamma.
        The model is not created until the ``histogram`` property is first
        accessed.

        Returns
        -------
        HistogramModel
            Histogram model instance for this layer.
        """
        if not hasattr(self, '_histogram'):
            from napari.components.histogram import HistogramModel

            self._histogram = HistogramModel(self)
        return self._histogram

    @ScalarFieldBase.data.setter
    def data(self, data: LayerDataProtocol | MultiScaleData) -> None:
        return _native.scalar_data_image_set(self, data, globals())

    @property
    def interpolation2d(self) -> InterpolationStr:
        return cast(InterpolationStr, str(self._interpolation2d))

    @interpolation2d.setter
    def interpolation2d(self, value: InterpolationStr | Interpolation) -> None:
        if value == 'bilinear':
            raise ValueError(
                "'bilinear' interpolation is not valid for interpolation2d. Did you mean 'linear' instead ?",
            )
        if value == 'bicubic':
            value = 'cubic'
            warnings.warn(
                "'bicubic' is deprecated. Please use 'cubic' instead",
                category=DeprecationWarning,
                stacklevel=2,
            )
        self._interpolation2d = Interpolation(value)
        self.events.interpolation2d(value=self._interpolation2d)
        self.events.interpolation(value=self._interpolation2d)

    @property
    def interpolation3d(self) -> InterpolationStr:
        return cast(InterpolationStr, str(self._interpolation3d))

    @interpolation3d.setter
    def interpolation3d(self, value: InterpolationStr | Interpolation) -> None:
        if value == 'custom':
            raise NotImplementedError(
                'custom interpolation is not implemented yet for 3D rendering'
            )
        if value == 'bicubic':
            value = 'cubic'
            warnings.warn(
                "'bicubic' is deprecated. Please use 'cubic' instead",
                category=DeprecationWarning,
                stacklevel=2,
            )
        self._interpolation3d = Interpolation(value)
        self.events.interpolation3d(value=self._interpolation3d)
        self.events.interpolation(value=self._interpolation3d)

    @property
    def iso_threshold(self) -> float:
        """float: threshold for isosurface."""
        return self._iso_threshold

    @iso_threshold.setter
    def iso_threshold(self, value: float) -> None:
        self._iso_threshold = value
        self._update_thumbnail()
        self.events.iso_threshold()

    def _get_level_shapes(self) -> Sequence[tuple[int, ...]]:
        locals_owner = _native.image_shape_locals()
        return _native.image_shape_levels(
            self, lambda self: super()._get_level_shapes, locals_owner
        )

    def _update_thumbnail(self) -> None:
        """Update thumbnail with current image data and colormap."""
        from scipy import ndimage as ndi

        from naparixx import _native

        locals_owner = _native.layer_thumbnail_locals()
        _native.layer_thumbnail_image(self, ndi, globals(), locals_owner)

    def _calc_data_range(
        self, mode: Literal['data', 'slice'] = 'data'
    ) -> tuple[float, float]:
        """
        Calculate the range of the data values in the currently viewed slice
        or full data array
        """
        input_data: np.ndarray
        input_data = _native.image_select_contrast_data(self, mode)
        return _native.image_calc_data_range(self, input_data, globals())

    def _raw_to_displayed(self, raw: np.ndarray) -> np.ndarray:
        """Determine displayed image from raw image.

        This function checks if current contrast_limits are within the range
        supported by vispy.
        If yes, it returns the raw image.
        If not, it rescales the raw image to fit within
        the range supported by vispy.

        Parameters
        ----------
        raw : array
            Raw array.

        Returns
        -------
        image : array
            Displayed array.
        """
        fixed_contrast_info = _native.image_prepare_raw_display(self, globals())
        return _native.image_raw_to_displayed(
            self, raw, fixed_contrast_info, globals()
        )

    @IntensityVisualizationMixin.contrast_limits.setter
    def contrast_limits(self, contrast_limits: tuple[float, float]) -> None:
        from naparixx import _native

        locals_owner = _native.layer_thumbnail_locals()
        _native.layer_image_contrast_limits(
            self, contrast_limits, globals(), locals_owner
        )

    def _calculate_value_from_ray(
        self, values: npt.NDArray
    ) -> float | npt.NDArray | None:
        owner = _native.viewer_status_provider_locals()
        return _native.viewer_image_value_ray(self, values, globals(), (0.5, "ray value calculation not implemented for "), owner)

    def _get_layer_slicing_state(
        self, data: LayerDataType, cache: bool
    ) -> _ImageSlicingState:
        return _native.image_get_layer_slicing_state(self, data, cache, globals())


class _ImageSlicingState(ScalarFieldSlicingState):
    layer: Image
    _slice_request_class = _ImageSliceRequest

    def _update_slice_response(
        self, response: _ScalarFieldSliceResponse
    ) -> None:
        """Restore the original contrast limits after slicing.

        WARNING: This is a hack.
        Will be removed as we want to go into multi canvas mode.
        """
        _native.image_apply_slice_response(
            self, response, super()._update_slice_response, globals()
        )
