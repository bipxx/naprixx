"""Finite contracts for the 39 scalar-data/viewport operations.

NAPARIXX_SCALAR_VIEWPORT_BASELINE=1 compares separately compiled pinned and
frozen-before methods without importing napari. Default mode compares those
pinned methods with actual installed current classes/functions and canonical
native receivers. No original body is installed into an application module.

All 39 before bodies are AST-identical to pinned 0.9.2. The independent oracle
also rebuilds Layer.units/ndim getters and the original world-dimension helper
as declared prerequisites. NumPy, copy and Python protocols are real external
primitives; transforms, events, dims, slice-model factories, multiscale wrapping,
thumbnail/refresh policy and backend metadata are controlled boundary services.
Their owned implementations and actual constructors/Viewer workflows are
covered by root's unchanged original and installed integration selectors.

This cohort proves finite publication/order/alias/error contracts, not complete
transform/model/provider authority, escaped-generator frame identity, arbitrary
plugin reflection, Qt/async lifecycle, or cross-platform compatibility.
"""

from __future__ import annotations

import ast
import copy
import gc
import hashlib
import inspect
import itertools
import operator
import os
import weakref
from collections.abc import Sequence
from contextlib import contextmanager
from pathlib import Path
from types import SimpleNamespace

import numpy as np
import pytest

ROOT = Path(__file__).resolve().parents[1]
PINNED = ROOT / ".upstream/napari/src/napari"
BEFORE = ROOT / ".artifacts/scalar-data-viewport-before/files/src/napari"
PIN_HASHES = {
    "layers/base/base.py": (
        "2c9e08f2cf0508fdf2399abf49eea42e"
        "cb545adb81ac4c8422b57103b57eefa4"
    ),
    "layers/_scalar_field/scalar_field.py": (
        "50d0f2c6adb4412589394dde4a55a8b1"
        "56799b89ed55610a57496ef6d0d2a0b4"
    ),
    "layers/image/image.py": (
        "73027b2f096445c6aea88689bbf29f87"
        "07a75847d1348fa3b80eaf867c728c56"
    ),
    "layers/utils/layer_utils.py": (
        "6d3ed1151813da2cc39a1d476fb0b13c"
        "d712e1b741c0a42fb02d7b7f6a244d6d"
    ),
    "layers/base/_slice.py": (
        "606b4e936922e0bb2c1bb6b3eed74a7d"
        "819db5b952cb3a954ff16395b8f80cfd"
    ),
}
BEFORE_HASHES = {
    "layers/base/base.py": (
        "d5bf0c671340f57fe04a3449ce0abde1"
        "b8d9f91681a04d079f34614a32378404"
    ),
    "layers/_scalar_field/scalar_field.py": (
        "55738eb7c82df1163ac106578f4cc617"
        "4aee1800187c437c10647772163c2cc2"
    ),
    "layers/image/image.py": (
        "d729f919eeb45bf35ed2de1e9b7ce843"
        "3fcde043b2dfe9294a06cad6fe014f39"
    ),
    "layers/utils/layer_utils.py": (
        "799a850c551b4050a91d17915bc1d2ba"
        "1fac9d2366e69e1d1120f76f1e61435d"
    ),
    "layers/base/_slice.py": (
        "32d28189c2944cb5031c8f9e4b2cee6e"
        "1373dd06ed4b8a9172ab3fee91cb4c2b"
    ),
}
OWNED = {
    "State": (
        "_slice_indices",
        "update_dims",
        "set_slice_input_from_dims",
        "set_slice_input",
        "make_slice_input",
        "_world_to_layer_dims",
        "ndim",
    ),
    "Layer": (
        "units:set",
        "_update_dims",
        "_slice_dims",
        "_update_draw",
        "_update_level_and_corners",
        "_reset_editable",
        "editable",
        "editable:set",
        "scale_factor",
        "scale_factor:set",
        "_slice_input",
        "_world_to_layer_dims",
    ),
    "Scalar": (
        "_slice_dtype",
        "_data_view",
        "_slice",
        "data_raw",
        "data",
        "data:set",
        "data_level",
        "data_level:set",
        "locked_data_level",
        "locked_data_level:set",
        "_reset_data_level",
        "_update_level_and_corners",
    ),
    "ScalarState": ("set_slice_input", "_make_slice_request"),
    "Image": ("data:set",),
    "Utils": (
        "compute_multiscale_level",
        "compute_multiscale_level_and_corners",
        "_chunk_boundaries",
        "_chunks_metadata",
        "expand_corners_to_chunk_boundaries",
    ),
}
ORIGINAL_SELECTORS = (
    ".upstream/napari/src/napari/layers/_scalar_field/_tests/"
    "test_scalar_field.py::test_data_setter_updates_transforms",
    ".upstream/napari/src/napari/layers/_scalar_field/_tests/"
    "test_scalar_field.py::test_thumbnail_level_data_refreshed_on_data_replacement",
    ".upstream/napari/src/napari/layers/_scalar_field/_tests/"
    "test_scalar_field.py::test_thumbnail_level_data_uses_new_data_ndim_2d_to_3d",
    ".upstream/napari/src/napari/layers/_scalar_field/_tests/"
    "test_scalar_field.py::test_thumbnail_level_data_uses_new_data_ndim_3d_to_2d",
    ".upstream/napari/src/napari/layers/image/_tests/"
    "test_multiscale.py::test_update_draw_variable_fov_fixed_canvas_size",
    ".upstream/napari/src/napari/layers/image/_tests/"
    "test_multiscale.py::test_update_draw_variable_canvas_size_fixed_fov",
    ".upstream/napari/src/napari/layers/image/_tests/"
    "test_multiscale.py::test_update_draw_expands_chunked_multiscale_fov",
    ".upstream/napari/src/napari/layers/base/_tests/test_base.py::test_assign_units",
    ".upstream/napari/src/napari/layers/base/_tests/"
    "test_base.py::test_no_emmit_on_identical_units",
    ".upstream/napari/src/napari/layers/base/_tests/"
    "test_base.py::test_exception_on_invalid_units",
    ".upstream/napari/src/napari/layers/utils/_tests/"
    "test_layer_utils.py::test_expand_corners_to_chunk_boundaries",
    ".upstream/napari/src/napari/layers/utils/_tests/"
    "test_layer_utils.py::test_expand_corners_to_chunk_boundaries_leaves_sliced_axis",
    ".upstream/napari/src/napari/layers/utils/_tests/"
    "test_layer_utils.py::test_compute_multiscale_level_ignores_non_downsampled_dimension",
    ".upstream/napari/src/napari/layers/utils/_tests/"
    "test_layer_utils.py::test_compute_multiscale_level_ignores_dimension_below_threshold",
    ".upstream/napari/src/napari/layers/utils/_tests/"
    "test_layer_utils.py::test_compute_multiscale_level_returns_zero_when_view_fits_canvas",
)


