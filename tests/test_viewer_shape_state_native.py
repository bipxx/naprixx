"""Missing ShapeList/Mesh authority contracts; application execution belongs to root.

Whole source bodies below are independent test-only oracles. The BEFORE candidate
adds only an already accepted policy-free namespace base; installed adoption is a
separate positive requirement. This file never supplies a production fallback.
"""

# Callbacks run in the same family iteration; live cells are intentional.
# ruff: noqa: B023
from __future__ import annotations

import ast
import builtins
import copy
import gc
import hashlib
import importlib
import inspect
import json
import os
import pickle
import sys
import types
import weakref
from contextlib import contextmanager
from pathlib import Path

import numpy as np
import pytest

ROOT = Path(__file__).resolve().parents[1]
BEFORE = ROOT / ".artifacts/viewer-shape-state-before/index.json"
BEFORE_SHA = "85ca0e472971a67f02527552992fac5dcf7e755c96a8c48cca80e1bfbfbd317e"
BASELINE = os.environ.get("NAPARIXX_SHAPE_STATE_BASELINE") == "1"
SHAPE_PATH = "src/napari/layers/shapes/_shape_list.py"
MESH_PATH = "src/napari/layers/shapes/_mesh.py"
CACHE_NAMES = ("_visible_shapes", "_bounding_boxes", "_visible_shapes_indices")
BATCH_LEVEL = "_ShapeList__batched_level"
BATCH_FORCE = "_ShapeList__batch_force_call"
BATCH_CALLED = "_ShapeList__update_displayed_called"
ACCEPTED_PICKERS = (
    "_visible_shapes",
    "_bounding_boxes",
    "inside",
    "_inside_3d",
    "_triangle_intersection",
)


OWNED_DEFINITIONS = (
    ("_ensure_color_arrays", None),
    ("_calculate_array_sizes", None),
    ("_preallocate_arrays", None),
    ("_fill_arrays", None),
    ("_batch_dec", None),
    ("_batch_dec.<locals>._wrapped", None),
    ("ShapeList.__init__", None),
    ("ShapeList._vertices_slice", None),
    ("ShapeList._vertices_slice_available", None),
    ("ShapeList._mesh_vertices_slice", None),
    ("ShapeList._mesh_vertices_slice_available", None),
    ("ShapeList._mesh_triangles_slice", None),
    ("ShapeList._slicess_to_array", None),
    ("ShapeList._mesh_triangles_range_seq", None),
    ("ShapeList._vertices_range_seq", None),
    ("ShapeList._mesh_triangles_slice_available", None),
    ("ShapeList._mesh_vertices_face_slice", None),
    ("ShapeList._mesh_triangles_face_slice", None),
    ("ShapeList._mesh_vertices_edge_slice", None),
    ("ShapeList._mesh_triangles_edge_slice", None),
    ("ShapeList.batched_updates", None),
    ("ShapeList.ndisplay", "getter"),
    ("ShapeList.ndisplay", "setter"),
    ("ShapeList.slice_keys", "getter"),
    ("ShapeList.slice_key", "getter"),
    ("ShapeList.slice_key", "setter"),
    ("ShapeList._update_displayed_triangles_to_shape_index", None),
    ("ShapeList._update_displayed_vertices_to_shape_num", None),
    ("ShapeList._update_displayed", None),
    ("ShapeList.add", None),
    ("ShapeList._add_single_shape", None),
    ("ShapeList._extend_meshes", None),
    ("ShapeList._add_multiple_shapes", None),
    ("ShapeList.remove_all", None),
    ("ShapeList._update_z_order", None),
    ("ShapeList._visible_shapes_indices", "getter"),
    ("ShapeList._clear_cache", None),
    ("Mesh.__init__", None),
    ("Mesh.clear", None),
    ("Mesh.ndisplay", "getter"),
    ("Mesh.ndisplay", "setter"),
)


