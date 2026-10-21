"""Finite independent comparisons of the 31 slicing-input helper bodies.

All owned callees are rebuilt together from independently pinned/current source.
Real Dask Cache/config, NumPy, functools LRU/cached_property, and accepted native
transform/dtype/namespace primitives supplement controlled callback boundaries.
No oracle is mounted in the application or invokes a current owned helper.

Layer/model construction, MultiScaleData construction/assert_protocol/backing
list ownership, units setters, non-scalar extent overrides and provider state
remain declared dependencies. Genuine generator/category, function/data/cache
cells and dictionary aliases are covered; private code/frame identity, platform,
plugin, startup/shutdown and complete workflow ownership remain outside scope.
"""

from __future__ import annotations

import ast
import collections
import contextlib
import copy
import gc
import hashlib
import inspect
import re
import weakref
from collections.abc import Callable, Iterator, Sequence
from functools import cached_property, lru_cache
from pathlib import Path
from types import SimpleNamespace
from typing import Any, NamedTuple

import dask
import dask.array as da
import numpy as np
import pint
import psutil
import pytest
from dask.cache import Cache

ROOT = Path(__file__).resolve().parents[1]
PINNED = ROOT / ".upstream/napari/src/napari"
CURRENT = ROOT / "src/napari"
BEFORE = ROOT / ".artifacts/slicing-input-helpers-before/files/src/napari"
HASHES = {
    "utils/_dask_utils.py": (
        "59fc0cdef8fed668e11ae16263847d265fcf17b898afa59fa63792a8c345771d"
    ),
    "layers/base/base.py": (
        "2c9e08f2cf0508fdf2399abf49eea42ecb545adb81ac4c8422b57103b57eefa4"
    ),
    "layers/utils/layer_utils.py": (
        "6d3ed1151813da2cc39a1d476fb0b13cd712e1b741c0a42fb02d7b7f6a244d6d"
    ),
    "layers/_scalar_field/scalar_field.py": (
        "50d0f2c6adb4412589394dde4a55a8b156799b89ed55610a57496ef6d0d2a0b4"
    ),
    "layers/image/image.py": (
        "73027b2f096445c6aea88689bbf29f8707a75847d1348fa3b80eaf867c728c56"
    ),
    "layers/_multiscale_data.py": (
        "6cca21966a2e2734e8e7e98e2a8bb4a9ad80d2787a8a8c400f2e44d9b594f7c4"
    ),
}
BEFORE_HASHES = {
    "utils/_dask_utils.py": HASHES["utils/_dask_utils.py"],
    "layers/base/base.py": (
        "324b01830b1607d4fbf25ffe28a3b2483a8c338b6135166f5ee5c87afd1dce89"
    ),
    "layers/utils/layer_utils.py": (
        "f3009e0847b14dd191f5df520aa41c6a5d5b84bcde0f0401d246a615bf5c1d6e"
    ),
    "layers/_scalar_field/scalar_field.py": (
        "b58d24a4f778226d00095b88e5ac54ca24010f128afd2251ef3749187b304cda"
    ),
    "layers/image/image.py": (
        "ba597084f1cdb5f61343b6a2d8a9d25a661f9e9641e623ce04e50fdc8d53eaf0"
    ),
    "layers/_multiscale_data.py": HASHES["layers/_multiscale_data.py"],
}
DASK = "utils/_dask_utils.py"
BASE = "layers/base/base.py"
UTILS = "layers/utils/layer_utils.py"
SCALAR = "layers/_scalar_field/scalar_field.py"
IMAGE = "layers/image/image.py"
MULTI = "layers/_multiscale_data.py"
OWNED = (
    (DASK, None, "resize_dask_cache", 0),
    (DASK, None, "_is_dask_data", 0),
    (DASK, None, "configure_dask", 0),
    (DASK, "configure_dask", "dask_optimized_slicing", 0),
    (BASE, "Layer", "projection_mode", 0),
    (BASE, "Layer", "projection_mode", 1),
    (BASE, "Layer", "units", 0),
    (BASE, "Layer", "_extent_world", 0),
    (BASE, "Layer", "_extent_world_augmented", 0),
    (BASE, "Layer", "extent", 0),
    (BASE, "Layer", "_extent_augmented", 0),
    (BASE, "Layer", "_data_to_world", 0),
    (UTILS, None, "get_extent_world", 0),
    (SCALAR, "ScalarFieldBase", "dtype", 0),
    (SCALAR, "ScalarFieldBase", "_get_ndim", 0),
    (SCALAR, "ScalarFieldBase", "_get_level_shapes", 0),
    (SCALAR, "ScalarFieldBase", "level_shapes", 0),
    (SCALAR, "ScalarFieldBase", "downsample_factors", 0),
    (SCALAR, "ScalarFieldBase", "_extent_data", 0),
    (SCALAR, "ScalarFieldBase", "_extent_data_augmented", 0),
    (SCALAR, "ScalarFieldBase", "_extent_level_data", 0),
    (SCALAR, "ScalarFieldBase", "_extent_level_data_augmented", 0),
    (SCALAR, None, "_make_level_materializer", 0),
    (SCALAR, "_make_level_materializer", "_materializer", 0),
    (SCALAR, "ScalarFieldBase", "_reset_thumbnail_level_data", 0),
    (IMAGE, "Image", "_get_level_shapes", 0),
    (MULTI, "MultiScaleData", "dtype", 0),
    (MULTI, "MultiScaleData", "shape", 0),
    (MULTI, "MultiScaleData", "shapes", 0),
    (MULTI, "MultiScaleData", "__getitem__", 0),
    (MULTI, "MultiScaleData", "__len__", 0),
)


def _source(root, relative):
    raw = (root / relative).read_bytes()
    expected = HASHES if root == PINNED else BEFORE_HASHES if root == BEFORE else None
    if expected is not None:
        assert hashlib.sha256(raw).hexdigest() == expected[relative]
    return raw.decode("utf-8")


