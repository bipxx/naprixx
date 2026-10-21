"""Whole Shape family state/control exits; root alone executes application code.

Frozen full modules are test-only oracles. Class dependency rebinding preserves
genuine ABCMeta/lexical cells. Optional provider doubles prove dispatch/errors
only; genuine optional-library acceptance remains explicitly open.
"""

# All callbacks/cells are consumed before the next source-family iteration.
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
from types import SimpleNamespace

import numpy as np
import pytest

ROOT = Path(__file__).resolve().parents[1]
BEFORE = ROOT / ".artifacts/viewer-shape-model-before/index.json"
BEFORE_SHA = "004a88143d5c47d858c855bda37957afa4016997d6230b3d3f64527ef992d507"
BASELINE = os.environ.get("NAPARIXX_SHAPE_MODEL_BASELINE") == "1"
DIRECTORY = "src/napari/layers/shapes/_shapes_models/"
CLASS_FILES = (
    ("Shape", "shape.py"),
    ("PolygonBase", "_polygon_base.py"),
    ("Rectangle", "rectangle.py"),
    ("Ellipse", "ellipse.py"),
    ("Line", "line.py"),
    ("Path", "path.py"),
    ("Polygon", "polygon.py"),
)
CACHE_NAMES = ("dims_displayed", "data_displayed")
DATA = {
    "Rectangle": [[2, 2], [7, 8]],
    "Ellipse": [[4, 5], [2, 3]],
    "Line": [[2, 2], [7, 8]],
    "PolygonBase": [[2, 2], [2, 8], [6, 9], [8, 5], [6, 2]],
    "Path": [[2, 2], [2, 8], [6, 9], [8, 5], [6, 2]],
    "Polygon": [[2, 2], [2, 8], [6, 9], [8, 5], [6, 2]],
}


def _sha(raw):
    return hashlib.sha256(raw).hexdigest()


def _function(cls, name, setter=False):
    value = inspect.getattr_static(cls, name)
    if isinstance(value, property):
        return value.fset if setter else value.fget
    if isinstance(value, (classmethod, staticmethod)):
        return value.__func__
    return getattr(value, "func", value)


def _module(raw, name, class_name, bindings, candidate_base=None):
    tree = ast.parse(raw)
    # Only declarations of already-created same-family classes are rebound.
    # Other imported helpers, providers, typing and Enum objects stay genuine.
    nodes, needed_bindings = [], {}
    for node in tree.body:
        if isinstance(node, ast.ImportFrom) and node.module in {
            "napari.layers.shapes._shapes_models.shape",
            "napari.layers.shapes._shapes_models._polygon_base",
        }:
            needed_bindings.update(
                {
                    alias.name: bindings[alias.name]
                    for alias in node.names
                    if alias.name in bindings
                }
            )
            node.names = [alias for alias in node.names if alias.name not in bindings]
            if not node.names:
                continue
        if (
            candidate_base is not None
            and isinstance(node, ast.ClassDef)
            and node.name == "Shape"
        ):
            assert len(node.bases) == 1 and isinstance(node.bases[0], ast.Name)
            assert node.bases[0].id == "ABC"
            node.bases.insert(0, ast.Name(id="_candidate_base", ctx=ast.Load()))
        nodes.append(node)
    tree.body = nodes
    ast.fix_missing_locations(tree)
    module = types.ModuleType(name)
    module.__package__ = "napari.layers.shapes._shapes_models"
    module.__file__ = f"<frozen Shape model {name}>"
    namespace = vars(module)
    namespace["__builtins__"] = dict(vars(builtins))
    namespace.update(needed_bindings)
    if candidate_base is not None and class_name == "Shape":
        namespace["_candidate_base"] = candidate_base
    sys.modules[name] = module  # Genuine stdlib copy/pickle resolution.
    try:
        exec(compile(tree, module.__file__, "exec", dont_inherit=True), namespace)
    except BaseException:
        sys.modules.pop(name, None)
        raise
    return module


