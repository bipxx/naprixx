"""Finite Layer namespace, thumbnail, contrast and loaded-publication baseline.

All seventeen owned bodies (including validate_n_seq's nested func) and their
owned callees are rebuilt independently from pinned/current source. Controlled
model, event and slice boundaries expose publication and failure order; NumPy,
SciPy, formatting, iteration and the accepted load-state engine are real services.
The allocation comparisons rebuild complete held-source Layer/slicing-state and
seven concrete class hierarchies without mounting them in application modules.
Nonowned constructors, events, transforms, models, slice requests and previously
accepted native helpers are explicit dependencies, not closure of those owners.
Before the port, the candidate hierarchy uses the installed VispyNamespaceBase;
afterwards the current hierarchy must use LayerNamespaceBase. This establishes
the bounded allocation decision, not arbitrary plugin MI, pickle resolution,
private generator frames, GPU/GUI behavior, full model authority or clean shutdown.
"""

from __future__ import annotations

import ast
import copy
import gc
import hashlib
import importlib
import inspect
import itertools
import re
import types
import weakref
from collections.abc import Collection, Generator, Iterable
from pathlib import Path
from types import SimpleNamespace

import numpy as np
import pytest

ROOT = Path(__file__).resolve().parents[1]
PINNED = ROOT / ".upstream/napari/src/napari"
CURRENT = ROOT / "src/napari"
BEFORE = ROOT / ".artifacts/layer-slice-publication-before/src/napari"
HASHES = {
    "layers/base/base.py": (
        "2c9e08f2cf0508fdf2399abf49eea42ecb545adb81ac4c8422b57103b57eefa4"
    ),
    "layers/image/image.py": (
        "73027b2f096445c6aea88689bbf29f8707a75847d1348fa3b80eaf867c728c56"
    ),
    "layers/labels/labels.py": (
        "8d5ac1e119018af7b49df29838a91273910df09c2d4e4e6ec7e58a3744e7a176"
    ),
    "layers/intensity_mixin.py": (
        "9d3a6aa09b010a133ccd63f237692f79e4013bf7b69f2a10b5d590237240e034"
    ),
    "layers/utils/layer_utils.py": (
        "6d3ed1151813da2cc39a1d476fb0b13cd712e1b741c0a42fb02d7b7f6a244d6d"
    ),
    "utils/colormaps/colormap.py": (
        "af2eed8dfa33850a7e8000cd3ac36cffca00d2519ffdcbed024aeae27819ff80"
    ),
    "utils/validators.py": (
        "8a7b3f3552b27cd50a829eee3222a0d86a08e1026b9a1a6b627033f1298aa862"
    ),
    "utils/status_messages.py": (
        "9ff136f36e17b9da21f3287b2023416bdfb382bb5a3594629930df3a530bb73f"
    ),
}
BEFORE_HASHES = {
    "layers/base/base.py": (
        "c52012df39a723ddae96685cb20f02dfedee8d8280dd0bc020aeeee36b24dff4"
    ),
    "layers/image/image.py": (
        "dfc435c8fdc60133abe2d0ef4a5f0522c3215ffc432188c929a027511362d1c6"
    ),
    "layers/labels/labels.py": (
        "f3944b0325086be9cce0cbf0457f3957fe7321f6ebc1367a8b72ad4b402b8472"
    ),
    "layers/intensity_mixin.py": (
        "37711d71ebe7385a723cbec79297e327a4a9f4ff70cf505cb66df42874e41b94"
    ),
}
OWNED = {
    ("layers/base/base.py", "Layer"): ("thumbnail",),
    ("layers/base/base.py", "_LayerSlicingState"): ("_set_loaded",),
    ("layers/image/image.py", "Image"): ("_update_thumbnail", "contrast_limits"),
    ("layers/labels/labels.py", "Labels"): ("_update_thumbnail",),
    ("layers/intensity_mixin.py", "IntensityVisualizationMixin"): (
        "contrast_limits",
        "contrast_limits_range",
    ),
    ("utils/colormaps/colormap.py", "Colormap"): ("map",),
}
ORIGINAL_SELECTORS = (
    ".upstream/napari/src/napari/layers/image/_tests/test_image.py::test_thumbnail",
    ".upstream/napari/src/napari/layers/image/_tests/test_image.py::test_narrow_thumbnail",
    ".upstream/napari/src/napari/layers/image/_tests/test_image.py::test_set_contrast_limits_range",
    ".upstream/napari/src/napari/layers/image/_tests/test_image.py::test_set_contrast_limits_range_at_boundary_of_contrast_limits",
    ".upstream/napari/src/napari/layers/image/_tests/test_image.py::test_adjust_contrast_limits_range_set_data",
    ".upstream/napari/src/napari/layers/image/_tests/test_multiscale.py::test_thumbnail",
    ".upstream/napari/src/napari/layers/labels/_tests/test_labels.py::test_thumbnail_non_visible",
    ".upstream/napari/src/napari/layers/labels/_tests/test_labels.py::test_thumbnail_single_color",
    ".upstream/napari/src/napari/layers/utils/_tests/test_layer_utils.py::test_convert_to_uint8_non_native_byte_order",
    ".upstream/napari/src/napari/layers/utils/_tests/test_link_layers.py::test_link_layers_with_images_then_loaded_not_linked",
    ".upstream/napari/src/napari/utils/_tests/test_validators.py::test_sequence_validator",
    ".upstream/napari/src/napari/utils/_tests/test_validators.py::test_pairwise",
    ".upstream/napari/src/napari/utils/_tests/test_validators.py::test_validate_increasing",
    ".upstream/napari/src/napari/utils/colormaps/_tests/test_colormaps.py::test_colormap",
    ".upstream/napari/src/napari/_qt/_tests/test_async_slicing.py::test_async_slice_image_loaded",
    ".upstream/napari/src/napari/_qt/_tests/test_qt_viewer.py::test_thumbnail_labels",
)


