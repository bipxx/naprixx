"""Finite twelve-body scalar/Image/Labels request-executor comparisons.

The original oracle reconstructs complete pinned classes in one test-only module;
no original application namespace is mounted. Current requests are the genuinely
imported application classes using the accepted canonical native dictionary and
29-field constructors. NumPy, Dask, enum metadata, Affine, normalize_dtype and
reorder_after_dim_reduction are declared provider/accepted-helper boundaries.
Controlled callbacks expose source ordering, aliases, failure publication and the
real with protocol; they do not replace the original owned request algorithms.

Baseline may expose the named existing callback/order/context and typed-kernel
contracts. All assertions remain repair targets; no skips, xfails or production
fallbacks. These finite cases do not prove general ArrayLike/provider, private
frame/code identity, plugin/platform, shutdown or complete workflow compatibility.
"""

from __future__ import annotations

import ast
import contextlib
import copy
import dataclasses
import gc
import hashlib
import importlib
import inspect
import sys
import weakref
from itertools import count
from pathlib import Path
from types import ModuleType, SimpleNamespace
from typing import Any, Generic, TypeVar

import numpy as np
import numpy.typing as npt
import pytest

ROOT = Path(__file__).resolve().parents[1]
PINNED = ROOT / ".upstream/napari/src/napari"
CURRENT = ROOT / "src/napari"
BEFORE = ROOT / ".artifacts/scalar-slice-executor-before/files/src/napari"
UTILS = "layers/utils/_slice_input.py"
SCALAR = "layers/_scalar_field/_slice.py"
IMAGE = "layers/image/_slice.py"
LABELS = "layers/labels/_slice.py"
IDS = "layers/base/_slice.py"
PINNED_HASHES = {
    UTILS: "a79eb82a741085e7c2a2e9f54710706fd812c7f3ba2d90c7a83cd795d700f7d4",
    SCALAR: "0e327552ef2d11eaa198f84e86cac2ddbdefd5fa2b10947c42c45956b1c61188",
    IMAGE: "f8b0e977572f7e769bdff90d04619d2f674ea7f091e8576a93d84b38da7fe968",
    LABELS: "f54233af8bca923043f2a2c97e9b6a691d162c5e0c23dc3cb8c43dbcf667b707",
    IDS: "606b4e936922e0bb2c1bb6b3eed74a7d819db5b952cb3a954ff16395b8f80cfd",
}
BEFORE_HASHES = {
    SCALAR: "85396d1359c52b0fb7f2bf2911475a8a446bea20f163daf9b180f346944003d9",
    IMAGE: "6c88164476da3f462071de48b2fa965e840b77f09ab454967499acbcf71d6cc4",
    LABELS: PINNED_HASHES[LABELS],
}
OWNED = {
    (SCALAR, "_ScalarFieldSliceRequest"): (
        "__call__",
        "_call_single_scale",
        "_call_multi_scale",
        "_thick_slice_at_level",
        "_project_thick_slice",
        "_project_slice",
        "_get_order",
        "_slice_out_of_bounds",
        "_point_to_slices",
        "_data_slice_to_slices",
    ),
    (IMAGE, "_ImageSliceRequest"): ("_project_slice",),
    (LABELS, "_LabelsSliceRequest"): ("_project_slice",),
}


def _source(root, relative):
    raw = (root / relative).read_bytes()
    hashes = (
        PINNED_HASHES if root == PINNED else BEFORE_HASHES if root == BEFORE else {}
    )
    if relative in hashes:
        assert hashlib.sha256(raw).hexdigest() == hashes[relative]
    return raw.decode("utf-8")


def _class(source, name):
    return next(
        node
        for node in ast.parse(source).body
        if isinstance(node, ast.ClassDef) and node.name == name
    )


def _method(source, owner, name):
    return next(
        node
        for node in _class(source, owner).body
        if isinstance(node, ast.FunctionDef) and node.name == name
    )


def _execute(source, nodes, namespace, filename):
    futures = [
        node
        for node in ast.parse(source).body
        if isinstance(node, ast.ImportFrom) and node.module == "__future__"
    ]
    tree = ast.fix_missing_locations(
        ast.Module(body=copy.deepcopy(futures + nodes), type_ignores=[])
    )
    exec(compile(tree, str(filename), "exec", dont_inherit=True), namespace)