class Family:
    def __init__(self, name, modules):
        self.name, self.modules = name, modules
        self.classes = {name: getattr(module, name) for name, module in modules.items()}
        self.namespace = vars(modules["Shape"])

    def cls(self, name):
        return self.classes[name]

    @contextmanager
    def pure(self, monkeypatch):
        with monkeypatch.context() as patch:
            patch.setitem(
                self.namespace,
                "TRIANGULATION_BACKEND",
                self.namespace["TriangulationBackend"].pure_python,
            )
            yield patch


@pytest.fixture(scope="module")
def families():
    assert _sha(BEFORE.read_bytes()) == BEFORE_SHA
    index = json.loads(BEFORE.read_bytes())
    records = {row["path"]: row for row in index["files"]}
    source = {}
    for kind, prefix in [("fork", "src/"), ("pinned", ".upstream/napari/src/")]:
        for class_name, filename in CLASS_FILES:
            path = prefix + (DIRECTORY + filename).removeprefix("src/")
            row = records[path]
            raw = (ROOT / row["before"]).read_bytes()
            assert _sha(raw) == row["sha256"] == row["before_sha256"]
            source[kind, class_name] = raw
    actual = {
        name: importlib.import_module(
            "napari.layers.shapes._shapes_models." + filename[:-3]
        )
        for name, filename in CLASS_FILES
    }
    native = importlib.import_module("naparixx._native")
    made, result = [], []
    try:
        for kind in ("fork", "pinned", "candidate"):
            selected = "fork" if kind == "candidate" else kind
            modules, bindings = {}, {}
            base = native.VispyNamespaceBase if kind == "candidate" else None
            for name, _filename in CLASS_FILES:
                module_name = f"_naparixx_shape_model_{kind}_{name}"
                module = _module(
                    source[selected, name], module_name, name, bindings, base
                )
                made.append(module_name)
                modules[name] = module
                if name in {"Shape", "PolygonBase"}:
                    bindings[name] = getattr(module, name)
            result.append(Family(kind, modules))
        accepted = inspect.getattr_static(result[0].cls("Shape"), "_all_triangles")
        for family in result[1:]:
            family.cls("Shape")._all_triangles = accepted
        result.append(Family("actual", actual))
        yield result, index
    finally:
        for name in made:
            sys.modules.pop(name, None)


class NumpyProxy:
    def __init__(self, **overrides):
        self.overrides = overrides

    def __getattr__(self, name):
        return self.overrides.get(name, getattr(np, name))


def _warm(owner):
    return tuple(getattr(owner, name) for name in CACHE_NAMES)


def _snapshot(owner):
    result = {}
    for name, value in vars(owner).items():
        if isinstance(value, np.ndarray):
            result[name] = value.copy()
        elif isinstance(value, (list, tuple, str, bool, int, float, type(None))):
            result[name] = copy.deepcopy(value)
    return result


def _equal(left, right):
    assert left.keys() == right.keys()
    for name in left:
        if isinstance(left[name], np.ndarray):
            assert right[name].dtype == left[name].dtype
            assert right[name].shape == left[name].shape
            np.testing.assert_allclose(right[name], left[name], rtol=1e-6, atol=1e-6)
        else:
            assert right[name] == left[name], name


