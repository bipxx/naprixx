"""Finite comparisons against fourteen unchanged napari 0.9.2 bodies.

Only the named bodies are compiled from the pinned source.  Each oracle has
its own namespace, and every helper converted in this packet is rebound to
its pinned counterpart.  NumPy, dask, typing metadata, and caller-provided
protocol callbacks remain external dependencies.  These cases do not create
an Image, QApplication, canvas, or asynchronous slicing task.
"""

from __future__ import annotations

import ast
import gc
import hashlib
import importlib
import weakref
from pathlib import Path
from types import SimpleNamespace

import numpy as np
import pytest

ROOT = Path(__file__).resolve().parents[1]
UPSTREAM = ROOT / ".upstream/napari/src/napari"
PINNED_COMMIT = "595639295a153c84bcb127a9e07d801355440a35"
PINNED = {
    "layers/utils/layer_utils.py":
        "6d3ed1151813da2cc39a1d476fb0b13cd712e1b741c0a42fb02d7b7f6a244d6d",
    "utils/_dtype.py":
        "7a03235fe5f89b86fd1b8f110eccb02f7a2afd9782cdf3df7f85e39be33320ff",
    "utils/colormaps/colormap_utils.py":
        "0de47b92f4f0a90af004a1918b62b835cb1afb7eae3e1014f63829c2a12fbf98",
    "layers/image/image.py":
        "73027b2f096445c6aea88689bbf29f8707a75847d1348fa3b80eaf867c728c56",
    "layers/intensity_mixin.py":
        "9d3a6aa09b010a133ccd63f237692f79e4013bf7b69f2a10b5d590237240e034",
}


def _body(relative, owner, name):
    raw = (UPSTREAM / relative).read_bytes()
    assert hashlib.sha256(raw).hexdigest() == PINNED[relative]
    source = raw.decode("utf-8")
    tree = ast.parse(source)
    members = tree.body
    if owner is not None:
        members = next(node.body for node in members
                       if isinstance(node, ast.ClassDef) and node.name == owner)
    original = next(node for node in members
                    if isinstance(node, ast.FunctionDef) and node.name == name)
    text = "".join(source.splitlines(keepends=True)[
        original.lineno - 1:original.end_lineno
    ])
    wrapped = text if owner is None else "class PinnedOwner:\n" + text
    extracted = ast.parse(wrapped).body[0]
    if owner is not None:
        extracted = extracted.body[0]
    assert ast.dump(extracted) == ast.dump(original)
    return text


def _function(relative, name, namespace):
    text = _body(relative, None, name)
    exec(compile("from __future__ import annotations\n" + text,
                 str(UPSTREAM / relative), "exec"), namespace)
    return namespace[name]


def _method(relative, owner, name, namespace):
    text = _body(relative, owner, name)
    exec(compile("from __future__ import annotations\n"
                 "class PinnedOwner:\n" + text,
                 str(UPSTREAM / relative), "exec"), namespace)
    return vars(namespace["PinnedOwner"])[name]