def _source(root, relative):
    raw = (root / relative).read_bytes()
    if root == PINNED:
        assert hashlib.sha256(raw).hexdigest() == HASHES[relative]
    if root == BEFORE and relative in BEFORE_HASHES:
        assert hashlib.sha256(raw).hexdigest() == BEFORE_HASHES[relative]
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


def _execute(source, nodes, namespace, filename):
    # Match the defining module's future flag, rather than inheriting this test's.
    future = (
        [
            ast.ImportFrom(
                module="__future__", names=[ast.alias(name="annotations")], level=0
            )
        ]
        if "from __future__ import annotations" in source
        else []
    )
    tree = ast.fix_missing_locations(ast.Module(body=future + nodes, type_ignores=[]))
    exec(compile(tree, filename, "exec", dont_inherit=True), namespace)


def _functions(root, relative, names, namespace):
    source = _source(root, relative)
    namespace["__name__"] = "napari." + relative[:-3].replace("/", ".")
    nodes = [
        copy.deepcopy(n)
        for n in ast.parse(source).body
        if isinstance(n, ast.FunctionDef) and n.name in names
    ]
    assert len(nodes) == len(names)
    _execute(source, nodes, namespace, str(root / relative))


def _shell(root, relative, owner, names, namespace, bases=()):
    source = _source(root, relative)
    namespace["__name__"] = "napari." + relative[:-3].replace("/", ".")
    node = ast.ClassDef(
        name=owner,
        bases=[ast.Name(id=name, ctx=ast.Load()) for name in bases],
        keywords=[],
        body=_methods(source, owner, names),
        decorator_list=[],
    )
    _execute(source, [node], namespace, str(root / relative))
    return namespace[owner]


def _value(value):
    if isinstance(value, np.ndarray):
        return value.shape, str(value.dtype), _value(value.tolist())
    if isinstance(value, np.generic):
        return _value(value.item())
    if isinstance(value, (tuple, list)):
        return tuple(_value(v) for v in value)
    if isinstance(value, float) and np.isnan(value):
        return "NaN"
    return value


def _error(callback):
    try:
        callback()
    except Exception as error:
        return type(error).__name__, re.sub(r"0x[0-9a-fA-F]+", "0xADDRESS", str(error))
    return None


@pytest.fixture(scope="module")
def implementations():
    from napari.layers.base import base as base_module
    from napari.layers.image import image as image_module
    from napari.layers.labels import labels as labels_module
    from napari.utils.colormaps import colormap as cmap_module
    from naparixx import _native

    def build(root):
        common = dict(vars(base_module))
        common.update(
            Collection=Collection,
            Generator=Generator,
            Iterable=Iterable,
            tee=itertools.tee,
            np=np,
            _native=_native,
        )
        # Every owned helper is compiled separately into each side's namespace.
        _functions(
            root,
            "utils/validators.py",
            ("validate_n_seq", "_pairwise", "_validate_increasing"),
            common,
        )
        common["validate_2_tuple"] = common["validate_n_seq"](2)
        common["DEFAULT_PRECISION"] = 3
        _functions(root, "utils/status_messages.py", ("format_float",), common)
        _functions(root, "layers/utils/layer_utils.py", ("convert_to_uint8",), common)
        layer = _shell(root, "layers/base/base.py", "Layer", ("thumbnail",), common)
        mixin = _shell(
            root,
            "layers/intensity_mixin.py",
            "IntensityVisualizationMixin",
            ("contrast_limits", "contrast_limits_range"),
            common,
        )
        # The source Image setter's decorator and lexical class name are real.
        image_ns = dict(vars(image_module))
        image_ns.update(common)
        image = _shell(
            root,
            "layers/image/image.py",
            "Image",
            ("_update_thumbnail", "contrast_limits"),
            image_ns,
            ("IntensityVisualizationMixin", "Layer"),
        )
        labels_ns = dict(vars(labels_module))
        labels_ns.update(common)
        labels = _shell(
            root,
            "layers/labels/labels.py",
            "Labels",
            ("_update_thumbnail",),
            labels_ns,
            ("Layer",),
        )
        cmap_ns = dict(vars(cmap_module))
        cmap_ns.update(common)
        cmap = _shell(
            root, "utils/colormaps/colormap.py", "Colormap", ("map",), cmap_ns
        )
        # _loaded's accepted backing store is reused; the targeted operation is
        # source-pinned on both sides rather than shared as a live helper.
        state_source = _source(BEFORE, "layers/base/base.py")
        state_methods = _methods(state_source, "_LayerSlicingState", ("_loaded",))
        state_methods += _methods(
            _source(root, "layers/base/base.py"), "_LayerSlicingState", ("_set_loaded",)
        )
        state_node = ast.ClassDef(
            name="_LayerSlicingState",
            bases=[],
            keywords=[],
            body=state_methods,
            decorator_list=[],
        )
        _execute(state_source, [state_node], common, str(root / "layers/base/base.py"))
        return SimpleNamespace(
            root=root,
            namespace=common,
            image_namespace=image_ns,
            layer=layer,
            mixin=mixin,
            image=image,
            labels=labels,
            cmap=cmap,
            cmap_namespace=cmap_ns,
            state=common["_LayerSlicingState"],
            native=_native,
        )

    return build


