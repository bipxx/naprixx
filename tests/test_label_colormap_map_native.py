"""Twenty-five finite comparisons for the eight label RGBA mapping bodies.

Every targeted body is rebuilt in an independent Base/Cyclic/Direct hierarchy.
The pinned hierarchy also uses independently rebuilt pinned texture, minimum
dtype, NumPy modulo/direct-backend and Direct cache callees. The current
hierarchy uses extracted current bodies and cache facades with the explicitly
selected current NumPy backend. Constructors/model_dump are controlled model
boundaries; NumPy is an external array service. These cases do not establish
Pydantic/event/provider closure, optional backend typing, or whole-module parity.
"""

from __future__ import annotations

import ast
import hashlib
import importlib
import inspect
from functools import cached_property
from pathlib import Path
from types import SimpleNamespace

import numpy as np
import pytest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / ".upstream/napari/src/napari/utils/colormaps/colormap.py"
CURRENT = ROOT / "src/napari/utils/colormaps/colormap.py"
PINNED_COMMIT = "595639295a153c84bcb127a9e07d801355440a35"
PINNED_SHA256 = "af2eed8dfa33850a7e8000cd3ac36cffca00d2519ffdcbed024aeae27819ff80"
BASE = ("_get_mapping_from_cache", "_num_unique_colors", "_cmap_without_selection")
CYCLIC = ("_map_without_cache", "map")
DIRECT = ("map", "_map_without_cache", "_map_precast")
DIRECT_CALLEES = (
    "_num_unique_colors", "_values_mapping_to_minimum_values_set",
    "_label_mapping_and_color_dict", "_array_map", "default_color",
)


def _segment(source, node, *, owner=None):
    lines = source.splitlines(keepends=True)
    first = min([node.lineno, *(item.lineno for item in node.decorator_list)])
    text = "".join(lines[first - 1:node.end_lineno])
    parsed = ast.parse((f"class {owner}:\n" if owner else "") + text).body[0]
    rebuilt = parsed.body[0] if owner else parsed
    assert ast.dump(rebuilt) == ast.dump(node)
    return text


def _methods(source, owner, names):
    members = next(node.body for node in ast.parse(source).body
                   if isinstance(node, ast.ClassDef) and node.name == owner)
    return [_segment(source, next(node for node in members
                                  if isinstance(node, ast.FunctionDef) and
                                  node.name == name), owner=owner)
            for name in names]


def _function(source, name):
    node = next(node for node in ast.parse(source).body
                if isinstance(node, ast.FunctionDef) and node.name == name)
    return _segment(source, node)


def _pinned_acceleration():
    path = SOURCE.with_name("_accelerated_cmap.py")
    raw = path.read_bytes()
    assert hashlib.sha256(raw).hexdigest() == (
        "ac4b1f4cb87980857811112d48676987e4fa722078f783681186b101a559d6b1"
    )
    source = raw.decode("utf-8")
    namespace = {"np": np}
    parts = [_function(source, name) for name in (
        "minimum_dtype_for_labels", "zero_preserving_modulo_numpy",
        "_labels_raw_to_texture_direct_numpy",
    )]
    # Execute the exact pinned version block, preserving both nested definitions
    # and their docstrings rather than dedenting a function's multiline text.
    block = next(node for node in ast.parse(source).body
                 if isinstance(node, ast.If) and
                 any(isinstance(item, ast.FunctionDef) and item.name == "clip"
                     for item in node.body))
    lines = source.splitlines(keepends=True)
    block_text = "".join(lines[block.lineno - 1:block.end_lineno])
    assert ast.dump(ast.parse(block_text).body[0]) == ast.dump(block)
    text = ("from __future__ import annotations\n"
            "from importlib.metadata import version\n"
            "from packaging.version import parse\n" +
            "\n".join(parts) + "\n" + block_text)
    exec(compile(text, str(path), "exec"), namespace)
    return SimpleNamespace(
        minimum_dtype_for_labels=namespace["minimum_dtype_for_labels"],
        zero_preserving_modulo_numpy=namespace["zero_preserving_modulo_numpy"],
        labels_raw_to_texture_direct=namespace["_labels_raw_to_texture_direct_numpy"],
        MAPPING_OF_UNKNOWN_VALUE=0,
    )