@pytest.fixture(scope="module")
def implementations():
    ranges = importlib.import_module("napari.layers.utils.layer_utils")
    dtypes = importlib.import_module("napari.utils._dtype")
    colormaps = importlib.import_module("napari.utils.colormaps.colormap_utils")
    image = importlib.import_module("napari.layers.image.image")
    intensity = importlib.import_module("napari.layers.intensity_mixin")
    range_globals = dict(vars(ranges))
    dtype_globals = dict(vars(dtypes))
    colormap_globals = dict(vars(colormaps))
    image_globals = dict(vars(image))
    reset_globals = dict(vars(intensity))
    for name in ("_nanmin", "_nanmax", "calc_data_range"):
        _function("layers/utils/layer_utils.py", name, range_globals)
    for name in ("_normalize_str_by_bit_depth", "normalize_dtype",
                 "get_dtype_limits"):
        _function("utils/_dtype.py", name, dtype_globals)
    coerce_data = _method(
        "utils/colormaps/colormap_utils.py", "CoercedContrastLimits",
        "coerce_data", colormap_globals,
    )
    coerced = type("PinnedCoercedContrastLimits",
                   (colormaps.CoercedContrastLimits,), {
                       "coerce_data": coerce_data,
                   })
    colormap_globals["CoercedContrastLimits"] = coerced
    for name in ("scale_down", "scale_up", "_coerce_contrast_limits"):
        _function("utils/colormaps/colormap_utils.py", name, colormap_globals)
    image_globals["calc_data_range"] = range_globals["calc_data_range"]
    image_globals["_coerce_contrast_limits"] = (
        colormap_globals["_coerce_contrast_limits"]
    )
    reset_globals["normalize_dtype"] = dtype_globals["normalize_dtype"]
    pinned = SimpleNamespace(
        range=range_globals["calc_data_range"], range_globals=range_globals,
        normalize=dtype_globals["normalize_dtype"],
        limits=dtype_globals["get_dtype_limits"],
        coerce=colormap_globals["_coerce_contrast_limits"],
        coerce_data=coerce_data, coerced=coerced,
        colormap_globals=colormap_globals, image_globals=image_globals,
        image_range=_method("layers/image/image.py", "Image",
                            "_calc_data_range", image_globals),
        displayed=_method("layers/image/image.py", "Image",
                          "_raw_to_displayed", image_globals),
        reset=_method("layers/intensity_mixin.py", "IntensityVisualizationMixin",
                      "reset_contrast_limits", reset_globals),
        reset_range=_method(
            "layers/intensity_mixin.py", "IntensityVisualizationMixin",
            "reset_contrast_limits_range", reset_globals,
        ),
    )
    current = SimpleNamespace(
        range=ranges.calc_data_range, range_globals=vars(ranges),
        normalize=dtypes.normalize_dtype, limits=dtypes.get_dtype_limits,
        coerce=colormaps._coerce_contrast_limits,
        coerce_data=colormaps.CoercedContrastLimits.coerce_data,
        coerced=colormaps.CoercedContrastLimits,
        colormap_globals=vars(colormaps), image_globals=vars(image),
        image_range=image.Image._calc_data_range,
        displayed=image.Image._raw_to_displayed,
        reset=intensity.IntensityVisualizationMixin.reset_contrast_limits,
        reset_range=(
            intensity.IntensityVisualizationMixin.reset_contrast_limits_range
        ),
    )
    # None of the oracle's packet dependencies may point at a current helper.
    assert pinned.range.__globals__["_nanmin"] is range_globals["_nanmin"]
    assert pinned.range.__globals__["_nanmax"] is range_globals["_nanmax"]
    assert pinned.limits.__globals__["normalize_dtype"] is pinned.normalize
    assert pinned.normalize.__globals__["_normalize_str_by_bit_depth"] is (
        dtype_globals["_normalize_str_by_bit_depth"]
    )
    assert pinned.coerce.__globals__["CoercedContrastLimits"] is coerced
    assert pinned.coerce.__globals__["scale_down"] is (
        colormap_globals["scale_down"]
    )
    assert pinned.coerce.__globals__["scale_up"] is colormap_globals["scale_up"]
    assert pinned.image_range.__globals__["calc_data_range"] is pinned.range
    assert pinned.displayed.__globals__["_coerce_contrast_limits"] is pinned.coerce
    assert pinned.reset_range.__globals__["normalize_dtype"] is pinned.normalize
    assert pinned.range is not current.range
    assert pinned.normalize is not current.normalize
    assert pinned.coerce is not current.coerce
    return pinned, current


def _compare(implementations, scenario, contract):
    pinned, current = implementations
    expected = scenario(pinned)
    assert expected == contract
    assert scenario(current) == expected


def _error(call):
    try:
        call()
    except Exception as error:
        return type(error).__name__, str(error)
    raise AssertionError("Expected provider/algorithm error")


