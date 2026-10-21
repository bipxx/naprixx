"""Finite independent twelve-body first-refresh control comparisons.

Pinned/current owned methods are rebuilt separately; slice-input, thick-slice,
view and response dataclasses retain their genuine class/decorator contracts.
Controlled layer/model/request/event boundaries expose policy, callbacks, aliases
and failures. Real NumPy, Affine/Pint, context-manager protocol, existing SlicePlan
and canonical native namespaces remain declared dependencies. Original oracles
never call a current owned body and are never mounted as application fallback.

The pre-port cohort must have zero changed bodies; afterwards all twelve must
be actual changed current facades. Construction, transform/unit policy, numerical
engines, model state, request execution, refresh-sync and actual Viewer delivery
remain outside this control packet. The unchanged installed original reports
supplement these comparisons; they do not close provider/platform/plugin, cold
startup, private frame identity, clean shutdown or full workflow ownership.
"""

from __future__ import annotations

import ast
import copy
import gc
import hashlib
import importlib
import inspect
import re
import warnings
import weakref
from pathlib import Path
from types import MethodType, SimpleNamespace

import numpy as np
import pytest

ROOT = Path(__file__).resolve().parents[1]
PINNED = ROOT / ".upstream/napari/src/napari"
CURRENT = ROOT / "src/napari"
BEFORE = ROOT / ".artifacts/first-refresh-control-before/files/src/napari"
HASHES = {
    "layers/base/base.py": (
        "2c9e08f2cf0508fdf2399abf49eea42ecb545adb81ac4c8422b57103b57eefa4"
    ),
    "layers/utils/_slice_input.py": (
        "a79eb82a741085e7c2a2e9f54710706fd812c7f3ba2d90c7a83cd795d700f7d4"
    ),
    "layers/_scalar_field/scalar_field.py": (
        "50d0f2c6adb4412589394dde4a55a8b156799b89ed55610a57496ef6d0d2a0b4"
    ),
    "layers/_scalar_field/_slice.py": (
        "0e327552ef2d11eaa198f84e86cac2ddbdefd5fa2b10947c42c45956b1c61188"
    ),
}
BEFORE_HASHES = {
    "layers/base/base.py": (
        "a302ad34b0a586d2ce2cdfda21e336d04d13503d8d4c0d67b33fb11c5ffa592a"
    ),
    "layers/utils/_slice_input.py": (
        "7c8c802c15269535c6f85279bea5bba52b0747575d711f0d1b5dbb0c9e151ffe"
    ),
    "layers/_scalar_field/scalar_field.py": (
        "0263dab9c8095857b7e60910fe8d6d6221aeebaae498203a091c329f4ab25276"
    ),
    "layers/_scalar_field/_slice.py": (
        "b8add84412376b7ccf205529152baf408bdbfbc627dc81ef1ccdfffaeb56c0c4"
    ),
}
OWNED = {
    ("layers/base/base.py", "Layer"): ("refresh", "_clear_extent", "set_view_slice"),
    ("layers/base/base.py", "_LayerSlicingState"): ("set_view_slice", "data_slice"),
    ("layers/utils/_slice_input.py", "_SliceInput"): ("data_slice",),
    ("layers/utils/_slice_input.py", "_ThickNDSlice"): ("as_array", "from_array"),
    ("layers/_scalar_field/scalar_field.py", "ScalarFieldSlicingState"): (
        "_set_view_slice",
        "_make_slice_request_internal",
    ),
    ("layers/_scalar_field/_slice.py", "_ScalarFieldSliceResponse"): ("make_empty",),
    ("layers/_scalar_field/_slice.py", "_ScalarFieldView"): ("from_view",),
}
REUSED_ORIGINAL_RECEIPT = (
    ".artifacts/layer-construction-installed-original-terminal.json"
)
REUSED_ORIGINAL_SELECTORS = (
    ".upstream/napari/src/napari/layers/image/_tests/test_image.py::test_random_image",
    ".upstream/napari/src/napari/layers/image/_tests/test_image.py::test_rgb_image",
    ".upstream/napari/src/napari/layers/image/_tests/test_multiscale.py::test_infer_multiscale",
    ".upstream/napari/src/napari/layers/image/_tests/test_multiscale.py::test_infer_tuple_multiscale",
    ".upstream/napari/src/napari/components/_tests/test_layer_slicer.py::test_submit_with_one_3d_image",
    ".upstream/napari/src/napari/_qt/_tests/test_async_slicing.py::test_async_slice_image_on_current_step_change",
    ".upstream/napari/src/napari/_qt/_tests/test_async_slicing.py::test_async_slice_multiscale_image_on_zoom",
)


