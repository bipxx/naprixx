"""Finite whole-owner comparisons for MultiScaleData and its protocol policy.

The original classes, validators and name initialization are compiled from exact
pinned source in test-only modules. Current behavior uses the imported fork.
The five accepted shape/index helpers and their five-case selector are reused;
they are dependencies here, not another numerical/generator matrix.

A separately named layout candidate uses the installed, policy-free
LayerNamespaceBase below the complete pinned class and genuine Sequence base.
That proves candidate cooperation only. Once current bodies change, the actual
MultiScaleNamespaceBase must be present beneath the production class. No future
ABI or application fallback is assumed. Private frame/MRO identity, arbitrary
plugins, provider/platform matrices and full workflow/lifetime parity are open.
"""

from __future__ import annotations

import ast
import copy
import gc
import hashlib
import inspect
import pickle
import sys
import weakref
from collections.abc import Sequence
from pathlib import Path
from types import ModuleType, SimpleNamespace
from typing import Protocol

import numpy as np
import pytest

ROOT = Path(__file__).resolve().parents[1]
PINNED = ROOT / ".upstream/napari/src/napari"
CURRENT = ROOT / "src/napari"
MULTI = "layers/_multiscale_data.py"
PROTOCOLS = "layers/_data_protocols.py"
HASHES = {
    MULTI: "6cca21966a2e2734e8e7e98e2a8bb4a9ad80d2787a8a8c400f2e44d9b594f7c4",
    PROTOCOLS: "5e929468dcf863a2521ac2e00c095388eb3b7faaf5bafb8c8d825081f00d4863",
}
BEFORE_HASHES = {
    MULTI: "ce29f86936214bf775907a76cdcb70a8709a51528ca7d8055378c18b1032a1e5",
    PROTOCOLS: HASHES[PROTOCOLS],
}
METHODS = (
    "__init__",
    "size",
    "ndim",
    "__eq__",
    "__add__",
    "__mul__",
    "__rmul__",
    "__array__",
    "__repr__",
)
VALIDATORS = ("assert_protocol", "_raise_protocol_error")
REUSED_SELECTOR = (
    "tests/test_slicing_input_helpers_native.py::"
    "test_multiscale_live_reads_lazy_shapes_and_generator_lifetime"
)
ORIGINAL_SELECTORS = (
    ".upstream/napari/src/napari/layers/image/_tests/test_multiscale.py::"
    "test_multiscale_data_protocol",
    ".upstream/napari/src/napari/layers/image/_tests/test_multiscale.py::"
    "test_random_multiscale",
    ".upstream/napari/src/napari/layers/_tests/test_data_protocol.py::"
    "test_layer_protocol",
    ".upstream/napari/src/napari/layers/_tests/test_data_protocol.py::"
    "test_layer_protocol_raises",
)


class ProbeError(RuntimeError):
    pass


def _source(root, relative):
    raw = (root / relative).read_bytes()
    if root == PINNED:
        assert hashlib.sha256(raw).hexdigest() == HASHES[relative]
    return raw.decode("utf-8")


def _class(source):
    return next(
        n
        for n in ast.parse(source).body
        if isinstance(n, ast.ClassDef) and n.name == "MultiScaleData"
    )


def _definition(source, name, owner=False):
    nodes = _class(source).body if owner else ast.parse(source).body
    return next(n for n in nodes if isinstance(n, ast.FunctionDef) and n.name == name)


def _body(node):
    return ast.dump(ast.Module(body=node.body, type_ignores=[]))


def _names_nodes(source):
    nodes = []
    for n in ast.parse(source).body:
        if isinstance(n, ast.Assign) and any(
            isinstance(t, ast.Name) and t.id == "_OBJ_NAMES" for t in n.targets
        ):
            nodes.append(n)
        elif isinstance(n, ast.Expr) and isinstance(n.value, ast.Call):
            f = n.value.func
            if isinstance(f, ast.Attribute) and (
                (isinstance(f.value, ast.Name) and f.value.id == "_OBJ_NAMES")
                or f.attr == "layer_data_protocol_init_names"
            ):
                nodes.append(n)
    assert nodes
    return nodes


