"""Finite next-owner contracts for Transform authority and retained controls.

Root runs this artifact. NAPARIXX_TRANSFORM_OWNER_BASELINE=1 pairs the actual
pre-port family with independently compiled pinned classes and a TEST-ONLY
allocation candidate using the installed LayerNamespaceBase. Default installed
mode requires the genuine new TransformNamespaceBase beneath all five actual
classes; there is no missing-native fallback or application namespace mount.

The original classes, numerical helpers and unit helpers use guarded pinned
source. EventedList is the real separately owned current dependency; NumPy,
Pint, psygnal, toolz, typing and Python restoration protocols are real services.
The candidate changes only pinned Transform's base. Its other four bases,
Generic metadata, methods, globals and lexical-super cells remain genuine.

These checks cover a finite namespace/cache/restoration/GC and binding-control
boundary. Existing numerical suites below are reused unchanged. Passing a
physical-layout candidate does not establish the new ABI, complete 69-operation
control, numerical/provider domains, private frames or full Viewer closure.
"""

from __future__ import annotations

import ast
import copy
import gc
import hashlib
import inspect
import os
import pickle
import sys
import weakref
from contextlib import contextmanager
from pathlib import Path
from types import FunctionType, ModuleType
from typing import Generic, get_origin

import numpy as np
import pytest

ROOT = Path(__file__).resolve().parents[1]
PINNED = ROOT / ".upstream/napari/src/napari/utils/transforms"
PIN_HASHES = {
    "transforms.py": "3c08fe6aa70cccdf25d420301a99c21015a4b3cd297f22025141f6afa8f8e6ec",
    "transform_utils.py": (
        "08b5d48b62796795a01d3efddf95ec2f1e36fd08b92f213053c559644bbe785c"
    ),
    "_units.py": "53be9fe25f2f71c8371db2e805d51529958ef980b4967b79f8b6810622183d25",
}
BEFORE_TRANSFORMS_SHA = (
    "084bfc619cbc615b438666dca8fc4c4a71c8d3f4b7d3c06f721b27f75bde54ee"
)
KINDS = ("Transform", "TransformChain", "ScaleTranslate", "Affine", "CompositeAffine")
REGRESSION_SELECTORS = (
    "tests/test_transform_objects.py",
    "tests/test_transforms.py",
    ".upstream/napari/src/napari/utils/transforms/_tests/"
    "test_transform_chain.py::test_base_transform_init_is_called",
    ".upstream/napari/src/napari/utils/transforms/_tests/"
    "test_transform_chain.py::test_setitem_invalidates_cache",
    ".upstream/napari/src/napari/utils/transforms/_tests/"
    "test_transform_chain.py::test_delitem_invalidates_cache",
    ".upstream/napari/src/napari/utils/transforms/_tests/"
    "test_transform_chain.py::test_mutate_item_invalidates_cache",
    ".upstream/napari/src/napari/utils/transforms/_tests/"
    "test_transforms.py::test_repeat_shear_setting",
    ".upstream/napari/src/napari/utils/transforms/_tests/"
    "test_transforms.py::test_replace_slice_independence",
)


def _tree(filename):
    raw = (PINNED / filename).read_bytes()
    assert hashlib.sha256(raw).hexdigest() == PIN_HASHES[filename]
    return ast.parse(raw.decode("utf-8"), filename=str(PINNED / filename))


def _module(name, tree, loaded, namespace=None):
    module = ModuleType(name)
    if namespace:
        vars(module).update(namespace)
    assert name not in sys.modules
    sys.modules[name] = module
    loaded.append(name)
    ast.fix_missing_locations(tree)
    exec(compile(tree, f"<test-only-{name}>", "exec"), vars(module))
    return module