def _compare(implementations, scenario):
    expected = scenario(implementations(PINNED))
    observed = scenario(implementations(CURRENT))
    assert observed == expected
    return observed


def _model(impl, kind="mixin", data=None, *, rgb=False, display=2):
    trace = []
    cls = getattr(impl, kind)
    model = cls()
    model._thumbnail_shape = (4, 6, 4)
    model._thumbnail = np.full(model._thumbnail_shape, 11, dtype=np.uint8)
    model._contrast_limits = (0.0, 1.0)
    model._contrast_limits_range = (0.0, 1.0)
    model._auto_contrast = True
    model._gamma = 1.0
    if kind in ("image", "labels"):
        payload = (
            np.arange(6, dtype=np.float32).reshape(2, 3) / 5 if data is None else data
        )
        model._slice = SimpleNamespace(
            empty=False, thumbnail=SimpleNamespace(raw=payload)
        )
        model._slice_input = SimpleNamespace(ndisplay=display)
        model._slicing_state = SimpleNamespace(loaded=True)
        model.ndim = payload.ndim
        model.dtype = payload.dtype
        model.rgb = rgb
        model.gamma = 1.0
        model.opacity = 0.5
        cmap = impl.cmap()
        cmap.colors = np.array([[0, 0, 0, 1], [1, 0.5, 0.25, 1]], dtype=np.float32)
        cmap.controls = np.array([0, 1], dtype=np.float32)
        cmap.interpolation = impl.cmap_namespace["ColormapInterpolationMode"].LINEAR
        cmap.nan_color = np.zeros(4)
        cmap.high_color = None
        cmap.low_color = None
        model.colormap = cmap
    else:
        model._update_thumbnail = lambda: trace.append(("update_thumbnail",))

    def event(name):
        def emit():
            trace.append(
                (
                    name,
                    _value(model._contrast_limits),
                    _value(model._contrast_limits_range),
                    _value(model._thumbnail),
                )
            )

        return emit

    model.events = SimpleNamespace(
        **{
            name: event(name)
            for name in ("thumbnail", "contrast_limits", "contrast_limits_range")
        }
    )
    model.refresh = lambda **kwargs: trace.append(
        ("refresh", kwargs, model._auto_contrast)
    )
    return model, trace


@pytest.mark.parametrize(
    "case", ("uint8", "float", "empty", "padding_failure", "event_failure")
)
def test_thumbnail_blending_padding_and_partial_publication(implementations, case):
    def scenario(impl):
        model, trace = _model(impl, "layer")
        original = model._thumbnail
        data = np.array([[[20, 40, 60, 128], [100, 80, 40, 255]]], dtype=np.uint8)
        if case == "float":
            data = data.astype(float) / 255
        if case == "empty":
            data = np.empty((0, 2, 4), dtype=np.uint8)
        if case == "padding_failure":
            data = np.zeros((8, 8, 4), dtype=np.uint8)
        if case == "event_failure":

            def fail():
                trace.append(("thumbnail_error", _value(model._thumbnail)))
                raise RuntimeError("thumbnail event")

            model.events.thumbnail = fail
        error = _error(lambda: setattr(model, "thumbnail", data))
        return (
            error,
            _value(model.thumbnail),
            model.thumbnail is original,
            _value(data),
            tuple(trace),
        )

    result = _compare(implementations, scenario)
    if case == "padding_failure":
        assert result[0][0] == "ValueError" and result[2]
    elif case == "event_failure":
        assert result[0] == ("RuntimeError", "thumbnail event") and not result[2]
    else:
        assert result[0] is None and result[1][:2] == ((4, 6, 4), "uint8")