def _source(root, hashes, path):
    raw = (root / path).read_bytes()
    assert hashlib.sha256(raw).hexdigest() == hashes[path]
    return ast.parse(raw.decode("utf-8"), filename=str(root / path))


def _key(node):
    setter = any(
        isinstance(d, ast.Attribute) and d.attr == "setter" for d in node.decorator_list
    )
    return node.name + (":set" if setter else "")


def _select(tree, class_name, names):
    body = (
        tree.body
        if class_name is None
        else next(
            c.body
            for c in tree.body
            if isinstance(c, ast.ClassDef) and c.name == class_name
        )
    )
    selected = [
        copy.deepcopy(n)
        for n in body
        if isinstance(n, ast.FunctionDef) and _key(n) in names
    ]
    assert {_key(n) for n in selected} == set(names)
    return selected


def _compile(nodes, namespace, filename):
    module = ast.Module(
        body=[
            ast.ImportFrom(
                module="__future__", names=[ast.alias(name="annotations")], level=0
            ),
            *nodes,
        ],
        type_ignores=[],
    )
    ast.fix_missing_locations(module)
    exec(compile(module, filename, "exec"), namespace)


def _class(tree, original_name, name, bases, names, namespace):
    node = ast.ClassDef(
        name=name,
        bases=[ast.Name(id=b, ctx=ast.Load()) for b in bases],
        keywords=[],
        body=_select(tree, original_name, names),
        decorator_list=[],
    )
    _compile([node], namespace, f"<independent-{name}>")
    return namespace[name]


class ControlledMultiScale(list):
    """External wrapping boundary; real owner integration uses original tests."""


class ControlledThick:
    def __init__(self, point):
        self.point = tuple(point)

    @classmethod
    def __class_getitem__(cls, item):
        return cls

    @classmethod
    def from_dims(cls, dims):
        return cls(dims.point)

    def __getitem__(self, item):
        return type(self)(self.point[item])


class ControlledInput:
    def __init__(self, *, ndisplay, world_slice, order):
        self.ndisplay = ndisplay
        self.world_slice = world_slice
        self.order = order


def _namespace():
    from naparixx import _native

    return {
        "__name__": "_scalar_viewport_oracle",
        "_native": _native,
        "np": np,
        "copy": copy,
        "itertools": itertools,
        "operator": operator,
        "Sequence": Sequence,
        "MultiScaleData": ControlledMultiScale,
        "_ThickNDSlice": ControlledThick,
        "_SliceInput": ControlledInput,
        "apply_units_to_transform": lambda transform, units: transform,
        "logger": SimpleNamespace(debug=lambda *a: None),
    }


def _rebuild(root, hashes):
    trees = {p: _source(root, hashes, p) for p in hashes}
    ns = {name: _namespace() for name in ("base", "scalar", "image", "utils")}
    base_tree = trees["layers/base/base.py"]
    state = _class(
        base_tree,
        "_LayerSlicingState",
        "_LayerSlicingState",
        (),
        (*OWNED["State"], "_world_to_layer_dims_impl"),
        ns["base"],
    )
    layer = _class(
        base_tree,
        "Layer",
        "Layer",
        (),
        (*OWNED["Layer"], "units", "ndim"),
        ns["base"],
    )
    _compile(
        _select(trees["layers/utils/layer_utils.py"], None, OWNED["Utils"]),
        ns["utils"],
        "<independent-viewport-utils>",
    )
    ns["scalar"].update(
        {
            "Layer": layer,
            "_LayerSlicingState": state,
            "compute_multiscale_level_and_corners": ns["utils"][
                "compute_multiscale_level_and_corners"
            ],
            "expand_corners_to_chunk_boundaries": ns["utils"][
                "expand_corners_to_chunk_boundaries"
            ],
        }
    )
    scalar_tree = trees["layers/_scalar_field/scalar_field.py"]
    scalar = _class(
        scalar_tree,
        "ScalarFieldBase",
        "ScalarFieldBase",
        ("Layer",),
        OWNED["Scalar"],
        ns["scalar"],
    )
    scalar_state = _class(
        scalar_tree,
        "ScalarFieldSlicingState",
        "ScalarFieldSlicingState",
        ("_LayerSlicingState",),
        OWNED["ScalarState"],
        ns["scalar"],
    )
    ns["image"]["ScalarFieldBase"] = scalar
    image = _class(
        trees["layers/image/image.py"],
        "Image",
        "Image",
        ("ScalarFieldBase",),
        OWNED["Image"],
        ns["image"],
    )
    return SimpleNamespace(
        Layer=layer,
        State=state,
        Scalar=scalar,
        ScalarState=scalar_state,
        Image=image,
        Utils=SimpleNamespace(**{k: ns["utils"][k] for k in OWNED["Utils"]}),
        ns=ns,
        actual=False,
    )


def _installed():
    import napari.layers._scalar_field.scalar_field as scalar
    import napari.layers.base.base as base
    import napari.layers.image.image as image
    import napari.layers.utils.layer_utils as utils
    from naparixx import _native

    return SimpleNamespace(
        Layer=base.Layer,
        State=base._LayerSlicingState,
        Scalar=scalar.ScalarFieldBase,
        ScalarState=scalar.ScalarFieldSlicingState,
        Image=image.Image,
        Utils=utils,
        ns={
            "base": vars(base),
            "scalar": vars(scalar),
            "image": vars(image),
            "utils": vars(utils),
        },
        actual=True,
        native=_native,
    )


@pytest.fixture
def pair():
    original = _rebuild(PINNED, PIN_HASHES)
    candidate = (
        _rebuild(BEFORE, BEFORE_HASHES)
        if os.environ.get("NAPARIXX_SCALAR_VIEWPORT_BASELINE") == "1"
        else _installed()
    )
    return original, candidate


def _probe(side, kind, overrides=None, **attrs):
    base = getattr(side, kind)
    # Supply only actual abstract construction hooks; operations are untouched.
    methods = {
        name: lambda self, *a, **k: None
        for name in getattr(base, "__abstractmethods__", ())
    }
    # These already-owned shape services are outside this 39-body boundary.
    # Use the same live test namespace payload on both sides; a property on
    # the actual class would otherwise ignore an instance dictionary entry.
    for key in ("dtype", "level_shapes", "downsample_factors"):
        if key in attrs:
            methods[key] = property(lambda self, key=key: vars(self)[key])
    methods.update(overrides or {})
    cls = type(f"Controlled{kind}", (base,), methods)
    obj = object.__new__(cls)
    vars(obj).update(attrs)
    if side.actual:
        assert isinstance(obj, side.native.LayerNamespaceBase)
        assert vars(obj) is obj.__dict__
    return obj