@pytest.fixture(scope="module")
def implementations():
    from napari.utils._dtype import normalize_dtype
    from napari.utils.misc import reorder_after_dim_reduction
    from napari.utils.transforms import Affine
    from naparixx import _native

    modules = {
        rel: importlib.import_module("napari." + rel[:-3].replace("/", "."))
        for rel in (UTILS, SCALAR, IMAGE, LABELS)
    }
    sources = {rel: _source(PINNED, rel) for rel in PINNED_HASHES}
    changes = 0
    for (rel, owner), names in OWNED.items():
        before, current = _source(BEFORE, rel), _source(CURRENT, rel)
        for name in names:
            changes += ast.dump(_method(before, owner, name)) != ast.dump(
                _method(current, owner, name)
            )
    assert changes in (0, 12), "All twelve owned bodies must move together"

    name = "_naparixx_scalar_executor_pinned_oracle"
    assert name not in sys.modules
    original_module = ModuleType(name)
    ns = vars(original_module)
    ns.update(
        dataclass=dataclasses.dataclass,
        field=dataclasses.field,
        Generic=Generic,
        TypeVar=TypeVar,
        Any=Any,
        np=np,
        npt=npt,
        count=count,
        Affine=Affine,
        normalize_dtype=normalize_dtype,
        reorder_after_dim_reduction=reorder_after_dim_reduction,
        ArrayLike=modules[IMAGE].ArrayLike,
        ImageProjectionMode=modules[IMAGE].ImageProjectionMode,
        BaseProjectionMode=modules[LABELS].BaseProjectionMode,
    )
    sys.modules[name] = original_module
    try:
        nodes = [
            node
            for node in ast.parse(sources[IDS]).body
            if isinstance(node, (ast.Assign, ast.FunctionDef))
        ]
        _execute(sources[IDS], nodes, ns, PINNED / IDS)
        nodes = [
            node
            for node in ast.parse(sources[UTILS]).body
            if isinstance(node, ast.Assign)
            and any(isinstance(t, ast.Name) and t.id == "_T" for t in node.targets)
        ]
        _execute(sources[UTILS], nodes, ns, PINNED / UTILS)
        for rel, owners in (
            (UTILS, ("_ThickNDSlice", "_SliceInput")),
            (
                SCALAR,
                (
                    "_ScalarFieldView",
                    "_ScalarFieldSliceResponse",
                    "_ScalarFieldSliceRequest",
                ),
            ),
            (IMAGE, ("_ImageSliceRequest",)),
            (LABELS, ("_LabelsSliceRequest",)),
        ):
            for owner in owners:
                _execute(sources[rel], [_class(sources[rel], owner)], ns, PINNED / rel)
        classes = (
            "_ThickNDSlice",
            "_SliceInput",
            "_ScalarFieldView",
            "_ScalarFieldSliceResponse",
            "_ScalarFieldSliceRequest",
            "_ImageSliceRequest",
            "_LabelsSliceRequest",
        )
        original = SimpleNamespace(
            classes={key: ns[key] for key in classes},
            namespace=ns,
            original=True,
        )
        current = SimpleNamespace(
            classes={
                key: getattr(
                    modules[
                        UTILS
                        if key in classes[:2]
                        else IMAGE
                        if key == "_ImageSliceRequest"
                        else LABELS
                        if key == "_LabelsSliceRequest"
                        else SCALAR
                    ],
                    key,
                )
                for key in classes
            },
            namespace=vars(modules[SCALAR]),
            original=False,
        )
        yield SimpleNamespace(
            original=original,
            current=current,
            pairs=(original, current),
            sources=sources,
            modules=modules,
            native=_native,
            Affine=Affine,
            ImageProjectionMode=modules[IMAGE].ImageProjectionMode,
        )
    finally:
        del sys.modules[name]