def test_real_class_metadata_abc_and_native_inherited_namespace(families):
    import abc

    rows, index = families
    reference, actual = rows[0], rows[-1]
    assert len(index["owned_scope"]) == 60
    for name, _filename in CLASS_FILES:
        original, current = reference.cls(name), actual.cls(name)
        assert isinstance(current, abc.ABCMeta)
        assert (
            current.__name__ == original.__name__
            and current.__doc__ == original.__doc__
        )
        assert current.__abstractmethods__ == original.__abstractmethods__
        assert current.__annotations__ == original.__annotations__
        for member, descriptor in vars(original).items():
            if not (
                isinstance(
                    descriptor,
                    (types.FunctionType, property, staticmethod, classmethod),
                )
                or hasattr(descriptor, "func")
            ):
                continue
            before, after = _function(original, member), _function(current, member)
            assert inspect.signature(after) == inspect.signature(before), (name, member)
            assert before.__name__ == after.__name__
            assert before.__qualname__ == after.__qualname__
            assert before.__doc__ == after.__doc__
            assert before.__annotations__ == after.__annotations__
            assert bool(getattr(before, "__isabstractmethod__", False)) == bool(
                getattr(after, "__isabstractmethod__", False)
            )
            if isinstance(descriptor, property) and descriptor.fset is not None:
                a, b = descriptor.fset, inspect.getattr_static(current, member).fset
                assert inspect.signature(a) == inspect.signature(b)
                assert a.__annotations__ == b.__annotations__ and a.__doc__ == b.__doc__
                assert bool(getattr(a, "__isabstractmethod__", False)) == bool(
                    getattr(b, "__isabstractmethod__", False)
                )
        for member in ("__init__", "__new__"):
            if member not in vars(original):
                continue
            function = _function(current, member)
            if "__class__" in function.__code__.co_freevars:
                position = function.__code__.co_freevars.index("__class__")
                assert function.__closure__[position].cell_contents is current
    native = importlib.import_module("naparixx._native")
    assert native.VispyNamespaceBase in rows[2].cls("Shape").__mro__
    if BASELINE:
        assert actual.cls("Shape").__bases__ == (abc.ABC,)
    else:
        assert actual.cls("Shape").__bases__ == (
            native.ShapeModelNamespaceBase,
            abc.ABC,
        )
    for name, _filename in CLASS_FILES[1:]:
        base = "PolygonBase" if name in {"Path", "Polygon"} else "Shape"
        assert actual.cls(name).__bases__ == (actual.cls(base),)


@pytest.mark.parametrize(
    "backend",
    ["bermuda", "partsegcore", "triangle", "pure_python", "fastest_available"],
)
def test_backend_alias_mutations_precede_genuine_abstract_allocation_failure(
    families, monkeypatch, backend
):
    for family in families[0]:
        cls = family.cls("Shape")
        with monkeypatch.context() as patch:
            namespace = family.namespace
            patch.setitem(
                namespace,
                "TRIANGULATION_BACKEND",
                getattr(namespace["TriangulationBackend"], backend),
            )
            patch.setitem(namespace, "bermuda", SimpleNamespace())
            patch.setitem(namespace, "partsegcore_triangulate", SimpleNamespace())
            patch.setitem(sys.modules, "triangle", types.ModuleType("triangle"))
            old_mesh = cls.__dict__["_set_meshes"]
            old_edge = cls.__dict__["_triangulate_edge"]
            patch.setattr(cls, "_set_meshes", old_mesh)

            def sentinel(self, *args, **kwargs):
                return None

            patch.setattr(cls, "_triangulate_edge", sentinel)
            with pytest.raises(TypeError, match="abstract"):
                cls()
            target = {
                "bermuda": "_set_meshes_compiled_bermuda",
                "partsegcore": "_set_meshes_compiled_partseg",
                "triangle": "_set_meshes_triangle",
                "pure_python": "_set_meshes_py",
                "fastest_available": "_set_meshes_compiled_bermuda",
            }[backend]
            assert cls.__dict__["_set_meshes"] is getattr(cls, target)
            if backend in {"bermuda", "partsegcore", "fastest_available"}:
                edge = (
                    "_triangulate_edge_partseg"
                    if backend == "partsegcore"
                    else "_triangulate_edge_bermuda"
                )
                assert cls.__dict__["_triangulate_edge"] is getattr(cls, edge)
            else:
                assert cls.__dict__["_triangulate_edge"] is sentinel
        assert cls.__dict__["_set_meshes"] is old_mesh
        assert cls.__dict__["_triangulate_edge"] is old_edge


