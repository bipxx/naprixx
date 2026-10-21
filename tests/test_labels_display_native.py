"""Finite Labels display comparisons against eleven pinned napari bodies.

Only named bodies are compiled, with independent namespaces replacing every
helper owned by this packet. NumPy, SciPy morphology, warning services, and
caller callbacks are external providers. Accelerated texture/cache services
are controlled protocols here; their implementation is outside this packet.
No Labels, colormap model, QApplication, canvas, or slicing task is created.
"""

from __future__ import annotations

import ast
import gc
import hashlib
import importlib
import sys
import weakref
from pathlib import Path
from types import ModuleType, SimpleNamespace

import numpy as np
import pytest

ROOT = Path(__file__).resolve().parents[1]
UPSTREAM = ROOT / ".upstream/napari/src/napari"
PINNED_COMMIT = "595639295a153c84bcb127a9e07d801355440a35"
PINNED = {
    "layers/labels/labels.py":
        "8d5ac1e119018af7b49df29838a91273910df09c2d4e4e6ec7e58a3744e7a176",
    "layers/labels/_labels_utils.py":
        "0fa4ebdb8d9d6664b405387730267153fbce79ebad94be548a04752f9ad18130",
    "utils/colormaps/colormap.py":
        "af2eed8dfa33850a7e8000cd3ac36cffca00d2519ffdcbed024aeae27819ff80",
}


def _body(relative, owner, name):
    raw = (UPSTREAM / relative).read_bytes()
    assert hashlib.sha256(raw).hexdigest() == PINNED[relative]
    source = raw.decode("utf-8")
    members = ast.parse(source).body
    if owner is not None:
        members = next(node.body for node in members
                       if isinstance(node, ast.ClassDef) and node.name == owner)
    # The implementation follows the typing.overload declarations unchanged.
    original = [node for node in members
                if isinstance(node, ast.FunctionDef) and node.name == name][-1]
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
    exec(compile("from __future__ import annotations\nclass PinnedOwner:\n" + text,
                 str(UPSTREAM / relative), "exec"), namespace)
    return vars(namespace["PinnedOwner"])[name]


@pytest.fixture(scope="module")
def implementations():
    labels = importlib.import_module("napari.layers.labels.labels")
    utils = importlib.import_module("napari.layers.labels._labels_utils")
    textures = importlib.import_module("napari.utils.colormaps.colormap")
    scipy = importlib.import_module("scipy")
    labels_globals, utils_globals = dict(vars(labels)), dict(vars(utils))
    texture_globals = dict(vars(textures))
    get_contours = _function("layers/labels/_labels_utils.py", "get_contours",
                             utils_globals)
    expand = _function("layers/labels/_labels_utils.py", "expand_slice",
                       utils_globals)
    labels_globals.update(get_contours=get_contours, expand_slice=expand)
    for name in ("_convert_small_ints_to_unsigned",
                 "_cast_labels_data_to_texture_dtype_auto",
                 "_cast_labels_data_to_texture_dtype_direct", "_texture_dtype"):
        _function("utils/colormaps/colormap.py", name, texture_globals)
    pinned = SimpleNamespace(
        labels_globals=labels_globals, texture_globals=texture_globals,
        scipy=scipy, contours=get_contours, expand=expand,
        raw=_method("layers/labels/labels.py", "Labels", "_raw_to_displayed",
                    labels_globals),
        contour=_method("layers/labels/labels.py", "Labels", "_calculate_contour",
                        labels_globals),
        slice_dtype=_method("layers/labels/labels.py", "Labels", "_slice_dtype",
                            labels_globals),
        small=texture_globals["_convert_small_ints_to_unsigned"],
        auto=texture_globals["_cast_labels_data_to_texture_dtype_auto"],
        direct=texture_globals["_cast_labels_data_to_texture_dtype_direct"],
        texture_dtype=texture_globals["_texture_dtype"],
        cyclic_method=_method("utils/colormaps/colormap.py", "CyclicLabelColormap",
                              "_data_to_texture", texture_globals),
        direct_method=_method("utils/colormaps/colormap.py", "DirectLabelColormap",
                              "_data_to_texture", texture_globals),
    )
    current = SimpleNamespace(
        labels_globals=vars(labels), texture_globals=vars(textures), scipy=scipy,
        contours=utils.get_contours, expand=utils.expand_slice,
        raw=labels.Labels._raw_to_displayed,
        contour=labels.Labels._calculate_contour,
        slice_dtype=labels.Labels._slice_dtype,
        small=textures._convert_small_ints_to_unsigned,
        auto=textures._cast_labels_data_to_texture_dtype_auto,
        direct=textures._cast_labels_data_to_texture_dtype_direct,
        texture_dtype=textures._texture_dtype,
        cyclic_method=textures.CyclicLabelColormap._data_to_texture,
        direct_method=textures.DirectLabelColormap._data_to_texture,
    )
    assert pinned.contour.__globals__["get_contours"] is pinned.contours
    assert pinned.contour.__globals__["expand_slice"] is pinned.expand
    assert pinned.auto.__globals__["_convert_small_ints_to_unsigned"] is pinned.small
    assert pinned.direct.__globals__["_convert_small_ints_to_unsigned"] is (
        pinned.small
    )
    assert pinned.cyclic_method.__globals__[
        "_cast_labels_data_to_texture_dtype_auto"
    ] is pinned.auto
    assert pinned.direct_method.__globals__[
        "_cast_labels_data_to_texture_dtype_direct"
    ] is pinned.direct
    assert pinned.contours is not current.contours
    assert pinned.small is not current.small
    assert pinned.auto is not current.auto
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
    raise AssertionError("Expected callback/algorithm failure")