@pytest.mark.parametrize("case", [
    "float16", "big-endian", "nonfinite", "constant", "wide-integers",
])
def test_range_numeric_extrema_preserve_pinned_order(implementations, case):
    arrays = {
        "float16": [np.array([-2, 0, 4], dtype=np.float16)],
        "big-endian": [np.array([-7, 3], dtype=">i2")],
        "nonfinite": [np.array([np.nan, np.inf, -np.inf]),
                      np.array([np.nan, -7, 4, np.inf, -np.inf])],
        "constant": [np.array([-5, -5]), np.array([5, 5]), np.array([0, 0])],
        "wide-integers": [
            np.array([2**63 + 1, 2**63 + 2], dtype=np.uint64),
            np.array([-2**63, -2**63 + 1], dtype=np.int64),
        ],
    }[case]
    contracts = {
        "float16": ((-2.0, 4.0),),
        "big-endian": ((-7.0, 3.0),),
        "nonfinite": ((0.0, 1.0), (-7.0, 4.0)),
        "constant": ((-5.0, 1.0), (0.0, 5.0), (0.0, 1.0)),
        "wide-integers": ((float(2**63), float(2**63)),
                          (-float(2**63), -float(2**63))),
    }
    _compare(implementations, lambda impl:
             tuple(impl.range(data) for data in arrays), contracts[case])


def test_range_empty_failure_and_uint8_empty_shortcut(implementations):
    def scenario(impl):
        failure = _error(lambda: impl.range(np.empty((0, 3))))
        empty_uint8 = impl.range(np.empty(0, dtype=np.uint8))
        return failure, empty_uint8

    _compare(implementations, scenario, (
        ("ValueError", "zero-size array to reduction operation minimum "
         "which has no identity"), (0, 255),
    ))


def test_range_uint8_override_precedes_provider_attributes(implementations):
    def scenario(impl):
        trace = []

        class UntouchedProvider:
            def __getattribute__(self, name):
                trace.append(name)
                raise AssertionError("uint8 override accessed provider")

        result = impl.range(UntouchedProvider(), dtype=np.dtype(np.uint8))
        return result, tuple(trace)

    _compare(implementations, scenario, ((0, 255), ()))


def test_range_ndarray_subclass_reductions_dispatch_to_provider(implementations):
    def scenario(impl):
        trace = []

        class ReductionArray(np.ndarray):
            def __array_function__(self, function, types, args, kwargs):
                if function is np.min:
                    trace.append("min")
                    return np.int64(-9)
                if function is np.max:
                    trace.append("max")
                    return np.int64(12)
                return super().__array_function__(function, types, args, kwargs)

        data = np.array([0, 1]).view(ReductionArray)
        result = impl.range(data)
        return result, tuple(trace)

    _compare(implementations, scenario, ((-9.0, 12.0), ("min", "max")))


def _index_key(index):
    if isinstance(index, slice):
        return ("slice", index.start, index.stop, index.step)
    return tuple(_index_key(value) if isinstance(value, slice) else value
                 for value in index)


@pytest.mark.parametrize("branch", ["line", "plane", "patch"])
def test_range_sampling_preserves_indices_reduction_and_compute_order(
    implementations, monkeypatch, branch,
):
    shapes = {"line": (20_000_000,), "plane": (11, 1000, 1000),
              "patch": (3, 4000, 4000)}
    keys = {
        "line": [("slice", 0, 4096, None),
                 ("slice", 9_997_952, 10_002_048, None),
                 ("slice", -4096, None, None)],
        "plane": [(0,), (5,), (10,)],
        "patch": [(plane, ("slice", 1969, 2031, None),
                   ("slice", 1969, 2031, None)) for plane in (0, 1, 2)],
    }[branch]

    def scenario(impl):
        trace = []

        class Sample(np.ndarray):
            def __array_function__(self, function, types, args, kwargs):
                if function is np.max:
                    trace.append(("max", self.key))
                    return 20
                if function is np.min:
                    trace.append(("min", self.key))
                    return -10
                return super().__array_function__(function, types, args, kwargs)

        class Provider:
            dtype = np.dtype("int16")
            shape = shapes[branch]
            ndim = len(shape)
            size = int(np.prod(shape))

            def __getitem__(self, index):
                key = _index_key(index)
                trace.append(("getitem", key))
                sample = np.array([-10, 20]).view(Sample)
                sample.key = key
                return sample

        def compute(*values):
            trace.append(("compute",))
            assert values == ([20, 20, 20], [-10, -10, -10])
            return values

        with monkeypatch.context() as patch:
            patch.setitem(impl.range_globals, "dask", SimpleNamespace(
                compute=compute,
            ))
            result = impl.range(Provider())
        return result, tuple(trace)

    trace = tuple(event for reduction in ("max", "min") for key in keys
                  for event in (("getitem", key), (reduction, key)))
    if branch != "line":
        trace += (("compute",),)
    _compare(implementations, scenario, ((-10.0, 20.0), trace))