def _function(side, kind, key):
    name, *suffix = key.split(":")
    value = inspect.getattr_static(getattr(side, kind), name)
    if isinstance(value, property):
        return value.fset if suffix else value.fget
    return value.__func__ if isinstance(value, staticmethod) else value


def _call(side, kind, key, obj, *args, **kwargs):
    return _function(side, kind, key)(obj, *args, **kwargs)


@contextmanager
def _services(side, **updates):
    saved = []
    missing = object()
    try:
        for namespace in side.ns.values():
            for key, value in updates.items():
                saved.append((namespace, key, namespace.get(key, missing)))
                namespace[key] = value
        yield
    finally:
        for namespace, key, value in reversed(saved):
            if value is missing:
                namespace.pop(key, None)
            else:
                namespace[key] = value


def _event(trace, name, fail=False):
    def emit(**kwargs):
        trace.append((name, tuple(kwargs)))
        if fail:
            raise LookupError(name)

    return emit


def _record(trace, name, fail=False):
    def callback(*args, **kwargs):
        trace.append((name, kwargs))
        if fail:
            raise LookupError(name)

    return callback


def test_all_39_source_metadata_and_current_adoption(pair):
    original, current = pair
    count = 0
    for kind, keys in OWNED.items():
        for key in keys:
            old = (
                getattr(original.Utils, key)
                if kind == "Utils"
                else _function(original, kind, key)
            )
            new = (
                getattr(current.Utils, key)
                if kind == "Utils"
                else _function(current, kind, key)
            )
            assert str(inspect.signature(old)) == str(inspect.signature(new))
            assert old.__doc__ == new.__doc__
            count += 1
            if current.actual:
                assert "_native" in new.__globals__
                assert "_native" in new.__code__.co_names
    assert count == 39


def test_live_getters_and_dynamic_dimension_dispatch(pair):
    for side in pair:
        image = SimpleNamespace(view=object())
        response = SimpleNamespace(image=image)
        state = SimpleNamespace(_slice=response, _slice_input=object())
        layer = _probe(
            side,
            "Scalar",
            _slicing_state=state,
            _data_raw=object(),
            _data=object(),
            _data_level=2,
            _locked_data_level=None,
            _editable=True,
            _scale_factor=3,
            _ndim=4,
            dtype=np.dtype("uint16"),
        )
        saved = vars(layer)
        assert _call(side, "Scalar", "_slice", layer) is response
        assert _call(side, "Scalar", "_data_view", layer) is image.view
        assert _call(side, "Layer", "_slice_input", layer) is state._slice_input
        for key, field in (
            ("data_raw", "_data_raw"),
            ("data", "_data"),
            ("data_level", "_data_level"),
            ("locked_data_level", "_locked_data_level"),
        ):
            assert _call(side, "Scalar", key, layer) is saved[field]
        assert _call(side, "Scalar", "_slice_dtype", layer) == np.dtype("uint16")
        saved["_editable"] = False
        saved["_scale_factor"] = 7
        assert _call(side, "Layer", "editable", layer) is False
        assert _call(side, "Layer", "scale_factor", layer) == 7
        st = _probe(side, "State", layer=layer)
        assert _call(side, "State", "ndim", st) == 4
        layer._slicing_state = st
        np.testing.assert_array_equal(
            _call(
                side,
                "Layer",
                "_world_to_layer_dims",
                layer,
                world_dims=[2, 1, 0, 3],
                ndim_world=4,
            ),
            [2, 1, 0, 3],
        )
        saved["_ndim"] = 2
        np.testing.assert_array_equal(
            _call(
                side,
                "Layer",
                "_world_to_layer_dims",
                layer,
                world_dims=[2, 1, 0, 3],
                ndim_world=4,
            ),
            [0, 1],
        )
        del saved["_data_raw"]
        with pytest.raises(AttributeError):
            _call(side, "Scalar", "data_raw", layer)


@pytest.mark.parametrize(
    "case", ["single", "multi", "wrap_error", "dims_error", "event_error"]
)
def test_data_replacement_publication_and_failure_order(pair, case):
    def observe(side):
        trace = []
        payload = [np.ones((8, 8)), np.ones((4, 4))]
        multi = case != "single"

        class Wrapper(ControlledMultiScale):
            def __init__(self, data):
                trace.append(("wrap", data is payload))
                if case == "wrap_error":
                    raise LookupError("wrap")
                super().__init__(data)

        layer = _probe(
            side,
            "Scalar",
            multiscale=multi,
            _data="previous",
            _data_raw="old",
            _locked_data_level=1,
            _data_level=1,
            _reset_thumbnail_level_data=_record(trace, "thumbnail"),
            _update_dims=_record(trace, "dims", case == "dims_error"),
            _reset_editable=_record(trace, "editable"),
        )

        def data_event(**kwargs):
            trace.append(("data_event", kwargs["value"] is layer._data))
            assert layer._data_raw is payload
            if case == "event_error":
                raise LookupError("data_event")

        layer.events = SimpleNamespace(data=data_event)
        with _services(side, MultiScaleData=Wrapper):
            if case.endswith("error"):
                with pytest.raises(LookupError):
                    _call(side, "Scalar", "data:set", layer, payload)
            else:
                _call(side, "Scalar", "data:set", layer, payload)
        assert layer._data_raw is payload
        if case == "wrap_error":
            assert layer._data == "previous"
            assert layer._locked_data_level == 1
        else:
            assert (layer._data is payload) is (not multi)
            assert layer._locked_data_level is None
            assert layer._data_level == (1 if multi else 0)
        return trace

    original, current = map(observe, pair)
    assert current == original
    expected = [] if case == "single" else [("wrap", True)]
    if case != "wrap_error":
        expected += [("thumbnail", {}), ("dims", {})]
    if case not in ("wrap_error", "dims_error"):
        expected += [("data_event", True)]
    if not case.endswith("error"):
        expected += [("editable", {})]
    assert original == expected


@pytest.mark.parametrize("case", ["off", "on", "base_error"])
def test_image_setter_fresh_contrast_after_base_publication(pair, case):
    def observe(side):
        trace = []
        payload = np.ones((3, 4))
        layer = _probe(
            side,
            "Image",
            overrides={
                "auto_contrast": property(lambda self: self._auto),
            },
            _auto=case == "on",
            multiscale=False,
            _data="old",
            _reset_data_level=_record(trace, "level"),
            _reset_thumbnail_level_data=_record(trace, "thumbnail"),
            _update_dims=_record(trace, "dims", case == "base_error"),
            _reset_editable=_record(trace, "editable"),
            reset_contrast_limits=_record(trace, "contrast"),
            events=SimpleNamespace(data=_event(trace, "data")),
        )
        with _services(side, ScalarFieldBase=side.Scalar):
            if case == "base_error":
                with pytest.raises(LookupError):
                    _call(side, "Image", "data:set", layer, payload)
            else:
                _call(side, "Image", "data:set", layer, payload)
        assert layer._data is payload
        return [name for name, _ in trace]

    old, new = map(observe, pair)
    assert new == old
    assert old == (
        ["level", "thumbnail", "dims"]
        if case == "base_error"
        else ["level", "thumbnail", "dims", "data", "editable"]
        + (["contrast"] if case == "on" else [])
    )