def _source(root, relative):
    raw = (root / relative).read_bytes()
    hashes = HASHES if root == PINNED else BEFORE_HASHES if root == BEFORE else None
    if hashes is not None:
        assert hashlib.sha256(raw).hexdigest() == hashes[relative]
    return raw.decode("utf-8")


def _class(source, name):
    return next(
        n
        for n in ast.parse(source).body
        if isinstance(n, ast.ClassDef) and n.name == name
    )


def _methods(source, owner, names):
    return [
        copy.deepcopy(n)
        for n in _class(source, owner).body
        if isinstance(n, ast.FunctionDef) and n.name in names
    ]


def _execute(source, node, namespace, filename):
    future = [
        ast.ImportFrom(
            module="__future__", names=[ast.alias(name="annotations")], level=0
        )
    ]
    tree = ast.fix_missing_locations(ast.Module(body=future + [node], type_ignores=[]))
    exec(compile(tree, filename, "exec", dont_inherit=True), namespace)


@pytest.fixture(scope="module")
def implementations():
    from naparixx import _native

    changes = sum(
        ast.dump(_methods(_source(CURRENT, rel), owner, (name,))[0])
        != ast.dump(_methods(_source(BEFORE, rel), owner, (name,))[0])
        for (rel, owner), names in OWNED.items()
        for name in names
    )
    assert changes in (0, 12), (
        "All twelve must be before-port or actual current facades"
    )

    def build(root):
        classes, namespaces = {}, {}
        packets = (
            ("layers/utils/_slice_input.py", ("_ThickNDSlice", "_SliceInput"), True),
            (
                "layers/_scalar_field/_slice.py",
                ("_ScalarFieldView", "_ScalarFieldSliceResponse"),
                True,
            ),
            ("layers/base/base.py", ("Layer", "_LayerSlicingState"), False),
            (
                "layers/_scalar_field/scalar_field.py",
                ("ScalarFieldSlicingState",),
                False,
            ),
        )
        for rel, owners, complete in packets:
            module = importlib.import_module("napari." + rel[:-3].replace("/", "."))
            namespace = dict(vars(module))
            namespace.update(classes)
            namespace["_native"] = _native
            source = _source(root, rel)
            for owner in owners:
                if complete:
                    node = copy.deepcopy(_class(source, owner))
                else:
                    names = OWNED[(rel, owner)]
                    if owner == "_LayerSlicingState":
                        names += ("ndim",)
                    bases = []
                    if owner == "ScalarFieldSlicingState":
                        bases = [ast.Name(id="_LayerSlicingState", ctx=ast.Load())]
                    elif root == CURRENT:
                        bases = [
                            ast.parse("_native.LayerNamespaceBase", mode="eval").body
                        ]
                    node = ast.ClassDef(
                        name=owner,
                        bases=bases,
                        keywords=[],
                        body=_methods(source, owner, names),
                        decorator_list=[],
                    )
                _execute(source, node, namespace, str(root / rel))
                classes[owner] = namespace[owner]
                for previous in namespaces.values():
                    previous[owner] = namespace[owner]
            namespaces[rel] = namespace
        return SimpleNamespace(
            root=root,
            classes=classes,
            namespaces=namespaces,
            native=_native,
            changes=changes,
        )

    return build


def _value(value):
    if isinstance(value, np.ndarray):
        return value.shape, str(value.dtype), _value(value.tolist())
    if isinstance(value, np.generic):
        return _value(value.item())
    if isinstance(value, float) and np.isnan(value):
        return "NaN"
    if isinstance(value, (list, tuple)):
        return tuple(_value(item) for item in value)
    if isinstance(value, dict):
        return tuple((key, _value(item)) for key, item in value.items())
    return value


def _error(callback):
    try:
        callback()
    except Exception as error:
        return type(error).__name__, re.sub(r"0x[0-9a-fA-F]+", "0xADDRESS", str(error))
    return None


def _compare(implementations, scenario):
    expected = scenario(implementations(PINNED))
    actual = scenario(implementations(CURRENT))
    assert actual == expected
    return actual


