"""Finite slice-model allocation, construction and restoration comparisons.

The normal current side is the actual imported production class, not a rebuilt
current class. Complete pinned model classes and their request-ID source are
rebuilt independently into genuine importable test-only modules. A separately
labelled candidate adds the installed physical LayerNamespaceBase beneath the
complete before-image classes; its constructors are still the real standard
library's generated baseline, and it proves no native constructor implementation.

Twenty-four prospective cases cover the five models' 29 field references and
the bounded allocation/metadata/descriptor/factory/dictionary/restoration/GC
boundary, including live dictionary authority over warmed numeric helper caches.
Two ordinary original-oracle copy_with cases intentionally expose the
known current callback gap. They have no skip, xfail or alternate passing oracle.
Private generated-code/frame identity, other immutable helper policy, supported
version/platform/plugin matrices and actual Viewer workflow closure remain open.
"""

from __future__ import annotations

import ast
import contextlib
import copy
import dataclasses
import gc
import hashlib
import importlib
import inspect
import pickle
import sys
import warnings
import weakref
from functools import cached_property
from itertools import count
from pathlib import Path
from types import CellType, ModuleType, SimpleNamespace
from typing import Any, Generic, TypeVar

import numpy as np
import numpy.typing as npt
import pytest

ROOT = Path(__file__).resolve().parents[1]
PINNED = ROOT / ".upstream/napari/src/napari"
CURRENT = ROOT / "src/napari"
BEFORE = ROOT / ".artifacts/slice-dataclass-state-before/files/src/napari"
UTILS = "layers/utils/_slice_input.py"
SCALAR = "layers/_scalar_field/_slice.py"
IDS = "layers/base/_slice.py"
IMAGE = "layers/image/_slice.py"
PINNED_HASHES = {
    UTILS: "a79eb82a741085e7c2a2e9f54710706fd812c7f3ba2d90c7a83cd795d700f7d4",
    SCALAR: "0e327552ef2d11eaa198f84e86cac2ddbdefd5fa2b10947c42c45956b1c61188",
    IDS: "606b4e936922e0bb2c1bb6b3eed74a7d819db5b952cb3a954ff16395b8f80cfd",
    IMAGE: "f8b0e977572f7e769bdff90d04619d2f674ea7f091e8576a93d84b38da7fe968",
}
BEFORE_HASHES = {
    UTILS: "78f1fdbbfb5c305709fb0eea1ce9f356526ee755c53ee691caaf77b70b4ce372",
    SCALAR: "4fb4547fcade16096a16e4264aa2b1d8ffb6c89246b3b368301fdfe481dba73d",
    IDS: PINNED_HASHES[IDS],
    IMAGE: "6c88164476da3f462071de48b2fa965e840b77f09ab454967499acbcf71d6cc4",
}
MODELS = {
    "_ThickNDSlice": (UTILS, ("point", "margin_left", "margin_right")),
    "_SliceInput": (UTILS, ("ndisplay", "world_slice", "order")),
    "_ScalarFieldView": (SCALAR, ("raw", "view")),
    "_ScalarFieldSliceResponse": (
        SCALAR,
        ("image", "thumbnail", "tile_to_data", "slice_input", "request_id", "empty"),
    ),
    "_ScalarFieldSliceRequest": (
        SCALAR,
        (
            "slice_input",
            "data_at_data_level",
            "data_at_thumbnail_level",
            "dtype",
            "dask_indexer",
            "data_slice",
            "projection_mode",
            "multiscale",
            "corner_pixels",
            "rgb",
            "data_level",
            "thumbnail_level",
            "level_shapes",
            "downsample_factors",
            "id",
        ),
    ),
}


def _source(root, relative):
    raw = (root / relative).read_bytes()
    hashes = (
        PINNED_HASHES if root == PINNED else BEFORE_HASHES if root == BEFORE else None
    )
    if hashes is not None:
        assert hashlib.sha256(raw).hexdigest() == hashes[relative]
    return raw.decode("utf-8")


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