def _execute(source, namespace, path, nodes=None, candidate=False):
    tree = ast.parse(source)
    if nodes is not None:
        futures = [
            n
            for n in tree.body
            if isinstance(n, ast.ImportFrom) and n.module == "__future__"
        ]
        tree.body = copy.deepcopy(futures + nodes)
    else:
        tree.body = [
            n
            for n in tree.body
            if not (
                isinstance(n, ast.ImportFrom)
                and n.module is not None
                and n.module.split(".")[0] in {"napari", "naparixx"}
            )
        ]
        if candidate:
            cls = next(n for n in tree.body if isinstance(n, ast.ClassDef))
            cls.bases.insert(
                0, ast.parse("_native.LayerNamespaceBase", mode="eval").body
            )
    exec(compile(ast.fix_missing_locations(tree), str(path), "exec"), namespace)


@pytest.fixture(scope="module")
def implementations():
    from napari.layers import _data_protocols, _multiscale_data
    from naparixx import _native

    original_sources = {p: _source(PINNED, p) for p in HASHES}
    current_sources = {p: _source(CURRENT, p) for p in HASHES}
    changes = sum(
        _body(_definition(original_sources[MULTI], n, True))
        != _body(_definition(current_sources[MULTI], n, True))
        for n in METHODS
    )
    changes += sum(
        _body(_definition(original_sources[PROTOCOLS], n))
        != _body(_definition(current_sources[PROTOCOLS], n))
        for n in VALIDATORS
    )
    names_changed = [
        ast.dump(n) for n in _names_nodes(original_sources[PROTOCOLS])
    ] != [ast.dump(n) for n in _names_nodes(current_sources[PROTOCOLS])]
    assert (changes, names_changed) in ((0, False), (11, True))
    if not changes:
        for relative in HASHES:
            assert (
                hashlib.sha256((CURRENT / relative).read_bytes()).hexdigest()
                == (BEFORE_HASHES[relative])
            )
    else:
        assert issubclass(
            _multiscale_data.MultiScaleData, _native.MultiScaleNamespaceBase
        )

    modules = []

    def register(name):
        assert name not in sys.modules
        module = ModuleType(name)
        sys.modules[name] = module
        modules.append(module)
        return module.__dict__

    protocol_ns = register("_naparixx_multiscale_pinned_protocols")
    _execute(original_sources[PROTOCOLS], protocol_ns, PINNED / PROTOCOLS)

    def build(name, candidate=False):
        ns = register(name)
        ns.update(
            LayerDataProtocol=protocol_ns["LayerDataProtocol"],
            assert_protocol=protocol_ns["assert_protocol"],
            _native=_native,
        )
        _execute(original_sources[MULTI], ns, PINNED / MULTI, candidate=candidate)
        return SimpleNamespace(
            cls=ns["MultiScaleData"], namespace=ns, protocol_namespace=protocol_ns
        )

    original = build("_naparixx_multiscale_pinned")
    candidate = build("_naparixx_multiscale_layout_candidate", candidate=True)
    current = SimpleNamespace(
        cls=_multiscale_data.MultiScaleData,
        namespace=vars(_multiscale_data),
        protocol_namespace=vars(_data_protocols),
    )
    try:
        yield SimpleNamespace(
            pairs=(original, current),
            layouts=(original, current, candidate),
            candidate=candidate,
            native=_native,
            changed=bool(changes),
            original_sources=original_sources,
            current_sources=current_sources,
        )
    finally:
        for module in modules:
            assert sys.modules.get(module.__name__) is module
            del sys.modules[module.__name__]


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