def _request(
    side,
    *,
    kind="image",
    data=None,
    ndim=3,
    ndisplay=2,
    order=None,
    rgb=False,
    point=None,
    left=None,
    right=None,
    projection="none",
    multiscale=False,
    thumbnail=None,
    factors=None,
    shapes=None,
    data_level=0,
    thumbnail_level=0,
    corners=None,
    indexer=contextlib.nullcontext,
    cls=None,
):
    if data is None:
        data = np.arange(60, dtype=np.float32).reshape(3, 4, 5)
    if order is None:
        order = tuple(range(ndim))
    if point is None:
        point = (1.2,) + (np.nan,) * (ndim - 1)
    if left is None:
        left = (0.0,) + (np.nan,) * (ndim - 1)
    if right is None:
        right = left
    thick = side.classes["_ThickNDSlice"](point, left, right)
    input_ = side.classes["_SliceInput"](ndisplay, thick, order)
    if shapes is None:
        shapes = np.asarray([data.shape[:ndim]])
    if factors is None:
        factors = np.ones_like(shapes, dtype=float)
    if corners is None:
        corners = np.stack((np.zeros(ndim, dtype=int), shapes[data_level] - 1))
    if cls is None:
        cls = side.classes[
            {
                "scalar": "_ScalarFieldSliceRequest",
                "image": "_ImageSliceRequest",
                "labels": "_LabelsSliceRequest",
            }[kind]
        ]
    return cls(
        slice_input=input_,
        data_at_data_level=data,
        data_at_thumbnail_level=data if thumbnail is None else thumbnail,
        dtype=data.dtype,
        dask_indexer=indexer,
        data_slice=thick,
        projection_mode=projection,
        multiscale=multiscale,
        corner_pixels=corners,
        rgb=rgb,
        data_level=data_level,
        thumbnail_level=thumbnail_level,
        level_shapes=shapes,
        downsample_factors=factors,
        id=17,
    )


def _response_state(response):
    return (
        np.asarray(response.image.raw),
        np.asarray(response.image.view),
        np.asarray(response.thumbnail.raw),
        np.asarray(response.thumbnail.view),
        response.image.raw is response.image.view,
        response.thumbnail.raw is response.thumbnail.view,
        response.image is response.thumbnail,
        np.asarray(response.tile_to_data.linear_matrix),
        np.asarray(response.tile_to_data.translate),
        response.request_id,
        response.empty,
    )


def _assert_same(left, right):
    if isinstance(left, np.ndarray):
        np.testing.assert_array_equal(left, right)
        assert left.dtype == right.dtype
    elif isinstance(left, (tuple, list)):
        assert len(left) == len(right)
        for a, b in zip(left, right, strict=True):
            _assert_same(a, b)
    else:
        assert left == right


def _selection(value):
    return tuple(
        (v.start, v.stop, v.step) if isinstance(v, slice) else v for v in value
    )


class ProbeError(Exception):
    pass


def test_owned_metadata_is_preserved(implementations):
    for (rel, owner), names in OWNED.items():
        old = implementations.original.classes[owner]
        new = implementations.current.classes[owner]
        current = _source(CURRENT, rel)
        for name in names:
            old_node = _method(implementations.sources[rel], owner, name)
            new_node = _method(current, owner, name)
            assert ast.dump(old_node.args) == ast.dump(new_node.args)
            assert [ast.dump(d) for d in old_node.decorator_list] == [
                ast.dump(d) for d in new_node.decorator_list
            ]
            assert ast.get_docstring(old_node) == ast.get_docstring(new_node)
            assert str(inspect.signature(getattr(old, name))) == str(
                inspect.signature(getattr(new, name))
            )


@pytest.mark.parametrize("kind", ("scalar", "image", "labels"))
def test_actual_requests_reuse_canonical_native_field_storage(implementations, kind):
    for side in implementations.pairs:
        req = _request(side, kind=kind)
        saved = vars(req)
        assert saved is req.__dict__
        assert len(dataclasses.fields(req)) == 15
        assert req.data_at_data_level is saved["data_at_data_level"]
        assert req.slice_input is saved["slice_input"]
        saved["id"] = 19
        assert req.id == 19
        if not side.original:
            assert isinstance(req, implementations.native.SliceDataclassNamespaceBase)
            assert isinstance(
                req.slice_input, implementations.native.SliceDataclassNamespaceBase
            )
        with pytest.raises(dataclasses.FrozenInstanceError):
            req.id = 20