@pytest.mark.parametrize(
    "case",
    (
        "blocked",
        "hidden",
        "sync_flags",
        "async",
        "async_hidden",
        "async_data_false",
        "slice_failure",
        "reentry",
    ),
)
def test_refresh_flags_visibility_async_failures_and_reentry(implementations, case):
    def scenario(impl):
        model = impl.classes["Layer"]()
        trace = []
        namespace = impl.namespaces["layers/base/base.py"]
        async_ = case.startswith("async")
        model._refresh_blocked = case == "blocked"
        model.visible = case not in ("hidden", "async_hidden")
        namespace["logger"] = SimpleNamespace(
            debug=lambda message, receiver: trace.append(
                ("log", message, receiver is model)
            )
        )

        def settings():
            trace.append(("settings",))
            return SimpleNamespace(experimental=SimpleNamespace(async_=async_))

        def slicing():
            trace.append(("slice",))
            if case == "slice_failure":
                raise RuntimeError("refresh slicing")

        depth = 0

        def syncing(**kwargs):
            nonlocal depth
            trace.append(("sync", tuple(kwargs.items())))
            if case == "reentry" and not depth:
                depth += 1
                model.refresh(
                    thumbnail=False,
                    data_displayed=False,
                    highlight=False,
                    extent=False,
                    force=True,
                )

        def reload(**kwargs):
            trace.append(("reload", kwargs["layer"] is model))

        namespace["get_settings"] = settings
        model.set_view_slice = slicing
        model._refresh_sync = syncing
        model.events = SimpleNamespace(reload=reload)
        error = _error(
            lambda: model.refresh(
                thumbnail=False,
                data_displayed=case != "async_data_false",
                highlight=False,
                extent=False,
                force=False,
            )
        )
        return tuple(trace), error

    trace, error = _compare(implementations, scenario)
    assert trace[0][0] == "log"
    assert (error is not None) == (case == "slice_failure")
    if case == "blocked":
        assert len(trace) == 1
    if case in ("async", "async_hidden"):
        assert trace[-2:] == (
            ("sync", (("extent", False), ("force", True))),
            ("reload", True),
        )


@pytest.mark.parametrize("case", ("empty", "both", "replace", "delete_failure"))
def test_extent_clear_uses_fresh_live_dictionary_and_publication(implementations, case):
    def scenario(impl):
        trace = []

        class Observed(impl.classes["Layer"]):
            def __delattr__(self, name):
                trace.append(("delete", name))
                if case == "delete_failure" and name == "extent":
                    raise RuntimeError("extent delete")
                super().__delattr__(name)
                if case == "replace" and name == "extent":
                    self.__dict__ = {
                        "events": self.events,
                        "_extent_augmented": "fresh",
                    }

        model = Observed()
        if case != "empty":
            model.extent = "extent-cache"
            model._extent_augmented = "augmented-cache"
        saved = vars(model)
        model.events = SimpleNamespace(
            _extent_augmented=lambda: trace.append(
                ("event", "extent" in vars(model), "_extent_augmented" in vars(model))
            )
        )
        error = _error(model._clear_extent)
        return (
            tuple(trace),
            error,
            ("extent" in vars(model)),
            ("_extent_augmented" in vars(model)),
            saved is vars(model),
        )

    trace, error, extent, augmented, same = _compare(implementations, scenario)
    assert (error is not None) == (case == "delete_failure")
    assert (extent, augmented) == ((True, True) if error else (False, False))
    assert same == (case != "replace")
    if not error:
        assert trace[-1] == ("event", False, False)


@pytest.mark.parametrize(
    "case",
    (
        "normal",
        "suppressed",
        "body_failure",
        "enter_failure",
        "exit_failure",
        "factory_failure",
    ),
)
def test_state_slice_uses_actual_context_protocol_and_exception_publication(
    implementations, case
):
    def scenario(impl):
        state = impl.classes["_LayerSlicingState"]()
        trace = []

        class Context:
            def __getattribute__(self, name):
                if name in ("__enter__", "__exit__"):
                    raise AssertionError("with must use type-special lookup")
                return super().__getattribute__(name)

            def __enter__(self):
                trace.append("enter")
                if case == "enter_failure":
                    raise RuntimeError("slice enter")
                return self

            def __exit__(self, kind, value, traceback):
                trace.append(
                    (
                        "exit",
                        kind.__name__ if kind else None,
                        str(value) if value else None,
                        traceback is not None,
                    )
                )
                if case == "exit_failure":
                    raise RuntimeError("slice exit")
                return case == "suppressed"

        def context():
            trace.append("factory")
            if case == "factory_failure":
                raise RuntimeError("slice factory")
            return Context()

        def slice_view():
            trace.append("body")
            if case in ("suppressed", "body_failure"):
                raise RuntimeError("slice body")

        state.dask_optimized_slicing = context
        state._set_view_slice = slice_view
        error = _error(state.set_view_slice)
        return tuple(trace), error

    trace, error = _compare(implementations, scenario)
    assert trace[0] == "factory"
    assert (error is None) == (case in ("normal", "suppressed"))
    assert sum(isinstance(item, tuple) and item[0] == "exit" for item in trace) == (
        case not in ("factory_failure", "enter_failure")
    )