def _original(name, evented_list, loaded, namespace_base=None):
    utilities = _module(name + "_utils", _tree("transform_utils.py"), loaded)
    units = _module(name + "_units", _tree("_units.py"), loaded)
    tree = _tree("transforms.py")
    namespace = {"EventedList": evented_list}
    kept = []
    for node in tree.body:
        if isinstance(node, ast.ImportFrom) and node.module in {
            "napari.utils.events",
            "napari.utils.transforms._units",
            "napari.utils.transforms.transform_utils",
        }:
            dependency = units if node.module.endswith("._units") else utilities
            if node.module != "napari.utils.events":
                for alias in node.names:
                    namespace[alias.asname or alias.name] = getattr(
                        dependency, alias.name
                    )
        else:
            kept.append(node)
    tree.body = kept
    if namespace_base is not None:
        transform = next(
            node
            for node in tree.body
            if isinstance(node, ast.ClassDef) and node.name == "Transform"
        )
        assert transform.bases == []
        namespace["CandidateNamespaceBase"] = namespace_base
        transform.bases = [ast.Name(id="CandidateNamespaceBase", ctx=ast.Load())]
    return _module(name, tree, loaded, namespace)


@pytest.fixture(scope="module")
def families():
    from napari.utils.transforms import transforms as current
    from naparixx import _native

    baseline = os.environ.get("NAPARIXX_TRANSFORM_OWNER_BASELINE") == "1"
    if baseline:
        raw = (ROOT / "src/napari/utils/transforms/transforms.py").read_bytes()
        assert hashlib.sha256(raw).hexdigest() == BEFORE_TRANSFORMS_SHA
    else:
        actual_base = _native.TransformNamespaceBase
        for kind in KINDS:
            assert issubclass(getattr(current, kind), actual_base)
    loaded = []
    try:
        original = _original("_transform_owner_pinned", current.EventedList, loaded)
        candidate = _original(
            "_transform_owner_layout_candidate",
            current.EventedList,
            loaded,
            _native.LayerNamespaceBase,
        )
        yield original, current, candidate
    finally:
        for name in reversed(loaded):
            sys.modules.pop(name, None)


def _make(module, kind):
    if kind == "Transform":
        return module.Transform(name="original")
    if kind == "TransformChain":
        return module.TransformChain(
            [
                module.Affine(scale=(2, 3), name="first"),
                module.Affine(translate=(5, 7), name="second"),
            ]
        )
    options = {"scale": (2, 3), "translate": (5, 7), "name": "original"}
    if kind != "ScaleTranslate":
        options["rotate"] = 17
    return getattr(module, kind)(**options)


def _equal(actual, expected):
    if isinstance(expected, np.ndarray):
        assert isinstance(actual, np.ndarray)
        assert actual.dtype == expected.dtype
        np.testing.assert_allclose(actual, expected, rtol=1e-10, atol=1e-10)
    elif isinstance(expected, (tuple, list)):
        assert type(actual) is type(expected)
        assert len(actual) == len(expected)
        for left, right in zip(actual, expected, strict=True):
            _equal(left, right)
    else:
        assert actual == expected


@contextmanager
def _attribute(cls, name, value):
    old = vars(cls)[name]
    setattr(cls, name, value)
    try:
        yield
    finally:
        setattr(cls, name, old)


def _clone(function, namespace):
    clone = FunctionType(
        function.__code__,
        namespace,
        function.__name__,
        function.__defaults__,
        function.__closure__,
    )
    clone.__kwdefaults__ = function.__kwdefaults__
    return clone


@pytest.mark.parametrize("kind", KINDS)
def test_live_namespace_saved_alias_replacement_and_deletion(families, kind):
    class Namespace(dict):
        pass

    for module in families:
        value = _make(module, kind)
        saved = vars(value)
        assert value.__dict__ is saved
        payload = []
        value.plugin_payload = payload
        assert saved["plugin_payload"] is payload
        replacement = Namespace(saved)
        value.__dict__ = replacement
        assert vars(value) is replacement
        saved["name"] = "detached"
        replacement["name"] = "live"
        assert value.name == "live"
        del value.name
        assert "name" not in replacement
        with pytest.raises(AttributeError):
            _ = value.name
        replacement["name"] = "restored"
        with pytest.raises(TypeError):
            value.__dict__ = []
        assert value.__dict__ is replacement
        del value.__dict__
        assert vars(value) == {}
        assert replacement["name"] == "restored"
        assert replacement["plugin_payload"] is payload
        value.__dict__ = replacement
        assert value.plugin_payload is payload
        del replacement["plugin_payload"]
        with pytest.raises(AttributeError):
            _ = value.plugin_payload


