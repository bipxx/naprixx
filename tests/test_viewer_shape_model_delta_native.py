"""M1-M4 source-order regressions; root alone executes the held families."""

# Source callbacks are consumed before the next family iteration.
# ruff: noqa: B023
from __future__ import annotations

import builtins
import hashlib
import importlib.util
import sys
import weakref
from pathlib import Path
from types import ModuleType, SimpleNamespace

import pytest

ROOT = Path(__file__).resolve().parents[1]
MAIN = ROOT / ".artifacts/test_viewer_shape_model_next.py"
MAIN_SHA = "34f8c8b9ac775ebd6adb4504891b31c972c710efe783d261ad6d37010c33d7cf"
assert hashlib.sha256(MAIN.read_bytes()).hexdigest() == MAIN_SHA
_spec = importlib.util.spec_from_file_location(
    "_naparixx_shape_model_delta_oracle", MAIN
)
assert _spec is not None and _spec.loader is not None
ORACLE = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(ORACLE)
families = ORACLE.families


def _constant_tuple(code, target):
    for value in code.co_consts:
        if isinstance(value, tuple):
            if value == target:
                return value
            pending = [value]
            while pending:
                part = pending.pop()
                for item in part:
                    if isinstance(item, tuple):
                        if item == target:
                            return item
                        pending.append(item)
    raise AssertionError(f"source literal not found: {target!r}")


def _polygon(family, monkeypatch):
    with family.pure(monkeypatch):
        owner = family.cls("PolygonBase")(ORACLE.DATA["PolygonBase"])
    owner.interpolation_order = 3
    return owner, ORACLE._function(family.cls("PolygonBase"), "_update_displayed_data")


def test_m1_set_captures_both_backend_members_before_first_hash(families, monkeypatch):
    for family in families[0]:
        cls, namespace = family.cls("Shape"), family.namespace
        trace = []
        enum = SimpleNamespace()

        class Member:
            def __init__(self, label, number):
                self.label, self.number = label, number

            def __hash__(self):
                trace.append(self.label)
                if self.label == "bermuda":
                    enum.fastest_available = replacement
                return self.number

            def __eq__(self, other):
                return self is other

        first, selected, replacement = (
            Member("bermuda", 101),
            Member("fastest-original", 103),
            Member("fastest-replacement", 107),
        )
        enum.bermuda, enum.fastest_available = first, selected
        enum.partsegcore, enum.triangle = (
            Member("partseg", 109),
            Member("triangle", 113),
        )
        with monkeypatch.context() as patch:
            patch.setitem(namespace, "TriangulationBackend", enum)
            patch.setitem(namespace, "TRIANGULATION_BACKEND", selected)
            patch.setitem(namespace, "bermuda", SimpleNamespace())
            patch.setattr(cls, "_set_meshes", cls.__dict__["_set_meshes"])
            patch.setattr(cls, "_triangulate_edge", cls.__dict__["_triangulate_edge"])
            with pytest.raises(TypeError, match="abstract"):
                cls()
            assert trace == ["bermuda", "fastest-original", "fastest-original"]
            assert cls.__dict__["_set_meshes"] is cls._set_meshes_compiled_bermuda
            assert cls.__dict__["_triangulate_edge"] is cls._triangulate_edge_bermuda


