"""Exact-profile and runtime controls for experimental fifty-entry frames."""
import inspect
import json
from pathlib import Path
import sys
from types import CodeType, FunctionType, SimpleNamespace

import numpy as np
import pytest

import _frontend_policy_probe as probe
from napari.layers import Labels
from napari.layers.base import Layer
from napari.layers.labels import _labels_mouse_bindings as mouse, _labels_utils as utils
from napari.layers.labels.labels import WrongSelectedLabelError
from napari.utils import interactions
from naparixx import _adapters

from freeze_transport import profile
from compiler_controls import controls
from inspect_inputs import STAGE, walk


def tuples(value):
    return tuple(tuples(item) for item in value) if isinstance(value, list) else value


FROZEN = controls()
SOURCE_PROFILES = tuple(tuples(row["original_profile_18"]) for row in FROZEN["frontend_rows"])
SHELL_PROFILES = tuple(tuples(row["shell_profile_18"]) for row in FROZEN["frontend_rows"])
ALL_SHELLS = list(walk(compile((STAGE / "frontend_shells.py").read_bytes(), str(STAGE / "frontend_shells.py"), "exec", dont_inherit=True)))
SHELLS = tuple(next(code for code in ALL_SHELLS if code.co_name == f"f{i}") for i in range(50))
PROTOCOL = next(code for code in walk(compile((STAGE / "state_protocol.py").read_bytes(), str(STAGE / "state_protocol.py"), "exec", dont_inherit=True)) if code.co_name == "protocol")
PROTOCOL_PROFILE = tuples(profile(PROTOCOL))
INTERPOLATION = inspect.unwrap(utils.interpolate_coordinates)
RAY = inspect.unwrap(utils.first_nonzero_coordinate)
INTERPOLATION_ADAPTER = inspect.getclosurevars(utils.interpolate_coordinates).nonlocals["adapter"]
RAY_ADAPTER = inspect.getclosurevars(utils.first_nonzero_coordinate).nonlocals["adapter"]
ORIGINALS = (
    Labels._validate_label_in_range, Labels._validate_non_painted_coord, Labels.fill,
    Labels._get_flood_mask_and_bbox, Labels._draw, Labels.paint, Labels._get_brush_mask_and_bbox,
    Labels.paint_polygon, Labels._get_polygon_mask_and_bbox, Labels._create_polygon_mask,
    Labels._create_brush_mask, Labels._compute_mask_bbox, Labels._build_slice_key,
    Labels._get_shape_and_dims_to_paint, Labels._get_dims_to_paint, WrongSelectedLabelError.__init__,
    Labels.contiguous.fget, Labels.n_edit_dimensions.fget, Labels.brush_size.fget, Labels.brush_size.fset,
    Labels._calculate_cursor_size, Labels.selected_label.fget, Labels.selected_label.fset, Labels.get_color,
    Labels.show_selected_label.fget, Labels.preserve_labels.fget, mouse.draw, mouse.pick,
    mouse.BrushSizeOnMouseMove.__init__, mouse.BrushSizeOnMouseMove.__call__, mouse.BrushSizeOnMouseMove._on_modifiers_change,
    INTERPOLATION, utils.get_dtype, RAY, utils.mouse_event_to_labels_coordinate,
    _adapters.interpolation_adapter, INTERPOLATION_ADAPTER, _adapters.ray_adapter, RAY_ADAPTER,
    _adapters._plain_real, Layer.ndim.fget, Layer.cursor.fget, Layer.cursor.fset,
    Layer.cursor_size.fget, Layer.cursor_size.fset, interactions.mouse_press_callbacks,
    interactions.mouse_move_callbacks, interactions.mouse_release_callbacks, Labels.colormap.fget, Layer.scale.fget)
assert len(ORIGINALS) == 50


def clone(original):
    result = FunctionType(original.__code__, original.__globals__, original.__name__, original.__defaults__, original.__closure__)
    result.__kwdefaults__ = original.__kwdefaults__
    result.__annotations__ = original.__annotations__
    result.__dict__.update(original.__dict__)
    result.__doc__ = original.__doc__
    result.__module__ = original.__module__
    result.__qualname__ = original.__qualname__
    return result


def prepared(entry, original=None):
    original = ORIGINALS[entry] if original is None else original
    assert profile(original.__code__) == FROZEN["frontend_rows"][entry]["original_profile_18"]
    result = clone(original)
    result.__code__ = probe.prepare_unified_probe(result, entry, SHELLS, SOURCE_PROFILES,
                                                 SHELL_PROFILES, PROTOCOL, PROTOCOL_PROFILE)
    return result


def freeze(value):
    if isinstance(value, np.ndarray):
        return (str(value.dtype), value.shape, value.tolist())
    if isinstance(value, (tuple, list)):
        return (type(value).__name__, tuple(freeze(v) for v in value))
    return value