@pytest.mark.parametrize(
    "name", ["PolygonBase", "Rectangle", "Ellipse", "Line", "Path", "Polygon"]
)
def test_six_constructors_use_genuine_cooperative_lexical_super(
    families, monkeypatch, name
):
    for family in families[0]:
        original = family.cls(name)
        base = family.cls("PolygonBase" if name in {"Path", "Polygon"} else "Shape")
        trace = []

        class Hook(base):
            def __init__(self, *args, **kwargs):
                trace.append((args, kwargs.copy(), list(vars(self))))
                super().__init__(*args, **kwargs)

        probe = type(f"Probe{name}", (original, Hook), {})
        data = np.asarray(DATA[name])
        with family.pure(monkeypatch):
            owner = probe(data, edge_width=2, z_index=9, dims_order=[0, 1])
        assert len(trace) == 1 and trace[0][2] == []
        args, kwargs, _fields = trace[0]
        assert kwargs["edge_width"] == 2 and kwargs["z_index"] == 9
        assert kwargs["dims_order"] == [0, 1] and kwargs["ndisplay"] == 2
        if name == "Path":
            assert len(args) == 1 and args[0] is data
            assert kwargs["closed"] is False and kwargs["filled"] is False
            assert kwargs["name"] == "path"
        elif name == "Polygon":
            assert args == () and kwargs["data"] is data
            assert kwargs["closed"] is True and kwargs["filled"] is True
            assert kwargs["name"] == "polygon"
        else:
            assert args == () and "data" not in kwargs
        assert isinstance(owner, original) and type(owner) is probe
        assert owner.edge_width == 2 and owner.z_index == 9


def test_live_dictionary_cached_aliases_deletion_slots_and_fresh_cache_lookup(
    families, monkeypatch
):
    for family in families[0]:
        with family.pure(monkeypatch):
            owner = family.cls("Rectangle")(DATA["Rectangle"])
        old = vars(owner)
        cached_dims, cached_data = _warm(owner)
        replacement = old.copy()
        replacement["_dims_order"] = [1, 0]
        replacement["_data"] = owner.data.copy() + 10
        owner.__dict__ = replacement
        assert vars(owner) is replacement and old is not replacement
        assert owner.dims_order is replacement["_dims_order"]
        assert (
            owner.dims_displayed is cached_dims and owner.data_displayed is cached_data
        )
        owner._clean_cache()
        assert all(name not in replacement for name in CACHE_NAMES)
        assert all(name in old for name in CACHE_NAMES)
        assert owner.dims_displayed == [1, 0]
        np.testing.assert_array_equal(
            owner.data_displayed, replacement["_data"][:, [1, 0]]
        )
        del owner.__dict__
        assert vars(owner) == {} and vars(owner) is owner.__dict__
        with pytest.raises(AttributeError):
            _ = owner.data
        owner.__dict__ = old
        assert vars(owner) is old
        trace = []
        later = dict(old)

        class ReplacingDict(dict):
            def __contains__(self, key):
                trace.append(key)
                owner.__dict__ = later
                return True

        first = ReplacingDict(old)
        owner.__dict__ = first
        owner._clean_cache()
        assert trace == ["dims_displayed"]
        assert all(name in first for name in CACHE_NAMES)
        assert all(name not in later for name in CACHE_NAMES)
        owner.__dict__ = old

        class SlotRectangle(family.cls("Rectangle")):
            __slots__ = ("payload",)

        with family.pure(monkeypatch):
            raw = object.__new__(SlotRectangle)
            SlotRectangle.__init__(raw, DATA["Rectangle"])
        raw.payload = owner
        assert raw.payload is owner and vars(raw) is raw.__dict__
        assert weakref.ref(raw)() is raw
        del raw.payload
        assert not hasattr(raw, "payload")


def test_inherited_shape_dictionary_cycle_has_only_named_saved_alias_root(
    families, monkeypatch
):
    for family in families[0]:
        with family.pure(monkeypatch):
            owner = family.cls("Rectangle")(DATA["Rectangle"])
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