@pytest.mark.parametrize("class_name", ["Rectangle", "Ellipse", "Line", "PolygonBase"])
def test_m2_comparison_operand_drops_before_decision_truth(
    families, monkeypatch, class_name
):
    for family in families[0]:
        with family.pure(monkeypatch):
            owner = family.cls(class_name)(ORACLE.DATA[class_name])
            replacement = owner.data.copy()
            trace, references = [], {}
            ndarray_calls = 0

            class Decision:
                def __bool__(self):
                    trace.append(("truth", references["length"]() is not None))
                    return False

            class Length:
                def __init__(self):
                    references["length"] = weakref.ref(self)

                def __eq__(self, other):
                    trace.append("compare")
                    return Decision()

                def __ne__(self, other):
                    trace.append("compare")
                    return Decision()

                def __lt__(self, other):
                    trace.append("compare")
                    return Decision()

                def __del__(self):
                    trace.append("length-drop")

            def source_len(value):
                nonlocal ndarray_calls
                if isinstance(value, ORACLE.np.ndarray):
                    ndarray_calls += 1
                    validation_call = 2 if class_name in {"Rectangle", "Ellipse"} else 1
                    if ndarray_calls == validation_call:
                        return Length()
                return builtins.len(value)

            with monkeypatch.context() as patch:
                patch.setitem(vars(family.modules[class_name]), "len", source_len)
                owner.data = replacement
            assert trace == ["compare", "length-drop", ("truth", False)]
            assert references["length"]() is None
            ORACLE.np.testing.assert_array_equal(owner.data, replacement)


@pytest.mark.parametrize("backend", ["bermuda", "partsegcore"])
def test_m2_exception_matcher_consumed_before_handler(families, monkeypatch, backend):
    for family in families[0]:
        with family.pure(monkeypatch):
            owner = family.cls("PolygonBase")(ORACLE.DATA["PolygonBase"])
        namespace = family.namespace
        trace, alive = [], {"matcher": True}
        failure = RuntimeError("provider failure")

        class Matcher(tuple):
            def __new__(cls):
                return tuple.__new__(cls, (RuntimeError,))

            def __del__(self):
                alive["matcher"] = False
                trace.append("matcher-drop")

        def provider(*args, **kwargs):
            trace.append("provider")
            raise failure

        def save(data, *, backend):
            trace.append("save-start")
            namespace["BaseException"] = builtins.BaseException
            trace.append(("save-after", alive["matcher"]))
            return "array.npy", "array.txt"

        with monkeypatch.context() as patch:
            patch.setitem(namespace, "BaseException", Matcher())
            patch.setitem(namespace, "_save_failed_triangulation", save)
            if backend == "bermuda":
                patch.setitem(
                    namespace,
                    "bermuda",
                    SimpleNamespace(triangulate_polygons_with_edge=provider),
                )
                method = owner._set_meshes_compiled_bermuda
            else:
                patch.setitem(
                    namespace,
                    "partsegcore_triangulate",
                    SimpleNamespace(triangulate_polygon_with_edge_numpy_li=provider),
                )
                method = owner._set_meshes_compiled_partseg
            with pytest.raises(RuntimeError) as caught:
                method(owner.data)
            assert (
                str(caught.value)
                == "Triangulation failed. Data saved to array.npy and array.txt"
            )
            assert caught.value.__cause__ is failure
            assert trace == [
                "provider",
                "save-start",
                "matcher-drop",
                ("save-after", False),
            ]


def test_m3_builtin_import_hook_literals_and_existing_locals(families, monkeypatch):
    interned_name = sys.intern("scipy.interpolate")
    for family in families[0]:
        owner, function = _polygon(family, monkeypatch)
        namespace, imports, trace = function.__globals__, [], []
        source_fromlist = _constant_tuple(function.__code__, ("splev", "splprep"))
        real_import = function.__builtins__["__import__"]
        later_failure = LookupError("after both imported bindings")
        imported = ModuleType("_naparixx_spline_import_witness")
        imported.splev, imported.splprep = object(), object()

        def hook(name, globals=None, locals=None, fromlist=(), level=0):
            if name == "scipy.interpolate":
                imports.append((name, fromlist, level))
                trace.append("import")
                assert name is interned_name
                assert globals is namespace
                assert locals is None
                assert fromlist is source_fromlist
                assert level == 0
                return imported
            return real_import(name, globals, locals, fromlist, level)

        def global_shadow(*args, **kwargs):
            raise AssertionError("IMPORT_NAME consulted the globals shadow")

        def after_import(*args, **kwargs):
            trace.append("after-import")
            raise later_failure

        with monkeypatch.context() as patch:
            patch.setitem(function.__builtins__, "__import__", hook)
            patch.setitem(namespace, "__import__", global_shadow)
            patch.setitem(namespace, "remove_path_duplicates", after_import)
            for _ in range(2):
                with pytest.raises(LookupError) as caught:
                    function(owner)
                assert caught.value is later_failure
        assert trace == ["import", "after-import", "import", "after-import"]
        assert imports[0][0] is imports[1][0]
        assert imports[0][1] is imports[1][1]