def test_dtype_existing_objects_keep_identity_and_endianness(implementations):
    values = (np.dtype(">f8"), np.dtype(np.float16))
    _compare(implementations, lambda impl:
             tuple(impl.normalize(value) is value for value in values),
             (True, True))


def test_dtype_duck_strings_preserve_depth_and_one_provider_read(implementations):
    tokens = ("duck.uint16", "duck.float16", "duck.complex128",
              "duck.float", "duck.bool")

    def scenario(impl):
        trace = []

        class Spec:
            def __init__(self, text):
                self.text = text

            def __str__(self):
                trace.append(self.text)
                return self.text

        results = tuple(impl.normalize(Spec(text)).name for text in tokens)
        return results, tuple(trace)

    _compare(implementations, scenario, (
        ("uint16", "float16", "complex128", "float64", "bool"), tokens,
    ))


def test_dtype_errors_keep_exception_types_and_formatting_reads(implementations):
    def scenario(impl):
        results = []

        class Spec:
            def __init__(self, token, trace):
                self.token, self.trace = token, trace

            def __str__(self):
                self.trace.append(self.token)
                return self.token

        for token in ("opaque", "duck.float24", "duck.float128"):
            trace = []
            failure = _error(lambda token=token, trace=trace:
                             impl.normalize(Spec(token, trace)))
            results.append((failure, tuple(trace)))
        return tuple(results)

    _compare(implementations, scenario, (
        (("ValueError", "Unrecognized dtype: opaque"), ("opaque", "opaque")),
        (("ValueError", "Unrecognized bit depth in dtype: duck.float24"),
         ("duck.float24",)),
        (("KeyError", "128"), ("duck.float128",)),
    ))


def test_dtype_limits_preserve_wide_integer_float16_and_endian_values(
    implementations,
):
    values = ("uint64", np.dtype(">i2"), np.dtype(np.float16))
    _compare(implementations, lambda impl:
             tuple(impl.limits(value) for value in values), (
                 (0.0, float(2**64)), (-32768.0, 32767.0),
                 (-65504.0, 65504.0),
             ))


def test_dtype_limits_reject_bool_and_complex_after_normalization(implementations):
    _compare(implementations, lambda impl: tuple(
        _error(lambda value=value: impl.limits(value))
        for value in ("bool", "complex64")
    ), (
        ("TypeError", "Unrecognized or non-numeric dtype: bool"),
        ("TypeError", "Unrecognized or non-numeric dtype: complex64"),
    ))


@pytest.mark.parametrize("branch", ["identity", "down", "up"])
def test_contrast_limits_preserve_three_numeric_branches(implementations, branch):
    limits = {
        "identity": (-3.0, 9.0),
        "down": (-float(2**128), float(2**128)),
        "up": (1.0, 1.0 + 2**-20),
    }[branch]
    contracts = {
        "identity": ((-3.0, 9.0), 0.0, 1.0, True),
        "down": ((-4.2535293329816107e37, 4.2535293329816107e37),
                 0.0, 0.1249999925494194, False),
        "up": ((0.0, 1000.0), -1_048_576_000.0, 1_048_576_000.0, False),
    }

    def scenario(impl):
        result = impl.coerce(limits)
        return (tuple(float(value) for value in result.contrast_limits),
                float(result.offset), float(result.scale),
                result.contrast_limits is limits)

    _compare(implementations, scenario, contracts[branch])


