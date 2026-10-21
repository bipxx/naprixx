from __future__ import annotations

from typing import TYPE_CHECKING
from copy import copy
from functools import wraps

import numpy as np
from pydantic import PrivateAttr, model_validator

from naparixx import _native

from napari.settings._application import (
    GridHeight,
    GridSpacing,
    GridStride,
    GridWidth,
)
from napari.utils.events import EventedModel

if TYPE_CHECKING:
    from collections.abc import Iterator, Sequence

    from napari.layers import Layer


class GridCanvas(EventedModel):
    """Grid for canvas.

    Right now the only grid mode that is still inside one canvas with one
    camera, but future grid modes could support multiple canvases.

    Attributes
    ----------
    enabled : bool
        If grid is enabled or not.
    stride : int
        Number of layers to place in each grid viewbox before moving on to
        the next viewbox. The default ordering is to place the most visible
        layer in the top left corner of the grid. A negative stride will
        cause the order in which the layers are placed in the grid to be
        reversed.
    shape : 2-tuple of int
        Number of rows and columns in the grid. A value of -1 for either or
        both of will be used the row and column numbers will trigger an
        auto calculation of the necessary grid shape to appropriately fill
        all the layers at the appropriate stride.
    spacing : float
        Spacing between grid viewboxes. If between 0 and 1, it's
        interpreted as a proportion of the size of the viewboxes.
        If equal or greater than 1, it's interpreted as screen pixels.

        .. versionadded:: 0.6.0
            ``spacing`` was added in 0.6.0.
    """

    stride: GridStride = 1
    shape: tuple[GridHeight, GridWidth] = (-1, -1)
    enabled: bool = False
    spacing: GridSpacing = 0.0
    _native_state: _native.GridState = PrivateAttr(default_factory=_native.GridState)

    @model_validator(mode="after")
    def _sync_native_state(self):
        return _native.grid_sync(self)

    def __copy__(self):
        return _native.grid_shallow_copy(self, super())

    def model_copy(self, *, update=None, deep=False):
        return _native.grid_copy_model(self, super(), update, deep)

    @wraps(EventedModel.model_post_init)
    def model_post_init(self, context, /):
        return _native.component_model_post_init(self, super(), context)

    @wraps(EventedModel.__deepcopy__)
    def __deepcopy__(self, memo=None):
        return _native.component_model_deep_copy(self, super(), memo)

    @wraps(EventedModel.__setstate__)
    def __setstate__(self, state):
        return _native.component_model_restore(self, super(), state)

    def _layout(self, layers):
        return _native.grid_layout(self, layers)

    def actual_shape(self, layers: Sequence[Layer]) -> tuple[int, int]:
        """Return the actual shape of the grid.

        This will return the shape parameter, unless one of the row
        or column numbers is -1 in which case it will compute the
        optimal shape of the grid given the number of layers and
        current stride.

        If the grid is not enabled, this will return (1, 1).

        Parameters
        ----------
        layers : Sequence[Layer]
            List of layers that need to be placed in the grid.

        Returns
        -------
        shape : 2-tuple of int
            Number of rows and columns in the grid.
        """
        return _native.grid_actual_shape(self, layers)

    def position(self, index: int, layers: Sequence[Layer]) -> tuple[int, int]:
        """Return the position of a given linear index in the grid, or (-1, -1) if the layer is hidden/excluded.

        If the grid is not enabled, this will return (0, 0).

        Parameters
        ----------
        index : int
            Position of current layer in layer list.
        layers : Sequence[Layer]
            List of layers that need to be placed in the grid.

        Returns
        -------
        position : 2-tuple of int
            Row and column position of current index in the grid, or (-1, -1) if the layer is hidden/excluded.
        """
        return _native.grid_position(self, index, layers)

    def contents_at(
        self, position: tuple[int, int], layers: Sequence[Layer]
    ) -> tuple[int, ...]:
        """Return the indices contained in the viewbox at the given position.

        If the grid is not enabled, this will return ().

        Parameters
        ----------
        position : 2-tuple of int
            Row and column position of current index in the grid.
        Returns
        -------
        indices : tuple of int
            Position of current layer in layer list.
        """
        return _native.grid_contents(self, position, layers)

    def iter_viewboxes(
        self, layers: Sequence[Layer]
    ) -> Iterator[tuple[tuple[int, int], tuple[int, ...]]]:
        """Iterate over each viewbox and its contained indices.

        Parameters
        ----------
        layers : Sequence[Layer]
            List of layers that need to be placed in the grid.

        Yields
        -------
        position : 2-tuple of int
            Row and column position of current index in the grid.
        indices : tuple of int
            Position of current layer in layer list.
        """
        yield from _native.grid_viewboxes(self, layers)

    def _compute_canvas_spacing(
        self,
        canvas_size: tuple[int, int] | np.ndarray,
        layers: Sequence[Layer],
    ) -> int:
        """Compute the spacing between viewboxes in canvas pixels.

        If the spacing is between 0 and 1, it is interpreted as a proportion
        of the size of the individual viewboxes.
        If it is equal to or greater than 1, it is interpreted as screen pixels.

        This value is restricted so that it does not cause viewboxes to become
        too small (<20px). If the spacing value is too large,
        then viewboxes will dissapear. If viewboxes are too small than
        there will be a division by zero for zoom calculation.
        """
        # limit spacing to avoid degenerate viewboxes
        # TODO: this should probably be done through a validator that somehow gets
        #       updated based on the canvas size and len(layers)...
        return _native.grid_spacing(self, canvas_size, layers)

    def _compute_canvas_spacing_raw(
        self,
        canvas_size: tuple[int, int] | np.ndarray,
        layers: Sequence[Layer],
    ) -> int:
        """Compute the raw spacing between viewboxes in canvas pixels.

        If the spacing is between 0 and 1, it is interpreted as a proportion
        of the size of the individual viewboxes.
        If it is equal to or greater than 1, it is interpreted as screen pixels.

        This value is unrestricted (can result in degenerate viewboxes).
        """
        return _native.grid_spacing_raw(self, canvas_size, layers)

    def _effective_indices(self, layers: Sequence[Layer]) -> list[int]:
        """Return indices of layers that are active (visible) in the grid.

        Only visible layers occupy grid viewboxes, so hidden layers never
        create empty viewboxes. Stacking within a viewbox is still determined
        by each layer's original index and the stride sign.
        """
        return _native.grid_effective_indices(self, layers)

    def _viewbox_groups(
        self, layers: Sequence[Layer]
    ) -> tuple[dict[int, int], list[int]]:
        """Return the viewbox grouping for the given layers.

        Parameters
        ----------
        layers : Sequence[Layer]
            List of layers that need to be placed in the grid.

        Returns
        -------
        group_of : dict[int, int]
            Maps each layer index to its viewbox group. Positive stride uses
            contiguous ranges of original indices (``i // stride``) so toggling
            visibility never moves a layer. Negative stride follows napari's
            reference behavior of reversing the sequence, then packing
            ``stride`` layers per viewbox (``(len(layers) - 1 - i) // stride``)
            so hidden layers never collapse or re-pack visible layers.
        occupied : list[int]
            Sorted viewbox groups that contain at least one visible layer.
        """

        return _native.grid_groups(self, layers)


_native.install_grid_namespace(GridCanvas)