def test_owned_metadata_and_accepted_helper_boundary(implementations):
    for relative, names, owner in (
        (MULTI, METHODS, True),
        (PROTOCOLS, VALIDATORS, False),
    ):
        for name in names:
            old = _definition(implementations.original_sources[relative], name, owner)
            new = _definition(implementations.current_sources[relative], name, owner)
            assert ast.dump(old.args) == ast.dump(new.args)
            assert ast.dump(old.returns) == ast.dump(new.returns)
            assert [ast.dump(d) for d in old.decorator_list] == [
                ast.dump(d) for d in new.decorator_list
            ]
            assert ast.get_docstring(old) == ast.get_docstring(new)
    for side in implementations.layouts:
        assert side.cls.__abstractmethods__ == frozenset()
        assert isinstance(side.cls([np.zeros((2, 3))]), Sequence)
    assert REUSED_SELECTOR.endswith(
        "test_multiscale_live_reads_lazy_shapes_and_generator_lifetime"
    )


@pytest.mark.parametrize("failure", (False, True))
def test_protocol_name_initialization_uses_declared_services(implementations, failure):
    observations = []

    def observe(source):
        events = []

        class Names(set):
            def update(self, values):
                events.append(("update", tuple(sorted(values))))
                return super().update(values)

        def source_dir(value):
            assert value is Protocol
            events.append(("dir",))
            if failure:
                raise ProbeError("dir")
            return ["alpha", "beta"]

        def source_set(values):
            events.append(("set", tuple(values)))
            return Names(values)

        ns = dict(
            Protocol=Protocol,
            dir=source_dir,
            set=source_set,
            _native=implementations.native,
        )
        if failure:
            with pytest.raises(ProbeError, match="^dir$"):
                _execute(source, ns, PROTOCOLS, _names_nodes(source))
            assert "_OBJ_NAMES" not in ns
        else:
            _execute(source, ns, PROTOCOLS, _names_nodes(source))
            assert ns["_OBJ_NAMES"] == {
                "alpha",
                "beta",
                "__annotations__",
                "__dict__",
                "__weakref__",
            }
        observations.append(events)

    for source in (
        implementations.original_sources[PROTOCOLS],
        implementations.current_sources[PROTOCOLS],
    ):
        observe(source)
    assert observations[0] == observations[1]


@pytest.mark.parametrize(
    "choice",
    (
        "valid",
        "empty",
        "list_error",
        "publication_error",
        "validation_error",
        "live_validation",
    ),
)
def test_constructor_publishes_then_validates_live_backing(
    implementations, monkeypatch, choice
):
    observations = []

    def observe(side):
        events = []
        backing = [] if choice == "empty" else ["first", "second"]
        incoming = object()

        class Wrapper(side.cls):
            def __setattr__(self, name, value):
                events.append(("publish", name, value is backing))
                if choice == "publication_error":
                    raise ProbeError("publication")
                super().__setattr__(name, value)

        wrapper = object.__new__(Wrapper)

        def source_list(value):
            assert value is incoming
            events.append(("list",))
            if choice == "list_error":
                raise ProbeError("list")
            return backing

        def replacement(value):
            events.append(("replacement", value, wrapper._data is backing))

        def validator(value):
            events.append(("validate", value, wrapper._data is backing))
            if choice == "validation_error":
                raise ProbeError("validation")
            if choice == "live_validation":
                backing.append("appended")
                side.namespace["assert_protocol"] = replacement

        expected = {
            "empty": (ValueError, "Multiscale data must be a \\(non-empty\\) sequence"),
            "list_error": (ProbeError, "^list$"),
            "publication_error": (ProbeError, "^publication$"),
            "validation_error": (ProbeError, "^validation$"),
        }.get(choice)
        with monkeypatch.context() as patch:
            patch.setitem(side.namespace, "list", source_list)
            patch.setitem(side.namespace, "assert_protocol", validator)
            if expected:
                with pytest.raises(expected[0], match=expected[1]):
                    wrapper.__init__(incoming)
            else:
                wrapper.__init__(incoming)
        published = "_data" in vars(wrapper)
        assert published == (choice not in {"list_error", "publication_error"})
        if published:
            assert wrapper._data is backing
        observations.append((events, published, backing.copy()))

    for side in implementations.pairs:
        observe(side)
    assert observations[0] == observations[1]
    if choice == "live_validation":
        assert observations[0][0][-1] == ("replacement", "appended", True)


