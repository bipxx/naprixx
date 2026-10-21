"""Focused actual-fork error contracts; root alone executes this packet.

Provider hooks are confined to the test. The function bodies, ordinary public
signatures, actual source cells and genuine generator objects remain real.
The empty-cell hook targets the current CPython 3.12 acceptance profile.
"""
from __future__ import annotations

import ctypes
import importlib
import inspect
import sys
import types
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]


@pytest.fixture
def shape_utils():
    module = importlib.import_module("napari.layers.shapes._shapes_utils")
    assert Path(module.__file__).resolve() == (
        ROOT / "src/napari/layers/shapes/_shapes_utils.py"
    ).resolve()
    return module


def _delete_actual_source_cell(frame, name):
    # Both the original function and its native facade own this genuine cell.
    # A fresh ctypes callable avoids changing pythonapi's cached signature.
    assert sys.implementation.name == "cpython"
    assert sys.version_info[:2] == (3, 12)
    assert name in frame.f_code.co_cellvars
    source_locals = frame.f_locals
    assert name in source_locals
    del source_locals[name]
    synchronize = ctypes.PYFUNCTYPE(None, ctypes.py_object, ctypes.c_int)(
        ("PyFrame_LocalsToFast", ctypes.pythonapi)
    )
    synchronize(frame, 1)
    assert name not in frame.f_locals


@pytest.mark.parametrize(
    ("function_name", "cell_name", "argument"),
    [
        ("is_collinear", "points", [[0, 0], [1, 0], [2, 0]]),
        ("get_default_shape_type", "first_type", ["polygon", "polygon"]),
    ],
)
def test_empty_actual_captured_cell_name_error(
    shape_utils, monkeypatch, function_name, cell_name, argument
):
    captured = []
    outside_exception = sys.exception()

    def consume(generator):
        assert isinstance(generator, types.GeneratorType)
        assert inspect.isgenerator(generator)
        captured.append(generator)
        caller = inspect.currentframe().f_back
        try:
            assert caller.f_code.co_name == function_name
            _delete_actual_source_cell(caller, cell_name)
        finally:
            del caller
        return next(generator)

    monkeypatch.setattr(shape_utils, "all", consume, raising=False)
    try:
        with pytest.raises(NameError) as failure:
            getattr(shape_utils, function_name)(argument)
        assert type(failure.value) is NameError
        assert failure.value.name == cell_name
        assert failure.value.args == (
            f"cannot access free variable '{cell_name}' where it is not "
            "associated with a value in enclosing scope",
        )
        assert len(captured) == 1
        assert captured[0].gi_frame is None
        with pytest.raises(StopIteration):
            captured[0].send(None)
        assert sys.exception() is outside_exception
    finally:
        if "failure" in locals():
            failure.value.__traceback__ = None
        captured.clear()


class _BodyFailure(Exception):
    pass


@pytest.mark.parametrize("failing_phase", ["binary", "text"])
@pytest.mark.parametrize("suppress", [False, True])
def test_failed_file_body_traceback_visible_inside_exit(
    shape_utils, monkeypatch, failing_phase, suppress
):
    body_failure = _BodyFailure(f"{failing_phase} body failure")
    outside_exception = sys.exception()
    payload = object()
    target_dir = "shape-geometry-provider-hook-only"
    events = []
    observed = []

    class FileContext:
        def __init__(self, phase):
            self.phase = phase
            self.handle = types.SimpleNamespace(
                name="binary.npz" if phase == "binary" else "text.txt"
            )

        def __enter__(self):
            events.append(("enter", self.phase))
            return self.handle

        def __exit__(self, exception_type, exception, traceback):
            events.append(("exit", self.phase))
            if exception is not None:
                frames = []
                cursor = traceback
                while cursor is not None:
                    frames.append(cursor.tb_frame.f_code.co_name)
                    cursor = cursor.tb_next
                observed.append(
                    {
                        "phase": self.phase,
                        "type": exception_type,
                        "exception": exception,
                        "traceback": traceback,
                        "same_exception_traceback": traceback
                        is exception.__traceback__,
                        "handled_exception": sys.exception(),
                        "frames": tuple(frames),
                    }
                )
                return suppress
            assert exception_type is None
            assert traceback is None
            return False

    def temporary_file(**kwargs):
        phase = "binary" if kwargs["suffix"] == ".npz" else "text"
        expected = {
            "delete": False,
            "suffix": ".npz" if phase == "binary" else ".txt",
            "prefix": "napari_geometry-test_triang_",
            "dir": target_dir,
        }
        if phase == "text":
            expected["mode"] = "w"
        assert kwargs == expected
        events.append(("factory", phase))
        return FileContext(phase)

    def provider_savez(handle, *, data):
        assert handle.name == "binary.npz"
        assert data is payload
        events.append(("body", "binary"))
        if failing_phase == "binary":
            raise body_failure

    def provider_savetxt(handle, data):
        assert handle.name == "text.txt"
        assert data is payload
        events.append(("body", "text"))
        if failing_phase == "text":
            raise body_failure

    monkeypatch.setattr(
        shape_utils,
        "tempfile",
        types.SimpleNamespace(NamedTemporaryFile=temporary_file),
    )
    monkeypatch.setattr(
        shape_utils,
        "np",
        types.SimpleNamespace(savez=provider_savez, savetxt=provider_savetxt),
    )
    try:
        if suppress:
            result = shape_utils._save_failed_triangulation(
                payload, target_dir=target_dir, backend="geometry-test"
            )
            assert result == ("binary.npz", "text.txt")
        else:
            with pytest.raises(_BodyFailure) as failure:
                shape_utils._save_failed_triangulation(
                    payload, target_dir=target_dir, backend="geometry-test"
                )
            assert failure.value is body_failure
            escaped_frames = []
            cursor = failure.value.__traceback__
            while cursor is not None:
                escaped_frames.append(cursor.tb_frame.f_code.co_name)
                cursor = cursor.tb_next
            assert escaped_frames.count("_save_failed_triangulation") == 1
        assert len(observed) == 1
        record = observed[0]
        assert record["phase"] == failing_phase
        assert record["type"] is _BodyFailure
        assert record["exception"] is body_failure
        assert record["same_exception_traceback"] is True
        assert record["handled_exception"] is body_failure
        provider_name = (
            "provider_savez" if failing_phase == "binary" else "provider_savetxt"
        )
        assert record["frames"] == ("_save_failed_triangulation", provider_name)
        binary_events = [
            ("factory", "binary"),
            ("enter", "binary"),
            ("body", "binary"),
            ("exit", "binary"),
        ]
        text_events = [
            ("factory", "text"),
            ("enter", "text"),
            ("body", "text"),
            ("exit", "text"),
        ]
        assert events == binary_events + (
            text_events if suppress or failing_phase == "text" else []
        )
        assert sys.exception() is outside_exception
    finally:
        body_failure.__traceback__ = None
        observed.clear()