@pytest.mark.parametrize(
    "choice",
    (
        "image_point",
        "image_rgb",
        "labels_point",
        "sum",
        "mean",
        "max",
        "min",
        "dask_point",
    ),
)
def test_single_scale_real_numerics_and_view_aliases(implementations, choice):
    results = []
    for side in implementations.pairs:
        rgb = choice == "image_rgb"
        shape = (3, 4, 5, 3) if rgb else (3, 4, 5)
        data = np.arange(np.prod(shape), dtype=np.float32).reshape(shape)
        if choice == "labels_point":
            data = data.astype(np.int32)
        if choice == "dask_point":
            import dask.array as da

            data = da.from_array(data, chunks=(1, 4, 5))
        projecting = choice in {"sum", "mean", "max", "min"}
        mode = (
            getattr(implementations.ImageProjectionMode, choice.upper())
            if projecting
            else "none"
        )
        req = _request(
            side,
            kind="labels" if choice == "labels_point" else "image",
            data=data,
            rgb=rgb,
            order=(0, 2, 1),
            projection=mode,
            left=(1.0, np.nan, np.nan) if projecting else None,
            right=(1.0, np.nan, np.nan) if projecting else None,
        )
        response = req()
        assert response.slice_input is req.slice_input
        results.append(_response_state(response))
    _assert_same(*results)


@pytest.mark.parametrize("choice", ("image_2d", "image_3d", "rgb_2d", "labels_2d"))
def test_multiscale_real_tiles_thumbnail_and_transform(implementations, choice):
    results = []
    for side in implementations.pairs:
        rgb = choice == "rgb_2d"
        channels = (3,) if rgb else ()
        fine = np.arange(np.prod((6, 8, 10) + channels), dtype=np.float32).reshape(
            (6, 8, 10) + channels
        )
        coarse = fine[::2, ::2, ::2]
        if choice == "labels_2d":
            fine, coarse = fine.astype(np.int32), coarse.astype(np.int32)
        three = choice == "image_3d"
        req = _request(
            side,
            kind="labels" if choice == "labels_2d" else "image",
            data=coarse,
            thumbnail=fine,
            rgb=rgb,
            multiscale=True,
            ndisplay=3 if three else 2,
            order=(0, 2, 1),
            point=(np.nan,) * 3 if three else (2.0, np.nan, np.nan),
            left=(np.nan,) * 3 if three else (0.0, np.nan, np.nan),
            factors=np.asarray([[1, 1, 1], [2, 2, 2]], dtype=float),
            shapes=np.asarray([[6, 8, 10], [3, 4, 5]]),
            data_level=1,
            corners=np.asarray([[0, 1, 1], [0, 2, 3]]),
        )
        response = req()
        assert response.slice_input is req.slice_input
        results.append(_response_state(response))
    _assert_same(*results)


@pytest.mark.parametrize("choice", ("singleton", "negative_singleton", "unsupported"))
def test_image_projection_singleton_bypass_and_source_axis_membership(
    implementations,
    choice,
):
    results = []
    for side in implementations.pairs:
        shape = (1, 4, 5) if choice == "singleton" else (2, 3, 1)
        if choice == "unsupported":
            shape = (2, 3, 4)
        data = np.arange(np.prod(shape)).reshape(shape)
        axis = (-1,) if choice == "negative_singleton" else (0,)
        callback = side.classes["_ImageSliceRequest"]._project_slice
        if choice == "unsupported":
            with pytest.raises(NotImplementedError, match="unimplemented projection"):
                callback(data, axis, "unsupported")
        else:
            result = callback(data, axis, "unsupported")
            assert np.shares_memory(result, data)
            results.append(result)
    if results:
        _assert_same(*results)


@pytest.mark.parametrize("owner", ("_ScalarFieldSliceRequest", "_LabelsSliceRequest"))
def test_abstract_projection_remains_unimplemented(implementations, owner):
    for side in implementations.pairs:
        with pytest.raises(NotImplementedError):
            side.classes[owner]._project_slice(np.ones((2, 3)), (0,), "max")


