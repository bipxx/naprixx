"""Source-first import arguments and map operand cleanup for LL1/LL2."""

# Source-family callbacks complete before the family loop advances.
# ruff: noqa: B023
from __future__ import annotations

import builtins
import hashlib
import importlib.util
import sys
import weakref
from pathlib import Path
from types import SimpleNamespace

import pytest

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / ".artifacts/test_viewer_layer_lifecycle_next.py"
BASE_SHA = "678abee9915902e28406a93e412f21c706ab35e667f922171beacfd7c0b19cf8"
KEY_GLOBALS = ("Points", "Labels", "Shapes", "Vectors", "Image", "Surface", "Tracks")
VALUE_GLOBALS = (
    "points_fun_to_mode",
    "labels_fun_to_mode",
    "shapes_fun_to_mode",
    "vectors_fun_to_mode",
    "image_fun_to_mode",
    "surface_fun_to_mode",
    "tracks_fun_to_mode",
)


@pytest.fixture(scope="module")
def regression_families():
    assert hashlib.sha256(BASE.read_bytes()).hexdigest() == BASE_SHA
    name = "_naparixx_lifecycle_regression_base"
    assert name not in sys.modules
    spec = importlib.util.spec_from_file_location(name, BASE)
    assert spec is not None and spec.loader is not None
    base = importlib.util.module_from_spec(spec)
    sys.modules[name] = base
    generator = None
    try:
        spec.loader.exec_module(base)
        builder = base._original_module

        def isolated_builder(name, *args, **kwargs):
            return builder(name + "_review_regressions", *args, **kwargs)

        base._original_module = isolated_builder
        generator = base.families.__wrapped__()
        yield next(generator)
    finally:
        if generator is not None:
            generator.close()
        sys.modules.pop(name, None)


def _settings_again(function):
    return function(None)


def test_source_import_name_passes_existing_locals_or_none(
    regression_families, monkeypatch
):
    for family in regression_families:
        namespace = family.settings_namespace
        calls = []
        original_import = builtins.__import__

        def importer(name, globals=None, locals=None, fromlist=(), level=0):
            if name == "inspect":
                calls.append((name, globals is namespace, locals, fromlist, level))
            return original_import(name, globals, locals, fromlist, level)

        def global_shadow(*args, **kwargs):
            raise AssertionError("IMPORT_NAME must use the genuine builtin importer")

        with monkeypatch.context() as patch:
            patch.setitem(namespace, "__import__", global_shadow)
            patch.setattr(builtins, "__import__", importer)
            with pytest.raises(
                RuntimeError,
                match=(
                    "^The path can only be set once per session\\. "
                    "Settings called from _settings_again$"
                ),
            ):
                _settings_again(family.get_settings)
        assert calls == [("inspect", True, None, None, 0)]
        assert builtins.__import__ is original_import


@pytest.mark.parametrize("mode", ["map_success", "hash_error"])
def test_source_build_map_releases_captured_operands_in_reverse(
    regression_families, monkeypatch, mode
):
    for family in regression_families:
        namespace = family.viewer_namespace
        references = {}
        released = []
        inserted = []
        stopped = RuntimeError("map construction hash error")
        after_map = RuntimeError("completed map observed")

        class Value:
            def __init__(self, index):
                self.label = f"value{index}"
                references[self.label] = weakref.ref(self)

            def __del__(self):
                released.append(self.label)

        class Key:
            def __init__(self, index):
                self.index = index
                self.label = f"key{index}"
                references[self.label] = weakref.ref(self)

            def __hash__(self):
                index = self.index
                inserted.append(("hash", index))
                if index == 0:
                    # All fourteen operands have already been evaluated. Drop
                    # their source-global roots without keeping any new ones.
                    for name in (*KEY_GLOBALS, *VALUE_GLOBALS):
                        namespace[name] = object()
                if mode == "hash_error" and index == 2:
                    # The saved callback traceback must not itself own this key.
                    del self
                    raise stopped
                return 23

            def __eq__(self, other):
                inserted.append(("equal", self.index, other.index))
                return True

            def __del__(self):
                released.append(self.label)

        expected_success = []
        for index in range(6, 0, -1):
            expected_success += [f"key{index}", f"value{index - 1}"]

        def settings_after_map():
            # Duplicate equal keys leave the first key and last value in the
            # canonical source dict. Other captured operands must be consumed.
            assert released == expected_success
            assert references["key0"]() is not None
            assert references["value6"]() is not None
            raise after_map

        with monkeypatch.context() as patch:
            for index, (key, value) in enumerate(
                zip(KEY_GLOBALS, VALUE_GLOBALS, strict=True)
            ):
                patch.setitem(namespace, key, Key(index))
                patch.setitem(namespace, value, Value(index))
            patch.setitem(namespace, "get_settings", settings_after_map)
            saved = None
            try:
                family.methods["_layer_help_from_mode"](SimpleNamespace(help="old"))
            except RuntimeError as error:
                assert error is (stopped if mode == "hash_error" else after_map)
                saved = error
            else:
                pytest.fail("source map boundary did not raise its exact error")
            assert saved is not None and saved.__traceback__ is not None
            if mode == "map_success":
                expected_insertions = [("hash", 0)]
                for index in range(1, 7):
                    expected_insertions += [("hash", index), ("equal", 0, index)]
                assert inserted == expected_insertions
                assert released == expected_success
                assert all(
                    reference() is None
                    for label, reference in references.items()
                    if label not in ("key0", "value6")
                )
            else:
                assert inserted == [
                    ("hash", 0),
                    ("hash", 1),
                    ("equal", 0, 1),
                    ("hash", 2),
                ]
                assert released == [
                    label
                    for index in range(6, -1, -1)
                    for label in (f"value{index}", f"key{index}")
                ]
                assert all(reference() is None for reference in references.values())
            # This is the actual saved source error, not a private frame probe.
            # Removing its traceback releases the completed dict's two roots.
            saved.__traceback__ = None
            assert all(reference() is None for reference in references.values())