@pytest.mark.parametrize("branch", ["down", "up"])
def test_coerce_data_preserves_arithmetic_and_fresh_attribute_order(
    implementations, branch,
):
    def scenario(impl):
        trace = []

        class Arithmetic:
            def __mul__(self, value):
                trace.append(("multiply", value))
                return self

            def __add__(self, value):
                trace.append(("add", value))
                return self

        class Parameters:
            @property
            def scale(self):
                trace.append("scale")
                return 0.5 if branch == "down" else 2

            @property
            def offset(self):
                trace.append("offset")
                return 3 if branch == "down" else 6

        data = Arithmetic()
        result = impl.coerce_data(Parameters(), data)
        return tuple(trace), result is data

    contracts = {
        "down": (("scale", "scale", ("multiply", 0.5), "offset", ("add", 3)),
                 True),
        "up": (("scale", "offset", "scale", ("add", 3.0), "scale",
                ("multiply", 2)), True),
    }
    _compare(implementations, scenario, contracts[branch])


def test_contrast_coercion_preserves_global_factory_and_helper_overrides(
    implementations, monkeypatch,
):
    def scenario(impl):
        trace = []
        identity_limits, sentinel = (-3.0, 9.0), object()

        def factory(limits, offset, scale):
            trace.append(("factory", limits is identity_limits, offset, scale))
            return sentinel

        def up(limits):
            trace.append(("up", limits == (1.0, 1.0 + 2**-20)))
            return sentinel

        def down(limits):
            trace.append(("down", limits == (-float(2**128), float(2**128))))
            return sentinel

        with monkeypatch.context() as patch:
            patch.setitem(impl.colormap_globals, "CoercedContrastLimits", factory)
            patch.setitem(impl.colormap_globals, "scale_up", up)
            patch.setitem(impl.colormap_globals, "scale_down", down)
            results = (impl.coerce(identity_limits),
                       impl.coerce((1.0, 1.0 + 2**-20)),
                       impl.coerce((-float(2**128), float(2**128))))
        return tuple(trace), tuple(value is sentinel for value in results)

    _compare(implementations, scenario, (
        (("factory", True, 0, 1), ("up", True), ("down", True)),
        (True, True, True),
    ))


@pytest.mark.parametrize("source", ["data", "multiscale", "slice"])
def test_image_range_preserves_source_and_provider_read_order(
    implementations, monkeypatch, source,
):
    def scenario(impl):
        trace = []
        raw, data, dtype = object(), object(), object()

        class Pyramid:
            def __getitem__(self, index):
                trace.append(("level", index))
                assert index == -1
                return raw

        class View:
            @property
            def raw(self):
                trace.append("raw")
                return raw

        class Slice:
            @property
            def image(self):
                trace.append("image")
                return View()

        class Layer:
            @property
            def multiscale(self):
                trace.append("multiscale")
                return source == "multiscale"

            @property
            def data(self):
                trace.append("data")
                return Pyramid() if source == "multiscale" else data

            @property
            def _slice(self):
                trace.append("slice")
                return Slice()

            @property
            def rgb(self):
                trace.append("rgb")
                return False

            @property
            def dtype(self):
                trace.append("dtype")
                return dtype

        def calculate(value, *, rgb, dtype):
            trace.append(("calculate", value is (data if source == "data" else raw),
                          rgb is False, dtype is expected_dtype))
            return (-11.0, 18.0)

        expected_dtype = dtype
        with monkeypatch.context() as patch:
            patch.setitem(impl.image_globals, "calc_data_range", calculate)
            result = impl.image_range(Layer(), "slice" if source == "slice"
                                      else "data")
        return result, tuple(trace)

    prefixes = {
        "data": ("multiscale", "data"),
        "multiscale": ("multiscale", "data", ("level", -1)),
        "slice": ("slice", "image", "raw"),
    }
    trace = prefixes[source] + ("rgb", "dtype", ("calculate", True, True, True))
    _compare(implementations, scenario, ((-11.0, 18.0), trace))


def test_image_range_invalid_mode_formats_provider_before_any_layer_read(
    implementations,
):
    def scenario(impl):
        trace = []

        class Mode:
            def __eq__(self, value):
                trace.append(("compare", value))
                return False

            def __repr__(self):
                trace.append("repr")
                return "<unknown-mode>"

        class UntouchedLayer:
            def __getattribute__(self, name):
                raise AssertionError("invalid mode accessed layer: " + name)

        failure = _error(lambda: impl.image_range(UntouchedLayer(), Mode()))
        return failure, tuple(trace)

    _compare(implementations, scenario, (
        ("ValueError", "mode must be either 'data' or 'slice', got <unknown-mode>"),
        (("compare", "data"), ("compare", "slice"), "repr"),
    ))