def test_layer_slice_delegation_rereads_replaced_state(implementations):
    def scenario(impl):
        model, trace = impl.classes["Layer"](), []
        second = SimpleNamespace(set_view_slice=lambda: trace.append("second"))

        def first():
            trace.append("first")
            model._slicing_state = second

        model._slicing_state = SimpleNamespace(set_view_slice=first)
        model.set_view_slice()
        model.set_view_slice()
        return tuple(trace)

    assert _compare(implementations, scenario) == ("first", "second")


@pytest.mark.parametrize(
    "case", ("all_displayed", "units", "inverse_failure", "slice_failure")
)
def test_state_data_slice_inverse_early_return_units_and_callback(
    implementations, case
):
    def scenario(impl):
        from napari.utils.transforms import Affine

        trace = []

        class Layer:
            ndim = 2 if case == "all_displayed" else 3

            @property
            def _data_to_world(self):
                trace.append("transform")
                if case == "inverse_failure":
                    raise RuntimeError("inverse acquisition")
                return Affine(
                    scale=(2, 3, 4),
                    translate=(5, 6, 7),
                    ndim=3,
                    units=("nm", "nm", "nm"),
                )

        state = impl.classes["_LayerSlicingState"]()
        state.layer = Layer()
        state._units = ("um", "nm", "nm") if case == "units" else None
        thick = impl.classes["_ThickNDSlice"].make_full(
            point=(3, 4, 5), ndim=state.ndim
        )
        input_ = impl.classes["_SliceInput"](2, thick, tuple(range(state.ndim)))
        if case == "slice_failure":

            def failing(**kwargs):
                trace.append("slice-callback")
                raise RuntimeError("slice transform")

            input_ = SimpleNamespace(not_displayed=[0], data_slice=failing)
        state._slice_input = input_
        result = []
        error = _error(lambda: result.append(state.data_slice))
        return tuple(trace), error, _value(result[0].as_array()) if result else None

    trace, error, result = _compare(implementations, scenario)
    assert (error is not None) == (case in ("inverse_failure", "slice_failure"))
    if case == "all_displayed":
        assert trace == () and result[0] == (3, 2)


@pytest.mark.parametrize(
    "case",
    (
        "identity",
        "scale_translate",
        "nonorthogonal",
        "all_displayed",
        "warning_failure",
    ),
)
def test_real_slice_input_numerics_and_warning_before_transform(implementations, case):
    def scenario(impl):
        from napari.utils.transforms import Affine

        input_class = impl.classes["_SliceInput"]
        phase, armed, observed_transform = [], False, False
        if case in ("nonorthogonal", "warning_failure"):

            class Input(input_class):
                def is_orthogonal(self, transform):
                    phase.append("orthogonal")
                    return False

            class Transform(Affine):
                def __getattribute__(self, name):
                    nonlocal observed_transform
                    if (
                        armed
                        and not observed_transform
                        and name in ("set_slice", "linear_matrix", "translate")
                    ):
                        observed_transform = True
                        phase.append("transform")
                    return super().__getattribute__(name)

            input_class = Input
            affine_class = Transform
        else:
            affine_class = Affine
        thick = impl.classes["_ThickNDSlice"].make_full(
            point=(4, 5, 6), margin_left=(1, 2, 3), margin_right=(2, 3, 4)
        )
        input_ = input_class(3 if case == "all_displayed" else 2, thick, (0, 1, 2))
        matrix = np.eye(3)
        if case in ("nonorthogonal", "warning_failure"):
            matrix[1, 0] = 0.3
        transform = affine_class(linear_matrix=matrix, ndim=3)
        if case == "scale_translate":
            transform = Affine(scale=(2, 3, 4), translate=(1, 2, 3), ndim=3)
        namespace = impl.namespaces["layers/utils/_slice_input.py"]
        if case in ("nonorthogonal", "warning_failure"):

            def warn(*args, **kwargs):
                phase.append("warn")
                if case == "warning_failure":
                    raise RuntimeError("warning boundary")
                return warnings.warn(*args, **kwargs)

            namespace["warnings"] = SimpleNamespace(warn=warn)
        armed = True
        result = []
        with warnings.catch_warnings(record=True) as caught:
            warnings.simplefilter("always")
            error = _error(lambda: result.append(input_.data_slice(transform)))
        return (
            tuple((type(w.message).__name__, str(w.message)) for w in caught),
            error,
            _value(result[0].as_array()) if result else None,
            tuple(phase),
        )

    caught, error, result, phase = _compare(implementations, scenario)
    assert (error is not None) == (case == "warning_failure")
    assert len(caught) == (case == "nonorthogonal")
    if result:
        assert result[0] == (3, 3)
    if case == "nonorthogonal":
        assert phase == ("orthogonal", "warn", "transform")
    elif case == "warning_failure":
        assert phase == ("orthogonal", "warn")