def _build(source, namespace, filename):
    namespace = dict(namespace)
    text = ("from __future__ import annotations\n" +
            _function(source, "_texture_dtype") +
            "\nclass LabelColormapBase:\n" +
            "\n".join(_methods(source, "LabelColormapBase", BASE)) +
            "\nclass CyclicLabelColormap(LabelColormapBase):\n" +
            "\n".join(_methods(source, "CyclicLabelColormap", CYCLIC)) +
            "\nclass DirectLabelColormap(LabelColormapBase):\n" +
            "\n".join(_methods(source, "DirectLabelColormap", DIRECT + DIRECT_CALLEES)))
    exec(compile(text, filename, "exec"), namespace)
    return SimpleNamespace(namespace=namespace, base=namespace["LabelColormapBase"],
                           cyclic=namespace["CyclicLabelColormap"],
                           direct=namespace["DirectLabelColormap"])


@pytest.fixture(scope="module")
def implementations():
    raw = SOURCE.read_bytes()
    assert hashlib.sha256(raw).hexdigest() == PINNED_SHA256
    original_namespace = {"np": np, "cached_property": cached_property,
                          "_accel_cmap": _pinned_acceleration()}
    module = importlib.import_module("napari.utils.colormaps.colormap")
    accel = importlib.import_module("napari.utils.colormaps._accelerated_cmap")
    current_namespace = dict(vars(module))
    current_namespace["_accel_cmap"] = SimpleNamespace(
        minimum_dtype_for_labels=accel.minimum_dtype_for_labels,
        zero_preserving_modulo_numpy=accel.zero_preserving_modulo_numpy,
        labels_raw_to_texture_direct=accel._labels_raw_to_texture_direct_numpy,
        MAPPING_OF_UNKNOWN_VALUE=accel.MAPPING_OF_UNKNOWN_VALUE,
    )
    pinned = _build(raw.decode("utf-8"), original_namespace, str(SOURCE))
    current = _build(CURRENT.read_text(encoding="utf-8"), current_namespace,
                     str(CURRENT))
    for name in ("base", "cyclic", "direct"):
        assert getattr(pinned, name) is not getattr(current, name)
    for name in ("minimum_dtype_for_labels", "zero_preserving_modulo_numpy",
                 "labels_raw_to_texture_direct"):
        assert getattr(pinned.namespace["_accel_cmap"], name) is not (
            getattr(current.namespace["_accel_cmap"], name))
    return pinned, current


def _colors():
    return np.array([[0.9, 0.8, 0.7, 1], [1, 0, 0, 1],
                     [0, 1, 0, 1], [0, 0, 1, 1]], dtype=np.float32)


def _palette():
    return {None: np.array([0.25, 0.5, 0.75, 1], dtype=np.float32),
            7: np.array([1, 0, 0, 1], dtype=np.float32),
            -2: np.array([0, 1, 0, 1], dtype=np.float32),
            0: np.zeros(4, dtype=np.float32)}


def _instance(impl, owner="direct", **data):
    parent = getattr(impl, owner)

    class Model(parent):
        def __init__(self, **values):
            for key, value in values.items():
                setattr(self, key, value)
            self._cache_mapping = {}
            self._cache_other = {}

        def model_dump(self):
            return {key: getattr(self, key) for key in fields}

    values = {"colors": _colors(), "color_dict": _palette(),
              "use_selection": False, "selection": 7, "background_value": 0}
    values.update(data)
    fields = tuple(values)
    return Model(**values)


def _array(value):
    return value.shape, str(value.dtype), value.tolist()


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
    raise AssertionError("Expected mapping/provider failure")


def test_all_eight_bodies_have_independent_hierarchies_and_exact_metadata(
    implementations,
):
    pinned, current = implementations
    for owner, names in (("base", BASE), ("cyclic", CYCLIC), ("direct", DIRECT)):
        for name in names:
            first, second = vars(getattr(pinned, owner))[name], (
                vars(getattr(current, owner))[name])
            if isinstance(first, property):
                assert isinstance(second, property)
                first, second = first.fget, second.fget
            assert first.__globals__ is pinned.namespace
            assert second.__globals__ is current.namespace
            assert inspect.signature(first) == inspect.signature(second)
            assert first.__doc__ == second.__doc__
    for impl in implementations:
        assert impl.cyclic.__mro__[1] is impl.base
        assert impl.direct.__mro__[1] is impl.base
        assert isinstance(vars(impl.base)["_num_unique_colors"], property)
        assert isinstance(vars(impl.direct)["_num_unique_colors"], cached_property)