@pytest.mark.parametrize("choice", ("permuted_2d", "rgb_2d", "permuted_3d"))
def test_order_reduction_and_rgb_axis(implementations, choice):
    results = []
    for side in implementations.pairs:
        req = _request(
            side,
            rgb=choice == "rgb_2d",
            order=(2, 0, 1),
            ndisplay=3 if choice == "permuted_3d" else 2,
        )
        results.append(req._get_order())
    assert results[0] == results[1]


@pytest.mark.parametrize("readonly", (False, True))
def test_level_rescale_uses_the_returned_array_in_place(implementations, readonly):
    results = []

    def observe(side):
        original = np.asarray([[8.0, 6.0, 4.0], [2.0, 4.0, 6.0], [4.0, 6.0, 8.0]])
        backing = original.copy()
        if readonly:
            backing.flags.writeable = False

        class ArraySlice:
            def as_array(self):
                return backing

        req = _request(
            side, factors=np.asarray([[2.0, 2.0, 2.0]]), shapes=np.asarray([[3, 4, 5]])
        )
        vars(req)["data_slice"] = ArraySlice()
        if readonly:
            with pytest.raises(ValueError, match="read-only"):
                req._thick_slice_at_level(0)
            np.testing.assert_array_equal(backing, original)
        else:
            result = req._thick_slice_at_level(0)
            results.append((backing.copy(), result.as_array()))
            np.testing.assert_array_equal(backing[1:], original[1:] / 2)
            assert backing[0, 0] == 2.0

    for side in implementations.pairs:
        observe(side)
    if results:
        _assert_same(*results)


@pytest.mark.parametrize(
    "choice",
    (
        "point_low",
        "point_high",
        "thick_overlap",
        "thick_outside",
        "all_displayed",
    ),
)
def test_out_of_bounds_point_and_thick_decisions(implementations, choice):
    results = []
    for side in implementations.pairs:
        point = (
            (-1.0, np.nan, np.nan)
            if choice in {"point_low", "thick_overlap", "thick_outside"}
            else (3.0, np.nan, np.nan)
        )
        projecting = choice.startswith("thick")
        if choice == "all_displayed":
            point = (np.nan,) * 3
        req = _request(
            side,
            point=point,
            projection="max" if projecting else "none",
            ndisplay=3 if choice == "all_displayed" else 2,
            right=(2.0, np.nan, np.nan) if choice == "thick_overlap" else None,
        )
        results.append(req._slice_out_of_bounds())
    assert results[0] is results[1]


@pytest.mark.parametrize(
    "point",
    (
        (np.nan, 1.5, 2.5, -1.5),
        (0.1, -0.1, np.nan),
        (1e20, np.nan),
    ),
)
def test_point_indices_preserve_numpy_round_and_python_integer_domain(
    implementations,
    point,
):
    results = [
        _selection(side.classes["_ScalarFieldSliceRequest"]._point_to_slices(point))
        for side in implementations.pairs
    ]
    assert results[0] == results[1]


@pytest.mark.parametrize("choice", ("inclusive_high", "collapsed", "displayed_nan"))
def test_thick_indices_inclusive_stop_minimum_width_and_displayed_skip(
    implementations,
    choice,
):
    results = []
    for side in implementations.pairs:
        point = (2.0, 3.0) if choice != "displayed_nan" else (2.0, np.nan)
        margins = (1.0, 1.0) if choice == "inclusive_high" else (0.1, 0.1)
        if choice == "displayed_nan":
            margins = (0.0, np.nan)
        data_slice = side.classes["_ThickNDSlice"](point, margins, margins)
        results.append(
            _selection(
                side.classes["_ScalarFieldSliceRequest"]._data_slice_to_slices(
                    data_slice,
                    [1] if choice == "displayed_nan" else [],
                )
            )
        )
    assert results[0] == results[1]


def _view_service(side, events, *, failure=False):
    real = side.classes["_ScalarFieldView"]

    class Service:
        def __call__(self, **kwargs):
            # Supports the valid old direct-constructor route so a baseline
            # mismatch is an algorithm trace failure, not a fake setup error.
            return real(**kwargs)

        def from_view(self, data):
            events.append(("from_view", data.shape))
            if failure:
                raise ProbeError("view")
            return real.from_view(data)

    return Service()