@pytest.mark.parametrize("case", ("c_order", "strided", "row_failure"))
def test_thick_slice_array_roundtrip_classmethod_and_partial_read_order(
    implementations, case
):
    def scenario(impl):
        trace = []
        data = np.arange(24, dtype=np.float32).reshape(3, 8)
        if case == "strided":
            data = data[:, ::2]

        class Rows:
            def __getitem__(self, index):
                trace.append(index)
                if case == "row_failure" and index == 1:
                    raise KeyError("slice row")
                return data[index]

        class Thick(impl.classes["_ThickNDSlice"]):
            pass

        result = []
        error = _error(lambda: result.append(Thick.from_array(Rows())))
        if error:
            return tuple(trace), error, None
        thick = result[0]
        array = thick.as_array()
        data[0, 0] = 1000
        return (
            tuple(trace),
            error,
            (
                type(thick).__name__,
                _value(array),
                thick.point[0],
                np.shares_memory(array, data),
            ),
        )

    trace, error, result = _compare(implementations, scenario)
    assert trace == ((0, 1) if case == "row_failure" else (0, 1, 2))
    assert (error is not None) == (case == "row_failure")
    if result:
        assert result[0] == "Thick" and result[-1] is False and result[-2] == 0


@pytest.mark.parametrize(
    "case", ("2d", "rgb3d", "explicit_id", "dtype", "normalization_failure")
)
def test_empty_response_real_type_view_alias_dtype_and_id_order(implementations, case):
    def scenario(impl):
        trace = []
        namespace = impl.namespaces["layers/_scalar_field/_slice.py"]
        normalize, affine = namespace["normalize_dtype"], namespace["Affine"]

        def normalizing(dtype):
            trace.append(("normalize", str(np.dtype(dtype))))
            if case == "normalization_failure":
                raise RuntimeError("empty dtype")
            return normalize(dtype)

        def transforming(**kwargs):
            trace.append(("affine", kwargs["ndim"], _value(kwargs["linear_matrix"])))
            return affine(**kwargs)

        def identifier():
            trace.append(("id",))
            return 73

        namespace.update(
            normalize_dtype=normalizing,
            Affine=transforming,
            _next_request_id=identifier,
        )
        thick = impl.classes["_ThickNDSlice"].make_full(ndim=3)
        input_ = impl.classes["_SliceInput"](
            3 if case == "rgb3d" else 2, thick, (0, 1, 2)
        )

        class Response(impl.classes["_ScalarFieldSliceResponse"]):
            pass

        result = []
        error = _error(
            lambda: result.append(
                Response.make_empty(
                    slice_input=input_,
                    rgb=case == "rgb3d",
                    dtype=np.int16 if case == "dtype" else np.uint8,
                    request_id=91 if case == "explicit_id" else None,
                )
            )
        )
        if error:
            return tuple(trace), error, None
        response = result[0]
        return (
            tuple(trace),
            error,
            (
                type(response).__name__,
                _value(response.image.raw),
                response.image.raw is response.image.view,
                response.image is response.thumbnail,
                response.slice_input is input_,
                response.request_id,
                response.empty,
                _value(response.tile_to_data.affine_matrix),
            ),
        )

    trace, error, result = _compare(implementations, scenario)
    assert trace[0][0] == "normalize"
    assert (error is not None) == (case == "normalization_failure")
    if result:
        assert result[0] == "_ScalarFieldSliceResponse" and result[2:5] == (
            True,
            True,
            True,
        )
        assert result[5] == (91 if case == "explicit_id" else 73) and result[6]
        assert trace[-1][0] == ("affine" if case == "explicit_id" else "id")