@pytest.mark.parametrize(
    "case",
    (
        "uint8",
        "bool",
        "float",
        "uint_small",
        "uint_wide",
        "int8",
        "int_wide",
        "non_native",
        "unsupported",
    ),
)
def test_uint8_identity_mutation_and_dtype_routes(implementations, case):
    def scenario(impl):
        values = {
            "uint8": np.array([0, 1, 255], dtype=np.uint8),
            "bool": np.array([False, True]),
            "float": np.array([-1, 0.5, 2, np.nan, np.inf]),
            "uint_small": np.array([0, 100, 254], dtype=np.uint16),
            "uint_wide": np.array([0, 256, 65535], dtype=np.uint16),
            "int8": np.array([-128, -1, 0, 127], dtype=np.int8),
            "int_wide": np.array([-1, 0, 256, 32767], dtype=np.int16),
            "non_native": np.array([-1, 0, 32767], dtype=">i2"),
            "unsupported": np.array([1j], dtype=np.complex64),
        }[case]
        output = []
        error = _error(
            lambda: output.append(impl.namespace["convert_to_uint8"](values))
        )
        return (
            error,
            _value(values),
            _value(output),
            bool(output and output[0] is values),
        )

    result = _compare(implementations, scenario)
    if case == "uint8":
        assert result[3]
    if case in ("int8", "int_wide", "non_native"):
        assert min(result[1][2]) == 0
    if case == "unsupported":
        assert result[0][0] == "NotImplementedError"
    else:
        assert result[0] is None


@pytest.mark.parametrize("case", ("linear", "zero", "special", "invalid"))
def test_colormap_owned_interpolation_shape_and_special_values(implementations, case):
    def scenario(impl):
        model, _ = _model(impl, "image")
        cmap = model.colormap
        if case == "zero":
            cmap.interpolation = impl.cmap_namespace["ColormapInterpolationMode"].ZERO
            cmap.controls = np.array([0, 0.5, 1])
        if case == "special":
            cmap.nan_color = np.array([0.2, 0.3, 0.4, 0.5])
            cmap.low_color = np.array([1, 1, 0, 1])
            cmap.high_color = np.array([0, 1, 1, 1])
        if case == "invalid":
            cmap.interpolation = "unsupported"
        output = []
        error = _error(
            lambda: output.append(cmap.map(np.array([[-1, 0, 0.25], [0.5, 1, np.nan]])))
        )
        return error, _value(output)

    result = _compare(implementations, scenario)
    if case == "invalid":
        assert result[0] == ("ValueError", "Unrecognized Colormap Interpolation Mode")
    else:
        assert result[0] is None and result[1][0][0] == (2, 3, 4)


@pytest.mark.parametrize(
    "case",
    (
        "empty",
        "float16",
        "projection",
        "integer_clip",
        "constant",
        "rgb8",
        "rgba8",
        "rgb_float",
    ),
)
def test_image_thumbnail_projection_normalization_and_alpha(implementations, case):
    def scenario(impl):
        data = np.arange(6, dtype=np.float32).reshape(2, 3) / 5
        rgb = case.startswith("rgb")
        if case == "float16":
            data = data.astype(np.float16)
        if case == "projection":
            data = np.stack((data, data * 0.5))
        if case == "integer_clip":
            data = np.array([[0, 100, 255], [200, 10, 30]], dtype=np.uint8)
        if rgb:
            channels = 4 if case == "rgba8" else 3
            data = (
                np.arange(2 * 3 * channels, dtype=np.uint8).reshape(2, 3, channels) * 7
            )
            if case == "rgb_float":
                data = data.astype(np.float32) / 255
        model, trace = _model(
            impl, "image", data, rgb=rgb, display=3 if case == "projection" else 2
        )
        if case == "empty":
            model._slice.empty = True
        if case == "integer_clip":
            model._contrast_limits = (-10, 500)
        if case == "constant":
            model._contrast_limits = (0.5, 0.5)
        before = data.copy()
        error = _error(model._update_thumbnail)
        return (
            error,
            _value(model.thumbnail),
            np.array_equal(data, before),
            tuple(trace),
        )

    result = _compare(implementations, scenario)
    assert result[0] is None and result[1][:2] == ((4, 6, 4), "uint8")
    assert result[2] and len(result[3]) == 1 and result[3][0][0] == "thumbnail"
    if case == "empty":
        assert np.array(result[1][2])[..., :3].max() == 0