def test_base_count_is_uncached_and_direct_count_uses_its_cached_override(
    implementations,
):
    def scenario(impl):
        base = _instance(impl, "base")
        first = base._num_unique_colors
        base.colors = base.colors[:2]
        direct = _instance(impl)
        direct_count = direct._num_unique_colors
        direct.color_dict[8] = np.array([0, 0, 1, 1], dtype=np.float32)
        return (first, base._num_unique_colors, direct_count,
                direct._num_unique_colors, "_num_unique_colors" in direct.__dict__,
                "_num_unique_colors" in base.__dict__)

    _compare(implementations, scenario, (3, 1, 4, 4, True, False))


@pytest.mark.parametrize("raw_dtype", [np.int8, np.uint8, np.int16, np.uint16])
def test_small_cache_builds_complete_unsigned_domain_in_raw_dtype_and_hits_alias(
    implementations, raw_dtype,
):
    def scenario(impl):
        instance = _instance(impl, "base")
        calls = []
        dtype = np.dtype(raw_dtype)
        target = np.dtype(np.uint8 if dtype.itemsize == 1 else np.uint16)
        expected_data = np.arange(np.iinfo(target).max + 1,
                                  dtype=target).astype(dtype)
        mapped = np.zeros((expected_data.size, 4), dtype=np.float32)

        def provider(data):
            calls.append((str(data.dtype), data.shape))
            np.testing.assert_array_equal(data, expected_data)
            return mapped

        instance._map_without_cache = provider
        first = instance._get_mapping_from_cache(dtype)
        first[0, 0] = 19
        second = instance._get_mapping_from_cache(dtype)
        return (calls, first is mapped, second is first,
                tuple((str(raw), str(texture)) for raw, texture in
                      instance._cache_mapping), float(second[0, 0]),
                tuple(int(x) for x in expected_data[[0, 1, -2, -1]]))

    _compare(implementations, scenario)


def test_wide_cache_misses_skip_build_but_existing_entries_still_return_exactly(
    implementations,
):
    def scenario(impl):
        results = []
        for raw_dtype in (np.int32, np.float64):
            instance = _instance(impl, "base")
            dtype, target = np.dtype(raw_dtype), np.dtype(np.uint8)
            calls = []
            instance._map_without_cache = lambda data, calls=calls: calls.append(data)
            absent = instance._get_mapping_from_cache(dtype)
            marker = object()
            instance._cache_mapping[(dtype, target)] = marker
            present = instance._get_mapping_from_cache(dtype)
            results.append((absent is None, present is marker, len(calls)))
        return results

    _compare(implementations, scenario, [(True, True, 0), (True, True, 0)])


def test_cache_provider_failure_does_not_publish_or_fetch_an_entry(implementations):
    def scenario(impl):
        trace = []

        class Cache(dict):
            def __contains__(self, key):
                trace.append(("contains", str(key[0]), str(key[1])))
                return super().__contains__(key)

            def __setitem__(self, key, value):
                trace.append(("publish",))
                super().__setitem__(key, value)

            def get(self, key):
                trace.append(("get",))
                return super().get(key)

        instance = _instance(impl, "base")
        instance._cache_mapping = Cache()

        def provider(data):
            trace.append(("map", str(data.dtype), data.size))
            raise RuntimeError("cache map failed")

        instance._map_without_cache = provider
        error = _error(lambda: instance._get_mapping_from_cache(np.dtype(np.int8)))
        return error, trace, len(instance._cache_mapping)

    _compare(implementations, scenario, (
        ("RuntimeError", "cache map failed"),
        [("contains", "int8", "uint8"), ("map", "int8", 256)], 0,
    ))


def test_no_selection_returns_self_and_selection_clone_preserves_constructor_order(
    implementations,
):
    def scenario(impl):
        trace, failure = [], [False]

        class Model(impl.base):
            def __init__(self, *, use_selection, selection, colors):
                trace.append(("construct", use_selection, selection))
                if failure[0]:
                    raise RuntimeError("constructor failed")
                self.use_selection = use_selection
                self.selection = selection
                self.colors = colors

            def model_dump(self):
                trace.append(("dump",))
                return {"use_selection": self.use_selection,
                        "selection": self.selection, "colors": self.colors}

        instance = Model(use_selection=False, selection=7, colors=_colors())
        trace.clear()
        unchanged = instance._cmap_without_selection() is instance
        assert trace == []
        instance.use_selection = True
        cloned = instance._cmap_without_selection()
        successful_trace = list(trace)
        trace.clear()
        failure[0] = True
        error = _error(instance._cmap_without_selection)
        return (unchanged, type(cloned) is type(instance), cloned is not instance,
                cloned.colors is instance.colors, cloned.selection,
                cloned.use_selection, instance.use_selection,
                successful_trace, error, trace)

    _compare(implementations, scenario, (
        True, True, True, True, 7, False, True,
        [("dump",), ("construct", True, 7)],
        ("RuntimeError", "constructor failed"),
        [("dump",), ("construct", True, 7)],
    ))


