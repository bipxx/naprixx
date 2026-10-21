from __future__ import annotations

import contextlib
import typing
import warnings
from collections.abc import Iterable
from functools import cached_property
from typing import TYPE_CHECKING, Literal

import numpy as np
import pint
from naparixx import native as _native

from napari.components.dims import RangeTuple
from napari.layers import Layer
from napari.layers.utils.layer_utils import Extent, LayerListExtent
from napari.utils.events import Event
from napari.utils.events.containers import SelectableEventedList
from napari.utils.naming import inc_name_count
from napari.utils.transforms._units import get_units_from_name

if TYPE_CHECKING:
    from typing import Self

    from npe2.manifest.io import WriterContribution

__all__ = ('LayerList',)


def get_name(layer: Layer) -> str:
    """Return the name of a layer."""
    return _native.layerlist_get_name(layer)


def _layerlist_yield_from(iterator):
    """Preserve Python generator protocols for the native lazy value iterator."""
    yield from iterator


class LayerList(SelectableEventedList[Layer]):
    """List-like layer collection with built-in reordering and callback hooks.

    Parameters
    ----------
    data : iterable
        Iterable of napari.layer.Layer

    Events
    ~~~~~~
    inserting : (index: int)
        emitted before an item is inserted at ``index``
    inserted : (index: int, value: T)
        emitted after ``value`` is inserted at ``index``
    removing : (index: int)
        emitted before an item is removed at ``index``
    removed : (index: int, value: T)
        emitted after ``value`` is removed at ``index``
    moving : (index: int, new_index: int)
        emitted before an item is moved from ``index`` to ``new_index``
    moved : (index: int, new_index: int, value: T)
        emitted after ``value`` is moved from ``index`` to ``new_index``
    changed : (index: int, old_value: T, value: T)
        emitted when item at ``index`` is changed from ``old_value`` to
        ``value``
    changed <OVERLOAD> : (index: slice, old_value: List[_T], value: List[_T])
        emitted when items at ``index``es are changed from ``old_value`` to
        ``value``
    reordered : (value: self)
        emitted when the list is reordered (eg. moved/reversed).
    renamed : (index: int)
        emitted when a layer in the list is renamed, providing the index
        of the layer.
    selection.events.changed : (added: Set[_T], removed: Set[_T])
        emitted when the set changes, includes item(s) that have been added
        and/or removed from the set.
    selection.events.active : (value: _T)
        emitted when the current item has changed.
    selection.events._current : (value: _T)
        emitted when the current item has changed. (Private event)
    units: (value: tuple[pint.Unit, ...] | None)
        emitted when the global overriding units for the layer list is changed.


    Notes
    -----

    Note that ``changed`` events are only emitted when an element of the
    list changes, *not* when the list itself changes (for example when items
    are added or removed). For example, ``layerlist.append(layer)`` will emit
    an ``inserted`` event. ``layerlist[idx] = layer`` *will* emit a ``changed``
    event.

    However, the layerlist does not have a way of detecting when an object in
    the list is modified in-place. Therefore, although
    ``layerlist[idx].scale = [2, 1, 1]`` changes the *value* of the layer at
    position ``idx``, a ``changed`` event will not be emitted.

    Examples
    --------

    >>> import napari
    >>> from skimage.data import astronaut
    >>> viewer = napari.Viewer()
    >>> event_list = []

    Connect to the event list:

    >>> viewer.layers.events.connect(event_list.append)
    <built-in method append of list object at 0x7fc225764780>

    >>> viewer.add_image(astronaut())
    <Image layer 'Image' at 0x7f0fe7fa9e90>
    >>> viewer.add_points()
    <Points layer 'Points' at 0x7f102f962350>
    >>> viewer.layers
    [<Image layer 'Image' at 0x7f0fe7fa9e90>, <Points layer 'Points' at 0x7f102f962350>]

    Inspecting the list of events, we see:

    >>> event_list[0].type
    'inserting'
    >>> viewer.layers.pop(1)
    <Points layer 'Points' at 0x7f102f962350>
    >>> event_list[-1].type
    'removed'

    """

    def __init__(self, data=()) -> None:
        return _native.layerlist_init(self, data, __class__)

    def _on_layer_inserted(self, event: Event):
        """Connect to layer events when a new layer is inserted."""
        return _native.layerlist_inserted(self, event)

    def _on_layer_removed(self, event: Event):
        """Disconnect from layer events when a layer is removed."""
        return _native.layerlist_removed(self, event)

    def _on_layer_renamed(self, event: Event):
        """Re-emit layer name changes from a layer as a LayerList event."""
        return _native.layerlist_renamed(self, event)

    def _create_contexts(self):
        """Create contexts to manage enabled/visible action/menu states.

        Connects LayerList and Selection[Layer] to their context keys to allow
        actions and menu items (in the GUI) to be dynamically enabled/disabled
        and visible/hidden based on the state of layers in the list.
        """

        # TODO: figure out how to move this context creation bit.
        # Ideally, the app should be aware of the layerlist, but not vice versa.
        # This could probably be done by having the layerlist emit events that
        # the app connects to, then the `_ctx` object would live on the app,
        # (not here)
        return _native.layerlist_contexts(self)

    def _process_delete_item(self, item: Layer):
        return _native.layerlist_delete(self, item, __class__)

    def _refresh_selection_ctx_keys(self, event):
        return _native.layerlist_refresh_selection(self, event)

    def _clean_cache(self):
        return _native.layerlist_clean_cache(self)

    def __newlike__(self, data):
        return _native.layerlist_newlike(self, data)

    def _coerce_name(self, name, layer=None):
        """Coerce a name into a unique equivalent.

        Parameters
        ----------
        name : str
            Original name.
        layer : napari.layers.Layer, optional
            Layer for which name is generated.

        Returns
        -------
        new_name : str
            Coerced, unique name.
        """
        return _native.coerce_layer_name(self, name, layer)

    def _update_name(self, event):
        """Coerce name of the layer in `event.layer`."""
        return _native.layerlist_update_name(self, event)

    def _ensure_unique(self, values, allow=()):
        return _native.unique_layers(self._list, values, allow)

    @typing.overload
    def __getitem__(self, item: int | str) -> Layer: ...

    @typing.overload
    def __getitem__(self, item: slice) -> Self: ...

    def __getitem__(self, item):
        return _native.layerlist_getitem(self, item, __class__)

    def __setitem__(self, key, value):
        return _native.layerlist_setitem(self, key, value, __class__)

    def insert(self, index: int, value: Layer):
        """Insert ``value`` before index."""
        return _native.layerlist_insert(self, index, value, __class__)

    def remove_selected(self):
        """Remove selected layers from LayerList, but first unlink them."""
        return _native.layerlist_remove_selected(self, __class__)

    def toggle_selected_visibility(self):
        """Toggle visibility of selected layers"""
        return _native.layerlist_toggle_visible(self)

    @cached_property
    def _extent_world(
        self,
    ) -> np.ndarray[tuple[Literal[2], int], np.dtype[np.float64]]:
        """Extent of layers in world coordinates.

        Default to 2D with (-0.5, 511.5) min/ max values if no data is present.
        Corresponds to pixels centered at [0, ..., 511].

        Returns
        -------
        extent_world : array, shape (2, D)
        """
        return _native.layerlist_cached_world(self, False)

    @cached_property
    def _extent_world_augmented(
        self,
    ) -> np.ndarray[tuple[Literal[2], int], np.dtype[np.float64]]:
        """Extent of layers in world coordinates.

        Default to 2D with (-0.5, 511.5) min/ max values if no data is present.
        Corresponds to pixels centered at [0, ..., 511].

        Returns
        -------
        extent_world : array, shape (2, D)
        """
        return _native.layerlist_cached_world(self, True)

    def _get_min_and_max(self, mins_list, maxes_list):
        # Reverse dimensions since it is the last dimensions that are
        # displayed.
        return _native.layer_extent_bounds(mins_list, maxes_list)

    def _get_extent_world(
        self,
        layer_extent_list,
        augmented: bool = False,
        units: tuple[pint.Unit, ...] | None = None,
    ) -> np.ndarray[tuple[Literal[2], int], np.dtype[np.float32]]:
        """Extent of layers in world coordinates.

        Default to 2D image-like with (0, 511) min/ max values if no data is present.
        Corresponds to image with 512 pixels in each dimension.

        Returns
        -------
        extent_world : array, shape (2, D)
        """
        return _native.layerlist_extent_world(self, layer_extent_list, augmented, units)

    @cached_property
    def _step_size(self) -> np.ndarray:
        """Ideal step size between planes in world coordinates.

        Computes the best step size that allows all data planes to be
        sampled if moving through the full range of world coordinates.
        The current implementation just takes the minimum scale.

        Returns
        -------
        step_size : array, shape (D,)
        """
        return _native.layerlist_cached_step(self)

    def _step_size_from_scales(self, scales):
        # Reverse order so last axes of scale with different ndim are aligned
        return _native.layer_step_sizes(scales)

    @staticmethod
    def _convert_scale_between_units(
        scale: np.ndarray[tuple[int], np.dtype[np.float32]],
        from_units: tuple[pint.Unit, ...],
        to_units: tuple[pint.Unit, ...],
    ) -> np.ndarray:
        """Convert units of scale to target units.

        Parameters
        ----------
        scale : np.ndarray
            Scale to convert.
        from_units : tuple[pint.Unit, ...]
            Units of the scale.
        to_units : tuple[pint.Unit, ...]
            Target units.

        Returns
        -------
        np.ndarray
            Converted scale.
        """
        return _native.convert_layer_scale(scale, from_units, to_units)

    def _get_step_size(
        self,
        layer_extent_list: list[Extent],
        units: tuple[pint.Unit, ...] | None = None,
    ):
        return _native.layerlist_step(self, layer_extent_list, units)

    def get_extent(self, layers: Iterable[Layer]) -> LayerListExtent:
        """
        Return extent for a given layer list.

        Extent bounds are inclusive. This function is useful for calculating the extent
        of a subset of layers when preparing and updating some supplementary layers.
        For example see the cross Vectors layer in the `multiple_viewer_widget` example.

        Parameters
        ----------
        layers : list of Layer
            list of layers for which extent should be calculated

        Returns
        -------
        extent : LayerListExtent
            extent for selected layers
        """
        return _native.layerlist_get_extent(self, layers)

    @staticmethod
    def _get_units(
        layers_extent: list[Extent],
    ) -> tuple[pint.Unit, ...] | None:
        """Get units for a given list of layer extents.

        Given extents could be of all layers in the `LayerList` or only a subset.

        Parameters
        ----------
        layers_extent : list of layer Extent
            list of layer extents for which units should be calculated

        Returns
        -------
        units : list of pint.Unit or None
            consistent units for given layer extents.
            If cannot be determined, returns None.
        """
        return _native.common_layer_units(layers_extent)

    @cached_property
    def extent(self) -> LayerListExtent:
        """
        Extent of layers in data and world coordinates.

        Extent bounds are inclusive. See Layer.extent for a detailed explanation
        of how extents are calculated.
        """
        return _native.layerlist_cached_extent(self)

    @property
    def _ranges(self) -> tuple[RangeTuple, ...]:
        """Get ranges for Dims.range in world coordinates."""
        return _native.layerlist_ranges(self)

    @property
    def units(self) -> tuple[pint.Unit, ...] | None:
        """Units of layers in world coordinates.

        Returns
        -------
        units : tuple[pint.Unit, ...] or None
            Units of layers in world coordinates.
        """
        return _native.layerlist_units(self)

    @units.setter
    def units(self, units: tuple[pint.Unit, ...] | None):
        """Set override of units of layers in world coordinates.

        Parameters
        ----------
        units : tuple[pint.Unit, ...] or None
            Units to set for layers in world coordinates. If None, units are not changed.
        """
        return _native.layerlist_set_units(self, units)

    def _trigger_check_ndim_and_maybe_clean_units(self, event: Event):
        """Trigger check of new layer's ndim and maybe clean units."""
        return _native.layerlist_trigger_units(self, event)

    def _check_ndim_and_maybe_clean_units(self, ndim: int):
        return _native.layerlist_check_units(self, ndim)

    @property
    def ndim(self) -> int:
        """Maximum dimensionality of layers.

        Defaults to 2 if no data is present.

        Returns
        -------
        ndim : int
        """
        return _native.layerlist_ndim(self)

    def _find_longest_annotated(self) -> tuple[str, ...]:
        """Return the annotated axis labels spanning the most dimensions.

        Returns () when no layer is annotated. When more are equally long,
        the most recent layer wins.
        """
        return _native.layerlist_longest_labels(self)

    @property
    def axis_labels(self) -> tuple[str, ...]:
        """Axis labels for the layer list.

        Uses the labels from the layer with the longest annotated axis labels.
        Missing axes annotations fall back to default indices.
        """
        return _native.layerlist_axis_labels(self)

    def _link_layers(
        self,
        method: str,
        layers: Iterable[str | Layer] | None = None,
        attributes: Iterable[str] = (),
    ):
        # adding this method here allows us to emit an event when
        # layers in this group are linked/unlinked.  Which is necessary
        # for updating context
        return _native.layerlist_link(self, method, layers, attributes)

    def link_layers(
        self,
        layers: Iterable[str | Layer] | None = None,
        attributes: Iterable[str] = (),
    ):
        """
        Links the selected layers.

        Once layers are linked, any action performed on one layer will be
        performed on all linked layers at the same time.
        """
        return _native.layerlist_link_layers(self, layers, attributes)

    def unlink_layers(
        self,
        layers: Iterable[str | Layer] | None = None,
        attributes: Iterable[str] = (),
    ):
        """Unlinks previously linked layers.

        Changes to one of the layer's properties no longer result in the same
        changes to the previously linked layers.
        """
        return _native.layerlist_unlink_layers(self, layers, attributes)

    def save(
        self,
        path: str,
        *,
        selected: bool = False,
        plugin: str | None = None,
        _writer: WriterContribution | None = None,
    ) -> list[str]:
        """Save all or only selected layers to a path using writer plugins.

        If ``plugin`` is provided, we attempt to write with that plugin.
        If ``plugin`` is not provided, we call the first compatible writer for
        the given layer(s) and path.
        If any errors occur during writing, they are raised. If the given ``plugin``
        does not provide a compatible writer or does not write any files, or
        if there are no compatible writers available, a warning will be issued.

        Parameters
        ----------
        path : str
            A filepath, directory, or URL to open.  Extensions may be used to
            specify output format (provided a plugin is available for the
            requested format).
        selected : bool
            Optional flag to only save selected layers. False by default.
        plugin : str, optional
            Name of the plugin to use for saving. If None then the first compatible
            writer (if one exists), will be used.
        _writer : WriterContribution, optional
            private: npe2 specific writer override.

        Returns
        -------
        list of str
            File paths of any files that were written.
        """
        return _native.layerlist_save(self, path, selected, plugin, _writer)

    def clear(self):
        """Remove all layers from viewer."""
        return _native.layerlist_clear(self, __class__)

    @contextlib.contextmanager
    def batched_update(self):
        with _native.layerlist_batch_context(self):
            yield


