"""Forty finite comparisons against twelve pinned accelerated colormap bodies.

Every owned helper is rebuilt in each independent namespace, including both
conditional clip definitions. NumPy, enum metadata, caller protocols, and
controlled PartSeg/Numba availability services are external boundaries. The
controlled services do not prove the optional backend/version matrix. No JIT,
real PartSeg kernel, QApplication, canvas, or original application is imported.
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
SOURCE = ROOT / ".upstream/napari/src/napari/utils/colormaps/_accelerated_cmap.py"
CURRENT = ROOT / "src/napari/utils/colormaps/_accelerated_cmap.py"
PINNED_COMMIT = "595639295a153c84bcb127a9e07d801355440a35"
PINNED_SHA256 = "ac4b1f4cb87980857811112d48676987e4fa722078f783681186b101a559d6b1"
NAMES = (
    "minimum_dtype_for_labels", "zero_preserving_modulo_numpy",
    "_zero_preserving_modulo_loop", "_zero_preserving_modulo_inner_loop",
    "_labels_raw_to_texture_direct_numpy", "_labels_raw_to_texture_direct_loop",
    "_labels_raw_to_texture_direct_inner_loop", "zero_preserving_modulo_partsegcore",
    "labels_raw_to_texture_direct_partsegcore", "set_colormap_backend",
)


def _bodies(source):
    lines = source.splitlines(keepends=True)
    result = {}
    for node in ast.walk(ast.parse(source)):
        if isinstance(node, ast.FunctionDef) and node.name in (*NAMES, "clip"):
            body = "".join(lines[node.lineno - 1:node.end_lineno])
            wrapped = body if node.col_offset == 0 else "if True:\n" + body
            extracted = ast.parse(wrapped).body[0]
            if node.col_offset:
                extracted = extracted.body[0]
            assert ast.dump(extracted) == ast.dump(node)
            result.setdefault(node.name, []).append((node.lineno, wrapped))
    assert set(result) == {*NAMES, "clip"}
    assert all(len(result[name]) == 1 for name in NAMES)
    assert len(result["clip"]) == 2
    return result


def _compile_body(body, filename, namespace):
    exec(compile("from __future__ import annotations\n" + body, filename, "exec"),
         namespace)


def _build(bodies, base, filename, active_clip):
    namespace = dict(base)
    for name in NAMES:
        _compile_body(bodies[name][0][1], filename, namespace)
    _compile_body(bodies["clip"][active_clip][1], filename, namespace)
    # Module aliases must also be independent packet functions.
    namespace.update(
        zero_preserving_modulo=namespace["zero_preserving_modulo_numpy"],
        labels_raw_to_texture_direct=namespace["_labels_raw_to_texture_direct_numpy"],
        prange=range,
    )
    return SimpleNamespace(namespace=namespace, bodies=bodies, filename=filename)


@pytest.fixture(scope="module")
def implementations():
    original = SOURCE.read_bytes()
    assert hashlib.sha256(original).hexdigest() == PINNED_SHA256
    module = importlib.import_module("napari.utils.colormaps._accelerated_cmap")
    bodies = _bodies(original.decode("utf-8"))
    current_bodies = _bodies(CURRENT.read_text(encoding="utf-8"))
    # Select the branch actually exposed by the installed module, while tests
    # below separately compile both definitions with their exact source bodies.
    active = next(index for index, (line, _) in enumerate(current_bodies["clip"])
                  if line == module.clip.__code__.co_firstlineno)
    pinned = _build(bodies, vars(module), str(SOURCE), active)
    current = _build(current_bodies, vars(module), str(CURRENT), active)
    for name in (*NAMES, "clip"):
        assert pinned.namespace[name] is not current.namespace[name]
        assert pinned.namespace[name].__globals__ is pinned.namespace
        assert current.namespace[name].__globals__ is current.namespace
    return pinned, current


def _compare(implementations, scenario, contract=None):
    pinned, current = implementations
    expected = scenario(pinned)
    if contract is not None:
        assert expected == contract
    assert scenario(current) == expected


def _error(call):
    try:
        call()
    except Exception as error:
        return type(error).__name__, str(error)
    raise AssertionError("Expected protocol/algorithm failure")


def _traceback(call):
    try:
        call()
    except RuntimeError as error:
        return str(error), error.__traceback__
    raise AssertionError("Expected callback failure")


def _array(array):
    return array.tolist(), array.dtype.str, array.shape, array.flags.writeable


def test_all_owned_helpers_are_independent(implementations):
    pinned, current = implementations
    assert len(pinned.bodies["clip"]) == len(current.bodies["clip"]) == 2
    for impl in implementations:
        namespace = impl.namespace
        assert namespace["_zero_preserving_modulo_loop"].__globals__[
            "_zero_preserving_modulo_inner_loop"
        ] is namespace["_zero_preserving_modulo_inner_loop"]
        assert namespace["_labels_raw_to_texture_direct_loop"].__globals__[
            "minimum_dtype_for_labels"
        ] is namespace["minimum_dtype_for_labels"]
        assert namespace["_labels_raw_to_texture_direct_numpy"].__globals__[
            "clip"
        ] is namespace["clip"]
        assert namespace["labels_raw_to_texture_direct_partsegcore"].__globals__[
            "minimum_dtype_for_labels"
        ] is namespace["minimum_dtype_for_labels"]


@pytest.mark.parametrize(("colors", "dtype"), [
    (-1, "uint8"), (255, "uint8"), (256, "uint16"),
    (65535, "uint16"), (65536, "float32"),
])
def test_minimum_dtype_boundaries(implementations, colors, dtype):
    _compare(implementations, lambda impl:
             impl.namespace["minimum_dtype_for_labels"](colors).name, dtype)


def test_minimum_dtype_factories_are_read_again_after_comparisons(
    implementations, monkeypatch,
):
    def scenario(impl):
        trace = []
        first_uint8, second_uint8, uint16, float32 = (object() for _ in range(4))

        class Numpy:
            reads = 0

            @property
            def uint8(self):
                self.reads += 1
                trace.append(("uint8", self.reads))
                return first_uint8 if self.reads == 1 else second_uint8

            @property
            def uint16(self):
                trace.append("uint16")
                return uint16

            @property
            def float32(self):
                trace.append("float32")
                return float32

            @property
            def iinfo(self):
                trace.append("iinfo-lookup")
                return lambda value: info(value)

            @property
            def dtype(self):
                trace.append("dtype-lookup")
                return lambda value: dtype(value)

        def info(value):
            trace.append(("iinfo", value is first_uint8))
            return SimpleNamespace(max=255)

        def dtype(value):
            trace.append(("dtype", value is second_uint8))
            return output

        output = object()
        with monkeypatch.context() as patch:
            patch.setitem(impl.namespace, "np", Numpy())
            result = impl.namespace["minimum_dtype_for_labels"](255)
        return tuple(trace), result is output

    _compare(implementations, scenario, (
        ("iinfo-lookup", ("uint8", 1), ("iinfo", True), "dtype-lookup",
         ("uint8", 2), ("dtype", True)), True,
    ))


@pytest.mark.parametrize("case", [
    "normal", "strided", "big-endian", "empty", "oversized", "scalar",
])
def test_numpy_modulo_arrays_masks_and_scalar_promotion(implementations, case):
    def scenario(impl):
        if case == "scalar":
            values = np.int64(4)
        elif case == "empty":
            values = np.empty((0, 2), dtype=np.int64)
        elif case == "strided":
            values = np.arange(-6, 7, dtype=np.int64)[::-2]
        elif case == "big-endian":
            values = np.array([-9, -1, 0, 1, 7, 256], dtype=">i8")
        else:
            values = np.array([-9, -1, 0, 1, 7, 256], dtype=np.int64)
        before = values.copy()
        function = impl.namespace["zero_preserving_modulo_numpy"]
        if case == "scalar":
            # The scalar arithmetic returns a scalar whose boolean assignment
            # is rejected; this API must not silently normalize it to an array.
            outcome = _error(lambda: function(values, 5, np.uint8, to_zero=-1))
        else:
            result = function(values, 300 if case == "oversized" else 5,
                              np.uint8, to_zero=-1)
            outcome = _array(result), np.shares_memory(result, values)
        return outcome, np.array_equal(values, before)

    _compare(implementations, scenario)


def test_numpy_modulo_preserves_iinfo_failure(implementations):
    def scenario(impl):
        return _error(lambda: impl.namespace["zero_preserving_modulo_numpy"](
            np.array([0, 1]), 3, np.float32))

    _compare(implementations, scenario, (
        "ValueError", "Invalid integer data type 'f'.",
    ))


def test_modulo_loop_strides_endian_empty_and_out_identity(implementations):
    def scenario(impl):
        results = []
        for values in (np.arange(-8, 9, dtype=np.int64)[::3],
                       np.array([-5, 0, 3, 8], dtype=">i8"),
                       np.empty((2, 0), dtype=np.int64)):
            values.flags.writeable = False
            out = np.full(values.shape, 99, dtype=np.uint16)
            result = impl.namespace["_zero_preserving_modulo_inner_loop"](
                values, 5, 0, out)
            allocated = impl.namespace["_zero_preserving_modulo_loop"](
                values, 5, np.uint16)
            results.append((_array(result), result is out, _array(allocated),
                            np.shares_memory(allocated, values)))
        return results

    _compare(implementations, scenario)


def test_modulo_inner_loop_reads_flat_again_only_for_nonbackground(
    implementations, monkeypatch,
):
    def scenario(impl):
        trace = []

        class Flat:
            reads = {0: 0, 1: 0}

            def __getitem__(self, key):
                self.reads[key] += 1
                trace.append(("get", key, self.reads[key]))
                return 0 if key == 0 else (7 if self.reads[key] == 1 else 9)

        class Values:
            @property
            def size(self):
                trace.append("size")
                return 2

            @property
            def flat(self):
                trace.append("values-flat")
                return flat

        class Out:
            @property
            def flat(self):
                trace.append("out-flat")
                return self

            def __setitem__(self, key, value):
                trace.append(("set", key, value))

        def iteration(count):
            trace.append(("prange", count))
            return range(count)

        flat, out = Flat(), Out()
        with monkeypatch.context() as patch:
            patch.setitem(impl.namespace, "prange", iteration)
            result = impl.namespace["_zero_preserving_modulo_inner_loop"](
                Values(), 5, 0, out)
        return tuple(trace), result is out

    _compare(implementations, scenario, (
        ("size", ("prange", 2), "values-flat", ("get", 0, 1),
         "out-flat", ("set", 0, 0), "values-flat", ("get", 1, 1),
         "values-flat", ("get", 1, 2), "out-flat", ("set", 1, 4)), True,
    ))


def test_modulo_loop_allocated_result_survives_inner_error_traceback(
    implementations, monkeypatch,
):
    def scenario(impl):
        trace, references = [], []
        data, dtype = object(), object()

        class Result:
            pass

        def empty_like(value, *, dtype):
            trace.append(("allocate", value is data, dtype is expected_dtype))
            output = Result()
            references.append(weakref.ref(output))
            return output

        def inner(values, n, to_zero, *, out):
            trace.append(("inner", values is data, n, to_zero,
                          out is references[0]()))
            del out
            raise RuntimeError("inner modulo failed")

        expected_dtype = dtype
        with monkeypatch.context() as patch:
            patch.setitem(impl.namespace, "np", SimpleNamespace(empty_like=empty_like))
            patch.setitem(impl.namespace, "_zero_preserving_modulo_inner_loop", inner)
            message, saved = _traceback(lambda:
                                       impl.namespace["_zero_preserving_modulo_loop"](
                                           data, 7, dtype, 3))
            retained = references[0]() is not None
            del saved
            gc.collect()
            released = references[0]() is None
        return tuple(trace), message, retained, released

    _compare(implementations, scenario, (
        (("allocate", True, True), ("inner", True, 7, 3, True)),
        "inner modulo failed", True, True,
    ))


@pytest.mark.parametrize("negative", [False, True])
def test_direct_numpy_clipping_key_ranges(implementations, negative):
    def scenario(impl):
        data = np.array([-100, -2, -1, 0, 1, 2, 100], dtype=">i8")
        data.flags.writeable = False
        mapper = np.arange(8, dtype=np.uint16)
        colormap = SimpleNamespace(
            use_selection=False, _array_map=mapper,
            color_dict={None: 0, -2 if negative else 2: 1},
        )
        result = impl.namespace["_labels_raw_to_texture_direct_numpy"](data, colormap)
        return _array(result), data.tolist(), np.shares_memory(result, mapper)

    _compare(implementations, scenario)


@pytest.mark.parametrize("name", [
    "_labels_raw_to_texture_direct_numpy", "_labels_raw_to_texture_direct_loop",
])
def test_direct_selection_precedes_all_cache_reads(implementations, name):
    def scenario(impl):
        trace = []

        class Colormap:
            @property
            def use_selection(self):
                trace.append("selection-enabled")
                return True

            @property
            def selection(self):
                trace.append("selection")
                return -2

            def __getattr__(self, name):
                raise AssertionError(f"selection branch queried {name}")

        result = impl.namespace[name](np.array([-2, 0, -2, 4]), Colormap())
        return _array(result), tuple(trace)

    _compare(implementations, scenario, (
        ([1, 0, 1, 0], "|u1", (4,), True), ("selection-enabled", "selection"),
    ))


def test_direct_numpy_captured_any_is_lazy_and_data_rebind_releases_input(
    implementations, monkeypatch,
):
    def scenario(impl):
        trace, references = [], []

        class Data:
            def __init__(self):
                references.append(weakref.ref(self))

        class Key:
            def __lt__(self, other):
                trace.append(("negative", other))
                return True

        class Keys:
            def __iter__(self):
                trace.append("keys-iter")
                return self.items()

            def items(self):
                yield None
                trace.append("first-key")
                yield Key()
                raise AssertionError("negative-key consumer failed to short circuit")

        class Mapper:
            @property
            def shape(self):
                trace.append("mapper-shape")
                return (8,)

            def __getitem__(self, value):
                trace.append(("map", value is replacement, references[0]() is None))
                return output

        class Colormap:
            use_selection = False

            @property
            def _array_map(self):
                trace.append("mapper")
                return Mapper()

            @property
            def color_dict(self):
                trace.append("color-dict")
                impl.namespace["any"] = decoy
                return Keys()

        def decoy(*args):
            raise AssertionError("captured any factory was read again")

        def consumer(values):
            trace.append("any-called")
            result = next(values)
            trace.append(("any-result", result))
            return result

        def clip(value, lower, upper):
            trace.append(("clip", value is references[0](), lower, upper))
            return replacement

        replacement, output = object(), object()
        with monkeypatch.context() as patch:
            patch.setitem(impl.namespace, "any", consumer)
            patch.setitem(impl.namespace, "clip", clip)
            result = impl.namespace["_labels_raw_to_texture_direct_numpy"](
                Data(), Colormap())
        return tuple(trace), result is output

    _compare(implementations, scenario, (
        ("mapper", "color-dict", "keys-iter", "any-called", "first-key",
         ("negative", 0), ("any-result", True), "mapper-shape",
         ("clip", True, -3, 3), ("map", True, True)), True,
    ))


def test_direct_inner_loop_keeps_unknown_prefill_and_out_identity(implementations):
    def scenario(impl):
        results = []
        for data in (np.array([-2, 8, 1, -2], dtype=">i8")[::-1],
                     np.empty((0, 3), dtype=np.int64)):
            data.flags.writeable = False
            out = np.full(data.shape, 44, dtype=np.uint8)
            result = impl.namespace["_labels_raw_to_texture_direct_inner_loop"](
                data, {-2: 7, 1: 9}, out)
            results.append((_array(result), result is out, data.tolist()))
        return results

    _compare(implementations, scenario)


def test_direct_inner_membership_can_mutate_second_flat_lookup(implementations):
    def scenario(impl):
        trace = []

        class Flat:
            value = 5

            def __getitem__(self, key):
                trace.append(("get", key, self.value))
                return self.value

        class Data:
            size = 1

            @property
            def flat(self):
                trace.append("data-flat")
                return flat

        class Mapping:
            def __contains__(self, value):
                trace.append(("contains", value))
                flat.value = 8
                return True

            def __getitem__(self, value):
                trace.append(("mapping-get", value))
                return 17

        class Out:
            @property
            def flat(self):
                trace.append("out-flat")
                return self

            def __setitem__(self, key, value):
                trace.append(("set", key, value))

        flat, out = Flat(), Out()
        result = impl.namespace["_labels_raw_to_texture_direct_inner_loop"](
            Data(), Mapping(), out)
        return tuple(trace), result is out

    _compare(implementations, scenario, (
        ("data-flat", ("get", 0, 5), ("contains", 5), "data-flat",
         ("get", 0, 8), ("mapping-get", 8), "out-flat", ("set", 0, 17)), True,
    ))


@pytest.mark.parametrize("failure", [False, True])
def test_direct_inner_last_val_traceback_and_cycle_release(implementations, failure):
    def scenario(impl):
        trace, references = [], []

        class Value:
            def __init__(self, index):
                self.index = index
                self.cycle = self
                references.append(weakref.ref(self))

        class Flat:
            def __getitem__(self, index):
                gc.collect()
                trace.append(("read", index,
                              not references or references[0]() is not None))
                return Value(index)

        class Data:
            size, flat = 2, Flat()

        class Mapping:
            def __contains__(self, value):
                gc.collect()
                index = value.index
                trace.append(("contains", index,
                              index == 0 or references[0]() is None))
                del value
                if failure and index == 1:
                    raise RuntimeError("last membership failed")
                return False

        out = object()
        if failure:
            message, saved = _traceback(lambda:
                                       impl.namespace[
                                           "_labels_raw_to_texture_direct_inner_loop"
                                       ](Data(), Mapping(), out))
            gc.collect()
            retained = references[-1]() is not None
            del saved
            gc.collect()
            released = all(ref() is None for ref in references)
            outcome = message, retained, released
        else:
            result = impl.namespace["_labels_raw_to_texture_direct_inner_loop"](
                Data(), Mapping(), out)
            gc.collect()
            outcome = result is out, all(ref() is None for ref in references)
        return tuple(trace), outcome

    contract = (("read", 0, True), ("contains", 0, True),
                ("read", 1, True), ("contains", 1, True))
    outcome = ("last membership failed", True, True) if failure else (True, True)
    _compare(implementations, scenario, (contract, outcome))


def test_direct_loop_preserves_typed_mapping_dtype_and_prefill_order(
    implementations, monkeypatch,
):
    def scenario(impl):
        trace = []
        data = np.array([-3, 2, 4], dtype=np.int64)

        class Colormap:
            use_selection = False

            def _get_typed_dict_mapping(self, dtype):
                trace.append(("typed-map", dtype == data.dtype))
                return {-3: 7, 2: 9}

            @property
            def _num_unique_colors(self):
                trace.append("unique-count")
                return 254

        def minimum(colors):
            trace.append(("minimum", colors))
            return np.dtype(np.uint16)

        def full_like(value, default, *, dtype):
            trace.append(("full", value is data, default, dtype.name))
            return np.full_like(value, default, dtype=dtype)

        with monkeypatch.context() as patch:
            patch.setitem(impl.namespace, "minimum_dtype_for_labels", minimum)
            patch.setitem(impl.namespace, "np",
                          SimpleNamespace(full_like=full_like, uint8=np.uint8))
            patch.setitem(impl.namespace, "MAPPING_OF_UNKNOWN_VALUE", 33)
            result = impl.namespace["_labels_raw_to_texture_direct_loop"](
                data, Colormap())
        return _array(result), tuple(trace)

    _compare(implementations, scenario, (
        ([7, 9, 33], "<u2", (3,), True),
        (("typed-map", True), "unique-count", ("minimum", 256),
         ("full", True, 33, "uint16")),
    ))


@pytest.mark.parametrize("branch", [0, 1])
def test_both_conditional_clip_definitions(implementations, branch):
    def scenario(impl):
        namespace = dict(impl.namespace)
        _compile_body(impl.bodies["clip"][branch][1], impl.filename, namespace)
        results = []
        for data in (np.array([0, 20, 250], dtype=np.uint8),
                     np.array([-120, 0, 120], dtype=np.int8)):
            result = namespace["clip"](data, -1000, 1000)
            results.append((_array(result), np.shares_memory(result, data)))
        return results

    _compare(implementations, scenario)


def test_numpy20_clip_rebinds_bounds_with_captured_factories(
    implementations, monkeypatch,
):
    def scenario(impl):
        trace = []
        namespace = dict(impl.namespace)
        _compile_body(impl.bodies["clip"][0][1], impl.filename, namespace)
        data, dtype, output = object(), object(), object()

        class Data:
            @property
            def dtype(self):
                trace.append("dtype")
                return dtype

        class Info:
            @property
            def min(self):
                trace.append("minimum")
                namespace["max"] = decoy
                return -8

            @property
            def max(self):
                trace.append("maximum")
                namespace["min"] = decoy
                return 7

        def info(value):
            trace.append(("iinfo", value is dtype))
            return Info()

        def maximum(a, b):
            trace.append(("max", a, b))
            return max(a, b)

        def minimum(a, b):
            trace.append(("min", a, b))
            return min(a, b)

        def clip(value, lower, upper):
            trace.append(("clip", value is data, lower, upper))
            return output

        def decoy(*args):
            raise AssertionError("bound factory was loaded after provider argument")

        data = Data()
        with monkeypatch.context() as patch:
            patch.setitem(namespace, "np", SimpleNamespace(iinfo=info, clip=clip))
            patch.setitem(namespace, "max", maximum)
            patch.setitem(namespace, "min", minimum)
            result = namespace["clip"](data, -100, 100)
        return tuple(trace), result is output

    _compare(implementations, scenario, (
        ("dtype", ("iinfo", True), "minimum", ("max", -100, -8),
         "maximum", ("min", 100, 7), ("clip", True, -8, 7)), True,
    ))


def test_partseg_modulo_ignores_kernel_result_and_reads_shape_after_release(
    implementations, monkeypatch,
):
    def scenario(impl):
        trace = []
        flat = np.array([0, 2, 7, 4], dtype=np.int64)

        class Ignored:
            def __del__(self):
                trace.append("ignored-released")

        class Values:
            @property
            def size(self):
                trace.append("size")
                return 4

            def reshape(self, value):
                trace.append(("reshape", value))
                return flat

            @property
            def shape(self):
                trace.append("shape")
                return (2, 2)

        def empty(size, *, dtype):
            trace.append(("empty", size, dtype == np.uint8))
            return np.empty(size, dtype=dtype)

        def kernel(values, n, to_zero, out):
            trace.append(("kernel", values is flat, n, to_zero, out.shape))
            out[:] = [0, 2, 1, 4]
            return Ignored()

        with monkeypatch.context() as patch:
            patch.setitem(impl.namespace, "np", SimpleNamespace(empty=empty))
            patch.setitem(impl.namespace, "partsegcore_mapping",
                          SimpleNamespace(zero_preserving_modulo_parallel=kernel))
            result = impl.namespace["zero_preserving_modulo_partsegcore"](
                Values(), 6, np.uint8, 0)
        return _array(result), tuple(trace)

    _compare(implementations, scenario, (
        ([[0, 2], [1, 4]], "|u1", (2, 2), True),
        ("size", ("empty", 4, True), ("reshape", -1),
         ("kernel", True, 6, 0, (4,)), "ignored-released", "shape"),
    ))


@pytest.mark.parametrize("selection", [False, True])
def test_partseg_direct_filters_dtype_and_preserves_none_collision(
    implementations, monkeypatch, selection,
):
    def scenario(impl):
        trace = []
        data = np.array([-2, 0, 2, 5], dtype=np.int8).reshape(2, 2)

        class Colormap:
            @property
            def use_selection(self):
                trace.append("selection-enabled")
                return selection

            @property
            def selection(self):
                trace.append("selection")
                return None

            @property
            def _label_mapping_and_color_dict(self):
                trace.append("mapping")
                return ({None: 11, -129: 7, -2: 9, 2: 13, 128: 17}, object())

            @property
            def _num_unique_colors(self):
                trace.append("unique-count")
                return 5

        def minimum(count):
            trace.append(("minimum", count))
            return np.dtype(np.uint8)

        def kernel(flat, mapping, unknown, out):
            trace.append(("kernel", flat.shape, tuple(mapping.items()), unknown))
            out[:] = [mapping.get(int(value), unknown) for value in flat]

        with monkeypatch.context() as patch:
            patch.setitem(impl.namespace, "minimum_dtype_for_labels", minimum)
            patch.setitem(impl.namespace, "partsegcore_mapping",
                          SimpleNamespace(map_array_parallel=kernel))
            result = impl.namespace["labels_raw_to_texture_direct_partsegcore"](
                data, Colormap())
        return _array(result), tuple(trace)

    if selection:
        contract = (([[0, 0], [0, 0]], "|u1", (2, 2), True),
                    ("selection-enabled", "selection", "unique-count",
                     ("minimum", 7), ("kernel", (4,), ((None, 1),), 0)))
    else:
        contract = (([[9, 0], [13, 0]], "|u1", (2, 2), True),
                    ("selection-enabled", "mapping", "unique-count",
                     ("minimum", 7),
                     ("kernel", (4,), ((None, 11), (-2, 9), (2, 13)), 0)))
    _compare(implementations, scenario, contract)


@pytest.mark.parametrize(("partseg", "numba", "backend", "chosen"), [
    (False, False, "fastest_available", "numpy"),
    (True, True, "fastest_available", "partseg"),
    (False, True, "numba", "numba"),
    (True, True, "Pure Python", "numpy"),
    (True, False, "PartSegCore", "partseg"),
])
def test_backend_availability_and_raw_string_selection(
    implementations, monkeypatch, partseg, numba, backend, chosen,
):
    def scenario(impl):
        namespace = impl.namespace
        enum = namespace["ColormapBackend"]
        request = getattr(enum, backend) if hasattr(enum, backend) else backend
        numba_range = object()
        with monkeypatch.context() as patch:
            patch.setitem(namespace, "partsegcore_mapping",
                          object() if partseg else None)
            patch.setitem(namespace, "numba",
                          SimpleNamespace(prange=numba_range) if numba else None)
            for name in ("COLORMAP_BACKEND", "labels_raw_to_texture_direct",
                         "zero_preserving_modulo", "prange"):
                patch.setitem(namespace, name, object())
            namespace["set_colormap_backend"](request)
            expected_modulo = {
                "numpy": "zero_preserving_modulo_numpy",
                "numba": "_zero_preserving_modulo_loop",
                "partseg": "zero_preserving_modulo_partsegcore",
            }[chosen]
            expected_direct = {
                "numpy": "_labels_raw_to_texture_direct_numpy",
                "numba": "_labels_raw_to_texture_direct_loop",
                "partseg": "labels_raw_to_texture_direct_partsegcore",
            }[chosen]
            return (
                namespace["COLORMAP_BACKEND"] is request,
                namespace["zero_preserving_modulo"] is namespace[expected_modulo],
                namespace["labels_raw_to_texture_direct"] is namespace[expected_direct],
                namespace["prange"] is (numba_range if chosen == "numba" else range),
            )

    _compare(implementations, scenario, (True, True, True, True))


def test_unhashable_backend_is_published_before_membership_failure(
    implementations, monkeypatch,
):
    def scenario(impl):
        namespace = impl.namespace
        request, original_direct, original_modulo, original_range = [], (object()), (
            object()), object()
        with monkeypatch.context() as patch:
            patch.setitem(namespace, "partsegcore_mapping", object())
            patch.setitem(namespace, "numba", None)
            patch.setitem(namespace, "labels_raw_to_texture_direct", original_direct)
            patch.setitem(namespace, "zero_preserving_modulo", original_modulo)
            patch.setitem(namespace, "prange", original_range)
            patch.setitem(namespace, "COLORMAP_BACKEND", None)
            error = _error(lambda: namespace["set_colormap_backend"](request))
            return (error, namespace["COLORMAP_BACKEND"] is request,
                    namespace["labels_raw_to_texture_direct"] is original_direct,
                    namespace["zero_preserving_modulo"] is original_modulo,
                    namespace["prange"] is original_range)

    _compare(implementations, scenario, (
        ("TypeError", "unhashable type: 'list'"), True, True, True, True,
    ))


def test_numba_prange_error_keeps_earlier_alias_publications(
    implementations, monkeypatch,
):
    def scenario(impl):
        namespace = impl.namespace
        trace = []
        request = namespace["ColormapBackend"].numba
        previous_range = object()

        class Numba:
            @property
            def prange(self):
                trace.append(("prange-read",
                              namespace["COLORMAP_BACKEND"] is request,
                              namespace["zero_preserving_modulo"] is namespace[
                                  "_zero_preserving_modulo_loop"],
                              namespace["labels_raw_to_texture_direct"] is namespace[
                                  "_labels_raw_to_texture_direct_loop"]))
                raise RuntimeError("Numba range failed")

        with monkeypatch.context() as patch:
            patch.setitem(namespace, "partsegcore_mapping", None)
            patch.setitem(namespace, "numba", Numba())
            patch.setitem(namespace, "COLORMAP_BACKEND", None)
            patch.setitem(namespace, "labels_raw_to_texture_direct", object())
            patch.setitem(namespace, "zero_preserving_modulo", object())
            patch.setitem(namespace, "prange", previous_range)
            error = _error(lambda: namespace["set_colormap_backend"](request))
            return tuple(trace), error, namespace["prange"] is previous_range

    _compare(implementations, scenario, (
        (("prange-read", True, True, True),),
        ("RuntimeError", "Numba range failed"), True,
    ))