def _traceback(call):
    try:
        call()
    except RuntimeError as error:
        return str(error), error.__traceback__
    raise AssertionError("Expected callback failure")


def _slices(value):
    return tuple((axis.start, axis.stop, axis.step) for axis in value)


@pytest.mark.parametrize("case", ["disabled", "3d"])
def test_contour_shortcuts_preserve_getter_and_warning_order(
    implementations, monkeypatch, case,
):
    def scenario(impl):
        trace = []

        class Layer:
            @property
            def contour(self):
                trace.append("contour")
                return 0 if case == "disabled" else 1

        class Labels:
            @property
            def ndim(self):
                trace.append("ndim")
                assert case == "3d"
                return 3

        def warn(message):
            trace.append(("warn", message))

        with monkeypatch.context() as patch:
            patch.setitem(impl.labels_globals, "warnings", SimpleNamespace(warn=warn))
            result = impl.contour(Layer(), Labels(), object())
        return tuple(trace), result is None

    trace = ("contour",)
    if case == "3d":
        trace += ("ndim", ("warn", "Contours are not displayed during 3D rendering"))
    _compare(implementations, scenario, (trace, True))


@pytest.mark.parametrize("thickness", [1, 2])
def test_contours_morphology_returns_copy_and_keeps_input_immutable(
    implementations, thickness,
):
    def scenario(impl):
        data = np.zeros((5, 5), dtype=np.int16)
        data[1:4, 1:4] = 7
        before = data.copy()
        data.flags.writeable = False
        result = impl.contours(data, thickness, 0)
        return (result.tolist(), np.array_equal(data, before),
                not np.shares_memory(result, data), result.dtype == data.dtype)

    expected = [[0] * 5 for _ in range(5)]
    expected[1][1:4] = [7, 7, 7]
    expected[2][1:4] = [7, 0 if thickness == 1 else 7, 7]
    expected[3][1:4] = [7, 7, 7]
    _compare(implementations, scenario, (expected, True, True, True))