@pytest.mark.parametrize("failure", (None, "order", "project", "view"))
def test_single_calls_overrides_in_order_and_reads_fresh_view_factory(
    implementations,
    monkeypatch,
    failure,
):
    observations = []

    def observe(side):
        events = []
        order = (1, 0)
        service = _view_service(side, events, failure=failure == "view")

        class Request(side.classes["_ImageSliceRequest"]):
            def _get_order(self):
                events.append(("order",))
                if failure == "order":
                    raise ProbeError("order")
                return order

            def _project_thick_slice(self, data, data_slice):
                events.append(
                    (
                        "project",
                        data is self.data_at_data_level,
                        data_slice is self.data_slice,
                    )
                )
                if failure == "project":
                    raise ProbeError("project")
                side.namespace["_ScalarFieldView"] = service
                return np.arange(20, dtype=np.float32).reshape(4, 5)

        req = _request(side, cls=Request, order=(0, 2, 1))
        with monkeypatch.context() as patch:
            # Register original binding before the callback changes it; pytest
            # restores the real current namespace after every independent side.
            patch.setitem(
                side.namespace, "_ScalarFieldView", side.classes["_ScalarFieldView"]
            )
            if failure:
                with pytest.raises(ProbeError, match=f"^{failure}$"):
                    req._call_single_scale()
                snapshot = None
            else:
                snapshot = _response_state(req._call_single_scale())
        observations.append((events, snapshot))

    for side in implementations.pairs:
        observe(side)
    assert observations[0][0][0] == ("order",)
    _assert_same(*observations)


@pytest.mark.parametrize("failure", (False, True))
def test_multiscale_reuses_one_order_object_and_both_projection_overrides(
    implementations,
    monkeypatch,
    failure,
):
    observations = []

    def observe(side):
        events = []
        order = [1, 0]
        calls = []

        class NumpyProxy:
            def __getattr__(self, name):
                return getattr(np, name)

            def transpose(self, data, axes):
                events.append(("transpose_alias", axes is order))
                return np.transpose(data, axes)

        class Request(side.classes["_ImageSliceRequest"]):
            def _get_order(self):
                events.append(("order",))
                calls.append(order)
                return order

            def _project_thick_slice(self, data, data_slice):
                thumbnail = data is self.data_at_thumbnail_level
                events.append(("project", "thumbnail" if thumbnail else "image"))
                if thumbnail and failure:
                    raise ProbeError("thumbnail")
                return np.arange(20, dtype=np.float32).reshape(4, 5)

        req = _request(
            side,
            cls=Request,
            multiscale=True,
            thumbnail=np.ones((3, 4, 5), dtype=np.float32),
            order=(0, 2, 1),
        )
        service = _view_service(side, events)
        with monkeypatch.context() as patch:
            patch.setitem(side.namespace, "_ScalarFieldView", service)
            patch.setitem(side.namespace, "np", NumpyProxy())
            if failure:
                with pytest.raises(ProbeError, match="^thumbnail$"):
                    req._call_multi_scale()
                result = None
            else:
                result = _response_state(req._call_multi_scale())
        observations.append((events, len(calls), result))

    for side in implementations.pairs:
        observe(side)
    assert observations[0][1] == 1
    assert observations[0][0].count(("transpose_alias", True)) == (1 if failure else 2)
    assert observations[0][0].count(("from_view", (5, 4))) == (1 if failure else 2)
    _assert_same(*observations)