def _sha(raw):
    return hashlib.sha256(raw).hexdigest()


def _function(owner, name, setter=False):
    value = inspect.getattr_static(owner, name)
    if isinstance(value, property):
        return value.fset if setter else value.fget
    if isinstance(value, (staticmethod, classmethod)):
        return value.__func__
    return getattr(value, "func", value)


def _metadata_ast(raw):
    tree = ast.parse(raw)
    declaration = next(
        n for n in tree.body
        if isinstance(n, ast.ClassDef) and n.name == "MeshArrayDict"
    )
    return ast.dump(declaration, include_attributes=False)


def _source_module(
    raw, name, class_name, candidate_base=None, mesh_array_dict=None
):
    tree = ast.parse(raw)
    if mesh_array_dict is not None:
        # Reuse genuine unchanged TypedDict metadata before annotations evaluate.
        tree.body = [
            n for n in tree.body
            if not (isinstance(n, ast.ClassDef) and n.name == "MeshArrayDict")
        ]
    if candidate_base is not None:
        cls = next(
            n for n in tree.body if isinstance(n, ast.ClassDef) and n.name == class_name
        )
        assert not cls.bases
        cls.bases.append(ast.Name(id="_candidate_base", ctx=ast.Load()))
        ast.fix_missing_locations(tree)
    module = types.ModuleType(name)
    module.__package__ = "napari.layers.shapes"
    module.__file__ = f"<frozen shape-state {name}>"
    namespace = vars(module)
    namespace["__builtins__"] = dict(vars(builtins))
    namespace["_candidate_base"] = candidate_base
    if mesh_array_dict is not None:
        namespace["MeshArrayDict"] = mesh_array_dict
    sys.modules[name] = module  # Genuine copy/pickle class lookup, removed at teardown.
    try:
        exec(compile(tree, module.__file__, "exec", dont_inherit=True), namespace)
    except BaseException:
        sys.modules.pop(name, None)
        raise
    return module


class Family:
    def __init__(self, name, shape_module, mesh_module):
        self.name = name
        self.shape_module, self.mesh_module = shape_module, mesh_module
        self.ShapeList, self.Mesh = shape_module.ShapeList, mesh_module.Mesh
        self.shape_namespace, self.mesh_namespace = (
            vars(shape_module),
            vars(mesh_module),
        )

    def cls(self, kind):
        return self.ShapeList if kind == "ShapeList" else self.Mesh

    def namespace(self, kind):
        return self.shape_namespace if kind == "ShapeList" else self.mesh_namespace


@pytest.fixture(scope="module")
def families():
    assert _sha(BEFORE.read_bytes()) == BEFORE_SHA
    index = json.loads(BEFORE.read_bytes())
    records = {row["path"]: row for row in index["files"]}
    source = {}
    for mode, prefix in [("fork", "src/"), ("pinned", ".upstream/napari/src/")]:
        for short, path in [("shape", SHAPE_PATH), ("mesh", MESH_PATH)]:
            original = prefix + path.removeprefix("src/")
            row = records[original]
            raw = (ROOT / row["before"]).read_bytes()
            assert _sha(raw) == row["before_sha256"] == row["sha256"]
            source[mode, short] = raw
    actual_shape = importlib.import_module("napari.layers.shapes._shape_list")
    actual_mesh = importlib.import_module("napari.layers.shapes._mesh")
    metadata = _metadata_ast(source["fork", "shape"])
    assert metadata == _metadata_ast(source["pinned", "shape"])
    assert metadata == _metadata_ast((ROOT / SHAPE_PATH).read_bytes())
    native = importlib.import_module("naparixx._native")
    made, result = [], []
    try:
        for mode in ("fork", "pinned", "candidate"):
            selected = "fork" if mode == "candidate" else mode
            base = native.VispyNamespaceBase if mode == "candidate" else None
            mesh_name = f"_naparixx_shape_state_{mode}_mesh"
            shape_name = f"_naparixx_shape_state_{mode}_shape_list"
            mesh = _source_module(source[selected, "mesh"], mesh_name, "Mesh", base)
            made.append(mesh_name)
            shape = _source_module(
                source[selected, "shape"], shape_name, "ShapeList", base,
                actual_shape.MeshArrayDict,
            )
            made.append(shape_name)
            shape.Mesh = mesh.Mesh
            result.append(Family(mode, shape, mesh))
        # Already accepted status74 bodies are reused, rather than retested as a
        # second Python picker implementation. Their globals remain their own.
        accepted = result[0].ShapeList
        for family in result[1:]:
            for name in ACCEPTED_PICKERS:
                descriptor = inspect.getattr_static(accepted, name)
                setattr(family.ShapeList, name, descriptor)
                if hasattr(descriptor, "__set_name__"):
                    descriptor.__set_name__(family.ShapeList, name)
        result.append(Family("actual", actual_shape, actual_mesh))
        yield result
    finally:
        for name in made:
            sys.modules.pop(name, None)