def _metadata(cls):
    params = cls.__dataclass_params__
    fields = []
    for item in dataclasses.fields(cls):
        fields.append(
            (
                item.name,
                item.type,
                item.init,
                item.repr,
                item.compare,
                item.hash,
                item.kw_only,
                item.default,
                item.default_factory,
                dict(item.metadata),
            )
        )
    return (
        str(inspect.signature(cls)),
        str(inspect.signature(cls.__init__)),
        cls.__doc__,
        cls.__match_args__,
        tuple(
            getattr(params, key)
            for key in ("init", "repr", "eq", "order", "unsafe_hash", "frozen")
        ),
        fields,
    )


@pytest.fixture(scope="module")
def implementations():
    from napari.utils._dtype import normalize_dtype
    from napari.utils.misc import reorder_after_dim_reduction
    from napari.utils.transforms import Affine
    from naparixx import _native

    sources = {
        root: {relative: _source(root, relative) for relative in PINNED_HASHES}
        for root in (PINNED, BEFORE, CURRENT)
    }
    modules = {
        relative: importlib.import_module("napari." + relative[:-3].replace("/", "."))
        for relative in PINNED_HASHES
    }
    current_classes = {
        name: getattr(modules[relative], name) for name, (relative, _) in MODELS.items()
    }
    current_classes["_ImageSliceRequest"] = modules[IMAGE]._ImageSliceRequest
    for name, (relative, names) in MODELS.items():
        pinned = _class(sources[PINNED][relative], name)
        current = _class(sources[CURRENT][relative], name)
        before = _class(sources[BEFORE][relative], name)

        def fields(node):
            return [
                ast.dump(item) for item in node.body if isinstance(item, ast.AnnAssign)
            ]

        assert fields(pinned) == fields(before) == fields(current)
        assert ast.get_docstring(pinned) == ast.get_docstring(current)
        assert (
            tuple(item.name for item in dataclasses.fields(current_classes[name]))
            == names
        )
        for method in (
            item for item in pinned.body if isinstance(item, ast.FunctionDef)
        ):
            replacement = next(
                item
                for item in current.body
                if isinstance(item, ast.FunctionDef) and item.name == method.name
            )
            assert ast.dump(method.args) == ast.dump(replacement.args)
            assert (ast.dump(method.returns) if method.returns else None) == (
                ast.dump(replacement.returns) if replacement.returns else None
            )
            assert ast.get_docstring(method) == ast.get_docstring(replacement)
            assert [ast.dump(item) for item in method.decorator_list] == [
                ast.dump(item) for item in replacement.decorator_list
            ]
    assert sum(len(names) for _, names in MODELS.values()) == 29

    registered = {}
    missing = object()

    def module(name, namespace):
        registered[name] = sys.modules.get(name, missing)
        result = ModuleType(name)
        result.__dict__.update(namespace)
        sys.modules[name] = result
        return result

    def build(root, label, *, candidate=False):
        classes = {}
        common = dict(
            np=np,
            npt=npt,
            Any=Any,
            Generic=Generic,
            TypeVar=TypeVar,
            dataclass=dataclasses.dataclass,
            field=dataclasses.field,
            cached_property=cached_property,
            warnings=warnings,
            _native=_native,
            Affine=Affine,
            normalize_dtype=normalize_dtype,
            reorder_after_dim_reduction=reorder_after_dim_reduction,
            ArrayLike=modules[IMAGE].ArrayLike,
            ImageProjectionMode=modules[IMAGE].ImageProjectionMode,
        )
        ids = module(f"_naparixx_slice_state_{label}_ids", {"count": count})
        # The independent original counter/factory is source-defined, not a
        # current factory imported into the oracle.
        nodes = [
            node
            for node in ast.parse(sources[root][IDS]).body
            if isinstance(node, (ast.Assign, ast.FunctionDef))
        ]
        _execute(sources[root][IDS], nodes, vars(ids), root / IDS)
        common["_next_request_id"] = ids._next_request_id
        for relative, names, suffix in (
            (UTILS, ("_ThickNDSlice", "_SliceInput"), "utils"),
            (
                SCALAR,
                (
                    "_ScalarFieldView",
                    "_ScalarFieldSliceResponse",
                    "_ScalarFieldSliceRequest",
                ),
                "scalar",
            ),
            (IMAGE, ("_ImageSliceRequest",), "image"),
        ):
            target = module(
                f"_naparixx_slice_state_{label}_{suffix}", dict(common, **classes)
            )
            source = sources[root][relative]
            if relative == UTILS:
                assignments = [
                    node
                    for node in ast.parse(source).body
                    if isinstance(node, ast.Assign)
                    and any(
                        isinstance(item, ast.Name) and item.id == "_T"
                        for item in node.targets
                    )
                ]
                _execute(source, assignments, vars(target), root / relative)
            for name in names:
                node = copy.deepcopy(_class(source, name))
                if candidate and name in MODELS:
                    node.bases.insert(
                        0, ast.parse("_native.LayerNamespaceBase", mode="eval").body
                    )
                _execute(source, [node], vars(target), root / relative)
                classes[name] = getattr(target, name)
        return SimpleNamespace(
            classes=classes, candidate=candidate, factory=ids._next_request_id
        )

    try:
        original = build(PINNED, "original")
        # This labelled candidate deliberately retains before-image generated
        # constructors. It is never substituted for actual imported current.
        candidate = build(BEFORE, "candidate", candidate=True)
        current = SimpleNamespace(
            classes=current_classes,
            candidate=False,
            factory=modules[IDS]._next_request_id,
        )
        yield SimpleNamespace(
            original=original,
            current=current,
            allocation_candidate=candidate,
            pairs=(original, current),
            native=_native,
            Affine=Affine,
        )
    finally:
        for name, previous in reversed(tuple(registered.items())):
            if previous is missing:
                del sys.modules[name]
            else:
                sys.modules[name] = previous