def _definition(source, owner, name, occurrence=0):
    tree = ast.parse(source)
    nodes = tree.body
    if owner is not None:
        parent = next(
            node
            for node in nodes
            if isinstance(node, (ast.ClassDef, ast.FunctionDef)) and node.name == owner
        )
        # The native binding declaration may put a nested function in its pure
        # declaration provider. This does not assert identical private scopes.
        nodes = (
            parent.body if isinstance(parent, ast.ClassDef) else list(ast.walk(parent))
        )
    return [
        node
        for node in nodes
        if isinstance(node, ast.FunctionDef) and node.name == name
    ][occurrence]


def _class(source, name):
    return next(
        node
        for node in ast.parse(source).body
        if isinstance(node, ast.ClassDef) and node.name == name
    )


def _execute(source, nodes, namespace, path):
    futures = [
        copy.deepcopy(node)
        for node in ast.parse(source).body
        if isinstance(node, ast.ImportFrom) and node.module == "__future__"
    ]
    tree = ast.fix_missing_locations(
        ast.Module(body=futures + copy.deepcopy(nodes), type_ignores=[])
    )
    exec(compile(tree, str(path), "exec", dont_inherit=True), namespace)


def _method_class(source, name, namespace, path, bases):
    selected = {
        (method, occurrence) for _, owner, method, occurrence in OWNED if owner == name
    }
    counts = collections.defaultdict(int)
    body = []
    for node in _class(source, name).body:
        if isinstance(node, ast.FunctionDef):
            occurrence = counts[node.name]
            counts[node.name] += 1
            if (node.name, occurrence) in selected:
                body.append(copy.deepcopy(node))
    node = ast.ClassDef(
        name=name, bases=bases, keywords=[], body=body, decorator_list=[]
    )
    _execute(source, [node], namespace, path)
    return namespace[name]


@pytest.fixture(scope="module")
def implementations():
    from napari.layers._data_protocols import LayerDataProtocol, assert_protocol
    from napari.utils._dtype import normalize_dtype
    from napari.utils.transforms import Affine, TransformChain
    from naparixx import _native

    changes = 0
    for relative, owner, name, occurrence in OWNED:
        old = _definition(_source(BEFORE, relative), owner, name, occurrence)
        current = _definition(_source(CURRENT, relative), owner, name, occurrence)
        changes += ast.dump(old) != ast.dump(current)
        pinned = _definition(_source(PINNED, relative), owner, name, occurrence)
        assert ast.dump(pinned.args) == ast.dump(current.args)
        assert (ast.dump(pinned.returns) if pinned.returns is not None else None) == (
            ast.dump(current.returns) if current.returns is not None else None
        )
        assert ast.get_docstring(pinned) == ast.get_docstring(current)
    assert len(OWNED) == 31 and changes in (0, 31)

    def build(root):
        sources = {relative: _source(root, relative) for relative in HASHES}
        common = dict(
            np=np,
            pint=pint,
            Any=Any,
            Sequence=Sequence,
            Callable=Callable,
            Iterator=Iterator,
            NamedTuple=NamedTuple,
            Affine=Affine,
            TransformChain=TransformChain,
            _native=_native,
        )
        dask_ns = dict(
            common,
            __name__="napari.utils._dask_utils",
            dask=dask,
            collections=collections,
            contextlib=contextlib,
            Cache=Cache,
            _DASK_CACHE=Cache(1),
            _DEFAULT_MEM_FRACTION=0.25,
            DaskIndexer=Callable[
                [], contextlib.AbstractContextManager[tuple[dict, Cache] | None]
            ],
        )
        _execute(
            sources[DASK],
            [
                _definition(sources[DASK], None, name)
                for name in ("resize_dask_cache", "_is_dask_data", "configure_dask")
            ],
            dask_ns,
            root / DASK,
        )
        import warnings

        utils_ns = dict(
            common, __name__="napari.layers.utils.layer_utils", warnings=warnings
        )
        _execute(
            sources[UTILS],
            [
                _class(sources[UTILS], "Extent"),
                _definition(sources[UTILS], None, "get_extent_world"),
            ],
            utils_ns,
            root / UTILS,
        )
        base_ns = dict(
            common,
            __name__="napari.layers.base.base",
            cached_property=cached_property,
            Extent=utils_ns["Extent"],
            get_extent_world=utils_ns["get_extent_world"],
        )
        bases = (
            [ast.parse("_native.LayerNamespaceBase", mode="eval").body]
            if root == CURRENT
            else []
        )
        layer = _method_class(sources[BASE], "Layer", base_ns, root / BASE, bases)
        multi_ns = dict(
            common,
            __name__="napari.layers._multiscale_data",
            LayerDataProtocol=LayerDataProtocol,
            assert_protocol=assert_protocol,
        )
        _execute(
            sources[MULTI],
            [_class(sources[MULTI], "MultiScaleData")],
            multi_ns,
            root / MULTI,
        )
        multi = multi_ns["MultiScaleData"]

        class DataBoundary(layer):
            @property
            def data(self):
                return self._data

            @property
            def data_level(self):
                return self._data_level

            @property
            def multiscale(self):
                return isinstance(self._data, multi)

        scalar_ns = dict(
            common,
            __name__="napari.layers._scalar_field.scalar_field",
            Layer=DataBoundary,
            MultiScaleData=multi,
            normalize_dtype=normalize_dtype,
            lru_cache=lru_cache,
        )
        scalar = _method_class(
            sources[SCALAR],
            "ScalarFieldBase",
            scalar_ns,
            root / SCALAR,
            [ast.Name(id="Layer", ctx=ast.Load())],
        )
        _execute(
            sources[SCALAR],
            [_definition(sources[SCALAR], None, "_make_level_materializer")],
            scalar_ns,
            root / SCALAR,
        )
        image_ns = dict(
            common,
            __name__="napari.layers.image.image",
            ScalarFieldBase=scalar,
        )
        image = _method_class(
            sources[IMAGE],
            "Image",
            image_ns,
            root / IMAGE,
            [ast.Name(id="ScalarFieldBase", ctx=ast.Load())],
        )
        return SimpleNamespace(
            dask=dask_ns,
            utils=utils_ns,
            base=base_ns,
            scalar=scalar_ns,
            image=image_ns,
            multi_ns=multi_ns,
            Layer=layer,
            Scalar=scalar,
            Image=image,
            MultiScale=multi,
            Affine=Affine,
            TransformChain=TransformChain,
        )

    return build