def test_live_dictionary_replacement_and_saved_list_alias(implementations):
    for side in implementations.layouts:
        first, second = np.zeros((2, 3)), np.ones((1, 2))
        incoming = [first, second]
        wrapper = side.cls(incoming)
        saved = vars(wrapper)
        backing = saved["_data"]
        assert backing is not incoming and backing[0] is first
        backing.reverse()
        assert wrapper[0] is second
        replacement = {"_data": [first], "note": object()}
        wrapper.__dict__ = replacement
        assert vars(wrapper) is replacement and wrapper.__dict__ is replacement
        assert saved["_data"] is backing and wrapper[0] is first
        replacement["_data"] = [second]
        assert wrapper[0] is second
        del replacement["_data"]
        with pytest.raises(AttributeError, match="_data"):
            _ = wrapper.size
        replacement["_data"] = backing
        assert wrapper[0] is second


def test_candidate_allocation_generic_sequence_and_descriptor_cooperation(
    implementations,
):
    candidate = implementations.candidate
    assert implementations.native.LayerNamespaceBase in candidate.cls.__mro__
    assert candidate.cls[np.ndarray].__origin__ is candidate.cls
    assert candidate.cls[np.ndarray].__args__ == (np.ndarray,)

    def observe(side):
        events = []

        class DescriptorWrapper(side.cls):
            @property
            def _data(self):
                events.append("get")
                return vars(self)["storage"]

            @_data.setter
            def _data(self, value):
                events.append("set")
                vars(self)["storage"] = value

        obj = object.__new__(DescriptorWrapper)
        obj.__init__([np.ones((2, 3))])
        assert vars(obj) is obj.__dict__ and "_data" not in vars(obj)
        assert obj.size == 6 and obj.ndim == 2
        assert events[0] == "set" and "get" in events
        assert weakref.ref(obj)() is obj
        assert inspect.signature(side.cls.__init__).parameters["data"].name == "data"

    for side in implementations.layouts:
        observe(side)


@pytest.mark.parametrize("operation", ("copy", "deepcopy", "pickle"))
def test_populated_dictionary_and_list_alias_restoration(implementations, operation):
    for side in implementations.layouts:
        array = np.arange(6).reshape(2, 3)
        wrapper = side.cls([array, array])
        wrapper.saved_list = wrapper._data
        if operation == "copy":
            restored = copy.copy(wrapper)
        elif operation == "deepcopy":
            restored = copy.deepcopy(wrapper)
        else:
            restored = pickle.loads(pickle.dumps(wrapper))
        assert type(restored) is side.cls and vars(restored) is restored.__dict__
        assert restored._data is restored.saved_list
        assert restored._data[0] is restored._data[1]
        if operation == "copy":
            assert restored._data is wrapper._data
        else:
            assert restored._data is not wrapper._data
            assert restored._data[0] is not array
        np.testing.assert_array_equal(restored._data[0], array)


def test_namespace_cycle_and_backing_list_release(implementations):
    for side in implementations.layouts:
        wrapper = side.cls([np.ones((2, 3))])
        wrapper.cycle = wrapper
        reference = weakref.ref(wrapper)
        del wrapper
        gc.collect()
        assert reference() is None


def test_sequence_mixins_keep_live_backing_aliases(implementations):
    for side in implementations.pairs:
        first, second = np.asarray([1]), np.asarray([2])
        wrapper = side.cls([first, second, first])
        assert list(wrapper)[0] is first and list(reversed(wrapper))[1] is second
        assert wrapper.count(first) == 2 and wrapper.index(second) == 1
        assert second in wrapper
        saved = wrapper._data
        saved.pop(0)
        assert next(iter(wrapper)) is second and wrapper.count(first) == 1