@pytest.mark.parametrize("method", ["copy", "deepcopy", "pickle"])
def test_warmed_shape_data_cache_aliases_and_native_storage_restore(
    families, monkeypatch, method
):
    for family in families[0]:
        with family.pure(monkeypatch):
            owner = family.cls("Rectangle")(DATA["Rectangle"])
            _warm(owner)
            owner.raw_alias, owner.display_alias = owner.data, owner.data_displayed
            expected = _snapshot(owner)
            if method == "copy":
                restored = copy.copy(owner)
            elif method == "deepcopy":
                restored = copy.deepcopy(owner)
            else:
                restored = pickle.loads(pickle.dumps(owner, protocol=4))
        assert type(restored) is type(owner) and vars(restored) is not vars(owner)
        assert restored.raw_alias is restored.data
        assert restored.display_alias is restored.data_displayed
        assert all(name in vars(restored) for name in CACHE_NAMES)
        _equal(expected, _snapshot(restored))
        if method == "copy":
            assert (
                restored.data is owner.data
                and restored.data_displayed is owner.data_displayed
            )
        else:
            assert restored.data is not owner.data
        restored._clean_cache()
        assert all(name not in vars(restored) for name in CACHE_NAMES)
        assert all(name in vars(owner) for name in CACHE_NAMES)


def test_base_constructor_partial_array_publication_before_failure(
    families, monkeypatch
):
    for family in families[0]:
        with family.pure(monkeypatch):
            cls = family.cls("Rectangle")
            owner = cls.__new__(cls)
        calls = []

        def empty(shape, *args, **kwargs):
            calls.append(shape)
            if len(calls) == 3:
                raise LookupError("third base allocation")
            return np.empty(shape, *args, **kwargs)

        with monkeypatch.context() as patch:
            patch.setitem(family.namespace, "np", NumpyProxy(empty=empty))
            with pytest.raises(LookupError, match="third base allocation"):
                family.cls("Shape").__init__(owner, ndisplay=3, dims_order=[2, 1, 0])
        assert calls == [(0, 3), (0, 3), (0, 3)]
        assert list(vars(owner)) == [
            "_dims_order",
            "_ndisplay",
            "_face_vertices",
            "_face_triangles",
        ]
        assert owner.dims_order == [2, 1, 0] and owner.ndisplay == 3
        assert owner._face_triangles.dtype == np.uint32
        assert not hasattr(owner, "_edge_vertices")


@pytest.mark.parametrize("name", ["Rectangle", "Ellipse", "Line", "PolygonBase"])
def test_data_validation_updates_dims_before_constructor_failure(
    families, monkeypatch, name
):
    for family in families[0]:
        cls = family.cls(name)
        invalid = np.zeros((1 if name == "PolygonBase" else 3, 3))
        with family.pure(monkeypatch):
            owner = cls.__new__(cls, invalid)
            with pytest.raises(ValueError):
                cls.__init__(owner, invalid)
        assert owner.dims_order == [0, 1, 2] and owner.ndisplay == 2
        assert not hasattr(owner, "_data")
        assert owner._bounding_box.shape == (0, 2)
        assert owner.name == ("polygon" if name == "PolygonBase" else "")
        assert owner._closed is (name != "Line")
        assert owner._filled is (name != "Line")
        if name == "Ellipse":
            assert owner._use_face_vertices is True


@pytest.mark.parametrize("field", ["ndisplay", "dims_order"])
def test_display_configuration_publishes_before_fresh_override_failure(
    families, monkeypatch, field
):
    for family in families[0]:
        with family.pure(monkeypatch):
            owner = family.cls("Rectangle")(DATA["Rectangle"])
        cached = _warm(owner)
        trace = []
        value = 3 if field == "ndisplay" else [1, 0]
        error = RuntimeError("display callback")

        def update():
            trace.append(getattr(owner, field))
            raise error

        owner._update_displayed_data = update
        with pytest.raises(RuntimeError) as caught:
            setattr(owner, field, value)
        assert caught.value is error and trace == [value]
        assert getattr(owner, field) == value
        assert all(
            vars(owner)[name] is value
            for name, value in zip(CACHE_NAMES, cached, strict=True)
        )