def _values(cls):
    return {item.name: object() for item in dataclasses.fields(cls)}


def _assert_fields(instance, values):
    assert tuple(vars(instance)) == tuple(values)
    for name, value in values.items():
        assert getattr(instance, name) is value
        assert vars(instance)[name] is value


@pytest.mark.parametrize("name", tuple(MODELS))
def test_allocation_fields_metadata_generic_and_inheritance(implementations, name):
    original = implementations.original.classes[name]
    # Default factories differ only by their genuinely separate source counter.
    old_metadata = _metadata(original)
    values = _values(original)
    reference_repr = repr(original(**values))
    for side in (*implementations.pairs, implementations.allocation_candidate):
        cls = side.classes[name]
        metadata = _metadata(cls)
        assert metadata[:-1] == old_metadata[:-1]
        for old, new in zip(old_metadata[-1], metadata[-1], strict=True):
            assert old[:8] == new[:8]
            assert old[9] == new[9]
            if old[0] == "id":
                assert new[8] is side.factory
            else:
                assert old[8] is new[8]
        instance = cls(**values)
        _assert_fields(instance, values)
        assert vars(instance) is instance.__dict__
        assert repr(instance) == reference_repr
        assert instance == cls(**values)
        assert hash(instance) == hash(cls(**values))
        with pytest.raises(dataclasses.FrozenInstanceError):
            setattr(instance, next(iter(values)), object())
        with pytest.raises(dataclasses.FrozenInstanceError):
            instance.extra = object()
        raw = object.__new__(cls)
        assert vars(raw) == {}
        assert cls.__init__(raw, **values) is None
        _assert_fields(raw, values)
        child = type(f"Child{name}", (cls,), {})
        child_instance = child(**values)
        _assert_fields(child_instance, values)
        child_instance.extra = object()
        del child_instance.extra
        with pytest.raises(dataclasses.FrozenInstanceError):
            delattr(child_instance, next(iter(values)))
        if side.candidate:
            assert issubclass(cls, implementations.native.LayerNamespaceBase)
            assert isinstance(raw, implementations.native.LayerNamespaceBase)
        if name == "_ThickNDSlice":
            typed = cls[int](**values)
            _assert_fields(typed, values)
            assert len(cls.__parameters__) == 1
            assert cls.__parameters__[0].__name__ == "_T"
            assert "__orig_class__" not in vars(typed)
        if name == "_ScalarFieldSliceRequest":
            image = side.classes["_ImageSliceRequest"]
            assert "__init__" not in vars(image)
            assert image.__init__ is cls.__init__
            _assert_fields(image(**values), values)


