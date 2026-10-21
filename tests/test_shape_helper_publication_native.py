"""Paired contracts for Shape publication, sorting and real settings events."""

from __future__ import annotations

import sys
from types import SimpleNamespace

import numpy as np
import pytest

from napari.layers.shapes import _accelerated_triangulate_dispatch as dispatch
from napari.layers.shapes import _accelerated_triangulate_python as python_impl
from napari.layers.shapes._shapes_models import shape
from napari.settings import _experimental as experimental
from napari.utils import misc, triangulation_backend as backend


@pytest.mark.parametrize("values, expected", [([], []), ([2, 1, 1, 3], [1, 2, 0, 3]), ([4, 0, 2], [1, 2, 0])])
def test_argsort_stable_indices(values, expected):
    assert misc.argsort(values) == expected


def test_argsort_captures_key_after_length_and_preserves_callback_failure():
    trace = []
    failure = RuntimeError("getitem failure")

    class Values:
        def __len__(self):
            trace.append("len")
            return 3

        def __getattribute__(self, name):
            if name == "__getitem__":
                trace.append("key")
            return object.__getattribute__(self, name)

        def __getitem__(self, index):
            trace.append(index)
            if index == 2:
                raise failure
            return 1

    with pytest.raises(RuntimeError) as caught:
        misc.argsort(Values())
    assert caught.value is failure
    assert trace == ["len", "key", 0, 1, 2]


def test_dispatch_aliases_share_actual_members_and_leave_imported_aliases(monkeypatch):
    assert dispatch._accelerated_triangulate_numba is not None
    imported_alias = dispatch.is_convex
    names = dispatch.SWAPPABLE_NUMBA
    for name in names:
        monkeypatch.setattr(dispatch, name, getattr(dispatch, name))
    monkeypatch.setattr(dispatch, "USE_NUMBA_FOR_EDGE_TRIANGULATION", dispatch.USE_NUMBA_FOR_EDGE_TRIANGULATION)
    assert dispatch._set_numba(False) is None
    for name in names:
        assert getattr(dispatch, name) is getattr(python_impl, name + "_py")
    assert dispatch.USE_NUMBA_FOR_EDGE_TRIANGULATION is False
    assert imported_alias is dispatch._accelerated_triangulate_numba.is_convex
    assert dispatch._set_numba(True) is None
    for name in names:
        assert getattr(dispatch, name) is getattr(dispatch._accelerated_triangulate_numba, name)


def test_dispatch_source_rhs_precedes_each_shadowed_globals_call(monkeypatch):
    trace = []
    first, second = {}, {}
    members = {"is_convex": object(), "normalize_vertices_and_edges": object()}

    class Provider:
        def __getattribute__(self, name):
            trace.append(("get", name))
            return members[name]

    def source_globals():
        trace.append("globals")
        return first if len(trace) == 2 else second

    monkeypatch.setattr(dispatch, "_accelerated_triangulate_numba", Provider())
    monkeypatch.setattr(dispatch, "SWAPPABLE_NUMBA", tuple(members))
    monkeypatch.setattr(dispatch, "USE_NUMBA_FOR_EDGE_TRIANGULATION", False)
    monkeypatch.setattr(dispatch, "globals", source_globals, raising=False)
    assert dispatch._set_numba(True) is None
    assert trace == [("get", "is_convex"), "globals", ("get", "normalize_vertices_and_edges"), "globals"]
    assert first == {"is_convex": members["is_convex"]}
    assert second == {"normalize_vertices_and_edges": members["normalize_vertices_and_edges"]}
    assert dispatch.USE_NUMBA_FOR_EDGE_TRIANGULATION is True


def test_dispatch_false_value_and_warmup_preserve_canonical_object(monkeypatch):
    value = []
    for name in dispatch.SWAPPABLE_NUMBA:
        monkeypatch.setattr(dispatch, name, getattr(dispatch, name))
    monkeypatch.setattr(dispatch, "USE_NUMBA_FOR_EDGE_TRIANGULATION", True)
    monkeypatch.setattr(dispatch, "RUN_WARMUP", True)
    dispatch._set_numba(value)
    dispatch._set_warmup(value)
    assert dispatch.USE_NUMBA_FOR_EDGE_TRIANGULATION is value
    assert dispatch.RUN_WARMUP is value


def test_universal_warmup_marks_attempt_before_failure_and_does_not_retry(monkeypatch):
    failure = RuntimeError("warmup provider")
    trace = []

    def fail(data, closed):
        trace.append((data.dtype, data.flags.c_contiguous, closed, dispatch.UNIVERSAL_CACHE_WARMUP))
        raise failure

    monkeypatch.setattr(dispatch, "_accelerated_triangulate_numba", SimpleNamespace(remove_path_duplicates=fail))
    monkeypatch.setattr(dispatch, "UNIVERSAL_CACHE_WARMUP", False)
    with pytest.raises(RuntimeError) as caught:
        dispatch.warmup_universal_numba()
    assert caught.value is failure
    assert trace == [(np.dtype("float32"), True, False, True)]
    assert dispatch.UNIVERSAL_CACHE_WARMUP is True
    assert dispatch.warmup_universal_numba() is None
    assert len(trace) == 1