@pytest.mark.parametrize("case", ["same", "changed", "refresh_error"])
def test_data_level_store_precedes_refresh(pair, case):
    def observe(side):
        trace = []
        layer = _probe(side, "Scalar", _data_level=2)

        def refresh(**kwargs):
            trace.append((layer._data_level, kwargs))
            if case == "refresh_error":
                raise LookupError("refresh")

        layer.refresh = refresh
        level = 2 if case == "same" else 3
        if case == "refresh_error":
            with pytest.raises(LookupError):
                _call(side, "Scalar", "data_level:set", layer, level)
        else:
            _call(side, "Scalar", "data_level:set", layer, level)
        assert layer._data_level == level
        assert trace == ([] if case == "same" else [(3, {"extent": False})])

    for side in pair:
        observe(side)


@pytest.mark.parametrize("level", [-1, 3, 1, None])
def test_locked_level_validation_corners_and_event_order(pair, level):
    def observe(side):
        trace = []
        layer = _probe(
            side,
            "Scalar",
            _locked_data_level=2,
            _data_level=2,
            _ndim=3,
            level_shapes=np.array([[5, 12, 16], [5, 6, 8], [5, 3, 4]]),
            _slicing_state=SimpleNamespace(
                    _slice_input=SimpleNamespace(displayed=[1, 2])
            ),
            refresh=_record(trace, "refresh"),
            events=SimpleNamespace(locked_data_level=_event(trace, "locked")),
        )

        def reset():
            trace.append(("reset", {}))
            layer._data_level = 0

        layer._reset_data_level = reset
        _call(side, "Scalar", "locked_data_level:set", layer, level)
        if level in (-1, 3):
            assert layer._locked_data_level == 2
            assert "corner_pixels" not in vars(layer)
        elif level is not None:
            assert layer._locked_data_level == level
            assert layer._data_level == level
            np.testing.assert_array_equal(layer.corner_pixels, [[0, 0, 0], [0, 5, 7]])
        else:
            assert layer._locked_data_level is None
            assert layer._data_level == 0
        return trace

    old, new = map(observe, pair)
    assert new == old
    assert old == (
        []
        if level in (-1, 3)
        else ([("reset", {})] if level is None else [])
        + [("refresh", {"extent": False}), ("locked", ())]
    )


@pytest.mark.parametrize("multi", [False, True])
def test_reset_level_uses_actual_live_wrapper(pair, multi):
    for side in pair:
        data = ControlledMultiScale([1, 2, 3]) if multi else np.ones((2, 2))
        layer = _probe(side, "Scalar", _data=data, _locked_data_level=1)
        with _services(side, MultiScaleData=ControlledMultiScale):
            _call(side, "Scalar", "_reset_data_level", layer)
        assert layer._locked_data_level is None
        assert layer._data_level == (2 if multi else 0)


@pytest.mark.parametrize("case", ["same", "changed", "callback_error", "reentry"])
def test_editable_reset_and_fresh_event_after_callback(pair, case):
    def observe(side):
        trace = []
        layer = _probe(side, "Layer", _editable=case == "same")

        def changed():
            trace.append(("callback", layer._editable))
            if case == "callback_error":
                raise LookupError("callback")
            if case == "reentry":
                vars(layer)["_editable"] = False
                layer.events = SimpleNamespace(editable=_event(trace, "replacement"))

        layer._on_editable_changed = changed
        layer.events = SimpleNamespace(editable=_event(trace, "initial"))
        if case == "callback_error":
            with pytest.raises(LookupError):
                _call(side, "Layer", "_reset_editable", layer)
        else:
            _call(side, "Layer", "_reset_editable", layer)
        assert layer._editable is (case != "reentry")
        return trace

    old, new = map(observe, pair)
    assert new == old
    assert old == (
        []
        if case == "same"
        else [("callback", True)]
        + (
            []
            if case == "callback_error"
            else [("replacement" if case == "reentry" else "initial", ())]
        )
    )


@pytest.mark.parametrize("same", [False, True])
def test_scale_factor_equality_controls_publication(pair, same):
    for side in pair:
        trace = []
        layer = _probe(
            side,
            "Layer",
            _scale_factor=2,
            events=SimpleNamespace(scale_factor=_event(trace, "scale")),
        )
        _call(side, "Layer", "scale_factor:set", layer, 2 if same else 3)
        assert layer._scale_factor == (2 if same else 3)
        assert trace == ([] if same else [("scale", ())])


@pytest.mark.parametrize("case", ["same", "changed", "setter_error"])
def test_units_live_transform_comparison_and_refresh_order(pair, case):
    def observe(side):
        trace = []

        class Physical:
            value = ("old",)

            @property
            def units(self):
                trace.append("read")
                return self.value

            @units.setter
            def units(self, value):
                trace.append(("set", value))
                if case == "setter_error":
                    raise LookupError("units")
                self.value = tuple(value)

        transform = Physical()
        layer = _probe(
            side,
            "Layer",
            _transforms={"data2physical": transform},
            _clear_extent=_record(trace, "clear"),
            refresh=_record(trace, "refresh"),
            events=SimpleNamespace(units=_event(trace, "event")),
        )
        value = ["old"] if case == "same" else ["new"]
        if case == "setter_error":
            with pytest.raises(LookupError):
                _call(side, "Layer", "units:set", layer, value)
        else:
            _call(side, "Layer", "units:set", layer, value)
        return trace

    old, new = map(observe, pair)
    assert new == old
    expected = ["read", ("set", ["old"] if case == "same" else ["new"])]
    if case != "setter_error":
        expected += ["read"]
    if case == "changed":
        expected += [("clear", {}), ("refresh", {"extent": False}), ("event", ())]
    assert old == expected