class ProbeError(RuntimeError):
    pass


@contextlib.contextmanager
def _constructor_captures(cls, *, saved_object=None, factory=None):
    """Change genuine captures semantically, without regenerating the body.

    No CPython-private cell names are assumed. A future current constructor must
    use its genuine signature-glue captures; absence is a failure, not permission
    to run a generated constructor in place of the installed implementation.
    """
    cells = []
    pending = list(cls.__init__.__closure__ or ())
    seen = set()
    while pending:
        cell = pending.pop()
        if id(cell) in seen:
            continue
        seen.add(id(cell))
        cells.append(cell)
        value = cell.cell_contents
        if isinstance(value, CellType):
            pending.append(value)
    object_cells = [cell for cell in cells if cell.cell_contents is object]
    factory_cells = []
    sentinel_cells = []
    if "id" in cls.__dataclass_fields__:
        original_factory = cls.__dataclass_fields__["id"].default_factory
        factory_cells = [
            cell for cell in cells if cell.cell_contents is original_factory
        ]
        sentinel = inspect.signature(cls.__init__).parameters["id"].default
        sentinel_cells = [cell for cell in cells if cell.cell_contents is sentinel]
        assert len(factory_cells) == len(sentinel_cells) == 1
    assert len(object_cells) == 1, (
        "current constructor lacks its genuine saved-object capture"
    )
    # Preserve actual cells across intra-invocation replacement/emptying. No
    # initial-value snapshot can stand in for the generated source LOAD_DEREF.
    preserved = [(cell, cell.cell_contents) for cell in cells]
    replacements = []
    if saved_object is not None:
        replacements.append((object_cells[0], saved_object))
    if factory is not None:
        replacements.append((factory_cells[0], factory))
    try:
        for cell, value in replacements:
            cell.cell_contents = value
        yield SimpleNamespace(
            saved_object=object_cells[0],
            factory=factory_cells[0] if factory_cells else None,
            sentinel=sentinel_cells[0] if sentinel_cells else None,
        )
    finally:
        for cell, previous in reversed(preserved):
            cell.cell_contents = previous


class SavedObject:
    def __init__(self, events, *, version=0, after_store=None):
        self.events = events
        self.version = version
        self.after_store = after_store

    def __getattribute__(self, name):
        if name != "__setattr__":
            return object.__getattribute__(self, name)
        events = object.__getattribute__(self, "events")
        version = object.__getattribute__(self, "version")
        after_store = object.__getattribute__(self, "after_store")
        events.append(("lookup", version))

        def assign(instance, field, value):
            events.append(("store", field, version))
            result = object.__setattr__(instance, field, value)
            if after_store is not None:
                after_store(field)
            return result

        return assign


class FieldDescriptor:
    def __init__(self, name, events, fail):
        self.name, self.events, self.fail = name, events, fail

    def __get__(self, instance, owner):
        if instance is None:
            return self
        return vars(instance)[self.name]

    def __set__(self, instance, value):
        self.events.append(("descriptor", self.name))
        if self.fail:
            raise ProbeError(self.name)
        vars(instance)[self.name] = value