def test_cyclic_map_keeps_scalar_multidimensional_noncontiguous_and_empty_shapes(
    implementations,
):
    def scenario(impl):
        values = (7, np.int16(-2), np.arange(12, dtype=np.int32).reshape(3, 4)[:, ::2],
                  np.empty((0, 2), dtype=np.uint8))
        results = []
        for data in values:
            instance = _instance(impl, "cyclic")
            mapped = instance.map(data)
            assert mapped.shape == np.shape(data) + (4,)
            assert mapped.dtype == np.float32
            assert not np.shares_memory(mapped, instance.colors)
            results.append(_array(mapped))
        return results

    _compare(implementations, scenario)


def test_cyclic_uncached_background_is_transparent_without_mutating_palette(
    implementations,
):
    def scenario(impl):
        instance = _instance(impl, "cyclic", background_value=-2)
        before = instance.colors.copy()
        values = np.array([-5, -2, 0, 1, 2], dtype=np.int32)
        mapped = instance._map_without_cache(values)
        np.testing.assert_array_equal(instance.colors, before)
        np.testing.assert_array_equal(mapped[1], np.zeros(4))
        return _array(mapped), _array(values), np.shares_memory(mapped, instance.colors)

    _compare(implementations, scenario)


def test_cyclic_float_cast_precedes_cache_lookup_and_selection_mask(implementations):
    def scenario(impl):
        trace = []
        instance = _instance(impl, "cyclic", use_selection=True, selection=1)
        original_map = instance._map_without_cache

        def cache(dtype):
            trace.append(("cache", str(dtype)))
            return None

        def mapping(values):
            trace.append(("map", str(values.dtype), values.tolist()))
            return original_map(values)

        instance._get_mapping_from_cache = cache
        instance._map_without_cache = mapping
        values = np.array([1.9, 1.1, -1.2], dtype=np.float32)
        mapped = instance.map(values)
        return trace, _array(mapped), _array(values)

    _compare(implementations, scenario)


def test_cyclic_selection_masks_a_copy_and_keeps_cached_rgba_unselected(
    implementations,
):
    def scenario(impl):
        instance = _instance(impl, "cyclic", use_selection=True, selection=1)
        values = np.array([0, 1, 2, 3], dtype=np.uint8)
        cached = instance._get_mapping_from_cache(values.dtype)
        before = cached.copy()
        selected = instance.map(values)
        np.testing.assert_array_equal(cached, before)
        instance.use_selection = False
        unselected = instance.map(values)
        np.testing.assert_array_equal(unselected, cached[values])
        return (_array(selected), _array(unselected),
                instance._get_mapping_from_cache(values.dtype) is cached,
                np.shares_memory(selected, cached))

    _compare(implementations, scenario)


def test_cyclic_cached_index_failure_precedes_uncached_fallback(implementations):
    def scenario(impl):
        instance = _instance(impl, "cyclic", use_selection=True)
        trace = []
        instance._get_mapping_from_cache = lambda dtype: np.ones((2, 4))
        instance._map_without_cache = lambda values: trace.append("fallback")
        error = _error(lambda: instance.map(np.array([2], dtype=np.uint8)))
        return error, trace

    _compare(implementations, scenario)