def cmp(u1: pint.Unit, u2: pint.Unit, registry: pint.UnitRegistry) -> bool:
    """
    Compare two units using pint register

    Parameters
    ----------
    u1: pint.Unit
        first unit to compare
    u2: pint.Unit
        second unit to compare
    registry: pint.UnitRegistry
        unit registry used for unit comparison

    Returns
    -------
    bool
        True if u1 is smaller than u2, False otherwise

    """
    return _native.unit_less(u1, u2, registry)


def update_u_dkt(
    units_l: Iterable[pint.Unit],
    dimensionality_to_unit: dict[pint.util.UnitsContainer, pint.Unit],
    registry: pint.UnitRegistry,
) -> None:
    """Update the dimensionality_to_unit dictionary with the smallest units.

    Iterate over the units in units_t and for each dimensionality,
    check if it is already in the dimensionality_to_unit dictionary.
    If it is not, or if the current unit is 'smaller' than the existing
    unit (as determined by the cmp function), update the dictionary

    Parameters
    ----------

    units_l : iterable of pint.Unit
        sequence of units to be checked and possibly updated
    dimensionality_to_unit : dict
        dictionary mapping dimensionality to the smallest unit
    registry : pint.UnitRegistry
        unit registry used for unit comparison
    """
    return _native.update_unit_selection(units_l, dimensionality_to_unit, registry)