@pytest.mark.parametrize("attribute", ("size", "ndim"))
def test_first_level_properties_return_exact_dynamic_value(implementations, attribute):
    def observe(side):
        marker = object()
        events = []

        class Level:
            def __getattribute__(self, name):
                events.append(name)
                assert name == attribute
                return marker

        wrapper = object.__new__(side.cls)
        vars(wrapper)["_data"] = [Level()]
        assert getattr(wrapper, attribute) is marker
        assert events == [attribute]

    for side in implementations.pairs:
        observe(side)


@pytest.mark.parametrize("operation", ("eq", "add", "mul", "rmul"))
def test_overloaded_list_operators_preserve_raw_result(implementations, operation):
    observations = []

    def observe(side):
        events, marker = [], object()

        class Backing(list):
            def __eq__(self, other):
                events.append(("eq", other is operand))
                return marker

            def __add__(self, other):
                events.append(("add", other is operand))
                return marker

            def __mul__(self, other):
                events.append(("mul", other is operand))
                return marker

            def __rmul__(self, other):
                events.append(("rmul", other is operand))
                return marker

        class Operand:
            def __mul__(self, other):
                events.append(("operand_mul", other is backing))
                return NotImplemented

        operand, backing = Operand(), Backing([1, 2])
        wrapper = object.__new__(side.cls)
        vars(wrapper)["_data"] = backing
        if operation == "eq":
            result = wrapper == operand
        elif operation == "add":
            result = wrapper + operand
        elif operation == "mul":
            result = wrapper * operand
        else:
            result = operand * wrapper
        assert result is marker and wrapper._data is backing
        observations.append(events)

    for side in implementations.pairs:
        observe(side)
    assert observations[0] == observations[1]


@pytest.mark.parametrize("failure", (False, True))
def test_array_captures_asarray_before_live_backing_lookup(
    implementations, monkeypatch, failure
):
    observations = []

    def observe(side):
        events, marker = [], object()

        def replacement(value):
            raise ProbeError("replacement must not be called")

        class Backing(list):
            def __getitem__(self, index):
                events.append(("getitem", index))
                side.namespace["np"] = SimpleNamespace(asarray=replacement)
                return marker

        def asarray(value):
            events.append(("asarray", value is marker))
            if failure:
                raise ProbeError("array")
            return marker

        class Numpy:
            @property
            def asarray(self):
                events.append(("capture_asarray",))
                return asarray

        wrapper = object.__new__(side.cls)
        vars(wrapper)["_data"] = Backing([object()])
        with monkeypatch.context() as patch:
            patch.setitem(side.namespace, "np", Numpy())
            if failure:
                with pytest.raises(ProbeError, match="^array$"):
                    wrapper.__array__()
            else:
                assert wrapper.__array__() is marker
        observations.append(events)

    for side in implementations.pairs:
        observe(side)
    assert (
        observations[0]
        == observations[1]
        == [("capture_asarray",), ("getitem", -1), ("asarray", True)]
    )


@pytest.mark.parametrize("failure", (False, True))
def test_repr_evaluates_dynamic_helpers_in_source_order(
    implementations, monkeypatch, failure
):
    observations = []

    def observe(side):
        events = []

        class Dtype:
            def __format__(self, specification):
                events.append(("dtype_format", specification))
                if failure:
                    raise ProbeError("dtype")
                return "float32"

        class Wrapper(side.cls):
            @property
            def dtype(self):
                events.append(("dtype",))
                return Dtype()

            @property
            def shapes(self):
                events.append(("shapes",))
                return ((2, 3), (1, 2))

        wrapper = Wrapper([np.ones((2, 3)), np.ones((1, 2))])

        def source_id(value):
            events.append(("id", value is wrapper))
            return 1234

        def source_hex(value):
            events.append(("hex", value))
            return hex(value)

        def source_len(value):
            events.append(("len", value is wrapper))
            return len(value)

        with monkeypatch.context() as patch:
            patch.setitem(side.namespace, "id", source_id)
            patch.setitem(side.namespace, "hex", source_hex)
            patch.setitem(side.namespace, "len", source_len)
            if failure:
                with pytest.raises(ProbeError, match="^dtype$"):
                    repr(wrapper)
                result = None
            else:
                result = repr(wrapper)
                assert result == (
                    "<MultiScaleData at 0x4d2. 2 levels, 'float32', "
                    "shapes: ((2, 3), (1, 2))>"
                )
        observations.append((events, result))

    for side in implementations.pairs:
        observe(side)
    assert observations[0] == observations[1]
    assert observations[0][0][:3] == [("id", True), ("hex", 1234), ("len", True)]