def test_direct_scalar_and_numpy_integer_keep_color_aliases_and_eager_default(
    implementations,
):
    def scenario(impl):
        trace = []

        class Colors(dict):
            def get(self, key, default):
                trace.append(("get", type(key).__name__, key))
                return super().get(key, default)

        instance = _instance(impl)
        instance.color_dict = Colors(instance.color_dict)
        known = instance.map(np.int16(-2))
        known_trace = list(trace)
        trace.clear()
        unknown = instance.map(99)
        unknown_trace = list(trace)
        trace.clear()
        instance.use_selection = True
        rejected = instance.map(-2)
        rejected_trace = list(trace)
        instance.selection = 99
        selected_unknown = instance.map(99)
        return (known is instance.color_dict[-2], known_trace,
                unknown is instance.color_dict[None], unknown_trace,
                _array(rejected), rejected_trace,
                selected_unknown is instance.color_dict[None])

    _compare(implementations, scenario, (
        True, [("get", "NoneType", None), ("get", "int", -2)],
        True, [("get", "NoneType", None), ("get", "int", 99)],
        ((4,), "int64", [0, 0, 0, 0]), [], True,
    ))


@pytest.mark.parametrize("container", [list, tuple], ids=["list", "tuple"])
def test_direct_containers_normalize_to_arrays_then_map(implementations, container):
    def scenario(impl):
        instance = _instance(impl)
        values = container([container([7, 99]), container([-2, 0])])
        mapped = instance.map(values)
        assert mapped.shape == (2, 2, 4)
        assert mapped.dtype == np.float32
        return _array(mapped), values == container([container([7, 99]),
                                                    container([-2, 0])])

    _compare(implementations, scenario)


def test_direct_validation_rejects_only_the_named_float_unicode_and_nonarray_cases(
    implementations,
):
    def scenario(impl):
        instance = _instance(impl)
        values = (np.array([1.0], dtype=np.float32), np.array(["1"]), object())
        return [_error(lambda value=value: instance.map(value)) for value in values]

    _compare(implementations, scenario, [
        ("TypeError", "DirectLabelColormap can only be used with int"),
        ("TypeError", "DirectLabelColormap can only be used with int"),
        ("TypeError", "DirectLabelColormap can only be used with int"),
    ])


def test_direct_other_dtype_kinds_reach_the_dynamic_backend_boundary(
    implementations, monkeypatch,
):
    def scenario(impl):
        results = []
        for values in (np.array([True]), np.array([1], dtype=object),
                       np.array([1 + 2j]), np.array([b"1"])):
            instance = _instance(impl)
            trace = []

            def cache(dtype, trace=trace):
                trace.append(("cache", dtype.kind))
                return None

            def backend(data, cmap, trace=trace, instance=instance):
                trace.append(("backend", data.dtype.kind, cmap is instance))
                raise RuntimeError("backend boundary")

            instance._get_mapping_from_cache = cache
            services = SimpleNamespace(**vars(impl.namespace["_accel_cmap"]))
            services.labels_raw_to_texture_direct = backend
            with monkeypatch.context() as patch:
                patch.setitem(impl.namespace, "_accel_cmap", services)
                results.append((_error(lambda instance=instance, values=values:
                                       instance.map(values)), trace))
        return results

    _compare(implementations, scenario)


def test_direct_signed_small_cache_is_unselected_and_outer_mask_keeps_it_intact(
    implementations,
):
    def scenario(impl):
        instance = _instance(impl, use_selection=True, selection=7)
        values = np.array([-2, 7, 0, 9], dtype=np.int8)
        cached = instance._get_mapping_from_cache(values.dtype)
        before = cached.copy()
        mapped = instance.map(values)
        np.testing.assert_array_equal(cached, before)
        np.testing.assert_array_equal(cached[-2], instance.color_dict[-2])
        np.testing.assert_array_equal(mapped[0], np.zeros(4))
        np.testing.assert_array_equal(mapped[1], instance.color_dict[7])
        return (_array(mapped), _array(cached[values]),
                instance._get_mapping_from_cache(values.dtype) is cached,
                instance.use_selection, np.shares_memory(mapped, cached))

    _compare(implementations, scenario)


def test_direct_wide_map_calls_dynamic_backend_then_original_precast_with_selection(
    implementations, monkeypatch,
):
    def scenario(impl):
        instance = _instance(impl, use_selection=True, selection=7)
        trace = []
        services = SimpleNamespace(**vars(impl.namespace["_accel_cmap"]))
        provider, precast = services.labels_raw_to_texture_direct, instance._map_precast

        def backend(values, cmap):
            trace.append(("backend", cmap is instance, cmap.use_selection,
                          str(values.dtype), values.tolist()))
            return provider(values, cmap)

        def mapping(values, apply_selection):
            trace.append(("precast", apply_selection, str(values.dtype),
                          values.tolist()))
            return precast(values, apply_selection)

        services.labels_raw_to_texture_direct = backend
        instance._map_precast = mapping
        with monkeypatch.context() as patch:
            patch.setitem(impl.namespace, "_accel_cmap", services)
            mapped = instance.map(np.array([[7, -2, 99]], dtype=np.int32))
        return trace, _array(mapped), tuple(instance._cache_mapping)

    _compare(implementations, scenario)