@pytest.mark.parametrize("change", [False, True])
def test_image_displayed_preserves_raw_identity_and_fresh_contrast_limits(
    implementations, monkeypatch, change,
):
    def scenario(impl):
        trace = []
        raw, displayed = object(), object()

        class Layer:
            limits = (0.0, 1.0)

            @property
            def contrast_limits(self):
                trace.append(("limits", self.limits))
                return self.limits

        layer = Layer()

        def coerce_data(value):
            trace.append(("coerce-data", value is raw))
            return displayed

        def coerce(limits):
            trace.append(("coerce", limits == (0.0, 1.0)))
            if change:
                layer.limits = (2.0, 3.0)
            return SimpleNamespace(contrast_limits=limits, coerce_data=coerce_data)

        with monkeypatch.context() as patch:
            patch.setitem(impl.image_globals, "_coerce_contrast_limits", coerce)
            result = impl.displayed(layer, raw)
        return tuple(trace), result is (displayed if change else raw)

    trace = (("limits", (0.0, 1.0)), ("coerce", True),
             ("limits", (2.0, 3.0) if change else (0.0, 1.0)))
    if change:
        trace += (("coerce-data", True),)
    _compare(implementations, scenario, (trace, True))


@pytest.mark.parametrize("failure", [None, "calculate", "publish"])
def test_reset_contrast_preserves_mode_and_publication_callback_boundaries(
    implementations, failure,
):
    """Publication callbacks are caller protocols, not reconstructed setters."""

    def scenario(impl):
        trace, bounds = [], object()

        class Mode:
            def __bool__(self):
                trace.append("mode-bool")
                return False

        class Layer:
            @property
            def _auto_contrast_source(self):
                trace.append("source")
                return "slice"

            @property
            def _calc_data_range(self):
                trace.append("calculate-lookup")
                return self.calculate

            def calculate(self, mode):
                trace.append(("calculate", mode))
                if failure == "calculate":
                    raise RuntimeError("range failed")
                self.publish = self.new_publish
                return bounds

            def new_publish(self):
                trace.append("thumbnail")
                self.events = SimpleNamespace(
                    contrast_limits=lambda: trace.append("fresh-event"),
                )
                self.events.contrast_limits()

            @property
            def contrast_limits(self):
                raise AssertionError("reset read contrast limits")

            @contrast_limits.setter
            def contrast_limits(self, value):
                trace.append(("publish", value is bounds))
                if failure == "publish":
                    raise RuntimeError("publication failed")
                self.publish()

        layer = Layer()
        layer.publish = lambda: trace.append("stale-publication")
        layer.events = SimpleNamespace(
            contrast_limits=lambda: trace.append("stale-event"),
        )
        if failure is None:
            result = impl.reset(layer, Mode())
            outcome = result is None
        else:
            outcome = _error(lambda: impl.reset(layer, Mode()))
        return tuple(trace), outcome

    prefix = ("mode-bool", "source", "calculate-lookup", ("calculate", "slice"))
    contracts = {
        None: (prefix + (("publish", True), "thumbnail", "fresh-event"), True),
        "calculate": (prefix, ("RuntimeError", "range failed")),
        "publish": (prefix + (("publish", True),),
                    ("RuntimeError", "publication failed")),
    }
    _compare(implementations, scenario, contracts[failure])