def _rectangle(offset=0, z_index=0, readonly=False):
    from napari.layers.shapes._shapes_models import Rectangle

    shape = Rectangle(
        np.array([[offset, offset], [offset + 4, offset + 5.0]]), z_index=z_index
    )
    if readonly:
        shape.data.flags.writeable = False
    return shape


def _warm(owner):
    for name in CACHE_NAMES:
        getattr(owner, name)
    assert all(name in vars(owner) for name in CACHE_NAMES)


def _snapshot(owner):
    out = {}
    for key, value in vars(owner).items():
        if isinstance(value, np.ndarray):
            out[key] = ("array", value.dtype.str, value.shape, value.tolist())
        elif key == "_mesh":
            out[key] = _snapshot(value)
        elif key == "shapes":
            out[key] = [
                (type(s).__name__, s.data.tolist(), s.ndisplay, s.z_index)
                for s in value
            ]
        elif isinstance(value, (int, float, bool, str, type(None))):
            out[key] = value
    return out


class NumpyProxy:
    def __init__(self, **overrides):
        self.overrides = overrides

    def __getattr__(self, name):
        return self.overrides.get(name, getattr(np, name))


@contextmanager
def _namespace_patch(monkeypatch, namespace, name, value):
    with monkeypatch.context() as patch:
        patch.setitem(namespace, name, value)
        yield


def test_source_metadata_and_real_namespace_adoption(families):
    reference, actual = families[0], families[-1]
    for class_name in ("Mesh", "ShapeList"):
        original, current = reference.cls(class_name), actual.cls(class_name)
        assert current.__name__ == original.__name__
        assert current.__doc__ == original.__doc__
        assert current.__annotations__ == original.__annotations__
        instance = current()
        assert vars(instance) is instance.__dict__
        assert weakref.ref(instance)() is instance
    assert len(OWNED_DEFINITIONS) == 41
    for qualified, accessor in OWNED_DEFINITIONS:
        if ".<locals>." in qualified:
            # The factory row exercises this genuine declaration and its cell.
            continue
        if "." in qualified:
            class_name, name = qualified.split(".")
            before = _function(reference.cls(class_name), name, accessor == "setter")
            after = _function(actual.cls(class_name), name, accessor == "setter")
        else:
            before = getattr(reference.shape_module, qualified)
            after = getattr(actual.shape_module, qualified)
        assert inspect.signature(after) == inspect.signature(before), qualified
        assert after.__name__ == before.__name__
        assert after.__qualname__ == before.__qualname__
        assert after.__doc__ == before.__doc__
        assert after.__annotations__ == before.__annotations__
    native = importlib.import_module("naparixx._native")
    candidate = families[2]
    assert issubclass(candidate.ShapeList, native.VispyNamespaceBase)
    assert issubclass(candidate.Mesh, native.VispyNamespaceBase)
    if BASELINE:
        assert actual.ShapeList.__bases__ == (object,)
        assert actual.Mesh.__bases__ == (object,)
    else:
        assert native.ShapeListNamespaceBase in actual.ShapeList.__mro__
        assert native.ShapeMeshNamespaceBase in actual.Mesh.__mro__