@pytest.mark.parametrize("failure", (None, "slice_input", "downsample_factors", "id"))
def test_saved_setter_descriptor_order_and_partial_failure(implementations, failure):
    names = MODELS["_ScalarFieldSliceRequest"][1]
    for side in implementations.pairs:
        cls = side.classes["_ScalarFieldSliceRequest"]
        events = []
        provider = SavedObject(events)
        descriptors = {
            name: FieldDescriptor(name, events, name == failure) for name in names
        }
        child = type("DescriptorRequest", (cls,), descriptors)
        instance = object.__new__(child)
        values = _values(cls)
        del values["id"]
        produced = object()

        def factory(_events=events, _produced=produced):
            _events.append(("factory",))
            return _produced

        alternate = SavedObject(events, version=1)
        with _constructor_captures(
            cls, saved_object=provider, factory=factory
        ) as captures:
            if failure is None:
                provider.after_store = lambda name, _alternate=alternate: (
                    setattr(captures.saved_object, "cell_contents", _alternate)
                    if name == names[0]
                    else None
                )
            if failure is None:
                child.__init__(instance, **values)
            else:
                with pytest.raises(ProbeError, match=f"^{failure}$"):
                    child.__init__(instance, **values)
        attempted = names if failure is None else names[: names.index(failure) + 1]
        expected = []
        for index, name in enumerate(attempted):
            version = int(failure is None and index > 0)
            expected.append(("lookup", version))
            if name == "id":
                expected.append(("factory",))
            expected.extend((("store", name, version), ("descriptor", name)))
        assert events == expected
        completed = names if failure is None else names[: names.index(failure)]
        assert tuple(vars(instance)) == completed
        for name in completed:
            assert getattr(instance, name) is (
                produced if name == "id" else values[name]
            )
        if failure is None:
            # Same row, separate source error path: empty saved-object cell
            # after the first successful store must fail before field two.
            instance = object.__new__(child)
            events.clear()
            with _constructor_captures(
                cls, saved_object=provider, factory=factory
            ) as captures:

                def empty_after_first(name):
                    if name == names[0]:
                        del captures.saved_object.cell_contents

                provider.after_store = empty_after_first
                with pytest.raises(NameError):
                    child.__init__(instance, **values)
            assert tuple(vars(instance)) == names[:1]
            assert events == [
                ("lookup", 0),
                ("store", names[0], 0),
                ("descriptor", names[0]),
            ]


class EqualityTrap:
    def __eq__(self, other):
        raise ProbeError("sentinel equality must not be evaluated")

    def __bool__(self):
        raise ProbeError("explicit ID truth must not be evaluated")


@pytest.mark.parametrize(
    "choice", ("omitted", "sentinel", "none", "zero", "equal_trap", "factory_error")
)
def test_exact_factory_sentinel_and_last_store_timing(implementations, choice):
    names = MODELS["_ScalarFieldSliceRequest"][1]
    for side in implementations.pairs:
        cls = side.classes["_ScalarFieldSliceRequest"]
        events = []
        provider = SavedObject(events)
        values = _values(cls)
        del values["id"]
        sentinel = inspect.signature(cls.__init__).parameters["id"].default
        assert sentinel is not dataclasses.MISSING
        explicit = {
            "sentinel": sentinel,
            "none": None,
            "zero": 0,
            "equal_trap": EqualityTrap(),
        }
        if choice in explicit:
            values["id"] = explicit[choice]
        produced = object()
        instance = object.__new__(cls)

        def factory(_instance=instance, _events=events, _produced=produced):
            assert tuple(vars(_instance)) == names[:-1]
            _events.append(("factory",))
            # The final setter was already captured before its RHS factory call.
            captures.saved_object.cell_contents = SavedObject(_events, version=2)
            if choice == "factory_error":
                raise ProbeError("factory")
            return _produced

        def stale_factory():
            raise ProbeError("stale factory cell was snapshotted before field stores")

        initial_factory = stale_factory if choice == "omitted" else factory
        with _constructor_captures(
            cls, saved_object=provider, factory=initial_factory
        ) as captures:
            if choice == "omitted":

                def switch_factory_after_first(name):
                    if name == names[0]:
                        captures.factory.cell_contents = factory

                provider.after_store = switch_factory_after_first
            if choice == "factory_error":
                with pytest.raises(ProbeError, match="^factory$"):
                    cls.__init__(instance, **values)
            else:
                cls.__init__(instance, **values)
        expected = []
        for name in names:
            expected.append(("lookup", 0))
            if name == "id" and choice in ("omitted", "sentinel", "factory_error"):
                expected.append(("factory",))
                if choice == "factory_error":
                    break
            expected.append(("store", name, 0))
        assert events == expected
        assert tuple(vars(instance)) == (
            names[:-1] if choice == "factory_error" else names
        )
        if choice != "factory_error":
            assert instance.id is (
                produced if choice in ("omitted", "sentinel") else explicit[choice]
            )
        assert cls.__dataclass_fields__["id"].default_factory is side.factory

        if choice == "sentinel":
            # The explicit old sentinel ceases to trigger the factory when its
            # actual comparison cell changes during an earlier successful store.
            instance = object.__new__(cls)
            events.clear()
            with _constructor_captures(
                cls, saved_object=provider, factory=factory
            ) as captures:

                def switch_sentinel_after_first(name):
                    if name == names[0]:
                        captures.sentinel.cell_contents = object()

                provider.after_store = switch_sentinel_after_first
                cls.__init__(instance, **values)
            assert instance.id is sentinel
            assert events == [
                event for name in names for event in (("lookup", 0), ("store", name, 0))
            ]

        if choice in ("sentinel", "factory_error"):
            instance = object.__new__(cls)
            events.clear()
            with _constructor_captures(
                cls, saved_object=provider, factory=factory
            ) as captures:

                def empty_after_first(name):
                    if name == names[0]:
                        cell = (
                            captures.sentinel
                            if choice == "sentinel"
                            else captures.factory
                        )
                        del cell.cell_contents

                provider.after_store = empty_after_first
                with pytest.raises(NameError):
                    cls.__init__(instance, **values)
            assert tuple(vars(instance)) == names[:-1]
            assert events == [
                event
                for name in names[:-1]
                for event in (("lookup", 0), ("store", name, 0))
            ] + [("lookup", 0)]