def _value(value):
    if isinstance(value, np.ndarray):
        return value.shape, str(value.dtype), value.tolist()
    if isinstance(value, np.dtype):
        return str(value)
    if isinstance(value, (tuple, list)):
        return tuple(_value(item) for item in value)
    return value


def _error(callback):
    try:
        callback()
    except Exception as error:
        return (
            type(error).__name__,
            re.sub(r"0x[0-9a-fA-F]+", "0xADDRESS", str(error)),
            type(error.__cause__).__name__ if error.__cause__ else None,
            type(error.__context__).__name__ if error.__context__ else None,
        )
    return None


def _compare(build, scenario):
    original = scenario(build(PINNED))
    observed = scenario(build(CURRENT))
    assert observed == original
    return observed


class _Manager:
    def __init__(
        self, name, value, trace, *, enter=None, exit_error=None, suppress=False
    ):
        self.name, self.value, self.trace = name, value, trace
        self.enter, self.exit_error, self.suppress = enter, exit_error, suppress

    def __enter__(self):
        self.trace.append(("enter", self.name))
        if self.enter:
            self.enter()
        return self.value

    def __exit__(self, kind, value, traceback):
        self.trace.append(("exit", self.name, kind.__name__ if kind else None))
        if self.exit_error:
            raise self.exit_error
        return self.suppress


@pytest.mark.parametrize(
    "case", ("default", "fraction", "zero", "noop", "different", "equal")
)
def test_resize_policy_uses_real_cache_and_original_memory_branch(
    implementations, monkeypatch, case
):
    def scenario(impl):
        trace = []
        initial = 1 if case in ("default", "fraction", "zero") else 64
        cache = Cache(initial)
        impl.dask["_DASK_CACHE"] = cache
        args = {
            "default": (None, None),
            "fraction": (None, 0.4),
            "zero": (0, 0.9),
            "noop": (None, None),
            "different": (32, None),
            "equal": (64, None),
        }[case]

        def memory():
            trace.append("memory")
            return SimpleNamespace(total=100)

        with monkeypatch.context() as context:
            context.setattr(psutil, "virtual_memory", memory)
            result = impl.dask["resize_dask_cache"](*args)
        return tuple(trace), result is cache, cache.cache.available_bytes

    trace, same, available = _compare(implementations, scenario)
    assert same
    assert (
        available
        == {
            "default": 25,
            "fraction": 40,
            "zero": 0,
            "noop": 64,
            "different": 32,
            "equal": 64,
        }[case]
    )
    assert trace == (("memory",) if case in ("default", "fraction") else ())


@pytest.mark.parametrize(
    "case", ("direct", "numpy", "early_true", "empty", "lazy_failure")
)
def test_dask_detection_preserves_sequence_short_circuit_and_generator_category(
    implementations, case
):
    def scenario(impl):
        if case in ("direct", "numpy", "empty"):
            data = (
                da.ones((2, 3), chunks=(1, 3))
                if case == "direct"
                else np.ones((2, 3))
                if case == "numpy"
                else []
            )
            return impl.dask["_is_dask_data"](data), (), None
        trace = []
        data = ["first", "second", "unused"]

        def probe(item):
            trace.append("data" if item is data else item)
            if case == "lazy_failure" and item == "second":
                raise RuntimeError("graph second")
            return item == "second"

        def consume(iterator):
            trace.append(
                (
                    "generator",
                    inspect.isgenerator(iterator),
                    iterator.__name__,
                    iterator.__qualname__,
                )
            )
            return any(iterator)

        impl.dask["dask"] = SimpleNamespace(is_dask_collection=probe)
        impl.dask["any"] = consume
        result = []
        error = _error(lambda: result.append(impl.dask["_is_dask_data"](data)))
        return result[0] if result else None, tuple(trace), error

    result, trace, error = _compare(implementations, scenario)
    assert result == (case in ("direct", "early_true") if not error else None)
    if trace:
        assert trace[:2] == (
            "data",
            (
                "generator",
                True,
                "<genexpr>",
                "_is_dask_data.<locals>.<genexpr>",
            ),
        )
        assert trace[2:] == ("first", "second")
    assert (error is not None) == (case == "lazy_failure")


@pytest.mark.parametrize("case", ("numpy", "cache", "no_cache", "cell_replaced"))
def test_configure_real_dask_cache_and_live_generator_cell(implementations, case):
    def scenario(impl):
        trace = []
        cache = Cache(64)
        impl.dask["_DASK_CACHE"] = cache
        if case == "numpy":

            class UnusedFlag:
                def __bool__(self):
                    raise AssertionError("non-Dask must not inspect cache")

            result = impl.dask["configure_dask"](np.zeros((2, 3)), UnusedFlag())
            return result is contextlib.nullcontext, (), None, ()
        data = da.ones((2, 3), chunks=(1, 3))
        if case != "cell_replaced":
            factory = impl.dask["configure_dask"](data, cache=case == "cache")
            with factory() as (cfg, selected):
                state = (
                    dask.config.get("optimization.fuse.active"),
                    selected is cache,
                    selected is None,
                    isinstance(cfg, dict),
                )
        else:
            holder = {}
            replacement = _Manager("replacement", "replacement-value", trace)

            def enter():
                function = holder["factory"].__wrapped__
                assert function.__code__.co_freevars == ("_cache",)
                function.__closure__[0].cell_contents = replacement

            def config(options):
                trace.append(("options", tuple(options.items())))
                return _Manager("cfg", "cfg-value", trace, enter=enter)

            impl.dask["dask"] = SimpleNamespace(
                is_dask_collection=dask.is_dask_collection,
                config=SimpleNamespace(set=config),
            )
            factory = impl.dask["configure_dask"](data)
            holder["factory"] = factory
            with factory() as value:
                state = value
        metadata = (
            inspect.isfunction(factory),
            inspect.isgeneratorfunction(factory.__wrapped__),
            factory.__qualname__,
            factory.__wrapped__.__qualname__,
            str(inspect.signature(factory)),
            factory.__wrapped__.__code__.co_freevars,
        )
        return False, tuple(trace), state, metadata

    null, trace, state, metadata = _compare(implementations, scenario)
    assert null == (case == "numpy")
    if case == "cache":
        assert state == (False, True, False, True)
    elif case == "no_cache":
        assert state == (False, False, True, True)
    elif case == "cell_replaced":
        assert state == ("cfg-value", "replacement-value")
        assert trace == (
            ("options", (("optimization.fuse.active", False),)),
            ("enter", "cfg"),
            ("enter", "replacement"),
            ("exit", "replacement", None),
            ("exit", "cfg", None),
        )
    if metadata:
        assert metadata[:4] == (
            True,
            True,
            "configure_dask.<locals>.dask_optimized_slicing",
            "configure_dask.<locals>.dask_optimized_slicing",
        )
        assert metadata[-1] == ("_cache",)


