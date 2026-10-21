"""Finite source comparisons for eight Direct/base colormap cache bodies.

The two classes are rebuilt independently with their exact original method
segments, cached_property/property decorators and zero-argument super cell.
Their instances are controlled shells, not EventedModel/Pydantic constructors.
NumPy, cache factories, color/cache inputs and optional typed-dictionary services
are declared boundaries. Mapping public arrays, full model/provider/private
contracts, event invalidation and the optional-backend matrix remain outside
this packet. The minimum-dtype oracle is independently rebuilt as well.
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
SOURCE = ROOT / ".upstream/napari/src/napari/utils/colormaps/colormap.py"
CURRENT = ROOT / "src/napari/utils/colormaps/colormap.py"
PINNED_COMMIT = "595639295a153c84bcb127a9e07d801355440a35"
PINNED_SHA256 = "af2eed8dfa33850a7e8000cd3ac36cffca00d2519ffdcbed024aeae27819ff80"
DIRECT = (
    "_num_unique_colors", "_clear_cache", "_values_mapping_to_minimum_values_set",
    "_label_mapping_and_color_dict", "_get_typed_dict_mapping", "_array_map",
    "default_color",
)


def _segments(source, owner, names):
    members = next(node.body for node in ast.parse(source).body
                   if isinstance(node, ast.ClassDef) and node.name == owner)
    lines = source.splitlines(keepends=True)
    segments = []
    for name in names:
        node = next(node for node in members
                    if isinstance(node, ast.FunctionDef) and node.name == name)
        first = min([node.lineno, *(item.lineno for item in node.decorator_list)])
        segment = "".join(lines[first - 1:node.end_lineno])
        rebuilt = ast.parse(f"class {owner}:\n" + segment).body[0].body[0]
        assert ast.dump(rebuilt) == ast.dump(node)
        segments.append(segment)
    return segments


def _build(source, namespace, filename):
    namespace = dict(namespace)
    text = ("from __future__ import annotations\nclass LabelColormapBase:\n" +
            "\n".join(_segments(source, "LabelColormapBase", ("_clear_cache",))) +
            "\nclass DirectLabelColormap(LabelColormapBase):\n" +
            "\n".join(_segments(source, "DirectLabelColormap", DIRECT)))
    exec(compile(text, filename, "exec"), namespace)
    return SimpleNamespace(namespace=namespace, base=namespace["LabelColormapBase"],
                           direct=namespace["DirectLabelColormap"])


@pytest.fixture(scope="module")
def implementations():
    original = SOURCE.read_bytes()
    assert hashlib.sha256(original).hexdigest() == PINNED_SHA256
    module = importlib.import_module("napari.utils.colormaps.colormap")
    accel = importlib.import_module("napari.utils.colormaps._accelerated_cmap")
    accel_path = SOURCE.with_name("_accelerated_cmap.py")
    accel_raw = accel_path.read_bytes()
    assert hashlib.sha256(accel_raw).hexdigest() == (
        "ac4b1f4cb87980857811112d48676987e4fa722078f783681186b101a559d6b1"
    )
    accel_source = accel_raw.decode("utf-8")
    minimum = next(node for node in ast.parse(accel_source).body
                   if isinstance(node, ast.FunctionDef) and
                   node.name == "minimum_dtype_for_labels")
    minimum_text = "".join(accel_source.splitlines(keepends=True)[
        minimum.lineno - 1:minimum.end_lineno])
    assert ast.dump(ast.parse(minimum_text).body[0]) == ast.dump(minimum)
    accel_globals = dict(vars(accel))
    exec(compile("from __future__ import annotations\n" + minimum_text,
                 str(accel_path), "exec"), accel_globals)
    original_globals = dict(vars(module))
    original_globals["_accel_cmap"] = SimpleNamespace(
        minimum_dtype_for_labels=accel_globals["minimum_dtype_for_labels"],
        MAPPING_OF_UNKNOWN_VALUE=0,
    )
    pinned = _build(original.decode("utf-8"), original_globals, str(SOURCE))
    current = _build(CURRENT.read_text(encoding="utf-8"), vars(module), str(CURRENT))
    assert pinned.base is not current.base
    assert pinned.direct is not current.direct
    assert pinned.namespace["_accel_cmap"].minimum_dtype_for_labels is not (
        current.namespace["_accel_cmap"].minimum_dtype_for_labels
    )
    return pinned, current


def _instance(impl, color_dict=None):
    instance = impl.direct()
    instance.color_dict = {} if color_dict is None else color_dict
    instance.use_selection = False
    instance.selection = 7
    instance._cache_mapping = {"old-mapping": object()}
    instance._cache_other = {"old-other": object()}
    return instance


def _palette():
    default = np.array([0.25, 0.5, 0.75, 1], dtype=np.float32)
    red = np.array([1, 0, 0, 1], dtype=np.float32)
    green = np.array([0, 1, 0, 1], dtype=np.float32)
    transparent = np.zeros(4, dtype=np.float32)
    return {None: default, 7: red, -2: green, 9: red.copy(),
            0: transparent, 11: default}


def _compare(implementations, scenario, contract=None):
    expected = scenario(implementations[0])
    if contract is not None:
        assert expected == contract
    assert scenario(implementations[1]) == expected


def _error(call):
    try:
        call()
    except Exception as error:
        return type(error).__name__, str(error)
    raise AssertionError("Expected cache/provider failure")


def _traceback(call):
    try:
        call()
    except RuntimeError as error:
        return str(error), error.__traceback__
    raise AssertionError("Expected cache/provider failure")


def _canonical(instance):
    mapping, colors = instance._label_mapping_and_color_dict
    aliases = tuple((slot, tuple(label for label, raw in instance.color_dict.items()
                                if color is raw)) for slot, color in colors.items())
    return tuple(mapping.items()), aliases


def _typed_services(trace=None, dictionary_factory=dict):
    trace = [] if trace is None else trace
    module = ModuleType("numba")

    class Types:
        def __getattr__(self, name):
            trace.append(("type", name))
            return name

    class Dict:
        @staticmethod
        def empty(*, key_type, value_type):
            trace.append(("empty", key_type, value_type))
            return dictionary_factory()

    module.typed = SimpleNamespace(Dict=Dict)
    module.types = Types()
    return module


def test_exact_descriptors_and_base_closure_are_preserved(implementations):
    for impl in implementations:
        for name in (
            "_num_unique_colors", "_label_mapping_and_color_dict", "_array_map",
        ):
            descriptor = vars(impl.direct)[name]
            assert isinstance(descriptor, impl.namespace["cached_property"])
            assert descriptor.attrname == name
            assert descriptor.func.__globals__ is impl.namespace
        assert isinstance(vars(impl.direct)["default_color"], property)
        assert impl.direct.__mro__[1] is impl.base
        assert vars(impl.base)["_clear_cache"].__globals__ is impl.namespace
    clear = vars(implementations[0].direct)["_clear_cache"]
    assert clear.__closure__ is not None
    assert clear.__closure__[0].cell_contents is implementations[0].direct


def test_unique_count_and_canonical_dedup_keep_order_and_color_aliases(implementations):
    def scenario(impl):
        instance = _instance(impl, _palette())
        first = instance._label_mapping_and_color_dict
        return (instance._num_unique_colors, _canonical(instance),
                instance._label_mapping_and_color_dict is first,
                first[1][0] is first[1][4])

    _compare(implementations, scenario, (
        4,
        (((None, 0), (7, 1), (-2, 2), (9, 1), (0, 3), (11, 4)),
         ((0, (None, 11)), (1, (7,)), (2, (-2,)), (3, (0,)), (4, (None, 11)))),
        True, True,
    ))


def test_color_tuple_equality_preserves_signed_zero_and_nan_behavior(implementations):
    def scenario(impl):
        default = np.ones(4)
        positive_zero = np.zeros(4)
        negative_zero = -np.zeros(4)
        first_nan = np.array([np.nan, 0, 0, 1])
        second_nan = first_nan.copy()
        instance = _instance(impl, {None: default, 1: positive_zero, 2: negative_zero,
                                    3: first_nan, 4: second_nan})
        return instance._num_unique_colors, _canonical(instance)

    _compare(implementations, scenario, (
        4, (((None, 0), (1, 1), (2, 1), (3, 2), (4, 3)),
            ((0, (None,)), (1, (1,)), (2, (3,)), (3, (4,)))),
    ))


def test_cached_count_and_mapping_stay_stale_until_explicit_clear(implementations):
    def scenario(impl):
        instance = _instance(impl, _palette())
        first_count, first_mapping = instance._num_unique_colors, (
            instance._label_mapping_and_color_dict)
        first_array = instance._array_map
        first_array[7] = 91
        original_colors = instance.color_dict
        instance.color_dict[22] = np.array([0, 0, 1, 1], dtype=np.float32)
        stale = (instance._num_unique_colors == first_count,
                 instance._label_mapping_and_color_dict is first_mapping,
                 instance._array_map is first_array, int(instance._array_map[7]))
        instance._clear_cache()
        fresh = (instance._num_unique_colors,
                 instance._label_mapping_and_color_dict is not first_mapping,
                 instance._array_map is not first_array, int(instance._array_map[7]),
                 instance.color_dict is original_colors,
                 tuple(first_mapping[0].items()), int(first_array[7]))
        return stale, fresh

    _compare(implementations, scenario, (
        (True, True, True, 91),
        (5, True, True, 1, True,
         ((None, 0), (7, 1), (-2, 2), (9, 1), (0, 3), (11, 4)), 91),
    ))


@pytest.mark.parametrize("present", [False, True])
def test_default_color_returns_present_alias_or_fresh_integer_zeros(
    implementations, present,
):
    def scenario(impl):
        default = np.array([1, 0.5, 0.25, 1])
        instance = _instance(impl, {None: default} if present else {})
        first, second = instance.default_color, instance.default_color
        return first.tolist(), first.dtype.str, first is default, first is second

    contract = ([1, 0.5, 0.25, 1], "<f8", True, True) if present else (
        [0, 0, 0, 0], np.dtype(int).str, False, False)
    _compare(implementations, scenario, contract)


@pytest.mark.parametrize("failure", [False, True])
def test_default_color_captures_get_before_eager_fallback_allocation(
    implementations, monkeypatch, failure,
):
    def scenario(impl):
        trace = []
        value, zeros = object(), np.zeros(4, dtype=int)

        class Colors:
            current = None

            @property
            def get(self):
                trace.append("get-lookup")
                return self.current

        def get(key, default):
            trace.append(("get", key is None, default is zeros))
            return value

        def decoy(*args):
            raise AssertionError("captured get was loaded after allocation")

        def array(data):
            trace.append(("array", data))
            colors.current = decoy
            if failure:
                raise RuntimeError("default allocation failed")
            return zeros

        colors = Colors()
        colors.current = get
        instance = _instance(impl, colors)
        with monkeypatch.context() as patch:
            patch.setitem(impl.namespace, "np", SimpleNamespace(array=array))
            if failure:
                outcome = _error(lambda: instance.default_color)
            else:
                outcome = instance.default_color is value
        return tuple(trace), outcome

    prefix = ("get-lookup", ("array", (0, 0, 0, 0)))
    contract = (prefix, ("RuntimeError", "default allocation failed")) if failure else (
        prefix + (("get", True, True),), True)
    _compare(implementations, scenario, contract)


@pytest.mark.parametrize("selected", [7, None])
def test_selected_mapping_is_fresh_and_keeps_eager_default_and_color_alias(
    implementations, monkeypatch, selected,
):
    def scenario(impl):
        allocations = []

        def array(value):
            allocations.append(value)
            return np.array(value)

        instance = _instance(impl, _palette())
        instance.use_selection, instance.selection = True, selected
        with monkeypatch.context() as patch:
            patch.setitem(impl.namespace, "np", SimpleNamespace(array=array))
            first = instance._values_mapping_to_minimum_values_set()
            second = instance._values_mapping_to_minimum_values_set()
        return (tuple(first[0].items()), first[1][0].tolist(), first[1][0].dtype.str,
                first[1][1] is instance.color_dict[selected],
                first is not second, first[0] is not second[0],
                first[1] is not second[1], first[1][0] is not second[1][0],
                "_label_mapping_and_color_dict" not in instance.__dict__,
                tuple(allocations))

    mapping = ((7, 1), (None, 0)) if selected == 7 else ((None, 0),)
    _compare(implementations, scenario, (
        mapping, [0, 0, 0, 0], np.dtype(int).str, True, True, True, True, True, True,
        ((0, 0, 0, 0),) * 4,
    ))


@pytest.mark.parametrize("enabled", [False, True])
def test_unselected_mapping_returns_exact_canonical_cache_and_short_circuits(
    implementations, enabled,
):
    def scenario(impl):
        trace = []

        class Apply:
            def __bool__(self):
                trace.append("apply-selection")
                if not enabled:
                    raise AssertionError("selection-disabled branch evaluated apply")
                return False

        instance = _instance(impl, _palette())
        instance.use_selection = enabled
        original = instance._label_mapping_and_color_dict
        result = instance._values_mapping_to_minimum_values_set(Apply())
        return result is original, tuple(trace)

    _compare(implementations, scenario,
             (True, ("apply-selection",) if enabled else ()))


@pytest.mark.parametrize("negative", [False, True])
def test_array_map_uses_negative_indices_and_cached_array_alias(
    implementations, negative,
):
    def scenario(impl):
        colors = _palette() if negative else {None: np.zeros(4), 7: np.ones(4)}
        instance = _instance(impl, colors)
        result = instance._array_map
        cached = instance._array_map
        items = tuple((key, int(result[key])) for key in colors if key is not None)
        result[0] = 73
        return result.shape, result.dtype.str, items, result is cached, int(cached[0])

    contract = ((24,), "|u1", ((7, 1), (-2, 2), (9, 1), (0, 3), (11, 4)), True, 73)
    if not negative:
        contract = ((9,), "|u1", ((7, 1),), True, 73)
    _compare(implementations, scenario, contract)


@pytest.mark.parametrize(("key", "failure", "length"), [
    (None, False, 2), (65536, False, 65538), (65537, True, None),
    (-32768, False, 65538), (-32769, True, None),
])
def test_array_extent_cutoff_precedes_other_caches(
    implementations, key, failure, length,
):
    def scenario(impl):
        colors = {None: np.zeros(4)}
        if key is not None:
            colors[key] = np.ones(4)
        instance = _instance(impl, colors)
        if failure:
            outcome = _error(lambda: instance._array_map)
        else:
            array = instance._array_map
            outcome = array.shape, int(array[0]), int(array[-1])
        return outcome, tuple(name in instance.__dict__ for name in (
            "_num_unique_colors", "_label_mapping_and_color_dict", "_array_map"))

    if failure:
        contract = (("RuntimeError", "Cannot use numpy implementation for large values "
                     "of labels direct colormap. Please install numba."),
                    (False, False, False))
    else:
        contract = (((length,), 0, 0), (True, True, True))
    _compare(implementations, scenario, contract)


def test_array_selected_label_outside_raw_extent_raises_without_mapper_cache(
    implementations,
):
    def scenario(impl):
        instance = _instance(impl, {None: np.zeros(4), 1: np.ones(4)})
        instance.use_selection, instance.selection = True, 7
        error = _error(lambda: instance._array_map)
        return error, tuple(name in instance.__dict__ for name in (
            "_num_unique_colors", "_label_mapping_and_color_dict", "_array_map"))

    _compare(implementations, scenario, (
        ("IndexError", "index 7 is out of bounds for axis 0 with size 3"),
        (True, False, False),
    ))


def test_array_write_failure_retains_partial_mapper_only_in_saved_traceback(
    implementations, monkeypatch,
):
    def scenario(impl):
        trace, references = [], []

        class Mapper:
            def __init__(self):
                self.writes = []
                references.append(weakref.ref(self))

            def __setitem__(self, key, value):
                trace.append(("write", key, value))
                self.writes.append((key, value))
                if key == -2:
                    del self
                    raise RuntimeError("mapper write failed")

        def full(size, unknown, *, dtype):
            trace.append(("full", size, unknown, dtype.name))
            return Mapper()

        instance = _instance(impl, _palette())
        with monkeypatch.context() as patch:
            provider = SimpleNamespace(array=np.array, full=full)
            patch.setitem(impl.namespace, "np", provider)
            message, saved = _traceback(lambda: instance._array_map)
            retained = references[0]() is not None
            partial = references[0]().writes.copy()
            cached = tuple(name in instance.__dict__ for name in (
                "_num_unique_colors", "_label_mapping_and_color_dict", "_array_map"))
            del saved
            gc.collect()
            released = references[0]() is None
        return tuple(trace), message, retained, partial, cached, released

    _compare(implementations, scenario, (
        (("full", 24, 0, "uint8"), ("write", 7, 1), ("write", -2, 2)),
        "mapper write failed", True, [(7, 1), (-2, 2)], (True, True, False), True,
    ))


def test_typed_cache_hit_refetches_replaced_cache_before_lookup_and_import(
    implementations, monkeypatch,
):
    def scenario(impl):
        trace = []
        output = object()
        instance = _instance(impl)

        class Replacement(dict):
            def __getitem__(self, key):
                trace.append(("get", key))
                return output

        class Existing(dict):
            def __contains__(self, key):
                trace.append(("contains", key))
                instance._cache_other = Replacement()
                return True

            def __getitem__(self, key):
                raise AssertionError("old cache was used after membership callback")

        instance._cache_other = Existing()
        with monkeypatch.context() as patch:
            patch.setitem(sys.modules, "numba", None)
            result = instance._get_typed_dict_mapping(np.dtype(np.int64))
        return tuple(trace), result is output, "_num_unique_colors" in instance.__dict__

    _compare(implementations, scenario, (
        (("contains", "_int64_typed_dict"), ("get", "_int64_typed_dict")), True, False,
    ))


@pytest.mark.parametrize("dtype_name", ["int8", "uint8", "int64", "uint64"])
def test_typed_mapping_filters_none_and_dtype_bounds_before_casting(
    implementations, monkeypatch, dtype_name,
):
    def scenario(impl):
        dtype = np.dtype(dtype_name)
        info = np.iinfo(dtype)
        labels = [None, int(info.min) - 1, int(info.min), int(info.min) + 1,
                  int(info.max) - 1, int(info.max), int(info.max) + 1]
        mapping = {key: value for value, key in enumerate(labels)}
        instance = _instance(impl)
        instance.__dict__.update(_num_unique_colors=4,
                                 _label_mapping_and_color_dict=(mapping, {}))
        with monkeypatch.context() as patch:
            patch.setitem(sys.modules, "numba", _typed_services())
            result = instance._get_typed_dict_mapping(dtype)
        return (tuple((int(key), int(value), type(key).__name__, type(value).__name__)
                      for key, value in result.items()),
                instance._cache_other[f"_{dtype}_typed_dict"] is result,
                instance._get_typed_dict_mapping(dtype) is result)

    dtype = np.dtype(dtype_name)
    info = np.iinfo(dtype)
    values = (int(info.min), int(info.min) + 1, int(info.max) - 1, int(info.max))
    _compare(implementations, scenario, (
        tuple((key, value, dtype_name, "uint8") for key, value in zip(
            values, (2, 3, 4, 5), strict=True)), True, True,
    ))


@pytest.mark.parametrize(("count", "target"), [
    (253, "uint8"), (254, "uint16"), (65533, "uint16"), (65534, "float32"),
])
def test_typed_target_uses_unique_count_plus_two(
    implementations, monkeypatch, count, target,
):
    def scenario(impl):
        trace = []
        instance = _instance(impl)
        instance.__dict__.update(_num_unique_colors=count,
                                 _label_mapping_and_color_dict=({None: 0, 7: 1}, {}))
        with monkeypatch.context() as patch:
            patch.setitem(sys.modules, "numba", _typed_services(trace))
            result = instance._get_typed_dict_mapping(np.dtype(np.int64))
        return (tuple(trace),
                tuple((int(k), type(v).__name__) for k, v in result.items()))

    _compare(implementations, scenario, (
        (("type", "int64"), ("type", target), ("empty", "int64", target)),
        ((7, target),),
    ))


def test_typed_value_cast_precedes_key_cast_and_publication_refetches_cache(
    implementations, monkeypatch,
):
    def scenario(impl):
        trace = []
        instance = _instance(impl)
        replacement = {}

        class DataDtype:
            name = "input-kind"

            def __str__(self):
                trace.append("format-dtype")
                return "input-dtype"

            def type(self, value):
                trace.append(("key-cast", value))
                return value + 100

        class Target:
            name = "output-kind"

            def type(self, value):
                trace.append(("value-cast", value))
                return value + 200

        class Mapping(dict):
            def __setitem__(self, key, value):
                trace.append(("insert", key, value))
                instance._cache_other = replacement
                super().__setitem__(key, value)

        def minimum(count):
            trace.append(("minimum", count))
            return Target()

        def iinfo(dtype):
            trace.append(("iinfo", dtype is data_dtype))
            return SimpleNamespace(min=-8, max=7)

        data_dtype = DataDtype()
        instance.__dict__.update(_num_unique_colors=3,
                                 _label_mapping_and_color_dict=({None: 0, 2: 5}, {}))
        original_cache = instance._cache_other
        with monkeypatch.context() as patch:
            patch.setitem(sys.modules, "numba", _typed_services(trace, Mapping))
            patch.setitem(impl.namespace, "np", SimpleNamespace(iinfo=iinfo))
            patch.setitem(impl.namespace, "_accel_cmap",
                          SimpleNamespace(minimum_dtype_for_labels=minimum))
            result = instance._get_typed_dict_mapping(data_dtype)
        return (tuple(trace), tuple(result.items()),
                replacement["_input-dtype_typed_dict"] is result,
                "_input-dtype_typed_dict" not in original_cache)

    _compare(implementations, scenario, (
        ("format-dtype", ("minimum", 5), ("type", "input-kind"),
         ("type", "output-kind"), ("empty", "input-kind", "output-kind"),
         ("iinfo", True), ("value-cast", 5), ("key-cast", 2), ("insert", 102, 205)),
        ((102, 205),), True, True,
    ))


def test_typed_miss_with_missing_numba_leaves_cache_and_properties_untouched(
    implementations, monkeypatch,
):
    def scenario(impl):
        instance = _instance(impl)
        original = instance._cache_other
        with monkeypatch.context() as patch:
            patch.setitem(sys.modules, "numba", None)
            kind, message = _error(lambda:
                                   instance._get_typed_dict_mapping(np.dtype(np.int64)))
        return (kind, "numba" in message, instance._cache_other is original,
                tuple(instance._cache_other), "_num_unique_colors" in instance.__dict__)

    _compare(implementations, scenario, (
        "ModuleNotFoundError", True, True, ("old-other",), False,
    ))


def test_typed_insert_error_keeps_partial_local_without_publishing_cache(
    implementations, monkeypatch,
):
    def scenario(impl):
        references = []
        instance = _instance(impl)
        instance.__dict__.update(
            _num_unique_colors=2,
            _label_mapping_and_color_dict=({None: 0, 2: 3, 4: 5}, {}),
        )

        class Mapping(dict):
            def __init__(self):
                references.append(weakref.ref(self))

            def __setitem__(self, key, value):
                super().__setitem__(key, value)
                if int(key) == 4:
                    del self
                    raise RuntimeError("typed insert failed")

        with monkeypatch.context() as patch:
            patch.setitem(sys.modules, "numba",
                          _typed_services(dictionary_factory=Mapping))
            message, saved = _traceback(lambda:
                                       instance._get_typed_dict_mapping(np.dtype(np.int64)))
            retained = references[0]() is not None
            partial = tuple((int(k), int(v)) for k, v in references[0]().items())
            published = "_int64_typed_dict" in instance._cache_other
            del saved
            gc.collect()
            released = references[0]() is None
        return message, retained, partial, published, released

    _compare(implementations, scenario, (
        "typed insert failed", True, ((2, 3), (4, 5)), False, True,
    ))


def test_real_optional_numba_typed_cache_matches_original(implementations):
    numba = pytest.importorskip(
        "numba", reason="real typed-container provider required")

    def scenario(impl):
        instance = _instance(impl, _palette())
        result = instance._get_typed_dict_mapping(np.dtype(np.int64))
        dtype = numba.typeof(result)
        return (tuple((int(k), int(v)) for k, v in result.items()),
                str(dtype.key_type), str(dtype.value_type),
                instance._get_typed_dict_mapping(np.dtype(np.int64)) is result)

    _compare(implementations, scenario, (
        ((7, 1), (-2, 2), (9, 1), (0, 3), (11, 4)), "int64", "uint8", True,
    ))


def test_base_clear_replaces_both_caches_and_preserves_populated_old_aliases(
    implementations, monkeypatch,
):
    def scenario(impl):
        instance = _instance(impl)
        old_mapping, old_other = instance._cache_mapping, instance._cache_other
        created = []

        def factory():
            result = {}
            created.append(result)
            return result

        with monkeypatch.context() as patch:
            patch.setitem(impl.namespace, "_RebuildableCache", factory)
            impl.base._clear_cache(instance)
        return (instance._cache_mapping is created[0],
                instance._cache_other is created[1],
                created[0] is not created[1], tuple(old_mapping), tuple(old_other))

    _compare(implementations, scenario, (
        True, True, True, ("old-mapping",), ("old-other",),
    ))


def test_direct_clear_deletes_only_three_cached_properties_in_original_order(
    implementations, monkeypatch,
):
    def scenario(impl):
        trace = []

        class Journal(dict):
            def __contains__(self, key):
                trace.append(("contains", key))
                return super().__contains__(key)

            def __delitem__(self, key):
                trace.append(("delete", key))
                super().__delitem__(key)

        instance = _instance(impl, _palette())
        journal = Journal(_num_unique_colors=7, _label_mapping_and_color_dict=object(),
                          _array_map=object(), unrelated=object())

        class Shell(impl.direct):
            @property
            def __dict__(self):
                return journal

        controlled = Shell()
        controlled.color_dict = instance.color_dict
        controlled._cache_mapping, controlled._cache_other = {}, {}
        controlled.use_selection, controlled.selection = False, 7
        with monkeypatch.context() as patch:
            patch.setitem(impl.namespace, "_RebuildableCache", dict)
            controlled._clear_cache()
        return (tuple(trace), tuple(journal),
                controlled.color_dict is instance.color_dict)

    _compare(implementations, scenario, (
        (("contains", "_num_unique_colors"), ("delete", "_num_unique_colors"),
         ("contains", "_label_mapping_and_color_dict"),
         ("delete", "_label_mapping_and_color_dict"),
         ("contains", "_array_map"), ("delete", "_array_map")),
        ("unrelated",), True,
    ))


def test_second_clear_factory_error_keeps_first_replacement_and_direct_caches(
    implementations, monkeypatch,
):
    def scenario(impl):
        instance = _instance(impl, _palette())
        instance.__dict__.update(
            _num_unique_colors=7,
            _label_mapping_and_color_dict=object(), _array_map=object(),
        )
        original_other = instance._cache_other
        replacement = {}
        calls = []

        def factory():
            calls.append(len(calls) + 1)
            if len(calls) == 2:
                raise RuntimeError("second cache factory failed")
            return replacement

        with monkeypatch.context() as patch:
            patch.setitem(impl.namespace, "_RebuildableCache", factory)
            error = _error(instance._clear_cache)
        return (tuple(calls), error, instance._cache_mapping is replacement,
                instance._cache_other is original_other,
                tuple(name in instance.__dict__ for name in (
                    "_num_unique_colors", "_label_mapping_and_color_dict", "_array_map",
                )))

    _compare(implementations, scenario, (
        (1, 2), ("RuntimeError", "second cache factory failed"), True, True,
        (True, True, True),
    ))