def test_live_dictionary_replacement_deletion_and_frozen_protocol(implementations):
    class Dictionary(dict):
        pass

    for side in (*implementations.pairs, implementations.allocation_candidate):
        for cls in (side.classes[name] for name in MODELS):
            values = _values(cls)
            instance = cls(**values)
            saved = vars(instance)
            first = next(iter(values))
            replacement = Dictionary(values)
            replacement["extra"] = object()
            with pytest.raises(dataclasses.FrozenInstanceError):
                instance.__dict__ = replacement
            object.__setattr__(instance, "__dict__", replacement)
            assert vars(instance) is instance.__dict__ is replacement
            assert tuple(saved) == tuple(values)
            assert instance.extra is replacement["extra"]
            newer = object()
            replacement[first] = newer
            assert getattr(instance, first) is newer
            assert saved[first] is values[first]
            with pytest.raises(TypeError):
                object.__setattr__(instance, "__dict__", [])
            assert vars(instance) is replacement
            object.__delattr__(instance, "__dict__")
            assert vars(instance) == {}
            assert vars(instance) is not replacement
            assert replacement[first] is newer
            assert tuple(saved) == tuple(values)


def _graph(side, affine):
    classes = side.classes
    thick = classes["_ThickNDSlice"]((1.0, 2.0), (0.0, 0.0), (0.0, 0.0))
    slice_input = classes["_SliceInput"](2, thick, (0, 1))
    # Warm the actual current numeric cache and preserve it in all restorations.
    assert slice_input.displayed == [0, 1]
    if "_native_plan" in vars(type(slice_input)):
        assert slice_input._native_plan is vars(slice_input)["_native_plan"]
    array = np.arange(6, dtype=np.uint8).reshape(2, 3)
    view = classes["_ScalarFieldView"](array, array)
    response = classes["_ScalarFieldSliceResponse"](
        view,
        view,
        affine(ndim=2),
        slice_input,
        31,
    )
    args = dict(
        slice_input=slice_input,
        data_at_data_level=array,
        data_at_thumbnail_level=array,
        dtype=np.dtype("uint8"),
        dask_indexer=contextlib.nullcontext,
        data_slice=thick,
        projection_mode="none",
        multiscale=False,
        corner_pixels=np.array([[0, 0], [1, 2]]),
        rgb=False,
        data_level=0,
        thumbnail_level=0,
        level_shapes=np.array([[2, 3]]),
        downsample_factors=np.array([[1.0, 1.0]]),
        id=37,
    )
    request = classes["_ScalarFieldSliceRequest"](**args)
    image_request = classes["_ImageSliceRequest"](**args)
    return (thick, slice_input, view, response, request, image_request)