def test_real_candidate_allocation_generic_chain_and_cooperative_mro(families):
    from naparixx import _native

    for module in families:
        for kind in KINDS:
            value = object.__new__(getattr(module, kind))
            assert vars(value) is value.__dict__
            assert vars(value) == {}
            assert weakref.ref(value)() is value
        chain_type = module.TransformChain
        assert chain_type.__abstractmethods__ == frozenset()
        assert issubclass(chain_type, Generic)
        assert chain_type.__mro__.index(module.EventedList) < chain_type.__mro__.index(
            module.Transform
        )
        assert get_origin(chain_type.__orig_bases__[0]) is module.EventedList
        assert chain_type.__parameters__[0].__bound__ is module.Transform
        alias = chain_type[module.Affine]
        chain = alias([module.Affine(name="named")])
        assert chain.__orig_class__ == alias
        assert vars(chain)["__orig_class__"] == alias
        assert chain["named"] is chain[0]
        assert chain[:1][0] is chain[0]
    candidate = families[2]
    for kind in KINDS:
        assert issubclass(getattr(candidate, kind), _native.LayerNamespaceBase)
        assert getattr(candidate, kind).__mro__.count(_native.LayerNamespaceBase) == 1


@pytest.mark.parametrize(
    "kind", ["Transform", "Affine", "CompositeAffine", "TransformChain"]
)
def test_inverse_observes_current_cache_dict_and_retained_alias(families, kind):
    for module in families:
        value = _make(module, kind)
        inverse = value.inverse
        saved_cache = value._cache_dict
        assert saved_cache["inverse"] is inverse
        sentinel = object()
        live_cache = {"inverse": sentinel}
        vars(value)["_cache_dict"] = live_cache
        assert value.inverse is sentinel
        assert saved_cache["inverse"] is inverse
        value._clean_cache()
        assert live_cache == {}
        assert saved_cache["inverse"] is inverse
        del vars(value)["_cache_dict"]
        with pytest.raises(AttributeError):
            _ = value.inverse
        vars(value)["_cache_dict"] = saved_cache
        assert value.inverse is inverse


def test_decompose_cache_uses_replaced_live_namespace(families):
    for module in families:
        value = module.Affine(scale=(2, 3), rotate=17)
        scale = value.scale
        saved_cache = value._cache_dict
        assert scale is saved_cache["decompose_linear_matrix"][1]
        vars(value)["_linear_matrix"] = np.diag((11.0, 13.0))
        assert value.scale is scale
        live_cache = {}
        vars(value)["_cache_dict"] = live_cache
        np.testing.assert_allclose(value.scale, [11, 13])
        assert saved_cache["decompose_linear_matrix"][1] is scale
        assert value._cache_dict is live_cache


def test_chain_sequence_payload_and_cache_follow_dictionary_replacement(families):
    for module in families:
        chain = _make(module, "TransformChain")
        first = chain[0]
        saved = vars(chain)
        old_list = saved["_list"]
        old_cache = saved["_cache_dict"]
        third = module.Affine(scale=(7, 11), name="third")
        live_list = [third]
        replacement = dict(saved)
        replacement["_list"] = live_list
        replacement["_cache_dict"] = {}
        chain.__dict__ = replacement
        assert len(chain) == 1
        assert chain[0] is third
        assert chain["third"] is third
        assert old_list[0] is first
        assert old_cache["getitem_0"] is first
        chain.append(module.Affine(name="fourth"))
        assert chain._list is live_list
        assert len(live_list) == 2
        assert len(old_list) == 2
        np.testing.assert_allclose(chain((1, 2)), [7, 22])


def test_chain_mutation_callbacks_clear_live_cache_preserving_saved_cache(families):
    def observe(module):
        first = module.Affine(scale=(2, 3))
        second = module.Affine(scale=(5, 7))
        chain = module.TransformChain([first])
        saved_cache = chain._cache_dict
        old_inverse = chain.inverse
        live_cache = {"sentinel": object()}
        vars(chain)["_cache_dict"] = live_cache
        trace = []

        def changed():
            trace.append(chain._cache_dict is live_cache and not live_cache)

        chain.changed.connect(changed)
        chain[0] = second
        second.scale = (11, 13)
        count = len(trace)
        first.scale = (17, 19)
        # Integer 0 fails source value membership, retaining first's callback.
        assert len(trace) == count + 1
        assert saved_cache["inverse"] is old_inverse
        assert all(trace)
        return trace, chain((1, 2))

    expected = observe(families[0])
    for module in families[1:]:
        _equal(observe(module), expected)