@pytest.mark.parametrize(
    "case",
    (
        "lazy_close",
        "send_close",
        "throw_suppress",
        "second_enter_failure",
        "exit_stop_iteration",
        "cycle_gc",
    ),
)
def test_dask_generator_two_context_lifecycle_and_gc(implementations, case):
    def scenario(impl):
        trace = []

        def fail_enter():
            raise RuntimeError("cache enter")

        cache_manager = _Manager(
            "cache",
            "cache-value",
            trace,
            enter=fail_enter if case == "second_enter_failure" else None,
            exit_error=StopIteration("cache exit")
            if case == "exit_stop_iteration"
            else None,
            suppress=case == "throw_suppress",
        )
        config_manager = _Manager("cfg", "cfg-value", trace)

        def config(options):
            trace.append(("options", tuple(options.items())))
            return config_manager

        impl.dask.update(
            _DASK_CACHE=Cache(64),
            dask=SimpleNamespace(
                is_dask_collection=dask.is_dask_collection,
                config=SimpleNamespace(set=config),
            ),
        )
        factory = impl.dask["configure_dask"](da.ones((2, 2), chunks=(1, 2)))
        factory.__wrapped__.__closure__[0].cell_contents = cache_manager
        generator = factory.__wrapped__()
        assert inspect.isgenerator(generator)
        if case == "lazy_close":
            generator.close()
            return tuple(trace), None, None, True
        yielded = []
        error = _error(lambda: yielded.append(next(generator)))
        if case == "cycle_gc":
            cache_manager.cycle = generator
            config_manager.cycle = generator
            refs = (
                weakref.ref(generator),
                weakref.ref(cache_manager),
                weakref.ref(config_manager),
            )
            # Remove fixture namespace/closure roots; only the deliberate cycle
            # remains. Neither yielded value refers to either manager.
            impl.dask["dask"] = dask
            del generator, factory, cache_manager, config_manager
            gc.collect()
            collected = all(reference() is None for reference in refs)
        else:
            if error is None:
                if case == "throw_suppress":
                    error = _error(lambda: generator.throw(ValueError("slice body")))
                else:
                    error = _error(lambda: generator.send("resume"))
            generator.close()
            collected = True
        return tuple(trace), yielded[0] if yielded else None, error, collected

    trace, yielded, error, collected = _compare(implementations, scenario)
    assert collected
    if case == "lazy_close":
        assert trace == () and yielded is None and error is None
    elif case == "second_enter_failure":
        assert trace[-1] == ("exit", "cfg", "RuntimeError")
        assert error[0] == "RuntimeError" and yielded is None
    else:
        assert yielded == ("cfg-value", "cache-value")
        if case == "exit_stop_iteration":
            assert error[:3] == (
                "RuntimeError",
                "generator raised StopIteration",
                "StopIteration",
            )
        elif case != "cycle_gc":
            assert error[0] == "StopIteration"
        expected = (
            "ValueError"
            if case == "throw_suppress"
            else "StopIteration"
            if case == "exit_stop_iteration"
            else "GeneratorExit"
            if case == "cycle_gc"
            else None
        )
        assert trace[-2] == (
            "exit",
            "cache",
            expected if expected != "StopIteration" else None,
        )
        assert trace[-1] == (
            "exit",
            "cfg",
            None if case == "throw_suppress" else expected,
        )