@pytest.mark.parametrize("kind", ["ShapeList", "Mesh"])
def test_live_dictionary_alias_replacement_deletion_and_slots(families, kind):
    for family in families:
        cls = family.cls(kind)
        owner = cls()
        saved = vars(owner)
        marker = object()
        saved["marker"] = marker
        assert owner.marker is marker
        replacement = saved.copy()
        replacement["_ndisplay"] = 3
        owner.__dict__ = replacement
        assert vars(owner) is replacement
        assert saved is not replacement and saved["_ndisplay"] == 2
        assert owner.ndisplay == 3 and owner.marker is marker
        del owner.__dict__
        assert vars(owner) == {} and vars(owner) is owner.__dict__
        with pytest.raises(AttributeError):
            _ = owner.ndisplay
        owner.__dict__ = saved
        assert vars(owner) is saved and owner.ndisplay == 2

        class SlotSubclass(cls):
            __slots__ = ("slot_value",)

        raw = object.__new__(SlotSubclass)
        SlotSubclass.__init__(raw)
        raw.slot_value = marker
        assert raw.slot_value is marker and vars(raw) is raw.__dict__
        assert isinstance(raw, cls) and weakref.ref(raw)() is raw
        del raw.slot_value
        assert not hasattr(raw, "slot_value")


@pytest.mark.parametrize("kind", ["ShapeList", "Mesh"])
def test_saved_dictionary_is_only_intentional_cycle_root(families, kind):
    for family in families:
        owner = family.cls(kind)()
        owner.cycle = owner
        drops = []
        reference = weakref.ref(owner, lambda _ref: drops.append("dead"))
        saved = vars(owner)
        del owner
        gc.collect()
        assert reference() is not None and drops == []
        del saved
        gc.collect()
        assert reference() is None and drops == ["dead"]


@pytest.mark.parametrize("restoration", ["copy", "deepcopy", "pickle"])
def test_warmed_shape_cache_and_mesh_aliases_survive_restoration(families, restoration):
    for family in families:
        owner = family.ShapeList([_rectangle()])
        _warm(owner)
        owner.mesh_alias = owner._mesh
        owner.vertex_alias = owner._mesh.vertices
        before = _snapshot(owner)
        if restoration == "copy":
            restored = copy.copy(owner)
        elif restoration == "deepcopy":
            restored = copy.deepcopy(owner)
        else:
            restored = pickle.loads(pickle.dumps(owner, protocol=4))
        assert type(restored) is type(owner)
        assert vars(restored) is not vars(owner)
        assert _snapshot(restored) == before
        assert restored.mesh_alias is restored._mesh
        assert type(restored._mesh) is family.Mesh
        assert restored.vertex_alias is restored._mesh.vertices
        assert all(name in vars(restored) for name in CACHE_NAMES)
        np.testing.assert_array_equal(
            restored._visible_shapes_indices, owner._visible_shapes_indices
        )
        for cloned, original in zip(
            restored._bounding_boxes, owner._bounding_boxes, strict=True
        ):
            np.testing.assert_array_equal(cloned, original)
        assert restored._visible_shapes is vars(restored)["_visible_shapes"]
        if restoration == "copy":
            assert restored._mesh is owner._mesh and restored.shapes is owner.shapes
            assert restored._bounding_boxes is owner._bounding_boxes
        else:
            assert (
                restored._mesh is not owner._mesh
                and restored.shapes is not owner.shapes
            )
            assert restored._visible_shapes[0][1] is restored.shapes[0]
        restored._clear_cache()
        assert all(name not in vars(restored) for name in CACHE_NAMES)
        assert all(name in vars(owner) for name in CACHE_NAMES)