@pytest.mark.parametrize("failure", [False, True])
def test_contours_provider_order_stops_at_erosion_failure(
    implementations, monkeypatch, failure,
):
    def scenario(impl):
        trace = []
        structure, thick, mask, eroded = object(), object(), object(), object()
        background = 2**70 + 3

        class Footprint:
            def astype(self, dtype):
                trace.append(("astype", dtype is bool))
                return thick

        class Dilated:
            def __eq__(self, value):
                trace.append(("compare", value is eroded))
                return mask

        class Contours:
            def __setitem__(self, index, value):
                trace.append(("assign", index is mask, value))

        output = Contours()

        class Labels:
            @property
            def ndim(self):
                trace.append("ndim")
                return 2

            def copy(self):
                trace.append("copy")
                return output

        labels = Labels()
        backend = ModuleType("scipy.ndimage")

        def generate(ndim, connectivity):
            trace.append(("structure", ndim, connectivity))
            return structure

        def iterate(value, thickness):
            trace.append(("iterate", value is structure, thickness))
            return Footprint()

        def dilation(value, *, footprint):
            trace.append(("dilation", value is labels, footprint is structure))
            return Dilated()

        def erosion(value, *, footprint):
            trace.append(("erosion", value is labels, footprint is thick))
            if failure:
                raise RuntimeError("erosion failed")
            return eroded

        backend.generate_binary_structure = generate
        backend.iterate_structure = iterate
        backend.grey_dilation = dilation
        backend.grey_erosion = erosion
        with monkeypatch.context() as patch:
            patch.setattr(impl.scipy, "ndimage", backend, raising=False)
            patch.setitem(sys.modules, "scipy.ndimage", backend)
            if failure:
                outcome = _error(lambda: impl.contours(labels, 3, background))
            else:
                result = impl.contours(labels, 3, background)
                outcome = result is output
        return tuple(trace), outcome

    prefix = ("ndim", ("structure", 2, 1), ("iterate", True, 3),
              ("astype", True), ("dilation", True, True), ("erosion", True, True))
    if failure:
        contract = prefix, ("RuntimeError", "erosion failed")
    else:
        contract = prefix + (("compare", True), "copy",
                             ("assign", True, 2**70 + 3)), True
    _compare(implementations, scenario, contract)


@pytest.mark.parametrize("dtype", ["i1", "i2", "u1", "u2", ">i2", "i4"])
def test_small_integer_conversion_keeps_views_strides_readonly_and_empty_aliases(
    implementations, dtype,
):
    def scenario(impl):
        signed = np.dtype(dtype).kind == "i"
        values = [-1, 0, 1, 2, 3, 4] if signed else [0, 1, 2, 3, 4, 5]
        data = np.array(values, dtype=dtype)[::2]
        data.flags.writeable = False
        result = impl.small(data)
        empty = np.empty(0, dtype=dtype)
        empty_result = impl.small(empty)
        return (result.tolist(), result.dtype.kind, result.dtype.itemsize,
                np.shares_memory(result, data), result is data,
                result.strides == data.strides, result.flags.writeable,
                empty_result.shape, empty_result is empty or empty_result.base is empty)

    values = {
        "i1": [255, 1, 3], "i2": [65535, 1, 3],
        "u1": [0, 2, 4], "u2": [0, 2, 4],
        ">i2": [65535, 256, 768] if np.little_endian else [65535, 1, 3],
        "i4": [-1, 1, 3],
    }[dtype]
    kind, itemsize = ("i", 4) if dtype == "i4" else (
        "u", 1 if dtype in {"i1", "u1"} else 2
    )
    _compare(implementations, scenario, (
        values, kind, itemsize, True, dtype == "i4", True, False, (0,), True,
    ))