def _restore_outcome(module, kind, operation):
    value = _make(module, kind)
    payload = np.array([3, 5])
    value.plugin_first = payload
    value.plugin_second = payload
    if kind != "ScaleTranslate":
        _ = value.inverse
    try:
        if operation == "copy":
            restored = copy.copy(value)
        elif operation == "deepcopy":
            restored = copy.deepcopy(value)
        else:
            restored = pickle.loads(pickle.dumps(value, protocol=5))
    except Exception as failure:
        return (
            "failure",
            type(failure).__name__,
            str(failure).replace(module.__name__, "<transform-family>"),
        )
    assert type(restored) is type(value)
    assert vars(restored) is restored.__dict__
    assert vars(restored) is not vars(value)
    assert restored.plugin_first is restored.plugin_second
    assert (restored.plugin_first is payload) is (operation == "copy")
    assert (restored._cache_dict is value._cache_dict) is (operation == "copy")
    np.testing.assert_array_equal(restored.plugin_first, payload)
    return "success", restored.name, restored((1.0, 2.0))


@pytest.mark.parametrize("operation", ["copy", "deepcopy", "pickle"])
def test_restoration_preserves_aliases_or_exact_original_failure(families, operation):
    for kind in KINDS:
        expected = _restore_outcome(families[0], kind, operation)
        for module in families[1:]:
            _equal(_restore_outcome(module, kind, operation), expected)


@pytest.mark.parametrize("kind", KINDS)
def test_native_namespace_cycles_weakrefs_and_subclass_finalizer(families, kind):
    def observe(module):
        trace = []

        class Child(getattr(module, kind)):
            def __del__(self):
                trace.append("finalize")

        value = Child()
        value._cache_dict["owner"] = value
        value.changed.connect(lambda value=value: value.name)
        owner = weakref.ref(value)
        del value
        gc.collect()
        assert owner() is None
        gc.collect()
        assert trace == ["finalize"]
        return trace

    expected = observe(families[0])
    for module in families[1:]:
        assert observe(module) == expected


@pytest.mark.parametrize("operation", ["replace", "delete"])
def test_dictionary_finalizer_observes_new_namespace(families, operation):
    def observe(module):
        trace = []
        value = module.Transform()
        owner = weakref.ref(value)

        class Namespace(dict):
            def __del__(self):
                target = owner()
                trace.append(vars(target).get("phase"))

        old = Namespace(vars(value))
        old["phase"] = "old"
        saved = weakref.ref(old)
        value.__dict__ = old
        del old
        if operation == "replace":
            value.__dict__ = {"phase": "new"}
        else:
            del value.__dict__
        assert saved() is None
        assert trace == (["new"] if operation == "replace" else [None])
        return trace

    expected = observe(families[0])
    for module in families[1:]:
        assert observe(module) == expected


def test_constructor_descriptor_failure_preserves_prior_field_publication(families):
    def observe(module, kind):
        trace = []

        class Child(getattr(module, kind)):
            def __setattr__(self, name, value):
                trace.append(name)
                if name == "_cache_dict":
                    raise LookupError("cache publication")
                super().__setattr__(name, value)

        value = object.__new__(Child)
        with pytest.raises(LookupError, match="cache publication"):
            Child.__init__(value)
        assert "func" in vars(value)
        assert "_inverse_func" in vars(value)
        assert "name" in vars(value)
        assert "_cache_dict" not in vars(value)
        return trace, tuple(vars(value))

    for kind in KINDS:
        expected = observe(families[0], kind)
        for module in families[1:]:
            assert observe(module, kind) == expected


@pytest.mark.parametrize("kind", ["Transform", "ScaleTranslate", "Affine"])
def test_inverse_uses_genuine_executing_globals_constructor(families, kind):
    def observe(module):
        value = _make(module, kind)
        result = object()
        calls = []

        def construct(*args, **kwargs):
            calls.append((len(args), tuple(kwargs)))
            return result

        function = inspect.getattr_static(getattr(module, kind), "inverse").fget
        namespace = dict(function.__globals__)
        namespace[kind] = construct
        inverse = _clone(function, namespace)(value)
        assert inverse is result
        return calls

    expected = observe(families[0])
    for module in families[1:]:
        assert observe(module) == expected