class _ReplacementOrigin(Exception):
    pass


def _replacement_traceback():
    try:
        raise _ReplacementOrigin("controlled replacement traceback")
    except _ReplacementOrigin as exception:
        return exception.__traceback__


@pytest.mark.parametrize("failing_phase", ["binary", "text"])
@pytest.mark.parametrize("mutation", ["clear", "replace"])
def test_failed_file_body_keeps_exit_traceback_mutation_on_escape(
    shape_utils, monkeypatch, failing_phase, mutation
):
    # Source RERAISE uses the exception's actual post-exit traceback. The outer
    # caller adds its own frame; the with-body frame is not added again.
    body_failure = _BodyFailure(f"{failing_phase} failure with {mutation}")
    outside_exception = sys.exception()
    replacement = _replacement_traceback() if mutation == "replace" else None
    payload = object()
    observed = []
    events = []

    class FileContext:
        def __init__(self, phase):
            self.phase = phase
            self.handle = types.SimpleNamespace(name=f"{phase}-mutation-file")

        def __enter__(self):
            events.append(("enter", self.phase))
            return self.handle

        def __exit__(self, exception_type, exception, traceback):
            events.append(("exit", self.phase))
            if exception is not None:
                frames = []
                cursor = traceback
                while cursor is not None:
                    frames.append(cursor.tb_frame.f_code.co_name)
                    cursor = cursor.tb_next
                observed.append(
                    {
                        "phase": self.phase,
                        "type": exception_type,
                        "exception": exception,
                        "original_traceback": traceback,
                        "same_traceback_before_mutation": traceback
                        is exception.__traceback__,
                        "handled_exception": sys.exception(),
                        "original_frames": tuple(frames),
                    }
                )
                exception.__traceback__ = replacement
                observed[-1]["actual_traceback_after_mutation"] = (
                    exception.__traceback__
                )
            else:
                assert exception_type is None
                assert traceback is None
            return False

    def temporary_file(**kwargs):
        phase = "binary" if kwargs["suffix"] == ".npz" else "text"
        events.append(("factory", phase))
        return FileContext(phase)

    def provider_savez(handle, *, data):
        assert handle.name == "binary-mutation-file"
        assert data is payload
        events.append(("body", "binary"))
        if failing_phase == "binary":
            raise body_failure

    def provider_savetxt(handle, data):
        assert handle.name == "text-mutation-file"
        assert data is payload
        events.append(("body", "text"))
        if failing_phase == "text":
            raise body_failure

    monkeypatch.setattr(
        shape_utils,
        "tempfile",
        types.SimpleNamespace(NamedTemporaryFile=temporary_file),
    )
    monkeypatch.setattr(
        shape_utils,
        "np",
        types.SimpleNamespace(savez=provider_savez, savetxt=provider_savetxt),
    )
    try:
        with pytest.raises(_BodyFailure) as failure:
            shape_utils._save_failed_triangulation(payload, backend="mutation")
        assert failure.value is body_failure
        assert len(observed) == 1
        record = observed[0]
        assert record["phase"] == failing_phase
        assert record["type"] is _BodyFailure
        assert record["exception"] is body_failure
        assert record["same_traceback_before_mutation"] is True
        assert record["handled_exception"] is body_failure
        provider_name = (
            "provider_savez" if failing_phase == "binary" else "provider_savetxt"
        )
        assert record["original_frames"] == (
            "_save_failed_triangulation",
            provider_name,
        )
        assert record["actual_traceback_after_mutation"] is replacement
        retained_frames = []
        cursor = record["original_traceback"]
        while cursor is not None:
            retained_frames.append(cursor.tb_frame.f_code.co_name)
            cursor = cursor.tb_next
        assert tuple(retained_frames) == record["original_frames"]
        escaped = failure.value.__traceback__
        assert escaped.tb_frame.f_code.co_name == (
            "test_failed_file_body_keeps_exit_traceback_mutation_on_escape"
        )
        assert escaped.tb_next is replacement
        if mutation == "replace":
            assert replacement.tb_frame.f_code.co_name == "_replacement_traceback"
            assert replacement.tb_next is None
        else:
            assert replacement is None
        phases = ["binary"] + (["text"] if failing_phase == "text" else [])
        assert events == [
            (event, phase)
            for phase in phases
            for event in ("factory", "enter", "body", "exit")
        ]
        assert sys.exception() is outside_exception
    finally:
        body_failure.__traceback__ = None
        observed.clear()