def test_texture_dtype_delegates_wide_boundaries_and_keeps_small_storage(
    implementations, monkeypatch,
):
    def scenario(impl):
        trace = []
        returned = {256: np.dtype(np.uint8), 257: np.dtype(np.uint16),
                    65536: np.dtype(np.uint16), 65537: np.dtype(np.float32)}

        def minimum(count):
            trace.append(count)
            return returned[count]

        cases = ((99, np.dtype(np.int8)), (99, np.dtype(np.int16)),
                 (99, np.dtype(np.float16)), (256, np.dtype(np.int32)),
                 (257, np.dtype(np.int64)), (65536, np.dtype(np.uint64)),
                 (65537, np.dtype(np.int32)))
        with monkeypatch.context() as patch:
            patch.setitem(impl.texture_globals, "_accel_cmap", SimpleNamespace(
                minimum_dtype_for_labels=minimum,
            ))
            names = tuple(impl.texture_dtype(count, dtype).name
                          for count, dtype in cases)
        return names, tuple(trace)

    _compare(implementations, scenario, (
        ("uint8", "uint16", "uint16", "uint8", "uint16", "uint16", "float32"),
        (256, 257, 65536, 65537),
    ))


@pytest.mark.parametrize("case", ["array", "scalar", "selection"])
def test_auto_texture_keeps_accelerated_lookup_selection_and_background_order(
    implementations, monkeypatch, case,
):
    def scenario(impl):
        trace = []
        target = np.dtype(np.uint8)
        data = (np.int64(-2**63 + 3) if case == "scalar" else
                np.array([[5, 0, -5], [7, 8, 9]], dtype=np.int32)[:, ::-1])

        def minimum(count):
            trace.append(("minimum", count))
            return target

        def modulo(values, count, dtype, background):
            trace.append(("modulo", values.tolist(), count,
                          dtype is target, background))
            return np.array([[10, 11, 12], [13, 14, 15]], dtype=target)

        def numpy_modulo(values, count, dtype, background=None):
            trace.append(("numpy", values.tolist(), count, dtype is target, background))
            return np.array([99 if case == "selection" else 77], dtype=target)

        class Backend:
            @property
            def zero_preserving_modulo(self):
                trace.append("get-modulo")
                return modulo

            @property
            def zero_preserving_modulo_numpy(self):
                trace.append("get-numpy")
                return numpy_modulo

            @property
            def minimum_dtype_for_labels(self):
                trace.append("get-minimum")
                return minimum

        class Colormap:
            selected = 5

            @property
            def colors(self):
                trace.append("colors")
                return [None] * 4

            @property
            def use_selection(self):
                trace.append("use-selection")
                return case == "selection"

            @property
            def selection(self):
                value = self.selected
                trace.append(("selection", value))
                self.selected = 7
                return value

            @property
            def background_value(self):
                trace.append("background")
                return -3

        with monkeypatch.context() as patch:
            patch.setitem(impl.texture_globals, "_accel_cmap", Backend())
            result = impl.auto(data, Colormap())
        return (np.asarray(result).tolist(), result.dtype.name, np.shape(result),
                "array" if isinstance(result, np.ndarray) else type(result).__name__,
                tuple(trace))

    prefix = ("colors", "get-modulo")
    if case == "scalar":
        prefix += ("get-numpy",)
    prefix += ("get-minimum", ("minimum", 4), "use-selection")
    if case == "selection":
        trace = prefix + ("get-numpy", ("selection", 5),
                          ("numpy", [5], 3, True, None), ("selection", 7))
        contract = ([[0, 0, 0], [0, 0, 99]], "uint8", (2, 3), "array", trace)
    elif case == "scalar":
        trace = prefix + ("background", ("numpy", [-2**63 + 3], 3, True, -3))
        contract = (77, "uint8", (), "uint8", trace)
    else:
        trace = prefix + ("background", ("modulo", [[-5, 0, 5], [9, 8, 7]],
                                         3, True, -3))
        contract = ([[10, 11, 12], [13, 14, 15]], "uint8", (2, 3), "array", trace)
    _compare(implementations, scenario, contract)


def test_auto_texture_reads_numpy_shape_before_small_integer_shortcut(
    implementations,
):
    def scenario(impl):
        trace, output = [], object()

        class Data:
            def __array_function__(self, function, types, args, kwargs):
                assert function is np.shape
                trace.append("shape")
                return (0,)

            @property
            def itemsize(self):
                trace.append("itemsize")
                return 1

            @property
            def dtype(self):
                trace.append("dtype")
                return SimpleNamespace(itemsize=1)

            def view(self, dtype):
                trace.append(("view", dtype is np.uint8))
                return output

        class UntouchedColormap:
            def __getattribute__(self, name):
                raise AssertionError("small integer shortcut read colormap: " + name)

        result = impl.auto(Data(), UntouchedColormap())
        return tuple(trace), result is output

    _compare(implementations, scenario, (
        ("shape", "itemsize", "dtype", ("view", True)), True,
    ))