def test_real_six_concrete_geometry_readonly_input_and_counts(families, monkeypatch):
    expected = {}
    for family in families[0]:
        for name, data in DATA.items():
            readonly = np.array(data)
            readonly.flags.writeable = False
            saved = readonly.copy()
            with family.pure(monkeypatch):
                owner = family.cls(name)(readonly)
            np.testing.assert_array_equal(readonly, saved)
            assert not readonly.flags.writeable and owner.data.dtype == np.float32
            assert vars(owner) is owner.__dict__ and weakref.ref(owner)() is owner
            assert owner.ndisplay == 2 and owner.data_displayed.shape[1] == 2
            assert (
                owner.vertices_count
                == owner.face_vertices_count + owner.edge_vertices_count
            )
            assert (
                owner.triangles_count
                == owner.face_triangles_count + owner.edge_triangles_count
            )
            assert owner._all_triangles().shape == (owner.triangles_count, 3, 2)
            state = _snapshot(owner)
            if family.name == "fork":
                expected[name] = state
            else:
                _equal(expected[name], state)


def test_warmed_nd_shape_display_configuration_recomputes_exact_cache(
    families, monkeypatch
):
    expected = []
    data = np.array([[0, 5, 2, 2], [0, 5, 2, 8], [0, 5, 7, 8], [0, 5, 7, 2]])
    for family in families[0]:
        with family.pure(monkeypatch):
            owner = family.cls("Rectangle")(data, ndisplay=2)
            old_dims, old_data = _warm(owner)
            assert old_dims == [2, 3] and old_data.shape == (4, 2)
            owner.ndisplay = 3
            assert owner.dims_displayed == [1, 2, 3]
            assert owner.data_displayed.shape == (4, 3)
            owner.dims_order = [3, 2, 1, 0]
            assert owner.dims_displayed == [2, 1, 0]
            assert owner.dims_not_displayed == [3]
            assert owner.data_displayed is not old_data
            state = _snapshot(owner)
        if expected:
            _equal(expected[0], state)
        else:
            expected.append(state)


@pytest.mark.parametrize("closed", [False, True])
def test_real_scipy_spline_sampling_and_data_identity(families, monkeypatch, closed):
    expected = []
    data = np.array(DATA["PolygonBase"])
    for family in families[0]:
        with family.pure(monkeypatch):
            owner = family.cls("PolygonBase")(
                data,
                closed=closed,
                filled=closed,
                interpolation_order=3,
                interpolation_sampling=4,
            )
        np.testing.assert_array_equal(owner.data, data.astype(np.float32))
        assert owner.interpolation_order == 3 and owner.interpolation_sampling == 4
        assert owner._closed is closed and owner._filled is closed
        assert owner.edge_vertices_count > len(data)
        assert bool(owner.face_triangles_count) is closed
        state = _snapshot(owner)
        if expected:
            _equal(expected[0], state)
        else:
            expected.append(state)


@pytest.mark.parametrize(
    "operation",
    [
        "rectangle_transform",
        "ellipse_transform",
        "scale_center",
        "rotate_center",
        "flip_center",
    ],
)
def test_transform_cache_invalidation_and_original_flip_center_quirk(
    families, monkeypatch, operation
):
    expected = []
    for family in families[0]:
        kind = "Ellipse" if operation == "ellipse_transform" else "Rectangle"
        with family.pure(monkeypatch):
            owner = family.cls(kind)(DATA[kind])
            _dims, old_cache = _warm(owner)
            original = owner.data.copy()
            center = np.array([2, 3])
            if operation.endswith("transform"):
                matrix = np.array([[1, 0.2], [0, 0.8]])
                owner.transform(matrix)
                np.testing.assert_allclose(owner.data, original @ matrix.T, atol=1e-6)
            elif operation == "scale_center":
                owner.scale([2, 3], center=center)
                np.testing.assert_allclose(
                    owner.data, (original - center) * [2, 3] + center
                )
            elif operation == "rotate_center":
                owner.rotate(90, center=center)
                matrix = np.array([[0, 1], [-1, 0]])
                np.testing.assert_allclose(
                    owner.data, (original - center) @ matrix.T + center, atol=1e-6
                )
            else:
                owner.flip(0, center=center)
                # Original shifts -center twice; this deliberate behavior is fixed.
                np.testing.assert_allclose(
                    owner.data, (original - center) * [1, -1] - center
                )
            assert "data_displayed" not in vars(owner)
            assert owner.data_displayed is not old_cache
            state = _snapshot(owner)
        if expected:
            _equal(expected[0], state)
        else:
            expected.append(state)