@pytest.mark.parametrize("case", ("unloaded", "empty", "projection"))
def test_labels_thumbnail_guard_and_publication(implementations, case):
    def scenario(impl):
        data = np.array([[0, 1, 0], [1, 0, 1]], dtype=np.uint8)
        if case == "projection":
            data = np.stack((data, 1 - data))
        model, trace = _model(
            impl, "labels", data, display=3 if case == "projection" else 2
        )
        saved = model.thumbnail
        model._slice.empty = case == "empty"
        model._slicing_state.loaded = case != "unloaded"
        error = _error(model._update_thumbnail)
        return error, model.thumbnail is saved, _value(model.thumbnail), tuple(trace)

    result = _compare(implementations, scenario)
    assert result[0] is None
    if case == "projection":
        assert not result[1] and len(result[3]) == 1
    else:
        assert result[1] and result[3] == ()


@pytest.mark.parametrize(
    "case",
    (
        "limits_alias",
        "range_clip",
        "range_outside",
        "range_same",
        "invalid",
        "range_error",
        "thumbnail_error",
    ),
)
def test_contrast_validation_alias_callbacks_and_partial_state(implementations, case):
    def scenario(impl):
        model, trace = _model(impl)
        value = [0.2, 0.8]
        property_name = "contrast_limits"
        if case.startswith("range_"):
            property_name = "contrast_limits_range"
            value = {
                "range_clip": [0.25, 0.75],
                "range_outside": [2, 3],
                "range_same": [0, 1],
                "range_error": [-1, 2],
            }[case]
        if case == "invalid":
            value = [0.8, 0.2]
        if case == "range_error":

            def fail():
                trace.append(("range_failure", _value(model._contrast_limits_range)))
                raise RuntimeError("range publication")

            model.events.contrast_limits_range = fail
        if case == "thumbnail_error":

            def fail():
                trace.append(("thumbnail_failure", _value(model._contrast_limits)))
                raise RuntimeError("thumbnail update")

            model._update_thumbnail = fail
        error = _error(lambda: setattr(model, property_name, value))
        limits = model.contrast_limits
        limits.append(999)
        return (
            error,
            _value(model._contrast_limits),
            _value(model._contrast_limits_range),
            model._contrast_limits is value,
            getattr(model, "_contrast_limits_msg", None),
            tuple(trace),
        )

    result = _compare(implementations, scenario)
    if case == "limits_alias":
        assert result[3] and result[1] == (0.2, 0.8)
        assert [entry[0] for entry in result[5]] == [
            "update_thumbnail",
            "contrast_limits",
        ]
    if case == "range_same":
        assert result[5] == ()
    if case == "invalid":
        assert result[0][0] == "ValueError" and result[1] == (0.0, 1.0)
    if case in ("range_error", "thumbnail_error"):
        assert result[0][0] == "RuntimeError"


def test_image_contrast_refresh_failure_restores_auto_state(implementations):
    def scenario(impl):
        model, trace = _model(impl, "image")
        impl.image_namespace["_coerce_contrast_limits"] = lambda value: SimpleNamespace(
            contrast_limits=(0, 1)
        )

        def fail(**kwargs):
            trace.append(("refresh_failure", kwargs, model._auto_contrast))
            raise RuntimeError("refresh failed")

        model.refresh = fail
        error = _error(lambda: setattr(model, "contrast_limits", (0.2, 0.8)))
        return error, model._auto_contrast, _value(model._contrast_limits), tuple(trace)

    result = _compare(implementations, scenario)
    assert result[0] == ("RuntimeError", "refresh failed") and result[1]
    assert result[3][-1] == (
        "refresh_failure",
        {"highlight": False, "extent": False},
        False,
    )


@pytest.mark.parametrize("case", ("change", "reentry_failure"))
def test_loaded_change_only_notification_and_reentry(implementations, case):
    def scenario(impl):
        model = impl.state()
        model._load_state = impl.native.LayerLoadState()
        model._loaded = True
        trace = []

        def emit():
            trace.append(model._loaded)
            if case == "reentry_failure":
                model._set_loaded(model._loaded)
                raise RuntimeError("loaded callback")

        model.loaded_data = SimpleNamespace(emit=emit)
        model._set_loaded(True)
        error = _error(lambda: model._set_loaded(False))
        return error, model._loaded, tuple(trace)

    result = _compare(implementations, scenario)
    assert not result[1] and result[2] == (False,)
    assert result[0] == (
        ("RuntimeError", "loaded callback") if case == "reentry_failure" else None
    )


def test_sequence_validator_real_callable_and_generator_exemption(implementations):
    def scenario(impl):
        validator = impl.namespace["validate_n_seq"](2, int)
        generator = (x for x in ("bad",))
        outcomes = [
            _error(lambda value=value: validator(value))
            for value in ([1, 2], [1], [1, "bad"], 3, generator)
        ]
        return (
            isinstance(validator, types.FunctionType),
            str(inspect.signature(validator)),
            validator.__doc__,
            tuple(outcomes),
            next(generator),
        )

    result = _compare(implementations, scenario)
    assert result[0] and result[1] == "(obj)" and result[4] == "bad"
    assert [None if e is None else e[0] for e in result[3]] == [
        None,
        "ValueError",
        "TypeError",
        "TypeError",
        None,
    ]