@pytest.mark.parametrize("case", ["scalar-known", "scalar-unknown", "array"])
def test_direct_texture_keeps_cache_scalar_defaults_and_array_backend_dispatch(
    implementations, monkeypatch, case,
):
    def scenario(impl):
        trace = []
        target = np.dtype(np.uint8)

        def minimum(count):
            trace.append(("minimum", count))
            return target

        def lookup(key, default):
            trace.append(("lookup", key, default))
            return 31 if case == "scalar-known" else default

        class Mapper:
            @property
            def get(self):
                trace.append("get-lookup")
                return lookup

        class Colormap:
            @property
            def _label_mapping_and_color_dict(self):
                trace.append("mapping")
                return (Mapper(), object())

            @property
            def _num_unique_colors(self):
                trace.append("colors-count")
                return 2

        colormap = Colormap()

        def direct(values, cmap):
            trace.append(("direct-array", values.shape, values.tolist(),
                          cmap is colormap))
            return np.full(values.shape, 21, dtype=target)

        class Backend:
            @property
            def minimum_dtype_for_labels(self):
                trace.append("get-minimum")
                return minimum

            @property
            def MAPPING_OF_UNKNOWN_VALUE(self):
                trace.append("unknown")
                return 0

            @property
            def labels_raw_to_texture_direct(self):
                trace.append("get-direct")
                return direct

        with monkeypatch.context() as patch:
            patch.setitem(impl.texture_globals, "_accel_cmap", Backend())
            if case == "array":
                data = np.array([[1, 2], [3, 4]], dtype=np.int32)[:, ::-1]
                result = impl.direct(data, colormap)
                empty = impl.direct(np.empty((0, 2), dtype=np.int32), colormap)
                outcome = (result.tolist(), result.dtype.name,
                           empty.tolist(), empty.shape)
            else:
                result = impl.direct(np.int64(-2**63 + 1), colormap)
                outcome = int(result), type(result).__name__
        return outcome, tuple(trace)

    if case == "array":
        contract = (([[21, 21], [21, 21]], "uint8", [], (0, 2)), (
            "get-direct", ("direct-array", (2, 2), [[2, 1], [4, 3]], True),
            "get-direct", ("direct-array", (0, 2), [], True),
        ))
    else:
        contract = ((31 if case == "scalar-known" else 0, "uint8"), (
            "mapping", "get-minimum", "colors-count", ("minimum", 4),
            "get-lookup", "unknown", ("lookup", -2**63 + 1, 0),
        ))
    _compare(implementations, scenario, contract)


@pytest.mark.parametrize("kind", ["cyclic", "direct"])
def test_colormap_texture_methods_preserve_helper_factory_overrides(
    implementations, monkeypatch, kind,
):
    def scenario(impl):
        trace = []
        raw, colormap, output = object(), object(), object()

        def helper(data, cmap):
            trace.append(("helper", data is raw, cmap is colormap))
            return output

        name = "_cast_labels_data_to_texture_dtype_" + (
            "auto" if kind == "cyclic" else "direct"
        )
        method = impl.cyclic_method if kind == "cyclic" else impl.direct_method
        with monkeypatch.context() as patch:
            patch.setitem(impl.texture_globals, name, helper)
            result = method.__get__(colormap, type(colormap))(raw)
        return tuple(trace), result is output

    _compare(implementations, scenario, ((("helper", True, True),), True))


