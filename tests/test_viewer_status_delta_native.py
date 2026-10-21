"""Only the three status contracts identified by the held source review.

Reuse the unchanged frozen/pinned/actual Family oracle. The parent fixture's
36 cases and assertions stay unchanged; root owns all collection and execution.
"""

# Each callback is consumed before advancing to the next source family.
# ruff: noqa: B023
from __future__ import annotations

import builtins
import hashlib
import importlib.util
import sys
from pathlib import Path
from types import SimpleNamespace

import pytest

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / ".artifacts/test_viewer_status_providers_next.py"
assert hashlib.sha256(FIXTURE.read_bytes()).hexdigest() == (
    "5264624201ab6cf4c9a581926420ff7ba4719fefe08647b24bed44f7eb40d905"
)
SPEC = importlib.util.spec_from_file_location(
    "_naparixx_viewer_status_delta_fixture", FIXTURE
)
assert SPEC is not None and SPEC.loader is not None
ORACLE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = ORACLE
SPEC.loader.exec_module(ORACLE)
families = ORACLE.families


@pytest.mark.parametrize("site", ["labels", "points", "shapes"])
def test_len_callable_captured_before_descriptor_or_index_rebinding(
    families, monkeypatch, site
):
    import numpy as np

    outcomes = []
    for family in families:
        trace, sequence, selected = [], [], [7]
        qualified = {
            "labels": "Labels._get_properties",
            "points": "Points._view_data",
            "shapes": "Shapes._get_value",
        }[site]
        namespace = family.namespace(qualified)

        def old_len(value):
            trace.append(("old", "selected" if value is selected else "argument"))
            return builtins.len(value)

        def new_len(value):
            trace.append(("new", "argument"))
            return builtins.len(value)

        class LabelsSubject:
            @property
            def _label_index(self):
                trace.append("descriptor")
                namespace["len"] = new_len
                return sequence

        class PointsSubject:
            _slice_input = SimpleNamespace(ndisplay=2)

            @property
            def _view_indices(self):
                trace.append("descriptor")
                namespace["len"] = new_len
                return sequence

        class Matches:
            def __getitem__(self, index):
                trace.append(("index", index))
                namespace["len"] = new_len
                return sequence

        class SelectedBox:
            def __getitem__(self, index):
                return np.zeros((1, 2))

        with monkeypatch.context() as patch:
            patch.setitem(namespace, "len", old_len)
            if site == "labels":
                result = family.function(qualified)(LabelsSubject(), (0, 0))
                expected = []
                expected_trace = ["descriptor", ("old", "argument")]
            elif site == "points":
                result = family.function(qualified)(PointsSubject())
                result = (result.shape, result.tolist())
                expected = ((0, 2), [])
                expected_trace = ["descriptor", ("old", "argument")]
            else:
                patch.setitem(
                    namespace,
                    "np",
                    SimpleNamespace(
                        all=lambda *args, **kwargs: SimpleNamespace(
                            nonzero=lambda: Matches()
                        )
                    ),
                )
                subject = SimpleNamespace(
                    _slice_input=SimpleNamespace(ndisplay=2, displayed=[0, 1]),
                    _is_moving=False,
                    _selected_data_in_view=selected,
                    scale=np.ones(2),
                    _mode=namespace["Mode"].SELECT,
                    _selected_box=SelectedBox(),
                    _normalized_vertex_radius=1,
                    _data_view=SimpleNamespace(inside=lambda coord: 9),
                )
                result = family.function(qualified)(subject, (0, 0))
                expected = (9, None)
                expected_trace = [
                    ("old", "selected"),
                    ("index", 0),
                    ("old", "argument"),
                ]
            assert namespace["len"] is new_len
        assert result == expected
        assert trace == expected_trace
        outcomes.append((result, trace))
    assert outcomes[0] == outcomes[1] == outcomes[2]


@pytest.mark.parametrize("owner", ["provider", "geometry", "adapter"])
@pytest.mark.parametrize("kind", ["noniterable", "custom_iterator_error"])
def test_unpack_noniterable_diagnostic_and_custom_iterator_error_identity(
    families, monkeypatch, owner, kind
):
    outcomes = []
    for family in families:
        trace = []
        marker = TypeError("custom iterator failure")

        class CustomIteratorError:
            def __iter__(self):
                trace.append("iter")
                raise marker

        value = 1 if kind == "noniterable" else CustomIteratorError()
        old = object()
        with monkeypatch.context() as patch:
            if owner == "provider":
                subject = SimpleNamespace(
                    visible=True,
                    ndim=3,
                    mode="edit",
                    _value=old,
                    _world_to_data_ray=lambda direction: (
                        trace.append("direction") or direction
                    ),
                    get_ray_intersections=lambda **kwargs: (
                        trace.append("ray") or value
                    ),
                )

                def invoke():
                    return family.function("Layer.get_value")(
                        subject,
                        (0, 0, 0),
                        view_direction=(0, 0, 1),
                        dims_displayed=[0, 1, 2],
                    )

                expected_trace = ["direction", "ray"]
            elif owner == "geometry":

                class BoundingBox:
                    def __getitem__(self, key):
                        trace.append(("box", key[0]))
                        return value

                def invoke():
                    return family.function("bounding_box_to_face_vertices")(
                        BoundingBox()
                    )

                expected_trace = [("box", -1)]
            else:
                namespace = family.namespace("projection_adapter")

                def original(*args):
                    trace.append("original")
                    return None

                adapted = family.function("projection_adapter")(original)
                patch.setitem(
                    namespace,
                    "map",
                    lambda *args: trace.append("map") or value,
                )

                def invoke():
                    return adapted((0, 0, 0), (0, 0, 0), (0, 0, 1))

                expected_trace = ["map"]
            with pytest.raises(TypeError) as caught:
                invoke()
            if owner == "provider":
                assert subject._value is old
        if kind == "noniterable":
            assert str(caught.value) == "cannot unpack non-iterable int object"
        else:
            assert caught.value is marker
            assert str(caught.value) == "custom iterator failure"
            expected_trace.append("iter")
        assert trace == expected_trace
        outcomes.append((str(caught.value), trace))
    assert outcomes[0] == outcomes[1] == outcomes[2]


def test_rgb_returned_exception_class_invalid_constructor_exact_diagnostic(
    families, monkeypatch
):
    outcomes = []
    for family in families:
        trace = []

        class BadError(Exception):
            def __new__(cls):
                trace.append(("construct", cls is BadError))
                return 1

        def factory(message):
            trace.append(("factory", message))
            return BadError

        namespace = family.namespace("rgb_to_luminance")
        with monkeypatch.context() as patch:
            patch.setitem(namespace, "ValueError", factory)
            with pytest.raises(TypeError) as caught:
                family.function("rgb_to_luminance")(SimpleNamespace(shape=(2,)))
        assert trace == [
            ("factory", "can only convert rgb or rgba"),
            ("construct", True),
        ]
        assert str(caught.value) == (
            f"calling {BadError!r} should have returned an instance of BaseException, "
            f"not {int!r}"
        )
        outcomes.append((str(caught.value), trace))
    assert outcomes[0] == outcomes[1] == outcomes[2]