@pytest.mark.parametrize("choice", ("true", "false", "truth_error"))
def test_assert_protocol_reads_error_callback_after_truth(
    implementations, monkeypatch, choice
):
    observations = []

    def observe(side):
        ns, events = side.protocol_namespace, []
        obj, protocol = object(), object()

        def replacement_error(value, expected):
            events.append(("replacement", value is obj, expected is protocol))
            raise ProbeError("missing")

        class Decision:
            def __bool__(self):
                events.append(("truth",))
                ns["_raise_protocol_error"] = replacement_error
                if choice == "truth_error":
                    raise ProbeError("truth")
                return choice == "true"

        def source_isinstance(value, expected):
            events.append(("isinstance", value is obj, expected is protocol))
            return Decision()

        with monkeypatch.context() as patch:
            patch.setitem(ns, "isinstance", source_isinstance)
            patch.setitem(ns, "_raise_protocol_error", ns["_raise_protocol_error"])
            if choice == "true":
                assert ns["assert_protocol"](obj, protocol) is None
            else:
                expected = "truth" if choice == "truth_error" else "missing"
                with pytest.raises(ProbeError, match=f"^{expected}$"):
                    ns["assert_protocol"](obj, protocol)
        observations.append(events)

    for side in implementations.pairs:
        observe(side)
    assert observations[0] == observations[1]
    if choice == "false":
        assert observations[0][-1] == ("replacement", True, True)


@pytest.mark.parametrize("choice", ("missing", "live_names", "annotations_error"))
def test_protocol_diagnostics_and_live_exclusion_set(
    implementations, monkeypatch, choice
):
    observations = []

    def observe(side):
        ns, events = side.protocol_namespace, []

        class Subject:
            pass

        class ExampleProtocol:
            alpha: int

        obj = Subject()
        names = ns["_OBJ_NAMES"].copy()

        def source_getattr(value, name, default):
            events.append(("getattr", name))
            assert value is ExampleProtocol and name == "__annotations__"
            if choice == "annotations_error":
                raise ProbeError("annotations")
            return getattr(value, name, default)

        def source_dir(value):
            if value is ExampleProtocol:
                events.append(("dir", "protocol"))
                if choice == "live_names":
                    names.add("alpha")
                return ["alpha", "beta"]
            assert value is obj
            events.append(("dir", "object"))
            return ["beta"]

        with monkeypatch.context() as patch:
            patch.setitem(ns, "getattr", source_getattr)
            patch.setitem(ns, "dir", source_dir)
            patch.setitem(ns, "_OBJ_NAMES", names)
            if choice == "annotations_error":
                with pytest.raises(ProbeError, match="^annotations$"):
                    ns["_raise_protocol_error"](obj, ExampleProtocol)
                message = None
            else:
                with pytest.raises(TypeError) as captured:
                    ns["_raise_protocol_error"](obj, ExampleProtocol)
                message = str(captured.value)
                missing = "set()" if choice == "live_names" else "{'alpha'}"
                assert message == (
                    "Object of type 'Subject' does not implement 'ExampleProtocol' "
                    f"Protocol.\nMissing methods: {missing}"
                )
                assert ns["_OBJ_NAMES"] is names
        observations.append((events, message))

    for side in implementations.pairs:
        observe(side)
    assert observations[0] == observations[1]