@pytest.mark.parametrize("kind", ["auto", "direct"])
def test_texture_error_traceback_keeps_source_locals_and_direct_rebinding(
    implementations, monkeypatch, kind,
):
    def scenario(impl):
        trace, shape_refs, array_refs, original_refs = [], [], [], []

        if kind == "auto":
            class Shape:
                pass

            def shape(data):
                trace.append("shape")
                result = Shape()
                shape_refs.append(weakref.ref(result))
                return result

            def atleast(data):
                trace.append("atleast")
                result = np.array([7], dtype=np.int32)
                array_refs.append(weakref.ref(result))
                return result

            def minimum(count):
                trace.append(("minimum", count))
                return np.dtype(np.uint8)

            def backend(data, count, dtype, background):
                trace.append(("backend-owned", shape_refs[0]() is not None,
                              array_refs[0]() is not None))
                del data
                raise RuntimeError("auto backend failed")

            provider = SimpleNamespace(shape=shape, atleast_1d=atleast,
                                       integer=np.integer)
            colormap = SimpleNamespace(colors=[None] * 4, use_selection=False,
                                       background_value=0)
            service = SimpleNamespace(zero_preserving_modulo=backend,
                                      minimum_dtype_for_labels=minimum)
            data = SimpleNamespace(itemsize=4)
            with monkeypatch.context() as patch:
                patch.setitem(impl.texture_globals, "np", provider)
                patch.setitem(impl.texture_globals, "_accel_cmap", service)
                message, saved = _traceback(lambda: impl.auto(data, colormap))
                retained = shape_refs[0]() is not None, array_refs[0]() is not None
                before_drop = tuple(trace)
                del saved
                gc.collect()
                released = shape_refs[0]() is None, array_refs[0]() is None
            return before_drop, message, retained, released

        class Original:
            def __init__(self):
                original_refs.append(weakref.ref(self))

            def __del__(self):
                trace.append("original-released")

        class Converted:
            def __init__(self):
                array_refs.append(weakref.ref(self))

            @property
            def itemsize(self):
                trace.append(("rebound-itemsize", original_refs[0]() is None,
                              array_refs[0]() is not None))
                del self
                raise RuntimeError("direct itemsize failed")

        def convert(value):
            trace.append("convert")
            return Converted()

        with monkeypatch.context() as patch:
            patch.setitem(impl.texture_globals,
                          "_convert_small_ints_to_unsigned", convert)
            message, saved = _traceback(lambda: impl.direct(Original(), object()))
            retained = original_refs[0]() is None, array_refs[0]() is not None
            before_drop = tuple(trace)
            del saved
            gc.collect()
            released = original_refs[0]() is None, array_refs[0]() is None
        return before_drop, message, retained, released

    contracts = {
        "auto": (("shape", "atleast", ("minimum", 4), ("backend-owned", True, True)),
                 "auto backend failed", (True, True), (True, True)),
        "direct": (("convert", "original-released", ("rebound-itemsize", True, True)),
                   "direct itemsize failed", (True, True), (True, True)),
    }
    _compare(implementations, scenario, contracts[kind])


@pytest.mark.parametrize("case", ["truncate-step", "shrink", "missing-start"])
def test_expand_slice_preserves_clipping_zip_truncation_steps_and_errors(
    implementations, case,
):
    def scenario(impl):
        if case == "truncate-step":
            return _slices(impl.expand((slice(-2, 9, 2), slice(20, 30, 3)), (5,), 2))
        if case == "shrink":
            return _slices(impl.expand((slice(2, 8, -1), slice(3, 12)), (10, 20), -3))
        return _error(lambda: impl.expand((slice(None, 2),), (4,), 1))

    contracts = {
        "truncate-step": ((0, 5, 2),),
        "shrink": ((5, 5, -1), (6, 9, None)),
        "missing-start": ("TypeError", "unsupported operand type(s) for -: "
                          "'NoneType' and 'int'"),
    }
    _compare(implementations, scenario, contracts[case])