@pytest.mark.parametrize("case", ["shrink", "grow", "same", "transform_error"])
def test_dimension_transition_transform_before_ndim_and_refresh(pair, case):
    def observe(side):
        trace = []
        new_ndim = (
            2 if case in ("shrink", "transform_error") else (4 if case == "grow" else 3)
        )

        class Transforms:
            def set_slice(self, axes):
                trace.append(("slice", tuple(axes)))
                if case == "transform_error":
                    raise LookupError("transform")
                return "sliced"

            def expand_dims(self, axes):
                trace.append(("expand", tuple(axes)))
                return "expanded"

        transform = Transforms()
        layer = _probe(
            side,
            "Layer",
            _ndim=3,
            _transforms=transform,
            _get_ndim=lambda: new_ndim,
            refresh=_record(trace, "refresh"),
        )

        def update():
            trace.append(("state", layer._ndim, layer._transforms))

        layer._slicing_state = SimpleNamespace(update_dims=update)
        if case == "transform_error":
            with pytest.raises(LookupError):
                _call(side, "Layer", "_update_dims", layer)
            assert layer._ndim == 3
            assert layer._transforms is transform
            return trace
        _call(side, "Layer", "_update_dims", layer)
        assert layer._ndim == new_ndim
        result = (
            "sliced"
            if case == "shrink"
            else "expanded"
            if case == "grow"
            else transform
        )
        assert layer._transforms is result
        return [
            tuple("original" if x is transform else x for x in item)
            if len(item) == 3
            else item
            for item in trace
        ]

    old, new = map(observe, pair)
    assert new == old
    branch = (
        [("slice", (1, 2))]
        if case in ("shrink", "transform_error")
        else ([("expand", (0,))] if case == "grow" else [])
    )
    assert old == branch + (
        []
        if case == "transform_error"
        else [
            (
                "state",
                2 if case == "shrink" else 4 if case == "grow" else 3,
                "sliced"
                if case == "shrink"
                else "expanded"
                if case == "grow"
                else "original",
            ),
            ("refresh", {}),
        ]
    )


@pytest.mark.parametrize("case", ["equal", "visible", "hidden", "force", "copy_error"])
def test_state_slice_publication_deepcopy_visibility_and_force(pair, case):
    def observe(side):
        trace = []
        previous = [1]
        incoming = previous if case in ("equal", "force") else [2]
        st = _probe(
            side,
            "State",
            _slice_input=previous,
            layer=SimpleNamespace(visible=case != "hidden"),
            set_view_slice=_record(trace, "view"),
        )

        def deepcopy(value):
            trace.append(("copy", value is incoming))
            if case == "copy_error":
                raise LookupError("copy")
            return copy.deepcopy(value)

        with _services(side, copy=SimpleNamespace(deepcopy=deepcopy)):
            if case == "copy_error":
                with pytest.raises(LookupError):
                    _call(side, "State", "set_slice_input", st, incoming, False)
                assert st._slice_input is previous
                return trace
            result = _call(
                side, "State", "set_slice_input", st, incoming, case == "force"
            )
        assert result is (case != "equal")
        if result:
            assert st._slice_input == incoming
            assert st._slice_input is not incoming
        else:
            assert st._slice_input is previous
        return trace

    old, new = map(observe, pair)
    assert new == old
    assert old == (
        []
        if case == "equal"
        else [("copy", True)] + ([("view", {})] if case == "visible" else [])
    )


def test_state_update_and_dims_units_publish_at_source_points(pair):
    def observe(side):
        trace = []
        new_slice = object()
        dims = SimpleNamespace(units=object())
        st = _probe(side, "State", layer=SimpleNamespace(ndim=4), _units="old")

        class Slice:
            def with_ndim(self, ndim):
                trace.append(("resize", ndim))
                return new_slice

        st._slice_input = Slice()
        _call(side, "State", "update_dims", st)
        assert st._slice_input is new_slice

        def make(value):
            assert value is dims
            assert st._units == "old"
            trace.append("make")
            return new_slice

        def install(value, force):
            assert value is new_slice
            assert st._units is dims.units
            trace.append(("install", force))
            return "exact-result"

        st.make_slice_input = make
        st.set_slice_input = install
        result = _call(side, "State", "set_slice_input_from_dims", st, dims, True)
        assert result == "exact-result"
        assert trace == [("resize", 4), "make", ("install", True)]

    for side in pair:
        observe(side)


@pytest.mark.parametrize("order", [None, (2, 1, 0, 3)])
def test_make_slice_input_real_order_mapping_and_model_aliases(pair, order):
    for side in pair:
        dims = SimpleNamespace(ndim=4, order=order, ndisplay=2, point=(1, 2, 3, 4))
        st = _probe(side, "State", layer=SimpleNamespace(ndim=2))
        with _services(
            side, _ThickNDSlice=ControlledThick, _SliceInput=ControlledInput
        ):
            result = _call(side, "State", "make_slice_input", st, dims)
        assert result.ndisplay == 2
        assert result.order == (0, 1)
        assert result.world_slice.point == (3, 4)


@pytest.mark.parametrize("fail", [False, True])
def test_slice_indices_fresh_units_service_before_slice_dispatch(pair, fail):
    def observe(side):
        trace = []
        transform = object()
        converted = object()
        dims = SimpleNamespace(units=object())
        st = _probe(
            side,
            "State",
            layer=SimpleNamespace(_data_to_world=SimpleNamespace(inverse=transform)),
        )

        def apply(value, units):
            assert value is transform and units is dims.units
            trace.append("units")
            if fail:
                raise LookupError("units")
            return converted

        def data_slice(value):
            assert value is converted
            trace.append("slice")
            return converted

        with _services(side, apply_units_to_transform=apply):
            if fail:
                with pytest.raises(LookupError):
                    _call(
                        side,
                        "State",
                        "_slice_indices",
                        st,
                        SimpleNamespace(data_slice=data_slice),
                        dims,
                    )
            else:
                assert (
                    _call(
                        side,
                        "State",
                        "_slice_indices",
                        st,
                        SimpleNamespace(data_slice=data_slice),
                        dims,
                    )
                    is converted
                )
        return trace

    old, new = map(observe, pair)
    assert new == old == (["units"] if fail else ["units", "slice"])


@pytest.mark.parametrize("case", ["unchanged", "changed", "force"])
def test_slice_dims_logger_dispatch_and_refresh_flags(pair, case):
    def observe(side):
        trace = []
        dims = object()

        def set_from(value, force):
            assert value is dims
            trace.append(("set", force))
            return case == "changed"

        layer = _probe(
            side,
            "Layer",
            _slicing_state=SimpleNamespace(set_slice_input_from_dims=set_from),
            _refresh_sync=_record(trace, "refresh"),
        )
        with _services(
            side, logger=SimpleNamespace(debug=lambda *a: trace.append("log"))
        ):
            _call(side, "Layer", "_slice_dims", layer, dims, case == "force")
        return trace

    old, new = map(observe, pair)
    assert new == old
    assert old == ["log", ("set", case == "force")] + (
        []
        if case == "unchanged"
        else [
            (
                "refresh",
                {
                    "data_displayed": True,
                    "thumbnail": True,
                    "highlight": True,
                    "extent": True,
                },
            )
        ]
    )