@pytest.mark.parametrize(
    "case",
    (
        "cache_hit_eviction",
        "replacement",
        "cell_order",
        "getitem_failure",
        "cache_clear_lifetime",
    ),
)
def test_level_materializer_real_lru_cell_lookup_and_lifetime(implementations, case):
    def scenario(impl):
        trace = []
        first = np.arange(6, dtype=np.uint16).reshape(2, 3)
        second = np.arange(2, dtype=np.uint16).reshape(1, 2)

        class Store:
            def __init__(self):
                self.levels = [first, second]
                self.fail = case == "getitem_failure"

            def __getitem__(self, level):
                trace.append(("getitem", level))
                if self.fail:
                    raise ValueError("materializer getitem")
                return self.levels[level]

        data = Store()
        factory = impl.scalar["_make_level_materializer"]
        materializer = factory(data)
        wrapped = materializer.__wrapped__
        metadata = (
            inspect.isfunction(wrapped),
            wrapped.__code__.co_freevars,
            wrapped.__qualname__,
            str(inspect.signature(wrapped)),
            materializer.cache_parameters(),
        )
        assert wrapped.__closure__[0].cell_contents is data
        if case == "cache_hit_eviction":
            a = materializer(0)
            hit = materializer(0)
            b = materializer(1)
            evicted = materializer(0)
            result = (a is first, hit is a, b is second, evicted is first)
        elif case == "replacement":
            a = materializer(0)
            replacement = first + 20
            data.levels[0] = replacement
            hit = materializer(0)
            materializer.cache_clear()
            b = materializer(0)
            result = (a is first, hit is a, b is replacement, _value(b))
        elif case == "cell_order":

            class ArrayProvider:
                @property
                def asarray(self):
                    trace.append("capture asarray")

                    def convert(value):
                        trace.append("captured asarray")
                        return np.asarray(value)

                    return convert

            class Replacement(Store):
                def __getitem__(self, level):
                    trace.append(("replacement getitem", level))
                    impl.scalar["np"] = SimpleNamespace(
                        asarray=lambda value: trace.append("wrong asarray")
                    )
                    return second

            replacement = Replacement()
            wrapped.__closure__[0].cell_contents = replacement
            impl.scalar["np"] = ArrayProvider()
            result = materializer(0) is second
        elif case == "getitem_failure":
            failure = _error(lambda: materializer(0))
            failed_info = tuple(materializer.cache_info())
            data.fail = False
            result = (failure, failed_info, materializer(0) is first)
        else:

            class ProducingStore:
                def __getitem__(self, level):
                    trace.append(("produce", level))
                    return np.arange(4, dtype=np.float32)

            data = ProducingStore()
            materializer = factory(data)
            wrapped = materializer.__wrapped__
            data_ref, wrapped_ref = weakref.ref(data), weakref.ref(wrapped)
            value = materializer(0)
            value_ref = weakref.ref(value)
            del value, data
            gc.collect()
            held = (data_ref() is not None, value_ref() is not None)
            materializer.cache_clear()
            gc.collect()
            cleared = (data_ref() is not None, value_ref() is None)
            del materializer, wrapped
            gc.collect()
            result = (held, cleared, data_ref() is None, wrapped_ref() is None)
            return metadata, tuple(trace), result, None
        return metadata, tuple(trace), result, tuple(materializer.cache_info())

    metadata, trace, result, info = _compare(implementations, scenario)
    assert metadata[:3] == (
        True,
        ("data",),
        "_make_level_materializer.<locals>._materializer",
    )
    assert metadata[-1] == {"maxsize": 1, "typed": False}
    if case == "cache_hit_eviction":
        assert result == (True, True, True, True)
        assert trace == (("getitem", 0), ("getitem", 1), ("getitem", 0))
        assert info == (1, 3, 1, 1)
    elif case == "replacement":
        assert result[:3] == (True, True, True)
        assert trace == (("getitem", 0), ("getitem", 0))
    elif case == "cell_order":
        assert result is True
        assert trace == (
            "capture asarray",
            ("replacement getitem", 0),
            "captured asarray",
        )
    elif case == "getitem_failure":
        assert result[0][:2] == ("ValueError", "materializer getitem")
        assert result[1] == (0, 1, 1, 0) and result[2] is True
        assert info == (0, 2, 1, 1)
    else:
        assert result == ((True, True), (True, True), True, True)
        assert trace == (("produce", 0),)