def test_contour_reentry_preserves_captured_helper_and_fresh_background(
    implementations, monkeypatch,
):
    def scenario(impl):
        trace = []
        original_slice = (slice(3, 7), slice(4, 8))
        expanded = (slice(1, 8), slice(2, 9))
        selected, output = object(), object()

        class Colormap:
            @property
            def background_value(self):
                trace.append("background")
                return 70

        class Layer:
            reads = 0
            current = object()

            @property
            def contour(self):
                self.reads += 1
                trace.append(("contour", self.reads))
                return self.reads

            @property
            def colormap(self):
                trace.append("colormap")
                return self.current

        layer = Layer()

        def decoy(*args):
            raise AssertionError("contour helper reloaded after labels indexing")

        class Labels:
            @property
            def ndim(self):
                trace.append("ndim")
                return 2

            @property
            def shape(self):
                trace.append("shape")
                return (10, 10)

            def __getitem__(self, key):
                trace.append(("labels", _slices(key)))
                layer.current = Colormap()
                impl.labels_globals["get_contours"] = decoy
                return selected

        class Contours:
            def __getitem__(self, key):
                trace.append(("crop", _slices(key)))
                return output

        def expand(value, shape, offset):
            trace.append(("expand", value is original_slice, shape, offset))
            return expanded

        def contours(value, thickness, background):
            trace.append(("contours", value is selected, thickness, background))
            return Contours()

        with monkeypatch.context() as patch:
            patch.setitem(impl.labels_globals, "expand_slice", expand)
            patch.setitem(impl.labels_globals, "get_contours", contours)
            result = impl.contour(layer, Labels(), original_slice)
        return tuple(trace), result is output

    _compare(implementations, scenario, (
        (("contour", 1), "ndim", ("contour", 2), "shape",
         ("expand", True, (10, 10), 2), ("labels", ((1, 8, None), (2, 9, None))),
         ("contour", 3), "colormap", "background", ("contours", True, 3, 70),
         ("crop", ((2, 6, None), (2, 6, None)))), True,
    ))


def test_contour_error_traceback_owns_sliced_labels(implementations, monkeypatch):
    def scenario(impl):
        trace, references = [], []

        class Contours:
            def __init__(self):
                references.append(weakref.ref(self))

        class Axis:
            @property
            def start(self):
                trace.append(("start", references[0]() is not None))
                del self
                raise RuntimeError("slice start failed")

        class Labels:
            ndim, shape = 2, (4,)

            def __getitem__(self, key):
                trace.append("labels")
                return object()

        layer = SimpleNamespace(contour=1, colormap=SimpleNamespace(background_value=0))

        def contours(*args):
            trace.append("contours")
            return Contours()

        with monkeypatch.context() as patch:
            patch.setitem(impl.labels_globals, "expand_slice",
                          lambda *args: (slice(0, 4),))
            patch.setitem(impl.labels_globals, "get_contours", contours)
            message, saved = _traceback(lambda:
                                       impl.contour.__get__(layer, type(layer))(
                                           Labels(), (Axis(),)))
            retained = references[0]() is not None
            before_drop = tuple(trace)
            del saved
            gc.collect()
            released = references[0]() is None
        return before_drop, message, retained, released

    _compare(implementations, scenario, (
        ("labels", "contours", ("start", True)), "slice start failed", True, True,
    ))


@pytest.mark.parametrize("whole", [True, False])
def test_raw_display_none_fallback_keeps_raw_views_and_fresh_colormap(
    implementations, whole,
):
    def scenario(impl):
        trace = []
        raw = np.arange(12, dtype=np.int16).reshape(3, 4)
        raw.flags.writeable = False
        requested = None if whole else (slice(0, 3, 2), slice(1, 4, 2))

        class Colormap:
            def _data_to_texture(self, value):
                trace.append(("texture", np.shares_memory(value, raw),
                              value.flags.writeable))
                return value

        class Layer:
            current = object()

            def _calculate_contour(self, value, data_slice):
                trace.append(("contour", value is raw, _slices(data_slice)))
                self.current = Colormap()
                return None

            @property
            def colormap(self):
                trace.append("colormap")
                return self.current

        result = impl.raw(Layer(), raw, requested)
        return result.tolist(), tuple(trace)

    key = ((0, 3, None), (0, 4, None)) if whole else ((0, 3, 2), (1, 4, 2))
    values = ([[0, 1, 2, 3], [4, 5, 6, 7], [8, 9, 10, 11]]
              if whole else [[1, 3], [9, 11]])
    _compare(implementations, scenario, (
        values, (("contour", True, key), "colormap", ("texture", True, False)),
    ))