def test_m3_missing_builtin_import_is_import_error(families, monkeypatch):
    for family in families[0]:
        owner, function = _polygon(family, monkeypatch)
        namespace, trace = function.__globals__, []

        def global_shadow(*args, **kwargs):
            trace.append("global-shadow")
            raise AssertionError("source must not consult globals __import__")

        with monkeypatch.context() as patch:
            patch.setitem(namespace, "__import__", global_shadow)
            patch.delitem(function.__builtins__, "__import__")
            with pytest.raises(ImportError, match="^__import__ not found$"):
                function(owner)
        assert trace == []


def test_m3_missing_imported_member_without_filename_is_import_error(
    families, monkeypatch
):
    for family in families[0]:
        owner, function = _polygon(family, monkeypatch)
        real_import = function.__builtins__["__import__"]
        imported = ModuleType("_naparixx_missing_spline_provider")
        trace = []

        def hook(name, globals=None, locals=None, fromlist=(), level=0):
            if name == "scipy.interpolate":
                trace.append("import")
                return imported
            return real_import(name, globals, locals, fromlist, level)

        def after_import(*args, **kwargs):
            trace.append("after-import")
            raise AssertionError("missing splev must fail before downstream work")

        with monkeypatch.context() as patch:
            patch.setitem(function.__builtins__, "__import__", hook)
            patch.setitem(function.__globals__, "remove_path_duplicates", after_import)
            with pytest.raises(ImportError) as caught:
                function(owner)
        assert (
            str(caught.value)
            == "cannot import name 'splev' from '_naparixx_missing_spline_provider' "
            "(unknown location)"
        )
        assert caught.value.name == "_naparixx_missing_spline_provider"
        assert caught.value.path is None
        assert trace == ["import"]


@pytest.mark.parametrize("custom_iterator", [False, True])
def test_m4_extended_unpack_preserves_noniterable_and_custom_iterator_errors(
    families, monkeypatch, custom_iterator
):
    for family in families[0]:
        owner, function = _polygon(family, monkeypatch)
        trace = []
        real_import = function.__builtins__["__import__"]
        failure = LookupError("custom spline iteration")

        class NonIterable:
            pass

        class CustomIterable:
            def __iter__(self):
                trace.append("iterate")
                raise failure

        def splprep(*args, **kwargs):
            trace.append("splprep")
            assert kwargs == {"s": 0, "k": 3, "per": True}
            return CustomIterable() if custom_iterator else NonIterable()

        def splev(*args, **kwargs):
            raise AssertionError("splev must not run after unpack failure")

        imported = ModuleType("_naparixx_unpack_spline_provider")
        imported.splev, imported.splprep = splev, splprep

        def hook(name, globals=None, locals=None, fromlist=(), level=0):
            if name == "scipy.interpolate":
                trace.append("import")
                return imported
            return real_import(name, globals, locals, fromlist, level)

        with family.pure(monkeypatch), monkeypatch.context() as patch:
            patch.setitem(function.__builtins__, "__import__", hook)
            if custom_iterator:
                with pytest.raises(LookupError) as caught:
                    function(owner)
                assert caught.value is failure
                assert trace == ["import", "splprep", "iterate"]
            else:
                with pytest.raises(TypeError) as caught:
                    function(owner)
                assert (
                    str(caught.value) == "cannot unpack non-iterable NonIterable object"
                )
                assert trace == ["import", "splprep"]