def test_pairwise_builtin_zip_eager_first_read_and_lazy_error(implementations):
    def scenario(impl):
        trace = []

        def values():
            trace.append("first")
            yield 1
            trace.append("second")
            yield 2
            trace.append("failure")
            raise RuntimeError("iterator failed")

        iterator = impl.namespace["_pairwise"](values())
        eager = tuple(trace)
        pair = next(iterator)
        error = _error(lambda: next(iterator))
        return isinstance(iterator, zip), eager, pair, error, tuple(trace)

    result = _compare(implementations, scenario)
    assert result == (
        True,
        ("first",),
        (1, 2),
        ("RuntimeError", "iterator failed"),
        ("first", "second", "failure"),
    )


def test_increasing_short_circuit_preserves_comparison_and_consumption(implementations):
    def scenario(impl):
        trace = []

        class Value:
            def __init__(self, number):
                self.number = number

            def __ge__(self, other):
                trace.append(("compare", self.number, other.number))
                return self.number >= other.number

        class Values:
            def __iter__(self):
                for number in (1, 3, 2, 9):
                    trace.append(("yield", number))
                    yield Value(number)

            def __str__(self):
                return "named values"

        error = _error(lambda: impl.namespace["_validate_increasing"](Values()))
        return error, tuple(trace)

    result = _compare(implementations, scenario)
    assert result[0] == (
        "ValueError",
        "Sequence named values must be monotonically increasing.",
    )
    assert result[1] == (
        ("yield", 1),
        ("yield", 3),
        ("compare", 1, 3),
        ("yield", 2),
        ("compare", 3, 2),
    )


def test_increasing_declared_any_boundary_raw_results_and_close(implementations):
    def scenario(impl):
        trace = []

        class Comparison:
            def __ge__(self, other):
                return "raw comparison"

        def any_provider(iterator):
            trace.append(next(iterator))
            iterator.close()
            trace.append(_error(lambda: next(iterator)))
            return False

        impl.namespace["any"] = any_provider
        error = _error(
            lambda: impl.namespace["_validate_increasing"]([Comparison(), Comparison()])
        )
        return error, tuple(trace)

    result = _compare(implementations, scenario)
    assert result == (None, ("raw comparison", ("StopIteration", "")))


def test_float_format_default_is_read_fresh_and_errors_preserved(implementations):
    def scenario(impl):
        formatter = impl.namespace["format_float"]
        first = formatter(1.23456)
        impl.namespace["DEFAULT_PRECISION"] = 5
        second = formatter(1.23456)
        return (
            first,
            second,
            formatter(1.25, 2, "f"),
            _error(lambda: formatter(1.0, 2, "bad")),
        )

    result = _compare(implementations, scenario)
    assert result[:3] == ("1.23", "1.2346", "1.25") and result[3][0] == "ValueError"


HIERARCHY = (
    ("layers/base/base.py", ("PostInit", "_LayerSlicingState", "Layer")),
    ("layers/intensity_mixin.py", ("IntensityVisualizationMixin",)),
    (
        "layers/_scalar_field/scalar_field.py",
        ("ScalarFieldBase", "ScalarFieldSlicingState"),
    ),
    ("layers/image/image.py", ("Image", "_ImageSlicingState")),
    ("layers/labels/labels.py", ("Labels", "_LabelsSlicingState")),
    ("layers/points/points.py", ("Points", "_PointsSlicingState")),
    ("layers/shapes/shapes.py", ("Shapes", "_ShapesSlicingState")),
    ("layers/surface/surface.py", ("Surface", "_SurfaceSlicingState")),
    ("layers/tracks/tracks.py", ("Tracks", "_TracksSlicingState")),
    ("layers/vectors/vectors.py", ("Vectors", "_VectorsSlicingState")),
)