@pytest.mark.parametrize(
    "case",
    ("single", "multiscale_2d", "multiscale_3d", "ndim_failure", "factory_failure"),
)
def test_thumbnail_reset_publication_and_partial_failures(implementations, case):
    def scenario(impl):
        trace = []

        class Model(impl.Scalar):
            def _get_ndim(self):
                trace.append(("ndim", self._thumbnail_level))
                if case == "ndim_failure":
                    raise RuntimeError("reset ndim")
                return super()._get_ndim()

        model = Model()
        old_materializer = object()
        model._thumbnail_level = 99
        model._level_materializer = old_materializer
        shape = (4, 6, 8) if case == "multiscale_3d" else (4, 6)
        model._data = (
            np.zeros(shape, dtype=np.uint16)
            if case == "single"
            else impl.MultiScale(
                [
                    np.zeros(shape, dtype=np.uint16),
                    np.ones(tuple(d // 2 for d in shape), dtype=np.uint16),
                ]
            )
        )
        real_factory = impl.scalar["_make_level_materializer"]

        def factory(data):
            trace.append(("factory", data is model._data, model._thumbnail_level))
            if case == "factory_failure":
                raise ValueError("reset factory")
            return real_factory(data)

        impl.scalar["_make_level_materializer"] = factory
        error = _error(model._reset_thumbnail_level_data)
        materializer = model._level_materializer
        state = (
            model._thumbnail_level,
            materializer is old_materializer,
            materializer is None,
            bool(materializer is not None and materializer is not old_materializer),
        )
        alias = materializer(1) is model._data[1] if case == "multiscale_2d" else None
        return tuple(trace), state, alias, error

    trace, state, alias, error = _compare(implementations, scenario)
    if case == "single":
        assert trace == () and state == (0, False, True, False)
    elif case == "multiscale_3d":
        assert trace == (("ndim", 1),) and state == (1, False, True, False)
    elif case == "multiscale_2d":
        assert trace == (("ndim", 1), ("factory", True, 1))
        assert state == (1, False, False, True) and alias is True
    else:
        assert state == (1, True, False, False)
        assert error[:2] == (
            ("RuntimeError", "reset ndim")
            if case == "ndim_failure"
            else ("ValueError", "reset factory")
        )


@pytest.mark.parametrize(
    "case",
    (
        "single",
        "multiscale",
        "fresh_single_data",
        "fresh_level_shapes",
        "level_shape_failure",
    ),
)
def test_scalar_dtype_shape_downsample_and_extent_control(implementations, case):
    def scenario(impl):
        trace = []
        model = impl.Scalar()
        model._data_level = 1 if case == "multiscale" else 0
        first = np.zeros((8, 12), dtype=np.int16)
        second = np.zeros((4, 3), dtype=np.int16)
        model._data = (
            impl.MultiScale([first, second]) if case == "multiscale" else first
        )
        if case == "fresh_single_data":

            class FreshData(impl.Scalar):
                @property
                def data(self):
                    trace.append("data")
                    return first if len(trace) == 1 else second

            model = FreshData()
            model._data = first
            return tuple(model._get_level_shapes()), tuple(trace), None
        if case == "fresh_level_shapes":

            class FreshShapes(impl.Scalar):
                @property
                def level_shapes(self):
                    trace.append("level_shapes")
                    return (
                        np.array([[8, 12], [4, 6]])
                        if len(trace) == 1
                        else np.array([[4, 3], [2, 3]])
                    )

            model = FreshShapes()
            return _value(model.downsample_factors), tuple(trace), None
        if case == "level_shape_failure":

            class FailingShapes(impl.Scalar):
                @property
                def level_shapes(self):
                    trace.append("level_shapes")
                    return np.array([[8, 12]])

                @property
                def data_level(self):
                    trace.append("data_level")
                    return 2

            model = FailingShapes()
            error = _error(lambda: model._extent_level_data_augmented)
            return None, tuple(trace), error
        values = (
            str(model.dtype),
            model._get_ndim(),
            _value(model.level_shapes),
            _value(model.downsample_factors),
            _value(model._extent_data),
            _value(model._extent_data_augmented),
            _value(model._extent_level_data),
            _value(model._extent_level_data_augmented),
        )
        return values, tuple(trace), None

    values, trace, error = _compare(implementations, scenario)
    if case == "fresh_single_data":
        assert values == ((4, 3),) and trace == ("data", "data")
    elif case == "fresh_level_shapes":
        assert values[-1] == [[2.0, 4.0], [4.0, 4.0]]
        assert trace == ("level_shapes", "level_shapes")
    elif case == "level_shape_failure":
        assert error[0] == "IndexError" and trace == ("level_shapes", "data_level")
    else:
        assert error is None and values[:2] == ("int16", 2)
        assert values[4][-1] == [[0.0, 0.0], [7.0, 11.0]]
        assert values[5][-1] == [[-0.5, -0.5], [7.5, 11.5]]
        upper = [3.0, 2.0] if case == "multiscale" else [7.0, 11.0]
        assert values[6][-1] == [[0.0, 0.0], upper]
        assert values[7][-1][0] == [-0.5, -0.5]


@pytest.mark.parametrize("case", ("plain", "rgb", "partial_failure"))
def test_image_shape_real_lexical_super_and_comprehension_failure(
    implementations, case
):
    def scenario(impl):
        trace = []
        source = [(8, 12, 3), (4, 6, 3)]

        class BadShape:
            def __getitem__(self, key):
                trace.append(("bad shape", key.start, key.stop, key.step))
                raise ValueError("image shape slice")

        if case == "partial_failure":
            source[1] = BadShape()

        class ShapeMixin(impl.Scalar):
            def _get_level_shapes(self):
                trace.append("dynamic super")
                return source

        class Model(impl.Image, ShapeMixin):
            @property
            def rgb(self):
                trace.append("rgb")
                return case != "plain"

        model = Model()
        result = []
        error = _error(lambda: result.append(model._get_level_shapes()))
        return (
            _value(result[0]) if result else None,
            tuple(trace),
            error,
            bool(result and result[0] is source),
            source[0] == (8, 12, 3) and len(source) == 2,
        )

    values, trace, error, alias, unchanged = _compare(implementations, scenario)
    assert trace[:2] == ("dynamic super", "rgb") and unchanged
    if case == "partial_failure":
        assert error[:2] == ("ValueError", "image shape slice") and values is None
        assert trace[-1] == ("bad shape", None, -1, None)
    else:
        assert error is None
        assert values == (
            ((8, 12, 3), (4, 6, 3)) if case == "plain" else ((8, 12), (4, 6))
        )
        assert alias is (case == "plain")


@pytest.mark.parametrize(
    "case",
    ("unchanged", "changed", "event_failure", "normalization_failure"),
)
def test_projection_class_capture_store_event_and_fresh_refresh(implementations, case):
    def scenario(impl):
        trace = []

        class Model(impl.Layer):
            @property
            def _projectionclass(self):
                trace.append("class")
                return self.normalizer

        model = Model()
        model._projection_mode = "max" if case == "unchanged" else "min"

        def normalize(value):
            trace.append(("normalize", value))
            if case == "normalization_failure":
                raise ValueError("normalize projection")
            return value.lower()

        model.normalizer = normalize

        class Mode:
            def __str__(self):
                trace.append("str")
                # The source already captured the class before evaluating str.
                model.normalizer = lambda value: trace.append("wrong class")
                return "MAX"

        def refresh(**kwargs):
            trace.append(
                ("fresh refresh", tuple(kwargs.items()), model.projection_mode)
            )

        def event():
            trace.append(("event", model.projection_mode))
            if case == "event_failure":
                raise RuntimeError("projection event")
            model.refresh = refresh

        model.events = SimpleNamespace(projection_mode=event)
        model.refresh = lambda **kwargs: trace.append("stale refresh")
        error = _error(lambda: setattr(model, "projection_mode", Mode()))
        return tuple(trace), model.projection_mode, error

    trace, mode, error = _compare(implementations, scenario)
    assert trace[:3] == ("class", "str", ("normalize", "MAX"))
    if case == "normalization_failure":
        assert mode == "min" and len(trace) == 3
        assert error[:2] == ("ValueError", "normalize projection")
    elif case == "unchanged":
        assert mode == "max" and len(trace) == 3 and error is None
    elif case == "event_failure":
        assert mode == "max" and trace[-1] == ("event", "max")
        assert error[:2] == ("RuntimeError", "projection event")
    else:
        assert error is None
        assert trace[-2:] == (
            ("event", "max"),
            ("fresh refresh", (("extent", False),), "max"),
        )


@pytest.mark.parametrize("case", ("real_chain", "units_failure"))
def test_data_to_world_actual_simplified_transform_and_units_order(
    implementations, case
):
    def scenario(impl):
        trace = []
        data_to_physical = impl.Affine(
            scale=(2, 3),
            translate=(1, 2),
            name="data2physical",
            units=("meter", "second"),
        )
        chain = impl.TransformChain(
            [
                impl.Affine(ndim=2, name="tile2data"),
                data_to_physical,
                impl.Affine(scale=(4, 5), translate=(6, 7), name="physical2world"),
            ]
        )
        sliced = chain[1:3]
        result_transform = sliced.simplified
        original_units = result_transform.units

        class Transforms:
            def __getitem__(self, key):
                if isinstance(key, slice):
                    trace.append(("slice", key.start, key.stop, key.step))
                    return sliced
                trace.append(("units transform", key))
                return chain[key]

        class Model(impl.Layer):
            @property
            def units(self):
                trace.append("units")
                if case == "units_failure":
                    raise ValueError("read transform units")
                return super().units

        model = Model()
        model._transforms = Transforms()
        returned = []
        error = _error(lambda: returned.append(model._data_to_world))
        return (
            tuple(trace),
            error,
            bool(returned and returned[0] is result_transform),
            tuple(str(unit) for unit in result_transform.units),
            result_transform.units is original_units,
            _value(result_transform(np.array([[1.0, 2.0]]))),
            tuple(str(unit) for unit in data_to_physical.units),
        )

    trace, error, alias, units, retained, coordinates, source_units = _compare(
        implementations, scenario
    )
    assert trace[:2] == (("slice", 1, 3, None), "units")
    assert coordinates[-1] == [[18.0, 47.0]]
    if case == "units_failure":
        assert error[:2] == ("ValueError", "read transform units")
        assert not alias and retained and len(trace) == 2
    else:
        assert error is None and alias
        assert units == source_units == ("meter", "second")
        assert trace[-1] == ("units transform", "data2physical")


@pytest.mark.parametrize("case", ("cache_alias", "failure", "replace_dictionary"))
def test_extent_cached_property_canonical_dictionary_and_publication(
    implementations, case
):
    def scenario(impl):
        trace = []
        data = np.array([[0.0, 1.0], [3.0, 5.0]])
        augmented = data + [[-0.5], [0.5]]
        transform = impl.Affine(
            scale=(-2, 3), translate=(4, 5), units=("meter", "meter")
        )

        class Model(impl.Layer):
            @property
            def _extent_data(self):
                trace.append("data")
                return data

            @property
            def _extent_data_augmented(self):
                trace.append("augmented data")
                return augmented

            @property
            def _data_to_world(self):
                trace.append("transform")
                return transform

            @property
            def units(self):
                trace.append("units")
                return transform.units

        model = Model()
        saved = model.__dict__
        assert saved is vars(model)
        real_world = impl.utils["get_extent_world"]

        def world(extent, affine):
            trace.append(("world", extent is data, affine is transform))
            if case == "failure":
                raise RuntimeError("extent world")
            if case == "replace_dictionary":
                model.__dict__ = {"replacement": True}
            return real_world(extent, affine)

        impl.base["get_extent_world"] = world
        returned = []
        error = _error(lambda: returned.append(model.extent))
        if case == "failure":
            return tuple(trace), error, "extent" in saved, "extent" in vars(model), None
        value = returned[0]
        values = (
            _value(value.data),
            _value(value.world),
            _value(value.step),
            tuple(str(unit) for unit in value.units),
            value.data is data,
        )
        if case == "replace_dictionary":
            return (
                tuple(trace),
                error,
                saved.get("extent") is value,
                "extent" in vars(model),
                values,
            )
        hit = model.extent
        saved["marker"] = "raw alias"
        augmented_value = model._extent_augmented
        # Both cached descriptors publish into the same exact live dictionary.
        aliases = (
            hit is value,
            saved["extent"] is value,
            saved["_extent_augmented"] is augmented_value,
            vars(model)["marker"] == "raw alias",
            augmented_value.data is augmented,
        )
        return tuple(trace), error, aliases, True, values

    trace, error, old_cache, current_cache, values = _compare(implementations, scenario)
    assert trace[:3] == ("data", "transform", ("world", True, True))
    if case == "failure":
        assert error[:2] == ("RuntimeError", "extent world")
        assert old_cache is False and current_cache is False and values is None
        assert len(trace) == 3
    elif case == "replace_dictionary":
        assert old_cache is True and current_cache is False and error is None
        assert trace[-1] == "units" and values[-1] is True
    else:
        assert error is None and old_cache == (True, True, True, True, True)
        assert trace == (
            "data",
            "transform",
            ("world", True, True),
            "units",
            "augmented data",
            "transform",
            ("world", False, True),
            "units",
        )
        assert values[1][-1] == [[-2.0, 8.0], [4.0, 20.0]]
        assert values[2][-1] == [2.0, 3.0]


@pytest.mark.parametrize(
    "case", ("real_affine", "strided_warning", "transform_failure")
)
def test_world_extent_meshgrid_numpy_transform_and_warning_order(implementations, case):
    def scenario(impl):
        trace = []
        backing = np.array([[0.0, 99.0, 1.0, 99.0], [3.0, 99.0, 5.0, 99.0]])
        extent = backing[:, ::2]
        affine = impl.Affine(scale=(-2, 3), translate=(4, 5))
        warnings_seen = []

        class Transform:
            def __call__(self, grid):
                trace.append(
                    ("transform", _value(grid), np.shares_memory(grid, extent))
                )
                if case == "transform_failure":
                    raise ValueError("transform grid")
                return affine(grid)

        class ExtentRead:
            @property
            def shape(self):
                trace.append("shape")
                return extent.shape

            @property
            def T(self):
                trace.append("transpose")
                return extent.T

        if case == "strided_warning":

            def warn(message, *, stacklevel):
                trace.append("warn")
                warnings_seen.append((message, stacklevel))

            impl.utils["warnings"] = SimpleNamespace(warn=warn)
        values = []
        error = _error(
            lambda: values.append(
                impl.utils["get_extent_world"](
                    ExtentRead(),
                    Transform(),
                    centered=False if case == "strided_warning" else None,
                )
            )
        )
        # The two owned Layer world properties reuse the independently rebuilt
        # helper. Their argument reads remain freshly dynamic and ordered.
        if case == "real_affine":

            class Model(impl.Layer):
                @property
                def _extent_data(self):
                    trace.append("layer data")
                    return extent

                @property
                def _extent_data_augmented(self):
                    trace.append("layer augmented")
                    return extent + [[-0.5], [0.5]]

                @property
                def _data_to_world(self):
                    trace.append("layer transform")
                    return affine

            model = Model()
            extra = (_value(model._extent_world), _value(model._extent_world_augmented))
        else:
            extra = None
        return (
            _value(values[0]) if values else None,
            tuple(trace),
            error,
            tuple(warnings_seen),
            extra,
            extent.strides != backing.strides,
        )

    value, trace, error, warning, extra, strided = _compare(implementations, scenario)
    assert strided
    assert trace[:2] == (
        ("warn", "shape") if case == "strided_warning" else ("shape", "transpose")
    )
    if case == "strided_warning":
        assert trace[2] == "transpose" and warning[0][1] == 2
        assert warning[0][0] == (
            "The `centered` argument is deprecated. "
            "Extents are now always centered on data points."
        )
    if case == "transform_failure":
        assert error[:2] == ("ValueError", "transform grid") and value is None
    else:
        assert error is None and value[-1] == [[-2.0, 8.0], [4.0, 20.0]]
        if case == "real_affine":
            assert trace[-4:] == (
                "layer data",
                "layer transform",
                "layer augmented",
                "layer transform",
            )
            assert extra[0] == value
            assert extra[1][-1] == [[-3.0, 6.5], [5.0, 21.5]]


@pytest.mark.parametrize(
    "case",
    (
        "real_constructor",
        "backing_replacement",
        "lazy_capture",
        "partial_failure",
        "generator_errors_and_gc",
    ),
)
def test_multiscale_live_reads_lazy_shapes_and_generator_lifetime(
    implementations, case
):
    def scenario(impl):
        trace = []
        first = np.zeros((8, 12), dtype=np.uint16)
        second = np.zeros((4, 6), dtype=np.uint16)
        input_levels = [first, second]
        model = impl.MultiScale(input_levels)
        if case == "real_constructor":
            return (
                (str(model.dtype), model.shape, model.shapes, len(model)),
                (
                    model[0] is first,
                    model[-1] is second,
                    model[:1][0] is first,
                    model._data is not input_levels,
                ),
                (),
                None,
            )
        if case == "backing_replacement":
            saved = vars(model)
            replacement = np.ones((2, 3), dtype=np.float32)
            saved["_data"] = [replacement]
            values = (str(model.dtype), model.shape, model.shapes, len(model))
            alias = model[0] is replacement
            model.__dict__ = {"_data": [first, second]}
            after = (str(model.dtype), model.shape, model.shapes, len(model))
            return (
                (values, after),
                (alias, saved is not vars(model), saved["_data"][0] is replacement),
                (),
                None,
            )

        class ShapeValue:
            dtype = np.dtype("uint16")
            size = 96
            ndim = 2

            def __init__(self, name, value, error=None):
                self.name, self.value, self.error = name, value, error

            @property
            def shape(self):
                trace.append(("shape", self.name))
                if self.error:
                    raise self.error
                return self.value

            def __getitem__(self, key):
                return first[key]

        left = ShapeValue("first", (8, 12))
        right = ShapeValue(
            "second",
            (4, 6),
            ValueError("second shape") if case == "partial_failure" else None,
        )
        # Deliberately mutate the same public dictionary used by the class.
        # Construction/protocol policy is a separately named next dependency.
        model.__dict__["_data"] = [left, right]
        if case == "partial_failure":
            error = _error(lambda: model.shapes)
            return None, None, tuple(trace), error

        def capture(generator):
            trace.append("tuple consumer")
            assert inspect.isgenerator(generator)
            assert generator.__name__ == "<genexpr>"
            assert generator.__qualname__ == "MultiScaleData.shapes.<locals>.<genexpr>"
            # The source iterator has already captured the old backing list.
            model.__dict__["_data"] = [first]
            return generator

        impl.multi_ns["tuple"] = capture
        generator = model.shapes
        assert trace == ["tuple consumer"]
        if case == "lazy_capture":
            values = (next(generator), generator.send("unused"))
            exhausted = _error(lambda active=generator: next(active))
            generator.close()
            return values, model._data[0] is first, tuple(trace), exhausted

        # Shape-getter StopIteration is converted by the original generator
        # expression rather than treated as normal iterator exhaustion.
        left.error = StopIteration("shape stop")
        converted = _error(lambda active=generator: next(active))
        generator.close()
        left.error = None
        model.__dict__["_data"] = [left, right]
        generator = model.shapes
        values = (next(generator), generator.send("ignored"))
        generator.close()
        closed = _error(lambda active=generator: next(active))
        model.__dict__["_data"] = [left, right]
        generator = model.shapes
        next(generator)
        left.generator = generator
        refs = weakref.ref(generator), weakref.ref(left), weakref.ref(right)
        impl.multi_ns["tuple"] = tuple
        model.__dict__["_data"] = []
        del generator, left, right
        gc.collect()
        collected = all(reference() is None for reference in refs)
        return values, collected, tuple(trace), (converted, closed)

    values, aliases, trace, error = _compare(implementations, scenario)
    if case == "real_constructor":
        assert values == ("uint16", (8, 12), ((8, 12), (4, 6)), 2)
        assert aliases == (True, True, True, True) and error is None
    elif case == "backing_replacement":
        assert values == (
            ("float32", (2, 3), ((2, 3),), 1),
            ("uint16", (8, 12), ((8, 12), (4, 6)), 2),
        )
        assert aliases == (True, True, True)
    elif case == "partial_failure":
        assert error[:2] == ("ValueError", "second shape")
        assert trace == (("shape", "first"), ("shape", "second"))
    elif case == "lazy_capture":
        assert values == ((8, 12), (4, 6)) and aliases is True
        assert trace == ("tuple consumer", ("shape", "first"), ("shape", "second"))
        assert error[0] == "StopIteration"
    else:
        assert values == ((8, 12), (4, 6)) and aliases is True
        assert error[0][:3] == (
            "RuntimeError",
            "generator raised StopIteration",
            "StopIteration",
        )
        assert error[1][0] == "StopIteration"
        assert trace == (
            "tuple consumer",
            ("shape", "first"),
            "tuple consumer",
            ("shape", "first"),
            ("shape", "second"),
            "tuple consumer",
            ("shape", "first"),
        )