@pytest.mark.parametrize("entry", ["construct", "clear", "set_ndisplay"])
def test_mesh_partial_sequential_store_and_fresh_clear_dispatch(
    families, monkeypatch, entry
):
    for family in families:
        owner = object.__new__(family.Mesh) if entry == "construct" else family.Mesh()
        old = vars(owner).copy()
        calls = []

        def empty(shape, *args, **kwargs):
            calls.append(tuple(shape) if isinstance(shape, tuple) else shape)
            if len(calls) == 3:
                raise LookupError("third mesh allocation")
            return np.empty(shape, *args, **kwargs)

        with _namespace_patch(
            monkeypatch, family.mesh_namespace, "np", NumpyProxy(empty=empty)
        ):
            with pytest.raises(LookupError, match="third mesh allocation"):
                if entry == "construct":
                    family.Mesh.__init__(owner, ndisplay=3)
                elif entry == "clear":
                    owner.clear()
                else:
                    owner.ndisplay = 3
        ndim = 2 if entry == "clear" else 3
        assert calls == [(0, ndim)] * 3
        assert owner._ndisplay == ndim
        assert owner.vertices.shape == owner.vertices_centers.shape == (0, ndim)
        if entry == "construct":
            assert list(vars(owner)) == ["_ndisplay", "vertices", "vertices_centers"]
        else:
            assert owner.vertices is not old["vertices"]
            assert owner.vertices_centers is not old["vertices_centers"]
            for name in old.keys() - {"_ndisplay", "vertices", "vertices_centers"}:
                assert vars(owner)[name] is old[name]

        log = []

        class Override(family.Mesh):
            def clear(self):
                log.append(("clear", self._ndisplay))

        custom = Override(2)
        custom.ndisplay = 2
        custom.ndisplay = 3
        assert log == [("clear", 2), ("clear", 3)]


def test_shape_constructor_mesh_failure_preserves_exact_publication(
    families, monkeypatch
):
    expected = [
        "_ndisplay",
        "shapes",
        "_displayed",
        "_slice_key",
        "displayed_vertices",
        "displayed_vertices_to_shape_num",
        "displayed_indices",
        "_vertices",
        "_vertices_index",
        "_z_index",
        "_z_order",
    ]
    for family in families:
        owner = object.__new__(family.ShapeList)
        calls = []

        def failed_mesh(*, ndisplay):
            calls.append(ndisplay)
            assert list(vars(owner)) == expected
            raise LookupError("mesh construction")

        with _namespace_patch(monkeypatch, family.shape_namespace, "Mesh", failed_mesh):
            with pytest.raises(LookupError, match="mesh construction"):
                family.ShapeList.__init__(owner, (), ndisplay=3)
        assert calls == [3] and list(vars(owner)) == expected
        assert owner.shapes == [] and owner._vertices.shape == (0, 3)
        assert not hasattr(owner, "_mesh") and not hasattr(owner, BATCH_LEVEL)


@pytest.mark.parametrize("entry", ["constructor", "single", "multiple"])
def test_real_shape_assembly_readonly_input_and_distinct_displayed_fields(
    families, entry
):
    outcomes = []
    for family in families:
        first = _rectangle(z_index=2, readonly=True)
        second = _rectangle(10, z_index=1)
        data = first.data
        frozen = data.copy()
        if entry == "constructor":
            owner = family.ShapeList([first, second])
        else:
            owner = family.ShapeList()
            if entry == "single":
                owner.add(first)
                owner.add(second)
            else:
                owner.add([first, second])
        assert owner.shapes[0] is first and owner.shapes[1] is second
        assert first.data is data and not data.flags.writeable
        np.testing.assert_array_equal(data, frozen)
        np.testing.assert_array_equal(
            owner.displayed_indices, np.array([], dtype=np.int64)
        )
        assert "displayed_index" in vars(owner)
        np.testing.assert_array_equal(owner.displayed_index, owner._vertices_index[:2])
        assert owner._mesh.vertices_index.size == 3
        assert owner._mesh.triangles_index.size == 3
        assert (
            owner._mesh.displayed_triangles.shape[0] == owner._mesh.triangles.shape[0]
        )
        assert (
            owner._mesh.displayed_triangles_to_shape_index.shape[0]
            == owner._mesh.triangles.shape[0]
        )
        outcomes.append(_snapshot(owner))
    assert all(state == outcomes[0] for state in outcomes[1:])