def test_decomposition_uses_executing_globals_and_exact_provider_alias(families):
    def observe(module):
        value = module.Affine(rotate=17)
        result = (np.eye(2), np.array([11, 13]), np.array([0.2]))
        calls = []

        def decompose(matrix, **kwargs):
            calls.append((matrix is value.linear_matrix, kwargs))
            return result

        function = module.Affine._setup_decompose_linear_matrix_cache
        namespace = dict(function.__globals__)
        namespace["decompose_linear_matrix"] = decompose
        _clone(function, namespace)(value)
        assert value._cache_dict["decompose_linear_matrix"] is result
        return calls

    expected = observe(families[0])
    assert expected == [(True, {"upper_triangular": True})]
    for module in families[1:]:
        assert observe(module) == expected


@pytest.mark.parametrize(
    "kind", ["TransformChain", "ScaleTranslate", "Affine", "CompositeAffine"]
)
def test_lexical_super_binds_overridden_descriptor_at_constructor_call(families, kind):
    def observe(module):
        target = (
            module.EventedList
            if kind == "TransformChain"
            else module.Affine
            if kind == "CompositeAffine"
            else module.Transform
        )
        original = vars(target)["__init__"]
        trace = []

        class Initializer:
            def __get__(self, receiver, owner):
                trace.append(("bind", type(receiver).__name__))

                def initialize(*args, **kwargs):
                    trace.append(("call", tuple(kwargs)))
                    return original(receiver, *args, **kwargs)

                return initialize

        with _attribute(target, "__init__", Initializer()):
            value = getattr(module, kind)()
        assert "_cache_dict" in vars(value)
        assert trace[0] == ("bind", kind)
        return trace

    expected = observe(families[0])
    for module in families[1:]:
        assert observe(module) == expected


@pytest.mark.parametrize("kind", ["ScaleTranslate", "Affine"])
def test_saved_numerical_adapter_alias_switches_without_reconversion(families, kind):
    from naparixx import _backend

    class ArrayProvider:
        def __init__(self, trace):
            self.trace = trace

        def __array__(self, dtype=None, copy=None):
            self.trace.append((dtype, copy))
            return np.array([[1.0, 2.0]], dtype=dtype)

    original = _make(families[0], kind)
    expected_trace = []
    expected = original(ArrayProvider(expected_trace))
    value = _make(families[1], kind)
    alias = value.__call__
    method = type(value).__call__
    previous = _backend.backend_status()["enabled"]
    try:
        for enabled in (False, True):
            (_backend.enable if enabled else _backend.disable)()
            trace = []
            result = alias(ArrayProvider(trace))
            assert type(value).__call__ is method
            assert alias.__func__ is method
            assert trace == expected_trace
            np.testing.assert_allclose(result, expected, rtol=1e-10, atol=1e-10)
    finally:
        (_backend.enable if previous else _backend.disable)()


def test_actual_image_transform_chain_observes_live_native_field_alias(families):
    from napari.layers import Image

    original, current, _ = families
    image = Image(np.zeros((4, 5)), scale=(2, 3), translate=(5, 7), units=("mm", "um"))
    expected = original.TransformChain(
        [
            original.CompositeAffine(scale=(2, 3), translate=(5, 7)),
            original.Affine(ndim=2),
        ]
    )
    position = (1.0, 2.0)
    np.testing.assert_allclose(image.data_to_world(position), expected(position))
    physical = image._transforms["data2physical"]
    assert isinstance(physical, current.Transform)
    saved = vars(physical)
    assert saved is physical.__dict__
    translation = np.array([11.0, 13.0])
    saved["_translate"] = translation
    assert physical.translate is translation
    physical._clean_cache()
    expected[0].translate = translation
    forward = image.data_to_world(position)
    np.testing.assert_allclose(forward, expected(position))
    np.testing.assert_allclose(image.world_to_data(forward), position)
    assert image._transforms["data2physical"] is physical
    assert vars(physical) is saved