@pytest.mark.parametrize("operation", ("copy", "deepcopy", "pickle"))
def test_restoration_real_bindings_shared_graph_and_warmed_cache(
    implementations, operation
):
    for side in (*implementations.pairs, implementations.allocation_candidate):
        graph = _graph(side, implementations.Affine)
        request = side.classes["_ScalarFieldSliceRequest"]

        def forbidden_factory():
            raise ProbeError("restoration must not rerun the request factory")

        with _constructor_captures(request, factory=forbidden_factory):
            if operation == "copy":
                restored = tuple(copy.copy(item) for item in graph)
            elif operation == "deepcopy":
                restored = copy.deepcopy(graph)
            else:
                restored = pickle.loads(
                    pickle.dumps(graph, protocol=pickle.HIGHEST_PROTOCOL)
                )
        for old, new in zip(graph, restored, strict=True):
            assert type(new) is type(old)
            assert new is not old
            assert vars(new) is not vars(old)
            assert tuple(vars(new)) == tuple(vars(old))
            if operation == "copy":
                for name, value in vars(old).items():
                    assert vars(new)[name] is value
        old_slice, new_slice = graph[1], restored[1]
        if "_native_plan" in vars(old_slice):
            old_plan, new_plan = (
                vars(old_slice)["_native_plan"],
                vars(new_slice)["_native_plan"],
            )
            assert isinstance(old_plan, implementations.native.SlicePlan)
            assert isinstance(new_plan, implementations.native.SlicePlan)
            assert new_plan.order == old_plan.order
            assert new_plan.ndisplay == old_plan.ndisplay
            assert new_slice._native_plan is new_plan
            assert (new_plan is old_plan) is (operation == "copy")
        assert restored[4].id == restored[5].id == 37
        if operation != "copy":
            thick, slice_input, view, response, request, image_request = restored
            assert slice_input.world_slice is thick
            assert response.image is response.thumbnail is view
            assert (
                response.slice_input
                is request.slice_input
                is image_request.slice_input
                is slice_input
            )
            assert request.data_slice is image_request.data_slice is thick
            assert (
                view.raw
                is view.view
                is request.data_at_data_level
                is request.data_at_thumbnail_level
            )
            assert image_request.data_at_data_level is view.raw
            assert view.raw is not graph[2].raw
            np.testing.assert_array_equal(view.raw, graph[2].raw)


def test_weakrefs_are_nonowning_for_all_models(implementations):
    for side in (*implementations.pairs, implementations.allocation_candidate):
        for cls in (side.classes[name] for name in MODELS):
            instance = cls(**_values(cls))
            observed = weakref.ref(instance)
            assert observed() is instance
            del instance
            gc.collect()
            assert observed() is None


def test_cycles_and_retained_dictionary_payload_are_not_cleared(implementations):
    class Payload:
        pass

    for side in (*implementations.pairs, implementations.allocation_candidate):
        view_cls = side.classes["_ScalarFieldView"]
        response_cls = side.classes["_ScalarFieldSliceResponse"]
        view = view_cls(None, None)
        response = response_cls(view, view, None, None, 1)
        vars(view)["raw"] = response
        view_ref, response_ref = weakref.ref(view), weakref.ref(response)
        del view, response
        gc.collect()
        assert view_ref() is response_ref() is None

        payload = Payload()
        instance = view_cls(payload, payload)
        saved = vars(instance)
        instance_ref, payload_ref = weakref.ref(instance), weakref.ref(payload)
        del instance, payload
        gc.collect()
        assert instance_ref() is None
        assert payload_ref() is saved["raw"] is saved["view"]
        del saved
        gc.collect()
        assert payload_ref() is None


def _copy_with_callback_trace(cls, *, raising):
    events = []
    result = object()

    class Hooked(cls):
        @classmethod
        def make_full(subclass, **kwargs):
            events.append(("callback", subclass, kwargs))
            if raising:
                raise ProbeError("make_full")
            return result

        def __getattribute__(self, name):
            if name in ("make_full", "point", "margin_left", "margin_right", "ndim"):
                events.append(("read", name))
            return super().__getattribute__(name)

    instance = Hooked((1, 2), (3, 4), (5, 6))
    if raising:
        with pytest.raises(ProbeError, match="^make_full$"):
            instance.copy_with()
    else:
        assert instance.copy_with() is result
    assert events == [
        ("read", "make_full"),
        ("read", "point"),
        ("read", "margin_left"),
        ("read", "margin_right"),
        ("read", "ndim"),
        ("read", "point"),
        (
            "callback",
            Hooked,
            {"point": (1, 2), "margin_left": (3, 4), "margin_right": (5, 6), "ndim": 2},
        ),
    ]


