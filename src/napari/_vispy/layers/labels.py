import math
from typing import TYPE_CHECKING

import numpy as np
from vispy.color import Colormap as VispyColormap
from vispy.gloo import Texture2D
from vispy.scene.node import Node

from napari._vispy.layers.scalar_field import (
    _DTYPE_TO_VISPY_FORMAT,
    _VISPY_FORMAT_TO_DTYPE,
    ScalarFieldLayerNode,
    VispyScalarFieldBaseLayer,
    get_dtype_from_vispy_texture_format,
)
from napari._vispy.utils.gl import get_max_texture_sizes
from napari._vispy.visuals.labels import LabelNode
from napari._vispy.visuals.volume import Volume as VolumeNode
from napari.utils.colormaps.colormap import (
    CyclicLabelColormap,
    _texture_dtype,
)

if TYPE_CHECKING:
    from napari.layers import Labels


ColorTuple = tuple[float, float, float, float]


auto_lookup_shader_uint8 = """
uniform sampler2D texture2D_values;

vec4 sample_label_color(float t) {
    if (($use_selection) && ($selection != int(t * 255))) {
        return vec4(0);
    }
    return texture2D(
        texture2D_values,
        vec2(0.0, t)
    );
}
"""

auto_lookup_shader_uint16 = """
uniform sampler2D texture2D_values;

vec4 sample_label_color(float t) {
    // uint 16
    t = t * 65535;
    if (($use_selection) && ($selection != int(t))) {
        return vec4(0);
    }
    float v = mod(t, 256);
    float v2 = (t - v) / 256;
    return texture2D(
        texture2D_values,
        vec2((v + 0.5) / 256, (v2 + 0.5) / 256)
    );
}
"""


direct_lookup_shader = """
uniform sampler2D texture2D_values;
uniform vec2 LUT_shape;

vec4 sample_label_color(float t) {
    t = t * $scale;
    return texture2D(
        texture2D_values,
        vec2(0.0, (t + 0.5) / $color_map_size)
    );
}

"""

direct_lookup_shader_many = """
uniform sampler2D texture2D_values;
uniform vec2 LUT_shape;

vec4 sample_label_color(float t) {
    t = t * $scale;
    float row = mod(t, LUT_shape.x);
    float col = int(t / LUT_shape.x);
    return texture2D(
        texture2D_values,
        vec2((col + 0.5) / LUT_shape.y, (row + 0.5) / LUT_shape.x)
    );
}
"""


class LabelVispyColormap(VispyColormap):
    def __init__(
        self,
        colormap: CyclicLabelColormap,
        view_dtype: np.dtype,
        raw_dtype: np.dtype,
    ):
        from naparixx import _native

        locals_owner = _native.vispy_labels_operation_locals()
        _native.vispy_labels_label_init(
            self,
            colormap,
            view_dtype,
            raw_dtype,
            lambda receiver: super().__init__,
            globals(),
            locals_owner,
        )


class DirectLabelVispyColormap(VispyColormap):
    def __init__(
        self,
        use_selection=False,
        selection=0.0,
        scale=1.0,
        color_map_size=255,
        multi=False,
    ):
        from naparixx import _native

        locals_owner = _native.vispy_labels_operation_locals()
        _native.vispy_labels_direct_init(
            self,
            use_selection,
            selection,
            scale,
            color_map_size,
            multi,
            lambda receiver: super().__init__,
            globals(),
            locals_owner,
        )


def build_textures_from_dict(
    color_dict: dict[int, ColorTuple], max_size: int
) -> np.ndarray:
    """This code assumes that the keys in the color_dict are sequential from 0.

    If any keys are larger than the size of the dictionary, they will
    overwrite earlier keys in the best case, or it might just crash.
    """
    from naparixx import _native

    locals_owner = _native.vispy_labels_operation_locals()
    return _native.vispy_labels_build_from_dict(
        color_dict, max_size, globals(), locals_owner
    )


def _select_colormap_texture(
    colormap: CyclicLabelColormap, view_dtype, raw_dtype
) -> np.ndarray:
    from naparixx import _native

    locals_owner = _native.vispy_labels_operation_locals()
    return _native.vispy_labels_select_texture_operation(
        colormap, view_dtype, raw_dtype, globals(), locals_owner
    )


class VispyLabelsLayer(VispyScalarFieldBaseLayer):
    layer: 'Labels'

    def __init__(
        self, layer, node=None, texture_format='r8', **kwargs
    ) -> None:
        from naparixx import _native

        self._colormap_dtypes: tuple[np.dtype, np.dtype] | None
        _native.vispy_labels_controller_init(
            self,
            layer,
            node,
            texture_format,
            kwargs,
            lambda receiver: super().__init__,
            globals(),
        )
        # as we generate colormap texture based on the data type, we need to
        # update it when the data type changes

    def _on_data_change(self) -> None:
        from naparixx import _native

        locals_owner = _native.vispy_labels_operation_locals()
        _native.vispy_labels_data_change(
            self, lambda receiver: super()._on_data_change, globals(), locals_owner
        )

    def _on_rendering_change(self):
        # overriding the Image method, so we can maintain the same old rendering name
        from naparixx import _native

        locals_owner = _native.vispy_labels_operation_locals()
        _native.vispy_labels_rendering_change(self, globals(), locals_owner)

    def _on_colormap_change(self, event=None):
        # self.layer.colormap is a labels_colormap, which is an evented model
        # from napari.utils.colormaps.Colormap (or similar). If we use it
        # in our constructor, we have access to the texture data we need
        from naparixx import _native

        locals_owner = _native.vispy_labels_operation_locals()
        _native.vispy_labels_colormap_change(self, event, globals(), locals_owner)

    def _on_iso_gradient_mode_change(self):
        from naparixx import _native

        _native.vispy_labels_iso_change(self, globals())

    def _on_partial_labels_update(self, event):
        from naparixx import _native

        locals_owner = _native.vispy_labels_operation_locals()
        _native.vispy_labels_partial_update(self, event, globals(), locals_owner)

    def reset(self, event=None) -> None:
        from naparixx import _native

        _native.vispy_labels_reset(self, lambda receiver: super().reset)


class LabelLayerNode(ScalarFieldLayerNode):
    def __init__(self, custom_node: Node = None, texture_format=None):
        from naparixx import _native

        _native.vispy_labels_node_init(self, custom_node, texture_format)

    def _setup_nodes(self, texture_format):
        from naparixx import _native

        _native.vispy_labels_setup_nodes(self, texture_format, globals())

    def get_node(self, ndisplay: int, dtype=None, shape=None) -> Node:
        from naparixx import _native

        locals_owner = _native.vispy_labels_operation_locals()
        return _native.vispy_labels_get_node(
            self, ndisplay, dtype, globals(), locals_owner
        )
