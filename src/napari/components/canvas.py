from __future__ import annotations

from enum import StrEnum
from typing import TYPE_CHECKING, Any

from naparixx import _native

import numpy as np
from pydantic import Field

from napari.components.grid import GridCanvas
from napari.components.overlays import (
    BrushCircleOverlay,
    CanvasAxesOverlay,
    CanvasOverlay,
    CurrentSliceOverlay,
    ScaleBarOverlay,
    TextOverlay,
    ZoomOverlay,
)
from napari.settings import get_settings
from napari.utils.color import ColorValue
from napari.utils.events import Event, EventedDictNamespace, EventedModel
from napari.utils.theme import get_theme

if TYPE_CHECKING:
    from napari.components import LayerList


class Orientation(StrEnum):
    HORIZONTAL = 'horizontal'
    VERTICAL = 'vertical'


class OverlayTiling(EventedModel):
    """Overlay tiling controls.

    For each canvas position, tiling direction can be set to vertical or horizontal.
    Padding between tiles can also be changed as a (vertical, horizontal) tuple.

    Examples
    --------
    >>> canvas = Canvas()
    >>> canvas.overlay_tiling.top_right = 'vertical'
    """

    padding: tuple[float, float] = (10.0, 10.0)
    top_left: Orientation = Orientation.VERTICAL
    top_center: Orientation = Orientation.VERTICAL
    top_right: Orientation = Orientation.HORIZONTAL
    middle_left: Orientation = Orientation.HORIZONTAL
    middle_center: Orientation = Orientation.VERTICAL
    middle_right: Orientation = Orientation.HORIZONTAL
    bottom_left: Orientation = Orientation.HORIZONTAL
    bottom_center: Orientation = Orientation.VERTICAL
    bottom_right: Orientation = Orientation.VERTICAL


class Canvas(EventedModel):
    """
    Canvas evented model.

    Controls canvas-related attributes, such as grid mode and canvas overlays.

    Attributes
    ----------
    background_color_override : ColorValue or None
        Override the theme background color with a custom one.
    grid : GridCanvas
        A model that controls the enabling and settings of the grid mode.
    overlays : EventedDictNamespace
        A dictionary/namespace containing canvas overlays. By default, it exposes
        publicly 'scale_bar', 'text', 'current_slice' and 'axes'
    overlay_tiling : OverlayTiling
        Controls for the overlay tiling direction and padding.
    size : tuple[int, int]
        The canvas size following the Numpy convention of height x width
    """

    background_color_override: ColorValue | None = None
    grid: GridCanvas = Field(default_factory=GridCanvas, frozen=True)
    overlays: EventedDictNamespace[CanvasOverlay] = Field(
        default_factory=lambda: EventedDictNamespace(
            {
                'scale_bar': ScaleBarOverlay(),
                'text': TextOverlay(),
                '_brush_circle': BrushCircleOverlay(),
                '_zoom_box': ZoomOverlay(),
                'current_slice': CurrentSliceOverlay(),
                'axes': CanvasAxesOverlay(),
            }
        ),
        frozen=True,
    )
    overlay_tiling: OverlayTiling = Field(
        default_factory=OverlayTiling, frozen=True
    )
    size: tuple[int, int] = (800, 600)

    def __init__(self, **kwargs: Any) -> None:
        super_provider = lambda _receiver=self: super()
        locals_owner = _native.settings_grid_publication_locals()
        return _native.settings_grid_canvas_init(
            self, kwargs, globals(), super_provider, locals_owner,
            ('_overlay_positions_changed',),
        )

    def viewbox_size(self, layers: LayerList) -> tuple[int, int]:
        """Get the size of a single viewbox (whether grid is enabled or not).

        If grid.border_width > 0, that's accounted for too.
        """
        return _native.canvas_viewbox_size(self, layers)

    def _update_viewer_grid(self) -> None:
        """Keep viewer grid settings up to date with settings values."""
        locals_owner = _native.settings_grid_publication_locals()
        return _native.settings_grid_canvas_update(self, globals(), locals_owner)

    @property
    def background_color(self) -> ColorValue:
        locals_owner = _native.theme_color_locals(0, self)
        del self
        return _native.theme_color_run(
            0, globals(), locals_owner,
            (255,),
        )

    def _update_bgcolor_from_viewer(self, theme: Event) -> None:
        locals_owner = _native.theme_color_locals(1, self, theme)
        del self, theme
        return _native.theme_color_run(
            1, globals(), locals_owner,
            (),
        )