@pytest.mark.parametrize(
    "choice",
    (
        "normal",
        "instance_shadow",
        "saved_exit",
        "body_error",
        "suppressed_lifetime",
        "exit_error",
        "enter_error",
        "exit_lookup_error",
        "reentry",
    ),
)
def test_request_uses_real_with_protocol_and_releases_saved_exit_after_truth(
    implementations,
    choice,
):
    observations = []

    def observe(side):
        events, manager_refs, result_refs = [], [], []
        depth = 0

        class Result:
            pass

        class EnterResult:
            def __del__(self):
                events.append(("enter_result_released",))

        class TruthResult:
            def __init__(self, manager):
                self.manager = weakref.ref(manager)

            def __bool__(self):
                events.append(
                    (
                        "suppression_truth",
                        self.manager() is not None,
                        type(sys.exception()).__name__,
                    )
                )
                return True

        def enter(manager):
            events.append(("enter",))
            if choice == "enter_error":
                raise ProbeError("enter")
            if choice == "saved_exit":
                type(manager).__exit__ = Special("late_exit", exit_)
            return EnterResult()

        def exit_(manager, exc_type, value, traceback):
            events.append(
                (
                    "exit",
                    exc_type.__name__ if exc_type else None,
                    traceback is not None,
                    type(sys.exception()).__name__ if sys.exception() else None,
                )
            )
            if choice == "exit_error":
                raise ProbeError("exit")
            if choice == "suppressed_lifetime":
                return TruthResult(manager)
            if result_refs:
                events.append(("return_alive_at_exit", result_refs[-1]() is not None))
            return False

        class Special:
            def __init__(self, name, callback):
                self.name, self.callback = name, callback

            def __get__(self, instance, owner):
                events.append(("bind", self.name))
                if self.name == "exit" and choice == "exit_lookup_error":
                    raise ProbeError("exit_lookup")
                callback = self.callback
                return lambda *args: callback(instance, *args)

        class Manager:
            __enter__ = Special("enter", enter)
            __exit__ = Special("exit", exit_)

        def indexer():
            events.append(("indexer",))
            manager = Manager()
            manager_refs.append(weakref.ref(manager))
            if choice == "instance_shadow":

                def bad_enter():
                    raise ProbeError("instance_enter")

                manager.__enter__ = bad_enter
            return manager

        class Request(side.classes["_ImageSliceRequest"]):
            def _slice_out_of_bounds(self):
                events.append(("bounds",))
                return False

            def _call_single_scale(self):
                nonlocal depth
                events.append(("body", depth))
                if choice in {"body_error", "suppressed_lifetime"}:
                    raise ProbeError("body")
                if choice == "reentry" and depth == 0:
                    depth = 1
                    inner = self()
                    assert inner is not None
                    depth = 0
                result = Result()
                result_refs.append(weakref.ref(result))
                return result

        req = _request(side, cls=Request, indexer=indexer)
        expected_error = {
            "body_error": "body",
            "exit_error": "exit",
            "enter_error": "enter",
            "exit_lookup_error": "exit_lookup",
        }.get(choice)
        if expected_error:
            with pytest.raises(ProbeError, match=f"^{expected_error}$"):
                req()
            result = None
        else:
            result = req()
            assert (result is None) == (choice == "suppressed_lifetime")
        del result
        gc.collect()
        released = all(reference() is None for reference in manager_refs)
        assert released
        observations.append((events.copy(), released))

    for side in implementations.pairs:
        observe(side)
    old_events = observations[0][0]
    prefix = [("bounds",), ("indexer",), ("bind", "enter"), ("bind", "exit")]
    if choice != "exit_lookup_error":
        prefix.append(("enter",))
    assert old_events[: len(prefix)] == prefix
    if choice == "suppressed_lifetime":
        assert ("suppression_truth", True, "ProbeError") in old_events
    if choice == "normal":
        assert old_events.index(("enter_result_released",)) < old_events.index(
            ("body", 0)
        )
        assert ("return_alive_at_exit", True) in old_events
    assert observations[0] == observations[1]


def test_out_of_bounds_short_return_uses_fresh_empty_factory_and_skips_indexer(
    implementations,
    monkeypatch,
):
    observations = []

    def observe(side):
        events = []
        token = object()

        class Request(side.classes["_ImageSliceRequest"]):
            def _slice_out_of_bounds(self):
                events.append(("bounds",))
                return True

        def forbidden_indexer():
            raise ProbeError("out-of-bounds must not enter the indexer")

        req = _request(side, cls=Request, indexer=forbidden_indexer)

        def make_empty(**kwargs):
            events.append(
                (
                    "empty",
                    tuple(kwargs),
                    kwargs["slice_input"] is req.slice_input,
                    kwargs["rgb"],
                    kwargs["request_id"],
                    kwargs["dtype"],
                )
            )
            return token

        with monkeypatch.context() as patch:
            patch.setitem(
                side.namespace,
                "_ScalarFieldSliceResponse",
                SimpleNamespace(make_empty=make_empty),
            )
            assert req() is token
        observations.append(events)

    for side in implementations.pairs:
        observe(side)
    assert observations[0] == observations[1]