def test_from_view_uses_actual_cls_and_same_strided_array_alias(implementations):
    def scenario(impl):
        data = np.arange(24).reshape(4, 6)[:, ::2]

        class View(impl.classes["_ScalarFieldView"]):
            pass

        view = View.from_view(data)
        data[0, 0] = 123
        return (
            type(view).__name__,
            view.raw is data,
            view.view is data,
            view.raw is view.view,
            view.view[0, 0],
            view.view.strides,
        )

    result = _compare(implementations, scenario)
    assert result[:5] == ("View", True, True, True, 123)


def _scalar_model(impl, *, multi=False, display=2, locked=None, level=0, thumbnail=1):
    from napari.layers._multiscale_data import MultiScaleData
    from napari.utils.transforms import Affine

    arrays = (
        [
            np.arange(256, dtype=np.uint8).reshape(4, 8, 8),
            np.arange(32, dtype=np.uint8).reshape(2, 4, 4),
        ]
        if multi
        else [np.arange(48, dtype=np.uint8).reshape(6, 8)]
    )
    data = MultiScaleData(arrays) if multi else arrays[0]
    ndim = 3 if multi else 2
    layer = SimpleNamespace(
        data=data,
        multiscale=multi,
        _locked_data_level=locked,
        data_level=level,
        _data_level=level,
        _thumbnail_level=thumbnail if multi else 0,
        _level_materializer=None,
        ndim=ndim,
        dtype=np.dtype(np.uint8),
        projection_mode="none",
        corner_pixels=np.zeros((2, ndim), dtype=int),
        level_shapes=np.array([a.shape for a in arrays]),
        downsample_factors=np.array(
            [[1] * ndim, [2] * ndim] if multi else [[1] * ndim]
        ),
        _data_to_world=Affine(ndim=ndim),
    )
    state = impl.classes["ScalarFieldSlicingState"]()
    state.layer, state._units = layer, None
    thick = impl.classes["_ThickNDSlice"].make_full(ndim=ndim)
    state._slice_input = impl.classes["_SliceInput"](display, thick, tuple(range(ndim)))
    return state, layer, arrays


def _request_snapshot(state, arrays, kwargs):
    return (
        tuple(kwargs),
        kwargs["slice_input"] is state._slice_input,
        tuple(kwargs["data_at_data_level"] is a for a in arrays),
        tuple(kwargs["data_at_thumbnail_level"] is a for a in arrays),
        kwargs["data_at_data_level"] is kwargs["data_at_thumbnail_level"],
        str(kwargs["dtype"]),
        kwargs["multiscale"],
        kwargs["rgb"],
        kwargs["data_level"],
        kwargs["thumbnail_level"],
        _value(kwargs["corner_pixels"]),
        _value(kwargs["level_shapes"]),
        _value(kwargs["downsample_factors"]),
    )


@pytest.mark.parametrize(
    "case",
    (
        "single",
        "unlocked3d",
        "locked3d",
        "request_failure",
        "update_failure",
    ),
)
def test_scalar_view_slice_level_corner_aliases_and_partial_failures(
    implementations, case
):
    def scenario(impl):
        state, layer, arrays = _scalar_model(
            impl,
            multi=case != "single",
            display=3 if case != "single" else 2,
            locked=0 if case == "locked3d" else None,
        )
        trace = []
        response = object()

        def request_factory(**kwargs):
            trace.append(
                (
                    "request",
                    layer._data_level,
                    _value(layer.corner_pixels),
                    _request_snapshot(state, arrays, kwargs),
                )
            )

            def execute():
                trace.append(("execute",))
                if case == "request_failure":
                    raise RuntimeError("first refresh request")
                return response

            return execute

        def update(value):
            trace.append(("update", value is response))
            if case == "update_failure":
                raise RuntimeError("first refresh update")

        state._slice_request_class = request_factory
        state._update_slice_response = update
        error = _error(state._set_view_slice)
        return tuple(trace), error, layer._data_level, _value(layer.corner_pixels)

    trace, error, level, _ = _compare(implementations, scenario)
    assert trace[0][0] == "request" and trace[1] == ("execute",)
    assert (error is not None) == (case in ("request_failure", "update_failure"))
    assert level == (0 if case in ("single", "locked3d") else 1)
    if case != "request_failure":
        assert trace[2] == ("update", True)