def test_nonsequence_constructor_consumes_once_and_fresh_add_override(families):
    for family in families:
        shape = _rectangle()
        log = []

        class Once:
            def __iter__(self):
                log.append("iterate")
                if log.count("iterate") != 1:
                    raise AssertionError("second constructor iteration")
                yield shape

        owner = family.ShapeList(Once())
        assert log == ["iterate"] and owner.shapes == [shape]
        seen = []

        class Override(family.ShapeList):
            def add(self, data, *args, **kwargs):
                seen.append((data, self._mesh.ndisplay, getattr(self, BATCH_LEVEL)))

        value = [shape]
        overridden = Override(value, ndisplay=2)
        assert seen == [(value, 2, 0)] and overridden.shapes == []


@pytest.mark.parametrize("failure", [False, True])
def test_shape_ndisplay_setter_publication_before_mesh_callback(families, failure):
    for family in families:
        owner = family.ShapeList()
        old_shapes, old_faces, old_edges = (
            owner.shapes,
            owner._face_color,
            owner._edge_color,
        )
        trace = []

        class MeshProbe:
            @property
            def ndisplay(self):
                return 2

            @ndisplay.setter
            def ndisplay(self, value):
                trace.append(("mesh-set", value, owner._ndisplay))
                if failure:
                    raise LookupError("mesh setter")

        owner._mesh = MeshProbe()
        if failure:
            with pytest.raises(LookupError, match="mesh setter"):
                owner.ndisplay = 3
            assert trace == [("mesh-set", 3, 3)]
            assert owner.shapes is old_shapes and owner._face_color is old_faces
            assert owner._edge_color is old_edges and getattr(owner, BATCH_LEVEL) == 0
        else:
            # Override subsequent owned callees only to observe their actual call order.
            owner.remove_all = lambda: trace.append(
                ("remove", getattr(owner, BATCH_LEVEL))
            )
            owner._add_multiple_shapes = lambda shapes, **kw: trace.append(
                (
                    "add",
                    shapes is old_shapes,
                    kw["face_colors"] is old_faces,
                    kw["edge_colors"] is old_edges,
                )
            )
            owner.ndisplay = 3
            assert trace == [
                ("mesh-set", 3, 3),
                ("remove", 1),
                ("add", True, True, True),
            ]
            assert owner._ndisplay == 3 and getattr(owner, BATCH_LEVEL) == 0


def test_cache_current_dictionary_and_per_pop_fresh_lookup(families):
    for family in families:
        owner = family.ShapeList([_rectangle()])
        _warm(owner)
        saved = vars(owner)
        replacement = saved.copy()
        marker = object()
        replacement["_visible_shapes"] = marker
        owner.__dict__ = replacement
        assert owner._visible_shapes is marker
        owner._clear_cache()
        assert all(name not in replacement for name in CACHE_NAMES)
        assert all(name in saved for name in CACHE_NAMES)
        # Three source expressions reload __dict__; the first callback replaces it.
        later = {name: object() for name in CACHE_NAMES}
        log = []

        class ReplacingDict(dict):
            def pop(self, key, default=None):
                log.append(key)
                result = super().pop(key, default)
                owner.__dict__ = later
                return result

        first = ReplacingDict({name: object() for name in CACHE_NAMES})
        owner.__dict__ = first
        owner._clear_cache()
        assert log == ["_bounding_boxes"]
        assert "_visible_shapes" in first and "_visible_shapes_indices" in first
        assert list(later) == ["_bounding_boxes"]
        assert vars(owner) is later