@pytest.mark.parametrize("projecting", (False, True))
def test_projection_helper_dispatch_and_callback_capture_before_materialization(
    implementations,
    projecting,
):
    observations = []

    def observe(side):
        events = []
        req = None

        def replacement(*, data, axis, mode):
            events.append(("replacement_project",))
            return data.sum(axis=axis)

        class Materializable:
            def __array__(self, dtype=None, copy=None):
                events.append(("materialize",))
                vars(req)["_project_slice"] = replacement
                return np.arange(12, dtype=np.float32).reshape(3, 4)

        class Data:
            def __getitem__(self, key):
                events.append(("index", _selection(key)))
                return Materializable()

        class Request(side.classes["_ImageSliceRequest"]):
            def _point_to_slices(self, point):
                events.append(("point_helper", point is self.data_slice.point))
                return (slice(None), slice(None))

            def _data_slice_to_slices(self, data_slice, displayed):
                events.append(
                    ("thick_helper", data_slice is self.data_slice, tuple(displayed))
                )
                return (slice(None), slice(None))

            def _project_slice(self, *, data, axis, mode):
                events.append(("original_project", tuple(axis), mode))
                return data.sum(axis=axis)

        req = _request(side, cls=Request, projection="sum" if projecting else "none")
        result = req._project_thick_slice(Data(), req.data_slice)
        observations.append((events, result))

    for side in implementations.pairs:
        observe(side)
    if projecting:
        assert any(event[0] == "original_project" for event in observations[0][0])
        assert not any(
            event[0] == "replacement_project" for event in observations[0][0]
        )
    else:
        assert observations[0][0][0][0] == "point_helper"
    _assert_same(*observations)


@pytest.mark.parametrize("choice", ("multiscale_shape", "thick_range"))
def test_unused_comprehension_item_is_released_before_the_next_source_callback(
    implementations,
    monkeypatch,
    choice,
):
    observations = []

    def observe(side):
        events, references = [], []

        class Item:
            def __init__(self, index):
                self.index = index

            def __del__(self):
                events.append(("item_released", self.index))

        class Items:
            def __init__(self, size):
                self.size, self.index = size, 0

            def __len__(self):
                return self.size

            def __iter__(self):
                return self

            def __next__(self):
                if self.index == self.size:
                    raise StopIteration
                item = Item(self.index)
                self.index += 1
                references.append(weakref.ref(item))
                return item

        def checkpoint(name):
            events.append(
                (
                    name,
                    len(references),
                    all(reference() is None for reference in references),
                )
            )

        with monkeypatch.context() as patch:
            if choice == "multiscale_shape":

                class Data:
                    dtype = np.dtype("float32")

                    @property
                    def shape(self):
                        return Items(3)

                    def __getitem__(self, key):
                        return np.arange(60, dtype=np.float32).reshape(3, 4, 5)[key]

                req = _request(side, multiscale=True)
                vars(req)["data_at_data_level"] = Data()

                def affine(**kwargs):
                    checkpoint("affine")
                    return implementations.Affine(**kwargs)

                patch.setitem(side.namespace, "Affine", affine)
                req._call_multi_scale()
                assert ("affine", 3, True) in events
            else:

                def source_range(size):
                    events.append(("range", size))
                    return Items(size)

                def source_enumerate(iterable):
                    checkpoint("enumerate")
                    return enumerate(iterable)

                patch.setitem(side.namespace, "range", source_range)
                patch.setitem(side.namespace, "enumerate", source_enumerate)
                thick = side.classes["_ThickNDSlice"](
                    (2.0, 3.0), (0.0, 0.0), (0.0, 0.0)
                )
                side.classes["_ScalarFieldSliceRequest"]._data_slice_to_slices(
                    thick, []
                )
                assert ("enumerate", 2, True) in events
        observations.append(events)

    for side in implementations.pairs:
        observe(side)
    assert observations[0] == observations[1]