@pytest.fixture(scope="module")
def hierarchies(implementations):
    from naparixx import _native

    def build(root):
        helpers = implementations(root).namespace
        cmap_module = importlib.import_module("napari.utils.colormaps.colormap")
        cmap_util = importlib.import_module("napari.utils.colormaps.colormap_utils")
        cmap_namespace = dict(vars(cmap_module))
        cmap_namespace["BackendColormap"] = cmap_module.Colormap
        map_model = _shell(
            root,
            "utils/colormaps/colormap.py",
            "Colormap",
            ("map",),
            cmap_namespace,
            ("BackendColormap",),
        )

        def independent_map(value):
            if type(value) is cmap_module.Colormap:
                return map_model(display_name=value._display_name, **value.model_dump())
            # Accepted cyclic/direct label maps are outside these 17 bodies.
            return value

        def ensure_map(value):
            return independent_map(cmap_util.ensure_colormap(value))

        classes = {"Colormap": map_model}
        namespaces = []
        ported = any(
            ast.dump(b)
            == ast.dump(ast.parse("_native.LayerNamespaceBase", mode="eval").body)
            for b in _class(_source(CURRENT, "layers/base/base.py"), "Layer").bases
        )
        for relative, names in HIERARCHY:
            module = importlib.import_module(
                "napari." + relative[:-3].replace("/", ".")
            )
            namespace = dict(vars(module))
            namespace.update(
                {
                    name: helpers[name]
                    for name in (
                        "validate_2_tuple",
                        "_validate_increasing",
                        "format_float",
                        "convert_to_uint8",
                    )
                }
            )
            namespace.update(classes)
            if "ensure_colormap" in namespace:
                namespace["ensure_colormap"] = ensure_map
            if "AVAILABLE_COLORMAPS" in namespace:
                namespace["AVAILABLE_COLORMAPS"] = {
                    key: independent_map(value)
                    for key, value in namespace["AVAILABLE_COLORMAPS"].items()
                }
            source_root = (
                BEFORE if root == PINNED and (BEFORE / relative).exists() else CURRENT
            )
            source = _source(source_root, relative)
            for name in names:
                node = copy.deepcopy(_class(source, name))
                owned_names = OWNED.get((relative, name), ())
                if root == PINNED and owned_names:
                    replacements = iter(
                        _methods(_source(PINNED, relative), name, owned_names)
                    )
                    node.body = [
                        next(replacements)
                        if isinstance(n, ast.FunctionDef) and n.name in owned_names
                        else n
                        for n in node.body
                    ]
                    assert next(replacements, None) is None
                if (
                    root == CURRENT
                    and not ported
                    and name in ("Layer", "_LayerSlicingState")
                ):
                    before = copy.deepcopy(node)
                    node.bases.insert(
                        0, ast.parse("_native.VispyNamespaceBase", mode="eval").body
                    )
                    reverse = copy.deepcopy(node)
                    reverse.bases.pop(0)
                    assert ast.dump(reverse) == ast.dump(before)
                    namespace["_native"] = _native
                _execute(source, [node], namespace, str(source_root / relative))
                classes[name] = namespace[name]
                for previous in namespaces:
                    previous[name] = namespace[name]
            namespaces.append(namespace)
        owner = _native.LayerNamespaceBase if ported else _native.VispyNamespaceBase
        if root == CURRENT:
            assert issubclass(classes["Layer"], owner)
            assert issubclass(classes["_LayerSlicingState"], owner)
        return SimpleNamespace(
            classes=classes, namespaces=namespaces, candidate=owner, root=root
        )

    return build


def _payload(kind):
    return {
        "Image": lambda: np.arange(6, dtype=np.float32).reshape(2, 3),
        "Labels": lambda: np.array([[0, 1], [1, 0]], dtype=np.uint8),
        "Points": lambda: np.array([[0, 0], [1, 1]], dtype=float),
        "Shapes": lambda: [np.array([[0, 0], [0, 1], [1, 1], [1, 0]], dtype=float)],
        "Surface": lambda: (
            np.array([[0, 0], [0, 1], [1, 0]], dtype=float),
            np.array([[0, 1, 2]]),
        ),
        "Tracks": lambda: np.array([[0, 0, 0, 0], [0, 1, 1, 1]], dtype=float),
        "Vectors": lambda: np.array([[[0, 0], [1, 1]]], dtype=float),
    }[kind]()


@pytest.mark.parametrize(
    "kind", ("Image", "Labels", "Points", "Shapes", "Surface", "Tracks", "Vectors")
)
def test_real_seven_layer_constructors_and_slicing_state_live_dictionary(
    hierarchies, kind
):
    def scenario(root):
        impl = hierarchies(root)
        layer = impl.classes[kind](_payload(kind))
        state = layer._slicing_state
        saved = vars(layer)
        saved_state = vars(state)
        layer._namespace_probe = object()
        state._namespace_probe = object()
        assert saved is layer.__dict__ and saved_state is state.__dict__
        assert saved["_namespace_probe"] is layer._namespace_probe
        assert saved_state["_namespace_probe"] is state._namespace_probe
        saved["_namespace_probe"] = "raw-layer"
        saved_state["_namespace_probe"] = "raw-state"
        if root == CURRENT:
            assert isinstance(layer, impl.candidate) and isinstance(
                state, impl.candidate
            )
        return (
            type(layer).__name__,
            type(state).__name__,
            layer._namespace_probe,
            state._namespace_probe,
            layer.ndim,
            weakref.ref(layer)() is layer,
            weakref.ref(state)() is state,
            bool(layer.thumbnail.size),
        )

    expected = scenario(PINNED)
    observed = scenario(CURRENT)
    assert observed == expected
    assert observed[0] == kind and observed[5:] == (True, True, True)