@pytest.mark.parametrize("case", ["rectangle", "line", "ellipse", "embedded_rectangle"])
def test_real_shape_mask_policy_boolean_plane_and_embedding(
    families, monkeypatch, case
):
    expected = []
    kind = {
        "rectangle": "Rectangle",
        "line": "Line",
        "ellipse": "Ellipse",
        "embedded_rectangle": "Rectangle",
    }[case]
    for family in families[0]:
        data = DATA[kind]
        if case == "embedded_rectangle":
            data = [[0, 2, 2], [0, 2, 8], [0, 7, 8], [0, 7, 2]]
        with family.pure(monkeypatch):
            owner = family.cls(kind)(data)
            mask = owner.to_mask(
                (3, 12, 12) if case == "embedded_rectangle" else (12, 12)
            )
        assert mask.dtype == bool and mask.any()
        if case == "embedded_rectangle":
            assert mask.shape == (3, 12, 12) and mask[0].any() and not mask[1:].any()
        else:
            assert mask.shape == (12, 12)
        if expected:
            np.testing.assert_array_equal(mask, expected[0])
        else:
            expected.append(mask)


@pytest.mark.parametrize("backend", ["bermuda", "partsegcore"])
def test_optional_backend_failure_preserves_cause_save_and_field_publication(
    families, monkeypatch, backend
):
    for family in families[0]:
        with family.pure(monkeypatch):
            owner = family.cls("Rectangle")(DATA["Rectangle"])
        data = np.array([[1, 1], [1, 4], [4, 4]])
        error = KeyboardInterrupt("provider failure")
        before = dict(vars(owner))
        trace = []

        def failed(values, **kwargs):
            assert len(values) == 1 and values[0] is data
            trace.append(("provider", kwargs))
            raise error

        def save(value, *, backend):
            assert value is data
            trace.append(("save", backend))
            return "array.npy", "array.txt"

        provider = SimpleNamespace(
            **{
                "triangulate_polygons_with_edge"
                if backend == "bermuda"
                else "triangulate_polygon_with_edge_numpy_li": failed
            }
        )
        with monkeypatch.context() as patch:
            patch.setitem(
                family.namespace,
                "bermuda" if backend == "bermuda" else "partsegcore_triangulate",
                provider,
            )
            patch.setitem(family.namespace, "_save_failed_triangulation", save)
            method = (
                "_set_meshes_compiled_bermuda"
                if backend == "bermuda"
                else "_set_meshes_compiled_partseg"
            )
            with pytest.raises(
                RuntimeError, match="Data saved to array.npy and array.txt"
            ) as caught:
                getattr(owner, method)(data)
        assert caught.value.__cause__ is error
        assert trace == [
            ("provider", {} if backend == "bermuda" else {"split_edges": True}),
            ("save", backend),
        ]
        assert vars(owner).keys() == before.keys()
        assert all(vars(owner)[name] is value for name, value in before.items())


@pytest.mark.parametrize("backend", ["bermuda", "partsegcore"])
def test_optional_three_dimensional_dispatch_uses_fresh_owner_override(
    families, monkeypatch, backend
):
    for family in families[0]:
        with family.pure(monkeypatch):
            owner = family.cls("Rectangle")(DATA["Rectangle"])
        data = np.array([[0, 1, 1], [0, 1, 4], [0, 4, 4]])
        trace = []

        def target(value, **kwargs):
            trace.append((value is data, kwargs))

        target_name = (
            "_set_meshes_compiled_3d" if backend == "bermuda" else "_set_meshes_py"
        )
        setattr(owner, target_name, target)
        method = (
            "_set_meshes_compiled_bermuda"
            if backend == "bermuda"
            else "_set_meshes_compiled_partseg"
        )
        getattr(owner, method)(data, closed=False, face=False, edge=True)
        assert trace == [(True, {"closed": False, "face": False, "edge": True})]