@pytest.mark.parametrize("run_warmup, expected_count, cache", [(False, 6, False), (True, 18, True)])
def test_warmup_policy_uses_actual_orders_and_one_canonical_flag(monkeypatch, run_warmup, expected_count, cache):
    trace = []

    def operation(name):
        def call(data, *args):
            trace.append((name, "F" if data.flags.f_contiguous else "C", data.dtype, args))
            if name == "normalize_vertices_and_edges":
                return data, np.array([[0, 1]], dtype=np.int64)
        return call

    names = ("remove_path_duplicates", "create_box_from_bounding", "generate_2D_edge_meshes", "is_convex", "normalize_vertices_and_edges", "reconstruct_polygons_from_edges")
    provider = SimpleNamespace(**{name: operation(name) for name in names})
    monkeypatch.setattr(dispatch, "_accelerated_triangulate_numba", provider)
    monkeypatch.setattr(dispatch, "RUN_WARMUP", run_warmup)
    monkeypatch.setattr(dispatch, "USE_NUMBA_FOR_EDGE_TRIANGULATION", False)
    monkeypatch.setattr(dispatch, "CACHE_WARMUP", False)
    monkeypatch.setattr(dispatch, "UNIVERSAL_CACHE_WARMUP", False)
    assert dispatch.warmup_numba_cache() is None
    assert len(trace) == expected_count
    assert dispatch.CACHE_WARMUP is cache
    assert dispatch.UNIVERSAL_CACHE_WARMUP is True
    assert {row[1] for row in trace} == {"C", "F"}
    assert all(row[2] == np.dtype("float32") for row in trace)
    before = len(trace)
    assert dispatch.warmup_numba_cache() is None
    assert len(trace) == before


@pytest.mark.parametrize("member", list(backend.TriangulationBackend))
def test_backend_setter_reads_and_updates_real_shape_authority(monkeypatch, member):
    assert "bermuda" in sys.modules
    assert "PartSegCore_compiled_backend" in sys.modules
    previous = shape.TRIANGULATION_BACKEND
    trace = []
    monkeypatch.setattr(shape, "TRIANGULATION_BACKEND", previous)
    monkeypatch.setattr(dispatch, "_set_numba", lambda value: trace.append(("numba", value, shape.TRIANGULATION_BACKEND)))
    monkeypatch.setattr(dispatch, "_set_warmup", lambda value: trace.append(("warmup", value, shape.TRIANGULATION_BACKEND)))
    assert backend.set_backend(member) is previous
    assert backend.get_backend() is member
    assert trace == [("numba", member is not backend.TriangulationBackend.pure_python, previous), ("warmup", member is backend.TriangulationBackend.numba, previous)]


def test_backend_callback_failure_preserves_previous_authority(monkeypatch):
    failure = RuntimeError("backend callback")
    previous = shape.TRIANGULATION_BACKEND
    monkeypatch.setattr(shape, "TRIANGULATION_BACKEND", previous)

    def fail(value):
        raise failure

    monkeypatch.setattr(dispatch, "_set_numba", fail)
    with pytest.raises(RuntimeError) as caught:
        backend.set_backend(backend.TriangulationBackend.pure_python)
    assert caught.value is failure
    assert shape.TRIANGULATION_BACKEND is previous


def test_backend_enum_metadata_and_missing_names():
    enum = backend.TriangulationBackend
    assert str(enum.numba) == "Numba"
    assert repr(enum.numba) == "numba"
    assert enum("PURE PYTHON") is enum.pure_python
    with pytest.raises(ValueError, match="'42' is not a valid TriangulationBackend"):
        enum(42)


def test_real_settings_events_connect_then_read_current_source(monkeypatch):
    trace = []
    monkeypatch.setattr(experimental, "set_triangulation_backend", lambda value: trace.append(("triangulation", value)))
    monkeypatch.setattr(experimental, "set_colormap_backend", lambda value: trace.append(("colormap", value)))
    settings = experimental.ExperimentalSettings(triangulation_backend=backend.TriangulationBackend.pure_python)
    assert settings._connect_events() is None
    assert trace == [("triangulation", backend.TriangulationBackend.pure_python), ("colormap", settings.colormap_backend)]
    trace.clear()
    settings.triangulation_backend = backend.TriangulationBackend.numba
    assert trace == [("triangulation", backend.TriangulationBackend.numba)]
    assert settings.events.triangulation_backend.source is settings