def test_raw_display_contour_falsy_array_is_used_without_truth_test(implementations):
    def scenario(impl):
        trace = []

        class FalsyArray(np.ndarray):
            def __bool__(self):
                raise AssertionError("contour result was tested for truth")

        class Raw(np.ndarray):
            def __getitem__(self, key):
                raise AssertionError("non-None contour result triggered raw slice")

        raw = np.array(7).view(Raw)
        contour = np.array(0).view(FalsyArray)

        class Layer:
            def _calculate_contour(self, labels, data_slice):
                trace.append(("contour", labels is raw, data_slice))
                return contour

            @property
            def colormap(self):
                trace.append("colormap")
                return SimpleNamespace(_data_to_texture=texture)

        def texture(value):
            trace.append(("texture", value is contour))
            return value

        result = impl.raw(Layer(), raw, ())
        return tuple(trace), result is contour

    _compare(implementations, scenario, (
        (("contour", True, ()), "colormap", ("texture", True)), True,
    ))


@pytest.mark.parametrize("failure", [False, True])
def test_slice_dtype_preserves_captured_factories_and_temporary_release(
    implementations, monkeypatch, failure,
):
    def scenario(impl):
        trace, references = [], []
        source_dtype, target_dtype = object(), np.dtype(np.uint16)

        def decoy(*args, **kwargs):
            raise AssertionError("slice dtype reloaded a captured provider")

        class Mapping:
            @property
            def dtype(self):
                trace.append(("mapped-dtype", references[0]() is None))
                return target_dtype

            def __del__(self):
                trace.append("mapping-released")

        def empty_released(reference):
            trace.append("empty-released")

        def texture(value):
            trace.append(("texture", value.shape, value.dtype.name))
            references.append(weakref.ref(value, empty_released))
            return Mapping()

        class Colormap:
            @property
            def _data_to_texture(self):
                trace.append("texture-lookup")
                return texture

        class Layer:
            current = Colormap()

            @property
            def colormap(self):
                trace.append("colormap")
                return self.current

            @property
            def dtype(self):
                trace.append("dtype")
                return source_dtype

        layer = Layer()

        def zeros(count, *, dtype):
            trace.append(("zeros", count, dtype.name))
            return np.zeros(count, dtype=dtype)

        class Numpy:

            @property
            def zeros(self):
                trace.append("zeros-lookup")
                return self.current

        provider = Numpy()
        provider.current = zeros

        def normalize(value):
            trace.append(("normalize", value is source_dtype))
            layer.current = SimpleNamespace(_data_to_texture=decoy)
            provider.current = decoy
            if failure:
                raise RuntimeError("dtype normalization failed")
            return np.dtype(np.int16)

        with monkeypatch.context() as patch:
            patch.setitem(impl.labels_globals, "np", provider)
            patch.setitem(impl.labels_globals, "normalize_dtype", normalize)
            if failure:
                outcome = _error(lambda: impl.slice_dtype.__get__(layer, type(layer))())
            else:
                result = impl.slice_dtype.__get__(layer, type(layer))()
                outcome = result is target_dtype
        return tuple(trace), outcome

    prefix = ("colormap", "texture-lookup", "zeros-lookup", "dtype",
              ("normalize", True))
    if failure:
        contract = prefix, ("RuntimeError", "dtype normalization failed")
    else:
        contract = prefix + (("zeros", 0, "int16"), ("texture", (0,), "int16"),
                             "empty-released", ("mapped-dtype", True),
                             "mapping-released"), True
    _compare(implementations, scenario, contract)