def test_direct_without_cache_clones_for_backend_but_uses_original_self_precast(
    implementations, monkeypatch,
):
    def scenario(impl):
        instance = _instance(impl, use_selection=True, selection=7)
        trace = []
        services = SimpleNamespace(**vars(impl.namespace["_accel_cmap"]))
        provider, precast, dump = (services.labels_raw_to_texture_direct,
                                  instance._map_precast, instance.model_dump)

        def model_dump():
            trace.append(("dump",))
            return dump()

        def backend(values, cmap):
            trace.append(("backend", cmap is instance, type(cmap) is type(instance),
                          cmap.use_selection, cmap.color_dict is instance.color_dict))
            return provider(values, cmap)

        def mapping(values, apply_selection):
            trace.append(("precast", apply_selection, instance.use_selection))
            return precast(values, apply_selection)

        services.labels_raw_to_texture_direct = backend
        instance.model_dump = model_dump
        instance._map_precast = mapping
        with monkeypatch.context() as patch:
            patch.setitem(impl.namespace, "_accel_cmap", services)
            mapped = instance._map_without_cache(np.array([-2, 7, 99], dtype=np.int32))
        return trace, _array(mapped), instance.use_selection

    _compare(implementations, scenario)


def test_precast_has_float32_c_order_and_preserves_scalar_empty_and_strided_shapes(
    implementations,
):
    def scenario(impl):
        values = (np.array([[1, 2], [2, 0]], dtype=np.uint8, order="F"),
                  np.array([[1, 2, 0], [2, 0, 1]], dtype=np.uint8)[:, ::2],
                  np.array(1, dtype=np.uint8), np.empty((0, 2), dtype=np.uint8))
        results = []
        for data in values:
            instance = _instance(impl)
            mapped = instance._map_precast(data, apply_selection=False)
            assert mapped.shape == data.shape + (4,)
            assert mapped.dtype == np.float32
            assert mapped.flags.c_contiguous
            results.append(_array(mapped))
        return results

    _compare(implementations, scenario)


def test_precast_passes_selection_flag_to_minimum_mapping(implementations):
    def scenario(impl):
        instance = _instance(impl, use_selection=True, selection=7)
        values = np.array([0, 1], dtype=np.uint8)
        selected = instance._map_precast(values, apply_selection=True)
        unselected = instance._map_precast(values, apply_selection=False)
        np.testing.assert_array_equal(selected[0], np.zeros(4))
        np.testing.assert_array_equal(unselected[0], instance.color_dict[None])
        np.testing.assert_array_equal(selected[1], instance.color_dict[7])
        return _array(selected), _array(unselected)

    _compare(implementations, scenario)


def test_precast_c_order_failure_keeps_the_exact_partial_float32_output(
    implementations, monkeypatch,
):
    def scenario(impl):
        instance = _instance(impl)
        trace, allocated = [], []

        class NumPyService:
            def __getattr__(self, name):
                return getattr(np, name)

            def zeros(self, shape, *, dtype):
                mapped = np.zeros(shape, dtype=dtype)
                allocated.append(mapped)
                return mapped

        class Colors(dict):
            def __getitem__(self, key):
                trace.append(int(key))
                return super().__getitem__(key)

        colors = Colors({1: instance.color_dict[7], 2: instance.color_dict[-2]})
        instance._values_mapping_to_minimum_values_set = lambda flag: ({}, colors)
        values = np.array([[1, 2], [99, 1]], dtype=np.uint8, order="F")
        with monkeypatch.context() as patch:
            patch.setitem(impl.namespace, "np", NumPyService())
            error = _error(lambda: instance._map_precast(values, apply_selection=False))
        assert len(allocated) == 1
        np.testing.assert_array_equal(allocated[0][0],
                                      [instance.color_dict[7], instance.color_dict[-2]])
        np.testing.assert_array_equal(allocated[0][1], np.zeros((2, 4)))
        return error, trace, _array(allocated[0]), allocated[0].flags.c_contiguous

    _compare(implementations, scenario)