@pytest.mark.parametrize("fail", [False, True])
def test_draw_updates_scale_before_transform_and_viewport_callback(pair, fail):
    def observe(side):
        trace = []
        axes = (1, 2)

        class Transform:
            @property
            def simplified(self):
                trace.append("simplified")
                return self

            def __getitem__(self, item):
                assert item == slice(1, None)
                trace.append("skip-tile")
                return self

            def set_slice(self, displayed):
                assert displayed is axes
                trace.append("set-slice")
                if fail:
                    raise LookupError("transform")
                return self

            def inverse(self, corners):
                trace.append(("inverse", tuple(corners)))
                return np.asarray(corners) * (2, 3) + (-0.25, 0.5)

        layer = _probe(
            side,
            "Layer",
            _scale_factor=1,
            _transforms=Transform(),
            _slicing_state=SimpleNamespace(
                _slice_input=SimpleNamespace(displayed=axes)
            ),
            events=SimpleNamespace(scale_factor=_event(trace, "scale")),
        )

        def update(bbox, threshold, displayed):
            assert displayed is axes
            assert threshold is limits
            assert layer._scale_factor == 4
            trace.append(("viewport", bbox.tolist()))

        layer._update_level_and_corners = update
        limits = (100, 200)
        if fail:
            with pytest.raises(LookupError):
                _call(
                    side,
                    "Layer",
                    "_update_draw",
                    layer,
                    4,
                    np.array([[1, 2], [4, 6]]),
                    limits,
                )
        else:
            _call(
                side,
                "Layer",
                "_update_draw",
                layer,
                4,
                np.array([[1, 2], [4, 6]]),
                limits,
            )
        return trace

    old, new = map(observe, pair)
    assert new == old
    assert old[:4] == [("scale", ()), "skip-tile", "simplified", "set-slice"]
    if fail:
        assert len(old) == 4
    else:
        assert old[-1] == ("viewport", [[1, 6], [8, 19]])


@pytest.mark.parametrize("nan_extent", [False, True])
def test_base_viewport_clips_or_preserves_empty_axes(pair, nan_extent):
    for side in pair:
        extent = (
            np.full((2, 3), np.nan) if nan_extent else np.array([[0, 1, 2], [4, 6, 8]])
        )
        layer = _probe(side, "Layer", _ndim=3, extent=SimpleNamespace(data=extent))
        _call(
            side,
            "Layer",
            "_update_level_and_corners",
            layer,
            np.array([[-10, 3], [20, 12]]),
            (10, 10),
            (1, 2),
        )
        np.testing.assert_array_equal(
            layer.corner_pixels,
            np.zeros((2, 3), dtype=int) if nan_extent else [[0, 1, 3], [0, 6, 8]],
        )


@pytest.mark.parametrize(
    "case",
    [
        "single",
        "locked",
        "locked_same",
        "auto",
        "auto_inside",
        "auto_zero",
        "three",
    ],
)
def test_scalar_viewport_source_decisions_and_alias_publication(pair, case):
    def observe(side):
        trace = []
        shapes = ((5, 16, 20), (5, 8, 10), (5, 4, 5))
        data = [SimpleNamespace(shape=s) for s in shapes]
        current = np.array([[0, 0, 0], [0, 7, 9]])
        layer = _probe(
            side,
            "Scalar",
            _ndim=3,
            multiscale=case != "single",
            _locked_data_level=1 if case.startswith("locked") else None,
            _data_level=1 if case == "locked_same" else 0,
            _data=data,
            corner_pixels=current,
            level_shapes=np.asarray(shapes),
            downsample_factors=np.array([[1, 1, 1], [1, 2, 2], [1, 4, 4]]),
            _slicing_state=SimpleNamespace(
                _slice_input=SimpleNamespace(ndisplay=3 if case == "three" else 2)
            ),
            extent=SimpleNamespace(data=np.array([[0, 0, 0], [4, 15, 19]])),
            refresh=_record(trace, "refresh"),
        )
        scaled = np.array([[1, 2], [5, 7]])
        if case == "auto_inside":
            layer._data_level = 1
        if case == "auto_zero":
            scaled[1, 0] = scaled[0, 0]
        expanded = np.array([[0, 0, 0], [0, 7, 9]])
        if case == "auto":
            expanded[0, 1] = -1

        def choose(bbox, threshold, factors):
            trace.append("choose")
            np.testing.assert_array_equal(factors, [[1, 1], [2, 2], [4, 4]])
            return 1, scaled

        def expand(corners, chosen_data, axes):
            assert chosen_data is data[1]
            trace.append("expand")
            return expanded

        with _services(
            side,
            compute_multiscale_level_and_corners=choose,
            expand_corners_to_chunk_boundaries=expand,
        ):
            _call(
                side,
                "Scalar",
                "_update_level_and_corners",
                layer,
                np.array([[1, 2], [10, 12]]),
                (4, 4),
                (1, 2),
            )
        if case in ("auto_inside", "auto_zero"):
            assert layer.corner_pixels is current
        elif case == "auto":
            assert layer.corner_pixels is expanded
            assert layer._data_level == 1
        else:
            expected = (
                [[0, 1, 2], [0, 10, 12]]
                if case == "single"
                else [[0, 0, 0], [0, 3, 4]]
                if case == "three"
                else [[0, 0, 0], [0, 7, 9]]
            )
            np.testing.assert_array_equal(layer.corner_pixels, expected)
        return trace

    old, new = map(observe, pair)
    assert new == old
    prefix = (
        ["choose", "expand"]
        if case in ("auto", "auto_inside")
        else (["choose"] if case == "auto_zero" else [])
    )
    assert old == prefix + (
        [("refresh", {"extent": False, "thumbnail": False})]
        if case in ("locked", "auto", "three")
        else []
    )


@pytest.mark.parametrize("case", ["hidden", "visible", "unchanged"])
def test_scalar_state_hidden_placeholder_without_materialization(pair, case):
    def observe(side):
        trace = []
        old = SimpleNamespace(ndisplay=2)
        incoming = old if case == "unchanged" else SimpleNamespace(ndisplay=3)
        layer = SimpleNamespace(
            visible=case == "visible",
            ndim=3,
            data=SimpleNamespace(shape=(2, 3, 4, 3)),
            _slice_dtype=lambda: np.dtype("uint8"),
        )
        st = _probe(
            side,
            "ScalarState",
            layer=layer,
            _slice_input=old,
            _slice="old-response",
            set_view_slice=_record(trace, "view"),
        )
        response = object()

        def empty(**kwargs):
            trace.append(
                (
                    "empty",
                    kwargs["rgb"],
                    kwargs["dtype"],
                    kwargs["slice_input"].ndisplay,
                )
            )
            assert kwargs["slice_input"] is st._slice_input
            return response

        with _services(
            side, _ScalarFieldSliceResponse=SimpleNamespace(make_empty=empty)
        ):
            changed = _call(side, "ScalarState", "set_slice_input", st, incoming, False)
        assert changed is (case != "unchanged")
        assert st._slice is (response if case == "hidden" else "old-response")
        return trace

    old, new = map(observe, pair)
    assert new == old
    assert old == (
        [("empty", True, np.dtype("uint8"), 3)]
        if case == "hidden"
        else [("view", {})]
        if case == "visible"
        else []
    )


