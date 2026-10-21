"""Original-first TC1 comparison, TC2 raise-from and TC3 comprehension roots."""

# Every callback completes within one original/current family iteration.
# ruff: noqa: B023
from __future__ import annotations

import hashlib
import importlib.util
import sys
import warnings
import weakref
from pathlib import Path

import numpy as np
import pytest

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / ".artifacts/test_theme_color_publication_viewer_correction.py"
BASE_SHA = "57c61f4b4b192318acf87d903ca72b114269383137eb8f03f5ff946d80ac1e15"


@pytest.fixture(scope="module")
def regression_families():
    assert hashlib.sha256(BASE.read_bytes()).hexdigest() == BASE_SHA
    name = "_naparixx_theme_color_regression_base"
    assert name not in sys.modules
    spec = importlib.util.spec_from_file_location(name, BASE)
    assert spec is not None and spec.loader is not None
    base = importlib.util.module_from_spec(spec)
    sys.modules[name] = base
    generator = None
    try:
        spec.loader.exec_module(base)
        builder, module_type = base._original_module, base.ModuleType

        def isolated_builder(name, *args, **kwargs):
            return builder(name + "_review_regressions", *args, **kwargs)

        def isolated_module_type(name):
            # The core's one-method Vispy declaration bypasses its builder.
            # Preserve a genuine ModuleType and matching registration/cleanup.
            if name == "_naparixx_theme_color_vispy_original":
                name += "_review_regressions"
            return module_type(name)

        base._original_module = isolated_builder
        base.ModuleType = isolated_module_type
        generator = base.families.__wrapped__()
        yield next(generator)
    finally:
        if generator is not None:
            generator.close()
        sys.modules.pop(name, None)


def test_source_chained_width_is_consumed_before_final_truth(
    regression_families, monkeypatch
):
    for family in regression_families:
        trace = []
        references = {}
        output = object()

        class FinalTruth:
            def __bool__(self):
                trace.append("final_truth")
                assert references["width"]() is None
                return True

        class Width:
            def __init__(self):
                references["width"] = weakref.ref(self)

            def __ge__(self, value):
                trace.append(("first_compare", value))
                return True

            def __le__(self, value):
                trace.append(("final_compare", value))
                return FinalTruth()

            def __del__(self):
                trace.append("width_released")

        class Shape:
            def __getitem__(self, index):
                trace.append(("shape", index))
                if index == 1:
                    return Width()
                return 2 if index == 0 else 3

        class Colors:
            dtype = type("Dtype", (), {"kind": "f"})()
            ndim = 2
            shape = Shape()

            def __array_function__(self, function, types, args, kwargs):
                # Genuine NumPy's public override keeps the source-facing
                # object; no substituted native array engine is involved.
                assert function is np.atleast_2d
                assert args == (self,) and kwargs == {}
                return self

        colors = Colors()

        def convert(value):
            assert value is colors
            trace.append("convert")
            return output

        with monkeypatch.context() as patch:
            patch.setattr(
                family.conversion, "_convert_array_to_correct_format", convert
            )
            assert family.conversion._handle_array(colors) is output
        assert trace == [
            ("shape", -1),
            ("shape", 0),
            ("shape", 1),
            ("first_compare", 3),
            ("final_compare", 4),
            "width_released",
            "final_truth",
            "convert",
        ]
        assert references["width"]() is None


def test_source_raise_from_normalizes_returned_exception_class_before_cause(
    regression_families, monkeypatch
):
    for family in regression_families:
        trace = []
        cause = KeyError("original handler failed")

        class Normalized(RuntimeError):
            def __init__(self):
                trace.append("normalize")
                super().__init__("normalized exception class")

        class Constructed(ValueError):
            def __new__(cls, message):
                trace.append(("construct", message))
                return Normalized

        def handler(value):
            trace.append(("handler", value))
            raise cause

        with monkeypatch.context() as patch:
            patch.setitem(family.conversion.__dict__, "ValueError", Constructed)
            patch.setitem(family.conversion._color_switch, str, handler)
            with pytest.raises(Normalized) as caught:
                family.conversion._handle_str_list_like(["bad"])
        assert type(caught.value) is Normalized
        assert caught.value.args == ("normalized exception class",)
        assert caught.value.__cause__ is cause
        assert caught.value.__suppress_context__ is True
        assert trace == [
            ("handler", "bad"),
            ("construct", "Invalid color found: bad at index 0."),
            "normalize",
        ]


@pytest.mark.parametrize("mode", ["filtered_success", "filter_valueerror"])
def test_source_comprehension_releases_iterator_partial_list_then_current(
    regression_families, monkeypatch, mode
):
    observations = []
    for family in regression_families:
        released = []
        references = {}
        trace = []
        failure = ValueError("filter callback failed")
        root = np.array([[0.125, 0.25, 0.5, 1]], dtype=np.float32)
        expected = (
            ["iterator", "current"]
            if mode == "filtered_success"
            else ["iterator", "partial_item", "current"]
        )

        class Item:
            def __init__(self, index, label):
                self.index = index
                self.label = label
                references[label] = weakref.ref(self)

            def __del__(self):
                released.append(self.label)

        class Cursor:
            def __init__(self):
                self.turn = 0
                references["iterator"] = weakref.ref(self)

            def __iter__(self):
                return self

            def __next__(self):
                index = self.turn
                self.turn += 1
                if index == 0:
                    return Item(
                        0, "partial_item" if mode == "filter_valueerror" else "current"
                    )
                if index == 1 and mode == "filter_valueerror":
                    return Item(1, "current")
                raise StopIteration

            def __del__(self):
                released.append("iterator")

        class Iterable:
            def __iter__(self):
                trace.append("iter")
                return Cursor()

            def __len__(self):
                trace.append("length")
                # The source ValueError handler cannot run before the inline
                # comprehension has cleaned iterator, partial list and c.
                assert released == expected
                return 2

            def __array__(self, dtype=None, copy=None):
                trace.append("asarray")
                assert released == expected
                return root

        def is_instance(item, kind):
            assert kind is str
            index = item.index
            del item  # The saved filter traceback must not own current c.
            trace.append(("filter", index))
            if index == 0:
                return True
            raise failure

        with monkeypatch.context() as patch:
            if mode == "filter_valueerror":
                patch.setitem(family.conversion.__dict__, "isinstance", is_instance)
            with warnings.catch_warnings(record=True) as captured:
                warnings.simplefilter("always")
                output = family.conversion._handle_list_like(Iterable())
        assert released == expected
        assert all(reference() is None for reference in references.values())
        assert output.dtype == np.float32 and output.shape[1] == 4
        if mode == "filtered_success":
            assert output is root
            assert trace == ["iter", "asarray"]
            assert captured == []
        else:
            assert trace == ["iter", ("filter", 0), ("filter", 1), "length"]
            assert failure.__traceback__ is not None
            assert len(captured) == 1
        observations.append(
            (output.shape, output.tobytes(), [str(w.message) for w in captured])
        )
    assert observations[1] == observations[0]