def test_real_namespace_replacement_saved_alias_and_descriptor_precedence(hierarchies):
    def scenario(root):
        impl = hierarchies(root)
        layer = impl.classes["Image"](_payload("Image"))
        state = layer._slicing_state
        old = layer.__dict__
        replacement = dict(old)
        replacement["user_value"] = 3
        layer.__dict__ = replacement
        replacement["user_value"] = 4
        old["user_value"] = 8
        replacement["thumbnail"] = "descriptor shadow"
        retained_thumbnail = layer.thumbnail is replacement["_thumbnail"]
        old_state = state.__dict__
        new_state = dict(old_state)
        state.__dict__ = new_state
        new_state["loaded"] = "descriptor shadow"
        state._set_loaded(False)
        return (
            layer.__dict__ is replacement,
            old is not replacement,
            layer.user_value,
            old["user_value"],
            retained_thumbnail,
            state.__dict__ is new_state,
            state.loaded,
            old_state is not new_state,
        )

    assert (
        scenario(CURRENT)
        == scenario(PINNED)
        == (True, True, 4, 8, True, True, False, True)
    )


def test_real_postinit_abc_and_cooperative_subclass_descriptor_gc(hierarchies):
    def scenario(root):
        impl = hierarchies(root)
        base = impl.classes["Image"]
        trace = []

        class Subclass(base):
            def __init__(self, data):
                trace.append("init")
                super().__init__(data)

            def _post_init(self):
                trace.append("post")
                super()._post_init()

            @property
            def plugin_value(self):
                return self._plugin_value

            @plugin_value.setter
            def plugin_value(self, value):
                self._plugin_value = value * 2

        layer = Subclass(_payload("Image"))
        layer.plugin_value = 3
        vars(layer)["plugin_value"] = 99
        result = (
            layer.plugin_value,
            tuple(trace),
            _error(lambda: impl.classes["Layer"](None)),
            _error(lambda: impl.classes["_LayerSlicingState"](None, None, None)),
        )
        layer._namespace_cycle = layer
        reference = weakref.ref(layer)
        del layer
        gc.collect()
        return result, reference() is None

    expected = scenario(PINNED)
    observed = scenario(CURRENT)
    assert observed == expected
    assert observed[0][0:2] == (6, ("init", "post"))
    assert observed[0][2][0] == observed[0][3][0] == "TypeError"
    assert observed[1]


def test_real_dictionary_deletion_restoration_and_copy_protocol(hierarchies):
    def scenario(root):
        impl = hierarchies(root)
        layer = impl.classes["Image"](_payload("Image"))
        saved = vars(layer)
        del layer.__dict__
        empty = vars(layer)
        detached = not empty and saved is not empty and "_thumbnail" in saved
        layer.__dict__ = saved
        copied = copy.copy(layer)
        # Layer.create deliberately calls the actual application's factory;
        # compare its documented copied data/metadata, not clone type identity.
        return (
            detached,
            vars(layer) is saved,
            _value(copied.data),
            copied.data is layer.data,
            copied.name == layer.name,
            copied.opacity == layer.opacity,
        )

    expected = scenario(PINNED)
    observed = scenario(CURRENT)
    assert observed == expected
    assert (
        observed[0:2] == (True, True)
        and not observed[3]
        and observed[4:6] == (True, True)
    )


def test_owned_method_metadata_and_independent_hierarchies(implementations):
    old, new = implementations(PINNED), implementations(CURRENT)
    pairs = [
        (old.layer.thumbnail.fget, new.layer.thumbnail.fget),
        (old.layer.thumbnail.fset, new.layer.thumbnail.fset),
        (old.mixin.contrast_limits.fget, new.mixin.contrast_limits.fget),
        (old.mixin.contrast_limits.fset, new.mixin.contrast_limits.fset),
        (old.mixin.contrast_limits_range.fget, new.mixin.contrast_limits_range.fget),
        (old.mixin.contrast_limits_range.fset, new.mixin.contrast_limits_range.fset),
        (old.image.contrast_limits.fset, new.image.contrast_limits.fset),
        (old.image._update_thumbnail, new.image._update_thumbnail),
        (old.labels._update_thumbnail, new.labels._update_thumbnail),
        (old.state._set_loaded, new.state._set_loaded),
        (old.cmap.map, new.cmap.map),
    ]
    pairs += [
        (old.namespace[name], new.namespace[name])
        for name in (
            "validate_n_seq",
            "_pairwise",
            "_validate_increasing",
            "format_float",
            "convert_to_uint8",
        )
    ]
    pairs.append(
        (old.namespace["validate_n_seq"](2), new.namespace["validate_n_seq"](2))
    )
    assert len(pairs) == 17
    for original, current in pairs:
        assert (
            original is not current and original.__globals__ is not current.__globals__
        )
        assert original.__name__ == current.__name__
        assert original.__qualname__ == current.__qualname__
        assert original.__doc__ == current.__doc__
        assert str(inspect.signature(original)) == str(inspect.signature(current))