@pytest.mark.parametrize("kind", ["integer", "float", "invalid"])
def test_reset_range_preserves_wide_integer_and_post_dtype_fresh_reads(
    implementations, kind,
):
    """Dtype conversion may replace the following mode and range callbacks."""

    def scenario(impl):
        trace = []
        bounds = (-2.5, 3.5)
        token = {"integer": "duck.uint64", "float": "duck.float16",
                 "invalid": "opaque"}[kind]

        class Mode:
            def __bool__(self):
                trace.append("mode-bool")
                return False

        class Spec:
            def __str__(self):
                trace.append(("dtype-string", token))
                layer.source = "slice"
                layer.calculate = fresh_calculate
                return token

        def fresh_calculate(mode):
            trace.append(("calculate", mode))
            return bounds

        class Layer:
            source = "data"

            @property
            def dtype(self):
                trace.append("dtype")
                return Spec()

            @property
            def _auto_contrast_source(self):
                trace.append("source")
                return self.source

            @property
            def _calc_data_range(self):
                trace.append("calculate-lookup")
                return self.calculate

            @property
            def contrast_limits_range(self):
                raise AssertionError("reset read contrast range")

            @contrast_limits_range.setter
            def contrast_limits_range(self, value):
                trace.append(("publish", tuple(value),
                              tuple(type(item).__name__ for item in value)))
                self.events.contrast_limits_range()

        layer = Layer()
        layer.calculate = lambda mode: trace.append("stale-calculation")
        layer.events = SimpleNamespace(
            contrast_limits_range=lambda: trace.append("range-event"),
        )
        if kind == "invalid":
            outcome = _error(lambda: impl.reset_range(layer, Mode()))
        else:
            result = impl.reset_range(layer, Mode())
            outcome = result is None
        return tuple(trace), outcome

    contracts = {
        "integer": (("dtype", ("dtype-string", "duck.uint64"),
                     ("publish", (0, 2**64 - 1), ("int", "int")),
                     "range-event"), True),
        "float": (("dtype", ("dtype-string", "duck.float16"), "mode-bool",
                   "source", "calculate-lookup", ("calculate", "slice"),
                   ("publish", (-2.5, 3.5), ("float", "float")),
                   "range-event"), True),
        "invalid": (("dtype", ("dtype-string", "opaque"),
                     ("dtype-string", "opaque")),
                    ("ValueError", "Unrecognized dtype: opaque")),
    }
    _compare(implementations, scenario, contracts[kind])


@pytest.mark.parametrize("reset", ["contrast", "range"])
def test_reset_transient_falsy_mode_releases_before_fresh_calculation(
    implementations, reset,
):
    def scenario(impl):
        trace, references = [], []
        bounds = object()

        def fresh_calculate(mode):
            trace.append(("calculate", mode, references[0]() is None))
            return bounds

        class Layer:
            @property
            def dtype(self):
                trace.append("dtype")
                return np.dtype(np.float16)

            @property
            def _auto_contrast_source(self):
                trace.append("source")
                return "slice"

            @property
            def _calc_data_range(self):
                trace.append("calculate-lookup")
                return self.calculate

            def __setattr__(self, name, value):
                if name in {"contrast_limits", "contrast_limits_range"}:
                    trace.append(("publish", name, value is bounds))
                else:
                    object.__setattr__(self, name, value)

        layer = Layer()
        layer.calculate = lambda mode: trace.append("stale-calculation")
        target = weakref.ref(layer)

        class Mode:
            def __init__(self):
                references.append(weakref.ref(self))

            def __bool__(self):
                trace.append("mode-bool")
                return False

            def __del__(self):
                trace.append("mode-finalizer")
                current = target()
                if current is not None:
                    current.calculate = fresh_calculate

        method = impl.reset if reset == "contrast" else impl.reset_range
        # A rewritten assert around this call would itself retain Mode().
        result = method.__get__(layer, type(layer))(Mode())
        released = references[0]() is None
        return tuple(trace), released, result is None

    field = "contrast_limits" if reset == "contrast" else "contrast_limits_range"
    trace = ("mode-bool", "source", "mode-finalizer", "calculate-lookup",
             ("calculate", "slice", True), ("publish", field, True))
    if reset == "range":
        trace = ("dtype",) + trace
    _compare(implementations, scenario, (trace, True, True))