@pytest.mark.parametrize("fail", [False, True])
def test_make_request_slice_indices_and_dask_provider_order(pair, fail):
    def observe(side):
        trace = []
        dims = object()
        slice_input, data_slice, indexer, request = (object() for _ in range(4))
        st = _probe(side, "ScalarState", dask_optimized_slicing=indexer)

        def make(value):
            assert value is dims
            trace.append("make")
            return slice_input

        def indices(value, value_dims):
            assert value is slice_input and value_dims is dims
            trace.append("indices")
            if fail:
                raise LookupError("indices")
            return data_slice

        def internal(**kwargs):
            assert kwargs == {
                "slice_input": slice_input,
                "data_slice": data_slice,
                "dask_indexer": indexer,
            }
            trace.append("request")
            return request

        st.make_slice_input, st._slice_indices = make, indices
        st._make_slice_request_internal = internal
        if fail:
            with pytest.raises(LookupError):
                _call(side, "ScalarState", "_make_slice_request", st, dims)
        else:
            assert (
                _call(side, "ScalarState", "_make_slice_request", st, dims) is request
            )
        assert trace == (
            ["make", "indices"] if fail else ["make", "indices", "request"]
        )

    for side in pair:
        observe(side)


@pytest.mark.parametrize("fail", [False, True])
def test_request_source_locals_release_after_return_or_exception(pair, fail):
    def observe(side):
        refs = []
        trace = []

        class Input:
            pass

        def make(dims):
            value = Input()
            refs.append(weakref.ref(value))
            trace.append("make")
            return value

        def indices(value, dims):
            assert refs[0]() is value
            trace.append("indices")
            if fail:
                raise LookupError("indices")
            return Input()

        def internal(**kwargs):
            assert refs[0]() is kwargs["slice_input"]
            trace.append("internal")
            return None

        def run():
            st = _probe(
                side,
                "ScalarState",
                make_slice_input=make,
                _slice_indices=indices,
                _make_slice_request_internal=internal,
                dask_optimized_slicing=object(),
            )
            try:
                _call(side, "ScalarState", "_make_slice_request", st, object())
            except LookupError:
                if not fail:
                    raise

        run()
        gc.collect()
        assert refs[0]() is None
        return trace

    old, new = map(observe, pair)
    assert (
        new == old == (["make", "indices"] if fail else ["make", "indices", "internal"])
    )


@pytest.mark.parametrize("case", ["fits", "inactive_axis", "highest", "none"])
def test_level_selection_real_numpy_active_dimensions(pair, case):
    factors = np.array([[1, 1], [1, 2], [1, 4], [1, 8]])
    shape = np.array([1000, 8]) if case == "inactive_axis" else np.array([4, 64])
    threshold = np.array([10, 10])
    if case == "fits":
        shape = np.array([4, 8])
    if case == "none":
        factors = np.ones((4, 2))
    expected = 2 if case == "highest" else 0
    results = [
        side.Utils.compute_multiscale_level(shape, threshold, factors) for side in pair
    ]
    assert results == [expected, expected]


def test_level_and_corners_fresh_owned_helper_and_rounding(pair):
    def observe(side):
        trace = []
        corners = np.array([[1, 3], [12, 17]])
        factors = np.array([[1, 1], [2, 4]])

        def choose(shape, threshold, downsamples):
            np.testing.assert_array_equal(shape, [11, 14])
            assert downsamples is factors
            trace.append("choose")
            return 1

        with _services(side, compute_multiscale_level=choose):
            level, result = side.Utils.compute_multiscale_level_and_corners(
                corners, (2, 2), factors
            )
        assert level == 1 and trace == ["choose"]
        np.testing.assert_array_equal(result, [[0, 0], [6, 5]])
        np.testing.assert_array_equal(corners, [[1, 3], [12, 17]])

    for side in pair:
        observe(side)


@pytest.mark.parametrize(
    "case",
    ["regular", "explicit", "bad_sum", "nonpositive", "type_error", "index_error"],
)
def test_chunk_boundary_index_protocol_and_source_error_filter(pair, case):
    def observe(side):
        trace = []

        class Index:
            def __init__(self, value):
                self.value = value

            def __index__(self):
                trace.append(self.value)
                if case == "type_error":
                    raise TypeError("index")
                if case == "index_error":
                    raise IndexError("index")
                return self.value

        chunks = (
            Index(4)
            if case in ("regular", "type_error", "index_error")
            else (
                [Index(4), Index(4), Index(2)]
                if case == "explicit"
                else [Index(4), Index(4)]
                if case == "bad_sum"
                else [Index(4), Index(0), Index(6)]
            )
        )
        if case == "index_error":
            with pytest.raises(IndexError, match="index"):
                side.Utils._chunk_boundaries(chunks, 10)
            return
        result = side.Utils._chunk_boundaries(chunks, 10)
        if case in ("bad_sum", "nonpositive", "type_error"):
            assert result is None
        else:
            np.testing.assert_array_equal(result, [0, 4, 8, 10])
        assert trace == (
            [4]
            if case in ("regular", "type_error")
            else [4, 4, 2]
            if case == "explicit"
            else [4, 4]
            if case == "bad_sum"
            else [4, 0, 6]
        )

    for side in pair:
        observe(side)