@pytest.mark.parametrize(
    "case",
    (
        "single",
        "multi2d",
        "multi3d",
        "locked",
        "same_level",
        "materializer_replaced",
    ),
)
def test_request_selection_live_materializer_and_data_aliases(implementations, case):
    def scenario(impl):
        state, layer, arrays = _scalar_model(
            impl,
            multi=case != "single",
            display=3 if case == "multi3d" else 2,
            locked=0 if case == "locked" else None,
            level=1 if case == "same_level" else 0,
        )
        trace, requests = [], []

        if case == "materializer_replaced":

            def replacement(level):
                trace.append(("materialize", level))
                return arrays[level]

            class Initial:
                def __bool__(self):
                    trace.append(("materializer-truth",))
                    layer._level_materializer = replacement
                    return True

                def __call__(self, level):
                    raise AssertionError("stale materializer called")

            layer._level_materializer = Initial()

        def request(**kwargs):
            requests.append(kwargs)
            return kwargs

        state._slice_request_class = request
        data_slice = state.data_slice
        indexer = object()
        result = state._make_slice_request_internal(
            slice_input=state._slice_input, data_slice=data_slice, dask_indexer=indexer
        )
        return (
            tuple(trace),
            _request_snapshot(state, arrays, result),
            (
                result is requests[0],
                result["data_slice"] is data_slice,
                result["dask_indexer"] is indexer,
            ),
        )

    trace, snapshot, aliases = _compare(implementations, scenario)
    assert aliases == (True, True, True)
    assert snapshot[8] == (1 if case in ("multi3d", "same_level") else 0)
    assert snapshot[4] == (case in ("single", "multi3d", "same_level"))
    if case == "materializer_replaced":
        assert trace == (("materializer-truth",), ("materialize", 1))


def test_context_and_request_source_local_lifetime_and_gc(implementations):
    def scenario(impl):
        trace = []
        state = impl.classes["_LayerSlicingState"]()

        class Context:
            def __init__(self, owner):
                self.owner = owner

            def __enter__(self):
                trace.append("enter")
                return self

            def __exit__(self, *args):
                trace.append("exit")

        context = Context(state)
        state.constructor_context = context

        def context_factory(self):
            return self.constructor_context

        state.dask_optimized_slicing = MethodType(context_factory, state)

        def slicing(self):
            trace.append("body")

        state._set_view_slice = MethodType(slicing, state)
        state.set_view_slice()
        state_ref, context_ref = weakref.ref(state), weakref.ref(context)
        del state, context
        gc.collect()
        collected = state_ref() is None and context_ref() is None
        scalar, _, _ = _scalar_model(impl)

        class Response:
            def __del__(self):
                trace.append("response-released")

        class Request:
            def __call__(self):
                trace.append("execute")
                return Response()

            def __del__(self):
                trace.append("request-released")

        scalar._slice_request_class = lambda **kwargs: Request()
        scalar._update_slice_response = lambda response: trace.append("update")
        scalar._set_view_slice()
        return tuple(trace), collected

    trace, collected = _compare(implementations, scenario)
    assert collected
    assert trace == (
        "enter",
        "body",
        "exit",
        "execute",
        "update",
        "request-released",
        "response-released",
    )


def test_all_twelve_owned_methods_have_independent_preserved_metadata(implementations):
    old, new = implementations(PINNED), implementations(CURRENT)
    count = 0
    for (_, owner), names in OWNED.items():
        for name in names:
            first, second = (
                old.classes[owner].__dict__[name],
                new.classes[owner].__dict__[name],
            )
            if isinstance(first, classmethod):
                assert isinstance(second, classmethod)
                first, second = first.__func__, second.__func__
            elif isinstance(first, property):
                assert isinstance(second, property)
                first, second = first.fget, second.fget
            assert first is not second
            assert (
                first.__name__,
                first.__qualname__,
                first.__doc__,
                str(inspect.signature(first)),
            ) == (
                second.__name__,
                second.__qualname__,
                second.__doc__,
                str(inspect.signature(second)),
            )
            count += 1
    assert count == 12