def test_image_range_callback_error_traceback_owns_selected_input(implementations):
    def scenario(impl):
        trace, references = [], []

        class Input:
            def __init__(self):
                references.append(weakref.ref(self))

            def __del__(self):
                trace.append("input-finalizer")

        class Layer:
            @property
            def multiscale(self):
                trace.append("multiscale")
                return False

            @property
            def data(self):
                trace.append("data")
                return Input()

            @property
            def rgb(self):
                trace.append(("rgb", references[0]() is not None))
                raise RuntimeError("rgb provider failed")

            @property
            def dtype(self):
                raise AssertionError("rgb failure must precede dtype read")

        layer = Layer()

        def invoke():
            try:
                impl.image_range.__get__(layer, type(layer))("data")
            except RuntimeError as error:
                return str(error), error.__traceback__
            raise AssertionError("Expected rgb provider failure")

        message, saved_traceback = invoke()
        retained = references[0]() is not None
        before_drop = tuple(trace)
        del saved_traceback
        gc.collect()
        released = references[0]() is None
        return before_drop, message, retained, released

    _compare(implementations, scenario, (
        ("multiscale", "data", ("rgb", True)), "rgb provider failed", True, True,
    ))


def test_image_displayed_callback_error_traceback_owns_coerced_information(
    implementations, monkeypatch,
):
    def scenario(impl):
        trace, references = [], []

        class Information:
            def __init__(self):
                references.append(weakref.ref(self))

            @property
            def contrast_limits(self):
                trace.append("fixed-limits")
                return (0.0, 1.0)

            def __del__(self):
                trace.append("information-finalizer")

        class Layer:
            reads = 0

            @property
            def contrast_limits(self):
                self.reads += 1
                if self.reads == 1:
                    trace.append(("limits", 1))
                    return (0.0, 1.0)
                trace.append(("limits", 2, references[0]() is not None))
                raise RuntimeError("contrast provider failed")

        def coerce(limits):
            assert limits == (0.0, 1.0)
            trace.append("coerce")
            return Information()

        layer, raw = Layer(), object()

        def invoke():
            try:
                impl.displayed.__get__(layer, type(layer))(raw)
            except RuntimeError as error:
                return str(error), error.__traceback__
            raise AssertionError("Expected contrast provider failure")

        with monkeypatch.context() as patch:
            patch.setitem(impl.image_globals, "_coerce_contrast_limits", coerce)
            message, saved_traceback = invoke()
            retained = references[0]() is not None
            before_drop = tuple(trace)
            del saved_traceback
            gc.collect()
            released = references[0]() is None
        return before_drop, message, retained, released

    _compare(implementations, scenario, (
        (("limits", 1), "coerce", "fixed-limits", ("limits", 2, True)),
        "contrast provider failed", True, True,
    ))


@pytest.mark.parametrize("factory_result", ["instance", "class", "invalid"])
def test_image_invalid_mode_preserves_dynamic_error_factory_raise_contract(
    implementations, monkeypatch, factory_result,
):
    message = "mode must be either 'data' or 'slice', got <factory-mode>"

    def scenario(impl):
        trace = []

        class SelectedError(RuntimeError):
            def __init__(self, *args):
                trace.append(("exception-init", args))
                super().__init__(*args)

        def factory(text):
            trace.append(("factory", text))
            if factory_result == "instance":
                return SelectedError(text)
            if factory_result == "class":
                return SelectedError
            return object()

        def decoy(text):
            raise AssertionError("error factory was reloaded after mode repr")

        class Mode:
            def __eq__(self, value):
                trace.append(("compare", value))
                return False

            def __repr__(self):
                trace.append("repr")
                # The executing body must already have captured the factory.
                impl.image_globals["ValueError"] = decoy
                return "<factory-mode>"

        class UntouchedLayer:
            def __getattribute__(self, name):
                raise AssertionError("invalid mode accessed layer: " + name)

        layer = UntouchedLayer()
        with monkeypatch.context() as patch:
            patch.setitem(impl.image_globals, "ValueError", factory)
            failure = _error(lambda:
                             impl.image_range.__get__(layer, type(layer))(Mode()))
        return tuple(trace), failure

    trace = (("compare", "data"), ("compare", "slice"), "repr",
             ("factory", message))
    if factory_result == "instance":
        trace += (("exception-init", (message,)),)
        failure = ("SelectedError", message)
    elif factory_result == "class":
        trace += (("exception-init", ()),)
        failure = ("SelectedError", "")
    else:
        failure = ("TypeError", "exceptions must derive from BaseException")
    _compare(implementations, scenario, (trace, failure))