@pytest.mark.parametrize(
    "case", ["direct", "missing", "layout", "translated", "origin_error"]
)
def test_chunk_metadata_direct_alias_and_lazy_origin_short_circuit(pair, case):
    def observe(side):
        trace = []
        direct = (4, 5)
        shape = (8, 16)

        class Origin(Sequence):
            def __len__(self):
                return 3

            def __getitem__(self, index):
                trace.append(("origin", index))
                if index == 0:
                    return 3 if case == "translated" else 0
                if index == 1:
                    if case == "origin_error":
                        raise LookupError("origin")
                    return None
                if index == 2:
                    return 0
                raise IndexError

        class Data:
            @property
            def chunks(self):
                trace.append("chunks")
                return direct if case == "direct" else None

            @property
            def chunk_layout(self):
                trace.append("layout")
                if case == "missing":
                    return None
                return SimpleNamespace(
                    read_chunk=SimpleNamespace(shape=shape), grid_origin=Origin()
                )

        if case == "origin_error":
            with pytest.raises(LookupError, match="origin"):
                side.Utils._chunks_metadata(Data())
        else:
            result = side.Utils._chunks_metadata(Data())
            assert result is (
                direct if case == "direct" else shape if case == "layout" else None
            )
        return trace

    old, new = map(observe, pair)
    assert new == old
    assert old == (
        ["chunks"]
        if case == "direct"
        else ["chunks", "layout"]
        if case == "missing"
        else ["chunks", "layout", ("origin", 0)]
        if case == "translated"
        else ["chunks", "layout", ("origin", 0), ("origin", 1)]
        if case == "origin_error"
        else [
            "chunks",
            "layout",
            ("origin", 0),
            ("origin", 1),
            ("origin", 2),
            ("origin", 3),
        ]
    )


@pytest.mark.parametrize(
    "case",
    ["regular", "explicit", "missing", "mismatch", "axis_error", "partial_invalid"],
)
def test_chunk_expansion_inclusive_bounds_original_alias_on_failure(pair, case):
    for side in pair:
        original = np.array([[2, 5, 1], [7, 12, 2]])
        chunks = ((4, 4, 2), (5, 5, 5), None) if case == "explicit" else (4, 5, None)
        if case == "missing":
            chunks = None
        if case == "mismatch":
            chunks = (4,)
        if case == "partial_invalid":
            chunks = (4, 0, None)
        data = SimpleNamespace(chunks=chunks, shape=(10, 15, 3))
        axes = (0, 5) if case == "axis_error" else (0, 1)
        result = side.Utils.expand_corners_to_chunk_boundaries(original, data, axes)
        if case not in ("regular", "explicit"):
            assert result is original
        else:
            assert result is not original
            np.testing.assert_array_equal(result, [[0, 5, 1], [7, 14, 2]])
        np.testing.assert_array_equal(original, [[2, 5, 1], [7, 12, 2]])


def test_chunk_exception_tuple_rejects_nested_provider_class(pair):
    for side in pair:
        corners = np.array([[1, 2], [3, 4]])
        data = SimpleNamespace(chunks=(4, 5), shape=(10, 15))
        with _services(side, TypeError=(TypeError,)):
            with pytest.raises(
                TypeError,
                match="catching classes that do not inherit from BaseException",
            ) as failure:
                side.Utils.expand_corners_to_chunk_boundaries(
                    corners, data, (5,)
                )
        assert isinstance(failure.value.__context__, IndexError)
        np.testing.assert_array_equal(corners, [[1, 2], [3, 4]])


@pytest.mark.parametrize("fail", [False, True])
def test_viewport_unpack_source_lease_and_release_before_publication(pair, fail):
    def observe(side):
        trace = []
        refs = []
        values = (1, np.array([[1, 2], [5, 7]]))

        class WeakIterator:
            def __init__(self, source, payload):
                self.source = source
                self.payload = payload
                self.index = 0

            def __iter__(self):
                return self

            def __next__(self):
                alive = self.source() is not None
                trace.append(("next", self.index, alive))
                if not alive:
                    raise RuntimeError("unpack source released early")
                if fail and self.index == 1:
                    raise LookupError("second value")
                if self.index == 2:
                    raise StopIteration
                value = self.payload[self.index]
                self.index += 1
                return value

        class PairSource:
            def __iter__(self):
                return WeakIterator(weakref.ref(self), values)

            def __del__(self):
                trace.append("release")

        def choose(*args):
            value = PairSource()
            refs.append(weakref.ref(value))
            return value

        def zeros(*args, **kwargs):
            trace.append(("zeros", refs[0]() is not None))
            return np.zeros(*args, **kwargs)

        layer = _probe(
            side,
            "Scalar",
            _ndim=3,
            multiscale=True,
            _locked_data_level=None,
            _data_level=0,
            _data=[SimpleNamespace(shape=(5, 16, 20)),
                   SimpleNamespace(shape=(5, 8, 10))],
            corner_pixels=np.zeros((2, 3), dtype=int),
            downsample_factors=np.array([[1, 1, 1], [1, 2, 2]]),
            _slicing_state=SimpleNamespace(
                _slice_input=SimpleNamespace(ndisplay=2)
            ),
            refresh=lambda **kwargs: None,
        )
        numpy_service = SimpleNamespace(
            zeros=zeros, take=np.take, clip=np.clip, any=np.any
        )
        with _services(
            side,
            np=numpy_service,
            compute_multiscale_level_and_corners=choose,
            expand_corners_to_chunk_boundaries=lambda corners, data, axes: corners,
        ):
            if fail:
                with pytest.raises(LookupError, match="second value"):
                    _call(
                        side, "Scalar", "_update_level_and_corners", layer,
                        np.array([[1, 2], [10, 12]]), (4, 4), [1, 2],
                    )
                assert layer._data_level == 0
            else:
                _call(
                    side, "Scalar", "_update_level_and_corners", layer,
                    np.array([[1, 2], [10, 12]]), (4, 4), [1, 2],
                )
                assert layer._data_level == 1
        assert refs[0]() is None
        return trace

    old, new = map(observe, pair)
    assert new == old
    expected = [("next", 0, True), ("next", 1, True)]
    if not fail:
        expected.append(("next", 2, True))
    expected.append("release")
    if not fail:
        expected.append(("zeros", False))
    assert old == expected


def test_chunk_expansion_finalizable_source_locals_release_in_original_order(pair):
    def observe(side):
        trace = []
        refs = {}

        class Chunks(list):
            def __del__(self):
                trace.append("chunks")

        class Boundaries(list):
            def __del__(self):
                trace.append("boundaries")

        def metadata(data):
            value = Chunks([4, 5])
            refs["chunks"] = weakref.ref(value)
            return value

        def boundaries(chunk_size, axis_size):
            assert (chunk_size, axis_size) == (4, 10)
            value = Boundaries([0, 4, 8, 10])
            refs["boundaries"] = weakref.ref(value)
            return value

        original = np.array([[2, 5], [7, 12]])
        data = SimpleNamespace(shape=(10, 15))
        with _services(
            side, _chunks_metadata=metadata, _chunk_boundaries=boundaries
        ):
            result = side.Utils.expand_corners_to_chunk_boundaries(
                original, data, [0]
            )
        assert result is not original
        np.testing.assert_array_equal(result, [[0, 5], [7, 12]])
        np.testing.assert_array_equal(original, [[2, 5], [7, 12]])
        assert refs["chunks"]() is None
        assert refs["boundaries"]() is None
        return trace

    old, new = map(observe, pair)
    assert new == old
    assert old == ["chunks", "boundaries"]