@pytest.mark.parametrize(
    "branch", ["no_update", "nested", "body_error", "final_update_error"]
)
def test_batch_suspension_nested_exit_and_partial_error_counters(families, branch):
    for family in families:
        owner = family.ShapeList()
        trace = []
        original_update = owner._update_displayed

        def update():
            trace.append((getattr(owner, BATCH_LEVEL), getattr(owner, BATCH_FORCE)))
            if branch == "final_update_error" and getattr(owner, BATCH_FORCE):
                raise LookupError("final display update")
            return original_update()

        owner._update_displayed = update
        context = owner.batched_updates()
        assert isinstance(context.gen, types.GeneratorType)
        assert getattr(owner, BATCH_LEVEL) == 0  # No eager context body execution.
        if branch == "no_update":
            with context:
                assert getattr(owner, BATCH_LEVEL) == 1
            assert trace == []
        elif branch == "final_update_error":
            with pytest.raises(LookupError, match="final display update"):
                with context:
                    owner._update_displayed()
            assert trace == [(1, False), (1, True)]
            assert getattr(owner, BATCH_LEVEL) == 1
            assert getattr(owner, BATCH_FORCE) is True
            assert getattr(owner, BATCH_CALLED) == 1
            continue
        elif branch == "body_error":
            with pytest.raises(LookupError, match="batch body"):
                with context:
                    owner._update_displayed()
                    raise LookupError("batch body")
            assert trace == [(1, False), (1, True)]
        else:
            with context:
                owner._update_displayed()
                with owner.batched_updates():
                    owner._update_displayed()
                    assert getattr(owner, BATCH_LEVEL) == 2
                assert getattr(owner, BATCH_CALLED) == 2
                assert getattr(owner, BATCH_LEVEL) == 1
            assert trace == [(1, False), (2, False), (1, True)]
        assert getattr(owner, BATCH_LEVEL) == 0
        assert getattr(owner, BATCH_FORCE) is False
        assert getattr(owner, BATCH_CALLED) == 0


@pytest.mark.parametrize("failure", [False, True])
def test_batch_decorator_genuine_live_method_cell_and_exit_order(families, failure):
    for family in families:
        trace = []
        marker = object()

        def original(self, value=4):
            """Original callable binding metadata."""
            trace.append(("old-method", value))
            return marker

        wrapped = family.shape_module._batch_dec(original)
        assert isinstance(wrapped, types.FunctionType)
        assert wrapped.__wrapped__ is original
        assert wrapped.__name__ == original.__name__
        assert wrapped.__doc__ == original.__doc__
        assert inspect.signature(wrapped) == inspect.signature(original)
        cells = [
            cell for cell in wrapped.__closure__ or () if cell.cell_contents is original
        ]
        assert len(cells) == 1

        def replacement(receiver, value=4):
            trace.append(("new-method", value))
            if failure:
                raise LookupError("decorated body")
            return marker

        class Context:
            def __enter__(self):
                trace.append("enter")
                cells[0].cell_contents = replacement

            def __exit__(self, typ, error, tb):
                trace.append(("exit", typ.__name__ if typ else None))
                return False

        class Receiver:
            def batched_updates(self):
                trace.append("capture-context")
                return Context()

        receiver = Receiver()
        if failure:
            with pytest.raises(LookupError, match="decorated body"):
                wrapped(receiver, value=7)
            assert trace == [
                "capture-context",
                "enter",
                ("new-method", 7),
                ("exit", "LookupError"),
            ]
        else:
            assert wrapped(receiver, value=7) is marker
            assert trace == [
                "capture-context",
                "enter",
                ("new-method", 7),
                ("exit", None),
            ]