def test_copy_with_original_oracle_preserves_overridden_callback(implementations):
    """Known current failure; retain the original expectation without xfail."""
    for side in implementations.pairs:
        _copy_with_callback_trace(side.classes["_ThickNDSlice"], raising=False)


def test_copy_with_original_oracle_preserves_callback_error(implementations):
    """Known current failure; retain the original expectation without xfail."""
    for side in implementations.pairs:
        _copy_with_callback_trace(side.classes["_ThickNDSlice"], raising=True)


def test_live_saved_dictionary_controls_helpers_after_plan_is_warmed(implementations):
    """The canonical fields remain authoritative over an older derived plan."""
    observations = []
    for side in implementations.pairs:
        thick_cls = side.classes["_ThickNDSlice"]
        slice_cls = side.classes["_SliceInput"]
        initial_world = thick_cls((1.0, 2.0, 3.0), (0.0,) * 3, (0.0,) * 3)
        slice_input = slice_cls(2, initial_world, (0, 1, 2))
        saved = vars(slice_input)
        assert saved is slice_input.__dict__
        assert slice_input.displayed == [1, 2]
        assert slice_input.not_displayed == [0]
        if "_native_plan" in vars(slice_cls):
            plan = slice_input._native_plan
            assert saved["_native_plan"] is plan
            assert plan.order == (0, 1, 2)
            assert plan.ndisplay == 2

        changed_world = thick_cls((3.0, 5.0, 7.0), (1.0, 2.0, 3.0), (2.0, 3.0, 4.0))
        changed_order = (2, 1, 0)
        saved.update(order=changed_order, ndisplay=1, world_slice=changed_world)
        assert vars(slice_input) is saved
        assert slice_input.order is changed_order
        assert slice_input.world_slice is changed_world
        assert slice_input.ndisplay == 1
        before_world = changed_world.as_array().copy()
        transform = implementations.Affine(
            ndim=3, scale=(2.0, 3.0, 4.0), translate=(10.0, 20.0, 30.0)
        )
        before_transform = transform.affine_matrix.copy()

        # Run all four actual helpers before comparing observations. A stale
        # displayed result cannot prevent with_ndim or complete data_slice.
        displayed = slice_input.displayed
        not_displayed = slice_input.not_displayed
        expanded = slice_input.with_ndim(4)
        data_slice = slice_input.data_slice(transform)
        data_array = data_slice.as_array()
        observations.append(
            {
                "displayed": displayed,
                "not_displayed": not_displayed,
                "expanded_ndisplay": expanded.ndisplay,
                "expanded_order": expanded.order,
                "expanded_world_slice": (
                    expanded.world_slice.point,
                    expanded.world_slice.margin_left,
                    expanded.world_slice.margin_right,
                ),
                "data_slice": tuple(
                    tuple(None if np.isnan(value) else float(value) for value in row)
                    for row in data_array
                ),
            }
        )
        assert type(expanded) is slice_cls
        assert expanded is not slice_input
        assert type(data_slice) is thick_cls
        assert data_slice is not changed_world
        assert data_array.shape == (3, 3)
        np.testing.assert_array_equal(changed_world.as_array(), before_world)
        np.testing.assert_array_equal(transform.affine_matrix, before_transform)

    expected = {
        "displayed": [0],
        "not_displayed": [2, 1],
        "expanded_ndisplay": 1,
        "expanded_order": (0, 3, 2, 1),
        "expanded_world_slice": (
            (0, 3.0, 5.0, 7.0),
            (0, 1.0, 2.0, 3.0),
            (0, 2.0, 3.0, 4.0),
        ),
        "data_slice": (
            (None, 35.0, 58.0),
            (None, 6.0, 12.0),
            (None, 9.0, 16.0),
        ),
    }
    assert observations[0] == expected
    assert observations[1] == observations[0]
